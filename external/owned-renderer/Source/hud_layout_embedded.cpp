#include "hud_layout_context.h"

namespace owned {
HudLayoutContext& embedded_hud_layout() noexcept {
    static HudLayoutContext context;
    return context;
}
void widescreen_set_ui_scale(int percent) noexcept {embedded_hud_layout().set_ui_scale(percent);}
void widescreen_set_hud_panels(const std::array<HudPanelSettings,7>& settings) noexcept {
    embedded_hud_layout().set_hud_panels(settings);
}
void widescreen_set_message_panel(const MessagePanelSettings& settings) noexcept {
    embedded_hud_layout().set_message_panel(settings);
}
const widescreen::Layout& widescreen_layout() noexcept {
    return embedded_hud_layout().layout();
}
bool widescreen_hud_panel_bounds(HudPanel panel,HudPanelBounds& bounds) noexcept {
    return embedded_hud_layout().widescreen_hud_panel_bounds(panel,bounds);
}
bool widescreen_hud_header_bounds(HudPanel panel,HudPanelBounds& bounds) noexcept {
    return embedded_hud_layout().widescreen_hud_header_bounds(panel,bounds);
}
void widescreen_hud_position_percent(HudPanel panel,float left,float top,float width,float height,float& x,float& y) noexcept {
    embedded_hud_layout().widescreen_hud_position_percent(panel,left,top,width,height,x,y);
}
void widescreen_hud_preview_slots(HudPanel panel,std::vector<HudPreviewSlot>& slots) {
    embedded_hud_layout().widescreen_hud_preview_slots(panel,slots);
}
bool widescreen_hud_asset_frame(HudPanel panel,HudAssetFrame& frame) noexcept {
    return embedded_hud_layout().widescreen_hud_asset_frame(panel,frame);
}
void widescreen_hud_asset_frames(std::vector<HudAssetFrame>& frames) {
    embedded_hud_layout().widescreen_hud_asset_frames(frames);
}
void widescreen_set_hud_fades(const std::array<HudFadeSettings,10>& settings,const std::array<float,2>& corner_scales,HudFadeTiming timing) noexcept {
    embedded_hud_layout().set_hud_fades(settings,corner_scales,timing,GetTickCount64());
}
void widescreen_update_hud_fades(float mouse_x,float mouse_y) noexcept {
    embedded_hud_layout().update_hud_fades(mouse_x,mouse_y,GetTickCount64());
}
bool widescreen_hud_hovered(HudPanel panel) noexcept {return embedded_hud_layout().hud_hovered(panel);}
bool widescreen_hud_fade_enabled(HudPanel panel) noexcept {return embedded_hud_layout().hud_fade_enabled(panel);}
float widescreen_hud_opacity(HudPanel panel) noexcept {return embedded_hud_layout().hud_opacity(panel);}
void widescreen_hud_activity(HudPanel panel) noexcept {embedded_hud_layout().hud_activity(panel,GetTickCount64());}
void widescreen_hud_content_values(HudPanel panel,const std::vector<std::uint64_t>& values) noexcept {
    embedded_hud_layout().hud_content_values(panel,values,GetTickCount64());
}
}
