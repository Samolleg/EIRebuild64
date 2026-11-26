// Live D3D12 memory by kind and creation site; see d3d12_memory.h.
#include "d3d12_memory.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <new>
#include <psapi.h>
#include <atomic>
#include <cstdio>
#include <deque>
#include <iterator>
#include <mutex>
#include <unordered_map>

namespace d3d12 {
namespace {

constexpr USHORT kFrames = 6;  // the backend method and five callers above it

const char* kind_name(MemoryKind kind) noexcept {
    static constexpr const char* kNames[] = {"texture", "texture_array", "storage_texture", "target", "depth", "buffer",
                                             "rt_structure", "rt_scratch", "texture_heap", "upload", "readback",
                                             "descriptors", "swap_chain", "other"};
    const auto index = static_cast<std::size_t>(kind);
    return index < std::size(kNames) ? kNames[index] : "?";
}

struct Site {
    MemoryKind kind = MemoryKind::other;
    USHORT frames = 0;
    void* stack[kFrames]{};
    std::atomic<std::int64_t> bytes{0}, count{0}, peak{0};
};

std::mutex g_lock;
std::deque<Site> g_sites;  // never shrinks: a few hundred creation sites at most
std::unordered_map<std::uint64_t, Site*> g_by_key;

// Held by the object as private data: the runtime releases it with the object.
class Tag final : public IUnknown {
public:
    Tag(Site* site, std::int64_t bytes) noexcept : site_(site), bytes_(bytes) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        if (id == __uuidof(IUnknown)) { *out = static_cast<IUnknown*>(this); AddRef(); return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --refs_;
        if (!left) {
            site_->bytes -= bytes_;
            --site_->count;
            delete this;
        }
        return left;
    }
private:
    std::atomic<ULONG> refs_{1};
    Site* site_;
    std::int64_t bytes_;
};

// {6c0f2b8e-3a41-4d5e-9b7a-2f8e1c4d6a90}
constexpr GUID kTagGuid = {0x6c0f2b8e, 0x3a41, 0x4d5e, {0x9b, 0x7a, 0x2f, 0x8e, 0x1c, 0x4d, 0x6a, 0x90}};

double mib(std::int64_t bytes) noexcept { return double(bytes) / 1048576.0; }

} // namespace

// Developer mode only (EI_RENDERER_DEVELOPER, as renderer_developer_mode):
// the accounting captures a stack per tracked resource and allocation.
bool memory_accounting() noexcept {
    static const bool on = [] {
        char value[8]{};
        const DWORD length = GetEnvironmentVariableA("EI_RENDERER_DEVELOPER", value, sizeof(value));
        return length > 0 && length < sizeof(value) && !(value[0] == '0' && value[1] == '\0');
    }();
    return on;
}

void track_memory(ID3D12Device* device, ID3D12Object* object, MemoryKind kind) noexcept try {
    if (!device || !object || !memory_accounting()) return;
    std::int64_t bytes = 0;
    ID3D12Resource* resource = nullptr;
    ID3D12Heap* heap = nullptr;
    ID3D12DescriptorHeap* descriptors = nullptr;
    if (SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&resource)))) {
        const D3D12_RESOURCE_DESC desc = resource->GetDesc();
        D3D12_HEAP_PROPERTIES properties{};
        D3D12_HEAP_FLAGS flags{};
        if (SUCCEEDED(resource->GetHeapProperties(&properties, &flags))) {
            if (properties.Type == D3D12_HEAP_TYPE_UPLOAD) kind = MemoryKind::upload;
            else if (properties.Type == D3D12_HEAP_TYPE_READBACK) kind = MemoryKind::readback;
        }
        const D3D12_RESOURCE_ALLOCATION_INFO info = device->GetResourceAllocationInfo(0, 1, &desc);
        bytes = info.SizeInBytes == UINT64_MAX ? std::int64_t(desc.Width) : std::int64_t(info.SizeInBytes);
        resource->Release();
    } else if (SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&heap)))) {
        const D3D12_HEAP_DESC desc = heap->GetDesc();
        bytes = std::int64_t(desc.SizeInBytes);
        if (desc.Properties.Type == D3D12_HEAP_TYPE_UPLOAD) kind = MemoryKind::upload;
        heap->Release();
    } else if (SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&descriptors)))) {
        const D3D12_DESCRIPTOR_HEAP_DESC desc = descriptors->GetDesc();
        bytes = std::int64_t(desc.NumDescriptors) * device->GetDescriptorHandleIncrementSize(desc.Type);
        descriptors->Release();
    }
    if (bytes <= 0) return;
    void* stack[kFrames]{};
    ULONG hash = 0;
    const USHORT frames = RtlCaptureStackBackTrace(1, kFrames, stack, &hash);
    const std::uint64_t key = (std::uint64_t(hash) << 8) | std::uint64_t(kind);
    Site* site = nullptr;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        auto found = g_by_key.find(key);
        if (found == g_by_key.end()) {
            Site& added = g_sites.emplace_back();
            added.kind = kind;
            added.frames = frames;
            std::copy(stack, stack + frames, added.stack);
            found = g_by_key.emplace(key, &added).first;
        }
        site = found->second;
    }
    Tag* tag = new Tag(site, bytes);
    site->bytes += bytes;
    ++site->count;
    if (FAILED(object->SetPrivateDataInterface(kTagGuid, tag))) {  // not held: not counted
        site->bytes -= bytes;
        --site->count;
    }
    tag->Release();  // the object's reference (if any) remains
    const std::int64_t now = site->bytes.load();
    std::int64_t peak = site->peak.load();
    while (now > peak && !site->peak.compare_exchange_weak(peak, now)) {}
} catch (...) {
}

std::vector<std::string> memory_report(std::uint64_t process_local_bytes) {
    struct Row { const Site* site; std::int64_t bytes, count, peak; };
    std::vector<Row> rows;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        for (const Site& site : g_sites) rows.push_back({&site, site.bytes.load(), site.count.load(), site.peak.load()});
    }
    std::int64_t kinds[std::size_t(MemoryKind::count)]{}, counts[std::size_t(MemoryKind::count)]{};
    std::int64_t local = 0, system = 0;
    for (const Row& row : rows) {
        kinds[std::size_t(row.site->kind)] += row.bytes;
        counts[std::size_t(row.site->kind)] += row.count;
        (row.site->kind == MemoryKind::upload || row.site->kind == MemoryKind::readback ? system : local) += row.bytes;
    }
    std::vector<std::string> lines;
    char text[1000];
    int n = std::snprintf(text, sizeof(text),
                          "tracked local %.0f MiB of process %.0f MiB (untracked %.0f: DLSS/FSR, pipelines, driver), upload+readback %.0f MiB; by kind:",
                          mib(local), mib(std::int64_t(process_local_bytes)), mib(std::int64_t(process_local_bytes) - local), mib(system));
    for (std::size_t k = 0; k < std::size_t(MemoryKind::count) && n > 0 && std::size_t(n) < sizeof(text); ++k)
        if (counts[k]) n += std::snprintf(text + n, sizeof(text) - std::size_t(n), " %s %.1f/%lld", kind_name(MemoryKind(k)), mib(kinds[k]), static_cast<long long>(counts[k]));
    lines.emplace_back(text);
    // The 30 largest sites by live bytes (and any whose peak was far above).
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.bytes > b.bytes; });
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::size_t shown = 0;
    for (const Row& row : rows) {
        if (shown >= 30 || row.bytes < (1 << 20)) break;
        ++shown;
        n = std::snprintf(text, sizeof(text), "%.1f MiB live, peak %.1f, %lld objects, %s, rva", mib(row.bytes), mib(row.peak),
                          static_cast<long long>(row.count), kind_name(row.site->kind));
        for (USHORT f = 0; f < row.site->frames && n > 0 && std::size_t(n) < sizeof(text); ++f) {
            const auto address = reinterpret_cast<std::uintptr_t>(row.site->stack[f]);
            HMODULE module = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(address), &module);
            if (reinterpret_cast<std::uintptr_t>(module) == base)
                n += std::snprintf(text + n, sizeof(text) - std::size_t(n), " %llx", static_cast<unsigned long long>(address - base));
            else
                n += std::snprintf(text + n, sizeof(text) - std::size_t(n), " ext");
        }
        lines.emplace_back(text);
    }
    return lines;
}

// ---- Host CPU memory by owner (#289) -----------------------------------
// Every allocation of kTrackedBytes or more through operator new is recorded
// with its creating stack in fixed tables (no allocation inside: the
// replacement operator new would recurse). host_memory_report attributes the
// process's resident pages (QueryWorkingSet) to loaded modules, write-
// combined memory, mapped sections, these allocation sites and the rest of
// private memory, and lists the sites by live and resident bytes.
namespace {
constexpr std::size_t kTrackedBytes = 64u << 10;
constexpr std::size_t kSlots = 1u << 18;      // live tracked allocations (open addressing)
constexpr std::size_t kHostSites = 4096;      // distinct creating stacks
struct HostSite { void* stack[kFrames]; USHORT frames; ULONG hash; std::int64_t bytes, count, peak; };
struct HostSlot { void* pointer; std::size_t bytes; std::uint32_t site; };
HostSlot* g_slots = nullptr;                  // VirtualAlloc'd on first use
HostSite* g_host_sites = nullptr;
std::size_t g_host_site_count = 0;
SRWLOCK g_host_lock = SRWLOCK_INIT;
bool g_host_tables_failed = false;

bool host_tables() noexcept {
    if (g_slots) return true;
    if (g_host_tables_failed) return false;
    g_slots = static_cast<HostSlot*>(VirtualAlloc(nullptr, sizeof(HostSlot) * kSlots, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    g_host_sites = static_cast<HostSite*>(VirtualAlloc(nullptr, sizeof(HostSite) * kHostSites, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!g_slots || !g_host_sites) g_host_tables_failed = true;
    return !g_host_tables_failed;
}
std::size_t slot_of(void* pointer) noexcept {
    return (reinterpret_cast<std::uintptr_t>(pointer) >> 4) * 0x9E3779B97F4A7C15ull >> (64 - 18);
}
void note_allocation(void* pointer, std::size_t bytes) noexcept {
    void* stack[kFrames]{};
    ULONG hash = 0;
    const USHORT frames = RtlCaptureStackBackTrace(2, kFrames, stack, &hash);
    AcquireSRWLockExclusive(&g_host_lock);
    if (host_tables()) {
        std::uint32_t site = UINT32_MAX;
        for (std::size_t s = 0; s < g_host_site_count; ++s)
            if (g_host_sites[s].hash == hash && g_host_sites[s].frames == frames) { site = std::uint32_t(s); break; }
        if (site == UINT32_MAX && g_host_site_count < kHostSites) {
            site = std::uint32_t(g_host_site_count++);
            HostSite& added = g_host_sites[site];
            std::memcpy(added.stack, stack, sizeof(stack)); added.frames = frames; added.hash = hash;
            added.bytes = added.count = added.peak = 0;
        }
        if (site != UINT32_MAX) {
            for (std::size_t probe = 0, at = slot_of(pointer); probe < kSlots; ++probe, at = (at + 1) & (kSlots - 1))
                if (!g_slots[at].pointer || g_slots[at].pointer == reinterpret_cast<void*>(1)) {
                    g_slots[at] = {pointer, bytes, site};
                    HostSite& counted = g_host_sites[site];
                    counted.bytes += std::int64_t(bytes); ++counted.count;
                    if (counted.bytes > counted.peak) counted.peak = counted.bytes;
                    break;
                }
        }
    }
    ReleaseSRWLockExclusive(&g_host_lock);
}
void note_free(void* pointer) noexcept {
    if (!g_slots) return;
    AcquireSRWLockExclusive(&g_host_lock);
    for (std::size_t probe = 0, at = slot_of(pointer); probe < kSlots; ++probe, at = (at + 1) & (kSlots - 1)) {
        if (!g_slots[at].pointer) break;
        if (g_slots[at].pointer == pointer) {
            HostSite& counted = g_host_sites[g_slots[at].site];
            counted.bytes -= std::int64_t(g_slots[at].bytes); --counted.count;
            g_slots[at].pointer = reinterpret_cast<void*>(1);  // tombstone
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_host_lock);
}
} // namespace

std::vector<std::string> host_memory_report() {
    std::vector<std::string> lines;
    // Resident pages (sorted page addresses) and the address space's regions.
    std::vector<std::uintptr_t> pages;
    {
        std::vector<ULONG_PTR> buffer(1u << 20);
        for (int attempt = 0; attempt < 4; ++attempt) {
            if (QueryWorkingSet(GetCurrentProcess(), buffer.data(), DWORD(buffer.size() * sizeof(ULONG_PTR)))) break;
            if (GetLastError() != ERROR_BAD_LENGTH) return lines;
            buffer.resize(std::size_t(buffer[0]) + (1u << 16));
        }
        const std::size_t count = std::size_t(buffer[0]);
        pages.reserve(count);
        for (std::size_t i = 1; i <= count && i < buffer.size(); ++i) pages.push_back(std::uintptr_t(buffer[i]) & ~std::uintptr_t(0xFFF));
        std::sort(pages.begin(), pages.end());
    }
    const auto resident = [&](std::uintptr_t from, std::uint64_t bytes) {
        const auto a = std::lower_bound(pages.begin(), pages.end(), from);
        const auto b = std::lower_bound(a, pages.end(), from + bytes);
        return std::uint64_t(b - a) * 4096u;
    };
    std::uint64_t ws_wc = 0, ws_mapped = 0, ws_private = 0, commit_private = 0;
    std::unordered_map<std::string, std::uint64_t> modules;  // image regions by file
    std::unordered_map<std::string, std::uint64_t> mapped, mapped_commit;  // mapped sections by file (resident, committed)
    MEMORY_BASIC_INFORMATION info{};
    std::uintptr_t at = 0;
    while (VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info)) == sizeof(info)) {
        const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if (info.State == MEM_COMMIT) {
            const std::uint64_t r = resident(base, info.RegionSize);
            if (info.Type == MEM_IMAGE) {
                char name[MAX_PATH]{};
                if (GetMappedFileNameA(GetCurrentProcess(), info.BaseAddress, name, MAX_PATH)) {
                    const char* leaf = std::strrchr(name, '\\');
                    modules[leaf ? leaf + 1 : name] += r;
                }
            } else if (info.Type == MEM_MAPPED) {
                ws_mapped += r;
                char name[MAX_PATH]{};
                const DWORD length = GetMappedFileNameA(GetCurrentProcess(), info.BaseAddress, name, MAX_PATH);
                const char* leaf = length ? std::strrchr(name, '\\') : nullptr;
                mapped[length ? (leaf ? leaf + 1 : name) : "(no file: pagefile-backed section)"] += r;
                mapped_commit[length ? (leaf ? leaf + 1 : name) : "(no file: pagefile-backed section)"] += info.RegionSize;
            }
            else {
                commit_private += info.RegionSize;
                if (info.Protect & PAGE_WRITECOMBINE) ws_wc += r; else ws_private += r;
            }
        }
        const auto next = base + info.RegionSize;
        if (next <= at) break;
        at = next;
    }
    // Tracked allocation sites: live and resident bytes.
    struct Row { std::uint32_t site; std::int64_t bytes, count, peak; std::uint64_t ws; };
    std::vector<Row> rows;
    std::uint64_t ws_tracked = 0, tracked = 0;
    AcquireSRWLockShared(&g_host_lock);
    if (g_slots) {
        for (std::size_t s = 0; s < g_host_site_count; ++s)
            rows.push_back({std::uint32_t(s), g_host_sites[s].bytes, g_host_sites[s].count, g_host_sites[s].peak, 0});
        for (std::size_t i = 0; i < kSlots; ++i) {
            const HostSlot& slot = g_slots[i];
            if (!slot.pointer || slot.pointer == reinterpret_cast<void*>(1)) continue;
            const std::uint64_t r = resident(reinterpret_cast<std::uintptr_t>(slot.pointer) & ~std::uintptr_t(0xFFF), slot.bytes);
            rows[slot.site].ws += r; ws_tracked += r; tracked += slot.bytes;
        }
    }
    ReleaseSRWLockShared(&g_host_lock);
    PROCESS_MEMORY_COUNTERS_EX counters{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    char text[1000];
    std::vector<std::pair<std::string, std::uint64_t>> by_module(modules.begin(), modules.end());
    std::sort(by_module.begin(), by_module.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::uint64_t ws_images = 0;
    for (const auto& m : by_module) ws_images += m.second;
    int n = std::snprintf(text, sizeof(text),
        "host working set %.0f MiB (private commit %.0f MiB): write-combined %.0f, other private %.0f (of it allocations >= 64 KiB %.0f MiB resident of %.0f live), mapped %.0f, images %.0f; images:",
        mib(std::int64_t(counters.WorkingSetSize)), mib(std::int64_t(counters.PrivateUsage)), mib(std::int64_t(ws_wc)),
        mib(std::int64_t(ws_private)), mib(std::int64_t(ws_tracked)), mib(std::int64_t(tracked)), mib(std::int64_t(ws_mapped)),
        mib(std::int64_t(ws_images)));
    for (std::size_t i = 0; i < by_module.size() && i < 10 && n > 0 && std::size_t(n) < sizeof(text) - 64; ++i)
        n += std::snprintf(text + n, sizeof(text) - std::size_t(n), " %.40s %.0f", by_module[i].first.c_str(), mib(std::int64_t(by_module[i].second)));
    lines.emplace_back(text);
    std::vector<std::pair<std::string, std::uint64_t>> by_mapping(mapped.begin(), mapped.end());
    std::sort(by_mapping.begin(), by_mapping.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    n = std::snprintf(text, sizeof(text), "host mapped sections by file (resident/committed MiB):");
    for (std::size_t i = 0; i < by_mapping.size() && i < 12 && n > 0 && std::size_t(n) < sizeof(text) - 80; ++i)
        n += std::snprintf(text + n, sizeof(text) - std::size_t(n), " %.48s %.0f/%.0f", by_mapping[i].first.c_str(),
                           mib(std::int64_t(by_mapping[i].second)), mib(std::int64_t(mapped_commit[by_mapping[i].first])));
    lines.emplace_back(text);
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.bytes > b.bytes; });
    const auto exe = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    for (std::size_t i = 0; i < rows.size() && i < 30 && rows[i].bytes >= (8 << 20); ++i) {
        const HostSite& site = g_host_sites[rows[i].site];
        n = std::snprintf(text, sizeof(text), "host alloc %.1f MiB live (%.1f resident), peak %.1f, %lld blocks, rva", mib(rows[i].bytes),
                          mib(std::int64_t(rows[i].ws)), mib(rows[i].peak), static_cast<long long>(rows[i].count));
        for (USHORT f = 0; f < site.frames && n > 0 && std::size_t(n) < sizeof(text) - 24; ++f) {
            const auto address = reinterpret_cast<std::uintptr_t>(site.stack[f]);
            HMODULE module = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(address), &module);
            n += reinterpret_cast<std::uintptr_t>(module) == exe ?
                std::snprintf(text + n, sizeof(text) - std::size_t(n), " %llx", static_cast<unsigned long long>(address - exe)) :
                std::snprintf(text + n, sizeof(text) - std::size_t(n), " ext");
        }
        lines.emplace_back(text);
    }
    return lines;
}

} // namespace d3d12

// The host's operator new/delete (#289 accounting): the CRT heap as before;
// blocks of kTrackedBytes or more are recorded with their creating stack.
// operator new itself is the shared one (Source/renderer_diagnostics.cpp):
// it reports blocks of 64 KiB or more through this hook.
namespace owned { extern void (*g_large_allocation_hook)(void*, std::size_t); }
// Developer mode only: a player's host installs no hook and checks no block size.
static const bool g_host_allocation_hook = d3d12::memory_accounting() && (owned::g_large_allocation_hook = &d3d12::note_allocation, true);
void* operator new[](std::size_t bytes) { return operator new(bytes); }
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
    void* pointer = std::malloc(bytes ? bytes : 1);
    if (pointer && g_host_allocation_hook && bytes >= d3d12::kTrackedBytes) d3d12::note_allocation(pointer, bytes);
    return pointer;
}
void* operator new[](std::size_t bytes, const std::nothrow_t& tag) noexcept { return operator new(bytes, tag); }
void operator delete(void* pointer) noexcept {
    if (!pointer) return;
    if (g_host_allocation_hook && _msize(pointer) >= d3d12::kTrackedBytes) d3d12::note_free(pointer);  // only blocks that can be tracked take the lock
    std::free(pointer);
}
void operator delete[](void* pointer) noexcept { operator delete(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { operator delete(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { operator delete(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { operator delete(pointer); }
