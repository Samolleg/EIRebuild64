#pragma once

#include "I3DScreen.h"
#include "SoundTypes.h"
#include "ChatServer.h"
/////////////////////////////////////////////////////////////////////////////////////
enum EBaseScreenModes
{
    BSM_MOVEMENT,
    BSM_TOPIC,
    BSM_APPROACH,
    BSM_BRIEFING,
    BSM_SUMMARY,
};
/////////////////////////////////////////////////////////////////////////////////////
struct SBriefingUnit
{
    CUnit *pUnit;
    CString szName;
    bool bAnimWasSet;
};
struct SWalkingUnit
{
    CPlot3D ptDest;
    CPlot3D rotDest;
    int nMode;
    CUnit *pUnit;
    bool bIsForBriefing;
    float fTime;
};
struct SBriefingQuestItem
{
    CString szName;
    CI3DFigure *pFigure;

    CI3DFigure *pFigFrame;
    CI3DFigure *pFigBack;
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DBaseFaces : public CI3DObject
{
    int nPersons;
    int nFaces;
    std::vector<CPtr<CChatPerson>> pPersons;
    CStringArray szStatusStrings;
    void AddFace(LPCTSTR pszFigureName, LPCTSTR pszTextureName);

public:
    CI3DBaseFaces(CInterface3D *pI);
    void AddFaces();
    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DScrollBar;
class CI3DScrollText;
class CI3DScreenBase : public CI3DScreen
{
private:
    int nCheckExit; // for exiting;
    bool bDoCheckExit;

    EBaseScreenModes eMode;

    CStringArray szBriefings;
    CString szBriefing;
    int nCurrentTopic;

    bool bWasForcedBriefing;

    CArray<CUnit *, CUnit *> pUnits;
    CUnit *pMainHero;
    CUnit *pPersonToTalk;
    CUnit *pThirdPerson;
    CPlot3D ptLeftPers, ptRightPers, ptMidPers;
    CPlot3D dirLeftPers, dirRightPers, dirMidPers;

    CPlot3D ptCameraFrom, ptCameraTo;

    CPlot3D ptMidPersOld, dirMidPersOld;
    CPlot3D ptLeftPersOld, dirLeftPersOld;

    std::list<SWalkingUnit> walks;

    bool bIsFirstPhrase;
    CString szAllTalk;
    float fPhraseTime;
    HSOUND hPhraseSound;
    CUnit *pWhosTalking;

    float fConstrTime;

    CI3DScrollText *pScroll;
    CI3DScrollBar *pScrollTopics;
    int nPos;

    float fOldFogDist;

    std::map<int, CString> localNames;

    std::list<SBriefingQuestItem> items;

    bool bForce;
    bool bCageBriefing;

    bool bNeedAutosave;

    CI3DBaseFaces *pFaces;
    std::vector<int> nBriefingPersIDs;

    float fIdleTime;

public:
    std::vector<SBriefingUnit> parts;
    EKickMode eKickMode;

private:
    void PlayBriefing(bool bForce = true, bool bNeedMemorizePos = true);
    void BeginBriefing();
    void EndBriefing();
    void FillBriefingsList(int nUnitID);
    CString GetBriefingLocalName(LPCTSTR pszName);
    void InitTopicMode(CUnit *pUnit);
    void RedrawTopics();
    void GetPersLocalNames();
    void NextCommand(bool bSkip = false);
    void AddWalkingUnit(CUnit *pUClient, CPlot3D ptDest, CPlot3D dirDest,
                        bool bIsForBriefing = true, bool bNow = false, bool bRun = false);
    void CommandShow(LPCTSTR pszWhat, int nSlot = -1, bool bHideWeapon = true);
    void CommandHide(LPCTSTR pszWhat, bool bSkip, bool bRun = false);

public:
    CI3DScreenBase(CInterface3D *pI)
        : CI3DScreen(pI)
    {
    }
    virtual void OnActivate();
    virtual void OnDeactivate();
    virtual void OnExitFromModal(DWORD dwRetValue);

    virtual void ForceBriefing(LPCTSTR pszBriefing);

    void AddControlUnit(CUnit *pUnit);
    void RemoveControlUnit(CUnit *pUnit);
    void GameOver(bool bVictory);
    void LMPPersonsUpdate(bool bAddDelete);
    bool Autosave();
    void StoreSpecific();
    void SetKickMode(EKickMode eMode) { eKickMode = eMode; }

    void PlayRndAnimation(CUnit *pUnit, bool bTalk = false, bool bShortPhrase = false);
    void PlayAnimation(CUnit *pUnit, int nAnim);
    void PlaceCamera(CUnit *pUnit, float fAngle = 0, float fDistance = 4, float fHeight = 3);
    void PlaceDefinedCamera(int nCamera);

    virtual BOOL Draw(CRenderer *pRend);

    virtual void OnIdle();

    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonDblClk(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
};
