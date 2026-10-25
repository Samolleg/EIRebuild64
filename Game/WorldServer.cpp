#include "StdAfx.h"
#include "SaveSlot.h"
#include "CorePerf.h"
#include "OwnedRender/E1Notices.h"
#include "OwnedRender/E2EffectDistance.h"
#include "ErrorDialog.h"
#include "GameJournal.h"
#include "EngineDiagnostics.h"
#include "ModRuntime.h"
#include "ModOutOfCombatRegeneration.h"
#include "ModPackage.h"
#include "ModItemUiPolicy.h"
#include "ModCliTool.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "io.h"
#include "WorldServer.h"
#include "Camera.h"

#include "ServerSpecificManager.h"

#include "Player.h"
#include "PlayerManager.h"
#include "OwnedRender/E1MobSource.h"
#include "PlayerServer.h"

#include "ServerConsts.h"

#include "WinRegistry.h"

#include "ObjectDBReader.h"
#include "MapObjectManager.h"
#include "MapObjectServer.h"
#include "MapEditorChunkIDs.h"
#include "Unit.h"
#include "net.h"

#include "Effect.h"
#include "EffectManager.h"
#include "ScenarioTracker.h"
#include "2DScreenWait.h"
#include "StuffMapObjects.h"
#include "party.h"
#include "WorldInterface.h"
#include "Hoover.h"
#include "RandomGen.h"
#include "Weather.h"
#include "UnitServer.h"
#include "ItemManager.h"
#include "QuestDatabase.h"
#include "StringProcessing.h"
#include "UnitStatisticsDriver.h"
#include "EffectSpell.h"
#include "Console.h"
#include "SpellManager.h"
#include "SimpleCryptographer.h"
#include "GameMain.h"
#include "ModAnimationIdSave.h"
#include <vector>
#include <set>
#include <cmath>
#include "ModCampReferences.inl"
/////////////////////////////////////////////////////////////////////////////////////
const int N_TIMERECALCINTERVAL = 63; // should be power of 2 - 1
/////////////////////////////////////////////////////////////////////////////////////
/*==========================================================================================================

        Server world class implementation

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

namespace {
CWorldServer initialWorldServer;
CWorldServer *publishedWorldServer = &initialWorldServer;
thread_local CWorldServer *preparingWorldServer = nullptr;
}
SavedWorldScope::SavedWorldScope(CWorldServer *world) noexcept : previous(preparingWorldServer) { preparingWorldServer=world; }
SavedWorldScope::~SavedWorldScope() { preparingWorldServer=previous; }
CWorldServer &ActiveWorldServer() { return *(preparingWorldServer?preparingWorldServer:publishedWorldServer); }
void PublishWorldServer(CWorldServer *world) noexcept { publishedWorldServer = world; }

struct SFieldCampSnapshot {
    ObjectId identity=0;
    int id=-1;
    float hp=0,mp=0;
    SRPGHitLocation hitLocations[RPG_NUM_HIT_LOCATIONS]{};
};
struct SFieldCampVarSnapshot { const char *name; float value; bool existed; };
struct CWorldServer::FieldCampState {
    bool armed=false,inCamp=false,commerce=false;
    int returnHeroId=-1;
    CGSVariableSet *commerceVars=nullptr;
    SFieldCampVarSnapshot commerceValues[6]{};
    std::vector<SFieldCampSnapshot> snapshots;
    ModCampReferences::Pending pending;
};
namespace {
const SFieldCampSnapshot *FindFieldCampSnapshot(const std::vector<SFieldCampSnapshot> &snapshots, int id) {
    for (const auto &snapshot : snapshots) if (snapshot.id == id) return &snapshot;
    return nullptr;
}
void RestoreFieldCampHitLocations(SRPGUnitStats &stats, const SFieldCampSnapshot &snapshot) {
    for (int i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i) {
        auto &hit = stats.hls[i];
        hit.fpHP = max(0.0f, min(hit.fpMaxHP, snapshot.hitLocations[i].fpHP));
        hit.dwCrippleMask = snapshot.hitLocations[i].dwCrippleMask;
        hit.state = snapshot.hitLocations[i].state;
    }
}
void ZeroFieldCampItem(CItem *item) {
    if (item && (item->GetTypeID() == ITID_ITEM_WEAPON || item->GetTypeID() == ITID_ITEM_ARMOR || item->GetTypeID() == ITID_ITEM_QUICK))
        item->SetMP(0.0f);
}
void ZeroFieldCampPersonage(CPersonage &personage) {
    for (int i = 0; i < personage.weapons.GetSize(); ++i) ZeroFieldCampItem(personage.weapons[i]);
    for (int i = 0; i < personage.armors.GetSize(); ++i) ZeroFieldCampItem(personage.armors[i]);
    for (int i = 0; i < personage.items.GetSize(); ++i) ZeroFieldCampItem(personage.items[i]);
}
}

/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::StoreFieldCamp(SaveSchema::Writer &w) const
{
    using namespace SaveSchema;using namespace Fields;
    const auto &camp=*fieldCamp;
    w.Bool(camp.armed);w.Bool(camp.inCamp);w.Int32(camp.returnHeroId);
    Count(w,camp.snapshots.size());
    for(const auto &s:camp.snapshots){
        w.UInt64(s.identity);w.Int32(s.id);w.Float32(s.hp);w.Float32(s.mp);
        // These are the only hit-location fields restored on return. Armor
        // pointers in the old scratch copy must not outlive their removed units.
        for(const auto &hit:s.hitLocations){w.Float32(hit.fpHP);Field(w,hit.dwCrippleMask);Field(w,hit.state);}
    }
    w.Bool(camp.commerceVars!=nullptr);
    if(camp.commerceVars)for(const auto &v:camp.commerceValues){w.Float32(v.value);w.Bool(v.existed);}
    ModCampReferences::WorldAccess context{const_cast<CWorldServer &>(*this)};
    std::vector<ModCampReferences::View> views;
    if(!context.Collect(views))throw Error(ErrorCode::Invalid,"invalid camp script scopes");
    std::vector<const ModCampReferences::Snapshot *> pending;
    for(const auto &s:camp.pending)
        if(std::find(views.begin(),views.end(),s.view)!=views.end())pending.push_back(&s);
    auto target=[&](int id){
        for(const auto &s:camp.snapshots)if(s.id==id&&s.identity){w.UInt64(s.identity);return;}
        throw Error(ErrorCode::Invalid,"missing camp target identity");
    };
    auto name=[&](const ModCampReferences::Name &n){w.Bool(n.available);w.Int32(n.count);w.String(n.text);};
    Count(w,pending.size());for(const auto *s:pending){
        const auto index=std::find(views.begin(),views.end(),s->view)-views.begin();
        w.UInt32(static_cast<std::uint32_t>(index));w.Int32(s->view.scope);w.String(s->view.name);
        Count(w,s->objects.size());for(const auto &o:s->objects){w.Int32(o.index);target(o.id);name(o.name);}
        Count(w,s->groups.size());for(const auto &g:s->groups){w.Int32(g.index);name(g.name);Count(w,g.ids.size());for(int id:g.ids)target(id);}
    }
}
void CWorldServer::LoadFieldCamp(SaveSchema::Reader &r)
{
    using namespace SaveSchema;using namespace Fields;
    auto camp=std::make_shared<FieldCampState>();
    camp->armed=r.Bool();camp->inCamp=r.Bool();camp->returnHeroId=r.Int32();
    auto n=r.Count();if(n>8)throw Error(ErrorCode::Invalid,"too many camp heroes");
    std::map<ObjectId,int> targets;std::set<int> mapIds;
    camp->snapshots.resize(n);for(auto &s:camp->snapshots){
        s.identity=r.UInt64();s.id=r.Int32();s.hp=r.Float32();s.mp=r.Float32();
        if(!s.identity||s.id<0||!targets.emplace(s.identity,s.id).second||!mapIds.insert(s.id).second)
            throw Error(ErrorCode::Invalid,"invalid camp hero identity");
        for(auto &hit:s.hitLocations){hit.fpHP=r.Float32();Field(r,hit.dwCrippleMask);Field(r,hit.state);}
    }
    camp->commerce=r.Bool();
    static const char *names[6]={"constr.current","i.noconstr","i.offspellconstr","i.onspellconstr","i.offitemconstr","i.onitemconstr"};
    if(camp->commerce)for(int i=0;i<6;++i){auto &v=camp->commerceValues[i];v.name=names[i];v.value=r.Float32();v.existed=r.Bool();}
    ModCampReferences::WorldAccess context{*this};std::vector<ModCampReferences::View> views;
    if(!context.Collect(views))throw Error(ErrorCode::Invalid,"invalid saved camp scopes");
    auto target=[&](){auto id=r.UInt64();auto it=targets.find(id);if(it==targets.end())throw Error(ErrorCode::Invalid,"invalid camp ObjectId reference");return it->second;};
    auto name=[&](ModCampReferences::Name &v){v.available=r.Bool();v.count=r.Int32();v.text=r.String();if(!ModCampReferences::Bounded(v.count))throw Error(ErrorCode::Invalid,"invalid camp variable metadata");};
    n=r.Count();std::set<std::uint32_t> scopes;
    for(std::uint32_t i=0;i<n;++i){
        auto index=r.UInt32();int scope=r.Int32();auto owner=r.String();
        if(index>=views.size()||!scopes.insert(index).second||views[index].scope!=scope||views[index].name!=owner)
            throw Error(ErrorCode::Invalid,"camp script scope mismatch");
        ModCampReferences::Snapshot s;s.view=views[index];
        auto count=r.Count();s.objects.resize(count);for(auto &o:s.objects){o.index=r.Int32();o.id=target();name(o.name);}
        count=r.Count();s.groups.resize(count);for(auto &g:s.groups){g.index=r.Int32();name(g.name);auto members=r.Count();if(members>8)throw Error(ErrorCode::Invalid,"invalid camp group size");g.ids.resize(members);for(auto &id:g.ids)id=target();}
        camp->pending.push_back(std::move(s));
    }
    fieldCamp.swap(camp);
}
void CWorldServer::BindSavedCampParty(CParty &party) noexcept
{
    fieldCamp->commerceVars=fieldCamp->commerce?&party.vars:nullptr;
}

static void SynchronizeFieldCampItemEnergy(CUnitServer &unitServer, CItem *item) {
    if (!item || !item->GetUserData() || !item->GetSpell(0)) return;
    const float mp = item->GetMP();
    const float maxMp = item->GetMaxMP();
    if (!std::isfinite(mp) || !std::isfinite(maxMp) || mp < 0.0f || maxMp <= 0.0f) return;
    unitServer.SynchronizeItemMana(item);
}
static void SynchronizeFieldCampUnitEnergy(CUnit *unit) {
    if (!unit || !unit->GetOwner()) return;
    CUnitSpecific *specific = unit->GetUnitSpecific();
    if (!specific || specific->WhoAmI() != HPC_UNITSERVERSPECIFIC) return;
    CUnitServer &unitServer = *static_cast<CUnitServer *>(specific);
    for (int i = 0; i < QI_MAX_WEAPONS; ++i)
        SynchronizeFieldCampItemEnergy(unitServer, unit->GetQWeapon(i));
    for (int i = 0; i < QI_MAX_ITEMS; ++i)
        SynchronizeFieldCampItemEnergy(unitServer, unit->GetQItem(i));
    for (int i = 0; i < RPG_NUM_ARMOR_TYPES; ++i)
        SynchronizeFieldCampItemEnergy(unitServer, unit->GetQArmor(i));
}
/////////////////////////////////////////////////////////////////////////////////////
// -------------------------------- Create/destroy
// ---------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////////////
CWorldServer::CWorldServer(bool detached) : detachedSave(detached), fieldCamp(std::make_shared<FieldCampState>())
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif
    Clear();
}

/////////////////////////////////////////////////////////////////////////////////////
CWorldServer::~CWorldServer()
{
    Destroy();
}

/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldServer::Create()
{
    Destroy();
    if (!CWorldBase::Create())
        return ReportError("CWorldServer::Create.\n Can't create base world.\n");

    CString szResourcesPath = GetResourcePath();
    if (!messages.Open(szResourcesPath + PSZ_MESSAGES_FILE_NAME))
        return ReportError("CWorldServer::Create.\n Can't opent message base resource.\n");
    // Specific manager
    pSpecificManager = dbgnew CServerSpecificManager();
    pSpecificManager->AddRef();
    pSpecificManager->SetWorld(this);
    // hoover
    pHoover = dbgnew CHoover();
    pHoover->AddRef();
    // wind
    ptWindDirection = CPlot3D(1, 0, 0);
    fpWindForce = 0.4f;
    // ai map
    //	if ( !aiMap.Create() )
    //		return FALSE;
    curInterface = IFACE_NONE;
    bCanRunScript = FALSE;
    exits.RemoveAll();
    weatherChange = GMW_RAIN;
    skyType = GMS_NORMAL;
    bSendCenterCamera = false;
    bLoadDynamic = true;
    bLoadStatic = true;
    szCurrentZoneName = "";
    serverMode = SERV_MODE_NONE;
    szLoadedMobs.RemoveAll();
    return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::Destroy()
{
    if (!detachedSave) ModOutOfCombatRegeneration::Reset();
    if (!detachedSave) OwnedRender::E1ClearSaveNotice();
    // Kill All clients
    {
    CPlayer *pPlayer;
    CPlayerIterator playerIterator;
    CPlayerIterator *playerIter=&playerIterator; // pins its manager without allocating
    if (GetPlayerManager())
    {
        pPlayer = playerIter->GetFirst((CPlayerManager *)GetPlayerManager());
        for (; pPlayer; pPlayer = playerIter->GetNext())
        {
            if(detachedSave){
                pPlayer->retiringSave=true;
                if(auto *specific=dynamic_cast<CPlayerServer *>(pPlayer->GetPlayerSpecific()))specific->RetireSavedWorld();
            }
            pPlayer->Disconnect();
            ((CPlayerManager *)GetPlayerManager())->RemovePlayer(pPlayer);
        }
    }
    } // release iterator's manager pin before destroying managers

    if (pHoover)
    {
        pHoover->DecRefCount();
        delete pHoover;
        pHoover = NULL;
    }

    script.Destroy();
    scriptTemp.Destroy();
    Clear();
    //	aiMap.Destroy();
    messages.Close();
    exits.RemoveAll();
    CWorldBase::Destroy();
}

/////////////////////////////////////////////////////////////////////////////////////
// Destroy effects
void CWorldServer::RemoveEffects()
{
    CEffectPtrHashIterator effIter;
    CEffect *pEff = effIter.GetFirst(&effects);
    while (pEff)
    {
        pEff->FreeReferences();
        pEff->Release();
        pEff = effIter.GetNext();
    }
    effects.RemoveAll();
}

/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::RemoveEverything()
{
    szLoadedMobs.RemoveAll();
    waterLevels.Clear();
    // remove exits from zone
    exits.RemoveAll();
    // erase script
    script.Clear();
    scriptTemp.Clear();
    // destroy spells
    spells.RemoveAll();
    //
    activeItems.RemoveAll();
    // release all refs on mapobjects from players
    if (GetPlayerManager())
    {
        CPlayerIterator playerIterator;
        CPlayerIterator *playerIter=&playerIterator;
        CPlayer *pPlayer = playerIter->GetFirst((CPlayerManager *)GetPlayerManager());
        for (; pPlayer; pPlayer = playerIter->GetNext())
            pPlayer->RemoveAllObjects();
    
    }
    // Destroy map objects
    ASSERT(mapObjects.GetCount() == staticObjs.GetCount() + dynamicObjs.GetCount());
    ASSERT(dynamicMapObjs.GetCount() <= dynamicObjs.GetCount());
    CMapObjPtrHashIterator mapobjIter;
    CMapObject *pMO = mapobjIter.GetFirst(&mapObjects);
    while (pMO)
    {
        pMO->FreeReferences();
        pMO = mapobjIter.GetNext();
    }
    ReleaseHashMap((GameBaseHashMap &)mapObjects);
    staticObjs.RemoveAll();
    dynamicObjs.RemoveAll();
    dynamicMapObjs.RemoveAll();
    activeSet.RemoveAll();
    questMapObjs.RemoveAll();
    // destroy spells/effects
    spells.RemoveAll();
    RemoveEffects();

    szMobFileName = "";
    szCurrentZoneName = "";
    // destroy ALL TRASH DATA!
    if (pFigureManager)
        pFigureManager->ClearTrash();
    if (pMapObjectManager)
        pMapObjectManager->ClearTrash();
    if (pSpecificManager)
        pSpecificManager->ClearTrash();
}

/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::Clear()
{
    if (!detachedSave) ModOutOfCombatRegeneration::Reset();
    // Camp state belongs to this world, including after it has been adopted.
    ModCampReferences::Reset(fieldCamp->pending);
    fieldCamp->snapshots.clear();fieldCamp->armed=fieldCamp->inCamp=false;
    fieldCamp->returnHeroId=-1;fieldCamp->commerceVars=nullptr;fieldCamp->commerce=false;
    RemoveEverything();
    szZoneFileName = "";
    // clear other objects
    fof.Clear();
    script.Clear();
    scriptTemp.Clear();
    aiMap.Clear();
    bMapLoaded = FALSE;
    fpNightPower = 0;
    for (int i = 0; i < ARRAY_SIZE(alarms); i++)
        alarms[i].Clear();
    curInterface = IFACE_NONE;
    ptDefaultCamera = CPlot3D(0, 0, 0);
    fpVisionBonus = 1;
    bTransferItemCreates = false;
    nLastLightningTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
//---------------------------------------- Update player
//--------------------------------------------------
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::SetChangedFlags(DWORD dwFlags)
{
    CPlayer *pPl;
    CPlayerIterator playerIter;
    // Set zone changed flag
    pPl = playerIter.GetFirst((CPlayerManager *)GetPlayerManager());
    while (pPl)
    {
        pPl->SetChanged(dwFlags);
        pPl = playerIter.GetNext();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldServer::UpdatePlayer(CPlayer *pPl)
{
    int i;
    // Update map objects
    static CGameBasePtrArray newVisible, toAdd, toRemove, toUpdate;
    CPlayerServer *pPlServ = (CPlayerServer *)pPl->GetPlayerSpecific();

    if (pPlServ->GetStatus() == CPlayerServer::PSS_ACTIVE && IsMapLoaded())
    {
        if (pPl->IsChanged(DW_PCHANGED_ZONE))
        {
            if (!SendNewZoneToPlayer(pPl))
                return FALSE;
            pPlServ->SendActiveItems();
            return pPlServ->SendUpdate();
        }
        if (pPl->IsChanged(DW_PCHANGED_WEATHER))
        {
            if (!pPlServ->SendWeather((BYTE)curWeather, Float2Int(fpWeatherComeTime)))
                return FALSE;
        }
        if (pPl->IsChanged(DW_PCHANGED_TIME))
        {
            float fpCurTime, fpTimeScale;
            GetTimeParams(fpCurTime, fpTimeScale);
            if (!pPlServ->SendWorldTime(fpCurTime, fpTimeScale))
                return FALSE;
        }
        if (pPl->IsChanged(DW_PCHANGED_WIND))
        {
            if (!pPlServ->SendWind(ptWindDirection, fpWindForce))
                return FALSE;
        }
        if (pPl->IsChanged(DW_PCHANGED_WATERLEVEL))
        {
            pPlServ->SendWaterLevels(waterLevels);
        }
        // pPlServ->FormNewVisibleSet();

        newVisible.SetSize(mapObjects.GetCount() * 7);
        CMapObjPtrHashIterator mapobjIter;
        CMapObject *pMO;
        LPGAMEBASE *plpVis = newVisible.GetData();
        if (curInterface == IFACE_MISSION && theSettings.nDrawVisibleOnly == 0)
        {
            // отрисовка всех юнитов на карте
            pMO = mapobjIter.GetFirst(&dynamicObjs);
            for (; pMO; pMO = mapobjIter.GetNext())
            {
                if (pMO->GetTypeID() == MOTID_TRAP)
                    continue;
                const CPlot3D &cp = pMO->GetCP();
                const CPlot3D &cpPlayer = pPlServ->GetCameraCP();
                float fpDx = cp.x - cpPlayer.x;
                float fpDy = cp.y - cpPlayer.y;
                float fpDist2 = fpDx * fpDx + fpDy * fpDy;
                if (fpDist2 < OwnedRender::E2EffectDistance() * OwnedRender::E2EffectDistance() ||
                    (pMO->WhoAmI() == HPC_UNIT && ((CUnit *)pMO)->GetOwner() == pPl))
                    *plpVis++ = pMO;
            }
        }
        else
        {
            // обычный режим, в случае mission отсылаютс€ динамические не юниты и видимые юниты
            // иначе отсылаютс€ динамические объекты из радиуса
            if (curInterface == IFACE_MISSION)
                pMO = mapobjIter.GetFirst(&dynamicMapObjs);
            else
                pMO = mapobjIter.GetFirst(&dynamicObjs);
            if (curInterface == IFACE_BASE)
            {
                for (; pMO; pMO = mapobjIter.GetNext())
                {
                    if (pMO->GetTypeID() == MOTID_TRAP)
                        continue;
                    if (pMO->CheckFlag(MAPOBJ_TRANSPARENT))
                        continue;
                    *plpVis++ = pMO;
                }
            }
            else
            {
                for (; pMO; pMO = mapobjIter.GetNext())
                {
                    if (pMO->GetTypeID() == MOTID_TRAP)
                        continue;
                    const CPlot3D &cp = pMO->GetCP();
                    const CPlot3D &cpPlayer = pPlServ->GetCameraCP();
                    float fpDx = cp.x - cpPlayer.x;
                    float fpDy = cp.y - cpPlayer.y;
                    float fpDist2 = fpDx * fpDx + fpDy * fpDy;
                    if (fpDist2 < OwnedRender::E2EffectDistance() * OwnedRender::E2EffectDistance())
                        *plpVis++ = pMO;
                }
            }
            if (curInterface == IFACE_MISSION)
            {
                int nVisibleUnitsCount = pPlServ->currentVisible.GetSize();
                for (i = 0; i < nVisibleUnitsCount; i++)
                    *plpVis++ = pPlServ->currentVisible.GetAt(i).GetObject();
                // add all quest objects
                CMapObjPtrHashIterator moIter;
                for (pMO = moIter.GetFirst(&questMapObjs); pMO; pMO = moIter.GetNext())
                    *plpVis++ = pMO;
                //
                CUnit *pUnit;
                CUnitPtrHashIterator unitIter;
                for (pUnit = unitIter.GetFirst(&pPl->unitsUnderControl); pUnit;
                     pUnit = unitIter.GetNext())
                {
                    CUnitList units;
                    float fpRadius = ((CUnitServer *)pUnit->GetUnitSpecific())
                                             ->GetMaxVisionRange() *
                                         AI_MAX_VISION_BONUS +
                                     5;
                    aiMap.GetUnitsNear(pUnit->GetCP(), units, fpRadius);
                    for (POSITION pos = units.GetHeadPosition(); pos;)
                        *plpVis++ = units.GetNext(pos);
                }
            }
        }

        newVisible.SetSize(plpVis - newVisible.GetData());
        SelectIDs(pPlServ->visibleObjects, newVisible, toAdd, toRemove, toUpdate);

        pPlServ->RemoveMapObjects(toRemove);
        pPlServ->AddMapObjects(toAdd);
        pPlServ->UpdateMapObjects(toUpdate);

        // Update effects
        newVisible.SetSize(effects.GetCount());
        CEffectPtrHashIterator effIter;

        CEffect *pEff = effIter.GetFirst(&effects);
        plpVis = newVisible.GetData();
        while (pEff)
        {
            const CPlot3D &cp = pEff->GetEffectPlace(pPlServ->currentVisible);
            const CPlot3D &cpPlayer = pPlServ->GetCameraCP();
            float fpDx = cp.x - cpPlayer.x;
            float fpDy = cp.y - cpPlayer.y;
            float fpDist2 = fpDx * fpDx + fpDy * fpDy;
            if (fpDist2 < pEff->GetRange() * pEff->GetRange())
                *plpVis++ = pEff;
            pEff = effIter.GetNext();
        }

        newVisible.SetSize(plpVis - newVisible.GetData());
        SelectIDs(pPlServ->visibleEffects, newVisible, toAdd, toRemove, toUpdate);

        pPlServ->RemoveEffects(toRemove);
        pPlServ->AddEffects(toAdd);
        pPlServ->UpdateEffects(toUpdate);



        // send changed items mana
        /*
        CList<CItem*,CItem*> aItems;
        for ( POSITION pos = activeItems.GetHeadPosition(); pos; )
                aItems.AddTail( activeItems.GetNext( pos ) );
        if ( aItems.GetCount() )
                pPlServ->SendItemsMana( aItems );
                */
    }
    //
    if ((pPlServ->GetStatus() == CPlayerServer::PSS_ACTIVE ||
         pPlServ->GetStatus() == CPlayerServer::PSS_CAMP) &&
        IsMapLoaded())
        pPlServ->SendVisibleUnitsList();
    //
    if (pPlServ->GetStatus() != CPlayerServer::PSS_LOGOUT)
    {
        if (pPlServ->GetStatus() != CPlayerServer::PSS_WAITING_FOR_LOGIN)
        {
            if (pPl->IsChanged(DW_PCHANGED_INTERFACE) &&
                pPlServ->GetStatus() != CPlayerServer::PSS_ZONELOADWAIT)
                pPlServ->SendNewInterface();
        }
        return pPlServ->SendUpdate();
    }
    else
        return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::GatherUnitStatistics(CMemorySave &packet)
{
    CMapObjPtrHashIterator mapobjIter;
    CMapObject *pMO;
    CUnitStatisticsDriver stats;
    for (pMO = mapobjIter.GetFirst(&dynamicObjs); pMO; pMO = mapobjIter.GetNext())
    {
        if (pMO->WhoAmI() != HPC_UNIT)
            continue;
        stats.AddUnit((CUnit *)pMO);
    }
    stats.Store(packet);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldServer::SendNewZoneToPlayer(CPlayer *pPl)
{
    BOOL bRv;
    CMemorySave packet;
    CPlayerServer *pPlServ = (CPlayerServer *)pPl->GetPlayerSpecific();
    if (pPlServ->GetStatus() != CPlayerServer::PSS_ACTIVE)
    {
        // ASSERT( 0 && "Trying to send new zone information to non connected client" );
        return TRUE; // FALSE;
    }
    // send new zone info
    /*
    if ( bCombatPause )
            packet.Write( nSegmentTimeCounter );
    else*/
    packet.Write(nSegmentTimeCounter - 1);
    packet.WriteString(szZoneFileName);
    packet.WriteString(szMobFileName);
    StoreCArray<SZoneExit, SZoneExit &>(exits, packet);
    packet.Write(&skyType, sizeof(skyType));
    GatherUnitStatistics(packet);

    bRv = pPlServ->SendNewZone(packet);
    if (bRv && bSendCenterCamera)
        bRv = CenterCameraOnSomeone(pPl);
    if (!bRv)
        return bRv;
    // send inventory update
    pPlServ->AddItemsToHoover(pPl->items, false);
    // Both normal entry and saved-world adoption must wait for PID_ZONE_LOADED.
    // Queue inventory while still active, then await the client's acknowledgement.
    pPlServ->StartZoneLoading();
    return bRv;
    /*
                    // transmitting all items and spells from server to client, make it happen dude
       :) packet.Clear(); pSpellManager->Store( packet ); pItemManager->Store( packet );
            pSpellManager->StoreData( packet );
            pItemManager->StoreData( packet );

            return pPlServ->SendNewItems( packet );*/
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldServer::CenterCameraOnSomeone(CPlayer *pPl)
{
    BOOL bRv = TRUE;
    CPlot3D ptLookAt;
    CPlayerServer *pPlServ = (CPlayerServer *)pPl->GetPlayerSpecific();
    CUnitPtrHashIterator unitIter;
    CUnit *pUnit;
    if (pPl->unitsUnderControl.GetCount())
    {
        pUnit = unitIter.GetFirst(&pPl->unitsUnderControl);
        ptLookAt = pUnit->GetCP();
        ptLookAt.z = GetSurfaceValue(ptLookAt.x, ptLookAt.y);
        bRv = pPlServ->SendMoveCamera(ptLookAt, 0, curInterface);
    }
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::SetSky(EGMZoneSky newSky)
{
    skyType = newSky;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::SetWeatherChange(EGMZoneWeather weather)
{
    if (curWeather != EW_CLEAR)
        SetWeather(EW_CLEAR, N_WEATHER_RAIN_TO_CLEAR);
    weatherChange = weather;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::RecalcNightPower()
{
    CMapObject *pMO;
    CMapObjPtrHashIterator mapobjIter;

    fpNightPower = GetNightPowerSlow();

    // maybe change weather ?
    if (curInterface == IFACE_MISSION)
    {
        if (weatherChange != GMW_NONE)
        {
            if (weatherChange == GMW_TORNADO)
            {
                CPlot3D ptCenter = CPlot3D(random.GetFloat(0,
                                                           512), // AITile2Meter( aiMap.XSize() ) ),
                                           random.GetFloat(0,
                                                           512), // AITile2Meter( aiMap.YSize() ) ),
                                           0);
                CPlot3D ptDir(0, 0, 0);
                int nTType = aiMap.GetTerraType(ptCenter);
                if ((nTType == AI_N_SAND_TYPE_ID || nTType == 1) && !aiMap.IsWater(ptCenter))
                {
                    ptDir.x = random.GetFloat(-70, 70);
                    ptDir.y = random.GetFloat(-70, 70);
                    if (fabs2(ptDir) > 900)
                    {
                        float fpDH = aiMap.TraceReliefDH(ptCenter, ptCenter + ptDir);
                        if (fpDH < 1)
                        {
                            CEffectTornado *pTornado;
                            pTornado = (CEffectTornado *)((CEffectManager *)GetEffectManager())
                                           ->CreateData(ETypeIDs(HPC_EFFECTTORNADO));
                            pTornado->SetParams(ptCenter, ptCenter + ptDir);
                            AddEffect(pTornado);
                        }
                    }
                }
            }
            else
            {
                static int nWeatherChangeDelay = 0;
                if (--nWeatherChangeDelay <= 0)
                {
                    if (curWeather == EW_CLEAR)
                    {
                        if (random.Get(0, 100) == 0)
                        {
                            nWeatherChangeDelay = 10;
                            if (weatherChange == GMW_RAIN)
                                SetWeather(EW_RAIN, N_WEATHER_CLEAR_TO_RAIN);
                            else
                                SetWeather(EW_SNOW, N_WEATHER_CLEAR_TO_SNOW);
                        }
                    }
                    else
                    {
                        if (random.Get(0, 30) == 0)
                        {
                            nWeatherChangeDelay = 10;
                            if (weatherChange == GMW_RAIN)
                                SetWeather(EW_CLEAR, N_WEATHER_RAIN_TO_CLEAR);
                            else
                                SetWeather(EW_CLEAR, N_WEATHER_SNOW_TO_CLEAR);
                        }
                    }
                }
            }
        }
        if (curWeather == EW_RAIN)
        {
            if (GetSegmentTimeCounter() > nLastLightningTime + 20)
            {
                nLastLightningTime = GetSegmentTimeCounter();
                CPlot3D ptFrom, ptTo;
                ptTo.x = random.GetFloat(2, 512 - 2);
                ptTo.y = random.GetFloat(2, 512 - 2);
                ptTo.z = this->GetSurfaceValue(ptTo.x, ptTo.y) - 5;
                ptFrom.x = ptTo.x + random.GetFloat(-2, 2);
                ptFrom.y = ptTo.y + random.GetFloat(-2, 2);
                ptFrom.z = ptTo.z + 40.f;
                IWorld.InvokeLightning(ptFrom, ptTo);
            }
        }
    }
    if (curWeather == EW_CLEAR)
        fpVisionBonus = 1;
    else
        fpVisionBonus = 0.9f;
    // Clear update flags
    pMO = mapobjIter.GetFirst(&mapObjects);
    while (!mapobjIter.IsReachEnd())
    {
        pMO->TimeOfDayChanged();
        pMO = mapobjIter.GetNext();
    }
    // update global time variables
    if (curInterface == IFACE_MISSION || curInterface == IFACE_BASE)
        AddPlayersGlobalTime();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::AddPlayersGlobalTime(float fpShift)
{
    CPlayerIterator playerIter;
    int nCurSegment = GetSegmentTimeCounter();
    for (CPlayer *pPlayer = playerIter.GetFirst((CPlayerManager *)GetPlayerManager()); pPlayer;
         pPlayer = playerIter.GetNext())
    {
        int nDeltaT = nCurSegment - pPlayer->nTimeWasAddedSegment;
        pPlayer->nTimeWasAddedSegment = nCurSegment;
        ASSERT(nDeltaT >= 0);
        if (nDeltaT < 0)
            nDeltaT = 0;
        float fpTime = pPlayer->party.vars.GetVar(szPartyGlobalTimeVarName);
        float fpDeltaT = GetTimeInHours(nDeltaT) + fpShift;
        fpTime += fpDeltaT;
        ((CPlayerServer *)pPlayer->GetPlayerSpecific())->SetVar(szPartyGlobalTimeVarName, fpTime);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::UnitSayAck(CUnit *pUnit, DWORD dwAck)
{
    CPlayerIterator playerIter;
    for (CPlayer *pPlayer = playerIter.GetFirst((CPlayerManager *)GetPlayerManager()); pPlayer;
         pPlayer = playerIter.GetNext())
    {
        CPlayerServer *pPS = (CPlayerServer *)pPlayer->GetPlayerSpecific();
        if (pPS->IsUnitVisibleToParty(pUnit))
            pPS->SendFailure(pUnit, dwAck);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// ********************* Active units set
static int ActiveObjectPeriod(CMapObject *object)
{
    CModPackage *mod = ModRuntime::Current();
    const bool extended = mod && mod->ExtendActiveObjectsArea();
    // The installed addon extends both the base period and the stagger.
    return (extended ? 256 : 128) +
           static_cast<int>((object->GetRTID() & 15) << (extended ? 1 : 0));
}
void CWorldServer::AddActiveSpot(const CPlot3D &ptCenter, float fpRadius)
{
    int nGridCount;
    static CAIGridMesh grid;
    int nUnitCellX, nUnitCellY;
    SAIGridPoint *pGrid = grid.GetGridPoints(fpRadius / aiMap.GetObjectCellSize(), nGridCount);
    aiMap.GetCellCoords(ptCenter, nUnitCellX, nUnitCellY);

    for (int i = 0; i < nGridCount; i++)
    {
        // Get map object list on the i-th cell surrounding unit
        CUnitList *pList = aiMap.GetUnitCellList(nUnitCellX + pGrid[i].nDX,
                                                 nUnitCellY + pGrid[i].nDY);
        if (pList == NULL)
            continue;
        for (POSITION pos = pList->GetHeadPosition(); pos;)
            AddActiveObject(pList->GetNext(pos));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::AddActiveObject(CMapObject *pObject)
{
    pObject->SetActivityCounter(ActiveObjectPeriod(pObject));
    activeSet.SeekNew(pObject->GetID()) = pObject;
}
/////////////////////////////////////////////////////////////////////////////////////
// Snapshot keys rather than pointers: Segment may remove or replace another object.
// Objects activated during this pass start on the next pass.
static const std::vector<std::pair<ObjectId, int>> &OrderedTickObjects(
    CMapObjPtrHash &objects, std::vector<std::pair<ObjectId, int>> &order)
{
    order.clear();
    order.reserve(objects.GetCount());
    CMapObjPtrHashIterator iter;
    for (auto *object = iter.GetFirst(&objects); object; object = iter.GetNext())
        order.emplace_back(object->GetRTID(), object->GetID());
    std::sort(order.begin(), order.end());
    return order;
}
static CMapObject *FindTickObject(CMapObjPtrHash &objects, const std::pair<ObjectId, int> &key)
{
    auto *slot = objects.Seek(key.second);
    return slot && *slot && (*slot)->GetRTID() == key.first ? *slot : nullptr;
}
void CWorldServer::ActiveObjectsSegment()
{
    CorePerf::Scope coreTiming(CorePerf::ActiveObjects);
    int i;
    POSITION pos;
    CMapObject *pMO;
    static CIntArray invalidObjects;
    static CIntArray notActiveObjects;

    CorePerf::Objects(dynamicObjs.GetCount(), activeSet.GetCount());
    invalidObjects.SetSize(0);
    notActiveObjects.SetSize(0);
    if(auto*mod=ModRuntime::Current())mod->TickStatRefresh();
    if (curInterface != IFACE_MISSION)
    {
        for (const auto &key : OrderedTickObjects(dynamicObjs, tickObjectOrder))
        {
            pMO = FindTickObject(dynamicObjs, key);
            if (!pMO) continue;
            CorePerf::ObjectVisited();
            pMO->Segment();
            if (!pMO->IsValid())
                invalidObjects.Add(pMO->GetID());
        }
    }
    else
    {
        // The addon keeps spell durations moving for objects outside the active set.
        for (const auto &key : OrderedTickObjects(dynamicObjs, tickObjectOrder))
        {
            pMO = FindTickObject(dynamicObjs, key);
            if (!pMO) continue;
            if (!pMO->IsInvalid() && !pMO->IsUnreferenced() && pMO->TickInactiveSpellEffects())
                AddActiveObject(pMO);
        }
        for (const auto &key : OrderedTickObjects(activeSet, tickObjectOrder))
        {
            pMO = FindTickObject(activeSet, key);
            if (!pMO) continue;
            CorePerf::ObjectVisited();
            pMO->Segment();
            if (!pMO->IsValid())
                invalidObjects.Add(pMO->GetID());
            if (pMO->WhoAmI() == HPC_UNIT && !pMO->CountActivity())
            {
                CUnitServer *pUS = (CUnitServer *)((CUnit *)pMO)->GetUnitSpecific();
                if (!pUS->IsAggressive() && !pUS->IsAlwaysActive() && !pUS->logic.bEnemyVisible)
                    notActiveObjects.Add(pMO->GetID());
                else
                    pMO->SetActivityCounter(ActiveObjectPeriod(pMO));
            }
        }
    }
    // call segment for items
    for (pos = activeItems.GetHeadPosition(); pos;)
    {
        POSITION delPos = pos;
        if (!activeItems.GetNext(pos)->Segment())
            activeItems.RemoveAt(delPos);
    }
    for (i = 0; i < notActiveObjects.GetSize(); i++)
        RemoveObjectFromActive(notActiveObjects[i]);
    // Remove invalid objects
    for (i = 0; i < invalidObjects.GetSize(); i++)
        RemoveMapObject(invalidObjects[i]);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::RemoveObjectFromActive(int nID)
{
    activeSet.RemoveAt(nID);
}
//-------------------------------------------- Segment
//----------------------------------------------------
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::KillPlayer(CPlayer *pPlayer)
{
    pPlayer->Disconnect();
    ((CPlayerManager *)GetPlayerManager())->RemovePlayer(pPlayer);
    NConsole::net << "Disconnecting player " << pPlayer->GetPlayerNumber() << endl;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::NetDriverSegment(CLowLevelNetDriver *pDriver)
{
    POSITION pos;
    CPlayer *pPlayer;
    CConnectionsList newPlayers;

    // check if server is ready to accept connections
    if (GetServerMode() == SERV_MODE_NONE)
        return;
    // check if there are any new players to come from here on they participate in game process
    pDriver->GetNewPlayers(newPlayers);
    pos = newPlayers.GetHeadPosition();
    while (pos)
    {
        CConnection *pConnection = newPlayers.GetNext(pos);
        // pPlayer = GetPlayer( 0 ); // we are considering single player strict for now
        // ASSERT( !pPlayer );
        /// if ( pPlayer )
        //((CPlayerManager*)GetPlayerManager())->RemovePlayer( pPlayer );
        pPlayer = ((CPlayerManager *)pPlayerManager)->CreatePlayer();
        pPlayer->SetConnection(pConnection);
        pPlayer->SetInterface(curInterface);
        pPlayer->SetChanged(DW_PCHANGED_EVERYTHING);
        ((CPlayerServer *)pPlayer->GetPlayerSpecific())->GetCameraCP() = ptDefaultCamera;
        NConsole::net << "Accepted player " << pPlayer->GetPlayerNumber() << endl;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::StartServer(EServerMode mode)
{
    GameJournal::Event("SESSION", "role", "server_mode=%d role=%s", int(mode), mode == SERV_MODE_LOCAL ? "single" : "interface");
    ASSERT(GetPlayerManager()->GetNumElements() == 0);
    ASSERT(serverMode == SERV_MODE_NONE);
    ASSERT(mode != SERV_MODE_NONE);
    if (serverMode != SERV_MODE_NONE)
        return;
    serverMode = mode;

}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::WorldSegment(bool bForceScriptRun)
{
    CorePerf::Scope coreTiming(CorePerf::WorldSegment);
    POSITION pos;
    CEffect *pEff;
    CEffectPtrHashIterator effIter;
    CMapObjPtrHashIterator mapobjIter;

    SetSegmentTimeCounter(GetSegmentTimeCounter() + 1);
    GameJournal::Tick(static_cast<unsigned long>(GetSegmentTimeCounter()));
    // Determine new night vision power
    if ((GetSegmentTimeCounter() & N_TIMERECALCINTERVAL) == 3)
        RecalcNightPower();
    CorePerf::Scope coreEffects(CorePerf::Effects);
    // Call segments for effects
    pEff = effIter.GetFirst(&effects);
    std::vector<int> ids;
    while (!effIter.IsReachEnd())
    {
        if (pEff->Segment() == FALSE)
        {
            ids.push_back(pEff->GetID());
            pEff->Release();
        }
        pEff = effIter.GetNext();
    }
    // actually remove
    for (auto pos1 = ids.begin(); pos1 != ids.end(); ++pos1)
        effects.RemoveAt(*pos1);
    ids.clear();
    coreEffects.Finish();
    // aiMap segment - footsteps management mainly
    { CorePerf::Scope coreTiming(CorePerf::AIMap);
    aiMap.Segment(GetSegmentTimeCounter()); }
    CorePerf::Scope coreWater(CorePerf::Water);
    // step of water levels
    if (waterLevels.Segment())
        SetChangedFlags(DW_PCHANGED_WATERLEVEL);
    coreWater.Finish();
    CorePerf::Scope coreSpells(CorePerf::Spells);
    // call segments for each spell in progress
    pos = spells.GetHeadPosition();
    while (pos)
    {
        POSITION posDel = pos;
        CEffectSpell *pSpell = spells.GetNext(pos);
        if (!pSpell->Segment())
            spells.RemoveAt(posDel);
    }
    coreSpells.Finish();
    // CRAP - single player only, in mission run script only if party is deployed
    if (bCanRunScript || bForceScriptRun) // curInterface != IFACE_MISSION || bCanRunScript )
    { CorePerf::Scope coreScript(CorePerf::Script); script.Segment(); }
    { CorePerf::Scope coreTiming(CorePerf::ScriptTemp);
    scriptTemp.Segment(); }
    // Call segment for map objects
    ActiveObjectsSegment();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldServer::Segment()
{
    CorePerf::ServerTick coreTick(GetJournalZoneName(), dynamicObjs.GetCount(), activeSet.GetCount());
    //
    BOOL bRv = TRUE;
    CPlayer *pPlayer;
    CPlayerIterator playerIter;

    CorePerf::Scope corePackets(CorePerf::ServerPackets);
    // Parse packets from client
    pPlayer = playerIter.GetFirst((CPlayerManager *)GetPlayerManager());
    for (; pPlayer; pPlayer = playerIter.GetNext())
    {
        // Parse incoming packets
        CConnection *pCon = pPlayer->GetConnection();
        if (!pCon || !pCon->CanSend() ||
            pPlayer->GetPlayerSpecific()->ParseIncomingPackets() == FALSE)
            KillPlayer(pPlayer);
        else
        {
            // update active zones
            CUnitPtrHashIterator unitIter;
            CUnit *pUnit;
            for (pUnit = unitIter.GetFirst(&pPlayer->unitsUnderControl); pUnit;
                 pUnit = unitIter.GetNext())
                AddActiveSpot(pUnit->GetCP(),
                              ((CUnitServer *)pUnit->GetUnitSpecific())->GetMaxVisionRange() *
                                      AI_MAX_VISION_BONUS +
                                  ((ModRuntime::Current() && ModRuntime::Current()->ExtendActiveObjectsArea()) ? 150 : 20));
        }
    }

    corePackets.Finish();
    CorePerf::Scope coreNetwork(CorePerf::ServerNetwork);
    // Call segment for network drivers
    if (pNetDriver)
    {
        pNetDriver->Segment();
        NetDriverSegment(pNetDriver);
    }

    coreNetwork.Finish();
    // world processing
    if (!IsCombatPause())
    {
        // clear all addtovisible arrays and the like from players data
        for (pPlayer = playerIter.GetFirst((CPlayerManager *)GetPlayerManager()); pPlayer;
             pPlayer = playerIter.GetNext())
            pPlayer->PrepareNewSegment();
        WorldSegment();
    }

    // Form visible set for players
    {
        CorePerf::Scope coreTiming(CorePerf::VisibleSet);
        pPlayer = playerIter.GetFirst((CPlayerManager *)GetPlayerManager());
        for (; pPlayer; pPlayer = playerIter.GetNext())
            ((CPlayerServer *)pPlayer->GetPlayerSpecific())->FormNewVisibleSet();
        //
        CScriptObjectGroup vis;
        script.GetGlobalVars().gVars.GetVar("Heroes", vis);
        pPlayer = playerIter.GetFirst((CPlayerManager *)GetPlayerManager());
        for (; pPlayer; pPlayer = playerIter.GetNext())
        {
            CPlayerServer *pPS = (CPlayerServer *)pPlayer->GetPlayerSpecific();
            if (!pPS->IsCameraBound())
            {
                CScriptObjectGroup temp;
                temp.Add(pPS->currentVisible, vis);
                vis = temp;
            }
        }
        //
        pPlayer = playerIter.GetFirst((CPlayerManager *)GetPlayerManager());
        for (; pPlayer; pPlayer = playerIter.GetNext())
        {
            CPlayerServer *pPS = (CPlayerServer *)pPlayer->GetPlayerSpecific();
            if (!pPS->IsCameraBound())
                pPS->currentVisible = vis;
        }
    }

    // Update players
    pPlayer = playerIter.GetFirst((CPlayerManager *)GetPlayerManager());
    for (; pPlayer; pPlayer = playerIter.GetNext())
    {
        // Create update info
        if (!UpdatePlayer(pPlayer))
        {
            KillPlayer(pPlayer);
            continue;
        }
    }
    // Increase updates counter
    ++nUpdateCounter;

    return bRv;
}

/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldServer::GetExitDirection(int nExit, CString &szZone, int &nZoneEntrance)
{
    if (nExit < 0 || nExit >= exits.GetSize())
        return FALSE;
    szZone = exits[nExit].szZone;
    nZoneEntrance = exits[nExit].nZoneEntrance;
    return TRUE;
}
// -------------------------------- Create effects
// ---------------------------------------------------------
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::AddEffect(CEffect *pEff)
{
    pEff->StartEffect(nSegmentTimeCounter);
    pEff->AddRef();
    pEff->SetUpdateMask();

    assert(effects.Seek(pEff->GetID()) == NULL);
    effects.SetAt(pEff->GetID(), pEff);
}

/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::RemoveEffect(int nID)
{
    CEffect **ppEff = effects.Seek(nID);
    // ASSERT( ppEff );
    if (!ppEff)
        return;
    (*ppEff)->Invalidate();
    (*ppEff)->Release();
    effects.RemoveAt(nID);
}

// --------------------------------- Cast spell
// ------------------------------------------------------------
void CWorldServer::CastSingleSpell(CSpell *pSpell, CUnit *pWho, CPlot3D ptFrom, CMapObject *pTarget,
                                   const CPlot3D &ptTarget, bool bQuiet)
{
    CEffectSpell *pES;
    CPlot3D ptTarg;
    if (pTarget)
        ptTarg = pTarget->GetCP();
    else
        ptTarg = ptTarget;
    if (pSpell->IsOnTile())
        pTarget = 0;
    ptTarg.z = GetSurfaceValue(ptTarg.x, ptTarg.y) + 0.3f;
    if (pWho)
        ptFrom = pWho->GetCP();

    pES = dbgnew CEffectSpell;
    pES->TargetSpell(this, *pSpell, pWho, ptFrom, pTarget, ptTarg, bQuiet);
    if (pES->Segment())
        spells.AddTail(pES);
    // if spell is disbonus treat it as an attack
    if (pTarget && pTarget->WhoAmI() == HPC_UNIT && pWho)
    {
        CUnit *_pTarget = (CUnit *)pTarget;
        CUnitServer *pUS = (CUnitServer *)_pTarget->GetUnitSpecific();
        switch (pSpell->GetSubtypeID() >> 16)
        {
        case SPELL_ID_WEAKEN:
        case SPELL_ID_FEEBLEMIND:
        case SPELL_ID_STUN:
        case SPELL_ID_SLOW:
            pUS->logic.TakeDamage(pWho);
            fof.MakeDamage(pWho, _pTarget, 10); // 10 - amount of damage
            _pTarget->SetFlag(UNIT_RECALCENEMY);
            pWho->SetFlag(UNIT_RECALCENEMY);
            break;
        case SPELL_ID_STENCH:
        case SPELL_ID_SHRINK:
            // f27 changes only the cast-time attack reaction for these two IDs.
            if (const auto* mod = ModRuntime::Current();
                !mod || mod->SpellAddon().Feature27() != 1)
            {
                pUS->logic.TakeDamage(pWho);
                fof.MakeDamage(pWho, _pTarget, 10);
                _pTarget->SetFlag(UNIT_RECALCENEMY);
                pWho->SetFlag(UNIT_RECALCENEMY);
            }
            break;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::CastSpell(CSpell *pSpell, CUnit *pWho, const CPlot3D &ptFrom,
                             CMapObject *pTarget, const CPlot3D &ptTarget, bool bQuiet)
{
    ASSERT(pSpell);
    if (!pSpell || (curInterface != IFACE_MISSION && pWho != 0))
        return;
    const auto* spellMod = ModRuntime::Current();
    if (!(spellMod && (spellMod->SpellAddon().Feature02() == 1 ||
                      spellMod->CombatSetting("UnlockExperimentalSpells"))))
    {
        switch (pSpell->GetSubtypeID() >> 16)
        {
        case SPELL_ID_LINK:
        case SPELL_ID_CHARM:
        case SPELL_ID_POSSESSION:
            return;
        }
    }
    int nTargets = pSpell->GetNumTargets();
    if (pSpell->GetFilters() == 0 && nTargets <= 1)
        CastSingleSpell(pSpell, pWho, ptFrom, pTarget, ptTarget, bQuiet);
    else
    {
        CPlayerServer *pPS = 0;
        // multiple targets spell
        CUnitList units;
        CPlot3D ptSrc;
        //
        if (pWho)
        {
            CPlayer *pPlayer = pWho->GetOwner();
            if (pPlayer)
                pPS = (CPlayerServer *)pPlayer->GetPlayerSpecific();
        }
        // for terrain spell cast first into terrain
        if (pSpell->IsOnTile() && !pSpell->IsItemContinuous())
        {
            nTargets--;
            CastSingleSpell(pSpell, pWho, ptFrom, pTarget, ptTarget, bQuiet);
        }
        //
        if (pWho)
            ptSrc = pWho->GetCP();
        else
            ptSrc = ptFrom;
        aiMap.GetUnitsNear(ptSrc, units, pSpell->GetRange());
        // make sure pTarget is first to fire at
        if (pTarget && pTarget->WhoAmI() == HPC_UNIT)
        {
            POSITION pos;
            pos = units.Find((CUnit *)pTarget);
            if (pos)
                units.RemoveAt(pos);
            units.AddHead((CUnit *)pTarget);
        }
        // search for valid targets
        while (nTargets > 0)
        {
            float fpBest = 1e38f;
            CUnit *pBest = 0;
            POSITION bestPos;
            for (POSITION pos = units.GetHeadPosition(); pos;)
            {
                POSITION delPos = pos;
                CUnit *pUnit = units.GetNext(pos);
                if (pUnit->IsDead())
                    units.RemoveAt(delPos);
                else
                {
                    if (pUnit == pTarget)
                    {
                        pBest = pUnit;
                        bestPos = delPos;
                        break;
                    }
                    else
                    {
                        float fpDist = fabs2(ptTarget - pUnit->GetCP());
                        if (fpDist < fpBest)
                        {
                            fpBest = fpDist;
                            pBest = pUnit;
                            bestPos = delPos;
                        }
                    }
                }
            }
            // if found current best target
            if (pBest)
            {
                bool bCanCast = true;
                units.RemoveAt(bestPos);
                // test visibility
                if (pWho)
                {
                    if (pPS)
                        bCanCast = pPS->currentVisible.HasMapObject(pBest) != 0;
                    else
                    {
                        if (!((CUnitServer *)pWho->GetUnitSpecific())
                                 ->logic.environment.visibleUnits.Find(pBest))
                            bCanCast = fof.IsPlayerAlly(pBest->GetPlayerNum(),
                                                        pWho->GetPlayerNum()) != 0;
                    }
                }
                // test terrain
                if (bCanCast)
                    bCanCast = IsSpellCastingDirect(this, pSpell, pWho, ptFrom, pBest,
                                                    pBest->GetCP());
                // now we check if target fits filter
                if (bCanCast && pSpell->IsValidTarget(pWho, pBest))
                {
                    CastSingleSpell(pSpell, pWho, ptFrom, pBest, pBest->GetCP(), bQuiet);
                    nTargets--;
                }
            }
            else
                break;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::SetupTrap(CItemQuick *pTrap, CUnit *pWho)
{
    CMETrap trap;
    trap.p_nID = -1;
    trap.p_szTemplate = "st.tr.130";
    trap.p_szPrimTexture = "unhuma";
    trap.p_ptPosition = pWho->GetCP();
    trap.p_ptPosition.z = 0;
    trap.p_tidTypeID = MOTID_KAPKAN;
    trap.p_Complection.grace = 0;
    int nID = AddMapObject(&trap);
    CTrapObject *pTrapObj = (CTrapObject *)GetMapObject(nID);
    ASSERT(pTrapObj->WhoAmI() == HPC_TRAP);
    pTrapObj->ActivateTrap();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::SetWeather(EWeather newWeather, float fpTransTime)
{
    curWeather = newWeather;
    fpWeatherComeTime = fpTransTime;
    SetChangedFlags(DW_PCHANGED_WEATHER);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::SetServerWind(const CPlot3D &_ptWindDirection, float fpForce)
{
    fpForce = Max(fpForce, 0.0f);
    fpForce = Min(fpForce, 1.0f);
    CWorldBase::SetWind(_ptWindDirection, fpForce);
    SetChangedFlags(DW_PCHANGED_WIND);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::SetWorldTime(float fpCurrentTime)
{
    if (curInterface == IFACE_MISSION || curInterface == IFACE_BASE)
        AddPlayersGlobalTime();
    CWorldBase::SetWorldTime(fpCurrentTime);
    SetChangedFlags(DW_PCHANGED_TIME);
    RecalcNightPower();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::SetWorldTimeScale(float fpScale)
{
    if (curInterface == IFACE_MISSION || curInterface == IFACE_BASE)
        AddPlayersGlobalTime();
    CWorldBase::SetWorldTimeScale(fpScale);
    SetChangedFlags(DW_PCHANGED_TIME);
    RecalcNightPower();
}
// ------------------------------ Create map objects
// -------------------------------------------------------
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::DeployPersonages(CPlayer *pPlayer, CPersonages &people, const CPlot3D &ptPlace,
                                    const CQuaternion rot, SDeployPlayerInfo *pInfo)
{
    int i, k;
    const bool fieldCampReturn = fieldCamp->inCamp;
    if(fieldCampReturn)if(auto* mod=ModRuntime::Current())mod->InvalidateInfoStatsCache();
    if (fieldCampReturn) {
        for (i = 0; i < people.GetSize(); ++i) ZeroFieldCampPersonage(*people[i]);
        if (pPlayer) {
            for (i = 0; i < pPlayer->party.allWagonItems.GetSize(); ++i) ZeroFieldCampItem(pPlayer->party.allWagonItems[i]);
            for (POSITION pos = pPlayer->items.GetHeadPosition(); pos;) ZeroFieldCampItem(pPlayer->items.GetNext(pos));
        }
    }
    CMEUnit me;
    CUnit *pUnit;
    const char *pszBPs[] = { "hd",  "bd",  "hp",  "hr",  "lh1", "lh2", "lh3", "rh1", "rh2",
                             "rh3", "ll1", "ll2", "ll3", "rl1", "rl2", "rl3", // "rh3.sworld00",
                             NULL };
    const char **ppBPs;
    CString szAddToScript, szTemp;
    bool bFirstDeploy;
    const auto* introMod=ModRuntime::Current();
    bFirstDeploy = ModItemUiPolicy::FirstDeploy(fieldCampReturn,
        introMod && introMod->CombatSetting("PreventZone1Animation"),szZoneFileName,
        fabs2(ptPlace - CPlot3D(54, 138, 0)) < 300);

    static unsigned introNotes=0;if(introNotes++<16)EngineDiagnostics::Event("P05 intro zone=%s campReturn=%d prevent=%d firstDeploy=%d",szZoneFileName.GetString(),int(fieldCampReturn),int(introMod && introMod->CombatSetting("PreventZone1Animation")),int(bFirstDeploy));
    if (fieldCampReturn) EngineDiagnostics::Event("Camp deploy: initial animation suppressed zone=%s", szZoneFileName.GetString());
    szAddToScript = "WorldScript(";
    for (i = 0; i < people.GetSize(); i++)
    {
        CPersonage &pers = *people[i];

        me.p_szUnitPrototype = pers.szPrototype;
        me.p_sLogic[0].eLogicModel = UM_PLAYER;
        me.p_sLogic[0].bEnabled = TRUE;
        me.p_sLogic[0].ClearMotivation();
        me.m_eType = MEOT_UNIT;
        me.p_szTemplate = pers.szTemplate;
        me.p_Complection = pers.complection;
        me.p_isShadow = TRUE;
        me.p_nPlayer = 0;
        me.p_szPrimTexture = pers.szPrimTexture;
        me.p_szSecTexture = pers.szSecondTexture;
        me.p_aBodyParts.SetSize(0);
        me.p_tidTypeID = static_cast<CMapObjectManager *>(GetMapObjectManager())
                             ->GetTypeIDFromPrototype(pers.szPrototype);
        // form bodypart names for human
        if (me.p_tidTypeID == MOTID_HUMAN)
        {
            const SUDBUnitPrototype *pUnitPrototype = theUnitDatabase.prototypes[pers.szPrototype];
            DWORD dwHair = 1;
            if (pUnitPrototype)
                dwHair = pUnitPrototype->dwHair;

            ppBPs = pszBPs;
            while (ppBPs[0])
            {
                if (strcmp(*ppBPs, "hr") == 0)
                {
                    if (int(dwHair) != -1)
                    {
                        szTemp.Format("hr.%.2d", dwHair);
                        me.p_aBodyParts.Add(szTemp);
                    }
                    ppBPs++;
                }
                else
                    me.p_aBodyParts.Add(*ppBPs++);
            }
        }
        //
        me.p_nID = pers.nID; // if ID is -1 it is assigned automatically
        ASSERT(!GetMapObject(me.p_nID));
        if (GetMapObject(me.p_nID))
            RemoveMapObject(me.p_nID);
        //
        SDeployUnitInfo *pPlace = 0;
        if (pInfo)
            pPlace = pInfo->GetInfo(me.p_nID);
        if (pPlace)
        {
            me.p_ptPosition = pPlace->ptPlace;
            me.p_qRotation = pPlace->orient;
        }
        else
        {
            float fpAngle;
            CPlot3D ptAxis, ptShift(0, 0, 0);
            rot.DecompAxisAngle(ptAxis, fpAngle);
            if (ptAxis.z < 0)
                fpAngle = -fpAngle;
            ptShift.x = cos(fpAngle + FP_PI);
            ptShift.y = sin(fpAngle + FP_PI);
            me.p_ptPosition = ptPlace; // position of object or/and visualisation object
            me.p_ptPosition += ptShift * i * 1.2f;
            me.p_qRotation = rot;
        }

        int nMissionUnitID = AddMapObject(&me);
        ASSERT(nMissionUnitID == pers.nID);

        pUnit = GetUnit(nMissionUnitID);
        if (pUnit)
        {
            CUnitServer *pUS = (CUnitServer *)pUnit->GetUnitSpecific();
            pUS->logic.SetPlayer();
            // find the place to put unit on
            aiMap.RemoveObject(pUnit);
            CPlot3D ptNewUnitPlace = pUnit->GetCP();
            aiMap.FindUnitPosition(pUnit, ptPlace, ptNewUnitPlace);
            pUnit->SetCP(ptNewUnitPlace);
            aiMap.AddObject(pUnit);
            //
            pUnit->SetOwner(pPlayer);
            pUnit->SetName(pers.szName);
            pUnit->SetAggression(pers.aggression);
            //
            const auto* armorMod = ModRuntime::Current();
            if (!(armorMod && armorMod->SpellAddon().Feature30() == 1))
                pUnit->ClearRPGAbsorbtions(); // Hero absorbtions must be equal to '0'
            pUnit->SetRPGStats(&pers.stats);
            // give unit some items etc
            pUnit->RemoveAllItems();
            for (k = 0; k < pers.weapons.GetSize(); k++)
                pUnit->AddWeapon((CItemWeapon *)DuplicateItemWithSrc(pers.weapons[k]));
            pUnit->ChangeWeapon(0);
            for (k = 0; k < pers.armors.GetSize(); k++)
                pUnit->Wear((CItemArmor *)DuplicateItemWithSrc(pers.armors[k]));
            for (k = 0; k < pers.items.GetSize(); k++)
            {
                CItem *pItem = DuplicateItemWithSrc(pers.items[k]);
                pItem->SetCount(pers.items[k]->GetCount());
                pItem->SetMP(pItem->GetMaxMP());
                pUnit->AddQItem((CItemQuick *)pItem);
            }
            for (k = 0; k < pers.spells.GetSize(); k++)
                pUnit->AddQSpell((CItemSpell *)DuplicateItemWithSrc(pers.spells[k]));
            pUnit->RecalcAIClass();
            pUnit->RecalcRPGStats(); // recalc RPG stats again to take in account items weight
            pUnit->SetVoicePrototype(pers.szVoicePrototype);
            //
            szTemp.Format("AddObject( Heroes, GetObjectByID( \"%i\" ) )", pUnit->GetID());
            szAddToScript += szTemp;
            //
            // pUnit->LowSetPoses( UP_LAY, UP_LAY );
            if (curInterface == IFACE_MISSION)
            {
                if (!bFirstDeploy)
                    pUnit->SetWishPose(pers.pose);
            }
            if (pPlace)
            {
                if (aiMap.CanPutUnit(1, pUnit->GetCP()))
                    pUnit->LowSetPoses(pPlace->pose, pPlace->pose);
                else
                    pUnit->SetWishPose(pPlace->pose);
            }
            pUS->InitializeUnit();
            if (curInterface == IFACE_MISSION || curInterface == IFACE_BASE)
                pUnit->WantWeapon(pers.dwCurrentWeapon);
            if (pPlace && curInterface != IFACE_BASE)
                pUnit->WantWeapon(pPlace->dwCurrentWeapon);
        }
    }
    // recreate follows
    if (curInterface == IFACE_MISSION)
    {
        for (i = 0; i < people.GetSize(); i++)
        {
            CPersonage &pers = *people[i];
            CUnit *pUnit = GetUnit(pers.nID);
            if (pUnit)
            {
                CUnitServer *pUS = (CUnitServer *)pUnit->GetUnitSpecific();
                if (pers.nFollowToID > 0)
                {
                    CUMPlayer *pUP = pUS->logic.GetPlayer();
                    if (pUP)
                        pUP->DoFollow(pUS->logic, GetUnit(pers.nFollowToID));
                }
            }
        }
    }
    if (bFirstDeploy)
        szAddToScript +=
            //		"BlockUnit(GetObjectByName( \"hero\" ),1)"
            "PlayAnimation(GetObjectByName( \"hero\" ),\"uspecial25\")"
            //		"Sleep(570)"
            //		"BlockUnit(GetObjectByName( \"hero\" ),0)"
            ; //"Sleep( 25 ) Stand(GetObjectByName( \"hero\" ))";
    if (const auto* mod = ModRuntime::Current())
        if (mod->SpellAddon().Feature13() >= 1)
            szAddToScript += "AddMob(\"AutoRunSpellAddon123.mob\")";
    szAddToScript += ")";
    if (pPlayer)
        script.LoadText(szAddToScript);
    if (fieldCampReturn) {
        if (ModRuntime::Current() && ModRuntime::Current()->CombatSetting("JMVGoCampRebindScriptReferences"))
            ModCampReferences::RestoreWorld(fieldCamp->pending,*this);
        ModCampReferences::Reset(fieldCamp->pending);
        bool restored = !fieldCamp->snapshots.empty();
        for (i = 0; i < static_cast<int>(fieldCamp->snapshots.size()); ++i) {
            const SFieldCampSnapshot &snapshot = fieldCamp->snapshots[i];
            CUnit *unit = GetUnit(snapshot.id);
            CUnitServer *unitServer = unit && unit->GetUnitSpecific() && unit->GetUnitSpecific()->WhoAmI() == HPC_UNITSERVERSPECIFIC
                ? static_cast<CUnitServer *>(unit->GetUnitSpecific()) : nullptr;
            CMapObjectServer *commonServer = unitServer ? unitServer->GetMapObjectServer() : nullptr;
            if (!unit || !unitServer || !commonServer) { restored = false; continue; }
            unitServer->RecalcRPGStats(false);
            RestoreFieldCampHitLocations(*unitServer->GetRPGStats(), snapshot);
            SRPGCommonStats *common = commonServer->GetRPGStats();
            float hp = unitServer->GetRPGStats()->CollectHP(common->ssHP.fpMax);
            common->ssHP.fpCurrent = max(0.0f, min(common->ssHP.fpMax, hp));
            common->ssMP.fpCurrent = max(0.0f, min(common->ssMP.fpMax, snapshot.mp));
            for (int slot = 0; slot < QI_MAX_WEAPONS; ++slot) ZeroFieldCampItem(unit->GetQWeapon(slot));
            for (int slot = 0; slot < QI_MAX_ITEMS; ++slot) ZeroFieldCampItem(unit->GetQItem(slot));
            for (int slot = 0; slot < RPG_NUM_ARMOR_TYPES; ++slot) ZeroFieldCampItem(unit->GetQArmor(slot));
            unit->SetUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);
            unit->SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);
            unit->SetUpdated();
        }
        const int mainHeroId = fieldCamp->snapshots.empty() ? -1 : fieldCamp->snapshots.front().id;
        for (const SFieldCampSnapshot &snapshot : fieldCamp->snapshots)
            SynchronizeFieldCampUnitEnergy(GetUnit(snapshot.id));
        fieldCamp->returnHeroId = mainHeroId;
        fieldCamp->snapshots.clear();
        fieldCamp->inCamp = false;
        if (!restored) fieldCamp->armed = false;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::DeployHooverItems(CPlayer *pPlayer, CList<CItem *, CItem *> &items)
{
    ASSERT(pPlayer);
    if (pPlayer == NULL)
        return;
    for (POSITION pos = items.GetHeadPosition(); pos;)
    {
        CItem *pItem = items.GetNext(pos);
        CList<CItem *, CItem *> newItem;
        CItem *pNewItem = DuplicateItem(pItem);
        pNewItem->SetCount(pItem->GetCount());
        newItem.AddTail(pNewItem);
        pPlayer->AddItemsToHoover(newItem);
        pItem->Release();
    }
    items.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::RemovePersonages(CPersonages &people, CWorld *pParty, SDeployPlayerInfo *pInfo)
{
    // RemovePlayerParty has already moved server items to the wagon. Clear the
    // retained client's old item IDs through the ordinary ordered packet stream.
    CModPackage *campMod = ModRuntime::Current();
    if (fieldCamp->armed && this == &theWorldServer && campMod && campMod->CombatSetting("JMVGoCamp")) {
        CPlayer *player = GetPlayer(0);
        CPlayerServer *server = player ? dynamic_cast<CPlayerServer *>(player->GetPlayerSpecific()) : nullptr;
        if (server && &player->party == pParty && player->curInterface == IFACE_CAMP && player->items.IsEmpty())
            server->RemoveAllHoover();
    }
    if (fieldCamp->armed) {
        fieldCamp->snapshots.clear();
        for (int index = 0; index < people.GetSize(); ++index) {
            CPersonage &personage = *people[index];
            CUnit *unit = GetUnit(personage.nID);
            CUnitServer *unitServer = unit && unit->GetUnitSpecific() && unit->GetUnitSpecific()->WhoAmI() == HPC_UNITSERVERSPECIFIC
                ? static_cast<CUnitServer *>(unit->GetUnitSpecific()) : nullptr;
            CMapObjectServer *commonServer = unitServer ? unitServer->GetMapObjectServer() : nullptr;
            if (!unit || unit->IsDead() || !unitServer || !commonServer) { fieldCamp->snapshots.clear(); fieldCamp->armed = false; break; }
            SFieldCampSnapshot snapshot = {};
            snapshot.id = personage.nID; snapshot.identity=unit->GetRTID();
            snapshot.hp = commonServer->GetCurrentHP();
            snapshot.mp = commonServer->GetCurrentMP();
            for (int hit = 0; hit < RPG_NUM_HIT_LOCATIONS; ++hit) snapshot.hitLocations[hit] = unitServer->GetRPGStats()->hls[hit];
            fieldCamp->snapshots.push_back(snapshot);
        }
        if (fieldCamp->snapshots.size() != static_cast<size_t>(people.GetSize())) { fieldCamp->snapshots.clear(); fieldCamp->armed = false; }
        if (fieldCamp->armed && ModRuntime::Current() && ModRuntime::Current()->CombatSetting("JMVGoCampRebindScriptReferences")) {
            std::vector<int> ids;
            for (const auto& snapshot : fieldCamp->snapshots) ids.push_back(snapshot.id);
            ModCampReferences::CaptureWorld(fieldCamp->pending,*this,ids);
        } else ModCampReferences::Reset(fieldCamp->pending);
    }
    int i, k;
    // store follow info
    if (curInterface == IFACE_MISSION)
    {
        for (i = 0; i < people.GetSize(); i++)
        {
            CPersonage &pers = *people[i];
            CUnit *pUnit = GetUnit(pers.nID);
            CUnitServer *pUS = (CUnitServer *)pUnit->GetUnitSpecific();
            CUnit *pLeader = pUS->logic.GetPlayerLeader();
            if (pLeader)
                pers.nFollowToID = pLeader->GetID();
            else
                pers.nFollowToID = -1;
        }
    }
    //
    for (i = 0; i < people.GetSize(); i++)
    {
        CPersonage &pers = *people[i];
        CUnit *pUnit = GetUnit(pers.nID);
        ASSERT(pUnit);
        if (pUnit)
        {
            CUnitServer *pUS = (CUnitServer *)pUnit->GetUnitSpecific();
            if (pInfo)
            {
                pInfo->units.Add(SDeployUnitInfo(pUnit->GetID(), pUnit->GetCP(),
                                                 pUnit->GetOrientation(), pUnit->GetShowPose(),
                                                 pUnit->GetCurrentWeaponSlotIndex()));
            }
            if (!pUnit->IsDead())
            {
                while (pers.armors.GetSize())
                    pers.RemoveArmor(pers.armors[0]);
                while (pers.weapons.GetSize())
                    pers.RemoveWeapon(pers.weapons[0]);
                while (pers.spells.GetSize())
                    pers.RemoveSpell(pers.spells[0]);
                while (pers.items.GetSize())
                    pers.RemoveQuick(pers.items[0]);
                for (k = 0; k < QI_MAX_WEAPONS; k++)
                {
                    CItemWeapon *pWeapon = pUnit->GetQWeapon(k);
                    if (pWeapon)
                        pers.AddWeapon(
                            CSmartPtr<CItemWeapon>((CItemWeapon *)pParty->DuplicateItem(pWeapon)));
                }
                for (k = 0; k < RPG_NUM_ARMOR_TYPES; k++)
                {
                    CItemArmor *pItem = pUnit->GetQArmor(k);
                    if (pItem)
                        pers.AddArmor(
                            CSmartPtr<CItemArmor>((CItemArmor *)pParty->DuplicateItem(pItem)));
                }
                for (k = 0; k < QI_MAX_ITEMS; k++)
                {
                    CItemQuick *pItem = pUnit->GetQItem(k);
                    if (pItem && pItem->GetCount() > 0)
                    {
                        CItem *pDup = pParty->DuplicateItem(pItem);
                        pDup->SetCount(pItem->GetCount());
                        pDup->SetMP(pDup->GetMaxMP());
                        pers.AddQuick(CSmartPtr<CItemQuick>((CItemQuick *)pDup));
                    }
                }
                for (k = 0; k < QI_MAX_SPELLS; k++)
                {
                    CItemSpell *pItem = pUnit->GetQSpellItem(k);
                    if (pItem)
                        pers.AddSpell(
                            CSmartPtr<CItemSpell>((CItemSpell *)pParty->DuplicateItem(pItem)));
                }
                pUS->GetRPGStats(&pers.stats);
                if (curInterface == IFACE_MISSION)
                {
                    pers.pose = pUnit->GetShowPose();
                    pers.dwCurrentWeapon = pUnit->GetCurrentWeaponSlotIndex();
                }
                pers.aggression = pUnit->GetAggression();
                //
                pers.stats.ResurrectAll();
            }
            if (!pUnit->IsDead())
                RemoveMapObject(pUnit->GetID());
            else
                pUnit->SetOwner(0);
        }
    }
    if (fieldCamp->armed && fieldCamp->snapshots.size() == static_cast<size_t>(people.GetSize())) {
        for (i = 0; i < people.GetSize(); ++i) if (const SFieldCampSnapshot *snapshot = FindFieldCampSnapshot(fieldCamp->snapshots, (*people[i]).nID)) RestoreFieldCampHitLocations((*people[i]).stats, *snapshot);
        fieldCamp->armed = false;
        fieldCamp->inCamp = true;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::AddItemsFromHoover(CPlayer *pPlayer, CParty *pWagon)
{
    ASSERT(pPlayer);
    if (!pPlayer)
        return;
    for (POSITION pos = pPlayer->items.GetHeadPosition(); pos;)
    {
        CItem *pItem = pPlayer->items.GetNext(pos);
        pWagon->AddItemToWagon(pItem, true);
        pItem->Release();
    }
    pPlayer->items.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
// выбирает квестовые предметы, которые могут пригодитс€ на текущей зоне
void CWorldServer::SelectQuestItems(CSimplyArray<CItem *, CItem *> &wagon,
                                    CList<CItem *, CItem *> &questItems)
{
    int k;
    CMapObject *pMO;
    CArray<DWORD, DWORD> dwRequiredKeys;
    CMapObjPtrHashIterator mapobjIter;
    // gather all scriptID`s of keys that might be useful on the map
    for (pMO = mapobjIter.GetFirst(&mapObjects); pMO; pMO = mapobjIter.GetNext())
    {
        if (pMO->IsScientific())
        {
            BOOL bFound = FALSE;
            DWORD dwKey = pMO->GetScienceKeyNumber();
            for (int i = 0; i < dwRequiredKeys.GetSize(); i++)
            {
                if (dwRequiredKeys[i] == dwKey)
                    bFound = TRUE;
            }
            if (!bFound)
                dwRequiredKeys.Add(dwKey);
        }
    }
    // select useful quest items
    for (k = 0; k < wagon.GetSize(); k++)
    {
        if (wagon[k]->WhoAmI() == HPC_ITEM_QUEST)
        {
            CItemQuest *pItem = (CItemQuest *)wagon[k];
            const SIDBQuestItemShape *pShape = (const SIDBQuestItemShape *)pItem->GetShapeEntry();
            bool bTake = false;
            if (curInterface == IFACE_MISSION)
            {
                if (pShape)
                {
                    //
                    for (int i = 0; i < pShape->szZones.size(); i++)
                    {
                        if (pShape->szZones[i].CompareNoCase(szCurrentZoneName) == 0)
                            bTake = true;
                    }
                }
                // if there is a lever matching this quest item
                if (!bTake)
                {
                    DWORD dwKey = pItem->GetScriptID();
                    for (int i = 0; i < dwRequiredKeys.GetSize(); i++)
                    {
                        if (dwRequiredKeys[i] == dwKey)
                        {
                            bTake = true;
                            break;
                        }
                    }
                }
            }
            else
                bTake = true;
            if (bTake)
            {
                questItems.AddTail(wagon[k]);
                wagon.RemoveAt(k);
                k--;
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// deploys player`s quest items
void CWorldServer::DeployPlayerItems(CPlayer *pPlayer)
{
    CList<CItem *, CItem *> questItems;
    if (curInterface == IFACE_GLOBAL_MAP)
        return;
    SelectQuestItems(pPlayer->party.allWagonItems, questItems);
    DeployHooverItems(pPlayer, questItems);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::RemovePlayerItems(CPlayer *pPlayer)
{
    if (curInterface == IFACE_GLOBAL_MAP)
        return;
    AddItemsFromHoover(pPlayer, &pPlayer->party);
    ((CPlayerServer *)pPlayer->GetPlayerSpecific())->SendPartyItems();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::DeployPlayerParty(CPlayer *pPlayer, SDeployPlayerInfo *pInfo, bool bExecScript)
{
    ASSERT(pPlayer);
    if (!pPlayer)
        return;

    DeployPlayerItems(pPlayer);
    //
    if (curInterface != IFACE_MISSION && curInterface != IFACE_BASE)
        return;
    if (!IsMapLoaded())
        return;
    CPersonages pers;
    for (int i = 0; i < pPlayer->party.people.GetSize(); i++)
        pers.Add(&pPlayer->party.people[i]);

    // mark current zone for local parties

    // determine deploy place
    CPlot3D ptPlace = CPlot3D(0, 0, 0);
    float fAngle = 0, fPersAngle;
    CQuaternion rot = QNULL;
    CPlot3D ptMapCenter(AITile2Meter(aiMap.XSize()) / 2, AITile2Meter(aiMap.YSize()) / 2, 0);

    SScenarioZone *pZone = theMap.GetZoneByName(pPlayer->party.szCurrentZone);
    SZoneExit *pEx = 0;
    ASSERT(pZone);
    if (pZone)
    {
        ASSERT(pPlayer->party.nEntrance >= 0 && pPlayer->party.nEntrance < pZone->exits.GetSize());
        if (pPlayer->party.nEntrance >= 0 && pPlayer->party.nEntrance < pZone->exits.GetSize())
        {
            pEx = &pZone->exits[pPlayer->party.nEntrance];
            SZoneExit &ex = *pEx;
            ptPlace = CPlot3D((ex.x1 + ex.x2) / 2, (ex.y1 + ex.y2) / 2, 0);
            fAngle = ex.fCameraRotate;
        }
    }
    fPersAngle = atan2(ptPlace.y - ptMapCenter.y, ptPlace.x - ptMapCenter.x) - FP_PI2;
    if (pEx && pEx->bPersRotate)
        fPersAngle = pEx->fPersRotate;
    rot.FromAxisAngle(CPlot3D(0, 0, 1), fPersAngle);
    // deploy itself
    DeployPersonages(pPlayer, pers, ptPlace, rot, pInfo);
    if (pInfo)
        pInfo->Clear();
    // set camera position for objects send
    CPlayerServer *pPS = ((CPlayerServer *)pPlayer->GetPlayerSpecific());
    if (pPS->GetCameraCP().z != -1313) //  не подали ли они знак ?
    {
        // нет, не подали
        pPS->GetCameraCP() = ptPlace;
        pPS->SendMoveCamera(ptPlace, fAngle, curInterface);
    }
    pPlayer->nTimeWasAddedSegment = GetSegmentTimeCounter();

    if (curInterface == IFACE_BASE)
        RecalcMercBriefings();
    // CRAP, single player only version, solves problems with hide/unhide on base script
    if (bExecScript)
    {
        bCanRunScript = TRUE;
        script.Segment();
        script.Segment();
        script.Segment();
        script.Segment();
        script.Segment();
    }
    // end of CRAP
}
/////////////////////////////////////////////////////////////////////////////////////
// this function updates inventory of party from current state on server
void CWorldServer::RemovePlayerParty(CPlayer *pPlayer, SDeployPlayerInfo *pInfo)
{
    int i;
    ASSERT(pPlayer);
    if (!pPlayer)
        return;
    if (curInterface == IFACE_MISSION || curInterface == IFACE_BASE)
        AddPlayersGlobalTime();
    // now grab items
    RemovePlayerItems(pPlayer);
    //
    if (curInterface != IFACE_MISSION && curInterface != IFACE_BASE)
        return;
    if (!IsMapLoaded())
        return;
    CPersonages pers;
    // remove dead units from party
    for (i = 0; i < pPlayer->party.people.GetSize(); i++)
    {
        CPersonage &pers = pPlayer->party.people[i];
        CUnit *pUnit = GetUnit(pers.nID);
        ASSERT(pUnit);
        if (!pUnit || (pUnit->IsDead()))
            pPlayer->party.people.RemoveAt(i--);
    }
    for (i = 0; i < pPlayer->party.people.GetSize(); i++)
        pers.Add(&pPlayer->party.people[i]);
    // get items and stats from alive ones
    RemovePersonages(pers, &pPlayer->party, pInfo);
}
/////////////////////////////////////////////////////////////////////////////////////
// using easiest path to create unit - via CParty
void CWorldServer::AddPersonage(int nID, const char *pszPrototype, const char *pszName,
                                const CPlot3D &ptPlace)
{
    CParty fake;
    fake.CreatePersonage("", pszPrototype, pszName);
    CPersonages pers;
    pers.Add(&fake.people[0]);
    pers[0]->nID = nID;
    DeployPersonages(0, pers, ptPlace, QNULL);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::GainExperience(CPlayer *pPlayer, float fpExp)
{
    if (fpExp == 0)
        return;
    if (!pPlayer)
        return;
    CParty &people = pPlayer->party;
    CArray<int, int> dudes;
    CUnitPtrHashIterator unitIter;
    for (CUnit *pUnit = unitIter.GetFirst(&pPlayer->unitsUnderControl); pUnit;
         pUnit = unitIter.GetNext())
    {
        if (!pUnit->IsDead() && pUnit->GetTypeID() == MOTID_HUMAN)
            dudes.Add(pUnit->GetID());
    }
    ASSERT(dudes.GetSize() > 0);
    if (dudes.GetSize() < 1)
        return;
    if(auto* mod=ModRuntime::Current())fpExp=mod->ChargeExperience(fpExp,dudes.GetSize());
    else fpExp /= dudes.GetSize();
    for (int i = 0; i < dudes.GetSize(); i++)
    {
        CUnit *pUnit = GetUnit(dudes[i]);
        ((CUnitServer *)pUnit->GetUnitSpecific())->GainExperience(fpExp);
        for (int k = 0; k < people.people.GetSize(); k++)
        {
            if (people.people[k].nID == dudes[i])
                people.people[k].stats.GainExperience(fpExp);
        }
    }
    ((CPlayerServer *)pPlayer->GetPlayerSpecific())->GainExperience(fpExp);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::SetExits(CAArray<SZoneExit, SZoneExit &> &newExits)
{
    exits.SetSize(0);
    for (int i = 0; i < newExits.GetSize(); i++)
    {
        if (newExits[i].szZone != "")
        {
            if (newExits[i].nZoneEntrance < 0 && newExits[i].szZone != "none")
                ReportError("Wrong entrance number in zone exits data");
            exits.Add(newExits[i]);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// количество наемников в игре и их родные зоны
const int N_MAX_MERC_NUMBER = 10;
static const char *pszLocalMercZone[N_MAX_MERC_NUMBER] = {
    "bz1g", "bz1g",
#ifdef __DEMO__
    "bz1g",
#else
    "bz2g",
#endif
    "bz6g",

    "bz8k", // 5
    "bz8k", "bz14h", "bz14h", "bz9k",
    "bz14h" // 10
};
/////////////////////////////////////////////////////////////////////////////////////
int CWorldServer::GetMercsNumber(int nPlayer)
{
    CPlayer *pPlayer = GetPlayer(nPlayer);
    if (pPlayer)
    {
        int nMercs = 0;
        for (int i = 0; i < pPlayer->party.people.GetSize(); i++)
        {
            CUnit *pUnit = GetUnit(pPlayer->party.people[i].nID);
            if (pUnit && !pUnit->IsDead())
                nMercs++;
        }
        return nMercs - 1;
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWorldServer::HasMerc(CPlayer *pPlayer, int nID)
{
    if (pPlayer)
    {
        for (int i = 0; i < pPlayer->party.people.GetSize(); i++)
        {
            if (pPlayer->party.people[i].nID == nID)
                return true;
        }
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::RecalcMercBriefings()
{
    const auto* mod = ModRuntime::Current();
    const int mercLimit = mod && mod->SpellAddon().Feature12() >= 1 ? 5 : 2;
    CPlayerIterator playerIter;
    CPlayer *pPlayer = playerIter.GetFirst((CPlayerManager *)GetPlayerManager());
    for (; pPlayer; pPlayer = playerIter.GetNext())
    {
        CPlayerServer *pPS = (CPlayerServer *)pPlayer->GetPlayerSpecific();
        CGSVariableSet &vars = pPlayer->party.vars;

        int nTotalMercs = GetMercsNumber(pPlayer->GetPlayerNumber());
        for (int nMerc = 1; nMerc <= N_MAX_MERC_NUMBER; nMerc++)
        {
            CString szMercName;
            szMercName.Format("merc%i", nMerc);
            int nID = GetPersonageIDByName(szMercName);
            bool bIsNativeZone = szCurrentZoneName.CompareNoCase(pszLocalMercZone[nMerc - 1]) == 0;
            CString szHire, szRehire, szRefuse, szHi, szFire, szGoodbye, szAvail, szChillOut;
            szHi.Format("b.%s.n8_%i", (const char *)szMercName, nMerc);
            szHire.Format("b.%s.n1_%i", (const char *)szMercName, nMerc);
            szFire.Format("b.%s.n2_%i", (const char *)szMercName, nMerc);
            szRehire.Format("b.%s.n3_%i", (const char *)szMercName, nMerc);
            szRefuse.Format("b.%s.n9_%i", (const char *)szMercName, nMerc);
            szGoodbye.Format("b.%s.n10_%i", (const char *)szMercName, nMerc);
            szChillOut.Format("b.%s.n11_%i", (const char *)szMercName, nMerc);
            szAvail.Format("amerc_%i", nMerc);
            //
            pPS->SetVar(szChillOut, 0);
            if (HasMerc(pPlayer, nID))
            {
                // has got this merc in party
                if (vars.GetVar(szGoodbye))
                {
                    pPS->SetVar(szHi, 0);
                    pPS->SetVar(szHire, 0);
                    pPS->SetVar(szFire, 0);
                    pPS->SetVar(szRehire, 0);
                    pPS->SetVar(szRefuse, 0);
                }
                else
                {
                    pPS->SetVar(szHi, 0);
                    pPS->SetVar(szHire, 0);
                    pPS->SetVar(szRehire, 0);
                    pPS->SetVar(szRefuse, 0);
                    if (bIsNativeZone)
                        pPS->SetVar(szFire, 1);
                    else
                        pPS->SetVar(szFire, 0);
                }
            }
            else
            {
                // has not this merc in party
                if (vars.GetVar(szGoodbye))
                {
                    pPS->SetVar(szHi, 0);
                    pPS->SetVar(szHire, 0);
                    pPS->SetVar(szFire, 0);
                    pPS->SetVar(szRehire, 0);
                    pPS->SetVar(szRefuse, 0);
                    pPS->SetVar(szGoodbye, 0);
                    pPS->SetVar(szAvail, 0);
                }
                else
                {
                    float fpAvail = vars.GetVar(szAvail);
                    if (fpAvail != 0)
                    {
                        if (vars.GetVar(szHi) != 1)
                        {
                            // merc could be taken
                            if (nTotalMercs < mercLimit)
                            {
                                if (fpAvail == 1)
                                {
                                    pPS->SetVar(szHire, 1);
                                    pPS->SetVar(szRehire, 0);
                                    pPS->SetVar(szHi, 0);
                                    pPS->SetVar(szFire, 0);
                                    pPS->SetVar(szRefuse, 0);
                                }
                                else
                                {
                                    pPS->SetVar(szHire, 0);
                                    pPS->SetVar(szRehire, 1);
                                    pPS->SetVar(szHi, 0);
                                    pPS->SetVar(szFire, 0);
                                    pPS->SetVar(szRefuse, 0);
                                }
                            }
                            else
                            {
                                pPS->SetVar(szHire, 0);
                                pPS->SetVar(szRehire, 0);
                                pPS->SetVar(szHi, 0);
                                pPS->SetVar(szFire, 0);
                                pPS->SetVar(szRefuse, 1);
                            }
                        }
                        else
                        {
                            // hi is set
                            pPS->SetVar(szHire, 0);
                            pPS->SetVar(szFire, 0);
                            pPS->SetVar(szRehire, 0);
                            pPS->SetVar(szRefuse, 0);
                            pPS->SetVar(szGoodbye, 0);
                            // pPS->SetVar( szAvail, 0 );
                        }
                    }
                    else
                    {
                        // merc is not available
                        if (vars.GetVar(szHi) != 1)
                        {
                            pPS->SetVar(szChillOut, 1);
                        }
                    }
                }
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::OnBriefingCompleteMercs(CPlayer *pPlayer, const char *pszBriefing)
{
    CPlayerServer *pPS = (CPlayerServer *)pPlayer->GetPlayerSpecific();
    CGSVariableSet &vars = pPlayer->party.vars;
    for (int nMerc = 1; nMerc <= N_MAX_MERC_NUMBER; nMerc++)
    {
        CString szMercName, szHire, szRehire, szRefuse, szHi, szFire, szGoodbye, szAvail;
        szMercName.Format("merc%i", nMerc);
        int nID = GetPersonageIDByName(szMercName);
        szHi.Format("b.%s.n8_%i", (const char *)szMercName, nMerc);
        szHire.Format("b.%s.n1_%i", (const char *)szMercName, nMerc);
        szFire.Format("b.%s.n2_%i", (const char *)szMercName, nMerc);
        szRehire.Format("b.%s.n3_%i", (const char *)szMercName, nMerc);
        szRefuse.Format("b.%s.n9_%i", (const char *)szMercName, nMerc);
        szGoodbye.Format("b.%s.n10_%i", (const char *)szMercName, nMerc);
        szAvail.Format("amerc_%i", nMerc);
        CUnit *pMerc = GetUnit(nID);
        if (szHi.CompareNoCase(pszBriefing) == 0)
        {
            pPS->SetVar(szHi, 0);
            // pPS->SetVar( szAvail, 1 );
        }
        if (szGoodbye.CompareNoCase(pszBriefing) == 0)
        {
            pPlayer->party.RemovePersonage(nID);
            RemoveMapObject(nID);
        }
        if (szFire.CompareNoCase(pszBriefing) == 0)
        {
            pPlayer->party.RemovePersonage(nID);
        }
        if (szHire.CompareNoCase(pszBriefing) == 0 || szRehire.CompareNoCase(pszBriefing) == 0)
        {
            pPS->SetVar(szAvail, 2);
            if (pMerc)
                pPlayer->party.CreatePersonage(szMercName, pMerc->GetPrototype()->szName, "");
            else
            {
                ASSERT(0);
                pPlayer->party.CreatePersonage(szMercName, "Human Hero", "");
            }
        }
    }
    RecalcMercBriefings();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::BriefingComplete(int nPlayer, const char *pszBriefing)
{

    CStringArray szParts;
    const SQDBBriefing *pBrief = 0;
    NStrProc::SplitString(pszBriefing, szParts, '.');
    if (szParts.GetSize() == 3)
        pBrief = theQuestDatabase.briefings.GetEntry(szParts[2]);
    CPlayer *pPlayer = GetPlayer(nPlayer);
    //
    if (pPlayer)
    {
        OnBriefingCompleteMercs(pPlayer, pszBriefing);
        //
        if (pBrief)
        {
            // вручаем бонуса за брифинг
            if (pBrief->fExp != 0)
                GainExperience(pPlayer, pBrief->fExp);
            pPlayer->party.nMoney += (int)pBrief->fMoney;

            // add some items
            CList<CItem *, CItem *> newItems;
            std::vector<CString>::const_iterator i;
            for (i = pBrief->szItems.begin(); i != pBrief->szItems.end(); ++i)
            {
                CItem *pItem = CreateItem(*i);
                if (pItem)
                    newItems.AddTail(pItem);
            }
            if (newItems.GetCount())
                pPlayer->AddItemsToHoover(newItems);
            // remove some items
            for (i = pBrief->szRemoveItems.begin(); i != pBrief->szRemoveItems.end(); ++i)
            {
                CItem *pItem = CreateItem(*i);
                if (pItem)
                {
                    pItem->AddRef();
                    if (pItem->WhoAmI() == HPC_ITEM_QUEST)
                        pPlayer->RemoveQuestItem(((CItemQuest *)pItem)->GetScriptID());
                    pItem->Release();
                }
            }
            // give/remove quests
            for (i = pBrief->szGiveQuests.begin(); i != pBrief->szGiveQuests.end(); ++i)
                ((CPlayerServer *)pPlayer->GetPlayerSpecific())->SetVarMax(*i, 1);
            for (i = pBrief->szCompleteQuests.begin(); i != pBrief->szCompleteQuests.end(); ++i)
                ((CPlayerServer *)pPlayer->GetPlayerSpecific())->SetVar(*i, 2);
            // zones
            for (i = pBrief->szAvailableZones.begin(); i != pBrief->szAvailableZones.end(); ++i)
                ((CPlayerServer *)pPlayer->GetPlayerSpecific())->SetVarMax(*i, 1);
            for (i = pBrief->szCompleteZones.begin(); i != pBrief->szCompleteZones.end(); ++i)
                ((CPlayerServer *)pPlayer->GetPlayerSpecific())->SetVar(*i, 2);
        }
    }
    //
    CString szRunBrf;
    szRunBrf.Format("WorldScript( GSSetVar( %i,\"%s\",2 ) #OnBriefingComplete( %i, \"%s\" ) )",
                    nPlayer, pszBriefing, nPlayer, pszBriefing);
    script.LoadText(szRunBrf);
    if (curInterface == IFACE_BASE) // CRAP
    {
        script.Segment();
        script.Segment();
        script.Segment();
        script.Segment();
        script.Segment();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::AddToDynamic(CMapObject *pObj)
{
    dynamicObjs.SetAt(pObj->GetID(), pObj);
    if (pObj->WhoAmI() != HPC_UNIT)
        dynamicMapObjs.SetAt(pObj->GetID(), pObj);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::RemoveFromDynamic(CMapObject *pObj)
{
    dynamicObjs.RemoveAt(pObj->GetID());
    if (dynamicMapObjs.Seek(pObj->GetID()))
        dynamicMapObjs.RemoveAt(pObj->GetID());
    else
        ASSERT(pObj->WhoAmI() == HPC_UNIT);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWorldServer::CheckAddObject(CMapObject *pObj)
{
    bool bOk;
    if (pObj->IsDynamic())
        bOk = bLoadDynamic;
    else
        bOk = bLoadStatic;
    if (!bOk)
    {
        pObj->AddRef();
        pObj->Release();
    }
    return bOk;
}
/////////////////////////////////////////////////////////////////////////////////////
int CWorldServer::AddMapObject(CMEBasicObject *pMEObject)
{
    int nResID;
    assert(bMapLoaded == TRUE);

    switch (pMEObject->m_eType)
    {
    case MEOT_TORCH: {
        CMETorch *pMapEditObj = (CMETorch *)pMEObject;
        CMapObject *pObj = (CMapObject *)((CMapObjectManager *)GetMapObjectManager())
                               ->CreateData(pMapEditObj->p_tidTypeID, pMapEditObj->p_nID);
        if (!CheckAddObject(pObj))
            return -1;
        if (pObj->GetID() == -1)
            return -1;
        if (pMapEditObj->p_nID == -1)
            pMapEditObj->p_nID = pObj->GetID();
        // campfires inflict damage
        if (fabs(pMapEditObj->p_ptLink.z + pMEObject->p_ptPosition.z) < 0.7f)
        {
            CPlot3D ptPlace = pMapEditObj->p_ptLink + pMEObject->p_ptPosition;
            CSmartPtr<CSpell> pSpell = GetSpellManager()->CreateSpell("campfire");
            CastSpell(pSpell, 0, ptPlace, 0, ptPlace);
        }

        pMapEditObj->p_lpMapObject = pObj;
        CTorchObjectSpecific *pTorchSpec = ((CTorchObject *)pObj)->GetTorchSpecific();

        CMapObjectServer *pSpec = (CMapObjectServer *)pObj->GetMapObjectSpecific();
        pObj->SetMaskName(pMapEditObj->p_szTemplate);
        pSpec->szPrimTexture = pMapEditObj->p_szPrimTexture;
        pSpec->szSecTexture = pMapEditObj->p_szSecTexture;
        pSpec->szNames.Copy(pMapEditObj->p_aBodyParts);

        pObj->AddRef();
        if (pMapEditObj->m_eType == MEOT_UNIT)
            pObj->LoadDefaultClientStats(((CMEUnit *)pMapEditObj)->p_szUnitPrototype);
        else
            pObj->LoadDefaultClientStats(pMapEditObj->p_szParentTemplate);
        pObj->UpdateInit();

        pTorchSpec->SetParticles(PTID_CAMPFIRE, pMapEditObj->p_ptLink, pMapEditObj->p_fStrenght);
        D3DCOLORVALUE color = { 0.8f, 0.8f, 0.8f, 1.0f };
        pTorchSpec->SetLight(pMapEditObj->p_ptLink, pMapEditObj->p_fStrenght * 10, color);
        pTorchSpec->SetSFX(pMapEditObj->p_szSound);

        pObj->SetComplection(pMapEditObj->p_Complection);
        pObj->SetBaseComplection(pMapEditObj->p_Complection);
        pObj->RestoreComplection();
        pObj->SetCP(pMapEditObj->p_ptPosition);
        pObj->SetOrientation(pMapEditObj->p_qRotation);
        // pObj->SetUpdateMask( CMapObject::VLD_MASK_COMPLECTION );
        pObj->SetUpdateMask(CMapObject::VLD_MASK_POSROT);
        // pObj->SetUpdateMask( CMapObject::VLD_MASK_PHASE );
        pObj->SetUpdateMask(CMapObject::VLD_MASK_TORCH);
        if (pMapEditObj->p_isShadow == FALSE)
            pObj->SetFlag(OBJ_NOSHADOW);

        ((CMapObjectManager *)GetMapObjectManager())->SetupDefaultStats(pObj);

        if (pObj->WhoAmI() == HPC_UNIT)
            ((CUnitServer *)((CUnit *)pObj)->GetUnitSpecific())->InitializeUnit();

        pObj->LoadFigureBoundingVolumes(pMapEditObj->p_szTemplate);
        assert(mapObjects.Seek(pObj->GetID()) == NULL);
        mapObjects.SetAt(pObj->GetID(), pObj);
        AddToDynamic(pObj);
        AddActiveObject(pObj);
        aiMap.AddObject(pObj);
        nResID = pObj->GetID();
        if (pObj->IsUpdated())
            pObj->UpdateFrame();
    }
    break;

    case MEOT_MAGIC_TRAP: {
        CMEObject *pMapEditObj = (CMEObject *)pMEObject;
        CMapObject *pObj = (CMapObject *)((CMapObjectManager *)GetMapObjectManager())
                               ->CreateData(pMapEditObj->p_tidTypeID, pMapEditObj->p_nID);
        if (!CheckAddObject(pObj))
            return -1;
        if (pObj->GetID() == -1)
            return -1;
        if (pMapEditObj->p_nID == -1)
            pMapEditObj->p_nID = pObj->GetID();

        pMapEditObj->p_lpMapObject = pObj;

        CMapObjectServer *pSpec = (CMapObjectServer *)pObj->GetMapObjectSpecific();

        pObj->AddRef();
        pObj->UpdateInit();

        pObj->SetUpdateMask(CMapObject::VLD_MASK_POSROT);
        pObj->SetFlag(OBJ_NOSHADOW);

        mapObjects.SetAt(pObj->GetID(), pObj);
        AddToDynamic(pObj);
        AddActiveObject(pObj);
        nResID = pObj->GetID();
        if (pObj->IsUpdated())
            pObj->UpdateFrame();
        static_cast<CMagicTrapObject *>(pObj)->SetParams(((CMEMagicTrap *)pMapEditObj)->p_Specific);
    }
    break;

    case MEOT_LEVER:
    case MEOT_UNIT:
    case MEOT_OBJECT:
    case MEOT_TRAP: {
        CMEObject *pMapEditObj = (CMEObject *)pMEObject;
        CMapObject *pObj = (CMapObject *)((CMapObjectManager *)GetMapObjectManager())
                               ->CreateData(pMapEditObj->p_tidTypeID, pMapEditObj->p_nID);
        if (pMapEditObj->p_isUseInScript)
            pObj->SetFlag(MAPOBJ_DYNAMIC);
        if (pMapEditObj->p_szQuestInfo != "")
            pObj->SetFlag(MAPOBJ_DYNAMIC);
        if (!CheckAddObject(pObj))
            return -1;
        if (pObj->GetID() == -1)
            return -1;
        if (pMapEditObj->p_nID == -1)
            pMapEditObj->p_nID = pObj->GetID();

        pMapEditObj->p_lpMapObject = pObj;

        CMapObjectServer *pSpec = (CMapObjectServer *)pObj->GetMapObjectSpecific();
        pObj->SetMaskName(pMapEditObj->p_szTemplate);
        pObj->SetAttachedQuest(pMapEditObj->p_szQuestInfo);
        pSpec->szPrimTexture = pMapEditObj->p_szPrimTexture;
        pSpec->szSecTexture = pMapEditObj->p_szSecTexture;
        pSpec->szNames.Copy(pMapEditObj->p_aBodyParts);

        pObj->AddRef();
        if (pMapEditObj->m_eType == MEOT_UNIT)
            pObj->LoadDefaultClientStats(((CMEUnit *)pMapEditObj)->p_szUnitPrototype);
        else
            pObj->LoadDefaultClientStats(pMapEditObj->p_szParentTemplate);
        pObj->UpdateInit();
        pObj->RecalcRPGStats();

        switch (pMEObject->m_eType)
        {
        case MEOT_UNIT: {
            CMEUnit *pMapEditUnit = (CMEUnit *)pMapEditObj;
            static_cast<CUnit *>(pObj)->SetOwner(NULL);
            // CAIUnitLogic &logic = ((CUnitServer*)((CUnit*)pObj)->GetUnitSpecific())->logic;
            ((CUnit *)pObj)->SetPlayerNum(pMapEditObj->p_nPlayer);
            ((CUnit *)pObj)->SetEnemyMask(fof.GetEnemyMask(pMapEditObj->p_nPlayer));
            ((CMapObjectManager *)GetMapObjectManager())
                ->SetupStats(pObj, pMapEditUnit->p_szUnitPrototype);
            ((CMapObjectManager *)GetMapObjectManager())
                ->WearAll(pObj, pMapEditUnit->p_szUnitPrototype);

            CStringArray &qItems = pMapEditUnit->p_UnitSpecific.aQuickItems;
            for (int i = 0; i < qItems.GetSize(); i++)
            {
                CItem *pItem = GetItemManager()->CreateItem(qItems[i]);
                ((CUnit *)pObj)->AddQItem((CItemQuick *)pItem);
            }
            pMapEditUnit->p_UnitSpecific.Import(static_cast<CUnit *>(pObj));
            // setup animation speed coeffs from race model
            {
                DWORD dwRaceModel = theUnitDatabase.prototypes[pMapEditUnit->p_szUnitPrototype]
                                        ->dwRaceIndex;
                const SUDBRaceModel *pRace = theUnitDatabase.races[dwRaceModel];
                ((CUnit *)pObj)->SetRaceAnimationCoeffs(pRace->fAnimationCoeffs);
            }
            //
            pObj->SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);
            pObj->SetUpdateMask(CMapObject::VLD_MASK_POSE);
            pObj->SetUpdateMask(CMapObject::VLD_MASK_DIPLOMACY);
            /*
            // transmit created items to all clients
            CGameBaseList items, spells;
            ((CUnit*)pObj)->GetAllItemsList( items, spells );
            SendAddedItemsToPlayers( items, spells );
            */
        }
        break;
        case MEOT_LEVER:
            static_cast<CLeverObject *>(pObj)->SetParams(((CMELever *)pMapEditObj)->p_Specific);
            pObj->SetUpdateMask(CMapObject::VLD_MASK_LEVER);
            break;
        default: {
            ((CMapObjectManager *)GetMapObjectManager())->SetupDefaultStats(pObj);
        }
        break;
        }

        pObj->SetComplection(pMapEditObj->p_Complection);
        pObj->SetBaseComplection(pMapEditObj->p_Complection);
        pObj->RestoreComplection();
        pObj->SetCP(pMapEditObj->p_ptPosition);
        pObj->SetOrientation(pMapEditObj->p_qRotation);
        // pObj->SetUpdateMask( CMapObject::VLD_MASK_COMPLECTION );
        pObj->SetUpdateMask(CMapObject::VLD_MASK_POSROT);
        pObj->SetUpdateMask(CMapObject::VLD_MASK_POSE);
        pObj->SetUpdateMask(CMapObject::VLD_MASK_FFLAGS);
        pObj->SetUpdateMask(CMapObject::VLD_MASK_ANIM);
        if (pMapEditObj->p_isShadow == FALSE)
            pObj->SetFlag(OBJ_NOSHADOW);
        pObj->LoadFigureBoundingVolumes(pMapEditObj->p_szTemplate);
        pObj->RecalcAIClass();

        if (pObj->WhoAmI() == HPC_UNIT)
            ((CUnitServer *)((CUnit *)pObj)->GetUnitSpecific())->InitializeUnit();
        ASSERT(mapObjects.Seek(pObj->GetID()) == NULL);
        mapObjects.SetAt(pObj->GetID(), pObj);
        AddToDynamic(pObj);
        aiMap.AddObject(pObj);
        nResID = pObj->GetID();
        if (pObj->IsUpdated())
            pObj->UpdateFrame();
        switch (pMEObject->m_eType)
        {
        case MEOT_LEVER:
        case MEOT_TRAP:
            AddActiveObject(pObj);
            break;
        }
        if (/*pObj->WhoAmI() == HPC_UNIT &&*/ pObj->GetAttachedQuest()[0])
            questMapObjs.SetAt(pObj->GetID(), pObj);
    }
    break;
    case MEOT_SOUND:
        if (!bLoadDynamic)
            return -1;
        {
            CMESound *pMapEditSound = (CMESound *)pMEObject;
            if (pMapEditSound->p_isAmbient)
            {
                CEffectAmbientSound *pEff = (CEffectAmbientSound *)((CEffectManager *)
                                                                        pEffectManager)
                                                ->CreateData(ETypeIDs(HPC_EFFECTAMBIENTSOUND),
                                                             pMapEditSound->p_nID);
                pEff->szSoundNames.Copy(pMapEditSound->p_aResNames);
                pEff->SetCP(pMapEditSound->p_ptPosition);
                pEff->fpMinRadius = float(pMapEditSound->p_nR);
                pEff->fpMaxRadius = float(pMapEditSound->p_nR2);
                pEff->nMsToPlayMin = pMapEditSound->p_nMin;
                pEff->nMsToPlayMax = pMapEditSound->p_nMax;
                if (pEff->nMsToPlayMin >= pEff->nMsToPlayMax)
                {
                    TRACE("Invalid ambient sound interval: map object %d, min=%d, max=%d; using defaults 1500-3000 ms\n",
                          pMapEditSound->p_nID, pEff->nMsToPlayMin, pEff->nMsToPlayMax);
                    pEff->nMsToPlayMin = 1500;
                    pEff->nMsToPlayMax = 3000;
                }
                assert(pEff->nMsToPlayMin < pEff->nMsToPlayMax);
                AddEffect(pEff);
                nResID = pEff->GetID();
            }
            else
            {
                CEffectSound *pEff = (CEffectSound *)((CEffectManager *)pEffectManager)
                                         ->CreateData(ETypeIDs(HPC_EFFECTSOUND),
                                                      pMapEditSound->p_nID);
                if (pMapEditSound->p_aResNames.GetSize() < 1)
                    ErrorDialog::ShowA(GetActiveWindow(), "Adding sound without wave name", "EIRebuild", MB_OK | MB_ICONERROR);
                else
                    strcpy(pEff->szSoundName, pMapEditSound->p_aResNames[0]);
                pEff->SetCP(pMapEditSound->p_ptPosition);
                pEff->fpMinRadius = float(pMapEditSound->p_nR);
                pEff->fpMaxRadius = float(pMapEditSound->p_nR2);
                AddEffect(pEff);
                nResID = pEff->GetID();
            }
        }
        break;
    case MEOT_LIGHT:
        if (!bLoadDynamic)
            return -1;
        {
            CMELight *pMapEditLight = (CMELight *)pMEObject;
            CEffectPointLight *pEff = (CEffectPointLight *)((CEffectManager *)pEffectManager)
                                          ->CreateData(ETypeIDs(HPC_EFFECTPOINTLIGHT),
                                                       pMapEditLight->p_nID);
            pEff->SetCP(pMapEditLight->p_ptPosition);
            pEff->fpRange = pMapEditLight->p_fRange;
            pEff->color = pMapEditLight->p_ptColor;
            AddEffect(pEff);
            nResID = pEff->GetID();
        }
        break;
    case MEOT_PARTICLE:
        if (!bLoadDynamic)
            return -1;
        {
            CMEParticle *pMapEditParticle = (CMEParticle *)pMEObject;
            CEffectParticle *pEff = (CEffectParticle *)((CEffectManager *)pEffectManager)
                                        ->CreateData(ETypeIDs(HPC_EFFECTPARTICLE),
                                                     pMapEditParticle->p_nID);
            pEff->SetCP(pMapEditParticle->p_ptPosition);
            pEff->typeID = pMapEditParticle->p_idType;
            pEff->fpScale = pMapEditParticle->p_fEfectStrength;
            AddEffect(pEff);
            nResID = pEff->GetID();
        }
        break;
    }
    return nResID;
}

/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::RemoveMapObject(int nID)
{
    CMapObject **ppObj = mapObjects.Seek(nID);
    if (ppObj == NULL || ppObj[0] == NULL)
        return;
    CMapObject *pMODel = ppObj[0];
    if (pMODel->WhoAmI() != HPC_MAGICTRAP)
        aiMap.RemoveObject(*ppObj);
    RemoveObjectFromActive(nID);
    mapObjects.RemoveAt(nID);
    if (questMapObjs.Seek(nID))
        questMapObjs.RemoveAt(nID);
    if (staticObjs.Seek(nID))
    {
        ASSERT(!dynamicObjs.Seek(nID));
        ASSERT(!dynamicMapObjs.Seek(nID));
        staticObjs.RemoveAt(nID);
    }
    else
    {
        ASSERT(dynamicObjs.Seek(nID));
        dynamicObjs.RemoveAt(nID);
        if (dynamicMapObjs.Seek(nID))
            dynamicMapObjs.RemoveAt(nID);
    }
    pMODel->FreeReferences();

    bool bIsUnit = pMODel->WhoAmI() == HPC_UNIT;
    CUnit *pUnit = (CUnit *)pMODel;
    CMapObjPtrHashIterator mapobjIter;
    pMODel->AddRef(); // add fake refcount for unit not to die too early
    if (bIsUnit)
        pUnit->SetOwner(NULL);
    pMODel->Invalidate();
    script.CheckObjectRefs();
    scriptTemp.CheckObjectRefs();
    // remove all references on pUnit from all classes
    for (CMapObject *pMO = mapobjIter.GetFirst(&mapObjects); pMO;)
    {
        pMO->RemoveReferencesOn(pMODel);
        pMO = mapobjIter.GetNext();
    }
    // remove spells related to this unit
    for (POSITION pos = spells.GetHeadPosition(); pos;)
    {
        POSITION oldPos = pos;
        CEffectSpell *pSpell = spells.GetNext(pos);
        if (!pSpell->RemoveReferencesOn(pMODel))
            spells.RemoveAt(oldPos);
    }
    // remove map object from every client
    CPlayerIterator playerIter;
    CPlayer *pPlayer = playerIter.GetFirst((CPlayerManager *)GetPlayerManager());
    for (; pPlayer; pPlayer = playerIter.GetNext())
        ((CPlayerServer *)pPlayer->GetPlayerSpecific())->RemoveMapObjectRefs(pMODel);

    pMODel->Release(); // kill him
    ASSERT(pMODel->GetRefCount() == 1);
    pMODel->Release();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::RemoveAllUnits()
{
    CMapObject *pMO;
    CMapObjPtrHashIterator mapobjIter;
    pMO = mapobjIter.GetFirst(&mapObjects);
    while (!mapobjIter.IsReachEnd())
    {
        int nID = pMO->GetID();
        BOOL bUnit = pMO->WhoAmI() == HPC_UNIT;
        pMO = mapobjIter.GetNext();
        if (bUnit)
            RemoveMapObject(nID);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
CMapObject *CWorldServer::GetMapObject(int nID)
{
    if (pMapObjectManager->IsOmittedSaveObject(nID)) return NULL;
    CMapObject *pMO;
    if (!mapObjects.Lookup(nID, pMO))
        return NULL;
    return pMO;
}
/////////////////////////////////////////////////////////////////////////////////////
// inform every player that somebody died if that player saw corpse
void CWorldServer::InformAboutUnitDeath(CUnit *pUnit)
{
    GameJournal::CombatBoundary("death", pUnit->GetID());
    GameJournal::Event("COMBAT", "death", "id=%d prototype=%s player=%d", pUnit->GetID(), GameJournal::Quote(pUnit->GetPrototype() ? (const char*)pUnit->GetPrototype()->szName : "unknown").c_str(), pUnit->GetPlayerNum());
    CPlayer *pPl;
    CPlayerIterator playerIter;
    pPl = playerIter.GetFirst((CPlayerManager *)GetPlayerManager());
    for (; pPl; pPl = playerIter.GetNext())
    {
        CPlayerServer *pPS = (CPlayerServer *)pPl->GetPlayerSpecific();
        if (pPS->currentVisible.HasMapObject(pUnit))
            pPS->SendDeathEvent(pUnit);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWorldServer::CheckLeversPlacement(int &nWrongID)
{
    CMapObject *pMO;
    CMapObjPtrHashIterator mapobjIter;
    pMO = mapobjIter.GetFirst(&mapObjects);
    for (; !mapobjIter.IsReachEnd(); pMO = mapobjIter.GetNext())
    {
        CArray<CPlot3D, CPlot3D &> points;
        pMO->GetUsePoints(points, 0.5f);
        if (points.GetSize())
        {
            bool bCanPut = false;
            for (int i = 0; i < points.GetSize(); i++)
            {
                if (aiMap.CanPutUnit(3, points[i]))
                {
                    bCanPut = true;
                    break;
                }
            }
            if (!bCanPut)
            {
                nWrongID = pMO->GetID();
                return false;
            }
        }
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::LootAllCorpses()
{
    CMapObjPtrHashIterator mapobjIter;
    CMapObject *pMO = mapobjIter.GetFirst(&mapObjects);
    CPlayer *pPlayer = GetPlayer(0);
    ASSERT(pPlayer);
    if (!pPlayer)
        return;
    std::vector<int> ids;
    for (; !mapobjIter.IsReachEnd(); pMO = mapobjIter.GetNext())
    {
        if (pMO->WhoAmI() == HPC_UNIT && pMO->IsDead())
        {
            theWorldServer.HooverUnit((CUnit *)pMO, pPlayer);
            ids.push_back(pMO->GetID());
        }
    }
    // actually remove
    for (auto pos = ids.begin(); pos != ids.end(); ++pos)
        theWorldServer.RemoveMapObject(*pos);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::SetWaterLevel(int nObjectID, float fpFinalH, int nTime)
{
    waterLevels.SetWaterLevel(nObjectID, fpFinalH, nTime);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::InvokeAlarm(int nAlarm, const CPlot3D &ptOrigin, bool bForceAlarm)
{
    CMapObjPtrHashIterator mapobjIter;
    CUnit *pUnit;

#ifdef _DEBUG
    CString szTemp;
    szTemp.Format("Alarm %i invoked at ( %g, %g )\n", nAlarm, ptOrigin.x, ptOrigin.y);
    OutputDebugString(szTemp);
#endif

    ASSERT(nAlarm >= 0 && nAlarm < N_NUMBER_OF_ALARM);
    if (nAlarm < 0 || nAlarm >= N_NUMBER_OF_ALARM)
        return;
    if (alarms[nAlarm].bActivated && !bForceAlarm)
        return;
    if (nAlarm == 0 && bForceAlarm)
        memset(alarms, 0, sizeof(alarms));

    alarms[nAlarm].bActivated = true;
    alarms[nAlarm].nTime = GetSegmentTimeCounter();
    alarms[nAlarm].fpX = ptOrigin.x;
    alarms[nAlarm].fpY = ptOrigin.y;

    // Inform all units that alarm happened
    pUnit = (CUnit *)mapobjIter.GetFirst(&mapObjects);
    while (pUnit)
    {
        if (pUnit->WhoAmI() == HPC_UNIT && pUnit->GetOwner() == 0)
        {
            CAIUnitLogic &logic = ((CUnitServer *)pUnit->GetUnitSpecific())->logic;
            logic.AlarmWasInvoked(nAlarm);
            if (logic.IsAlwaysActive())
                AddActiveObject(pUnit);
        }
        pUnit = (CUnit *)mapobjIter.GetNext();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWorldServer::CanBeginFieldCamp()
{
    const auto availability = [](bool value, const char* reason, int enemy = -1, float distance = 0.f) {
        static const char* lastReason = nullptr;
        static DWORD lastTick = 0;
        static unsigned reports = 0;
        const DWORD now = GetTickCount();
        if (reason != lastReason && reports < 64 && (!reports || now - lastTick >= 2000)) {
            lastReason = reason; lastTick = now; ++reports;
            GameJournal::Event("CAMP", "availability", "available=%d reason=%s enemy=%d distance=%.3f",
                int(value), reason, enemy, distance);
        }
        return value;
    };
    CModPackage *mod = ModRuntime::Current();
    if (!mod || !mod->CombatSetting("JMVGoCamp")) return availability(false, "feature-off");
    if (!IsMapLoaded() || curInterface != IFACE_MISSION) return availability(false, "mission-unavailable");
    if (fieldCamp->armed || fieldCamp->inCamp || fieldCamp->returnHeroId >= 0) return availability(false, "transition-active");
    CPlayer *player = const_cast<CWorldServer *>(this)->GetPlayer(0);
    CPlayerServer *playerServer = player ? dynamic_cast<CPlayerServer *>(player->GetPlayerSpecific()) : nullptr;
    if (!player || !playerServer) return availability(false, "player-unavailable");
    if (playerServer->dangerState == WDS_COMBAT) return availability(false, "combat");
    if (player->party.people.GetSize() <= 0 || player->party.people.GetSize() > 8) return availability(false, "party-size");
    try { if (mod->GoCampZoneExcluded((LPCSTR)player->party.szCurrentZone)) return availability(false, "zone-excluded"); }
    catch (...) { GameJournal::AllocationFailureInCatch("WorldServer"); return availability(false, "zone-policy-error"); }
    std::vector<CUnit *> partyUnits;
    for (int i = 0; i < player->party.people.GetSize(); ++i) {
        CUnit *unit = GetUnit(player->party.people[i].nID);
        if (!unit || unit->IsDead() || !unit->GetUnitSpecific() || unit->GetUnitSpecific()->WhoAmI() != HPC_UNITSERVERSPECIFIC)
            return availability(false, "party-unit-unavailable");
        partyUnits.push_back(unit);
    }
    float radius = 25.0f;
    try { radius = mod->GoCampEnemyRadius(); }
    catch (...) { GameJournal::AllocationFailureInCatch("WorldServer"); return availability(false, "radius-policy-error"); }
    const float radiusSquared = radius * radius;
    CMapObjPtrHashIterator iterator;
    for (CMapObject *object = iterator.GetFirst(const_cast<CMapObjPtrHash *>(&dynamicObjs)); !iterator.IsReachEnd(); object = iterator.GetNext()) {
        // Match the addon: detached objects no longer belong to the live world.
        if (!object || object->IsInvalid() || object->IsUnreferenced() || object->WhoAmI() != HPC_UNIT) continue;
        CUnit *enemy = static_cast<CUnit *>(object);
        if (!ModOutOfCombatRegeneration::IsLivingHostile(enemy, player->GetPlayerNumber())) continue;
        for (CUnit *member : partyUnits) {
            const float distanceSquared = distanceXY2(enemy->GetCP(), member->GetCP());
            if (distanceSquared <= radiusSquared)
                return availability(false, "enemy-nearby", enemy->GetID(), sqrtf((std::max)(0.f, distanceSquared)));
        }
    }
    return availability(true, "ready");
}

bool CWorldServer::BeginFieldCamp()
{
    if (!CanBeginFieldCamp()) return false;
    ModCampReferences::Reset(fieldCamp->pending);
    fieldCamp->snapshots.clear();
    fieldCamp->armed = true;
    return true;
}
int CWorldServer::FieldCampReturnHeroID() const { return fieldCamp->returnHeroId; }
bool CWorldServer::IsFieldCampPending() const { return fieldCamp->armed || fieldCamp->inCamp || fieldCamp->returnHeroId >= 0; }
bool CWorldServer::IsInFieldCamp() const { return fieldCamp->inCamp; }
void CWorldServer::CompleteFieldCampReturn() { fieldCamp->returnHeroId = -1; theCamera.DiscardFieldCampCamera(); }
bool CWorldServer::BeginFieldCampCommerceSuppression()
{
    GameJournal::Source journalSource("engine.field_camp_suppression");
    CModPackage *mod = ModRuntime::Current();
    CPlayerClient *player = IWorldClient.GetPlayer();
    if (!fieldCamp->inCamp || !mod || !mod->CombatSetting("JMVGoCamp") || !player || fieldCamp->commerceVars) return false;
    static const char *names[6] = { "constr.current", "i.noconstr", "i.offspellconstr", "i.onspellconstr", "i.offitemconstr", "i.onitemconstr" };
    static const float values[6] = { 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f };
    CGSVariableSet &vars = IWorldClient.GetParty().vars;
    fieldCamp->commerceVars = &vars;
    for (int i = 0; i < 6; ++i) {
        auto found = vars.find(names[i]);
        fieldCamp->commerceValues[i].name = names[i];
        fieldCamp->commerceValues[i].existed = found != vars.end();
        fieldCamp->commerceValues[i].value = found != vars.end() ? found->second : 0.0f;
        vars.SetVar(names[i], values[i]);
    }
    return true;
}
void CWorldServer::EndFieldCampCommerceSuppression(bool active)
{
    GameJournal::Source journalSource("engine.field_camp_restore");
    if (!active || !fieldCamp->commerceVars) return;
    for (int i = 0; i < 6; ++i) {
        const SFieldCampVarSnapshot &snapshot = fieldCamp->commerceValues[i];
        // SetVar(0) deletes the key; preserve zero-valued keys loaded from saves.
        if (snapshot.existed) {
            GameJournal::Variable(fieldCamp->commerceVars, snapshot.name, fieldCamp->commerceVars->find(snapshot.name) != fieldCamp->commerceVars->end(), fieldCamp->commerceVars->GetVar(snapshot.name), snapshot.value, false);
            (*fieldCamp->commerceVars)[snapshot.name] = snapshot.value;
        }
        else fieldCamp->commerceVars->DelVar(snapshot.name);
    }
    fieldCamp->commerceVars = nullptr;
}
CPlayer *CWorldServer::GetPlayer(int nPlayer)
{
    CManagerIterator iter;
    CPlayer *lpPlayer = (CPlayer *)iter.GetFirst(GetPlayerManager());
    while (lpPlayer)
    {
        if (lpPlayer->GetPlayerNumber() == nPlayer)
            return lpPlayer;
        lpPlayer = (CPlayer *)iter.GetNext();
    }
    return NULL;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// ----------------------------------- Z coordinate calculation
// -------------------------------------------
float CWorldServer::GetZValue(float x, float y) const
{
    int nX = Float2AITile(x), nY = Float2AITile(y);
    if (!aiMap.TileInBounds(nX, nY))
        return 0;
    return aiMap.GetTileHeight(nX, nY);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::RequestCombatPause(bool bPause)
{
    bCombatPause = bPause;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
/*void CWorldServer::SendAddedItemsToPlayer( CPlayer *pPlayer, CGameBaseList &items, CGameBaseList
&spells )
{
        //CPlayer* pPl;
        CMemorySave packet;
        //CPlayerIterator playerIter;

        if ( !bTransferItemCreates )
                return;
        // form packet with new items
        StoreList( items, packet );
        StoreList( spells, packet );
        //
        CPlayerServer* pPlServ = ( CPlayerServer* )pPlayer->GetPlayerSpecific();
        pPlServ->SendAddItems( packet );
        // transfer new items to all players
        // player is considered to be in game if he has not bZoneHasChanged flag and he is connected
        //pPl = playerIter.GetFirst( ( CPlayerManager* )GetPlayerManager() );
        //while ( pPl )
        //{
                //CPlayerServer* pPlServ = ( CPlayerServer* )pPl->GetPlayerSpecific();
                //if ( pPl->IsConnected() && !pPl->CheckFlag( DW_PCHANGED_ZONE ) &&
pPlServ->NeedItemsUpdate() )
                        //pPlServ->SendAddItems( packet );
                //pPl = playerIter.GetNext();
        //}
}*/
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CWorldServer::HooverUnit(CUnit *pVictim, CPlayer *pWho)
{
    int nMoney;
    POSITION pos;
    CList<CItem *, CItem *> hooveredItems;
    CList<CItem *, CItem *> newItems;
    CList<CSpell *, CSpell *> newSpells;
    ASSERT(pWho);
    if (!pWho)
        return false;
    if (pVictim->IsHero() && !pVictim->IsDead())
        return false;
    if (pVictim->CheckFlag(UNIT_LOOT_LOCKED))
        return false;

    if (pHoover->Hoover(this, pVictim, hooveredItems, newItems, newSpells, nMoney))
    {
        pHoover->HooverCarriedLoot(this, pVictim, hooveredItems, newItems, nMoney);
        // в случае мп квестовые итема идут лидеру

        // here some items can be added to inventory of some units
        // update hoover of pWho
        pWho->party.nMoney += nMoney;
        pWho->AddItemsToHoover(hooveredItems);
        for (pos = hooveredItems.GetHeadPosition(); pos;)
            hooveredItems.GetNext(pos)->Release();
        return hooveredItems.GetCount() > 0 || nMoney > 0;
    }
    return false;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CWorldServer::HooverUnitAddon(CUnit *victim,CPlayer *player,CUnitServer *unitServer,bool loot)
{
 if(!victim||!player||!unitServer||(victim->IsHero()&&!victim->IsDead())) return false;

 int money=0; POSITION pos; CList<CItem*,CItem*> items,newItems; CList<CSpell*,CSpell*> newSpells;
 if(loot&&!victim->CheckFlag(UNIT_ALREADY_HOOVER))
 {
  pHoover->HooverAddon(this,victim,items,newItems,newSpells,money,unitServer->GetRPGStats()->GetManualSkillLevel(RPGMSKL_SCIENCE,victim),unitServer->GetRPGStats()->psDexterity.fpEffectiveValue,false);

  if(!victim->CheckFlag(UNIT_ALREADY_MHOOVER)){newItems.RemoveAll();pHoover->HooverAddon(this,victim,items,newItems,newSpells,money,unitServer->GetRPGStats()->GetManualSkillLevel(RPGMSKL_SCIENCE,victim),unitServer->GetRPGStats()->psDexterity.fpEffectiveValue,true);player->party.nMoney+=money;victim->SetFlag(UNIT_ALREADY_MHOOVER);}pHoover->HooverCarriedLoot(this,victim,items,newItems,money);
  if(items.GetCount())player->AddItemsToHoover(items);for(pos=items.GetHeadPosition();pos;)items.GetNext(pos)->Release();victim->SetFlag(UNIT_ALREADY_HOOVER);return items.GetCount()>0||money>0;
 }
 if(!loot&&!victim->CheckFlag(UNIT_ALREADY_MHOOVER))
 {pHoover->HooverAddon(this,victim,items,newItems,newSpells,money,unitServer->GetRPGStats()->GetManualSkillLevel(RPGMSKL_SCIENCE,victim),unitServer->GetRPGStats()->psDexterity.fpEffectiveValue,true);player->party.nMoney+=money;victim->SetFlag(UNIT_ALREADY_MHOOVER);if(items.GetCount())player->AddItemsToHoover(items);for(pos=items.GetHeadPosition();pos;)items.GetNext(pos)->Release();return money>0;}
 return false;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////
CItem *CWorldServer::CreateItem(const char *pszItem)
{
    CItem *pItem = static_cast<CItem *>(theWorldServer.GetItemManager()->CreateItem(pszItem));
    if (!bTransferItemCreates)
        return pItem;
    return pItem;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CItemSpell *CWorldServer::CreateItemSpell(const char *pszItem)
{
    CItemSpell *pItem = theWorldServer.GetItemManager()->CreateItemSpell(pszItem,
                                                                         GetSpellManager());
    if (!bTransferItemCreates)
        return pItem;
    return pItem;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CItem *CWorldServer::DuplicateItem(CItem *pItem)
{
    CItem *lpItem = GetItemManager()->CreateItem(pItem->GetTypeID(), pItem->GetShape(),
                                                 pItem->GetMaterial(), -1);
    pItem->Duplicate(lpItem, GetSpellManager());
    lpItem->SetMP(pItem->GetMP());
    if (pItem->GetMP() < pItem->GetMaxMP())
        AddActiveItem(lpItem);
    if (!bTransferItemCreates)
        return lpItem;
    return lpItem;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CItem *CWorldServer::DuplicateItemWithSrc(CItem *pItem)
{
    CItem *pRes = DuplicateItem(pItem);
    CItemSrcInfo *pInfo = dbgnew CItemSrcInfo;
    pInfo->nSrcItemID = pItem->GetID();
    pRes->AttachUserData(pInfo);
    return pRes;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::UpdateItemSrc(CItem *pItem, CItem *pSrc)
{
    CItemSrcInfo *pInfo = dbgnew CItemSrcInfo;
    pInfo->nSrcItemID = pSrc->GetID();
    pItem->AttachUserData(pInfo);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::AddActiveItem(CItem *pItem)
{
    if (!pItem)
        return;
    if (!activeItems.Find(pItem))
        activeItems.AddTail(pItem);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// reduce item`s count and remove if everything count is zero
void CWorldServer::ReduceQItemCount(CUnit *pUnit, CItem *pItem)
{
    CPlayer *pPlayer = pUnit->GetOwner();
    if (pPlayer && pItem->GetUserData())
    {
        CItemSrcInfo *pInfo = (CItemSrcInfo *)pItem->GetUserData();
        CPersonage *pPers = pPlayer->party.GetPersonage(pUnit->GetID());
        ASSERT(pPers);
        if (pPers)
        {
            CItem *pSrc = (CItem *)pPlayer->party.GetItemManager()->Find(pInfo->nSrcItemID);
            if (pSrc)
            {
                pSrc->SetCount(pSrc->GetCount() - 1);
                // if ( pItem->GetCount() == 0 )
                // pPers->RemoveQuick( (CItemQuick*)pSrc );
            }
        }
    }
    pItem->SetCount(pItem->GetCount() - 1);
    /*
    if ( pItem->GetCount() == 0 )
    {
            int nIdx;
            for ( int i = 0; i < QI_MAX_ITEMS; i++ )
            {
                    if ( pUnit->GetQItem(i) == pItem )
                            pUnit->RemoveQItem( i );
            }
    }*/
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// for screen wait
void CWorldServer::SetupScreenWait(const char *pszZoneName)
{
    if (ModCliTool::GeneratingGraph()) return;
    SScenarioZone *pZone = 0;
    CString szAllodName;

    if (pszZoneName)
        pZone = theMap.GetZoneByName(pszZoneName);
    if (pZone)
        szAllodName = pZone->szAllodName;
    // CRAP{
    /*	CStringArray szTmpArray;
            if ( pszMobName )
                    NStrProc::SplitString( pszMobName, szTmpArray, '.' );
            szTmpArray[0].MakeLower();
            SScenarioAllod *pA = theMap.GetAllodByMobName( szTmpArray[0] );
            CString szAllodName;
            if ( pA )
                    szAllodName = pA->szName;
            SScenarioAllod *pA = theMap.GetAllodByName( pszZoneName );*/

    if (szAllodName == "gipat")
        C2DScreenWait::pszProgressName = "progres2.bik";
    else if (szAllodName == "gipat2")
        C2DScreenWait::pszProgressName = "progres2.bik";
    else if (szAllodName == "jigran")
        C2DScreenWait::pszProgressName = "progres5.bik";
    else if (szAllodName == "ingos")
        C2DScreenWait::pszProgressName = "progres3.bik";
    else if (szAllodName == "suslanger")
        C2DScreenWait::pszProgressName = "progres4.bik";
    else
        C2DScreenWait::pszProgressName = "progres.bik";

    if (pszZoneName)
    {
        if (stricmp(pszZoneName, "gz1g") == 0)
            C2DScreenWait::pszProgressName = "progres1.bik";
        if (stricmp(pszZoneName, "bz7g") == 0)
            C2DScreenWait::pszProgressName = "progres1.bik";
    }
    // The installed addon asks main.lua after the original allod/zone choice.
    // Keep the returned filename alive while the loading screen consumes it.
    static std::string modProgressName;
    if (auto *mod = ModRuntime::Current())
    {
        try
        {
            std::string overrideName = mod->LoadVideo(pszZoneName);
            if (!overrideName.empty())
            {
                modProgressName = overrideName;
                C2DScreenWait::pszProgressName = modProgressName.c_str();
            }
        }
        catch (const std::exception &error)
        { GameJournal::AllocationFailureInCatch("WorldServer");
            TRACE("GetLoadVideo failed: %s\n", error.what());
        }
    }
    // CRAP}
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// ------------------------------------ Load map
// -----------------------------------------------------------
void CWorldServer::CreateInitialUpdate()
{
    POSITION pos;
    CMapObjPtrHashIterator mapobjIter;
    CList<CMapObject *, CMapObject *> staticList;
    //
    theWSInitialUpdate.Clear();
    ASSERT(staticObjs.GetCount() == 0);
    CMapObject *pMO = mapobjIter.GetFirst(&mapObjects);
    while (pMO)
    {
        if (dynamicObjs.Seek(pMO->GetID()))
        {
            if (!pMO->IsDynamic())
            {
                staticObjs.SetAt(pMO->GetID(), pMO);
                staticList.AddTail(pMO);
            }
        }
        else
            ASSERT(0);
        pMO = mapobjIter.GetNext();
    }
    //
    CPlayer *pPlayer;
    CPlayerServer *pPlServ;
    pPlayer = ((CPlayerManager *)pPlayerManager)->CreatePlayer();
    pPlServ = (CPlayerServer *)pPlayer->GetPlayerSpecific();
    CGameBasePtrArray toAdd;
    for (pos = staticList.GetHeadPosition(); pos;)
    {
        pMO = staticList.GetNext(pos);
        RemoveFromDynamic(pMO);
        toAdd.Add(pMO);
    }
    pPlServ->AddMapObjects(toAdd);
    pPlServ->CopyUpdatePkt(theWSInitialUpdate);
    ((CPlayerManager *)GetPlayerManager())->RemovePlayer(pPlayer);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// начать функционирование нового сервера
BOOL CWorldServer::NewZone(LPCSTR pszMapName, LPCSTR pszMobName, LPCSTR pszZoneName)
{
    CorePerf::Operation coreOperation("world.new_zone", pszZoneName);
    GameJournal::Event("ZONE", "resources", "mpr=%s mob=%s", GameJournal::Quote(pszMapName).c_str(), GameJournal::Quote(pszMobName).c_str());
    GameJournal::Operation journal("ZONE", "new_zone", pszZoneName);
    SetSky(GMS_NORMAL);
    SetupScreenWait(pszZoneName);
    ASSERT(GetPlayerManager()->GetNumElements() == 0);
    C2DScreenWait::Step(0);

    // if Map file does not exist
    if (pszMapName != NULL &&
        access(ModRuntime::MapPath(pszMapName).c_str(), 0) == -1 &&
        access(ModRuntime::MapPath(GetMobPath() + pszMapName).c_str(), 0) == -1)
        return FALSE;

    // if Mob file does not exist
    if (pszMobName != NULL &&
        access(ModRuntime::MapPath(pszMobName).c_str(), 0) == -1 &&
        access(ModRuntime::MapPath(GetMobPath() + pszMobName).c_str(), 0) == -1)
        return FALSE;

    coreOperation.Stage("ai_graph");
    CMemorySave graphChunk;
    if (!ModCliTool::GeneratingGraph() && !LoadAIGraph(pszMapName, pszMobName, graphChunk))
        return ReportError("CWorldServer::LoadMap.\n Can't load AIGraph.\n");

    coreOperation.Stage("static_mob");
    szLoadedMobs.RemoveAll();
    StartZoneLoading();
    memset(alarms, 0, sizeof(alarms));
    if (!LoadMainMob(pszMapName, pszMobName, graphChunk, false))
        return ReportError("CWorldServer::NewZone()\n Can't load map.\n");
    C2DScreenWait::Step(6);
    coreOperation.Stage("finalize");
    FinishZoneLoading();
    C2DScreenWait::Step(7);
    if (pszZoneName)
        szCurrentZoneName = pszZoneName;
    bCanRunScript = FALSE;
#ifdef _DEBUG
    int nWrongID;
    if (!CheckLeversPlacement(nWrongID))
    {
        CString str;
        str.Format("Lever %i is not placed correctly", nWrongID);
        ErrorDialog::ShowA(GetActiveWindow(), str, "EIRebuild", MB_OK | MB_ICONERROR);
    }
#endif

    JMV_NpcConsumablesLoaded(WorldLoadOrigin::ZoneEntry);
    { coreOperation.Result(true); journal.Result(true); return TRUE; }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::HealAllUnits()
{
    CMapObjPtrHashIterator mapobjIter;
    for (CMapObject *pMO = mapobjIter.GetFirst(&dynamicObjs); pMO; pMO = mapobjIter.GetNext())
    {
        if (pMO->WhoAmI() == HPC_UNIT && !pMO->IsDead())
        {
            CUnitServer *pUS = (CUnitServer *)((CUnit *)pMO)->GetUnitSpecific();
            pUS->GetRPGStats()->HealAll();
            pUS->RecalcRPGStats();
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// this simply erases everything dynamic from server and sets it to single player mode
void CWorldServer::RemoveEverythingDynamic()
{
    GameJournal::MemorySnapshot("zone_unload");
    for (;;)
    {
        CPlayerIterator playerIter;
        CPlayer *pPlayer = playerIter.GetFirst((CPlayerManager *)GetPlayerManager());
        if (!pPlayer)
            break;
        pPlayer->Disconnect();
        ((CPlayerManager *)GetPlayerManager())->RemovePlayer(pPlayer);
    }
    bMapLoaded = FALSE;
    szLoadedMobs.RemoveAll();
    aiMap.SetGraphRebuild(false);
    script.Clear();
    scriptTemp.Clear();
    fof.Clear();
    RemoveDynamicObjects();
    spells.RemoveAll();
    RemoveEffects();
    serverMode = SERV_MODE_NONE;
    curInterface = IFACE_NONE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::StartZoneLoading()
{
    aiMap.StartDelayedConvert();
    bTransferItemCreates = false;
    aiMap.SetGraphRebuild(false);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::FinishZoneLoading()
{
    bTransferItemCreates = true;
    aiMap.FinishDelayedConvert();
    aiMap.SetGraphRebuild(true);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldServer::AddSecondaryMob(LPCSTR pszMobName)
{
    // check if one is already loaded
    const auto* mod = ModRuntime::Current();
    if (!(mod && mod->SpellAddon().Feature14() >= 1) ||
        !pszMobName || strncmp(pszMobName, "Auto", 4) == 0)
    {
        for (int i = 0; i < szLoadedMobs.GetSize(); i++)
        {
            if (szLoadedMobs[i] == pszMobName)
                return TRUE;
        }
    }
    //
    OwnedRender::E1MobSource ownedSource(pszMobName);
    C2DScreenWait::Ignore(true);
    if (pszMobName && !LoadMapObjects(pszMobName, false))
    {
        C2DScreenWait::Ignore(false);
        bMapLoaded = FALSE;
        return ReportError("CWorldServer::LoadMap.\n Can't load .mob file \"%s\".\n", pszMobName);
    }
    C2DScreenWait::Ignore(false);
    szLoadedMobs.Add(pszMobName);
    ownedSource.Succeeded(true);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::CheckZone(const char *pszMapName, const char *pszMobName)
{
    if (szZoneFileName == pszMapName && szMobFileName == pszMobName)
        return;
    NewZone(pszMapName, pszMobName);
    RemoveEverythingDynamic();
}
/////////////////////////////////////////////////////////////////////////////////////
CString CWorldServer::GetFullMobName(const char *pszFileName)
{
    CString szName;
    if (pszFileName[1] == ':')
        szName = pszFileName;
    else
        szName = GetMobPath() + pszFileName;
    return szName;
}
/////////////////////////////////////////////////////////////////////////////////////
// aiMap.StartDelayConvert should be called before this function (use StartZoneLoading() )
// script and fof are cleared if requested static only
BOOL CWorldServer::LoadMainMob(const char *pszMprName, const char *pszMobName,
                               CMemorySave &graphChunk, bool bStaticOnly)
{
    OwnedRender::E1MobSource ownedSource(pszMprName, pszMobName);
    // load ai map if changed
    if (szZoneFileName.CompareNoCase(pszMprName) != 0)
    {
        aiMap.Clear();
        aiMap.StartDelayedConvert();
        if (!aiMap.Load(GetFullMobName(pszMprName)))
            return FALSE;
        szZoneFileName = pszMprName;
        szMobFileName = "";
    }

    // unconditionally load aiGraph
    graphChunk.Seek(0);
    if (graphChunk.GetSize() > 0 && !aiMap.LoadGraph(graphChunk))
        return FALSE;

    // prepare for map objects loading
    if (szMobFileName.CompareNoCase(pszMobName) == 0)
    {
        script.Clear();
        scriptTemp.Clear();
        fof.Clear();
        RemoveDynamicObjects();
        spells.RemoveAll();
        RemoveEffects();
        ASSERT(pItemManager->GetNumElements() == 0);
        ASSERT(pSpellManager->GetNumElements() == 0);
        bLoadStatic = false;
        bLoadDynamic = !bStaticOnly;
        aiMap.ClearLastDangerAddTime();
    }
    else
    {
        RemoveEverything();
        bLoadStatic = true;
        bLoadDynamic = !bStaticOnly;
    }
    SetChangedFlags(DW_PCHANGED_EVERYTHING);

    bMapLoaded = TRUE;
    SetWeather(EW_CLEAR, 0); // CRAP :)

    if (bLoadStatic || bLoadDynamic)
    {
        szMobFileName = pszMobName;
        if (!LoadMapObjects(pszMobName, !bLoadDynamic))
            return FALSE;
        if (!bLoadDynamic)
        {
            script.Clear();
            fof.Clear();
        }
        if (bLoadStatic)
            CreateInitialUpdate();
    }

    if (bStaticOnly)
    {
        RemoveDynamicObjects();
        RemoveEffects();
    }
    bLoadDynamic = true;
    bLoadStatic = true;
    if(auto*mod=ModRuntime::Current())mod->ResetRuntimeState();
    ownedSource.Succeeded(true);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldServer::LoadMapObjects(LPCSTR pszMobName, bool bSkipScript)
{
    CGameMOBFile reader;
    BOOL bRv = reader.Open(GetFullMobName(pszMobName),
                           CFile::modeRead | CFile::shareDenyWrite | CFile::typeBinary);
    if (!bRv)
        return ReportError("CWorldServer::LoadMapObjects()\nUnable to read file \"%s\"\n",
                           pszMobName);
    else
    {
        OwnedRender::E1MobSource::Parsed(pszMobName);
        bRv = reader.Load(this, bSkipScript);
        reader.Close();
        if (!bRv)
            return ReportError("CWorldServer::LoadMapObjects\nUnable to read file \"%s\"\n",
                               pszMobName);
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// extract graph chunk from mob or from .grp file
BOOL CWorldServer::LoadAIGraph(LPCSTR pszMapName, LPCSTR pszMobName, CMemorySave &graphChunk)
{
    CFileSave mobFile;
    BOOL bGraphLoaded = FALSE;
    if (pszMobName) // crap, graph & save should be resolved
    {
        mobFile.OpenRead(GetFullMobName(pszMobName));
        if (mobFile.GetChunk(ID_AIGRAPH, graphChunk))
            bGraphLoaded = TRUE;
        mobFile.CloseFile();
    }
    // CRAP, loading graph from .grp file, this should be avoided as possible
    if (!bGraphLoaded)
    {
        CString szGraphName = GetFullMobName(pszMapName);
        int nInd = szGraphName.ReverseFind('.');
        if (nInd > 0)
            szGraphName = szGraphName.Left(nInd);

        CFileSave fsave;
        if (fsave.OpenRead(szGraphName + ".grp"))
        {
            bGraphLoaded = TRUE;
            graphChunk.CopyFrom(fsave);
            fsave.CloseFile();
        }
    }
    return bGraphLoaded;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::LoadScriptFromMOB(const char *pszMobName)
{
    CFileSave mob;
    CString szScript;
    CMemorySave chunk, chunk1;
    CSimpleCryptographer cr;
    mob.OpenRead(GetFullMobName(pszMobName));
    mob.GetChunk(ID_OBJECTDBFILE, chunk);
    if (chunk.GetChunk(ID_SS_TEXT, chunk1))
    {
        cr.SetCodetData(chunk1.GetBuffer(), chunk1.GetSize());
        cr.GetString(szScript);
    }
    else if (chunk.GetChunk(ID_SS_TEXT_OLD, chunk1))
    {
        chunk1.Seek(chunk1.GetSize());
        chunk1.Write((char)0);
        szScript = chunk1.GetBuffer();
    }
    else
        ASSERT("Can't find script in mob." && 0);
    script.Clear();
    script.LoadText(szScript);
}
/////////////////////////////////////////////////////////////////////////////////////
// some functions to store/load dynamic objects
/////////////////////////////////////////////////////////////////////////////////////
// уничтожить все объекты, €вл€ющиес€ динамическими
void CWorldServer::RemoveDynamicObjects()
{
    POSITION pos;
    CMapObjPtrHashIterator mapobjIter;
    CList<CMapObject *, CMapObject *> toDel;
    for (CMapObject *pMO = mapobjIter.GetFirst(&dynamicObjs); pMO; pMO = mapobjIter.GetNext())
        toDel.AddTail(pMO);
    for (pos = toDel.GetHeadPosition(); pos;)
        RemoveMapObject(toDel.GetNext(pos)->GetID());

    spells.RemoveAll();
    activeItems.RemoveAll();
    waterLevels.Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
// main LOAD/STORE functions
/////////////////////////////////////////////////////////////////////////////////////
// when this function is called server should be connected to local client
// and should not be in local or multiplayer mode
BOOL CWorldServer::LoadSave(CSave &file, WorldLoadOrigin origin, const char *pszTerrain)
{
    return SaveSchema::Slot::LoadWorld(file);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServer::StoreSave(CSave &file)
{
    SaveSchema::Slot::StoreWorld(file,*this);
}
/////////////////////////////////////////////////////////////////////////////////////

#include "ModAnimationIdSave.inl"
