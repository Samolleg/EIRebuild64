#include "res_cache_release.h"
#include "addon_hook_chain.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <atomic>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

namespace owned {

namespace {

// Supported game image (verified by the caller), RVAs from image base 400000.
constexpr std::uint32_t kGetRva = 0x00271b40;            // CMappedResource Get
constexpr std::uint32_t kReleaseRva = 0x00271e00;        // CMappedResource Release
constexpr std::uint32_t kGetSlotRva = 0x003432f4;        // vtable +14
constexpr std::uint32_t kReleaseSlotRva = 0x003432f8;    // vtable +18
constexpr std::uint32_t kReleaseSehHandlerRva = 0x00331343;
constexpr std::uint32_t kGetMmpDataRva = 0x00291cf0;     // texture manager GetFile wrapper
constexpr std::size_t kReleasePatchBytes = 6;            // mov eax, fs:[0]
constexpr std::uint32_t kMappedBaseOffset = 0x28;        // non-null: whole-file view
constexpr std::array<std::uint32_t, 13> kGameDirectGetCallRvas{{
    0x063474, 0x068b16, 0x1202cd, 0x13a2ad, 0x1b451c, 0x1b4b14, 0x1b507a,
    0x1c94b2, 0x2845fd, 0x2a684d, 0x2ae48a, 0x2d0717, 0x2d770e}};

using ReleaseFn = unsigned char(__fastcall*)(void* self, void* unused, const void* data);
using CloseFileFn = unsigned char(__fastcall*)(void* manager, void* unused, const void* data);

std::uint8_t* g_game = nullptr;
std::uint8_t* g_addon = nullptr;
const ResCacheAddonLayout* g_layout = nullptr;
std::uint8_t* g_release_trampoline = nullptr;
ReleaseFn g_release = nullptr;
void* g_icon_close_all_target = nullptr;
const void* g_private_get_return = nullptr;
std::array<std::uint8_t, kReleasePatchBytes> g_release_original{};
std::array<std::uint8_t, 5> g_icon_call_original{};
std::atomic<ResCacheReleaseStatus> g_status{ResCacheReleaseStatus::not_attempted};
std::atomic<bool> g_active{false};
std::atomic<std::uint64_t> g_override_gets{0};
std::atomic<std::uint64_t> g_override_releases{0};
std::atomic<std::uint64_t> g_icon_releases{0};
#ifdef EID3D11_TESTING
unsigned g_test_fail_write_mask=0;
#endif

std::uint32_t loaded(const std::uint8_t* image, std::uint32_t rva) noexcept {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image) + rva);
}

bool read_word(const void* address, std::uint32_t& value) noexcept {
    __try { value = *static_cast<const volatile std::uint32_t*>(address); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool read_byte(const void* address, std::uint8_t& value) noexcept {
    __try { value = *static_cast<const volatile std::uint8_t*>(address); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool bytes_at(const std::uint8_t* address, std::initializer_list<std::uint8_t> expected) noexcept {
    return std::memcmp(address, expected.begin(), expected.size()) == 0;
}

bool word_at(const std::uint8_t* address, std::uint32_t expected) noexcept {
    std::uint32_t value = 0;
    std::memcpy(&value, address, sizeof(value));
    return value == expected;
}

bool call_targets(const std::uint8_t* call, const std::uint8_t* target) noexcept {
    if (call[0] != 0xe8) return false;
    std::int32_t relative = 0;
    std::memcpy(&relative, call + 1, sizeof(relative));
    return call + 5 + relative == target;
}

// Encodes a 5-byte relative branch into `out` for execution at `site`.
bool encode_branch(std::uint8_t* out, const std::uint8_t* site, std::uint8_t opcode,
                   const void* target) noexcept {
    const auto delta = static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(target)) -
        static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(site + 5));
    if (delta < (std::numeric_limits<std::int32_t>::min)() ||
        delta > (std::numeric_limits<std::int32_t>::max)()) return false;
    const auto relative = static_cast<std::int32_t>(delta);
    out[0] = opcode;
    std::memcpy(out + 1, &relative, sizeof(relative));
    return true;
}

bool write_code(std::uint8_t* at, const std::uint8_t* bytes, std::size_t size) noexcept {
#ifdef EID3D11_TESTING
    const bool fail_write=(g_test_fail_write_mask & 1u)!=0;
    g_test_fail_write_mask >>= 1;
    if(fail_write) return false;
#endif
    DWORD protection = 0;
    if (!VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &protection)) return false;
    std::memcpy(at, bytes, size);
    DWORD ignored = 0;
    VirtualProtect(at, size, protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), at, size);
    return true;
}

bool instance_mapped(void* instance) noexcept {
    std::uint32_t base = 0;
    // An unreadable instance is never offered a Release.
    return !read_word(static_cast<const std::uint8_t*>(instance) + kMappedBaseOffset, base) ||
        base != 0;
}

bool route_to_override(void* caller, const void* data) noexcept {
    if (!g_addon) return false;
    // The resolver mutates private caches only under its own lock; the routed
    // Release follows the same discipline.
    auto* lock = reinterpret_cast<CRITICAL_SECTION*>(g_addon + g_layout->resolver_lock);
    EnterCriticalSection(lock);
    std::vector<void*> instances;
    const bool routed =
        collect_override_instances(g_addon + g_layout->override_map, instances) &&
        release_through_override_instances(
            caller, data, instances.data(), instances.size(), instance_mapped,
            [](void* instance, const void* bytes) {
                return g_release(instance, nullptr, bytes) != 0;
            });
    LeaveCriticalSection(lock);
    if (routed) g_override_releases.fetch_add(1, std::memory_order_relaxed);
    return routed;
}

unsigned char __fastcall hook_release(void* self, void*, const void* data) {
    const unsigned char released = g_release(self, nullptr, data);
    // A miss (or a mapped caller, which reports success without a lookup) may
    // be data the addon resolver served from one of its private instances.
    if (data && (!released || instance_mapped(self)) && route_to_override(self, data))
        return 1;
    return released;
}

// The addon's icon loader acquires one reference through the texture
// manager's GetFile and then calls CloseAllFiles, which clears only the
// manager's own instances. Release that reference through the manager's
// CloseFile first; this reaches hook_release and therefore the owner.
void __stdcall release_icon_texture(void* manager, const void* data) noexcept {
    if (!manager || !data) return;
    std::uint32_t vtable = 0;
    std::uint32_t close_file = 0;
    if (!read_word(manager, vtable) ||
        !read_word(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(vtable) + 0x50),
                   close_file) || !close_file) return;
    if (reinterpret_cast<CloseFileFn>(static_cast<std::uintptr_t>(close_file))(
            manager, nullptr, data))
        g_icon_releases.fetch_add(1, std::memory_order_relaxed);
}

__declspec(naked) void hook_icon_close_all() {
    __asm {
        // ECX = texture manager; EBP = the icon loader frame, whose local at
        // EBP-0C4 holds the one data pointer it acquired (or null).
        push ecx
        push dword ptr [ebp-0C4h]
        push ecx
        call release_icon_texture
        pop ecx
        jmp dword ptr [g_icon_close_all_target]
    }
}

// Stock Release entry or an addon hook-framework chain on it.
EntryResolution release_entry_owner(const std::uint8_t* game) noexcept {
    std::array<std::uint8_t, 15> prologue{{0x64,0xa1,0,0,0,0,0x6a,0xff,0x68,0,0,0,0,0x50,0x64}};
    const std::uint32_t handler = loaded(game, kReleaseSehHandlerRva);
    std::memcpy(prologue.data() + 9, &handler, sizeof(handler));
    return resolve_entry_owner(game + kReleaseRva, prologue.data(), prologue.size(),
                               kReleasePatchBytes, &addon_module_contains);
}

bool release_contract(std::uint8_t* game) noexcept {
    return release_entry_owner(game).owner != EntryOwner::rejected &&
        // Mapped instance: success without lookup. Unmapped: lookup by data.
        bytes_at(game + kReleaseRva + 0x1d, {0x8b,0x43,0x28,0x57,0x85,0xc0,0x0f,0x85}) &&
        bytes_at(game + kReleaseRva + 0x29, {0x8b,0x7c,0x24,0x38,0x85,0xff,0x75,0x07,0x32,0xc0}) &&
        // Miss: no-op warning call, then return false with no mutation.
        bytes_at(game + kReleaseRva + 0x54, {0x85,0xc0,0x75,0x14,0x68}) &&
        bytes_at(game + kReleaseRva + 0x65, {0x32,0xc0}) &&
        // Hit: decrement entry+14 and free only at zero.
        bytes_at(game + kReleaseRva + 0x6c, {0x8b,0x68,0x08,0x8b,0x45,0x14,0x48,0x89,0x45,0x14}) &&
        // Get's mapped branch reads the same +28 field.
        bytes_at(game + kGetRva + 0xd8, {0x8b,0x4d,0x28,0x85,0xc9,0x74,0x14}) &&
        word_at(game + kReleaseSlotRva, loaded(game, kReleaseRva));
}

bool addon_contract(std::uint8_t* game, std::uint8_t* addon,
                    const ResCacheAddonLayout& l) noexcept {
    // The live Get slot belongs to the addon resolver, whose first loop calls
    // the native Get on each private instance from the override map.
    if (!word_at(game + kGetSlotRva, loaded(addon, l.get_slot_target)) ||
        !word_at(addon + l.native_get_pointer, loaded(game, kGetRva)) ||
        !bytes_at(addon + l.map_address_load, {0xb9}) ||
        !word_at(addon + l.map_address_load + 1, loaded(addon, l.override_map)) ||
        l.map_head_opcode_bytes < 1 || l.map_head_opcode_bytes > 2 ||
        std::memcmp(addon + l.map_head_ref, l.map_head_opcode, l.map_head_opcode_bytes) != 0 ||
        !word_at(addon + l.map_head_ref + l.map_head_opcode_bytes, loaded(addon, l.override_map)) ||
        !bytes_at(addon + l.instance_loop, {0x8b,0x73,0x28,0x3b,0x73,0x2c,0x74,0x2a}) ||
        !bytes_at(addon + l.private_get_call, {0xff,0x15}) ||
        !word_at(addon + l.private_get_call + 2, loaded(addon, l.native_get_pointer)) ||
        !bytes_at(addon + l.private_get_return, {0x89,0x45,0xf4}) ||
        !bytes_at(addon + l.resolver_enter, {0x68}) ||
        !word_at(addon + l.resolver_enter + 1, loaded(addon, l.resolver_lock)) ||
        !bytes_at(addon + l.resolver_enter + 5, {0xff,0x15}) ||
        !word_at(addon + l.resolver_enter + 7, loaded(addon, l.enter_cs_iat)))
        return false;
    // Every direct native Get call site is redirected to the same resolver.
    for (auto rva : kGameDirectGetCallRvas)
        if (!call_targets(game + rva, addon + l.get_slot_target)) return false;
    // Icon loader: ebp frame, three GetFile attempts storing the result in
    // the EBP-0C4 local, no CloseFile, then manager CloseAllFiles (+58).
    if (!bytes_at(addon + l.icon_loader, {0x55,0x8b,0xec,0x6a,0xff}) ||
        !bytes_at(addon + l.get_mmp_thunk, {0x55,0x8b,0xec,0xff,0x75,0x08,0x68}) ||
        !word_at(addon + l.get_mmp_thunk + 7, loaded(game, kGetMmpDataRva)) ||
        !bytes_at(addon + l.manager_close_all_thunk, {0x8b,0x01,0xff,0x70,0x58,0x51,0xe8}) ||
        !bytes_at(addon + l.icon_close_all_call - 2, {0x8b,0xc8}) ||
        !call_targets(addon + l.icon_close_all_call, addon + l.manager_close_all_thunk) ||
        !bytes_at(addon + l.icon_close_all_call + 5, {0xe9}))
        return false;
    for (auto rva : l.icon_get_calls)
        if (!call_targets(addon + rva, addon + l.get_mmp_thunk)) return false;
    for (auto rva : l.icon_data_stores)
        if (!bytes_at(addon + rva, {0x89,0xbd,0x3c,0xff,0xff,0xff})) return false;
    return true;
}

} // namespace

bool collect_override_instances(const void* map_object,
                                std::vector<void*>& instances) noexcept {
    instances.clear();
    std::uint32_t head = 0, size = 0, root = 0;
    if (!map_object || !read_word(map_object, head) || !head ||
        !read_word(static_cast<const std::uint8_t*>(map_object) + 4, size) ||
        !read_word(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(head) + 4), root))
        return false;
    const auto node_at = [](std::uint32_t node, std::uint32_t offset, std::uint32_t& value) {
        return read_word(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(node) + offset), value);
    };
    const auto is_nil = [](std::uint32_t node, bool& nil) {
        std::uint8_t flag = 1;
        if (!read_byte(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(node) + 0x0d), flag))
            return false;
        nil = flag != 0;
        return true;
    };
    try {
        std::vector<std::uint32_t> pending;
        bool nil = true;
        if (!is_nil(root, nil)) return false;
        if (!nil) pending.push_back(root);
        std::uint32_t visited = 0;
        while (!pending.empty()) {
            const std::uint32_t node = pending.back();
            pending.pop_back();
            // The authoritative size bounds the walk; a cycle or extra node fails.
            if (++visited > size) return false;
            std::uint32_t begin = 0, end = 0;
            if (!node_at(node, 0x28, begin) || !node_at(node, 0x2c, end) || end < begin ||
                (end - begin) % 4u) return false;
            for (std::uint32_t at = begin; at != end; at += 4) {
                std::uint32_t instance = 0;
                if (!read_word(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(at)), instance))
                    return false;
                instances.push_back(reinterpret_cast<void*>(static_cast<std::uintptr_t>(instance)));
            }
            for (std::uint32_t link : {0u, 8u}) {
                std::uint32_t child = 0;
                if (!node_at(node, link, child) || !is_nil(child, nil)) return false;
                if (!nil) pending.push_back(child);
            }
        }
        return visited == size;
    } catch (...) {
        instances.clear();
        return false;
    }
}

void res_cache_note_get(const void* return_address, const void* data) noexcept {
    if (data && g_active.load(std::memory_order_relaxed) &&
        return_address == g_private_get_return)
        g_override_gets.fetch_add(1, std::memory_order_relaxed);
}

bool install_res_cache_release_hooks(std::uint8_t* game, std::uint8_t* addon,
                                     const ResCacheAddonLayout* layout) noexcept {
    if (g_active.load()) return g_status.load()==ResCacheReleaseStatus::active;
    if (!game || !addon || !layout) {
        g_status = ResCacheReleaseStatus::addon_missing;
        return false;
    }
    if (!release_contract(game) || !addon_contract(game, addon, *layout)) {
        g_status = ResCacheReleaseStatus::contract_mismatch;
        return false;
    }
    auto* release_entry = game + kReleaseRva;
    auto* icon_call = addon + layout->icon_close_all_call;
    auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, kReleasePatchBytes + 5, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!trampoline) {
        g_status = ResCacheReleaseStatus::protection_failure;
        return false;
    }
    // Stock: replay the prologue and continue. Chain: enter the addon's
    // dispatcher, which stays addon-owned; this small trampoline is ours.
    const auto owner = release_entry_owner(game);
    const std::size_t replayed = owner.owner == EntryOwner::stock ? kReleasePatchBytes : 0;
    std::memcpy(trampoline, release_entry, replayed);
    DWORD protection = 0;
    std::array<std::uint8_t, kReleasePatchBytes> release_patch{};
    release_patch.fill(0x90);
    std::array<std::uint8_t, 5> icon_patch{};
    if (owner.owner == EntryOwner::rejected ||
        !encode_branch(trampoline + replayed, trampoline + replayed, 0xe9,
                       replayed ? static_cast<const void*>(release_entry + kReleasePatchBytes)
                                : owner.dispatcher) ||
        !VirtualProtect(trampoline, kReleasePatchBytes + 5, PAGE_EXECUTE_READ, &protection) ||
        !encode_branch(release_patch.data(), release_entry, 0xe9,
                       reinterpret_cast<const void*>(&hook_release)) ||
        !encode_branch(icon_patch.data(), icon_call, 0xe8,
                       reinterpret_cast<const void*>(&hook_icon_close_all))) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        g_status = ResCacheReleaseStatus::protection_failure;
        return false;
    }
    std::memcpy(g_release_original.data(), release_entry, kReleasePatchBytes);
    std::memcpy(g_icon_call_original.data(), icon_call, g_icon_call_original.size());
    g_game = game;
    g_addon = addon;
    g_layout = layout;
    g_release_trampoline = trampoline;
    g_release = reinterpret_cast<ReleaseFn>(trampoline);
    g_icon_close_all_target = addon + layout->manager_close_all_thunk;
    g_private_get_return = addon + layout->private_get_return;
    if (!write_code(release_entry, release_patch.data(), release_patch.size())) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        g_release_trampoline = nullptr;
        g_release = nullptr;
        g_status = ResCacheReleaseStatus::protection_failure;
        return false;
    }
    // Any live entry owns the trampoline, including a failed installation.
    g_active = true;
    if (!write_code(icon_call, icon_patch.data(), icon_patch.size())) {
        // The unpatched icon site is already restored. A failed rollback
        // keeps the live release entry and its trampoline for a later retry.
        uninstall_res_cache_release_hooks();
        g_status = ResCacheReleaseStatus::protection_failure;
        return false;
    }
    g_active = true;
    g_status = ResCacheReleaseStatus::active;
    return true;
}

bool uninstall_res_cache_release_hooks() noexcept {
    if (!g_active.load()) return true;
    auto* release_entry = g_game + kReleaseRva;
    auto* icon_call = g_addon + g_layout->icon_close_all_call;
    std::array<std::uint8_t,kReleasePatchBytes> release_patch{};release_patch.fill(0x90);
    std::array<std::uint8_t,5> icon_patch{};
    if(!encode_branch(release_patch.data(),release_entry,0xe9,reinterpret_cast<const void*>(&hook_release)) ||
       !encode_branch(icon_patch.data(),icon_call,0xe8,reinterpret_cast<const void*>(&hook_icon_close_all))) return false;
    const bool release_restored=std::memcmp(release_entry,g_release_original.data(),g_release_original.size())==0;
    const bool icon_restored=std::memcmp(icon_call,g_icon_call_original.data(),g_icon_call_original.size())==0;
    // Accept each already-restored site, but never overwrite a foreign hook.
    if ((!release_restored && std::memcmp(release_entry,release_patch.data(),release_patch.size())!=0) ||
        (!icon_restored && std::memcmp(icon_call,icon_patch.data(),icon_patch.size())!=0)) {
        g_status = ResCacheReleaseStatus::teardown_foreign_hook;
        return false;
    }
    if ((!icon_restored && !write_code(icon_call, g_icon_call_original.data(), g_icon_call_original.size())) ||
        (!release_restored && !write_code(release_entry, g_release_original.data(), g_release_original.size()))) {
        g_status = ResCacheReleaseStatus::protection_failure;
        return false;
    }
    if(std::memcmp(release_entry,g_release_original.data(),g_release_original.size())!=0 ||
       std::memcmp(icon_call,g_icon_call_original.data(),g_icon_call_original.size())!=0) return false;
    g_active = false;
    VirtualFree(g_release_trampoline, 0, MEM_RELEASE);
    g_release_trampoline = nullptr;
    g_release = nullptr;
    g_game=nullptr;g_addon=nullptr;g_icon_close_all_target=nullptr;g_private_get_return=nullptr;
    g_status = ResCacheReleaseStatus::inactive;
    return true;
}

#ifdef EID3D11_TESTING
bool res_cache_test_partial_restore() noexcept {
    try {
        if(g_active.load())return false;
        // Fixture layout: only the icon call site is used by teardown.
        ResCacheAddonLayout fixture{};
        fixture.icon_close_all_call = 0x100;
        std::vector<std::uint8_t> game(kReleaseRva+kReleasePatchBytes),addon(fixture.icon_close_all_call+5);
        bool ok=true;
        for (const auto* layout : {&fixture}) {
        g_game=game.data();g_addon=addon.data();g_layout=layout;
        auto* release=g_game+kReleaseRva;auto* icon=g_addon+layout->icon_close_all_call;
        g_release_original.fill(0xcc);g_icon_call_original.fill(0xcc);
        std::array<std::uint8_t,kReleasePatchBytes> release_patch{};release_patch.fill(0x90);
        std::array<std::uint8_t,5> icon_patch{};
        encode_branch(release_patch.data(),release,0xe9,reinterpret_cast<const void*>(&hook_release));
        encode_branch(icon_patch.data(),icon,0xe8,reinterpret_cast<const void*>(&hook_icon_close_all));
        for(bool partial:{true,false}) {
            g_game=game.data();g_addon=addon.data();
            g_release_trampoline=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,16,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
            if(!g_release_trampoline)return false;
            g_release=reinterpret_cast<ReleaseFn>(g_release_trampoline);g_active=true;
            std::memcpy(release,release_patch.data(),release_patch.size());
            std::memcpy(icon,partial?g_icon_call_original.data():icon_patch.data(),5);
            g_test_fail_write_mask=partial?1u:2u;
            ok=(!uninstall_res_cache_release_hooks() && g_active.load() && g_release && g_release_trampoline) && ok;
            if(!partial)ok=(std::memcmp(icon,g_icon_call_original.data(),5)==0) && ok;
            g_test_fail_write_mask=0;
            ok=(uninstall_res_cache_release_hooks() && !g_active.load() && !g_release && !g_release_trampoline) && ok;
        }
        }
        return ok;
    } catch (...) { return false; }
}
#endif

ResCacheReleaseSnapshot res_cache_release_snapshot() noexcept {
    ResCacheReleaseSnapshot snapshot;
    snapshot.status = g_status.load();
    snapshot.hooks_active = g_active.load();
    snapshot.override_gets = g_override_gets.load(std::memory_order_relaxed);
    snapshot.override_releases = g_override_releases.load(std::memory_order_relaxed);
    snapshot.icon_releases = g_icon_releases.load(std::memory_order_relaxed);
    return snapshot;
}

const char* res_cache_release_status_name(ResCacheReleaseStatus status) noexcept {
    switch (status) {
    case ResCacheReleaseStatus::not_attempted: return "not_attempted";
    case ResCacheReleaseStatus::active: return "active";
    case ResCacheReleaseStatus::addon_missing: return "addon_missing";
    case ResCacheReleaseStatus::contract_mismatch: return "contract_mismatch";
    case ResCacheReleaseStatus::protection_failure: return "protection_failure";
    case ResCacheReleaseStatus::inactive: return "inactive";
    case ResCacheReleaseStatus::teardown_foreign_hook: return "teardown_foreign_hook";
    }
    return "unknown";
}

} // namespace owned
