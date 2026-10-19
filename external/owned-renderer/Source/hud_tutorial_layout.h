#pragma once
#include <windows.h>
#include "widescreen.h"
#include <cmath>
#include <cstring>

namespace owned {
enum class TutorialTargetKind { panel,native,control,controls,skill,spell };
struct TutorialTarget { const char* name; int page; RECT authored; HudPanel panel; TutorialTargetKind kind; int selector=0; };
// This shipped build publishes general manual-action type 0: its native
// target-object mode dispatches stealing on units and science on objects.
constexpr TutorialTarget kTutorialTargets[] = {
    {"zt3",1,{183,0,618,96},HudPanel::messages,TutorialTargetKind::panel},
    {"zt3",1,{385,1,412,19},HudPanel::messages,TutorialTargetKind::control},
    {"zt3",2,{770,568,799,599},HudPanel::game_speed,TutorialTargetKind::native},
    {"zt4",0,{720,519,799,599},HudPanel::game_speed,TutorialTargetKind::panel},
    {"zt4",1,{720,519,799,599},HudPanel::game_speed,TutorialTargetKind::panel},
    {"zt7",0,{762,432,798,468},HudPanel::spells,TutorialTargetKind::spell},
    {"zt9",0,{1,567,36,599},HudPanel::stance,TutorialTargetKind::native},
    {"zt10",0,{619,1,799,162},HudPanel::minimap,TutorialTargetKind::panel},
    {"zt10",1,{780,70,799,162},HudPanel::minimap,TutorialTargetKind::controls,0x38},
    {"zt10",1,{778,1,799,26},HudPanel::minimap,TutorialTargetKind::control},
    {"zt11",0,{183,0,618,96},HudPanel::messages,TutorialTargetKind::panel},
    {"zt11",0,{385,1,412,19},HudPanel::messages,TutorialTargetKind::control},
    {"zt11",1,{183,0,618,96},HudPanel::messages,TutorialTargetKind::panel},
    {"zt11",1,{385,1,412,19},HudPanel::messages,TutorialTargetKind::control},
    {"zt12",0,{1,1,185,222},HudPanel::character_info,TutorialTargetKind::panel},
    {"zt12",0,{1,1,23,29},HudPanel::character_info,TutorialTargetKind::control},
    {"zt12",1,{1,99,23,222},HudPanel::character_info,TutorialTargetKind::controls,0x1e},
    {"zt13",0,{1,516,83,599},HudPanel::stance,TutorialTargetKind::panel},
    {"zt14",0,{1,430,37,466},HudPanel::abilities,TutorialTargetKind::skill,0},
    {"zt14",2,{1,430,37,466},HudPanel::abilities,TutorialTargetKind::skill,0},
    {"zt17",0,{74,491,321,599},HudPanel::weapons,TutorialTargetKind::panel},
    {"zt18",0,{452,484,709,599},HudPanel::items,TutorialTargetKind::panel},
    {"zt20",0,{1,470,38,507},HudPanel::abilities,TutorialTargetKind::skill,-1},
    {"zt22",0,{1,432,39,469},HudPanel::abilities,TutorialTargetKind::skill,0},
};
inline const TutorialTarget* tutorial_target(const char* name,int page,const RECT& authored) noexcept {
    if(!name)return nullptr;
    for(const auto& target:kTutorialTargets)
        if(target.page==page && _stricmp(target.name,name)==0 &&
           EqualRect(&target.authored,&authored))return &target;
    return nullptr;
}
template<class Header>
void tutorial_visible_bounds(HudPanel panel,float f,const Header& header,HudPanelBounds& b) noexcept {
    if(header.side==decltype(header.side)::left)b.width=header.width+(b.width-header.width)*(1.f-f);
    else if(header.side==decltype(header.side)::right){const float w=header.width+(b.width-header.width)*(1.f-f);b.left+=b.width-w;b.width=w;}
    else if(header.side==decltype(header.side)::bottom){const float h=panel==HudPanel::messages?header.height+(b.height-header.height)*(1.f-f):b.height*(1.f-f)+header.height;b.top=header.top+header.height-h;b.height=h;}
    else if(panel==HudPanel::messages && header.side==decltype(header.side)::top)b.height=header.height+(b.height-header.height)*(1.f-f);
    else {const float bottom=b.top+b.height*(1.f-f);b.top=header.top;b.height=(std::max)(header.height,bottom-b.top);}
}

// Reader supplies native facts; all semantic selection and target geometry are
// shared by the fingerprinted proxy and the typed embedded publication.
template<class Reader>
bool resolve_tutorial_target(const TutorialTarget& target,Reader& reader,HudPanelBounds& b) noexcept {
    const unsigned panel=unsigned(target.panel);
    if(!reader.select(target.panel,b))return false;
    if(target.kind==TutorialTargetKind::skill) {
        for(unsigned slot=0;slot<reader.count();++slot)
            if(reader.skill_type(slot)==target.selector)return reader.slot_bounds(slot,b);
        return false;
    }
    if(target.kind==TutorialTargetKind::spell) {
        const auto& layout=reader.layout();const auto anchor=reader.anchor();
        const float x=widescreen::layout_to_placed_x(layout,anchor,.5f*float(target.authored.left+target.authored.right),false);
        const float y=widescreen::layout_to_placed_y(layout,anchor,.5f*float(target.authored.top+target.authored.bottom),false);
        for(unsigned slot=0;slot<reader.count();++slot) {
            const auto r=reader.slot_rect(slot);
            if(x>=r.left && x<r.right && y>=r.top && y<r.bottom)return reader.slot_bounds(slot,b);
        }
        return false;
    }
    if(target.kind==TutorialTargetKind::native) {
        const auto& layout=reader.layout();const auto h=reader.composition();const auto anchor=reader.anchor();
        const auto x=[&](LONG v){return widescreen::compose_x(widescreen::placed_to_screen_x(layout,widescreen::layout_to_placed_x(layout,anchor,float(v),false)),h);};
        const auto y=[&](LONG v){return widescreen::compose_y(widescreen::placed_to_screen_y(layout,widescreen::layout_to_placed_y(layout,anchor,float(v),false)),h);};
        const float left=x(target.authored.left),top=y(target.authored.top);
        b={left,top,x(target.authored.right)-left,y(target.authored.bottom)-top};return true;
    }
    if(target.panel!=HudPanel::messages && target.panel!=HudPanel::minimap && target.panel!=HudPanel::character_info)
        return target.kind==TutorialTargetKind::panel;
    const auto header=reader.header(b);
    if(target.kind==TutorialTargetKind::panel) {reader.visible(header,b);return true;}
    const unsigned mask=target.kind==TutorialTargetKind::control?1u:unsigned(target.selector);
    if(mask!=1u && reader.slide()>=1.f)return false;
    bool first=true;
    const float unit=reader.layout().unit*header.scale;
    const float width=(target.panel==HudPanel::messages?30.f:20.f)*unit;
    const float height=(target.panel==HudPanel::messages?20.f:30.f)*unit;
    for(unsigned area=0;area<6;++area)if(mask&(1u<<area)) {
        float x,y;reader.control_target(area,header,x,y);
        const float left=x-width*.5f,top=y-height*.5f;
        if(first){b={left,top,width,height};first=false;}
        else {const float right=(std::max)(b.left+b.width,left+width),bottom=(std::max)(b.top+b.height,top+height);
            b.left=(std::min)(b.left,left);b.top=(std::min)(b.top,top);b.width=right-b.left;b.height=bottom-b.top;}
    }
    return !first;
}
}
