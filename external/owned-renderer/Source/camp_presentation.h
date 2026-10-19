#pragma once
#include "camp_frontend.h"

// Synchronous c74 presentation inputs. Adapters own source objects, polygons,
// native figures and transactions; the renderer owns layout and enumeration.
namespace owned::camp::frontend {
inline RECT pixels(const Layout& l,Rect r) noexcept {
    return {LONG(std::lround((r.left-l.left)*l.unit)),LONG(std::lround(r.top*l.unit)),
            LONG(std::lround((r.right-l.left)*l.unit)),LONG(std::lround(r.bottom*l.unit))};
}
inline Rect card_plate(const Layout& l,unsigned card) noexcept {
    return card==0?l.ordinary_info:card==1?l.result_info:l.comparison_info;
}
struct BackgroundState {
    int mode=0;
    bool quick=false,stats=false,perks=false,wagon=false,merchant=false;
    bool trade=false,construction=false,repair=false,empty_construction=false;
    std::array<bool,3> cards{};
};
// The authoritative child body supplies lists, prices and constructor results.
// c74 keeps this canvas active for figures emitted after ObjectDraw as well.
struct ChildVisualScope {
    VisualScope scope;
    ChildVisualScope(const Layout& l,int mode,Role role,bool active=true) noexcept
        :scope(active&&role==Role::construction?composition(l,uniform(l,mode,role)):visual,
               (active&&role==Role::construction)||composing){}
};
inline Rect background_panel(const Layout& l,int index) noexcept {
    const Rect panels[]{l.merchant,l.stash,l.skills,{200,100,600,500},
                        {0,100,200,400},l.learned,l.available};
    return panels[index];
}
template<class Background,class Panel>
void draw_background(const Layout& l,int mode,Background background,Panel panel) {
    background(Rect{l.left,0,l.left+l.width,600},int(std::ceil(l.width/100)),6);
    for(int i=0;i<7;++i){
        if((i==0&&mode<2)||(i==4&&mode>=2)||((i==2||i>=5)&&mode!=1)||
           (i==3&&(mode<2||mode==3||mode==5)))continue;
        const auto source=background_panel(l,i);
        panel(i,Placement{source,i==3?stage(l,mode):i==4?l.stats:source});
    }
}
template<class Card>
void draw_card_backgrounds(const Layout& l,const std::array<bool,3>& cards,Card draw) {
    for(unsigned i=0;i<cards.size();++i)if(cards[i])
        draw(Placement{{0,0,200,300},card_plate(l,i)});
}
template<class Scrollbar>
CampDecoration background_decoration(const Layout& l,const BackgroundState& s,Scrollbar scrollbar) {
    CampDecoration d;d.pixel_unit=l.unit;d.title_plate=pixels(l,l.title_plate);
    const auto panel=[&](Rect r,float corner){d.panels[d.count++]={pixels(l,r),corner};};
    const auto divider=[&](Rect span,float height=50.f/3){
        const float w=(std::min)(span.width(),800.f/3),x=(span.left+span.right-w)*.5f,y=(span.top+span.bottom)*.5f;
        d.dividers[d.divider_count++]={pixels(l,{x,y-height*.5f,x+w,y+height*.5f}),false};
    };
    if(s.mode==0&&s.quick)panel(l.character,8);
    if(s.mode<2&&s.stats)panel({l.stats.left-110.f/3,l.stats.top,l.stats.right+110.f/3,l.stats.bottom},12);
    if(((s.mode==2||s.mode==4)&&s.trade)||((s.mode==3||s.mode==5)&&s.construction)||
       (s.mode==6&&s.repair))panel(stage(l,s.mode),8);
    if(s.mode==1&&s.perks){
        panel({l.skills.left,161,l.skills.right,590},12);
        panel({l.available.left,161,l.available.right,590},12);
        divider({l.skills.left+24,310,l.skills.right-24,320},6);
    }
    if(s.mode>=2&&s.merchant)divider({l.merchant.left,l.category(true,0).bottom,l.merchant.right,l.grid(true).top});
    if(s.wagon)divider({l.stash.left,l.category(false,0).bottom,l.stash.right,l.grid(false).top});
    if(s.mode!=1)divider({l.content.left+25,490,l.content.right-25,502});
    if(s.mode==2||s.mode==4||s.mode==6){
        const auto gutter=[&](float left,float right){
            constexpr float w=70.f/3;if(right-left<w+16)return;
            const float x=(left+right)*.5f;
            d.dividers[d.divider_count++]={pixels(l,{x-w*.5f,215,x+w*.5f,335}),true};
        };
        if(s.merchant){const auto bar=scrollbar(s.mode==2?List::spell_vendor:List::item_vendor);
            gutter(bar.maximum?bar.track.right+10.f/3:l.grid(true).right,l.workspace.left);}
        if(s.wagon)gutter(l.workspace.right,l.grid(false).left);
    }
    for(unsigned i=0;i<s.cards.size();++i)if(s.cards[i])d.cards[d.card_count++]=pixels(l,card_plate(l,i));
    if(s.cards[1]&&s.empty_construction){
        const auto r=l.result_info;const float x=(r.left+r.right)*.5f,y=(r.top+r.bottom)*.5f;
        d.construction_icon=pixels(l,{x-40,y-40,x+40,y+40});d.construction_icon_visible=true;
    }
    return d;
}
struct InputState {
    Role role=Role::none;
    int mode=0,first=0,capacity=8,areas=0,learned_start=34,learned_rows=14;
    std::array<bool,2> actions{{true,true}};
};
inline int area_template(const InputState& s,int i) noexcept {
    return s.role==Role::perks&&i>=s.learned_start?s.learned_start:i;
}
inline Placement area(const Layout& l,const InputState& s,int i,Rect source) noexcept {
    auto target=source;
    if(s.role==Role::perks){
        if(i<4)return {};
        const bool learned=i>=s.learned_start;
        const float y=learned?270+(i-s.learned_start)*15.f:source.top;
        const auto band=perk_band(l,source.left,y);
        const float sx=band.destination.width()/band.source.width(),sy=band.destination.height()/band.source.height();
        target={band.destination.left+(source.left-band.source.left)*sx,band.destination.top+(y-band.source.top)*sy,
                band.destination.left+(source.right-band.source.left)*sx,band.destination.top+(y+source.height()-band.source.top)*sy};
    }else switch(s.role){
    case Role::root:
        if(i<2){const auto r=actions(l,s.mode);const float scale=r.width()/200;
            target={r.left+source.left*scale,r.top+(source.top-400)*scale,
                    r.left+source.right*scale,r.top+(source.bottom-400)*scale};}
        else if(i<4)target=l.rotation[i-2];
        else if(i<12)target=l.mode_button(i-4);
        break;
    case Role::wagon:case Role::vendor:return grid_area(l,s.role==Role::vendor,i,source);
    case Role::quick:
        if(i<8){target=l.equipped(i<4?i:11-i,s.first,true);break;}
        {const auto r=stage(l,s.mode);const float scale=r.width()/400;
            target={r.left+(source.left-200)*scale,r.top+(source.top-100)*scale,
                    r.left+(source.right-200)*scale,r.top+(source.bottom-100)*scale};break;}
    default:{
        const bool stats=s.role==Role::stats;const auto r=stats?l.stats:stage(l,s.mode);
        const float sx=r.width()/(stats?200:400),sy=r.height()/(stats?300:400);
        target={r.left+(source.left-(stats?0:200))*sx,r.top+(source.top-100)*sy,
                r.left+(source.right-(stats?0:200))*sx,r.top+(source.bottom-100)*sy};break;
    }
    case Role::spells:target=l.equipped(i,s.first);break;
    case Role::trade:target=l.trade_cell(i);break;
    }
    return {source,target};
}
inline bool area_admitted(const Layout& l,const InputState& s,int i,Placement p,bool tip=false) noexcept {
    if(s.role==Role::root&&i<2&&!s.actions[i])return false;
    if((s.role==Role::wagon||s.role==Role::vendor)&&i<2)return false;
    if(s.role==Role::perks&&i<4)return false;
    if(tip&&s.role==Role::spells&&s.capacity==16&&i>=7)return false;
    if((s.role==Role::spells||(s.role==Role::quick&&i<8))&&!belt_visible(l,p.destination,s.role==Role::quick))return false;
    return p.source.width()>0&&p.source.height()>0&&p.destination.width()>0&&p.destination.height()>0;
}
template<class Contains>
bool point_in_area(const Layout& l,const InputState& s,int i,Placement p,POINT point,Contains contains,bool tip=false) {
    return area_admitted(l,s,i,p,tip)&&p.destination.contains(float(point.x),float(point.y))&&
        contains(area_template(s,i),inverse(p,point));
}
template<class Source,class Contains>
int hit(const Layout& l,const InputState& s,POINT point,Source source,Contains contains) {
    if(s.role==Role::none)return -2;
    if(s.role==Role::spells){
        for(int i=s.first;i<(std::min)(s.capacity,s.first+8);++i){
            const Placement p{source(0),l.equipped(i,s.first)};
            if(area_admitted(l,s,i,p)&&p.destination.contains(float(point.x),float(point.y))&&contains(0,inverse(p,point)))return i;
        }
        return -1;
    }
    const int count=s.role==Role::perks?s.learned_start+s.learned_rows:s.areas;
    for(int i=0;i<count;++i){
        const int native=area_template(s,i);if(native<0||native>=s.areas)continue;
        if(point_in_area(l,s,i,area(l,s,i,source(native)),point,contains))return i;
    }
    return -1;
}
inline bool inside(const Layout& l,Role role,int mode,POINT point) noexcept {
    Rect bounds;
    switch(role){
    case Role::root:case Role::background:bounds={l.left,0,l.left+l.width,600};break;
    case Role::wagon:bounds=l.stash;break;
    case Role::vendor:bounds=l.merchant;break;
    case Role::info:case Role::help:case Role::comparison:case Role::blueprint:case Role::none:return false;
    case Role::stats:bounds=l.stats;break;
    case Role::spells:bounds=l.belt;break;
    case Role::quick:bounds={l.weapons.left,l.character.top,l.quick_items.right,l.character.bottom};break;
    default:bounds=stage(l,mode);break;
    }
    return bounds.contains(float(point.x),float(point.y));
}
inline int spell_first(int first,int count) noexcept {return std::clamp(first,0,(std::max)(0,count-8));}
inline int spell_reveal(int first,int selected,int count) noexcept {
    if(selected>=0&&selected<count){if(selected<first)first=selected;else if(selected>=first+8)first=selected-7;}
    return spell_first(first,count);
}
template<class Item,class ClickItem,class ClickSlot>
void slot_click(Item item,int slot,int part,ClickItem click_item,ClickSlot click_slot) {
    if(item)click_item(item,part);else if(slot)click_slot(slot,part);
}
}
