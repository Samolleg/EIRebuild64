#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "GameBase.h"
#include "BaseManager.h"
#include "Plot.h"
#include "GeometryInterfaces.h"
#include "WaterLevels.h"

#include "GraphicsProcessor.h"
#include "TLPipeline.h"
#include "Net.h"
#include "GeneralSettings.h"
/////////////////////////////////////////////////////////////////////////////////////
class CItem;
class CUnit;
class CItemConstructor;
class CSpellConstructor;
class CMapObject;
class CSpellManager;
class CItemManager;
class CPlayerManager;
class CWorldBase;
class CTextureManager;
/////////////////////////////////////////////////////////////////////////////////////
// GUID
const GUID gGameGUID = {
    0xa92d2fe0, 0x8ddf, 0x11d2, { 0x8a, 0xa, 0x0, 0xa0, 0xc9, 0xa0, 0xf9, 0x4e }
}; // {A92D2FE0-8DDF-11d2-8A0A-00A0C9A0F94E}
/////////////////////////////////////////////////////////////////////////////////////
const DWORD WORLD_LAST_BIT = MAKE_LAST_BIT(CGB_LAST_BIT, -1);
/////////////////////////////////////////////////////////////////////////////////////
// some general class incorporating set of managers - worldclient, worldserver, scenariotracker
class CWorld : public CGameBase
{
    // registry key name
    static CString szRegistryKey; // registry key name ( ZB "Nival Interactive\\Allods3" )
    // pathes to work folder, resources, etc.
    static CString szCurrentDir;   // current directory path
    static CString szWorkPath;     // work folder path
    static CString szResourcePath; // resource folder path
    static CString szCDPath;       // CD-ROM path
protected:
    static void SetWorkPath(const char *pszPath) { szWorkPath = pszPath; }
    static void SetResourcePath(const char *pszPath) { szResourcePath = pszPath; }
    static void SetCDPath(const char *pszPath) { szCDPath = pszPath; }
    static void SetCurrentDir(const char *pszPath) { szCurrentDir = pszPath; }

public:
    virtual CWorldBase *GetWorldBase() { return NULL; }
    virtual BOOL Create();
    // functions to obtain managers
    virtual CBaseManager *GetEffectManager()
    {
        ASSERT(0);
        return NULL;
    }
    virtual CBaseManager *GetFigureManager()
    {
        ASSERT(0);
        return NULL;
    }
    virtual CItemConstructor *GetItemConstructor()
    {
        ASSERT(0);
        return NULL;
    }
    virtual CSpellConstructor *GetSpellConstructor()
    {
        ASSERT(0);
        return NULL;
    }
    virtual CItemManager *GetItemManager()
    {
        ASSERT(0);
        return NULL;
    }
    virtual CBaseManager *GetMapObjectManager()
    {
        ASSERT(0);
        return NULL;
    }
    virtual CBaseManager *GetPlayerManager()
    {
        ASSERT(0);
        return NULL;
    }
    virtual CBaseManager *GetSpecificManager()
    {
        ASSERT(0);
        return NULL;
    }
    virtual CSpellManager *GetSpellManager()
    {
        ASSERT(0);
        return NULL;
    }
    virtual IGeometryManager *GetGeometryManager()
    {
        ASSERT(0);
        return 0;
    }
    // some universal functions
    // make a copy of item pItem, result belongs to this[0] world
    virtual CItem *DuplicateItem(CItem *pItem);
    // some initialization information settings
    static void SetRegistryKey(LPCSTR pszRegistryKey) { szRegistryKey = pszRegistryKey; }
    static const CString &GetRegistryKey() { return szRegistryKey; }
    // pathes...
    static const CString &GetCurrentPath() { return szCurrentDir; }
    static const CString &GetWorkPath() { return szWorkPath; }
    static const CString &GetResourcePath() { return szResourcePath; }
    static const CString &GetCDPath() { return szCDPath; }
    static void InitCurrentDir(const char *dataRoot = nullptr);
    CString GetMobPath();
    // CRAP
    virtual LPCSTR GetTexturesPath() const { return NULL; }
    // client/server determinition
    virtual BOOL IsServer() { return FALSE; }
};
/////////////////////////////////////////////////////////////////////////////////////
const DWORD WORLD_BASE_LAST_BIT = MAKE_LAST_BIT(WORLD_LAST_BIT, -1);
/////////////////////////////////////////////////////////////////////////////////////
const float TIME_DAY_START = 6.0f;
const float TIME_DAY_END = 18.0f;
const float TIME_NIGHT_START = 22.0f;
const float TIME_NIGHT_END = 2.0f;
const float TIME_MORNING_LENGTH = 4.0f;
const float TIME_EVENING_LENGTH = 4.0f;
const float TIME_MORNING = 4.0f;
const float TIME_EVENING = 20.0f;
/////////////////////////////////////////////////////////////////////////////////////
class CWorldBase : public CWorld
{
    friend class SaveSchema::Slot;
    float fpTimeModifier, fpTimeScale;

protected:
    // wind
    CPlot3D ptWindDirection;
    float fpWindForce;
    // different data managers...
    IGeometryManagerPtr pGeometryManager; // manager for figures in the new concept
    CBaseManager *pPlayerManager;         // player manager: players
    CBaseManager *pFigureManager;         // figure manager: figures
    CBaseManager
        *pMapObjectManager; // map object manager: units, trees, stones, buildings, stuff objects
    CBaseManager *pSpecificManager; // specific data manager: all specifics for all classes
    CBaseManager *pEffectManager;   // effects manager: sounds, particles, lights
    CBaseManager *pSpellManager;    // spell manager: spell effects, spells and their containers
    CSpellConstructor *pSpellConstructor; // spell constructor
    CBaseManager *pItemManager;           // item manager
    CItemConstructor *pItemConstructor;   // item constructor
    CObj<CLocalNetDriver> pNetDriver;     // handling of network stuff
                                          // time
    int nSegmentTimeCounter;              // segment time counter <= for game time tracking
    int nUpdateCounter; // update counter to update only when objects really changed, used on server
    bool bCombatPause;
    CWaterLevels waterLevels;

    void SetWind(const CPlot3D &windDirection, float windForce);

public:
    virtual CWorldBase *GetWorldBase() { return this; }
    CWorldBase();
    virtual ~CWorldBase();
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    // 'z' value virtual function
    virtual float GetZValue(float x, float y) const = 0;
    virtual float GetSurfaceValue(float x, float y) const = 0;
    virtual float GetBridgeZValue(float x, float y) const = 0;
    virtual float GetBridgeSurfaceZValue(float x, float y) const = 0;
    // time and segment functions
    virtual BOOL Segment();
    int GetSegmentTimeCounter() const { return nSegmentTimeCounter; }
    void SetSegmentTimeCounter(int nTime) { nSegmentTimeCounter = nTime; }
    int GetUpdateCounter() { return nUpdateCounter; }
    float GetWorldTime() const
    {
        // Saved offsets may be negative; all hour consumers require [0,24).
        const float hour = std::fmod(float(nSegmentTimeCounter) * fpTimeScale + fpTimeModifier, 24.0f);
        const float wrapped = hour < 0.0f ? hour + 24.0f : hour;
        return wrapped >= 24.0f ? 0.0f : wrapped;
    }
    float GetTimeInHours(int nSegments) const { return float(nSegments) * fpTimeScale; }
    double GetUnwrappedWorldTime() const
    {
        return double(nSegmentTimeCounter) * fpTimeScale + fpTimeModifier;
    }
    CString GetTimeName() const;
    virtual void SetWorldTime(float fpCurrentTime);
    virtual void SetWorldTimeScale(float fpScale);
    void GetTimeParams(float &fpCurrentTime, float &fpScale);
    bool IsCombatPause() { return bCombatPause; }
    BOOL IsNightTime();
    float GetNightPowerSlow(); // like CWorldServer::GetNightPower()
    // wind
    CPlot3D GetWindDirection() const { return ptWindDirection; }
    float GetWindForce() const { return fpWindForce; }
    void GetWind(CPlot3D &windDirection, float &windForce)
    {
        windDirection = ptWindDirection;
        windForce = fpWindForce;
    }
    // managers access functions
    virtual CBaseManager *GetPlayerManager() { return pPlayerManager; }
    virtual CBaseManager *GetFigureManager() { return pFigureManager; }
    virtual CBaseManager *GetMapObjectManager() { return pMapObjectManager; }
    virtual CBaseManager *GetSpecificManager() { return pSpecificManager; }
    virtual CBaseManager *GetEffectManager() { return pEffectManager; }
    virtual CSpellManager *GetSpellManager() { return (CSpellManager *)pSpellManager; }
    virtual CItemManager *GetItemManager() { return (CItemManager *)pItemManager; }
    virtual CItemConstructor *GetItemConstructor() { return pItemConstructor; }
    virtual CSpellConstructor *GetSpellConstructor() { return pSpellConstructor; }
    virtual IGeometryManager *GetGeometryManager() { return pGeometryManager; }
    // following managers will be overloaded in CWorldClient class
    virtual CBaseManager *GetMaterialManager() { return NULL; }
    virtual CTextureManager *GetTextureManager() { return NULL; }
    virtual CBaseManager *GetLightManager() { return NULL; }
    virtual CBaseManager *GetParticleManager() { return NULL; }
    virtual CBaseManager *GetWindowManager() { return NULL; }
    virtual CBaseManager *GetBitmapManager() { return NULL; }
    virtual CBaseManager *GetInterfaceManager() { return NULL; }
    //
    virtual CMapObject *GetMapObject(int nID);
    virtual void MoveObject(CMapObject *pMO, const CPlot3D &newCP) { ASSERT(0); };
    CUnit *GetUnit(int nID);
    //
    CLocalNetDriver *GetLocalNetDriver() { return pNetDriver; }
};
/////////////////////////////////////////////////////////////////////////////////////
extern void StoreList(const CGameBaseList &objs, CMemorySave &packet);
/////////////////////////////////////////////////////////////////////////////////////
// CString CWorldBase::GetTimeName() const
// returns current time name: morning/day/evening/night
inline CString CWorldBase::GetTimeName() const
{
    float fpTime = GetWorldTime();

    if ((fpTime >= TIME_DAY_START) && (fpTime <= TIME_DAY_END))
        return "day"; // day time ( day_start < time < day_end )
    else if ((fpTime >= TIME_NIGHT_START) || (fpTime <= TIME_NIGHT_END))
        return "night"; // night time (night_start < time, night_end > time)
    else if (fpTime < TIME_DAY_START)
        return "morning"; // morning ( time < day_start but time > night_end )
    else
        return "evening"; // evening ( the rest :)
}
/////////////////////////////////////////////////////////////////////////////////////
