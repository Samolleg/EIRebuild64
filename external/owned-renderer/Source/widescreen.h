#pragma once
// Experimental owned widescreen for stock vanilla: hor+ projection, centred
// 4:3 content and edge-anchored HUD. Foreign layout patches are rejected.
#include "widescreen_layout.h"

#include <array>
#include <cstdint>
#include <vector>
#include <string>

namespace owned {

struct WidescreenSettings {
    widescreen::HudWidth hud = widescreen::HudWidth::wide_16_9;
    int ui_scale_percent = 100;
};

// At DirectDrawCreate (after the addon applied its patches, before the
// visualizer builds its matrices and before the engine bridge hooks).
// sites_ok: the widescreen site group of the on-disk game.exe verified.
void widescreen_install(bool sites_ok, const WidescreenSettings& settings) noexcept;
// Install using the running image and its verified on-disk bytes.
void widescreen_install_image(std::uint8_t* image, std::vector<std::uint8_t> file,
                              bool (*addon_owned)(const void*), const WidescreenSettings& settings) noexcept;
// After bridge teardown on explicit unload, or before it installs on startup failure.
bool widescreen_uninstall() noexcept;
// At SetCooperativeLevel: the surface the game renders and lays out for.
void widescreen_set_surface(unsigned width, unsigned height) noexcept;
// Native span refresh follows every live screen and its owned visual/input objects.
const char* widescreen_display_blocker() noexcept;
// Read-only validation of current figures, surfaces and hit areas before Apply;
// preserves the native font/raster unit while allowing anchor-span changes.
const char* widescreen_display_span_blocker(unsigned width,unsigned height) noexcept;
void widescreen_refresh_display(unsigned width, unsigned height) noexcept;
// At BeginScene: updates owned uniform HUD composition and paired input.
void widescreen_set_ui_scale(int percent) noexcept;
// Match the established 500 m addon landscape admission, growing with owned
// draw distance. Native decorative phase/streaming publication stays intact.
void widescreen_set_landscape_radius(float draw_distance) noexcept;
// quest_items: slots 4-7 of the game's items panel (quest items, main hero
// only), laid out as their own widget; they have no game object of their own.
enum class HudPanel : unsigned { spells, abilities, minimap, character_info, portraits, weapons, items, messages, quest_items, stance, game_speed, count };
struct HudPanelDescription { const char* key; const char* label; };
inline constexpr std::array<HudPanelDescription, 11> kHudPanels{{
    {"spell_bar", "Spell bar"}, {"ability_bar", "Ability bar"}, {"minimap", "Minimap"},
    {"character_info", "Character info"}, {"portraits", "Character portraits"},
    {"weapon_switcher", "Weapon switcher"}, {"quick_items", "Quick items"},
    {"message_panel", "Loot and quest messages"}, {"quest_items", "Quest items"},
    {"stance", "Current stance"}, {"game_speed", "Game speed and time"}
}};
struct HudFadeSettings {bool enabled=false;float transparency_percent=80.f;};
struct HudFadeTiming {int out_ms=250,in_ms=250,idle_ms=2000;};
void widescreen_set_hud_fades(const std::array<HudFadeSettings,kHudPanels.size()>& settings,const std::array<float,2>& corner_scales,HudFadeTiming timing) noexcept;
void widescreen_update_hud_fades(float mouse_x,float mouse_y) noexcept;
bool widescreen_hud_hovered(HudPanel panel) noexcept;
bool widescreen_hud_fade_enabled(HudPanel panel) noexcept;
float widescreen_hud_opacity(HudPanel panel) noexcept;
void widescreen_hud_activity(HudPanel panel) noexcept;
// The mission screen's own OnActivate is running: it is already the current
// screen but has not assigned its message panel members yet.
void widescreen_mission_activation(bool active) noexcept;
// Compare renderer-native content values, never draw/rebuild/animation serials.
void widescreen_hud_content_values(HudPanel panel,const std::vector<std::uint64_t>& values) noexcept;
struct HudPanelSettings {
    float scale_percent = 100.f;
    // -1 keeps the original anchor. Slot layouts place their starting end,
    // independently of the number of native or preview slots.
    float x_percent = -1.f, y_percent = -1.f;
    float buttons_scale_percent=100.f;
    // 0 auto (stock side at reset), 1 stock, 2 left, 3 right, 4 top, 5 bottom.
    int buttons_side=0;
    int direction=0;
    float padding=0.f;
    float border_padding=3.f;
    bool preview=false;
    unsigned preview_count=3;
    // Slot bars wrap into a further column (vertical) or row (horizontal)
    // every N slots; 0 keeps one line. Not used by the portraits.
    unsigned slots_per_line=0;
};
struct HudPanelBounds {
    float left=0, top=0, width=0, height=0;
    float native_width=0, native_height=0;
};
struct HudPreviewSlot {HudPanelBounds bounds;unsigned index=0;};
// Borrowed unit addresses are valid only for the synchronous bridge read;
// consumers publish values and never retain these addresses.
struct HudPortraitSlot {const void* unit=nullptr;std::int32_t id=0;HudPanelBounds bounds;};
std::size_t widescreen_hud_portrait_slots(HudPortraitSlot* slots,std::size_t capacity) noexcept;
void widescreen_hud_preview_slots(HudPanel panel,std::vector<HudPreviewSlot>& slots);
// Scope each synchronous native figure call; keeps slot glyphs upright and bindings intact.
void widescreen_apply_hud_figure_layout(const void* figure) noexcept;
bool widescreen_hud_header_bounds(HudPanel panel,HudPanelBounds& bounds) noexcept;
void widescreen_set_hud_panels(const std::array<HudPanelSettings, 7>& settings) noexcept;
// Quest items (HudPanel::quest_items): x/y -1 keeps the stock position; the
// slots keep the stock layout of the items panel's areas 4-7.
struct QuestItemSettings { float scale_percent=100.f, x_percent=-1.f, y_percent=-1.f; };
void widescreen_set_quest_items(const QuestItemSettings& settings) noexcept;
bool widescreen_hud_panel_bounds(HudPanel panel, HudPanelBounds& bounds) noexcept;
void widescreen_hud_position_percent(HudPanel panel,float left,float top,float width,float height,float& x,float& y) noexcept;
struct HudAssetFrame { HudPanelBounds bounds; float thickness=0.f; unsigned edges=15; bool portrait=false; float opacity=1.f; };
void widescreen_hud_asset_frames(std::vector<HudAssetFrame>& frames);
bool widescreen_hud_asset_frame(HudPanel panel,HudAssetFrame& frame) noexcept;
bool widescreen_replaces_hud_frame(const void* figure) noexcept;
// The verified screen tooltip boundary follows all normal HUD submissions.
void widescreen_set_hud_frame_sink(void* owner,void (*sink)(void*) noexcept) noexcept;
void widescreen_clear_hud_frame_sink(void* owner) noexcept;

struct MessagePanelSettings {
    float width_percent = 100.f, height_percent = 100.f;
    float centre_x_percent = 50.f, top_y_percent = 0.f;
    float text_scale_percent = 100.f;
    float buttons_scale_percent=100.f;
    int buttons_side=0;
    float border_padding=3.f;
};
struct MessagePanelBounds { float left = 0, top = 0, width = 0, height = 0; };
// Owned dimensions/reflow and uniform composition; native text/mode/slide time remain authoritative.
void widescreen_set_message_panel(const MessagePanelSettings& settings) noexcept;
void widescreen_set_combat_text_scale(int percent) noexcept;
// The independent combat text size (owned combat text, combat_text.h).
float widescreen_combat_text_scale() noexcept;
bool widescreen_message_panel_bounds(MessagePanelBounds& bounds) noexcept;

// The active layout (valid after widescreen_set_surface).
const widescreen::Layout& widescreen_layout() noexcept;

// The interface sites are ours (installed and verified).
bool widescreen_interface_owned() noexcept;
// Active area membership owner, scoped by the owned widget wrapper.
const void* widescreen_input_area_object() noexcept;
// CI3DObjectInfo::Draw through our anchored wrapper, callable like the game
// function (__thiscall, one argument): the character pane forwards here.
const void* widescreen_info_draw_entry() noexcept;
// Our CInterface3D::VirtualToScreen(CRect&) and the camera's cursor-delta
// conversion at the RMB camera call site.
const void* widescreen_virtual_to_screen_rect() noexcept;
const void* widescreen_cursor_delta() noexcept;
// The 4:3 content in interface pixels (800 and 600 placed units), updated
// with the surface: the divisors the camera's mouse normalisation reads.
const std::int32_t* widescreen_content_width() noexcept;
const std::int32_t* widescreen_content_height() noexcept;

} // namespace owned
