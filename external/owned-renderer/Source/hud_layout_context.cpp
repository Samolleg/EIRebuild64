#include "hud_layout_context.h"
#include <cmath>
#include <climits>
#include <utility>

namespace owned {
using widescreen::Anchor;
namespace {
struct PanelRole { widescreen::Anchors anchor; };
constexpr std::array<PanelRole,kHudPanels.size()> kPanelRoles{{
    {{Anchor::end,Anchor::end}}, {{Anchor::start,Anchor::end}},
    {{Anchor::end,Anchor::start}}, {{Anchor::start,Anchor::start}},
    {{Anchor::centre,Anchor::end}}, {{Anchor::start,Anchor::end}},
    {{Anchor::end,Anchor::end}}, {{}}, {{Anchor::end,Anchor::end}},
    {{Anchor::start,Anchor::end}}, {{Anchor::end,Anchor::end}}
}};
}
void HudLayoutContext::publish(HudLayoutFrame&& frame) noexcept {
    if(frame.active && !frame_.active)fade_lifetime_=false;
    g_layout=widescreen::make_layout(frame.layout.width,frame.layout.height,hud_width_,
        ui_scale_percent_?ui_scale_percent_/100.f:frame.layout.scale);
    frame_=std::move(frame);
    if(!frame_.active){editing=false;fades_={};fade_lifetime_=false;}
}
void HudLayoutContext::retire() noexcept { frame_=HudLayoutFrame{};g_layout=widescreen::make_layout(800.f,600.f,hud_width_,ui_scale_percent_?ui_scale_percent_/100.f:1.f);editing=false;fades_={};fade_lifetime_=false; }
void HudLayoutContext::publish_canvas(unsigned width,unsigned height) noexcept {
    g_layout=widescreen::make_layout(float(width),float(height),hud_width_,g_layout.scale);
}
const HudNativePanel* HudLayoutContext::current_panel(unsigned index) const noexcept {
    // The quest widget uses the items panel's published areas 4-7.
    if(index==unsigned(HudPanel::quest_items))index=unsigned(HudPanel::items);
    return frame_.active && index<frame_.panels.size() && frame_.panels[index].visible ? &frame_.panels[index] : nullptr;
}
bool HudLayoutContext::current_message_panel(const HudNativePanel*& output,const HudNativePanel*& text) const noexcept {
    output=text=current_panel(unsigned(HudPanel::messages));return output!=nullptr;
}
unsigned HudLayoutContext::native_slot_count(unsigned panel,const HudNativePanel* object) const noexcept {
    if(panel!=unsigned(HudPanel::items))return object->native_count;
    unsigned count=0;
    for(unsigned i=0;i<(std::min)(unsigned(object->slots.size()),4u);++i)
        if(object->slots[i].active)count=i+1;
    return count;
}
bool HudLayoutContext::active_slot(unsigned panel,unsigned slot,const HudNativePanel* object) const noexcept {
    return slot<object->slots.size() && object->slots[slot].active &&
        (panel==unsigned(HudPanel::items)?slot<8u:slot<object->native_count);
}
void HudLayoutContext::slot_dimensions(unsigned panel,unsigned slot,const HudNativePanel* object,float& width,float& height) const noexcept {
    if(panel==5){width=40.f;height=80.f;return;}
    if(!object->slots.empty()) {
        const auto& r=object->slots[slot<object->slots.size()?slot:0].rect;
        width=float(r.right-r.left);height=float(r.bottom-r.top);return;
    }
    width=panel==4?56.f:panel==6?50.f:40.f;height=panel==4?90.f:panel==6?50.f:40.f;
}
RECT HudLayoutContext::panel_source_rect(unsigned index,const HudNativePanel* object) const noexcept {
    RECT r=object->rect;
    if(index==4 && !object->slots.empty()) {
        r=object->slots[0].rect;
        for(const auto& slot:object->slots) {
            r.left=(std::min)(r.left,slot.rect.left);r.top=(std::min)(r.top,slot.rect.top);
            r.right=(std::max)(r.right,slot.rect.right);r.bottom=(std::max)(r.bottom,slot.rect.bottom);
        }
    }
    return r;
}
float HudLayoutContext::slide_fraction(HudPanel,const HudNativePanel* object) const noexcept {return object->slide;}

void HudLayoutContext::clip_to_panel(widescreen::HudComposition& h,const HeaderLayout& header,bool frame) noexcept {
    const auto b=header.panel;const float margin=frame?5.f*g_layout.unit*h.scale:0.f;
    h.clip_native_screen=true;
    h.clip_left=(std::max)(0.f,b.left-margin);h.clip_top=(std::max)(0.f,b.top-margin);
    h.clip_right=(std::min)(g_layout.width,b.left+b.width+margin);h.clip_bottom=(std::min)(g_layout.height,b.top+b.height+margin);
    if(frame) {
        if(b.left<=edge_slack())h.clip_left=5.f*g_layout.unit*h.scale;
        if(b.top<=edge_slack())h.clip_top=5.f*g_layout.unit*h.scale;
        if(b.left+b.width>=g_layout.width-edge_slack())h.clip_right=g_layout.width-5.f*g_layout.unit*h.scale;
        if(b.top+b.height>=g_layout.height-edge_slack())h.clip_bottom=g_layout.height-5.f*g_layout.unit*h.scale;
    } else {
        // The background reaches the frame; only the content is inset (#186).
        h.clip_left=(std::max)(0.f,header.background.left);h.clip_top=(std::max)(0.f,header.background.top);
        h.clip_right=(std::min)(g_layout.width,header.background.left+header.background.width);
        h.clip_bottom=(std::min)(g_layout.height,header.background.top+header.background.height);
    }
}
void HudLayoutContext::slide_delta(const HeaderLayout& header,float fraction,float& x,float& y,bool inset_header) noexcept {
    x=y=0.f;
    if(header.side==DockSide::left)x=-fraction*(header.panel.width-header.width);
    else if(header.side==DockSide::right)x=fraction*(header.panel.width-header.width);
    else y=(header.side==DockSide::bottom?1.f:-1.f)*fraction*(header.panel.height-
        (inset_header && (header.side==DockSide::top || header.side==DockSide::bottom)?header.height:0.f));
}
bool HudLayoutContext::slot_composition(HudPanel panel,unsigned slot,widescreen::HudComposition& out) noexcept {
    out={};const unsigned index=unsigned(panel);const auto* object=current_panel(index);
    if(!object || !slot_panel(index) || slot>=object->slots.size() || !active_slot(index,slot,object))return false;
    auto h=panel_composition(index,object);
    if(index==4) {
        if(!portrait_slot_composition(slot,object,h))return false;
    } else {
        const auto& area=object->slots[slot];const auto& r=area.rect;
        const float x=widescreen::placed_to_screen_x(g_layout,.5f*float(r.left+r.right)+float(area.shift.x));
        const float y=widescreen::placed_to_screen_y(g_layout,(index==5?float(r.bottom)-40.f:.5f*float(r.top+r.bottom))+float(area.shift.y));
        HudPanelBounds b;if(!slot_screen_bounds(index,slot,object,b))return false;
        if(panel==HudPanel::items && slot>=4) {
            h.scale=h.scale_y=g_layout.scale*g_quest_items.scale_percent/100.f;
            h.opacity=hud_opacity(HudPanel::quest_items);
        }
        h.offset_x+=b.left+b.width*.5f-widescreen::compose_x(x,h);
        h.offset_y+=b.top+b.height*.5f-widescreen::compose_y(y,h);
    }
    out=h;return true;
}
bool HudLayoutContext::body_composition(HudPanel panel,widescreen::HudComposition& out,bool background) noexcept {
    out={};const unsigned index=unsigned(panel);const auto* object=current_panel(index);
    if(!object)return false;
    if(panel!=HudPanel::messages && panel!=HudPanel::minimap && panel!=HudPanel::character_info) {
        out=panel_composition(index,object);return true;
    }
    auto header=header_layout(panel);if(background)header.body=header.background;
    widescreen::HudComposition h;
    if(panel==HudPanel::messages) {
        const auto b=message_panel_bounds();
        const float left=widescreen::placed_to_screen_x(g_layout,180.f),top=widescreen::placed_to_screen_y(g_layout,0.f);
        h={g_layout.scale,left,top,g_layout.scale,b.left-left,b.top-top};
        h.opacity=hud_opacity(panel);
    } else {
        h=panel_composition(index,object);
        const float cx=panel==HudPanel::minimap?700.f:100.f,cy=panel==HudPanel::minimap?80.f:110.f;
        const auto anchor=kPanelRoles[index].anchor;
        const float x=widescreen::placed_to_screen_x(g_layout,widescreen::layout_to_placed_x(g_layout,anchor,cx,false));
        const float y=widescreen::placed_to_screen_y(g_layout,widescreen::layout_to_placed_y(g_layout,anchor,cy,false));
        h.offset_x+=header.body.left+header.body.width*.5f-widescreen::compose_x(x,h);
        h.offset_y+=header.body.top+header.body.height*.5f-widescreen::compose_y(y,h);
        const float native=float(object->slide_pixels)*g_layout.unit*h.scale;
        h.offset_x-=panel==HudPanel::minimap?native:-native;
    }
    float x,y;slide_delta(header,object->slide,x,y,panel==HudPanel::messages);h.offset_x+=x;h.offset_y+=y;
    clip_to_panel(h,header);
    if(object->slide>=1.f)h.clip_right=h.clip_left;
    out=h;return true;
}
bool HudLayoutContext::control_composition(HudPanel panel,unsigned area,widescreen::HudComposition& out,bool arrow) noexcept {
    out={};const unsigned index=unsigned(panel);const auto* object=current_panel(index);
    if(!object || area>=object->slots.size() || (area!=0 && object->slide>=1.f))return false;
    if(panel==HudPanel::minimap) {if(area!=0 && area!=3 && area!=4 && area!=5)return false;}
    else if(panel==HudPanel::character_info) {if(area>=5)return false;}
    else if(panel==HudPanel::messages) {if(area>=3)return false;}
    else return false;
    widescreen::HudComposition h;
    if(panel==HudPanel::messages) {
        const auto b=message_panel_bounds();
        const float left=widescreen::placed_to_screen_x(g_layout,180.f),top=widescreen::placed_to_screen_y(g_layout,0.f);
        h={g_layout.scale,left,top,g_layout.scale,b.left-left,b.top-top};h.opacity=hud_opacity(panel);
    } else h=panel_composition(index,object);
    const auto& a=object->slots[area];const auto& r=a.rect;
    const float x=widescreen::placed_to_screen_x(g_layout,.5f*float(r.left+r.right)+float(a.shift.x));
    const float y=widescreen::placed_to_screen_y(g_layout,.5f*float(r.top+r.bottom)+float(a.shift.y));
    const auto header=header_layout(panel);h.scale=h.scale_y=header.scale;
    float tx,ty;header_control_target(panel,area,header,tx,ty);
    h.offset_x+=tx-widescreen::compose_x(x,h);h.offset_y+=ty-widescreen::compose_y(y,h);
    h.clip_native_screen=false;
    if(arrow && area==0) {
        const bool closing=object->slide_closing;
        const int outward=header.side==DockSide::left?1:header.side==DockSide::right?3:header.side==DockSide::bottom?0:2;
        const int native=panel==HudPanel::minimap?(closing?1:3):panel==HudPanel::character_info?(closing?3:1):(closing?0:2);
        h.rotation_quarters=(outward+(closing?2:0)-native+4)%4;
        h.rotation_x=tx;h.rotation_y=ty;
    }
    out=h;return true;
}
bool HudLayoutContext::figure_composition(HudPanel panel,unsigned figure,int slot,widescreen::HudComposition& out) noexcept {
    out={};const unsigned index=unsigned(panel);const auto* object=current_panel(index);
    if(!object)return false;
    if(slot_panel(index)) {
        // Selection perimeters are emitted by widescreen_hud_asset_frames.
        if(panel==HudPanel::portraits && figure>=4 && (figure-4)%5==0)return false;
        return slot>=0 && slot_composition(panel,unsigned(slot),out);
    }
    const bool map=panel==HudPanel::minimap,info=panel==HudPanel::character_info;
    const bool message=panel==HudPanel::messages;
    if(!map && !info && !message)return body_composition(panel,out);
    const unsigned frame=map?9:info?7:5,arrow=map?5:2,title=map?4:1;
    if(figure==frame || (info && figure==8) || (figure!=arrow && object->slide>=1.f))return false;
    int area=-1;
    if(figure==arrow)area=0;
    else if(map) {if(figure==6)area=3;else if(figure==7)area=4;else if(figure==11)area=5;}
    else if(figure>=3 && figure<=unsigned(info?6:4))area=int(figure)-2;
    if(area>=0)return control_composition(panel,unsigned(area),out,figure==arrow);
    if(figure!=title) {
        const bool background=figure==unsigned(map?3:0);
        if(!body_composition(panel,out,background))return false;
        if(!background)return true;
        // The embedded host keeps the native plain quad. Fit that quad in the
        // renderer rather than rebuilding host geometry when docking changes.
        const auto header=header_layout(panel);
        float dx,dy;slide_delta(header,object->slide,dx,dy,message);
        const auto geometry=message_geometry(header,g_layout.unit*g_layout.scale,g_layout.unit);
        const float native_x=message?geometry.background_x:map?700.f+object->slide_pixels:100.f-object->slide_pixels;
        const float native_y=message?geometry.background_y:map?80.f:110.f;
        const auto anchor=kPanelRoles[index].anchor;
        const float x=widescreen::placed_to_screen_x(g_layout,widescreen::layout_to_placed_x(g_layout,anchor,native_x,false));
        const float y=widescreen::placed_to_screen_y(g_layout,widescreen::layout_to_placed_y(g_layout,anchor,native_y,false));
        out.scale=header.background.width/(g_layout.unit*(message?geometry.background.right-geometry.background.left:160.f));
        out.scale_y=header.background.height/(g_layout.unit*(message?geometry.background.bottom-geometry.background.top:map?160.f:220.f));
        out.pivot_x=x;out.pivot_y=y;
        out.offset_x=header.background.left+header.background.width*.5f+dx-x;
        out.offset_y=header.background.top+header.background.height*.5f+dy-y;
        return true;
    }
    const auto header=header_layout(panel);
    // Docking changes both dimensions of the native toolbar quad. Resize the
    // renderer composition, keeping the host's source geometry unchanged.
    widescreen::HudComposition h;
    if(message) {
        const auto b=message_panel_bounds();
        const float left=widescreen::placed_to_screen_x(g_layout,180.f),top=widescreen::placed_to_screen_y(g_layout,0.f);
        h={g_layout.scale,left,top,g_layout.scale,b.left-left,b.top-top};h.opacity=hud_opacity(panel);
    } else h=panel_composition(index,object);
    h.scale=h.scale_y=header.scale;
    float x,y;
    if(message) {
        x=widescreen::placed_to_screen_x(g_layout,message_geometry(header,g_layout.unit*g_layout.scale,g_layout.unit).centre_x);
        y=widescreen::placed_to_screen_y(g_layout,10.f);
    } else {
        const unsigned titleArea=map?6:5;
        if(titleArea>=object->slots.size())return false;
        const auto anchor=kPanelRoles[index].anchor;
        x=widescreen::placed_to_screen_x(g_layout,widescreen::layout_to_placed_x(g_layout,anchor,
            (map?790.f:10.f)+float(object->slots[titleArea].shift.x),false));
        y=widescreen::placed_to_screen_y(g_layout,widescreen::layout_to_placed_y(g_layout,anchor,map?80.f:110.f,false));
    }
    h.scale=message?header.scale:header.width/(g_layout.unit*20.f);
    h.scale_y=message?header.scale:header.height/(g_layout.unit*(map?160.f:220.f));
    h.pivot_x=x;h.pivot_y=y;
    h.offset_x=header.left+header.width*.5f-x;
    h.offset_y=header.top+header.height*.5f-y;
    out=h;return true;
}
bool HudLayoutContext::surface_composition(HudPanel panel,int slot,widescreen::HudComposition& out) noexcept {
    out={};const auto* object=current_panel(unsigned(panel));
    if(!object)return false;
    if(slot_panel(unsigned(panel)))return slot>=0 && slot_composition(panel,unsigned(slot),out);
    if((panel==HudPanel::minimap || panel==HudPanel::character_info || panel==HudPanel::messages) && object->slide>=1.f)return false;
    return body_composition(panel,out);
}
bool HudLayoutContext::message_text_composition(const RECT& pixels,const RECT& placed,widescreen::HudComposition& out) noexcept {
    out={};widescreen::HudComposition body;
    if(pixels.right<=pixels.left || pixels.bottom<=pixels.top || placed.right-placed.left<=14 ||
       placed.bottom<=placed.top || !surface_composition(HudPanel::messages,-1,body))return false;
    auto h=body;
    const float x=float(pixels.left),y=float(pixels.top);
    const float target_x=widescreen::compose_x(x,body),target_y=widescreen::compose_y(y,body);
    h.scale*=g_message_panel.text_scale_percent/100.f;h.scale_y=h.scale;
    h.pivot_x=x;h.pivot_y=y;h.offset_x=target_x-x;h.offset_y=target_y-y;
    h.clip_native_screen=true;
    h.clip_left=(std::max)(h.clip_left,widescreen::compose_x(widescreen::placed_to_screen_x(g_layout,float(placed.left)),body));
    h.clip_right=(std::min)(h.clip_right,widescreen::compose_x(widescreen::placed_to_screen_x(g_layout,float(placed.right-14)),body));
    h.clip_top=(std::max)(h.clip_top,widescreen::compose_y(widescreen::placed_to_screen_y(g_layout,float(placed.top)),body));
    h.clip_bottom=(std::min)(h.clip_bottom,widescreen::compose_y(widescreen::placed_to_screen_y(g_layout,float(placed.bottom)),body));
    if(h.clip_right<=h.clip_left || h.clip_bottom<=h.clip_top)return false;
    out=h;return true;
}
bool HudLayoutContext::clock_tooltip_composition(const RECT& surface,widescreen::HudComposition& out) noexcept {
    HudPanelBounds clock;
    if(surface.right<=surface.left || surface.bottom<=surface.top ||
       !widescreen_hud_panel_bounds(HudPanel::game_speed,clock))return false;
    const float width=float(surface.right)-float(surface.left);
    const float height=float(surface.bottom)-float(surface.top);
    const float left=std::clamp(clock.left+clock.width-width,0.f,(std::max)(0.f,g_layout.width-width));
    const float top=std::clamp(clock.top-height-4.f*g_layout.unit,0.f,(std::max)(0.f,g_layout.height-height));
    out={1.f,float(surface.left),float(surface.top),1.f,left-float(surface.left),top-float(surface.top)};
    return true;
}
bool HudLayoutContext::area_point(HudPanel panel,unsigned area,POINT pixel,POINT& placed) noexcept {
    const unsigned index=unsigned(panel);const auto* object=current_panel(index);
    if(!object || area>=object->slots.size())return false;
    widescreen::HudComposition h;
    if(slot_panel(index)) {
        if(!slot_composition(panel,area,h))return false;
        // Weapon hit polygons are taller than their displayed 40x80 cells.
        // Restrict membership to the same visible slot, including reordered bars.
        HudPanelBounds b;
        if(!slot_screen_bounds(index,area,object,b) || pixel.x<b.left || pixel.y<b.top ||
            pixel.x>=b.left+b.width || pixel.y>=b.top+b.height)return false;
    } else if(panel==HudPanel::minimap || panel==HudPanel::character_info || panel==HudPanel::messages) {
        if(area && object->slide>=1.f)return false;
        if(!control_composition(panel,area,h)) {
            if(!body_composition(panel,h))return false;
        }
    } else if(!body_composition(panel,h))return false;
    return inverse_composed_point(pixel,h,placed);
}
bool HudLayoutContext::inverse_composed_point(POINT pixel,const widescreen::HudComposition& h,POINT& placed) const noexcept {
    if(!std::isfinite(h.scale) || !std::isfinite(h.scale_y) || h.scale<=0.f || h.scale_y<=0.f)return false;
    float x=float(pixel.x),y=float(pixel.y);
    if(h.clip_native_screen) {
        const float left=h.clip_right>=0.f?h.clip_left:widescreen::compose_x(0.f,h);
        const float top=h.clip_right>=0.f?h.clip_top:widescreen::compose_y(0.f,h);
        const float right=h.clip_right>=0.f?h.clip_right:widescreen::compose_x(g_layout.width,h);
        const float bottom=h.clip_right>=0.f?h.clip_bottom:widescreen::compose_y(g_layout.height,h);
        if(x<left || y<top || x>=right || y>=bottom)return false;
    }
    const float dx=x-h.rotation_x,dy=y-h.rotation_y;
    switch((h.rotation_quarters%4+4)%4) {
    case 1:x=h.rotation_x+dy;y=h.rotation_y-dx;break;
    case 2:x=h.rotation_x-dx;y=h.rotation_y-dy;break;
    case 3:x=h.rotation_x-dy;y=h.rotation_y+dx;break;
    }
    x=h.pivot_x+(x-h.offset_x-h.pivot_x)/h.scale;
    y=h.pivot_y+(y-h.offset_y-h.pivot_y)/h.scale_y;
    x=std::floor(widescreen::screen_to_placed_x(g_layout,x));y=std::floor(widescreen::screen_to_placed_y(g_layout,y));
    if(!std::isfinite(x) || !std::isfinite(y) || double(x)<LONG_MIN || double(x)>LONG_MAX || double(y)<LONG_MIN || double(y)>LONG_MAX)return false;
    placed={LONG(x),LONG(y)};return true;
}

// Geometry below follows c56 widescreen.cpp. Only the native-data readers above
// differ: the host publishes typed values instead of exposing its memory layout.
float HudLayoutContext::edge_slack() noexcept { return (std::max)(.5f,g_layout.unit); }
HudLayoutContext::DockSide HudLayoutContext::stock_header_side(HudPanel panel) noexcept {
    return panel==HudPanel::minimap?DockSide::right:panel==HudPanel::character_info?DockSide::left:DockSide::top;
}
int HudLayoutContext::button_side(HudPanel panel) noexcept {return panel==HudPanel::messages?g_message_panel.buttons_side:g_hud_panels[unsigned(panel)].buttons_side;}
float HudLayoutContext::border_padding(HudPanel panel) noexcept {
    return (panel==HudPanel::messages?g_message_panel.border_padding:g_hud_panels[unsigned(panel)].border_padding)*g_layout.unit*g_layout.scale;
}
float HudLayoutContext::border_inset(HudPanel panel) noexcept {return (std::max)(0.f,border_padding(panel));}
float HudLayoutContext::button_multiplier(HudPanel panel) noexcept {return (panel==HudPanel::messages?g_message_panel.buttons_scale_percent:g_hud_panels[unsigned(panel)].buttons_scale_percent)/100.f;}
HudLayoutContext::DockSide HudLayoutContext::panel_header_side(unsigned index) noexcept {
    const auto panel=HudPanel(index);const auto& s=g_hud_panels[index];const int side=s.buttons_side;
    if(side==1 || (side==0 && s.x_percent<0.f && s.y_percent<0.f))return stock_header_side(panel);
    if(side>=2)return DockSide(side-2);
    if(s.x_percent<0.f)return stock_header_side(panel);
    if(s.x_percent<=.01f)return DockSide::left;
    if(s.x_percent>=99.99f)return DockSide::right;
    if(s.y_percent>=99.99f)return DockSide::bottom;
    if(s.y_percent<=.01f)return DockSide::top;
    return DockSide::floating;
}
bool HudLayoutContext::slot_panel(unsigned panel) noexcept {return panel<2 || panel==4 || panel==5 || panel==6;}
unsigned HudLayoutContext::layout_slot_count(unsigned panel,const HudNativePanel* object) noexcept {
    const auto& s=g_hud_panels[panel];const unsigned count=native_slot_count(panel,object);
    return s.preview && editing?(std::max)(count,panel==6?(std::min)(s.preview_count,4u):s.preview_count):count;
}
int HudLayoutContext::slot_direction(unsigned panel) noexcept {
    const int d=g_hud_panels[panel].direction;
    return panel!=4?d:d==1?1:d==2?0:d==4?3:2;
}
unsigned HudLayoutContext::slots_per_line(unsigned panel) noexcept {return panel==4?0u:g_hud_panels[panel].slots_per_line;}
bool HudLayoutContext::lines_grow_negative(unsigned panel) noexcept {
    const auto anchor=kPanelRoles[panel].anchor;
    return slot_direction(panel)>=2?anchor.y==Anchor::end:anchor.x==Anchor::end;
}
void HudLayoutContext::slot_extent(unsigned panel,const HudNativePanel* object,float& width,float& height) noexcept {
    const unsigned count=layout_slot_count(panel,object);width=height=0.f;
    const bool horizontal=slot_direction(panel)>=2;const unsigned per=slots_per_line(panel);
    float length=0.f,line=0.f,cross=0.f;unsigned lines=count?1u:0u;
    for(unsigned i=0;i<count;++i) {float w,h;slot_dimensions(panel,i,object,w,h);
        if(per && i && i%per==0){length=(std::max)(length,line);line=0.f;++lines;}
        const float size=horizontal?w:h;
        // Signed spacing may overlap slots, but never reverse their order.
        const bool line_end=i+1==count || (per && (i+1)%per==0);
        line+=line_end?size:(std::max)(1.f,size+g_hud_panels[panel].padding);
        cross=(std::max)(cross,horizontal?h:w);
    }
    length=(std::max)(length,line);
    const float across=lines?cross+float(lines-1)*(std::max)(1.f,cross+g_hud_panels[panel].padding):0.f;
    if(horizontal){width=length;height=across;}else{height=length;width=across;}
}
RECT HudLayoutContext::panel_native_rect(unsigned index,const HudNativePanel* object) noexcept {
    RECT r=panel_source_rect(index,object);
    if(index==unsigned(HudPanel::minimap) || index==unsigned(HudPanel::character_info)) {
        const auto side=panel_header_side(index);
        const float buttons=button_multiplier(HudPanel(index));const bool vertical=side==DockSide::left || side==DockSide::right;
        const float count=index==unsigned(HudPanel::minimap)?4.f:5.f;
        const LONG content_height=index==unsigned(HudPanel::minimap)?165:225;
        const float gap=(std::max)(0.f,g_hud_panels[index].border_padding)/(g_hud_panels[index].scale_percent/100.f);
        r.right=r.left+(std::max)(1L,std::lround(vertical?165.f+2.f*gap+20.f*buttons:(std::max)(165.f+2.f*gap,count*20.f*buttons)));
        r.bottom=r.top+(std::max)(1L,std::lround(vertical?(std::max)(float(content_height)+2.f*gap,count*30.f*buttons):float(content_height)+2.f*gap));
        return r;
    }
    if(!slot_panel(index))return r;
    float width,height;slot_extent(index,object,width,height);
    if(editing && (width<=0.f || height<=0.f))slot_dimensions(index,0,object,width,height);
    if(width>0.f && height>0.f){r.right=r.left+std::lround(width);r.bottom=r.top+std::lround(height);}
    return r;
}
bool HudLayoutContext::horizontal_bar(unsigned index) noexcept {return slot_panel(index) && slot_direction(index)>=2;}
void HudLayoutContext::panel_native_size(unsigned,const LONG* r,float& width,float& height) noexcept {
    width=float(r[2]-r[0])*g_layout.unit;height=float(r[3]-r[1])*g_layout.unit;
}
widescreen::HudComposition HudLayoutContext::panel_composition(unsigned index,const HudNativePanel* object) noexcept {
    const auto anchor=kPanelRoles[index].anchor;
    if(index>=unsigned(HudPanel::stance)) {
        auto h=widescreen::hud_composition(g_layout,anchor);
        h.scale=h.scale_y=g_layout.scale*g_corner_scales[index-unsigned(HudPanel::stance)]/100.f;
        h.pivot_x=widescreen::placed_to_screen_x(g_layout,widescreen::anchor_pivot_x(g_layout,anchor.x));
        h.pivot_y=widescreen::placed_to_screen_y(g_layout,widescreen::anchor_pivot_y(g_layout,anchor.y));
        h.opacity=hud_opacity(HudPanel(index));return h;
    }
    const auto& s=g_hud_panels[index];
    const RECT native=panel_native_rect(index,object);const auto* r=&native.left;
    const float left=widescreen::placed_to_screen_x(g_layout,float(r[0]));
    const float top=widescreen::placed_to_screen_y(g_layout,float(r[1]));
    const float scale=g_layout.scale*s.scale_percent/100.f;
    const widescreen::HudComposition h{scale,
        widescreen::placed_to_screen_x(g_layout,widescreen::anchor_pivot_x(g_layout,anchor.x)),
        widescreen::placed_to_screen_y(g_layout,widescreen::anchor_pivot_y(g_layout,anchor.y)),scale};
    float width,height;panel_native_size(index,r,width,height);width*=scale;height*=scale;
    const RECT source=panel_source_rect(index,object);
    const float source_width=float(source.right-source.left)*g_layout.unit*scale;
    const float source_height=float(source.bottom-source.top)*g_layout.unit*scale;
    const auto factor=[](Anchor a){return a==Anchor::end?1.f:a==Anchor::centre?.5f:0.f;};
    float ax=factor(anchor.x),ay=factor(anchor.y),cell_width=width,cell_height=height;
    if(slot_panel(index)) {
        const int direction=slot_direction(index);
        if(index==4 && s.direction==0)ax=.5f,ay=1.f;
        else if(direction>=2)ax=direction==2?0.f:1.f;
        else ay=direction==1?0.f:1.f;
        slot_dimensions(index,0,object,cell_width,cell_height);
        cell_width*=g_layout.unit*scale;cell_height*=g_layout.unit*scale;
    }
    const float base_x=widescreen::compose_x(left,h)+source_width*ax;
    const float base_y=widescreen::compose_y(top,h)+source_height*ay;
    const float placed_left=s.x_percent<0.f?base_x-width*ax:
        (std::max)(0.f,g_layout.width-cell_width)*s.x_percent/100.f+cell_width*ax-width*ax;
    float placed_top=s.y_percent<0.f?base_y-height*ay:
        (std::max)(0.f,g_layout.height-cell_height)*s.y_percent/100.f+cell_height*ay-height*ay;
    if((index==unsigned(HudPanel::minimap) || index==unsigned(HudPanel::character_info)) &&
       panel_header_side(index)!=DockSide::left && panel_header_side(index)!=DockSide::right) {
        // A horizontal strip stays outside the complete content rectangle.
        const float header=30.f*g_layout.unit*scale*button_multiplier(HudPanel(index));
        const float maximum=(std::max)(header,g_layout.height-height-header);
        placed_top=std::clamp(placed_top,header,maximum);
    }
    auto composed=widescreen::HudComposition{scale,left,top,scale,placed_left-left,placed_top-top};
    composed.opacity=hud_opacity(HudPanel(index));return composed;
}
HudLayoutContext::HeaderLayout HudLayoutContext::header_layout(HudPanel panel) noexcept {
    HudPanelBounds b{};widescreen_hud_panel_bounds(panel,b);
    const float content_scale=panel==HudPanel::messages?g_layout.scale:g_layout.scale*g_hud_panels[unsigned(panel)].scale_percent/100.f;
    const float scale=content_scale*button_multiplier(panel);
    const float gap=border_inset(panel);
    const float thickness=(std::max)(1.f,(panel==HudPanel::messages?20.f:30.f)*g_layout.unit*scale);
    DockSide side;
    if(panel!=HudPanel::messages)side=panel_header_side(unsigned(panel));
    else {
        const int override_side=button_side(panel);
        side=override_side==1?stock_header_side(panel):override_side>=2?DockSide(override_side-2):
            b.left<=edge_slack()?DockSide::left:b.left+b.width>=g_layout.width-edge_slack()?DockSide::right:
            b.top<=edge_slack()?DockSide::top:b.top+b.height>=g_layout.height-edge_slack()?DockSide::bottom:DockSide::floating;
    }
    float left=b.left,top=b.top,width=b.width,height=thickness;auto body=b;
    if(side==DockSide::left || side==DockSide::right) {
        width=(std::max)(1.f,(panel==HudPanel::messages?30.f:20.f)*g_layout.unit*scale);height=b.height;
        body.width=(std::max)(0.f,b.width-width);
        if(side==DockSide::right)left=b.left+b.width-width;else body.left+=width;
    } else if(panel==HudPanel::messages) {
        if(side==DockSide::bottom){top=b.top+b.height-thickness;body.height-=thickness;}
        else if(side==DockSide::top){body.top+=thickness;body.height-=thickness;}
        else top=b.top-thickness;
    } else if(side==DockSide::bottom)top=b.top+b.height;
    else top=b.top-thickness;
    // Padding reserves space for controls/content, never a hole in the fill.
    const auto background=body;
    body.left+=gap;body.top+=gap;body.width=(std::max)(1.f,body.width-2.f*gap);body.height=(std::max)(1.f,body.height-2.f*gap);
    return {b,body,background,left,top,width,height,scale,side};
}
void HudLayoutContext::header_control_target(HudPanel panel,unsigned area,const HeaderLayout& header,float& x,float& y) noexcept {
    const bool vertical=header.side==DockSide::left || header.side==DockSide::right;
    const float unit=g_layout.unit*header.scale;
    const float cell_w=panel==HudPanel::messages?30.f:20.f,cell_h=panel==HudPanel::messages?20.f:30.f;
    const int slot=panel==HudPanel::messages?(area==0?2:int(area)-1):panel==HudPanel::minimap?(area==0?0:area==5?1:area==3?2:3):int(area);

    x=header.left+(vertical?(header.side==DockSide::left?cell_w*.5f*unit:header.width-cell_w*.5f*unit):(cell_w*.5f+cell_w*slot)*unit);
    y=header.top+(vertical?(cell_h*.5f+cell_h*slot)*unit:header.height*.5f);
    // Preserve stock spacing on its original side. All other orientations pack
    // actual cell widths/heights, so rotating a strip introduces no padding.
    if(header.side==stock_header_side(panel) && button_multiplier(panel)==1.f) {
        if(panel==HudPanel::minimap)y=header.top+(area==0?15.f:area==5?85.f:area==3?115.f:145.f)*unit;
        else if(panel==HudPanel::character_info)y=header.top+(area==0?15.f:85.f+30.f*area)*unit;
        else if(area==0)x=header.left+header.width*.5f;
        else x=header.left+(area==1?20.f:50.f)*unit;
    }
    // Signed gaps may overlap the frame. Keep complete controls on the window,
    // and use these exact centres for drawing and native hit inversion.
    const float half_x=cell_w*.5f*unit,half_y=cell_h*.5f*unit;
    x=std::clamp(x,half_x,(std::max)(half_x,g_layout.width-half_x));
    y=std::clamp(y,half_y,(std::max)(half_y,g_layout.height-half_y));
}
MessagePanelBounds HudLayoutContext::message_panel_bounds() noexcept {
    const float width = (std::min)(g_layout.width, 440.f*g_layout.unit*g_layout.scale*g_message_panel.width_percent/100.f);
    float height = (std::min)(g_layout.height, 100.f*g_layout.unit*g_layout.scale*g_message_panel.height_percent/100.f);
    const float left=std::clamp(g_layout.width*g_message_panel.centre_x_percent/100.f-width*.5f,0.f,g_layout.width-width);
    // Explicit side overrides use the same scaled three-cell footprint as docking.
    const int side=button_side(HudPanel::messages);
    const bool vertical=side==2 || side==3 || (side==0 && (left<=.5f || left+width>=g_layout.width-.5f));
    const float unit=g_layout.unit*g_layout.scale,buttons=button_multiplier(HudPanel::messages);
    if(vertical)height=(std::max)(height,(std::min)(g_layout.height,60.f*unit*buttons));
    // Horizontal controls must leave a positive text/scrollbar aperture at
    // the smallest editor size, including independently enlarged buttons.
    else {
        const float top=std::clamp(g_layout.height*g_message_panel.top_y_percent/100.f,0.f,g_layout.height-height);
        if(side==1 || side==4 || side==5 || (side==0 && (top<=.5f || top+height>=g_layout.height-.5f)))
            height=(std::max)(height,(std::min)(g_layout.height,(30.f+20.f*buttons)*unit));
    }
    return {left,std::clamp(g_layout.height*g_message_panel.top_y_percent/100.f,0.f,g_layout.height-height),width,height};
}
// Candidate-79 quest layout, with typed area publications instead of retail reads.
RECT HudLayoutContext::quest_area_rect(const HudNativePanel* object,unsigned slot) const noexcept {
    const auto& area=object->slots[slot];const auto r=area.rect;
    return {r.left+area.shift.x,r.top+area.shift.y,r.right+area.shift.x,r.bottom+area.shift.y};
}
bool HudLayoutContext::quest_slot_bounds(unsigned slot,const HudNativePanel* object,HudPanelBounds& out) noexcept {
    if(slot<4 || slot>=8 || object->slots.size()<8)return false;
    RECT group=quest_area_rect(object,4);
    for(unsigned i=5;i<8;++i){const RECT a=quest_area_rect(object,i);
        group.left=(std::min)(group.left,a.left);group.top=(std::min)(group.top,a.top);
        group.right=(std::max)(group.right,a.right);group.bottom=(std::max)(group.bottom,a.bottom);}
    const RECT r=quest_area_rect(object,slot);
    const float scale=g_layout.scale*g_quest_items.scale_percent/100.f,unit=g_layout.unit*scale;
    const float width=float(r.right-r.left)*unit,height=float(r.bottom-r.top)*unit;
    float left,top;
    if(g_quest_items.x_percent<0.f || g_quest_items.y_percent<0.f) {
        const auto anchor=kPanelRoles[unsigned(HudPanel::items)].anchor;
        const widescreen::HudComposition h{scale,
            widescreen::placed_to_screen_x(g_layout,widescreen::anchor_pivot_x(g_layout,anchor.x)),
            widescreen::placed_to_screen_y(g_layout,widescreen::anchor_pivot_y(g_layout,anchor.y)),scale};
        left=widescreen::compose_x(widescreen::placed_to_screen_x(g_layout,float(r.left)),h);
        top=widescreen::compose_y(widescreen::placed_to_screen_y(g_layout,float(r.top)),h);
    } else {
        const float group_width=float(group.right-group.left)*unit,group_height=float(group.bottom-group.top)*unit;
        left=(std::max)(0.f,g_layout.width-group_width)*g_quest_items.x_percent/100.f+float(r.left-group.left)*unit;
        top=(std::max)(0.f,g_layout.height-group_height)*g_quest_items.y_percent/100.f+float(r.top-group.top)*unit;
    }
    out={left,top,width,height,width/unit,height/unit};
    return true;
}
bool HudLayoutContext::quest_group_bounds(const HudNativePanel* object,HudPanelBounds& out) noexcept {
    HudPanelBounds b;if(!quest_slot_bounds(4,object,b))return false;
    float right=b.left+b.width,bottom=b.top+b.height;out=b;
    for(unsigned i=5;i<8;++i)if(quest_slot_bounds(i,object,b)){
        out.left=(std::min)(out.left,b.left);out.top=(std::min)(out.top,b.top);
        right=(std::max)(right,b.left+b.width);bottom=(std::max)(bottom,b.top+b.height);}
    const float scale=g_layout.scale*g_quest_items.scale_percent/100.f;
    out.width=right-out.left;out.height=bottom-out.top;out.native_width=out.width/scale;out.native_height=out.height/scale;
    return true;
}
bool HudLayoutContext::slot_screen_bounds(unsigned panel,unsigned slot,const HudNativePanel* object,HudPanelBounds& out) noexcept {
    if(panel==unsigned(HudPanel::items) && slot>=4)return quest_slot_bounds(slot,object,out);
    if(!slot_panel(panel) || slot>=layout_slot_count(panel,object))return false;
    HudPanelBounds b;if(!widescreen_hud_panel_bounds(HudPanel(panel),b))return false;
    const int direction=slot_direction(panel);const bool horizontal=direction>=2;
    const float unit=g_layout.unit*g_layout.scale*g_hud_panels[panel].scale_percent/100.f;
    float width,height;slot_dimensions(panel,slot,object,width,height);width*=unit;height*=unit;
    const unsigned per=slots_per_line(panel),count=layout_slot_count(panel,object);
    const unsigned line=per?slot/per:0u;
    float prefix=0.f;
    for(unsigned i=line*(per?per:0u);i<slot;++i){float w,h;slot_dimensions(panel,i,object,w,h);prefix+=(std::max)(1.f,(horizontal?w:h)+g_hud_panels[panel].padding)*unit;}
    out={b.left+(b.width-width)*.5f,b.top+(b.height-height)*.5f,width,height,width/unit,height/unit};
    if(per && count>per) {
        // Each line is a cell of the widest slot across the bar; line 0 on the anchored side.
        float cross=0.f;
        for(unsigned i=0;i<count;++i){float w,h;slot_dimensions(panel,i,object,w,h);cross=(std::max)(cross,horizontal?h:w);}
        const float cell=cross*unit,step=(std::max)(1.f,cross+g_hud_panels[panel].padding)*unit;
        const float offset=float(line)*step;
        if(horizontal) out.top=lines_grow_negative(panel)?b.top+b.height-cell-offset+(cell-height)*.5f:b.top+offset+(cell-height)*.5f;
        else out.left=lines_grow_negative(panel)?b.left+b.width-cell-offset+(cell-width)*.5f:b.left+offset+(cell-width)*.5f;
    }
    if(direction==0)out.top=b.top+b.height-prefix-height;
    else if(direction==1)out.top=b.top+prefix;
    else if(direction==2)out.left=b.left+prefix;
    else out.left=b.left+b.width-prefix-width;
    return true;
}
bool HudLayoutContext::widescreen_hud_header_bounds(HudPanel panel,HudPanelBounds& bounds) noexcept {
    if(panel!=HudPanel::minimap && panel!=HudPanel::character_info && panel!=HudPanel::messages)return false;
    HudPanelBounds body;if(!widescreen_hud_panel_bounds(panel,body))return false;
    const auto h=header_layout(panel);
    const HudNativePanel* object=current_panel(unsigned(panel));
    if(panel==HudPanel::messages){const HudNativePanel* text=nullptr;if(!current_message_panel(object,text))return false;}
    const bool closed=slide_fraction(panel,object)>=1.f;
    const float unit=g_layout.unit*h.scale,cw=(panel==HudPanel::messages?30.f:20.f)*unit,ch=(panel==HudPanel::messages?20.f:30.f)*unit;
    float x,y;header_control_target(panel,0,h,x,y);
    float left=x-cw*.5f,top=y-ch*.5f,right=x+cw*.5f,bottom=y+ch*.5f;
    if(!closed) {
        left=(std::min)(left,h.left);top=(std::min)(top,h.top);right=(std::max)(right,h.left+h.width);bottom=(std::max)(bottom,h.top+h.height);
        const unsigned map_areas[]={5,3,4};
        const unsigned count=panel==HudPanel::minimap?3u:panel==HudPanel::character_info?4u:2u;
        for(unsigned i=0;i<count;++i){header_control_target(panel,panel==HudPanel::minimap?map_areas[i]:i+1,h,x,y);
            left=(std::min)(left,x-cw*.5f);top=(std::min)(top,y-ch*.5f);right=(std::max)(right,x+cw*.5f);bottom=(std::max)(bottom,y+ch*.5f);}
    }
    bounds={left,top,right-left,bottom-top,(right-left)/h.scale,(bottom-top)/h.scale};return true;
}
bool HudLayoutContext::widescreen_hud_panel_bounds(HudPanel panel,HudPanelBounds& bounds) noexcept {
    if(panel==HudPanel::quest_items) {
        const auto* object=current_panel(unsigned(HudPanel::items));if(!object)return false;
        bool any=editing;for(unsigned i=4;i<8 && !any;++i)any=active_slot(6,i,object);
        return any && quest_group_bounds(object,bounds);
    }
    if(panel==HudPanel::messages) {
        MessagePanelBounds b;
        if(!current_panel(unsigned(panel)))return false; b=message_panel_bounds();
        bounds={b.left,b.top,b.width,b.height,440.f*g_layout.unit,100.f*g_layout.unit};return true;
    }
    const unsigned i=unsigned(panel);
    const auto* object=current_panel(i);
    if(!object)return false;
    const RECT native=panel_native_rect(i,object);const auto* r=&native.left;
    const auto h=panel_composition(i,object);
    float width,height;panel_native_size(i,r,width,height);
    bounds={widescreen::compose_x(widescreen::placed_to_screen_x(g_layout,float(r[0])),h),
        widescreen::compose_y(widescreen::placed_to_screen_y(g_layout,float(r[1])),h),
        width*h.scale,height*h.scale,width,height};
    return bounds.width>0.f && bounds.height>0.f;
}
void HudLayoutContext::widescreen_hud_position_percent(HudPanel panel,float left,float top,float width,float height,float& x,float& y) noexcept {
    const unsigned i=unsigned(panel);const auto* object=current_panel(i);
    float ax=0.f,ay=0.f,cw=width,ch=height;
    if(object && slot_panel(i)) {
        const int d=slot_direction(i);const auto anchor=kPanelRoles[i].anchor;
        ax=anchor.x==Anchor::end?1.f:anchor.x==Anchor::centre?.5f:0.f;
        ay=anchor.y==Anchor::end?1.f:anchor.y==Anchor::centre?.5f:0.f;
        if(i==4 && g_hud_panels[i].direction==0)ax=.5f,ay=1.f;
        else if(d>=2)ax=d==2?0.f:1.f;else ay=d==1?0.f:1.f;
        const auto r=panel_native_rect(i,object);slot_dimensions(i,0,object,cw,ch);
        cw*=width/(std::max)(1.f,float(r.right-r.left));ch*=height/(std::max)(1.f,float(r.bottom-r.top));
    }
    x=g_layout.width>cw?(left+ax*(width-cw))*100.f/(g_layout.width-cw):0.f;
    y=g_layout.height>ch?(top+ay*(height-ch))*100.f/(g_layout.height-ch):0.f;
    x=std::clamp(x,0.f,100.f);y=std::clamp(y,0.f,100.f);
}
void HudLayoutContext::widescreen_hud_preview_slots(HudPanel panel,std::vector<HudPreviewSlot>& slots) {
    slots.clear();const unsigned index=unsigned(panel);
    if(panel==HudPanel::quest_items) {
        const auto* object=current_panel(unsigned(HudPanel::items));if(!editing || !object)return;
        for(unsigned i=4;i<8;++i)if(!active_slot(6,i,object)){HudPanelBounds b;if(quest_slot_bounds(i,object,b))slots.push_back({b,i-4});}
        return;
    }
    if(!editing || !slot_panel(index) || !g_hud_panels[index].preview)return;
    const auto* object=current_panel(index);if(!object)return;
    for(unsigned i=0;i<layout_slot_count(index,object);++i)if(!active_slot(index,i,object)) {
        HudPanelBounds b;if(slot_screen_bounds(index,i,object,b))slots.push_back({b,i});
    }
}
bool HudLayoutContext::portrait_slot_composition(unsigned slot,const HudNativePanel* object,widescreen::HudComposition& h) noexcept {
    if(slot>=object->slots.size())return false;
    const auto& native=object->slots[slot];
    const auto r=native.rect;const auto* shift=&native.shift.x;
    HudPanelBounds b;if(!slot_screen_bounds(4,slot,object,b))return false;
    h.offset_x+=b.left-widescreen::compose_x(widescreen::placed_to_screen_x(g_layout,float(r.left+shift[0])),h);
    h.offset_y+=b.top-widescreen::compose_y(widescreen::placed_to_screen_y(g_layout,float(r.top+shift[1])),h);return true;
}
bool HudLayoutContext::widescreen_hud_asset_frame(HudPanel panel,HudAssetFrame& frame) noexcept {
    // These are the only panels with a native perimeter frame. Bars/items/
    // weapons remain borderless; portraits use their per-character selection frame.
    if(panel!=HudPanel::minimap && panel!=HudPanel::character_info && panel!=HudPanel::messages)return false;
    HudPanelBounds b;if(!widescreen_hud_panel_bounds(panel,b))return false;
    const auto header=header_layout(panel);float fraction=0.f;
    {
        const HudNativePanel* object=panel==HudPanel::messages?nullptr:current_panel(unsigned(panel));
        if(panel==HudPanel::messages){const HudNativePanel* text=nullptr;if(!current_message_panel(object,text))return false;}
        if(!object->shell_visible)return false;
        const float f=slide_fraction(panel,object);fraction=f;
        if(header.side==DockSide::left)b.width=header.width+(b.width-header.width)*(1.f-f);
        else if(header.side==DockSide::right){const float w=header.width+(b.width-header.width)*(1.f-f);b.left+=b.width-w;b.width=w;}
        else if(header.side==DockSide::bottom){const float h=panel==HudPanel::messages?header.height+(b.height-header.height)*(1.f-f):b.height*(1.f-f)+header.height;b.top=header.top+header.height-h;b.height=h;}
        else if(panel==HudPanel::messages && header.side==DockSide::top)b.height=header.height+(b.height-header.height)*(1.f-f);
        else {const float bottom=b.top+b.height*(1.f-f);b.top=header.top;b.height=(std::max)(header.height,bottom-b.top);}
    }
    unsigned edges=15;
    const float slack=edge_slack();
    if(b.left<=slack)edges&=~8u;if(b.top<=slack)edges&=~1u;
    if(b.left+b.width>=g_layout.width-slack)edges&=~2u;if(b.top+b.height>=g_layout.height-slack)edges&=~4u;
    const unsigned dock=header.side==DockSide::left && b.left<=slack?8u:
        header.side==DockSide::right && b.left+b.width>=g_layout.width-slack?2u:
        header.side==DockSide::top && b.top<=slack?1u:
        header.side==DockSide::bottom && b.top+b.height>=g_layout.height-slack?4u:0u;
    if(dock && fraction>=1.f)edges=dock;
    frame={b,5.f*g_layout.unit*g_layout.scale,edges,false,hud_opacity(panel)};return true;
}
bool HudLayoutContext::portrait_frame(unsigned slot, HudAssetFrame& frame) {
    const auto* object=current_panel(unsigned(HudPanel::portraits));
    if (!object || slot>=object->native_count || slot>=object->slots.size()) return false;
    const auto& native=object->slots[slot];
    const auto& r=native.rect;
    auto h=panel_composition(unsigned(HudPanel::portraits),object);
    if (!portrait_slot_composition(slot,object,h)) return false;
    HudPanelBounds b{widescreen::compose_x(widescreen::placed_to_screen_x(g_layout,float(r.left+native.shift.x)),h),
        widescreen::compose_y(widescreen::placed_to_screen_y(g_layout,float(r.top+native.shift.y)),h),
        float(r.right-r.left)*g_layout.unit*h.scale,float(r.bottom-r.top)*g_layout.unit*h.scale};
    frame={b,5.f*g_layout.unit*g_layout.scale,15,true,hud_opacity(HudPanel::portraits)};
    return true;
}
void HudLayoutContext::widescreen_hud_asset_frames(std::vector<HudAssetFrame>& frames) {
    frames.clear();
    for(const auto panel:{HudPanel::minimap,HudPanel::character_info,HudPanel::messages}) {
        HudAssetFrame frame;if(widescreen_hud_asset_frame(panel,frame))frames.push_back(frame);
    }
    const auto* object=current_panel(unsigned(HudPanel::portraits));
    if (!object) return;
    for (unsigned i=0; i<object->native_count && i<object->slots.size(); ++i) {
        HudAssetFrame frame;
        if (object->slots[i].selection_frame && portrait_frame(i,frame)) frames.push_back(frame);
    }
}
void HudLayoutContext::reset_fades(std::uint64_t now) noexcept {
    if(fade_lifetime_)return;
    fades_={};
    for(auto& state:fades_)state.active_at=state.transition_at=now;
    fade_lifetime_=true;
}
void HudLayoutContext::advance_fade(FadeState& state,std::uint64_t now) noexcept {
    const int duration=state.target_opacity>state.start_opacity?fade_timing_.in_ms:fade_timing_.out_ms;
    const float progress=duration?(std::min)(1.f,float(now-state.transition_at)/float(duration)):1.f;
    const float eased=progress*progress*(3.f-2.f*progress);
    state.opacity=state.start_opacity+(state.target_opacity-state.start_opacity)*eased;
}
void HudLayoutContext::set_fade_target(FadeState& state,float target,std::uint64_t now) noexcept {
    if(state.target_opacity==target)return;
    advance_fade(state,now);
    state.start_opacity=state.opacity;state.target_opacity=target;state.transition_at=now;
    if((target>state.start_opacity?fade_timing_.in_ms:fade_timing_.out_ms)==0)state.opacity=target;
}
void HudLayoutContext::set_hud_fades(const std::array<HudFadeSettings,kHudPanels.size()>& settings,const std::array<float,2>& corner_scales,HudFadeTiming timing,std::uint64_t now) noexcept {
    reset_fades(now);
    for(unsigned i=0;i<settings.size();++i) {
        if(settings[i].enabled!=fade_settings_[i].enabled) {
            auto& state=fades_[i];state.active_at=state.transition_at=now;
            state.opacity=state.start_opacity=state.target_opacity=1.f;
        }
        fade_settings_[i]={settings[i].enabled,std::clamp(settings[i].transparency_percent,0.f,100.f)};
    }
    for(unsigned i=0;i<2;++i)g_corner_scales[i]=std::clamp(corner_scales[i],50.f,300.f);
    fade_timing_={std::clamp(timing.out_ms,0,10000),std::clamp(timing.in_ms,0,2000),std::clamp(timing.idle_ms,0,60000)};
}
bool HudLayoutContext::hud_hovered(HudPanel panel) const noexcept {
    return current_panel(unsigned(panel)) && fades_[unsigned(panel)].hovered;
}
bool HudLayoutContext::hud_fade_enabled(HudPanel panel) const noexcept {
    return unsigned(panel)<fade_settings_.size() && fade_settings_[unsigned(panel)].enabled;
}
float HudLayoutContext::hud_opacity(HudPanel panel) const noexcept {
    const auto* object=current_panel(unsigned(panel));
    if(!object || editing)return 1.f;
    return object->opacity*(hud_fade_enabled(panel)?fades_[unsigned(panel)].opacity:1.f);
}
void HudLayoutContext::hud_activity(HudPanel panel,std::uint64_t now) noexcept {
    if(!current_panel(unsigned(panel)))return;
    reset_fades(now);auto& state=fades_[unsigned(panel)];state.active_at=now;
    set_fade_target(state,1.f,now);
}
void HudLayoutContext::hud_content_values(HudPanel panel,const std::vector<std::uint64_t>& values,std::uint64_t now) noexcept {
    if(!current_panel(unsigned(panel)) || !hud_fade_enabled(panel))return;
    reset_fades(now);auto& previous=fades_[unsigned(panel)].content_values;
    if(previous==values)return;
    try{previous=values;}catch(const std::bad_alloc&){}
    hud_activity(panel,now);
}
void HudLayoutContext::update_hud_fades(float mouse_x,float mouse_y,std::uint64_t now) noexcept {
    reset_fades(now);
    for(unsigned i=0;i<fades_.size();++i) {
        auto& state=fades_[i];state.hovered=false;
        advance_fade(state,now);
        const auto panel=HudPanel(i);const auto* object=current_panel(i);
        HudPanelBounds bounds;
        if(widescreen_hud_panel_bounds(panel,bounds)) {
            HudAssetFrame frame;
            if(widescreen_hud_asset_frame(panel,frame))bounds=frame.bounds;
            state.hovered=mouse_x>=bounds.left && mouse_y>=bounds.top && mouse_x<bounds.left+bounds.width && mouse_y<bounds.top+bounds.height;
            HudPanelBounds controls;
            if(widescreen_hud_header_bounds(panel,controls)) {
                const bool on_controls=mouse_x>=controls.left && mouse_y>=controls.top && mouse_x<controls.left+controls.width && mouse_y<controls.top+controls.height;
                state.hovered=object && slide_fraction(panel,object)>=1.f?on_controls:state.hovered || on_controls;
            }
        }
        if(state.hovered)state.active_at=now;
        if(!fade_settings_[i].enabled || editing) {
            state.opacity=state.start_opacity=state.target_opacity=1.f;state.transition_at=now;
        } else {
            const bool waking=state.target_opacity>state.start_opacity && state.opacity<state.target_opacity;
            const float target=state.hovered || waking || now-state.active_at<unsigned(fade_timing_.idle_ms)?
                1.f:1.f-fade_settings_[i].transparency_percent/100.f;
            set_fade_target(state,target,now);
        }
        // Like retail c79, the quest widget wakes on its own hover, not on
        // unrelated quick-item activity from the shared native items object.
        if(panel==HudPanel::quest_items || !fade_settings_[i].enabled || !object || !object->activity_available)continue;
        if(state.native_values!=object->activity_values) {
            try{state.native_values=object->activity_values;}catch(const std::bad_alloc&){}
            hud_activity(panel,now);
        }
    }
}
}

namespace owned {
void HudLayoutContext::set_hud_width(int value) noexcept {
    hud_width_=static_cast<widescreen::HudWidth>(std::clamp(value,0,2));
    g_layout=widescreen::make_layout(g_layout.width,g_layout.height,hud_width_,g_layout.scale);
}
void HudLayoutContext::set_ui_scale(int percent) noexcept {
    ui_scale_percent_=std::clamp(percent,50,120);
    g_layout.scale=ui_scale_percent_/100.f;
}
void HudLayoutContext::publish_message(HudNativePanel&& panel) noexcept {
    auto& current=frame_.panels[unsigned(HudPanel::messages)];
    // Synchronous layout is not a new content/activity publication. Preserve
    // the current wake facts, fade timing and opacity across reflow.
    panel.activity_values=std::move(current.activity_values);
    panel.activity_available=current.activity_available;panel.opacity=current.opacity;
    frame_.active=true;current=std::move(panel);
}
bool HudLayoutContext::message_layout(MessageGeometry& geometry,float& text_scale) noexcept {
    if(!current_panel(unsigned(HudPanel::messages)) || !(g_layout.unit*g_layout.scale>0.f))return false;
    geometry=message_geometry(header_layout(HudPanel::messages),g_layout.unit*g_layout.scale,g_layout.unit);
    text_scale=g_message_panel.text_scale_percent/100.f;
    return geometry.text.right-geometry.text.left>14 && geometry.text.bottom>geometry.text.top;
}
bool HudLayoutContext::message_text_rect(RECT& placed,float& text_scale) noexcept {
    MessageGeometry geometry;
    if(!message_layout(geometry,text_scale))return false;
    placed=geometry.text;return true;
}
bool HudLayoutContext::tutorial_bounds(const TutorialTarget& target,HudPanelBounds& bounds) noexcept {
    struct Reader {
        HudLayoutContext& c;
        const HudNativePanel* object=nullptr;
        unsigned panel=0;
        bool select(HudPanel role,HudPanelBounds& b) noexcept {
            panel=unsigned(role);object=c.current_panel(panel);
            return object && c.widescreen_hud_panel_bounds(role,b);
        }
        unsigned count() const noexcept {return object->native_count;}
        int skill_type(unsigned slot) const noexcept {return slot<object->slots.size()?object->slots[slot].semantic_type:INT_MIN;}
        RECT slot_rect(unsigned slot) const noexcept {
            if(slot>=object->slots.size())return {};
            auto r=object->slots[slot].rect;OffsetRect(&r,object->slots[slot].shift.x,object->slots[slot].shift.y);return r;
        }
        bool slot_bounds(unsigned slot,HudPanelBounds& b) noexcept {return c.slot_screen_bounds(panel,slot,object,b);}
        const widescreen::Layout& layout() const noexcept {return c.g_layout;}
        widescreen::Anchors anchor() const noexcept {return kPanelRoles[panel].anchor;}
        widescreen::HudComposition composition() noexcept {return c.panel_composition(panel,object);}
        HeaderLayout header(const HudPanelBounds&) noexcept {return c.header_layout(HudPanel(panel));}
        float slide() const noexcept {return object->slide;}
        void visible(const HeaderLayout& h,HudPanelBounds& b) noexcept {tutorial_visible_bounds(HudPanel(panel),object->slide,h,b);}
        void control_target(unsigned area,const HeaderLayout& h,float& x,float& y) noexcept {c.header_control_target(HudPanel(panel),area,h,x,y);}
    } reader{*this};
    return resolve_tutorial_target(target,reader,bounds);
}
void HudLayoutContext::set_quest_items(const QuestItemSettings& settings) noexcept {
    g_quest_items.scale_percent=std::clamp(settings.scale_percent,50.f,300.f);
    g_quest_items.x_percent=settings.x_percent<0.f?-1.f:std::clamp(settings.x_percent,0.f,100.f);
    g_quest_items.y_percent=settings.y_percent<0.f?-1.f:std::clamp(settings.y_percent,0.f,100.f);
}
void HudLayoutContext::set_hud_panels(const std::array<HudPanelSettings,7>& settings) noexcept {
    for(unsigned i=0;i<settings.size();++i) {
        g_hud_panels[i].scale_percent=std::clamp(settings[i].scale_percent,50.f,300.f);
        g_hud_panels[i].x_percent=std::clamp(settings[i].x_percent,-1.f,100.f);
        g_hud_panels[i].y_percent=std::clamp(settings[i].y_percent,-1.f,100.f);
        g_hud_panels[i].buttons_scale_percent=std::clamp(settings[i].buttons_scale_percent,50.f,200.f);
        g_hud_panels[i].buttons_side=std::clamp(settings[i].buttons_side,0,5);
        g_hud_panels[i].direction=std::clamp(settings[i].direction,0,i==4?4:3);
        g_hud_panels[i].padding=std::clamp(settings[i].padding,-100.f,100.f);
        g_hud_panels[i].border_padding=std::clamp(settings[i].border_padding,-100.f,100.f);
        g_hud_panels[i].preview=settings[i].preview;
        g_hud_panels[i].preview_count=std::clamp(settings[i].preview_count,1u,i==5||i==6?4u:32u);
        g_hud_panels[i].slots_per_line=i==4?0u:(std::min)(settings[i].slots_per_line,32u);
    }

}

void HudLayoutContext::set_message_panel(const MessagePanelSettings& settings) noexcept {
    g_message_panel.width_percent=std::clamp(settings.width_percent,50.f,800.f);
    g_message_panel.height_percent=std::clamp(settings.height_percent,50.f,600.f);
    g_message_panel.centre_x_percent=std::clamp(settings.centre_x_percent,0.f,100.f);
    g_message_panel.top_y_percent=std::clamp(settings.top_y_percent,0.f,100.f);
    g_message_panel.text_scale_percent=std::clamp(settings.text_scale_percent,50.f,300.f);
    g_message_panel.buttons_scale_percent=std::clamp(settings.buttons_scale_percent,50.f,200.f);
    g_message_panel.buttons_side=std::clamp(settings.buttons_side,0,5);
    g_message_panel.border_padding=std::clamp(settings.border_padding,-100.f,100.f);

}
}
