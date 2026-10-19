#include "StdAfx.h"
#include "E4Camp.h"
#include "E1RendererFacade.h"
#include <utility>
#include "../dx11/RendererDX11.h"
#include "../Renderer.h"
#include "../GameJournal.h"
#include <engine_bridge.h>

namespace OwnedRender::E4 {
namespace {
Host host;
owned::camp::Layout layout;
unsigned width=800,height=600,frame=72;
bool requested=true,valid=false,focus=true,controls=false;
bool art_failed=false,popup_failed=false,decoration_pending=false;
int mode=0;
Role drawing=Role::none;
std::array<bool,3> cards{};
owned::CampDecoration decoration;
owned::CampScrollCostSnapshot cost;
std::uint64_t generation=0;
thread_local Preview* preview=nullptr;
thread_local Icon* icon=nullptr;
bool high_resolution_icons=false;
unsigned measured_items=0;
unsigned frame_depth=0;
void* popup_context=nullptr;
owned::CampPopupSink popup_sink=nullptr;
struct Drag { HWND window=nullptr; List list=List::stash; int mode=0,category=0; float grab=0; } drag;
std::array<float,6> wheel_remainder{};
struct Rail { owned::camp::Rect track,thumb; Scroll scroll; bool horizontal=false; int maximum=0; };
Rail rail(List list) noexcept {
    Rail b;if(!host.scroll)return b;
    b.scroll=host.scroll(host.context,list);
    const auto value=owned::camp::frontend::rail(layout,list,b.scroll);
    b.track=value.track;b.thumb=value.thumb;b.horizontal=value.horizontal;b.maximum=value.maximum;
    return b;
}
POINT virtual_point(POINT p) noexcept {return {LONG(std::floor(p.x/layout.unit+layout.left)),LONG(std::floor(p.y/layout.unit))};}
void move_drag(POINT physical) noexcept {
    if(!drag.window)return;
    const auto b=rail(drag.list);
    if(!Current()||mode!=drag.mode||b.scroll.category!=drag.category||GetCapture()!=drag.window||!b.maximum){owned::camp_inventory_cancel_drag();return;}
    const auto p=virtual_point(physical);
    if(host.scroll_to)host.scroll_to(host.context,drag.list,owned::camp::frontend::drag_position(
        {b.track,b.thumb,b.scroll.position,b.maximum,b.horizontal},p,drag.grab));

}
std::uint64_t now() noexcept {LARGE_INTEGER q{};if(!QueryPerformanceCounter(&q)){cost.valid=false;return 0;}return q.QuadPart;}
bool observe() noexcept {static const bool enabled=[] {char value[8]{};return GetEnvironmentVariableA("EI_RENDERER_ATTRIBUTION",value,sizeof(value))&&value[0]=='1';}();return enabled;}
}
bool OwnsDraw() noexcept {return host.context&&(frame_depth||drawing!=Role::none);}
void RegisterTextures(bool high_resolution) noexcept {high_resolution_icons=high_resolution;owned::camp_inventory_embedded_owner(host.context,generation,high_resolution_icons);}
Icon::Icon(CI3DFigure* f,std::shared_ptr<const owned::PreparedCampIcon> v):previous(icon),figure(f),value(std::move(v)){icon=this;}
Icon::~Icon(){icon=previous;}
bool Active() noexcept {return host.context&&valid&&layout.wide;}
bool Current() noexcept {return Active()&&host.current&&host.current(host.context);}
const owned::camp::Layout& Layout() noexcept {return layout;}
owned::camp::Rect Stage() noexcept {
    return owned::camp::frontend::stage(layout,mode);
}
owned::camp::Rect Actions() noexcept {
    return owned::camp::frontend::actions(layout,mode);
}
owned::camp::Placement Uniform(Role r) noexcept {
    return owned::camp::frontend::uniform(layout,mode,r);
}
owned::camp::Placement PerkBand(float x,float y) noexcept {
    return owned::camp::frontend::perk_band(layout,x,y);
}
RECT Pixels(owned::camp::Rect r) noexcept {return owned::camp::frontend::pixels(layout,r);}
void Activate(Host value) noexcept {
    if(host.context)Deactivate(host.context);
    host=value;valid=true;art_failed=popup_failed=false;wheel_remainder={};++generation;
    layout=owned::camp::make_layout(float(width),float(height),float(frame));layout.wide=layout.wide&&requested;
}
void Deactivate(void* owner) noexcept {
    if(owner!=host.context)return;
    owned::camp_inventory_cancel_drag();owned::camp_inventory_retire_textures();
    host={};owned::camp_inventory_embedded_owner(nullptr,0,high_resolution_icons);valid=false;cards={};decoration={};decoration_pending=false;cost={};preview=nullptr;drawing=Role::none;
}
void BeginDraw(int value) noexcept {
    if(!host.context)return;
    if(mode!=value){owned::camp_inventory_cancel_drag();cards={};wheel_remainder={};}
    mode=value;
    const bool want=owned::camp::make_layout(float(width),float(height),float(frame)).wide&&requested;
    if(want!=layout.wide){owned::camp_inventory_retire_textures();layout=owned::camp::make_layout(float(width),float(height),float(frame));layout.wide=want;valid=true;}
    owned::camp_inventory_begin_icon_frame();
    decoration_pending=false;
}
void PublishDecoration(const owned::CampDecoration& value) noexcept {
    if(!Active())return;
    decoration=value;
    for(int i=0;i<6;++i){const auto b=rail(List(i));if(b.maximum&&decoration.thumb_count<decoration.thumbs.size())decoration.thumbs[decoration.thumb_count++]=Pixels(b.thumb);}
    decoration_pending=true;
}
void DrawDecorationBackground(CRenderer* renderer) {
    if(!Active()||!decoration_pending||!renderer)return;
    auto background=decoration;background.card_count=0;background.construction_icon_visible=false;
    renderer->GetD3D()->GetRendererFacade().Visit([&](owned::Renderer& backend){
        backend.queue_camp_decoration(background,false);return S_OK;
    });
    decoration.title_plate={};decoration.count=0;decoration.thumb_count=0;decoration.divider_count=0;
}
void FinishDraw2D(CRenderer* renderer) {
    if(!renderer)return;
    owned::CampDecoration frame;
    if(owned::camp_inventory_take_decoration(frame))
        renderer->GetD3D()->GetRendererFacade().Visit([&](owned::Renderer& backend){backend.queue_camp_decoration(frame);return S_OK;});
}
void SetCards(std::array<bool,3> value) noexcept {cards=value;}
Role Drawing() noexcept {return drawing;}
bool RailTrack(List list,owned::camp::Rect& track) noexcept {const auto b=rail(list);track=b.track;return b.maximum>0;}
bool Popup(owned::CampRenderRequest request,owned::CampPopupDraw draw,void* data){
    const bool complete=popup_sink&&popup_sink(popup_context,request,draw,data);
    static std::array<int,4> reported{{-1,-1,-1,-1}};
    static unsigned reportCount=0;
    const unsigned index=unsigned(request);
    if(index<reported.size()&&reported[index]!=int(complete)&&reportCount<64){
        ++reportCount;
        reported[index]=int(complete);
        GameJournal::Event("RENDER","camp_popup_stage","request=%u complete=%d role=%u",index,int(complete),unsigned(drawing));
    }
    if(!complete&&draw)owned::camp_inventory_popup_failed();
    return complete;
}
Scope::Scope(Role r) noexcept:previous(drawing){drawing=Active()?r:Role::none;}
Scope::~Scope(){drawing=previous;}
Frame::Frame(CRenderer* value) noexcept:renderer(value) {++frame_depth;}
Frame::~Frame(){
    if(frame_depth==1 && renderer) {
        try { renderer->GetD3D()->FlushC56CampFigures(); }
        catch(...) { owned::camp_inventory_popup_failed(); }
    }
    --frame_depth;
}
Preview::Preview(std::uint32_t color,bool active) noexcept:previous(preview),rgb(color),enabled(active){preview=this;}
Preview::~Preview(){preview=previous;}
ItemCost::ItemCost(bool measure) noexcept {if(measure&&Active()&&observe()){begin=now();if(begin)++measured_items;if(!cost.first_qpc)cost.first_qpc=begin;}}
ItemCost::~ItemCost(){if(begin){--measured_items;const auto end=now();++cost.item_calls;if(end>=begin){cost.item_wall_qpc+=end-begin;cost.item_max_wall_qpc=(std::max)(cost.item_max_wall_qpc,end-begin);cost.last_qpc=end;}else cost.valid=false;}}
MissCost::MissCost(bool miss) noexcept {if(miss&&measured_items)begin=now();}
MissCost::~MissCost(){if(begin){const auto end=now();++cost.miss_calls;if(end>=begin){cost.miss_wall_qpc+=end-begin;cost.miss_max_wall_qpc=(std::max)(cost.miss_max_wall_qpc,end-begin);}else cost.valid=false;}}
void FigureMiss() noexcept {if(measured_items)++cost.figure_misses;}
void ObserveGrid(bool vendor,int first,int count,int category,int columns,int rows) noexcept {
    if(!observe())return;
    if(cost.observed&&(cost.mode!=mode||cost.generation!=generation))cost.valid=false;
    cost.observed=true;cost.mode=mode;cost.generation=generation;
    auto& g=cost.grids[vendor?1:0];
    if(g.observed&&(g.first!=first||g.count!=count||g.category!=category||g.columns!=columns||g.rows!=rows))cost.valid=false;
    g={true,first,count,category,columns,rows};
}
bool Message(HWND window,UINT message,WPARAM wparam,LPARAM lparam) noexcept {
    if(owned::camp_inventory_input(window,message,wparam,lparam,false))return true;
    if(message!=WM_MOUSEWHEEL)return false;
    POINT p{LONG(short(LOWORD(lparam))),LONG(short(HIWORD(lparam)))};
    if(!ScreenToClient(window,&p))return false;
    return owned::camp_inventory_wheel(p,short(HIWORD(wparam)),LOWORD(wparam));
}
}

namespace owned {
using namespace OwnedRender::E4;
CampScrollCostSnapshot camp_inventory_take_scroll_cost() noexcept {return std::exchange(cost,CampScrollCostSnapshot{});}
void camp_inventory_retire_textures() noexcept {if(host.context&&host.retire)host.retire(host.context);decoration_pending=false;cards={};++generation;camp_inventory_embedded_owner(host.context,generation,high_resolution_icons);}
bool camp_inventory_take_decoration(CampDecoration& value) noexcept {
    if(!Active()||!camp::primary_output||!decoration_pending||frame_depth)return false;
    value=decoration;decoration_pending=false;return true;
}
void camp_inventory_art_failed() noexcept {art_failed=true;}
void camp_inventory_popup_failed() noexcept {cards={};popup_failed=true;decoration.card_count=0;}
void camp_inventory_popup_published(bool visible) noexcept {
    static int reported=-1;
    const int state=int(decoration.card_count)*2+int(visible);
    if(reported!=state){
        reported=state;
        GameJournal::Event("RENDER","camp_popup_published","cards=%u visible=%d",unsigned(decoration.card_count),int(visible));
    }
    if(!visible){cards={};decoration.card_count=0;}
}
void camp_inventory_output(unsigned w,unsigned h,unsigned size) noexcept {
    if(w==width&&h==height&&size==frame)return;
    camp_inventory_cancel_drag();camp_inventory_retire_textures();
    width=w;height=h;frame=size;
    layout=owned::camp::make_layout(float(width),float(height),float(frame));
    layout.wide=layout.wide&&requested;
    valid=host.context&&w&&h;
}
void camp_inventory_set_wide(bool wide) noexcept {if(wide!=requested){camp_inventory_cancel_drag();cards={};requested=wide;}}
const char* camp_inventory_notice() noexcept {
    if(!host.context)return nullptr;
    if(!valid)return "Camp layout unavailable: reopen camp after output resize";
    if(!requested)return "Camp: original UI comparison";
    if(!layout.wide)return nullptr;
    if(popup_failed)return "Camp popup unavailable";
    if(art_failed)return "Camp artwork unavailable";
    return layout.compact?"Camp: compact 16:10 grid":"Camp: wide grid";
}
bool camp_inventory_preview_specular_rgb(std::uint32_t& rgb) noexcept {
    if(!Active()||!preview||!preview->enabled||!camp::primary_output)return false;
    rgb=preview->rgb;return true;
}
void camp_inventory_cancel_drag() noexcept {const HWND window=drag.window;drag={};if(window&&GetCapture()==window)ReleaseCapture();}
void camp_inventory_set_popup_sink(void* context,CampPopupSink sink,ID3D11Device*) noexcept {popup_context=context;popup_sink=sink;}
bool camp_inventory_wheel(POINT physical,short delta,UINT) noexcept {
    if(!Current()||!focus||controls)return false;
    const auto p=virtual_point(physical);
    for(int i=0;i<6;++i){const auto list=List(i);const auto b=rail(list);if(!b.scroll.visible)continue;
        const auto area=i<3?layout.grid(i!=0):i==3?layout.belt:i==4?layout.learned:layout.available;
        if(!area.contains(float(p.x),float(p.y)))continue;
        wheel_remainder[i]-=float(delta)/WHEEL_DELTA;const int steps=int(wheel_remainder[i]);wheel_remainder[i]-=steps;
        if(steps&&host.scroll_to){host.scroll_to(host.context,list,std::clamp(b.scroll.position+steps,0,b.maximum));if(host.wheel_sound)host.wheel_sound(host.context);}
        return true;
    }return false;
}
bool camp_inventory_input(HWND window,UINT message,WPARAM wparam,LPARAM lparam,bool renderer_controls) noexcept {
    controls=renderer_controls;
    if(message==WM_KILLFOCUS||(message==WM_ACTIVATEAPP&&!wparam)||(message==WM_ACTIVATE&&LOWORD(wparam)==WA_INACTIVE))focus=false;
    if(message==WM_SETFOCUS||(message==WM_ACTIVATEAPP&&wparam)||(message==WM_ACTIVATE&&LOWORD(wparam)!=WA_INACTIVE))focus=true;
    if(controls||!focus||!Current()||message==WM_CANCELMODE||message==WM_CAPTURECHANGED||message==WM_SIZE||message==WM_DESTROY||message==WM_NCDESTROY||(message==WM_KEYDOWN&&wparam==VK_F10)){
        camp_inventory_cancel_drag();return false;
    }
    POINT p{LONG(short(LOWORD(lparam))),LONG(short(HIWORD(lparam)))};
    if(drag.window){
        if(message==WM_MOUSEWHEEL)return true;
        if(message==WM_MOUSEMOVE){move_drag(p);return true;}
        if(message==WM_LBUTTONUP){move_drag(p);camp_inventory_cancel_drag();return true;}
        return false;
    }
    if(message!=WM_LBUTTONDOWN||GetCapture())return false;
    p=virtual_point(p);
    for(int i=0;i<6;++i){const auto b=rail(List(i));if(!b.maximum)continue;
        auto hit=b.track;if(b.horizontal){hit.top-=2.5f;hit.bottom+=2.5f;}else{hit.left-=10.f/3;hit.right+=10.f/3;}
        if(!hit.contains(float(p.x),float(p.y)))continue;
        const float v=float(b.horizontal?p.x:p.y),start=b.horizontal?b.thumb.left:b.thumb.top,length=b.horizontal?b.thumb.width():b.thumb.height();
        SetCapture(window);if(GetCapture()!=window)return false;
        drag={window,List(i),mode,b.scroll.category,v>=start&&v<start+length?v-start:length*.5f};
        move_drag({LONG(short(LOWORD(lparam))),LONG(short(HIWORD(lparam)))});return true;
    }return false;
}
}

namespace owned {
using namespace OwnedRender::E4;
int camp_inventory_blit(const RECT& destination, const RECT& source, CampBlitPiece (&pieces)[kCampBlitPieces]) noexcept {
    return camp::frontend::SurfaceMapper{layout,mode,drawing,Active(),true,cards,width,height}.blit(destination,source,pieces);
}
bool camp_inventory_clear(const RECT& source, RECT& destination) noexcept {
    return camp::frontend::SurfaceMapper{layout,mode,drawing,Active(),true,cards,width,height}.clear(source,destination);
}


}

namespace OwnedRender::E4 {
std::shared_ptr<const owned::PreparedCampIcon> BoundIcon(CI3DFigure* figure) {return icon&&icon->figure==figure?icon->value:nullptr;}
}
