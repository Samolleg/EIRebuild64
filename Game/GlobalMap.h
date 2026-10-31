#pragma once

#include "ZoneExit.h"
#include "sArray.h"
/////////////////////////////////////////////////////////////////////////////////////
enum EGMZoneType
{
    GM_REALZONE,
    GM_BETWEENZONES,
    GM_BASE
};
/////////////////////////////////////////////////////////////////////////////////////
struct SScenarioAllod
{
public:
    CString szName;
    CString szArrowFigure1;
    CString szArrowFigure2;
    CString szObjFigure;
    CString szBottomFigure;
    CString szCampFigure;
    CArray<int, int> zones;
};

// --------------------------------------------------------------------------------------------
struct SScenarioZone
{
public:
    CString szName;
    CString szAllodName;         // parent allod
    CString szMobName;           // mob file to load
    CString szTerrainName;       // mpr file to load
    CString szFigureName;        // figure to load on global map screen
    CString szMapTexture;        // texture to show in objectives screen
    CString szMinimapTexture;    // texture to show in minimap
    CString szCameraName;        // default camera view for briefing zones
    int nSizeX, nSizeY;          // tiles
    EGMZoneType eType;           // real zone, between zones or base (briefing zone)
    EGMZoneWeather eWeatherType; // weather possible in this zone
    EGMZoneSky eSkyType;         // sky in this zone - normal or cave
    bool bRestrictCamera;        // do we need to restrict camera movement (briefing zones)
    float fRestrictX, fRestrictY, fRadius; // restriction circle
    bool bCageBriefings; // true if all briefings take place near hero's location (briefing zones)
    CAArray<SZoneExit, SZoneExit &> exits; // exits
};

// --------------------------------------------------------------------------------------------
class CTextManager;
class CGlobalMap
{
private:
    CArray<SScenarioAllod, SScenarioAllod &> allAllods;
    CArray<SScenarioZone, SScenarioZone &> allZones;

public:
    void Init();
    bool LoadZones(const char *pszFileName);
    bool LoadZones(const char *pszFileName,CTextManager& texts);

    SScenarioAllod *GetAllod(int i) { return &allAllods[i]; }
    SScenarioAllod *GetAllodByName(LPCTSTR pszName);
    int GetAllodNum() { return allAllods.GetSize(); }

    SScenarioZone *GetZone(int i) { return &allZones[i]; }
    SScenarioZone *GetZoneByName(LPCTSTR pszName);
    SScenarioZone *GetZoneByMPRName(LPCTSTR pszName);
    int GetZoneNum() { return allZones.GetSize(); }

    SScenarioAllod *GetAllodByMobName(LPCTSTR pszName);
};
// --------------------------------------------------------------------------------------------
extern CGlobalMap theMap;
