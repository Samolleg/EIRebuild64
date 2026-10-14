#pragma once
#include <windows.h>
#include "widescreen.h"

namespace owned {
// Value-only publications. No native object pointers, addresses or ownership of
// engine geometry cross this boundary. Rectangles are in native placed units.
struct HudNativeSlot {
    RECT rect{}; bool active=false;
    POINT shift{};
    // Final native Show() flag, including multiple selected characters.
    bool selection_frame=false;
};
struct HudNativePanel {
    bool visible=false;
    RECT rect{};
    std::vector<HudNativeSlot> slots;
    unsigned native_count=0;
    float slide=0.f, opacity=1.f;
    bool shell_visible=true; // Retail fallback when the shell pair is absent.
    // Native slide displacement is not inferred from the resized panel width.
    int slide_pixels=0;
    bool slide_closing=false;
    // Semantic owner values only: no animation ticks, draw serials or pointers.
    std::vector<std::uint64_t> activity_values;
    bool activity_available=false;
};
struct HudLayoutFrame {
    widescreen::Layout layout{};
    bool active=false;
    std::array<HudNativePanel,10> panels{};
};
class HudLayoutContext {
public:
    void publish(HudLayoutFrame&& frame) noexcept;
    void retire() noexcept;
    // Canvas lifetime is independent of the sole-mission HUD graph (menus and
    // modal screens also submit native UI in the host canvas).
    void publish_canvas(unsigned width,unsigned height) noexcept;
    void set_ui_scale(int percent) noexcept;
    void set_hud_width(int value) noexcept;
    void set_hud_panels(const std::array<HudPanelSettings,7>& settings) noexcept;
    void set_message_panel(const MessagePanelSettings& settings) noexcept;
    bool message_text_rect(RECT& placed,float& text_scale) noexcept;
    const widescreen::Layout& layout() const noexcept { return g_layout; }
    bool editing=false;
    std::array<HudPanelSettings,7> g_hud_panels{};
    MessagePanelSettings g_message_panel{};
    std::array<float,2> g_corner_scales{{100.f,100.f}};
    enum class DockSide { left,right,top,bottom,floating };
    struct HeaderLayout { HudPanelBounds panel,body,background; float left,top,width,height,scale; DockSide side; };
    float edge_slack() noexcept;
    DockSide stock_header_side(HudPanel panel) noexcept;
    int button_side(HudPanel panel) noexcept;
    float border_padding(HudPanel panel) noexcept;
    float border_inset(HudPanel panel) noexcept;
    float button_multiplier(HudPanel panel) noexcept;
    DockSide panel_header_side(unsigned index) noexcept;
    bool slot_panel(unsigned panel) noexcept;
    unsigned layout_slot_count(unsigned panel,const HudNativePanel* object) noexcept;
    int slot_direction(unsigned panel) noexcept;
    unsigned slots_per_line(unsigned panel) noexcept;
    bool lines_grow_negative(unsigned panel) noexcept;
    void slot_extent(unsigned panel,const HudNativePanel* object,float& width,float& height) noexcept;
    RECT panel_native_rect(unsigned index,const HudNativePanel* object) noexcept;
    bool horizontal_bar(unsigned index) noexcept;
    void panel_native_size(unsigned,const LONG* r,float& width,float& height) noexcept;
    widescreen::HudComposition panel_composition(unsigned index,const HudNativePanel* object) noexcept;
    HeaderLayout header_layout(HudPanel panel) noexcept;
    void header_control_target(HudPanel panel,unsigned area,const HeaderLayout& header,float& x,float& y) noexcept;
    MessagePanelBounds message_panel_bounds() noexcept;
    bool slot_screen_bounds(unsigned panel,unsigned slot,const HudNativePanel* object,HudPanelBounds& out) noexcept;
    bool widescreen_hud_header_bounds(HudPanel panel,HudPanelBounds& bounds) noexcept;
    bool widescreen_hud_panel_bounds(HudPanel panel,HudPanelBounds& bounds) noexcept;
    void widescreen_hud_position_percent(HudPanel panel,float left,float top,float width,float height,float& x,float& y) noexcept;
    bool portrait_slot_composition(unsigned slot,const HudNativePanel* object,widescreen::HudComposition& h) noexcept;
    bool widescreen_hud_asset_frame(HudPanel panel,HudAssetFrame& frame) noexcept;
    void widescreen_hud_asset_frames(std::vector<HudAssetFrame>& frames);
    void set_hud_fades(const std::array<HudFadeSettings,10>& settings,const std::array<float,2>& corner_scales,HudFadeTiming timing,std::uint64_t now) noexcept;
    void update_hud_fades(float mouse_x,float mouse_y,std::uint64_t now) noexcept;
    bool hud_hovered(HudPanel panel) const noexcept;
    float hud_opacity(HudPanel panel) const noexcept;
    bool hud_fade_enabled(HudPanel panel) const noexcept;
    void hud_activity(HudPanel panel,std::uint64_t now) noexcept;
    void hud_content_values(HudPanel panel,const std::vector<std::uint64_t>& values,std::uint64_t now) noexcept;
    void widescreen_hud_preview_slots(HudPanel panel,std::vector<HudPreviewSlot>& slots);
    // Shared by synchronous drawing and area membership. Slot/area are native
    // ordinals, never resource addresses. False means absent or clipped out.
    bool slot_composition(HudPanel panel,unsigned slot,widescreen::HudComposition& out) noexcept;
    bool body_composition(HudPanel panel,widescreen::HudComposition& out,bool background=false) noexcept;
    bool control_composition(HudPanel panel,unsigned area,widescreen::HudComposition& out,bool arrow=false) noexcept;
    // Native figure ordinal plus host-resolved slot (item substitution/camera).
    // False suppresses hidden or renderer-replaced art; no object addresses.
    bool figure_composition(HudPanel panel,unsigned figure,int slot,widescreen::HudComposition& out) noexcept;
    // Surfaces use the same body/slot mapping as figures. Message text has
    // its own pixel origin and scale, but clips to the unscaled text viewport.
    bool surface_composition(HudPanel panel,int slot,widescreen::HudComposition& out) noexcept;
    bool message_text_composition(const RECT& pixels,const RECT& placed,widescreen::HudComposition& out) noexcept;
    // Pixel input -> native placed coordinates for the original polygon test.
    // A rejected/hidden/preview area leaves the output untouched.
    bool area_point(HudPanel panel,unsigned area,POINT pixel,POINT& placed) noexcept;
    // Native-size tooltip follows the moved clock, without inheriting its scale.
    bool clock_tooltip_composition(const RECT& surface,widescreen::HudComposition& out) noexcept;
    bool inverse_composed_point(POINT pixel,const widescreen::HudComposition& composition,POINT& placed) const noexcept;
private:
    void clip_to_panel(widescreen::HudComposition& h,const HeaderLayout& header,bool frame=false) noexcept;
    void slide_delta(const HeaderLayout& header,float fraction,float& x,float& y,bool inset_header=false) noexcept;
    widescreen::Layout g_layout{};
    int ui_scale_percent_=0; // Preserve the published scale until configured.
    widescreen::HudWidth hud_width_=widescreen::HudWidth::full;
    HudLayoutFrame frame_{};
    struct FadeState {
        std::uint64_t active_at=0,transition_at=0;
        float opacity=1.f,start_opacity=1.f,target_opacity=1.f;
        bool hovered=false;
        std::vector<std::uint64_t> native_values,content_values;
    };
    std::array<FadeState,10> fades_{};
    std::array<HudFadeSettings,10> fade_settings_{};
    HudFadeTiming fade_timing_{};
    bool fade_lifetime_=false;
    void reset_fades(std::uint64_t now) noexcept;
    void advance_fade(FadeState& state,std::uint64_t now) noexcept;
    void set_fade_target(FadeState& state,float target,std::uint64_t now) noexcept;
    const HudNativePanel* current_panel(unsigned index) const noexcept;
    bool current_message_panel(const HudNativePanel*& output,const HudNativePanel*& text) const noexcept;
    unsigned native_slot_count(unsigned panel,const HudNativePanel* object) const noexcept;
    bool active_slot(unsigned panel,unsigned slot,const HudNativePanel* object) const noexcept;
    void slot_dimensions(unsigned panel,unsigned slot,const HudNativePanel* object,float& width,float& height) const noexcept;
    RECT panel_source_rect(unsigned index,const HudNativePanel* object) const noexcept;
    float slide_fraction(HudPanel panel,const HudNativePanel* object) const noexcept;
};
// Used only by the embedded frontend. The context itself is platform-independent
// apart from the RECT value type and is usable by either frontend or a headless test.
HudLayoutContext& embedded_hud_layout() noexcept;
}
