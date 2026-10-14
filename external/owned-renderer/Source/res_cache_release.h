#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace owned {

// Release routing for the game's file-read resource cache (CMappedResource).
//
// Get (vtable +14) returns heap data with one reference; Release (vtable +18,
// game VA 671E00) looks the data pointer up only in the instance it is called
// on and frees the entry when the last reference is released. The supported
// addon serves overridden files from its own private resource instances, but
// callers still release on the instance they asked. That Release misses, so
// every overridden file ever requested stays resident. The fix routes a
// missed Release to the private instance that owns the pointer, which restores
// the stock lifetime: an entry is freed when its last real user releases it.
enum class ResCacheReleaseStatus : std::uint8_t {
    not_attempted,
    active,
    addon_missing,  // no addon (or no addon contract): nothing to route
    contract_mismatch,
    protection_failure,
    inactive,
    teardown_foreign_hook,
};

struct ResCacheReleaseSnapshot {
    ResCacheReleaseStatus status = ResCacheReleaseStatus::not_attempted;
    bool hooks_active = false;
    // Gets served by an addon override instance (observed at the existing
    // native Get detour; zero when that detour is not installed).
    std::uint64_t override_gets = 0;
    // Missed Releases routed to the override instance that owned the data.
    std::uint64_t override_releases = 0;
    // Addon inventory/store icon loads whose missing release was supplied.
    std::uint64_t icon_releases = 0;
};

// Addon RVAs of the resolver and icon-loader contract, discovered from the
// loaded addon image (addon_discovery.h).
struct ResCacheAddonLayout {
    std::uint32_t get_slot_target;       // resolver thunk installed in Get slot
    std::uint32_t override_map;          // name -> private instances map object
    std::uint32_t resolver_lock;         // resolver CRITICAL_SECTION
    std::uint32_t resolver_enter;        // push lock; call [EnterCriticalSection]
    std::uint32_t enter_cs_iat;
    std::uint32_t native_get_pointer;    // holds the game's native Get
    std::uint32_t private_get_call;      // call [native_get_pointer] in the loop
    std::uint32_t private_get_return;    // mov [ebp-0C], eax
    std::uint32_t map_address_load;      // mov ecx, override_map
    std::uint32_t map_head_ref;          // instruction comparing/loading [map]
    std::uint8_t map_head_opcode[2];
    std::uint8_t map_head_opcode_bytes;  // 1 (A1) or 2 (3B 1D)
    std::uint32_t instance_loop;         // mov esi,[ebx+28]; cmp esi,[ebx+2C]; je
    std::uint32_t icon_loader;
    std::uint32_t icon_close_all_call;
    std::uint32_t manager_close_all_thunk;
    std::uint32_t get_mmp_thunk;
    std::uint32_t icon_get_calls[3];
    std::uint32_t icon_data_stores[3];   // mov [ebp-0C4], edi
};

// `game_image` must be the hash-verified game and `layout` the layout
// discovered from `addon_image`. Every patched span and
// every addon layout this relies on is byte-verified first; any mismatch
// leaves all code unchanged and reports contract_mismatch.
bool install_res_cache_release_hooks(std::uint8_t* game_image,
                                     std::uint8_t* addon_image,
                                     const ResCacheAddonLayout* layout) noexcept;
bool uninstall_res_cache_release_hooks() noexcept;
#ifdef EID3D11_TESTING
bool res_cache_test_partial_restore() noexcept;
#endif
ResCacheReleaseSnapshot res_cache_release_snapshot() noexcept;
const char* res_cache_release_status_name(ResCacheReleaseStatus status) noexcept;
// Called by the native Get detour with its caller's return address.
void res_cache_note_get(const void* return_address, const void* data) noexcept;

// Pure routing rule shared by the live hook and its regression. A Release
// that missed `caller` is offered to each other unmapped instance in turn;
// the stock Release returns true only for the instance whose cache holds
// `data`, so at most one instance releases it.
template <class Mapped, class Release>
bool release_through_override_instances(void* caller, const void* data,
                                        void* const* instances, std::size_t count,
                                        Mapped&& mapped, Release&& release) {
    if (!data) return false;
    for (std::size_t i = 0; i < count; ++i) {
        void* instance = instances[i];
        // A mapped instance returns success for any pointer without owning it.
        if (!instance || instance == caller || mapped(instance)) continue;
        if (release(instance, data)) return true;
    }
    return false;
}

// Collects every instance pointer held by an MSVC
// std::map<std::string, std::vector<void*>> (head/size object, node value at
// +10 with the vector begin/end at node +28/+2C). Fails on a malformed tree.
bool collect_override_instances(const void* map_object,
                                std::vector<void*>& instances) noexcept;

} // namespace owned
