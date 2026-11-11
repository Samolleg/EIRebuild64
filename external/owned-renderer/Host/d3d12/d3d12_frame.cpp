// The D3D12 frame executor of the x64 host (renderer_backend=d3d12, DX12
// plan M1a): the shared Renderer's UI records, the F10 overlay and present
// on the native D3D12 core (d3d12_core.h and its neighbours), in the core-mode
// host's own child window. Source/d3d12_frame.h is the Renderer's view.
//
// One command list a frame, opened by the first thing the frame records and
// submitted at present: texture writes and draws stay in stream order, so a
// write after a draw never changes what that draw read. The back buffer is
// taken at the first draw and cleared then (the world marker colour when a
// world pass was omitted). Every texture lives in PIXEL_SHADER_RESOURCE
// between frames; a write moves it to COPY_DEST and the next read back.
//
// Threading: the replay thread only (the Renderer's), except the pipeline
// workers inside Pipelines.
#include "d3d12_core.h"
#include "d3d12_pipelines.h"
#include "d3d12_pacer.h"
#include "d3d12_fsr_fg.h"
#include "d3d12_presenter.h"
#include "d3d12_rings.h"
#include "../reflex.h"

#include "renderer.h"
#include "dxil_table.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <memory>
#include <tuple>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace remote { void host_log(const char* format, ...); }

namespace owned {

// Shader reads of textures and buffers use one combined state.
constexpr D3D12_RESOURCE_STATES kShaderRead =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
// A target's depth read by shaders (and still usable as a read-only depth).
constexpr D3D12_RESOURCE_STATES kDepthRead = kShaderRead | D3D12_RESOURCE_STATE_DEPTH_READ;

// EI_RENDERER_D3D12_HAZARDS: a released resource leaves the hazard checker (null when off).
static void (*hazard_forget)(ID3D12Resource*) = nullptr;
static void (*hazard_index_freed)(std::uint32_t, std::uint64_t) = nullptr;  // a bindless index returned (and its fence)

// External SurfaceImage snapshots may release their last reference after the
// embedded executor has retired. Serialize that release with executor teardown
// and detach the borrowed Core once its GPU work and presenter have drained.
// Resource use and ordinary release still follow the frame-thread contract.
struct D3D12ResourceOwner {
    explicit D3D12ResourceOwner(d3d12::Core* value) : core(value) {}
    std::recursive_mutex mutex;
    d3d12::Core* core;
};

struct D3D12Texture {
    std::shared_ptr<D3D12ResourceOwner> owner;
    d3d12::ComPtr<ID3D12Resource> resource;
    D3D12_CPU_DESCRIPTOR_HANDLE srv{};  // CPU staging view, copied into the ring per draw
    UINT width = 0, height = 0, mips = 1, slices = 1;
    std::uint32_t index = UINT32_MAX;  // bindless: its persistent heap index once a shader read it by index
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
    // An offscreen target (create_target): its colour view and its own depth.
    d3d12::ComPtr<ID3D12DescriptorHeap> rtv_heap, dsv_heap;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{}, dsv{};
    D3D12_CPU_DESCRIPTOR_HANDLE depth_srv{};  // the target's depth as R24_UNORM or R32_FLOAT (staging)
    d3d12::ComPtr<ID3D12Resource> depth;
    d3d12::ComPtr<ID3D12Heap> heap;  // a placed texture's heap (prepare_texture_batch), kept while it lives
    DXGI_FORMAT depth_format = DXGI_FORMAT_UNKNOWN;  // its depth view's format (none: no depth)
    D3D12_RESOURCE_STATES depth_state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    // A storage texture (create_storage_texture): its unordered view of mip 0,
    // and generate_mips' per-level views (srv, uav pairs, made once).
    D3D12_CPU_DESCRIPTOR_HANDLE uav{};
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> mip_views;
    UINT mip_views_slice = 0, mip_views_count = 1;  // the array slices mip_views were made for
    // set_unordered_level's views of levels 1.. of a 2D storage texture
    // (entry m - 1 is level m; made on first use).
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> level_uavs;
    bool depth_array = false;  // create_depth_array: resource is the depth, dsv the first slice view
    bool written = false;  // a dispatch wrote it as a UAV since its last barrier
    D3D12Texture() = default;
    D3D12Texture(const D3D12Texture&) = delete;
    D3D12Texture& operator=(const D3D12Texture&) = delete;
    // Released once the frame being recorded (the last that may read it) finished.
    ~D3D12Texture() {
        if (!owner) return;
        std::lock_guard<std::recursive_mutex> lock(owner->mutex);
        d3d12::Core* core = owner->core;
        // Detached resources retain their COM references until this destructor.
        // The old executor already waited for their GPU work before detaching.
        if (!core) {
            // A placed resource must be released before its backing heap.
            resource.Reset();
            depth.Reset();
            heap.Reset();
            return;
        }
        if (srv.ptr) core->staging().free(srv);
        if (depth_srv.ptr) core->staging().free(depth_srv);
        if (uav.ptr) core->staging().free(uav);
        for (const auto view : mip_views) core->staging().free(view);
        for (const auto view : level_uavs) core->staging().free(view);
        if (index != UINT32_MAX) core->views().free(index, core->recording_value());
        if (index != UINT32_MAX && hazard_index_freed) hazard_index_freed(index, core->recording_value());
        if (hazard_forget) { hazard_forget(resource.Get()); if (depth) hazard_forget(depth.Get()); }
        core->release_later(resource);
        if (depth) core->release_later(depth);
        if (heap) core->release_later(heap);  // after its placed resource
    }
};

struct D3D12Buffer {
    std::shared_ptr<D3D12ResourceOwner> owner;
    d3d12::ComPtr<ID3D12Resource> resource;
    D3D12_CPU_DESCRIPTOR_HANDLE srv{}, uav{};  // CPU staging views
    UINT64 bytes = 0;
    UINT stride = 0, flags = 0;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    bool written = false;  // a dispatch wrote it as a UAV since its last barrier
    D3D12_CPU_DESCRIPTOR_HANDLE clear_uav{};  // clear_buffer_uint's raw view (made on first use)
    D3D12Buffer() = default;
    D3D12Buffer(const D3D12Buffer&) = delete;
    D3D12Buffer& operator=(const D3D12Buffer&) = delete;
    ~D3D12Buffer() {
        if (!owner) return;
        std::lock_guard<std::recursive_mutex> lock(owner->mutex);
        d3d12::Core* core = owner->core;
        // Detached resources retain their COM references until this destructor.
        // The old executor already waited for their GPU work before detaching.
        if (!core) return;
        core->staging().free(srv);
        core->staging().free(uav);
        if (clear_uav.ptr) core->staging().free(clear_uav);
        if (hazard_forget) hazard_forget(resource.Get());
        core->release_later(resource);
    }
};

// A one-off upload a worker prepared (D3D12Preparer): nothing here has been
// recorded or seen by the GPU, so dropping it unfinished just releases it.
struct D3D12PreparedUpload {
    // Keep session identity even if a worker outlives executor teardown.
    std::shared_ptr<D3D12ResourceOwner> owner;
    d3d12::ComPtr<ID3D12Heap> heap;          // a batch's placed texture: the heap it lives in (released last)
    d3d12::ComPtr<ID3D12Resource> resource;  // the destination: a buffer (COMMON) or texture (COPY_DEST)
    d3d12::ComPtr<ID3D12Resource> upload;    // every byte, in the copy's layout (shared by a batch)
    bool texture = false;
    UINT64 bytes = 0;                        // a buffer's size (16-rounded)
    UINT width = 0, height = 0, mips = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> levels;  // a texture's levels in upload
};

} // namespace owned

namespace d3d12 {
namespace {

using owned::D3D12Texture;
using owned::D3D12UiProgram;
using owned::kShaderRead;
using owned::kDepthRead;

// A world pass omitted: a diagnostic colour, never a game colour (as the bring-up's).
constexpr float kWorldMarker[4] = {0.32f, 0.05f, 0.38f, 1.0f};
constexpr float kBlack[4] = {0.0f, 0.0f, 0.0f, 1.0f};

// Off-render-thread upload preparation (owned::D3D12Preparer): device
// creation, Map and memcpy only, all free-threaded; no descriptors, no
// command list, no executor state. finish_buffer / finish_texture adopt it.
class Preparer final : public owned::D3D12Preparer {
public:
    Preparer(ID3D12Device* device, std::shared_ptr<owned::D3D12ResourceOwner> owner)
        : device_(device), owner_(std::move(owner)) {}

    HRESULT prepare_buffer(UINT64 bytes, const void* data, owned::D3D12PreparedUploadRef& out) noexcept override try {
        out.reset();
        if (!data && bytes) return E_INVALIDARG;
        const UINT64 size = (std::max<UINT64>)(16, (bytes + 15) & ~UINT64(15));
        auto prepared = std::make_shared<owned::D3D12PreparedUpload>();
        prepared->owner = owner_;
        D3D12_RESOURCE_DESC desc = buffer_desc(size);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        HRESULT hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                      IID_PPV_ARGS(&prepared->resource));
        if (FAILED(hr)) return hr;
        std::uint8_t* cpu = nullptr;
        if (FAILED(hr = create_upload(size, prepared->upload, cpu))) return hr;
        if (bytes) std::memcpy(cpu, data, static_cast<std::size_t>(bytes));
        if (size > bytes) std::memset(cpu + bytes, 0, static_cast<std::size_t>(size - bytes));
        prepared->upload->Unmap(0, nullptr);
        prepared->bytes = size;
        out = std::move(prepared);
        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }

    HRESULT prepare_texture(UINT width, UINT height, DXGI_FORMAT format, UINT mips, const void* const* levels, const UINT* pitches,
                            owned::D3D12PreparedUploadRef& out) noexcept override try {
        out.reset();
        if (!width || !height || !mips || !levels || !pitches) return E_INVALIDARG;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = UINT16(mips);
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        auto prepared = std::make_shared<owned::D3D12PreparedUpload>();
        prepared->owner = owner_;
        prepared->levels.resize(mips);
        std::vector<UINT> rows(mips);
        std::vector<UINT64> row_bytes(mips);
        UINT64 total = 0;
        device_->GetCopyableFootprints(&desc, 0, mips, 0, prepared->levels.data(), rows.data(), row_bytes.data(), &total);
        for (UINT level = 0; level < mips; ++level)
            if (!levels[level] || pitches[level] < row_bytes[level]) return E_INVALIDARG;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        HRESULT hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                      IID_PPV_ARGS(&prepared->resource));
        if (FAILED(hr)) return hr;
        std::uint8_t* cpu = nullptr;
        if (FAILED(hr = create_upload(total, prepared->upload, cpu))) return hr;
        for (UINT level = 0; level < mips; ++level) {
            const auto& footprint = prepared->levels[level];
            const auto* source = static_cast<const std::uint8_t*>(levels[level]);
            for (UINT row = 0; row < rows[level]; ++row)
                std::memcpy(cpu + footprint.Offset + std::size_t(row) * footprint.Footprint.RowPitch, source + std::size_t(row) * pitches[level],
                            static_cast<std::size_t>(row_bytes[level]));
        }
        prepared->upload->Unmap(0, nullptr);
        prepared->texture = true;
        prepared->width = width;
        prepared->height = height;
        prepared->mips = mips;
        prepared->format = format;
        out = std::move(prepared);
        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }

    HRESULT prepare_texture_batch(const TextureLevels* textures, std::size_t count,
                                  std::vector<owned::D3D12PreparedUploadRef>& out) noexcept override try {
        out.assign(count, nullptr);
        if (!count) return S_OK;
        if (!textures) return E_INVALIDARG;
        // Layout: each texture's place in one heap and its levels in one upload buffer.
        struct Entry {
            D3D12_RESOURCE_DESC desc{};
            UINT64 heap_offset = 0;
            std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> levels;
            std::vector<UINT> rows;
            std::vector<UINT64> row_bytes;
            bool usable = false;
        };
        std::vector<Entry> entries(count);
        UINT64 heap_bytes = 0, heap_alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, upload_bytes = 0;
        for (std::size_t n = 0; n < count; ++n) {
            const TextureLevels& source = textures[n];
            Entry& entry = entries[n];
            if (!source.width || !source.height || !source.mips || !source.levels || !source.pitches) continue;
            D3D12_RESOURCE_DESC& desc = entry.desc;
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = source.width;
            desc.Height = source.height;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = UINT16(source.mips);
            desc.Format = source.format;
            desc.SampleDesc.Count = 1;
            const D3D12_RESOURCE_ALLOCATION_INFO info = device_->GetResourceAllocationInfo(0, 1, &desc);
            if (info.SizeInBytes == UINT64_MAX || !info.Alignment) continue;
            entry.levels.resize(source.mips);
            entry.rows.resize(source.mips);
            entry.row_bytes.resize(source.mips);
            const UINT64 base = (upload_bytes + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) & ~UINT64(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
            UINT64 total = 0;
            device_->GetCopyableFootprints(&desc, 0, source.mips, base, entry.levels.data(), entry.rows.data(), entry.row_bytes.data(), &total);
            bool fits = true;
            for (UINT level = 0; level < source.mips; ++level)
                fits = fits && source.levels[level] && source.pitches[level] >= entry.row_bytes[level];
            if (!fits) continue;
            heap_alignment = (std::max)(heap_alignment, info.Alignment);
            entry.heap_offset = (heap_bytes + info.Alignment - 1) & ~(info.Alignment - 1);
            heap_bytes = entry.heap_offset + info.SizeInBytes;
            upload_bytes = base + total;
            entry.usable = true;
        }
        if (!heap_bytes) return S_OK;
        D3D12_HEAP_DESC heap_desc{};
        heap_desc.SizeInBytes = heap_bytes;
        heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap_desc.Alignment = heap_alignment;
        heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
        ComPtr<ID3D12Heap> heap;
        HRESULT hr = device_->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap));
        if (FAILED(hr)) return hr;
        ComPtr<ID3D12Resource> upload;
        std::uint8_t* cpu = nullptr;
        if (FAILED(hr = create_upload(upload_bytes, upload, cpu))) return hr;
        std::vector<owned::D3D12PreparedUploadRef> prepared(count);
        for (std::size_t n = 0; n < count; ++n) {
            Entry& entry = entries[n];
            if (!entry.usable) continue;
            auto item = std::make_shared<owned::D3D12PreparedUpload>();
            item->owner = owner_;
            item->heap = heap;
            if (FAILED(device_->CreatePlacedResource(heap.Get(), entry.heap_offset, &entry.desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(&item->resource))))
                continue;
            item->upload = upload;
            item->texture = true;
            item->width = textures[n].width;
            item->height = textures[n].height;
            item->mips = textures[n].mips;
            item->format = textures[n].format;
            item->levels = entry.levels;
            prepared[n] = std::move(item);
        }
        // The rows into upload memory, split over threads (write-combined memcpy).
        const auto copy = [&](std::size_t first, std::size_t step) noexcept {
            for (std::size_t n = first; n < count; n += step) {
                if (!prepared[n]) continue;
                const Entry& entry = entries[n];
                for (UINT level = 0; level < textures[n].mips; ++level) {
                    const auto& footprint = entry.levels[level];
                    const auto* source = static_cast<const std::uint8_t*>(textures[n].levels[level]);
                    for (UINT row = 0; row < entry.rows[level]; ++row)
                        std::memcpy(cpu + footprint.Offset + std::size_t(row) * footprint.Footprint.RowPitch,
                                    source + std::size_t(row) * textures[n].pitches[level], static_cast<std::size_t>(entry.row_bytes[level]));
                }
            }
        };
        const std::size_t threads = (std::max<std::size_t>)(1, (std::min<std::size_t>)({count, std::size_t(8), std::size_t(std::thread::hardware_concurrency() / 2)}));
        std::vector<std::thread> workers;
        std::size_t started = 1;
        try { for (; started < threads; ++started) workers.emplace_back(copy, started, threads); } catch (...) {}
        copy(0, threads);
        for (std::size_t first = started; first < threads; ++first) copy(first, threads);
        for (auto& worker : workers) worker.join();
        upload->Unmap(0, nullptr);
        out = std::move(prepared);
        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }

private:
    static D3D12_RESOURCE_DESC buffer_desc(UINT64 bytes) noexcept {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return desc;
    }
    HRESULT create_upload(UINT64 bytes, ComPtr<ID3D12Resource>& upload, std::uint8_t*& cpu) noexcept {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        const D3D12_RESOURCE_DESC desc = buffer_desc((std::max<UINT64>)(bytes, 16));
        HRESULT hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                      IID_PPV_ARGS(&upload));
        if (FAILED(hr)) return hr;
        const D3D12_RANGE none{0, 0};  // the CPU never reads it
        void* mapped = nullptr;
        if (FAILED(hr = upload->Map(0, &none, &mapped))) return hr;
        cpu = static_cast<std::uint8_t*>(mapped);
        return S_OK;
    }
    ComPtr<ID3D12Device> device_;
    std::shared_ptr<owned::D3D12ResourceOwner> owner_;
};

D3D12_SHADER_BYTECODE resource_program(const char* name) {
    const HRSRC found = FindResourceA(nullptr, name, MAKEINTRESOURCEA(10));  // RT_RCDATA
    const HGLOBAL loaded = found ? LoadResource(nullptr, found) : nullptr;
    if (!loaded) return {};
    return {LockResource(loaded), SizeofResource(nullptr, found)};
}

// A program's identity across builds: its source name, entry, profile and
// defines, never its source text, so a recorded pipeline still finds its
// program after a shader edit (and is built with the edited program's DXIL).
std::uint64_t program_identity(const owned::ShaderProgram& program) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    const auto text = [&hash](const char* value) {
        for (; value && *value; ++value) hash = (hash ^ std::uint8_t(*value)) * 1099511628211ull;
        hash = (hash ^ 0xffu) * 1099511628211ull;  // separator
    };
    text(program.name);
    text(program.entry);
    text(program.profile);
    for (const D3D_SHADER_MACRO* define = program.defines; define && define->Name; ++define) {
        text(define->Name);
        text(define->Definition);
    }
    return hash ? hash : 1;
}

bool block_format(DXGI_FORMAT format, UINT& block_bytes) noexcept {
    block_bytes = format == DXGI_FORMAT_BC1_UNORM ? 8u : format == DXGI_FORMAT_BC2_UNORM || format == DXGI_FORMAT_BC3_UNORM ? 16u : 0u;
    return block_bytes != 0;
}

D3D12_BLEND_DESC blend_desc(const owned::FixedBlend& choice) {
    const D3D11_BLEND_DESC source = owned::fixed_blend_desc(choice);
    const auto& in = source.RenderTarget[0];
    D3D12_BLEND_DESC desc{};
    auto& out = desc.RenderTarget[0];
    out.BlendEnable = in.BlendEnable;
    out.SrcBlend = static_cast<D3D12_BLEND>(in.SrcBlend);
    out.DestBlend = static_cast<D3D12_BLEND>(in.DestBlend);
    out.BlendOp = static_cast<D3D12_BLEND_OP>(in.BlendOp);
    out.SrcBlendAlpha = static_cast<D3D12_BLEND>(in.SrcBlendAlpha);
    out.DestBlendAlpha = static_cast<D3D12_BLEND>(in.DestBlendAlpha);
    out.BlendOpAlpha = static_cast<D3D12_BLEND_OP>(in.BlendOpAlpha);
    out.LogicOp = D3D12_LOGIC_OP_NOOP;
    out.RenderTargetWriteMask = in.RenderTargetWriteMask;
    return desc;
}

D3D12_RASTERIZER_DESC raster_desc() {
    // The UI's fixed state: no culling, solid; the scissor is always on in D3D12.
    const D3D11_RASTERIZER_DESC source = owned::fixed_raster_desc(D3DCULL_NONE, false, true);
    D3D12_RASTERIZER_DESC desc{};
    desc.FillMode = static_cast<D3D12_FILL_MODE>(source.FillMode);
    desc.CullMode = static_cast<D3D12_CULL_MODE>(source.CullMode);
    desc.FrontCounterClockwise = source.FrontCounterClockwise;
    desc.DepthClipEnable = source.DepthClipEnable;
    return desc;
}

// Debug layer messages (diagnostic runs): warnings and errors to host.log,
// each distinct message text once when first seen; every repeat is counted
// and the counts are logged at a device removal (debug_message_summary).
std::mutex debug_message_lock;
std::map<std::string, std::uint64_t> debug_message_counts;
void __stdcall debug_message(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR text, void*) {
    if (severity > D3D12_MESSAGE_SEVERITY_WARNING) return;
    char key[64];
    std::snprintf(key, sizeof(key), "%s %d: ", severity <= D3D12_MESSAGE_SEVERITY_ERROR ? "ERROR" : "warning", int(id));
    std::string line;
    try { line = std::string(key) + (text ? text : ""); } catch (...) { return; }
    {
        std::lock_guard<std::mutex> guard(debug_message_lock);
        // Distinct texts past 400 are counted, not logged (a flood of per-draw variants).
        try { if (++debug_message_counts[line] > 1 || debug_message_counts.size() > 400) return; } catch (...) { return; }
    }
    for (std::size_t at = 0; at < line.size(); at += 900)
        remote::host_log("d3d12 debug%s %s", at ? " (cont.)" : "", line.substr(at, 900).c_str());
}
void debug_message_summary() {
    std::lock_guard<std::mutex> guard(debug_message_lock);
    remote::host_log("d3d12 debug: %zu distinct messages before the removal (the first 100 listed)", debug_message_counts.size());
    std::size_t listed = 0;
    for (const auto& entry : debug_message_counts)
        if (++listed <= 100)
        remote::host_log("d3d12 debug: x%llu %s", static_cast<unsigned long long>(entry.second), entry.first.substr(0, 300).c_str());
}

// EI_RENDERER_D3D12_HAZARDS=1 (diagnostic runs): every command the executor
// records names each resource it can touch (its targets, the slots its
// programs declare, bindless textures, indirect arguments and index buffer,
// copy and clear operands). This checker keeps each resource's state as the
// recorded barriers left it, and its last write, and logs each distinct
// fault once (with counts at a device removal and at session end):
//  - state: a resource used in a state other than its barriers left it in;
//  - UAV RAW / WAW / WAR: an unordered access after another command's
//    unordered write (or a write after its read) with no barrier between;
//  - untracked: a resource or a descriptor the executor never registered.
// Off (the default) it is never constructed: one null test per command.
class Hazards {
public:
    void created(ID3D12Resource* resource, D3D12_RESOURCE_STATES state, const std::string& what) {
        if (!resource) return;
        std::lock_guard<std::mutex> guard(lock_);
        Record& record = records_[resource];
        const bool bindless = record.bindless;
        record = {};
        record.bindless = bindless;
        record.state = state;
        record.name = what + " (made in '" + pass_ + "')";
    }
    // Owned elsewhere (the presenter's back buffers, the interface depth): known, states not checked here.
    void external(ID3D12Resource* resource, const char* what) {
        if (!resource) return;
        std::lock_guard<std::mutex> guard(lock_);
        auto found = records_.find(resource);
        if (found != records_.end()) return;
        Record& record = records_[resource];
        record.name = what;
        record.external = true;
    }
    void forget(ID3D12Resource* resource) {
        std::lock_guard<std::mutex> guard(lock_);
        auto found = records_.find(resource);
        if (found == records_.end()) return;
        if (found->second.bindless) bindless_.erase(std::remove(bindless_.begin(), bindless_.end(), resource), bindless_.end());
        records_.erase(found);
    }
    void bindless(ID3D12Resource* resource) {
        std::lock_guard<std::mutex> guard(lock_);
        auto found = records_.find(resource);
        if (found == records_.end() || found->second.bindless) return;
        found->second.bindless = true;
        bindless_.push_back(resource);
        bindless_dirty_ = true;
    }
    void frame(std::uint64_t serial) noexcept { frame_ = serial; }
    void pass(const char* name) noexcept { pass_ = name ? name : "?"; }
    const char* pass() const noexcept { return pass_; }
    void command(const char* kind) noexcept { ++command_; kind_ = kind; }
    void transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES after) {
        std::lock_guard<std::mutex> guard(lock_);
        auto found = records_.find(resource);
        if (found == records_.end()) return;  // its next access names it
        Record& record = found->second;
        record.state = after;
        record.write_open = record.read_open = false;
        if (record.bindless) bindless_dirty_ = true;
    }
    void uav_barrier(ID3D12Resource* resource) {
        std::lock_guard<std::mutex> guard(lock_);
        for (auto& entry : records_)
            if (!resource || entry.first == resource) entry.second.write_open = entry.second.read_open = false;
    }
    void access(ID3D12Resource* resource, D3D12_RESOURCE_STATES need, bool write, const char* how) {
        std::lock_guard<std::mutex> guard(lock_);
        access_locked(resource, need, write, how);
    }
    // A declared slot whose descriptor names no registered resource.
    void untracked_view(const char* how, const char* detail) {
        std::lock_guard<std::mutex> guard(lock_);
        report(std::string("untracked descriptor|") + how + "|" + kind_ + "|" + pass_,
               format("untracked descriptor: %s (%s) by %s in '%s' (frame %llu)", how, detail, kind_, pass_, frame_));
    }
    // A program that reads textures by bindless index can read any of them.
    void bindless_reads() {
        std::lock_guard<std::mutex> guard(lock_);
        if (!bindless_dirty_) return;  // nothing bindless changed since the last check
        bindless_dirty_ = false;
        for (ID3D12Resource* resource : bindless_) access_locked(resource, kShaderRead, false, "bindless index");
    }
    // ---- Frame lifetimes: memory the GPU may still read, handed out again ----
    // A ring allocation [start, start + bytes) (upload memory by GPU address,
    // ring descriptors by GPU handle) for the frame signalling `fence`: any
    // earlier allocation it overlaps must belong to a completed frame.
    enum Ring { kUploadRing, kViewRing, kSamplerRing, kRings };
    void taken(Ring which, std::uint64_t start, std::uint64_t bytes, std::uint64_t fence, std::uint64_t completed) {
        if (!bytes) return;
        std::lock_guard<std::mutex> guard(lock_);
        static const char* const names[kRings] = {"upload ring", "descriptor ring", "sampler ring"};
        const char* const ring = names[which];
        auto& spans = spans_[which];
        const std::uint64_t end = start + bytes;
        auto it = spans.upper_bound(start);
        if (it != spans.begin()) --it;
        while (it != spans.end() && it->first < end) {
            const Span& old = it->second;
            if (old.end <= start) { ++it; continue; }
            if (old.fence > completed)
                report(std::string("reuse in flight|") + ring + "|" + old.pass + "|" + pass_,
                       format("reuse in flight: %s range taken by '%s' (frame %llu, fence %llu) handed out again to '%s' (frame %llu, fence %llu); "
                              "GPU completed %llu",
                              ring, old.pass, old.frame, old.fence, pass_, frame_, fence, completed));
            it = spans.erase(it);
        }
        spans[start] = {end, fence, frame_, pass_, ring};
    }
    // A command about to read ring memory at address (a constant view, a
    // table): it must be an allocation of the frame being recorded; an older
    // one may already be handed out again (addresses outside the rings pass).
    void used(Ring which, const char* what, std::uint64_t address, std::uint64_t fence) {
        if (!address) return;
        std::lock_guard<std::mutex> guard(lock_);
        const auto& spans = spans_[which];
        auto it = spans.upper_bound(address);
        if (it == spans.begin()) return;
        --it;
        const Span& span = it->second;
        if (address >= span.end || span.fence == fence) return;
        report(std::string("stale ring memory|") + what + "|" + span.pass + "|" + kind_ + "|" + pass_,
               format("stale ring memory: %s (%s taken by '%s' in frame %llu, fence %llu) read by %s in '%s' (frame %llu, fence %llu)", what,
                      span.ring, span.pass, span.frame, span.fence, kind_, pass_, frame_, fence));
    }
    // GPU-visible memory the executor lets go of while a frame that uses it
    // (fence) may still run.
    void released(const char* what, std::uint64_t fence, std::uint64_t completed) {
        if (fence <= completed) return;
        std::lock_guard<std::mutex> guard(lock_);
        report(std::string("released in flight|") + what + "|" + pass_,
               format("released in flight: %s (frame fence %llu) let go in '%s' (frame %llu); GPU completed %llu", what, fence, pass_, frame_,
                      completed));
    }
    // Bindless indices: freed with the fence of the last frame that may read
    // them; handed out again only once that fence completed.
    void index_freed(std::uint32_t index, std::uint64_t fence) {
        std::lock_guard<std::mutex> guard(lock_);
        freed_indices_[index] = {fence, frame_, pass_};
        if (index / 32 < live_.size()) live_[index / 32] &= ~(1u << (index & 31));
    }
    // The live bindless indices as a bitmap (the GPU side's index check).
    std::uint32_t live_word(std::uint32_t word) {
        std::lock_guard<std::mutex> guard(lock_);
        return word < live_.size() ? live_[word] : 0u;
    }
    void live_words(std::uint32_t* out, std::uint32_t words) {
        std::lock_guard<std::mutex> guard(lock_);
        for (std::uint32_t word = 0; word < words; ++word) out[word] = word < live_.size() ? live_[word] : 0u;
    }
    void index_taken(std::uint32_t index, std::uint64_t completed) {
        std::lock_guard<std::mutex> guard(lock_);
        if (index / 32 >= live_.size()) live_.resize(index / 32 + 1, 0u);
        live_[index / 32] |= 1u << (index & 31);
        auto it = freed_indices_.find(index);
        if (it == freed_indices_.end()) return;
        if (it->second.fence > completed)
            report(std::string("bindless index reuse|") + it->second.pass + "|" + pass_,
                   format("bindless index reuse in flight: index %u freed in '%s' (frame %llu, fence %llu) taken again in '%s' (frame %llu); "
                          "GPU completed %llu",
                          index, it->second.pass, it->second.frame, it->second.fence, pass_, frame_, completed));
        freed_indices_.erase(it);
    }
    void summary(const char* why) {
        std::lock_guard<std::mutex> guard(lock_);
        remote::host_log("d3d12 hazards (%s): %zu distinct, %llu commands checked", why, seen_.size(), command_);
        std::size_t listed = 0;
        for (const auto& entry : seen_)
            if (++listed <= 100) remote::host_log("d3d12 hazards: x%llu %s", entry.second.first, entry.second.second.substr(0, 700).c_str());
    }

private:
    struct Record {
        std::string name;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        bool external = false, bindless = false;
        bool write_open = false, write_uav = false;  // written since its last barrier (and how)
        bool read_open = false;                       // read as a UAV since its last barrier
        std::uint64_t write_command = 0, read_command = 0, write_frame = 0;
        const char *write_kind = "", *write_pass = "", *read_kind = "", *read_pass = "";
    };
    static constexpr D3D12_RESOURCE_STATES kWrites =
        D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_DEPTH_WRITE |
        D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_STREAM_OUT | D3D12_RESOURCE_STATE_RESOLVE_DEST;
    static bool fits(D3D12_RESOURCE_STATES have, D3D12_RESOURCE_STATES need) noexcept {
        if (have == need) return true;
        return !(need & kWrites) && !(have & kWrites) && (have & need) == need;  // a read inside a combined read state
    }
    template <typename... Args>
    static std::string format(const char* pattern, Args... args) {
        char text[900];
        std::snprintf(text, sizeof(text), pattern, args...);
        return text;
    }
    void access_locked(ID3D12Resource* resource, D3D12_RESOURCE_STATES need, bool write, const char* how) {
        if (!resource) return;
        auto found = records_.find(resource);
        if (found == records_.end()) {
            report(std::string("untracked resource|") + how + "|" + kind_ + "|" + pass_,
                   format("untracked resource %p: %s (needs 0x%x%s) by %s in '%s' (frame %llu)", static_cast<void*>(resource), how,
                          unsigned(need), write ? ", write" : "", kind_, pass_, frame_));
            return;
        }
        Record& r = found->second;
        if (r.external) return;
        const std::string last = r.write_command ? format("; last write: %s in '%s' (frame %llu)%s", r.write_kind, r.write_pass,
                                                          r.write_frame, r.write_open ? ", no barrier since" : "")
                                                 : std::string();
        if (!fits(r.state, need))
            report("state|" + r.name + "|" + how + "|" + kind_ + "|" + pass_ + "|" + std::to_string(unsigned(r.state)) + "|" + std::to_string(unsigned(need)),
                   format("state: %s is 0x%x, %s needs 0x%x%s, by %s in '%s' (frame %llu)%s", r.name.c_str(), unsigned(r.state), how,
                          unsigned(need), write ? " (write)" : "", kind_, pass_, frame_, last.c_str()));
        if (need == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            if (r.write_open && r.write_uav && r.write_command != command_)
                report(std::string(write ? "UAV WAW|" : "UAV RAW|") + r.name + "|" + how + "|" + kind_ + "|" + pass_ + "|" + r.write_kind + "|" +
                           r.write_pass,
                       format("UAV %s: %s, %s by %s in '%s' (frame %llu) after an unbarriered UAV write by %s in '%s'", write ? "WAW" : "RAW",
                              r.name.c_str(), how, kind_, pass_, frame_, r.write_kind, r.write_pass));
            else if (write && r.read_open && r.read_command != command_)
                report("UAV WAR|" + r.name + "|" + how + "|" + kind_ + "|" + pass_ + "|" + r.read_kind + "|" + r.read_pass,
                       format("UAV WAR: %s, %s by %s in '%s' (frame %llu) after an unbarriered UAV read by %s in '%s'", r.name.c_str(), how,
                              kind_, pass_, frame_, r.read_kind, r.read_pass));
        }
        if (write) {
            r.write_open = true;
            r.write_uav = need == D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            r.write_command = command_;
            r.write_frame = frame_;
            r.write_kind = kind_;
            r.write_pass = pass_;
            if (r.bindless) bindless_dirty_ = true;
        } else if (need == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            r.read_open = true;
            r.read_command = command_;
            r.read_kind = kind_;
            r.read_pass = pass_;
        }
    }
    void report(const std::string& key, const std::string& line) {
        auto& entry = seen_[key];
        if (entry.first++) return;
        entry.second = line;
        if (seen_.size() <= 300) remote::host_log("d3d12 hazard: %s", line.c_str());
    }
    struct Span {
        std::uint64_t end, fence, frame;
        const char* pass;
        const char* ring;
    };
    struct Freed {
        std::uint64_t fence, frame;
        const char* pass;
    };
    std::map<std::uint64_t, Span> spans_[kRings];  // ring allocations by start (frame lifetimes)
    std::unordered_map<std::uint32_t, Freed> freed_indices_;
    std::vector<std::uint32_t> live_;  // live bindless indices, a bit each
    std::mutex lock_;
    std::unordered_map<ID3D12Resource*, Record> records_;
    std::vector<ID3D12Resource*> bindless_;
    bool bindless_dirty_ = false;
    std::map<std::string, std::pair<unsigned long long, std::string>> seen_;  // key: count, first line
    std::uint64_t frame_ = 0;
    unsigned long long command_ = 0;
    const char* pass_ = "frame start";
    const char* kind_ = "";
};
Hazards* g_hazards = nullptr;  // the executor's checker while EI_RENDERER_D3D12_HAZARDS is on

// A program's DXIL name in the host's table: with EI_RENDERER_D3D12_HAZARDS on,
// its hazards variant when the table has one (the native shader's programs,
// which carry the GPU report only there); else the production program.
std::string program_dxil_name(const owned::ShaderProgram& program) {
    if (g_hazards) {
        std::string hazards = owned::dxil_name(program, true);
        if (FindResourceA(nullptr, hazards.c_str(), MAKEINTRESOURCEA(10))) return hazards;  // RT_RCDATA
    }
    return owned::dxil_name(program);
}

class Executor final : public owned::D3D12Frame, public owned::OverlayRenderer, public owned::ProfileTimestamps {
    struct StampSlot;  // the timestamp slots (ProfileTimestamps, below)
    struct ProgramCode;  // a program's DXIL and declared resource kinds (below)

public:
    ~Executor() override {
        // Presenter teardown can retire textures on this same frame thread.
        std::lock_guard<std::recursive_mutex> lock(resource_owner_->mutex);
        if (core_.device()) {
            UINT count = 0;
            double milliseconds = 0.0;
            native_pipeline_stats(count, milliseconds);
            remote::host_log("d3d12: session end: %u native pipelines (%.1f ms to build in all), descriptor ring %u, bindless %u (%u in use), "
                             "%zu staging blocks",
                             count, milliseconds, core_.views().transient_capacity(), core_.views().persistent_capacity(),
                             core_.views().persistent_used(), core_.staging().blocks());
            remote::host_log("d3d12: session end: %u pipelines were first requested in play, not warmed at start (named above)", unwarmed_);
            pipelines_.wait_all();
            save_pipelines("at exit");
            // One line an entry: the log truncates long lines (the diag counts were lost).
            if (counted_.empty()) remote::host_log("d3d12: session counts: nothing drawn");
            for (const auto& entry : counted_) remote::host_log("d3d12: session count: %s x%llu", entry.first.c_str(), static_cast<unsigned long long>(entry.second));
            if (omitted_.empty()) remote::host_log("d3d12: session omitted: nothing");
            for (const auto& entry : omitted_) remote::host_log("d3d12: session omitted: %s x%llu", entry.first.c_str(), static_cast<unsigned long long>(entry.second));
            if (g_hazards) g_hazards->summary("session end");
            for (const auto& entry : gpu_report_seen_)
                remote::host_log("d3d12 gpu loop (session end): x%llu report %s (loop id:bound or 100:index, then :origin)", entry.second, entry.first.c_str());
            core_.wait_idle();
            presenter_.destroy(core_);  // drains the pacer first (before_resize)
            pacer_.stop();
#ifdef EID3D11_EMBEDDED
            reflex::retire_device(core_.device());
#endif
        }
        // Late snapshots must not call Core or a subsequent executor's hazards.
        resource_owner_->core = nullptr;
        owned::hazard_forget = nullptr;
        owned::hazard_index_freed = nullptr;
        g_hazards = nullptr;
    }

    bool create(HWND child, const std::wstring& directory, std::string& reason) {
        // EI_RENDERER_X64_D3D12_DEBUG=1 (diagnostic runs): the debug layer and
        // GPU-based validation, their messages in host.log.
        // EI_RENDERER_D3D12_VALIDATE=1 is the same switch; =2 the debug layer
        // without GPU-based validation (near normal speed: the CPU-side state,
        // format and binding checks).
        const auto env_value = [](const char* name) {
            char flag[4]{};
            return GetEnvironmentVariableA(name, flag, sizeof(flag)) ? flag[0] : '\0';
        };
        const char validate = env_value("EI_RENDERER_D3D12_VALIDATE");
        bool debug = env_value("EI_RENDERER_X64_D3D12_DEBUG") == '1' || validate == '1' || validate == '2';
        const bool gpu_validation = debug && validate != '2';
        if (debug) {
            // The debug layer ships as System32\d3d12SDKLayers.dll (Graphics Tools).
            HMODULE layers = LoadLibraryExW(L"d3d12SDKLayers.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!layers) {
                remote::host_log("d3d12: validation requested but d3d12SDKLayers.dll is not in System32 (error %lu); running without it",
                                 GetLastError());
                debug = false;
            } else FreeLibrary(layers);
        }
        if (!core_.create(debug, reason, gpu_validation)) return false;
        debug_ = debug;
        // EI_RENDERER_D3D12_HAZARDS=1: the hazard checker (Hazards, above).
        if (env_value("EI_RENDERER_D3D12_HAZARDS") == '1') {
            hazards_ = std::make_unique<Hazards>();
            g_hazards = hazards_.get();
            owned::hazard_forget = [](ID3D12Resource* resource) { if (g_hazards) g_hazards->forget(resource); };
            owned::hazard_index_freed = [](std::uint32_t index, std::uint64_t fence) { if (g_hazards) g_hazards->index_freed(index, fence); };
            remote::host_log("d3d12: hazard checker on (EI_RENDERER_D3D12_HAZARDS): each distinct hazard is logged once");
        }
        // Bindless index 0 is permanently a null 2D view, taken before any
        // texture: a texture reference of 0 (a draw without a D3D12 texture)
        // samples zeros, as an unbound D3D11 slot does, never the stale
        // descriptor of a freed texture that once held index 0 (the cave TDR,
        // named by EI_RENDERER_D3D12_HAZARDS: "bindless index 0 is not a live texture").
        if (core_.views().allocate() != kNullBindlessIndex) {
            reason = "bindless index 0 was taken before the permanent null view";
            return false;
        }
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC null_texture{};
            null_texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            null_texture.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            null_texture.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            null_texture.Texture2D.MipLevels = 1;
            core_.device()->CreateShaderResourceView(nullptr, &null_texture, core_.views().persistent_cpu(kNullBindlessIndex));
            core_.views().publish(core_.device(), kNullBindlessIndex);
        }
        if (debug) {
            ComPtr<ID3D12InfoQueue1> queue;
            DWORD cookie = 0;
            const bool logged = SUCCEEDED(core_.device()->QueryInterface(IID_PPV_ARGS(&queue))) &&
                                SUCCEEDED(queue->RegisterMessageCallback(&debug_message, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie));
            remote::host_log("d3d12: debug layer on, GPU-based validation %s%s", gpu_validation ? "on" : "off",
                             logged ? " (messages below)" : " (no message callback: see a debugger)");
        }
        char adapter[256]{};
        WideCharToMultiByte(CP_UTF8, 0, core_.adapter_name().c_str(), -1, adapter, sizeof(adapter) - 1, nullptr, nullptr);
        remote::host_log("d3d12: device %s (SM 6.6, binding tier 3); %s", adapter, core_.heap_cost().c_str());
        if (!upload_.create(core_.device(), reason)) return false;
        if (!bulk_.create(core_.device(), reason, L"eid3d11 bulk upload block")) return false;
        try { preparer_ = std::make_shared<Preparer>(core_.device(), resource_owner_); } catch (...) { reason = "the upload preparer could not be created"; return false; }
#ifdef EID3D11_EMBEDDED
        // The embedded caller passes the native top-level game window, not
        // a Host child. It remains owned by the engine (no message thread).
        if (!presenter_.create(core_, child, reason, child)) return false;
#else
        if (!presenter_.create(core_, GetParent(child), reason, child)) return false;
#endif
        presenter_.set_before_resize([](void* self) {
            // The pacer's presents finish; FSR's context goes before the proxy
            // resizes or is destroyed (its display size follows the swap chain).
            Executor& executor = *static_cast<Executor*>(self);
            executor.pacer_.drain();
            executor.fsr_.destroy(executor.presenter_.swap_chain());
        }, this);
        if (!create_native_objects(reason)) return false;
        std::string note;
        const unsigned workers = (std::max)(1u, std::thread::hardware_concurrency() / 2);
        if (!pipelines_.open(core_.device(), directory + L"eid3d11_d3d12_pipelines.bin", workers, note, reason)) return false;
        if (!note.empty()) remote::host_log("d3d12: %s", note.c_str());
        // Build-ahead cost of this backend's whole pipeline set: the fixed
        // ones and every pipeline earlier sessions recorded (loaded from the
        // library, or created when the driver or a program changed), waited
        // for here so their first use draws.
        LARGE_INTEGER frequency{}, begin{}, end{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&begin);
        if (!request_programs(reason)) return false;
        std::uint32_t recorded = 0, dropped = 0, shipped = 0;
        request_recorded(directory, recorded, dropped, shipped);
        pipelines_.wait_all();
        warmed_ = true;
        QueryPerformanceCounter(&end);
        remote::host_log("d3d12: %u pipelines ready in %.1f ms on %u workers (%u loaded from the library, %u created; "
                         "%u from the recorded lists (%u shipped), %u records dropped: program gone or unused for %u starts)",
                         pipelines_.loaded() + pipelines_.created(), double(end.QuadPart - begin.QuadPart) * 1000.0 / double(frequency.QuadPart),
                         workers, pipelines_.loaded(), pipelines_.created(), recorded, shipped, dropped, kRecordIdleStarts);
        save_pipelines("at start");
        remote::host_log("d3d12: presenting %ux%u in the host child window", presenter_.width(), presenter_.height());
        // The GPU report buffer every native program binds (u0 space3).
        if (!create_gpu_reports()) {
            reason = "the GPU report buffer (u0 space3) could not be created";
            return false;
        }
        return true;
    }

    const std::string& failure() const noexcept { return failure_; }

    // ---- D3D12Frame ------------------------------------------------------------
    HRESULT create_texture(UINT width, UINT height, DXGI_FORMAT format, owned::D3D12TextureRef& out, UINT mips = 1) override {
        out.reset();
        if (!failure_.empty()) return E_FAIL;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = UINT16((std::max)(1u, mips));
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        auto texture = std::make_shared<D3D12Texture>();
        HRESULT hr = core_.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                             IID_PPV_ARGS(&texture->resource));
        if (FAILED(hr)) {
            report_once(texture_failures_, "texture creation", hresult_text("CreateCommittedResource (UI texture)", hr));
            return hr;
        }
        texture->owner = resource_owner_;
        texture->width = width;
        texture->height = height;
        texture->mips = desc.MipLevels;
        texture->format = format;
        texture->srv = core_.staging().allocate();
        if (!texture->srv.ptr) {
            report_once(texture_failures_, "texture creation", "no staging descriptor block");
            return E_OUTOFMEMORY;
        }
        core_.device()->CreateShaderResourceView(texture->resource.Get(), nullptr, texture->srv);
        hazard_created(*texture, "texture");
        out = std::move(texture);
        return S_OK;
    }

    bool texture_info(const D3D12Texture& texture, UINT& width, UINT& height, UINT& mips, DXGI_FORMAT& format) const noexcept override {
        // A surviving SurfaceImage may belong to an earlier executor even
        // when its dimensions and generation match the new device's request.
        width = height = mips = 0;
        format = DXGI_FORMAT_UNKNOWN;
        if (texture.owner != resource_owner_ || !texture.resource) return false;
        width = texture.width;
        height = texture.height;
        mips = texture.mips;
        format = texture.format;
        return true;
    }

    HRESULT create_texture_array(UINT width, UINT height, DXGI_FORMAT format, UINT mips, UINT slices, owned::D3D12TextureRef& out) override {
        out.reset();
        if (!failure_.empty()) return E_FAIL;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = UINT16((std::max)(1u, slices));
        desc.MipLevels = UINT16((std::max)(1u, mips));
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        auto texture = std::make_shared<D3D12Texture>();
        HRESULT hr = core_.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                             IID_PPV_ARGS(&texture->resource));
        if (FAILED(hr)) {
            report_once(texture_failures_, "texture creation", hresult_text("CreateCommittedResource (texture array)", hr));
            return hr;
        }
        texture->owner = resource_owner_;
        texture->width = width;
        texture->height = height;
        texture->mips = desc.MipLevels;
        texture->format = format;
        texture->srv = core_.staging().allocate();
        if (!texture->srv.ptr) {
            report_once(texture_failures_, "texture creation", "no staging descriptor block");
            return E_OUTOFMEMORY;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2DArray.MipLevels = desc.MipLevels;
        view.Texture2DArray.ArraySize = desc.DepthOrArraySize;
        texture->slices = desc.DepthOrArraySize;
        core_.device()->CreateShaderResourceView(texture->resource.Get(), &view, texture->srv);
        hazard_created(*texture, "texture array");
        out = std::move(texture);
        return S_OK;
    }

    HRESULT create_storage_texture(UINT width, UINT height, UINT depth, DXGI_FORMAT format, owned::D3D12TextureRef& out,
                                   UINT mips = 1, UINT slices = 1, bool not_zeroed = false) override {
        out.reset();
        if (!failure_.empty()) return E_FAIL;
        const bool volume = depth > 0, array = !volume && slices > 1;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = volume ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = UINT16(volume ? depth : array ? slices : 1);
        desc.MipLevels = UINT16((std::max)(1u, mips));
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        auto texture = std::make_shared<D3D12Texture>();
        HRESULT hr = E_FAIL;
        if (not_zeroed)  // an older runtime may refuse the flag: then cleared, as any other
            hr = core_.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_CREATE_NOT_ZEROED, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                         nullptr, IID_PPV_ARGS(&texture->resource));
        if (FAILED(hr)) hr = core_.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                             nullptr, IID_PPV_ARGS(&texture->resource));
        if (FAILED(hr)) {
            report_once(texture_failures_, "texture creation", hresult_text("CreateCommittedResource (storage texture)", hr));
            return hr;
        }
        texture->owner = resource_owner_;
        texture->state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        texture->width = width;
        texture->height = height;
        texture->mips = desc.MipLevels;
        texture->slices = array ? slices : 1;
        texture->format = format;
        texture->srv = core_.staging().allocate();
        texture->uav = core_.staging().allocate();
        if (!texture->srv.ptr || !texture->uav.ptr) {
            report_once(texture_failures_, "texture creation", "no staging descriptor block");
            return E_OUTOFMEMORY;
        }
        // An array reads as Texture2DArray (every level and slice).
        core_.device()->CreateShaderResourceView(texture->resource.Get(), nullptr, texture->srv);
        D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
        view.Format = format;
        if (volume) {
            view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            view.Texture3D.WSize = depth;
        } else if (array) {
            view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.ArraySize = slices;
        } else {
            view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        }
        core_.device()->CreateUnorderedAccessView(texture->resource.Get(), nullptr, &view, texture->uav);
        hazard_created(*texture, "storage texture");
        out = std::move(texture);
        return S_OK;
    }

    // Whether a storage texture of this format can be written by compute
    // (typed UAV store) and so can take generate_mips.
    bool storage_supported(DXGI_FORMAT format) noexcept override {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format, {}, {}};
        if (FAILED(core_.device()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)))) return false;
        return (support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) &&
               (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
    }

    // D3D11 GenerateMips for a 2D storage texture or slices [slice, slice +
    // count) of an array: each level from the one above by the downsample
    // program (t0 -> u0, 8x8 groups; for an array, its Texture2DArray/
    // RWTexture2DArray form over the views' count slices, one dispatch z a
    // slice: d3d12_mip_array_program reads only z 0, so count 1 for it), the
    // levels moved between read and write together for all those slices.
    HRESULT generate_mips(D3D12Texture& texture, const owned::ShaderProgram& downsample, UINT slice = 0, UINT count = 1) override {
        if (!accept_resource(&texture)) return DXGI_ERROR_DEVICE_RESET;
        if (texture.mips < 2 || !texture.uav.ptr || slice >= texture.slices || !count) return S_OK;
        count = (std::min)(count, texture.slices - slice);
        ID3D12GraphicsCommandList* list = open_list();
        if (!list) return E_FAIL;
        ID3D12PipelineState* pipeline = compute_pipeline(downsample);
        if (!pipeline) return S_FALSE;
        if (texture.mip_views.empty() || texture.mip_views_slice != slice || texture.mip_views_count != count) {
            for (const auto view : texture.mip_views) core_.staging().free(view);
            texture.mip_views.clear();
            const bool array = texture.slices > 1;
            for (UINT mip = 0; mip < texture.mips; ++mip) {
                const D3D12_CPU_DESCRIPTOR_HANDLE srv = core_.staging().allocate(), uav = core_.staging().allocate();
                if (srv.ptr) texture.mip_views.push_back(srv);
                if (uav.ptr) texture.mip_views.push_back(uav);
                if (!srv.ptr || !uav.ptr) {
                    for (const auto view : texture.mip_views) core_.staging().free(view);
                    texture.mip_views.clear();
                    report_once(texture_failures_, "texture creation", "no staging descriptor block");
                    return E_OUTOFMEMORY;
                }
                D3D12_SHADER_RESOURCE_VIEW_DESC read{};
                read.Format = texture.format;
                read.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                D3D12_UNORDERED_ACCESS_VIEW_DESC write{};
                write.Format = texture.format;
                if (array) {
                    read.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                    read.Texture2DArray = {mip, 1, slice, count, 0, 0.0f};
                    write.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                    write.Texture2DArray = {mip, slice, count, 0};
                } else {
                    read.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    read.Texture2D.MostDetailedMip = mip;
                    read.Texture2D.MipLevels = 1;
                    write.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                    write.Texture2D.MipSlice = mip;
                }
                core_.device()->CreateShaderResourceView(texture.resource.Get(), &read, srv);
                core_.device()->CreateUnorderedAccessView(texture.resource.Get(), nullptr, &write, uav);
            }
            texture.mip_views_slice = slice;
            texture.mip_views_count = count;
            ++view_writes_;  // the views were rewritten in place (tables_)
        }
        const ProgramCode* code = nullptr;
        (void)program_code(downsample, nullptr, &code);
        clear_slots();
        transition(list, texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (g_hazards) { g_hazards->command("generate_mips"); g_hazards->access(texture.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, "mip chain"); }
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        const auto to_read = [&](UINT subresource) {
            D3D12_RESOURCE_BARRIER barrier = transition_barrier(texture.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kShaderRead);
            barrier.Transition.Subresource = subresource;
            barriers.push_back(barrier);
        };
        const auto flush = [&] {
            if (!barriers.empty()) list->ResourceBarrier(UINT(barriers.size()), barriers.data());
            barriers.clear();
        };
        const auto range = [&](UINT subresource) {  // in the generated slices, a level below the last
            const UINT at = subresource / texture.mips, mip = subresource % texture.mips;
            return at >= slice && at < slice + count && mip + 1 < texture.mips;
        };
        for (UINT mip = 1; mip < texture.mips; ++mip) {
            for (UINT at = slice; at < slice + count; ++at) to_read(at * texture.mips + mip - 1);
            flush();
            resources_[0] = {texture.mip_views[(mip - 1) * 2], nullptr, nullptr};
            unordered_[0] = {texture.mip_views[mip * 2 + 1], nullptr, nullptr};
            slots_dirty_ = true;
            if (!bind_slots(list, true, code, nullptr)) return owned::kD3D12Dropped;
            set_pipeline(list, pipeline);
            const UINT width = (std::max)(1u, texture.width >> mip), height = (std::max)(1u, texture.height >> mip);
            list->Dispatch((width + 7) / 8, (height + 7) / 8, count);
        }
        // Every subresource readable again: the slices' last levels and the other slices.
        for (UINT subresource = 0; subresource < texture.mips * texture.slices; ++subresource)
            if (!range(subresource)) to_read(subresource);
        flush();
        texture.state = kShaderRead;
        texture.written = false;
        if (g_hazards) g_hazards->transition(texture.resource.Get(), kShaderRead);  // the per-level barriers above
        clear_slots();
        return S_OK;
    }

    HRESULT copy_texture(D3D12Texture& destination, UINT destination_subresource, D3D12Texture& source, UINT source_subresource) override {
        if (!accept_resource(&destination) || !accept_resource(&source)) return DXGI_ERROR_DEVICE_RESET;
        ID3D12GraphicsCommandList* list = open_list();
        if (!list) return E_FAIL;
        transition(list, destination, D3D12_RESOURCE_STATE_COPY_DEST);
        transition(list, source, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION to{}, from{};
        to.pResource = destination.resource.Get();
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.SubresourceIndex = destination_subresource;
        from.pResource = source.resource.Get();
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.SubresourceIndex = source_subresource;
        if (g_hazards) {
            g_hazards->command("copy_texture");
            g_hazards->access(destination.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, true, "copy destination");
            g_hazards->access(source.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, false, "copy source");
        }
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        transition(list, destination, kShaderRead);
        transition(list, source, kShaderRead);
        return S_OK;
    }

    HRESULT create_buffer(UINT64 bytes, UINT stride, UINT flags, owned::D3D12BufferRef& out) override {
        out.reset();
        if (!failure_.empty()) return E_FAIL;
        if ((flags & owned::kD3D12BufferTyped) && stride != 4 && stride != 16) return E_INVALIDARG;
        bytes = (std::max<UINT64>)(16, (bytes + 15) & ~UINT64(15));
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (flags & owned::kD3D12BufferUnordered) desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> resource;
        HRESULT hr = core_.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                             IID_PPV_ARGS(&resource));
        if (FAILED(hr)) {
            report_once(buffer_failures_, "buffer creation", hresult_text("CreateCommittedResource (native buffer)", hr));
            return hr;
        }
        return adopt_buffer(std::move(resource), bytes, stride, flags, out);
    }

    // A created buffer resource (COMMON) as a D3D12Buffer: its views and its hazard record.
    HRESULT adopt_buffer(ComPtr<ID3D12Resource> resource, UINT64 bytes, UINT stride, UINT flags, owned::D3D12BufferRef& out) {
        auto buffer = std::make_shared<owned::D3D12Buffer>();
        buffer->resource = std::move(resource);
        buffer->owner = resource_owner_;
        buffer->bytes = bytes;
        buffer->stride = stride;
        buffer->flags = flags;
        buffer->srv = core_.staging().allocate();
        if ((flags & owned::kD3D12BufferUnordered)) buffer->uav = core_.staging().allocate();
        if (!buffer->srv.ptr || ((flags & owned::kD3D12BufferUnordered) && !buffer->uav.ptr)) {
            report_once(buffer_failures_, "buffer creation", "no staging descriptor block");
            return E_OUTOFMEMORY;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        if (flags & owned::kD3D12BufferTyped) {
            srv.Format = uav.Format = stride == 4 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R32G32B32A32_FLOAT;
            srv.Buffer.NumElements = uav.Buffer.NumElements = UINT(bytes / stride);
        } else if (stride) {
            srv.Format = uav.Format = DXGI_FORMAT_UNKNOWN;
            srv.Buffer.NumElements = uav.Buffer.NumElements = UINT(bytes / stride);
            srv.Buffer.StructureByteStride = uav.Buffer.StructureByteStride = stride;
        } else {
            srv.Format = uav.Format = DXGI_FORMAT_R32_TYPELESS;
            srv.Buffer.NumElements = uav.Buffer.NumElements = UINT(bytes / 4);
            srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        }
        core_.device()->CreateShaderResourceView(buffer->resource.Get(), &srv, buffer->srv);
        if (buffer->uav.ptr) core_.device()->CreateUnorderedAccessView(buffer->resource.Get(), nullptr, &uav, buffer->uav);
        hazard_created(*buffer);
        out = std::move(buffer);
        return S_OK;
    }

    UINT64 buffer_bytes(const owned::D3D12Buffer& buffer) const noexcept override { return buffer.bytes; }

    // ---- Uploads prepared off the render thread (D3D12Preparer) ----------
    std::shared_ptr<owned::D3D12Preparer> preparer() noexcept override { return preparer_; }

    HRESULT finish_buffer(owned::D3D12PreparedUpload& upload, UINT stride, UINT flags, owned::D3D12BufferRef& out) override {
        out.reset();
        if (!failure_.empty()) return E_FAIL;
        if (upload.owner != resource_owner_) return DXGI_ERROR_DEVICE_RESET;
        // The prepared resource has no unordered access.
        if (upload.texture || !upload.resource || !upload.upload || (flags & owned::kD3D12BufferUnordered)) return E_INVALIDARG;
        if ((flags & owned::kD3D12BufferTyped) && stride != 4 && stride != 16) return E_INVALIDARG;
        ID3D12GraphicsCommandList* list = open_list();
        if (!list) return E_FAIL;
        owned::D3D12BufferRef buffer;
        const HRESULT hr = adopt_buffer(std::move(upload.resource), upload.bytes, stride, flags, buffer);
        if (FAILED(hr)) return hr;
        transition(list, *buffer, D3D12_RESOURCE_STATE_COPY_DEST);
        if (g_hazards) { g_hazards->command("prepared buffer upload"); g_hazards->access(buffer->resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, true, "copy destination"); }
        list->CopyBufferRegion(buffer->resource.Get(), 0, upload.upload.Get(), 0, upload.bytes);
        core_.release_later(std::move(upload.upload));  // read by this frame's copy
        out = std::move(buffer);
        return S_OK;
    }

    HRESULT finish_texture(owned::D3D12PreparedUpload& upload, owned::D3D12TextureRef& out) override {
        out.reset();
        if (!failure_.empty()) return E_FAIL;
        if (upload.owner != resource_owner_) return DXGI_ERROR_DEVICE_RESET;
        if (!upload.texture || !upload.resource || !upload.upload || upload.levels.size() != upload.mips) return E_INVALIDARG;
        ID3D12GraphicsCommandList* list = open_list();
        if (!list) return E_FAIL;
        auto texture = std::make_shared<D3D12Texture>();
        texture->resource = std::move(upload.resource);
        texture->heap = std::move(upload.heap);  // placed: its heap goes with it
        texture->owner = resource_owner_;
        texture->width = upload.width;
        texture->height = upload.height;
        texture->mips = upload.mips;
        texture->format = upload.format;
        texture->srv = core_.staging().allocate();
        if (!texture->srv.ptr) {
            report_once(texture_failures_, "texture creation", "no staging descriptor block");
            return E_OUTOFMEMORY;
        }
        core_.device()->CreateShaderResourceView(texture->resource.Get(), nullptr, texture->srv);
        hazard_created(*texture, "texture (prepared)");
        if (g_hazards) { g_hazards->command("prepared texture upload"); g_hazards->access(texture->resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, true, "copy destination"); }
        for (UINT level = 0; level < upload.mips; ++level) {
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = texture->resource.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = level;
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = upload.upload.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = upload.levels[level];
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }
        // Shader-readable at once, as write_texture leaves it.
        transition(list, *texture, kShaderRead);
        core_.release_later(std::move(upload.upload));  // read by this frame's copies
        out = std::move(texture);
        return S_OK;
    }

    HRESULT write_buffer(owned::D3D12Buffer& buffer, UINT64 offset, const void* data, UINT64 bytes) override {
        return write_buffer_from(upload_, buffer, offset, data, bytes);
    }
    HRESULT write_buffer_bulk(owned::D3D12Buffer& buffer, UINT64 offset, const void* data, UINT64 bytes) override {
        return write_buffer_from(bulk_, buffer, offset, data, bytes);
    }
    HRESULT write_buffer_from(UploadRing& ring, owned::D3D12Buffer& buffer, UINT64 offset, const void* data, UINT64 bytes) {
        if (!accept_resource(&buffer)) return DXGI_ERROR_DEVICE_RESET;
        if (!bytes) return S_OK;
        if (offset > buffer.bytes || bytes > buffer.bytes - offset) return E_INVALIDARG;
        ID3D12GraphicsCommandList* list = open_list();
        if (!list) return E_FAIL;
        UploadAllocation upload;
        std::string reason;
        if (!ring.push(core_, data, bytes, 16, upload, reason)) return fail(E_OUTOFMEMORY, reason);
        if (g_hazards) g_hazards->taken(Hazards::kUploadRing, upload.gpu, bytes, core_.recording_value(), core_.completed());
        if (&ring == &upload_) upload_buffer_bytes_ += bytes;
        transition(list, buffer, D3D12_RESOURCE_STATE_COPY_DEST);
        if (g_hazards) { g_hazards->command("write_buffer"); g_hazards->access(buffer.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, true, "copy destination"); }
        list->CopyBufferRegion(buffer.resource.Get(), offset, upload.resource, upload.offset, bytes);
        return S_OK;
    }

    HRESULT copy_buffer(owned::D3D12Buffer& destination, owned::D3D12Buffer& source, UINT64 bytes) override {
        if (!accept_resource(&destination) || !accept_resource(&source)) return DXGI_ERROR_DEVICE_RESET;
        if (!bytes) return S_OK;
        if (bytes > destination.bytes || bytes > source.bytes) return E_INVALIDARG;
        ID3D12GraphicsCommandList* list = open_list();
        if (!list) return E_FAIL;
        transition(list, destination, D3D12_RESOURCE_STATE_COPY_DEST);
        transition(list, source, D3D12_RESOURCE_STATE_COPY_SOURCE);
        if (g_hazards) {
            g_hazards->command("copy_buffer");
            g_hazards->access(destination.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, true, "copy destination");
            g_hazards->access(source.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, false, "copy source");
        }
        list->CopyBufferRegion(destination.resource.Get(), 0, source.resource.Get(), 0, bytes);
        return S_OK;
    }

    // ---- Native passes: the D3D11 register model slot by slot -------------
    void set_constants(UINT slot, const void* data, UINT bytes) override {
        if (slot >= kConstantSlots || !bytes) return;
        if (!open_list()) return;
        UploadAllocation allocation;
        std::string reason;
        // b0 takes at least 512 bytes: native_shader.h reads b0 through a raw
        // view (no bounds), so a shorter block reads zeros past its data up to
        // the native layout's 368 bytes, as the constant buffer view did.
        const UINT size = (std::max)((bytes + 255u) & ~255u, slot == 0 ? 512u : 256u);
        if (!upload_allocate(size, 256, allocation, reason)) { fail(E_OUTOFMEMORY, reason); return; }
        std::memcpy(allocation.cpu, data, bytes);
        if (size > bytes) std::memset(allocation.cpu + bytes, 0, size - bytes);
        constants_[slot] = {allocation.gpu, size};
        slots_dirty_ = true;
    }
    void set_resource(UINT slot, const owned::D3D12Buffer* buffer) noexcept override {
        if (!accept_resource(buffer)) return;
        if (slot >= kD3D11ResourceSlots) return;
        resources_[slot] = {buffer ? buffer->srv : D3D12_CPU_DESCRIPTOR_HANDLE{}, const_cast<owned::D3D12Buffer*>(buffer), nullptr};
        slots_dirty_ = true;
    }
    void set_resource(UINT slot, const D3D12Texture* texture) noexcept override {
        if (!accept_resource(texture)) return;
        if (slot >= kD3D11ResourceSlots) return;
        resources_[slot] = {texture ? texture->srv : D3D12_CPU_DESCRIPTOR_HANDLE{}, nullptr, const_cast<D3D12Texture*>(texture)};
        slots_dirty_ = true;
    }
    void set_unordered(UINT slot, const owned::D3D12Buffer* buffer) noexcept override {
        if (!accept_resource(buffer)) return;
        if (slot >= kD3D11UnorderedSlots) return;
        unordered_[slot] = {buffer ? buffer->uav : D3D12_CPU_DESCRIPTOR_HANDLE{}, const_cast<owned::D3D12Buffer*>(buffer), nullptr};
        slots_dirty_ = true;
    }
    void set_unordered(UINT slot, const D3D12Texture* texture) noexcept override {
        if (!accept_resource(texture)) return;
        if (slot >= kD3D11UnorderedSlots) return;
        unordered_[slot] = {texture ? texture->uav : D3D12_CPU_DESCRIPTOR_HANDLE{}, nullptr, const_cast<D3D12Texture*>(texture)};
        slots_dirty_ = true;
    }
    void set_unordered_level(UINT slot, D3D12Texture* texture, UINT mip) noexcept override {
        if (!accept_resource(texture)) return;
        if (slot >= kD3D11UnorderedSlots) return;
        const D3D12_CPU_DESCRIPTOR_HANDLE view = texture ? level_uav(*texture, mip) : D3D12_CPU_DESCRIPTOR_HANDLE{};
        unordered_[slot] = {view, nullptr, view.ptr ? texture : nullptr};
        slots_dirty_ = true;
    }
    // A 2D storage texture's unordered view of one level (mip 0: its own
    // view); null when it has no such level or no staging descriptor is left.
    D3D12_CPU_DESCRIPTOR_HANDLE level_uav(D3D12Texture& texture, UINT mip) noexcept {
        if (!accept_resource(&texture)) return {};
        if (mip == 0 || !texture.uav.ptr) return texture.uav;
        if (mip >= texture.mips || texture.slices > 1) return {};
        if (texture.level_uavs.empty()) {
            for (UINT level = 1; level < texture.mips; ++level) {
                const D3D12_CPU_DESCRIPTOR_HANDLE view = core_.staging().allocate();
                if (!view.ptr) {
                    for (const auto made : texture.level_uavs) core_.staging().free(made);
                    texture.level_uavs.clear();
                    report_once(texture_failures_, "texture creation", "no staging descriptor block");
                    return {};
                }
                D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
                desc.Format = texture.format;
                desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice = level;
                core_.device()->CreateUnorderedAccessView(texture.resource.Get(), nullptr, &desc, view);
                texture.level_uavs.push_back(view);
            }
        }
        return texture.level_uavs[mip - 1];
    }
    bool unordered_load_supported(DXGI_FORMAT format) noexcept override {
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        if (FAILED(core_.device()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) return false;
        if (format == DXGI_FORMAT_R32_FLOAT || format == DXGI_FORMAT_R32_UINT || format == DXGI_FORMAT_R32_SINT) return true;
        if (!options.TypedUAVLoadAdditionalFormats) return false;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format, {}, {}};
        if (FAILED(core_.device()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)))) return false;
        return (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0;
    }
    void set_sampler(UINT slot, const D3D11_SAMPLER_DESC& sampler) noexcept override {
        if (slot >= kD3D11SamplerSlots) return;
        samplers_set_[slot] = true;
        sampler_descs_[slot] = to_sampler(sampler);
        slots_dirty_ = true;
    }
    void clear_slots() noexcept override {
        for (auto& slot : resources_) slot = {};
        for (auto& slot : unordered_) slot = {};
        for (auto& constant : constants_) constant = {};
        for (bool& set : samplers_set_) set = false;
        slots_dirty_ = true;
    }

    HRESULT dispatch(const owned::ShaderProgram& program, UINT x, UINT y, UINT z) override {
        ID3D12GraphicsCommandList* list = open_list();
        if (!list) return E_FAIL;
        const ProgramCode* code = nullptr;
        ID3D12PipelineState* pipeline = compute_pipeline(program, &code);
        if (!pipeline) return S_FALSE;
        if (!bind_slots(list, true, code, nullptr)) return owned::kD3D12Dropped;
        set_pipeline(list, pipeline);
        if (g_hazards) hazard_native("dispatch", true, code, nullptr, false, nullptr, nullptr);
        list->Dispatch(x, y, z);
        for (auto& slot : unordered_)
            if (slot.buffer) slot.buffer->written = true;
        for (auto& slot : unordered_)
            if (slot.texture) slot.texture->written = true;
        return S_OK;
    }

    // A dispatch whose group counts come from the arguments (three words at
    // offset, as D3D11 DispatchIndirect reads them).
    HRESULT dispatch_indirect(const owned::ShaderProgram& program, owned::D3D12Buffer& arguments, UINT offset) override {
        if (!accept_resource(&arguments)) return DXGI_ERROR_DEVICE_RESET;
        ID3D12GraphicsCommandList* list = open_list();
        if (!list) return E_FAIL;
        const ProgramCode* code = nullptr;
        ID3D12PipelineState* pipeline = compute_pipeline(program, &code);
        if (!pipeline || !dispatch_signature_) return S_FALSE;
        if (!bind_slots(list, true, code, nullptr, &arguments)) return owned::kD3D12Dropped;
        transition(list, arguments, indirect_state(arguments));
        set_pipeline(list, pipeline);
        if (g_hazards) hazard_native("dispatch_indirect", true, code, nullptr, false, &arguments, nullptr);
        list->ExecuteIndirect(dispatch_signature_.Get(), 1, arguments.resource.Get(), offset, nullptr, 0);
        for (auto& slot : unordered_) {
            if (slot.buffer) slot.buffer->written = true;
            if (slot.texture) slot.texture->written = true;
        }
        return S_OK;
    }

    void set_index_buffer(owned::D3D12Buffer* indices) noexcept override { if (accept_resource(indices)) index_buffer_ = indices; }

    // An indexed draw (the index buffer, 32-bit) from five argument words at
    // offset, as D3D11 DrawIndexedInstancedIndirect reads them.
    HRESULT draw_indexed_indirect(const owned::ShaderProgram& vertex, const owned::ShaderProgram& pixel,
                                  const owned::D3D12NativeState& state, const RECT* scissor, owned::D3D12Buffer& arguments,
                                  UINT offset) override {
        if (!accept_resource(&arguments)) return DXGI_ERROR_DEVICE_RESET;
        ID3D12GraphicsCommandList* list = open_target();
        if (!list) return failure_.empty() ? S_OK : E_FAIL;  // minimized: nothing visible
        if (!index_buffer_ || !indexed_signature_) return E_INVALIDARG;
        const ProgramCode* codes[2] = {};
        ID3D12PipelineState* pipeline = native_pipeline(vertex, pixel, state, nullptr, nullptr, codes);
        if (!pipeline) return S_FALSE;
        const ProgramCode *vertex_code = codes[0], *pixel_code = codes[1];
        if (!bind_slots(list, false, vertex_code, pixel_code, &arguments)) return owned::kD3D12Dropped;
        transition(list, arguments, indirect_state(arguments));
        transition(list, *index_buffer_, D3D12_RESOURCE_STATE_INDEX_BUFFER);
        const D3D12_INDEX_BUFFER_VIEW view{index_buffer_->resource->GetGPUVirtualAddress(), UINT(index_buffer_->bytes), DXGI_FORMAT_R32_UINT};
        bind_index_view(list, view);
        bind_target(list, true);
        set_pipeline(list, pipeline);
        set_topology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const D3D12_RECT clip = scissor ? D3D12_RECT{scissor->left, scissor->top, scissor->right, scissor->bottom}
                                        : D3D12_RECT{0, 0, LONG(target_width()), LONG(target_height())};
        set_scissor(list, clip);
        if (g_hazards) hazard_native("draw_indexed_indirect", false, vertex_code, pixel_code, false, &arguments, index_buffer_);
        list->ExecuteIndirect(indexed_signature_.Get(), 1, arguments.resource.Get(), offset, nullptr, 0);
        if (!depth_target_) drawn_ = true;
        if (offscreen_world_) world_drawn_ = true;
        return S_OK;
    }

    // Every 32-bit word of the buffer set to value (D3D11
    // ClearUnorderedAccessViewUint on its raw view), in stream order.
    void clear_buffer_uint(owned::D3D12Buffer& buffer, UINT value) noexcept override {
        if (!accept_resource(&buffer)) return;
        ID3D12GraphicsCommandList* list = open_list();
        if (!list || buffer.bytes < 4) return;
        if (!buffer.clear_uav.ptr) {
            buffer.clear_uav = core_.staging().allocate();
            if (!buffer.clear_uav.ptr) { omit("buffer clears (no staging descriptor)", false); return; }
            D3D12_UNORDERED_ACCESS_VIEW_DESC raw{};
            raw.Format = DXGI_FORMAT_R32_TYPELESS;
            raw.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            raw.Buffer.NumElements = UINT(buffer.bytes / 4);
            raw.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            core_.device()->CreateUnorderedAccessView(buffer.resource.Get(), nullptr, &raw, buffer.clear_uav);
        }
        const std::uint32_t index = ring(1);
        if (index == UINT32_MAX) return;  // named by ring
        core_.device()->CopyDescriptorsSimple(1, core_.views().cpu(index), buffer.clear_uav, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        transition(list, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (g_hazards) { g_hazards->command("clear_buffer_uint"); g_hazards->access(buffer.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, "cleared"); }
        const UINT values[4] = {value, value, value, value};
        list->ClearUnorderedAccessViewUint(core_.views().gpu(index), buffer.clear_uav, buffer.resource.Get(), values, 0, nullptr);
        buffer.written = true;
    }

    // A non-blocking readback: bytes of the buffer copied in stream order;
    // poll_readback hands them over once the frame that copied them finished.
    HRESULT request_readback(owned::D3D12Buffer& source, UINT64 bytes, std::uint64_t& ticket) override {
        ticket = 0;
        if (!accept_resource(&source)) return DXGI_ERROR_DEVICE_RESET;
        ID3D12GraphicsCommandList* list = open_list();
        if (!list) return E_FAIL;
        bytes = (std::min)(bytes, source.bytes);
        if (!bytes) return E_INVALIDARG;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Readback readback;
        HRESULT hr = core_.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                             IID_PPV_ARGS(&readback.heap));
        if (FAILED(hr)) return hr;
        transition(list, source, D3D12_RESOURCE_STATE_COPY_SOURCE);
        if (g_hazards) { g_hazards->command("readback"); g_hazards->access(source.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, false, "copy source"); }
        list->CopyBufferRegion(readback.heap.Get(), 0, source.resource.Get(), 0, bytes);
        readback.ticket = next_readback_++;
        readback.fence = core_.recording_value();
        readback.bytes = bytes;
        // Unpolled copies are dropped past a small bound (their owner moved on).
        // The GPU may still be copying into the dropped one: released by fence.
        if (readbacks_.size() >= 32) {
            const Readback& dropped = readbacks_.front();
            if (g_hazards) g_hazards->released("an unpolled readback buffer", dropped.fence, core_.completed());
            core_.release_later(dropped.heap);
            readbacks_.erase(readbacks_.begin());
        }
        readbacks_.push_back(std::move(readback));
        ticket = readbacks_.back().ticket;
        return S_OK;
    }
    HRESULT poll_readback(std::uint64_t ticket, std::vector<std::uint8_t>& data) override {
        for (auto it = readbacks_.begin(); it != readbacks_.end(); ++it) {
            if (it->ticket != ticket) continue;
            if (core_.completed() < it->fence) return S_FALSE;
            void* mapped = nullptr;
            const D3D12_RANGE range{0, SIZE_T(it->bytes)};
            HRESULT hr = it->heap->Map(0, &range, &mapped);
            if (SUCCEEDED(hr)) {
                try {
                    data.assign(static_cast<const std::uint8_t*>(mapped), static_cast<const std::uint8_t*>(mapped) + it->bytes);
                } catch (...) { hr = E_OUTOFMEMORY; }
                const D3D12_RANGE written{0, 0};
                it->heap->Unmap(0, &written);
            }
            readbacks_.erase(it);
            return hr;
        }
        return E_INVALIDARG;
    }

    HRESULT draw_indirect(const owned::ShaderProgram& vertex, const owned::ShaderProgram& pixel, const owned::D3D12NativeState& state,
                          const RECT* scissor, owned::D3D12Buffer& arguments, UINT offset, const owned::ShaderProgram* hull = nullptr,
                          const owned::ShaderProgram* domain = nullptr, bool strip = false) override {
        if (!accept_resource(&arguments)) return DXGI_ERROR_DEVICE_RESET;
        const bool patches = hull && domain;
        ID3D12GraphicsCommandList* list = open_target();
        if (!list) return failure_.empty() ? S_OK : E_FAIL;  // minimized: nothing visible
        const ProgramCode* codes[2] = {};
        ID3D12PipelineState* pipeline = native_pipeline(vertex, pixel, state, patches ? hull : nullptr, patches ? domain : nullptr, codes);
        if (!pipeline || !draw_signature_) return S_FALSE;
        const ProgramCode *vertex_code = codes[0], *pixel_code = codes[1];
        if (!bind_slots(list, false, vertex_code, pixel_code, &arguments)) return owned::kD3D12Dropped;
        transition(list, arguments, indirect_state(arguments));
        bind_target(list, true);
        set_pipeline(list, pipeline);
        set_topology(list, patches ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST
                           : strip ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const D3D12_RECT clip = scissor ? D3D12_RECT{scissor->left, scissor->top, scissor->right, scissor->bottom}
                                        : D3D12_RECT{0, 0, LONG(target_width()), LONG(target_height())};
        set_scissor(list, clip);
        if (g_hazards) hazard_native("draw_indirect", false, vertex_code, pixel_code, patches, &arguments, nullptr);
        list->ExecuteIndirect(draw_signature_.Get(), 1, arguments.resource.Get(), offset, nullptr, 0);
        if (!depth_target_) drawn_ = true;  // a shadow map draw leaves the frame undrawn
        if (offscreen_world_) world_drawn_ = true;
        return S_OK;
    }

    HRESULT draw(const owned::ShaderProgram& vertex, const owned::ShaderProgram& pixel, const owned::D3D12NativeState& state,
                 const RECT* scissor, UINT vertex_count) override {
        return draw_instanced(vertex, pixel, state, scissor, vertex_count, 1, false);
    }

    HRESULT draw_instanced(const owned::ShaderProgram& vertex, const owned::ShaderProgram& pixel, const owned::D3D12NativeState& state,
                           const RECT* scissor, UINT vertex_count, UINT instance_count, bool strip) override {
        if (!vertex_count || !instance_count) return S_OK;
        ID3D12GraphicsCommandList* list = open_target();
        if (!list) return failure_.empty() ? S_OK : E_FAIL;  // minimized: nothing visible
        const ProgramCode* codes[2] = {};
        ID3D12PipelineState* pipeline = native_pipeline(vertex, pixel, state, nullptr, nullptr, codes);
        if (!pipeline) return S_FALSE;
        const ProgramCode *vertex_code = codes[0], *pixel_code = codes[1];
        if (!bind_slots(list, false, vertex_code, pixel_code)) return owned::kD3D12Dropped;
        bind_target(list, true);
        set_pipeline(list, pipeline);
        set_topology(list, strip ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const D3D12_RECT clip = scissor ? D3D12_RECT{scissor->left, scissor->top, scissor->right, scissor->bottom}
                                        : D3D12_RECT{0, 0, LONG(target_width()), LONG(target_height())};
        set_scissor(list, clip);
        if (g_hazards) hazard_native("draw_instanced", false, vertex_code, pixel_code, false, nullptr, nullptr);
        list->DrawInstanced(vertex_count, instance_count, 0, 0);
        if (!depth_target_) drawn_ = true;  // a shadow map draw leaves the frame undrawn
        if (offscreen_world_) world_drawn_ = true;
        return S_OK;
    }

    void clear_depth(float depth, UINT8 stencil) noexcept override {
        if (offscreen_) { clear_target(nullptr, true); return; }
        if (target_ && depth_) list_->ClearDepthStencilView(depth_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depth, stencil, 0, nullptr);
    }

    void clear_rectangles(const RECT* rects, UINT count, const float* color, bool depth, bool stencil, float z,
                          UINT8 stencil_value) noexcept override {
        if (!rects || !count || (!color && !depth && !stencil) || depth_target_) return;
        ID3D12GraphicsCommandList* list = open_target();
        if (!list) return;
        std::vector<D3D12_RECT> clipped;
        try { clipped.reserve(count); } catch (...) { return; }
        const LONG width = LONG(target_width()), height = LONG(target_height());
        for (UINT index = 0; index < count; ++index) {
            const D3D12_RECT r{(std::max)(rects[index].left, 0L), (std::max)(rects[index].top, 0L), (std::min)(rects[index].right, width),
                               (std::min)(rects[index].bottom, height)};
            if (r.left < r.right && r.top < r.bottom) clipped.push_back(r);
        }
        if (clipped.empty()) return;
        const UINT n = UINT(clipped.size());
        if (g_hazards) g_hazards->command("clear_rectangles");
        if (color) {
            if (offscreen_) {
                transition(list, *offscreen_, D3D12_RESOURCE_STATE_RENDER_TARGET);
                if (g_hazards) g_hazards->access(offscreen_->resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, true, "cleared colour");
                list->ClearRenderTargetView(offscreen_->rtv, color, n, clipped.data());
                if (offscreen_world_) world_drawn_ = true;
            } else {
                list->ClearRenderTargetView(back_rtv(list), color, n, clipped.data());
            }
        }
        if (depth || stencil) {
            D3D12Texture* const owner = depth_override_ ? depth_owner_ : offscreen_;
            if (owner) {
                if (!owner->dsv.ptr) return;
                D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAGS(0);
                const D3D12_CLEAR_FLAGS available = depth_clear_flags(*owner);
                if (depth) flags |= D3D12_CLEAR_FLAG_DEPTH;
                if (stencil && (available & D3D12_CLEAR_FLAG_STENCIL)) flags |= D3D12_CLEAR_FLAG_STENCIL;
                transition_depth(list, *owner, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                if (g_hazards)
                    g_hazards->access(owner->depth ? owner->depth.Get() : owner->resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, true, "cleared depth");
                list->ClearDepthStencilView(owner->dsv, flags, z, stencil_value, n, clipped.data());
            } else if (depth_ && !depth_override_) {
                D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAGS(0);
                if (depth) flags |= D3D12_CLEAR_FLAG_DEPTH;
                if (stencil) flags |= D3D12_CLEAR_FLAG_STENCIL;
                list->ClearDepthStencilView(depth_dsv_, flags, z, stencil_value, n, clipped.data());
            }
        }
    }

    void native_pipeline_stats(UINT& count, double& milliseconds) const noexcept override {
        count = 0;
        milliseconds = 0.0;
        for (const auto& entry : native_pipelines_) {
            const double built = pipelines_.milliseconds(entry.second);
            if (built > 0.0) { ++count; milliseconds += built; }
        }
        for (const auto& entry : compute_pipelines_) {
            const double built = pipelines_.milliseconds(entry.second);
            if (built > 0.0) { ++count; milliseconds += built; }
        }
    }

    bool texture_matches(const D3D12Texture& texture, UINT width, UINT height, DXGI_FORMAT format) const noexcept override {
        return texture.owner == resource_owner_ && texture.resource &&
            texture.width == width && texture.height == height && texture.format == format;
    }

    HRESULT write_texture(D3D12Texture& texture, const RECT& rect, const void* data, UINT pitch, UINT mip = 0) override {
        if (!accept_resource(&texture)) return DXGI_ERROR_DEVICE_RESET;
        ID3D12GraphicsCommandList* list = open_list();
        if (!list) return E_FAIL;
        if (rect.right <= rect.left || rect.bottom <= rect.top) return S_OK;
        if (mip >= texture.mips * texture.slices) return E_INVALIDARG;  // an array's subresource: mip + slice * mips
        const UINT width = UINT(rect.right - rect.left), height = UINT(rect.bottom - rect.top);
        UINT block_bytes = 0;
        const bool blocks = block_format(texture.format, block_bytes);
        UINT texel_bytes = 4;
        switch (texture.format) {
        case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_UINT: texel_bytes = 1; break;
        case DXGI_FORMAT_B5G6R5_UNORM: case DXGI_FORMAT_B5G5R5A1_UNORM: case DXGI_FORMAT_B4G4R4A4_UNORM:
        case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_UINT: case DXGI_FORMAT_R16_SNORM: texel_bytes = 2; break;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R32G32_FLOAT: case DXGI_FORMAT_R32G32_UINT: texel_bytes = 8; break;
        case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT: texel_bytes = 16; break;
        default: break;
        }
        const UINT row_bytes = blocks ? (width + 3) / 4 * block_bytes : width * texel_bytes;
        const UINT rows = blocks ? (height + 3) / 4 : height;
        const UINT row_pitch = (row_bytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        UploadAllocation upload;
        std::string reason;
        if (!upload_allocate(std::uint64_t(row_pitch) * rows, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, upload, reason))
            return fail(E_OUTOFMEMORY, reason);
        upload_texture_bytes_ += std::uint64_t(row_pitch) * rows;
        for (UINT row = 0; row < rows; ++row)
            std::memcpy(upload.cpu + std::size_t(row) * row_pitch, static_cast<const std::uint8_t*>(data) + std::size_t(row) * pitch, row_bytes);
        transition(list, texture, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture.resource.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = mip;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload.resource;
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Offset = upload.offset;
        source.PlacedFootprint.Footprint = {texture.format, blocks ? (width + 3) & ~3u : width, blocks ? (height + 3) & ~3u : height, 1,
                                            row_pitch};
        if (g_hazards) { g_hazards->command("texture upload"); g_hazards->access(texture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, true, "copy destination"); }
        list->CopyTextureRegion(&destination, UINT(rect.left), UINT(rect.top), 0, &source, nullptr);
        // Shader-readable again at once: bindless draws read any texture by
        // index without a per-draw transition.
        transition(list, texture, kShaderRead);
        return S_OK;
    }

    HRESULT capture(D3D12Texture& texture, UINT x, UINT y, const RECT& box, D3D12Texture* from = nullptr) override {
        if (!accept_resource(&texture) || !accept_resource(from)) return DXGI_ERROR_DEVICE_RESET;
        ID3D12GraphicsCommandList* list = open_target();
        if (!list) return failure_.empty() ? S_FALSE : E_FAIL;  // minimized: nothing drawn to copy
        if (box.right <= box.left || box.bottom <= box.top) return S_OK;
        D3D12Texture* image = from ? from : frame_image();  // an offscreen target, or the frame image while it is the back buffer
        if (!image) image = screen_;          // frame generation: this frame's screen image
        ID3D12Resource* back = image ? image->resource.Get() : presenter_.back_buffer();
        if (image) {
            transition(list, *image, D3D12_RESOURCE_STATE_COPY_SOURCE);
        } else {
            D3D12_RESOURCE_BARRIER to_source = transition_barrier(back, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->ResourceBarrier(1, &to_source);
        }
        transition(list, texture, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture.resource.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = back;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        const D3D12_BOX region{UINT(box.left), UINT(box.top), 0, UINT(box.right), UINT(box.bottom), 1};
        if (g_hazards) {
            g_hazards->command("capture");
            g_hazards->access(texture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, true, "copy destination");
            if (image) g_hazards->access(back, D3D12_RESOURCE_STATE_COPY_SOURCE, false, "copy source");
        }
        list->CopyTextureRegion(&destination, x, y, 0, &source, &region);
        transition(list, texture, kShaderRead);
        if (image) {
            transition(list, *image, D3D12_RESOURCE_STATE_RENDER_TARGET);
        } else {
            D3D12_RESOURCE_BARRIER to_target = transition_barrier(back, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            list->ResourceBarrier(1, &to_target);
        }
        return S_OK;
    }

    HRESULT create_target(UINT width, UINT height, owned::D3D12TextureRef& out, DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM,
                          DXGI_FORMAT depth_format = DXGI_FORMAT_D24_UNORM_S8_UINT) override {
        return make_target(width, height, out, format, depth_format, false);
    }
    // unordered: the colour target also takes a UAV (frame generation's output).
    HRESULT make_target(UINT width, UINT height, owned::D3D12TextureRef& out, DXGI_FORMAT format, DXGI_FORMAT depth_format, bool unordered) {
        out.reset();
        if (!failure_.empty()) return E_FAIL;
        if (format == DXGI_FORMAT_UNKNOWN && depth_format == DXGI_FORMAT_UNKNOWN) return E_INVALIDARG;
        if (depth_format != DXGI_FORMAT_UNKNOWN && depth_format != kDepthFormat && depth_format != DXGI_FORMAT_D32_FLOAT) return E_INVALIDARG;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | (unordered ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = desc.Format;
        clear.Color[3] = 1.0f;
        auto texture = std::make_shared<D3D12Texture>();
        const auto failed = [&](HRESULT hr, const char* what) {
            report_once(texture_failures_, "offscreen target creation", hresult_text(what, hr));
            return hr;
        };
        texture->owner = resource_owner_;
        texture->width = width;
        texture->height = height;
        HRESULT hr = S_OK;
        D3D12_DESCRIPTOR_HEAP_DESC views{};
        views.NumDescriptors = 1;
        if (format != DXGI_FORMAT_UNKNOWN) {
            hr = core_.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
                                                         IID_PPV_ARGS(&texture->resource));
            if (FAILED(hr)) return failed(hr, "CreateCommittedResource (offscreen colour)");
            texture->format = desc.Format;
            texture->state = D3D12_RESOURCE_STATE_RENDER_TARGET;
            texture->srv = core_.staging().allocate();
            if (!texture->srv.ptr) return failed(E_OUTOFMEMORY, "staging descriptor block");
            core_.device()->CreateShaderResourceView(texture->resource.Get(), nullptr, texture->srv);
            views.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            if (FAILED(hr = core_.device()->CreateDescriptorHeap(&views, IID_PPV_ARGS(&texture->rtv_heap))))
                return failed(hr, "CreateDescriptorHeap (offscreen colour)");
            texture->rtv = texture->rtv_heap->GetCPUDescriptorHandleForHeapStart();
            core_.device()->CreateRenderTargetView(texture->resource.Get(), nullptr, texture->rtv);
        }
        if (depth_format != DXGI_FORMAT_UNKNOWN) {
            // D24S8 readable as R24_UNORM, or D32 readable as R32_FLOAT.
            const bool d32 = depth_format == DXGI_FORMAT_D32_FLOAT;
            views.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            if (FAILED(hr = core_.device()->CreateDescriptorHeap(&views, IID_PPV_ARGS(&texture->dsv_heap))))
                return failed(hr, "CreateDescriptorHeap (offscreen depth)");
            texture->dsv = texture->dsv_heap->GetCPUDescriptorHandleForHeapStart();
            desc.Format = d32 ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R24G8_TYPELESS;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            clear.Format = depth_format;
            clear.DepthStencil = {1.0f, 0};
            if (FAILED(hr = core_.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                                                                    IID_PPV_ARGS(&texture->depth))))
                return failed(hr, "CreateCommittedResource (offscreen depth)");
            texture->depth_format = depth_format;
            texture->depth_state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
            dsv.Format = depth_format;
            dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            core_.device()->CreateDepthStencilView(texture->depth.Get(), &dsv, texture->dsv);
            texture->depth_srv = core_.staging().allocate();
            if (!texture->depth_srv.ptr) return failed(E_OUTOFMEMORY, "staging descriptor block");
            D3D12_SHADER_RESOURCE_VIEW_DESC depth_view{};
            depth_view.Format = d32 ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            depth_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            depth_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            depth_view.Texture2D.MipLevels = 1;
            core_.device()->CreateShaderResourceView(texture->depth.Get(), &depth_view, texture->depth_srv);
        }
        hazard_created(*texture, "target");
        out = std::move(texture);
        return S_OK;
    }

    // Several colour targets (up to 8) and the depth of `depth` (null: none)
    // for the draws that follow; set_target returns to one.
    void set_targets(D3D12Texture* const* colors, UINT count, D3D12Texture* depth) noexcept override {
        if (!accept_resource(depth)) return;
        for (UINT index = 0; index < (std::min)(count, UINT(extra_targets_.size()) + 1); ++index)
            if (!colors || !accept_resource(colors[index])) return;
        count = (std::min)(count, UINT(extra_targets_.size()) + 1);
        offscreen_ = count && colors[0] && colors[0]->rtv.ptr ? colors[0] : nullptr;
        // The world target with more targets (the AO ambient share) stays the world's.
        offscreen_world_ = offscreen_ && offscreen_ == world_target_;
        target_count_ = offscreen_ ? count : 1;
        // A null entry after the first binds a null view (the shader's writes
        // there are discarded, as an unbound D3D11 slot), e.g. an unbound
        // shadow history beside the AO ambient share.
        for (UINT index = 1; index < target_count_; ++index) extra_targets_[index - 1] = colors[index] && colors[index]->rtv.ptr ? colors[index] : nullptr;
        depth_override_ = true;
        depth_owner_ = depth && depth->dsv.ptr ? depth : nullptr;
        depth_target_ = nullptr;
    }

    // Any target's colour, cleared now (the bound targets are not changed).
    void clear_texture(D3D12Texture& target, const float color[4], UINT mip = 0) noexcept override {
        if (!accept_resource(&target)) return;
        ID3D12GraphicsCommandList* list = target.rtv.ptr ? open_target() : open_list();
        if (!list) return;
        if (mip > 0) {  // a storage texture's level
            const D3D12_CPU_DESCRIPTOR_HANDLE view = level_uav(target, mip);
            const std::uint32_t index = view.ptr ? ring(1) : UINT32_MAX;
            if (index == UINT32_MAX) return;  // no such level, or named by ring
            core_.device()->CopyDescriptorsSimple(1, core_.views().cpu(index), view, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            transition(list, target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if (g_hazards) { g_hazards->command("clear_texture"); g_hazards->access(target.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, "cleared level"); }
            list->ClearUnorderedAccessViewFloat(core_.views().gpu(index), view, target.resource.Get(), color, 0, nullptr);
            target.written = true;
            return;
        }
        if (target.rtv.ptr) {
            transition(list, target, D3D12_RESOURCE_STATE_RENDER_TARGET);
            if (g_hazards) { g_hazards->command("clear_texture"); g_hazards->access(target.resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, true, "cleared colour"); }
            list->ClearRenderTargetView(target.rtv, color, 0, nullptr);
        } else if (target.uav.ptr) {
            const std::uint32_t index = ring(1);
            if (index == UINT32_MAX) return; // named by ring
            core_.device()->CopyDescriptorsSimple(1, core_.views().cpu(index), target.uav, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            transition(list, target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if (g_hazards) { g_hazards->command("clear_texture"); g_hazards->access(target.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, "cleared"); }
            list->ClearUnorderedAccessViewFloat(core_.views().gpu(index), target.uav, target.resource.Get(), color, 0, nullptr);
            target.written = true;
        }
    }

    void clear_texture_uint(D3D12Texture& target, const UINT values[4]) noexcept override {
        if (!accept_resource(&target)) return;
        ID3D12GraphicsCommandList* list = open_list();
        if (!list || !target.uav.ptr) return;
        const std::uint32_t index = ring(1);
        if (index == UINT32_MAX) return;  // named by ring
        core_.device()->CopyDescriptorsSimple(1, core_.views().cpu(index), target.uav, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        transition(list, target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (g_hazards) { g_hazards->command("clear_texture_uint"); g_hazards->access(target.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, "cleared"); }
        list->ClearUnorderedAccessViewUint(core_.views().gpu(index), target.uav, target.resource.Get(), values, 0, nullptr);
        target.written = true;
    }

    // Work a library records itself (NGX): the uses moved to their states,
    // then the executor's heaps and root signature bound again and its
    // cached list state forgotten (the library set its own).
    HRESULT record_external(const char* name, const ExternalUse* uses, UINT count, const ExternalRecord& record) override {
        constexpr UINT kMaximum = 8;
        if (count > kMaximum || (count && !uses) || !record) return E_INVALIDARG;
        // Validate the complete set before opening a list or transitioning any
        // resource. A surviving snapshot must never reach NGX/FSR on a new device.
        for (UINT index = 0; index < count; ++index) {
            const D3D12Texture* texture = uses[index].texture;
            if (!texture || (uses[index].depth && !texture->depth)) return E_INVALIDARG;
            if (texture->owner != resource_owner_ || !texture->resource) return DXGI_ERROR_DEVICE_RESET;
        }
        ID3D12GraphicsCommandList* list = open_list();
        if (!list) return E_FAIL;
        ID3D12Resource* resources[kMaximum]{};
        if (g_hazards) g_hazards->command(name);
        for (UINT index = 0; index < count; ++index) {
            D3D12Texture* texture = uses[index].texture;
            const D3D12_RESOURCE_STATES state = uses[index].written ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : uses[index].depth ? kDepthRead : kShaderRead;
            if (uses[index].depth) transition_depth(list, *texture, state);
            else transition(list, *texture, state);
            resources[index] = uses[index].depth ? texture->depth.Get() : texture->resource.Get();
            if (g_hazards) g_hazards->access(resources[index], state, uses[index].written, name);
        }
        const HRESULT hr = record(list, resources);
        core_.bind(list);
        list_state_ = {};
        slots_dirty_ = true;
        for (UINT index = 0; index < count; ++index)
            if (uses[index].written) uses[index].texture->written = true;
        return hr;
    }
    ID3D12Device* device() noexcept override { return core_.device(); }
    IDXGIAdapter* adapter() noexcept override { return core_.adapter(); }
    // Not Core::wait_idle: NGX calls this inside record_external, with the
    // frame's list open (a new fence value there retired the frame's own
    // ring memory and releases early: the TDR of the first DLSS mode change).
    void wait_idle() noexcept override {
        if (!core_.wait_submitted()) fail(E_FAIL, "the GPU did not finish submitted work within 20 s");
    }

    // One target's depth into another's of the same size and format (stream order).
    HRESULT copy_depth(D3D12Texture& destination, D3D12Texture& source) override {
        if (!accept_resource(&destination) || !accept_resource(&source)) return DXGI_ERROR_DEVICE_RESET;
        ID3D12GraphicsCommandList* list = open_list();
        if (!list) return E_FAIL;
        if (!destination.depth || !source.depth || destination.depth_format != source.depth_format ||
            destination.width != source.width || destination.height != source.height)
            return E_INVALIDARG;
        transition_depth(list, destination, D3D12_RESOURCE_STATE_COPY_DEST);
        transition_depth(list, source, D3D12_RESOURCE_STATE_COPY_SOURCE);
        if (g_hazards) {
            g_hazards->command("copy_depth");
            g_hazards->access(destination.depth.Get(), D3D12_RESOURCE_STATE_COPY_DEST, true, "copy destination");
            g_hazards->access(source.depth.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, false, "copy source");
        }
        list->CopyResource(destination.depth.Get(), source.depth.Get());
        transition_depth(list, source, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        transition_depth(list, destination, kDepthRead);
        return S_OK;
    }

    void set_depth_resource(UINT slot, const D3D12Texture* target) noexcept override {
        if (!accept_resource(target)) return;
        if (slot >= kD3D11ResourceSlots) return;
        const bool readable = target && target->depth_srv.ptr;
        resources_[slot] = {readable ? target->depth_srv : D3D12_CPU_DESCRIPTOR_HANDLE{}, nullptr,
                            readable ? const_cast<D3D12Texture*>(target) : nullptr, true};
        slots_dirty_ = true;
    }

    // A depth array (shadow maps): D16/D32 readable as Texture2DArray,
    // one depth view per slice.
    HRESULT create_depth_array(UINT side, UINT slices, owned::D3D12TextureRef& out, DXGI_FORMAT depth_format, bool plain) override {
        out.reset();
        if (!failure_.empty()) return E_FAIL;
        if (depth_format != DXGI_FORMAT_D16_UNORM && depth_format != DXGI_FORMAT_D32_FLOAT) return E_INVALIDARG;
        const bool d16 = depth_format == DXGI_FORMAT_D16_UNORM;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = side;
        desc.Height = side;
        desc.DepthOrArraySize = UINT16(slices);
        desc.MipLevels = 1;
        desc.Format = d16 ? DXGI_FORMAT_R16_TYPELESS : DXGI_FORMAT_R32_TYPELESS;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = depth_format;
        clear.DepthStencil = {1.0f, 0};
        auto texture = std::make_shared<D3D12Texture>();
        const auto failed = [&](HRESULT hr, const char* what) {
            report_once(texture_failures_, "depth array creation", hresult_text(what, hr));
            return hr;
        };
        HRESULT hr = core_.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                                                             IID_PPV_ARGS(&texture->resource));
        if (FAILED(hr)) return failed(hr, "CreateCommittedResource (depth array)");
        texture->owner = resource_owner_;
        texture->width = texture->height = side;
        texture->slices = slices;
        texture->format = d16 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R32_FLOAT;
        texture->depth_format = depth_format;
        texture->state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        texture->depth_array = true;
        texture->srv = core_.staging().allocate();
        if (!texture->srv.ptr) return failed(E_OUTOFMEMORY, "staging descriptor block");
        D3D12_SHADER_RESOURCE_VIEW_DESC read{};
        read.Format = texture->format;
        if (plain && slices == 1) {
            read.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            read.Texture2D.MipLevels = 1;
        } else {
            read.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            read.Texture2DArray.MipLevels = 1;
            read.Texture2DArray.ArraySize = slices;
        }
        read.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        core_.device()->CreateShaderResourceView(texture->resource.Get(), &read, texture->srv);
        D3D12_DESCRIPTOR_HEAP_DESC views{};
        views.NumDescriptors = slices;
        views.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        if (FAILED(hr = core_.device()->CreateDescriptorHeap(&views, IID_PPV_ARGS(&texture->dsv_heap))))
            return failed(hr, "CreateDescriptorHeap (depth array)");
        texture->dsv = texture->dsv_heap->GetCPUDescriptorHandleForHeapStart();
        const UINT stride = core_.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        for (UINT slice = 0; slice < slices; ++slice) {
            D3D12_DEPTH_STENCIL_VIEW_DESC view{};
            view.Format = depth_format;
            view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.FirstArraySlice = slice;
            view.Texture2DArray.ArraySize = 1;
            core_.device()->CreateDepthStencilView(texture->resource.Get(), &view, D3D12_CPU_DESCRIPTOR_HANDLE{texture->dsv.ptr + SIZE_T(slice) * stride});
        }
        hazard_created(*texture, "depth array");
        out = std::move(texture);
        return S_OK;
    }

    // Depth-only draws into one slice of a depth array, in the viewport rect
    // (also the scissor); set_target or set_targets returns to colour.
    void set_depth_target(D3D12Texture* array, UINT slice, const RECT& viewport, D3D12Texture* color) noexcept override {
        if (!accept_resource(array) || !accept_resource(color)) return;
        depth_target_ = array && array->depth_array && slice < array->slices ? array : nullptr;
        depth_target_color_ = depth_target_ && color && color->rtv.ptr ? color : nullptr;
        depth_target_slice_ = slice;
        depth_target_rect_ = viewport;
        offscreen_world_ = false;
    }

    // A whole slice, or just one shared-atlas cell, cleared to depth now.
    void clear_depth_slice(D3D12Texture& array, UINT slice, float depth, const RECT* rect) noexcept override {
        if (!accept_resource(&array)) return;
        ID3D12GraphicsCommandList* list = open_target();
        if (!list || !array.depth_array || slice >= array.slices) return;
        transition(list, array, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        if (g_hazards) { g_hazards->command("clear_depth_slice"); g_hazards->access(array.resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, true, "cleared depth"); }
        list->ClearDepthStencilView(depth_slice_view(array, slice), D3D12_CLEAR_FLAG_DEPTH, depth, 0, rect ? 1 : 0, rect);
    }

    std::uint32_t bindless_index(D3D12Texture& texture) noexcept override {
        if (!accept_resource(&texture)) return UINT32_MAX;
        if (texture.index != UINT32_MAX) return texture.index;
        const std::uint32_t index = core_.views().allocate();
        if (index == UINT32_MAX) {
            if (!bindless_full_) omit("draws beyond the bindless heap (it grows next frame)", false);
            bindless_full_ = true;
            return index;
        }
        core_.device()->CopyDescriptorsSimple(1, core_.views().persistent_cpu(index), texture.srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        core_.views().publish(core_.device(), index);
        texture.index = index;
        if (g_hazards) {
            g_hazards->index_taken(index, core_.completed());
            g_hazards->bindless(texture.resource.Get());
            if (gpu_reports_) gpu_report_live(index);
        }
        return index;
    }

    void set_target(D3D12Texture* target, bool world = false) noexcept override {
        if (!accept_resource(target)) return;
        offscreen_ = target && target->rtv.ptr ? target : nullptr;
        offscreen_world_ = offscreen_ && world;
        if (offscreen_world_) world_target_ = offscreen_;
        target_count_ = 1;
        depth_override_ = false;
        depth_owner_ = nullptr;
        depth_target_ = nullptr;
    }

    void clear_target(const float* color, bool depth) noexcept override {
        ID3D12GraphicsCommandList* list = open_target();
        if (!list || !offscreen_) return;
        transition(list, *offscreen_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        if (g_hazards) { g_hazards->command("clear_target"); g_hazards->access(offscreen_->resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, true, "cleared colour"); }
        // The world target keeps the marker colour once a world pass was omitted.
        if (color) list->ClearRenderTargetView(offscreen_->rtv, offscreen_world_ && world_omitted_ ? kWorldMarker : color, 0, nullptr);
        if (depth && offscreen_->dsv.ptr) {
            transition_depth(list, *offscreen_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            if (g_hazards) g_hazards->access(offscreen_->depth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, true, "cleared depth");
            list->ClearDepthStencilView(offscreen_->dsv, depth_clear_flags(*offscreen_), 1.0f, 0, 0, nullptr);
        }
    }

    void clear_depth_of(D3D12Texture& target, float depth, UINT8 stencil) noexcept override {
        if (!accept_resource(&target)) return;
        ID3D12GraphicsCommandList* list = open_target();
        if (!list || !target.dsv.ptr) return;
        transition_depth(list, target, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        if (g_hazards) {
            g_hazards->command("clear_depth_of");
            g_hazards->access(target.depth ? target.depth.Get() : target.resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, true, "cleared depth");
        }
        list->ClearDepthStencilView(target.dsv, depth_clear_flags(target), depth, stencil, 0, nullptr);
    }

    HRESULT draw_ui(const owned::D3D12UiPass& pass) override {
        if (pass.draw_count && !pass.draws) return E_INVALIDARG;
        for (std::size_t index = 0; index < pass.draw_count; ++index) {
            const D3D12Texture* texture = pass.draws[index].texture;
            if (texture && (texture->owner != resource_owner_ || !texture->resource)) return DXGI_ERROR_DEVICE_RESET;
        }
        ID3D12GraphicsCommandList* list = open_target();
        if (!list) return failure_.empty() ? S_OK : E_FAIL;  // minimized: nothing visible
        if (!pass.draw_count || !pass.vertex_bytes) return S_OK;
        std::string reason;
        UploadAllocation vertices, constants;
        if (!upload_push(pass.vertices, pass.vertex_bytes, 16, vertices, reason)) return fail(E_OUTOFMEMORY, reason);
        const UINT block = (pass.constant_block_bytes + 255u) & ~255u;
        if (!upload_allocate(std::uint64_t(block) * pass.constant_blocks, 256, constants, reason)) return fail(E_OUTOFMEMORY, reason);
        for (UINT index = 0; index < pass.constant_blocks; ++index)
            std::memcpy(constants.cpu + std::size_t(index) * block,
                        static_cast<const std::uint8_t*>(pass.constants) + std::size_t(index) * pass.constant_block_bytes,
                        pass.constant_block_bytes);
        // Every texture the pass reads is shader-readable before its first draw.
        for (std::size_t index = 0; index < pass.draw_count; ++index)
            if (pass.draws[index].texture) transition(list, const_cast<D3D12Texture&>(*pass.draws[index].texture), kShaderRead);
        bind_target(list, false);
        const D3D12_VERTEX_BUFFER_VIEW view{vertices.gpu, pass.vertex_bytes, pass.vertex_stride};
        list->IASetVertexBuffers(0, 1, &view);
        const D3D12_RECT full{0, 0, LONG(target_width()), LONG(target_height())};
        for (std::size_t index = 0; index < pass.draw_count; ++index) {
            const owned::D3D12UiDraw& draw = pass.draws[index];
            // Every skipped quad is named (omit: the session total, and the
            // frame's interface line at present).
            if (depth_target_) { omit("interface quads (a depth-only target was bound)", false); continue; }
            ID3D12PipelineState* pipeline = draw.fixed_state ? fixed_pipeline(draw) : ui_pipeline(draw.program, draw.blend);
            if (!pipeline) { omit("interface quads (pipeline not ready or failed)", false); continue; }
            const std::uint32_t sampler = sampler_index(draw.sampler);
            if (!draw.texture || sampler == UINT32_MAX) { omit("interface quads (no texture or sampler)", false); continue; }
            // t0: the texture's view copied into the ring (the table's other
            // slots are volatile and unread by the UI programs).
            const std::uint32_t table = ring(1);
            if (table == UINT32_MAX) { omit("interface quads (beyond the descriptor ring)", false); continue; }  // it grows before the next frame
            core_.device()->CopyDescriptorsSimple(1, core_.views().cpu(table), draw.texture->srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            set_pipeline(list, pipeline);
            set_topology(list, draw.fixed_state ? static_cast<D3D_PRIMITIVE_TOPOLOGY>(draw.fixed_topology) :
                draw.triangle_list ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            list->SetGraphicsRootConstantBufferView(kRootD3D11Constants, constants.gpu + std::uint64_t(draw.constants) * block);
            list->SetGraphicsRootDescriptorTable(kRootD3D11Resources, core_.views().gpu(table));
            list->SetGraphicsRootDescriptorTable(kRootD3D11Samplers, core_.samplers().gpu(sampler));
            const D3D12_RECT scissor = draw.scissor ? D3D12_RECT{draw.scissor_rect.left, draw.scissor_rect.top, draw.scissor_rect.right,
                                                                 draw.scissor_rect.bottom}
                                                    : full;
            set_scissor(list, scissor);
            if (g_hazards) {
                g_hazards->command("ui draw");
                g_hazards->access(draw.texture->resource.Get(), kShaderRead, false, "t0");
                hazard_targets(false);
            }
            list->DrawInstanced(draw.vertex_count, 1, draw.first_vertex, 0);
            drawn_ = true;
            if (offscreen_world_) world_drawn_ = true;
        }
        return S_OK;
    }

    void clear(const float color[4]) noexcept override {
        if (offscreen_) {
            if (offscreen_world_ && world_drawn_) count("world target colour clears after a world draw");
            clear_target(color, false);
            return;
        }
        if (drawn_) return;
        std::memcpy(background_, color, sizeof(background_));
        if (target_ && !world_omitted_) list_->ClearRenderTargetView(back_rtv(list_), background_, 0, nullptr);
    }

    HRESULT read_back_buffer(std::vector<std::uint8_t>& pixels, UINT& width, UINT& height) override {
        if (!open_target()) return failure_.empty() ? S_FALSE : E_FAIL;
        if (D3D12Texture* const image = frame_image()) return read_bgra(image->resource.Get(), image->state, pixels, width, height);
        if (screen_) return read_bgra(screen_->resource.Get(), screen_->state, pixels, width, height);
        return read_bgra(presenter_.back_buffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, pixels, width, height);
    }

    void set_frame_image(bool wanted) noexcept override {
        frame_image_wanted_ = wanted;
        if (!wanted && !frame_image_active_) frame_image_.reset();  // its release waits for the frames that read it
    }

    void composite_frame_image() noexcept override {
        D3D12Texture* const image = frame_image();
        if (!image || !list_) return;
        if (g_hazards) g_hazards->command("frame image composite");
        transition(list_, *image, D3D12_RESOURCE_STATE_COPY_SOURCE);
        if (g_hazards) g_hazards->access(image->resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, false, "copy source");
        if (fg_on_) {
            // Frame generation: the real screen image of this slot, presented by the pacer.
            screen_ = real_[fg_slot_].get();
            transition(list_, *screen_, D3D12_RESOURCE_STATE_COPY_DEST);
            if (g_hazards) g_hazards->access(screen_->resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, true, "copy destination");
            list_->CopyResource(screen_->resource.Get(), image->resource.Get());
            transition(list_, *screen_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        } else {
            ID3D12Resource* const back = presenter_.back_buffer();
            D3D12_RESOURCE_BARRIER to_dest = transition_barrier(back, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
            list_->ResourceBarrier(1, &to_dest);
            list_->CopyResource(back, image->resource.Get());
            D3D12_RESOURCE_BARRIER to_target = transition_barrier(back, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
            list_->ResourceBarrier(1, &to_target);
        }
        // Later back-buffer draws, clears and reads go to the swap-chain
        // buffer; the frame's whole-target clear no longer applies.
        frame_image_active_ = false;
        drawn_ = true;
    }

    // ---- Frame generation presentation (DLSS-G, app paced) ----
    void set_frame_generation(int mode) noexcept override { fg_wanted_ = mode >= 0 && mode <= 2 ? mode : 0; }
    bool frame_generation_active(int mode) const noexcept override {
        return (mode == 1 ? fg_on_ : mode == 2 ? fsr_on_ : false) && frame_image_active_;
    }
    // ---- FSR frame generation (FidelityFX proxy swap chain) ----
    HRESULT fsr_prepare(ID3D12GraphicsCommandList* list, ID3D12Resource* depth, ID3D12Resource* motion, const FsrPrepare& in) noexcept override {
        if (!fsr_on_ || !fsr_.created()) {
            remote::host_log("d3d12: FSR frame generation prepare without %s", fsr_on_ ? "a context" : "the proxy swap chain");
            return E_FAIL;
        }
        d3d12::FsrFrameGeneration::Prepare p;
        p.render_w = in.render_w; p.render_h = in.render_h;
        p.jitter_x = in.jitter_x; p.jitter_y = in.jitter_y;
        p.frame_ms = in.frame_ms; p.reset = in.reset;
        p.near_plane = in.near_plane; p.far_plane = in.far_plane; p.fov_y = in.fov_y;
        std::memcpy(p.position, in.position, sizeof(p.position)); std::memcpy(p.up, in.up, sizeof(p.up));
        std::memcpy(p.right, in.right, sizeof(p.right)); std::memcpy(p.forward, in.forward, sizeof(p.forward));
        std::string error;
        if (!fsr_.prepare(list, depth, motion, p, error)) { remote::host_log("d3d12: %s", error.c_str()); return E_FAIL; }
        return S_OK;
    }
    void set_fsr_hudless(D3D12Texture* hudless) noexcept override { if (accept_resource(hudless)) fsr_hudless_ = fsr_on_ ? hudless : nullptr; }
    std::uint64_t fsr_vram() const noexcept override { return fsr_.vram(); }
    D3D12Texture* frame_image_texture() noexcept override { return frame_image(); }
    D3D12Texture* generated_image() noexcept override { return fg_on_ ? generated_[fg_slot_].get() : nullptr; }
    void select_generated_screen() noexcept override {
        if (!fg_on_ || !list_ || frame_image_active_) return;  // after composite_frame_image only
        screen_ = generated_[fg_slot_].get();
        generated_drawn_ = true;
        if (g_hazards) g_hazards->command("frame generation generated screen");
    }
    FrameGenerationStats frame_generation_stats() const noexcept override {
        FrameGenerationStats out;
        if (fsr_on_) {
            // Generated frames: the proxy's interpolation dispatches (its
            // callback runs once per generated frame) since FSR came on; ours
            // are the real frames. DXGI's PresentCount on the proxy stood
            // still for whole runs while generating (941df5ae, 8b6ba2b4,
            // 41d4f805), so it is not used. Never assumed 2x.
            out.active = true;
            out.real_presents = fsr_real_;
            const std::uint64_t dispatches = d3d12::FsrFrameGeneration::dispatches();
            out.generated_presents = dispatches > fsr_dispatch_base_ ? dispatches - fsr_dispatch_base_ : 0;
            return out;
        }
        const d3d12::Pacer::Stats stats = pacer_.running() ? pacer_.stats() : d3d12::Pacer::Stats{};
        out.active = fg_on_;
        out.generated_presents = stats.generated;
        out.real_presents = stats.real;
        out.real_interval_ms = stats.real_interval_ms;
        return out;
    }

    HRESULT read_texture(D3D12Texture& texture, std::vector<std::uint8_t>& pixels, UINT& width, UINT& height) override {
        if (!accept_resource(&texture)) { pixels.clear(); width = height = 0; return DXGI_ERROR_DEVICE_RESET; }
        return read_bgra(texture.resource.Get(), texture.state, pixels, width, height);
    }

    HRESULT read_bgra(ID3D12Resource* source_resource, D3D12_RESOURCE_STATES source_state,
                      std::vector<std::uint8_t>& pixels, UINT& width, UINT& height) {
        width = height = 0;
        const D3D12_RESOURCE_DESC desc = source_resource->GetDesc();
        if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || desc.SampleDesc.Count != 1 || desc.DepthOrArraySize != 1)
            return E_NOTIMPL;
        ID3D12GraphicsCommandList* list = open_list();
        if (!list) return E_FAIL;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 bytes = 0, row_bytes = 0;
        core_.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, &row_bytes, &bytes);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = bytes;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> readback;
        HRESULT hr = core_.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                             IID_PPV_ARGS(&readback));
        if (FAILED(hr)) return hr;
        if (source_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
            const auto barrier = transition_barrier(source_resource, source_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->ResourceBarrier(1, &barrier);
        }
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = readback.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = source_resource;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        if (source_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
            const auto barrier = transition_barrier(source_resource, D3D12_RESOURCE_STATE_COPY_SOURCE, source_state);
            list->ResourceBarrier(1, &barrier);
        }
        // Submit what the frame recorded so far and wait for it; the frame
        // continues on a new list with the source's resource state restored.
        std::string reason;
        resolve_stamps();
        const std::uint64_t value = core_.end_frame(reason);
        list_ = nullptr;
        if (!value) return fail(E_FAIL, reason);
        upload_end_frame(value);
        if (!core_.wait_idle()) return fail(E_FAIL, "the GPU did not finish the image readback");
        width = UINT(desc.Width);
        height = desc.Height;
        void* mapped = nullptr;
        const D3D12_RANGE read{0, SIZE_T(bytes)};
        if (FAILED(hr = readback->Map(0, &read, &mapped))) return hr;
        try {
            pixels.resize(std::size_t(row_bytes) * height);
            for (UINT y = 0; y < height; ++y)
                std::memcpy(pixels.data() + std::size_t(y) * row_bytes,
                            static_cast<const std::uint8_t*>(mapped) + footprint.Offset + std::size_t(y) * footprint.Footprint.RowPitch,
                            std::size_t(row_bytes));
        } catch (...) {
            hr = E_OUTOFMEMORY;
        }
        const D3D12_RANGE written{0, 0};
        readback->Unmap(0, &written);
        if (!open_list()) return E_FAIL;
        return hr;
    }

    void omit(const char* name, bool world) noexcept override try {
        if (world) {
            world_omitted_ = true;
            // Taken but untouched this frame: show the marker right away (in the
            // world target while it is bound and nothing was drawn there yet).
            if (offscreen_world_) {
                if (!world_drawn_ && open_target()) {
                    transition(list_, *offscreen_, D3D12_RESOURCE_STATE_RENDER_TARGET);
                    list_->ClearRenderTargetView(offscreen_->rtv, kWorldMarker, 0, nullptr);
                }
            } else if (target_ && !drawn_) list_->ClearRenderTargetView(back_rtv(list_), kWorldMarker, 0, nullptr);
        }
        // Interface omissions also go into this frame's line (present).
        if (std::strstr(name, "interface")) {
            ++frame_interface_omitted_;
            if (frame_interface_names_.find(name) == std::string::npos) {
                if (!frame_interface_names_.empty()) frame_interface_names_ += ", ";
                frame_interface_names_ += name;
            }
        }
        for (auto& entry : omitted_)
            if (entry.first == name) { ++entry.second; return; }
        omitted_.emplace_back(name, 1);
        remote::host_log("d3d12: %s not on the Direct3D 12 backend yet: omitted%s", name,
                         world ? " (the world shows the marker colour)" : "");
    } catch (...) {
    }

    // GPU pass breadcrumbs: each pass change writes its serial into a small
    // readback buffer when the GPU reaches it (MARKER_IN) and the previous
    // serial once the work before it has finished (MARKER_OUT). After a
    // device removal the pair names the pass the GPU was inside (fail()).
    // DRED kept no marker text on this driver, so the names stay on the CPU.
    void marker(const char* name) noexcept override {
        if (g_hazards && name) g_hazards->pass(name);
        if (!name || name == last_marker_ || !list_ || breadcrumbs_failed_) return;
        if (!breadcrumb_buffer_) {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = 256;
            desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            void* mapped = nullptr;
            if (FAILED(core_.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                               IID_PPV_ARGS(&breadcrumb_buffer_))) ||
                FAILED(breadcrumb_buffer_->Map(0, nullptr, &mapped))) {
                breadcrumb_buffer_.Reset();
                breadcrumbs_failed_ = true;
                return;
            }
            breadcrumb_values_ = static_cast<const volatile std::uint32_t*>(mapped);
        }
        if (list_ != breadcrumb_list_) {
            breadcrumb_list2_.Reset();
            breadcrumb_list_ = list_;
            if (FAILED(list_->QueryInterface(IID_PPV_ARGS(&breadcrumb_list2_)))) { breadcrumbs_failed_ = true; return; }
        }
        last_marker_ = name;
        const std::uint32_t serial = ++breadcrumb_serial_;
        breadcrumb_names_[serial % breadcrumb_names_.size()] = name;
        const D3D12_GPU_VIRTUAL_ADDRESS address = breadcrumb_buffer_->GetGPUVirtualAddress();
        const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER values[2] = {{address + 4, serial - 1}, {address, serial}};
        const D3D12_WRITEBUFFERIMMEDIATE_MODE modes[2] = {D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT, D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN};
        breadcrumb_list2_->WriteBufferImmediate(2, values, modes);
    }
    std::string breadcrumb_report() const {
        if (!breadcrumb_values_) return {};
        const std::uint32_t started = breadcrumb_values_[0], finished = breadcrumb_values_[1];
        const auto name = [&](std::uint32_t serial) -> const char* {
            if (!serial || serial > breadcrumb_serial_ || breadcrumb_serial_ - serial >= breadcrumb_names_.size()) return "?";
            const char* text = breadcrumb_names_[serial % breadcrumb_names_.size()];
            return text ? text : "?";
        };
        char text[320];
        std::snprintf(text, sizeof(text), "GPU pass breadcrumbs: last pass reached '%s' (#%u), work finished up to before '%s' (#%u); %u recorded",
                      name(started), started, name(finished + 1), finished + 1, breadcrumb_serial_);
        return text;
    }

    void count(const char* name) noexcept override try {
        for (auto& entry : counted_)
            if (entry.first == name) { ++entry.second; return; }
        counted_.emplace_back(name, 1);
    } catch (...) {
    }

    HRESULT present(UINT sync_interval, int frame_limit = 0) override {
        if (!failure_.empty()) return E_FAIL;
        ID3D12GraphicsCommandList* list = open_target();  // a frame with no draws still shows its clear
        if (!list && !failure_.empty()) return E_FAIL;
        const bool shown = target_;
        if (shown) {
            composite_frame_image();  // still the frame image: it is the presented image as drawn
            if (fsr_on_ && fsr_hudless_) {
                // FidelityFX reads the HUD-less image in shader-read state at Present.
                transition(list_, *fsr_hudless_, kShaderRead);
                if (g_hazards) g_hazards->access(fsr_hudless_->resource.Get(), kShaderRead, false, "FSR frame generation HUD-less");
            }
            if (fg_on_) {
                // The pacer's copy sources.
                transition(list_, *real_[fg_slot_], D3D12_RESOURCE_STATE_COPY_SOURCE);
                if (generated_drawn_) transition(list_, *generated_[fg_slot_], D3D12_RESOURCE_STATE_COPY_SOURCE);
            } else {
                D3D12_RESOURCE_BARRIER barrier =
                    transition_barrier(presenter_.back_buffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
                list_->ResourceBarrier(1, &barrier);
            }
        }
        HRESULT hr = S_OK;
        std::uint64_t submitted = 0;
        if (list_) {
            if (g_hazards && gpu_reports_) gpu_report_frame_end();
            std::string reason;
            resolve_stamps();
            const std::uint64_t value = core_.end_frame(reason);
            list_ = nullptr;
            if (!value) return fail(E_FAIL, reason);
            upload_end_frame(value);
            submitted = value;
        }
        // One line per frame that omitted interface work (the first 200 such
        // frames, then every 100th), so a flicker shows which frames and why.
        if (frame_interface_omitted_) {
            ++interface_omitted_frames_;
            if (interface_omitted_frames_ <= 200 || interface_omitted_frames_ % 100 == 0)
                remote::host_log("d3d12: frame %llu omitted interface work x%u (%llu such frames): %s",
                                 static_cast<unsigned long long>(frame_serial_), frame_interface_omitted_,
                                 static_cast<unsigned long long>(interface_omitted_frames_), frame_interface_names_.c_str());
            frame_interface_omitted_ = 0;
            frame_interface_names_.clear();
        }
        target_ = drawn_ = world_omitted_ = frame_image_active_ = false;
        offscreen_ = nullptr;
        world_target_ = nullptr;
        offscreen_world_ = world_drawn_ = false;
        std::memcpy(background_, kBlack, sizeof(background_));
        // Reflex: the frame's render submission ends before its PRESENT
        // markers (nvapi.h: RENDERSUBMIT_END comes before Present).
        reflex::marker(reflex::Marker::render_end, reflex::frame());
        if (shown && fg_on_) {
            presentation_limiter_.wait(0, nullptr, &remote::host_log);  // the pacer owns both output presents
            // Reflex's per-rendered-frame PRESENT markers bracket the
            // hand-over after render submission. Rendering and final copies
            // use the same normal queue, with no out-of-band queue/markers.
            reflex::marker(reflex::Marker::present_start, reflex::frame());
            pacer_.submit(presenter_, generated_drawn_ ? generated_[fg_slot_]->resource.Get() : nullptr, real_[fg_slot_]->resource.Get(),
                          submitted, fg_slot_, reflex::frame_start(), reflex::frame_taken(), sync_interval, frame_limit);
            reflex::marker(reflex::Marker::present_end, reflex::frame());
        } else if (shown) {
            if (fsr_on_) {
                // The configure with this frame's id goes right before Present
                // (the proxy dispatches the interpolation inside it).
                std::string error;
                if (!fsr_.configure(presenter_.swap_chain(), fsr_hudless_ && (frame_limit <= 0 || frame_limit >= 2) ? fsr_hudless_->resource.Get() : nullptr, error)) {
                    std::snprintf(fg_failure_[2], sizeof(fg_failure_[2]), "%s", error.c_str());
                    remote::host_log("d3d12: %s; FSR frame generation off", fg_failure_[2]);
                    fg_wanted_ = 0;  // the plain swap chain from the next frame
                }
            }
            // The AMD proxy owns its final presents: its documented producer
            // limit is half the desired output rate while interpolating.
            presentation_limiter_.wait(frame_limit > 0 && fsr_on_ && fsr_.generating()
                ? (std::max)(1, frame_limit / 2) : frame_limit, nullptr, &remote::host_log);
            reflex::marker(reflex::Marker::present_start, reflex::frame());
            hr = presenter_.present(sync_interval);
            reflex::marker(reflex::Marker::present_end, reflex::frame());
            if (FAILED(hr)) return fail(hr, hresult_text("IDXGISwapChain::Present", hr));
            // FSR: the proxy shows the real image after the generated one (this
            // measures its Present call, not that later present).
            reflex::real_presented(reflex::frame_start());
            if (fsr_on_) {
                ++fsr_real_;
                log_fsr_rates();
            }
        } else if (fsr_on_) {
            std::string error;
            fsr_.configure(presenter_.swap_chain(), nullptr, error);  // nothing presented: no generation from this frame
        }
        fsr_hudless_ = nullptr;
        screen_ = nullptr;
        generated_drawn_ = false;
        // Pipelines first used in play are written once all are built (at
        // most every 30 s), so a session that crashes still leaves them for
        // the next start.
        if (records_dirty_ && GetTickCount64() - records_saved_tick_ >= 30000 && pipelines_.idle()) save_pipelines("in play");
        return hr;
    }

    HRESULT resize(UINT width, UINT height) override {
        if (!failure_.empty()) return E_FAIL;
        // What the frame recorded so far goes first. A taken back buffer is
        // presented as it is: its frame latency wait must meet a Present.
        if (list_ || target_) {
            const HRESULT hr = present(0);
            if (FAILED(hr)) return hr;
        }
        std::string reason;
        if (!presenter_.resize(core_, width, height, reason)) return fail(E_FAIL, reason);
        if (!create_depth(reason)) return fail(E_FAIL, reason);
        return S_OK;
    }

    std::string omitted() const override {
        std::string text;
        for (const auto& entry : omitted_) {
            if (!text.empty()) text += ", ";
            text += entry.first + " x" + std::to_string(entry.second);
        }
        return text;
    }

    owned::OverlayRenderer& overlay_renderer() noexcept override { return *this; }
    owned::ProfileTimestamps& profile_timestamps() noexcept override { return *this; }

    // ---- ProfileTimestamps (the renderer's profiler on this backend) --------
    // One timestamp query heap and readback buffer per frame slot, sized by
    // demand; a list's stamps resolve when it is submitted and are read once
    // its fence completed. Nothing is written unless the profiler asks.
    std::uint64_t stamp() noexcept override {
        ID3D12GraphicsCommandList* list = open_list();
        if (!list) return 0;
        StampSlot& slot = stamp_slots_[core_.frame_slot()];
        ++slot.wanted;
        if (slot.used >= slot.capacity) return 0;  // lost; the slot grows when it comes round again
        list->EndQuery(slot.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot.used);
        const std::uint64_t serial = slot.first + slot.used++;
        next_stamp_ = serial + 1;
        return serial;
    }
    bool value(std::uint64_t serial, std::uint64_t& ticks) noexcept override {
        if (stamp_history_.empty()) return false;
        auto* entry = &stamp_history_[serial % stamp_history_.size()];
        if (entry->first != serial) {
            for (auto& slot : stamp_slots_)
                if (slot.submitted && serial >= slot.first && serial < slot.first + slot.used && core_.completed() >= slot.fence)
                    harvest(slot);
            if (entry->first != serial) return false;
        }
        ticks = entry->second;
        return true;
    }
    std::uint64_t frequency() noexcept override {
        UINT64 frequency = 0;
        return SUCCEEDED(core_.queue()->GetTimestampFrequency(&frequency)) ? frequency : 0;
    }

    // ---- OverlayRenderer (Dear ImGui 1.92 renderer backend) ------------------
    bool init() override {
        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererName = "eid3d11_d3d12";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
        ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
        platform.Renderer_TextureMaxWidth = platform.Renderer_TextureMaxHeight = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        return imgui_pipeline_ != UINT32_MAX;
    }

    void shutdown() noexcept override {
        for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
            if (texture->BackendUserData) destroy_imgui_texture(texture);
        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererName = nullptr;
        io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
        ImGui::GetPlatformIO().ClearRendererHandlers();
    }

    void render(ImDrawData* data) override {
        if (!data) return;
        if (data->Textures)
            for (ImTextureData* texture : *data->Textures)
                if (texture->Status != ImTextureStatus_OK) update_imgui_texture(texture);
        if (data->DisplaySize.x <= 0.0f || data->DisplaySize.y <= 0.0f || !data->TotalVtxCount) return;
        ID3D12PipelineState* pipeline = pipelines_.get(imgui_pipeline_);
        ID3D12GraphicsCommandList* list = open_target();
        if (!list || !pipeline) return;
        std::string reason;
        UploadAllocation vertices, indices;
        const std::uint64_t vertex_bytes = std::uint64_t(data->TotalVtxCount) * sizeof(ImDrawVert);
        const std::uint64_t index_bytes = std::uint64_t(data->TotalIdxCount) * sizeof(ImDrawIdx);
        if (!upload_allocate(vertex_bytes, 16, vertices, reason) || !upload_allocate(index_bytes, 16, indices, reason)) {
            fail(E_OUTOFMEMORY, reason);
            return;
        }
        std::uint8_t* vertex_out = vertices.cpu;
        std::uint8_t* index_out = indices.cpu;
        for (const ImDrawList* draw_list : data->CmdLists) {
            std::memcpy(vertex_out, draw_list->VtxBuffer.Data, std::size_t(draw_list->VtxBuffer.Size) * sizeof(ImDrawVert));
            std::memcpy(index_out, draw_list->IdxBuffer.Data, std::size_t(draw_list->IdxBuffer.Size) * sizeof(ImDrawIdx));
            vertex_out += std::size_t(draw_list->VtxBuffer.Size) * sizeof(ImDrawVert);
            index_out += std::size_t(draw_list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        }
        for (auto& entry : imgui_textures_) transition(list, *entry.second, kShaderRead);
        bind_target(list, false);
        set_pipeline(list, pipeline);
        set_topology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const D3D12_VERTEX_BUFFER_VIEW vertex_view{vertices.gpu, UINT(vertex_bytes), sizeof(ImDrawVert)};
        const D3D12_INDEX_BUFFER_VIEW index_view{indices.gpu, UINT(index_bytes),
                                                 sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT};
        list->IASetVertexBuffers(0, 1, &vertex_view);
        bind_index_view(list, index_view);
        const float scale[2] = {2.0f / data->DisplaySize.x, -2.0f / data->DisplaySize.y};
        const float offset[2] = {-1.0f - data->DisplayPos.x * scale[0], 1.0f - data->DisplayPos.y * scale[1]};
        UINT root[kRootConstantCount]{};
        std::memcpy(&root[0], scale, sizeof(scale));
        std::memcpy(&root[2], offset, sizeof(offset));
        const ImVec2 clip_offset = data->DisplayPos, clip_scale = data->FramebufferScale;
        UINT global_vertex = 0, global_index = 0;
        for (const ImDrawList* draw_list : data->CmdLists) {
            for (const ImDrawCmd& command : draw_list->CmdBuffer) {
                if (command.UserCallback) {
                    // Reset-render-state is the only callback the overlay uses: the state above stays bound.
                    if (command.UserCallback != ImDrawCallback_ResetRenderState) command.UserCallback(draw_list, &command);
                    continue;
                }
                const D3D12_RECT scissor{LONG((command.ClipRect.x - clip_offset.x) * clip_scale.x),
                                         LONG((command.ClipRect.y - clip_offset.y) * clip_scale.y),
                                         LONG((command.ClipRect.z - clip_offset.x) * clip_scale.x),
                                         LONG((command.ClipRect.w - clip_offset.y) * clip_scale.y)};
                if (scissor.right <= scissor.left || scissor.bottom <= scissor.top) continue;
                const auto found = imgui_textures_.find(command.GetTexID());
                if (found == imgui_textures_.end()) {
                    // A texture no ImGui texture request created here: not on this backend.
                    omit("overlay image of another renderer", false);
                    continue;
                }
                const std::uint32_t index = ring(1);
                if (index == UINT32_MAX) continue;  // the ring grows before the next frame (named)
                core_.device()->CopyDescriptorsSimple(1, core_.views().cpu(index), found->second->srv,
                                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                root[4] = index;
                list->SetGraphicsRoot32BitConstants(kRootConstants, kRootConstantCount, root, 0);
                set_scissor(list, scissor);
                list->DrawIndexedInstanced(command.ElemCount, 1, command.IdxOffset + global_index, INT(command.VtxOffset + global_vertex), 0);
                drawn_ = true;
                if (offscreen_world_) world_drawn_ = true;
            }
            global_vertex += UINT(draw_list->VtxBuffer.Size);
            global_index += UINT(draw_list->IdxBuffer.Size);
        }
    }

private:
    // The list of the frame being recorded (opened on first use).
    ID3D12GraphicsCommandList* open_list() {
        if (list_) return list_;
        if (!failure_.empty()) return nullptr;
        std::string reason;
        list_ = core_.begin_frame(reason);
        if (!list_) {
            const std::string removal = core_.removal_report();
            fail(E_FAIL, removal.empty() ? reason : reason + "; " + removal);
            return nullptr;
        }
        upload_.retire(core_.completed());
        bulk_.retire(core_.completed());
        begin_stamps();
        // Demand sizing: the ring holds the frames in flight, so it doubles
        // before this frame records anything once a frame's peak times the
        // frames in flight passes 3/4 of it (or a frame ran out), not after
        // the frame that needs the room has dropped draws.
        // The bindless (persistent) region doubles the same way when over half
        // of it is in use or an index was refused.
        const std::uint32_t capacity = core_.views().transient_capacity();
        const std::uint32_t persistent = core_.views().persistent_capacity(), used = core_.views().persistent_used();
        // The ring may take everything the bindless region and the table
        // padding leave of the device's shader-visible limit (it was capped at
        // half: 500000, which neighbour-map frames outgrew, so every frame
        // dropped draws at random - the "everything flickers" report).
        const bool persistent_grows = (bindless_full_ || used * 2 > persistent) && persistent < Core::kViewDescriptorLimit / 2;
        const std::uint32_t grown_persistent = persistent_grows ? (std::min)(persistent * 2, Core::kViewDescriptorLimit / 2) : persistent;
        const std::uint32_t ring_limit = Core::kViewDescriptorLimit - grown_persistent - kD3D11ResourceSlots;
        const bool ring_grows = (ring_full_ || std::uint64_t(ring_frame_peak_) * Core::kFramesInFlight * 4 > std::uint64_t(capacity) * 3) &&
                                capacity < ring_limit;
        if (ring_grows || persistent_grows) {
            const std::uint32_t grown = ring_grows ? (std::min)(capacity * 2, ring_limit) : (std::min)(capacity, ring_limit);
            if (core_.grow_views(grown_persistent, grown, reason)) {
                core_.bind(list_);
                remote::host_log("d3d12: descriptor heap: ring %u -> %u (frame peak %u%s), bindless %u -> %u (%u in use%s)", capacity, grown,
                                 ring_frame_peak_, ring_full_ ? ", ran out: that frame omitted draws" : "", persistent, grown_persistent,
                                 used, bindless_full_ ? ", ran out: that frame omitted draws" : "");
            } else {
                remote::host_log("d3d12: descriptor ring not grown: %s", reason.c_str());
            }
        }
        ring_full_ = sampler_ring_full_ = bindless_full_ = false;
        ring_frame_peak_ = 0;
        ring_frame_taken_ = 0;
        zero_constants_ = 0;
        for (auto& constant : constants_) constant = {};  // last frame's ring addresses
        ++frame_serial_;  // last frame's tables are not rebound (tables_)
        if (g_hazards) { g_hazards->frame(frame_serial_); g_hazards->pass("frame start"); }
        list_state_ = {};  // a new list starts with no state recorded
        last_marker_ = nullptr;  // and no pass marked
        slots_dirty_ = true;
        sampler_table_valid_ = false;  // a table of an earlier frame is reclaimed with it
        frame_sampler_tables_.clear();
        if (g_hazards && gpu_reports_) gpu_report_frame_start(list_);
        return list_;
    }

    // count consecutive ring descriptors for the frame being recorded, or
    // UINT32_MAX when the ring is full (that draw is omitted; it grows next frame).
    std::uint32_t ring(UINT count) noexcept {
        const std::uint32_t index = core_.views().transient(count);
        if (index == UINT32_MAX) {
            omit("draws beyond the descriptor ring (it grows next frame)", false);  // every one, named
            ring_full_ = true;
            return index;
        }
        ring_frame_taken_ += count;
        ring_frame_peak_ = (std::max)(ring_frame_peak_, ring_frame_taken_);
        if (g_hazards)
            g_hazards->taken(Hazards::kViewRing, core_.views().gpu(index).ptr, std::uint64_t(count) * core_.views().stride(), core_.recording_value(),
                             core_.completed());
        return index;
    }
    // The upload ring (EI_RENDERER_D3D12_HAZARDS sees each allocation's frame).
    bool upload_allocate(std::uint64_t size, std::uint64_t alignment, UploadAllocation& out, std::string& reason) {
        if (!upload_.allocate(core_, size, alignment, out, reason)) return false;
        if (g_hazards) g_hazards->taken(Hazards::kUploadRing, out.gpu, size, core_.recording_value(), core_.completed());
        return true;
    }
    bool upload_push(const void* data, std::uint64_t size, std::uint64_t alignment, UploadAllocation& out, std::string& reason) {
        if (!upload_.push(core_, data, size, alignment, out, reason)) return false;
        if (g_hazards) g_hazards->taken(Hazards::kUploadRing, out.gpu, size, core_.recording_value(), core_.completed());
        return true;
    }
    // The submitted frame's upload memory; one line for a frame that added
    // per-frame upload blocks (how many, how many the render thread had to
    // create and their cost, whose writes filled them), one for a frame that
    // added bulk blocks (load geometry).
    void upload_end_frame(std::uint64_t fence_value) {
        const double mb = 1.0 / 1048576.0;
        upload_.end_frame(fence_value);
        const auto& frame = upload_.last_frame();
        if (frame.added) {
            remote::host_log("d3d12: upload blocks +%u (%u created here in %.2f ms; now %zu, %.0f MB); this frame %.1f MB in blocks "
                             "(buffer writes %.1f, texture writes %.1f, other %.1f) and %u dedicated buffers (%.1f MB)",
                             frame.added, frame.created, frame.create_ms, upload_.blocks(), double(upload_.capacity()) * mb, double(frame.bytes) * mb,
                             double(upload_buffer_bytes_) * mb, double(upload_texture_bytes_) * mb,
                             double(frame.bytes + frame.dedicated_bytes - (std::min)(frame.bytes + frame.dedicated_bytes, upload_buffer_bytes_ + upload_texture_bytes_)) * mb,
                             frame.dedicated, double(frame.dedicated_bytes) * mb);
        }
        upload_buffer_bytes_ = upload_texture_bytes_ = 0;
        bulk_.end_frame(fence_value);
        const auto& bulk = bulk_.last_frame();
        if (bulk.added)
            remote::host_log("d3d12: bulk upload blocks +%u (%u created here in %.2f ms; now %zu, %.0f MB); this frame %.1f MB of geometry "
                             "and %u dedicated buffers (%.1f MB)",
                             bulk.added, bulk.created, bulk.create_ms, bulk_.blocks(), double(bulk_.capacity()) * mb, double(bulk.bytes) * mb,
                             bulk.dedicated, double(bulk.dedicated_bytes) * mb);
    }

    // The list with the back buffer taken and cleared; null while minimized.
    ID3D12GraphicsCommandList* open_target() {
        ID3D12GraphicsCommandList* list = open_list();
        if (!list || target_) return list;
        std::string reason;
        // A mode switch and the first prepare after it, timed (one line: each
        // switch measured a ~5 s stall in host.log; this names the step).
        const bool switching = fg_wanted_ != frame_generation_mode();
        const auto now_ms = [] { LARGE_INTEGER t{}, f{}; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f); return 1000.0 * double(t.QuadPart) / double(f.QuadPart); };
        const double switch_start = switching ? now_ms() : 0.0;
        // Plain/DLSS-G share this swap chain and queue. Only entering or
        // leaving the FidelityFX proxy requires replacing the swap chain.
        if (switching) {
            switch_steps_.clear();
            remote::host_log("d3d12: frame generation switching from mode %d to %d", frame_generation_mode(), fg_wanted_);
        }
        if (switching && !switch_frame_generation(fg_wanted_)) return nullptr;
        const double prepare_start = switching ? now_ms() : 0.0;
        const Presenter::State state = presenter_.prepare(core_, reason, !fg_on_);
        if (switching)
            remote::host_log("d3d12: frame generation switch to mode %d took %.1f ms (%s), then the first prepare %.1f ms",
                             frame_generation_mode(), prepare_start - switch_start, switch_steps_.c_str(), now_ms() - prepare_start);
        if (state == Presenter::State::failed) { fail(E_FAIL, reason); return nullptr; }
        if (state == Presenter::State::minimized) return nullptr;
        if (fsr_on_ && (!fsr_.created() || fsr_.width() != presenter_.width() || fsr_.height() != presenter_.height())) {
            // FSR's context at the swap chain's size (destroyed before each resize).
            std::string note;
            fsr_.destroy(presenter_.swap_chain());
            if (fsr_.create(core_.device(), presenter_.width(), presenter_.height(), Presenter::kFormat, note)) {
                remote::host_log("d3d12: FSR frame generation context %ux%u (%s), VRAM %.1f MiB", presenter_.width(), presenter_.height(),
                                 note.c_str(), double(fsr_.vram()) / (1024.0 * 1024.0));
            } else {
                std::snprintf(fg_failure_[2], sizeof(fg_failure_[2]), "%s", note.c_str());
                remote::host_log("d3d12: %s; FSR frame generation off", fg_failure_[2]);
                fg_wanted_ = 0;  // this frame the proxy presents real frames only; the plain swap chain next frame
            }
        }
        screen_ = nullptr;
        generated_drawn_ = false;
        if (fg_on_) {
            // Frame generation: this frame's screen images (the pacer copies
            // them to the swap chain); the slot's previous copies must be done.
            // Images that cannot be made turn frame generation off (one line).
            fg_slot_ = (fg_slot_ + 1) % d3d12::Pacer::kSlots;
            if (!ensure_screen_images()) {
                std::snprintf(fg_failure_[1], sizeof(fg_failure_[1]), "%s", "frame generation screen images could not be created");
                remote::host_log("d3d12: %s; frame generation off", fg_failure_[1]);
                fg_wanted_ = 0;
                if (!switch_frame_generation(0)) return nullptr;
            }
        }
        if (fg_on_) {
            pacer_.wait_slot(fg_slot_);
            screen_ = real_[fg_slot_].get();
        } else {
            D3D12_RESOURCE_BARRIER barrier =
                transition_barrier(presenter_.back_buffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            list->ResourceBarrier(1, &barrier);
        }
        // fg_frame_image: the frame draws into its own image (frame
        // generation's real-frame input) until composite_frame_image copies it
        // whole into the swap-chain buffer, which needs no clear of its own.
        frame_image_active_ = false;
        if (frame_image_wanted_) {
            if (frame_image_ && !texture_matches(*frame_image_, presenter_.width(), presenter_.height(), Presenter::kFormat)) frame_image_.reset();
            if (!frame_image_ &&
                FAILED(create_target(presenter_.width(), presenter_.height(), frame_image_, Presenter::kFormat, DXGI_FORMAT_UNKNOWN))) {
                frame_image_.reset();
                frame_image_wanted_ = false;  // create_target named it once; frames draw into the swap-chain buffer
            }
            frame_image_active_ = frame_image_ != nullptr;
        }
        list->ClearRenderTargetView(back_rtv(list), world_omitted_ ? kWorldMarker : background_, 0, nullptr);
        if (depth_) list->ClearDepthStencilView(depth_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
        target_ = true;
        return list;
    }

    // The screen images of every slot at the presenter's size: real (render
    // target) and generated (render target and NGX's unordered-access output).
    bool ensure_screen_images() {
        for (unsigned slot = 0; slot < d3d12::Pacer::kSlots; ++slot) {
            for (int generated = 0; generated < 2; ++generated) {
                owned::D3D12TextureRef& image = generated ? generated_[slot] : real_[slot];
                if (image && texture_matches(*image, presenter_.width(), presenter_.height(), Presenter::kFormat)) continue;
                if (image) { pacer_.drain(); image.reset(); }
                if (FAILED(make_target(presenter_.width(), presenter_.height(), image, Presenter::kFormat, DXGI_FORMAT_UNKNOWN, generated != 0))) {
                    image.reset();  // make_target reported the reason once
                    return false;
                }
            }
        }
        return true;
    }
    int frame_generation_mode() const noexcept { return fg_on_ ? 1 : fsr_on_ ? 2 : 0; }
    // Plain/DLSS-G share the main queue and swap chain; the pacer presents
    // offscreen images while DLSS-G is selected. FSR needs its proxy swap
    // chain instead. A mode that cannot start is reported once and stays off.
    bool switch_frame_generation(int wanted) {
        std::string reason;
        LARGE_INTEGER frequency{}, mark{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&mark);
        const auto step = [&](const char* name) {
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            char text[160];
            const bool moved = std::strstr(name, "swap chain") != nullptr;
            std::snprintf(text, sizeof(text), "%s%s %.1f%s%s%s", switch_steps_.empty() ? "" : ", ", name,
                          1000.0 * double(now.QuadPart - mark.QuadPart) / double(frequency.QuadPart), moved ? " [" : "",
                          moved ? presenter_.move_timing() : "", moved ? "]" : "");
            try { switch_steps_ += text; } catch (...) {}
            mark = now;
        };
        if (fg_on_ || fsr_on_) {
            pacer_.drain();
            step("drain");
            if (fsr_on_) {
                fsr_.destroy(presenter_.swap_chain());  // SDK Shutdown: before the proxy's context
                step("fsr destroy");
                if (!presenter_.move_to_queue(core_, core_.queue(), reason)) return fail(E_FAIL, reason), false;
                step("plain swap chain");
            }
            pacer_.stop();
            step("pacer stop");
            for (auto& image : real_) image.reset();
            for (auto& image : generated_) image.reset();
            screen_ = nullptr;
            remote::host_log("d3d12: %s presentation off", fg_on_ ? "frame generation (DLSS-G)" : "FSR frame generation");
            fg_on_ = fsr_on_ = false;
        }
        if (wanted == 0) return true;
        if (fg_failure_[wanted][0]) { fg_wanted_ = 0; return true; }
        if (wanted == 1) {
            const bool started = pacer_.start(core_, reason);
            step("pacer start");
            if (started) {
                fg_on_ = true;
                remote::host_log("d3d12: frame generation presentation on (shared direct queue)");
                return true;
            }
        } else if (d3d12::FsrFrameGeneration::available(core_.device(), reason) && presenter_.move_to_proxy(core_, reason)) {
            step("proxy swap chain");
            fsr_on_ = true;
            fsr_real_ = 0;
            fsr_dispatch_base_ = d3d12::FsrFrameGeneration::dispatches();
            fsr_rate_tick_ = 0;
            remote::host_log("d3d12: FSR frame generation presentation on (FidelityFX proxy swap chain)");
            return true;
        }
        std::snprintf(fg_failure_[wanted], sizeof(fg_failure_[wanted]), "%s", reason.c_str());
        remote::host_log("d3d12: frame generation mode %d could not start: %s", wanted, fg_failure_[wanted]);
        fg_wanted_ = 0;
        pacer_.stop();
        // A half-made switch leaves the window without a usable swap chain:
        // the plain one again on the core queue.
        if (!presenter_.move_to_queue(core_, core_.queue(), reason)) return fail(E_FAIL, reason), false;
        return true;
    }
    // FSR: real and generated (the proxy's interpolation dispatches) frames
    // per second, one host.log line every 5 s (the harness reads it; F10
    // shows the same counts every half second).
    void log_fsr_rates() noexcept {
        LARGE_INTEGER now{}, frequency{};
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&frequency);
        const std::uint64_t dispatches = d3d12::FsrFrameGeneration::dispatches();
        if (!fsr_rate_tick_) { fsr_rate_tick_ = now.QuadPart; fsr_rate_real_ = fsr_real_; fsr_rate_dispatches_ = dispatches; return; }
        const double seconds = double(now.QuadPart - fsr_rate_tick_) / double(frequency.QuadPart);
        if (seconds < 5.0) return;
        const double real = double(fsr_real_ - fsr_rate_real_) / seconds;
        const double generated = double(dispatches - fsr_rate_dispatches_) / seconds;
        remote::host_log("d3d12: FSR frame generation 5 s: presented %.1f/s (real %.1f/s + generated %.1f/s, the proxy's interpolation "
                         "dispatches), ratio %.2f, generating %s",
                         real + generated, real, generated, real > 0 ? (real + generated) / real : 0.0, fsr_.generating() ? "yes" : "no");
        fsr_rate_dispatches_ = dispatches;
        fsr_rate_tick_ = now.QuadPart;
        fsr_rate_real_ = fsr_real_;
    }
    const char* frame_generation_failure(int mode) const noexcept override {
        return mode >= 1 && mode <= 2 && fg_failure_[mode][0] ? fg_failure_[mode] : nullptr;
    }

    // The back buffer, with the interface depth for native draws.
    // An offscreen target (the camp popup) with its own depth instead.
    D3D12_CPU_DESCRIPTOR_HANDLE depth_slice_view(const D3D12Texture& array, UINT slice) const noexcept {
        return {array.dsv.ptr + SIZE_T(slice) * core_.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV)};
    }
    // set_targets: several colour targets and their depth (or none).
    void bind_target(ID3D12GraphicsCommandList* list, bool depth) {
        if (depth_target_) {  // set_depth_target: one depth slice (and its optional colour), its own viewport
            transition(list, *depth_target_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            if (depth_target_color_) transition(list, *depth_target_color_, D3D12_RESOURCE_STATE_RENDER_TARGET);
            const D3D12_CPU_DESCRIPTOR_HANDLE dsv = depth_slice_view(*depth_target_, depth_target_slice_);
            const RECT& r = depth_target_rect_;
            const D3D12_VIEWPORT viewport{float(r.left), float(r.top), float(r.right - r.left), float(r.bottom - r.top), 0.0f, 1.0f};
            if (depth_target_color_) set_targets_viewport(list, 1, &depth_target_color_->rtv, &dsv, viewport);
            else set_targets_viewport(list, 0, nullptr, &dsv, viewport);
            return;
        }
        if (offscreen_) transition(list, *offscreen_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[8] = {offscreen_ ? offscreen_->rtv : back_rtv(list)};
        for (UINT index = 1; index < target_count_; ++index) {
            if (!extra_targets_[index - 1]) { rtvs[index] = null_target_view(); continue; }
            transition(list, *extra_targets_[index - 1], D3D12_RESOURCE_STATE_RENDER_TARGET);
            rtvs[index] = extra_targets_[index - 1]->rtv;
        }
        D3D12Texture* const owner = depth_override_ ? depth_owner_ : offscreen_;
        if (owner && owner->dsv.ptr) transition_depth(list, *owner, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = owner ? owner->dsv : depth_dsv_;
        // A target made without depth binds none (its pipelines take no depth
        // format): a null view handle here crashed the driver (281c932a, SMAA
        // edges target, nvwgf2umx OMSetRenderTargets).
        const bool with_depth = depth && (owner ? owner->dsv.ptr != 0 : !depth_override_ && !offscreen_ && depth_);
        const D3D12_VIEWPORT viewport{0.0f, 0.0f, float(target_width()), float(target_height()), 0.0f, 1.0f};
        set_targets_viewport(list, target_count_, rtvs, with_depth ? &dsv : nullptr, viewport);
    }

    // The list's current pipeline, topology, scissor, index buffer, targets
    // and viewport: a call that sets what is already set is not recorded
    // (every call recorded the same state again per draw). Reset with each
    // new list. Target and depth views live in per-texture heaps released
    // with their texture, so a recorded target set holds only while no
    // texture changed hands (the staging epoch: every target texture owns a
    // staging view).
    struct ListState {
        ID3D12PipelineState* pipeline = nullptr;
        D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        bool scissor_set = false, index_set = false, targets_set = false;
        D3D12_RECT scissor{};
        D3D12_INDEX_BUFFER_VIEW index{};
        UINT target_count = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[8]{}, dsv{};
        bool with_dsv = false;
        D3D12_VIEWPORT viewport{};
        std::uint64_t targets_epoch = 0;
    } list_state_;
    void set_pipeline(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pipeline) {
        if (list_state_.pipeline == pipeline) return;
        list->SetPipelineState(pipeline);
        list_state_.pipeline = pipeline;
    }
    void set_topology(ID3D12GraphicsCommandList* list, D3D_PRIMITIVE_TOPOLOGY topology) {
        if (list_state_.topology == topology) return;
        list->IASetPrimitiveTopology(topology);
        list_state_.topology = topology;
    }
    void set_scissor(ID3D12GraphicsCommandList* list, const D3D12_RECT& scissor) {
        if (list_state_.scissor_set && std::memcmp(&list_state_.scissor, &scissor, sizeof(scissor)) == 0) return;
        list->RSSetScissorRects(1, &scissor);
        list_state_.scissor = scissor;
        list_state_.scissor_set = true;
    }
    void bind_index_view(ID3D12GraphicsCommandList* list, const D3D12_INDEX_BUFFER_VIEW& view) {
        if (list_state_.index_set && list_state_.index.BufferLocation == view.BufferLocation &&
            list_state_.index.SizeInBytes == view.SizeInBytes && list_state_.index.Format == view.Format) return;
        list->IASetIndexBuffer(&view);
        list_state_.index = view;
        list_state_.index_set = true;
    }
    void set_targets_viewport(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs,
                              const D3D12_CPU_DESCRIPTOR_HANDLE* dsv, const D3D12_VIEWPORT& viewport) {
        ListState& s = list_state_;
        const std::uint64_t epoch = core_.staging().epoch();
        bool same = s.targets_set && s.targets_epoch == epoch && s.target_count == count && s.with_dsv == (dsv != nullptr) &&
                    (!dsv || s.dsv.ptr == dsv->ptr) && std::memcmp(&s.viewport, &viewport, sizeof(viewport)) == 0;
        for (UINT index = 0; same && index < count; ++index) same = s.rtvs[index].ptr == rtvs[index].ptr;
        if (same) return;
        list->OMSetRenderTargets(count, rtvs, FALSE, dsv);
        list->RSSetViewports(1, &viewport);
        s.targets_set = true;
        s.targets_epoch = epoch;
        s.target_count = count;
        for (UINT index = 0; index < count; ++index) s.rtvs[index] = rtvs[index];
        s.with_dsv = dsv != nullptr;
        s.dsv = dsv ? *dsv : D3D12_CPU_DESCRIPTOR_HANDLE{};
        s.viewport = viewport;
    }

    // The slot's resolved stamps into the history (its fence has completed).
    void harvest(StampSlot& slot) noexcept {
        if (!slot.submitted) return;
        slot.submitted = false;
        if (stamp_history_.empty()) {
            try { stamp_history_.resize(1u << 15); } catch (...) { return; }
        }
        void* mapped = nullptr;
        const D3D12_RANGE range{0, SIZE_T(slot.used) * sizeof(UINT64)};
        if (FAILED(slot.readback->Map(0, &range, &mapped))) return;
        const auto* ticks = static_cast<const UINT64*>(mapped);
        for (UINT index = 0; index < slot.used; ++index)
            stamp_history_[(slot.first + index) % stamp_history_.size()] = {slot.first + index, ticks[index]};
        const D3D12_RANGE written{0, 0};
        slot.readback->Unmap(0, &written);
    }
    // A new list on this frame slot: read what it timed last, then start over
    // (grown when the last use wanted more stamps than it held).
    void begin_stamps() noexcept {
        StampSlot& slot = stamp_slots_[core_.frame_slot()];
        harvest(slot);
        if (slot.wanted > slot.capacity) {
            const UINT capacity = (std::max)(256u, slot.wanted * 2);
            D3D12_QUERY_HEAP_DESC heap{};
            heap.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            heap.Count = capacity;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = UINT64(capacity) * sizeof(UINT64);
            desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            D3D12_HEAP_PROPERTIES readback{};
            readback.Type = D3D12_HEAP_TYPE_READBACK;
            d3d12::ComPtr<ID3D12QueryHeap> queries;
            d3d12::ComPtr<ID3D12Resource> buffer;
            if (SUCCEEDED(core_.device()->CreateQueryHeap(&heap, IID_PPV_ARGS(&queries))) &&
                SUCCEEDED(core_.device()->CreateCommittedResource(&readback, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                                  nullptr, IID_PPV_ARGS(&buffer)))) {
                slot.heap = std::move(queries);
                slot.readback = std::move(buffer);
                slot.capacity = capacity;
            }
        }
        slot.used = slot.wanted = 0;
        slot.first = next_stamp_;
    }
    // Before a list is submitted: its stamps into the slot's readback buffer.
    void resolve_stamps() noexcept {
        StampSlot& slot = stamp_slots_[core_.frame_slot()];
        if (!slot.used || !list_) return;
        list_->ResolveQueryData(slot.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, slot.used, slot.readback.Get(), 0);
        slot.fence = core_.recording_value();
        slot.submitted = true;
    }

    DXGI_FORMAT target_format() const noexcept { return offscreen_ ? offscreen_->format : Presenter::kFormat; }
    // A depth-slice target's colour (set_depth_target colour): none for shadow maps.
    UINT slice_color_targets() const noexcept { return depth_target_color_ ? 1u : 0u; }
    DXGI_FORMAT slice_color_format() const noexcept { return depth_target_color_ ? depth_target_color_->format : DXGI_FORMAT_UNKNOWN; }
    // The depth format the next draw binds (bind_target's choice).
    DXGI_FORMAT bound_depth_format() const noexcept {
        if (depth_target_) return depth_target_->depth_format;
        if (depth_override_) return depth_owner_ ? depth_owner_->depth_format : DXGI_FORMAT_UNKNOWN;
        return offscreen_ ? offscreen_->depth_format : kDepthFormat;
    }
    UINT target_width() const noexcept { return depth_target_ ? UINT(depth_target_rect_.right) : offscreen_ ? offscreen_->width : presenter_.width(); }
    UINT target_height() const noexcept { return depth_target_ ? UINT(depth_target_rect_.bottom) : offscreen_ ? offscreen_->height : presenter_.height(); }

    static D3D12_RESOURCE_BARRIER transition_barrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                                     D3D12_RESOURCE_STATES after) noexcept {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        return barrier;
    }
    // The back buffer's image this frame: the frame image while it is taken and
    // not yet composited (fg_frame_image), else null (the swap-chain buffer).
    D3D12Texture* frame_image() const noexcept { return frame_image_active_ ? frame_image_.get() : nullptr; }
    // The back buffer's view for a bind or colour clear (the frame image moved to a target first).
    D3D12_CPU_DESCRIPTOR_HANDLE back_rtv(ID3D12GraphicsCommandList* list) noexcept {
        if (D3D12Texture* const image = frame_image()) {
            transition(list, *image, D3D12_RESOURCE_STATE_RENDER_TARGET);
            return image->rtv;
        }
        if (screen_) {
            transition(list, *screen_, D3D12_RESOURCE_STATE_RENDER_TARGET);
            return screen_->rtv;
        }
        return presenter_.rtv();
    }
    static void transition(ID3D12GraphicsCommandList* list, D3D12Texture& texture, D3D12_RESOURCE_STATES state) noexcept {
        if (texture.state == state) {
            if (state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS && texture.written) {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.UAV.pResource = texture.resource.Get();
                list->ResourceBarrier(1, &barrier);
                texture.written = false;
                if (g_hazards) g_hazards->uav_barrier(texture.resource.Get());
            }
            return;
        }
        const D3D12_RESOURCE_BARRIER barrier = transition_barrier(texture.resource.Get(), texture.state, state);
        list->ResourceBarrier(1, &barrier);
        if (g_hazards) g_hazards->transition(texture.resource.Get(), state);
        texture.state = state;
        texture.written = false;
    }
    static void transition_depth(ID3D12GraphicsCommandList* list, D3D12Texture& target, D3D12_RESOURCE_STATES state) noexcept {
        if (!target.depth || target.depth_state == state) return;
        const D3D12_RESOURCE_BARRIER barrier = transition_barrier(target.depth.Get(), target.depth_state, state);
        list->ResourceBarrier(1, &barrier);
        if (g_hazards) g_hazards->transition(target.depth.Get(), state);
        target.depth_state = state;
    }
    static D3D12_CLEAR_FLAGS depth_clear_flags(const D3D12Texture& target) noexcept {
        return target.depth_format == DXGI_FORMAT_D32_FLOAT ? D3D12_CLEAR_FLAG_DEPTH : D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
    }
    // Buffers: a state change is a transition; staying in UNORDERED_ACCESS
    // after a dispatch wrote it is a UAV barrier (D3D11 orders dispatches).
    static void transition(ID3D12GraphicsCommandList* list, owned::D3D12Buffer& buffer, D3D12_RESOURCE_STATES state) noexcept {
        if (buffer.state == state) {
            if (state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS && buffer.written) {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.UAV.pResource = buffer.resource.Get();
                list->ResourceBarrier(1, &barrier);
                buffer.written = false;
                if (g_hazards) g_hazards->uav_barrier(buffer.resource.Get());
            }
            return;
        }
        const D3D12_RESOURCE_BARRIER barrier = transition_barrier(buffer.resource.Get(), buffer.state, state);
        list->ResourceBarrier(1, &barrier);
        if (g_hazards) g_hazards->transition(buffer.resource.Get(), state);
        buffer.state = state;
        buffer.written = false;
    }

    // ---- EI_RENDERER_D3D12_HAZARDS: what each recorded command touches ----
    void hazard_created(const D3D12Texture& texture, const char* what) {
        if (!g_hazards) return;
        char name[160];
        std::snprintf(name, sizeof(name), "%s %ux%ux%u fmt %d mips %u", what, texture.width, texture.height, texture.slices, int(texture.format),
                      texture.mips);
        g_hazards->created(texture.resource.Get(), texture.state, name);
        if (texture.depth) g_hazards->created(texture.depth.Get(), texture.depth_state, std::string(name) + " (its depth)");
    }
    void hazard_created(const owned::D3D12Buffer& buffer) {
        if (!g_hazards) return;
        char name[160];
        std::snprintf(name, sizeof(name), "buffer %llu bytes stride %u flags 0x%x", static_cast<unsigned long long>(buffer.bytes), buffer.stride,
                      buffer.flags);
        g_hazards->created(buffer.resource.Get(), buffer.state, name);
    }
    // The bound targets (bind_target's choice).
    void hazard_targets(bool depth) {
        Hazards& h = *g_hazards;
        if (depth_target_) {
            h.access(depth_target_->resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, true, "depth slice target");
            if (depth_target_color_) h.access(depth_target_color_->resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, true, "depth slice colour");
            return;
        }
        if (offscreen_) h.access(offscreen_->resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, true, "colour target");
        else if (const D3D12Texture* image = frame_image()) h.access(image->resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, true, "frame image");
        else if (screen_) h.access(screen_->resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, true, "frame generation screen");
        else {
            h.external(presenter_.back_buffer(), "back buffer");
            h.access(presenter_.back_buffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, true, "colour target");
        }
        for (UINT index = 1; index < target_count_; ++index)
            if (extra_targets_[index - 1]) h.access(extra_targets_[index - 1]->resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, true, "extra target");
        if (!depth) return;
        D3D12Texture* const owner = depth_override_ ? depth_owner_ : offscreen_;
        if (owner && owner->dsv.ptr) {
            h.access(owner->depth ? owner->depth.Get() : owner->resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, true, "depth target");
        } else if (!owner && !depth_override_ && !offscreen_ && depth_) {
            h.external(depth_.Get(), "interface depth");
            h.access(depth_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, true, "depth target");
        }
    }
    // A native draw or dispatch, just before it is recorded: every slot its
    // programs declare (all set slots for an unreflected program, or with
    // hull and domain stages, whose declarations are not read here), its
    // indirect arguments, index buffer, targets and bindless textures.
    void hazard_native(const char* kind, bool compute, const ProgramCode* first, const ProgramCode* second, bool other_stages,
                       owned::D3D12Buffer* arguments, owned::D3D12Buffer* indices) {
        Hazards& h = *g_hazards;
        h.command(kind);
        const bool all = other_stages || (!first && !second) || (first && !first->reflected) || (second && !second->reflected);
        char how[48];
        for (UINT index = 0; index < kD3D11ResourceSlots; ++index) {
            const Slot& slot = resources_[index];
            if (!all && !(first && first->srv_kind[index]) && !(second && second->srv_kind[index])) continue;
            std::snprintf(how, sizeof(how), "t%u", index);
            if (slot.buffer) {
                if (slot.view.ptr != slot.buffer->srv.ptr) h.untracked_view(how, "the slot's view is not its buffer's");
                h.access(slot.buffer->resource.Get(), kShaderRead, false, how);
            } else if (slot.texture && slot.depth) {
                if (slot.view.ptr != slot.texture->depth_srv.ptr) h.untracked_view(how, "the slot's view is not its target's depth view");
                h.access(slot.texture->depth ? slot.texture->depth.Get() : slot.texture->resource.Get(), kShaderRead, false, how);
            } else if (slot.texture) {
                if (slot.view.ptr != slot.texture->srv.ptr) h.untracked_view(how, "the slot's view is not its texture's");
                h.access(slot.texture->resource.Get(), kShaderRead, false, how);
            } else if (slot.view.ptr) {
                h.untracked_view(how, "a view with no executor resource");
            }
        }
        for (UINT index = 0; index < kD3D11UnorderedSlots; ++index) {
            const Slot& slot = unordered_[index];
            if (!all && !(first && first->uav_kind[index]) && !(second && second->uav_kind[index])) continue;
            std::snprintf(how, sizeof(how), "u%u", index);
            if (slot.buffer) {
                if (slot.view.ptr != slot.buffer->uav.ptr) h.untracked_view(how, "the slot's view is not its buffer's");
                h.access(slot.buffer->resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, how);
            } else if (slot.texture) {
                const auto& levels = slot.texture->level_uavs;
                if (slot.view.ptr != slot.texture->uav.ptr &&
                    std::none_of(levels.begin(), levels.end(), [&](D3D12_CPU_DESCRIPTOR_HANDLE level) { return level.ptr == slot.view.ptr; }))
                    h.untracked_view(how, "the slot's view is not its texture's");
                h.access(slot.texture->resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, how);
            } else if (slot.view.ptr) {
                h.untracked_view(how, "a view with no executor resource");
            }
        }
        // Frame lifetimes: the constant views and tables it reads must be this frame's ring memory.
        const std::uint64_t fence = core_.recording_value();
        for (UINT index = 0; index < kConstantSlots; ++index) {
            if (!all && !(first && (first->cbv_mask >> index & 1u)) && !(second && (second->cbv_mask >> index & 1u))) continue;
            std::snprintf(how, sizeof(how), "b%u constants", index);
            h.used(Hazards::kUploadRing, how, index == 0 && !constants_[0].address ? zero_constants_ : constants_[index].address, fence);
        }
        h.used(Hazards::kViewRing, "constant table b1-b13", core_.views().gpu(constant_table_).ptr, fence);
        h.used(Hazards::kViewRing, "SRV table", core_.views().gpu(resource_table_).ptr, fence);
        h.used(Hazards::kViewRing, "UAV table", core_.views().gpu(unordered_table_).ptr, fence);
        h.used(Hazards::kSamplerRing, "sampler table", core_.samplers().gpu(sampler_table_).ptr, fence);
        if (arguments) h.access(arguments->resource.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, false, "indirect arguments");
        if (indices) h.access(indices->resource.Get(), D3D12_RESOURCE_STATE_INDEX_BUFFER, false, "index buffer");
        if (!compute) hazard_targets(true);
        // Native programs read textures through ResourceDescriptorHeap (SM 6.6),
        // which PSV0 does not list: every native command may read any bindless texture.
        h.bindless_reads();
    }

    // An indirect call's argument state: also shader-readable while a t slot
    // holds the same buffer (ground cover reads its counts as both; either single
    // state left the other use wrong, found by EI_RENDERER_D3D12_HAZARDS).
    static constexpr D3D12_RESOURCE_STATES kIndirectRead = kShaderRead | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    D3D12_RESOURCE_STATES indirect_state(const owned::D3D12Buffer& arguments) const noexcept {
        for (const auto& slot : resources_)
            if (slot.buffer == &arguments) return kIndirectRead;
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }

    // ---- GPU reports: EI_RENDERER_D3D12_HAZARDS, the shader side ----------
    // The buffer native_shader.h declares at u0 space3 (nativeGpuReport):
    // bound to every native program. Without the switch it is 64 zero bytes
    // and every shader check is off. With it, each frame starts with the
    // header {loop cap, 0 reports, bindless limit, bitmap offset, fallback
    // index} and the live bindless bitmap, and present reads the reports back.
    static constexpr std::uint32_t kNullBindlessIndex = owned::kD3D12NullTextureIndex;
    static constexpr UINT kGpuReportRecords = 64, kGpuReportBitmap = 32 + kGpuReportRecords * 16, kGpuReportCap = 256;
    bool create_gpu_reports() {
        const UINT64 bytes = g_hazards ? kGpuReportBitmap + Core::kViewDescriptorLimit / 8 : 64;
        if (FAILED(create_buffer(bytes, 0, owned::kD3D12BufferUnordered, gpu_reports_)) || !gpu_reports_) return false;
        if (!g_hazards) return true;
        remote::host_log("d3d12: GPU reports on: loops stop at %u iterations, bindless indices checked against the live set", kGpuReportCap);
        return true;
    }
    // The frame's header and live bitmap, before anything is recorded (open_list's new frame).
    void gpu_report_frame_start(ID3D12GraphicsCommandList* list) {
        const std::uint32_t limit = (std::min)(core_.views().persistent_capacity(), Core::kViewDescriptorLimit);
        const std::uint32_t words = (limit + 31) / 32;
        std::vector<std::uint32_t> data(kGpuReportBitmap / 4 + words, 0u);
        data[0] = kGpuReportCap;
        data[1] = 0;
        data[2] = limit;
        data[3] = kGpuReportBitmap;
        data[4] = kNullBindlessIndex;  // a dead index samples the permanent null view
        g_hazards->live_words(data.data() + kGpuReportBitmap / 4, words);
        data[kGpuReportBitmap / 4] |= 1u << kNullBindlessIndex;  // the null view itself is always valid
        UploadAllocation upload;
        std::string reason;
        const UINT64 bytes = UINT64(data.size()) * 4;
        if (!upload_push(data.data(), bytes, 16, upload, reason)) { fail(E_OUTOFMEMORY, reason); return; }
        transition(list, *gpu_reports_, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(gpu_reports_->resource.Get(), 0, upload.resource, upload.offset, bytes);
    }
    // A texture took a bindless index mid-frame: its bit in this frame's bitmap.
    void gpu_report_live(std::uint32_t index) {
        if (index >= core_.views().persistent_capacity()) return;
        const std::uint32_t word = g_hazards->live_word(index / 32);
        (void)write_buffer(*gpu_reports_, kGpuReportBitmap + UINT64(index / 32) * 4, &word, 4);
    }
    // present: read the reports of finished frames, then copy this frame's.
    void gpu_report_frame_end() {
        static const char* const loops[] = {"?", "neighbour piece scan", "neighbour piece sweep", "shadow cascade levels", "?",
                                            "PCSS blocker search", "PCSS/PCF taps", "point-shadow blocker search",
                                            "point-light grid range", "cave exit glow", "neighbour fog piece scan", "neighbour fog piece sweep"};
        for (auto it = gpu_report_tickets_.begin(); it != gpu_report_tickets_.end();) {
            std::vector<std::uint8_t> data;
            const HRESULT hr = poll_readback(it->first, data);
            if (hr == S_FALSE) { ++it; continue; }
            const std::uint64_t frame = it->second;
            it = gpu_report_tickets_.erase(it);
            if (FAILED(hr) || data.size() < kGpuReportBitmap) continue;
            const auto* words = reinterpret_cast<const std::uint32_t*>(data.data());
            const std::uint32_t taken = words[1], records = (std::min)(taken, kGpuReportRecords);
            for (std::uint32_t r = 0; r < records; ++r) {
                const std::uint32_t* record = words + 8 + r * 4;
                // id: the report in the low 16 bits, the entry point above (native_shader.h).
                static const char* const origins[] = {"a program without a report origin", "nativeVS", "nativePS", "nativeShadowPS",
                                                      "nativePrepassPS", "nativeMotionPS"};
                const std::uint32_t id = record[0] & 0xFFFFu, origin = record[0] >> 16, value = record[1], who = record[2], pixel = record[3];
                const char* const program = origin < sizeof(origins) / sizeof(origins[0]) ? origins[origin] : "?";
                char key[48];
                std::snprintf(key, sizeof(key), "%u:%u:%u", id, value, origin);
                if (gpu_report_seen_[key]++) continue;
                char where[160];
                if (!origin) std::snprintf(where, sizeof(where), "%s", program);
                else if (pixel == 0xFFFFFFFFu) std::snprintf(where, sizeof(where), "%s, vertex %u", program, who);
                else std::snprintf(where, sizeof(where), "%s, pixel (%u, %u), texture reference %u", program, pixel & 0xFFFFu, pixel >> 16, who);
                if (id == 100)
                    remote::host_log("d3d12 gpu loop: bindless index %u is not a live texture (sampled the null view instead); %s; frame %llu; "
                                     "%u reports that frame",
                                     value, where, static_cast<unsigned long long>(frame), taken);
                else
                    remote::host_log("d3d12 gpu loop: '%s' (loop %u) stopped at %u iterations, bound read %u (0x%08x); %s; frame %llu; %u reports "
                                     "that frame",
                                     id < sizeof(loops) / sizeof(loops[0]) ? loops[id] : "?", id, kGpuReportCap, value, value, where,
                                     static_cast<unsigned long long>(frame), taken);
            }
        }
        std::uint64_t ticket = 0;
        if (SUCCEEDED(request_readback(*gpu_reports_, kGpuReportBitmap, ticket)) && ticket)
            gpu_report_tickets_.push_back({ticket, frame_serial_});
    }

    // ---- Native slots -> descriptors (the thin per-flush translation) -------
    static D3D12_SAMPLER_DESC to_sampler(const D3D11_SAMPLER_DESC& source) noexcept {
        D3D12_SAMPLER_DESC desc{};
        desc.Filter = static_cast<D3D12_FILTER>(source.Filter);
        desc.AddressU = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(source.AddressU ? source.AddressU : D3D11_TEXTURE_ADDRESS_WRAP);
        desc.AddressV = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(source.AddressV ? source.AddressV : D3D11_TEXTURE_ADDRESS_WRAP);
        desc.AddressW = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(source.AddressW ? source.AddressW : D3D11_TEXTURE_ADDRESS_WRAP);
        desc.MipLODBias = source.MipLODBias;
        desc.MaxAnisotropy = (std::max)(1u, source.MaxAnisotropy);
        desc.ComparisonFunc = source.ComparisonFunc ? static_cast<D3D12_COMPARISON_FUNC>(source.ComparisonFunc) : D3D12_COMPARISON_FUNC_NEVER;
        std::memcpy(desc.BorderColor, source.BorderColor, sizeof(desc.BorderColor));
        desc.MinLOD = source.MinLOD;
        desc.MaxLOD = source.MaxLOD;
        return desc;
    }

    // Transitions every bound resource, then (when a slot changed) copies the
    // slots into ring tables and binds them with b0. False: the ring is full.
    // first/second: the programs about to run (their declared kinds pick each
    // unset slot's null view; a different pair rewrites the tables).
    // arguments: an indirect call's argument buffer; bound at a t slot too, it
    // takes the combined state there (indirect_state), so neither use flips the other.
    bool bind_slots(ID3D12GraphicsCommandList* list, bool compute, const ProgramCode* first, const ProgramCode* second,
                    const owned::D3D12Buffer* arguments = nullptr) {
        if (first != table_programs_[0] || second != table_programs_[1]) {
            table_programs_[0] = first;
            table_programs_[1] = second;
            slots_dirty_ = true;
        }
        for (auto& slot : resources_) {
            if (slot.buffer) transition(list, *slot.buffer, slot.buffer == arguments ? kIndirectRead : kShaderRead);
            else if (slot.texture && slot.depth) transition_depth(list, *slot.texture, kDepthRead);
            else if (slot.texture) transition(list, *slot.texture, kShaderRead);
        }
        for (auto& slot : unordered_) {
            if (slot.buffer) transition(list, *slot.buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            else if (slot.texture) transition(list, *slot.texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        if (slots_dirty_) {
            // Every slot a program declares is written: a native program may
            // read any declared slot (D3D11 reads unbound slots as zero), and a
            // ring slot past the last bound one would hold an older frame's
            // descriptor, not a null view (the 43636144 hang). Slots past the
            // highest declared one are never read (readable below).
            ID3D12Device* device = core_.device();
            const auto declared = [&](bool uav, UINT index) -> std::uint8_t {
                for (const ProgramCode* program : {second, first})
                    if (program) {
                        const std::uint8_t kind = uav ? program->uav_kind[index] : program->srv_kind[index];
                        if (kind) return kind;
                    }
                return 0;
            };
            // Only the registers the two programs declare are ever read: the
            // table is written up to the highest of them (every slot below it
            // in full, null views where unset), not all 128 + 8 per draw. That
            // per-draw copy filled the ring on neighbour-map frames. A program
            // without complete reflection keeps the full table.
            const auto readable = [&](bool uav, UINT capacity) -> UINT {
                UINT count = 0;
                for (const ProgramCode* program : {first, second}) {
                    if (!program) continue;
                    if (!program->reflected) return capacity;
                    count = (std::max)(count, uav ? program->uav_count : program->srv_count);
                }
                return (first || second) ? (std::max)(1u, (std::min)(count, capacity)) : capacity;
            };
            // A table written earlier in this frame is bound again when it
            // holds exactly these descriptors: the same staging handles (no
            // handle was handed out again since: the staging epoch), the same
            // constant views. The ring keeps a frame's tables until that frame
            // completes, and records are dropped at each frame's start.
            const bool live = tables_.frame == frame_serial_ && tables_.epoch == core_.staging().epoch() &&
                              tables_.view_writes == view_writes_;
            if (!live) tables_.srv_count = tables_.uav_count = tables_.constants_valid = 0;
            tables_.frame = frame_serial_;
            tables_.epoch = core_.staging().epoch();
            tables_.view_writes = view_writes_;
            const auto copy_table = [&](const Slot* slots, UINT capacity, bool uav, std::uint32_t& table, UINT& last_count,
                                        D3D12_CPU_DESCRIPTOR_HANDLE* last) {
                UINT count = readable(uav, capacity);
                D3D12_CPU_DESCRIPTOR_HANDLE sources[kD3D11ResourceSlots];
                UINT sizes[kD3D11ResourceSlots];
                for (UINT index = 0; index < count; ++index) {
                    const std::uint8_t kind = slots[index].view.ptr ? 0 : declared(uav, index);
                    sources[index] = slots[index].view.ptr ? slots[index].view : kind ? null_view(uav, kind) : (uav ? null_uav_ : null_srv_);
                    sizes[index] = 1;
                }
                if (last_count == count && std::memcmp(last, sources, count * sizeof(sources[0])) == 0) {
                    return true;  // `table` still names that copy
                }
                last_count = 0;
                table = ring(count);
                if (table == UINT32_MAX) return false;
                const D3D12_CPU_DESCRIPTOR_HANDLE destination = core_.views().cpu(table);
                device->CopyDescriptors(1, &destination, &count, count, sources, sizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                std::memcpy(last, sources, count * sizeof(sources[0]));
                last_count = count;
                return true;
            };
            if (!copy_table(resources_.data(), kD3D11ResourceSlots, false, resource_table_, tables_.srv_count, tables_.srv) ||
                !copy_table(unordered_.data(), kD3D11UnorderedSlots, true, unordered_table_, tables_.uav_count, tables_.uav))
                return false;
            // b1-b13: views written straight into the ring (null where unset).
            const UINT constant_count = kConstantSlots - 1;  // b1-b13
            bool same_constants = tables_.constants_valid;
            for (UINT index = 0; index < constant_count && same_constants; ++index)
                same_constants = tables_.constants[index].address == constants_[index + 1].address &&
                                 tables_.constants[index].bytes == constants_[index + 1].bytes;
            if (!same_constants) {
                tables_.constants_valid = false;
                constant_table_ = ring(constant_count);
                if (constant_table_ == UINT32_MAX) return false;
                for (UINT index = 0; index < constant_count; ++index) {
                    const auto& constant = constants_[index + 1];
                    const D3D12_CONSTANT_BUFFER_VIEW_DESC view{constant.address, constant.bytes};
                    device->CreateConstantBufferView(constant.address ? &view : nullptr, core_.views().cpu(constant_table_ + index));
                    tables_.constants[index] = constant;
                }
                tables_.constants_valid = true;
            }
            // The sampler table holds the samplers the bound programs declare
            // (from their PSV0; an unreflected program takes every slot); the
            // other slots take the default sampler, so samplers left bound by
            // earlier draws make no new table. A table's contents never change,
            // so each distinct set is written once into the sampler heap's
            // persistent region and reused by content in every later frame;
            // only when that region is full does a set take a per-frame table
            // from the ring (named once).
            std::uint32_t sampler_slots = (first || second) ? 0u : 0xFFFFu;
            for (const ProgramCode* program : {first, second})
                if (program) sampler_slots |= program->reflected ? program->sampler_mask : 0xFFFFu;
            std::array<D3D12_SAMPLER_DESC, kD3D11SamplerSlots> canonical;
            for (UINT slot = 0; slot < kD3D11SamplerSlots; ++slot)
                canonical[slot] = (sampler_slots >> slot & 1u) && samplers_set_[slot] ? sampler_descs_[slot] : default_sampler_;
            const bool same_samplers = sampler_table_valid_ && std::memcmp(table_sampler_descs_.data(), canonical.data(), sizeof(canonical)) == 0;
            if (!same_samplers) {
                std::uint64_t key = 1469598103934665603ull;  // FNV-1a over the set
                const auto* byte = reinterpret_cast<const unsigned char*>(canonical.data());
                for (std::size_t index = 0; index < sizeof(canonical); ++index) key = (key ^ byte[index]) * 1099511628211ull;
                const auto find = [&](const std::vector<SamplerTable>& tables) -> const SamplerTable* {
                    for (const auto& table : tables)
                        if (table.key == key && std::memcmp(table.descs.data(), canonical.data(), sizeof(canonical)) == 0) return &table;
                    return nullptr;
                };
                const SamplerTable* found = find(sampler_tables_);
                if (!found) found = find(frame_sampler_tables_);
                if (found) {
                    sampler_table_ = found->index;
                } else {
                    const UINT sampler_count = kD3D11SamplerSlots;
                    bool persistent = true;
                    sampler_table_ = core_.samplers().allocate_block(sampler_count);
                    if (sampler_table_ == UINT32_MAX) {
                        report_once(sampler_table_failures_, "sampler tables",
                                    "the persistent sampler region is full; further sets take per-frame tables from the ring");
                        persistent = false;
                        sampler_table_ = core_.samplers().transient(sampler_count);
                        if (g_hazards && sampler_table_ != UINT32_MAX)
                            g_hazards->taken(Hazards::kSamplerRing, core_.samplers().gpu(sampler_table_).ptr,
                                             std::uint64_t(sampler_count) * core_.samplers().stride(), core_.recording_value(), core_.completed());
                    }
                    if (sampler_table_ == UINT32_MAX) {
                        omit("native draws beyond the sampler ring", false);  // every one, named
                        sampler_ring_full_ = true;
                        sampler_table_valid_ = false;
                        return false;
                    }
                    for (UINT slot = 0; slot < sampler_count; ++slot)
                        device->CreateSampler(&canonical[slot], core_.samplers().cpu(sampler_table_ + slot));
                    try {
                        (persistent ? sampler_tables_ : frame_sampler_tables_).push_back({key, sampler_table_, canonical});
                    } catch (...) {
                        // Unrecorded: the set is written again when it comes back.
                    }
                }
                table_sampler_descs_ = canonical;
                sampler_table_valid_ = true;
            }
            slots_dirty_ = false;
        }
        D3D12_GPU_VIRTUAL_ADDRESS b0 = constants_[0].address;
        if (!b0) {  // an unset b0 reads zeros, as an unbound D3D11 constant buffer
            if (!ensure_zero_constants()) return false;
            b0 = zero_constants_;
        }
        // b1's raw view (t1 space4) reads zeros while b1 is unset.
        D3D12_GPU_VIRTUAL_ADDRESS b1 = constants_[1].address;
        if (!b1) {
            if (!ensure_zero_constants()) return false;
            b1 = zero_constants_;
        }
        // u0 space3: the GPU report buffer (UNORDERED_ACCESS for the frame once its header is written).
        if (g_hazards) transition(list, *gpu_reports_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const D3D12_GPU_VIRTUAL_ADDRESS reports = gpu_reports_->resource->GetGPUVirtualAddress();
        if (compute) {
            list->SetComputeRootUnorderedAccessView(kRootGpuReport, reports);
            list->SetComputeRootConstantBufferView(kRootD3D11Constants, b0);
            list->SetComputeRootShaderResourceView(kRootD3D11RawConstants, b0);
            list->SetComputeRootShaderResourceView(kRootD3D11RawLighting, b1);
            list->SetComputeRootDescriptorTable(kRootD3D11ConstantTable, core_.views().gpu(constant_table_));
            list->SetComputeRootDescriptorTable(kRootD3D11Resources, core_.views().gpu(resource_table_));
            list->SetComputeRootDescriptorTable(kRootD3D11Unordered, core_.views().gpu(unordered_table_));
            list->SetComputeRootDescriptorTable(kRootD3D11Samplers, core_.samplers().gpu(sampler_table_));
        } else {
            list->SetGraphicsRootUnorderedAccessView(kRootGpuReport, reports);
            list->SetGraphicsRootConstantBufferView(kRootD3D11Constants, b0);
            list->SetGraphicsRootShaderResourceView(kRootD3D11RawConstants, b0);
            list->SetGraphicsRootShaderResourceView(kRootD3D11RawLighting, b1);
            list->SetGraphicsRootDescriptorTable(kRootD3D11ConstantTable, core_.views().gpu(constant_table_));
            list->SetGraphicsRootDescriptorTable(kRootD3D11Resources, core_.views().gpu(resource_table_));
            list->SetGraphicsRootDescriptorTable(kRootD3D11Unordered, core_.views().gpu(unordered_table_));
            list->SetGraphicsRootDescriptorTable(kRootD3D11Samplers, core_.samplers().gpu(sampler_table_));
        }
        return true;
    }

    // This frame's zero block (512 bytes: b0's raw view reads up to 368).
    bool ensure_zero_constants() {
        if (zero_constants_) return true;
        UploadAllocation allocation;
        std::string reason;
        if (!upload_allocate(512, 256, allocation, reason)) { fail(E_OUTOFMEMORY, reason); return false; }
        std::memset(allocation.cpu, 0, 512);
        zero_constants_ = allocation.gpu;
        return true;
    }

    // PSV0 (DxilPipelineStateValidation.h): runtime info size and block, then
    // the resource count, the bind-info stride and the entries {type, space,
    // lower, upper[, kind, flags]}. A part without kinds (PSV0 version 0)
    // derives them from the type (raw, structured, else a 2D texture).
    static void declared_kinds(ProgramCode& code) noexcept {
        const auto* data = static_cast<const std::uint8_t*>(code.bytecode.pShaderBytecode);
        const std::size_t size = code.bytecode.BytecodeLength;
        const auto u32 = [&](std::size_t at) { std::uint32_t v = 0; if (at + 4 <= size) std::memcpy(&v, data + at, 4); return v; };
        if (!data || size < 32 || u32(0) != 0x43425844u) return;  // "DXBC"
        const std::uint32_t parts = u32(28);
        for (std::uint32_t part = 0; part < parts; ++part) {
            const std::size_t at = u32(32 + std::size_t(part) * 4);
            if (u32(at) != 0x30565350u) continue;  // "PSV0"
            const std::size_t end = at + 8 + u32(at + 4);
            std::size_t p = at + 8;
            p += 4 + u32(p);  // runtime info
            const std::uint32_t count = u32(p);
            p += 4;
            if (!count) { code.reflected = true; return; }
            const std::uint32_t stride = u32(p);
            // The counts bound the table only when every entry was read.
            code.reflected = stride >= 16 && p + 4 + std::size_t(count) * stride <= end;
            p += 4;
            for (std::uint32_t index = 0; index < count && p + stride <= end && stride >= 16; ++index, p += stride) {
                const std::uint32_t type = u32(p), space = u32(p + 4), lower = u32(p + 8), upper = u32(p + 12);
                std::uint32_t kind = stride >= 24 ? u32(p + 16) : 0;
                if (!kind) kind = type == 4 || type == 7 ? 11u : type == 5 || type == 8 || type == 9 ? 12u : 2u;
                if (space != 0 && type >= 3 && type <= 5) code.bindless = true;  // reads views by heap index
                if (space != 0 || lower > upper) continue;
                const bool srv = type >= 3 && type <= 5, uav = type >= 6 && type <= 9;
                if (type == 2)  // PSVResourceType::CBV
                    for (std::uint32_t slot = lower; slot <= upper && slot < kConstantSlots; ++slot) code.cbv_mask |= std::uint16_t(1u << slot);
                if (type == 1)  // PSVResourceType::Sampler
                    for (std::uint32_t slot = lower; slot <= upper && slot < kD3D11SamplerSlots; ++slot) code.sampler_mask |= std::uint16_t(1u << slot);
                for (std::uint32_t slot = lower; slot <= upper && slot < 256; ++slot) {
                    if (srv && slot < kD3D11ResourceSlots) { code.srv_kind[slot] = std::uint8_t(kind); code.srv_count = (std::max)(code.srv_count, slot + 1); }
                    if (uav && slot < kD3D11UnorderedSlots) { code.uav_kind[slot] = std::uint8_t(kind); code.uav_count = (std::max)(code.uav_count, slot + 1); }
                }
            }
            return;
        }
    }

    // A null view of the declared kind (created once per kind).
    D3D12_CPU_DESCRIPTOR_HANDLE null_view(bool uav, std::uint8_t kind) noexcept {
        if (kind >= 16) kind = 2;
        D3D12_CPU_DESCRIPTOR_HANDLE& view = (uav ? null_uav_kind_ : null_srv_kind_)[kind];
        if (view.ptr) return view;
        view = core_.staging().allocate();
        if (!view.ptr) return uav ? null_uav_ : null_srv_;
        ID3D12Device* device = core_.device();
        if (uav) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
            desc.Format = DXGI_FORMAT_R32_UINT;
            switch (kind) {
            case 1: desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D; break;
            case 2: desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; break;
            case 4: desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D; desc.Texture3D.WSize = 1; break;
            case 6: desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY; desc.Texture1DArray.ArraySize = 1; break;
            case 7: desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY; desc.Texture2DArray.ArraySize = 1; break;
            case 11: desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; desc.Format = DXGI_FORMAT_R32_TYPELESS;
                desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW; break;
            case 12: desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; desc.Format = DXGI_FORMAT_UNKNOWN;
                desc.Buffer.StructureByteStride = 4; break;
            default: desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; break;
            }
            device->CreateUnorderedAccessView(nullptr, nullptr, &desc, view);
            return view;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        switch (kind) {
        case 1: desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D; desc.Texture1D.MipLevels = 1; break;
        case 3: desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS; break;
        case 4: desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D; desc.Texture3D.MipLevels = 1; break;
        case 5: desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; desc.TextureCube.MipLevels = 1; break;
        case 6: desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY; desc.Texture1DArray.MipLevels = 1; desc.Texture1DArray.ArraySize = 1; break;
        case 7: desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; desc.Texture2DArray.MipLevels = 1; desc.Texture2DArray.ArraySize = 1; break;
        case 8: desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY; desc.Texture2DMSArray.ArraySize = 1; break;
        case 9: desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY; desc.TextureCubeArray.MipLevels = 1; desc.TextureCubeArray.NumCubes = 1; break;
        case 10: desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; desc.Format = DXGI_FORMAT_R32_UINT; break;
        case 11: desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; desc.Format = DXGI_FORMAT_R32_TYPELESS;
            desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW; break;
        case 12: desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.Buffer.StructureByteStride = 4; break;
        default: desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; desc.Texture2D.MipLevels = 1; break;
        }
        device->CreateShaderResourceView(nullptr, &desc, view);
        return view;
    }

    // A shared program's DXIL from the host's table (cached by the program's
    // stable source and entry pointers: dxil_name hashes the whole source).
    const D3D12_SHADER_BYTECODE* program_code(const owned::ShaderProgram& program, std::string* name, const ProgramCode** out = nullptr) {
        const ProgramKey key{program.source.data(), program.source.size(), program.entry, program.defines};
        auto found = programs_.find(key);
        if (found == programs_.end()) {
            ProgramCode code;
            code.name = program_dxil_name(program);
            code.entry = program.entry ? program.entry : "";
            code.identity = program_identity(program);
            code.bytecode = resource_program(code.name.c_str());
            declared_kinds(code);
            found = programs_.emplace(key, std::move(code)).first;
            if (!found->second.bytecode.BytecodeLength)
                report_once(program_failures_, "a native program", "the host has no DXIL for " + std::string(program.entry) + " (" +
                                                                     found->second.name + "; dxc rejected it?)");
        }
        if (!found->second.bytecode.BytecodeLength) return nullptr;
        if (name) *name = found->second.name;
        if (out) *out = &found->second;
        return &found->second.bytecode;
    }

    // code: the program's reflection (for bind_slots), when asked.
    ID3D12PipelineState* compute_pipeline(const owned::ShaderProgram& program, const ProgramCode** code = nullptr) {
        const ProgramCode* found_code = nullptr;
        if (!program_code(program, nullptr, &found_code)) return nullptr;
        if (code) *code = found_code;
        // Keyed by the program record (stable map node), never by building its name.
        auto found = compute_pipelines_.find(found_code);
        if (found == compute_pipelines_.end()) {
            const std::wstring name = compute_name(found_code->name);
            found = compute_pipelines_.emplace(found_code, pipelines_.request(name, compute_desc(found_code->bytecode))).first;
            PipelineRecord record;
            record.kind = PipelineRecord::kCompute;
            record.programs[0] = found_code->identity;
            record.exact[0] = name_hash(found_code->name);
            if (note_pipeline(name, record))
                remote::host_log("d3d12: pipeline not warmed at start, built on first use (its work is omitted until ready; recorded "
                                 "for the next start): compute %s",
                                 found_code->entry.c_str());
        }
        return ready_pipeline(found->second);
    }

    // codes: the vertex and pixel programs' reflection (for bind_slots), when asked.
    ID3D12PipelineState* native_pipeline(const owned::ShaderProgram& vertex, const owned::ShaderProgram& pixel,
                                         const owned::D3D12NativeState& state, const owned::ShaderProgram* hull = nullptr,
                                         const owned::ShaderProgram* domain = nullptr, const ProgramCode** codes = nullptr) {
        // A program without an entry: no pixel stage (depth-only draws).
        const bool no_pixel = !pixel.entry || !pixel.entry[0];
        NativePipelineKey key;  // zeroed whole: compared and hashed byte for byte
        const D3D12_SHADER_BYTECODE* vs = program_code(vertex, nullptr, &key.programs[0]);
        const D3D12_SHADER_BYTECODE* ps = no_pixel ? nullptr : program_code(pixel, nullptr, &key.programs[1]);
        const D3D12_SHADER_BYTECODE* hs = hull ? program_code(*hull, nullptr, &key.programs[2]) : nullptr;
        const D3D12_SHADER_BYTECODE* ds = domain ? program_code(*domain, nullptr, &key.programs[3]) : nullptr;
        if (!vs || (!ps && !no_pixel) || (hull && !hs) || (domain && !ds)) return nullptr;
        if (codes) { codes[0] = key.programs[0]; codes[1] = key.programs[1]; }
        std::memcpy(&key.state, &state, sizeof(state));
        DXGI_FORMAT* formats = key.formats;
        formats[0] = depth_target_ ? slice_color_format() : target_format();
        for (UINT index = 1; index < target_count_ && !depth_target_; ++index) formats[index] = extra_format(index);
        formats[8] = bound_depth_format();
        // Consecutive draws mostly repeat the last pipeline.
        if (last_native_valid_ && std::memcmp(&key, &last_native_key_, sizeof(key)) == 0) return ready_pipeline(last_native_id_);
        auto found = native_pipelines_.find(key);
        if (found == native_pipelines_.end()) {
            const UINT targets = depth_target_ ? slice_color_targets() : target_count_;
            const D3D12_SHADER_BYTECODE* code[4] = {vs, ps, hs, ds};
            const std::string* names[4] = {};
            PipelineRecord record;
            record.kind = PipelineRecord::kNative;
            record.targets = targets;
            for (int stage = 0; stage < 4; ++stage)
                if (key.programs[stage]) {
                    names[stage] = &key.programs[stage]->name;
                    record.programs[stage] = key.programs[stage]->identity;
                    record.exact[stage] = name_hash(key.programs[stage]->name);
                }
            std::memcpy(&record.state, &state, sizeof(state));
            std::memcpy(record.formats, key.formats, sizeof(key.formats));
            const std::wstring name = native_name(names, state, key.formats);
            found = native_pipelines_.emplace(key, pipelines_.request(name, native_desc(code, state, key.formats, targets))).first;
            if (note_pipeline(name, record)) {
                const auto entry = [&](int stage) { return key.programs[stage] ? key.programs[stage]->entry.c_str() : "-"; };
                remote::host_log("d3d12: pipeline not warmed at start, built on first use (its draws are omitted until ready; "
                                 "recorded for the next start): vs=%s ps=%s hs=%s ds=%s targets=%u formats=%u,%u depth=%u",
                                 entry(0), entry(1), entry(2), entry(3), targets, unsigned(key.formats[0]),
                                 unsigned(key.formats[1]), unsigned(key.formats[8]));
            }
        }
        last_native_key_ = key;
        last_native_id_ = found->second;
        last_native_valid_ = true;
        return ready_pipeline(found->second);
    }

    // A native pipeline's library name: its program names, the state and
    // the formats, byte for byte (hashed).
    static std::wstring native_name(const std::string* const (&names)[4], const owned::D3D12NativeState& state,
                                    const DXGI_FORMAT (&formats)[9]) {
        std::string name_key;
        for (const std::string* name : names) if (name) name_key += *name;
        name_key.append(reinterpret_cast<const char*>(&state), sizeof(state));
        name_key.append(reinterpret_cast<const char*>(formats), sizeof(formats));
        std::uint64_t hash = 1469598103934665603ull;
        for (const char c : name_key) hash = (hash ^ std::uint8_t(c)) * 1099511628211ull;
        wchar_t name[64];
        swprintf_s(name, L"native.%016llx", static_cast<unsigned long long>(hash));
        return name;
    }

    // A native pipeline's description (code: vertex, pixel or null, hull and
    // domain or null; targets: colour targets, 0 for a depth-only draw).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC native_desc(const D3D12_SHADER_BYTECODE* const (&code)[4], const owned::D3D12NativeState& state,
                                                   const DXGI_FORMAT (&formats)[9], UINT targets) const {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = targets_desc(targets, formats);
        desc.VS = *code[0];
        if (code[1]) desc.PS = *code[1];
        if (code[2] && code[3]) {
            desc.HS = *code[2];
            desc.DS = *code[3];
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
        }
        desc.BlendState = blend_desc(state.blend);
        if (state.custom_blend.custom & owned::D3D12NativeState::kCustomBlendOver) {
            auto& target = desc.BlendState.RenderTarget[0];
            const auto& c = state.custom_blend;
            target.BlendEnable = TRUE;
            target.SrcBlend = static_cast<D3D12_BLEND>(c.source);
            target.DestBlend = static_cast<D3D12_BLEND>(c.destination);
            target.BlendOp = static_cast<D3D12_BLEND_OP>(c.op);
            target.SrcBlendAlpha = static_cast<D3D12_BLEND>(c.source_alpha);
            target.DestBlendAlpha = static_cast<D3D12_BLEND>(c.destination_alpha);
            target.BlendOpAlpha = static_cast<D3D12_BLEND_OP>(c.op_alpha);
            target.RenderTargetWriteMask = UINT8(c.write_mask);
        }
        if (state.custom_blend.custom & owned::D3D12NativeState::kCustomBlendMotionTargets) {
            // The object motion (RG) and its depth (R) at targets 1 and 2 take
            // the output where its alpha is 1 and keep their value where it is 0.
            desc.BlendState.IndependentBlendEnable = TRUE;
            for (UINT index = 1; index < 3; ++index) {
                auto& target = desc.BlendState.RenderTarget[index];
                target.BlendEnable = TRUE;
                target.SrcBlend = target.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
                target.DestBlend = target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                target.BlendOp = target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                target.LogicOp = D3D12_LOGIC_OP_NOOP;
                target.RenderTargetWriteMask = UINT8(index == 1 ? D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN
                                                                : D3D12_COLOR_WRITE_ENABLE_RED);
            }
        }
        desc.RasterizerState.FillMode = static_cast<D3D12_FILL_MODE>(state.raster.FillMode);
        desc.RasterizerState.CullMode = static_cast<D3D12_CULL_MODE>(state.raster.CullMode);
        desc.RasterizerState.FrontCounterClockwise = state.raster.FrontCounterClockwise;
        desc.RasterizerState.DepthBias = state.raster.DepthBias;
        desc.RasterizerState.DepthBiasClamp = state.raster.DepthBiasClamp;
        desc.RasterizerState.SlopeScaledDepthBias = state.raster.SlopeScaledDepthBias;
        desc.RasterizerState.DepthClipEnable = state.raster.DepthClipEnable;
        const auto& depth = state.depth;
        desc.DepthStencilState.DepthEnable = depth.DepthEnable;
        desc.DepthStencilState.DepthWriteMask = static_cast<D3D12_DEPTH_WRITE_MASK>(depth.DepthWriteMask);
        desc.DepthStencilState.DepthFunc = depth.DepthFunc ? static_cast<D3D12_COMPARISON_FUNC>(depth.DepthFunc) : D3D12_COMPARISON_FUNC_LESS;
        desc.DepthStencilState.StencilEnable = depth.StencilEnable;
        desc.DepthStencilState.StencilReadMask = depth.StencilReadMask;
        desc.DepthStencilState.StencilWriteMask = depth.StencilWriteMask;
        const auto face = [](const D3D11_DEPTH_STENCILOP_DESC& in) {
            D3D12_DEPTH_STENCILOP_DESC out{};
            out.StencilFailOp = in.StencilFailOp ? static_cast<D3D12_STENCIL_OP>(in.StencilFailOp) : D3D12_STENCIL_OP_KEEP;
            out.StencilDepthFailOp = in.StencilDepthFailOp ? static_cast<D3D12_STENCIL_OP>(in.StencilDepthFailOp) : D3D12_STENCIL_OP_KEEP;
            out.StencilPassOp = in.StencilPassOp ? static_cast<D3D12_STENCIL_OP>(in.StencilPassOp) : D3D12_STENCIL_OP_KEEP;
            out.StencilFunc = in.StencilFunc ? static_cast<D3D12_COMPARISON_FUNC>(in.StencilFunc) : D3D12_COMPARISON_FUNC_ALWAYS;
            return out;
        };
        desc.DepthStencilState.FrontFace = face(depth.FrontFace);
        desc.DepthStencilState.BackFace = face(depth.BackFace);
        desc.DSVFormat = formats[8];
        if (formats[8] == DXGI_FORMAT_UNKNOWN) {  // no depth bound: no depth or stencil test
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DepthStencilState.StencilEnable = FALSE;
        }
        return desc;
    }

    void set_hit_test_frame(bool active) noexcept override { hit_test_frame_ = active; }

    // A requested pipeline when built; a failure is named once. The game's
    // one-shot zone mask is copied immediately, so it cannot omit cold work
    // and rely on another displayed frame to finish it.
    ID3D12PipelineState* ready_pipeline(std::uint32_t id) {
        ID3D12PipelineState* pipeline = pipelines_.get(id);
        if (!pipeline && hit_test_frame_) {
            pipelines_.wait_all();
            pipeline = pipelines_.get(id);
        }
        if (!pipeline) {
            const std::string why = pipelines_.failure(id);
            if (!why.empty()) report_once(native_pipeline_failures_, "a native pipeline", why);
        }
        return pipeline;
    }

    // Null views for unset slots, the default sampler, the indirect draw
    // signature and the interface depth.
    bool create_native_objects(std::string& reason) {
        ID3D12Device* device = core_.device();
        null_srv_ = core_.staging().allocate();
        null_uav_ = core_.staging().allocate();
        if (!null_srv_.ptr || !null_uav_.ptr) { reason = "no staging descriptors for null views"; return false; }
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(nullptr, &srv, null_srv_);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = DXGI_FORMAT_R32_UINT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        device->CreateUnorderedAccessView(nullptr, nullptr, &uav, null_uav_);
        default_sampler_.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        default_sampler_.AddressU = default_sampler_.AddressV = default_sampler_.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        default_sampler_.MaxAnisotropy = 1;
        default_sampler_.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        default_sampler_.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_INDIRECT_ARGUMENT_DESC argument{};
        argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        D3D12_COMMAND_SIGNATURE_DESC signature{};
        signature.ByteStride = 16;  // DrawInstancedIndirect's four words
        signature.NumArgumentDescs = 1;
        signature.pArgumentDescs = &argument;
        const HRESULT hr = device->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&draw_signature_));
        if (FAILED(hr)) { reason = hresult_text("CreateCommandSignature (indirect draws)", hr); return false; }
        // DispatchIndirect's three words and DrawIndexedInstancedIndirect's five.
        D3D12_INDIRECT_ARGUMENT_DESC dispatch{};
        dispatch.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        signature.ByteStride = 12;
        signature.pArgumentDescs = &dispatch;
        HRESULT made = device->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&dispatch_signature_));
        if (FAILED(made)) { reason = hresult_text("CreateCommandSignature (indirect dispatches)", made); return false; }
        D3D12_INDIRECT_ARGUMENT_DESC indexed{};
        indexed.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        signature.ByteStride = 20;
        signature.pArgumentDescs = &indexed;
        made = device->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&indexed_signature_));
        if (FAILED(made)) { reason = hresult_text("CreateCommandSignature (indexed indirect draws)", made); return false; }
        return create_depth(reason);
    }

    bool create_depth(std::string& reason) {
        depth_.Reset();
        if (!depth_heap_) {
            D3D12_DESCRIPTOR_HEAP_DESC heap{};
            heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            heap.NumDescriptors = 1;
            const HRESULT hr = core_.device()->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&depth_heap_));
            if (FAILED(hr)) { reason = hresult_text("CreateDescriptorHeap (interface depth)", hr); return false; }
            depth_dsv_ = depth_heap_->GetCPUDescriptorHandleForHeapStart();
        }
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = presenter_.width();
        desc.Height = presenter_.height();
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = kDepthFormat;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = kDepthFormat;
        clear.DepthStencil = {1.0f, 0};
        const HRESULT hr = core_.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                                   &clear, IID_PPV_ARGS(&depth_));
        if (FAILED(hr)) { reason = hresult_text("CreateCommittedResource (interface depth)", hr); return false; }
        core_.device()->CreateDepthStencilView(depth_.Get(), nullptr, depth_dsv_);
        return true;
    }

    // Null unbinds a slot/target. Any non-null resource must belong to this
    // session before its descriptors, GPU addresses or bindless index are used.
    bool accept_resource(const owned::D3D12Buffer* buffer) {
        if (!buffer || (buffer->owner == resource_owner_ && buffer->resource)) return true;
        fail(DXGI_ERROR_DEVICE_RESET, "buffer is empty or belongs to another D3D12 executor");
        return false;
    }
    bool accept_resource(const D3D12Texture* texture) {
        // Depth-only targets own depth without a colour resource. Shadow
        // history copies and bindings must accept that allocation too.
        if (!texture || (texture->owner == resource_owner_ && (texture->resource || texture->depth))) return true;
        fail(DXGI_ERROR_DEVICE_RESET, "texture is empty or belongs to another D3D12 executor");
        return false;
    }

    // A device-level failure ends this backend: named once, every later call fails.
    HRESULT fail(HRESULT hr, const std::string& reason) {
        if (failure_.empty()) {
            const std::string removal = core_.removal_report();
            failure_ = removal.empty() || reason.find(removal) != std::string::npos ? reason : reason + "; " + removal;
            if (failure_.empty()) failure_ = "the D3D12 backend failed";
            const std::string crumbs = breadcrumb_report();
            if (!crumbs.empty()) failure_ += "; " + crumbs;
            remote::host_log("d3d12: %s", failure_.c_str());
            if (debug_) debug_message_summary();
            if (g_hazards) g_hazards->summary("at the failure");
        }
        return FAILED(hr) ? hr : E_FAIL;
    }
    void report_once(bool& reported, const char* what, const std::string& reason) {
        if (reported) return;
        reported = true;
        remote::host_log("d3d12: %s failed: %s (the quads needing it are omitted)", what, reason.c_str());
    }

    bool request_programs(std::string& reason) {
        const auto dxil = [&](const owned::ShaderProgram& program, D3D12_SHADER_BYTECODE& code) {
            const std::string name = owned::dxil_name(program);
            code = resource_program(name.c_str());
            if (!code.BytecodeLength) reason = "the host has no DXIL for " + std::string(program.entry ? program.entry : "?") + " (" + name + ")";
            return code.BytecodeLength != 0;
        };
        if (!dxil(owned::interface_shader_program(owned::InterfaceShader::game_vs), ui_vs_) ||
            !dxil(owned::interface_shader_program(owned::InterfaceShader::game_ps), ui_ps_[size_t(D3D12UiProgram::game)]) ||
            !dxil(owned::world_support_shader_program(owned::WorldSupportShader::interface_scaled_ps),
                  ui_ps_[size_t(D3D12UiProgram::interface_scaled)]) ||
            !dxil(owned::world_support_shader_program(owned::WorldSupportShader::interface_text_ps),
                  ui_ps_[size_t(D3D12UiProgram::interface_text)]) ||
            !dxil(owned::interface_shader_program(owned::InterfaceShader::backdrop_ps), ui_ps_[size_t(D3D12UiProgram::backdrop)]))
            return false;
        // The UI's one blend (its overlay state; a faded widget blends the same).
        for (size_t program = 0; program < size_t(D3D12UiProgram::count); ++program)
            (void)ui_pipeline(D3D12UiProgram(program), owned::fixed_blend_choice(TRUE, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, false));
        const D3D12_SHADER_BYTECODE imgui_vs = resource_program("D3D12_IMGUI_VS"), imgui_ps = resource_program("D3D12_IMGUI_PS");
        if (!imgui_vs.BytecodeLength || !imgui_ps.BytecodeLength) { reason = "the host has no overlay DXIL (D3D12_IMGUI_*)"; return false; }
        static const D3D12_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = base_desc();
        desc.VS = imgui_vs;
        desc.PS = imgui_ps;
        desc.InputLayout = {layout, UINT(std::size(layout))};
        auto& blend = desc.BlendState.RenderTarget[0];  // as Dear ImGui's own backends
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        imgui_pipeline_ = pipelines_.request(L"overlay.imgui.bgra8", desc);
        return true;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC base_desc() const {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = core_.root_signature();
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.SampleMask = UINT_MAX;
        desc.RasterizerState = raster_desc();
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = depth_target_ ? slice_color_targets() : target_count_;
        desc.RTVFormats[0] = depth_target_ ? slice_color_format() : target_format();
        for (UINT index = 1; index < target_count_ && !depth_target_; ++index) desc.RTVFormats[index] = extra_format(index);
        desc.SampleDesc.Count = 1;
        return desc;
    }

    // The bound targets as a pipeline sees them: colour formats (none for a
    // depth-only draw) and [8] the depth format.
    void current_formats(DXGI_FORMAT (&formats)[9], std::uint32_t& targets) const {
        for (DXGI_FORMAT& format : formats) format = DXGI_FORMAT_UNKNOWN;
        targets = depth_target_ ? slice_color_targets() : target_count_;
        if (targets) formats[0] = depth_target_ ? slice_color_format() : target_format();
        for (UINT index = 1; index < targets; ++index) formats[index] = extra_format(index);
        formats[8] = bound_depth_format();
    }

    // base_desc for recorded targets instead of the bound ones.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC targets_desc(UINT targets, const DXGI_FORMAT (&formats)[9]) const {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = base_desc();
        desc.NumRenderTargets = targets;
        for (UINT index = 0; index < 8; ++index) desc.RTVFormats[index] = index < targets ? formats[index] : DXGI_FORMAT_UNKNOWN;
        return desc;
    }

    static std::wstring compute_name(const std::string& program) { return L"native.cs." + std::wstring(program.begin(), program.end()); }
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute_desc(const D3D12_SHADER_BYTECODE& code) const {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = core_.root_signature();
        desc.CS = code;
        return desc;
    }

    static std::wstring ui_name(D3D12UiProgram program, const owned::FixedBlend& blend, DXGI_FORMAT format) {
        wchar_t name[96];
        swprintf_s(name, L"ui.%u.blend%u.%u.%u.f%u", unsigned(program), unsigned(blend.enabled != 0), unsigned(blend.source),
                   unsigned(blend.destination), unsigned(format));
        return name;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC ui_desc(D3D12UiProgram program, const owned::FixedBlend& blend, UINT targets,
                                               const DXGI_FORMAT (&formats)[9]) const {
        static const D3D12_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = targets_desc(targets, formats);
        desc.VS = ui_vs_;
        desc.PS = ui_ps_[size_t(program)];
        desc.InputLayout = {layout, UINT(std::size(layout))};
        desc.BlendState = blend_desc(blend);
        return desc;
    }

    // The recorded pipelines (eid3d11_d3d12_pipelines.list beside the
    // library): every pipeline this machine's sessions used, by its programs'
    // identities, so a start requests (and waits for) all of them and their
    // first use draws instead of omitting. A record unused for
    // kRecordIdleStarts starts is dropped, as is one whose program this build
    // no longer has. A package may carry eid3d11_d3d12_pipelines.shipped.list
    // (a harness run's list, staged under that name): read first, never
    // written, so a player's first start is warm too.
    static constexpr std::uint32_t kRecordMagic = 0x4c504945u;  // "EIPL"
    static constexpr std::uint32_t kRecordVersion = 2;  // 2: exact program names beside the identities
    static constexpr std::uint32_t kRecordIdleStarts = 30;
    struct PipelineRecord {
        enum : std::uint32_t { kNative = 0, kCompute = 1, kInterface = 2 };
        std::uint32_t kind;
        std::uint32_t targets;        // colour targets (0: depth only)
        std::uint64_t programs[4];    // vertex, pixel, hull, domain identities (0: none); compute [0]; interface [0] the program
        std::uint64_t exact[4];       // the same programs' DXIL names (name_hash): found first, so programs sharing an identity still resolve
        owned::D3D12NativeState state;  // interface: state.blend
        DXGI_FORMAT formats[9];       // colour targets, [8] depth
        std::uint32_t idle_starts;    // starts since a session last used it
        DXGI_FORMAT ui_format;        // interface: the format its name carries
        PipelineRecord() noexcept { std::memset(static_cast<void*>(this), 0, sizeof(*this)); }
    };
    struct RecordHeader {
        std::uint32_t magic, version, record_bytes, count;
    };
    static std::uint64_t name_hash(const std::string& name) noexcept {
        std::uint64_t hash = 1469598103934665603ull;
        for (const char c : name) hash = (hash ^ std::uint8_t(c)) * 1099511628211ull;
        return hash ? hash : 1;
    }

    // A pipeline requested for the first time this session: recorded (or its
    // record marked used). True when it was not warmed at start (the caller
    // names it in the log).
    bool note_pipeline(const std::wstring& name, const PipelineRecord& record) {
        auto found = records_.find(name);
        if (found != records_.end()) {
            if (found->second.idle_starts) {
                found->second.idle_starts = 0;
                records_dirty_ = true;
            }
            return false;
        }
        records_.emplace(name, record);
        records_dirty_ = true;
        if (!warmed_) return false;
        ++unwarmed_;
        return true;
    }

    // A list's records, or none (absent, another layout or damaged: named).
    static std::vector<PipelineRecord> read_records(const std::wstring& path, const char* what) {
        std::vector<char> bytes;
        {
            std::ifstream in(path, std::ios::binary);
            if (in) bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        std::vector<PipelineRecord> records;
        if (bytes.empty()) return records;
        RecordHeader header{};
        if (bytes.size() >= sizeof(header)) std::memcpy(&header, bytes.data(), sizeof(header));
        if (header.magic != kRecordMagic || header.version != kRecordVersion || header.record_bytes != sizeof(PipelineRecord) ||
            bytes.size() != sizeof(header) + std::size_t(header.count) * sizeof(PipelineRecord)) {
            remote::host_log("d3d12: the %s pipeline list is from another build layout or damaged; not used", what);
            return records;
        }
        records.resize(header.count);
        std::memcpy(static_cast<void*>(records.data()), bytes.data() + sizeof(header), records.size() * sizeof(PipelineRecord));
        return records;
    }

    // At start: requests every recorded pipeline this build can still make.
    void request_recorded(const std::wstring& directory, std::uint32_t& requested, std::uint32_t& dropped, std::uint32_t& shipped) {
        requested = dropped = shipped = 0;
        records_path_ = directory + L"eid3d11_d3d12_pipelines.list";
        records_dirty_ = true;  // idle counts change; a damaged list is replaced
        std::vector<PipelineRecord> records = read_records(directory + L"eid3d11_d3d12_pipelines.shipped.list", "shipped");
        for (PipelineRecord& record : records) record.idle_starts = 0;  // shipped: never idle
        const std::size_t shipped_count = records.size();
        {
            std::vector<PipelineRecord> own = read_records(records_path_, "recorded");
            records.insert(records.end(), own.begin(), own.end());
        }
        if (records.empty()) return;  // nothing recorded yet
        // This build's programs (the DXIL table's program list), by exact
        // DXIL name and by identity (ambiguous when it names several).
        struct Found {
            const std::string* name = nullptr;
            D3D12_SHADER_BYTECODE code{};
            bool ambiguous = false;
        };
        std::vector<owned::ShaderProgram> programs;
        owned::append_shader_groups(0xFFFFFFFFu, programs);
        owned::append_d3d12_shader_programs(programs);
        std::vector<std::string> names(programs.size());
        std::unordered_map<std::uint64_t, Found> by_identity, by_exact;
        for (std::size_t index = 0; index < programs.size(); ++index) {
            names[index] = program_dxil_name(programs[index]);
            const D3D12_SHADER_BYTECODE code = resource_program(names[index].c_str());
            by_exact.emplace(name_hash(names[index]), Found{&names[index], code, false});
            auto added = by_identity.emplace(program_identity(programs[index]), Found{&names[index], code, false});
            if (!added.second && *added.first->second.name != names[index]) added.first->second.ambiguous = true;
        }
        const auto resolve = [&](std::uint64_t identity, std::uint64_t exact, const D3D12_SHADER_BYTECODE*& code, const std::string*& name) {
            code = nullptr;
            name = nullptr;
            if (!identity) return true;
            auto found = by_exact.find(exact);
            if (found == by_exact.end()) {
                found = by_identity.find(identity);
                if (found == by_identity.end() || found->second.ambiguous) return false;
            }
            if (!found->second.code.BytecodeLength) return false;
            code = &found->second.code;
            name = found->second.name;
            return true;
        };
        for (std::size_t index = 0; index < records.size(); ++index) {
            PipelineRecord record = records[index];
            if (record.idle_starts >= kRecordIdleStarts) { ++dropped; continue; }
            if (index >= shipped_count) ++record.idle_starts;  // reset when a session uses it
            std::wstring name;
            if (record.kind == PipelineRecord::kNative) {
                const D3D12_SHADER_BYTECODE* code[4]{};
                const std::string* program_names[4]{};
                bool ok = record.programs[0] != 0;
                for (int stage = 0; stage < 4 && ok; ++stage)
                    ok = resolve(record.programs[stage], record.exact[stage], code[stage], program_names[stage]);
                if (!ok || record.targets > 8) { ++dropped; continue; }
                name = native_name(program_names, record.state, record.formats);
                if (!records_.count(name)) pipelines_.request(name, native_desc(code, record.state, record.formats, record.targets));
            } else if (record.kind == PipelineRecord::kCompute) {
                const D3D12_SHADER_BYTECODE* code = nullptr;
                const std::string* program_name = nullptr;
                if (!record.programs[0] || !resolve(record.programs[0], record.exact[0], code, program_name)) { ++dropped; continue; }
                name = compute_name(*program_name);
                if (!records_.count(name)) pipelines_.request(name, compute_desc(*code));
            } else if (record.kind == PipelineRecord::kInterface && record.programs[0] < std::uint64_t(D3D12UiProgram::count) &&
                       record.targets <= 8) {
                const D3D12UiProgram program = D3D12UiProgram(record.programs[0]);
                name = ui_name(program, record.state.blend, record.ui_format);
                if (!records_.count(name)) pipelines_.request(name, ui_desc(program, record.state.blend, record.targets, record.formats));
            } else {
                ++dropped;
                continue;
            }
            if (records_.emplace(name, record).second) {
                ++requested;
                if (index < shipped_count) ++shipped;
            }
        }
    }

    // Writes the pipeline library and the recorded list (when either changed).
    void save_pipelines(const char* when) {
        LARGE_INTEGER frequency{}, begin{}, end{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&begin);
        std::string reason;
        bool library_written = false;
        if (!pipelines_.save(reason, &library_written)) remote::host_log("d3d12: pipeline library not saved (%s): %s", when, reason.c_str());
        bool list_written = false;
        if (records_dirty_ && !records_path_.empty()) {
            std::vector<char> bytes(sizeof(RecordHeader) + records_.size() * sizeof(PipelineRecord));
            const RecordHeader header{kRecordMagic, kRecordVersion, std::uint32_t(sizeof(PipelineRecord)), std::uint32_t(records_.size())};
            std::memcpy(bytes.data(), &header, sizeof(header));
            std::size_t at = sizeof(header);
            for (const auto& record : records_) {
                std::memcpy(bytes.data() + at, &record.second, sizeof(PipelineRecord));
                at += sizeof(PipelineRecord);
            }
            const std::wstring temporary = records_path_ + L".tmp";
            {
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                list_written = bool(out);
            }
            list_written = list_written && MoveFileExW(temporary.c_str(), records_path_.c_str(), MOVEFILE_REPLACE_EXISTING);
            if (list_written) records_dirty_ = false;
            else remote::host_log("d3d12: the recorded pipeline list could not be written (%s; error %lu)", when, GetLastError());
        }
        records_saved_tick_ = GetTickCount64();
        QueryPerformanceCounter(&end);
        const double ms = double(end.QuadPart - begin.QuadPart) * 1000.0 / double(frequency.QuadPart);
        if (library_written)
            remote::host_log("d3d12: pipelines saved (%s): library written, %u session pipelines in it (%s), %zu KB; list %s, %zu records; "
                             "%.1f ms", when, pipelines_.saved_count(),
                             pipelines_.save_refused() ? "the new library refused some: the loaded library plus new ones was written"
                                                       : "a new library of exactly these",
                             pipelines_.saved_bytes() >> 10, list_written ? "written" : "unchanged", records_.size(), ms);
        else if (list_written)
            remote::host_log("d3d12: pipelines saved (%s): library unchanged (nothing new was created); list written, %zu records; %.1f ms",
                             when, records_.size(), ms);
    }

    // The shared UI program with this blend: requested once (built ahead for
    // the UI's blend at start); null until ready or after it failed (named once).
    ID3D12PipelineState* fixed_pipeline(const owned::D3D12UiDraw& draw) {
        DXGI_FORMAT formats[9]{}; std::uint32_t targets = 0;
        current_formats(formats, targets);
        const std::string identity = "embedded.fixed." + std::to_string(unsigned(draw.fixed_topology));
        const std::string* names[4] = {&identity, nullptr, nullptr, nullptr};
        const std::wstring name = native_name(names, *draw.fixed_state, formats);
        auto found = fixed_pipelines_.find(name);
        if (found == fixed_pipelines_.end()) {
            const D3D12_SHADER_BYTECODE* code[4] = {&ui_vs_, &ui_ps_[size_t(D3D12UiProgram::game)], nullptr, nullptr};
            auto desc = native_desc(code, *draw.fixed_state, formats, targets);
            desc.InputLayout = ui_desc(D3D12UiProgram::game, draw.blend, targets, formats).InputLayout;
            desc.PrimitiveTopologyType = draw.fixed_topology == D3DPT_POINTLIST ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT :
                draw.fixed_topology == D3DPT_LINELIST || draw.fixed_topology == D3DPT_LINESTRIP ?
                D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            found = fixed_pipelines_.emplace(name, pipelines_.request(name, desc)).first;
        }
        return ready_pipeline(found->second);
    }

    ID3D12PipelineState* ui_pipeline(D3D12UiProgram program, const owned::FixedBlend& blend) {
        const std::uint64_t key = (std::uint64_t(target_format()) << 56) | (std::uint64_t(program) << 48) | (std::uint64_t(blend.enabled != 0) << 40) |
                                  (std::uint64_t(blend.source) << 20) | blend.destination;
        auto found = ui_pipelines_.find(key);
        if (found == ui_pipelines_.end()) {
            PipelineRecord record;
            record.kind = PipelineRecord::kInterface;
            record.programs[0] = std::uint64_t(program);
            record.state.blend = blend;
            current_formats(record.formats, record.targets);
            record.ui_format = target_format();
            const std::wstring name = ui_name(program, blend, target_format());
            found = ui_pipelines_.emplace(key, pipelines_.request(name, ui_desc(program, blend, record.targets, record.formats))).first;
            if (note_pipeline(name, record))
                remote::host_log("d3d12: pipeline not warmed at start, built on first use (its quads are omitted until ready; "
                                 "recorded for the next start): interface program %u blend %u/%u/%u format %u",
                                 unsigned(program), unsigned(blend.enabled != 0), unsigned(blend.source), unsigned(blend.destination),
                                 unsigned(target_format()));
        }
        ID3D12PipelineState* pipeline = pipelines_.get(found->second);
        if (!pipeline) {
            const std::string why = pipelines_.failure(found->second);
            if (!why.empty()) report_once(pipeline_failures_, "a UI pipeline", why);
        }
        return pipeline;
    }

    // The sampler's persistent heap index (the table spans s0-s15 from it).
    std::uint32_t sampler_index(const D3D11_SAMPLER_DESC& source) {
        D3D12_SAMPLER_DESC desc{};
        desc.Filter = static_cast<D3D12_FILTER>(source.Filter);
        desc.AddressU = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(source.AddressU);
        desc.AddressV = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(source.AddressV);
        desc.AddressW = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(source.AddressW);
        desc.MipLODBias = source.MipLODBias;
        desc.MaxAnisotropy = (std::max)(1u, source.MaxAnisotropy);
        desc.ComparisonFunc = source.ComparisonFunc ? static_cast<D3D12_COMPARISON_FUNC>(source.ComparisonFunc) : D3D12_COMPARISON_FUNC_NEVER;
        std::memcpy(desc.BorderColor, source.BorderColor, sizeof(desc.BorderColor));
        desc.MinLOD = source.MinLOD;
        desc.MaxLOD = source.MaxLOD;
        std::string key(reinterpret_cast<const char*>(&desc), sizeof(desc));
        const auto found = samplers_.find(key);
        if (found != samplers_.end()) return found->second;
        const std::uint32_t index = core_.samplers().allocate();
        if (index == UINT32_MAX || index + kD3D11SamplerSlots > Core::kSamplerPersistent) {
            report_once(sampler_failures_, "sampler creation", "the sampler heap is full");
            return UINT32_MAX;
        }
        core_.device()->CreateSampler(&desc, core_.samplers().cpu(index));
        samplers_.emplace(std::move(key), index);
        return index;
    }

    void update_imgui_texture(ImTextureData* texture) {
        if (texture->Status == ImTextureStatus_WantCreate) {
            owned::D3D12TextureRef created;
            if (FAILED(create_texture(UINT(texture->Width), UINT(texture->Height), DXGI_FORMAT_R8G8B8A8_UNORM, created))) return;
            const RECT whole{0, 0, texture->Width, texture->Height};
            if (FAILED(write_texture(*created, whole, texture->GetPixels(), UINT(texture->GetPitch())))) return;
            const ImTextureID id = ImTextureID(reinterpret_cast<std::uintptr_t>(created.get()));
            imgui_textures_[id] = created;
            texture->BackendUserData = created.get();
            texture->SetTexID(id);
            texture->SetStatus(ImTextureStatus_OK);
        } else if (texture->Status == ImTextureStatus_WantUpdates) {
            const auto found = imgui_textures_.find(texture->GetTexID());
            if (found == imgui_textures_.end()) return;
            for (const ImTextureRect& rect : texture->Updates) {
                const RECT region{rect.x, rect.y, rect.x + rect.w, rect.y + rect.h};
                if (FAILED(write_texture(*found->second, region, texture->GetPixelsAt(rect.x, rect.y), UINT(texture->GetPitch())))) return;
            }
            texture->SetStatus(ImTextureStatus_OK);
        }
        if (texture->Status == ImTextureStatus_WantDestroy && texture->UnusedFrames > 0) destroy_imgui_texture(texture);
    }
    void destroy_imgui_texture(ImTextureData* texture) {
        imgui_textures_.erase(texture->GetTexID());
        texture->SetTexID(ImTextureID_Invalid);
        texture->BackendUserData = nullptr;
        texture->SetStatus(ImTextureStatus_Destroyed);
    }

    Core core_;
    std::shared_ptr<owned::D3D12ResourceOwner> resource_owner_ =
        std::make_shared<owned::D3D12ResourceOwner>(&core_);
    UploadRing upload_;
    UploadRing bulk_;  // one-off load data (write_buffer_bulk): never grows the per-frame blocks
    std::uint64_t upload_buffer_bytes_ = 0, upload_texture_bytes_ = 0;  // this frame's write_buffer / write_texture bytes (upload_end_frame)
    std::shared_ptr<Preparer> preparer_;  // off-render-thread uploads (finish_buffer / finish_texture)
    Presenter presenter_;
    Pipelines pipelines_;
    ID3D12GraphicsCommandList* list_ = nullptr;  // the frame being recorded, else null
    bool target_ = false;         // the back buffer is taken (and cleared) this frame
    bool drawn_ = false;          // something was drawn on it this frame
    D3D12Texture* offscreen_ = nullptr;  // set_target: the bound offscreen target, else the back buffer
    owned::D3D12TextureRef frame_image_;  // fg_frame_image: the frame's back-buffer image (output size)
    bool frame_image_wanted_ = false;     // set_frame_image: used from the next frame taken
    bool frame_image_active_ = false;     // this frame draws into frame_image_ (composite_frame_image not run yet)
    // Frame generation: the swap chain on the shared direct queue (fg_on_), each
    // frame's real and generated screen images (ring slot fg_slot_), and the
    // screen the back buffer is after composite_frame_image (null: the
    // swap-chain buffer).
    d3d12::Pacer pacer_;
    owned::FrameLimiter presentation_limiter_;  // normal/FSR producer, replay thread
    std::string switch_steps_;  // the last mode switch's step times (ms)
    int fg_wanted_ = 0;
    bool fg_on_ = false, generated_drawn_ = false;
    // FSR frame generation: the swap chain is the FidelityFX proxy (fsr_on_),
    // its frame generation context, this frame's HUD-less image, our real
    // presents and the proxy's interpolation dispatches when it came on.
    bool fsr_on_ = false;
    d3d12::FsrFrameGeneration fsr_;
    D3D12Texture* fsr_hudless_ = nullptr;
    std::uint64_t fsr_real_ = 0;
    std::uint64_t fsr_dispatch_base_ = 0;
    std::int64_t fsr_rate_tick_ = 0;  // log_fsr_rates' window start (QPC), its real count and dispatches
    std::uint64_t fsr_rate_real_ = 0;
    std::uint64_t fsr_rate_dispatches_ = 0;
    unsigned fg_slot_ = 0;
    owned::D3D12TextureRef real_[d3d12::Pacer::kSlots], generated_[d3d12::Pacer::kSlots];
    D3D12Texture* screen_ = nullptr;
    char fg_failure_[3][200] = {};  // per mode (1 DLSS-G, 2 FSR)
    bool offscreen_world_ = false;  // the bound offscreen target is the world's
    // set_targets: colour targets after offscreen_, and the target whose depth
    // is bound (depth_override_; null: none) instead of offscreen_'s.
    std::array<D3D12Texture*, 7> extra_targets_{};
    UINT target_count_ = 1;
    // The target set_target last bound as the world's (compared, never read).
    const D3D12Texture* world_target_ = nullptr;
    // A null render target view for null set_targets entries; the shader's
    // writes there are discarded. Its pipelines must name that slot UNKNOWN:
    // a null view under a pipeline that names a format is invalid (debug
    // layer error 613, the cave TDR in an interface ExecuteIndirect).
    static constexpr DXGI_FORMAT kNullTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;  // the null view's own description
    ComPtr<ID3D12DescriptorHeap> null_target_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE null_target_{};
    DXGI_FORMAT extra_format(UINT index) const noexcept {
        return extra_targets_[index - 1] ? extra_targets_[index - 1]->format : DXGI_FORMAT_UNKNOWN;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE null_target_view() noexcept {
        if (!null_target_.ptr) {
            D3D12_DESCRIPTOR_HEAP_DESC heap{};
            heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            heap.NumDescriptors = 1;
            if (FAILED(core_.device()->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&null_target_heap_)))) return {};
            null_target_ = null_target_heap_->GetCPUDescriptorHandleForHeapStart();
            D3D12_RENDER_TARGET_VIEW_DESC view{};
            view.Format = kNullTargetFormat;
            view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            core_.device()->CreateRenderTargetView(nullptr, &view, null_target_);
        }
        return null_target_;
    }
    bool depth_override_ = false;
    D3D12Texture* depth_owner_ = nullptr;
    // set_depth_target: the depth array slice the next draws write (no colour).
    D3D12Texture* depth_target_ = nullptr;
    D3D12Texture* depth_target_color_ = nullptr;  // set_depth_target colour (read only while depth_target_ is set)
    UINT depth_target_slice_ = 0;
    RECT depth_target_rect_{};
    bool world_drawn_ = false;      // something was drawn into the world target this frame
    struct StampSlot {
        d3d12::ComPtr<ID3D12QueryHeap> heap;
        d3d12::ComPtr<ID3D12Resource> readback;
        UINT capacity = 0, used = 0, wanted = 0;
        std::uint64_t first = 1, fence = 0;
        bool submitted = false;  // resolved into readback, not yet read
    };
    StampSlot stamp_slots_[Core::kFramesInFlight];
    std::uint64_t next_stamp_ = 1;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> stamp_history_;  // serial, ticks (a ring)
    bool bindless_full_ = false;  // a bindless index was refused this frame: the persistent region grows
    bool world_omitted_ = false;  // a world pass was omitted this frame: the marker colour
    bool hit_test_frame_ = false;  // one-shot data frame: no cold pipeline omissions
    float background_[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // the frame clear (the game's whole-target clear)
    std::string failure_;
    std::vector<std::pair<std::string, std::uint64_t>> omitted_;
    std::uint32_t frame_interface_omitted_ = 0;  // this frame's interface omissions (present logs them)
    std::string frame_interface_names_;
    std::uint64_t interface_omitted_frames_ = 0;
    std::vector<std::pair<std::string, std::uint64_t>> counted_;
    D3D12_SHADER_BYTECODE ui_vs_{}, ui_ps_[size_t(D3D12UiProgram::count)]{};
    std::unordered_map<std::uint64_t, std::uint32_t> ui_pipelines_;
    std::unordered_map<std::wstring, std::uint32_t> fixed_pipelines_;
    std::uint32_t imgui_pipeline_ = UINT32_MAX;
    std::unordered_map<std::string, std::uint32_t> samplers_;
    std::unordered_map<ImTextureID, owned::D3D12TextureRef> imgui_textures_;
    const char* last_marker_ = nullptr;  // marker(): the pass last marked in this list
    d3d12::ComPtr<ID3D12Resource> breadcrumb_buffer_;  // [0] last pass reached, [1] last finished (readback, mapped)
    const volatile std::uint32_t* breadcrumb_values_ = nullptr;
    d3d12::ComPtr<ID3D12GraphicsCommandList2> breadcrumb_list2_;
    const ID3D12GraphicsCommandList* breadcrumb_list_ = nullptr;
    std::array<const char*, 8192> breadcrumb_names_{};  // pass names by serial (string literals)
    bool debug_ = false;  // debug layer and GPU-based validation on (EI_RENDERER_D3D12_VALIDATE)
    std::unique_ptr<Hazards> hazards_;  // EI_RENDERER_D3D12_HAZARDS (g_hazards while it lives)
    owned::D3D12BufferRef gpu_reports_;  // u0 space3, bound to every native program (create_gpu_reports)
    std::vector<std::pair<std::uint64_t, std::uint64_t>> gpu_report_tickets_;  // readback ticket, frame
    std::map<std::string, unsigned long long> gpu_report_seen_;  // "loop:bound" reported, and how often
    std::uint32_t breadcrumb_serial_ = 0;
    bool breadcrumbs_failed_ = false;
    bool texture_failures_ = false, pipeline_failures_ = false, sampler_failures_ = false, sampler_table_failures_ = false;

    // ---- descriptor ring demand (open_list grows it) ----
    bool ring_full_ = false;
    std::uint32_t ring_frame_peak_ = 0, ring_frame_taken_ = 0;

    // ---- native passes ----
    static constexpr UINT kConstantSlots = 14;  // b0-b13
    static constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    struct Slot {
        D3D12_CPU_DESCRIPTOR_HANDLE view{};
        owned::D3D12Buffer* buffer = nullptr;
        D3D12Texture* texture = nullptr;
        bool depth = false;  // the texture's depth (set_depth_resource)
    };
    struct Constant {
        D3D12_GPU_VIRTUAL_ADDRESS address = 0;
        UINT bytes = 0;
    };
    std::array<Slot, kD3D11ResourceSlots> resources_{};
    std::array<Slot, kD3D11UnorderedSlots> unordered_{};
    std::array<Constant, kConstantSlots> constants_{};
    // The descriptors the current SRV/UAV/b1-b13 tables hold (bind_slots
    // binds them again while they match). Valid for one frame (frame_serial_),
    // while no staging handle changed hands (epoch) and no view was rewritten
    // in place (view_writes_).
    struct TableRecord {
        std::uint64_t frame = 0, epoch = 0, view_writes = 0;
        UINT srv_count = 0, uav_count = 0;
        bool constants_valid = false;
        D3D12_CPU_DESCRIPTOR_HANDLE srv[kD3D11ResourceSlots]{}, uav[kD3D11UnorderedSlots]{};
        Constant constants[kConstantSlots - 1]{};
    } tables_;
    std::uint64_t frame_serial_ = 1, view_writes_ = 0;
    std::array<D3D12_SAMPLER_DESC, kD3D11SamplerSlots> sampler_descs_{};
    std::array<bool, kD3D11SamplerSlots> samplers_set_{};
    D3D12_SAMPLER_DESC default_sampler_{};
    bool slots_dirty_ = true, sampler_ring_full_ = false, sampler_table_valid_ = false;
    std::array<D3D12_SAMPLER_DESC, kD3D11SamplerSlots> table_sampler_descs_{};  // the bound table's contents
    struct SamplerTable {
        std::uint64_t key;
        std::uint32_t index;
        std::array<D3D12_SAMPLER_DESC, kD3D11SamplerSlots> descs;
    };
    std::vector<SamplerTable> sampler_tables_;        // persistent tables by content (kept for the session)
    std::vector<SamplerTable> frame_sampler_tables_;  // this frame's ring tables (only once the persistent region is full)
    std::uint32_t resource_table_ = 0, unordered_table_ = 0, constant_table_ = 0, sampler_table_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS zero_constants_ = 0;  // this frame's zero b0
    D3D12_CPU_DESCRIPTOR_HANDLE null_srv_{}, null_uav_{};
    D3D12_CPU_DESCRIPTOR_HANDLE null_srv_kind_[16]{}, null_uav_kind_[16]{};  // by DXIL resource kind
    const ProgramCode* table_programs_[2]{};  // the programs the current tables were written for
    struct ProgramKey {
        const void* source;
        std::size_t size;
        const char* entry;
        const void* defines;
        bool operator<(const ProgramKey& other) const noexcept {
            return std::tie(source, size, entry, defines) < std::tie(other.source, other.size, other.entry, other.defines);
        }
    };
    struct ProgramCode {
        std::string name;
        std::string entry;            // for the log
        std::uint64_t identity = 0;   // program_identity (the recorded pipelines)
        D3D12_SHADER_BYTECODE bytecode{};
        // The DXIL resource kind (DXIL::ResourceKind, 0: not declared) the
        // program declares at each space-0 t and u register, from its PSV0
        // part: an unset slot gets a null view of exactly that kind.
        std::array<std::uint8_t, kD3D11ResourceSlots> srv_kind{};
        std::array<std::uint8_t, kD3D11UnorderedSlots> uav_kind{};
        // One past the highest declared t and u register: the table part the
        // program can read (the rest of the range is volatile and never read).
        UINT srv_count = 0, uav_count = 0;
        std::uint16_t sampler_mask = 0;  // the s registers it declares (s0-s15)
        bool reflected = false;  // its PSV0 entries were all read (else the tables are written in full)
        bool bindless = false;   // it declares SRVs outside space 0 (bindless reads; the hazard checker)
        std::uint16_t cbv_mask = 0;  // the b registers it declares (b0-b13; the hazard checker)
    };
    std::map<ProgramKey, ProgramCode> programs_;
    // A native pipeline's identity: its program records (stable map nodes),
    // the state and the target formats. Zeroed whole and copied byte for byte
    // (padding included), so it compares and hashes as bytes.
    struct NativePipelineKey {
        const ProgramCode* programs[4];
        owned::D3D12NativeState state;
        DXGI_FORMAT formats[9];
        NativePipelineKey() noexcept { std::memset(static_cast<void*>(this), 0, sizeof(*this)); }
        NativePipelineKey(const NativePipelineKey& other) noexcept { std::memcpy(static_cast<void*>(this), &other, sizeof(*this)); }
        NativePipelineKey& operator=(const NativePipelineKey& other) noexcept {
            std::memcpy(static_cast<void*>(this), &other, sizeof(*this));
            return *this;
        }
        bool operator==(const NativePipelineKey& other) const noexcept { return std::memcmp(this, &other, sizeof(*this)) == 0; }
    };
    struct NativePipelineHash {
        std::size_t operator()(const NativePipelineKey& key) const noexcept {
            const auto* bytes = reinterpret_cast<const unsigned char*>(&key);
            std::uint64_t hash = 0x9e3779b97f4a7c15ull, word = 0;
            std::size_t at = 0;
            for (; at + 8 <= sizeof(key); at += 8) {
                std::memcpy(&word, bytes + at, 8);
                hash = (hash ^ word) * 0xff51afd7ed558ccdull;
                hash ^= hash >> 32;
            }
            for (; at < sizeof(key); ++at) hash = (hash ^ bytes[at]) * 1099511628211ull;
            return std::size_t(hash);
        }
    };
    std::unordered_map<const ProgramCode*, std::uint32_t> compute_pipelines_;
    std::unordered_map<NativePipelineKey, std::uint32_t, NativePipelineHash> native_pipelines_;
    NativePipelineKey last_native_key_;
    std::uint32_t last_native_id_ = 0;
    bool last_native_valid_ = false;
    ComPtr<ID3D12CommandSignature> draw_signature_;
    ComPtr<ID3D12CommandSignature> dispatch_signature_, indexed_signature_;
    owned::D3D12Buffer* index_buffer_ = nullptr;  // set_index_buffer
    struct Readback {
        std::uint64_t ticket = 0, fence = 0, bytes = 0;
        ComPtr<ID3D12Resource> heap;
    };
    std::vector<Readback> readbacks_;
    std::uint64_t next_readback_ = 1;
    ComPtr<ID3D12Resource> depth_;
    ComPtr<ID3D12DescriptorHeap> depth_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE depth_dsv_{};
    bool buffer_failures_ = false, program_failures_ = false, native_pipeline_failures_ = false;
    // The recorded pipelines (request_recorded), by library name.
    std::map<std::wstring, PipelineRecord> records_;
    std::wstring records_path_;
    bool records_dirty_ = false;
    bool warmed_ = false;            // the start's requests are done: later first uses are named
    std::uint32_t unwarmed_ = 0;     // pipelines first requested after the start
    ULONGLONG records_saved_tick_ = 0;
};

} // namespace

// Core mode with renderer_backend=d3d12 (Host/core_host.cpp): the executor in
// the host's child window, or null with reason naming what is missing
// (below the floor, no DXIL, a failed call). The embedded caller must report
// failure and refuse startup; it must not substitute a D3D11 renderer.
std::unique_ptr<owned::D3D12Frame> create_frame_executor(HWND child, const std::wstring& directory, std::string& reason) {
    auto executor = std::make_unique<Executor>();
    if (!executor->create(child, directory, reason)) return nullptr;
    return executor;
}

const std::string& frame_executor_failure(owned::D3D12Frame& frame) noexcept {
    return static_cast<Executor&>(frame).failure();
}

} // namespace d3d12
