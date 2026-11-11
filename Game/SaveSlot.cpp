#include "StdAfx.h"
#include "SaveSlot.h"
#include "SaveModIdentity.h"
#include "ModEncounter.h"
#include "ModArena.h"
#include "ZoneIce.h"
#include "WorldServer.h"
#include "WorldClient.h"
#include "WorldInterface.h"
#include "ScenarioTracker.h"
#include "MapObjectManager.h"
#include "ServerSpecificManager.h"
#include "ItemManager.h"
#include "SpellManager.h"
#include "EffectManager.h"
#include "PlayerManager.h"
#include "Player.h"
#include "PlayerServer.h"
#include "PlayerClient.h"
#include "ItemConstructor.h"
#include "SpellConstructor.h"
#include "FigureManager.h"
#include "Hoover.h"
#include "Camera.h"
#include "I3DMain.h"
#include "I3DScreenSaveLoad.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "SpellDatabase.h"
#include "GameJournal.h"
#include "LiveObserver.h"
#include "UnitServer.h"
#include "OwnedRender/E1MobSource.h"
#include "dx11/Surface.h"
#include <filesystem>
#include <set>

namespace SaveSchema {
using namespace Fields;
namespace {
Limits SlotLimits() { Limits l; l.payloadBytes=512u*1024*1024; l.fieldBytes=256u*1024*1024; return l; }
[[noreturn]] void Invalid(const char *text) { throw Error(ErrorCode::Invalid,text); }
Bytes Snapshot(CSave &f)
{
    if (f.GetSize()>SlotLimits().payloadBytes) Invalid("slot too large");
    Bytes bytes(f.GetSize()); f.Seek(0);
    if (!bytes.empty() && f.Read(bytes.data(),static_cast<UINT>(bytes.size()))!=bytes.size())
        Invalid("incomplete slot read");
    return bytes;
}
void Buffer(CMemorySave &f,const Bytes &bytes)
{
    f.Clear(); if (!bytes.empty()) f.Write(bytes.data(),static_cast<UINT>(bytes.size()));
    f.Seek(0); f.SetStrictRead(true,"Saves64");
}
std::uint64_t Content(const Bytes &bytes)
{
    std::uint64_t hash=14695981039346656037ull;
    for (auto b:bytes) { hash^=b; hash*=1099511628211ull; }
    return hash;
}
template<class H> void Hash(Writer &w,const H &hash)
{
    std::map<int,CGameBase *> sorted;
    for (const auto &x:hash) sorted.emplace(x.first,x.second);
    Count(w,sorted.size()); for (const auto &x:sorted) w.GameReference(x.second);
}
template<class T,class H> void Hash(Reader &r,H &hash,bool owning)
{
    if (hash.GetCount()) Invalid("root set not empty");
    const auto n=r.Count();
    for (std::uint32_t i=0;i<n;++i) {
        auto *object=r.GameReferenceAs<T>();
        if (!object || hash.Seek(object->GetID())) Invalid("null/duplicate root member");
        hash.SetAt(object->GetID(),object); if (owning) object->AddRef();
    }
}
template<class H> void ReleaseHash(H &hash)
{ for (const auto &x:hash) x.second->Release(); hash.RemoveAll(); }
template<class F> void Values(Writer &w,std::size_t n,F f)
{ Count(w,n); for(std::size_t i=0;i<n;++i) f(i); }
void Interface(Writer &w,const CInterface3D &v)
{
    Field(w,v.bInfoClosed); Field(w,v.bTextClosed); Field(w,v.bMapClosed);
    Field(w,v.nInfoMode); Field(w,v.nTextMode); Field(w,v.nSpeed);
    Field(w,v.szLastMissionTutorial); Field(w,v.nLastMissionTutorialPage); Field(w,v.szLastReceivedQuest);
    Count(w,v.tutorials.size()); for (const auto &x:v.tutorials) { Field(w,x.first); Field(w,x.second); }
}
struct InterfaceState {
    bool info=false,text=false,map=false; int infoMode=0,textMode=0,speed=0,page=0;
    CString tutorial,quest; std::map<CString,int> tutorials;
    void Read(Reader &r) {
        Field(r,info);Field(r,text);Field(r,map);Field(r,infoMode);Field(r,textMode);Field(r,speed);
        Field(r,tutorial);Field(r,page);Field(r,quest);
        const auto n=r.Count();for(std::uint32_t i=0;i<n;++i) {
            CString name;int p;Field(r,name);Field(r,p);
            if(!tutorials.emplace(name,p).second)Invalid("duplicate tutorial");
        }
    }
    void Apply() noexcept {
        auto &v=theInterface;v.bInfoClosed=info;v.bTextClosed=text;v.bMapClosed=map;
        v.nInfoMode=infoMode;v.nTextMode=textMode;v.nSpeed=speed;
        std::swap(v.szLastMissionTutorial,tutorial);std::swap(v.szLastReceivedQuest,quest);
        v.nLastMissionTutorialPage=page;v.tutorials.swap(tutorials);
    }
};
// The old per-screen native byte buffer is deliberately not a new slot payload.
// Selection/scroll caches are recreated by the accepted screen from world state.
void CheckedName(const char *name)
{
    if (!name || !*name || strlen(name)>128) Invalid("invalid slot/zone name");
    for(const char *p=name;*p;++p)
        if (!(isalnum(static_cast<unsigned char>(*p)) || *p=='_' || *p=='-')) Invalid("invalid slot/zone name");
}
Bytes ReadFile(const CString &path)
{
    CFileSave f;if(!f.OpenRead(path)) throw Error(ErrorCode::Stream,"cannot open Saves64 slot");
    return Snapshot(f);
}
void AtomicFile(const CString &path,const Bytes &bytes)
{
    const CString temp=path+".tmp-"+std::to_string(GetCurrentProcessId()).c_str();
    try {
        CFile f(temp,CFile::modeCreate|CFile::modeWrite|CFile::shareExclusive);
        if(!bytes.empty()) f.Write(bytes.data(),static_cast<UINT>(bytes.size()));
        f.Flush();f.Close();
        if(!MoveFileExA(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw Error(ErrorCode::Stream,"cannot replace Saves64 slot");
    } catch(...) { DeleteFileA(temp);throw; }
}
}

struct Slot::PartyOwner { ObjectId item=0,spell=0; };
struct Slot::Document {
    bool staged=false;
    std::uint32_t kind=0; // 1 world, 2 scenario, 3 local party
    ObjectId next=1;
    CString terrain,mob,zone;
    std::vector<OwnerDescription> descriptions;
    struct PlayerOwner { ObjectId id=0;int map=0; PartyOwner party; };
    std::vector<PlayerOwner> players;
    std::vector<PartyOwner> parties;
    std::vector<int> partyKeys;
    std::vector<CString> singleNames;
    std::vector<int> shopKeys;
    Bytes payload;
    std::shared_ptr<SavedOwners> owners=std::make_shared<SavedOwners>();
    std::shared_ptr<CWorldServer> world;
    std::shared_ptr<CGlobalHat> scenario;
    std::vector<CParty *> partyTargets;
    std::unique_ptr<CCamera> camera;
    std::shared_ptr<CModPackage::SaveState> mod;
    Graph graph;
    Document()=default;
    Document(Document&&)=default;
    Document& operator=(Document&&)=default;
    Document(const Document&)=delete;
    ~Document() {
        // Graph is destroyed before the captured owners. Root links are released
        // while all registries still pin their targets; no live-world lookup.
        if(world && staged) {
            world->script.Clear();world->scriptTemp.Clear();
            world->spells.RemoveAll();world->activeItems.RemoveAll();
            ReleaseHash(world->mapObjects); ReleaseHash(world->effects);
            world->staticObjs.RemoveAll();world->dynamicObjs.RemoveAll();
            world->dynamicMapObjs.RemoveAll();world->questMapObjs.RemoveAll();world->activeSet.RemoveAll();
        }
    }
};
struct Slot::State {
    std::vector<Document> zones;
    Document scenario;
    InterfaceState uiState;
    RandomState random;
    ObjectId next=1;
    std::string current;
    std::map<std::string,Bytes> files;
    bool helmets=false,upper=false;
    bool fullSlot=false;
    bool zoneOnly=false;
};
std::unique_ptr<Slot::State> Slot::active;
std::map<std::string,Bytes> Slot::visited;
Bytes Slot::presentation,Slot::screenshot;

void Slot::CopyParty(CParty &dst,const CParty &src)
{
    if(&dst==&src)return;
    dst.Clear();if(!dst.GetItemManager())dst.Create();
    std::map<const CItem *,CItem *> copies;
    auto clone=[&](CItem *item)->CItem *{
        if(!item)return nullptr;
        auto found=copies.find(item);if(found!=copies.end())return found->second;
        auto *copy=dst.DuplicateItem(item);copy->SetCount(item->GetCount());copies.emplace(item,copy);return copy;
    };
    dst.people.SetSize(src.people.GetSize());
    for(INT_PTR i=0;i<src.people.GetSize();++i){
        const auto &s=src.people[i];auto &d=dst.people[i];
        d.szName=s.szName;d.szInternalName=s.szInternalName;d.szTemplate=s.szTemplate;
        d.szPrimTexture=s.szPrimTexture;d.szSecondTexture=s.szSecondTexture;
        d.szPrototype=s.szPrototype;d.szVoicePrototype=s.szVoicePrototype;d.nID=s.nID;
        d.stats=s.stats;d.complection=s.complection;d.pose=s.pose;d.dwCurrentWeapon=s.dwCurrentWeapon;
        d.bIsSelected=s.bIsSelected;d.nFollowToID=s.nFollowToID;d.aggression=s.aggression;
        auto array=[&](auto &out,const auto &in){out.SetSize(in.GetSize());for(INT_PTR k=0;k<in.GetSize();++k){
            using T=std::remove_pointer_t<std::decay_t<decltype(out[k])>>;
            out[k]=dynamic_cast<T *>(clone(in[k]));if(out[k])out[k]->AddRef();
        }};
        array(d.weapons,s.weapons);array(d.items,s.items);array(d.armors,s.armors);array(d.spells,s.spells);
    }
    for(INT_PTR i=0;i<src.allWagonItems.GetSize();++i){auto *item=clone(src.allWagonItems[i]);dst.allWagonItems.Add(item);if(item)item->AddRef();}
    for(INT_PTR i=0;i<dst.people.GetSize();++i)
        for(auto &hit:dst.people[i].stats.hls)for(auto &layer:hit.layers)for(auto &item:layer.lpItems)
            if(item){auto found=copies.find(dynamic_cast<CItem *>(item));if(found==copies.end())Invalid("party armor reference outside inventory");item=found->second;}
    dst.nMoney=src.nMoney;dst.vars=src.vars;dst.szCurrentZone=src.szCurrentZone;dst.nEntrance=src.nEntrance;

}
void Slot::BindParty(Document &d,CParty &party,std::shared_ptr<void> owner)
{
    d.partyTargets.push_back(&party);
    d.owners->Add(OwnerKind::Item,{owner,party.GetItemManager()});
    d.owners->Add(OwnerKind::Spell,{owner,party.GetSpellManager()});
}
void Slot::BindWorld(Document &d,std::shared_ptr<CWorldServer> world,bool zoneOnly)
{
    d.kind=1;d.world=world;d.terrain=world->szZoneFileName;d.mob=world->szMobFileName;d.zone=world->szCurrentZoneName;
    auto add=[&](OwnerKind kind,CBaseManager *p){d.owners->Add(kind,{world,p});};
    add(OwnerKind::Map,world->GetMapObjectManager());add(OwnerKind::ServerSpecific,world->GetSpecificManager());
    add(OwnerKind::Item,world->GetItemManager());add(OwnerKind::Spell,world->GetSpellManager());
    add(OwnerKind::Effect,world->GetEffectManager());add(OwnerKind::Player,world->GetPlayerManager());
    d.owners->omitPlayers=zoneOnly;
    if (!zoneOnly) world->GetPlayerManager()->ForEach([&](CGameBase *p){
        auto *player=dynamic_cast<CPlayer *>(p);if(!player)Invalid("unexpected player class");
        d.players.push_back({player->GetRTID(),player->GetID(),{player->party.GetItemManager()->GetRTID(),player->party.GetSpellManager()->GetRTID()}});
        BindParty(d,player->party,world);
    });
}
void Slot::Describe(Writer &w,const Document &d)
{
    w.UInt32(d.kind);w.UInt64(d.next);Field(w,d.terrain);Field(w,d.mob);Field(w,d.zone);
    Count(w,d.descriptions.size());for(const auto &o:d.descriptions)o.Store(w);
    Count(w,d.players.size());for(const auto &p:d.players){w.UInt64(p.id);w.Int32(p.map);w.UInt64(p.party.item);w.UInt64(p.party.spell);}
    Count(w,d.parties.size());for(const auto &p:d.parties){w.UInt64(p.item);w.UInt64(p.spell);}
    Count(w,d.partyKeys.size());for(auto key:d.partyKeys)w.Int32(key);
    Count(w,d.singleNames.size());for(const auto &key:d.singleNames)Field(w,key);
    Count(w,d.shopKeys.size());for(auto key:d.shopKeys)w.Int32(key);
    w.UInt64(Content(d.payload));w.Blob(d.payload);
}
void Slot::Describe(Reader &r,Document &d)
{
    d.kind=r.UInt32();d.next=r.UInt64();Field(r,d.terrain);Field(r,d.mob);Field(r,d.zone);
    if(d.kind<1 || d.kind>3 || d.next<2)Invalid("invalid document description");
    d.descriptions.resize(r.Count());std::set<ObjectId> ids;
    for(auto &o:d.descriptions){o.Load(r);if(o.id>=d.next || !ids.insert(o.id).second)Invalid("duplicate owner identity");}
    d.players.resize(r.Count());
    for(auto &p:d.players){p.id=r.UInt64();p.map=r.Int32();p.party.item=r.UInt64();p.party.spell=r.UInt64();
        if(!p.id || p.id>=d.next || p.map<0 || !ids.insert(p.id).second)Invalid("invalid saved player");}
    d.parties.resize(r.Count());for(auto &p:d.parties){p.item=r.UInt64();p.spell=r.UInt64();}
    d.partyKeys.resize(r.Count());for(auto &key:d.partyKeys)key=r.Int32();
    d.singleNames.resize(r.Count());for(auto &key:d.singleNames)Field(r,key);
    d.shopKeys.resize(r.Count());for(auto &key:d.shopKeys)key=r.Int32();
    auto content=r.UInt64();d.payload=r.Blob();if(Content(d.payload)!=content)Invalid("document content mismatch");
}
Bytes Slot::Encode(Document &d,const std::function<void(Writer &)> &root)
{
    Registry types;RegisterGameTypes(types,d.owners);CMemorySave body;
    SaveSchema::Store(body,types,[&](Writer &w){d.owners->StoreMembers(w);root(w);},SlotLimits());
    d.payload=Snapshot(body);d.next=ObjectIdentity::GetNext();d.descriptions=d.owners->Describe();
    d.parties.clear();for(auto *party:d.partyTargets)d.parties.push_back({party->GetItemManager()->GetRTID(),party->GetSpellManager()->GetRTID()});
    Registry meta;CMemorySave out;SaveSchema::Store(out,meta,[&](Writer &w){Describe(w,d);},SlotLimits());return Snapshot(out);
}
Slot::Document Slot::ReadDocument(const Bytes &bytes)
{
    Document d;Registry meta;CMemorySave f;Buffer(f,bytes);
    auto graph=SaveSchema::Load(f,meta,[&](Reader &r){Describe(r,d);},SlotLimits());
    return d;
}
void Slot::Decode(Document &d,const std::function<void(Reader &)> &root)
{
    Registry types;RegisterGameTypes(types,d.owners);CMemorySave f;Buffer(f,d.payload);
    d.graph=SaveSchema::Load(f,types,[&](Reader &r){d.owners->LoadMembers(r);root(r);},SlotLimits());
    std::set<ObjectId> identities;
    for(const auto &o:d.descriptions)identities.insert(o.id);
    for(auto id:d.graph.Identities())if(!identities.insert(id).second)Invalid("object overlaps owner identity");
}

void Slot::Generator(Writer &w,const CAssortmentGenerator &g)
{
    w.Int32(g.nShopID);Field(w,g.fMinSkills);Field(w,g.fMaxSkills);
    auto shapes=[&](const auto &v){Count(w,v.size());for(auto *p:v){if(!p)Invalid("missing assortment resource");Field(w,p->szName);}};
    shapes(g.armorShapes);shapes(g.weaponShapes);shapes(g.quickitemShapes);
    Count(w,g.materials.size());for(const auto &m:g.materials){Field(w,m.pMaterial->szName);Field(w,m.nMin);Field(w,m.nMax);Field(w,m.nAmount);}
    Count(w,g.spellPrototypes.size());for(auto *p:g.spellPrototypes)Field(w,p->szCodeName);
    Count(w,g.spellModifiers.size());for(auto *p:g.spellModifiers)w.UInt32(p->dwCode);
}
void Slot::Generator(Reader &r,CAssortmentGenerator &g)
{
    g.Clear();g.nShopID=r.Int32();Field(r,g.fMinSkills);Field(r,g.fMaxSkills);
    auto *im=g.pWorld->GetItemManager();auto *sm=g.pWorld->GetSpellManager();
    auto shapes=[&](auto &v,auto lookup){const auto n=r.Count();for(std::uint32_t i=0;i<n;++i){CString name;Field(r,name);auto *p=lookup(name);if(!p)Invalid("unknown assortment shape");v.push_back(p);}};
    shapes(g.armorShapes,[&](const CString &s){return im->GetArmorShape(s);});
    shapes(g.weaponShapes,[&](const CString &s){return im->GetWeaponShape(s);});
    shapes(g.quickitemShapes,[&](const CString &s){return im->GetQuickItemShape(s);});
    auto n=r.Count();for(std::uint32_t i=0;i<n;++i){CString name;Field(r,name);auto *p=im->GetMaterial(name);if(!p)Invalid("unknown assortment material");
        const int low=r.Int32(),high=r.Int32(),amount=r.Int32();g.materials.emplace_back(p,low,high,amount);}
    n=r.Count();for(std::uint32_t i=0;i<n;++i){CString name;Field(r,name);auto *p=theSpellDatabase.spells[name];if(!p)Invalid("unknown assortment spell");g.spellPrototypes.push_back(p);}
    n=r.Count();for(std::uint32_t i=0;i<n;++i){auto code=r.UInt32();char name[5]={};for(int j=0;j<4;++j)name[j]=char(code>>(8*j));auto *p=sm->GetModifier(name);if(!p)Invalid("unknown assortment modifier");g.spellModifiers.push_back(p);}
    g.cspells.clear();g.runes.clear(); // resource-derived tables rebuild at generation
}
void Slot::Scenario(Writer &w,const CGlobalHat &hat)
{
    w.Int32(hat.nLastPartyID);
    std::map<int,const CParty *> sorted;for(const auto &p:hat.parties)sorted.emplace(p.first.nSomeNumber,&p.second);
    Count(w,sorted.size());for(const auto &p:sorted){w.Int32(p.first);p.second->Store(w);}
    Count(w,hat.users.size());for(const auto &u:hat.users){Field(w,u.szName);Field(w,u.szPassword);Count(w,u.parties.size());for(const auto &p:u.parties)Field(w,p);}
    Field(w,hat.singleParty.szCurrentParty);Field(w,hat.singleParty.szPartyOnServer);
    Count(w,hat.singleParty.parties.size());for(const auto &p:hat.singleParty.parties){Field(w,p->szName);p->heroes.Store(w);}
    Count(w,hat.shops.shops.size());for(const auto &s:hat.shops.shops){
        Field(w,s->nID);Field(w,s->bItem);Field(w,s->bSpell);Field(w,s->bNeedUpdate);
        ShopCoefficients coeff;std::copy(std::begin(s->fKoeffs),std::end(s->fKoeffs),coeff.values.begin());w.Value(coeff);
        s->itemAssort.Store(w);s->spellAssort.Store(w);Generator(w,s->itemGenerator);Generator(w,s->spellGenerator);
    }
}
void Slot::Scenario(Reader &r,CGlobalHat &hat)
{
    hat.nLastPartyID=r.Int32();auto n=r.Count();if(n!=hat.parties.size())Invalid("party directory mismatch");
    std::set<int> seen;for(std::uint32_t i=0;i<n;++i){SPartyID id;id.nSomeNumber=r.Int32();auto p=hat.parties.find(id);if(p==hat.parties.end() || !seen.insert(id.nSomeNumber).second)Invalid("party key mismatch");p->second.Load(r);}
    hat.users.resize(r.Count());for(auto &u:hat.users){Field(r,u.szName);Field(r,u.szPassword);u.parties.resize(r.Count());for(auto &p:u.parties)Field(r,p);}
    Field(r,hat.singleParty.szCurrentParty);Field(r,hat.singleParty.szPartyOnServer);
    n=r.Count();if(n!=hat.singleParty.parties.size())Invalid("single party directory mismatch");
    for(auto &p:hat.singleParty.parties){CString name;Field(r,name);if(name!=p->szName)Invalid("single party name mismatch");p->heroes.Load(r);}
    if(!hat.singleParty.GetParty(hat.singleParty.szCurrentParty))Invalid("missing selected party");
    n=r.Count();if(n!=hat.shops.shops.size())Invalid("shop directory mismatch");
    for(auto &s:hat.shops.shops){int id=r.Int32();if(id!=s->nID)Invalid("shop key mismatch");Field(r,s->bItem);Field(r,s->bSpell);Field(r,s->bNeedUpdate);
        ShopCoefficients coeff;r.Value(coeff);std::copy(coeff.values.begin(),coeff.values.end(),std::begin(s->fKoeffs));
        s->itemAssort.Load(r);s->spellAssort.Load(r);Generator(r,s->itemGenerator);Generator(r,s->spellGenerator);
    }
}

void Slot::PrepareScenario(Document &d)
{
    if(d.kind!=2)Invalid("not a scenario document");
    d.scenario=std::make_shared<CGlobalHat>();auto owner=d.scenario;std::size_t next=0;
    auto party=[&](CParty &p){if(next>=d.parties.size())Invalid("missing party owner");const auto &o=d.parties[next++];
        p.Destroy();p.CreateSavedOwners(o.item,o.spell);BindParty(d,p,owner);};
    std::set<int> keys;
    for(auto key:d.partyKeys){if(!keys.insert(key).second)Invalid("duplicate party");SPartyID id;id.nSomeNumber=key;party(owner->parties[id]);}
    std::set<CString> names;
    for(const auto &name:d.singleNames){if(!names.insert(name).second)Invalid("duplicate single party");CPtr<CSingleParty> p=new CSingleParty;p->szName=name;party(p->heroes);owner->singleParty.parties.push_back(p);}
    keys.clear();for(auto key:d.shopKeys){if(!keys.insert(key).second)Invalid("duplicate shop");CPtr<CShop> s=new CShop;s->nID=key;party(s->itemAssort);party(s->spellAssort);owner->shops.shops.push_back(s);}
    if(next!=d.parties.size())Invalid("extra party owners");
}
void Slot::PrepareWorld(Document &d)
{
    if(d.kind!=1)Invalid("not a zone document");
    auto world=std::make_shared<CWorldServer>(true);d.world=world;d.staged=true;
    auto make=[&](OwnerKind kind,auto *tag)->CBaseManager *{
        using T=std::remove_pointer_t<decltype(tag)>;const OwnerDescription *found=nullptr;
        for(const auto &o:d.descriptions)if(o.kind==kind){
            // Party managers are bound separately from their explicit identities.
            bool party=false;for(const auto &p:d.players)if(o.id==p.party.item||o.id==p.party.spell)party=true;
            if(party)continue;if(found)Invalid("duplicate zone manager");found=&o;}
        if(!found)Invalid("missing zone manager");
        auto manager=ObjectIdentity::Create<T,std::unique_ptr<T>>(found->id);
        manager->AddRef();
        if(!static_cast<CBaseManager *>(manager.get())->Create(world.get()))Invalid("cannot create staged manager");
        d.owners->Add(kind,std::shared_ptr<CBaseManager>(world,manager.get()));return manager.release();
    };
    world->pMapObjectManager=make(OwnerKind::Map,(CMapObjectManager *)nullptr);
    world->pSpecificManager=make(OwnerKind::ServerSpecific,(CServerSpecificManager *)nullptr);
    world->pItemManager=make(OwnerKind::Item,(CItemManager *)nullptr);
    world->pSpellManager=make(OwnerKind::Spell,(CSpellManager *)nullptr);
    world->pEffectManager=make(OwnerKind::Effect,(CEffectManager *)nullptr);
    world->pPlayerManager=make(OwnerKind::Player,(CPlayerManager *)nullptr);
    world->pFigureManager=new CFigureManager;world->pFigureManager->AddRef();world->pFigureManager->Create(world.get());
    world->pItemConstructor=new CItemConstructor;world->pItemConstructor->AddRef();world->pItemConstructor->Create();
    world->pSpellConstructor=new CSpellConstructor;world->pSpellConstructor->AddRef();world->pSpellConstructor->Create();
    world->pHoover=new CHoover;world->pHoover->AddRef();world->pNetDriver=new CLocalNetDriver;
    for(const auto &p:d.players){
        auto object=world->pPlayerManager->CreateSavedData(STID_PLAYER,p.map,p.id);
        auto *player=dynamic_cast<CPlayer *>(static_cast<CGameBase *>(object));if(!player)Invalid("wrong player factory");
        player->party.Destroy();player->party.CreateSavedOwners(p.party.item,p.party.spell);
        BindParty(d,player->party,world);d.owners->prepared.emplace(p.id,object);
    }
    // Load only immutable terrain/navigation source resources. No MOB object factory, global
    // SetWeather, camera, script execution, RNG initialization or renderer calls.
    if(!d.terrain.IsEmpty()) {
        if(!world->aiMap.Load(world->GetFullMobName(d.terrain)))Invalid("cannot load saved terrain");
        world->aiMap.Navigation().Scene(d.terrain,d.mob,*world);
    }
    world->szZoneFileName=d.terrain;world->szMobFileName=d.mob;world->szCurrentZoneName=d.zone;
    world->aiMap.SetGraphRebuild(false);
    world->aiMap.marks.BindSavedMap(world->aiMap);
}

void Slot::World(Writer &w,const CWorldServer &v)
{
    Field(w,v.ptWindDirection);Field(w,v.fpWindForce);Field(w,v.fpNightPower);Field(w,v.fpVisionBonus);
    Field(w,v.alarms);Field(w,v.nSegmentTimeCounter);Field(w,v.nUpdateCounter);
    Field(w,v.curWeather);Field(w,v.fpWeatherComeTime);Field(w,v.fpTimeModifier);Field(w,v.fpTimeScale);
    Field(w,v.nLastLightningTime);Field(w,v.weatherChange);Field(w,v.skyType);Field(w,v.curInterface);
    Field(w,v.bCombatPause);Field(w,v.bMapLoaded);Field(w,v.bCanRunScript);Field(w,v.ptDefaultCamera);Field(w,v.serverMode);
    Field(w,v.bSendCenterCamera);
    Count(w,v.exits.GetSize());for(INT_PTR i=0;i<v.exits.GetSize();++i){const auto &e=v.exits[i];Field(w,e.x1);Field(w,e.y1);Field(w,e.x2);Field(w,e.y2);Field(w,e.szZone);Field(w,e.fPassTime);Field(w,e.ex1);Field(w,e.ey1);Field(w,e.ex2);Field(w,e.ey2);Field(w,e.eangle);Field(w,e.nZoneEntrance);Field(w,e.fCameraRotate);Field(w,e.bPersRotate);Field(w,e.fPersRotate);}
    Hash(w,v.mapObjects);Hash(w,v.staticObjs);Hash(w,v.dynamicObjs);Hash(w,v.dynamicMapObjs);Hash(w,v.questMapObjs);Hash(w,v.activeSet);Hash(w,v.effects);
    Field(w,v.spells);Field(w,v.activeItems);
    v.aiMap.marks.Store(w);v.script.StoreScript(w);v.scriptTemp.StoreScript(w);v.fof.Store(w);v.waterLevels.Store(w);
    Count(w,v.szLoadedMobs.GetSize());for(INT_PTR i=0;i<v.szLoadedMobs.GetSize();++i)Field(w,v.szLoadedMobs[i]);
    // Item-origin keys point into their owning party, independently of map IDs.
    std::vector<CItem *> sources;v.pItemManager->ForEach([&](CGameBase *o){auto *item=dynamic_cast<CItem *>(o);if(item&&item->GetUserData())sources.push_back(item);});
    Count(w,sources.size());for(auto *item:sources){w.GameReference(item);w.Int32(static_cast<CItemSrcInfo *>(item->GetUserData())->nSrcItemID);}
    w.UInt32(Navigation::ContainerVersion); // rebuild derived tiles/routes from source, never graph deltas
    v.StoreFieldCamp(w);
}
void Slot::World(Reader &r,CWorldServer &v)
{
    Field(r,v.ptWindDirection);Field(r,v.fpWindForce);Field(r,v.fpNightPower);Field(r,v.fpVisionBonus);
    Field(r,v.alarms);Field(r,v.nSegmentTimeCounter);Field(r,v.nUpdateCounter);
    Field(r,v.curWeather);Field(r,v.fpWeatherComeTime);Field(r,v.fpTimeModifier);Field(r,v.fpTimeScale);
    Field(r,v.nLastLightningTime);Field(r,v.weatherChange);Field(r,v.skyType);Field(r,v.curInterface);
    Field(r,v.bCombatPause);Field(r,v.bMapLoaded);Field(r,v.bCanRunScript);Field(r,v.ptDefaultCamera);Field(r,v.serverMode);
    if(v.serverMode!=SERV_MODE_NONE && v.serverMode!=SERV_MODE_LOCAL)Invalid("multiplayer slot not supported");
    Field(r,v.bSendCenterCamera);
    v.exits.SetSize(r.Count());for(INT_PTR i=0;i<v.exits.GetSize();++i){auto &e=v.exits[i];Field(r,e.x1);Field(r,e.y1);Field(r,e.x2);Field(r,e.y2);Field(r,e.szZone);Field(r,e.fPassTime);Field(r,e.ex1);Field(r,e.ey1);Field(r,e.ex2);Field(r,e.ey2);Field(r,e.eangle);Field(r,e.nZoneEntrance);Field(r,e.fCameraRotate);Field(r,e.bPersRotate);Field(r,e.fPersRotate);}
    Hash<CMapObject>(r,v.mapObjects,true);Hash<CMapObject>(r,v.staticObjs,false);Hash<CMapObject>(r,v.dynamicObjs,false);
    Hash<CMapObject>(r,v.dynamicMapObjs,false);Hash<CMapObject>(r,v.questMapObjs,false);Hash<CMapObject>(r,v.activeSet,false);Hash<CEffect>(r,v.effects,true);
    for(auto *set:{&v.staticObjs,&v.dynamicObjs,&v.dynamicMapObjs,&v.questMapObjs,&v.activeSet})
        for(const auto &x:*set)if(!v.mapObjects.Seek(x.first)||*v.mapObjects.Seek(x.first)!=x.second)Invalid("root set outside zone");
    for(const auto &x:v.staticObjs)if(v.dynamicObjs.Seek(x.first))Invalid("static/dynamic overlap");
    if(v.staticObjs.GetCount()+v.dynamicObjs.GetCount()!=v.mapObjects.GetCount())Invalid("incomplete world ownership");
    Field(r,v.spells);Field(r,v.activeItems);
    v.aiMap.marks.Load(r);v.aiMap.marks.BindSavedMap(v.aiMap);v.script.LoadScript(r);v.scriptTemp.LoadScript(r);v.fof.Load(r);v.waterLevels.Load(r);
    v.szLoadedMobs.SetSize(r.Count());for(INT_PTR i=0;i<v.szLoadedMobs.GetSize();++i)Field(r,v.szLoadedMobs[i]);
    const auto n=r.Count();for(std::uint32_t i=0;i<n;++i){auto *item=r.GameReferenceAs<CItem>();if(!item||item->GetManager()!=v.pItemManager||item->GetUserData())Invalid("invalid item origin");auto *info=new CItemSrcInfo;info->nSrcItemID=r.Int32();item->AttachUserData(info);}
    if(r.UInt32()!=Navigation::ContainerVersion)Invalid("incompatible Nav64 world");
    v.LoadFieldCamp(r);v.bTransferItemCreates=true;v.bLoadDynamic=v.bLoadStatic=true;
}

void Slot::FinalizeWorld(Document &d, CGSVariableSet* arriving)
{
                SavedWorldScope lookups(d.world.get());
                for(const auto &x:d.world->mapObjects){
                    auto *object=x.second;
                    // Spell triggers have no skeleton or AI-map geometry (MOTID_TRAP).
                    const bool hasGeometry=object->GetTypeID()!=MOTID_TRAP;
                    if(hasGeometry){
                        auto *animation=static_cast<CAnimationSystem2 *>(d.world->GetFigureManager()->CreateData(ASTID_ANIMATION_SYSTEM2));
                        if(!animation)Invalid("cannot create saved animation system");
                        object->SetAnimationSystem2(animation);object->LoadFigureBoundingVolumes(object->GetMaskName());
                        if(auto *unit=dynamic_cast<CUnit *>(object)){
                            const auto *race=unit->GetRaceModel();
                            if(!race)Invalid("saved unit race unavailable");
                            unit->SetRaceAnimationCoeffs(race->fAnimationCoeffs);
                            auto id=unit->GetStartAnimationId();
                            if(id!=0xffffffffu){auto *entry=unit->GetAnimationEntryByIndex(id);if(!entry)Invalid("saved animation unavailable");unit->SetCurrentAnimationEntry(entry);animation->SetAnimation(entry);}
                            unit->RecalcAIParams();
                        }
                    }
                    // Rebuild every history, including ownership and animation.
                    // SetUpdateMask() without a bit only marks the object dirty.
                    object->ExportStats();object->update.RestoreState();object->UpdateFrame();
                    if(hasGeometry)d.world->aiMap.AddObject(object);
                }
                for(const auto &x:d.world->effects){x.second->SetUpdateMask();x.second->UpdateFrame();}
                if(!d.terrain.IsEmpty()) {
                    const bool iceZone=ZoneIce::Load(*d.world,arriving);
                    if(!iceZone && !d.world->aiMap.Navigation().Load())Invalid(d.world->aiMap.Navigation().Error().c_str());
                    for(const auto& entry:d.world->mapObjects)if(!iceZone)if(auto* unit=dynamic_cast<CUnit*>(entry.second)) {
                        d.world->aiMap.Navigation().Restore(unit,d.world->aiMap);
                        if(!static_cast<CUnitServer*>(unit->GetUnitSpecific())->RestoreNavigation())Invalid("saved navigation route is unavailable");
                    }
                }
                d.world->aiMap.SetGraphRebuild(false);
                // Static objects are sent once by NewZone, outside visibility updates.
                auto snapshotPlayer=std::unique_ptr<CPlayer,void(*)(CPlayer *)>(
                    static_cast<CPlayerManager *>(d.world->GetPlayerManager())->CreatePlayer(),
                    [](CPlayer *p){p->Release();});
                if(!snapshotPlayer)Invalid("cannot create static snapshot player");
                auto *snapshotServer=static_cast<CPlayerServer *>(snapshotPlayer->GetPlayerSpecific());
                CGameBasePtrArray objects;
                for(const auto &x:d.world->staticObjs)objects.Add(x.second);
                snapshotServer->AddMapObjects(objects);
                snapshotServer->CopyUpdatePkt(d.world->theWSInitialUpdate);
}

void Slot::StoreParty(CSave &file,const CParty &party)
{
    Document d;d.kind=3;
    auto pin=std::shared_ptr<void>(const_cast<CParty *>(&party),[](void *){});
    BindParty(d,const_cast<CParty &>(party),pin);
    const auto bytes=Encode(d,[&](Writer &w){party.Store(w);});
    CMemorySave payload;Buffer(payload,bytes);file.Write(payload);
}
void Slot::LoadParty(CSave &file,CParty &party)
{
    CMemorySave payload;file.Read(payload);auto d=ReadDocument(Snapshot(payload));
    if(d.kind!=3 || d.parties.size()!=1)Invalid("invalid local party document");
    std::shared_ptr<CParty> staged;
    {
        ObjectIdentity::RestoreNamespace allocation((std::max)(d.next,ObjectIdentity::GetNext()));
        staged=std::make_shared<CParty>();staged->CreateSavedOwners(d.parties[0].item,d.parties[0].spell);
        BindParty(d,*staged,staged);Decode(d,[&](Reader &r){staged->Load(r);});
    }
    // This is a copy into an already-live party, not saved identity publication.
    // DuplicateItem/CopyFrom allocate fresh IDs in the live namespace.
    CopyParty(party,*staged);
}
void Slot::StoreWorld(CSave &file,CWorldServer &world)
{
    Document d;BindWorld(d,{&world,[](CWorldServer *){}});
    const auto bytes=Encode(d,[&](Writer &w){World(w,world);theCamera.Store(w);if(auto *mod=ModRuntime::Current())mod->StoreSaveState(w);else Invalid("missing mod");});
    if(!bytes.empty())file.Write(bytes.data(),static_cast<UINT>(bytes.size()));
}
void Slot::CameraAdopt(const CCamera &camera)
{
    auto &dst=theCamera;
    dst.ptAnchorPoint=camera.ptAnchorPoint;dst.fAnchorHeight=camera.fAnchorHeight;
    dst.qRotate=camera.qRotate;dst.fPitch=camera.fPitch;dst.fDistance=camera.fDistance;
    dst.scroll=camera.scroll;dst.rotate=camera.rotate;dst.pitch=camera.pitch;dst.zoom=camera.zoom;
    std::copy(std::begin(camera.shortcuts),std::end(camera.shortcuts),std::begin(dst.shortcuts));
    if(camera.campPlacementValid)dst.campPlacement=camera.campPlacement;dst.campPlacementValid=camera.campPlacementValid;
    dst.campEntered=camera.campEntered;dst.campTerrain=theWorld.GetTerrain();dst.campRequestTime=GetTickCount();
    dst.ResetAllFlags();dst.SetFlag(camera.GetFlags());
    if(camera.GetFlags()&CAM_FPS_MODE)dst.SetFPSMode(camera.dwCarrierID);else dst.SetCarrier(camera.dwCarrierID);
}
void Slot::ApplyCamera()
{
    if(!active)return;
    for(auto &d:active->zones)if(d.zone==active->current.c_str()&&d.camera){CameraAdopt(*d.camera);d.camera.reset();break;}
}
void Slot::Commit(std::unique_ptr<State> state)
{
    Document *zone=nullptr;
    for(auto &d:state->zones)if(d.zone==state->current.c_str())zone=&d;
    if(!zone)Invalid("missing current zone");
    auto *current=zone->world.get();auto *previous=&theWorldServer;
    if(ObjectIdentity::RestoreNamespace::Active())Invalid("nested world publication");
    std::set<ObjectId> accepted;
    auto identity=[&](const Document &d){
        for(const auto &o:d.descriptions)if(!accepted.insert(o.id).second)Invalid("duplicate accepted owner identity");
        for(auto id:d.graph.Identities())if(!accepted.insert(id).second)Invalid("duplicate accepted object identity");
    };
    if(state->scenario.scenario)identity(state->scenario);
    identity(*zone);
    auto *player=current->GetPlayer(0);
    auto *server=player?dynamic_cast<CPlayerServer *>(player->GetPlayerSpecific()):nullptr;
    auto *client=theWorld.GetPlayer();
    auto *mod=ModRuntime::Current();if(!mod)Invalid("missing mod at saved-world preparation");
    CPtr<CLocalConnection> clientConnection;
    CPtr<CConnection> serverConnection,oldConnection;
    std::unique_ptr<CParty> clientParty;
    std::vector<owned::RetainedAdmission> renderMembers;
    renderMembers.reserve(current->mapObjects.GetCount());
    for(const auto &x:current->mapObjects){
        const int id=x.second->GetID();
        if(id>0)renderMembers.push_back({std::uint32_t(id),std::uint32_t(x.second->GetTypeID()),id});
    }
    // Preparation is reversible: no live disconnect, screen destruction,
    // namespace publication or world ownership changes occur in this scope.
    {
        ObjectIdentity::RestoreNamespace preparation(state->next);
        SavedWorldScope lookups(current);
        struct RandomGuard { RandomState before=RandomState::Capture();~RandomGuard(){before.Apply();} } randomGuard;
        state->random.Apply();
        for(POSITION p=current->spells.GetHeadPosition();p;)
            current->spells.GetNext(p)->AdoptSavedState(*current);
        mod->PrepareSaveState(zone->mod,state->zoneOnly?WorldLoadOrigin::ZoneEntry:WorldLoadOrigin::SavedGame,[&](){
        if(!state->zoneOnly){
            if(!player||!server||!client)Invalid("local restored player specifics missing");
            if(current->GetPlayerManager()->GetNumElements()!=1)Invalid("slot must contain exactly one local player");
            CPtr<CLocalNetDriver> driver=new CLocalNetDriver;clientConnection=driver->Connect(current->GetLocalNetDriver());
            CConnectionsList connections;current->GetLocalNetDriver()->GetNewPlayers(connections);
            if(!clientConnection||connections.GetCount()!=1)Invalid("cannot prepare local connection");
            serverConnection=connections.GetHead();player->pConnection=serverConnection;
            server->status=CPlayerServer::PSS_ACTIVE;
            clientParty=std::make_unique<CParty>();CopyParty(*clientParty,player->party);
            // Restore the local map ID before the first unit ownership packet.
            SCP_LOGGED logged;logged.nPlayerID=player->GetID();
            server->wpkt.Write(PID_LOGGED);server->wpkt.Write(&logged,sizeof(logged));
            player->SetChanged(DW_PCHANGED_EVERYTHING);
            if(current->bMapLoaded&&!current->SendNewZoneToPlayer(player))Invalid("cannot prepare new-zone update");
            const int savedCounter=server->nStorePartyCounter;
            server->SendParty();server->nStorePartyCounter=savedCounter;server->SendNewInterface();
            oldConnection=client->GetPlayer()->pConnection;
        } else {current->serverMode=SERV_MODE_NONE;current->bCanRunScript=FALSE;} });
        state->next=ObjectIdentity::GetNext();
    }
    // Publication does not allocate. Retained owners, old
    // connection and old client party are released only after the whole exchange.
    previous->detachedSave=true;previous->script.detachedSaveState=true;previous->scriptTemp.detachedSaveState=true;
    if(state->scenario.scenario)PublishGlobalHat(state->scenario.scenario.get());
    else if(active)state->scenario=std::move(active->scenario);
    theWorld.InvalidatePicking();
    PublishWorldServer(current);
    // CreatePlayer normally owns one reference until RemovePlayer. Saved
    // factories instead hold temporary graph/prepared pins; transfer that root.
    if(player)player->AddRef();
    current->script.AdoptSaveState();current->scriptTemp.AdoptSaveState();zone->graph.Adopt();
    if(state->scenario.scenario)state->scenario.graph.Adopt();
    ObjectIdentity::RestoreNamespace::Publish(state->next);
    mod->AdoptSaveState(zone->mod);
    if(clientParty){
        client->GetPlayer()->party.SwapSavedState(*clientParty);
        client->AdoptSavedConnection(clientConnection.GetPtr());
        current->BindSavedCampParty(client->GetPlayer()->party);
    }

    state->random.Apply();theInterface.SetScreensMustDie();
    auto old=std::move(active);active=std::move(state);visited.swap(active->files);
    // Retirement is outside the acceptance decision: never report a failed
    // load after publication. Cleanup still sees its own former world.
    {
        SavedWorldScope cleanupLookups(previous);
        ObjectIdentity::RestoreNamespace cleanup(active->next);
        struct RandomGuard {RandomState before=RandomState::Capture();~RandomGuard(){before.Apply();}} randomGuard;
        if(clientParty){
            // Camp screen deactivation mutates its party and may allocate.
            // Route it to the retained old party/connection, then discard its
            // outgoing client commands before exposing the accepted stream.
            client->GetPlayer()->party.SwapSavedState(*clientParty);
            std::swap(client->GetPlayer()->pConnection,oldConnection);
            theInterface.RetireSavedScreens();
            std::swap(client->GetPlayer()->pConnection,oldConnection);
            client->GetPlayer()->party.SwapSavedState(*clientParty);
            client->AdoptSavedConnection(clientConnection.GetPtr());
        }
        if(oldConnection)oldConnection->Disconnect();
        zone->mod.reset();clientParty.reset();
        if(old)old.reset();
        else try{previous->Destroy();}catch(CException *e){e->Delete();GameJournal::AllocationFailureInCatch("Save64 retirement");}
             catch(...){GameJournal::AllocationFailureInCatch("Save64 retirement");}
    }
    // Managers force-delete their members on Destroy, regardless of refcount.
    // Drop staging pins while every manager is alive, not at process shutdown.
    zone->staged=false;
    zone->graph=Graph{};zone->owners.reset();
    active->scenario.graph=Graph{};active->scenario.owners.reset();
    current->detachedSave=false;
    // The staged loader deliberately never runs the MOB parser. Publish the
    // accepted source to the renderer now, before the client new-zone stream.
    if(!current->szZoneFileName.IsEmpty()&&!current->szMobFileName.IsEmpty()){
        OwnedRender::E1RestoreSavedWorldSource(current->szZoneFileName,current->szMobFileName,renderMembers);
        for(INT_PTR i=0;i<current->szLoadedMobs.GetSize();++i)
            if(current->szLoadedMobs[i].CompareNoCase(current->szMobFileName)!=0)
                OwnedRender::E1RestoreSavedWorldSource(nullptr,current->szLoadedMobs[i],renderMembers);
    }
    if(active->fullSlot){active->uiState.Apply();theInterface.AdoptSavedInterface();}
    // Audio handles belong to the retired world. This is a non-throwing
    // best-effort stop, after acceptance, never a reason to reject a staged slot.
    try{IWorldClient.StopAllSounds();}catch(...){GameJournal::AllocationFailureInCatch("Save64 audio retirement");}
}
bool Slot::LoadWorld(CSave &file, CParty* arriving)
{
    CGSVariableSet entryVars;if(arriving)entryVars=arriving->vars;
    auto state=std::make_unique<State>();state->zoneOnly=true;state->files=visited;auto bytes=Snapshot(file);
    state->zones.push_back(ReadDocument(bytes));auto &d=state->zones.back();state->current=(LPCSTR)d.zone;
    {
        ObjectIdentity::RestoreNamespace allocation((std::max)(d.next,ObjectIdentity::GetNext()));
        PrepareWorld(d);d.camera=std::make_unique<CCamera>();d.camera->detachedSave=true;
        Decode(d,[&](Reader &r){World(r,*d.world);d.camera->Load(r);d.mod=CModPackage::LoadSaveState(r);});
        FinalizeWorld(d,arriving?&entryVars:nullptr);state->next=ObjectIdentity::GetNext();state->random=RandomState::Capture();
    }
    Commit(std::move(state));
    if(arriving)arriving->vars.swap(entryVars);
    return true;
}
CString Slot::Directory()
{
    auto path=std::filesystem::path(ModRuntime::SaveRoot(IWorld.GetWorkPath()))/"Saves64";
    return (path.string()+"\\").c_str();
}
void Slot::NewGame(){visited.clear();presentation.clear();screenshot.clear();}
void Slot::Shutdown()
{
    // Retire accepted save owners before global databases and renderer resources.
    theWorldServer.Destroy();
    PublishWorldServer(nullptr);PublishGlobalHat(nullptr);
    active.reset();
    // The initial new-game scenario survives the first slot adoption. Retire
    // its shops before the global world server's virtual bases are destroyed.
    theHat.Clear();
    visited.clear();presentation.clear();screenshot.clear();
}
bool Slot::HasZone(const char *name){return visited.find(name)!=visited.end();}
bool Slot::StoreZone(const char *name)
{
    CheckedName(name);Document d;BindWorld(d,{&theWorldServer,[](CWorldServer *){}},true);
    // Party objects have already been removed from this zone. The arriving
    // party selects its own camera carrier; only the view belongs to the zone.
    visited[name]=Encode(d,[&](Writer &w){World(w,theWorldServer);theCamera.Store(w,false);if(auto *mod=ModRuntime::Current())mod->StoreSaveState(w);else Invalid("missing mod");});
    return true;
}
bool Slot::LoadZone(const char *name, CParty& arriving)
{
    auto it=visited.find(name);if(it==visited.end())return false;
    CMemorySave f;Buffer(f,it->second);return LoadWorld(f,&arriving);
}
void Slot::SetPresentation(const SSaveSlotDesc &desc,CDXGISurface *surface)
{
    Registry r;CMemorySave f;
    SaveSchema::Store(f,r,[&](Writer &w){Field(w,desc.szName);Field(w,desc.szAllodName);Field(w,desc.szZoneName);w.Float32(desc.fGameTime);w.Int64(desc.time);});
    presentation=Snapshot(f);screenshot.clear();
    if(surface){
        std::vector<DWORD> pixels(size_t(surface->GetWidth())*surface->GetHeight());
        if(FAILED(surface->CopyPixelBuffer(pixels.data(),pixels.size())))throw Error(ErrorCode::Stream,"thumbnail capture failed");
        Registry registry;CMemorySave image;
        SaveSchema::Store(image,registry,[&](Writer &w){w.UInt32(surface->GetWidth());w.UInt32(surface->GetHeight());Count(w,pixels.size());for(auto p:pixels)w.UInt32(p);},SlotLimits());
        screenshot=Snapshot(image);
    }
}
// The complete envelope is read for metadata too: an info/shot from a different
// generation cannot be listed as belonging to this slot.
bool Slot::Presentation(const char *name,SSaveSlotDesc &desc,Bytes *shot)
{
    try{
        CheckedName(name);auto bytes=ReadFile(Directory()+name+CString("\\slot.sav"));
        CMemorySave f;Buffer(f,bytes);Registry reg;Bytes info,image;
        auto graph=SaveSchema::Load(f,reg,[&](Reader &r){
            if(!SaveModIdentity::CurrentEnvelope(r.String()))Invalid("unsupported slot root");
            r.UInt64();r.String();r.String();r.UInt64();r.Blob();info=r.Blob();image=r.Blob();
        },SlotLimits());
        CMemorySave metadata;Buffer(metadata,info);auto g=SaveSchema::Load(metadata,reg,[&](Reader &r){
            Field(r,desc.szName);Field(r,desc.szAllodName);Field(r,desc.szZoneName);desc.fGameTime=r.Float32();
            auto time=r.Int64();if(time<0||time>INT32_MAX)Invalid("slot timestamp out of range");desc.time=static_cast<__time32_t>(time);
        });
        desc.szPath=name;desc.bNoInfo=false;if(shot)*shot=std::move(image);return true;
    }catch(...){return false;}
}
bool Slot::Save(const char *name)
{
    if(ModEncounter::AccessLocked())return false;
    // All automatic, quick and manual slot writes converge here. Owner slots
    // are never overwritten after MCP takes control of this process.
    const auto protectedName=LiveObserver::ProtectedSaveSlot(name);name=protectedName.c_str();
    try{
        CheckedName(name);if(theWorldServer.GetServerMode()!=SERV_MODE_LOCAL)Invalid("save requires a local world");
        auto *player=theWorldServer.GetPlayer(0);if(!player)Invalid("save requires a local player");
        if(ModEncounter::Arena(theWorldServer))for(int i=0;i<player->party.people.GetSize();++i){
            auto* unit=theWorldServer.GetUnit(player->party.people[i].nID);
            if(auto* server=unit?dynamic_cast<CUnitServer*>(unit->GetUnitSpecific()):nullptr)server->FinishEncounterPerformance();
        }
        if(!ModArena::Active())ModArena::ClearSavedStatuses(theWorldServer);
        // Keep the existing gameplay snapshot operations, before capturing RNG.
        if(auto *mod=ModRuntime::Current())mod->SaveSkillCooldowns(player->party.vars);
        theHat.SetSinglePlayerParty(player->party);
        Document scenario;scenario.kind=2;scenario.scenario={&theHat,[](CGlobalHat *){}};
        std::map<int,CParty *> sorted;for(auto &p:theHat.parties)sorted.emplace(p.first.nSomeNumber,&p.second);
        for(auto &p:sorted){scenario.partyKeys.push_back(p.first);BindParty(scenario,*p.second,scenario.scenario);}
        for(auto &p:theHat.singleParty.parties){scenario.singleNames.push_back(p->szName);BindParty(scenario,p->heroes,scenario.scenario);}
        for(auto &s:theHat.shops.shops){scenario.shopKeys.push_back(s->nID);BindParty(scenario,s->itemAssort,scenario.scenario);BindParty(scenario,s->spellAssort,scenario.scenario);}
        auto scenarioBytes=Encode(scenario,[&](Writer &w){Scenario(w,theHat);});
        std::map<std::string,Bytes> files=visited;
        const std::string current=(LPCSTR)player->party.szCurrentZone;CheckedName(current.c_str());
        Document currentWorld;BindWorld(currentWorld,{&theWorldServer,[](CWorldServer *){}});currentWorld.zone=current.c_str();
        files[current]=Encode(currentWorld,[&](Writer &w){World(w,theWorldServer);theCamera.Store(w);ModRuntime::Current()->StoreSaveState(w);});
        // All visited zones survive, irrespective of the selected allod.
        CMemorySave contents;Registry types;
        RandomState rng=RandomState::Capture();const auto next=ObjectIdentity::GetNext();
        SaveSchema::Store(contents,types,[&](Writer &w){w.UInt64(next);w.Value(rng);Interface(w,theInterface);
            auto *mod=ModRuntime::Current();w.Bool(mod&&mod->HidePartyHelmets());w.Bool(mod&&mod->HidePartyUpperArmor());
            w.Blob(scenarioBytes);Count(w,files.size());for(const auto &f:files){w.String(f.first);w.UInt64(Content(f.second));w.Blob(f.second);}
        },SlotLimits());
        auto payload=Snapshot(contents);
        if(presentation.empty()){
            SSaveSlotDesc desc;desc.szName=name;desc.szZoneName=current.c_str();desc.szAllodName="";desc.fGameTime=player->party.vars.GetVar(szPartyGlobalTimeVarName);desc.time=CTime32::GetCurrentTime();SetPresentation(desc,nullptr);
        }
        FILETIME time;GetSystemTimeAsFileTime(&time);const std::uint64_t generation=(std::uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime;
        CMemorySave envelope;Registry meta;
        SaveSchema::Store(envelope,meta,[&](Writer &w){w.String(SaveModIdentity::SlotRoot);w.UInt64(generation);
            auto *mod=ModRuntime::Current();if(!mod)Invalid("slot requires loaded mod");w.String(mod->SaveIdentity());w.String(current);
            w.UInt64(Content(payload));w.Blob(payload);w.Blob(presentation);w.Blob(screenshot);
        },SlotLimits());
        std::filesystem::create_directories(std::filesystem::path((LPCSTR)Directory())/name);
        AtomicFile(Directory()+name+CString("\\slot.sav"),Snapshot(envelope));
        presentation.clear();screenshot.clear();
        GameJournal::Event("SAVE","complete","slot=%s source=%s",GameJournal::Quote(name).c_str(),GameJournal::Quote(GameJournal::CurrentSource()).c_str());
        return true;
    }catch(const std::exception &e){ReportError("Save64: %s",e.what());return false;}
    catch(CException *e){e->Delete();ReportError("Save64: file or memory error");return false;}
}
bool Slot::Load(const char *name)
{
    try{
        CheckedName(name);const auto bytes=ReadFile(Directory()+name+CString("\\slot.sav"));
        auto state=std::make_unique<State>();state->fullSlot=true;Bytes payload;Registry meta;CMemorySave f;Buffer(f,bytes);
        auto envelope=SaveSchema::Load(f,meta,[&](Reader &r){
            if(!SaveModIdentity::CurrentEnvelope(r.String()))throw Error(ErrorCode::Incompatible,"Incompatible save");
            if(!r.UInt64())Invalid("invalid slot generation");auto mod=r.String();if(!ModRuntime::Current()||!SaveModIdentity::BelongsTo(mod,ModRuntime::Current()->SaveIdentity()))Invalid("slot belongs to another mod");
            state->current=r.String();CheckedName(state->current.c_str());const auto content=r.UInt64();payload=r.Blob();
            if(Content(payload)!=content)Invalid("slot content mismatch");r.Blob();r.Blob();
        },SlotLimits());
        CMemorySave contents;Buffer(contents,payload);Registry types;
        Bytes scenario;
        auto graph=SaveSchema::Load(contents,types,[&](Reader &r){state->next=r.UInt64();r.Value(state->random);state->uiState.Read(r);state->helmets=r.Bool();state->upper=r.Bool();
            scenario=r.Blob();const auto n=r.Count();for(std::uint32_t i=0;i<n;++i){auto key=r.String();CheckedName(key.c_str());auto content=r.UInt64();auto document=r.Blob();
                if(Content(document)!=content||!state->files.emplace(key,std::move(document)).second)Invalid("invalid/duplicate zone document");}
        },SlotLimits());
        state->scenario=ReadDocument(scenario);
        ObjectId minimum=(std::max)(state->next,ObjectIdentity::GetNext());minimum=(std::max)(minimum,state->scenario.next);
        std::set<ObjectId> owners;

        for(const auto &file:state->files){auto d=ReadDocument(file.second);if(d.kind!=1||d.zone!=file.first.c_str())Invalid("zone directory mismatch");minimum=(std::max)(minimum,d.next);
            state->zones.push_back(std::move(d));}
        {
            ObjectIdentity::RestoreNamespace allocation(minimum);
            PrepareScenario(state->scenario);Decode(state->scenario,[&](Reader &r){Scenario(r,*state->scenario.scenario);});
            for(auto &d:state->zones){PrepareWorld(d);d.camera=std::make_unique<CCamera>();d.camera->detachedSave=true;Decode(d,[&](Reader &r){World(r,*d.world);d.camera->Load(r);d.mod=CModPackage::LoadSaveState(r);});
            }
            for(auto &d:state->zones)FinalizeWorld(d);
            state->next=ObjectIdentity::GetNext();
        }
        state->zones.erase(std::remove_if(state->zones.begin(),state->zones.end(),[&](const Document &d){return d.zone!=state->current.c_str();}),state->zones.end());
        Commit(std::move(state));
        GameJournal::Event("SAVE","loaded","slot=%s",GameJournal::Quote(name).c_str());
        return true;
    }catch(const Error &e){ReportError("Save64: %s",e.what());return false;}
    catch(const std::exception &e){ReportError("Save64: %s",e.what());return false;}
    catch(CException *e){e->Delete();ReportError("Save64: file or memory error");return false;}
}
} // namespace SaveSchema
