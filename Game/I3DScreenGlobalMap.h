#pragma once

#include "I3DScreen.h"
#include "GlobalMap.h"
#include "I3DScreenObjectives.h"
#include <vector>
#include <map>
/////////////////////////////////////////////////////////////////////////////////////
struct SGlobalMapZone
{
    int state;
    int iChildBase;
    CString szName;
    CString szLocalName; // localized
};
/////////////////////////////////////////////////////////////////////////////////////
struct SGlobalMapBase
{
    int iParentZone;
    CPlot3D loc; // position
    CString szName;
    CString szLocalName; // localized
};
/////////////////////////////////////////////////////////////////////////////////////
struct SGlobalMapObj
{
    int iParentZone;
    CPlot3D loc; // position
    CString szName;
    CString szLocalName; // localized
};
/////////////////////////////////////////////////////////////////////////////////////
typedef std::pair<SScenarioZone *, int> CHAIN_ELEMENT;
typedef std::vector<CHAIN_ELEMENT> CHAIN;
class CCloudsGenerator;
/////////////////////////////////////////////////////////////////////////////////////
class CI3DScreenGlobalMap : public CI3DScreen
{
    friend struct GlobalMapPreviewAccess;
private:
    int nZones;
    int nObjectives;
    int nBases;
    int nTotal;

    int nRotate; // -1, 0, 1
    float fTime;

    CString szAllodLocalName; // localized
    CPlot3D cur_loc;          // where arrow is

    CArray<SGlobalMapZone, SGlobalMapZone &> zones;
    CArray<SGlobalMapBase, SGlobalMapBase &> bases;
    CArray<SGlobalMapObj, SGlobalMapObj &> objs;

    // for finding shortest path
    CHAIN path;
    float fPathLen;
    CString szDestZoneName;
    bool bPathFound;
    bool bIsSecondaryPath;

    ZONE_ENTRANCES entrances;

    CQuaternion qRot;
    float fRotAngle;
    float fDist;
    float nCenterX, nCenterY;

    BOOL bGenerateHitTestDDS;
    std::unique_ptr<CI3DSurface> pHitTestDDS;

    CCloudsGenerator *pCloudGen;

    bool bRButDown;
    CPoint ptBegin;
    float fRotBegin;

    CString szCurZoneName;
    bool bNeedMergeBaseWithZone;

    int nCampState;
    float fZoneR, fZoneG, fZoneB;

    bool bNeedAutosave;

    HSOUND hSnd;

private:
    void ApplyCloudsParams();

private:
    bool CheckZonePresence(LPCTSTR pszZoneName);

    void GenerateHitTestDDS();
    void RefreshPreviewHitTest();
    void SetZonesColors(int nCurZone = -1, bool bIsBase = false);

    void SetFigurePos(CI3DFigure *pFig, CPlot3D pos);

    CPlot3D GetObjPos(LPCTSTR pszName);
    CPlot3D GetZonePos(LPCTSTR pszName);

    CString GetObjLocalName(LPCTSTR pszName);
    CString GetZoneLocalName(LPCTSTR pszName);
    CString GetAllodLocalName(LPCTSTR pszName);

    int FindPathes();
    void RecurseFindPath(SScenarioZone *pZ);
    int HitTestFiguresForCurrentZone(const CPoint point);

public:
    CI3DScreenGlobalMap(CInterface3D *pI)
        : CI3DScreen(pI)
    {
    }
    virtual void OnActivate();
    virtual void OnDeactivate();
    virtual void OnExitFromModal(DWORD dwRetValue);
    virtual LPCTSTR GetTutorialScreenName();

    void StoreSpecific();

    virtual BOOL Draw(CRenderer *pRend);
    BOOL DrawWithCamera(CRenderer *pRend);

    virtual void ResetInputState();

    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual void OnRButtonDown(UINT nFlags, CPoint point);
    virtual void OnRButtonUp(UINT nFlags, CPoint point);
};
