#pragma once
#include "ModMessages.h"

#include "I3DScreen.h"
#include "Unit.h"
#include "ChatServer.h"
#include <map>
/////////////////////////////////////////////////////////////////////////////////////
class CWorldClient;
const int MAX_NUM_WEAPONS = 4;
const int MAX_NUM_ITEMS = 4;
const int MAX_NUM_FACES = 5;
const int MAX_NUM_SPELLS = 8;
const int MAX_NUM_SKILLS = 5;
/////////////////////////////////////////////////////////////////////////////////////
enum ECursorInputModes
{
    MODE_SIMPLE = 0,
    MODE_SPELL_TARGET_UNIT,
    MODE_SPELL_TARGET_TILE,
    MODE_ITEM_TARGET_UNIT,
    MODE_ITEM_TARGET_TILE,
    MODE_ITEM_TARGET_OBJECT,
    MODE_QUEST_TARGET_OBJECT,
    MODE_SKILL_TARGET_UNIT,
    MODE_SKILL_TARGET_OBJECT
};
/////////////////////////////////////////////////////////////////////////////////////
struct SI3DFaceState
{
    int nOldHP;
    float fTime;
    CQuaternion qDef;
    CQuaternion qCur;
    CQuaternion qSel;

    int nPlayAnim;
    float fAnimTime;
    // 0 - no animation
    // 1 - yes
    // 2 - no
    // 3 - idle movement

    CString szTexName;
    int nCurTex;
    int nPlayTex;
    float fTexTime;
    // 0 - default
    // 1 - happy
    // 2 - angry
    // 3 - tired

    CString szFigNameB;
    CString szFigNameE;
    float fCurGrace;

    CQuaternion qIdle;
    float fIdleSpeed;

    bool bNotOnZone;
    int nUniquePersonID;
};
/////////////////////////////////////////////////////////////////////////////////////
struct SLMPFacesState
{
    std::vector<int> nPeopleIDs;
    int nMainHeroID;
    std::vector<int> nUniqueIDs;

    bool operator==(const SLMPFacesState &a) const;
    bool operator!=(const SLMPFacesState &a) const { return !(*this == a); }
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectWeapons;
class CI3DObjectItems;
class CI3DObjectSpells;
class CI3DObjectSkills;
class CI3DObjectFaces;
class CI3DObjectInfo;
class CI3DObjectMap;
class CI3DObjectClock;
class CI3DObjectCommands;
class CI3DScrollText;
class CI3DObjectOutput;
class CI3DScreenMission : public CI3DScreen
{
private:
    CUnit *pLMPUnit;
    CArray<CUnit *, CUnit *> pUnits;

    int nInputMode;
    int nQSlot; // for spells, perks etc.
    int nParam; // for scientific qitems and so on

    int nCheckExit; // for exiting;
    bool bDoCheckExit;

    bool bHLButtonDown[6];

    CI3DObjectWeapons *pWeapons;
    CI3DObjectItems *pItems;
    CI3DObjectSpells *pSpells;
    CI3DObjectSkills *pSkills;
    CI3DObjectFaces *pFaces;
    CI3DObjectInfo *pInfo;
    CI3DObjectMap *pMap;
    CI3DObjectClock *pClock;
    CI3DObjectCommands *pCommands;
    CI3DScrollText *pScrollText;
    CI3DObjectOutput *pOutput;

    bool bLButDown, bRubberSel;
    CPoint startRubber;
    CRect rcRubber;

    CArray<int, int> selectionIDs; // when we load game here we store the selection

    std::map<int, CString> localNames; // all personages' names

    int nQuestSound;
    bool bNeedAutosave;

    SLMPFacesState lmpState;

public:
    SRPGScienceStats tool;
    EKickMode eKickMode;

private:
    void CheckCursorType() {}
    void AllMove(CPlot3D plot, bool bSwarm = false, bool bRun = false);
    void AllAttack(CUnit *pUnit, bool bRun = false);
    void AllLoot(CUnit *pUnit, bool bRun = false);
    // selection
    void ResetSelection();
    void UpdateAllObjects();

public:
    CI3DScreenMission(CInterface3D *pI)
        : CI3DScreen(pI)
    {
    }
    virtual void OnActivate();
    virtual void OnDeactivate();
    virtual void OnExitFromModal(DWORD dwRetValue);

    // called from outside
    void NewHooveredItems(CList<CItem *, CItem *> &newItems, int nArrivedBucks, bool bCrippled);
    void AddControlUnit(CUnit *pUnit);
    void RemoveControlUnit(CUnit *pUnit);
    void GameOver(bool bVictory);
    void LMPPersonsUpdate(bool bAddDelete);
    void PlayAcknowlegment(DWORD dwUnitID, DWORD dwAck, const char *pszAckID);
    void QuestUpdate(LPCTSTR pszQuestVar, float fState);
    bool Autosave();
    void StoreSpecific();
    void SetKickMode(EKickMode eMode) { eKickMode = eMode; }

    // called from inside
    CUnit *GetSelection(int &nSelected, int &nIndex);
    void SelectionUpdate(int num);
    void CenterCamera(int num);
    CUnit *GetUnit(int i) const { return pUnits[i]; }
    int GetNumUnits() const { return pUnits.GetSize(); }
    bool IsOneOfUnits(CUnit *pU) const;
    ERPGHitLocations GetCShotState();
    void CastSpell(CUnit *pUnit, bool bWand = false, bool bRun = false);
    void CastSpell(CPlot3D plot, CUnit *pTarget = 0, bool bWand = false);
    void SetInputMode(int nMode, int nQSlot = -1, int nParam = -1);
    void GetInputMode(int &nMode, int &nQSlot);
    bool GetAreaTargetPreview(CWorldClient& world,CPlot3D& center,float& radius) const;
    void CheckAction(CUnit *pU, CPoint point);
    void PerformAction(CUnit *pU, CPoint point);
    void GetUnitName(CUnit *pUnit, CString &szOut);

    virtual BOOL Draw(CRenderer *pRend);

    virtual void OnIdle();

    virtual void ResetInputState();

    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
    virtual void OnLButtonDblClk(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectWeapons : public CI3DObject
{
private:
    CI3DScreenMission *pScreen;

    int nWeapons;
    int iSelected;
    CUnit *pUnit;

    enum
    {
        DAMAGE,
        ACTIONPTS,
        RANGE,
        SPELL,
        TOTAL
    };
    CStringArray szStrings;

public:
    CI3DObjectWeapons(CInterface3D *pI, CI3DScreenMission *pScr);

    void AddWeapon(LPCTSTR pszFigureName, LPCTSTR pszTextureName);
    void RemoveAllWeapons();
    void FormToolTipW(int slot,CString& text);
    void ShowSelectWeapon(int num, BOOL bSelect);

    void ChangeWeapon(int nSlot);

    void SetUnit(CUnit *pNewUnit);

    virtual BOOL Draw(CRenderer *pRend);
    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
};
/////////////////////////////////////////////////////////////////////////////////////
struct SQItemDesc
{
    bool bQuick;
    int nID;
    CItem *pItem;
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectItems : public CI3DObject
{
private:
    CI3DScreenMission *pScreen;

    int nItems;
    int iSelected;
    CUnit *pUnit;
    CArray<SQItemDesc, SQItemDesc &> items;
    int nSubst[8];

    enum
    {
        MANA,
        EFFECT,
        SPEED,
        RANGE,
        AREA,
        DURATION,
        TARGETS,
        INSTANT,
        AREAUNIT,
        METERS,
        SECONDS,
        FIXEDEFF,
        TOTAL
    };
    CStringArray szStrings;

private:
    void FormToolTip(int nQItem, CString &szOutput);

public:
    CI3DObjectItems(CInterface3D *pI, CI3DScreenMission *pScr);

    void AddItem(LPCTSTR pszFigureName, LPCTSTR pszTextureName);
    void RemoveAllItems();
    void ShowSelectItem(int num, BOOL bSelect);
    void PlaceItem(int num, int pos);

    void DeselectItems();

    void ActivateQItem(int nItem);
    void SelfActivateQItem(int nItem);

    void SetUnit(CUnit *pNewUnit);

    virtual BOOL Draw(CRenderer *pRend);
    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
    virtual void OnLButtonDblClk(UINT nFlags, CPoint point);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectSpells : public CI3DObject
{
private:
    CI3DScreenMission *pScreen;

    int nSpells;
    int iSelected;
    CUnit *pUnit;

    enum
    {
        MANA,
        MAGICSCHOOL,
        EFFECT,
        SPEED,
        RANGE,
        AREA,
        DURATION,
        TARGETS,
        INSTANT,
        AREAUNIT,
        METERS,
        SECONDS,
        FIXEDEFF,
        TOTAL
    };
    CStringArray szStrings;

public:
    CI3DObjectSpells(CInterface3D *pI, CI3DScreenMission *pScr);

    void AddSpell(LPCTSTR pszFigureName, LPCTSTR pszTextureName);
    void RemoveAllSpells();
    void ShowSelectSpell(int num, BOOL bSelect);
    void DeselectSpells();

    void ChangeSpell(int nSlot);
    void SelfCast(int nSlot);
    CSpell* GetAreaPreviewSpell(const CI3DScreenMission* owner,int slot) const;

    void SetUnit(CUnit *pNewUnit);

    virtual BOOL Draw(CRenderer *pRend);
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
    virtual void OnLButtonDblClk(UINT nFlags, CPoint point);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectSkills : public CI3DObject
{
private:
    CI3DScreenMission *pScreen;

    int nSkills;
    int iSelected;
    CUnit *pUnit;
    DWORD fieldCampButtonLastUpdate;
    int fieldCampButtonAvailability;
    CArray<int, int> nTypes;

    CStringArray szStrings;

public:
    CI3DObjectSkills(CInterface3D *pI, CI3DScreenMission *pScr);

    void AddSkill(LPCTSTR pszFigureName, LPCTSTR pszTextureName, int nType);
    void RemoveAllSkills();
    void ShowSelectSkill(int num, BOOL bSelect);
    void DeselectSkills();

    void ShowTacticsMenu();
    void RestoreSkillBar();
    void ChangeSkill(LPCTSTR pszSkillName);
    void ChangeSkill(int nSlot);

    void SetUnit(CUnit *pNewUnit, bool bIsSomeone = true);

    virtual BOOL Draw(CRenderer *pRend);
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectFaces : public CI3DObject
{
private:
    CI3DScreenMission *pScreen;

    int nFaces;
    float fHealthLength;
    float fManaLength;
    std::vector<SI3DFaceState> faces;

public:
    CI3DObjectFaces(CInterface3D *pI, CI3DScreenMission *pScr);

    void AddFace(LPCTSTR pszFigureName, LPCTSTR pszTextureName, bool bIsMainHero = false,
                 bool bNotOnZone = false);
    void AddFaces(bool bIsMainHero = false);
    void SelectFace(int numFace, BOOL bSelect);
    void PlayAnim(int nFace, int nAnim);
    void PlayTex(int nFace, int nTex);
    void ChangeFaceTexture(int nFace, int nTex);

    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectMap : public CI3DObject
{
private:
    CI3DScreenMission *pScreen;

    float fXCenter, fYCenter;
    float fXDir, fYDir;
    float fXMapSize, fYMapSize;
    float fMaxSize; // for zone in meters

    float fTerr2FigScale; // terrain meter to figure meter
    float fZoom;          // zoom in center
    float fMaxZoom;       // max zoom

    float fPixInMeter;

    CSimplyArray<CPlot2D, CPlot2D> orig_edges;
    CSimplyArray<CPlot2D, CPlot2D> map_edges;

    BOOL slide_bIsSlide;    // is sliding now
    int slide_nCurPixels;   // how many pixels are visible now
    int slide_nTotalPixels; // all pixels to slide
    float slide_fCurTime;   // current time ( 0..1 )
    float slide_dir;        // 1 show, -1 hide

    int nMode; // 0 - none, 1 - zoom in, 2 - zoom out

    float fNormTime;

public:
    CI3DObjectMap(CInterface3D *pI, CI3DScreenMission *pScr)
        : CI3DObject(pI)
        , pScreen(pScr)
    {
    }
    void Init(float fInitZoom, bool bClosed);

    float GetZoomValue() { return fZoom; }

    void InvertClosedState();

    void ScrollFigures();
    void ShowCurrentMode();
    void NormalizeCamera();

    CPlot2D FromTerrainToFigure(CPlot2D pt, BOOL bZoom = FALSE);
    CPlot2D FromFigureToTerrain(CPlot2D pt, BOOL bZoom = FALSE);
    CPlot2D FromTerrainToMap(CPlot2D pt);
    CPlot2D FromMapToTerrain(CPlot2D pt);

    void ModifyMapFigure();
    void PlaceUnits();

    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
    virtual BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectInfo : public CI3DObject
{
private:
    CI3DScreenMission *pScreen;

    CUnit *pUnit;
    CUnit *pTempUnit;
    float fAltUnit, fAltTempUnit;
    float fDistUnit, fDistTempUnit;
    CString szUnitName, szTempUnitName;

    BOOL slide_bIsSlide;    // is sliding now
    int slide_nCurPixels;   // how many pixels are visible now
    int slide_nTotalPixels; // all pixels to slide
    float slide_fCurTime;   // current time ( 0..1 )
    float slide_dir;        // 1 show, -1 hide

    int nMode;

    int nEffectsLoaded[6];

    CStringArray szStrings, szHLNames;
    enum
    {
        HEALTH,
        MANA,
        SPEED,
        ATTACK,
        DEFENSE,
        USEMAGIC,
        SIGHT,
        TRACK,
        TOHIT,
        DAMAGE,
        ABSORPTION,
        RESIST,
        VULNERABLE,
        METERS,
        CURWEAPON,
        ACTIONS,
        CONSIDER,
        OVERLOAD,
        TOTAL
    };

    enum
    {
        SKULL,
        TORSO,
        LEG,
        ARM
    };

private:
    void DrawTotalHP(CUnit *pUnit);
    void DrawHLState(CUnit *pUnit);
    void DrawStats(CUnit *pUnit);
    void DrawName(LPCTSTR pszName);
    void DrawSpellEffects(CUnit *pUnit);

public:
    CI3DObjectInfo(CInterface3D *pI, CI3DScreenMission *pScr)
        : CI3DObject(pI)
        , pScreen(pScr)
    {
    }
    void Init(int iMode, bool bClosed);

    void ActivateMode(int nMode);

    void ScrollFigures();
    void ShowCurrentMode();

    void SetUnit(CUnit *pNewUnit);
    void SetTempUnit(CUnit *pUnit);

    virtual BOOL Draw(CRenderer *pRend);

    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
    virtual BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectClock : public CI3DObject
{
private:
    friend class CI3DScreenMission;
    void AccelerateFromKeyboard();
    void DecelerateFromKeyboard();
    void SetSpeedMode(int mode, bool fromKeyboard = false);
    CI3DScreenMission *pScreen;
    float fIdleTime;
    float fAngleSeconds;
    int nMode;
    bool bNewObj;

public:
    CI3DObjectClock(CInterface3D *pI, CI3DScreenMission *pScr)
        : CI3DObject(pI)
        , pScreen(pScr)
    {
    }
    void Init(int nInitSpeed);

    void TogglePause();
    void ViewObjectives();
    void NewObjectives();
    void Accelerate();
    void Decelerate();

    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectCommands : public CI3DObject
{
private:
    CI3DScreenMission *pScreen;
    int nMode;
    int nAggr;

public:
    CI3DObjectCommands(CInterface3D *pI, CI3DScreenMission *pScr)
        : CI3DObject(pI)
        , pScreen(pScr)
    {
    }
    virtual void Init();

    void ChangePose(int nPose);
    void Swarm();

    void SetMode(int iMode);
    void SetAggr(int iAggr);

    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectOutput : public CI3DObject
{
private:
    CI3DScreenMission *pScreen;

    BOOL slide_bIsSlide;    // is sliding now
    int slide_nCurPixels;   // how many pixels are visible now
    int slide_nTotalPixels; // all pixels to slide
    float slide_fCurTime;   // current time ( 0..1 )
    float slide_dir;        // 1 show, -1 hide

    int nMode;
    enum NotificationMode { Hidden, Transient, History };
    NotificationMode notificationMode = Hidden;
    DWORD lastNotification = 0;
    void SetNotificationMode(NotificationMode mode);
    void SyncNotificationVisibility();

    CString szOutputText;
    CString szHooverText;

private:
    void ShowCurrentMode();
    void ScrollFigures();

public:
    CI3DObjectOutput(CInterface3D *pI, CI3DScreenMission *pScr)
        : CI3DObject(pI)
        , pScreen(pScr)
    {
    }
    virtual ~CI3DObjectOutput(){ModMessages::queue.Unbind(this);}
    void Init(int iMode, bool bClosed);

    void ActivateMode(int nNewMode);

    void SetOutputRect(CRect rc);

    const CString &GetOutput() { return szOutputText; }
    const int GetMoney();

    void ClearOutput();
    void SetMoney(int nBucks);

    void AddOutput(LPCTSTR pszText);
    void RestoreOutput(LPCTSTR text);
    bool CloseHistory();
    bool ScrollHistory(UINT flags, short delta, CPoint point);
    void AddHoover(CItem *pItem, bool bCrippled);
    void AddHoover(int nArrivedBucks);
    void UpdateHoover();

    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
    virtual BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
};
