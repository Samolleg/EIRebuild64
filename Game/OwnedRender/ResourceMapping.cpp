#include "ResourceMapping.h"
#include <res_mapping.h>
#include <psapi.h>
#include <map>
#include <mutex>

namespace {
struct View {
    std::uint64_t bytes;
    bool readOnly;
    std::wstring physical;
};
struct MappingState {
    std::mutex mutex;
    std::map<std::uintptr_t, View> views;
    owned::ResMappingSnapshot counters;
};
MappingState& state() {
    // CMappedFile instances can be destroyed during static teardown.
    // Keep the registry alive until process exit, like the OS mapping table.
    static MappingState* value = new MappingState;
    return *value;
}
}

namespace OwnedRender {
void* MapResourceView(HANDLE mapping, DWORD access, DWORD fileSize) noexcept {
    void* view = MapViewOfFile(mapping, access, 0, 0, 0);
    if (!view) return nullptr;
    try {
        std::wstring physical(32768, L'\0');
        const DWORD length = GetMappedFileNameW(GetCurrentProcess(), view,
            &physical[0], static_cast<DWORD>(physical.size()));
        if (length && length < physical.size()) {
            physical.resize(length);
            physical.insert(0, L"\\\\?\\GLOBALROOT");
        } else physical.clear();
        auto& s = state();
        const std::lock_guard<std::mutex> lock(s.mutex);
        const bool readOnly = access == FILE_MAP_READ;
        s.views.emplace(reinterpret_cast<std::uintptr_t>(view),
            View{fileSize, readOnly, std::move(physical)});
        ++s.counters.physical_map_calls;
        s.counters.physical_view_bytes += fileSize;
        if (!readOnly || s.views.at(reinterpret_cast<std::uintptr_t>(view)).physical.empty())
            ++s.counters.identity_rejections;
    } catch (...) {
        // Tracking is not allowed to change a successful resource load.
        // The renderer has an OS provenance fallback for an untracked view.
        try {
            auto& s = state();
            const std::lock_guard<std::mutex> lock(s.mutex);
            ++s.counters.tracking_capacity_failures;
        } catch (...) {}
    }
    return view;
}

BOOL UnmapResourceView(const void* view) noexcept {
    auto& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);
    // A failed unmap leaves both the real mapping and its provenance alive.
    const BOOL result = UnmapViewOfFile(view);
    if (result) {
        const auto found = s.views.find(reinterpret_cast<std::uintptr_t>(view));
        if (found != s.views.end()) {
            s.counters.physical_view_bytes -= found->second.bytes;
            s.views.erase(found);
            ++s.counters.physical_unmap_calls;
        }
    }
    return result;
}
}

namespace owned {
ResMappingSnapshot res_mapping_snapshot() noexcept {
    auto& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);
    auto result = s.counters;
    // Direct source calls are active. No retail hooks are installed, and no
    // view deduplication/deferred unmapping is claimed by this implementation.
    result.status = ResMappingStatus::active;
    result.logical_views = result.physical_views = s.views.size();
    return result;
}

bool res_mapping_view_provenance(const void* view, std::wstring& physical) {
    auto& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);
    physical.clear();
    const auto found = s.views.find(reinterpret_cast<std::uintptr_t>(view));
    if (found == s.views.end() || !found->second.readOnly || found->second.physical.empty())
        return false;
    physical = found->second.physical;
    return true;
}

bool res_mapping_view_containing(const void* address, std::uint64_t size,
    const void*& viewBase, std::wstring& physical) {
    viewBase = nullptr;
    physical.clear();
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    if (!start || !size || size > UINTPTR_MAX - start) return false;
    auto& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);
    auto found = s.views.upper_bound(start);
    if (found == s.views.begin()) return false;
    --found;
    const auto offset = start - found->first;
    const auto& view = found->second;
    if (!view.readOnly || view.physical.empty() || offset >= view.bytes || size > view.bytes - offset)
        return false;
    viewBase = reinterpret_cast<const void*>(found->first);
    physical = view.physical;
    return true;
}
}
