// Embedded build only (EIRebuild links the renderer into the game).
// engine_bridge.cpp is compiled for its owned-visual frame pipeline; its
// game.exe hook installers and hook-only helpers below are never called in an
// embedded host, whose game code owns these features directly.
#ifdef EID3D11_EMBEDDED
#include "camp_inventory.h"
#include "res_cache_release.h"
#include "widescreen.h"
#include "test_harness.h"

namespace owned {
bool camp_inventory_observation_enabled() noexcept { return false; }
void camp_inventory_observe_close_call(const char*, std::int64_t, std::int64_t) noexcept {}
void camp_inventory_observe_count_time_clock(std::int64_t) noexcept {}
void camp_inventory_observe_modal_idle(std::int64_t, std::int64_t) noexcept {}

bool install_res_cache_release_hooks(std::uint8_t*, std::uint8_t*, const ResCacheAddonLayout*) noexcept { return false; }
bool uninstall_res_cache_release_hooks() noexcept { return true; }
const char* res_cache_release_status_name(ResCacheReleaseStatus) noexcept { return "embedded"; }
void res_cache_note_get(const void*, const void*) noexcept {}

bool widescreen_replaces_hud_frame(const void*) noexcept { return false; }
const std::int32_t* widescreen_content_height() noexcept { return nullptr; }
const std::int32_t* widescreen_content_width() noexcept { return nullptr; }
std::size_t widescreen_hud_portrait_slots(HudPortraitSlot*, std::size_t) noexcept { return 0; }
void widescreen_apply_hud_figure_layout(const void*) noexcept {}
const void* widescreen_cursor_delta() noexcept { return nullptr; }
const void* widescreen_info_draw_entry() noexcept { return nullptr; }
const void* widescreen_virtual_to_screen_rect() noexcept { return nullptr; }

void debug_lightning_world_frame(void*) noexcept {}
void game_back_clip_world_frame(void*) noexcept {}
void load_pause_world_frame(void*, bool, std::uint64_t, bool) noexcept {}
}
#endif
