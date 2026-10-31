#include "StdAfx.h"
#include "SettlementMob.h"
#include "WorldServer.h"
#include "WorldClient.h"
#include "PlayerServer.h"
#include "PlayerClient.h"
#include "UnitServer.h"
#include "MapObjectServer.h"
#include "ItemManager.h"
#include "SpellManager.h"
#include "ScenarioTracker.h"
#include "I3DScreenCamp.h"
#include "I3DMain.h"
#include "DynamicSave.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "GameJournal.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <set>
#include <tuple>

namespace Settlement {
namespace {
enum class Place { Weapon, Quick, Armor, Spell, Wagon, Hoover, ItemStock, SpellStock };
bool Gear(Place p) { return p<=Place::Spell; }
bool Stock(Place p) { return p==Place::ItemStock || p==Place::SpellStock; }
struct Source {
    std::uint64_t identity=0;
    Place place=Place::Wagon;
    int actor=-1, slot=-1, count=0, id=-1;
    float hp=0;
    std::string payload;
};
struct Entry { CItem *item; Place place; int actor=-1, slot=-1; };
struct RandomRollback {
    SaveSchema::RandomState saved=SaveSchema::RandomState::Capture();
    bool committed=false;
    ~RandomRollback() { if (!committed) saved.Apply(); }
};
void Require(bool condition,const char *reason)
{
    if (!condition) throw std::runtime_error(reason);
}
template<class T> void Append(CAArray<T*,T*> &array,T *item)
{
    if (item) { array.Add(item); item->AddRef(); }
}
void Append(CParty &party,CItem *item)
{
    party.allWagonItems.Add(item); item->AddRef();
}
CItem *Clone(CItem *source,CWorld &owner,int id=-1)
{
    CSmartPtr<CItem> result=static_cast<CItem *>(owner.GetItemManager()->CreateData(source->GetTypeID(),id));
    Require(result!=nullptr,"cannot allocate merchant item");
    Require(source->Duplicate(result,owner.GetSpellManager())!=FALSE,"cannot copy merchant item");
    result->SetCount(source->GetCount());
    // The caller takes its reference before the temporary guard is released.
    result->AddRef(); return result;
}
struct Pin {
    CItem *item=nullptr;
    explicit Pin(CItem *p=nullptr):item(p) { if(item)item->AddRef(); }
    ~Pin() { if(item)item->Release(); }
    Pin(const Pin&)=delete; Pin& operator=(const Pin&)=delete;
    Pin(Pin&&other) noexcept:item(other.item) { other.item=nullptr; }
};
struct GearArrays {
    CPersonage value;
    void Swap(CPersonage &other) noexcept {
        value.weapons.Swap(other.weapons); value.items.Swap(other.items);
        value.armors.Swap(other.armors); value.spells.Swap(other.spells);
    }
};
void AddGear(CPersonage &p,CItem *item,Place place)
{
    switch(place) {
    case Place::Weapon: Append(p.weapons,static_cast<CItemWeapon*>(item)); break;
    case Place::Quick: Append(p.items,static_cast<CItemQuick*>(item)); break;
    case Place::Armor: Append(p.armors,static_cast<CItemArmor*>(item)); break;
    case Place::Spell: Append(p.spells,static_cast<CItemSpell*>(item)); break;
    default: throw std::runtime_error("invalid equipment destination");
    }
}
void PartyEntries(CParty &p,std::vector<Entry> &out,Place wagon)
{
    for (int i=0;i<p.allWagonItems.GetSize();++i) out.push_back({p.allWagonItems[i],wagon});
    for (int a=0;a<p.people.GetSize();++a) {
        auto &person=p.people[a];
        for(int i=0;i<person.weapons.GetSize();++i)out.push_back({person.weapons[i],Place::Weapon,a,i});
        for(int i=0;i<person.items.GetSize();++i)out.push_back({person.items[i],Place::Quick,a,i});
        for(int i=0;i<person.armors.GetSize();++i)out.push_back({person.armors[i],Place::Armor,a,i});
        for(int i=0;i<person.spells.GetSize();++i)out.push_back({person.spells[i],Place::Spell,a,i});
    }
}
}

struct TradeState {
    CParty party;
    CShop shop;
    CShop *owner=nullptr;
    std::uint64_t token=0, revision=0;
    std::string topic;
    std::map<std::uint64_t,Source> sources;
    std::vector<std::pair<int,std::uint64_t>> actors;
    std::vector<std::uint64_t> views;
    std::string recipes;
    int money=0;
    bool applying=false;
    TradeState() { Require(party.Create()!=FALSE,"cannot create merchant model"); }
};

// The only bridge to the legacy merchant kernel and the fixed unit slots. All
// access is on the local simulation/UI thread while the dialogue lock is held.
struct TradeAccess {
    static std::string Payload(CItem *item,bool ignoreHP)
    {
        const int count=item->nCount; const float hp=item->fpHP;
        struct Restore { CItem *p; int count; float hp; ~Restore(){p->nCount=count;p->fpHP=hp;} } restore{item,count,hp};
        item->nCount=1; if(ignoreHP)item->fpHP=0;
        CMemorySave base,bytes;
        item->CItem::StoreLocal(base); item->StoreLocal(bytes);
        Require(base.GetSize()>=sizeof(int) && bytes.GetSize()>=base.GetSize(),"invalid item payload");
        // Manager-local spell ID is not content identity across isolated copies.
        std::string result(bytes.GetBuffer(),bytes.GetSize());
        result.erase(base.GetSize()-sizeof(int),sizeof(int));
        auto type=item->GetTypeID(); auto subtype=item->GetSubtypeID();
        result.append(reinterpret_cast<const char*>(&type),sizeof(type));
        result.append(reinterpret_cast<const char*>(&subtype),sizeof(subtype));
        if (auto *spell=item->GetSpell(0)) {
            CMemorySave data; spell->StoreLocal(data);
            const auto kind=spell->GetSubtypeID();
            result.append(reinterpret_cast<const char*>(&kind),sizeof(kind));
            result.append(data.GetBuffer(),data.GetSize());
        }
        return result;
    }
    static std::string Recipes(CShop &shop)
    {
        CMemorySave bytes; shop.itemGenerator.Store(bytes); shop.spellGenerator.Store(bytes);
        return std::string(bytes.GetBuffer(),bytes.GetSize());
    }
    static std::vector<Entry> Real(CWorldServer &world,CPlayer &player,CShop &shop)
    {
        std::vector<Entry> result;
        for(int a=0;a<player.party.people.GetSize();++a) {
            auto *unit=world.GetUnit(player.party.people[a].nID);
            Require(unit && unit->GetOwner()==&player,"party member unavailable");
            for(int i=0;i<QI_MAX_WEAPONS;++i)if(unit->lpQWeapons[i])result.push_back({unit->lpQWeapons[i],Place::Weapon,a,i});
            for(int i=0;i<QI_MAX_ITEMS;++i)if(unit->lpQItems[i])result.push_back({unit->lpQItems[i],Place::Quick,a,i});
            for(int i=0;i<RPG_NUM_ARMOR_TYPES;++i)if(unit->lpArmors[i])result.push_back({unit->lpArmors[i],Place::Armor,a,i});
            for(int i=0;i<QI_MAX_SPELLS;++i)if(unit->lpQSpells[i])result.push_back({unit->lpQSpells[i],Place::Spell,a,i});
        }
        for(int i=0;i<player.party.allWagonItems.GetSize();++i)result.push_back({player.party.allWagonItems[i],Place::Wagon});
        for(POSITION p=player.items.GetHeadPosition();p;)result.push_back({player.items.GetNext(p),Place::Hoover});
        for(int i=0;i<shop.itemAssort.allWagonItems.GetSize();++i)result.push_back({shop.itemAssort.allWagonItems[i],Place::ItemStock});
        for(int i=0;i<shop.spellAssort.allWagonItems.GetSize();++i)result.push_back({shop.spellAssort.allWagonItems[i],Place::SpellStock});
        return result;
    }
    static std::map<std::uint64_t,Source> Capture(const std::vector<Entry> &entries)
    {
        std::map<std::uint64_t,Source> result;
        for(const auto &entry:entries) {
            auto *item=entry.item;
            Require(item && item->GetCount()>0 && item->GetCount()<=1000000,"invalid item count");
            Source value{item->GetRTID(),entry.place,entry.actor,entry.slot,item->GetCount(),item->GetID(),item->GetHP(),Payload(item,true)};
            Require(result.emplace(value.identity,std::move(value)).second,"item has multiple owners");
        }
        return result;
    }
    static void Check(TradeState &state,CWorldServer &world,CPlayer &player,const std::vector<Entry> &real)
    {
        Require(state.money==player.party.nMoney && state.party.nMoney==state.money,"merchant money changed");
        Require(state.actors.size()==std::size_t(player.party.people.GetSize()),"party changed");
        for(std::size_t i=0;i<state.actors.size();++i) {
            const auto actor=state.actors[i]; auto *unit=world.GetUnit(actor.first);
            Require(player.party.people[int(i)].nID==actor.first && unit && unit->GetRTID()==actor.second,"party identity changed");
            auto *view=theWorld.GetUnit(actor.first);
            Require(view && view->GetRTID()==state.views.at(i),"local party identity changed");
        }
        auto snapshot=Capture(real);
        Require(snapshot.size()==state.sources.size(),"merchant inventory changed");
        for(const auto &entry:snapshot) {
            const auto found=state.sources.find(entry.first);
            Require(found!=state.sources.end(),"merchant item identity changed");
            const auto &a=entry.second,&b=found->second;
            Require(a.place==b.place && a.actor==b.actor && a.slot==b.slot && a.count==b.count && a.hp==b.hp &&
                    a.id==b.id && a.payload==b.payload,"merchant item changed");
        }
        auto &shop=*state.owner;
        Require(!shop.bNeedUpdate && shop.nID==state.shop.nID && shop.bItem==state.shop.bItem && shop.bSpell==state.shop.bSpell &&
            !memcmp(shop.fKoeffs,state.shop.fKoeffs,sizeof(shop.fKoeffs)) && Recipes(shop)==state.recipes,"merchant terms changed");
    }
    static void Model(TradeState &state,CWorldServer &world,CPlayer &player)
    {
        auto real=Real(world,player,*state.owner);
        state.sources=Capture(real); state.money=player.party.nMoney;
        state.party.nMoney=state.money;
        state.party.people.SetSize(player.party.people.GetSize());
        for(int a=0;a<player.party.people.GetSize();++a) {
            auto &person=state.party.people[a];
            person.CopyFrom(player.party.people[a],&state.party); person.ClearItems();
            auto *unit=world.GetUnit(person.nID);
            person.stats=*static_cast<CUnitServer*>(unit->GetUnitSpecific())->GetRPGStats();
            person.dwCurrentWeapon=DWORD(-1);
            state.actors.emplace_back(person.nID,unit->GetRTID());
            auto *view=theWorld.GetUnit(person.nID);Require(view!=nullptr,"local party member missing");
            state.views.push_back(view->GetRTID());
            // Preview IDs must never alias an actual client object.
            Require(person.nID>=0 && person.nID<=INT_MAX-100000000 && !theWorld.GetUnit(person.nID+100000000),"merchant preview identity occupied");
        }
        for(const auto &entry:real) {
            CParty *destination=&state.party;
            if(entry.place==Place::ItemStock)destination=&state.shop.itemAssort;
            if(entry.place==Place::SpellStock)destination=&state.shop.spellAssort;
            CItem *copy=Clone(entry.item,*destination); Pin release(copy); copy->Release();
            copy->SetTradeOrigin(entry.item->GetRTID());
            if(Gear(entry.place)) {
                auto &person=state.party.people[entry.actor];
                if(entry.place==Place::Weapon && world.GetUnit(person.nID)->GetCurrentQWeapon()==entry.item)
                    person.dwCurrentWeapon=person.weapons.GetSize();
                AddGear(person,copy,entry.place);
            } else Append(*destination,copy);
        }
        state.recipes=Recipes(*state.owner);
    }
    static std::vector<Entry> Draft(TradeState &state,CI3DScreenCamp &screen,bool after,TradeAction action)
    {
        std::vector<Entry> entries;
        PartyEntries(state.party,entries,Place::Wagon);
        PartyEntries(state.shop.itemAssort,entries,Place::ItemStock);
        PartyEntries(state.shop.spellAssort,entries,Place::SpellStock);
        auto basket=[&](CParty *party,int role) {
            for(int i=0;i<party->allWagonItems.GetSize();++i) {
                auto *item=party->allWagonItems[i];
                const auto source=state.sources.find(item->TradeOrigin());
                const bool produced=after && action==TradeAction::Deal &&
                    ((role==5 && screen.eScreenMode==ISM_ITEMCONSTR)||(role==6 && screen.eScreenMode==ISM_SPELLCONSTR));
                if(produced) { entries.push_back({item,Place::Wagon}); continue; }
                Require(source!=state.sources.end(),"unidentified basket item");
                const bool stock=Stock(source->second.place);
                if(role==1)Require(source->second.place==Place::ItemStock,"invalid item purchase");
                if(role==2)Require(source->second.place==Place::SpellStock,"invalid spell purchase");
                if(role==3 || role==4 || role==7)Require(!stock && !Gear(source->second.place),"invalid sale or repair ownership");
                if(role==5 || role==6) {
                    auto *info=static_cast<CRotateItem*>(item->GetUserData());
                    Require((info && info->bIncludePrice)==stock,"constructor price ownership changed");
                }
                entries.push_back({item,stock?source->second.place:Place::Wagon});
            }
        };
        basket(screen.pItemsBuy,1); basket(screen.pSpellsBuy,2);
        basket(screen.pItemsSell,3); basket(screen.pSpellsSell,4);
        basket(screen.pItemsConstr,5); basket(screen.pSpellsConstr,6); basket(screen.pItemsRepair,7);
        std::map<std::uint64_t,std::int64_t> amounts;
        std::set<CItem*> seen;
        for(const auto &entry:entries) {
            auto *item=entry.item; const auto origin=item->TradeOrigin();
            Require(item && seen.insert(item).second && item->GetCount()>0 && item->GetCount()<=1000000,"invalid merchant basket");
            if(!origin) { Require(after && action==TradeAction::Deal &&
                (screen.eScreenMode==ISM_ITEMCONSTR || screen.eScreenMode==ISM_SPELLCONSTR),"unexpected merchant output"); continue; }
            const auto source=state.sources.find(origin);
            Require(source!=state.sources.end() && Payload(item,true)==source->second.payload,"merchant payload changed");
            if(!after) {
                if(Gear(entry.place))Require(source->second.place==entry.place && source->second.actor==entry.actor,"unpaid equipment");
                else if(Stock(entry.place))Require(source->second.place==entry.place,"unconfirmed stock transfer");
                else Require(!Gear(source->second.place) && !Stock(source->second.place),"unconfirmed inventory transfer");
            }
            Require(item->GetHP()==source->second.hp || (after && action==TradeAction::Deal && item->GetHP()==item->GetMaxHP()),"unexpected repair");
            amounts[origin]+=item->GetCount();
        }
        for(const auto &source:state.sources) {
            const auto amount=amounts[source.first];
            Require(after && action==TradeAction::Deal ? amount<=source.second.count : amount==source.second.count,"merchant quantity changed");
        }
        return entries;
    }

    template<class T,std::size_t N> static void SetSlots(T *(&slots)[N],const std::array<T*,N> &desired) noexcept
    {
        for(std::size_t i=0;i<N;++i)if(slots[i]!=desired[i]) {
            if(desired[i])desired[i]->AddRef();
            auto *old=slots[i]; slots[i]=desired[i]; if(old)old->Release();
        }
    }
    struct UnitItems {
        std::array<CItemWeapon*,QI_MAX_WEAPONS> weapons{};
        std::array<CItemQuick*,QI_MAX_ITEMS> quick{};
        std::array<CItemArmor*,RPG_NUM_ARMOR_TYPES> armor{};
        std::array<CItemSpell*,QI_MAX_SPELLS> spells{};
        DWORD current=DWORD(-1),wish=DWORD(-1);
        static UnitItems Read(CUnit &unit) {
            UnitItems result;
            std::copy(std::begin(unit.lpQWeapons),std::end(unit.lpQWeapons),result.weapons.begin());
            std::copy(std::begin(unit.lpQItems),std::end(unit.lpQItems),result.quick.begin());
            std::copy(std::begin(unit.lpArmors),std::end(unit.lpArmors),result.armor.begin());
            std::copy(std::begin(unit.lpQSpells),std::end(unit.lpQSpells),result.spells.begin());
            result.current=unit.dwCurrentWeapon;result.wish=unit.dwWishWeapon;return result;
        }
        void Apply(CUnit &unit) const noexcept {
            SetSlots(unit.lpQWeapons,weapons);SetSlots(unit.lpQItems,quick);
            SetSlots(unit.lpArmors,armor);SetSlots(unit.lpQSpells,spells);
            unit.dwCurrentWeapon=current;unit.dwWishWeapon=wish;
        }
        void Put(CItem *item,Place place,int slot) {
            switch(place) {
            case Place::Weapon: weapons.at(slot)=static_cast<CItemWeapon*>(item); break;
            case Place::Quick: quick.at(slot)=static_cast<CItemQuick*>(item); break;
            case Place::Armor: armor.at(slot)=static_cast<CItemArmor*>(item); break;
            case Place::Spell: spells.at(slot)=static_cast<CItemSpell*>(item); break;
            default: throw std::runtime_error("invalid live slot");
            }
        }
        void PreserveSelection(const UnitItems &old) {
            auto remap=[&](DWORD index) { if(index>=old.weapons.size())return DWORD(-1);
                auto found=std::find(weapons.begin(),weapons.end(),old.weapons[index]);
                return found==weapons.end()?DWORD(-1):DWORD(found-weapons.begin()); };
            current=remap(old.current);wish=remap(old.wish);
        }
    };
    struct UnitUndo {
        CUnit *unit;
        UnitItems slots;
        SRPGUnitStats stats;
        SRPGCommonStats common;
        SRPGUserUnitStats user;
        int attack,ai;
        float attackDistance,collisionDistance;
        explicit UnitUndo(CUnit *p):unit(p),slots(UnitItems::Read(*p)),
            stats(*static_cast<CUnitServer*>(p->GetUnitSpecific())->GetRPGStats()),
            common(*static_cast<CUnitServer*>(p->GetUnitSpecific())->GetMapObjectServer()->GetRPGStats()),
            user(p->rpgUserUnitStats),attack(static_cast<CUnitServer*>(p->GetUnitSpecific())->nCanAttackTime),
            ai(p->nAIClass),attackDistance(p->fpAttackDistance),collisionDistance(p->fpCollisionDistance) {}
        void Restore() noexcept {
            auto *server=static_cast<CUnitServer*>(unit->GetUnitSpecific());
            slots.Apply(*unit);*server->GetRPGStats()=stats;*server->GetMapObjectServer()->GetRPGStats()=common;
            unit->rpgUserUnitStats=user;server->nCanAttackTime=attack;
            unit->nAIClass=ai;unit->fpAttackDistance=attackDistance;unit->fpCollisionDistance=collisionDistance;
        }
    };
    struct Group {
        std::uint64_t origin;
        Place place;
        int actor,slot=-1,count=0;
        CItem *sample=nullptr,*item=nullptr,*peer=nullptr,*metadata=nullptr,*peerMetadata=nullptr;
        std::vector<CItem*> copies;
        CPtr<CItemSrcInfo> sourceInfo;
    };
    struct ValueUndo { CItem *item; int count; float hp; };
    static void ApplyValue(CItem *item,int count,float hp,std::vector<ValueUndo> &undo)
    {
        undo.push_back({item,item->nCount,item->fpHP});item->nCount=count;item->fpHP=hp;
    }
    static void Publish(TradeState &state,CWorldServer &world,CPlayer &player,const std::vector<Entry> &desired)
    {
        auto *client=theWorld.GetPlayer();
        Require(client && client->GetPlayer(),"local merchant peer unavailable");
        auto &peer=*client->GetPlayer();
        Require(peer.party.people.GetSize()==player.party.people.GetSize() && peer.party.nMoney==state.money,"local merchant party mismatch");
        for(std::size_t a=0;a<state.actors.size();++a)
            Require(peer.party.people[int(a)].nID==state.actors[a].first,"local merchant roster mismatch");
        std::vector<Pin> pins;
        std::map<std::uint64_t,CItem*> originals;
        for(const auto &entry:Real(world,player,*state.owner)) {
            pins.emplace_back(entry.item); originals.emplace(entry.item->GetRTID(),entry.item);
        }
        std::vector<Group> groups;
        for(const auto &entry:desired) {
            auto origin=entry.item->TradeOrigin();
            const auto source=state.sources.find(origin);
            Place place=entry.place;
            if(place==Place::Wagon && source!=state.sources.end() &&
                (source->second.place==Place::Hoover || Gear(source->second.place)))place=Place::Hoover;
            // World-owned quest items participate in the mission's native quest lookup.
            if(place==Place::Wagon && entry.item->GetTypeID()==ITID_ITEM_QUEST)place=Place::Hoover;
            auto found=groups.end();
            if(!Gear(place) && origin) found=std::find_if(groups.begin(),groups.end(),[&](const Group &g){
                return g.origin==origin && g.place==place && g.sample->GetHP()==entry.item->GetHP(); });
            if(found==groups.end()) {
                Group g{};g.origin=origin;g.place=place;g.actor=entry.actor;g.sample=entry.item;
                groups.push_back(std::move(g));found=groups.end()-1;
            }
            found->count+=entry.item->GetCount();found->copies.push_back(entry.item);
        }
        // Reserve original fixed slots first. Camp arrays are compact; mission slots are not.
        std::set<std::tuple<int,Place,int>> used;
        for(auto &g:groups)if(Gear(g.place)) {
            const auto old=state.sources.find(g.origin);
            if(g.place==Place::Armor)g.slot=static_cast<CItemArmor*>(g.sample)->GetArmorType();
            else if(old!=state.sources.end() && old->second.place==g.place && old->second.actor==g.actor)g.slot=old->second.slot;
            if(g.slot>=0)Require(used.emplace(g.actor,g.place,g.slot).second,"duplicate equipment slot");
        }
        for(auto &g:groups)if(Gear(g.place) && g.slot<0) {
            const int limit=g.place==Place::Weapon?QI_MAX_WEAPONS:g.place==Place::Quick?QI_MAX_ITEMS:QI_MAX_SPELLS;
            for(int i=0;i<limit;++i)if(used.emplace(g.actor,g.place,i).second){g.slot=i;break;}
            Require(g.slot>=0,"equipment slots exhausted");
        }
        CParty wagon,itemStock,spellStock,peerWagon;
        wagon.Create();itemStock.Create();spellStock.Create();peerWagon.Create();
        std::vector<std::unique_ptr<GearArrays>> metadata,peerMetadata;
        std::vector<UnitItems> slots(state.actors.size()),peerSlots(state.actors.size());
        std::vector<UnitUndo> unitUndo;
        for(const auto &actor:state.actors) {
            auto *unit=world.GetUnit(actor.first),*view=theWorld.GetUnit(actor.first);
            Require(unit && view,"live merchant actor missing");
            unitUndo.emplace_back(unit);
            metadata.emplace_back(std::make_unique<GearArrays>());peerMetadata.emplace_back(std::make_unique<GearArrays>());
        }
        std::set<CItem*> retained,metadataUsed;
        std::vector<bool> changedUnits(state.actors.size(),false);
        std::vector<ValueUndo> values;
        std::vector<POSITION> addedNodes,peerAddedNodes;
        std::vector<CItem*> removed,peerRemoved;
        std::map<std::uint64_t,Source> nextSources;
        CAssortmentGenerator nextItems(&state.owner->itemAssort),nextSpells(&state.owner->spellAssort);
        nextItems.CopyState(state.shop.itemGenerator);nextSpells.CopyState(state.shop.spellGenerator);
        auto nextRecipes=Recipes(state.shop);
        bool published=false;
        struct Undo {
            std::vector<ValueUndo>&values;std::vector<UnitUndo>&units;
            CPlayer &owner,&peer;std::vector<POSITION>&nodes,&peerNodes;bool &published;
            ~Undo() { if(published)return;
                for(auto p:nodes){auto *item=owner.items.GetAt(p);owner.items.RemoveAt(p);item->Release();}
                for(auto p:peerNodes){auto *item=peer.items.GetAt(p);peer.items.RemoveAt(p);item->Release();}
                for(auto i=values.rbegin();i!=values.rend();++i){i->item->nCount=i->count;i->item->fpHP=i->hp;}
                for(auto &unit:units)unit.Restore();
            }
        } undo{values,unitUndo,player,peer,addedNodes,peerAddedNodes,published};
        auto allocate=[&](CItem *sample,CWorld &owner,int id=-1) {
            auto *item=Clone(sample,owner,id);pins.emplace_back(item);item->Release();item->SetTradeOrigin(0);return item;
        };
        // Items that stay with the same owner keep their actual pointer and ObjectId.
        // Prefer the original location when one stack is split into several owners.
        std::stable_sort(groups.begin(),groups.end(),[&](const Group &a,const Group &b){
            auto kept=[&](const Group &g){auto i=state.sources.find(g.origin);return i!=state.sources.end() && i->second.place==g.place;};
            return kept(a)>kept(b);
        });
        for(auto &g:groups) {
            CWorld *owner=Gear(g.place)||g.place==Place::Hoover?static_cast<CWorld*>(&world):
                g.place==Place::ItemStock?static_cast<CWorld*>(&state.owner->itemAssort):
                g.place==Place::SpellStock?static_cast<CWorld*>(&state.owner->spellAssort):static_cast<CWorld*>(&player.party);
            auto old=originals.find(g.origin);
            if(old!=originals.end() && old->second->GetManager()==owner->GetItemManager() && retained.insert(old->second).second)g.item=old->second;
            else g.item=allocate(g.sample,*owner);
            ApplyValue(g.item,g.count,g.sample->GetHP(),values);
            if(Stock(g.place)) {
                Append(g.place==Place::ItemStock?itemStock:spellStock,g.item);
            } else {
                CWorld &peerOwner=Gear(g.place)||g.place==Place::Hoover?static_cast<CWorld&>(theWorld):static_cast<CWorld&>(peer.party);
                g.peer=static_cast<CItem*>(peerOwner.GetItemManager()->Find(g.item->GetID()));
                if(g.peer) {
                    Require(old!=originals.end() && old->second==g.item && g.peer->GetTypeID()==g.item->GetTypeID() &&
                        Payload(g.peer,true)==Payload(g.item,true),"local item identity collision");
                } else g.peer=allocate(g.item,peerOwner,g.item->GetID());
                pins.emplace_back(g.peer);ApplyValue(g.peer,g.count,g.item->GetHP(),values);
                if(Gear(g.place)) {
                    slots.at(g.actor).Put(g.item,g.place,g.slot);peerSlots.at(g.actor).Put(g.peer,g.place,g.slot);
                    CItem *partner=nullptr;
                    if(auto *info=dynamic_cast<CItemSrcInfo*>(g.item->GetUserData()))
                        partner=static_cast<CItem*>(player.party.GetItemManager()->Find(info->nSrcItemID));
                    if(partner && (retained.count(partner) || metadataUsed.count(partner) || Payload(partner,true)!=Payload(g.item,true)))partner=nullptr;
                    // Reuse a persistent wagon item when equipping it into the live world.
                    if(!partner && old!=originals.end() && old->second->GetManager()==player.party.GetItemManager() &&
                        !retained.count(old->second) && !metadataUsed.count(old->second))partner=old->second;
                    if(!partner)partner=allocate(g.item,player.party);
                    metadataUsed.insert(partner);
                    g.metadata=partner;pins.emplace_back(partner);ApplyValue(partner,g.count,g.item->GetHP(),values);
                    g.peerMetadata=static_cast<CItem*>(peer.party.GetItemManager()->Find(partner->GetID()));
                    if(g.peerMetadata)Require(g.peerMetadata->GetTypeID()==partner->GetTypeID() && Payload(g.peerMetadata,true)==Payload(partner,true),"persistent peer collision");
                    else g.peerMetadata=allocate(partner,peer.party,partner->GetID());
                    pins.emplace_back(g.peerMetadata);ApplyValue(g.peerMetadata,g.count,g.item->GetHP(),values);
                    AddGear(metadata[g.actor]->value,partner,g.place);AddGear(peerMetadata[g.actor]->value,g.peerMetadata,g.place);
                    g.sourceInfo=new CItemSrcInfo;g.sourceInfo->nSrcItemID=partner->GetID();
                } else if(g.place==Place::Wagon) {Append(wagon,g.item);Append(peerWagon,g.peer);}
            }
            Source source{g.item->GetRTID(),g.place,g.actor,g.slot,g.count,g.item->GetID(),g.item->GetHP(),Payload(g.item,true)};
            Require(nextSources.emplace(source.identity,std::move(source)).second,"duplicate trade result");
        }
        // Recalculate only affected living actors, retaining health, wounds and clocks.
        for(std::size_t a=0;a<unitUndo.size();++a) {
            auto &before=unitUndo[a]; auto *unit=before.unit;
            slots[a].PreserveSelection(before.slots);
            const auto peerBefore=UnitItems::Read(*theWorld.GetUnit(state.actors[a].first));
            peerSlots[a].PreserveSelection(peerBefore);
            bool changed=slots[a].weapons!=before.slots.weapons || slots[a].quick!=before.slots.quick ||
                slots[a].armor!=before.slots.armor || slots[a].spells!=before.slots.spells;
            for(const auto &g:groups)if(Gear(g.place) && g.actor==int(a)) {
                auto old=state.sources.find(g.origin);if(old!=state.sources.end() && old->second.hp!=g.item->GetHP())changed=true;
            }
            if(!changed)continue;
            changedUnits[a]=true;
            slots[a].Apply(*unit);
            auto *server=static_cast<CUnitServer*>(unit->GetUnitSpecific());
            server->rpgUnitStats.RecalcHitLocations(unit);unit->RecalcRPGStats();
            auto &common=*server->GetMapObjectServer()->GetRPGStats();
            common.ssHP.fpCurrent=(std::min)(before.common.ssHP.fpCurrent,common.ssHP.fpMax);
            common.ssMP.fpCurrent=(std::min)(before.common.ssMP.fpCurrent,common.ssMP.fpMax);
            common.nRegenerateCounter=before.common.nRegenerateCounter;
            for(int i=0;i<RPG_NUM_HIT_LOCATIONS;++i) {
                auto &now=server->rpgUnitStats.hls[i];const auto &old=before.stats.hls[i];
                now.state=old.state;now.fpHP=(std::min)(old.fpHP,now.fpMaxHP);
            }
            server->nCanAttackTime=before.attack;
            server->ExportStats();
        }
        auto prepareHoover=[&](CPlayer &owner,bool peerSide,std::vector<POSITION> &nodes,std::vector<CItem*> &remove) {
            std::set<CItem*> wanted;
            for(auto &g:groups)if(g.place==Place::Hoover)wanted.insert(peerSide?g.peer:g.item);
            for(POSITION p=owner.items.GetHeadPosition();p;) {
                auto *item=owner.items.GetNext(p);
                if(!wanted.erase(item)) {pins.emplace_back(item);remove.push_back(item);}
            }
            // Reserve vector capacity before adding the first fallible MFC list node.
            nodes.reserve(wanted.size());
            for(auto *item:wanted) {auto p=owner.items.AddTail(item);item->AddRef();nodes.push_back(p);}
        };
        prepareHoover(player,false,addedNodes,removed);prepareHoover(peer,true,peerAddedNodes,peerRemoved);
        // Publication below exchanges prepared references and scalar values only.
        auto removeHoover=[](CPlayer &owner,const std::vector<CItem*> &remove) noexcept {
            for(auto *item:remove) {POSITION p=owner.items.Find(item);if(p){owner.items.RemoveAt(p);item->Release();}}
        };
        removeHoover(player,removed);removeHoover(peer,peerRemoved);
        wagon.allWagonItems.Swap(player.party.allWagonItems);peerWagon.allWagonItems.Swap(peer.party.allWagonItems);
        itemStock.allWagonItems.Swap(state.owner->itemAssort.allWagonItems);
        spellStock.allWagonItems.Swap(state.owner->spellAssort.allWagonItems);
        nextItems.SwapState(state.owner->itemGenerator);nextSpells.SwapState(state.owner->spellGenerator);
        for(std::size_t a=0;a<state.actors.size();++a) {
            slots[a].Apply(*unitUndo[a].unit);auto *view=theWorld.GetUnit(state.actors[a].first);peerSlots[a].Apply(*view);
            metadata[a]->Swap(player.party.people[int(a)]);peerMetadata[a]->Swap(peer.party.people[int(a)]);
            auto *server=static_cast<CUnitServer*>(unitUndo[a].unit->GetUnitSpecific());
            state.party.people[int(a)].stats=*server->GetRPGStats();
            DWORD selected=DWORD(-1);
            if(slots[a].current<QI_MAX_WEAPONS)for(const auto &g:groups)
                if(g.actor==int(a) && g.place==Place::Weapon && g.slot==int(slots[a].current)) {
                    const auto &array=player.party.people[int(a)].weapons;
                    for(int i=0;i<array.GetSize();++i)if(array[i]==g.metadata)selected=i;
                }
            player.party.people[int(a)].dwCurrentWeapon=peer.party.people[int(a)].dwCurrentWeapon=selected;
            view->rpgUserUnitStats=unitUndo[a].unit->rpgUserUnitStats;
            auto &common=const_cast<SRPGUserCommonStats&>(view->GetUserCommonStats());
            server->GetMapObjectServer()->GetRPGStats()->Export(&common);
        }
        for(auto &g:groups) {
            if(g.sourceInfo)g.item->AttachUserData(g.sourceInfo);
            else if(g.place==Place::Hoover)g.item->FreeUserData();
            for(auto *copy:g.copies)copy->SetTradeOrigin(g.item->GetRTID());
        }
        player.party.nMoney=peer.party.nMoney=state.party.nMoney;
        state.money=state.party.nMoney;state.sources.swap(nextSources);state.recipes.swap(nextRecipes);
        ++state.revision;published=true;
        // Visual notifications cannot turn an accepted deal into a retryable failure.
        try {
            for(std::size_t a=0;a<state.actors.size();++a)if(changedUnits[a]) {
                auto *view=theWorld.GetUnit(state.actors[a].first);
                view->GetUnitSpecific()->NotifyItemsUpdated();view->GetUnitSpecific()->NotifyWeaponUpdated();
                view->GetUnitSpecific()->NotifyHitPointsUpdated();
            }
        } catch(...) {GameJournal::AllocationFailureInCatch("Merchant presentation refresh after commit");}
    }
    static bool Commit(World &settlement,CWorldServer &world,CI3DScreenCamp &screen,TradeAction action,CItem *item,CItem **result)
    {
        auto state=settlement.trade;
        Require(state && !state->applying && screen.tradeParty==&state->party && screen.tradeShop==&state->shop,"invalid merchant screen");
        auto *player=world.GetPlayer(settlement.dialogue.player);Require(player!=nullptr,"merchant player retired");
        auto actual=Real(world,*player,*state->owner);Check(*state,world,*player,actual);
        Require(!memcmp(screen.fPriceKoeffs,state->owner->fKoeffs,sizeof(screen.fPriceKoeffs)),"merchant price coefficients changed");
        Draft(*state,screen,false,action);
        state->applying=true;screen.applyingTrade=true;
        struct Applying { TradeState &state;CI3DScreenCamp &screen;~Applying(){state.applying=false;screen.applyingTrade=false;} } applying{*state,screen};
        RandomRollback random;
        if(action==TradeAction::Deal) {
            Require(screen.eScreenMode!=ISM_SPELLS,"progression is not a merchant operation");
            screen.CheckValidTransaction();
            const auto money=std::int64_t(state->money)-std::int64_t(screen.nPrice);
            Require(screen.bValidOK && money>=0 && money<=INT_MAX,"merchant quote is unaffordable");
            Require(screen.ApplyDraftTransaction(),"merchant operation rejected");
            Require(state->party.nMoney==money,"merchant charge changed");
        } else {
            Require(item!=nullptr && state->sources.count(item->TradeOrigin()),"unknown equipment intent");
            auto *person=screen.GetCurrentPers();
            auto *unit=person?world.GetUnit(person->nID):nullptr;
            Require(unit && !unit->IsDead(),"equipment actor is unavailable");
            if(action==TradeAction::Equip) {
                Require(state->party.GetWagonItemIdx(item)>=0,"equipment is not in the wagon");
                screen.InventoryToPersDraft(item);
            } else {
                auto *person=screen.GetCurrentPers();Require(person!=nullptr,"equipment actor missing");
                bool owned=false;
                for(int i=0;i<person->weapons.GetSize();++i)owned|=person->weapons[i]==item;
                for(int i=0;i<person->items.GetSize();++i)owned|=person->items[i]==item;
                for(int i=0;i<person->armors.GetSize();++i)owned|=person->armors[i]==item;
                for(int i=0;i<person->spells.GetSize();++i)owned|=person->spells[i]==item;
                Require(owned,"equipment belongs to another actor");
                auto *output=screen.PersToInventoryDraft(item);if(result)*result=output;
            }
        }
        const auto desired=Draft(*state,screen,true,action);
        Publish(*state,world,*player,desired);
        random.committed=action==TradeAction::Deal;
        try {
            GameJournal::Event("SETTLEMENT","trade_commit","token=%llu revision=%llu shop=%d action=%d money=%d",
                static_cast<unsigned long long>(state->token),static_cast<unsigned long long>(state->revision),state->shop.nID,int(action),state->money);
        } catch(...) {GameJournal::AllocationFailureInCatch("Merchant receipt after commit");}
        return true;
    }
    static void Generate(CShop &shop,CParty &party)
    {
        float low[RPG_NUM_MAGIC_SUBTYPES],high[RPG_NUM_MAGIC_SUBTYPES];
        auto levels=[&](bool updated) {
            auto *mod=ModRuntime::Current();
            for(int i=0;i<RPG_NUM_MAGIC_SUBTYPES;++i) {
                low[i]=1e20f;high[i]=-1e20f;
                for(int a=0;a<party.people.GetSize();++a) {
                    float skill=party.people[a].stats.GetMagicSkill(ERPGMagicSubtypes(i));
                    low[i]=(std::min)(low[i],skill);high[i]=(std::max)(high[i],skill);
                    if(updated) {low[i]=(std::max)(low[i],10.f);high[i]=(std::max)(high[i],float(mod->ShopConstructLimit(shop.nID)));}
                }
            }
            shop.SetAssortmentParameters(party,low,high);
        };
        levels(false);shop.GenerateSpells();shop.GenerateItems();
        if(auto *mod=ModRuntime::Current();mod && mod->CombatSetting("JMVShopUpdate"))levels(true);
        shop.bNeedUpdate=false;
    }
};

bool World::BeginTrade(CWorldServer &world,std::uint64_t token,const std::string &topic)
{
    try {
        if(trade || !DialogueValid(world,token) || !dialogue.topic.empty())return false;
        const auto topics=DialogueTopics(world,dialogue.player,dialogue.target);
        auto binding=std::find_if(topics.begin(),topics.end(),[&](const auto &t){return t.key==topic;});
        if(binding==topics.end() || !binding->binding.shop)return false;
        auto *shop=theHat.shops.GetShop(binding->binding.shop);
        Require(shop && (shop->bItem || shop->bSpell),"merchant shop is unavailable");
        auto state=std::make_shared<TradeState>();state->token=token;state->topic=topic;state->owner=shop;
        state->shop.nID=shop->nID;state->shop.bItem=shop->bItem;state->shop.bSpell=shop->bSpell;
        std::copy(std::begin(shop->fKoeffs),std::end(shop->fKoeffs),std::begin(state->shop.fKoeffs));
        state->shop.itemGenerator.CopyState(shop->itemGenerator);state->shop.spellGenerator.CopyState(shop->spellGenerator);
        auto *player=world.GetPlayer(dialogue.player);Require(player!=nullptr,"merchant player missing");
        TradeAccess::Model(*state,world,*player);
        if(shop->bNeedUpdate) {
            RandomRollback random;
            TradeAccess::Generate(state->shop,state->party);
            CParty itemStock,spellStock;itemStock.Create();spellStock.Create();
            auto prepare=[](CParty &draft,CParty &owner,CParty &prepared) {
                for(int i=0;i<draft.allWagonItems.GetSize();++i) {
                    auto *item=Clone(draft.allWagonItems[i],owner);Pin pin(item);item->Release();
                    item->SetTradeOrigin(0);Append(prepared,item);
                }
            };
            prepare(state->shop.itemAssort,shop->itemAssort,itemStock);prepare(state->shop.spellAssort,shop->spellAssort,spellStock);
            // Prepare the model's new manifest before publishing refreshed stock.
            auto entries=TradeAccess::Real(world,*player,*shop);
            entries.erase(std::remove_if(entries.begin(),entries.end(),[](const auto &e){return Stock(e.place);}),entries.end());
            for(int i=0;i<itemStock.allWagonItems.GetSize();++i) {
                auto *item=itemStock.allWagonItems[i];state->shop.itemAssort.allWagonItems[i]->SetTradeOrigin(item->GetRTID());
                entries.push_back({item,Place::ItemStock});
            }
            for(int i=0;i<spellStock.allWagonItems.GetSize();++i) {
                auto *item=spellStock.allWagonItems[i];state->shop.spellAssort.allWagonItems[i]->SetTradeOrigin(item->GetRTID());
                entries.push_back({item,Place::SpellStock});
            }
            auto sources=TradeAccess::Capture(entries);auto recipes=TradeAccess::Recipes(state->shop);
            CAssortmentGenerator items(&shop->itemAssort),spells(&shop->spellAssort);
            items.CopyState(state->shop.itemGenerator);spells.CopyState(state->shop.spellGenerator);
            itemStock.allWagonItems.Swap(shop->itemAssort.allWagonItems);spellStock.allWagonItems.Swap(shop->spellAssort.allWagonItems);
            items.SwapState(shop->itemGenerator);spells.SwapState(shop->spellGenerator);shop->bNeedUpdate=false;
            state->sources.swap(sources);state->recipes.swap(recipes);random.committed=true;
        }
        state->shop.bNeedUpdate=false;
        Require(SelectDialogueTopic(world,token,topic),"merchant topic changed");
        trade=std::move(state);
        GameJournal::Event("SETTLEMENT","trade_open","token=%llu shop=%d",static_cast<unsigned long long>(token),shop->nID);
        return true;
    } catch(CException *error) {error->Delete();GameJournal::AllocationFailureInCatch("Merchant opening");}
      catch(const std::exception &error) {GameJournal::Notification("settlement_trade",error.what(),"opening rejected");}
    return false;
}
bool World::TradeValid(CWorldServer &world,std::uint64_t token) const
{
    if(!trade || trade->token!=token)return false;
    auto *player=world.GetPlayer(dialogue.player);
    return DialogueValid(world,token) && dialogue.topic==trade->topic &&
        player && player->party.vars.GetVar(trade->topic.c_str())==1 && theHat.shops.GetShop(trade->shop.nID)==trade->owner;
}
CParty *World::TradeParty(std::uint64_t token) const {return trade && trade->token==token?&trade->party:nullptr;}
CShop *World::TradeShop(std::uint64_t token) const {return trade && trade->token==token?&trade->shop:nullptr;}
std::shared_ptr<void> World::TradeLifetime(std::uint64_t token) const {return trade && trade->token==token?trade:nullptr;}
bool World::CommitTrade(CWorldServer &world,std::uint64_t token,CI3DScreenCamp &screen,TradeAction action,CItem *item,CItem **result)
{
    try {
        Require(TradeValid(world,token),"merchant session retired");
        return TradeAccess::Commit(*this,world,screen,action,item,result);
    } catch(CException *error) {error->Delete();GameJournal::AllocationFailureInCatch("Merchant transaction rejected");}
      catch(const std::exception &error) {GameJournal::Notification("settlement_trade",error.what(),"transaction rejected");}
    if(result)*result=nullptr;
    return false;
}
void World::EndTrade(std::uint64_t token)
{
    if(!trade || trade->token!=token)return;
    GameJournal::Event("SETTLEMENT","trade_close","token=%llu revision=%llu shop=%d",
        static_cast<unsigned long long>(token),static_cast<unsigned long long>(trade->revision),trade->shop.nID);
    trade.reset();if(dialogue.token==token)dialogue.topic.clear();
}
}
