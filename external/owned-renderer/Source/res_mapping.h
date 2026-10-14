#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <string>

namespace owned {

enum class ResMappingStatus : std::uint8_t {
    not_attempted,
    active,
    unsupported_image,
    import_contract_mismatch,
    foreign_hook,
    protection_failure,
    patch_race,
    inactive,
    teardown_foreign_hook,
};

struct ResMappingSnapshot {
    ResMappingStatus status = ResMappingStatus::not_attempted;
    bool hooks_active = false;
    std::uint64_t tracked_mapping_handles = 0;
    std::uint64_t logical_views = 0;
    std::uint64_t physical_views = 0;
    std::uint64_t physical_view_bytes = 0;
    std::uint64_t active_saved_view_bytes = 0;
    std::uint64_t peak_saved_view_bytes = 0;
    std::uint64_t shared_reuses = 0;
    std::uint64_t deferred_unmaps = 0;
    std::uint64_t passthrough_maps = 0;
    std::uint64_t physical_map_calls = 0;
    std::uint64_t physical_unmap_calls = 0;
    std::uint64_t identity_rejections = 0;
    std::uint64_t tracking_capacity_failures = 0;
    std::uint64_t hook_validation_failures = 0;
};

// Installs only when the res_mapping sites verify in game.exe (game_sites.h). The
// implementation additionally verifies the exact import names, IAT RVAs,
// resource-wrapper call sites, and unmodified system targets before changing
// any slot.
bool install_res_mapping_hooks(bool sites_verified) noexcept;
void uninstall_res_mapping_hooks() noexcept;
ResMappingSnapshot res_mapping_snapshot() noexcept;
// Mapped-file provenance ("\\?\GLOBALROOT" + device path) of a resource view
// these hooks mapped, by its exact base, captured once when the physical view
// was mapped and dropped when these hooks unmap it. False for any address
// that is not the base of a live tracked view (callers then query the OS).
bool res_mapping_view_provenance(const void* view_base, std::wstring& physical);
// The live offset-zero read view these hooks mapped that wholly contains
// [address, address+size): its base (the file origin) and provenance. The
// record lives exactly as long as the view (dropped before the hooked unmap),
// so a hit proves a committed read-only file view without asking the OS.
// False for views these hooks do not hold; callers then query the OS.
bool res_mapping_view_containing(const void* address, std::uint64_t size,
    const void*& view_base, std::wstring& physical);
const char* res_mapping_status_name(ResMappingStatus status) noexcept;

#ifdef EID3D11_TESTING
namespace res_mapping_test {

void reset() noexcept;
HANDLE create_file_mapping(HANDLE file, LPSECURITY_ATTRIBUTES attributes,
                           DWORD protect, DWORD maximum_high,
                           DWORD maximum_low, LPCSTR name,
                           bool supported_resource_callsite) noexcept;
LPVOID map_view_of_file(HANDLE mapping, DWORD access, DWORD offset_high,
                       DWORD offset_low, SIZE_T requested) noexcept;
BOOL unmap_view_of_file(LPCVOID base) noexcept;
BOOL close_handle(HANDLE handle) noexcept;

} // namespace res_mapping_test
#endif

} // namespace owned
