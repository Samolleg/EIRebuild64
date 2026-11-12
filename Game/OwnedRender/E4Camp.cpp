#include "StdAfx.h"
#include "E4Camp.h"
#include <controller_input_state.h>
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
bool requested=true,valid=false;
owned::camp::frontend::CampSession session;
bool art_failed=false,popup_failed=false,decoration_pending=false;
int mode=0;
Role drawing=Role::none;
std::array<bool,3> cards{};
owned::CampDecoration decoration;
owned::CampScrollCostSnapshot cost;

thread_local Preview* preview=nullptr;
thread_local Icon* icon=nullptr;
bool high_resolution_icons=false;
unsigned measured_items=0;
unsigned frame_depth=0;
void* popup_context=nullptr;
owned::CampPopupSink popup_sink=nullptr;
struct Rail { owned::camp::Rect track,thumb; Scroll scroll; bool horizontal=false; int maximum=0; };
Rail rail(List list) noexcept {
    Rail b;if(!host.scroll)return b;
    b.scroll=host.scroll(host.context,list);
    const auto value=owned::camp::frontend::rail(layout,list,b.scroll);
    b.track=value.track;b.thumb=value.thumb;b.horizontal=value.horizontal;b.maximum=value.maximum;
    return b;
}
std::uint64_t now() noexcept {LARGE_INTEGER q{};if(!QueryPerformanceCounter(&q)){cost.valid=false;return 0;}return q.QuadPart;}
bool observe() noexcept {static const bool enabled=[] {char value[8]{};return GetEnvironmentVariableA("EI_RENDERER_ATTRIBUTION",value,sizeof(value))&&value[0]=='1';}();return enabled;}
}
bool OwnsDraw() noexcept {return host.context&&(frame_depth||drawing!=Role::none);}
void RegisterTextures(bool high_resolution) noexcept {high_resolution_icons=high_resolution;owned::camp_inventory_embedded_owner(host.context,session.generation,high_resolution_icons);}
Icon::Icon(CI3DFigure* f,std::shared_ptr<const owned::PreparedCampIcon> v):previous(icon),figure(f),value(std::move(v)){icon=this;}
Icon::~Icon(){icon=previous;}
bool Active() noexcept {return host.context&&valid&&layout.wide;}
bool Current() noexcept {return Active()&&host.current&&host.current(host.context);}
owned::camp::frontend::CampSession& Session() noexcept {return session;}
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
    host=value;session.activate(value.context);valid=true;art_failed=popup_failed=false;
    layout=owned::camp::make_layout(float(width),float(height),float(frame));layout.wide=layout.wide&&requested;
}
void Deactivate(void* owner) noexcept {
    if(owner!=host.context)return;
    owned::camp_inventory_cancel_drag();owned::camp_inventory_retire_textures();
    session.deactivate(owner);host={};owned::camp_inventory_embedded_owner(nullptr,0,high_resolution_icons);valid=false;cards={};decoration={};decoration_pending=false;cost={};preview=nullptr;drawing=Role::none;
}
void BeginDraw(int value) noexcept {
    if(!host.context)return;
    if(mode!=value){owned::camp_inventory_cancel_drag();cards={};}
    mode=value;
    const unsigned keys=(((GetKeyState(VK_CONTROL)&0x8000) || owned::controller_modifier_down(VK_CONTROL))?1u:0u)|(((GetKeyState(VK_MENU)&0x8000) || owned::controller_modifier_down(VK_MENU))?2u:0u)|(((GetKeyState(VK_SHIFT)&0x8000) || owned::controller_modifier_down(VK_SHIFT))?4u:0u);
    session.begin(value,keys,Current());
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
    if(cost.observed&&(cost.mode!=mode||cost.generation!=session.generation))cost.valid=false;
    cost.observed=true;cost.mode=mode;cost.generation=session.generation;
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
void camp_inventory_retire_textures() noexcept {if(host.context&&host.retire)host.retire(host.context);decoration_pending=false;cards={};session.retire_input();++session.generation;camp_inventory_embedded_owner(host.context,session.generation,high_resolution_icons);}
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
void camp_inventory_set_wide(bool wide) noexcept {if(wide!=requested){camp_inventory_cancel_drag();session.retire_input();cards={};requested=wide;}}
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
void camp_inventory_cancel_drag() noexcept {camp::frontend::cancel_drag(session);}
void camp_inventory_set_popup_sink(void* context,CampPopupSink sink,ID3D11Device*) noexcept {popup_context=context;popup_sink=sink;}
bool camp_inventory_wheel(POINT physical,short delta,UINT) noexcept {
    return camp::frontend::wheel(session,layout,camp::frontend::virtual_point(layout,physical),delta,Current(),
        [](List list){return host.scroll?host.scroll(host.context,list):Scroll{};},
        [](List list,Scroll scroll,int steps){
            const int next=std::clamp(scroll.position+steps,0,(std::max)(0,scroll.total-scroll.page));
            if(host.scroll_to&&next!=scroll.position){host.scroll_to(host.context,list,next);
                if(unsigned(list)<3&&host.wheel_sound)host.wheel_sound(host.context);}
        });
}
bool camp_inventory_input(HWND window,UINT message,WPARAM wparam,LPARAM lparam,bool renderer_controls) noexcept {
    return camp::frontend::input(session,layout,mode,Current(),window,message,wparam,lparam,renderer_controls,
        [](List list){return host.scroll?host.scroll(host.context,list):Scroll{};},
        [](List list,int position){if(host.scroll_to)host.scroll_to(host.context,list,position);});
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
