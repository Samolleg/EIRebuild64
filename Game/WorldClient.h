#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "World.h"

#include "Weather.h"
#include "ZoneExit.h"
#include "MapObject.h"
#include "AreaMap.h"
#include "GeometryInterfaces.h"
#include "UnitStatisticsDriver.h"
#include "TerrainPrints.h"

#include <list>
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
const DWORD WC_USE_CAMERA_SCRIPT = MAKE_FLAG(WORLD_BASE_LAST_BIT, 0);
const DWORD WC_SAVE_CAMERA = MAKE_FLAG(WORLD_BASE_LAST_BIT, 1);
const DWORD WC_MAKE_MOVIE = MAKE_FLAG(WORLD_BASE_LAST_BIT, 2);

const DWORD WC_RENDER_OBJECTS = MAKE_FLAG(WORLD_BASE_LAST_BIT, 3);
const DWORD WC_RENDER_UNITS = MAKE_FLAG(WORLD_BASE_LAST_BIT, 4);
const DWORD WC_RENDER_TERRAIN = MAKE_FLAG(WORLD_BASE_LAST_BIT, 5);
const DWORD WC_RENDER_SHADOWS = MAKE_FLAG(WORLD_BASE_LAST_BIT, 6);
const DWORD WC_RENDER_EFFECTS = MAKE_FLAG(WORLD_BASE_LAST_BIT, 7);
const DWORD WC_RENDER_INTERFACE = MAKE_FLAG(WORLD_BASE_LAST_BIT, 8);
const DWORD WC_RENDER_SKY = MAKE_FLAG(WORLD_BASE_LAST_BIT, 9);
const DWORD WC_RENDER_FLYINGHP = MAKE_FLAG(WORLD_BASE_LAST_BIT, 10);

const DWORD WC_RENDER_BOUNDBOXES = MAKE_FLAG(WORLD_BASE_LAST_BIT, 11);
const DWORD WC_RENDER_WIREFRAME = MAKE_FLAG(WORLD_BASE_LAST_BIT, 12);

const DWORD WC_CHECK_TEXTURE_MAPPING_CORRECTION = MAKE_FLAG(WORLD_BASE_LAST_BIT, 13);

const DWORD WC_DO_ADJUST_LIGHTING = MAKE_FLAG(WORLD_BASE_LAST_BIT, 14);

const DWORD WC_LAST_BIT = MAKE_LAST_BIT(WORLD_LAST_BIT, 14);
/////////////////////////////////////////////////////////////////////////////////////
// statistics show flags
const DWORD WCSS_SHOW_FPS = 0x00000001;
const DWORD WCSS_SHOW_RENDER_INFO = 0x00000002;
const DWORD WCSS_SHOW_MANAGERS = 0x00000004;
const DWORD WCSS_SHOW_WORLD_DATA = 0x00000008;
/////////////////////////////////////////////////////////////////////////////////////
struct SObjectScreenRect
{
    EClassIDs clsid; // class ID - unit, tree, building, objects, etc...
    int nID;         // map object's ID
    CRect rcRect;    // screen extent
};
typedef std::list<SObjectScreenRect> CObjectScreenRectList;
typedef std::vector<SObjectScreenRect> CObjectScreenRectVector;
/////////////////////////////////////////////////////////////////////////////////////
// description of one bridge surface
struct SBridge
{
    int nID;
    CPlot3D ptCenter;             // центр моста
    float fpRadius;               // радиус, bounding sphere
    CPlot3D ptCorner, vec1, vec2; // угол и два вектора верхней части моста
    CPlot3D ptNormal;             // нормаль к плоскости
    float fpLeng1, fpLeng2;       // длины моста вдоль каждого вектора
};

/////////////////////////////////////////////////////////////////////////////////////
// system allowing automatic bridge height determinition on client side
class CBridgeManager : public CGameBase
{
public:
    CList<SBridge> bridges;

public:
    void Clear() { bridges.RemoveAll(); }
    BOOL AddSurface(const CPlot3D &p1, const CPlot3D &p2, const CPlot3D &p3, const CPlot3D &normal,
                    SBridge **pRes);
    void AddBridge(int nID, SBoundingVolume &bv, const CPlot3D &pos, const CQuaternion &rot);
    float CalcZ(const CPlot3D &pos, float fpZTerrain) const;
    void GetBridgeSurface(float x, float y, CPlot3D &ptNormal, float &fpH);
    bool IsBridge(float x, float y);
    void RemoveBridge(int nID);
    void RemoveAllBridges() { bridges.RemoveAll(); }
    virtual BOOL Create()
    {
        Clear();
        return TRUE;
    }
    virtual void Destroy() { Clear(); }
    virtual LPCSTR GetGameClassName() const { return "CBridgeManager"; }
    virtual EClassIDs WhoAmI() const { return HPC_BRIDGEMANAGER; };
};

/////////////////////////////////////////////////////////////////////////////////////
class CLightningManager;
class CPlayer;
class CPlayerClient;
class CSkyObject;
class CSoundManager;
class CParticleManager;
class CTerrain;
class CTextManager;
class CCamera;
class CTextureManager;
class CRenderer;
class CFlyingHitPoints;
class CVisFogManager;
class CMusicSystem;
class CEffectMoshka;
/////////////////////////////////////////////////////////////////////////////////////
class CWorldClient : public CWorldBase
{
    friend class SaveSchema::Slot;
    typedef std::list<CMapObject *> CMapObjectPtrList;
    typedef CList<CMapObject *, CMapObject *> CMapObjectsList;
    typedef CHashMap<int, POSITION> CPositionMap;

private:
    // IDs for temprorary objects ( this should have no ID at all !!! )
    int nLastTemporaryMapObjectID;
    DWORD dwShowStatisticsFlag; // flag, which will indicate show statistics mode: fps, managers,
                                // triangles, etc...
    //
    CAArray<SZoneExit, SZoneExit &> exits; // exits from current zone
    CArray<CSmartPtr<CParticle>, CParticle *> exitParticles;
    std::list<CSmartPtr<CMapObject>> heroes;
    CMapObjectPtrList objects; // objects, which are live in the world
    CMapObjectsList objList;
    CPositionMap objMap;
    CObjectScreenRectList lstScreenObjects;          // list of the object's rects from last render
    CObjectScreenRectVector vcPicked;                // vector of picked and sorted elements
    CObjectScreenRectVector::iterator vciLastPicked; // iterator of last picked element
    int nPickedIndex;
    CHashMap<int, CPolyLine *> polylines;
    // area maps
    CAreaMap<CMapObject> areaObjects; // area map of the all objects on the map
    CAreaMap<CBase3D> areaLights;     // area map of the all lights on the map
    //
    CTerrain *pTerrain; // all the terrain data
    CString szCurrentAllodName;
    // different data managers...
    CBaseManager *pMaterialManager;   // material manager
    CTextureManager *pTextureManager; // texture manager
    CBaseManager *pLightManager;      // light manager
    CBaseManager *pWindowManager;     // 2D window manager
    CBaseManager *pBitmapManager;     // bitmap manager
    CBaseManager *pParticleManager;   // particle manager
    CLightningManager *pLightningManager;
    CVisFogManager* pVisFogManager;
    CMusicSystem *pMusicSystem;
    //
    CSkyObject *pSky;       // object for rendering high sky
    CEffectMoshka *pMoshka; // object for rendering moshka
    //
    CFlyingHitPoints *pFlyingHP; // flying hit points
    // blood and foot prints on the terrain
    std::list<CBloodPrints> bloodprints;
    std::list<CFootPrints> footprints;
    std::list<CFirePrints> fireprints;
    int nPrintsRelightCounter;
    //
    // non-basemanager-derived
    CSoundManager *pSoundManager;  // sound manager (miniaudio output)
    CTextManager *pTextManager;    // text manager (loads strings)
    CBridgeManager bridgesManager; // object to calculate bridge height at given point
    // player for connection with server
    CPlayer *pPlayer;
    //
    CCamera *pCamera;         // camera to work with
    CPlot3D ptRotationCenter; // camera's rotation center
    float fpRotationDistance; // camera's rotation distance
    //
    CString szTexturesPath; // textures path
    // mouse scrolling
    int nMouseScrollSensitiveArea;
    //
    D3DCOLORVALUE dcvSunlights[24];
    D3DCOLORVALUE dcvAmbients[24];
    D3DCOLORVALUE dcvSkys[24];
    D3DCOLORVALUE dcvAmbient, dcvSkyAmbient, dcvSkySun;
    int nShadowAlpha;
    EGMZoneSky eZoneSky;
    EWorldDangerState dangerState;
    // для управления текстурами
    int nFrameCounter;
    // для отлова первых 'n' сегментов
    int nFirstSegmentsCounter;
    //
    float fpTimeFraction; // time fraction from last segment (between last and next: 0...1)
    // Render-only scene lifetime; never serialized or sent as gameplay state.
    ULONGLONG ownedRenderGeneration = 1;
private:
    void AddQuickReferences();
    void CreateSky();
    void SetNewConnection(CConnection *pConnection);

    int FormVisibilityLists(std::list<CSmartPtr<CMapObject>> *pObjects, const SHMatrix &matCamera,
                            float fpTime, DWORD dwCurrentLightTime, bool bPickDynamic,
                            bool bPickHeroes);
    void PrecacheData(std::vector<SUnitStatistics> &statistics,
                      std::vector<SUnitWearsStatistics> &wears);
    void SpawnExitZoneEffects();
    void ColorExitZoneEffects();
    void AddBridgeInformation(CMapObject *pObj);

public:
    CWeather weather;

public:
    std::list<int> selectedObjects; // list of selected objects
    //
    int nLastReceivedEvent;
    // windows identifiers
    HWND hWnd;           // main window handle
    HINSTANCE hInstance; // instance of this app
    HACCEL hAccel;       // accelerator
    HCURSOR hCursor;     // current cursor

    // names
    CString szMainName;  // main app name
    CString szMainClass; // main app class name
    CString szMainTitle; // title for main window
    CString szPaused;    // text in paused window

public:
    CWorldClient();
    virtual ~CWorldClient();
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    // create/destroy d3d data
    void ReleaseTextures();
    // pathes...
    virtual LPCSTR GetTexturesPath() const { return szTexturesPath; }
    //
    int GetFirstSegmentsCounter() const { return nFirstSegmentsCounter; }
    // object adding/removing/retrieving for camera
    bool AddDynamicObject(CMapObject *pMO);
    bool RemoveDynamicObject(int nID);
    BOOL AddObject(CMapObject *pMapObject);
    void RemoveObject(int nID);
    BOOL HasObject(int nID);
    virtual void MoveObject(CMapObject *pMO, const CPlot3D &newCP)
    {
        areaObjects.MoveTo(pMO, newCP);
    }
    virtual void MoveLight(CBase3D *pLight, const CPlot3D &newCP)
    {
        areaLights.MoveTo(pLight, newCP);
    }
    void RemoveAllObjects();
    void RemoveAllEffects();
    void UpdateBridgeInformation(CMapObject *lpObject);
    void RemoveShadowInformationFromObjects();
    void CollectObjectsInArea(const CPlot3D &ptPos, float fRadius, std::list<CMapObject *> &result);
    // polylines
    int AddPolyLine(CPolyLine *lpPolyLine);
    CPolyLine *RemovePolyLine(int nID);
    CPolyLine *FindPolyLine(int nID);
    BOOL DrawPolyLines();
    // terrain functions
    void CastShadowToTerrain(const CPlot3D &ptSunDir, const CPlot3D &ptCenter, float fpRadius,
                             CXYZW4Array &srcRect, CXYZW4Array &dstGeoms, CPlot2DArray &dstMaps,
                             CWORDArray &dstIndices, bool bSmallPrint = false,
                             bool bCastOnBridges = true);
    CPlot3D GetTerrainXYZFromScreenXY(int sx, int sy);
    CPlot3D GetTerrainXYZFromScreenXYWithBridge(int sx, int sy);
    CPlot3D GetTerrainXYZFromScreen(CPoint ptOnScreen)
    {
        return GetTerrainXYZFromScreenXY(ptOnScreen.x, ptOnScreen.y);
    }
    const CString& GetBackgroundAllod() const { return szCurrentAllodName; }
    CTerrain *GetTerrain() { return pTerrain; }
    CSkyObject *GetSky() { return pSky; }
    //
    void AddBloodPrints(float fX, float fY, float fSize, float fFinalSize, int nType, bool bSmall);
    int AddFootPrints(CMapObject *owner, float fX, float fY, float fSizeX, float fSizeY,
                      float fRotate, int nType, float fDetection, bool bVisible, int nBloodType);
    void AddFirePrints(float fX, float fY, float fSizeX, float fSizeY, float fRotate, float fPower);
    void VisualizePrints(const CPlot3D &ptCenter, float fVision);
    // for debug data access
    int GetNumBloodPrints() const { return bloodprints.size(); }
    int GetNumFootPrints() const { return footprints.size(); }
    int GetNumFirePrints() const { return fireprints.size(); }

    //
    CTexture *CreateTexture(int nTypeID);
    CMaterial *CreateMaterial(int nTypeID);
    CTexture *FindTexture(int nID);
    CMaterial *FindMaterial(int nID);
    //
    void LoadTextures();
    void ToggleWireframe();
    BOOL IsWireframe() const { return CheckFlag(WC_RENDER_WIREFRAME) != 0; }
    //
    void ConnectLocal(CLocalNetDriver *pServerDriver);
    void Disconnect();
    bool HasPacket();
    bool IsConnected();
    bool IsFailedConnection();
    int GetInputPacketsNum();
    void NetSegment();
    //
    void RemoveQuickItemsAndSpells();
    BOOL NewZone(CMemorySave &packet);
    BOOL AddItems(CMemorySave &packet);
    //
    BOOL NewWaterLevels(CMemorySave &packet);
    //
    void DeployClientPersonages(CPersonages &people, bool bShowHelm);
    void RemoveClientPersonages(CPersonages &people);
    BOOL IsExitPoint(const CPlot3D &ptCheck, int *nExitPoint = NULL);
    BOOL ArePersonagesAtExit(CPersonages &people, int nExitPoint, CPersonages &exitingPeople);
    LPCTSTR GetExitZoneName(int nExitPoint) const { return exits[nExitPoint].szZone; }
    int GetNumExits() { return exits.GetSize(); }
    CPlot2D GetExitCenter(int nExit);
    bool IsCaveZone() const { return eZoneSky == GMS_CAVE; }
    EWorldDangerState GetDangerState() const { return dangerState; }
    void SetDangerState(EWorldDangerState ds) { dangerState = ds; }
    //
    DWORD FormBoredAckFlagsForUnit(CUnit *pUnit);
    //
    CPlayerClient *GetPlayer();
    //
    void ReceiveDamage(int nMapObjectID, int nAmount,
                       DWORD dwDamageFlags = 0); // for flying hit points
    void CreateMoshka();
    //
    // cursor functions
    void LoadCursors();
    void BeginWaitCursor();
    void EndWaitCursor();
    // 'z' value virtual function
    virtual float GetZValue(float x, float y) const;
    virtual float GetSurfaceValue(float x, float y) const;
    virtual float GetBridgeZValue(float x, float y) const
    {
        return bridgesManager.CalcZ(CPlot3D(x, y, 0), GetZValue(x, y));
    }
    virtual float GetBridgeSurfaceZValue(float x, float y) const
    {
        return bridgesManager.CalcZ(CPlot3D(x, y, 0), GetSurfaceValue(x, y));
    }
    bool IsBridge(float x, float y) { return bridgesManager.IsBridge(x, y); }
    void GetBridgeSurface(float x, float y, CPlot3D &ptNorm, float &fpH)
    {
        bridgesManager.GetBridgeSurface(x, y, ptNorm, fpH);
    }
    // wind, this function should be used from playerclient only
    void SetClientWind(const CPlot3D &newWindVector, float fpNewWindForce);
    // sun
    BOOL LoadLights(LPCSTR pszLightsName);
    BOOL AdjustLightingWithTime();
    //
    void LoadHeroesMimic();
    // segment
    int Segment();
    float GetFPSCoeff() const;
    float GetTimeStep() const;
    float GetPreciseTime() { return fpTimeFraction + nSegmentTimeCounter; }
    // Render-only clock: preserve the same tick/fraction precision read by c74.
    double GetRenderTimeSegments() const { return double(nSegmentTimeCounter) + double(fpTimeFraction); }
    void SetTimeFraction(float fpNewVal) { fpTimeFraction = fpNewVal; }
    int GetFrameCounter() { return nFrameCounter; }
    int GetShadowAlpha() const { return nShadowAlpha; };
    void CalculateShadowAlpha();
    void ReceiveEvent(int nEventID);
    //
    void NewHooveredItems(CList<CItem *, CItem *> &newItems);
    // 'get manager' function family
    virtual CBaseManager *GetMaterialManager() { return pMaterialManager; }
    virtual CTextureManager *GetTextureManager() { return pTextureManager; }
    virtual CBaseManager *GetLightManager() { return pLightManager; }
    virtual CBaseManager *GetParticleManager() { return pParticleManager; }
    virtual CBaseManager *GetWindowManager() { return pWindowManager; }
    virtual CBaseManager *GetBitmapManager() { return pBitmapManager; }
    CLightningManager *GetLightningManager() { return pLightningManager; }
    CVisFogManager *GetVisFogManager() { return pVisFogManager; }
    CMusicSystem *GetMusicSystem() { return pMusicSystem; }
    // non-basemanager classes
    CSoundManager *GetSoundManager() { return pSoundManager; }
    CTextManager *GetTextManager() { return pTextManager; }
    //
    int GetTemporaryMapObjectID() { return nLastTemporaryMapObjectID--; }
    //
    void RecheckAllQuestStates();
    void UpdateQuestState(const char *pszQuestName, float fpVal);
    CString GetFullQuestName(const char *pszQuestName);
    //
    void SetCamera(CCamera *lpNewCamera);
    CCamera *GetCamera() { return pCamera; }
    void SetCameraScroll(CPoint point, int nFlags);
    //
    BOOL Prepare3DScene();
    BOOL Draw3DScene();
    BOOL Draw();
    // idle function (call from CWinApp::OnIdle)
    void OnIdle();
    // screen selection functions
    void PickMapObjects(const CRect &rcRect, std::list<CMapObject *> &mapobjects);
    void InvalidatePicking();
    CMapObject *RayPickFirstMapObject(const CPoint &point);
    CMapObject *RoughlyPickFirstMapObject(const CPoint &point);
    CMapObject *PickFirstMapObject(const CPoint &point);
    CMapObject *CombinedPickFirstMapObject(const CPoint &point);
    CMapObject *PickNextMapObject();
    void PickUnits(const CRect &rcRect, std::list<CUnit *> &units);
    CUnit *RoughlyPickFirstUnit(const CPoint &point);
    CUnit *PickFirstUnit(const CPoint &point);
    CUnit *CombinedPickFirstUnit(const CPoint &point);
    CUnit *PickNextUnit();
    CMapObject *
    PickMapObject(const CPoint &point); // pick first-in-the-list map object (not a nearest!)
    void SelectObject(LPGAMEBASE lpNewObject);
    void SelectObject(int nID);
    void ResetSelection(LPGAMEBASE lpNewObject = NULL);
    void ResetSelection(int nID);
    // camera centrifying on map object
    BOOL CenterOnMapObject(const LPBASE3D lpNewObject);
    BOOL CenterOnLocation(const CPlot3D &plot);
    // combat pause
    void CombatPause(bool bPause);
    // this function for playerclient only, users should call CombatPause or server will continue
    // running
    void SetCombatPause(bool bPause);
    // call this function on server side pls
    void SetWeather(EWeather newWeather, float fpTransTime)
    {
        weather.SetWeather(newWeather, fpTransTime);
    }
    CUnit *GetNearestEnemy(CUnit *pHero);
    //
    void Init(CRenderer *pRend);
    //
    void ShowFPSOn();
    void ShowFPSOff();
    BOOL IsShowFPS() const { return (dwShowStatisticsFlag & WCSS_SHOW_FPS); }
    void ShowRenderInfoOn();
    void ShowRenderInfoOff();
    BOOL IsShowRenderInfo() const { return (dwShowStatisticsFlag & WCSS_SHOW_RENDER_INFO); }
    void ShowManagersOn();
    void ShowManagersOff();
    BOOL IsShowManagers() const { return (dwShowStatisticsFlag & WCSS_SHOW_MANAGERS); }
    void ShowWorldDataOn();
    void ShowWorldDataOff();
    BOOL IsShowWorldData() const { return (dwShowStatisticsFlag & WCSS_SHOW_WORLD_DATA); }
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CWorldClient"; };
    virtual EClassIDs WhoAmI() const { return HPC_WORLD_CLIENT; }
};
/////////////////////////////////////////////////////////////////////////////////////
extern CWorldClient theWorld;
/////////////////////////////////////////////////////////////////////////////////////
