#include "ResourceMapping.h"
#include <res_mapping.h>
#include <psapi.h>
#include <map>
#include <algorithm>
#include <mutex>

namespace {
struct View {
    std::uint64_t bytes;
    std::uint64_t references;
    BY_HANDLE_FILE_INFORMATION file;
    bool fileKnown;
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
void* MapResourceView(HANDLE mapping, DWORD access, DWORD fileSize, HANDLE file) noexcept {
    auto& registry = state();
    std::lock_guard<std::mutex> serial(registry.mutex);
    BY_HANDLE_FILE_INFORMATION fileInfo = {};
    const bool known = access == FILE_MAP_READ && file != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(file, &fileInfo) && fileInfo.nNumberOfLinks &&
        !fileInfo.nFileSizeHigh && fileInfo.nFileSizeLow == fileSize;
    if (known) for (auto& entry : registry.views) {
        auto& value = entry.second;
        const auto& held = value.file;
        if (!value.readOnly || !value.fileKnown || value.bytes != fileSize ||
            held.dwVolumeSerialNumber != fileInfo.dwVolumeSerialNumber ||
            held.nFileIndexHigh != fileInfo.nFileIndexHigh || held.nFileIndexLow != fileInfo.nFileIndexLow ||
            held.ftLastWriteTime.dwHighDateTime != fileInfo.ftLastWriteTime.dwHighDateTime ||
            held.ftLastWriteTime.dwLowDateTime != fileInfo.ftLastWriteTime.dwLowDateTime) continue;
        // Each logical owner holds a read-only file handle denying writers.
        // The view outlives mapping handles; unmap its last logical reference.
        ++value.references;
        ++registry.counters.shared_reuses;
        registry.counters.active_saved_view_bytes += fileSize;
        registry.counters.peak_saved_view_bytes = (std::max)(registry.counters.peak_saved_view_bytes,
            registry.counters.active_saved_view_bytes);
        return reinterpret_cast<void*>(entry.first);
    }
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
        const bool readOnly = access == FILE_MAP_READ;
        s.views.emplace(reinterpret_cast<std::uintptr_t>(view),
            View{fileSize, 1, fileInfo, known, readOnly, std::move(physical)});
        ++s.counters.physical_map_calls;
        s.counters.physical_view_bytes += fileSize;
        if (!readOnly || s.views.at(reinterpret_cast<std::uintptr_t>(view)).physical.empty())
            ++s.counters.identity_rejections;
    } catch (...) {
        // Tracking is not allowed to change a successful resource load.
        // The renderer has an OS provenance fallback for an untracked view.
        try {
            auto& s = state();
            ++s.counters.tracking_capacity_failures;
        } catch (...) {}
    }
    return view;
}

BOOL UnmapResourceView(const void* view) noexcept {
    auto& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);
    auto held = s.views.find(reinterpret_cast<std::uintptr_t>(view));
    if (held != s.views.end() && held->second.references > 1) {
        --held->second.references;
        s.counters.active_saved_view_bytes -= held->second.bytes;
        ++s.counters.deferred_unmaps;
        return TRUE;
    }
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
    // Direct source calls share only proven read-only file incarnations.
    result.status = ResMappingStatus::active;
    result.physical_views = s.views.size();
    result.logical_views = 0;
    for (const auto& entry : s.views) result.logical_views += entry.second.references;
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
