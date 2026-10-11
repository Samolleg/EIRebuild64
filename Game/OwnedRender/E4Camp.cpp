#include "E4Camp.h"
#include <utility>

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
unsigned measured_items=0;
unsigned frame_depth=0;
void* popup_context=nullptr;
owned::CampPopupSink popup_sink=nullptr;
struct Drag { HWND window=nullptr; List list=List::stash; int mode=0,category=0; float grab=0; } drag;
std::array<float,6> wheel_remainder{};
struct Rail { owned::camp::Rect track,thumb; Scroll scroll; bool horizontal=false; int maximum=0; };
Rail rail(List list) noexcept {
    Rail b;
    if(!host.scroll)return b;
    b.scroll=host.scroll(host.context,list);
    if(!b.scroll.visible||b.scroll.page<=0)return b;
    b.maximum=(std::max)(0,b.scroll.total-b.scroll.page);
    b.scroll.position=std::clamp(b.scroll.position,0,b.maximum);
    if(!b.maximum)return b;
    if(list==List::spells){b.horizontal=true;b.track={layout.belt.left,146,layout.belt.right,156};}
    else if(list==List::learned||list==List::available){
        const bool learned=list==List::learned;const auto p=learned?layout.learned:layout.available;
        b.track={p.right-(layout.compact?20.f:30.f),learned?346.f:200.f,
                 p.right-(layout.compact?10.f:20.f),learned?569.f:550.f};
    }else{const auto g=layout.grid(list!=List::stash);b.track={g.right+5.f/3,g.top,g.right+40.f/3,g.bottom};}
    const float length=b.horizontal?b.track.width():b.track.height();
    const float size=(std::min)(length,(std::max)(110.f/3,length*b.scroll.page/b.scroll.total));
    const float offset=(length-size)*b.scroll.position/b.maximum;
    b.thumb=b.horizontal?owned::camp::Rect{b.track.left+offset,b.track.top,b.track.left+offset+size,b.track.bottom}:
        owned::camp::Rect{b.track.left,b.track.top+offset,b.track.right,b.track.top+offset+size};
    return b;
}
POINT virtual_point(POINT p) noexcept {return {LONG(std::floor(p.x/layout.unit+layout.left)),LONG(std::floor(p.y/layout.unit))};}
void move_drag(POINT physical) noexcept {
    if(!drag.window)return;
    const auto b=rail(drag.list);
    if(!Current()||mode!=drag.mode||b.scroll.category!=drag.category||GetCapture()!=drag.window||!b.maximum){owned::camp_inventory_cancel_drag();return;}
    const auto p=virtual_point(physical);
    const float length=b.horizontal?b.track.width()-b.thumb.width():b.track.height()-b.thumb.height();
    const float offset=(b.horizontal?p.x-b.track.left:p.y-b.track.top)-drag.grab;
    if(length>0&&host.scroll_to)host.scroll_to(host.context,drag.list,int(std::lround(std::clamp(offset/length,0.f,1.f)*b.maximum)));
}
std::uint64_t now() noexcept {LARGE_INTEGER q{};if(!QueryPerformanceCounter(&q)){cost.valid=false;return 0;}return q.QuadPart;}
bool observe() noexcept {static const bool enabled=[] {char value[8]{};return GetEnvironmentVariableA("EI_RENDERER_ATTRIBUTION",value,sizeof(value))&&value[0]=='1';}();return enabled;}
}
bool Active() noexcept {return host.context&&valid&&layout.wide;}
bool Current() noexcept {return Active()&&host.current&&host.current(host.context);}
const owned::camp::Layout& Layout() noexcept {return layout;}
owned::camp::Rect Stage() noexcept {
    if(mode==0)return layout.character;
    if(mode==1)return layout.content;
    if(mode==3||mode==5){auto r=layout.workbench;r.left+=10;r.right+=10;return r;}
    return layout.workspace;
}
owned::camp::Rect Actions() noexcept {
    if(mode!=1)return layout.actions;
    const float middle=(layout.stash.left+layout.stash.right)*.5f;
    return {middle-90,510,middle+90,600};
}
owned::camp::Placement Uniform(Role r) noexcept {
    switch(r){
    case Role::stats:return {{0,100,200,400},layout.stats};
    case Role::info:return {{600,100,800,400},layout.ordinary_info};
    case Role::help:return {{0,100,200,400},layout.result_info};
    case Role::comparison:return {{600,100,800,400},layout.comparison_info};
    case Role::blueprint:return {{600,100,800,400},layout.result_info};
    case Role::background:return {{0,100,800,500},{layout.left,0,layout.left+layout.width,600}};
    default:return {{200,100,600,500},Stage()};
    }
}
owned::camp::Placement PerkBand(float x,float y) noexcept {
    const auto& p=x>=400?layout.available:y<245?layout.skills:layout.learned;
    const float left=(p.left+15+p.right-25-200)*.5f;
    if(x>=400)return {{400,100,600,500},{left,165,left+200,565}};
    if(y<245)return {{200,100,400,245},{left,165,left+200,310}};
    if(y<270)return {{200,245,400,265},{left,320,left+200,340}};
    const int row=std::clamp(int((y-270)/15),0,13);const float top=346+row*16.f;
    return {{200,270+row*15.f,400,285+row*15.f},{left,top,left+200,top+15}};
}
RECT Pixels(owned::camp::Rect r) noexcept {return {LONG(std::lround((r.left-layout.left)*layout.unit)),LONG(std::lround(r.top*layout.unit)),LONG(std::lround((r.right-layout.left)*layout.unit)),LONG(std::lround(r.bottom*layout.unit))};}
void Activate(Host value) noexcept {
    if(host.context)Deactivate(host.context);
    host=value;valid=true;art_failed=popup_failed=false;wheel_remainder={};++generation;
    layout=owned::camp::make_layout(float(width),float(height),float(frame));layout.wide=layout.wide&&requested;
}
void Deactivate(void* owner) noexcept {
    if(owner!=host.context)return;
    owned::camp_inventory_cancel_drag();owned::camp_inventory_retire_textures();
    host={};valid=false;cards={};decoration={};decoration_pending=false;cost={};preview=nullptr;drawing=Role::none;
}
void BeginDraw(int value) noexcept {
    if(!host.context)return;
    if(mode!=value){owned::camp_inventory_cancel_drag();cards={};wheel_remainder={};}
    mode=value;
    const bool want=owned::camp::make_layout(float(width),float(height),float(frame)).wide&&requested;
    if(want!=layout.wide){owned::camp_inventory_retire_textures();layout=owned::camp::make_layout(float(width),float(height),float(frame));layout.wide=want;valid=true;}
    decoration_pending=false;
}
void PublishDecoration(const owned::CampDecoration& value) noexcept {
    if(!Active())return;
    decoration=value;
    for(int i=0;i<6;++i){const auto b=rail(List(i));if(b.maximum&&decoration.thumb_count<decoration.thumbs.size())decoration.thumbs[decoration.thumb_count++]=Pixels(b.thumb);}
    decoration_pending=true;
}
void SetCards(std::array<bool,3> value) noexcept {cards=value;}
Role Drawing() noexcept {return drawing;}
bool RailTrack(List list,owned::camp::Rect& track) noexcept {const auto b=rail(list);track=b.track;return b.maximum>0;}
bool Popup(owned::CampRenderRequest request,owned::CampPopupDraw draw,void* data){
    if(popup_sink&&popup_sink(popup_context,request,draw,data))return true;
    if(draw)owned::camp_inventory_popup_failed();
    return false;
}
Scope::Scope(Role r) noexcept:previous(drawing){drawing=Active()?r:Role::none;}
Scope::~Scope(){drawing=previous;}
Frame::Frame() noexcept {++frame_depth;}
Frame::~Frame(){--frame_depth;}
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
void camp_inventory_retire_textures() noexcept {if(host.context&&host.retire)host.retire(host.context);decoration_pending=false;cards={};++generation;}
bool camp_inventory_take_decoration(CampDecoration& value) noexcept {
    if(!Active()||!camp::primary_output||!decoration_pending||frame_depth)return false;
    value=decoration;decoration_pending=false;return true;
}
void camp_inventory_art_failed() noexcept {art_failed=true;}
void camp_inventory_popup_failed() noexcept {cards={};popup_failed=true;decoration.card_count=0;}
void camp_inventory_popup_published(bool visible) noexcept {if(!visible){cards={};decoration.card_count=0;}}
void camp_inventory_output(unsigned w,unsigned h,unsigned size) noexcept {
    if(w==width&&h==height&&size==frame)return;
    camp_inventory_cancel_drag();camp_inventory_retire_textures();
    // As in c56, live resize requires a fresh authoritative activation.
    if(Active())valid=false;
    width=w;height=h;frame=size;
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
void camp_inventory_set_popup_sink(void* context,CampPopupSink sink) noexcept {popup_context=context;popup_sink=sink;}
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
static int card_role(Role r) noexcept {return r==Role::info?0:(r==Role::help||r==Role::blueprint)?1:r==Role::comparison?2:-1;}
static bool any_cards() noexcept {return std::any_of(cards.begin(),cards.end(),[](bool value){return value;});}
static int map_camp_blit(const RECT& destination, const RECT& source, CampBlitPiece* pieces, unsigned capacity) noexcept {
    if (!camp::primary_output || camp::remapping_blit || !Active() || drawing == Role::none) return -1;
    if(destination.right<=destination.left||destination.bottom<=destination.top||source.right<=source.left||source.bottom<=source.top)return 0;
    if(card_role(drawing)>=0&&(!cards[card_role(drawing)]||!true))return 0;
    if (drawing == Role::wagon || drawing == Role::vendor) return -1; // Owned grid text is already placed.
    const float unit = layout.unit;
    const camp::Rect native{layout.left + destination.left / unit, destination.top / unit,
                            layout.left + destination.right / unit, destination.bottom / unit};
    auto emit = [&](camp::Rect crop, camp::Rect target, unsigned index) {
        const auto x = [&](float v) { return source.left + LONG(std::lround((v - native.left) / native.width() * (source.right - source.left))); };
        const auto y = [&](float v) { return source.top + LONG(std::lround((v - native.top) / native.height() * (source.bottom - source.top))); };
        pieces[index].source = {x(crop.left), y(crop.top), x(crop.right), y(crop.bottom)};
        pieces[index].destination = {LONG(std::lround((target.left - layout.left) * unit)), LONG(std::lround(target.top * unit)),
                                     LONG(std::lround((target.right - layout.left) * unit)), LONG(std::lround(target.bottom * unit))};
    };
    if (drawing == Role::trade) {
        if (capacity < 16) return 0;
        unsigned emitted = 0;
        for (int i = 0; i < 16; ++i) {
            const float x = 200.f + (i % 4) * 100, y = 100.f + (i / 4) * 100;
            const camp::Rect crop{(std::max)(native.left,x),(std::max)(native.top,y),
                                  (std::min)(native.right,x+100),(std::min)(native.bottom,y+100)};
            if(crop.width() <= 0 || crop.height() <= 0) continue;
            const auto target=layout.trade_cell(i);const float scale=target.width()/100;
            emit(crop,{target.left+(crop.left-x)*scale,target.top+(crop.top-y)*scale,
                       target.left+(crop.right-x)*scale,target.top+(crop.bottom-y)*scale},emitted++);
        }
        return int(emitted);
    }
    if(drawing==Role::perks){
        if(capacity<kCampMappedPieces)return 0;
        unsigned emitted=0;
        const auto band=[&](camp::Placement p){
            const auto& s=p.source;
            const camp::Rect crop{(std::max)(native.left,s.left),(std::max)(native.top,s.top),
                (std::min)(native.right,s.right),(std::min)(native.bottom,s.bottom)};
            if(crop.width()<=0||crop.height()<=0)return;
            const float sx=p.destination.width()/s.width(),sy=p.destination.height()/s.height();
            emit(crop,{p.destination.left+(crop.left-s.left)*sx,p.destination.top+(crop.top-s.top)*sy,
                p.destination.left+(crop.right-s.left)*sx,p.destination.top+(crop.bottom-s.top)*sy},emitted++);
        };
        band(PerkBand(200,100));band(PerkBand(200,245));band(PerkBand(400,100));
        for(int row=0;row<14;++row)band(PerkBand(200,270+row*15.f));
        return int(emitted);
    }
    if (!capacity) return 0;
    auto p = Uniform(drawing);
    if (drawing == Role::root) p = native.left < 300 ? camp::Placement{{0, 400, 200, 500}, Actions()} : camp::Placement{{600, 400, 800, 430}, layout.heading};
    const auto transform = camp::composing ? camp::visual : camp::composition(layout,p);
    const float x1=transform.a*destination.left+transform.b*destination.top+transform.x;
    const float y1=transform.c*destination.left+transform.d*destination.top+transform.y;
    const float x2=transform.a*destination.right+transform.b*destination.bottom+transform.x;
    const float y2=transform.c*destination.right+transform.d*destination.bottom+transform.y;
    pieces[0].source=source;
    pieces[0].destination={LONG(std::lround((std::min)(x1,x2))),LONG(std::lround((std::min)(y1,y2))),
                           LONG(std::lround((std::max)(x1,x2))),LONG(std::lround((std::max)(y1,y2)))};
    return 1;
}

int camp_inventory_blit(const RECT& destination, const RECT& source, CampBlitPiece (&pieces)[kCampBlitPieces]) noexcept {
    if(camp::remapping_blit)return -1;
    CampBlitPiece mapped[kCampMappedPieces];
    int count=map_camp_blit(destination,source,mapped,kCampMappedPieces);
    const bool occlude=any_cards()&&Active()&&camp::primary_output&&
        drawing!=Role::none&&card_role(drawing)<0;
    if(count<0){
        if(!occlude)return -1;
        mapped[0]={source,destination}; count=1;
    }
    unsigned emitted=0;
    const auto append=[&](const CampBlitPiece& p,RECT r){
        if(r.right<=r.left||r.bottom<=r.top)return;
        const auto& d=p.destination; const auto& s=p.source;
        const auto x=[&](LONG v){return s.left+LONG(std::lround(double(v-d.left)*(s.right-s.left)/(d.right-d.left)));};
        const auto y=[&](LONG v){return s.top+LONG(std::lround(double(v-d.top)*(s.bottom-s.top)/(d.bottom-d.top)));};
        pieces[emitted++]={{x(r.left),y(r.top),x(r.right),y(r.bottom)},r};
    };
    // At most seventeen native bands, split four ways by each of three cards.
    // Crop sources from the original piece so successive cuts retain its scale.
    for(int i=0;i<count;++i){
        const auto& p=mapped[i]; const auto& d=p.destination;
        RECT regions[kCampCutRegions]{d};unsigned region_count=1;
        if(occlude)for(unsigned card=0;card<cards.size();++card)if(cards[card]){
            const auto plate=(card==0?layout.ordinary_info:card==1?layout.result_info:layout.comparison_info);
            const RECT bounds{LONG(std::lround((plate.left-layout.left)*layout.unit)),LONG(std::lround(plate.top*layout.unit)),
                LONG(std::lround((plate.right-layout.left)*layout.unit)),LONG(std::lround(plate.bottom*layout.unit))};
            RECT next[kCampCutRegions]{};unsigned next_count=0;
            const auto keep=[&](RECT r){if(r.left<r.right&&r.top<r.bottom)next[next_count++]=r;};
            for(unsigned j=0;j<region_count;++j){
                const auto& r=regions[j];
                const RECT cut{(std::max)(r.left,bounds.left),(std::max)(r.top,bounds.top),
                               (std::min)(r.right,bounds.right),(std::min)(r.bottom,bounds.bottom)};
                if(cut.left>=cut.right||cut.top>=cut.bottom){keep(r);continue;}
                keep({r.left,r.top,cut.left,r.bottom});keep({cut.right,r.top,r.right,r.bottom});
                keep({cut.left,r.top,cut.right,cut.top});keep({cut.left,cut.bottom,cut.right,r.bottom});
            }
            std::copy_n(next,next_count,regions);region_count=next_count;
        }
        for(unsigned j=0;j<region_count;++j)append(p,regions[j]);
    }
    return int(emitted);
}
bool camp_inventory_clear(const RECT& source, RECT& destination) noexcept {
    if (!camp::primary_output || !Active() || drawing == Role::none) return false;
    if (source.left == 0 && source.top == 0 && source.right == LONG(width) && source.bottom == LONG(height)) return false;
    const auto p = Uniform(drawing);
    const auto& h = camp::composing ? camp::visual : camp::composition(layout, p);
    const float x1 = h.a * source.left + h.b * source.top + h.x;
    const float y1 = h.c * source.left + h.d * source.top + h.y;
    const float x2 = h.a * source.right + h.b * source.bottom + h.x;
    const float y2 = h.c * source.right + h.d * source.bottom + h.y;
    destination = {LONG(std::lround((std::min)(x1,x2))), LONG(std::lround((std::min)(y1,y2))),
                   LONG(std::lround((std::max)(x1,x2))), LONG(std::lround((std::max)(y1,y2)))};
    return true;
}


}
