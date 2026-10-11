#pragma once
#include "WorldLoadOrigin.h"
/*==========================================================================================================

        Server class header

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/
#include "World.h"
#include "FileRegistry.h"
#include "PacketIDs.h"
#include "script.h"
#include "FofSystem.h"
#include "aiSensesConst.h"
#include "ServerConsts.h"
#include "ZoneExit.h"
#include "GeneralUnitLogic.h"
#include "aiMap.h"
#include "EffectSpell.h"
/////////////////////////////////////////////////////////////////////////////////////
class CPlayer;
class CEffect;
class CMEBasicObject;
class CHoover;
class CParty;
class CEffectSpell;
/////////////////////////////////////////////////////////////////////////////////////
// для UserData item`ов
// структура для хранения информации об идентификаторе итема, с которого этот итем
// скопирован
class CItemSrcInfo : public CObjectBase
{
    OBJECT_BASIC_METHODS(CItemSrcInfo)
public:
    int nSrcItemID; // ID в itemmanager`e партии
};
/////////////////////////////////////////////////////////////////////////////////////
// -------------------------------- World server
// -----------------------------------------------------------
class CWorldServer : public CWorldBase
{
private:
    float fpNightPower;  // strength of night for current moment
    float fpVisionBonus; // multiplier of vision due to weather reasons
    SAlarmInfo alarms[N_NUMBER_OF_ALARM];

    EGMZoneWeather weatherChange;
    EGMZoneSky skyType;
    EWeather curWeather;
    float fpWeatherComeTime;
    CAArray<SZoneExit, SZoneExit &> exits; // exits from current zone
    bool bTransferItemCreates, bCreateUnits;
    CMapObjPtrHash activeSet;
    bool bSendCenterCamera;
    bool bLoadDynamic;
    bool bLoadStatic;
    CString szCurrentZoneName; // gz1g or something
    CStringArray szLoadedMobs;
    EServerMode serverMode;
    int nLastLightningTime;

public:
    CHoover *pHoover; // just stuff hoover :)

    CFileRegistry messages; // server messages data

    BOOL bMapLoaded;
    BOOL bCanRunScript; // kind of crap
    CPlot3D ptDefaultCamera;
    CAIMap aiMap;
    CScript script, scriptTemp;
    CFofTracker fof;         // свой-чужой
    EInterface curInterface; // интерфейс необходимый клиентам для работы с этим сервером

    CString szMobFileName;     // name of the loaded MOB-file
    CString szZoneFileName;    // name of zone
    CMapObjPtrHash mapObjects; // all map objects on server
    CMapObjPtrHash staticObjs,
        dynamicObjs; // mapObjects that are loaded with start (static) and changing (dynamic)
    CMapObjPtrHash dynamicMapObjs; // dynamic objects that are not units
    CMapObjPtrHash questMapObjs;   // set of objects having quest on them
    CEffectPtrHash effects;        // all effects on server
    CPacket theWSInitialUpdate; // packet with updates on static objects (for use with local client)
    CList<CPtr<CEffectSpell>, CEffectSpell *> spells; // active spells on server
    typedef CSmartPtr<CItem> CItemPtr;
    CList<CItemPtr, CItem *> activeItems; // all active items for whom segment is called

    struct SCorpseItems
    {
        int nID;
        int nCanLootBy;
        DWORD dwMoney;
        CAArray<CItemPtr, CItem *> items; // all items from hoover of dead unit in LMP
        //
        void Store(CSave &file);
        void Load(CSave &file);
    };
    CList<SCorpseItems> LMPCorpses;

private:
    void Clear();
    BOOL UpdatePlayer(CPlayer *pPl);
    BOOL SendNewZoneToPlayer(CPlayer *pPl);
    void GatherUnitStatistics(CMemorySave &packet);
    // void SendAddedItemsToPlayer( CPlayer *pPlayer, CGameBaseList &items, CGameBaseList &spells );
    void RecalcNightPower();
    BOOL LoadAIGraph(LPCSTR pszMapName, LPCSTR pszMobName, CMemorySave &graphChunk);
    BOOL LoadMapObjects(LPCSTR pszMobName, bool bSkipScript);
    BOOL LoadMainMob(const char *pszMprName, const char *pszMobName, CMemorySave &graphChunk,
                     bool bStaticOnly);
    void StartZoneLoading();
    void FinishZoneLoading();
    void SetChangedFlags(DWORD dwFlags);
    void KillPlayer(CPlayer *pPlayer);
    void NetDriverSegment(CLowLevelNetDriver *pDriver);
    void CastSingleSpell(CSpell *pSpell, CUnit *pWho, CPlot3D ptFrom, CMapObject *pTarget,
                         const CPlot3D &ptTarget, bool bQuiet);
    void OnBriefingCompleteMercs(CPlayer *pPlayer, const char *pszBriefing);
    bool HasMerc(CPlayer *pPlayer, int nID);

    void RemoveEverything(); // do you really need to call this ??
    void RemoveEffects();
    // low level functions working with deploying/removing process
    void SelectQuestItems(CSimplyArray<CItem *, CItem *> &wagon,
                          CList<CItem *, CItem *> &questItems);
    void DeployPersonages(CPlayer *pPlayer, CPersonages &people, const CPlot3D &ptPlace,
                          const CQuaternion rot, SDeployPlayerInfo *pInfo = NULL);
    void DeployHooverItems(CPlayer *pPlayer, CList<CItem *, CItem *> &items);
    void RemovePersonages(CPersonages &people, CWorld *pParty, SDeployPlayerInfo *pInfo);
    void AddItemsFromHoover(CPlayer *pPlayer, CParty *pWagon);
    void SetupScreenWait(const char *pszZoneName);
    void DeployPlayerItems(CPlayer *pPlayer);
    void RemovePlayerItems(CPlayer *pPlayer);
    // separates objects on dynamic and static ones, fill initial update packet
    void CreateInitialUpdate();
    void StoreDynamic(CSave &file);
    void StoreDynamicData(CSave &file);
    std::map<int, int> omittedDynamicObjects; // ID -> saved type, per LoadDynamic
    void LoadDynamic(CSave &file);
    void LoadDynamicData(CSave &file);
    void RemoveDynamicObjects();
    // active objects handling
    void AddActiveObject(CMapObject *pObject);
    void ActiveObjectsSegment();
    void RemoveObjectFromActive(int nID);
    // dynamic objects handling
    void AddToDynamic(CMapObject *pObj);
    void RemoveFromDynamic(CMapObject *pObj);

public:
    CWorldServer();
    virtual ~CWorldServer();
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    // client/server determinition
    virtual BOOL IsServer() { return TRUE; }
    EServerMode GetServerMode() { return serverMode; }
    // Load map
    BOOL NewZone(LPCSTR pszMapName, LPCSTR pszMobName, LPCSTR pszZoneName = 0);
    // all secondary mobs should be added after NewZone()
    BOOL AddSecondaryMob(LPCSTR pszMobName);
    void CheckZone(const char *pszMapName,
                   const char *pszMobName); // for precaching only (on client)
    void RemoveEverythingDynamic();         // prepare server for map change
    void StartServer(EServerMode mode);
    CString GetFullMobName(const char *pszFileName);
    CString GetCurrentZoneName() { return szCurrentZoneName; }
    const CString& GetJournalZoneName() const noexcept { return szCurrentZoneName; }
    void HealAllUnits();
    // this function is used to restart script on zone after loading a saved zone on personages
    // reenter
    void LoadScriptFromMOB(const char *pszMobName);
    // work with partys of players
    //  deploy party of some player into specified position
    void DeployPlayerParty(CPlayer *pPlayer, SDeployPlayerInfo *pInfo = 0, bool bExecScript = true);
    //  this function updates inventory of party from current state on server
    void RemovePlayerParty(CPlayer *pPlayer, SDeployPlayerInfo *pInfo = 0);
    // these functions are used to update party of CPlayer
    void FakeRemoveParty(CPlayer *pPlayer);
    void FakeDeployParty(CPlayer *pPlayer);
    //
    // give experience
    void GainExperience(CPlayer *pPlayer, float fpExp);
    // function to use from script to create units from minimal information
    void AddPersonage(int nID, const char *pszPrototype, const char *pszName,
                      const CPlot3D &ptPlace);
    void SetExits(CAArray<SZoneExit, SZoneExit &> &newExits);
    void BriefingComplete(int nPlayer, const char *pszBriefing);
    void RecalcMercBriefings();
    int GetMercsNumber(int nPlayer);
    void SetWeatherChange(EGMZoneWeather weather);
    void SetSky(EGMZoneSky newSky); // changes after zone reload on client
    //
    BOOL IsMapLoaded() { return bMapLoaded; }
    // load&store
    BOOL LoadSave(CSave &file, WorldLoadOrigin origin, const char *pszTerrain = 0);
    void StoreSave(CSave &file);
    //
    BOOL GetExitDirection(int nExit, CString &szZone, int &nZoneEntrance);
    //
    CPlayer *GetPlayer(int nPlayer);
    // crap function for sending camera position to client
    BOOL CenterCameraOnSomeone(CPlayer *pPl);
    void RequestSendStartCamera(bool bFlag) { bSendCenterCamera = bFlag; }
    //
    void RequestCombatPause(bool bPause);
    bool CanBeginFieldCamp();
    bool BeginFieldCamp();
    int FieldCampReturnHeroID() const;
    void CompleteFieldCampReturn();
    bool BeginFieldCampCommerceSuppression();
    void EndFieldCampCommerceSuppression(bool active);
    bool HooverUnit(CUnit *pVictim, CPlayer *pWho);
    bool HooverUnitAddon(CUnit *pVictim, CPlayer *pWho, CUnitServer *unitServer, bool loot);
    void AddCorpse(int nID, int nLootByID, DWORD dwMoney, CList<CItem *, CItem *> &items);
    bool CanLoot(int nTargetID, int nUnitID);
    void GetCorpses(int nUnitID, CScriptObjectGroup &res);
    CItem *CreateItem(const char *pszItem);
    CItemSpell *CreateItemSpell(const char *pszItem);
    CItem *DuplicateItem(CItem *pItem);
    CItem *DuplicateItemWithSrc(CItem *pItem);
    void UpdateItemSrc(CItem *pItem, CItem *pSrc);
    void AddActiveItem(CItem *pItem);
    void ReduceQItemCount(CUnit *pUnit, CItem *pItem);
    //
    // Some global parameters of world
    float GetNightPower()
    {
        return fpNightPower;
    } // obtain strength of night ie evening = 0.5f, night = 1
    float GetVisionBonus() { return fpVisionBonus; }
    // adds some time for every player on server
    void AddPlayersGlobalTime(float fpShift = 0);
    // Create map object from map edit object
    bool CheckAddObject(CMapObject *pObj);
    int AddMapObject(CMEBasicObject *pMEObject);
    void RemoveMapObject(int nID);
    void RemoveAllUnits();
    virtual CMapObject *GetMapObject(int nID);
    void UnitSayAck(CUnit *pUnit, DWORD dwAck);
    // Read iteration only: group sensing must never insert or wake an NPC.
    CMapObjPtrHash *GetActiveSet() { return &activeSet; }
    void AddObjectToActiveSet(CMapObject *pObject) { AddActiveObject(pObject); }
    //
    void InformAboutUnitDeath(CUnit *pUnit);
    bool CheckLeversPlacement(int &nWrongID);
    void LootAllCorpses();
    void SetWaterLevel(int nObjectID, float fpFinalH, int nTime);
    //
    void AddEffect(CEffect *);
    void RemoveEffect(int nID);
    //
    void InvokeAlarm(int nAlarm, const CPlot3D &ptOrigin, bool bForceAlarm = false);
    void SetWeather(EWeather newWeather, float fpTransTime);
    virtual void SetWorldTime(float fpCurrentTime);
    virtual void SetWorldTimeScale(float fpScale);
    void SetServerWind(const CPlot3D &_ptWindDirection, float fpForce);
    bool IsAlarm(int nAlarm)
    {
        ASSERT(nAlarm >= 0 && nAlarm < N_NUMBER_OF_ALARM);
        return alarms[nAlarm].bActivated;
    }
    const SAlarmInfo &GetAlarm(int nAlarm)
    {
        ASSERT(nAlarm >= 0 && nAlarm < N_NUMBER_OF_ALARM);
        return alarms[nAlarm];
    }
    // 'z' value virtual function
    virtual float GetZValue(float x, float y) const;
    virtual float GetSurfaceValue(float x, float y) const
    {
        return aiMap.GetTileSurface(CPlot3D(x, y, 0));
    }
    virtual float GetBridgeZValue(float x, float y) const { return GetZValue(x, y); }
    virtual float GetBridgeSurfaceZValue(float x, float y) const { return GetSurfaceValue(x, y); }
    // spells
    void CastSpell(CSpell *pSpell, CUnit *pWho, const CPlot3D &ptFrom, CMapObject *pTarget,
                   const CPlot3D &ptTarget, bool bQuiet = false);
    void SetupTrap(CItemQuick *pTrap, CUnit *pWho);
    // segment
    BOOL Segment();
    void WorldSegment(bool bForceScriptRun = false);
    // active objects handling
    void AddActiveSpot(const CPlot3D &ptCenter, float fpRadius);
    // load secondary script
    BOOL LoadScript(LPCSTR pszFileName) { return script.Load(pszFileName); }
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CWorldServer"; }
    virtual EClassIDs WhoAmI() const { return HPC_WORLD_SERVER; }
};
/////////////////////////////////////////////////////////////////////////////////////
extern CWorldServer theWorldServer;
/////////////////////////////////////////////////////////////////////////////////////
