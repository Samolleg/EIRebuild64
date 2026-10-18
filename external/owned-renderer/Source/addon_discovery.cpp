#include "addon_discovery.h"

#include <windows.h>

#include <cstring>

namespace owned {
namespace {

// Fixed game addresses (the game executable is hash-checked and loads at its
// preferred base) that the addon's code references as immediates.
constexpr std::uint32_t kGameTextureLockVa = 0x00690fc0;
constexpr std::uint32_t kGameGetMmpDataVa = 0x00691cf0;
constexpr std::uint32_t kGameAddFigureVa = 0x005ff3c0;
constexpr std::uint32_t kGameSelectPrimaryVa = 0x005190c0;
constexpr std::uint32_t kGameSelectSecondaryVa = 0x00519380;
constexpr std::uint32_t kGameLoadFiguresVa = 0x00517110;
constexpr std::uint32_t kGameWeaponSetUnitVa = 0x0063c180;
constexpr std::uint32_t kGameSpellSetUnitVa = 0x0063e820;
constexpr std::uint32_t kGameSkillSetUnitVa = 0x0063f800;
constexpr std::uint32_t kGameChangeWeaponVa = 0x00535590;
constexpr std::uint32_t kGameLoadTexturesVa = 0x0051c250;
constexpr std::uint32_t kGameHideWeaponVa = 0x00537cb0;

struct Image {
    const std::uint8_t* base = nullptr;
    std::uint32_t size = 0;
    std::uint32_t text_begin = 0;
    std::uint32_t text_end = 0;
};

bool image_view(const std::uint8_t* base, Image& image) noexcept {
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return false;
    image.base = base;
    image.size = nt->OptionalHeader.SizeOfImage;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        image.text_begin = section->VirtualAddress;
        image.text_end = section->VirtualAddress + section->Misc.VirtualSize;
        return image.text_end > image.text_begin && image.text_end <= image.size;
    }
    return false;
}

// "55 8b ?? ..." with ?? wildcards.
struct Sig {
    std::uint8_t bytes[128]{};
    bool known[128]{};
    std::uint32_t size = 0;
};

int hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

Sig sig(const char* text) noexcept {
    Sig result{};
    for (const char* at = text; *at && result.size < sizeof(result.bytes);) {
        if (*at == ' ') { ++at; continue; }
        if (at[0] == '?' && at[1] == '?') {
            result.known[result.size++] = false;
        } else {
            result.bytes[result.size] =
                static_cast<std::uint8_t>(hex_digit(at[0]) * 16 + hex_digit(at[1]));
            result.known[result.size++] = true;
        }
        at += 2;
    }
    return result;
}

// Appends a literal little-endian word to a signature.
Sig& add_word(Sig& value, std::uint32_t word) noexcept {
    for (int index = 0; index < 4 && value.size < sizeof(value.bytes); ++index) {
        value.bytes[value.size] = static_cast<std::uint8_t>(word >> (8 * index));
        value.known[value.size++] = true;
    }
    return value;
}

Sig& add(Sig& value, const char* text) noexcept {
    const Sig tail = sig(text);
    for (std::uint32_t index = 0; index < tail.size && value.size < sizeof(value.bytes); ++index) {
        value.bytes[value.size] = tail.bytes[index];
        value.known[value.size++] = tail.known[index];
    }
    return value;
}

bool matches(const Image& image, std::uint32_t at, const Sig& pattern) noexcept {
    if (at + pattern.size > image.size) return false;
    for (std::uint32_t index = 0; index < pattern.size; ++index)
        if (pattern.known[index] && image.base[at + index] != pattern.bytes[index]) return false;
    return true;
}

// Finds up to `capacity` matches in [begin, end) and returns the total count.
std::uint32_t find_all(const Image& image, std::uint32_t begin, std::uint32_t end,
                       const Sig& pattern, std::uint32_t* found, std::uint32_t capacity) noexcept {
    if (begin < image.text_begin) begin = image.text_begin;
    if (end > image.text_end) end = image.text_end;
    std::uint32_t count = 0;
    if (!pattern.size || !pattern.known[0] || begin >= end || end - begin < pattern.size) return 0;
    const std::uint8_t first = pattern.bytes[0];
    for (std::uint32_t at = begin; at + pattern.size <= end;) {
        const void* next = std::memchr(image.base + at, first, end - pattern.size + 1 - at);
        if (!next) break;
        at = static_cast<std::uint32_t>(static_cast<const std::uint8_t*>(next) - image.base);
        if (matches(image, at, pattern)) {
            if (count < capacity && found) found[count] = at;
            ++count;
        }
        ++at;
    }
    return count;
}

bool find_unique(const Image& image, std::uint32_t begin, std::uint32_t end,
                 const Sig& pattern, std::uint32_t& at) noexcept {
    std::uint32_t found = 0;
    if (find_all(image, begin, end, pattern, &found, 1) != 1) return false;
    at = found;
    return true;
}

bool find_unique(const Image& image, const Sig& pattern, std::uint32_t& at) noexcept {
    return find_unique(image, image.text_begin, image.text_end, pattern, at);
}

std::uint32_t word(const Image& image, std::uint32_t at) noexcept {
    std::uint32_t value = 0;
    if (at + 4 <= image.size) std::memcpy(&value, image.base + at, 4);
    return value;
}

// RVA referenced by an absolute operand at `at`, or 0 outside the image.
std::uint32_t absolute_rva(const Image& image, std::uint32_t at) noexcept {
    const std::uint32_t value = word(image, at) -
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image.base));
    return value < image.size ? value : 0;
}

// Target RVA of the E8/E9 rel32 at `at`.
std::uint32_t branch_target(const Image& image, std::uint32_t at) noexcept {
    return at + 5 + word(image, at + 1);
}

bool in_text(const Image& image, std::uint32_t rva) noexcept {
    return rva >= image.text_begin && rva < image.text_end;
}

// Direct calls to `target` in the executable section.
std::uint32_t call_sites(const Image& image, std::uint32_t target, std::uint32_t* found,
                         std::uint32_t capacity) noexcept {
    std::uint32_t count = 0;
    for (std::uint32_t at = image.text_begin; at + 5 <= image.text_end;) {
        const void* next = std::memchr(image.base + at, 0xe8, image.text_end - 4 - at);
        if (!next) break;
        at = static_cast<std::uint32_t>(static_cast<const std::uint8_t*>(next) - image.base);
        if (branch_target(image, at) == target) {
            if (count < capacity && found) found[count] = at;
            ++count;
        }
        ++at;
    }
    return count;
}

// The addon's function-hook registration loads the hooked game entry into ECX
// right after pushing the thiscall convention and its callback:
//   push callback; push 3; mov ecx, game_entry; call <hook builder>
// Every registration of one entry must name the same callback.
std::uint32_t registered_thiscall_callback(const Image& image, std::uint32_t game_va) noexcept {
    Sig pattern = sig("68 ?? ?? ?? ?? 6a 03 b9");
    add_word(pattern, game_va);
    add(pattern, "e8");
    std::uint32_t found[8]{};
    const std::uint32_t count = find_all(image, image.text_begin, image.text_end,
                                         pattern, found, 8);
    if (!count || count > 8) return 0;
    const std::uint32_t callback = absolute_rva(image, found[0] + 1);
    for (std::uint32_t index = 1; index < count; ++index)
        if (absolute_rva(image, found[index] + 1) != callback) return 0;
    return in_text(image, callback) ? callback : 0;
}

// Start of the function containing `at`: int3 padding then the standard
// SEH frame prologue, searched backwards within `window` bytes.
std::uint32_t frame_function_start(const Image& image, std::uint32_t at,
                                   std::uint32_t window) noexcept {
    static const std::uint8_t kPrologue[] = {0xcc, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68};
    for (std::uint32_t back = 1; back <= window && back <= at; ++back) {
        const std::uint32_t candidate = at - back;
        if (std::memcmp(image.base + candidate, kPrologue, sizeof(kPrologue)) == 0)
            return candidate + 1;
    }
    return 0;
}

bool exact(const Image& image, std::uint32_t at, const char* text) noexcept {
    return matches(image, at, sig(text));
}

// First `mov esp,ebp; pop ebp; ret` epilogue in [begin, begin+window).
bool first_epilogue_is(const Image& image, std::uint32_t begin, std::uint32_t window,
                       const char* expected) noexcept {
    std::uint32_t found = 0;
    if (!find_all(image, begin, begin + window, sig("8b e5 5d c2 ?? 00"), &found, 1))
        return false;
    return exact(image, found, expected);
}

#define DISCOVER_REQUIRE(condition, reason) do { if (!(condition)) return reason; } while (false)

const char* discover_hud_atlas(const Image& image, AddonHudAtlasLayout& out) noexcept {
    AddonHudAtlasLayout layout{};
    Sig thunk = sig("55 8b ec ff 75 08 68");
    add_word(thunk, kGameTextureLockVa);
    add(thunk, "51 e8 ?? ?? ?? ?? 5d c2 04 00");
    DISCOVER_REQUIRE(find_unique(image, thunk, layout.texture_lock_thunk),
                     "hud_atlas.texture_lock_thunk");
    DISCOVER_REQUIRE(find_unique(image, sig(
        "8b 95 44 ff ff ff 8b fb 8b 4d 10 8b 45 8c 8b 5d 20 0f af d1 03 95 50 ff ff ff 8d 34 50"),
        layout.addressing), "hud_atlas.addressing");
    layout.producer = frame_function_start(image, layout.addressing, 0x400);
    DISCOVER_REQUIRE(layout.producer, "hud_atlas.producer");
    std::uint32_t calls[4]{};
    std::uint32_t lock_calls = 0;
    const std::uint32_t total = call_sites(image, layout.texture_lock_thunk, calls, 4);
    for (std::uint32_t index = 0; index < total && index < 4; ++index)
        if (calls[index] > layout.producer && calls[index] < layout.addressing) {
            layout.lock_call = calls[index];
            ++lock_calls;
        }
    DISCOVER_REQUIRE(total <= 4 && lock_calls == 1, "hud_atlas.lock_call");
    DISCOVER_REQUIRE(find_unique(image, layout.addressing, layout.addressing + 0x200, sig(
        "8b 17 8b ca c1 e9 06 8b c2 c1 e8 03 81 e1 e0 03 00 00 83 e0 1f 83 c7 04 0b c8 "
        "8b c2 c1 e8 10 25 00 80 00 00 c1 ea 09 0b c8 81 e2 00 7c 00 00 0b ca 66 89 0e "
        "83 c6 02"), layout.write), "hud_atlas.write");
    DISCOVER_REQUIRE(find_unique(image, layout.write, layout.write + 0x200, sig(
        "8b 85 64 ff ff ff 85 c0 74 09 50 e8 ?? ?? ?? ?? 83 c4 04 8b 8d 54 ff ff ff "
        "e8 ?? ?? ?? ?? e8 ?? ?? ?? ?? 8b c8 e8 ?? ?? ?? ??"), layout.cleanup),
        "hud_atlas.cleanup");
    std::uint32_t close_all = 0;
    DISCOVER_REQUIRE(find_unique(image, sig("8b 01 ff 70 58 51 e8 ?? ?? ?? ?? c3"), close_all) &&
                     branch_target(image, layout.cleanup + 37) == close_all,
                     "hud_atlas.cleanup_close_all");
    DISCOVER_REQUIRE(find_unique(image, layout.cleanup, layout.cleanup + 0x100,
                                 sig("83 bd 60 ff ff ff 00 0f 95 c3"), layout.result),
                     "hud_atlas.result");
    std::uint32_t variant_read = 0;
    DISCOVER_REQUIRE(find_unique(image, layout.producer, layout.producer + 0x40,
                                 sig("83 3d ?? ?? ?? ?? 00"), variant_read) &&
                     (layout.variant = absolute_rva(image, variant_read + 2)) != 0,
                     "hud_atlas.variant");
    Sig writer = sig("c7 05");
    add_word(writer, word(image, variant_read + 2));
    add(writer, "00 00 00 00");
    DISCOVER_REQUIRE(find_unique(image, writer, layout.variant_writer),
                     "hud_atlas.variant_writer");
    out = layout;
    return nullptr;
}

const char* discover_pane(const Image& image, AddonPaneLayout& out) noexcept {
    AddonPaneLayout layout{};
    DISCOVER_REQUIRE(find_unique(image, sig(
        "81 ec 00 01 00 00 53 8b d9 56 33 f6 a1 ?? ?? ?? ?? f7 d8 a3 ?? ?? ?? ?? "
        "68 ac 62 64 00 c3"), layout.draw_start), "pane.draw_start");
    layout.virtual_shift = absolute_rva(image, layout.draw_start + 13);
    layout.current_shift = absolute_rva(image, layout.draw_start + 20);
    DISCOVER_REQUIRE(find_unique(image, sig(
        "8b 84 24 10 01 00 00 8b cb 50 b8 00 00 00 00 a3 ?? ?? ?? ?? 68 27 6b 64 00 c3"),
        layout.draw_end) && absolute_rva(image, layout.draw_end + 16) == layout.current_shift,
        "pane.draw_end");
    DISCOVER_REQUIRE(find_unique(image, sig(
        "53 56 8b 74 24 0c a1 ?? ?? ?? ?? f7 2e bb 20 03 00 00 f7 fb 85 c9 74 06 03 05 "
        "?? ?? ?? ?? 89 06 a1 ?? ?? ?? ?? f7 6e 04 bb 58 02 00 00 f7 fb 89 46 04 a1 "
        "?? ?? ?? ?? f7 6e 08 bb 20 03 00 00 f7 fb 85 c9 74 06 03 05 ?? ?? ?? ?? 89 46 "
        "08 a1 ?? ?? ?? ?? f7 6e 0c bb 58 02 00 00 f7 fb 89 46 0c 5e 5b c2 04 00"),
        layout.virtual_to_screen), "pane.virtual_to_screen");
    const std::uint32_t transform = layout.virtual_to_screen;
    layout.screen_effective = absolute_rva(image, transform + 7);
    layout.screen_shift = absolute_rva(image, transform + 26);
    layout.screen_height = absolute_rva(image, transform + 33);
    DISCOVER_REQUIRE(layout.virtual_shift && layout.current_shift && layout.screen_effective &&
                     layout.screen_shift && layout.screen_height &&
                     absolute_rva(image, transform + 51) == layout.screen_effective &&
                     absolute_rva(image, transform + 71) == layout.screen_shift &&
                     absolute_rva(image, transform + 79) == layout.screen_height,
                     "pane.screen_globals");
    out = layout;
    return nullptr;
}

// Resolver shared by the resource-cache and visual-database contracts.
struct Resolver {
    std::uint32_t thunk = 0, body = 0, lookup_call = 0, lookup = 0;
    std::uint32_t instance_loop = 0, member_slot = 0;
    std::uint32_t member_calls[2]{};
};

const char* discover_resolver(const Image& image, Resolver& out) noexcept {
    Resolver resolver{};
    DISCOVER_REQUIRE(find_unique(image, sig("ff 74 24 04 ff 74 24 0c 51 e8 ?? ?? ?? ?? c2 08 00"),
                                 resolver.thunk), "resolver.thunk");
    resolver.body = branch_target(image, resolver.thunk + 9);
    DISCOVER_REQUIRE(in_text(image, resolver.body) && exact(image, resolver.body,
        "55 8b ec 83 ec ?? a1 ?? ?? ?? ?? 33 c5 89 45 fc 53 56 8b 75 10 57 68 ?? ?? ?? ?? "
        "ff 15 ?? ?? ?? ??"), "resolver.body");
    std::uint32_t call = 0;
    DISCOVER_REQUIRE(find_unique(image, resolver.body, resolver.body + 0x100,
                                 sig("50 e8 ?? ?? ?? ?? 8b f8 85 ff 75 2f"), call),
                     "resolver.lookup_call");
    resolver.lookup_call = call + 1;
    resolver.lookup = branch_target(image, resolver.lookup_call);
    DISCOVER_REQUIRE(in_text(image, resolver.lookup) &&
                     find_unique(image, resolver.lookup, resolver.lookup + 0x180,
                                 sig("8b 73 28 3b 73 2c 74 2a"), resolver.instance_loop),
                     "resolver.instance_loop");
    std::uint32_t members[2]{};
    DISCOVER_REQUIRE(find_all(image, resolver.lookup, resolver.lookup + 0x180,
                              sig("ff 75 0c ff 75 e8 ff 15 ?? ?? ?? ?? 89 45 f4"),
                              members, 2) == 2 && members[0] > resolver.instance_loop,
                     "resolver.member_calls");
    resolver.member_calls[0] = members[0] + 6;
    resolver.member_calls[1] = members[1] + 6;
    resolver.member_slot = absolute_rva(image, resolver.member_calls[0] + 2);
    DISCOVER_REQUIRE(resolver.member_slot &&
                     absolute_rva(image, resolver.member_calls[1] + 2) == resolver.member_slot,
                     "resolver.member_slot");
    out = resolver;
    return nullptr;
}

// The resolver form without the inline lookup prologue: the Get-slot thunk
// calls a forwarding stub that jumps to a locked Find, which calls one lookup
// (directly, and again for a stripped "s\"/"t\" prefix). The lookup walks the
// handle's override instances and then the stock handle; its result word is
// written only by two calls through one data word that must hold the native
// Get, so every resolution is a native Get call and the first hit wins.
const char* discover_forwarding_resolver(const Image& image, AddonResolverLayout& out) noexcept {
    std::uint32_t thunk = 0;
    DISCOVER_REQUIRE(find_unique(image, sig("ff 74 24 04 ff 74 24 0c 51 e8 ?? ?? ?? ?? c2 08 00"),
                                 thunk), "resolver.thunk");
    const std::uint32_t stub = branch_target(image, thunk + 9);
    DISCOVER_REQUIRE(in_text(image, stub) && exact(image, stub, "55 8b ec 5d e9"),
                     "resolver.forward_stub");
    const std::uint32_t find = branch_target(image, stub + 4);
    DISCOVER_REQUIRE(in_text(image, find) && exact(image, find,
        "55 8b ec 51 51 53 56 57 bb ?? ?? ?? ?? 53 ff 15 ?? ?? ?? ??"), "resolver.forward_find");
    std::uint32_t lookup_calls[3]{};
    DISCOVER_REQUIRE(find_all(image, find, find + 0x80, sig("8d 45 08 50 e8"), lookup_calls, 3) == 2,
                     "resolver.forward_lookup_calls");
    const std::uint32_t lookup = branch_target(image, lookup_calls[0] + 4);
    DISCOVER_REQUIRE(in_text(image, lookup) && branch_target(image, lookup_calls[1] + 4) == lookup,
                     "resolver.forward_lookup");
    std::uint32_t loop = 0;
    DISCOVER_REQUIRE(find_unique(image, lookup, lookup + 0x100, sig("8b 7e 28 3b 7e 2c 74"), loop),
                     "resolver.forward_instance_loop");
    std::uint32_t members[3]{};
    DISCOVER_REQUIRE(find_all(image, lookup, lookup + 0x100,
                              sig("ff 75 0c ff 75 10 ff 15 ?? ?? ?? ?? 89 45 fc"), members, 3) == 2 &&
                     members[0] > loop, "resolver.forward_member_calls");
    const std::uint32_t slot = absolute_rva(image, members[0] + 8);
    DISCOVER_REQUIRE(slot && absolute_rva(image, members[1] + 8) == slot,
                     "resolver.forward_member_slot");
    out.thunk = thunk;
    out.member_slot = slot;
    return nullptr;
}

const char* discover_res_cache(const Image& image, const Resolver& resolver,
                               ResCacheAddonLayout& out) noexcept {
    ResCacheAddonLayout layout{};
    layout.get_slot_target = resolver.thunk;
    layout.resolver_enter = resolver.body + 0x16;
    layout.resolver_lock = absolute_rva(image, resolver.body + 0x17);
    layout.enter_cs_iat = absolute_rva(image, resolver.body + 0x1d);
    layout.native_get_pointer = resolver.member_slot;
    layout.private_get_call = resolver.member_calls[0];
    layout.private_get_return = resolver.member_calls[0] + 6;
    layout.instance_loop = resolver.instance_loop;
    DISCOVER_REQUIRE(layout.resolver_lock && layout.enter_cs_iat, "res_cache.resolver_lock");
    DISCOVER_REQUIRE(find_unique(image, resolver.lookup, resolver.lookup + 0x180,
                                 sig("b9 ?? ?? ?? ?? 56 8d 45 ?? 50 e8"), layout.map_address_load),
                     "res_cache.map_address_load");
    layout.override_map = absolute_rva(image, layout.map_address_load + 1);
    // The only other reference to the map before the instance loop reads its
    // head: MOV EAX,[map] or CMP EBX,[map].
    const std::uint32_t map_word = word(image, layout.map_address_load + 1);
    std::uint32_t head_refs = 0;
    for (std::uint32_t at = layout.map_address_load + 5; at + 4 <= layout.instance_loop; ++at) {
        if (word(image, at) != map_word) continue;
        ++head_refs;
        if (image.base[at - 1] == 0xa1) {
            layout.map_head_ref = at - 1;
            layout.map_head_opcode[0] = 0xa1;
            layout.map_head_opcode_bytes = 1;
        } else if (image.base[at - 2] == 0x3b && image.base[at - 1] == 0x1d) {
            layout.map_head_ref = at - 2;
            layout.map_head_opcode[0] = 0x3b;
            layout.map_head_opcode[1] = 0x1d;
            layout.map_head_opcode_bytes = 2;
        } else {
            return "res_cache.map_head_form";
        }
    }
    DISCOVER_REQUIRE(layout.override_map && head_refs == 1, "res_cache.map_head_ref");
    Sig get_mmp = sig("55 8b ec ff 75 08 68");
    add_word(get_mmp, kGameGetMmpDataVa);
    add(get_mmp, "51 e8 ?? ?? ?? ?? 5d c2 04 00");
    DISCOVER_REQUIRE(find_unique(image, get_mmp, layout.get_mmp_thunk), "res_cache.get_mmp_thunk");
    // Icon loader: exactly three GetFile attempts that store into EBP-0C4.
    std::uint32_t calls[16]{};
    const std::uint32_t total = call_sites(image, layout.get_mmp_thunk, calls, 16);
    DISCOVER_REQUIRE(total <= 16, "res_cache.icon_get_calls");
    const Sig store = sig("89 bd 3c ff ff ff");
    std::uint32_t icon_calls = 0;
    for (std::uint32_t index = 0; index < total; ++index) {
        std::uint32_t at = 0;
        if (!find_all(image, calls[index] + 5, calls[index] + 0x20, store, &at, 1)) continue;
        if (icon_calls == 3) return "res_cache.icon_get_calls";
        layout.icon_get_calls[icon_calls] = calls[index];
        layout.icon_data_stores[icon_calls++] = at;
    }
    DISCOVER_REQUIRE(icon_calls == 3, "res_cache.icon_get_calls");
    layout.icon_loader = frame_function_start(image, layout.icon_get_calls[0], 0x200);
    DISCOVER_REQUIRE(layout.icon_loader, "res_cache.icon_loader");
    DISCOVER_REQUIRE(find_unique(image, sig("8b 01 ff 70 58 51 e8 ?? ?? ?? ?? c3"),
                                 layout.manager_close_all_thunk), "res_cache.close_all_thunk");
    const std::uint32_t close_calls = call_sites(image, layout.manager_close_all_thunk, calls, 16);
    std::uint32_t icon_close = 0;
    for (std::uint32_t index = 0; index < close_calls && index < 16; ++index)
        if (exact(image, calls[index] - 2, "8b c8") && image.base[calls[index] + 5] == 0xe9) {
            layout.icon_close_all_call = calls[index];
            ++icon_close;
        }
    DISCOVER_REQUIRE(close_calls <= 16 && icon_close == 1, "res_cache.icon_close_all_call");
    out = layout;
    return nullptr;
}

const char* discover_hud_visual(const Image& image, const AddonHudAtlasLayout& atlas,
                                std::uint32_t native_forwarder,
                                AddonHudVisualLayout& out) noexcept {
    AddonHudVisualLayout layout{};
    layout.producer = atlas.producer;
    layout.variant = atlas.variant;
    layout.variant_writer = atlas.variant_writer;
    layout.native_forwarder = native_forwarder;
    layout.weapon_set_unit = registered_thiscall_callback(image, kGameWeaponSetUnitVa);
    DISCOVER_REQUIRE(layout.weapon_set_unit && exact(image, layout.weapon_set_unit,
        "55 8b ec ff 75 0c 8b 4d 08 e8 ?? ?? ?? ?? 5d c2 08 00"), "hud_visual.weapon_set_unit");
    layout.skill_set_unit = registered_thiscall_callback(image, kGameSkillSetUnitVa);
    DISCOVER_REQUIRE(layout.skill_set_unit && exact(image, layout.skill_set_unit,
        "55 8b ec 51 ff 75 10 8b 4d 08 ff 75 0c e8 ?? ?? ?? ?? 59 5d c2 0c 00"),
        "hud_visual.skill_set_unit");
    layout.spell_callback = registered_thiscall_callback(image, kGameSpellSetUnitVa);
    DISCOVER_REQUIRE(layout.spell_callback && exact(image, layout.spell_callback,
        "55 8b ec ff 75 0c 8b 4d 08 e8 ?? ?? ?? ?? 5d c2 08 00"), "hud_visual.spell_callback");
    layout.spell_set_unit = branch_target(image, layout.spell_callback + 9);
    DISCOVER_REQUIRE(in_text(image, layout.spell_set_unit) &&
                     exact(image, layout.spell_set_unit,
                           "55 8b ec 6a ff 68 ?? ?? ?? ?? 64 a1 00 00 00 00 50 81") &&
                     first_epilogue_is(image, layout.spell_set_unit, 0x2000, "8b e5 5d c2 04 00"),
                     "hud_visual.spell_set_unit");
    Sig add_figure = sig("55 8b ec ff 75 08 68");
    add_word(add_figure, kGameAddFigureVa);
    add(add_figure, "51 e8 ?? ?? ?? ?? 5d c2 04 00");
    DISCOVER_REQUIRE(find_unique(image, add_figure, layout.add_figure) &&
                     branch_target(image, layout.add_figure + 12) == native_forwarder,
                     "hud_visual.add_figure");
    // Lane publication: a producer call whose atlas is then attached with
    // AddFigure(name) on the same receiver.
    std::uint32_t producer_calls[16]{};
    const std::uint32_t total = call_sites(image, layout.producer, producer_calls, 16);
    DISCOVER_REQUIRE(total <= 16, "hud_visual.producer_calls");
    const Sig attach = sig("ff 75 ?? 8b ?? e8");
    for (std::uint32_t index = 0; index < total; ++index) {
        std::uint32_t candidates[4]{};
        const std::uint32_t count = find_all(image, producer_calls[index] + 5,
                                             producer_calls[index] + 0x20, attach, candidates, 4);
        for (std::uint32_t candidate = 0; candidate < count && candidate < 4; ++candidate) {
            if (branch_target(image, candidates[candidate] + 5) != layout.add_figure) continue;
            if (layout.lane_count == layout.producer_calls.size()) return "hud_visual.lanes";
            layout.producer_calls[layout.lane_count] = producer_calls[index];
            layout.add_figure_calls[layout.lane_count++] = candidates[candidate] + 5;
            break;
        }
    }
    DISCOVER_REQUIRE(layout.lane_count >= 3, "hud_visual.lanes");
    std::uint32_t boxes[2]{};
    const std::uint32_t box_count = find_all(image, image.text_begin, image.text_end, sig(
        "55 8b ec 83 e4 f8 83 ec 10 80 3d ?? ?? ?? ?? 00 56 57 8b f9 74"), boxes, 2);
    DISCOVER_REQUIRE(box_count <= 1, "hud_visual.autocast_box");
    if (box_count == 1) {
        layout.autocast_box = boxes[0];
        std::uint32_t box_calls[2]{};
        DISCOVER_REQUIRE(call_sites(image, layout.autocast_box, box_calls, 2) == 1,
                         "hud_visual.autocast_call");
        layout.autocast_call = box_calls[0];
        for (std::uint32_t lane = 0; lane < layout.lane_count; ++lane)
            if (layout.producer_calls[lane] > layout.autocast_box &&
                layout.producer_calls[lane] < layout.autocast_box + 0x200) {
                DISCOVER_REQUIRE(layout.autocast_lane == kNoAddonLane, "hud_visual.autocast_lane");
                layout.autocast_lane = lane;
            }
        DISCOVER_REQUIRE(layout.autocast_lane != kNoAddonLane, "hud_visual.autocast_lane");
    }
    out = layout;
    return nullptr;
}

const char* discover_visual(const Image& image, const Resolver& resolver,
                            std::uint32_t native_forwarder, AddonVisualLayout& out) noexcept {
    AddonVisualLayout layout{};
    layout.native_forwarder = native_forwarder;
    layout.resolver_thunk = resolver.thunk;
    layout.resolver_body = resolver.body;
    layout.lookup = resolver.lookup;
    layout.member_slot = resolver.member_slot;
    layout.member_calls = {{resolver.member_calls[0], resolver.member_calls[1]}};

    // Generated-MMP route byte: read by the loader, and every reference is
    // either its single config writer or a boolean test.
    std::uint32_t loader = 0;
    DISCOVER_REQUIRE(find_unique(image, sig("80 3d ?? ?? ?? ?? 00 52 74 09 e8"), loader) &&
                     (layout.route = absolute_rva(image, loader + 2)) != 0, "visual.route");
    const std::uint32_t route_word = word(image, loader + 2);
    std::uint32_t writers = 0, tests = 0;
    for (std::uint32_t at = image.text_begin + 2; at + 5 <= image.text_end; ++at) {
        if (word(image, at) != route_word) continue;
        if (image.base[at - 1] == 0xa2) ++writers;
        else if (image.base[at - 2] == 0x80 && image.base[at - 1] == 0x3d && image.base[at + 4] == 0) ++tests;
        else return "visual.route_reference";
    }
    DISCOVER_REQUIRE(writers == 1 && tests >= 1, "visual.route_reference");

    Sig compose = sig("80 3d");
    add_word(compose, route_word);
    add(compose, "00 8b cb 56 74 18 83 ec 10 8d 45 ?? 50 8d 45 ?? 50 ff b5 ?? ff ff ff "
                 "e8 ?? ?? ?? ?? eb 20 68 88 88 00 00 6a 00 6a 03 8d 45 ?? 50 8d 45 ?? 50 "
                 "8d 45 ?? 50 ff b5 ?? ff ff ff e8");
    std::uint32_t primary = 0;
    DISCOVER_REQUIRE(find_unique(image, compose, primary), "visual.compose_primary");
    layout.compose_memory = branch_target(image, primary + 0x1d);
    layout.compose_file = branch_target(image, primary + 0x3f);
    DISCOVER_REQUIRE(in_text(image, layout.compose_memory) && in_text(image, layout.compose_file) &&
                     exact(image, layout.compose_memory, "55 8b ec 6a ff") &&
                     exact(image, layout.compose_file, "55 8b ec 6a ff") &&
                     first_epilogue_is(image, layout.compose_memory, 0x800, "8b e5 5d c2 20 00"),
                     "visual.compose");
    // The secondary route site calls the same memory/file composition pair.
    std::uint32_t secondary[8]{};
    const std::uint32_t secondary_count = find_all(image, image.text_begin, image.text_end,
        sig("e8 ?? ?? ?? ?? eb 1f 6a 00 68 88 88 00 00 6a 00 6a 03"), secondary, 8);
    std::uint32_t secondary_pairs = 0;
    for (std::uint32_t index = 0; index < secondary_count && index < 8; ++index) {
        if (branch_target(image, secondary[index]) != layout.compose_memory) continue;
        std::uint32_t follow[4]{};
        const std::uint32_t follows = find_all(image, secondary[index] + 5, secondary[index] + 0x30,
                                               sig("e8"), follow, 4);
        for (std::uint32_t next = 0; next < follows && next < 4; ++next)
            if (branch_target(image, follow[next]) == layout.compose_file) {
                ++secondary_pairs;
                break;
            }
    }
    DISCOVER_REQUIRE(secondary_count <= 8 && secondary_pairs == 1, "visual.compose_secondary");

    Sig selector = sig("55 8b ec ff 75 08 68");
    add_word(selector, kGameSelectPrimaryVa);
    add(selector, "51 e8 ?? ?? ?? ?? 5d c2 04 00");
    DISCOVER_REQUIRE(find_unique(image, selector, layout.selector_primary) &&
                     branch_target(image, layout.selector_primary + 12) == native_forwarder,
                     "visual.selector_primary");
    selector = sig("55 8b ec ff 75 08 68");
    add_word(selector, kGameSelectSecondaryVa);
    add(selector, "51 e8 ?? ?? ?? ?? 5d c2 04 00");
    DISCOVER_REQUIRE(find_unique(image, selector, layout.selector_secondary) &&
                     branch_target(image, layout.selector_secondary + 12) == native_forwarder,
                     "visual.selector_secondary");

    layout.change_weapon_callback = registered_thiscall_callback(image, kGameChangeWeaponVa);
    DISCOVER_REQUIRE(layout.change_weapon_callback && exact(image, layout.change_weapon_callback,
        "55 8b ec ff 75 0c 8b 4d 08 e8 ?? ?? ?? ?? 5d c2 08 00"), "visual.change_weapon_callback");
    layout.change_weapon = branch_target(image, layout.change_weapon_callback + 9);
    DISCOVER_REQUIRE(in_text(image, layout.change_weapon) &&
                     exact(image, layout.change_weapon, "55 8b ec 6a ff"), "visual.change_weapon");
    layout.load_textures_callback = registered_thiscall_callback(image, kGameLoadTexturesVa);
    DISCOVER_REQUIRE(layout.load_textures_callback && exact(image, layout.load_textures_callback,
        "55 8b ec 51 ff 75 10 8b 4d 08 ff 75 0c e8 ?? ?? ?? ?? 59 5d c2 0c 00"),
        "visual.load_textures_callback");
    layout.load_textures = branch_target(image, layout.load_textures_callback + 13);
    DISCOVER_REQUIRE(in_text(image, layout.load_textures) &&
                     exact(image, layout.load_textures, "55 8b ec 6a ff"), "visual.load_textures");
    layout.hide_weapon_callback = registered_thiscall_callback(image, kGameHideWeaponVa);
    DISCOVER_REQUIRE(layout.hide_weapon_callback && exact(image, layout.hide_weapon_callback,
        "55 8b ec ff 75 0c 8b 4d 08 e8 ?? ?? ?? ?? 5d c2 08 00"), "visual.hide_weapon_callback");
    layout.hide_weapon = branch_target(image, layout.hide_weapon_callback + 9);
    DISCOVER_REQUIRE(in_text(image, layout.hide_weapon) &&
                     exact(image, layout.hide_weapon, "55 8b ec 6a ff 68"), "visual.hide_weapon");

    // Addon LoadFigures forwards to native 517110 through the shared shim.
    Sig native_load = sig("68");
    add_word(native_load, kGameLoadFiguresVa);
    add(native_load, "57 c6 45 fc 00 e8");
    std::uint32_t native_push = 0;
    DISCOVER_REQUIRE(find_unique(image, native_load, native_push) &&
                     branch_target(image, native_push + 10) == native_forwarder,
                     "visual.load_figures_native");
    layout.load_figures = frame_function_start(image, native_push, 0x80);
    DISCOVER_REQUIRE(layout.load_figures && exact(image, layout.load_figures, "55 8b ec 6a ff"),
                     "visual.load_figures");
    // ChangeWeapon loads the shell set, then the first and dual weapon sets.
    std::uint32_t loads[16]{};
    const std::uint32_t load_total = call_sites(image, layout.load_figures, loads, 16);
    std::uint32_t weapon_loads[4]{};
    std::uint32_t weapon_count = 0;
    for (std::uint32_t index = 0; index < load_total && index < 16; ++index)
        if (loads[index] > layout.change_weapon && loads[index] < layout.change_weapon + 0x1000) {
            if (weapon_count == 4) return "visual.weapon_loads";
            weapon_loads[weapon_count++] = loads[index];
        }
    DISCOVER_REQUIRE(load_total <= 16 && weapon_count == 3, "visual.weapon_loads");
    layout.weapon_load_returns = {{weapon_loads[1] + 5, weapon_loads[2] + 5}};

    // Attack animation-speed flag guards the 535C71 patch write.
    std::uint32_t anim_write = 0;
    DISCOVER_REQUIRE(find_unique(image, sig("6a 05 68 ?? ?? ?? ?? 68 71 5c 53 00"), anim_write),
                     "visual.anim_speed_flag");
    std::uint32_t anim_tests[4]{};
    const std::uint32_t anim_count = find_all(image, anim_write - 0x20, anim_write,
                                              sig("80 3d ?? ?? ?? ?? 00"), anim_tests, 4);
    DISCOVER_REQUIRE(anim_count >= 1 && anim_count <= 4 &&
                     (layout.anim_speed_flag = absolute_rva(image, anim_tests[anim_count - 1] + 2)) != 0,
                     "visual.anim_speed_flag");
    // The attack-rule flag skips the 55071F patch.
    std::uint32_t attack_rule = 0;
    DISCOVER_REQUIRE(find_unique(image, sig(
        "80 3d ?? ?? ?? ?? 00 75 64 6a 01 8d 45 ec c6 45 ec e9 50 68 1f 07 55 00"), attack_rule) &&
        (layout.attack_rule_flag = absolute_rva(image, attack_rule + 2)) != 0,
        "visual.attack_rule_flag");

    // Generated-texture lookup: the route test is preceded by the mouth-atlas
    // alias flag test and the alias map head load.
    Sig lookup = sig("80 3d");
    add_word(lookup, route_word);
    add(lookup, "00 0f 84");
    std::uint32_t route_lookup = 0, alias_flag = 0, alias_map = 0;
    DISCOVER_REQUIRE(find_unique(image, lookup, route_lookup), "visual.route_lookup");
    std::uint32_t flags[4]{};
    const std::uint32_t flag_count = find_all(image, route_lookup - 0x180, route_lookup,
                                              sig("80 3d ?? ?? ?? ?? 00 0f 84"), flags, 4);
    DISCOVER_REQUIRE(flag_count == 1, "visual.alias_flag");
    alias_flag = flags[0];
    DISCOVER_REQUIRE(find_unique(image, route_lookup - 0x400, route_lookup,
                                 sig("75 06 8b 35 ?? ?? ?? ?? c7 45 fc ff ff ff ff"), alias_map),
                     "visual.alias_map");
    layout.alias_flag = absolute_rva(image, alias_flag + 2);
    layout.alias_map = absolute_rva(image, alias_map + 4);
    DISCOVER_REQUIRE(layout.alias_flag && layout.alias_map, "visual.alias");

    // Completed generated cache list: its clear routine reloads the head.
    std::uint32_t clear = 0;
    DISCOVER_REQUIRE(find_unique(image, sig(
        "55 8b ec 8b 15 ?? ?? ?? ?? 56 57 8b 7d 08 e8 ?? ?? ?? ?? a1 ?? ?? ?? ?? b9 ?? ?? ?? ?? "
        "89 00 a1"), clear) && word(image, clear + 5) == word(image, clear + 20) &&
        (layout.completed_list = absolute_rva(image, clear + 5)) != 0, "visual.completed_list");

    // Forced weapon visibility flag and id vector, both read by HideWeapon.
    std::uint32_t force = 0;
    DISCOVER_REQUIRE(find_unique(image, layout.hide_weapon, layout.hide_weapon + 0x200,
                                 sig("3b c1 75 f5 32 c0 80 3d ?? ?? ?? ?? 00 88 45"), force) &&
                     (layout.force_flag = absolute_rva(image, force + 8)) != 0,
                     "visual.force_flag");
    std::uint32_t vectors[8]{};
    const std::uint32_t vector_count = find_all(image, layout.hide_weapon, layout.hide_weapon + 0x200,
                                                sig("a1 ?? ?? ?? ?? 8b 0d ?? ?? ?? ??"), vectors, 8);
    for (std::uint32_t index = 0; index < vector_count && index < 8; ++index) {
        if (word(image, vectors[index] + 7) != word(image, vectors[index] + 1) + 4) continue;
        const std::uint32_t list = absolute_rva(image, vectors[index] + 1);
        DISCOVER_REQUIRE(!layout.force_list || layout.force_list == list, "visual.force_list");
        layout.force_list = list;
    }
    DISCOVER_REQUIRE(vector_count <= 8 && layout.force_list, "visual.force_list");

    // Animation-coefficient receiver at the game's 52F3A9 jump: the older
    // inline read, or a call to the payload reader. Both resume at 52F3B0.
    std::uint32_t tail = 0;
    DISCOVER_REQUIRE(find_unique(image, sig("83 be 5c 02 00 00 01 68 b0 f3 52 00 c3"), tail),
                     "visual.receiver");
    if (tail >= 18 && exact(image, tail - 18,
                            "6a 04 8d 86 78 01 00 00 50 8b cf b8 00 e0 47 00 ff d0")) {
        layout.receiver = tail - 18;
    } else if (tail >= 7 && exact(image, tail - 7, "57 56 e8")) {
        layout.receiver = tail - 7;
        layout.payload_reader = branch_target(image, tail - 5);
        const std::uint32_t reader = layout.payload_reader;
        std::uint32_t flag_test = 0, posrot = 0, append = 0;
        DISCOVER_REQUIRE(in_text(image, reader) && exact(image, reader, "55 8b ec 83 e4 f8") &&
                         find_unique(image, reader, reader + 0x30,
                                     sig("80 3d ?? ?? ?? ?? 00"), flag_test) &&
                         absolute_rva(image, flag_test + 2) == layout.anim_speed_flag &&
                         find_unique(image, reader, reader + 0x30, sig("ba 00 e0 47 00"), posrot) &&
                         find_unique(image, reader, reader + 0x60,
                                     sig("6a 04 8d 87 78 01 00 00 50 ff d2"), append) &&
                         first_epilogue_is(image, reader, 0x80, "8b e5 5d c2 08 00"),
                         "visual.payload_reader");
    } else {
        return "visual.receiver_form";
    }
    std::uint32_t update_to[2]{};
    const std::uint32_t update_count = find_all(image, image.text_begin, image.text_end, sig(
        "6a 04 8d 86 78 01 00 00 50 8b cf b8 a0 dc 47 00 ff d0 8b cf b8 20 dc 47 00 ff d0 "
        "68 7d ee 52 00 c3"), update_to, 2);
    DISCOVER_REQUIRE(update_count <= 1, "visual.update_to_receiver");
    layout.update_to_receiver = update_count ? update_to[0] : 0;
    out = layout;
    return nullptr;
}

const char* discover_combat(const Image& image,AddonCombatLayout& output) noexcept {
    std::uint32_t expanded=0,selfcast=0,wrapper=0;
    if(!find_unique(image,sig("0f b7 41 1a 83 f8 4a 77 11 0f b6 80 ?? ?? ?? ?? ff 24 85 ?? ?? ?? ?? b0 01 c3 32 c0 c3"),expanded))
        return "combat.expanded_shape";
    AddonCombatLayout value{};
    const auto table=[&](std::uint32_t entry_operand,std::uint32_t jump_operand,
                         std::uint32_t yes,std::uint32_t no,auto& values) {
        const auto entries=absolute_rva(image,entry_operand),jumps=absolute_rva(image,jump_operand);
        if(!entries || entries>image.size-values.size() || !jumps || jumps>image.size-8 ||
           absolute_rva(image,jumps)!=yes || absolute_rva(image,jumps+4)!=no)return false;
        for(std::size_t i=0;i<values.size();++i) {
            const auto choice=image.base[entries+i];
            if(choice>1)return false;
            values[i]=choice==0;
        }
        return true;
    };
    if(!table(expanded+12,expanded+19,expanded+23,expanded+26,value.offensive))
        return "combat.expanded_table";
    if(find_unique(image,sig("55 8b ec 56 8b 75 08 56 ff 15 ?? ?? ?? ?? 80 3d ?? ?? ?? ?? 00 8a d0 74 0b 8b ce e8 ?? ?? ?? ?? 84 c0 75 12 8b ce e8 ?? ?? ?? ?? 84 c0 75 07 84 d2 75 03 5e 5d c3 b0 01 5e 5d c3"),wrapper)) {
        if(!find_unique(image,sig("85 c9 74 1a 0f b7 41 1a 83 f8 36 77 11 0f b6 80 ?? ?? ?? ?? ff 24 85 ?? ?? ?? ?? b0 01 c3 32 c0 c3"),selfcast) ||
           branch_target(image,wrapper+27)!=selfcast || branch_target(image,wrapper+38)!=expanded)
            return "combat.predicate_owner";
        if(!table(selfcast+16,selfcast+23,selfcast+27,selfcast+30,value.selfcast))
            return "combat.selfcast_table";
    } else if(find_unique(image,sig("55 8b ec 56 8b 75 08 56 ff 15 ?? ?? ?? ?? 80 3d ?? ?? ?? ?? 00 8a d0 74 1b 85 f6 74 17 0f b7 4e 1a 83 f9 36 77 0e 0f b6 81 ?? ?? ?? ?? ff 24 85 ?? ?? ?? ?? 8b ce e8 ?? ?? ?? ?? 84 c0 75 07 84 d2 75 03 5e 5d c3 b0 01 5e 5d c3"),wrapper)) {
        // JMV's compiler inlines the flag-controlled self-cast table into
        // the same wrapper. Its false leaf proceeds to the expanded call.
        if(branch_target(image,wrapper+54)!=expanded)return "combat.predicate_owner";
        if(!table(wrapper+41,wrapper+48,wrapper+70,wrapper+52,value.selfcast))
            return "combat.selfcast_table";
    } else return "combat.predicate_shapes";
    value.selfcast_enabled=absolute_rva(image,wrapper+16);
    if(!value.selfcast_enabled)return "combat.selfcast_flag";
    output=value;return nullptr;
}

// The modifier constants 30, 120, 100 and 0.8 of the skill multiplier.
bool effect_modifier_constants(const Image& image,std::uint32_t at) noexcept {
    const std::uint8_t constants[4][8]={{0,0,0,0,0,0,0x3e,0x40},{0,0,0,0,0,0,0x5e,0x40},
        {0,0,0,0,0,0,0x59,0x40},{0,0,0,0xa0,0x99,0x99,0xe9,0x3f}};
    for (unsigned i=0;i<4;++i) {
        const auto instruction=at+i*6,operand=absolute_rva(image,instruction+2);
        if (!matches(image,instruction,sig(i==0 ? "dc 0d ?? ?? ?? ??" : i==3 ? "dc 05 ?? ?? ?? ??" : "dc 35 ?? ?? ?? ??")) ||
            !operand || operand>image.size-8 || std::memcmp(image.base+operand,constants[i],8)) return false;
    }
    return true;
}

// The Dejavu (LS) build of the same DrawSpellEffects body: another frame and
// register allocation, and the perk lookup inlined. It reads the native perk
// database through the addon's reference to the game object 0x7AF268
// (entries +0xC/+0x10, 0x44 bytes, modifier +0x34) with the unit's perk level
// at +0x41A+subtype, exactly what the JMV build's perk getter reads.
const char* discover_inline_effect_values(const Image& image,std::uint32_t body,std::uint32_t game) noexcept {
    const std::uint32_t end=body+0x600;
    std::uint32_t lookup_anchor=0,attacking=0,skill=0,perk=0,constants=0,power=0,maximum_anchor=0;
    if (!find_unique(image,body,end,sig("80 3c ?? 08 76 0a 80 3c ?? 35 c6 45 bf 00 75 04 c6 45 bf 01"),attacking) ||
        !find_unique(image,body,end,sig("0f b6 83 3e 03 00 00 8b 4f 14 83 e8 19 d9 ee 89 45 ?? d9 55 ?? 83 f9 02 77 0f "
            "0f b6 83 0e 04 00 00 89 45 ?? db 45 ?? eb 2c 8d 41 fd 83 f8 01 77 0f 0f b6 83 0f 04 00 00 89 45 ?? db 45 ?? eb 15 "
            "8d 41 fb 83 f8 02 77 10 0f b6 83 10 04 00 00 89 45 ?? db 45 ?? d9 5d ??"),skill) ||
        !find_unique(image,body,end,sig("8a 84 0b 1a 04 00 00 84 c0 74 ?? 0f b6 f8 8d 0c 49 a1 ?? ?? ?? ?? 83 c1 14 03 f9 "
            "8b 50 0c 8b 48 10 b8 79 78 78 78 2b ca 89 55 ?? f7 e9 c1 fa 05 8b c2 c1 e8 1f 03 c2 3b f8 7d ?? "
            "8b 4d ?? 8b c7 c1 e0 04 03 c7 8d 04 81 85 c0 74 ?? 8b 40 34"),perk) ||
        !find_unique(image,body,end,sig("dc 0d ?? ?? ?? ?? dc 35 ?? ?? ?? ?? dc 35 ?? ?? ?? ?? dc 05 ?? ?? ?? ??"),constants) ||
        !find_unique(image,body,end,sig("8b 45 ?? 3b 87 9c 01 00 00 7d ?? 80 7d bf 00 74 05 d9 45 ?? eb 02 d9 e8 "
            "8b 8f 98 01 00 00 03 ce d9 5d ?? 8b 41 08"),power) ||
        !(attacking<skill && skill<perk && perk<constants && constants<power)) return "effects.value_fields";
    const auto database=absolute_rva(image,perk+18);
    if (!database || database>image.size-4 || word(image,database)!=game+0x3af268) return "effects.perk_database";
    if (!effect_modifier_constants(image,constants)) return "effects.modifier_constants";
    if (!find_unique(image,body,end,sig("b9 ?? ?? ?? ?? ff 73 0c e8 ?? ?? ?? ?? 8b 93 98 01 00 00"),lookup_anchor) ||
        !find_unique(image,body,end,sig("0f b6 c1 8b cf 50 e8 ?? ?? ?? ?? 8b 93 98 01 00 00 ff 75 e8 d9 5d ?? d9 05"),maximum_anchor))
        return "effects.helper_calls";
    const auto ten=absolute_rva(image,maximum_anchor+25);
    if (!ten || ten>image.size-4 || word(image,ten)!=0x41200000) return "effects.precision";
    const auto lookup=branch_target(image,lookup_anchor+8),maximum=branch_target(image,maximum_anchor+6);
    Sig lookup_pattern=sig("55 8b ec ff 75 08 68"); add_word(lookup_pattern,game+0x15fb00); add(lookup_pattern,"51 e8 ?? ?? ?? ?? 5d c2 04 00");
    Sig maximum_pattern=sig("55 8b ec ff 75 08 68"); add_word(maximum_pattern,game+0x10f690); add(maximum_pattern,"51 e8 ?? ?? ?? ?? 5d c2 04 00");
    const auto forwarder=branch_target(image,lookup+12);
    if (word(image,lookup_anchor+1)!=game+0x3aff60 || !matches(image,lookup,lookup_pattern) ||
        !matches(image,maximum,maximum_pattern) || branch_target(image,maximum+12)!=forwarder ||
        !matches(image,forwarder,sig("58 59 87 04 24 ff e0"))) return "effects.native_owners";
    return nullptr;
}

const char* discover_effect_values(const Image& image,AddonLayout& out) noexcept {
    const auto game=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)));
    const auto callback=registered_thiscall_callback(image,game+0x246b90);
    if (!callback || !matches(image,callback,sig("55 8b ec ff 75 0c 8b 4d 08 e8 ?? ?? ?? ?? 5d c2 08 00"))) return "effects.callback";
    const auto body=branch_target(image,callback+9);
    if (!in_text(image,body) || !matches(image,body,sig("55 8b ec 6a ff 68 ?? ?? ?? ?? 64 a1 00 00 00 00 50 81 ec ?? 00 00 00")))
        return "effects.value_fields";
    if (!matches(image,body+0x236,sig("80 3c 32 08 8b 44 32 08 89 45 98 76 0d 80 3c 32 35 89 45 98 c6 45 bf 00 75 04 c6 45 bf 01"))) {
        const char* failure=discover_inline_effect_values(image,body,game);
        if (!failure) out.effect_value_callback=callback;
        return failure;
    }
    if (!matches(image,body,sig("55 8b ec 6a ff 68 ?? ?? ?? ?? 64 a1 00 00 00 00 50 81 ec b8 00 00 00")) ||
        !matches(image,body+0x236,sig("80 3c 32 08 8b 44 32 08 89 45 98 76 0d 80 3c 32 35 89 45 98 c6 45 bf 00 75 04 c6 45 bf 01")) ||
        !matches(image,body+0x254,sig("8b 4f 14 0f b6 83 3e 03 00 00 83 e8 19 d9 ee 89 45 a8 8d 51 07 d9 55 b4 83 f9 02 77 0f "
            "0f b6 83 0e 04 00 00 89 45 ac db 45 ac eb 2c 8d 41 fd 83 f8 01 77 0f 0f b6 83 0f 04 00 00 89 45 ac db 45 ac eb 15 "
            "8d 41 fb 83 f8 02 77 10 0f b6 83 10 04 00 00 89 45 ac db 45 ac d9 5d b4")) ||
        !matches(image,body+0x330,sig("80 7d bf 00 74 05 d9 45 a8 eb 02 d9 e8")) ||
        !matches(image,body+0x33d,sig("8b 8f 98 01 00 00 03 ce d9 5d ac 8b 41 08 89 45 98 d9 45 98 d8 4d ac d9 5d ac")) ||
        !matches(image,body+0x325,sig("8b 45 84 3b 87 9c 01 00 00"))) return "effects.value_fields";
    std::uint32_t lookup_anchor=0,perk_anchor=0,maximum_anchor=0;
    if (!find_unique(image,body,body+0x800,sig("ff 73 0c e8 ?? ?? ?? ?? 8b 93 98 01 00 00"),lookup_anchor) ||
        !find_unique(image,body,body+0x800,sig("52 8d 8b 30 03 00 00 e8 ?? ?? ?? ?? 85 c0 74 18 8b 40 34"),perk_anchor) ||
        !find_unique(image,body,body+0x800,sig("8b 0c 32 dd d8 0f b6 c1 8b cf 50 e8 ?? ?? ?? ?? 8b 93 98 01 00 00"),maximum_anchor)) return "effects.helper_calls";
    const auto lookup=branch_target(image,lookup_anchor+3),maximum=branch_target(image,maximum_anchor+11);
    const auto ten=absolute_rva(image,maximum_anchor+30);
    if (!matches(image,maximum_anchor+22,sig("ff 75 e8 d9 5d b8 d9 05 ?? ?? ?? ?? d8 5d b8 8b 4c 32 04 df e0 dd 45 a4 f6 c4 05")) ||
        !ten || ten>image.size-4 || word(image,ten)!=0x41200000) return "effects.precision";
    Sig lookup_pattern=sig("55 8b ec ff 75 08 68"); add_word(lookup_pattern,game+0x15fb00);
    add(lookup_pattern,"68"); add_word(lookup_pattern,game+0x3aff60); add(lookup_pattern,"e8 ?? ?? ?? ?? 5d c2 04 00");
    Sig maximum_pattern=sig("55 8b ec ff 75 08 68"); add_word(maximum_pattern,game+0x10f690);
    add(maximum_pattern,"51 e8 ?? ?? ?? ?? 5d c2 04 00");
    const auto forwarder=branch_target(image,lookup+16);
    if (!matches(image,lookup,lookup_pattern) || !matches(image,maximum,maximum_pattern) ||
        branch_target(image,maximum+12)!=forwarder || !matches(image,forwarder,sig("58 59 87 04 24 ff e0"))) return "effects.native_owners";
    const auto perk=branch_target(image,perk_anchor+7);
    Sig perk_pattern=sig("55 8b ec 8b 45 08 8a 94 08 e3 00 00 00 84 d2 75 06 33 c0 5d c2 04 00 8d 0c 45 ff ff ff ff 03 c8 "
        "b8 79 78 78 78 56 0f b6 f2 03 f1 8b 0d");
    add_word(perk_pattern,game+0x3af278); add(perk_pattern,"57 8b 3d"); add_word(perk_pattern,game+0x3af274);
    add(perk_pattern,"2b cf f7 e9 c1 fa 05 8b c2 c1 e8 1f 03 c2 3b f0 7d 10 8b c6 c1 e0 04 03 c6 8d 04 87 5f 5e 5d c2 04 00 5f 33 c0 5e 5d c2 04 00");
    // The getter reads the native perk database that combat_perk_modifier reads.
    if (!matches(image,perk,perk_pattern)) return "effects.perk_getter";
    if (!effect_modifier_constants(image,body+0x303)) return "effects.modifier_constants";
    out.effect_value_callback=callback; return nullptr;
}

#undef DISCOVER_REQUIRE

} // namespace

AddonLayout discover_addon_layout(const std::uint8_t* base) noexcept {
    AddonLayout layout{};
    Image image{};
    if (!image_view(base, image)) {
        layout.hud_atlas_failure = layout.pane_failure = layout.res_cache_failure =
            layout.hud_visual_failure = layout.visual_failure = layout.resolver_failure =
            "addon.image_headers";
        return layout;
    }
    layout.hud_atlas_failure = discover_hud_atlas(image, layout.hud_atlas);
    layout.combat_failure = discover_combat(image,layout.combat);
    layout.effect_value_failure=discover_effect_values(image,layout);
    layout.pane_failure = discover_pane(image, layout.pane);
    Resolver resolver{};
    const char* resolver_failure = discover_resolver(image, resolver);
    if (!resolver_failure) {
        layout.resolver = {resolver.thunk, resolver.member_slot};
        layout.resolver_failure = nullptr;
    } else {
        layout.resolver_failure = discover_forwarding_resolver(image, layout.resolver);
        if (layout.resolver_failure) layout.resolver = {};
    }
    layout.res_cache_failure = resolver_failure ? resolver_failure :
        discover_res_cache(image, resolver, layout.res_cache);
    std::uint32_t forwarder = 0;
    const bool forwarder_found = find_unique(image, sig("58 59 87 04 24 ff e0"), forwarder);
    layout.hud_visual_failure = layout.hud_atlas_failure ? layout.hud_atlas_failure :
        !forwarder_found ? "addon.native_forwarder" :
        discover_hud_visual(image, layout.hud_atlas, forwarder, layout.hud_visual);
    layout.visual_failure = resolver_failure ? resolver_failure :
        !forwarder_found ? "addon.native_forwarder" :
        discover_visual(image, resolver, forwarder, layout.visual);
    if (layout.hud_atlas_failure) layout.hud_atlas = {};
    if (layout.pane_failure) layout.pane = {};
    if (layout.res_cache_failure) layout.res_cache = {};
    if (layout.hud_visual_failure) layout.hud_visual = {};
    if (layout.visual_failure) layout.visual = {};
    return layout;
}

} // namespace owned
