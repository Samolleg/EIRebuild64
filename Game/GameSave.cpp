#include "StdAfx.h"
#include "GameSave.h"
#include "UnitServer.h"
#include "UnitClientSpecific.h"
#include "MapObjectServer.h"
#include "StuffMapObjects.h"
#include "EffectSpell.h"
#include "Item.h"
#include "SpellDatabase.h"
#include "Party.h"
#include "Camera.h"
#include "UnitStatisticsDriver.h"
#include "PlayerServer.h"
#include "PlayerClient.h"
#include "MapObjectManager.h"
#include "ServerSpecificManager.h"
#include "ClientSpecificManager.h"
#include "ItemManager.h"
#include "SpellManager.h"
#include "EffectManager.h"
#include "PlayerManager.h"
#include "Object3DServer.h"
#include "Object3DClientSpecific.h"
#include "MapObjectClientSpecific.h"
#include "StuffMOClientSpec.h"
#include "FigureClientSpecific.h"

namespace SaveSchema::Fields {
// Party arrays add ownership swapping, but keep the base array's field codec.
// Template specialization does not select Codec<Base> for a derived container.
template<class T, class Base> struct Codec<CPartySaveArray<T, Base>> : Codec<Base> {};

template<> struct Codec<D3DCOLORVALUE>
{
    static void Store(Writer &w, const D3DCOLORVALUE &v)
    { w.Float32(v.r); w.Float32(v.g); w.Float32(v.b); w.Float32(v.a); }
    static void Load(Reader &r, D3DCOLORVALUE &v)
    { v.r=r.Float32(); v.g=r.Float32(); v.b=r.Float32(); v.a=r.Float32(); }
    static void Clear(D3DCOLORVALUE &) {}
};
template<> struct Codec<SDeployUnitInfo>
{
    static void Store(Writer &w, const SDeployUnitInfo &v)
    { w.MapReference(v.nID); Field(w,v.ptPlace); Field(w,v.orient); Field(w,v.pose); Field(w,v.dwCurrentWeapon); }
    static void Load(Reader &r, SDeployUnitInfo &v)
    { v.nID=r.MapReference(); Field(r,v.ptPlace); Field(r,v.orient); Field(r,v.pose); Field(r,v.dwCurrentWeapon); }
    static void Clear(SDeployUnitInfo &) {}
};
template<> struct Codec<SDeployPlayerInfo>
{
    static void Store(Writer &w, const SDeployPlayerInfo &v) { Field(w,v.units); }
    static void Load(Reader &r, SDeployPlayerInfo &v) { Field(r,v.units); }
    static void Clear(SDeployPlayerInfo &) {}
};
template<> struct Codec<CSpellEffect>
{
    static void Store(Writer &w, const CSpellEffect &v)
    { w.UInt8(v.nSpellID); w.UInt8(v.nVisStrength); w.Int32(v.nDuration); w.Float32(v.fpPower); w.Float32(v.fpRadius); w.Bool(v.bQuiet); }
    static void Load(Reader &r, CSpellEffect &v)
    { v.nSpellID=r.UInt8(); v.nVisStrength=r.UInt8(); v.nDuration=r.Int32(); v.fpPower=r.Float32(); v.fpRadius=r.Float32(); v.bQuiet=r.Bool(); }
    static void Clear(CSpellEffect &) {}
};
// Runtime IDs are keys in these lookup caches, but the saved targets are ObjectId.
template<class K, class T> struct Codec<CHashMap<K,T *>>
{
    static void Store(Writer &w, const CHashMap<K,T *> &v)
    {
        Count(w,v.GetCount());
        std::map<DWORD,T *> ordered(v.begin(),v.end());
        for (const auto &x : ordered) w.GameReference(x.second);
    }
    static void Load(Reader &r, CHashMap<K,T *> &v)
    {
        Clear(v); const auto n=r.Count();
        for (std::uint32_t i=0;i<n;++i)
        {
            auto *object=r.GameReferenceAs<T>();
            if (!object || v.Seek(static_cast<K>(object->GetID())))
                throw Error(ErrorCode::Invalid,"null/duplicate hash reference");
            v.SetAt(static_cast<K>(object->GetID()),object);
            object->AddRef();
        }
    }
    static void Clear(CHashMap<K,T *> &v)
    {
        for (const auto &x : v) if (x.second) x.second->Release();
        v.RemoveAll();
    }
};
}

using namespace SaveSchema::Fields;

void CGameBase::Store(SaveSchema::Writer &) const { SaveSchema::RequireDynamicSchema(); }
void CGameBase::Load(SaveSchema::Reader &) { SaveSchema::RequireDynamicSchema(); }

void CBaseManager::RestoreSavedKeyRange(int first, int last, int previous)
{
    if (!ObjectIdentity::RestoreNamespace::Active() ||
        first < 0 || last < first || (previous != -1 && (previous < first || previous > last)))
        throw SaveSchema::Error(SaveSchema::ErrorCode::Invalid, "invalid staged manager key range");
    ForEach([&](CGameBase *object) {
        if (object->GetID()<first || object->GetID()>last)
            throw SaveSchema::Error(SaveSchema::ErrorCode::Identity,"saved member outside manager key range");
    });
    nKeyRangeFrom=first; nKeyRangeTo=last; nLastKey=previous;
}

namespace SaveSchema {
void OwnerDescription::Store(Writer &w) const
{ w.UInt64(id); Field(w,kind); Field(w,first); Field(w,last); Field(w,previous); }
void OwnerDescription::Load(Reader &r)
{
    id=r.UInt64(); Field(r,kind); Field(r,first); Field(r,last); Field(r,previous);
    if (!id || kind < OwnerKind::Map || kind > OwnerKind::Player || first < 0 || last < first ||
        (previous != -1 && (previous < first || previous > last)))
        throw Error(ErrorCode::Invalid, "invalid saved owner description");
}
void SavedOwners::Add(OwnerKind kind, std::shared_ptr<CBaseManager> manager)
{
    if (!manager) throw Error(ErrorCode::Invalid, "missing staged owner");
    bool matches = false;
    switch (kind)
    {
    case OwnerKind::Map: matches=typeid(*manager)==typeid(CMapObjectManager); break;
    case OwnerKind::ServerSpecific: matches=typeid(*manager)==typeid(CServerSpecificManager); break;
    case OwnerKind::ClientSpecific: matches=typeid(*manager)==typeid(CClientSpecificManager); break;
    case OwnerKind::Item: matches=typeid(*manager)==typeid(CItemManager); break;
    case OwnerKind::Spell: matches=typeid(*manager)==typeid(CSpellManager); break;
    case OwnerKind::Effect: matches=typeid(*manager)==typeid(CEffectManager); break;
    case OwnerKind::Player: matches=typeid(*manager)==typeid(CPlayerManager); break;
    }
    if (!matches || !entries.emplace(manager->GetRTID(), Entry{kind,manager}).second)
        throw Error(ErrorCode::Identity, "duplicate or mismatched staged owner");
}
std::vector<OwnerDescription> SavedOwners::Describe() const
{
    std::vector<OwnerDescription> result;
    for (const auto &x : entries)
    {
        OwnerDescription d; d.id=x.first; d.kind=x.second.kind;
        x.second.manager->SavedKeyRange(d.first,d.last,d.previous);
        result.push_back(d);
    }
    return result;
}
CBaseManager &SavedOwners::Find(ObjectId id, OwnerKind kind) const
{
    const auto it=entries.find(id);
    if (it==entries.end() || it->second.kind!=kind)
        throw Error(ErrorCode::Identity,"missing or wrong saved manager owner");
    return *it->second.manager;
}
CGameBase *SavedOwners::Map(int id) const
{
    CGameBase *result=nullptr;
    for (const auto &x : entries)
        if (x.second.kind==OwnerKind::Map)
            if (auto *object=x.second.manager->Find(id))
            {
                if (result) throw Error(ErrorCode::Identity,"ambiguous map ID across staged zones");
                result=object;
            }
    return result;
}
void SavedOwners::StoreMembers(Writer &w) const
{
    Count(w,entries.size());
    for (const auto &x : entries)
    {
        OwnerDescription description;
        description.id=x.first; description.kind=x.second.kind;
        x.second.manager->SavedKeyRange(description.first,description.last,description.previous);
        description.Store(w);
        std::vector<CGameBase *> members;
        x.second.manager->ForEach([&](CGameBase *object) {
            if (omitPlayers && (x.second.kind==OwnerKind::Player ||
                (x.second.kind==OwnerKind::ServerSpecific && object->GetTypeID()==SPTID_PLAYER_SPECIFIC))) return;
            // Figure specifics contain renderer resources, not simulation state.
            // They are recreated from the staged model at P17c adoption.
            if (x.second.kind==OwnerKind::ClientSpecific && object->GetTypeID()==SPTID_FIGURE_SPECIFIC) return;
            members.push_back(object);
        });
        Count(w,members.size());
        for (auto *object : members) w.GameReference(object);
    }
}
void SavedOwners::LoadMembers(Reader &r) const
{
    const auto n=r.Count();
    if (n!=entries.size()) throw Error(ErrorCode::Identity,"owner manifest count mismatch");
    std::map<ObjectId,bool> owners, members;
    for (std::uint32_t i=0;i<n;++i)
    {
        OwnerDescription description; description.Load(r);
        const auto id=description.id; const auto it=entries.find(id);
        if (it==entries.end() || !owners.emplace(id,true).second)
            throw Error(ErrorCode::Identity,"unknown/duplicate manifest owner");
        if (it->second.kind!=description.kind) throw Error(ErrorCode::Identity,"manifest owner kind mismatch");
        it->second.manager->RestoreSavedKeyRange(description.first,description.last,description.previous);
        const auto count=r.Count();
        if (count!=it->second.manager->GetNumElements())
            throw Error(ErrorCode::Identity,"owner membership count mismatch");
        for (std::uint32_t j=0;j<count;++j)
        {
            auto *object=r.GameReference();
            if (!object || object->GetManager()!=it->second.manager.get() ||
                !members.emplace(object->GetRTID(),true).second)
                throw Error(ErrorCode::Identity,"wrong or duplicate owner member");
        }
    }
}
namespace {
template<class T> void Game(Registry &r, std::shared_ptr<SavedOwners> owners,
                            std::uint32_t id, OwnerKind kind, std::initializer_list<int> runtimeTypes)
{
    const std::vector<int> allowed(runtimeTypes);
    r.RegisterGame(id,typeid(T),{
        [owners,kind,allowed](ObjectId saved, int mapId, std::uint32_t type, std::uint32_t subtype, ObjectId owner) {
            const auto runtime=static_cast<ETypeIDs>(type);
            if (std::find(allowed.begin(),allowed.end(),runtime)==allowed.end())
                throw Error(ErrorCode::UnknownType,std::string("saved runtime type does not match schema: ") + typeid(T).name() + " runtimeType=" + std::to_string(type));
            auto found=owners->prepared.find(saved);
            auto object=found!=owners->prepared.end() ? found->second :
                owners->Find(owner,kind).CreateSavedData(runtime,mapId,saved);
            if (!dynamic_cast<T *>(static_cast<CGameBase *>(object)) || typeid(*static_cast<CGameBase *>(object))!=typeid(T))
                throw Error(ErrorCode::UnknownType,std::string("manager factory returned wrong class for ") + typeid(T).name() + " runtimeType=" + std::to_string(type));
            object->SetSubtypeID(subtype);
            return object;
        },
        [](Writer &w, CGameBase &object) { static_cast<const T &>(object).Store(w); },
        [](Reader &r, CGameBase &object) { static_cast<T &>(object).Load(r); },
        [](CGameBase &object) { static_cast<T &>(object).DiscardSave(); }});
}
template<class T> void Motivation(Registry &r, std::uint32_t id)
{
    r.Register(id,typeid(T),{
        [](ObjectId saved) { return ObjectIdentity::Create<T,CPtr<CFundament>>(saved); },
        [](Writer &w, CFundament &object) { static_cast<T &>(object).Store(w); },
        [](Reader &r, CFundament &object) { static_cast<T &>(object).Load(r); },
        [](CFundament &object) { static_cast<T &>(object).DiscardSave(); }});
}
}
void RegisterGameTypes(Registry &r, std::shared_ptr<SavedOwners> owners)
{
    if (!owners) throw Error(ErrorCode::Invalid,"missing saved owner manifest");
    r.SetGameObjects({[owners](int id) { return owners->Map(id); }});
    Game<CUnit>(r,owners,0x1701,OwnerKind::Map,{MOTID_UNIT,MOTID_HUMAN,MOTID_ANIMAL,MOTID_MONSTER});
    Game<CTree>(r,owners,0x1702,OwnerKind::Map,{MOTID_TREE});
    Game<CStone>(r,owners,0x1703,OwnerKind::Map,{MOTID_STONE});
    Game<CBuilding>(r,owners,0x1704,OwnerKind::Map,{MOTID_BUILDING});
    Game<CStuffObject>(r,owners,0x1705,OwnerKind::Map,{MOTID_OBJECT});
    Game<CTrapObject>(r,owners,0x1706,OwnerKind::Map,{MOTID_KAPKAN});
    Game<CMagicTrapObject>(r,owners,0x1707,OwnerKind::Map,{MOTID_TRAP});
    Game<CLeverObject>(r,owners,0x1708,OwnerKind::Map,{MOTID_LEVER});
    Game<CTorchObject>(r,owners,0x1709,OwnerKind::Map,{MOTID_TORCH});
    Game<CObject3DServer>(r,owners,0x1710,OwnerKind::ServerSpecific,{SPTID_OBJECT3D_SPECIFIC});
    Game<CMapObjectServer>(r,owners,0x1711,OwnerKind::ServerSpecific,{SPTID_MAPOBJECT_SPECIFIC});
    Game<CUnitServer>(r,owners,0x1712,OwnerKind::ServerSpecific,{SPTID_UNIT_SPECIFIC});
    Game<CTorchObjectSpecific>(r,owners,0x1713,OwnerKind::ServerSpecific,{SPTID_TORCH_SPECIFIC});
    Game<CObject3DClientSpecific>(r,owners,0x1714,OwnerKind::ClientSpecific,{SPTID_OBJECT3D_SPECIFIC});
    Game<CMapObjectClientSpecific>(r,owners,0x1715,OwnerKind::ClientSpecific,{SPTID_MAPOBJECT_SPECIFIC});
    Game<CUnitClientSpecific>(r,owners,0x1716,OwnerKind::ClientSpecific,{SPTID_UNIT_SPECIFIC});
    Game<CTorchObjectClient>(r,owners,0x1717,OwnerKind::ClientSpecific,{SPTID_TORCH_SPECIFIC});
    Game<CItemWeapon>(r,owners,0x1720,OwnerKind::Item,{ITID_ITEM_WEAPON});
    Game<CItemArmor>(r,owners,0x1721,OwnerKind::Item,{ITID_ITEM_ARMOR});
    Game<CItemQuick>(r,owners,0x1722,OwnerKind::Item,{ITID_ITEM_QUICK});
    Game<CItemQuest>(r,owners,0x1723,OwnerKind::Item,{ITID_ITEM_QUEST});
    Game<CItemLoot>(r,owners,0x1724,OwnerKind::Item,{ITID_ITEM_LOOT});
    Game<CItemSpell>(r,owners,0x1725,OwnerKind::Item,{ITID_ITEM_SPELL});
    Game<CSpell>(r,owners,0x1726,OwnerKind::Spell,{SSOTID_SPELL});
    Game<CSpellContainer>(r,owners,0x1727,OwnerKind::Spell,{SSOTID_SPELL_CONTAINER});
    Game<CEffectSound>(r,owners,0x1730,OwnerKind::Effect,{HPC_EFFECTSOUND});
    Game<CEffectSoundSeq>(r,owners,0x1731,OwnerKind::Effect,{HPC_EFFECTSOUNDSEQ});
    Game<CEffectAmbientSound>(r,owners,0x1732,OwnerKind::Effect,{HPC_EFFECTAMBIENTSOUND});
    Game<CEffectPointLight>(r,owners,0x1733,OwnerKind::Effect,{HPC_EFFECTPOINTLIGHT});
    Game<CEffectParticle>(r,owners,0x1734,OwnerKind::Effect,{HPC_EFFECTPARTICLE});
    Game<CEffectWall>(r,owners,0x1735,OwnerKind::Effect,{HPC_EFFECTWALL});
    Game<CEffectLightning>(r,owners,0x1736,OwnerKind::Effect,{HPC_EFFECTLIGHTNING});
    Game<CEffectArrow>(r,owners,0x1737,OwnerKind::Effect,{HPC_EFFECTARROW});
    Game<CEffectArrowFigure>(r,owners,0x1738,OwnerKind::Effect,{HPC_EFFECTARROWFIGURE});
    Game<CEffectShowVisibility>(r,owners,0x1739,OwnerKind::Effect,{HPC_EFFECTSHOWVISIBILITY});
    Game<CEffectMoshka>(r,owners,0x173a,OwnerKind::Effect,{HPC_EFFECTMOSHKA});
    Game<CEffectTornado>(r,owners,0x173b,OwnerKind::Effect,{HPC_EFFECTTORNADO});
    Game<CPlayer>(r,owners,0x1740,OwnerKind::Player,{STID_PLAYER});
    Game<CPlayerServer>(r,owners,0x1741,OwnerKind::ServerSpecific,{SPTID_PLAYER_SPECIFIC});
    Game<CPlayerClient>(r,owners,0x1742,OwnerKind::ClientSpecific,{SPTID_PLAYER_SPECIFIC});
    Motivation<CUMGuard>(r,0x1750); Motivation<CUMPatrol>(r,0x1751);
    Motivation<CUMSentry>(r,0x1752); Motivation<CUMFollow>(r,0x1753);
    Motivation<CUMSuspection>(r,0x1754); Motivation<CUMAggression>(r,0x1755);
    Motivation<CUMFear>(r,0x1756); Motivation<CUMPlayer>(r,0x1757);
    Motivation<CUMTamed>(r,0x1758); Motivation<CUMCorpseWatcher>(r,0x1759);
    r.Register(0x1760,typeid(CEffectSpell),{
        [](ObjectId saved) { return ObjectIdentity::Create<CEffectSpell,CPtr<CFundament>>(saved); },
        [](Writer &w, CFundament &object) { static_cast<CEffectSpell &>(object).Store(w); },
        [](Reader &r, CFundament &object) { static_cast<CEffectSpell &>(object).Load(r); },
        [](CFundament &object) { static_cast<CEffectSpell &>(object).DiscardSave(); }});
}
} // namespace SaveSchema

void CEffectSpell::AdoptSavedState(CWorldServer &world)
{
    if (!savedStatePending) throw SaveSchema::Error(SaveSchema::ErrorCode::Invalid,"spell already adopted");
    pWS=&world;
    if (savedDangerMapping) AddRemoveDangerArea(spell.GetSubtypeID() >> 16,TRUE);
    savedStatePending=false;
}

void CUnitStatisticsDriver::Store(SaveSchema::Writer &w) const
{
    std::uint64_t count=0;
    for (const auto &group : units) count+=group.second.size();
    Count(w,count);
    for (const auto &group : units)
        for (const auto &unit : group.second) w.Value(unit.second);
    Count(w,wears.size());
    for (const auto &entry : wears) w.Value(entry.second);
}
void CUnitStatisticsDriver::Load(SaveSchema::Reader &r, std::vector<SUnitStatistics> &statistics,
                               std::vector<SUnitWearsStatistics> &wears)
{
    statistics.resize(r.Count());
    for (auto &entry : statistics) r.Value(entry);
    wears.resize(r.Count());
    for (auto &entry : wears) r.Value(entry);
}

void CObject3DSpecific::Store(SaveSchema::Writer &w) const
{
    w.GameReference(lpObject);
}
void CObject3DSpecific::Load(SaveSchema::Reader &r)
{
    lpObject = r.GameReferenceAs<std::remove_pointer_t<decltype(lpObject)>>();
}
void CObject3DSpecific::DiscardSave()
{
    lpObject = nullptr;
}

void CMapObjectSpecific::Store(SaveSchema::Writer &w) const
{
    w.GameReference(lpMapObject);
}
void CMapObjectSpecific::Load(SaveSchema::Reader &r)
{
    lpMapObject = r.GameReferenceAs<std::remove_pointer_t<decltype(lpMapObject)>>();
}
void CMapObjectSpecific::DiscardSave()
{
    lpMapObject = nullptr;
}

void CUnitSpecific::Store(SaveSchema::Writer &w) const
{
    w.GameReference(lpUnit);
}
void CUnitSpecific::Load(SaveSchema::Reader &r)
{
    lpUnit = r.GameReferenceAs<std::remove_pointer_t<decltype(lpUnit)>>();
}
void CUnitSpecific::DiscardSave()
{
    lpUnit = nullptr;
}

void CObject3D::Store(SaveSchema::Writer &w) const
{
    Field(w, lpObjectSpecific);
}
void CObject3D::Load(SaveSchema::Reader &r)
{
    Field(r, lpObjectSpecific);
}
void CObject3D::DiscardSave()
{
    ClearField(lpObjectSpecific);
}

void CMapObject::Store(SaveSchema::Writer &w) const
{
    CObject3D::Store(w);
    Field(w, lpMapObjectSpecific);
    Field(w, fpAnimationCoeff);
    Field(w, cp);
    Field(w, rotation);
    Field(w, szMaskName);
    Field(w, complection);
    Field(w, spellEffects);
    Field(w, szAttachedQuest);
    Field(w, baseComplection);
}
void CMapObject::Load(SaveSchema::Reader &r)
{
    CObject3D::Load(r);
    Field(r, lpMapObjectSpecific);
    Field(r, fpAnimationCoeff);
    Field(r, cp);
    Field(r, rotation);
    Field(r, szMaskName);
    Field(r, complection);
    Field(r, spellEffects);
    Field(r, szAttachedQuest);
    Field(r, baseComplection);
}
void CMapObject::DiscardSave()
{
    ClearField(lpMapObjectSpecific);
    spellEffects.RemoveAll();
    CObject3D::DiscardSave();
}

void CMapObjectServer::Store(SaveSchema::Writer &w) const
{
    CMapObjectSpecific::Store(w);
    Field(w, rpgCommonStats);
    Field(w, szPrimTexture);
    Field(w, szSecTexture);
    Field(w, szNames);
}
void CMapObjectServer::Load(SaveSchema::Reader &r)
{
    CMapObjectSpecific::Load(r);
    Field(r, rpgCommonStats);
    Field(r, szPrimTexture);
    Field(r, szSecTexture);
    Field(r, szNames);
}
void CMapObjectServer::DiscardSave()
{
    CMapObjectSpecific::DiscardSave();
}

void CUnit::Store(SaveSchema::Writer &w) const
{
    CMapObject::Store(w);
    Field(w, lpUnitSpecific);
    w.GameReference(lpOwner);
    Field(w, szName);
    Field(w, dwEnemyMask);
    Field(w, nPlayerNum);
    Field(w, nStartAnimation);
    Field(w, nCycleAnimation);
    Field(w, fullStartAnimation);
    Field(w, fullCycleAnimation);
    Field(w, bExtendedAnimationId);
    Field(w, nAnimationSound);
    Field(w, nAnimationStartSegment);
    Field(w, animationSpeed);
    Field(w, locomotionSpeed);
    Field(w, currentPose);
    Field(w, wishPose);
    Field(w, dwCurrentWeapon);
    Field(w, dwWishWeapon);
    Field(w, bDead);
    Field(w, bMoving);
    Field(w, walker);
    Field(w, fpBaseZ);
    Field(w, nAIClass);
    Field(w, fpAttackDistance);
    Field(w, lpQWeapons);
    Field(w, lpQItems);
    Field(w, lpQSpells);
    Field(w, lpArmors);
    Field(w, sensesDetection);
    Field(w, dwPossibleEnemies);
    Field(w, questItems);
    Field(w, fpCollisionDistance);
    Field(w, aggression);
    Field(w, dwVoicePrototype);
}
void CUnit::Load(SaveSchema::Reader &r)
{
    CMapObject::Load(r);
    Field(r, lpUnitSpecific);
    lpOwner = r.GameReferenceAs<std::remove_pointer_t<decltype(lpOwner)>>();
    Field(r, szName);
    Field(r, dwEnemyMask);
    Field(r, nPlayerNum);
    Field(r, nStartAnimation);
    Field(r, nCycleAnimation);
    Field(r, fullStartAnimation);
    Field(r, fullCycleAnimation);
    Field(r, bExtendedAnimationId);
    Field(r, nAnimationSound);
    Field(r, nAnimationStartSegment);
    Field(r, animationSpeed);
    Field(r, locomotionSpeed);
    Field(r, currentPose);
    Field(r, wishPose);
    Field(r, dwCurrentWeapon);
    Field(r, dwWishWeapon);
    Field(r, bDead);
    Field(r, bMoving);
    Field(r, walker);
    Field(r, fpBaseZ);
    Field(r, nAIClass);
    Field(r, fpAttackDistance);
    Field(r, lpQWeapons);
    Field(r, lpQItems);
    Field(r, lpQSpells);
    Field(r, lpArmors);
    Field(r, sensesDetection);
    Field(r, dwPossibleEnemies);
    Field(r, questItems);
    Field(r, fpCollisionDistance);
    Field(r, aggression);
    Field(r, dwVoicePrototype);
    rpgUserUnitStats.Clear();
}
void CUnit::DiscardSave()
{
    lpOwner = nullptr;
    ClearField(lpUnitSpecific);
    ClearField(lpQWeapons);
    ClearField(lpQItems);
    ClearField(lpQSpells);
    ClearField(lpArmors);
    ClearField(questItems);
    CMapObject::DiscardSave();
}

void CUnitServer::Store(SaveSchema::Writer &w) const
{
    CUnitSpecific::Store(w);
    Field(w, bL0Frozen);
    Field(w, bL0MovementFrozen);
    Field(w, level0Animation);
    Field(w, path);
    Field(w, level1Command);
    Field(w, bL1PlayingSuffer);
    Field(w, bL1MovementFailed);
    Field(w, bL1SpeedChanged);
    Field(w, bL1MoveAnimationSet);
    Field(w, sufferAnimationMem);
    Field(w, nL1SufferDirection);
    Field(w, eL1State);
    Field(w, currentPose);
    Field(w, wishPose);
    Field(w, nCanAttackTime);
    Field(w, nMoveToTargetTime);
    Field(w, nL1RunFailCounter);
    Field(w, curCommand);
    Field(w, nextCommand);
    Field(w, rpgUnitStats);
    Field(w, fpModSound);
    Field(w, fpSModSound);
    Field(w, fpModVision);
    Field(w, fpSModVision);
    Field(w, tameLevel);
    Field(w, nOriginalPlayerNum);
    Field(w, nPlayerWhoTamed);
    Field(w, nFootstepGenInterval);
    Field(w, dwFutureEnemies);
    Field(w, logic);
    Field(w, acknowledgements);
    Field(w, bLcCommandFail);
    Field(w, bLcCompletelyDead);
    Field(w, fpZSpeed);
    Field(w, pCastParticle);
    Field(w, activeContSpells);
    Field(w, train);
    Field(w, nAttackParamsSetTime);
    Field(w, bLcChangingPoseToAttack);
    Field(w, rpgDefend);
    Field(w, nBoredCounter);
    Field(w, level1AP);
    Field(w, nCommandExecStart);
}
void CUnitServer::Load(SaveSchema::Reader &r)
{
    CUnitSpecific::Load(r);
    Field(r, bL0Frozen);
    Field(r, bL0MovementFrozen);
    Field(r, level0Animation);
    Field(r, path);
    Field(r, level1Command);
    Field(r, bL1PlayingSuffer);
    Field(r, bL1MovementFailed);
    Field(r, bL1SpeedChanged);
    Field(r, bL1MoveAnimationSet);
    Field(r, sufferAnimationMem);
    Field(r, nL1SufferDirection);
    Field(r, eL1State);
    Field(r, currentPose);
    Field(r, wishPose);
    Field(r, nCanAttackTime);
    Field(r, nMoveToTargetTime);
    Field(r, nL1RunFailCounter);
    Field(r, curCommand);
    Field(r, nextCommand);
    Field(r, rpgUnitStats);
    Field(r, fpModSound);
    Field(r, fpSModSound);
    Field(r, fpModVision);
    Field(r, fpSModVision);
    Field(r, tameLevel);
    Field(r, nOriginalPlayerNum);
    Field(r, nPlayerWhoTamed);
    Field(r, nFootstepGenInterval);
    Field(r, dwFutureEnemies);
    Field(r, logic);
    Field(r, acknowledgements);
    Field(r, bLcCommandFail);
    Field(r, bLcCompletelyDead);
    Field(r, fpZSpeed);
    Field(r, pCastParticle);
    Field(r, activeContSpells);
    Field(r, train);
    Field(r, nAttackParamsSetTime);
    Field(r, bLcChangingPoseToAttack);
    Field(r, rpgDefend);
    Field(r, nBoredCounter);
    Field(r, level1AP);
    Field(r, nCommandExecStart);
    dwSpellAddonLostSight = 0;
    lastFootstep = nullptr;
    animMoveCache.Clear();
    animIdleCache.Clear();
    animCallback = nullptr;
}
void CUnitServer::DiscardSave()
{
    curCommand.Clear();
    nextCommand.Clear();
    ClearField(pCastParticle);
    activeContSpells.RemoveAll();
    train.Clear();
    logic.DiscardSave();
    CUnitSpecific::DiscardSave();
}

void CUnitClientSpecific::Store(SaveSchema::Writer &w) const
{
    CUnitSpecific::Store(w);
    Field(w, route);
    Field(w, bWeaponHidden);
    Field(w, bArrowHidden);
    Field(w, fFadeOutAlpha);
    Field(w, nTemporalHighlightSegmentsCounter);
    Field(w, nBloodyPrintsType);
    Field(w, nBloodyPrintsCounter);
}
void CUnitClientSpecific::Load(SaveSchema::Reader &r)
{
    CUnitSpecific::Load(r);
    Field(r, route);
    Field(r, bWeaponHidden);
    Field(r, bArrowHidden);
    Field(r, fFadeOutAlpha);
    Field(r, nTemporalHighlightSegmentsCounter);
    Field(r, nBloodyPrintsType);
    Field(r, nBloodyPrintsCounter);
}
void CUnitClientSpecific::DiscardSave()
{
    CUnitSpecific::DiscardSave();
}

void CItem::Store(SaveSchema::Writer &w) const
{
    Field(w, fpHP);
    Field(w, fpMaxHP);
    Field(w, fpMP);
    Field(w, fpMaxMP);
    Field(w, fpRegenMP);
    Field(w, fpWeight);
    Field(w, fpPrice);
    Field(w, fpSize);
    Field(w, pSpell);
    Field(w, nCount);
    Field(w, dwSpellSlots);
}
void CItem::Load(SaveSchema::Reader &r)
{
    Field(r, fpHP);
    Field(r, fpMaxHP);
    Field(r, fpMP);
    Field(r, fpMaxMP);
    Field(r, fpRegenMP);
    Field(r, fpWeight);
    Field(r, fpPrice);
    Field(r, fpSize);
    Field(r, pSpell);
    Field(r, nCount);
    Field(r, dwSpellSlots);
}
void CItem::DiscardSave()
{
    ClearField(pSpell);
}

void CItemWeapon::Store(SaveSchema::Writer &w) const
{
    CItem::Store(w);
    Field(w, dwProficiencyClass);
    Field(w, fpDamageMin);
    Field(w, fpDamageRnd);
    Field(w, fpRange);
}
void CItemWeapon::Load(SaveSchema::Reader &r)
{
    CItem::Load(r);
    Field(r, dwProficiencyClass);
    Field(r, fpDamageMin);
    Field(r, fpDamageRnd);
    Field(r, fpRange);
}
void CItemWeapon::DiscardSave()
{
    CItem::DiscardSave();
}

void CItemArmor::Store(SaveSchema::Writer &w) const
{
    CItem::Store(w);
    Field(w, fpAbsorbtions);
}
void CItemArmor::Load(SaveSchema::Reader &r)
{
    CItem::Load(r);
    Field(r, fpAbsorbtions);
}
void CItemArmor::DiscardSave()
{
    CItem::DiscardSave();
}

void CItemQuick::Store(SaveSchema::Writer &w) const
{
    CItem::Store(w);
    Field(w, dwSkill);
}
void CItemQuick::Load(SaveSchema::Reader &r)
{
    CItem::Load(r);
    Field(r, dwSkill);
}
void CItemQuick::DiscardSave()
{
    CItem::DiscardSave();
}

void CItemQuest::Store(SaveSchema::Writer &w) const
{
    CItem::Store(w);
    Field(w, dwScriptID);
}
void CItemQuest::Load(SaveSchema::Reader &r)
{
    CItem::Load(r);
    Field(r, dwScriptID);
}
void CItemQuest::DiscardSave()
{
    CItem::DiscardSave();
}

void CItemLoot::Store(SaveSchema::Writer &w) const
{
    CItem::Store(w);
    Field(w, litType);
    Field(w, itInstructionType);
    Field(w, dwMaterialIndex);
}
void CItemLoot::Load(SaveSchema::Reader &r)
{
    CItem::Load(r);
    Field(r, litType);
    Field(r, itInstructionType);
    Field(r, dwMaterialIndex);
}
void CItemLoot::DiscardSave()
{
    CItem::DiscardSave();
}

void CSpell::Store(SaveSchema::Writer &w) const
{
    w.UInt32(GetSubtypeID());
    w.UInt32(GetFlags());
    Field(w, effect);
    Field(w, fpSpeed);
    Field(w, fpRange);
    Field(w, dwNumTargets);
    Field(w, dwResolve);
    Field(w, fpManaCost);
    Field(w, dwSlots);
    Field(w, fpPrice);
    Field(w, dwActions);
    for (unsigned char rune : runes) w.UInt8(rune);
    Field(w, dwComplexity);
}
void CSpell::Load(SaveSchema::Reader &r)
{
    SetSubtypeID(r.UInt32());
    ResetAllFlags(); SetFlag(r.UInt32());
    Field(r, effect);
    Field(r, fpSpeed);
    Field(r, fpRange);
    Field(r, dwNumTargets);
    Field(r, dwResolve);
    Field(r, fpManaCost);
    Field(r, dwSlots);
    Field(r, fpPrice);
    Field(r, dwActions);
    for (char &rune : runes) rune=static_cast<char>(r.UInt8());
    Field(r, dwComplexity);
}
void CSpell::DiscardSave()
{
}

void CSpellContainer::Store(SaveSchema::Writer &w) const
{
    w.Bool(pPrototype!=nullptr);
    if (pPrototype) w.String(pPrototype->GetCodeName());
    Field(w, spell);
    Field(w, szRuneIndices);
    Field(w, szName);
    Count(w,runes.size());
    for (const auto *rune : runes)
    {
        if (!rune) throw SaveSchema::Error(SaveSchema::ErrorCode::Invalid,"null spell rune");
        w.String(rune->GetCodeName());
    }
}
void CSpellContainer::Load(SaveSchema::Reader &r)
{
    pPrototype=nullptr;
    if (r.Bool())
    {
        const auto code=r.String();
        pPrototype=theSpellDatabase.spells.GetEntry(code);
        if (!pPrototype) throw SaveSchema::Error(SaveSchema::ErrorCode::Invalid,"unknown spell container prototype");
    }
    Field(r, spell);
    Field(r, szRuneIndices);
    Field(r, szName);
    runes.clear();
    const auto count=r.Count();
    for (std::uint32_t i=0;i<count;++i)
    {
        const auto code=r.String();
        const auto *rune = theSpellDatabase.runes.GetEntry(code);
        if (!rune) throw SaveSchema::Error(SaveSchema::ErrorCode::Invalid, "unknown spell rune");
        runes.push_back(rune);
    }
}
void CSpellContainer::DiscardSave()
{
}

void CEffect::Store(SaveSchema::Writer &w) const
{
    Field(w, cp);
    Field(w, vecSpeed);
    Field(w, nStartSegment);
    Field(w, bMoving);
}
void CEffect::Load(SaveSchema::Reader &r)
{
    Field(r, cp);
    Field(r, vecSpeed);
    Field(r, nStartSegment);
    Field(r, bMoving);
}
void CEffect::DiscardSave()
{
}

void CEffectSound::Store(SaveSchema::Writer &w) const
{
    CEffect::Store(w);
    Field(w, szSoundName);
    Field(w, fpMinRadius);
    Field(w, fpMaxRadius);
    Field(w, bLooping);
    Field(w, bHighPriority);
}
void CEffectSound::Load(SaveSchema::Reader &r)
{
    CEffect::Load(r);
    Field(r, szSoundName);
    Field(r, fpMinRadius);
    Field(r, fpMaxRadius);
    Field(r, bLooping);
    Field(r, bHighPriority);
}
void CEffectSound::DiscardSave()
{
    CEffect::DiscardSave();
}

void CEffectSoundSeq::Store(SaveSchema::Writer &w) const
{
    CEffect::Store(w);
    Field(w, szStartSound);
    Field(w, szSounds);
    Field(w, fpMinRadius);
    Field(w, fpMaxRadius);
    Field(w, bHighPriority);
}
void CEffectSoundSeq::Load(SaveSchema::Reader &r)
{
    CEffect::Load(r);
    Field(r, szStartSound);
    Field(r, szSounds);
    Field(r, fpMinRadius);
    Field(r, fpMaxRadius);
    Field(r, bHighPriority);
}
void CEffectSoundSeq::DiscardSave()
{
    CEffect::DiscardSave();
}

void CEffectAmbientSound::Store(SaveSchema::Writer &w) const
{
    CEffect::Store(w);
    Field(w, szSoundNames);
    Field(w, fpMinRadius);
    Field(w, fpMaxRadius);
    Field(w, nMsToPlayMin);
    Field(w, nMsToPlayMax);
}
void CEffectAmbientSound::Load(SaveSchema::Reader &r)
{
    CEffect::Load(r);
    Field(r, szSoundNames);
    Field(r, fpMinRadius);
    Field(r, fpMaxRadius);
    Field(r, nMsToPlayMin);
    Field(r, nMsToPlayMax);
}
void CEffectAmbientSound::DiscardSave()
{
    CEffect::DiscardSave();
}

void CEffectPointLight::Store(SaveSchema::Writer &w) const
{
    CEffect::Store(w);
    Field(w, color);
    Field(w, fpRange);
    Field(w, dwFlags);
}
void CEffectPointLight::Load(SaveSchema::Reader &r)
{
    CEffect::Load(r);
    Field(r, color);
    Field(r, fpRange);
    Field(r, dwFlags);
}
void CEffectPointLight::DiscardSave()
{
    CEffect::DiscardSave();
}

void CEffectParticle::Store(SaveSchema::Writer &w) const
{
    CEffect::Store(w);
    Field(w, typeID);
    Field(w, fpScale);
    w.MapReference(nAttachedID);
    Field(w, ptDest);
    w.MapReference(nStartID);
    Field(w, fpPower);
    Field(w, fpPower2);
}
void CEffectParticle::Load(SaveSchema::Reader &r)
{
    CEffect::Load(r);
    Field(r, typeID);
    Field(r, fpScale);
    nAttachedID = r.MapReference();
    Field(r, ptDest);
    nStartID = r.MapReference();
    Field(r, fpPower);
    Field(r, fpPower2);
}
void CEffectParticle::DiscardSave()
{
    CEffect::DiscardSave();
}

void CEffectWall::Store(SaveSchema::Writer &w) const
{
    CEffectParticle::Store(w);
    Field(w, ptWallDirection);
}
void CEffectWall::Load(SaveSchema::Reader &r)
{
    CEffectParticle::Load(r);
    Field(r, ptWallDirection);
}
void CEffectWall::DiscardSave()
{
    CEffectParticle::DiscardSave();
}

void CEffectLightning::Store(SaveSchema::Writer &w) const
{
    CEffect::Store(w);
    Field(w, ptSrc);
    Field(w, ptDst);
    Field(w, fpWidth);
    w.MapReference(nAttachedID);
    w.MapReference(nCasterID);
    Field(w, bColor);
}
void CEffectLightning::Load(SaveSchema::Reader &r)
{
    CEffect::Load(r);
    Field(r, ptSrc);
    Field(r, ptDst);
    Field(r, fpWidth);
    nAttachedID = r.MapReference();
    nCasterID = r.MapReference();
    Field(r, bColor);
}
void CEffectLightning::DiscardSave()
{
    CEffect::DiscardSave();
}

void CEffectArrowBase::Store(SaveSchema::Writer &w) const
{
    CEffect::Store(w);
    Field(w, bDelivered);
    w.MapReference(nTargetID);
    Field(w, bNewArrow);
}
void CEffectArrowBase::Load(SaveSchema::Reader &r)
{
    CEffect::Load(r);
    Field(r, bDelivered);
    nTargetID = r.MapReference();
    Field(r, bNewArrow);
}
void CEffectArrowBase::DiscardSave()
{
    CEffect::DiscardSave();
}

void CEffectArrow::Store(SaveSchema::Writer &w) const
{
    CEffectArrowBase::Store(w);
    Field(w, szSoundName);
    Field(w, color);
    Field(w, fpRange);
    Field(w, typeID);
    Field(w, fpScale);
    w.MapReference(nAttackerID);
    Field(w, weaponAttack);
    Field(w, weaponAttackPending);
}
void CEffectArrow::Load(SaveSchema::Reader &r)
{
    CEffectArrowBase::Load(r);
    Field(r, szSoundName);
    Field(r, color);
    Field(r, fpRange);
    Field(r, typeID);
    Field(r, fpScale);
    nAttackerID = r.MapReference();
    Field(r, weaponAttack);
    Field(r, weaponAttackPending);
}
void CEffectArrow::DiscardSave()
{
    CEffectArrowBase::DiscardSave();
}

void CEffectArrowFigure::Store(SaveSchema::Writer &w) const
{
    CEffectArrowBase::Store(w);
    w.MapReference(nAttackerID);
    Field(w, pMapObj);
    Field(w, attackPortion);
    Field(w, attackProcessed);
    Field(w, explosivePending);
    if (explosivePending) { Field(w, explosivePoint); Field(w, explosiveRadius); Field(w, explosiveStart); }
}
void CEffectArrowFigure::Load(SaveSchema::Reader &r)
{
    CEffectArrowBase::Load(r);
    nAttackerID = r.MapReference();
    Field(r, pMapObj);
    Field(r, attackPortion);
    Field(r, attackProcessed);
    Field(r, explosivePending);
    if (explosivePending) { Field(r, explosivePoint); Field(r, explosiveRadius); Field(r, explosiveStart); }
}
void CEffectArrowFigure::DiscardSave()
{
    pMapObj.Clear();
    CEffectArrowBase::DiscardSave();
}

void CEffectShowVisibility::Store(SaveSchema::Writer &w) const
{
    CEffect::Store(w);
    w.MapReference(nTargetID);
    w.MapReference(nWatcherID);
}
void CEffectShowVisibility::Load(SaveSchema::Reader &r)
{
    CEffect::Load(r);
    nTargetID = r.MapReference();
    nWatcherID = r.MapReference();
}
void CEffectShowVisibility::DiscardSave()
{
    CEffect::DiscardSave();
}

void CEffectMoshka::Store(SaveSchema::Writer &w) const
{
    CEffect::Store(w);
    Field(w, pMapObj);
    w.MapReference(nUnitID);
    Field(w, fpPrevTime);
    Field(w, fpStateTime);
    Field(w, fpBaseAngle);
    Field(w, fpRoundAwayH);
    Field(w, state);
}
void CEffectMoshka::Load(SaveSchema::Reader &r)
{
    CEffect::Load(r);
    Field(r, pMapObj);
    nUnitID = r.MapReference();
    Field(r, fpPrevTime);
    Field(r, fpStateTime);
    Field(r, fpBaseAngle);
    Field(r, fpRoundAwayH);
    Field(r, state);
}
void CEffectMoshka::DiscardSave()
{
    pMapObj.Clear();
    CEffect::DiscardSave();
}

void CEffectTornado::Store(SaveSchema::Writer &w) const
{
    CEffect::Store(w);
    Field(w, nTimeLeft);
    Field(w, nStartSegment);
}
void CEffectTornado::Load(SaveSchema::Reader &r)
{
    CEffect::Load(r);
    Field(r, nTimeLeft);
    Field(r, nStartSegment);
}
void CEffectTornado::DiscardSave()
{
    CEffect::DiscardSave();
}

void CTrapObject::Store(SaveSchema::Writer &w) const
{
    CMapObject::Store(w);
    Field(w, dwEnemyMask);
}
void CTrapObject::Load(SaveSchema::Reader &r)
{
    CMapObject::Load(r);
    Field(r, dwEnemyMask);
}
void CTrapObject::DiscardSave()
{
    CMapObject::DiscardSave();
}

void CMagicTrapObject::Store(SaveSchema::Writer &w) const
{
    CMapObject::Store(w);
    Field(w, pSpell);
    Field(w, trapAreas);
    Field(w, trappedPersons);
    Field(w, ptTargets);
    Field(w, nCastingInterval);
    Field(w, nTimeToCast);
    Field(w, bCastOnce);
    Field(w, dwEnemyMask);
}
void CMagicTrapObject::Load(SaveSchema::Reader &r)
{
    CMapObject::Load(r);
    Field(r, pSpell);
    Field(r, trapAreas);
    Field(r, trappedPersons);
    Field(r, ptTargets);
    Field(r, nCastingInterval);
    Field(r, nTimeToCast);
    Field(r, bCastOnce);
    Field(r, dwEnemyMask);
}
void CMagicTrapObject::DiscardSave()
{
    ClearField(pSpell);
    trappedPersons.RemoveAll();
    CMapObject::DiscardSave();
}

void CLeverObject::Store(SaveSchema::Writer &w) const
{
    CMapObject::Store(w);
    Field(w, nCurrentState);
    Field(w, nTotalStates);
    Field(w, bCycle);
    Field(w, science);
    Field(w, bIsDoor);
    Field(w, bGraphRecalc);
}
void CLeverObject::Load(SaveSchema::Reader &r)
{
    CMapObject::Load(r);
    Field(r, nCurrentState);
    Field(r, nTotalStates);
    Field(r, bCycle);
    Field(r, science);
    Field(r, bIsDoor);
    Field(r, bGraphRecalc);
}
void CLeverObject::DiscardSave()
{
    CMapObject::DiscardSave();
}

void CTorchObject::Store(SaveSchema::Writer &w) const
{
    CMapObject::Store(w);
    Field(w, pTorchSpecific);
}
void CTorchObject::Load(SaveSchema::Reader &r)
{
    CMapObject::Load(r);
    Field(r, pTorchSpecific);
}
void CTorchObject::DiscardSave()
{
    ClearField(pTorchSpecific);
    CMapObject::DiscardSave();
}

void CTorchObjectSpecific::Store(SaveSchema::Writer &w) const
{
    w.GameReference(lpTorch);
    Field(w, tidParticlesType);
    Field(w, szSFXName);
    Field(w, fpParticleScale);
    Field(w, fpLightRange);
    Field(w, dcvLightColor);
    Field(w, ptParticlesCenter);
    Field(w, ptLightCenter);
}
void CTorchObjectSpecific::Load(SaveSchema::Reader &r)
{
    lpTorch = r.GameReferenceAs<std::remove_pointer_t<decltype(lpTorch)>>();
    Field(r, tidParticlesType);
    Field(r, szSFXName);
    Field(r, fpParticleScale);
    Field(r, fpLightRange);
    Field(r, dcvLightColor);
    Field(r, ptParticlesCenter);
    Field(r, ptLightCenter);
}
void CTorchObjectSpecific::DiscardSave()
{
    lpTorch = nullptr;
}

void CAISpellCaster::SAIPotentialAttack::Store(SaveSchema::Writer &w) const
{
    Field(w, type);
    Field(w, nAttackNumber);
    Field(w, fpRange);
    Field(w, nTargets);
    Field(w, fpDamageRadius);
    Field(w, nDuration);
    Field(w, attack);
}
void CAISpellCaster::SAIPotentialAttack::Load(SaveSchema::Reader &r)
{
    Field(r, type);
    Field(r, nAttackNumber);
    Field(r, fpRange);
    Field(r, nTargets);
    Field(r, fpDamageRadius);
    Field(r, nDuration);
    Field(r, attack);
}
void CAISpellCaster::SAIPotentialAttack::DiscardSave()
{
}

void CAISpellCaster::Store(SaveSchema::Writer &w) const
{
    Field(w, attacks);
    Field(w, heals);
    Field(w, fpAvrgDamage);
}
void CAISpellCaster::Load(SaveSchema::Reader &r)
{
    Field(r, attacks);
    Field(r, heals);
    Field(r, fpAvrgDamage);
    skillCacheValid = false;
    companionObserved = companionLogged = false;
    companionRefusals = 0;
}
void CAISpellCaster::DiscardSave()
{
}

void CAIUnitMotivation::Store(SaveSchema::Writer &w) const
{
}
void CAIUnitMotivation::Load(SaveSchema::Reader &r)
{
}
void CAIUnitMotivation::DiscardSave()
{
}

void CUMGuard::Store(SaveSchema::Writer &w) const
{
    Field(w, fpGuardRadius);
    Field(w, fpStayIdleTime);
    Field(w, ptGuard);
    Field(w, nGuardFail);
}
void CUMGuard::Load(SaveSchema::Reader &r)
{
    Field(r, fpGuardRadius);
    Field(r, fpStayIdleTime);
    Field(r, ptGuard);
    Field(r, nGuardFail);
}
void CUMGuard::DiscardSave()
{
}

void CUMPatrol::Store(SaveSchema::Writer &w) const
{
    Field(w, patrol);
    Field(w, nCurrentPatrolPoint);
}
void CUMPatrol::Load(SaveSchema::Reader &r)
{
    Field(r, patrol);
    Field(r, nCurrentPatrolPoint);
}
void CUMPatrol::DiscardSave()
{
}

void CUMSentry::Store(SaveSchema::Writer &w) const
{
    Field(w, ptSentry);
}
void CUMSentry::Load(SaveSchema::Reader &r)
{
    Field(r, ptSentry);
}
void CUMSentry::DiscardSave()
{
}

void CUMFollow::Store(SaveSchema::Writer &w) const
{
    Field(w, pFollowTo);
}
void CUMFollow::Load(SaveSchema::Reader &r)
{
    Field(r, pFollowTo);
}
void CUMFollow::DiscardSave()
{
    pFollowTo.Clear();
}

void CUMSuspection::Store(SaveSchema::Writer &w) const
{
    Field(w, ptUnderSuspection);
    Field(w, nPower);
}
void CUMSuspection::Load(SaveSchema::Reader &r)
{
    Field(r, ptUnderSuspection);
    Field(r, nPower);
}
void CUMSuspection::DiscardSave()
{
}

void CUMAggression::Store(SaveSchema::Writer &w) const
{
    Field(w, pTarget);
    Field(w, bRevenge);
}
void CUMAggression::Load(SaveSchema::Reader &r)
{
    Field(r, pTarget);
    Field(r, bRevenge);
}
void CUMAggression::DiscardSave()
{
    pTarget.Clear();
    tempEnemy.Clear();
}

void CUMFear::Store(SaveSchema::Writer &w) const
{
    Field(w, fpFearRadius);
    Field(w, fpRandomShift);
    Field(w, ptDangerousPlace);
    Field(w, bFearPlayer);
    Field(w, ptPreparedRunaway);
}
void CUMFear::Load(SaveSchema::Reader &r)
{
    Field(r, fpFearRadius);
    Field(r, fpRandomShift);
    Field(r, ptDangerousPlace);
    Field(r, bFearPlayer);
    Field(r, ptPreparedRunaway);
}
void CUMFear::DiscardSave()
{
}

void CUMPlayer::Store(SaveSchema::Writer &w) const
{
    Field(w, pTarget);
    Field(w, pFollowTo);
    Field(w, ptSwarmTarget);
    Field(w, cmdState);
    Field(w, bRun);
}
void CUMPlayer::Load(SaveSchema::Reader &r)
{
    Field(r, pTarget);
    Field(r, pFollowTo);
    Field(r, ptSwarmTarget);
    Field(r, cmdState);
    Field(r, bRun);
    resumeCompanionAttack = false;
}
void CUMPlayer::DiscardSave()
{
    pTarget.Clear();
    pFollowTo.Clear();
}

void CAIIgnoredTargetsList::Store(SaveSchema::Writer &w) const
{
    Count(w, ignoredList.GetSize());
    for (int i = 0; i < ignoredList.GetSize(); ++i)
    { Field(w, ignoredList[i].pTarget); Field(w, ignoredList[i].nTime); }
}
void CAIIgnoredTargetsList::Load(SaveSchema::Reader &r)
{
    Clear();
    ignoredList.SetSize(r.Count());
    for (int i = 0; i < ignoredList.GetSize(); ++i)
    { Field(r, ignoredList[i].pTarget); Field(r, ignoredList[i].nTime); }
}
void CAIIgnoredTargetsList::DiscardSave()
{
    Clear();
}

void CAIDelayedSoundsList::SSound::Store(SaveSchema::Writer &w) const
{
    Field(w, ptSrc);
    Field(w, nDelay);
    Field(w, szDescription);
    Field(w, dwNewEnemies);
}
void CAIDelayedSoundsList::SSound::Load(SaveSchema::Reader &r)
{
    Field(r, ptSrc);
    Field(r, nDelay);
    Field(r, szDescription);
    Field(r, dwNewEnemies);
}
void CAIDelayedSoundsList::SSound::DiscardSave()
{
}

void CAIDelayedSoundsList::Store(SaveSchema::Writer &w) const
{
    Field(w, soundsQueue);
}
void CAIDelayedSoundsList::Load(SaveSchema::Reader &r)
{
    Field(r, soundsQueue);
}
void CAIDelayedSoundsList::DiscardSave()
{
}

void CAIUnitEnvironment::Store(SaveSchema::Writer &w) const
{
    Field(w, units);
    Field(w, visibleUnits);
    Field(w, seenCorpses);
    Field(w, freshCorpses);
    Field(w, ptPointOfInterest);
    Field(w, nPoIPower);
    Field(w, nDelayListsUpdate);
    Field(w, ignoredList);
    Field(w, delayedSounds);
    Field(w, nPoIPowerDecrease);
}
void CAIUnitEnvironment::Load(SaveSchema::Reader &r)
{
    Field(r, units);
    Field(r, visibleUnits);
    Field(r, seenCorpses);
    Field(r, freshCorpses);
    Field(r, ptPointOfInterest);
    Field(r, nPoIPower);
    Field(r, nDelayListsUpdate);
    Field(r, ignoredList);
    Field(r, delayedSounds);
    Field(r, nPoIPowerDecrease);
}
void CAIUnitEnvironment::DiscardSave()
{
    Clear();
}

void CPersonage::Store(SaveSchema::Writer &w) const
{
    Field(w, szName);
    Field(w, szTemplate);
    Field(w, szPrimTexture);
    Field(w, szSecondTexture);
    Field(w, szPrototype);
    Field(w, szVoicePrototype);
    Field(w, nID);
    Field(w, stats);
    Field(w, complection);
    Field(w, weapons);
    Field(w, items);
    Field(w, armors);
    Field(w, spells);
    Field(w, szInternalName);
    Field(w, pose);
    Field(w, dwCurrentWeapon);
    Field(w, bIsSelected);
    Field(w, nFollowToID);
    Field(w, aggression);
}
void CPersonage::Load(SaveSchema::Reader &r)
{
    Field(r, szName);
    Field(r, szTemplate);
    Field(r, szPrimTexture);
    Field(r, szSecondTexture);
    Field(r, szPrototype);
    Field(r, szVoicePrototype);
    Field(r, nID);
    Field(r, stats);
    Field(r, complection);
    Field(r, weapons);
    Field(r, items);
    Field(r, armors);
    Field(r, spells);
    Field(r, szInternalName);
    Field(r, pose);
    Field(r, dwCurrentWeapon);
    Field(r, bIsSelected);
    Field(r, nFollowToID);
    Field(r, aggression);
}
void CPersonage::DiscardSave()
{
    ClearItems();
}

void CParty::Store(SaveSchema::Writer &w) const
{
    if (!pItemManager || !pSpellManager) throw SaveSchema::Error(SaveSchema::ErrorCode::Identity,"party managers not created");
    w.UInt64(pItemManager->GetRTID()); w.UInt64(pSpellManager->GetRTID());
    Field(w, szCurrentZone);
    Field(w, nEntrance);
    Field(w, allWagonItems);
    Field(w, people);
    Field(w, vars);
    Field(w, nMoney);
}
void CParty::Load(SaveSchema::Reader &r)
{
    const auto itemOwner=r.UInt64(),spellOwner=r.UInt64();
    if (!pItemManager || !pSpellManager || itemOwner!=pItemManager->GetRTID() || spellOwner!=pSpellManager->GetRTID())
        throw SaveSchema::Error(SaveSchema::ErrorCode::Identity,"party requires its saved staged managers before graph load");
    Field(r, szCurrentZone);
    Field(r, nEntrance);
    Field(r, allWagonItems);
    Field(r, people);
    Field(r, vars);
    Field(r, nMoney);
}
void CParty::DiscardSave()
{
    for (int i = 0; i < people.GetSize(); ++i) people[i].DiscardSave();
    ClearField(allWagonItems);
}

void CCamera::Store(SaveSchema::Writer &w) const
{
    Field(w, ptAnchorPoint);
    Field(w, fAnchorHeight);
    Field(w, qRotate);
    Field(w, fPitch);
    Field(w, fDistance);
    Field(w, scroll);
    Field(w, rotate);
    Field(w, pitch);
    Field(w, zoom);
    w.MapReference(dwCarrierID);
    Field(w, shortcuts);
    w.UInt32(GetFlags());
    w.Bool(campPlacementValid);
    if(campPlacementValid){
        Field(w,campPlacement.position);Field(w,campPlacement.anchor);
        Field(w,campPlacement.orientation);Field(w,campPlacement.orbit);Field(w,campPlacement.baseline);
        Field(w,campPlacement.anchorHeight);Field(w,campPlacement.distance);Field(w,campPlacement.pitch);
        Field(w,campPlacement.baselinePitch);Field(w,campPlacement.telephoto);
        // Carrier is a party routing ID: camp deliberately removes/recreates the
        // hero's map object, so there is no current graph object to reference.
        w.UInt32(campPlacement.carrier);Field(w,campPlacement.mode);w.Bool(campPlacement.baselineValid);w.Bool(campEntered);
    }
}
void CCamera::Load(SaveSchema::Reader &r)
{
    Field(r, ptAnchorPoint);
    Field(r, fAnchorHeight);
    Field(r, qRotate);
    Field(r, fPitch);
    Field(r, fDistance);
    Field(r, scroll);
    Field(r, rotate);
    Field(r, pitch);
    Field(r, zoom);
    dwCarrierID = r.MapReference();
    Field(r, shortcuts);
    ResetAllFlags(); SetFlag(r.UInt32());
    campPlacementValid=r.Bool();campTerrain=nullptr;campRequestTime=0;campEntered=false;
    if(campPlacementValid){
        Field(r,campPlacement.position);Field(r,campPlacement.anchor);
        Field(r,campPlacement.orientation);Field(r,campPlacement.orbit);Field(r,campPlacement.baseline);
        Field(r,campPlacement.anchorHeight);Field(r,campPlacement.distance);Field(r,campPlacement.pitch);
        Field(r,campPlacement.baselinePitch);Field(r,campPlacement.telephoto);
        campPlacement.carrier=r.UInt32();Field(r,campPlacement.mode);campPlacement.baselineValid=r.Bool();campEntered=r.Bool();
    }
}
void CCamera::DiscardSave()
{
}

void CAIUnitLogic::Store(SaveSchema::Writer &w) const
{
    Field(w, state);
    Field(w, currentRally);
    Field(w, nLookFinishTime);
    Field(w, pTarget);
    Field(w, pHitBy);
    Field(w, fpCallForHelpRadius);
    Field(w, fpSuspectRadius);
    Field(w, fpAttackRadius);
    Field(w, fpRadiusKoef);
    Field(w, ptPreviousPoint);
    Field(w, ptCurrentPoint);
    Field(w, alarmCondition);
    Field(w, motivations);
    Field(w, pControl);
    Field(w, environment);
    Field(w, nAlarmInvoked);
    Field(w, nAlarmNumber);
    Field(w, alarmLogics);
    Field(w, nAlarmDelay);
    Field(w, bAlwaysOnTheRun);
    Field(w, caster);
    Field(w, nCurrentAlarm);
}
void CAIUnitLogic::Load(SaveSchema::Reader &r)
{
    Field(r, state);
    Field(r, currentRally);
    Field(r, nLookFinishTime);
    Field(r, pTarget);
    Field(r, pHitBy);
    Field(r, fpCallForHelpRadius);
    Field(r, fpSuspectRadius);
    Field(r, fpAttackRadius);
    Field(r, fpRadiusKoef);
    Field(r, ptPreviousPoint);
    Field(r, ptCurrentPoint);
    Field(r, alarmCondition);
    Field(r, motivations);
    Field(r, pControl);
    Field(r, environment);
    Field(r, nAlarmInvoked);
    Field(r, nAlarmNumber);
    Field(r, alarmLogics);
    Field(r, nAlarmDelay);
    Field(r, bAlwaysOnTheRun);
    Field(r, caster);
    Field(r, nCurrentAlarm);
    nGuardPntFail = 0; nFollowRest = 0;
    bEnemyVisible = false;
    bool controlFound = pControl.GetPtr() == nullptr;
    for (POSITION p = motivations.GetHeadPosition(); p;)
        if (motivations.GetNext(p).GetPtr() == pControl.GetPtr()) controlFound = true;
    if (!controlFound) throw SaveSchema::Error(SaveSchema::ErrorCode::Invalid, "AI control is outside motivations");
}
void CAIUnitLogic::DiscardSave()
{
    for (POSITION p = motivations.GetHeadPosition(); p;)
        motivations.GetNext(p)->DiscardSave();
    pControl = nullptr;
    motivations.RemoveAll();
    pTarget.Clear(); pHitBy.Clear(); environment.DiscardSave();
}

void CEffectSpell::Store(SaveSchema::Writer &w) const
{
    Field(w, cp);
    Field(w, pWho);
    Field(w, pTarget);
    Field(w, ptTarget);
    Field(w, castPhase);
    Field(w, spell);
    Field(w, pEffect);
    Field(w, pEffectSound);
    Field(w, pEffectLight);
    Field(w, nTimeLeft);
    Field(w, dwFlags);
    Field(w, bQuiet);
    w.UInt32(spell.GetSubtypeID());
    w.Bool(savedStatePending ? savedDangerMapping : bDangerIsMapped);
}
void CEffectSpell::Load(SaveSchema::Reader &r)
{
    Field(r, cp);
    Field(r, pWho);
    Field(r, pTarget);
    Field(r, ptTarget);
    Field(r, castPhase);
    Field(r, spell);
    Field(r, pEffect);
    Field(r, pEffectSound);
    Field(r, pEffectLight);
    Field(r, nTimeLeft);
    Field(r, dwFlags);
    Field(r, bQuiet);
    spell.SetSubtypeID(r.UInt32());
    savedDangerMapping = r.Bool();
    savedStatePending = true;
    bDangerIsMapped = false;
    pWS = nullptr;
}
void CEffectSpell::DiscardSave()
{
    savedStatePending = true;
    bDangerIsMapped = false;
    pWho.Clear(); pTarget.Clear();
    pEffect = nullptr; pEffectSound = nullptr; pEffectLight = nullptr;
    pWS = nullptr;
}

void CPlayerSpecific::Store(SaveSchema::Writer &w) const
{
    w.GameReference(pPlayer);
}
void CPlayerSpecific::Load(SaveSchema::Reader &r)
{
    pPlayer = r.GameReferenceAs<std::remove_pointer_t<decltype(pPlayer)>>();
}
void CPlayerSpecific::DiscardSave()
{
    pPlayer = nullptr;
}

void CPlayer::Store(SaveSchema::Writer &w) const
{
    Field(w, pPlayerSpecific);
    Field(w, nPlayerNum);
    Field(w, party);
    Field(w, items);
    Field(w, partyID);
    Field(w, curInterface);
    Field(w, prevInterface);
    Field(w, bGod);
    Field(w, nTimeWasAddedSegment);
    Field(w, activeItems);
    Field(w, dwChangedMask);
    Field(w, unitsUnderControl);
}
void CPlayer::Load(SaveSchema::Reader &r)
{
    Field(r, pPlayerSpecific);
    Field(r, nPlayerNum);
    Field(r, party);
    Field(r, items);
    Field(r, partyID);
    Field(r, curInterface);
    Field(r, prevInterface);
    Field(r, bGod);
    Field(r, nTimeWasAddedSegment);
    Field(r, activeItems);
    Field(r, dwChangedMask);
    Field(r, unitsUnderControl);
}
void CPlayer::DiscardSave()
{
    retiringSave=true;
    ClearField(unitsUnderControl);
    ClearField(pPlayerSpecific);
    ClearField(items);
    party.DiscardSave();
}

void CPlayerServer::Store(SaveSchema::Writer &w) const
{
    CPlayerSpecific::Store(w);
    Field(w, status);
    w.MapReference(nLookFromID);
    Field(w, cameraCP);
    Field(w, nCameraUpdateCounter);
    Field(w, bPartyDeployed);
    Field(w, partyPlace);
    Field(w, dangerState);
    Field(w, nStorePartyCounter);
}
void CPlayerServer::Load(SaveSchema::Reader &r)
{
    CPlayerSpecific::Load(r);
    Field(r, status);
    nLookFromID = r.MapReference();
    Field(r, cameraCP);
    Field(r, nCameraUpdateCounter);
    Field(r, bPartyDeployed);
    Field(r, partyPlace);
    Field(r, dangerState);
    Field(r, nStorePartyCounter);
}
void CPlayerServer::DiscardSave()
{
    RetireSavedWorld();
    addToVisible.Clear(); currentVisible.Clear();
    CPlayerSpecific::DiscardSave();
}

void CPlayerClient::Store(SaveSchema::Writer &w) const
{
    CPlayerSpecific::Store(w);
    Field(w, status);
    Field(w, bIgnoreInterfaceChange);
    Field(w, bSendParty);
}
void CPlayerClient::Load(SaveSchema::Reader &r)
{
    CPlayerSpecific::Load(r);
    Field(r, status);
    Field(r, bIgnoreInterfaceChange);
    Field(r, bSendParty);
}
void CPlayerClient::DiscardSave()
{
    CPlayerSpecific::DiscardSave();
}

void CAIMarkManager::Store(SaveSchema::Writer &w) const
{
    using namespace SaveSchema;
    std::vector<const SAIMark *> table;
    std::map<const SAIMark *,std::uint32_t> ids;
    for (int y=0;y<cells.GetYSize();++y)
        for (int x=0;x<cells.GetXSize();++x)
            for (int player=0;player<N_AI_MARKS_PLAYER_NUM;++player)
                for (const SAIMark *p=cells[y][x].pMark[player];p;p=p->pNext)
                {
                    if (!ids.emplace(p,static_cast<std::uint32_t>(table.size()+1)).second)
                        throw Error(ErrorCode::Invalid,"cycle/shared AI cell mark");
                    table.push_back(p);
                }
    auto link=[&](const SAIMark *p) {
        if (!p) return std::uint32_t{0};
        const auto it=ids.find(p);
        if (it==ids.end()) throw Error(ErrorCode::Invalid,"AI link outside mark owner");
        return it->second;
    };
    w.Int32(nSegment); Count(w,cells.GetXSize()); Count(w,cells.GetYSize()); Count(w,table.size());
    for (const auto *p : table)
    {
        w.Value(*p);
        w.UInt32(link(p->pNext)); w.UInt32(link(p->pPrev)); w.UInt32(link(p->pNextDel));
    }
    for (int y=0;y<cells.GetYSize();++y)
        for (int x=0;x<cells.GetXSize();++x)
            for (int player=0;player<N_AI_MARKS_PLAYER_NUM;++player)
                w.UInt32(link(cells[y][x].pMark[player]));
    // Preserve expiration-list order as well as cell-list order.
    for (int tick=0;tick<N_MAX_MARK_LIFETIME;++tick)
        for (int player=0;player<N_AI_MARKS_PLAYER_NUM;++player)
            w.UInt32(link(pDeleteLists[tick][player]));
}
void CAIMarkManager::Load(SaveSchema::Reader &r)
{
    using namespace SaveSchema;
    for (const auto &row : pDeleteLists) for (const auto *p : row)
        if (p) throw Error(ErrorCode::Invalid,"AI marks require a fresh staged owner");
    const auto segment=r.Int32(); const auto sx=r.Count(),sy=r.Count(),n=r.Count();
    if (!sx || !sy || std::uint64_t(sx)*sy>1000000/N_AI_MARKS_PLAYER_NUM)
        throw Error(ErrorCode::Limit,"AI mark grid dimensions");
    struct Links { std::uint32_t next,prev,del; };
    std::vector<std::unique_ptr<SAIMark>> marks(n);
    std::vector<Links> links(n);
    auto index=[&]() { auto i=r.UInt32(); if (i>n) throw Error(ErrorCode::Invalid,"AI mark index"); return i; };
    for (std::uint32_t i=0;i<n;++i)
    {
        marks[i]=std::make_unique<SAIMark>();
        r.Value(*marks[i]);
        links[i]={index(),index(),index()};
    }
    std::vector<std::uint32_t> heads(std::size_t(sx)*sy*N_AI_MARKS_PLAYER_NUM);
    std::vector<std::uint32_t> expiration(N_MAX_MARK_LIFETIME*N_AI_MARKS_PLAYER_NUM);
    for (auto &i : heads) i=index();
    for (auto &i : expiration) i=index();
    std::vector<bool> inCell(n),inExpiration(n);
    for (std::size_t cell=0;cell<heads.size();++cell)
    {
        std::uint32_t previous=0;
        for (auto i=heads[cell];i;i=links[i-1].next)
        {
            const auto &mark=*marks[i-1];
            if (inCell[i-1] || links[i-1].prev!=previous ||
                (mark.nPlayer & (N_AI_MARKS_PLAYER_NUM-1))!=cell%N_AI_MARKS_PLAYER_NUM ||
                !std::isfinite(mark.ptPosition.x) || !std::isfinite(mark.ptPosition.y) ||
                mark.ptPosition.x<0 || mark.ptPosition.y<0 ||
                mark.ptPosition.x>=float(sx)*N_AI_MARK_CELL_SIZE ||
                mark.ptPosition.y>=float(sy)*N_AI_MARK_CELL_SIZE ||
                int(mark.ptPosition.x/N_AI_MARK_CELL_SIZE)!=(cell/N_AI_MARKS_PLAYER_NUM)%sx ||
                int(mark.ptPosition.y/N_AI_MARK_CELL_SIZE)!=(cell/N_AI_MARKS_PLAYER_NUM)/sx)
                throw Error(ErrorCode::Invalid,"AI cell mark order/owner");
            inCell[i-1]=true; previous=i;
        }
    }
    for (std::size_t bucket=0;bucket<expiration.size();++bucket)
        for (auto i=expiration[bucket];i;i=links[i-1].del)
        {
            const auto &mark=*marks[i-1];
            const auto expected=((std::uint32_t(mark.nTime)-1u)&(N_MAX_MARK_LIFETIME-1))*N_AI_MARKS_PLAYER_NUM+
                (mark.nPlayer&(N_AI_MARKS_PLAYER_NUM-1));
            if (inExpiration[i-1] || bucket!=expected)
                throw Error(ErrorCode::Invalid,"AI expiration mark order/owner");
            inExpiration[i-1]=true;
        }
    for (std::uint32_t i=0;i<n;++i)
        if (!inCell[i] || !inExpiration[i]) throw Error(ErrorCode::Invalid,"unowned AI mark");
    auto pointer=[&](std::uint32_t i) { return i ? marks[i-1].get() : nullptr; };
    cells.SetSizes(sx,sy); cells.FillZero();
    for (std::uint32_t i=0;i<n;++i)
    { marks[i]->pNext=pointer(links[i].next); marks[i]->pPrev=pointer(links[i].prev); marks[i]->pNextDel=pointer(links[i].del); }
    std::size_t cell=0;
    for (std::uint32_t y=0;y<sy;++y)
        for (std::uint32_t x=0;x<sx;++x)
            for (int player=0;player<N_AI_MARKS_PLAYER_NUM;++player)
                cells[y][x].pMark[player]=pointer(heads[cell++]);
    std::size_t bucket=0;
    for (auto &row : pDeleteLists) for (auto &p : row) p=pointer(expiration[bucket++]);
    for (auto &p : marks) p.release();
    nSegment=segment; bVisionInitiated=false;
}
