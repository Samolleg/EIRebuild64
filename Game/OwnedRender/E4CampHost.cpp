#include "StdAfx.h"
#include "E4Camp.h"
#include "../I3DScreenCamp.h"
#include "../I3DMain.h"
#include "../SpellManager.h"
#include "../GameJournal.h"
#include "../dx11/Surface.h"
#include <map>
#include "../dx11/RendererDX11.h"
#include "../ModRuntime.h"
#include "../ModPackage.h"

using namespace OwnedRender::E4;
using owned::camp::Rect;
using owned::camp::Placement;
using owned::camp::VisualScope;
namespace {
CI3DScreenCamp* screen=nullptr;
std::map<CI3DObject*,std::unique_ptr<CI3DSurface>> grid_text;
std::map<CI3DObject*,std::unique_ptr<CI3DObject>> art_owners;
int belt_first=0;
bool in_grid=false;
bool in_popup=false,ordinary_hover=false;
bool previous_wide=false;
int previous_mode=-1;
std::uint64_t grid_epoch=1;
struct GridCache {
    std::uint64_t epoch=0;
    int mode=-1,type=-1;
    std::vector<CItem*> items;
    std::vector<std::string> visible;
    std::vector<std::pair<int,int>> text;
};
std::map<CI3DObjectInventoryItems*,GridCache> grids;
int comparison_modifiers=-1,blueprint_modifiers=-1;
std::unique_ptr<CI3DObjectItemInfo> comparison,blueprint;
std::array<bool,3> shown_cards{};
bool show_blueprint=false,empty_construction=false;
struct BoolScope {bool& value;bool previous;explicit BoolScope(bool& flag):value(flag),previous(flag){value=true;}~BoolScope(){value=previous;}};
Rect rect(const CRect& r){return {float(r.left),float(r.top),float(r.right),float(r.bottom)};}
VisualScope placed(Placement p){return VisualScope(owned::camp::composition(Layout(),p),Active());}
bool vendor(CI3DObject* p){return E4CampAccess::RoleOf(p)==Role::vendor;}
CI3DObjectInventoryItems* inventory(CI3DObject* p){const auto r=E4CampAccess::RoleOf(p);return r==Role::wagon||r==Role::vendor?static_cast<CI3DObjectInventoryItems*>(p):nullptr;}
CI3DObject* art(CI3DObject* owner){
    auto& value=art_owners[owner];
    if(!value){value=std::make_unique<CI3DObject>(owner->GetI3D());value->SetTexture(owner->GetTexture(0));}
    return value.get();
}
void rail_draw(List list,CRenderer* renderer){
    Rect track;if(!RailTrack(list,track))return;
    auto* owner=art(screen);auto* bar=owner->AddFigure();
    if(!bar){owned::camp_inventory_art_failed();return;}
    bar->MakeBar(32,100,0,0,12,100,14,14,114,114);
    {auto transform=placed({{0,0,12,100},track});bar->Draw(renderer);}
    owner->RemoveFigure(bar);
}
}
void E4CampAccess::Activate(CI3DScreenCamp* owner){
    screen=owner;belt_first=0;previous_wide=false;previous_mode=-1;
    OwnedRender::E4::Activate({owner,[](void* p){auto* s=static_cast<CI3DScreenCamp*>(p);return s->GetI3D()->GetCurrentScreen()==s&&!s->GetI3D()->GetCapture();},&Scroll,&ScrollTo,
        [](void*){grid_text.clear();art_owners.clear();grids.clear();shown_cards={};comparison_modifiers=blueprint_modifiers=-1;},
        [](void* p){static_cast<CI3DScreenCamp*>(p)->GetI3D()->BeginFX("buttons\\camp\\scroll.wav");}});
    const auto* mod=ModRuntime::Current();
    RegisterTextures(mod&&mod->CombatSetting("JMVGraphics2"));
    auto* backend=owner->GetI3D()->GetRenderer()->GetD3D();
    owned::camp_inventory_set_popup_sink(backend,[](void* p,owned::CampRenderRequest request,owned::CampPopupDraw draw,void* data){
        return static_cast<CDX11*>(p)->SubmitC56CampPopup(request,draw,data);
    },backend->GetDevice().Get());
    GameJournal::Event("RENDER","camp_activate","source=CI3DScreenCamp owner=c56");
}
void E4CampAccess::Deactivate(CI3DScreenCamp* owner){
    if(screen!=owner)return;
    Popup(owned::CampRenderRequest::invalidate_popup,nullptr,nullptr);
    owned::camp_inventory_set_popup_sink(nullptr,nullptr);
    comparison.reset();blueprint.reset();shown_cards={};ordinary_hover=false;
    OwnedRender::E4::Deactivate(owner);screen=nullptr;belt_first=0;
    GameJournal::Event("RENDER","camp_deactivate","borrowed_state=retired");
}
Role E4CampAccess::RoleOf(CI3DObject* p){
    if(!screen||!p)return Role::none;
    if(p==screen)return Role::root;
    if(p==comparison.get())return Role::comparison;
    if(p==blueprint.get())return Role::blueprint;
    if(p==screen->pCampBG)return Role::background;
    if(p==screen->pItemInfo)return Role::info;
    if(p==screen->pConstrInfo)return Role::help;
    if(p==screen->pNameStats)return Role::stats;
    if(p==screen->pWagon)return Role::wagon;
    if(p==screen->pSlots)return Role::quick;
    if(p==screen->pSpells)return Role::spells;
    if(p==screen->pPerks)return Role::perks;
    // R08: optional shop fields are never read when the capability is absent.
    if(screen->bIsSpellConstr){
        if(p==screen->pAssortSpells)return Role::vendor;
        if(p==screen->pTradeSpells)return Role::trade;
        if(p==screen->pConstrSpells)return Role::construction;
    }
    if(screen->bIsItemConstr){
        if(p==screen->pAssortItems)return Role::vendor;
        if(p==screen->pTradeItems)return Role::trade;
        if(p==screen->pConstrItems)return Role::construction;
        if(p==screen->pRepairItems)return Role::repair;
    }
    return Role::none;
}
void E4CampAccess::BeginDraw(CI3DScreenCamp* owner){
    if(owner!=screen)return;
    BeginInput();
    OwnedRender::E4::BeginDraw(int(owner->eScreenMode));
    ordinary_hover=false;
    if(previous_wide!=Active()||previous_mode!=int(owner->eScreenMode)){
        owner->pPerks->CheckRanges();owner->pPerks->RedrawPerks();
        previous_wide=Active();previous_mode=int(owner->eScreenMode);
    }
}
void E4CampAccess::Hover(CI3DObjectItemInfo* info,bool value){if(screen&&info==screen->pItemInfo)ordinary_hover=value;}
void E4CampAccess::Cards(CI3DScreenCamp* owner,CRenderer* renderer){
    if(owner!=screen||!Active())return;
    if(!comparison){comparison=std::make_unique<CI3DObjectItemInfo>(owner->GetI3D(),owner);comparison->Init(600,100);}
    if(!blueprint){blueprint=std::make_unique<CI3DObjectItemInfo>(owner->GetI3D(),owner);blueprint->Init(600,100);}
    shown_cards={ordinary_hover&&Current(),false,false};show_blueprint=false;empty_construction=false;
    auto* info=owner->pConstrInfo;
    const bool construction=owner->eScreenMode==ISM_SPELLCONSTR||owner->eScreenMode==ISM_ITEMCONSTR;
    if(construction&&info&&info->IsVisible()){
        shown_cards[1]=true;
        if(!info->GetItem()){
            auto* party=owner->eScreenMode==ISM_SPELLCONSTR?owner->pSpellsConstr:owner->pItemsConstr;
            CItem* selected=nullptr;
            if(party)for(int i=0;i<party->allWagonItems.GetSize();++i){auto* item=party->allWagonItems[i];
                if(item&&(owner->eScreenMode==ISM_SPELLCONSTR?owner->IsItemPrototype(item):owner->IsItemInstruction(item)))selected=item;
            }
            if(selected||blueprint->GetItem())blueprint->SetItem(selected);
            show_blueprint=selected!=nullptr;empty_construction=!selected;
        }
    }
    CItem* item=shown_cards[0]?owner->pMouseItem:construction?info->GetItem():nullptr;
    CItem* counterpart=nullptr;auto* person=owner->GetCurrentPers();
    if(item&&person&&Current()&&(GetKeyState(VK_SHIFT)&0x8000)){
        if(item->GetTypeID()==ITID_ITEM_WEAPON&&person->dwCurrentWeapon<DWORD(person->weapons.GetSize()))counterpart=person->weapons[person->dwCurrentWeapon];
        else if(item->GetTypeID()==ITID_ITEM_ARMOR)for(int i=0;i<person->armors.GetSize();++i){auto* armor=person->armors[i];if(armor&&armor->GetArmorType()==static_cast<CItemArmor*>(item)->GetArmorType()){counterpart=armor;break;}}
    }
    if(counterpart||comparison->GetItem())comparison->SetItem(counterpart);
    shown_cards[2]=counterpart!=nullptr;
    const int modifiers=((GetKeyState(VK_CONTROL)&0x8000)?1:0)|((GetKeyState(VK_MENU)&0x8000)?2:0);
    if(counterpart&&modifiers!=comparison_modifiers){
        comparison->FormTextInfo();comparison_modifiers=modifiers;
    }
    if(show_blueprint&&modifiers!=blueprint_modifiers){
        blueprint->FormTextInfo();blueprint_modifiers=modifiers;
    }
    if(!Popup(owned::CampRenderRequest::begin_interface,nullptr,nullptr)){shown_cards={};owned::camp_inventory_popup_failed();}
    SetCards(shown_cards);
    // Keep the hover decision observable without logging each rendered frame.
    static CI3DScreenCamp* reportedOwner=nullptr;
    static int reportedItem=-2;
    static unsigned reportedState=~0u;
    const int itemId=owner->pMouseItem?owner->pMouseItem->GetID():-1;
    const unsigned state=(ordinary_hover?1u:0u)|(Current()?2u:0u)|
        (shown_cards[0]?4u:0u)|(shown_cards[1]?8u:0u)|(shown_cards[2]?16u:0u);
    if(reportedOwner!=owner||reportedItem!=itemId||reportedState!=state){
        reportedOwner=owner;reportedItem=itemId;reportedState=state;
        const CPoint mouse=owner->GetI3D()->GetMousePos();
        GameJournal::Event("RENDER","camp_hover_card",
            "item=%d hover=%d current=%d cards=%u mouse=%d,%d capture=%d",
            itemId,int(ordinary_hover),int(Current()),state>>2,mouse.x,mouse.y,
            int(owner->GetI3D()->GetCapture()!=nullptr));
    }
    // These independent visual owners are never inserted into the handler tree.
    (void)renderer;
}
bool E4CampAccess::CardDraw(CI3DObjectItemInfo* info,CRenderer* renderer){
    const auto role=RoleOf(info);if(!Active()||in_popup||role==Role::none)return false;
    const int index=role==Role::info?0:role==Role::comparison?2:1;
    if(!shown_cards[index]||(role==Role::help&&(show_blueprint||empty_construction)))return true;
    Scope scope(role);auto transform=placed(Uniform(role));
    struct Args {CI3DObjectItemInfo* info;CRenderer* renderer;} args{info,renderer};
    Popup(owned::CampRenderRequest::draw_popup,[](void* p){auto& a=*static_cast<Args*>(p);BoolScope drawing_popup(in_popup);a.info->Draw(a.renderer);},&args);
    return true;
}
bool E4CampAccess::CardTextDraw(CI3DObjectItemInfo* info){
    const auto role=RoleOf(info);if(!Active()||in_popup||role==Role::none)return false;
    const int index=role==Role::info?0:role==Role::comparison?2:1;
    if(!shown_cards[index]||(role==Role::help&&(show_blueprint||empty_construction)))return true;
    Scope scope(role);auto transform=placed(Uniform(role));
    Popup(owned::CampRenderRequest::draw_popup,[](void* p){BoolScope drawing_popup(in_popup);static_cast<CI3DObjectItemInfo*>(p)->Draw2D();},info);
    return true;
}
void E4CampAccess::CardText(CI3DScreenCamp* owner){
    if(owner!=screen||!Active())return;
    if(shown_cards[2])comparison->Draw2D();
    if(show_blueprint)blueprint->Draw2D();
}
OwnedRender::E4::Scroll E4CampAccess::Scroll(void* owner,List list){
    auto* s=static_cast<CI3DScreenCamp*>(owner);
    CI3DObjectInventoryItems* grid=nullptr;
    switch(list){
    case List::stash:grid=s->pWagon;break;
    case List::spell_vendor:if(s->bIsSpellConstr)grid=s->pAssortSpells;break;
    case List::item_vendor:if(s->bIsItemConstr)grid=s->pAssortItems;break;
    case List::spells:{
        auto* person=s->GetCurrentPers();
        const int count=person?(std::min)(int(person->spells.GetSize()),int(QI_MAX_SPELLS)):0;
        belt_first=std::clamp(belt_first,0,(std::max)(0,count-8));
        return {s->pSpells&&s->pSpells->IsVisible()!=FALSE,belt_first,count,8,0};
    }
    default:{
        auto* p=s->pPerks;if(!p||!p->IsVisible())return {};
        const bool learned=list==List::learned;
        return {true,learned?p->nPosReal:p->nPosAv,int(learned?p->perksReal.size():p->perksAv.size()),
                learned?LearnedRows(p):23,0};
    }}
    if(!grid||!grid->IsVisible())return {};
    PrepareGrid(grid);
    const int cols=Layout().grid_columns(list!=List::stash);
    return {true,First(grid)/cols,(grid->pSortedItems.GetSize()+cols-1)/cols,Layout().grid_rows(list!=List::stash),grid->nSortMode};
}
void E4CampAccess::ScrollTo(void* owner,List list,int pos){
    auto* s=static_cast<CI3DScreenCamp*>(owner);const auto value=Scroll(owner,list);
    if(!value.visible)return;
    pos=std::clamp(pos,0,(std::max)(0,value.total-value.page));
    if(list==List::spells){belt_first=pos;return;}
    if(list==List::learned||list==List::available){
        (list==List::learned?s->pPerks->nPosReal:s->pPerks->nPosAv)=pos;s->pPerks->RedrawPerks();return;
    }
    auto* p=list==List::stash?s->pWagon:list==List::spell_vendor?s->pAssortSpells:s->pAssortItems;
    p->fCurPos[p->nSortMode]=float(pos*Layout().grid_columns(list!=List::stash));p->fToScroll=p->fSpeed=0;
}
void E4CampAccess::BeginInput(){if(++grid_epoch==0){grids.clear();grid_epoch=1;}}
void E4CampAccess::PrepareGrid(CI3DObjectInventoryItems* p){
    auto& cache=grids[p];if(cache.epoch==grid_epoch&&cache.mode==p->nSortMode&&cache.type==p->nSortType)return;
    std::vector<CItem*> current;
    current.reserve(p->pAllItems->GetSize());
    for(int i=0;i<p->pAllItems->GetSize();++i){
        auto* item=(*p->pAllItems)[i];
        if((p->nSortType==0&&p->nSortMode==5)||p->GetItemSortMode(item)==p->nSortMode)
            current.push_back(item);
    }
    bool changed=current!=cache.items||p->pSortedItems.GetSize()!=int(current.size());
    if(!changed)for(int i=0;i<p->pSortedItems.GetSize();++i)
        if(p->pSortedItems[i]!=current[i]){changed=true;break;}
    if(changed){
        p->pSortedItems.RemoveAll();for(const auto& value:current)p->pSortedItems.Add(value);
        p->nSortedItems=p->pSortedItems.GetSize();cache.items=std::move(current);
    }
    cache.epoch=grid_epoch;cache.mode=p->nSortMode;cache.type=p->nSortType;
}
bool E4CampAccess::TipInside(CI3DObject* p,int index,POINT physical,bool& inside){
    if(!Active()||RoleOf(p)==Role::none)return false;
    inside=Hit(p,physical)==index;return true;
}
int E4CampAccess::First(CI3DObjectInventoryItems* p){
    const int cols=Layout().grid_columns(vendor(p)),rows=Layout().grid_rows(vendor(p));
    const int maximum=(std::max)(0,(p->pSortedItems.GetSize()+cols-1)/cols-rows);
    const int row=std::clamp(int(p->fCurPos[p->nSortMode])/cols,0,maximum);
    p->fCurPos[p->nSortMode]=float(row*cols);return row*cols;
}
Placement E4CampAccess::Area(CI3DObject* p,Role role,int i){
    constexpr int learned_start=4+RPG_NUM_ACTUAL_AUTO_SKILLS+RPG_NUM_ACTUAL_MANUAL_SKILLS+23;
    const bool learned=role==Role::perks&&i>=learned_start;
    const int template_index=learned?learned_start:i;
    if(template_index<0||template_index>=p->areas.GetSize())return {};
    const auto source=rect(p->areas[template_index]->rect);auto target=source;const auto& l=Layout();
    if(learned){const auto band=PerkBand(source.left,270+(i-learned_start)*15.f);const float dy=band.destination.top;return {source,{band.destination.left,dy,band.destination.right,dy+15}};}
    switch(role){
    case Role::root:
        if(i<2){const auto r=Actions();const float s=r.width()/200;target={r.left+source.left*s,r.top+(source.top-400)*s,r.left+source.right*s,r.top+(source.bottom-400)*s};}
        else if(i<4)target=l.rotation[i-2];
        else if(i<12){
            const int mode=i-4;
            if(mode+2>=screen->figures.GetSize()||!screen->figures[mode+2]->IsVisible())return {source,{}};
            int count=0,index=0;
            for(int m=0;m<8;++m)if(m+2<screen->figures.GetSize()&&screen->figures[m+2]->IsVisible()){
                if(m<mode)++index;++count;
            }
            target=l.mode_button(mode);
            const float width=target.width();
            target.left=(l.dock.left+l.dock.right-count*width)*.5f+index*width;
            target.right=target.left+width;
        }
        break;
    case Role::wagon:case Role::vendor:
        target=i<2?Rect{}:i<8?l.category(role==Role::vendor,i-2):l.grid(role==Role::vendor);break;
    case Role::quick:
        if(i<8){target=l.equipped(i<4?i:11-i,belt_first,true);break;}
        [[fallthrough]];
    default:{
        auto u=role==Role::perks?PerkBand(source.left,source.top):Uniform(role);
        const auto r=u.destination;const float sx=r.width()/u.source.width(),sy=r.height()/u.source.height();
        target={r.left+(source.left-u.source.left)*sx,r.top+(source.top-u.source.top)*sy,
                r.left+(source.right-u.source.left)*sx,r.top+(source.bottom-u.source.top)*sy};break;
    }
    case Role::spells:target=l.equipped(i,belt_first);break;
    case Role::trade:target=l.trade_cell(i);break;
    }
    return {source,target};
}
int E4CampAccess::Hit(CI3DObject* p,POINT physical){
    const auto role=RoleOf(p);if(!Active()||role==Role::none)return -2;
    CPoint point(physical);p->GetI3D()->ScreenToVirtual(point);
    constexpr int learned_start=4+RPG_NUM_ACTUAL_AUTO_SKILLS+RPG_NUM_ACTUAL_MANUAL_SKILLS+23;
    const int count=role==Role::perks?learned_start+14:p->areas.GetSize();
    if(role==Role::spells){
        Scroll(screen,List::spells);
        for(int i=belt_first;i<(std::min)(int(QI_MAX_SPELLS),belt_first+8);++i)if(Layout().equipped(i,belt_first).contains(float(point.x),float(point.y)))return i-belt_first;
        return -1;
    }
    for(int i=0;i<count;++i){
        if(role==Role::root&&i<2&&screen->eScreenMode==ISM_WEAPONS&&!(i==0?screen->bValidOK:screen->bValidCancel))continue;
        const auto a=Area(p,role,i);
        if(a.source.width()<=0||a.source.height()<=0||!a.destination.contains(float(point.x),float(point.y)))continue;
        const CPoint inverse(int(std::floor(a.source.left+(point.x-a.destination.left)*a.source.width()/a.destination.width())),
            int(std::floor(a.source.top+(point.y-a.destination.top)*a.source.height()/a.destination.height())));
        if(p->areas[role==Role::perks&&i>=learned_start?learned_start:i]->PtInArea(inverse))return i;
    }return -1;
}
int E4CampAccess::LearnedRows(CI3DObject* p){return Active()&&RoleOf(p)==Role::perks?14:10;}
bool E4CampAccess::Inside(CI3DObject* p,POINT point,bool& inside){
    if(!Active()||RoleOf(p)==Role::none)return false;
    inside=Hit(p,point)>=0;return true;
}
bool E4CampAccess::MouseItem(CI3DObjectInventoryItems* p,POINT physical,int& slot,CItem*& item){
    if(!Active()||!inventory(p))return false;
    item=nullptr;if(!p->IsVisible())return true;
    CPoint point(physical);p->GetI3D()->ScreenToVirtual(point);const auto& l=Layout();const bool v=vendor(p);const auto r=l.grid(v);
    if(!r.contains(float(point.x),float(point.y)))return true;
    slot=p->ePartType==ISP_WAGON?IST_WAGON:p->ePartType==ISP_SPELLASSORT?IST_SPELL_ASSORT:IST_ITEM_ASSORT;
    PrepareGrid(static_cast<CI3DObjectInventoryItems*>(p));const int index=First(p)+int((point.y-r.top)/l.cell)*l.grid_columns(v)+int((point.x-r.left)/l.cell);
    if(index>=0&&index<p->pSortedItems.GetSize())item=p->pSortedItems[index];return true;
}
bool E4CampAccess::Idle(CI3DObjectInventory* p){
    if(!Active()||!inventory(p))return false;
    PrepareGrid(static_cast<CI3DObjectInventoryItems*>(p));
    // Whole rows replace the native horizontal interpolation, retaining its category/sort producer.
    const int rows=int(p->fToScroll);p->fToScroll-=rows;p->fSpeed=0;
    p->fCurPos[p->nSortMode]+=rows*Layout().grid_columns(vendor(p));First(static_cast<CI3DObjectInventoryItems*>(p));return true;
}
bool E4CampAccess::SetToItem(CI3DObjectInventoryItems* p,int target){
    if(!Active()||!inventory(p))return false;
    const int cols=Layout().grid_columns(vendor(p)),rows=Layout().grid_rows(vendor(p));int first=First(p);
    if(target>=0){if(target<first)first=target/cols*cols;else if(target>=first+cols*rows)first=(target/cols-rows+1)*cols;}
    p->fCurPos[p->nSortMode]=float(first);p->fToScroll=p->fSpeed=0;First(p);return true;
}
bool E4CampAccess::DrawGrid(CI3DObjectInventoryItems* p,CRenderer* renderer){
    if(!Active()||!inventory(p))return false;
    PrepareGrid(static_cast<CI3DObjectInventoryItems*>(p));const auto& l=Layout();const bool v=vendor(p);const int first=First(p),count=p->pSortedItems.GetSize();
    const int cols=l.grid_columns(v),rows=l.grid_rows(v);ObserveGrid(v,first,count,p->nSortMode,cols,rows);
    Scope scope(v?Role::vendor:Role::wagon);
    auto& cache=grids[p];cache.visible.clear();
    for(int i=first;i<count&&i<first+cols*rows;++i)
        if(cache.items[i])cache.visible.emplace_back(cache.items[i]->GetI3DTextureName());
    // Queue only the current page; hidden categories must not compete with it.
    if(!cache.visible.empty())owned::camp_inventory_request_icon(cache.visible.front().c_str(),renderer->GetD3D()->GetDevice().Get());
    owned::camp_inventory_prioritize_icons(!v?0:screen->bIsSpellConstr&&p==screen->pAssortSpells?1:2,cache.visible,{});
    const Rect native{50,float(p->nTopHeight),150,float(p->nTopHeight+100)};
    auto* cell=art(p);
    if(!cell->figures.GetSize()){
        auto* background=cell->AddFigure();if(!background){owned::camp_inventory_art_failed();return true;}
        background->MakeBar(p->fDepth*2,100,native.left,native.top,native.right,native.bottom,14,14,114,114);
    }
    for(int i=0;i<cols*rows;++i){
        const auto r=l.inventory_cell(v,i);auto transform=placed({native,r});
        cell->GetFigure(0)->Draw(renderer);
        p->figures[10]->SetRelativeToScreen(100,float(p->nTopHeight+50),p->fDepth);p->figures[10]->Draw(renderer);
        if(first+i>=count||!p->pSortedItems[first+i])continue;
        SDrawItem item;item.pItem=p->pSortedItems[first+i];item.fX=100;item.fY=float(p->nTopHeight+50);item.fZ=p->fDepth*1.5f;item.fPixSize=100;item.eFrom=p->ePartType;
        VisualScope clip(owned::camp::clipped_composition(l,{native,r},{65,native.top+15,135,native.top+85}),true);
        ItemCost timer(true);BoolScope grid_scope(in_grid);p->pScreen->DrawItem(renderer,item);
    }
    for(int i=0;i<6;++i){auto transform=placed(Area(p,v?Role::vendor:Role::wagon,i+2));p->figures[i+2]->Draw(renderer);}
    rail_draw(!v?List::stash:screen->bIsSpellConstr&&p==screen->pAssortSpells?List::spell_vendor:List::item_vendor,renderer);
    auto& text=grid_text[p];const auto r=l.grid(v);
    if(!text)text=std::make_unique<CI3DSurface>(p->GetI3D(),int(r.left),int(r.top),int(r.right),int(r.bottom));
    std::vector<std::pair<int,int>> labels(cols*rows,{1,-1});
    for(int i=0;i<cols*rows&&first+i<count;++i){
        auto* item=p->pSortedItems[first+i];if(!item)continue;
        labels[i].first=item->GetCount();
        if(p->ePriceType!=ISK_TOTAL&&p->pScreen->CanClickOnItem(item,p->ePartType))
            labels[i].second=p->pScreen->GetItemPrice(item,p->ePriceType);
    }
    if(labels!=cache.text){
        text->GetDC();text->Clear();
        for(int i=0;i<int(labels.size());++i){
            CString number;const int x=int((i%cols)*l.cell),y=int((i/cols)*l.cell);
            if(labels[i].first!=1){number.Format("%d",labels[i].first);text->DrawText(number,CRect(x+6,y+3,x+int(l.cell)-6,y+23),1,RGB(228,215,167),DT_RIGHT);}
            if(labels[i].second>=0){number.Format("%d",labels[i].second);text->DrawText(number,CRect(x+6,y+int(l.cell)-23,x+int(l.cell)-6,y+int(l.cell)-3),1,RGB(228,215,167),DT_CENTER);}
        }
        text->ReleaseDC();cache.text=std::move(labels);
    }
    return true;
}
bool E4CampAccess::DrawSpells(CI3DObjectSpellSlots* p,CRenderer* renderer){
    if(!Active()||RoleOf(p)!=Role::spells)return false;
    const auto scroll=Scroll(screen,List::spells);auto* person=screen->GetCurrentPers();
    if(!person)return true;
    auto* cell=art(p);
    if(!cell->figures.GetSize()){
        auto* background=cell->AddFigure();if(!background){owned::camp_inventory_art_failed();return true;}
        background->MakeBar(16,100,0,0,100,100,14,142,114,242);
        auto* frame=cell->AddFigure();if(!frame){cell->RemoveAllFigures();owned::camp_inventory_art_failed();return true;}
        frame->MakeBarFrame2(8,100,0,0,100,100,15,14,142,114,242);
    }
    for(int i=belt_first;i<(std::min)(int(QI_MAX_SPELLS),belt_first+8);++i){
        {
            auto transform=placed({{0,0,100,100},Layout().equipped(i,belt_first)});
            cell->GetFigure(0)->Draw(renderer);cell->GetFigure(1)->Draw(renderer);
            if(i==p->nCurrentSpell&&i<scroll.total){p->figures[0]->Show(TRUE);p->figures[0]->SetRelativeToScreen(50,50,3);p->figures[0]->Draw(renderer);}
        }
        if(i>=scroll.total)continue;
        SDrawItem item;item.pItem=person->spells[i];item.fX=(i+.5f)*100;item.fY=50;item.fZ=6;item.fPixSize=100;item.eFrom=ISP_CAMPSLOTS;
        // The source row index is the actual logical slot, not the old seven-cell viewport.
        screen->DrawItem(renderer,item);
    }rail_draw(List::spells,renderer);return true;
}
bool E4CampAccess::SpellMouse(CI3DObjectSpellSlots* p,POINT physical,int& slot,CItem*& item){
    if(!Active()||RoleOf(p)!=Role::spells)return false;
    item=nullptr;slot=0;if(!p->IsVisible())return true;
    CPoint point(physical);p->GetI3D()->ScreenToVirtual(point);auto* person=screen->GetCurrentPers();if(!person)return true;
    const auto value=Scroll(screen,List::spells);
    for(int i=belt_first;i<(std::min)(value.total,belt_first+8);++i)if(Layout().equipped(i,belt_first).contains(float(point.x),float(point.y))){slot=IST_SPELL;item=person->spells[i];break;}
    return true;
}
void E4CampAccess::SpellCurrent(CI3DObjectSpellSlots* p,int index){if(Active()&&RoleOf(p)==Role::spells){if(index<belt_first)belt_first=index;else if(index>=belt_first+8)belt_first=index-7;Scroll(screen,List::spells);}}
bool E4CampAccess::Draw(CI3DObject* p,CRenderer* renderer){
    const auto role=RoleOf(p);if(!Active()||role==Role::none)return false;
    Scope scope(role);
    if(role==Role::background){
        auto* holder=art(p);auto* background=holder->AddFigure();
        if(background){background->MakeTilingBar(32,100,Layout().left,0,Layout().left+Layout().width,600,14,14,114,114,int(std::ceil(Layout().width/100)),6);background->Draw(renderer);holder->RemoveFigure(background);}
        const Rect panels[]={Layout().merchant,Layout().stash,Layout().skills,Stage(),Layout().stats,Layout().learned,Layout().available};
        for(int i=0;i<7;++i){const int mode=int(screen->eScreenMode);if((i==0&&mode<2)||(i==4&&mode>=2)||((i==2||i>=5)&&mode!=1)||(i==3&&(mode<2||mode==3||mode==5)))continue;
            auto* panel=holder->AddFigure();if(panel){const auto r=panels[i];panel->MakeBar(28,100,r.left,r.top,r.right,r.bottom,14,14,114,114);panel->Draw(renderer);holder->RemoveFigure(panel);}}
        if(std::any_of(shown_cards.begin(),shown_cards.end(),[](bool value){return value;})){
            struct Args {CI3DObject* holder;CRenderer* renderer;} args{holder,renderer};
            if(!Popup(owned::CampRenderRequest::clear_popup,[](void* context){
                auto& a=*static_cast<Args*>(context);auto* plate=a.holder->AddFigure();
                if(!plate){owned::camp_inventory_art_failed();return;}
                plate->MakeBar(32,100,0,0,200,300,14,14,114,114);
                for(int i=0;i<3;++i)if(shown_cards[i]){auto transform=placed({{0,0,200,300},i==0?Layout().ordinary_info:i==1?Layout().result_info:Layout().comparison_info});plate->Draw(a.renderer);}
                a.holder->RemoveFigure(plate);
            },&args)){shown_cards={};SetCards(shown_cards);}
        }
        owned::CampDecoration d;const auto& l=Layout();const int mode=int(screen->eScreenMode);
        d.pixel_unit=l.unit;d.title_plate=Pixels(l.title_plate);
        const auto visible=[](CI3DObject* object){return object&&object->IsVisible();};
        const auto panel=[&](Rect r,float corner){if(d.count<d.panels.size())d.panels[d.count++]={Pixels(r),corner};};
        const auto divider=[&](Rect span,float height){
            const float w=(std::min)(span.width(),800.f/3),x=(span.left+span.right-w)*.5f,y=(span.top+span.bottom)*.5f;
            if(d.divider_count<d.dividers.size())d.dividers[d.divider_count++]={Pixels({x,y-height*.5f,x+w,y+height*.5f}),false};
        };
        if(mode==0&&visible(screen->pSlots))panel(l.character,8);
        if(mode<2&&visible(screen->pNameStats))panel({l.stats.left-110.f/3,l.stats.top,l.stats.right+110.f/3,l.stats.bottom},12);
        if((mode==2&&screen->bIsSpellConstr&&visible(screen->pTradeSpells))||(mode==4&&screen->bIsItemConstr&&visible(screen->pTradeItems)))panel(Stage(),8);
        if((mode==3&&screen->bIsSpellConstr&&visible(screen->pConstrSpells))||(mode==5&&screen->bIsItemConstr&&visible(screen->pConstrItems))||(mode==6&&screen->bIsItemConstr&&visible(screen->pRepairItems)))panel(Stage(),8);
        if(mode==1&&visible(screen->pPerks)){panel({l.skills.left,161,l.skills.right,590},12);panel({l.available.left,161,l.available.right,590},12);divider({l.skills.left+24,310,l.skills.right-24,320},6);}
        const bool merchant=mode>=2&&((mode<=3&&screen->bIsSpellConstr&&visible(screen->pAssortSpells))||(mode>3&&screen->bIsItemConstr&&visible(screen->pAssortItems)));
        if(merchant)divider({l.merchant.left,l.category(true,0).bottom,l.merchant.right,l.grid(true).top},50.f/3);
        if(visible(screen->pWagon))divider({l.stash.left,l.category(false,0).bottom,l.stash.right,l.grid(false).top},50.f/3);
        if(mode!=1)divider({l.content.left+25,490,l.content.right-25,502},50.f/3);
        if(mode==2||mode==4||mode==6){
            const auto gutter=[&](float left,float right){const float w=70.f/3;if(right-left<w+16||d.divider_count>=d.dividers.size())return;const float x=(left+right)*.5f;d.dividers[d.divider_count++]={Pixels({x-w*.5f,215,x+w*.5f,335}),true};};
            if(merchant){Rect track;const bool rail=RailTrack(mode==2?List::spell_vendor:List::item_vendor,track);gutter(rail?track.right+10.f/3:l.grid(true).right,l.workspace.left);}
            if(visible(screen->pWagon))gutter(l.workspace.right,l.grid(false).left);
        }
        for(int i=0;i<3;++i)if(shown_cards[i])d.cards[d.card_count++]=Pixels(i==0?Layout().ordinary_info:i==1?Layout().result_info:Layout().comparison_info);
        if(empty_construction){const auto r=Layout().result_info;const float x=(r.left+r.right)*.5f,y=(r.top+r.bottom)*.5f;d.construction_icon=Pixels({x-40,y-40,x+40,y+40});d.construction_icon_visible=true;}
        PublishDecoration(d);
        if(shown_cards[2])comparison->Draw(renderer);
        if(show_blueprint)blueprint->Draw(renderer);
        return true;
    }
    if(role==Role::quick){
        auto* holder=art(p);
        if(!holder->figures.GetSize())for(int i=0;i<2;++i){auto* background=holder->AddFigure();if(!background){holder->RemoveAllFigures();owned::camp_inventory_art_failed();return true;}
            background->MakeBar(16,100,0,0,100,100,14+i*128,14,114+i*128,114);
        }
        for(int i=0;i<8;++i){auto transform=placed({{0,0,100,100},Layout().equipped(i,0,true)});holder->GetFigure(i<4?0:1)->Draw(renderer);}
    }
    for(int i=0;i<p->figures.GetSize();++i){
        auto a=Uniform(role);
        if(role==Role::root){
            if(i<2){if(screen->eScreenMode==ISM_WEAPONS&&!(i==0?screen->bValidOK:screen->bValidCancel))continue;a={{0,400,200,500},Actions()};}
            else if(i<10)a=Area(p,role,i+2);else continue;
        }else if(role==Role::quick&&i<12){
            if(i==6||i==11)continue;
            float x,y,z;p->figures[i]->GetRelativeToScreen(x,y,z);const int slot=int(x/100);
            a={{x-50,y-50,x+50,y+50},Layout().equipped(slot<4?slot:11-slot,0,true)};
        }else if(role==Role::trade)a=Area(p,role,i/2);
        else if(role==Role::perks){if(i<4)continue;float x,y,z;p->figures[i]->GetRelativeToScreen(x,y,z);a=PerkBand(x,y);}
        auto transform=placed(a);p->figures[i]->Draw(renderer);
    }
    if(role==Role::perks){rail_draw(List::learned,renderer);rail_draw(List::available,renderer);}
    // Preserve non-widget map previews owned by the original object.
    // Their implementation remains in CI3DObject after the handled figure pass.
    return true;
}
bool E4CampAccess::Blit(CI3DSurface* surface){
    if(!Active()||!surface->pSurface||!surface->IsVisible())return false;
    owned::CampBlitPiece pieces[owned::kCampBlitPieces];
    const RECT source{0,0,LONG(surface->pSurface->GetWidth()),LONG(surface->pSurface->GetHeight())};
    const int count=owned::camp_inventory_blit(surface->rect,source,pieces);if(count<0)return false;
    VisualScope no_second_transform({},false);
    struct Remapping {bool previous=owned::camp::remapping_blit;Remapping(){owned::camp::remapping_blit=true;}~Remapping(){owned::camp::remapping_blit=previous;}} remapping;
    DDBLTFX fx{};fx.dwSize=sizeof(fx);fx.ddckSrcColorkey.dwColorSpaceHighValue=fx.ddckSrcColorkey.dwColorSpaceLowValue=surface->colorKey;
    const DWORD flags=DDBLT_ASYNC|DDBLT_WAIT|(surface->HasFlag(BS_COLOR_KEY)?DDBLT_KEYSRCOVERRIDE:0);
    for(int i=0;i<count;++i){const auto& p=pieces[i];
        RECT source_piece=p.source,destination_piece=p.destination;
        const HRESULT result=surface->pSurface->BltToRenderSurface(&destination_piece,&source_piece,flags,surface->HasFlag(BS_COLOR_KEY)?&fx:nullptr);
        if(FAILED(result)){owned::camp_inventory_popup_failed();return true;}
    }return true;
}
bool E4CampAccess::Draw2D(CI3DObject* p){
    if(!Active()||RoleOf(p)==Role::none)return false;
    if(RoleOf(p)==Role::root)DrawDecorationBackground(p->GetI3D()->GetRenderer());
    if(auto* card=dynamic_cast<CI3DObjectItemInfo*>(p))if(CardTextDraw(card))return true;
    const auto found=grid_text.find(p);
    if(found==grid_text.end())return false;
    Scope scope(RoleOf(p));found->second->Draw();return true;
}
bool E4CampAccess::ItemPlacement(CI3DItemScreen* owner,const SDrawItem& item,owned::camp::Composition& result){
    if(owner!=screen||!Active()||in_grid)return false;
    const Rect source{item.fX-item.fPixSize*.5f,item.fY-item.fPixSize*.5f,item.fX+item.fPixSize*.5f,item.fY+item.fPixSize*.5f};
    Rect target;
    if(item.eFrom==ISP_CAMPSLOTS&&item.fY<100){int slot=int(item.fX/100);if(screen->eScreenMode==ISM_WEAPONS&&slot>=4)slot=11-slot;target=Layout().equipped(slot,belt_first,screen->eScreenMode==ISM_WEAPONS);}
    else if(item.eFrom==ISP_ITEMTRADE||item.eFrom==ISP_SPELLTRADE)target=Layout().trade_cell(int((item.fX-200)/100)+int((item.fY-100)/100)*4);
    else {
        const auto a=Uniform(item.eFrom==ISP_UNKNOWN?Drawing():Role::construction);const float s=a.destination.width()/a.source.width();
        target={a.destination.left+(source.left-a.source.left)*s,a.destination.top+(source.top-a.source.top)*s,a.destination.left+(source.right-a.source.left)*s,a.destination.top+(source.bottom-a.source.top)*s};
    }
    result=owned::camp::composition(Layout(),{source,target});return true;
}
std::uint32_t E4CampAccess::PreviewRGB(CI3DItemScreen* owner,CItem* item){
    if(owner!=screen||!Active()||!item)return 0;
    if(item->GetTypeID()!=ITID_ITEM_WEAPON&&item->GetTypeID()!=ITID_ITEM_ARMOR)return 0;
    auto* spell=item->GetSpell(0);auto* phase=static_cast<CRotateItem*>(item->GetUserData());if(!spell||!phase)return 0;
    const int subtype=int(spell->GetMagicSubtype());
    static constexpr float palette[][3]={{1,0,0},{0,1,1},{0,1,0},{1,1,0},{1,1,0},{1,0,1},{1,0,1},{0,0,1}};
    if(subtype<0||subtype>=8||!std::isfinite(phase->fFlashVal)||!std::isfinite(owner->GetFrameTime()))return 0;
    const double next=std::fmod(double(phase->fFlashVal)+owner->GetFrameTime()*5.0,2.0*3.141592653589793);
    phase->fFlashVal=float(next);const double strength=(std::sin(next)+1)*double(.15f);
    return (std::uint32_t(strength*palette[subtype][0]*255)<<16)|(std::uint32_t(strength*palette[subtype][1]*255)<<8)|std::uint32_t(strength*palette[subtype][2]*255);
}
namespace OwnedRender::E4 {
PreviewMaterial::PreviewMaterial(CI3DFigure* value,bool active,std::uint32_t rgb):figure(active?value:nullptr),previous(0){
    if(figure){previous=figure->GetDefaultSpecular();figure->SetDefaultSpecular((previous&0xff000000u)|rgb);}
}
PreviewMaterial::~PreviewMaterial(){if(figure)figure->SetDefaultSpecular(previous);}
}

namespace OwnedRender::E4 {
bool BindIcon(CI3DFigure* figure,CRenderer* renderer){
    const auto value=BoundIcon(figure);
    if(!value)return false;
    renderer->GetD3D()->BindTexture(0,value->srv.Get());
    return true;
}
}
