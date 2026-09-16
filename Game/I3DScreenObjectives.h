#pragma once

#include "I3DScreen.h"
#include <map>
/////////////////////////////////////////////////////////////////////////////////////
typedef std::map<int, float> ZONE_ENTRANCES;
struct SEntranceDesc
{
    bool bValid;
    int nE;             // number of entrance - in map numeration
    float fTime;        // time needed to reach this entrance
    float fX, fY;       // center of deploy rect
    float fXScr, fYScr; // the same in screen coordinates
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DScrollBar;
class CI3DScreenObjectives : public CI3DScreen
{
private:
    // input information
    std::vector<CString> szAllZones;
    CString szBaseZone;
    BOOL bFromGM;
    ZONE_ENTRANCES *pEntrances;
    BOOL bFromBase;

    int nCurZone;
    CString szZoneName;

    CArray<SEntranceDesc, SEntranceDesc &> descEntrances;
    int nSelEntrance; // internal numeration

    int nCurObj, nOldObj;
    CStringArray szObjectives;

    CI3DScrollBar *pScroll;
    int nPos;

    float fArrowScale;
    float fTime;

    std::vector<CPlot2D> crosses;

    CStringArray szStrings;
    enum
    {
        PASSTIME,
        DAYS,
        HOURS,
        TOTAL
    };
    static CString szCurrentQuest;
    DWORD dwTempAlpha;

private:
    void RedrawObjective();
    void RedrawObjectivesList();
    void MoveScrollToSelection();

    void GoToZone();
    void SetCurrentEntrance(int num);
    void ChangeZone(LPCTSTR pszZoneName);

    void ReloadObjPos(LPCTSTR pszName);

public:
    CI3DScreenObjectives(CInterface3D *pI)
        : CI3DScreen(pI)
        , pEntrances(0)
    {
    }
    virtual void OnActivate();
    virtual void OnDeactivate();
    virtual LPCTSTR GetTutorialScreenName();

    void SetParams(LPCTSTR pszZone, ZONE_ENTRANCES *pEntr = 0, BOOL bFromGM = FALSE,
                   BOOL bFromBase = FALSE);

    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
};
