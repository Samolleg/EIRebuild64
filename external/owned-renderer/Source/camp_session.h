#pragma once
#include "camp_frontend.h"

// Synchronous presentation state shared by the proxy and the source adapter.
// Item arguments are borrowed for one publication; only copied scalar state stays.
namespace owned::camp::frontend {
enum class HoverKind { none, item, help, skill };
enum class ConstructorCard { none, empty, blueprint, result };
struct OrdinaryHover {
    HoverKind kind=HoverKind::none;
    int value=0,screen_mode=-1;
    unsigned modifiers=0;
    bool touched=false;
    void reset() noexcept {kind=HoverKind::none;touched=false;}
    void begin(int mode,unsigned keys,bool allowed) noexcept {
        if(screen_mode!=mode||modifiers!=keys||!allowed)reset();
        screen_mode=mode;modifiers=keys;touched=false;
    }
    void publish(HoverKind type,bool present,int argument=0) noexcept {
        if(!present){reset();return;}
        kind=type;value=argument;touched=true;
    }
    bool fresh(bool allowed) const noexcept {return kind!=HoverKind::none&&touched&&allowed;}
};
struct CampSession {
    void* owner=nullptr;
    std::uint64_t generation=0;
    int mode=-1;
    bool focus=true,controls=false;
    OrdinaryHover hover;
    std::array<float,6> wheel{};
    std::array<int,6> category{{-1,-1,-1,-1,-1,-1}};
    struct Drag {
        HWND window=nullptr;
        List list=List::stash;
        int screen_mode=0,category=0;
        float grab=0;
    } drag;
    void retire_input() noexcept {hover.reset();wheel={};category.fill(-1);}
    void activate(void* value) noexcept {retire_input();owner=value;mode=-1;++generation;}
    bool deactivate(void* value) noexcept {
        if(owner!=value)return false;
        retire_input();owner=nullptr;mode=-1;++generation;return true;
    }
    bool allowed(bool current) const noexcept {return owner&&current&&focus&&!controls;}
    void begin(int value,unsigned keys,bool current) noexcept {
        if(mode!=value){retire_input();mode=value;}
        hover.begin(value,keys,allowed(current));
    }
    bool input(UINT message,WPARAM wparam,bool renderer_controls,bool current) noexcept {
        controls=renderer_controls;
        if(message==WM_KILLFOCUS||(message==WM_ACTIVATEAPP&&!wparam)||
           (message==WM_ACTIVATE&&LOWORD(wparam)==WA_INACTIVE))focus=false;
        if(message==WM_SETFOCUS||(message==WM_ACTIVATEAPP&&wparam)||
           (message==WM_ACTIVATE&&LOWORD(wparam)!=WA_INACTIVE))focus=true;
        const bool cancel=!allowed(current)||message==WM_CANCELMODE||message==WM_CAPTURECHANGED||
            message==WM_SIZE||message==WM_DESTROY||message==WM_NCDESTROY||
            (message==WM_KEYDOWN&&wparam==VK_F10);
        if(cancel)retire_input();
        return cancel;
    }
    int wheel_steps(List list,int tab,short delta) noexcept {
        const auto i=unsigned(list);
        if(category[i]!=tab){wheel[i]=0;category[i]=tab;}
        wheel[i]-=float(delta)/WHEEL_DELTA;
        const int steps=int(wheel[i]);wheel[i]-=steps;return steps;
    }
};
inline void cancel_drag(CampSession& session) noexcept {
    const HWND window=session.drag.window;session.drag={};
    if(window&&GetCapture()==window)ReleaseCapture();
}
inline POINT virtual_point(const Layout& layout,POINT p) noexcept {
    return {LONG(std::floor(p.x/layout.unit+layout.left)),LONG(std::floor(p.y/layout.unit))};
}
template<class Source,class Apply>
void move_drag(CampSession& session,const Layout& layout,int mode,bool current,POINT point,Source source,Apply apply) {
    const auto drag=session.drag;if(!drag.window)return;
    const auto scroll=source(drag.list);const auto bar=rail(layout,drag.list,scroll);
    if(!session.allowed(current)||mode!=drag.screen_mode||scroll.category!=drag.category||
       !scroll.visible||!bar.maximum||GetCapture()!=drag.window){cancel_drag(session);return;}
    apply(drag.list,drag_position(bar,virtual_point(layout,point),drag.grab));
}
template<class Source,class Apply>
bool input(CampSession& session,const Layout& layout,int mode,bool current,HWND window,
           UINT message,WPARAM wparam,LPARAM lparam,bool controls,Source source,Apply apply) {
    if(session.input(message,wparam,controls,current)){cancel_drag(session);return false;}
    const POINT physical{LONG(short(LOWORD(lparam))),LONG(short(HIWORD(lparam)))};
    if(session.drag.window){
        if(message==WM_MOUSEWHEEL)return true;
        if(message==WM_MOUSEMOVE){move_drag(session,layout,mode,current,physical,source,apply);return true;}
        if(message==WM_LBUTTONUP){move_drag(session,layout,mode,current,physical,source,apply);cancel_drag(session);return true;}
        return false;
    }
    if(message!=WM_LBUTTONDOWN||GetCapture())return false;
    const auto p=virtual_point(layout,physical);
    for(unsigned i=0;i<6;++i){
        const auto list=List(i);const auto scroll=source(list);const auto bar=rail(layout,list,scroll);
        if(!bar.maximum)continue;
        auto hit=bar.track;
        if(bar.horizontal){hit.top-=2.5f;hit.bottom+=2.5f;}
        else{hit.left-=10.f/3;hit.right+=10.f/3;}
        if(i<3){hit.left=(std::max)(hit.left,layout.grid(list!=List::stash).right);
            hit.right=(std::min)(hit.right,list==List::stash?layout.left+layout.width:layout.content.left);}
        if(!hit.contains(float(p.x),float(p.y)))continue;
        const float coordinate=float(bar.horizontal?p.x:p.y),start=bar.horizontal?bar.thumb.left:bar.thumb.top;
        const float length=bar.horizontal?bar.thumb.width():bar.thumb.height();
        SetCapture(window);if(GetCapture()!=window)return false;
        session.drag={window,list,mode,scroll.category,
            coordinate>=start&&coordinate<start+length?coordinate-start:length*.5f};
        move_drag(session,layout,mode,current,physical,source,apply);return true;
    }
    return false;
}
inline Rect wheel_area(const Layout& layout,List list) noexcept {
    switch(list){
    case List::stash:return layout.grid(false);
    case List::spell_vendor:case List::item_vendor:return layout.grid(true);
    case List::spells:return layout.belt;
    case List::learned:return layout.learned;
    default:return layout.available;
    }
}
template<class ScrollSource,class Apply>
bool wheel(CampSession& session,const Layout& layout,POINT point,short delta,bool current,
           ScrollSource source,Apply apply) {
    if(!session.allowed(current))return false;
    for(unsigned i=0;i<6;++i){
        const auto list=List(i);const auto scroll=source(list);
        if(!scroll.visible||!wheel_area(layout,list).contains(float(point.x),float(point.y)))continue;
        const int steps=session.wheel_steps(list,scroll.category,delta);
        if(steps)apply(list,scroll,steps);
        return true;
    }
    return false;
}
inline int role_card(Role role) noexcept {
    return role==Role::info?0:(role==Role::help||role==Role::blueprint)?1:role==Role::comparison?2:-1;
}
inline bool card_content_visible(Role role,ConstructorCard card) noexcept {
    return role==Role::help?card==ConstructorCard::result:
        role!=Role::blueprint||card==ConstructorCard::blueprint;
}
inline bool card_visible(Role role,ConstructorCard card,const std::array<bool,3>& visible,bool allowed) noexcept {
    const int index=role_card(role);
    return index>=0&&visible[index]&&card_content_visible(role,card)&&
        ((role!=Role::info&&role!=Role::comparison)||allowed);
}
inline std::array<bool,3> visible_cards(const OrdinaryHover& hover,bool allowed,bool ordinary_visible,
                                     bool result_visible,ConstructorCard card,bool comparison) noexcept {
    return {{ordinary_visible&&hover.fresh(allowed),result_visible&&card!=ConstructorCard::none,
             allowed&&comparison}};
}
template<class Item,class At,class IsBlueprint>
Item last_blueprint(int count,At at,IsBlueprint is_blueprint) {
    Item selected=nullptr;
    for(int i=0;i<count;++i){auto item=at(i);if(item&&is_blueprint(item))selected=item;}
    return selected;
}
template<class Item,class ArmorAt,class ArmorType>
Item comparison_item(Item item,int kind,Item weapon,int count,ArmorAt at,ArmorType type) {
    if(!item)return nullptr;
    if(kind==0x3004)return weapon;
    if(kind==0x3005){const int wanted=type(item);
        for(int i=0;i<count;++i){auto armor=at(i);if(armor&&type(armor)==wanted)return armor;}}
    return nullptr;
}
struct CardPublication {
    ConstructorCard constructor=ConstructorCard::none;
    bool comparison=false;
    int comparison_modifiers=-1,blueprint_modifiers=-1,result_modifiers=-1;
    void begin() noexcept {constructor=ConstructorCard::none;comparison=false;}
    void reset() noexcept {begin();comparison_modifiers=blueprint_modifiers=result_modifiers=-1;}
};
// Owner callbacks copy into independent native display Parties and format text.
template<class Item,class Set,class Get,class Format>
void publish_comparison(CardPublication& state,Item counterpart,int modifiers,Set set,Get get,Format format) {
    if(counterpart||get())set(counterpart);
    if(counterpart&&state.comparison_modifiers!=modifiers){format();state.comparison_modifiers=modifiers;}
    state.comparison=counterpart!=nullptr;
}
template<class Item,class Set,class Get,class Format,class Reject>
void publish_constructor(CardPublication& state,int mode,bool visible,int help,Item result,Item selected,
                         int modifiers,Set set,Get get,Format format,Reject reject) {
    if((mode!=3&&mode!=5)||!visible){if(get())set(nullptr);return;}
    if(help){
        if(help!=(mode==3?103:102)){reject("constructor_fallback_help_differs");return;}
        if(selected||get())set(selected);
        if(selected&&state.blueprint_modifiers!=modifiers){format(false);state.blueprint_modifiers=modifiers;}
        state.constructor=selected?ConstructorCard::blueprint:ConstructorCard::empty;
    }else{
        if(!result){reject("constructor_result_publication_missing");return;}
        if(state.result_modifiers!=modifiers){format(true);state.result_modifiers=modifiers;}
        state.constructor=ConstructorCard::result;
    }
}
}
