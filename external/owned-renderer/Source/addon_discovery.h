#pragma once

#include "res_cache_release.h"

#include <array>
#include <cstdint>

namespace owned {

// RVAs inside one loaded addon.dll image, found at load time from code shape
// rather than per-build tables: unique signatures anchored on fixed game
// addresses, the addon's hook-registration calls, call targets, RET sizes
// and data references. Each contract carries its own failure reason; a
// failed contract leaves its fields zero and its installer rejects visibly.

struct AddonHudAtlasLayout {
    std::uint32_t producer = 0;
    std::uint32_t lock_call = 0;
    std::uint32_t texture_lock_thunk = 0;
    std::uint32_t addressing = 0;
    std::uint32_t write = 0;
    std::uint32_t cleanup = 0;
    std::uint32_t result = 0;
    std::uint32_t variant = 0;         // overlay variant word read by the producer
    std::uint32_t variant_writer = 0;  // the only `mov [variant], 0`
};

struct AddonPaneLayout {
    std::uint32_t draw_start = 0;
    std::uint32_t draw_end = 0;
    std::uint32_t virtual_to_screen = 0;
    std::uint32_t current_shift = 0;
    std::uint32_t virtual_shift = 0;
    std::uint32_t screen_effective = 0;
    std::uint32_t screen_shift = 0;
    std::uint32_t screen_height = 0;
};

constexpr std::uint32_t kNoAddonLane = UINT32_MAX;

struct AddonHudVisualLayout {
    std::uint32_t producer = 0;          // shared with the HUD atlas contract
    std::uint32_t variant = 0;
    std::uint32_t variant_writer = 0;
    std::uint32_t weapon_set_unit = 0;   // registered callback
    std::uint32_t spell_callback = 0;    // registered callback
    std::uint32_t spell_set_unit = 0;    // body called by the callback
    std::uint32_t skill_set_unit = 0;    // registered callback
    std::uint32_t add_figure = 0;        // native AddFigure forwarding shim
    std::uint32_t native_forwarder = 0;
    std::array<std::uint32_t, 4> producer_calls{};
    std::array<std::uint32_t, 4> add_figure_calls{};
    std::uint32_t lane_count = 0;
    // Optional autocast indicator box (absent in builds without it).
    std::uint32_t autocast_box = 0;
    std::uint32_t autocast_call = 0;
    std::uint32_t autocast_lane = kNoAddonLane;
};

struct AddonVisualLayout {
    std::uint32_t route = 0;             // generated-MMP memory route byte
    std::uint32_t compose_memory = 0;
    std::uint32_t compose_file = 0;
    std::uint32_t selector_primary = 0;  // native 5190C0 shim
    std::uint32_t selector_secondary = 0;// native 519380 shim
    std::uint32_t native_forwarder = 0;
    std::uint32_t resolver_thunk = 0;    // installed in the resource Get slot
    std::uint32_t resolver_body = 0;
    std::uint32_t lookup = 0;
    std::uint32_t member_slot = 0;       // holds the native Get
    std::array<std::uint32_t, 2> member_calls{};
    std::uint32_t change_weapon_callback = 0;
    std::uint32_t change_weapon = 0;
    std::array<std::uint32_t, 2> weapon_load_returns{};
    std::uint32_t load_textures_callback = 0;
    std::uint32_t load_textures = 0;
    std::uint32_t hide_weapon_callback = 0;
    std::uint32_t hide_weapon = 0;
    std::uint32_t load_figures = 0;
    std::uint32_t anim_speed_flag = 0;
    std::uint32_t attack_rule_flag = 0;
    std::uint32_t alias_flag = 0;
    std::uint32_t alias_map = 0;
    std::uint32_t completed_list = 0;
    std::uint32_t force_flag = 0;
    std::uint32_t force_list = 0;
    std::uint32_t receiver = 0;          // target of the game's 52F3A9 jump
    std::uint32_t payload_reader = 0;    // zero for the inline receiver form
    std::uint32_t update_to_receiver = 0;// optional 52EE76 receiver
};

// Resource-selection contract alone: the resolver thunk the addon installs in
// the resource Get slot and the addon data word holding the native Get that
// it calls for every override and stock lookup. Found for addons whose wider
// res_cache/visual contracts do not match, so stock appearance publication
// can still coexist with the addon's resource overrides.
struct AddonResolverLayout {
    std::uint32_t thunk = 0;
    std::uint32_t member_slot = 0;
};
struct AddonCombatLayout {
    std::array<bool,75> offensive{};
    std::array<bool,55> selfcast{};
    std::uint32_t selfcast_enabled=0;
};

struct AddonLayout {
    std::uint32_t effect_value_callback=0, effect_perk_getter=0;
    const char* effect_value_failure="not_discovered";
    AddonCombatLayout combat;
    const char* combat_failure="not_discovered";
    AddonResolverLayout resolver;
    const char* resolver_failure = "not_discovered";
    AddonHudAtlasLayout hud_atlas;
    const char* hud_atlas_failure = "not_discovered";
    AddonPaneLayout pane;
    const char* pane_failure = "not_discovered";
    ResCacheAddonLayout res_cache{};
    const char* res_cache_failure = "not_discovered";
    AddonHudVisualLayout hud_visual;
    const char* hud_visual_failure = "not_discovered";
    AddonVisualLayout visual;
    const char* visual_failure = "not_discovered";
};

// `image` is a loaded (relocated) PE image. Reads only inside its headers'
// SizeOfImage and searches code only inside its first executable section.
AddonLayout discover_addon_layout(const std::uint8_t* image) noexcept;

} // namespace owned
