// The x86-only half of the resource store: resolving an ANM the game's
// native mapper selected, from the borrowed span's mapped-file provenance
// (VirtualQuery, the res_mapping hooks, GetMappedFileNameW). It reads the
// game process's own views, so it never builds into the x64 render host.
#include "owned_resource_store.h"
#include "res_mapping.h"

#include <psapi.h>

#include <algorithm>
#include <cstdint>

namespace owned {

namespace {
void fail(ResourceFailure& failure, ResourceFailureCode code, const char* message) {
    failure.code = code;
    failure.message = message;
}
} // namespace

bool ResourceStore::resolve_native_mapped_anm(const std::string& resource_name,
    const void* selected_bytes, std::uint64_t selected_size,
    ResourceBlob& blob, ResourceFailure& failure) {
    blob = {};
    std::string canonical;
    if (!resource_canonical_name(resource_name, canonical, failure)) return false;
    if (canonical.size() < 5 || canonical.compare(canonical.size()-4, 4, ".anm") != 0) {
        fail(failure, ResourceFailureCode::invalid_name, "native animation selection requires an ANM member");
        return false;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(selected_bytes);
    if (!selected_bytes || !selected_size || selected_size > SIZE_MAX ||
        selected_size > UINTPTR_MAX-address) {
        fail(failure, ResourceFailureCode::archive_bounds, "native animation selection range");
        return false;
    }
    {
        // A live view the mapping hooks hold needs no OS query (L-021).
        const void* view_base = nullptr;
        std::wstring held;
        if (res_mapping_view_containing(selected_bytes, selected_size, view_base, held))
            return resolve_selected_file_impl(held, canonical, selected_bytes, selected_size,
                address-reinterpret_cast<std::uintptr_t>(view_base), true, blob, failure, nullptr, true);
    }
    const auto end = address+static_cast<std::uintptr_t>(selected_size);
    std::uintptr_t origin = 0;
    for (auto cursor = address; cursor < end;) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT || memory.Type != MEM_MAPPED ||
            memory.AllocationProtect != PAGE_READONLY || memory.Protect != PAGE_READONLY ||
            !memory.AllocationBase) {
            fail(failure, ResourceFailureCode::selection_mismatch,
                "native animation selection is not an original read-only file view");
            return false;
        }
        const auto allocation = reinterpret_cast<std::uintptr_t>(memory.AllocationBase);
        const auto region = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        if ((!origin ? address < allocation : origin != allocation) || region > cursor ||
            memory.RegionSize > UINTPTR_MAX-region || region+memory.RegionSize <= cursor) {
            fail(failure, ResourceFailureCode::archive_bounds, "native animation selection crosses its mapped allocation");
            return false;
        }
        origin = allocation;
        cursor = (std::min)(end, static_cast<std::uintptr_t>(region+memory.RegionSize));
    }
    std::wstring physical;
    if (!res_mapping_view_provenance(reinterpret_cast<const void*>(origin), physical)) {
        physical.assign(32768, L'\0');
        const DWORD count = GetMappedFileNameW(GetCurrentProcess(), const_cast<void*>(selected_bytes),
            physical.data(), static_cast<DWORD>(physical.size()));
        if (!count || count >= physical.size()) {
            fail(failure, ResourceFailureCode::file_open, "native animation selection has no mapped-file provenance");
            return false;
        }
        physical.resize(count);
        physical.insert(0, L"\\\\?\\GLOBALROOT");
    }
    // Existing installation validation proves the native mapper's offset-zero,
    // full-file read view. Never retain its address or infer identity from it;
    // the one scoped exception is the provenance the mapping hooks hold for a
    // live view they mapped (res_mapping_view_provenance).
    return resolve_selected_file_impl(physical, canonical, selected_bytes,
        selected_size, address-origin, true, blob, failure, nullptr, true);
}

} // namespace owned
