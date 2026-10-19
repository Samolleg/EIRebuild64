#pragma once
#include "camp_inventory.h"
#include <algorithm>
#include <cmath>
#include <utility>

// c74 presentation algorithms. Adapters supply source values and synchronous
// native drawing callbacks; no game object, address or borrowed array is kept.
namespace owned::camp::frontend {
enum class Role { none, root, background, info, help, stats, wagon, vendor,
                  quick, spells, perks, trade, repair, construction, comparison, blueprint };
enum class List { stash, spell_vendor, item_vendor, spells, learned, available };
struct Scroll { bool visible=false; int position=0,total=0,page=0,category=0; };
struct Rail {
    Rect track,thumb;
    int position=0,maximum=0;
    bool horizontal=false;
};
inline Rect stage(const Layout& layout,int mode) noexcept {
    if(mode==0)return layout.character;
    if(mode==1)return layout.content;
    if(mode==3||mode==5){auto r=layout.workbench;r.left+=10;r.right+=10;return r;}
    return layout.workspace;
}
inline Rect actions(const Layout& layout,int mode) noexcept {
    if(mode!=1)return layout.actions;
    const float middle=(layout.stash.left+layout.stash.right)*.5f;
    return {middle-90,510,middle+90,600};
}
inline Placement uniform(const Layout& layout,int mode,Role role) noexcept {
    switch(role){
    case Role::stats:return {{0,100,200,400},layout.stats};
    case Role::info:return {{600,100,800,400},layout.ordinary_info};
    case Role::help:return {{0,100,200,400},layout.result_info};
    case Role::comparison:return {{600,100,800,400},layout.comparison_info};
    case Role::blueprint:return {{600,100,800,400},layout.result_info};
    case Role::background:return {{0,100,800,500},{layout.left,0,layout.left+layout.width,600}};
    default:return {{200,100,600,500},stage(layout,mode)};
    }
}
inline Placement perk_band(const Layout& layout,float x,float y) noexcept {
    const auto& pane=x>=400?layout.available:y<245?layout.skills:layout.learned;
    const float left=(pane.left+15+pane.right-25-200)*.5f;
    if(x>=400)return {{400,100,600,500},{left,165,left+200,565}};
    if(y<245)return {{200,100,400,245},{left,165,left+200,310}};
    if(y<270)return {{200,245,400,265},{left,320,left+200,340}};
    const int row=std::clamp(int((y-270)/15),0,13);const float top=346+row*16.f;
    return {{200,270+row*15.f,400,285+row*15.f},{left,top,left+200,top+15}};
}
inline Rail rail(const Layout& layout,List list,Scroll scroll) noexcept {
    Rail bar;
    if(!scroll.visible||scroll.page<=0)return bar;
    bar.position=scroll.position;
    bar.maximum=(std::max)(0,scroll.total-scroll.page);
    if(list==List::spells){bar.horizontal=true;bar.track={layout.belt.left,146,layout.belt.right,156};}
    else if(list==List::learned||list==List::available){
        const bool learned=list==List::learned;const auto pane=learned?layout.learned:layout.available;
        bar.track={pane.right-(layout.compact?20.f:30.f),learned?346.f:200.f,
                   pane.right-(layout.compact?10.f:20.f),learned?569.f:550.f};
    }else{const auto grid=layout.grid(list!=List::stash);bar.track={grid.right+5.f/3,grid.top,grid.right+40.f/3,grid.bottom};}
    if(!bar.maximum)return bar;
    const float length=bar.horizontal?bar.track.width():bar.track.height();
    const float thumb=(std::min)(length,(std::max)(110.f/3,length*scroll.page/scroll.total));
    const float offset=(length-thumb)*bar.position/bar.maximum;
    bar.thumb=bar.horizontal?Rect{bar.track.left+offset,bar.track.top,bar.track.left+offset+thumb,bar.track.bottom}:
        Rect{bar.track.left,bar.track.top+offset,bar.track.right,bar.track.top+offset+thumb};
    return bar;
}
inline int drag_position(const Rail& bar,POINT p,float grab) noexcept {
    const float track=bar.horizontal?bar.track.width():bar.track.height();
    const float thumb=bar.horizontal?bar.thumb.width():bar.thumb.height();
    const float start=bar.horizontal?bar.track.left:bar.track.top;
    const float coordinate=float(bar.horizontal?p.x:p.y);
    if(track<=thumb)return 0;
    return int(std::lround(std::clamp((coordinate-start-(std::min)(grab,thumb))/(track-thumb),0.f,1.f)*bar.maximum));
}
inline int first_item(const Layout& layout,bool vendor,int count,float position) noexcept {
    const int columns=layout.grid_columns(vendor),rows=layout.grid_rows(vendor);
    const int maximum=(std::max)(0,(count+columns-1)/columns-rows)*columns;
    return (std::min)((std::max)(0,int(position)/columns*columns),maximum);
}
inline int reveal_item(const Layout& layout,bool vendor,int first,int target) noexcept {
    const int columns=layout.grid_columns(vendor),rows=layout.grid_rows(vendor);
    if(target<first){if(target>=0)first=target/columns*columns;}
    else if(target>=first+columns*rows)first=(target/columns-rows+1)*columns;
    return first;
}
inline int grid_pick(const Layout& layout,bool vendor,int first,POINT p) noexcept {
    const auto r=layout.grid(vendor);
    if(!r.contains(float(p.x),float(p.y)))return -1;
    return first+int((p.y-r.top)/layout.cell)*layout.grid_columns(vendor)+int((p.x-r.left)/layout.cell);
}
template<class Draw,class Thumb>
void draw_rail(const Rail& bar,Draw draw,Thumb thumb) {
    if(!bar.maximum)return;
    const Placement p{{0,0,12,100},bar.track};
    draw(0,p);draw(1,p);thumb(bar.thumb);
}
inline POINT inverse(Placement p,POINT point) noexcept {
    return {LONG(std::floor(p.source.left+(point.x-p.destination.left)*p.source.width()/p.destination.width())),
        LONG(std::floor(p.source.top+(point.y-p.destination.top)*p.source.height()/p.destination.height()))};
}
inline Placement grid_area(const Layout&,bool,int,Rect) noexcept;
template<class Source,class Contains>
int grid_hit(const Layout& layout,bool vendor,int count,POINT point,Source source,Contains contains) {
    for(int i=2;i<count;++i){
        const auto a=grid_area(layout,vendor,i,source(i));
        if(a.source.width()<=0||a.source.height()<=0||!a.destination.contains(float(point.x),float(point.y)))continue;
        if(contains(i,inverse(a,point)))return i;
    }
    return -1;
}
inline int idle_rows(float request,int lock,float& elapsed,float dt) noexcept {
    int rows=int(request);
    if(lock){
        if(elapsed==0){rows+=lock==1?-1:1;elapsed=.001f;}
        else {elapsed+=dt;if(elapsed>=.18f){rows+=lock==1?-1:1;elapsed=.001f;}}
    }
    return rows;
}
inline Placement grid_area(const Layout& layout,bool vendor,int index,Rect source) noexcept {
    return {source,index<2?Rect{}:index<8?layout.category(vendor,index-2):layout.grid(vendor)};
}
inline bool belt_visible(const Layout& layout,const Rect& r,bool equipment=false) noexcept {
    const auto contains=[&](const Rect& b){return r.left>=b.left-.01f&&r.right<=b.right+.01f&&r.top>=b.top-.01f&&r.bottom<=b.bottom+.01f;};
    return equipment?(contains(layout.weapons)||contains(layout.quick_items)):contains(layout.belt);
}
inline Placement item_placement(const Layout& layout,int mode,int first,Role drawing,
                                 float x,float y,float size,int part) noexcept {
    Placement p{{x-size*.5f,y-size*.5f,x+size*.5f,y+size*.5f},{}};
    if(part==0&&y<100){int slot=int(x/100);if(mode==0&&slot>=4)slot=11-slot;p.destination=layout.equipped(slot,first,mode==0);}
    else if(part==4||part==5)p.destination=layout.trade_cell(int((x-200)/100)+int((y-100)/100)*4);
    else {
        const auto u=uniform(layout,mode,part==9?drawing:Role::construction);
        const float s=u.destination.width()/u.source.width();
        p.destination={u.destination.left+(p.source.left-u.source.left)*s,u.destination.top+(p.source.top-u.source.top)*s,
            u.destination.left+(p.source.right-u.source.left)*s,u.destination.top+(p.source.bottom-u.source.top)*s};
    }
    return p;
}
template<class Item>
void draw_spell_items(const Layout& layout,int first,int count,float top,Item item) {
    for(int i=first;i<(std::min)(count,first+8);++i){
        const float x=50+i*100.f,y=top+50.f;
        item(i,x,y,Placement{{x-50,y-50,x+50,y+50},layout.equipped(i,first)},Rect{x-35,y-35,x+35,y+35});
    }
}
template<class Background,class Selected,class Scrollbar>
void draw_spell_frames(const Layout& layout,int first,int capacity,int count,int selected,
                       Background background,Selected selection,Scrollbar scrollbar) {
    for(int i=first;i<(std::min)(capacity,first+8);++i){
        const auto destination=layout.equipped(i,first);
        if(belt_visible(layout,destination))background(Placement{{0,0,100,100},destination});
    }
    const auto destination=layout.equipped(selected,first);
    selection(selected>=0&&selected<count&&belt_visible(layout,destination),destination);
    scrollbar();
}
template<class Background>
void draw_quick_backgrounds(const Layout& layout,int first,Background background) {
    for(int i=0;i<8;++i){const auto destination=layout.equipped(i,first,true);
        if(belt_visible(layout,destination,true))background(i<4?0:1,Placement{{0,0,100,100},destination});}
}
template<class Figure,class Area,class Position,class Action>
void draw_object_figures(const Layout& layout,int mode,int first,Role role,int count,
                         Figure figure,Area area,Position position,Action action) {
    for(int i=0;i<count;++i){
        if(role==Role::quick&&(i==6||i==11))continue;
        Placement p=uniform(layout,mode,role);
        if(role==Role::root){
            if(i<2){if(!action(i))continue;p={{0,400,200,500},actions(layout,mode)};}
            else if(i<10)p=area(i+2);
            else p={{0,0,800,600},{layout.left,0,layout.left+layout.width,600}};
        }else if(role==Role::quick&&i<12){
            const auto xy=position(i);const float x=xy.first,y=xy.second;const int slot=int(x/100);
            const auto r=layout.equipped(slot<4?slot:11-slot,first,true);
            if(!belt_visible(layout,r,true))continue;
            p={{x-50,y-50,x+50,y+50},r};
        }else if(role==Role::trade)p=area(i/2);
        else if(role==Role::perks){if(i<4)continue;const auto xy=position(i);p=perk_band(layout,xy.first,xy.second);}
        figure(i,p);
    }
}
struct GridCell { int index=0; Placement placement; Rect icon_clip; };
// Figures finish before either adapter acquires the text DC. Empty cells still
// receive the same background and frame as occupied ones.
template<class Cell,class Category,class Scrollbar>
void draw_grid(const Layout& layout,bool vendor,int first,float top,Cell cell,Category category,Scrollbar scrollbar) {
    const Rect native{50,top,150,top+100};
    const int capacity=layout.grid_columns(vendor)*layout.grid_rows(vendor);
    for(int i=0;i<capacity;++i)cell(GridCell{first+i,{native,layout.inventory_cell(vendor,i)},{65,top+15,135,top+85}});
    for(int i=0;i<6;++i)category(i,layout.category(vendor,i));
    scrollbar();
}
template<class Label>
void grid_labels(const Layout& layout,bool vendor,int first,int count,Label label) {
    const int columns=layout.grid_columns(vendor),capacity=columns*layout.grid_rows(vendor);
    for(int i=0;i<capacity&&first+i<count;++i){
        const LONG x=LONG((i%columns)*layout.cell),y=LONG((i/columns)*layout.cell);
        label(i,first+i,RECT{x+6,y+3,x+LONG(layout.cell)-6,y+23},
            RECT{x+6,y+LONG(layout.cell)-23,x+LONG(layout.cell)-6,y+LONG(layout.cell)-3});
    }
}
}

namespace owned::camp::frontend {
// Identical surface remap, perk/trade splitting and card occlusion in proxy
// and embedded mode. Values describe a single synchronous draw only.
struct SurfaceMapper {
    const Layout& layout;
    int mode;
    Role drawing;
    bool active,card_visible;
    const std::array<bool,3>& cards_drawn;
    unsigned output_width,output_height;
    static int role_card(Role role) noexcept {return role==Role::info?0:(role==Role::help||role==Role::blueprint)?1:role==Role::comparison?2:-1;}
    bool any_cards() const noexcept {return std::any_of(cards_drawn.begin(),cards_drawn.end(),[](bool v){return v;});}
    Rect context_plate(unsigned card) const noexcept {return card==0?layout.ordinary_info:card==1?layout.result_info:layout.comparison_info;}
    Placement mapped_uniform(Role role) const noexcept {return frontend::uniform(layout,mode,role);}
    Placement mapped_perk_band(float x,float y) const noexcept {return frontend::perk_band(layout,x,y);}
int map(const RECT& destination, const RECT& source, CampBlitPiece* pieces, unsigned capacity) noexcept {
    if (!camp::primary_output || camp::remapping_blit || !active || drawing == Role::none) return -1;
    if(role_card(drawing)>=0&&(!cards_drawn[role_card(drawing)]||!card_visible))return 0;
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
        band(mapped_perk_band(200,100));band(mapped_perk_band(200,245));band(mapped_perk_band(400,100));
        for(int row=0;row<14;++row)band(mapped_perk_band(200,270+row*15.f));
        return int(emitted);
    }
    if (!capacity) return 0;
    auto p = mapped_uniform(drawing);
    if (drawing == Role::root) p = native.left < 300 ? camp::Placement{{0, 400, 200, 500}, frontend::actions(layout,mode)} : camp::Placement{{600, 400, 800, 430}, layout.heading};
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

int blit(const RECT& destination, const RECT& source, CampBlitPiece (&pieces)[kCampBlitPieces]) noexcept {
    if(camp::remapping_blit)return -1;
    CampBlitPiece mapped[kCampMappedPieces];
    int count=map(destination,source,mapped,kCampMappedPieces);
    const bool occlude=any_cards()&&active&&camp::primary_output&&
        drawing!=Role::none&&role_card(drawing)<0;
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
        if(occlude)for(unsigned card=0;card<cards_drawn.size();++card)if(cards_drawn[card]){
            const auto plate=context_plate(card);
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
bool clear(const RECT& source, RECT& destination) noexcept {
    if (!camp::primary_output || !active || drawing == Role::none) return false;
    if (source.left == 0 && source.top == 0 && source.right == LONG(output_width) && source.bottom == LONG(output_height)) return false;
    const auto p = mapped_uniform(drawing);
    const auto& h = camp::composing ? camp::visual : camp::composition(layout, p);
    const float x1 = h.a * source.left + h.b * source.top + h.x;
    const float y1 = h.c * source.left + h.d * source.top + h.y;
    const float x2 = h.a * source.right + h.b * source.bottom + h.x;
    const float y2 = h.c * source.right + h.d * source.bottom + h.y;
    destination = {LONG(std::lround((std::min)(x1,x2))), LONG(std::lround((std::min)(y1,y2))),
                   LONG(std::lround((std::max)(x1,x2))), LONG(std::lround((std::max)(y1,y2)))};
    return true;
}


};
}
