#include "StdAfx.h"
#include "E4Camp.h"
#include <controller_input_state.h>
#include "E1Surface.h"
#include "model_preview_presentation.h"
#include "../I3DScreenCamp.h"
#include "../I3DMain.h"
#include "../SpellManager.h"
#include "../GameJournal.h"
#include "../dx11/Surface.h"
#include <map>
#include <exception>
#include "../dx11/RendererDX11.h"
#include "../ModRuntime.h"
#include "../ModPackage.h"
#include "../WorldClient.h"
#include "../TextureManager.h"

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
bool in_popup=false;
thread_local E4CampAccess::Content* content=nullptr;
owned::camp::frontend::CardPublication publication;
int result_help=0;
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
    const auto scroll=E4CampAccess::Scroll(screen,list);
    const auto bar=owned::camp::frontend::rail(Layout(),list,scroll);
    auto* owner=art(screen);
    owned::camp::frontend::draw_rail(bar,[&](int i,Placement p){
        auto* figure=owner->AddFigure();if(!figure){owned::camp_inventory_art_failed();return;}
        if(i==0)figure->MakeBar(32,100,0,0,12,100,14,14,114,114);
        else figure->MakeBarFrame2(24,100,0,0,12,100,1.5f,14,14,114,114);
        {auto transform=placed(p);figure->Draw(renderer);}
        owner->RemoveFigure(figure);
    },[](Rect){}); // Thumb is published once with the camp decoration snapshot.
}
}
void E4CampAccess::Activate(CI3DScreenCamp* owner){
    screen=owner;belt_first=0;previous_wide=false;previous_mode=-1;
    OwnedRender::E4::Activate({owner,[](void* p){auto* s=static_cast<CI3DScreenCamp*>(p);return s->GetI3D()->GetCurrentScreen()==s&&!s->GetI3D()->GetCapture();},&Scroll,&ScrollTo,
        [](void*){grid_text.clear();art_owners.clear();grids.clear();shown_cards={};publication.reset();},
        [](void* p){static_cast<CI3DScreenCamp*>(p)->GetI3D()->BeginFX("buttons\\camp\\scroll.wav");}});
    const auto* mod=ModRuntime::Current();
    RegisterTextures(mod&&mod->CombatSetting("JMVGraphics2"));
    owned::camp_inventory_embedded_texture_manager(theWorld.GetTextureManager());
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
    comparison.reset();blueprint.reset();shown_cards={};publication.reset();result_help=0;
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
    publication.begin();result_help=0;shown_cards={};show_blueprint=empty_construction=false;
    SetCards(shown_cards);Popup(owned::CampRenderRequest::invalidate_popup,nullptr,nullptr);
    if(previous_wide!=Active()||previous_mode!=int(owner->eScreenMode)){
        owner->pPerks->CheckRanges();owner->pPerks->RedrawPerks();
        previous_wide=Active();previous_mode=int(owner->eScreenMode);
    }
}
E4CampAccess::Content::Content(CI3DObjectItemInfo* value,owned::camp::frontend::HoverKind type,bool visible,int arg)
    :info(value),previous(content),kind(type),present(visible),argument(arg),exceptions(std::uncaught_exceptions()){content=this;}
E4CampAccess::Content::~Content(){
    content=previous;
    if((!previous||previous->info!=info)&&exceptions==std::uncaught_exceptions())Hover(info,kind,present,argument);
}
void E4CampAccess::Hover(CI3DObjectItemInfo* info,owned::camp::frontend::HoverKind kind,bool present,int argument){
    if(!screen||!Active())return;
    if(info==screen->pItemInfo)Session().hover.publish(kind,present,argument);
    if(info==screen->pConstrInfo)result_help=kind==owned::camp::frontend::HoverKind::help&&present?argument:0;
}
void E4CampAccess::Text(CI3DObjectItemInfo* info){
    const int modifiers=(((GetKeyState(VK_CONTROL)&0x8000) || owned::controller_modifier_down(VK_CONTROL))?1:0)|(((GetKeyState(VK_MENU)&0x8000) || owned::controller_modifier_down(VK_MENU))?2:0);
    if(info==comparison.get())publication.comparison_modifiers=modifiers;
    if(info==blueprint.get())publication.blueprint_modifiers=modifiers;
    if(screen&&info==screen->pConstrInfo)publication.result_modifiers=modifiers;
}
void E4CampAccess::Cards(CI3DScreenCamp* owner,CRenderer* renderer){
    if(owner!=screen||!Active())return;
    namespace front=owned::camp::frontend;
    if(!comparison){comparison=std::make_unique<CI3DObjectItemInfo>(owner->GetI3D(),owner);comparison->Init(600,100);}
    if(!blueprint){blueprint=std::make_unique<CI3DObjectItemInfo>(owner->GetI3D(),owner);blueprint->Init(600,100);}
    auto* info=owner->pConstrInfo;
    const int mode=int(owner->eScreenMode);
    const int modifiers=int(Session().hover.modifiers&3);
    CItem* selected=nullptr;
    if((mode==3||mode==5)&&info&&info->IsVisible()&&result_help){
        auto* party=mode==3?owner->pSpellsConstr:owner->pItemsConstr;
        if(party)selected=front::last_blueprint<CItem*>(party->allWagonItems.GetSize(),
            [&](int i){return party->allWagonItems[i];},
            [&](CItem* item){return mode==3?owner->IsItemPrototype(item):owner->IsItemInstruction(item);});
    }
    front::publish_constructor(publication,mode,info&&info->IsVisible(),result_help,
        info?info->GetItem():nullptr,selected,modifiers,
        [&](CItem* item){blueprint->SetItem(item);},[&]{return blueprint->GetItem();},
        [&](bool result){(result?info:blueprint.get())->FormTextInfo();},
        [](const char* reason){GameJournal::Event("RENDER","camp_card_omitted","reason=%s",reason);});
    const bool allowed=Session().allowed(Current());
    CItem* item=Session().hover.fresh(allowed)&&Session().hover.kind==front::HoverKind::item?owner->pMouseItem:
        publication.constructor==front::ConstructorCard::result?info->GetItem():nullptr;
    CItem* counterpart=nullptr;
    auto* person=owner->GetCurrentPers();
    if(item&&person&&allowed&&(Session().hover.modifiers&4)){
        CItem* weapon=person->dwCurrentWeapon<DWORD(person->weapons.GetSize())?person->weapons[person->dwCurrentWeapon]:nullptr;
        counterpart=front::comparison_item<CItem*>(item,item->GetTypeID(),weapon,person->armors.GetSize(),
            [&](int i)->CItem*{return person->armors[i];},
            [](CItem* armor){return static_cast<CItemArmor*>(armor)->GetArmorType();});
    }
    front::publish_comparison(publication,counterpart,modifiers,
        [&](CItem* value){comparison->SetItem(value);},[&]{return comparison->GetItem();},[&]{comparison->FormTextInfo();});
    shown_cards=front::visible_cards(Session().hover,allowed,owner->pItemInfo&&owner->pItemInfo->IsVisible(),
        info&&info->IsVisible(),publication.constructor,publication.comparison&&comparison->IsVisible());
    show_blueprint=shown_cards[1]&&publication.constructor==front::ConstructorCard::blueprint;
    empty_construction=shown_cards[1]&&publication.constructor==front::ConstructorCard::empty;
    if(!Popup(owned::CampRenderRequest::begin_interface,nullptr,nullptr)){shown_cards={};owned::camp_inventory_popup_failed();}
    SetCards(shown_cards);
    static CI3DScreenCamp* reportedOwner=nullptr;
    static int reportedItem=-2;
    static unsigned reportedState=~0u;
    const int itemId=owner->pMouseItem?owner->pMouseItem->GetID():-1;
    const unsigned state=(Session().hover.touched?1u:0u)|(allowed?2u:0u)|
        (shown_cards[0]?4u:0u)|(shown_cards[1]?8u:0u)|(shown_cards[2]?16u:0u);
    if(reportedOwner!=owner||reportedItem!=itemId||reportedState!=state){
        reportedOwner=owner;reportedItem=itemId;reportedState=state;
        const CPoint mouse=owner->GetI3D()->GetMousePos();
        GameJournal::Event("RENDER","camp_hover_card",
            "item=%d hover=%d current=%d cards=%u mouse=%d,%d capture=%d",
            itemId,int(Session().hover.touched),int(allowed),state>>2,mouse.x,mouse.y,
            int(owner->GetI3D()->GetCapture()!=nullptr));
    }
    (void)renderer;
}
bool E4CampAccess::CardDraw(CI3DObjectItemInfo* info,CRenderer* renderer){
    const auto role=RoleOf(info);if(!Active()||in_popup||owned::camp::frontend::role_card(role)<0)return false;

    if(!owned::camp::frontend::card_visible(role,publication.constructor,shown_cards,Session().allowed(Current())))return true;
    Scope scope(role);auto transform=placed(Uniform(role));
    struct Args {CI3DObjectItemInfo* info;CRenderer* renderer;} args{info,renderer};
    Popup(owned::CampRenderRequest::draw_popup,[](void* p){auto& a=*static_cast<Args*>(p);BoolScope drawing_popup(in_popup);a.info->Draw(a.renderer);},&args);
    return true;
}
bool E4CampAccess::CardTextDraw(CI3DObjectItemInfo* info){
    const auto role=RoleOf(info);if(!Active()||in_popup||owned::camp::frontend::role_card(role)<0)return false;

    if(!owned::camp::frontend::card_visible(role,publication.constructor,shown_cards,Session().allowed(Current())))return true;
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
        belt_first=owned::camp::frontend::spell_first(belt_first,count);
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
    return {true,First(grid)/cols,static_cast<int>((grid->pSortedItems.GetSize()+cols-1)/cols),Layout().grid_rows(list!=List::stash),grid->nSortMode};
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
    CPoint point(physical);p->GetI3D()->ScreenToVirtual(point);
    const auto s=Input(p);
    inside=owned::camp::frontend::point_in_area(Layout(),s,index,Area(p,s.role,index),point,
        [&](int native,POINT q){return native>=0&&native<p->areas.GetSize()&&p->areas[native]->PtInArea(CPoint(q))!=FALSE;},true);
    return true;
}
int E4CampAccess::First(CI3DObjectInventoryItems* p){
    const int first=owned::camp::frontend::first_item(Layout(),vendor(p),p->pSortedItems.GetSize(),p->fCurPos[p->nSortMode]);
    p->fCurPos[p->nSortMode]=float(first);return first;
}
owned::camp::frontend::InputState E4CampAccess::Input(CI3DObject* p){
    owned::camp::frontend::InputState s;
    s.role=RoleOf(p);s.mode=int(screen->eScreenMode);s.first=belt_first;s.capacity=int(QI_MAX_SPELLS);
    s.areas=p->areas.GetSize();s.learned_start=4+RPG_NUM_ACTUAL_AUTO_SKILLS+RPG_NUM_ACTUAL_MANUAL_SKILLS+23;
    s.actions={{screen->eScreenMode!=ISM_WEAPONS||screen->bValidOK,screen->eScreenMode!=ISM_WEAPONS||screen->bValidCancel}};
    return s;
}
Placement E4CampAccess::Area(CI3DObject* p,Role role,int i){
    auto s=Input(p);s.role=role;
    const int native=owned::camp::frontend::area_template(s,i);
    if(native<0||native>=p->areas.GetSize())return {};
    return owned::camp::frontend::area(Layout(),s,i,rect(p->areas[native]->rect));
}
int E4CampAccess::Hit(CI3DObject* p,POINT physical){
    const auto role=RoleOf(p);if(!Active()||role==Role::none)return -2;
    CPoint point(physical);p->GetI3D()->ScreenToVirtual(point);
    if(role==Role::spells)Scroll(screen,List::spells);
    const auto s=Input(p);
    return owned::camp::frontend::hit(Layout(),s,point,
        [&](int i){return rect(p->areas[i]->rect);},
        [&](int i,POINT q){return p->areas[i]->PtInArea(CPoint(q))!=FALSE;});
}
int E4CampAccess::LearnedRows(CI3DObject* p){return Active()&&RoleOf(p)==Role::perks?14:10;}
bool E4CampAccess::Inside(CI3DObject* p,POINT point,bool& inside){
    if(!Active()||RoleOf(p)==Role::none)return false;
    CPoint local(point);p->GetI3D()->ScreenToVirtual(local);
    inside=owned::camp::frontend::inside(Layout(),RoleOf(p),int(screen->eScreenMode),local);return true;
}
bool E4CampAccess::MouseItem(CI3DObjectInventoryItems* p,POINT physical,int& slot,CItem*& item){
    if(!Active()||!inventory(p))return false;
    item=nullptr;if(!p->IsVisible())return true;
    CPoint point(physical);p->GetI3D()->ScreenToVirtual(point);const auto& l=Layout();const bool v=vendor(p);const auto r=l.grid(v);
    if(!r.contains(float(point.x),float(point.y)))return true;
    slot=p->ePartType==ISP_WAGON?IST_WAGON:p->ePartType==ISP_SPELLASSORT?IST_SPELL_ASSORT:IST_ITEM_ASSORT;
    PrepareGrid(static_cast<CI3DObjectInventoryItems*>(p));const int index=owned::camp::frontend::grid_pick(l,v,First(p),point);
    if(index>=0&&index<p->pSortedItems.GetSize())item=p->pSortedItems[index];return true;
}
bool E4CampAccess::Idle(CI3DObjectInventory* p){
    if(!Active()||!inventory(p))return false;
    PrepareGrid(static_cast<CI3DObjectInventoryItems*>(p));
    const int rows=owned::camp::frontend::idle_rows(p->fToScroll,p->nLock,p->fLockTime,p->GetFrameTime());
    p->fToScroll=p->fSpeed=0;p->fCurPos[p->nSortMode]+=rows*Layout().grid_columns(vendor(p));
    First(static_cast<CI3DObjectInventoryItems*>(p));return true;
}
bool E4CampAccess::SetToItem(CI3DObjectInventoryItems* p,int target){
    if(!Active()||!inventory(p))return false;
    p->fCurPos[p->nSortMode]=float(owned::camp::frontend::reveal_item(Layout(),vendor(p),First(p),target));
    p->fToScroll=p->fSpeed=0;First(p);return true;
}
bool E4CampAccess::DrawGrid(CI3DObjectInventoryItems* p,CRenderer* renderer){
    if(!Active()||!inventory(p))return false;
    PrepareGrid(p);const auto& l=Layout();const bool v=vendor(p);const int first=First(p),count=p->pSortedItems.GetSize();
    const int cols=l.grid_columns(v),rows=l.grid_rows(v);ObserveGrid(v,first,count,p->nSortMode,cols,rows);
    p->AnticipatePage(first,cols*rows);
    Scope scope(v?Role::vendor:Role::wagon);
    auto& cache=grids[p];cache.visible.clear();
    const Rect native{50,float(p->nTopHeight),150,float(p->nTopHeight+100)};
    auto* cell=art(p);
    if(!cell->figures.GetSize()){
        auto* background=cell->AddFigure();if(!background){owned::camp_inventory_art_failed();return true;}
        background->MakeBar(p->fDepth*2,100,native.left,native.top,native.right,native.bottom,14,14,114,114);
    }
    owned::camp::frontend::draw_grid(l,v,first,float(p->nTopHeight),[&](const owned::camp::frontend::GridCell& entry){
        auto transform=placed(entry.placement);
        cell->GetFigure(0)->Draw(renderer);
        p->figures[10]->SetRelativeToScreen(100,float(p->nTopHeight+50),p->fDepth);p->figures[10]->Draw(renderer);
        if(entry.index>=count||!p->pSortedItems[entry.index])return;
        SDrawItem item;item.pItem=p->pSortedItems[entry.index];item.fX=100;item.fY=float(p->nTopHeight+50);item.fZ=p->fDepth*1.5f;item.fPixSize=100;item.eFrom=p->ePartType;
        VisualScope clip(owned::camp::clipped_composition(l,entry.placement,entry.icon_clip),true);
        ItemCost timer(true);BoolScope grid_scope(in_grid);p->pScreen->DrawItem(renderer,item,&cache.visible);
    },[&](int i,Rect target){
        auto transform=placed({rect(p->areas[i+2]->rect),target});p->figures[i+2]->Draw(renderer);
    },[&]{rail_draw(!v?List::stash:screen->bIsSpellConstr&&p==screen->pAssortSpells?List::spell_vendor:List::item_vendor,renderer);});
    owned::camp_inventory_prioritize_icons(!v?0:screen->bIsSpellConstr&&p==screen->pAssortSpells?1:2,cache.visible,{});
    auto& text=grid_text[p];const auto r=l.grid(v);
    if(!text)text=std::make_unique<CI3DSurface>(p->GetI3D(),int(r.left),int(r.top),int(r.right),int(r.bottom));
    std::vector<std::pair<int,int>> labels(cols*rows,{1,-1});
    owned::camp::frontend::grid_labels(l,v,first,count,[&](int i,int index,RECT,RECT){
        auto* item=p->pSortedItems[index];if(!item)return;
        labels[i].first=item->GetCount();
        if(p->ePriceType!=ISK_TOTAL&&p->pScreen->CanClickOnItem(item,p->ePartType))
            labels[i].second=p->pScreen->GetItemPrice(item,p->ePriceType);
    });
    if(labels!=cache.text){
        text->GetDC();text->Clear();
        owned::camp::frontend::grid_labels(l,v,first,count,[&](int i,int,RECT quantity_rect,RECT price_rect){
            CString number;
            if(labels[i].first!=1){number.Format("%d",labels[i].first);text->DrawText(number,CRect(quantity_rect),1,RGB(228,215,167),DT_RIGHT);}
            if(labels[i].second>=0){number.Format("%d",labels[i].second);text->DrawText(number,CRect(price_rect),1,RGB(228,215,167),DT_CENTER);}
        });
        text->ReleaseDC();cache.text=std::move(labels);
    }
    return true;
}
bool E4CampAccess::DrawSpells(CI3DObjectSpellSlots* p,CRenderer* renderer){
    if(!Active()||RoleOf(p)!=Role::spells)return false;
    const auto scroll=Scroll(screen,List::spells);auto* person=screen->GetCurrentPers();
    if(!person)return true;
    Scope scope(Role::spells);
    auto* cell=art(p);
    if(!cell->figures.GetSize()){
        auto* background=cell->AddFigure();if(!background){owned::camp_inventory_art_failed();return true;}
        background->MakeBar(16,100,0,0,100,100,14,142,114,242);
        auto* frame=cell->AddFigure();if(!frame){cell->RemoveAllFigures();owned::camp_inventory_art_failed();return true;}
        frame->MakeBarFrame2(8,100,0,0,100,100,15,14,142,114,242);
    }
    owned::camp::frontend::draw_spell_items(Layout(),belt_first,scroll.total,float(p->nTopHeight),
        [&](int i,float x,float y,Placement a,Rect clip){
            SDrawItem item;item.pItem=person->spells[i];item.fX=x;item.fY=y;item.fZ=12;item.fPixSize=100;item.eFrom=ISP_CAMPSLOTS;
            VisualScope transform(owned::camp::clipped_composition(Layout(),a,clip),true);
            BoolScope grid_scope(in_grid);screen->DrawItem(renderer,item);
        });
    owned::camp::frontend::draw_spell_frames(Layout(),belt_first,int(QI_MAX_SPELLS),scroll.total,p->nCurrentSpell,
        [&](Placement a){auto transform=placed(a);cell->GetFigure(0)->Draw(renderer);cell->GetFigure(1)->Draw(renderer);},
        [&](bool visible,Rect target){
            p->figures[0]->Show(visible);if(!visible)return;
            float x,y,z;p->figures[0]->GetRelativeToScreen(x,y,z);
            auto transform=placed({{x-50,y-50,x+50,y+50},target});p->figures[0]->Draw(renderer);
        },[&]{rail_draw(List::spells,renderer);});
    return true;
}
bool E4CampAccess::SpellMouse(CI3DObjectSpellSlots* p,POINT physical,int& slot,CItem*& item){
    if(!Active()||RoleOf(p)!=Role::spells)return false;
    item=nullptr;slot=0;if(!p->IsVisible())return true;
    const int index=Hit(p,physical);
    if(index<0||index>=int(QI_MAX_SPELLS))return true;
    slot=IST_SPELL;
    auto* person=screen->GetCurrentPers();
    if(person&&index<(std::min)(int(person->spells.GetSize()),int(QI_MAX_SPELLS)))item=person->spells[index];
    return true;
}
void E4CampAccess::SpellCurrent(CI3DObjectSpellSlots* p,int index){
    if(!Active()||RoleOf(p)!=Role::spells)return;
    const auto value=Scroll(screen,List::spells);
    belt_first=owned::camp::frontend::spell_reveal(belt_first,index,value.total);
}
bool E4CampAccess::Draw(CI3DObject* p,CRenderer* renderer){
    const auto role=RoleOf(p);if(!Active()||role==Role::none)return false;
    Scope scope(role);
    if(role==Role::background){
        auto* holder=art(p);
        owned::camp::frontend::draw_background(Layout(),int(screen->eScreenMode),
            [&](Rect r,int columns,int rows){
                auto* figure=holder->AddFigure();if(!figure){owned::camp_inventory_art_failed();return;}
                figure->MakeTilingBar(32,100,r.left,r.top,r.right,r.bottom,14,14,114,114,columns,rows);
                {VisualScope raw({},false);figure->Draw(renderer);}holder->RemoveFigure(figure);
            },[&](int,Placement a){
                auto* figure=holder->AddFigure();if(!figure){owned::camp_inventory_art_failed();return;}
                const auto r=a.source;figure->MakeBar(28,100,r.left,r.top,r.right,r.bottom,14,14,114,114);
                {auto transform=placed(a);figure->Draw(renderer);}holder->RemoveFigure(figure);
            });
        if(std::any_of(shown_cards.begin(),shown_cards.end(),[](bool value){return value;})){
            struct Args {CI3DObject* holder;CRenderer* renderer;} args{holder,renderer};
            if(!Popup(owned::CampRenderRequest::clear_popup,[](void* context){
                auto& a=*static_cast<Args*>(context);auto* plate=a.holder->AddFigure();
                if(!plate){owned::camp_inventory_art_failed();return;}
                plate->MakeBar(32,100,0,0,200,300,14,14,114,114);
                owned::camp::frontend::draw_card_backgrounds(Layout(),shown_cards,[&](Placement p){
                    auto transform=placed(p);plate->Draw(a.renderer);
                });
                a.holder->RemoveFigure(plate);
            },&args)){shown_cards={};SetCards(shown_cards);}
        }
        owned::camp::frontend::BackgroundState state;state.mode=int(screen->eScreenMode);
        const auto visible=[](CI3DObject* object){return object&&object->IsVisible();};
        const bool spells=state.mode==2||state.mode==3;
        const bool shop=spells?screen->bIsSpellConstr:screen->bIsItemConstr;
        state.quick=visible(screen->pSlots);state.stats=visible(screen->pNameStats);
        state.perks=visible(screen->pPerks);state.wagon=visible(screen->pWagon);
        state.merchant=shop&&visible(spells?screen->pAssortSpells:screen->pAssortItems);
        state.trade=shop&&visible(spells?static_cast<CI3DObject*>(screen->pTradeSpells):screen->pTradeItems);
        state.construction=shop&&visible(spells?static_cast<CI3DObject*>(screen->pConstrSpells):screen->pConstrItems);
        state.repair=screen->bIsItemConstr&&visible(screen->pRepairItems);
        state.cards=shown_cards;state.empty_construction=empty_construction;
        PublishDecoration(owned::camp::frontend::background_decoration(Layout(),state,[](List list){
            return owned::camp::frontend::rail(Layout(),list,Scroll(screen,list));
        }));
        if(shown_cards[2])comparison->Draw(renderer);
        if(show_blueprint)blueprint->Draw(renderer);
        return true;
    }
    if(role==Role::quick){
        auto* holder=art(p);
        if(!holder->figures.GetSize())for(int i=0;i<2;++i){auto* background=holder->AddFigure();if(!background){holder->RemoveAllFigures();owned::camp_inventory_art_failed();return true;}
            background->MakeBar(16,100,0,0,100,100,14+i*128,14,114+i*128,114);
        }
        owned::camp::frontend::draw_quick_backgrounds(Layout(),belt_first,[&](int index,Placement p){
            auto transform=placed(p);holder->GetFigure(index)->Draw(renderer);
        });
    }
    owned::camp::frontend::draw_object_figures(Layout(),int(screen->eScreenMode),belt_first,role,p->figures.GetSize(),
        [&](int i,Placement a){auto transform=placed(a);p->figures[i]->Draw(renderer);},
        [&](int i){return Area(p,role,i);},[&](int i){float x,y,z;p->figures[i]->GetRelativeToScreen(x,y,z);return std::pair<float,float>{x,y};},
        [&](int i){return screen->eScreenMode!=ISM_WEAPONS||(i==0?screen->bValidOK:screen->bValidCancel);});
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
    const auto p=owned::camp::frontend::item_placement(Layout(),int(screen->eScreenMode),belt_first,Drawing(),
        item.fX,item.fY,item.fPixSize,int(item.eFrom));
    result=owned::camp::composition(Layout(),p);return true;
}
std::uint32_t E4CampAccess::PreviewRGB(CI3DItemScreen* owner,CItem* item){
    if(owner!=screen||!Active()||!item)return 0;
    if(item->GetTypeID()!=ITID_ITEM_WEAPON&&item->GetTypeID()!=ITID_ITEM_ARMOR)return 0;
    auto* spell=item->GetSpell(0);auto* phase=static_cast<CRotateItem*>(item->GetUserData());if(!spell||!phase)return 0;
    const int subtype=int(spell->GetMagicSubtype());
    static constexpr float palette[][3]={{1,0,0},{0,1,1},{0,1,0},{1,1,0},{1,1,0},{1,0,1},{1,0,1},{0,0,1}};
    if(subtype<0||subtype>=8)return 0;
    std::uint32_t rgb=0;
    if(!owned::advance_model_preview_glow(phase->fFlashVal,owner->GetFrameTime(),
        {palette[subtype][0],palette[subtype][1],palette[subtype][2]},rgb))return 0;
    return rgb;
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
    if (value->native_surface) {
        const bool bound = renderer->GetD3D()->BindTexture(0, nullptr, value->native_surface->Id()) != FALSE;
        if (!bound) owned::bridge_report_event("camp_icon_omitted", "D3D12 icon texture binding failed");
        return bound;
    }
    renderer->GetD3D()->BindTexture(0,value->srv.Get());
    return true;
}
}
