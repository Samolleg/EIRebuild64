#pragma once

#include "SoundTypes.h"
#include "ServerConsts.h"
#include "Renderer.h"
#include <vector>
#include <map>
/////////////////////////////////////////////////////////////////////////////////////
const CSize BG_SMALL_PICTURE = { 256, 192 }; //-V1043 who cares
const int NUM_TT_FONTS = 3;
/////////////////////////////////////////////////////////////////////////////////////
class CUnit;
class CItem;
class CParty;
/////////////////////////////////////////////////////////////////////////////////////
enum EI3DBGFlags
{
    BG_FLAG_NOTHING,
    BG_FLAG_CREATE,
    BG_FLAG_DRAW,
    BG_FLAG_CREATE_S,
    BG_FLAG_DRAW_S
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DCursor;
class CI3DConsole;
class CI3DScreen;
class CI3DObject;
class CI3DHandler;
class CI3DSurface;
class CRenderer;
class CI3DEffects;
class CDXGISurface;
/////////////////////////////////////////////////////////////////////////////////////
class CInterface3D
{
private:
    int nScreenWidth, nScreenHeight; // screen size ( for area initialization )

    CRenderer *pRend;

    CI3DHandler *m_pCapture; // pointer to object who capture mouse
    CI3DCursor *pCursor;     // cursor
    CI3DConsole *pConsole;
    CI3DEffects *m_pEffects;

    std::unique_ptr<CI3DSurface> pSurfaceFPS;
    float fRefreshFPSTime;
    DWORD dwLastUpdateTime;
    int nCountFrames;

    bool bDoRender;

    std::vector<CI3DScreen *> pScreens;
    CI3DScreen *pNextScreen;
    bool bScreensMustDie;

    DWORD dwModalRetValue;
    bool bExitModal;

    EI3DBGFlags eBGFlag;
    std::unique_ptr<CI3DSurface> pBGScreen; // faded background screen (e.g. in saveload)
    std::unique_ptr<CI3DSurface> pBGScreen2;
    std::unique_ptr<CDXGISurface> pBGSmallScreen; // background screen for save / load snapshot
    bool bModalPause;

    CI3DSurface *pTextOver;

    CPoint lastCursorPos;

    CFont *pTTFonts[NUM_TT_FONTS];

    bool bCtrlDown;
    bool bShiftDown;
    bool bAltDown;
    bool bLDblClk;
    bool bRButDown;

    bool bNeedAutosave;
    bool bSavedScreenPending = false;
    bool bActivatingSavedScreen = false;

    std::list<CString> missionTutorials;


public:
    // window states
    bool bInfoClosed;
    bool bTextClosed;
    bool bMapClosed;
    int nInfoMode;
    int nTextMode;
    int nSpeed;

    std::map<CString, int> tutorials;
    CString szLastMissionTutorial;
    int nLastMissionTutorialPage;
    CString szLastReceivedQuest;

public:
    void Init(CRenderer *pRend);
    void Fini();
    void ClearStates();
    CI3DEffects *GetIEffect() { return m_pEffects; }

    size_t GetAudioScreenDepth() const { return pScreens.size(); }
    CRenderer *GetRenderer() { return pRend; }
    CDX11 *GetD3D() { return pRend->GetD3D(); }
    int GetWidth() { return nScreenWidth; }
    int GetHeight() { return nScreenHeight; }

    void VirtualToScreenLocal(CRect &rect);
    void VirtualToScreenLocal(CPoint &point);
    void ScreenToVirtualLocal(CPoint &point);
    void VirtualToScreen(CRect &rect);
    void VirtualToScreen(CPoint &point);
    void VirtualToScreen(CSize &size);

    void ScreenToVirtual(CRect &rect);
    void ScreenToVirtual(CPoint &point);
    void ScreenToVirtual(CSize &size);

    // last mouse pos
    CPoint GetMousePos() { return lastCursorPos; }

    // screen managing functions - for external calls
    void SetNextScreen(CI3DScreen *pScreen);
    void ActivateScreen(EInterface iface);
    void ActivateScreen(CI3DScreen *pScreen);
    void DoModal(CI3DScreen *pScreen, bool bSaveBack = true, bool bGrayBack = true);
    void ExitFromModal(DWORD dwRetValue = 0);
    void DestroyScreens();
    void RetireSavedScreens() noexcept;
    void AdoptSavedInterface() noexcept { bNeedAutosave=false; bSavedScreenPending=true; }
    bool IsRestoringSavedScreen() const noexcept { return bActivatingSavedScreen; }
    void SetScreensMustDie() { bScreensMustDie = true; }
    void CheckScreensMustDie();
    CI3DScreen *GetCurrentScreen();
    int GetScreensNum() const { return pScreens.size(); }

    bool GetModalPause() { return bModalPause; }
    void SetModalPause(bool bPause) { bModalPause = bPause; }


    // tutorial
    void CheckTutorial(bool bForce = false);
    void CheckMissionTutorials(bool bForce = false);

    // fonts
    CFont *GetFont(int i) { return pTTFonts[i]; }
    CFont **GetFontSlot(int i) { return &pTTFonts[i]; }
    CSize TextSize(CString szStr, int nFont);

    // sounds
    HSOUND BeginFX(LPCTSTR pszSoundName, BOOL bCycle = FALSE);
    void StopFX(HSOUND hSound = SND_EMPTY);

    CDXGISurface *GetSmallPicture() { return pBGSmallScreen.get(); }
    CI3DSurface *GetPicture() { return pBGScreen.get(); }

    // updates
    void AddControlUnit(CUnit *pUnit);
    void RemoveControlUnit(CUnit *pUnit);
    void NewHooveredItems(CList<CItem *, CItem *> &newItems, int nArrivedBucks, bool bCrippled);
    void GameOver();
    void StringEvent(LPCTSTR pszText);
    void ActionFailure(DWORD dwUnitID, DWORD dwFailure);
    void QuestUpdate(LPCTSTR pszQuestVar, float fState);



    float InternetRefreshProgress();

    // console & debug info
    void ConsoleOutput(LPCTSTR pszText);
    void ConsoleResize(CRect &rc);
    void ConsoleHistory(int nLines);
    void DebugInfoResize(CRect &rc);
    void ShowFPS(bool bOn);

    // mouse input
    CI3DHandler *GetCapture() { return m_pCapture; }
    void SetCapture(CI3DHandler *pObj) { m_pCapture = pObj; }
    void ReleaseCapture() { m_pCapture = 0; }

    // kbd input
    bool IsCtrlDown() const { return bCtrlDown; }
    bool IsShiftDown() const { return bShiftDown; }
    bool IsAltDown() const { return bAltDown; }
    bool IsLDblClk() const { return bLDblClk; }
    bool IsRButDown() const { return bRButDown; }

    void ResetInputState();

    // saving/loading
    void Load(CSave &file);
    void Store(CSave &file);
    void StoreSpecific();

    // special for drawing "saving..." message
    void DrawTextOverFrame(LPCTSTR pszText);

    // drawing
    BOOL Draw();
    void Draw2D();
    void ToggleRender() { bDoRender = !bDoRender; }
    bool IsRendered() { return bDoRender; }

    // messages
    void OnIdle();
    void OnLButtonDown(UINT nFlags, CPoint point);
    void OnLButtonUp(UINT nFlags, CPoint point);
    void OnMouseMove(UINT nFlags, CPoint point);
    void OnRButtonDown(UINT nFlags, CPoint point);
    void OnRButtonUp(UINT nFlags, CPoint point);
    void OnLButtonDblClk(UINT nFlags, CPoint point);
    void OnRButtonDblClk(UINT nFlags, CPoint point);
    BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
    void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
};
std::unique_ptr<CDXGISurface> SmallScreenFromScreen(CI3DSurface *pBGScreen);
/////////////////////////////////////////////////////////////////////////////////////
extern CInterface3D theInterface;
