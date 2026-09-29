#include "StdAfx.h"
#include "WideScreen.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DMain.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "EngineDiagnostics.h"
#include "I3DScreen.h"
#include "I3DConsole.h"
#include "A3Cursor.h"
#include "I3DHandler.h"
#include "DebugInfoSystem.h"
#include "IDebugInfoSystem.h"
#include "I3DScreenDebug.h"
#include "I3DEffects.h"

#include "I3DChat.h"
#include "GameMain.h"
#include "I3DScreenMainMenu.h"
#include "I3DScreenMission.h"
#include "I3DScreenCamp.h"
#include "I3DScreenGlobalMap.h"
#include "I3DScreenBase.h"
#include "I3DScreenEndGame.h"
#include "I3DScreenTutorial.h"
#include "I3DScreenObjectives.h"
#include "I3DScreenExchange.h"
#include "I3DScreenMessageBox.h"

#include "WorldClient.h"
#include "WorldInterface.h"
#include "SoundManager.h"
#include "TextManager.h"
#include "StringProcessing.h"
#include "Renderer.h"
#include "KeyMap.h"
#include "Console.h"
#include "PlayerClient.h"

#include "ScreenGrab.h"
#include "DirectXHelpers.h"
#include "dx11/RendererDX11.h"
#include "dx11/Surface.h"
/////////////////////////////////////////////////////////////////////////////////////
CInterface3D theInterface;

std::unique_ptr<CDXGISurface> SmallScreenFromScreen(CI3DSurface *pBGScreen)
{
    D3D11_TEXTURE2D_DESC tex2d;
    pBGScreen->GetSurface()->DefaultSurfaceDesc(&tex2d);
    tex2d.Width = BG_SMALL_PICTURE.cx;
    tex2d.Height = BG_SMALL_PICTURE.cx; // 256 x 256, but write to file only 256 x 192
    tex2d.Format = DXGI_FORMAT::DXGI_FORMAT_B5G6R5_UNORM;
    tex2d.MiscFlags = 0;

    std::unique_ptr<CDXGISurface> pBGSmallScreen = std::make_unique<CDXGISurface>(
        theInterface.GetD3D(), &tex2d);

    pBGScreen->StretchTo(pBGSmallScreen.get(),
                         RECT({ 0, 0, BG_SMALL_PICTURE.cx, BG_SMALL_PICTURE.cy }));
    return pBGSmallScreen;
};

/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::Init(CRenderer *pNewRend)
{
    DWORD w, h;
    pRend = pNewRend;
    pRend->GetCurrentMode(w, h);
    nScreenWidth = w;
    nScreenHeight = h;

    const char* face = "Times New Roman";
    if (auto* mod = ModRuntime::Current()) {
        if (mod->CombatSetting("useLS_NewFont")) {
            // Private registration also supports direct EXE startup without EIStarter.
            static std::string registeredFont;
            std::string path = mod->Directory() + "/Evil Font.ttf";
            if (registeredFont != path) {
                if (!registeredFont.empty()) RemoveFontResourceExA(registeredFont.c_str(), FR_PRIVATE, nullptr);
                registeredFont.clear();
                if (AddFontResourceExA(path.c_str(), FR_PRIVATE, nullptr)) registeredFont = path;
            }
            face = "Evil Font";
            EngineDiagnostics::Event("Interface font=%s private=%d", face, !registeredFont.empty());
        }
    }
    LOGFONT lf;
    float fScale;
    pTTFonts[0] = new CFont();
    if (!pTTFonts[0]->CreatePointFont(750, face))
    {
        ReportError("CInterface3D::Create.\n Cannot create font.\n");
        return;
    }
    pTTFonts[0]->GetLogFont(&lf);
    fScale = abs(lf.lfHeight) / 100.f;
    if (pTTFonts[0])
        delete pTTFonts[0];

    pTTFonts[0] = new CFont();
    if (!pTTFonts[0]->CreatePointFont(int(104 * WideScreen::Effective() / fScale / 800), face)) // was 96
    {
        ReportError("CInterface3D::Create.\n Cannot create font.\n");
        return;
    }
    pTTFonts[1] = new CFont();
    if (!pTTFonts[1]->CreatePointFont(int(112 * WideScreen::Effective() / fScale / 800), face))
    {
        ReportError("CInterface3D::Create.\n Cannot create font.\n");
        return;
    }
    pTTFonts[2] = new CFont();
    if (!pTTFonts[2]->CreatePointFont(int(144 * WideScreen::Effective() / fScale / 800), face))
    {
        ReportError("CInterface3D::Create.\n Cannot create font.\n");
        return;
    }

    DWORD dwNeedLayout = theSettings.dwKeyboardLayout;
    HKL kls[16];
    char pszLocaleName[KL_NAMELENGTH];
    int nLocales = GetKeyboardLayoutList(16, kls);
    int nEnglish = -1, nFound = -1;
    HKL kl = GetKeyboardLayout(0);
    for (int l = 0; l < nLocales; l++)
    {
        ActivateKeyboardLayout(kls[l], 0);
        if (GetKeyboardLayoutName(pszLocaleName))
        {
            DWORD dwLayout = 0;
            sscanf(pszLocaleName, "%x", &dwLayout);
            dwLayout &= 0xFFFF;
            if (dwLayout == dwNeedLayout)
            {
                nFound = l;
                break;
            }
            if (dwLayout == 0x00000409)
                nEnglish = l;
        }
    }
    if (nFound >= 0)
        ActivateKeyboardLayout(kls[nFound], 0);
    else if (nEnglish >= 0)
        ActivateKeyboardLayout(kls[nEnglish], 0);
    else
        ActivateKeyboardLayout(kl, 0);
    NKeyMap::Init();

    pConsole = dbgnew CI3DConsole(this);
    theDebugInfoSystem.Init(this);

    pChat = dbgnew CI3DChat(this);

    m_pCapture = 0;

    pBGScreen = 0;
    pBGScreen2 = 0;
    pBGSmallScreen = 0;
    eBGFlag = BG_FLAG_NOTHING;
    bScreensMustDie = false;

    pNextScreen = 0;

    bDoRender = true;

    lastCursorPos = CPoint(0, 0);

    m_pEffects = dbgnew CI3DEffects();

    bInfoClosed = false;
    bTextClosed = false;
    bMapClosed = false;
    nInfoMode = 0;
    nTextMode = 0;
    nSpeed = 0;

    bCtrlDown = false;
    bShiftDown = false;
    bAltDown = false;
    bLDblClk = false;
    bRButDown = false;

    bNeedAutosave = false;

    CStdioFile file;
    CString tutorialPath = IWorld.GetWorkPath() + "config\\tutorial.ini";
    std::string resolvedTutorialPath = ModRuntime::ConfigPath((LPCSTR)tutorialPath);
    if (file.Open(resolvedTutorialPath.c_str(), CFile::modeRead | CFile::typeText))
    {
        CString szStr;
        CStringArray szStrArray;
        while (file.ReadString(szStr))
        {
            NStrProc::SplitStringMulti(szStr, szStrArray, ' ');
            if (szStrArray.GetSize() > 1)
                tutorials[szStrArray[0]] = atoi(szStrArray[1]);
        }
        file.Close();
    }
    nLastMissionTutorialPage = 0;

    pSurfaceFPS = 0;
    fRefreshFPSTime = 0;
    dwLastUpdateTime = timeGetTime();
    nCountFrames = 0;

    ShowFPS(true);

    bCheckDisconnect = false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::Fini()
{
    DestroyScreens();

    CStdioFile file;
    CString tutorialPath = IWorld.GetWorkPath() + "config\\tutorial.ini";
    std::string resolvedTutorialPath = ModRuntime::ConfigPath((LPCSTR)tutorialPath);
    if (file.Open(resolvedTutorialPath.c_str(),
                  CFile::modeCreate | CFile::modeWrite))
    {
        CString szStr;
        for (auto it = tutorials.begin(); it != tutorials.end(); ++it)
        {
            szStr.Format("%s %d\n", it->first, it->second);
            file.WriteString(szStr);
        }
        file.Close();
    }

    pSurfaceFPS = 0;

    if (pNextScreen)
        delete pNextScreen;

    pBGScreen = 0;
    pBGScreen2 = 0;
    pBGSmallScreen = 0;

    theDebugInfoSystem.Fini();

    if (pChat)
    {
        delete pChat;
        pChat = 0;
    }

    if (pConsole)
    {
        delete pConsole;
        pConsole = 0;
    }

    NKeyMap::Fini();

    for (int i = 0; i < NUM_TT_FONTS; i++)
    {
        if (pTTFonts[i])
            delete pTTFonts[i];
        pTTFonts[i] = 0;
    }

    delete m_pEffects;
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::ClearStates()
{
    bInfoClosed = false;
    bTextClosed = false;
    bMapClosed = false;
    nInfoMode = 0;
    nTextMode = 0;
    nSpeed = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CInterface3D::Draw()
{
    CheckScreensMustDie();
    if (eBGFlag == BG_FLAG_DRAW || eBGFlag == BG_FLAG_DRAW_S || eBGFlag == BG_FLAG_CREATE_S)
    { // if we have background screen
        GetD3D()->ClearColorBuffer(0);
        if (pBGScreen)
            pBGScreen->Draw();
    }
    // restore state after XTK->DRAW mushes it
    GetD3D()->SetRenderStates();
    if (!pScreens.empty())
    {
        pScreens.back()->Draw(GetRenderer());
        // pScreens.back()->DrawTips( GetRenderer() );
    }
    m_pEffects->Draw(&theRenderer);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::Draw2D()
{
    CDX11 *pDX11 = GetD3D();
    pDX11->GetSpriteBatch()->Begin();

    GetRenderer()->SetTexturingAlphaMode(AM_DEFAULT);
    CString szString;
    if (!pScreens.empty())
    {
        pScreens.back()->Draw2D();
        pScreens.back()->Draw2DTips();
    }
    m_pEffects->Draw2D();

    if (pTextOver)
        pTextOver->Draw();
    pDX11->GetSpriteBatch()->End();

    if (eBGFlag == BG_FLAG_CREATE)
    { // if we have to create background screen
        pBGScreen = std::make_unique<CI3DSurface>(this, 0, 0, nScreenWidth, nScreenHeight, BS_VISIBLE, false);
        pBGScreen->GetFromScreen();
        DirectX::SetDebugObjectName(pBGScreen->GetSurface()->GetTexture().Get(), "pBGScreen");

        pBGSmallScreen = SmallScreenFromScreen(pBGScreen.get());
        pBGScreen->Fade();
        eBGFlag = BG_FLAG_DRAW;
    }
    else if (eBGFlag == BG_FLAG_CREATE_S)
    {
        if (pBGScreen)
            pBGScreen->MemoryBackup(FALSE);
        pBGScreen2 = std::move(pBGScreen);
        pBGScreen = std::make_unique<CI3DSurface>(this, 0, 0, nScreenWidth, nScreenHeight, BS_VISIBLE, false);
        pBGScreen->GetFromScreen();
        DirectX::SetDebugObjectName(pBGScreen->GetSurface()->GetTexture().Get(), "pBGScreenSmall");

        eBGFlag = BG_FLAG_DRAW_S;
    }

    pDX11->GetSpriteBatch()->Begin();

    if (IWorldConnector.IsLMP() && pScreens.size() == 1)
        pChat->Draw2D();

    theDebugInfoSystem.Draw2D();
    {
        nCountFrames++;
        DWORD dwNewTime = timeGetTime();
        if (dwNewTime - dwLastUpdateTime > 400)
        {
            if (pSurfaceFPS)
            {
                CString szFPS;
                szFPS.Format("%.1f", nCountFrames * 1000.f / (dwNewTime - dwLastUpdateTime));
#ifndef NOGETDC
                pSurfaceFPS->GetDC();
                pSurfaceFPS->Clear();
                pSurfaceFPS->DrawText("FPS:", CRect(0, 0, 60, 18), 1, RGB(255, 255, 255), DT_LEFT);
                pSurfaceFPS->DrawText(szFPS, CRect(0, 0, 70, 18), 1, RGB(255, 255, 255), DT_RIGHT);
                pSurfaceFPS->DrawText("FPS:", CRect(0, 0, 60, 18), 1, RGB(255, 255, 255), DT_LEFT);
                pSurfaceFPS->DrawText(szFPS, CRect(0, 0, 70, 18), 1, RGB(255, 255, 255), DT_RIGHT);
                pSurfaceFPS->ReleaseDC();
#endif // NOGETDC
            }
            dwLastUpdateTime = dwNewTime;
            nCountFrames = 0;
        }
        if (pSurfaceFPS)
            pSurfaceFPS->Draw();
    }
    if (pConsole && pConsole->IsVisible())
        pConsole->Draw2D();

    pDX11->GetSpriteBatch()->End();
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::ShowFPS(bool bOn)
{
    if (bOn)
    {
        if (!pSurfaceFPS)
            pSurfaceFPS = std::make_unique<CI3DSurface>(this, 730, 10, 800, 28);
    }
    else
    {
        pSurfaceFPS = 0;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::OnIdle()
{
    while (bExitModal)
    {
        bExitModal = false;
        theCursor.SetMode(CT_WAIT);
        ResetInputState();
        if (!pScreens.empty())
        {
            pScreens.back()->OnDeactivate();
            delete pScreens.back();
            pScreens.pop_back();
        }
        NGame::DestroyInputMessages();
        if (eBGFlag == BG_FLAG_DRAW_S)
        {
            pBGScreen = std::move(pBGScreen2);
            eBGFlag = (pBGScreen != 0) ? BG_FLAG_DRAW : BG_FLAG_NOTHING;
            if (pBGScreen)
                pBGScreen->MemoryBackup(TRUE);
        }
        if (pScreens.size() == 1)
        {
            pBGScreen = 0;
            pBGSmallScreen = 0;

            eBGFlag = BG_FLAG_NOTHING;
            if (!IWorldConnector.IsLMP())
                IWorld.CombatPause(bModalPause);
            bModalPause = false;
        }
        if (!pScreens.empty())
        {
            pScreens.back()->MemoryBackup(TRUE);
            pScreens.back()->OnExitFromModal(dwModalRetValue);
        }
        if (dwModalRetValue == 0x00990001)
        {
            SetScreensMustDie();
            IWorldConnector.StopLMP();
            IWorldConnector.ReqGoMainMenu();
        }
        ReleaseCapture();
        theCursor.SetMode(CT_DEFAULT);
    }

    if (bNeedAutosave && !pScreens.empty())
        bNeedAutosave = !pScreens.back()->Autosave();

    CheckScreensMustDie();
    if (pNextScreen)
    {
        ActivateScreen(pNextScreen);
        pNextScreen = 0;
    }

    lastCursorPos = theCursor.GetPos();

    if (bCheckDisconnect && IWorldConnector.IsLMP() && !pChatClient->IsConnected())
    {
        bCheckDisconnect = false;

        CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(this, "disconnect",
                                                                 "disconnect_msg", 0x00990000,
                                                                 MBF_OK);
        DoModal(pScr);
    }
    else if (!pScreens.empty())
        pScreens.back()->OnIdle();

    pChat->OnIdle();
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::CheckScreensMustDie()
{
    if (bScreensMustDie)
    {
        bScreensMustDie = false;
        DestroyScreens();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::SetNextScreen(CI3DScreen *pScreen)
{
    theCursor.SetMode(CT_WAIT);
    if (pNextScreen)
        delete pNextScreen;
    pNextScreen = pScreen;
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::ActivateScreen(EInterface iface)
{
    CI3DScreen *pScreen = nullptr;
    switch (iface)
    {
    case IFACE_NONE:
        return;
    case IFACE_DEBUG:
        pScreen = dbgnew CI3DScreenDebug(this);
        break;
    case IFACE_MISSION:
        pScreen = dbgnew CI3DScreenMission(this);
        break;
    case IFACE_MAINMENU:
        pScreen = dbgnew CI3DScreenMainMenu(this);
        break;
    case IFACE_BASE:
        pScreen = dbgnew CI3DScreenBase(this);
        break;
    case IFACE_GLOBAL_MAP:
        pScreen = dbgnew CI3DScreenGlobalMap(this);
        break;
    case IFACE_CAMP:
        pScreen = dbgnew CI3DScreenCamp(this);
        break;
    case IFACE_EXCHANGE:
        pScreen = dbgnew CI3DScreenExchange(this);
        break;
    case IFACE_AUTOPLAY:
        break;
    case IFACE_ENDGAME:
        pScreen = dbgnew CI3DScreenEndGame(this, false, "Crdt");
        break;
    }
    ActivateScreen(pScreen);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::ActivateScreen(CI3DScreen *pScreen)
{
    CheckScreensMustDie();
    theCursor.SetMode(CT_WAIT);
    pBGScreen = 0;
    pBGScreen2 = 0;
    eBGFlag = BG_FLAG_NOTHING;
    bModalPause = false;
    IWorld.CombatPause(false);
    DestroyScreens();
    if (!pScreen)
    {
        // error
        ReportError("Cannot activate NULL interface screen\n");
        return;
    }
    pScreens.push_back(pScreen);

    GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, TRUE);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHAREF, 128);

    NGame::bNeedRender = true;

    pScreen->OnActivate();

    NGame::DestroyInputMessages();
    ReleaseCapture();
    theCursor.SetMode(CT_DEFAULT);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::DoModal(CI3DScreen *pScreen, bool bSaveBack, bool bGrayBack)
{
    theCursor.SetMode(CT_WAIT);
    if (bSaveBack && !bGrayBack)
    {
        eBGFlag = BG_FLAG_CREATE_S;
        NGame::DrawFrame(false);
    }
    if (pScreens.size() == 1)
    {
        bModalPause = IWorld.IsCombatPause();
        if (!IWorldConnector.IsLMP())
            IWorld.CombatPause(true);
        if (bSaveBack && bGrayBack)
        {
            eBGFlag = BG_FLAG_CREATE;
            NGame::DrawFrame(false);
        }
    }
    ResetInputState();
    if (!pScreens.empty())
        pScreens.back()->MemoryBackup(FALSE);
    if (!pScreen)
    {
        // error
        ReportError("Cannot activate NULL interface screen\n");
    }
    pScreens.push_back(pScreen);

    GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, TRUE);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHAREF, 128);

    NGame::bNeedRender = true;
    pScreen->OnActivate();
    NGame::DestroyInputMessages();
    ReleaseCapture();
    theCursor.SetMode(CT_DEFAULT);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::ExitFromModal(DWORD dwRetValue)
{
    dwModalRetValue = dwRetValue;
    bExitModal = true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::DestroyScreens()
{
    theCursor.SetMode(CT_WAIT);
    while (!pScreens.empty())
    {
        pScreens.back()->ResetInputState();
        pScreens.back()->OnDeactivate();
        delete pScreens.back();
        pScreens.pop_back();
    }
    ResetInputState();
    NGame::DestroyInputMessages();
    NGame::bNeedRender = false;
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DScreen *CInterface3D::GetCurrentScreen()
{
    if (!pScreens.empty())
        return pScreens.back();
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::DrawTextOverFrame(LPCTSTR pszText)
{
    pTextOver = dbgnew CI3DSurface(this, 0, 280, 800, 320);
    if (pTextOver)
    {
#ifndef NOGETDC
        pTextOver->GetDC();
        pTextOver->Clear();
        pTextOver->DrawText(pszText, CRect(0, 0, 800, 40), 2, RGB(0, 255, 0), DT_CENTER);
        pTextOver->ReleaseDC();
#endif // NOGETDC

        NGame::DrawFrame();

        delete pTextOver;
        pTextOver = 0;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::OnLButtonDown(UINT nFlags, CPoint point)
{
    bLDblClk = false;
    if (m_pCapture)
        m_pCapture->OnLButtonDown(nFlags, point);
    else if (!pScreens.empty())
        pScreens.back()->OnLButtonDown(nFlags, point);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (m_pCapture)
        m_pCapture->OnLButtonUp(nFlags, point);
    else if (!pScreens.empty())
        pScreens.back()->OnLButtonUp(nFlags, point);
    bLDblClk = false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::OnMouseMove(UINT nFlags, CPoint point)
{
    if (m_pCapture)
        m_pCapture->OnMouseMove(nFlags, point);
    else if (!pScreens.empty())
        pScreens.back()->OnMouseMove(nFlags, point);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::OnRButtonDown(UINT nFlags, CPoint point)
{
    bRButDown = true;
    if (m_pCapture)
        m_pCapture->OnRButtonDown(nFlags, point);
    else if (!pScreens.empty())
        pScreens.back()->OnRButtonDown(nFlags, point);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::OnRButtonUp(UINT nFlags, CPoint point)
{
    bRButDown = false;
    if (m_pCapture)
        m_pCapture->OnRButtonUp(nFlags, point);
    else if (!pScreens.empty())
        pScreens.back()->OnRButtonUp(nFlags, point);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    bLDblClk = true;
    if (m_pCapture)
        m_pCapture->OnLButtonDblClk(nFlags, point);
    else if (!pScreens.empty())
        pScreens.back()->OnLButtonDblClk(nFlags, point);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::OnRButtonDblClk(UINT nFlags, CPoint point)
{
    bRButDown = true;
    if (m_pCapture)
        m_pCapture->OnRButtonDblClk(nFlags, point);
    else if (!pScreens.empty())
        pScreens.back()->OnRButtonDblClk(nFlags, point);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CInterface3D::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    if (m_pCapture)
        return m_pCapture->OnMouseWheel(nFlags, zDelta, pt);
    else
    {
        if (!pScreens.empty())
            return pScreens.back()->OnMouseWheel(nFlags, zDelta, pt);
        else
            return FALSE;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    int nKey = (nFlags & 0x1ff);
    switch (nKey)
    {
#ifndef NODEBUGINFO
    case 57: // space
        break;
#endif       // NODEBUGINFO
    case 70: // Scroll Lock
#ifndef NODEBUGINFO
        theDebugInfoSystem.Toggle();
#endif
        return;
    case 41: // "~"
        pConsole->Toggle();
        return;
    case 29:
    case 285: // ctrl
        if (!(nFlags & 0x4000))
            bCtrlDown = true;
        else
            return;
        break;
    case 42:
    case 54: // shift
        if (!(nFlags & 0x4000))
            bShiftDown = true;
        else
            return;
        break;
    case 56:
    case 312: // alt
        if (!(nFlags & 0x4000))
            bAltDown = true;
        else
            return;
        break;
    }

    if (pConsole->IsVisible())
    {
        pConsole->OnKeyDown(nChar, nRepCnt, nFlags);
        return;
    }

    if (IWorldConnector.IsLMP() && pChat->IsEntering())
    {
        pChat->OnKeyDown(nChar, nRepCnt, nFlags);
        return;
    }

    if (!pScreens.empty())
        pScreens.back()->OnKeyDown(nChar, nRepCnt, nFlags);

    if (!pScreens.empty() && !pScreens.back()->GetProcessed())
        switch (NKeyMap::keyMap[nFlags & 0x1ff])
        {
        case KA_TUTORIAL_SCRIPT:
            CheckTutorial(true);
            break;
#ifndef NODEBUGINFO
        case KA_DEBUG_SHOT: {
        }
        break;
        case KA_DEBUG_SHOT_SEQ:
            break;

#endif // NODEBUGINFO
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if ((nFlags & 0x1ff) == 41)
        return;
    if (pConsole->IsVisible())
    {
        pConsole->OnChar(nChar, nRepCnt, nFlags);
        return;
    }

    if (IWorldConnector.IsLMP() && pChat->IsEntering())
    {
        pChat->OnChar(nChar, nRepCnt, nFlags);
        return;
    }

    if (!pScreens.empty())
        pScreens.back()->OnChar(nChar, nRepCnt, nFlags);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    switch (nChar)
    {
    case VK_CONTROL:
        bCtrlDown = false;
        break;
    case VK_SHIFT:
        bShiftDown = false;
        break;
    case VK_MENU:
        bAltDown = false;
        break;
    }

    if (pConsole->IsVisible())
    {
        pConsole->OnKeyUp(nChar, nRepCnt, nFlags);
        return;
    }

    if (!pScreens.empty())
        pScreens.back()->OnKeyUp(nChar, nRepCnt, nFlags);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::BeginChatLine()
{
    if (!pChat->IsEntering())
        pChat->SetEntering(true);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::ClearChatLines()
{
    if (!pChat->IsEntering())
        pChat->ClearLines();
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::VirtualToScreen(CRect &rect)
{
    rect.left = rect.left * WideScreen::Effective(nScreenWidth, nScreenHeight) / 800 + (nScreenWidth - WideScreen::Effective(nScreenWidth, nScreenHeight)) / 2;
    rect.top = rect.top * nScreenHeight / 600;
    rect.right = rect.right * WideScreen::Effective(nScreenWidth, nScreenHeight) / 800 + (nScreenWidth - WideScreen::Effective(nScreenWidth, nScreenHeight)) / 2;
    rect.bottom = rect.bottom * nScreenHeight / 600;
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::VirtualToScreen(CPoint &point)
{
    point.x = point.x * WideScreen::Effective(nScreenWidth, nScreenHeight) / 800 + (nScreenWidth - WideScreen::Effective(nScreenWidth, nScreenHeight)) / 2;
    point.y = point.y * nScreenHeight / 600;
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::VirtualToScreen(CSize &point)
{
    point.cx = point.cx * WideScreen::Effective(nScreenWidth, nScreenHeight) / 800;
    point.cy = point.cy * nScreenHeight / 600;
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::ScreenToVirtual(CRect &rect)
{
    rect.left = (rect.left - (nScreenWidth - WideScreen::Effective(nScreenWidth, nScreenHeight)) / 2) * 800 / WideScreen::Effective(nScreenWidth, nScreenHeight);
    rect.top = rect.top * 600 / nScreenHeight;
    rect.right = (rect.right - (nScreenWidth - WideScreen::Effective(nScreenWidth, nScreenHeight)) / 2) * 800 / WideScreen::Effective(nScreenWidth, nScreenHeight);
    rect.bottom = rect.bottom * 600 / nScreenHeight;
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::ScreenToVirtual(CPoint &pt)
{
    pt.x = (pt.x - (nScreenWidth - WideScreen::Effective(nScreenWidth, nScreenHeight)) / 2) * 800 / WideScreen::Effective(nScreenWidth, nScreenHeight);
    pt.y = pt.y * 600 / nScreenHeight;
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::ScreenToVirtual(CSize &pt)
{
    pt.cx = pt.cx * 800 / WideScreen::Effective(nScreenWidth, nScreenHeight);
    pt.cy = pt.cy * 600 / nScreenHeight;
}
/////////////////////////////////////////////////////////////////////////////////////
HSOUND CInterface3D::BeginFX(LPCTSTR pszSoundName, BOOL bCycle)
{
    return theWorld.GetSoundManager()->BeginFX(pszSoundName, 1000, 100, 0, bCycle);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::StopFX(HSOUND hSound)
{
    if (hSound != SND_EMPTY)
        theWorld.GetSoundManager()->CancelFX(hSound);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::ConsoleOutput(LPCTSTR pszText)
{
    if (pConsole)
        pConsole->Output(pszText);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::ConsoleResize(CRect &rc)
{
    if (pConsole)
        pConsole->SetRect(rc);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::ConsoleHistory(int nLines)
{
    if (pConsole)
        pConsole->SetHistory(nLines);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::DebugInfoResize(CRect &rc)
{
    theDebugInfoSystem.SetRect(rc);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::NewHooveredItems(CList<CItem *, CItem *> &newItems, int nArrivedBucks,
                                    bool bCrippled)
{
    if (!pScreens.empty())
        pScreens.back()->NewHooveredItems(newItems, nArrivedBucks, bCrippled);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::AddControlUnit(CUnit *pUnit)
{
    if (!pScreens.empty())
        pScreens.back()->AddControlUnit(pUnit);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::RemoveControlUnit(CUnit *pUnit)
{
    if (!pScreens.empty())
        pScreens.back()->RemoveControlUnit(pUnit);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::GameOver()
{
    if (!pScreens.empty())
        pScreens.back()->GameOver(false);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::QuestUpdate(LPCTSTR pszQuestVar, float fState)
{
    if (!pScreens.empty())
        pScreens.back()->QuestUpdate(pszQuestVar, fState);
    CStringArray szVarParts;
    if (fState == 1)
    {
        NStrProc::SplitStringMulti(pszQuestVar, szVarParts, '.');
        if (szVarParts.GetSize() > 2)
            szLastReceivedQuest = szVarParts[2];
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::StringEvent(LPCTSTR pszMsg)
{
    if (pszMsg)
    {
        CStringArray szTmpArray;
        CString szStr(pszMsg);
        NStrProc::SplitStringMulti(szStr, szTmpArray, ' ');
        szTmpArray[0].MakeLower();
        if ((szTmpArray[0] == "say") || (szTmpArray[0] == "say_block"))
        {
            int nUnitID = 0;
            if (szTmpArray.GetSize() < 2)
                return;
            if (szTmpArray.GetSize() > 2)
                nUnitID = GetPersonageIDByName(szTmpArray[2]);
            DWORD dwAckType = szTmpArray[0] == "say_block" ? ACK_HERO_SCENARIO_BLOCK
                                                           : ACK_HERO_SCENARIO;
            if (!pScreens.empty())
                pScreens.back()->PlayAcknowlegment(nUnitID, dwAckType, szTmpArray[1]);
        }
        else if (szTmpArray[0] == "tutorial")
        {
            if (szTmpArray.GetSize() < 2)
                return;
            missionTutorials.push_back(szTmpArray[1]);
        }
        else if (szTmpArray[0] == "briefing")
        {
            if (szTmpArray.GetSize() < 2)
                return;
            if (!pScreens.empty())
                pScreens.back()->ForceBriefing(szTmpArray[1]);
        }
        else if (szTmpArray[0] == "autosave")
            bNeedAutosave = true;
        else if (szTmpArray[0] == "gameover")
        {
            IWorldClient.StopAllSounds();
            IWorldClient.DisableAmbientSounds();
            BeginFX("buttons\\gameover.wav");
            const CString title = szTmpArray.GetSize() > 1 ? szTmpArray[1] : "game_over";
            const CString message = szTmpArray.GetSize() > 2 ? szTmpArray[2] : "game_over_msg";
            CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(this, title, message,
                                                                      0x00020000);
            DoModal(pScr);
        }
        else if (szTmpArray[0] == "endofgame")
        {
            if (!pScreens.empty())
                pScreens.back()->GameOver(true);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::ActionFailure(DWORD dwUnitID, DWORD dwFailure)
{
    if (!pScreens.empty())
        pScreens.back()->PlayAcknowlegment(dwUnitID, dwFailure, 0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::LMPPersonsUpdate(bool bAddDelete)
{
    if (!pScreens.empty())
        pScreens.back()->LMPPersonsUpdate(bAddDelete);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::LMPPhrase(LPCTSTR pszPhrase, int nOrder)
{
    pChat->NewPhrase(pszPhrase, nOrder);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::LMPItemXCHGComplete(CParty &itemsRemote, CParty &itemsOffer, CParty &itemsInv)
{
    if (!pScreens.empty())
        pScreens.back()->LMPItemXCHGComplete(itemsRemote, itemsOffer, itemsInv);
}
/////////////////////////////////////////////////////////////////////////////////////
CSize CInterface3D::TextSize(CString szStr, int nFont)
{
    CSize sizeChar;
    CDC *pDC;

    pDC = CDC::FromHandle(GetDC(NGame::hWnd));
    // TODO some font error? maybe from offscreen
    if (!pDC) //
        return 0;
    CFont *pFont = GetFont(nFont);
    CFont *pOldFont = pDC->SelectObject(pFont);
    sizeChar = pDC->GetOutputTextExtent(szStr);
    pDC->SelectObject(pOldFont);
    ReleaseDC(NGame::hWnd, pDC->m_hDC);
    ScreenToVirtual(sizeChar);

    return sizeChar;
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::Load(CSave &file)
{
    file.Read(bInfoClosed);
    file.Read(bTextClosed);
    file.Read(bMapClosed);
    file.Read(nInfoMode);
    file.Read(nTextMode);

    if (file.GetVersion() < 71)
        return;

    file.Read(nSpeed);

    if (file.GetVersion() < 86)
        return;

    int n, nTmp;
    CString szTmp;
    file.Read(n);
    for (int i = 0; i < n; i++)
    {
        file.Read(szTmp);
        file.Read(nTmp);
    }

    file.Read(szLastMissionTutorial);
    if (file.GetVersion() < 93)
        return;
    file.Read(nLastMissionTutorialPage);
    if (file.GetVersion() < 95)
        return;
    file.Read(szLastReceivedQuest);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::Store(CSave &file)
{
    file.Write(bInfoClosed);
    file.Write(bTextClosed);
    file.Write(bMapClosed);
    file.Write(nInfoMode);
    file.Write(nTextMode);
    file.Write(nSpeed);

    int nTmp = 0;
    file.Write(nTmp);

    file.Write(szLastMissionTutorial);
    file.Write(nLastMissionTutorialPage);

    file.Write(szLastReceivedQuest);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::StoreSpecific()
{
    if (!pScreens.empty())
        pScreens[0]->StoreSpecific();
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::CheckTutorial(bool bForce)
{
    if (pScreens.empty())
        return;

    LPCTSTR pszName = pScreens.back()->GetTutorialScreenName();
    if (!pszName)
        return;

    int nPos = -1;
    auto it = tutorials.find(pszName);
    if (it != tutorials.end())
        nPos = it->second;
    else
        tutorials[pszName] = 0;

    if (!theSettings.nShowTutorial && !bForce)
        return;
    if (bForce || nPos < 0)
    {
        CI3DScreenTutorial *pScr = dbgnew CI3DScreenTutorial(this, pszName, nPos);
        DoModal(pScr, true, false);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::CheckMissionTutorials(bool bForce)
{
    bool bNewTut = false;
    if (!bForce && !missionTutorials.empty())
    {
        auto it = missionTutorials.begin();
        CString szTut(*it);
        missionTutorials.erase(it);
        szLastMissionTutorial = szTut;
        nLastMissionTutorialPage = 0;
        bNewTut = true;
    }

    if (!bForce && !bNewTut)
        return;

    if (szLastMissionTutorial.IsEmpty())
        return;

    if (!theSettings.nShowTutorial && !bForce)
        return;
    CI3DScreenTutorial *pScr = dbgnew CI3DScreenTutorial(this, szLastMissionTutorial,
                                                         nLastMissionTutorialPage, true);
    DoModal(pScr, true, false);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::ResetInputState()
{
    bCtrlDown = false;
    bAltDown = false;
    bShiftDown = false;
    bRButDown = false;
    bLDblClk = false;
    if (!pScreens.empty())
        pScreens.back()->ResetInputState();
}
/////////////////////////////////////////////////////////////////////////////////////
CString CInterface3D::FormChatMessageString(EChatMessages eMsg, LPCTSTR pszParam)
{
    CString szTemplate, szOut;
    switch (eMsg)
    {
    case CMS_PLAYERENTER:
        szTemplate = "lmp_player_connected";
        break;
    case CMS_PLAYERLEAVE:
        szTemplate = "lmp_player_disconnected";
        break;
    case CMS_PLAYERKICKED:
        szTemplate = "lmp_player_kicked";
        break;
    case CMS_PLAYERBANNED:
        szTemplate = "lmp_player_banned";
        break;
    case CMS_PLAYERDROPPED:
        szTemplate = "lmp_player_lost_connection";
        break;
    case CMS_QUESTTAKEN:
        szTemplate = "lmp_quest_taken";
        break;
    case CMS_QUESTCANCELED:
        szTemplate = "lmp_quest_canceled";
        break;
    case CMS_QUESTCOMPLETED:
        szTemplate = "lmp_quest_completed";
        break;
    case CMS_ZONEENABLED:
        szTemplate = "lmp_zone_enabled";
        break;
    case CMS_ZONEDISABLED:
        szTemplate = "lmp_zone_disabled";
        break;
    case CMS_TRADEREQUEST:
        szTemplate = "lmp_trade_requested";
        break;
    case CMS_TRADECANCEL:
        szTemplate = "lmp_trade_canceled";
        break;
    case CMS_PAUSEDBY:
        szTemplate = "reserved_02";
        break;
    }
    theWorld.GetTextManager()->GetSystemString(szTemplate, szTemplate);
    if (pszParam)
        szOut.Format(szTemplate, pszParam);
    else
        szOut = szTemplate;
    return szOut;
}
/////////////////////////////////////////////////////////////////////////////////////

void CInterface3D::VirtualToScreenLocal(CRect &r)
{
    CPoint origin(0, 0); VirtualToScreen(origin); VirtualToScreen(r); r.OffsetRect(-origin.x, -origin.y);
}
void CInterface3D::VirtualToScreenLocal(CPoint &p)
{
    CSize size(p.x, p.y); VirtualToScreen(size); p = CPoint(size.cx, size.cy);
}
void CInterface3D::ScreenToVirtualLocal(CPoint &p)
{
    CSize size(p.x, p.y); ScreenToVirtual(size); p = CPoint(size.cx, size.cy);
}
