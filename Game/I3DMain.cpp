#include "StdAfx.h"
#include "AgentBackground.h"
#include "Gamepad64.h"
#include "RuntimePaths64.h"
#include "CorePerf.h"
#include "OwnedRender/E1TextLifetime.h"
#include "widescreen_layout.h"
#include "OwnedRender/E3Input.h"
#include "GameJournal.h"
#include "InterfaceFont.h"
#include <typeinfo>
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

#include "GameMain.h"
#include "I3DScreenMainMenu.h"
#include "I3DScreenMission.h"
#include "I3DScreenCamp.h"
#include "I3DScreenGlobalMap.h"
#include "I3DScreenBase.h"
#include "I3DScreenEndGame.h"
#include "I3DScreenTutorial.h"
#include "I3DScreenObjectives.h"
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
        if (mod->AdvancedUI("useLS_NewFont")) {
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
    if (!InterfaceFont::CreatePointFont(*pTTFonts[0], 750, face))
    {
        ReportError("CInterface3D::Create.\n Cannot create font.\n");
        return;
    }
    pTTFonts[0]->GetLogFont(&lf);
    // Use the same interface unit and font divisor as the renderer.
    const auto fontLayout = owned::widescreen::make_layout(float(w), float(h));
    const float fontFactor = owned::widescreen::interface_font_scale(fontLayout);
    fScale = abs(lf.lfHeight) * fontFactor;
    if (pTTFonts[0])
        delete pTTFonts[0];

    pTTFonts[0] = new CFont();
    if (!InterfaceFont::CreatePointFont(*pTTFonts[0], int(double(104 * w) / fScale * 0.00125f), face)) // was 96
    {
        ReportError("CInterface3D::Create.\n Cannot create font.\n");
        return;
    }
    pTTFonts[1] = new CFont();
    if (!InterfaceFont::CreatePointFont(*pTTFonts[1], int(double(112 * w) / fScale * 0.00125f), face))
    {
        ReportError("CInterface3D::Create.\n Cannot create font.\n");
        return;
    }
    pTTFonts[2] = new CFont();
    if (!InterfaceFont::CreatePointFont(*pTTFonts[2], int(double(144 * w) / fScale * 0.00125f), face))
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
    std::string resolvedTutorialPath = RuntimePaths64::ConfigRead("tutorial.ini", ModRuntime::ConfigPath((LPCSTR)tutorialPath).c_str());
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

    OwnedRender::E1TextInterfaceBegin((HFONT)GetFont(0)->GetSafeHandle());
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::Fini()
{
    OwnedRender::E1TextInterfaceEnd();
    OwnedRender::E3RetireInterface();
    DestroyScreens();

    CStdioFile file;
    if (!AgentBackground::Enabled() && file.Open(RuntimePaths64::Config("tutorial.ini").string().c_str(),
                  CFile::modeCreate | CFile::modeWrite))
    {
        CString szStr;
        for (auto it = tutorials.begin(); it != tutorials.end(); ++it)
        {
            szStr.Format("%s %d\n", static_cast<LPCSTR>(it->first), it->second);
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
    CorePerf::Partition coreParts(CorePerf::InterfaceSetup);
    CheckScreensMustDie();
    OwnedRender::E3RefreshInterface();
    if (eBGFlag == BG_FLAG_DRAW || eBGFlag == BG_FLAG_DRAW_S || eBGFlag == BG_FLAG_CREATE_S)
    { // if we have background screen
        GetD3D()->ClearColorBuffer(0);
        if (pBGScreen)
            pBGScreen->Draw();
    }
    // restore state after XTK->DRAW mushes it
    GetD3D()->SetRenderStates();
    coreParts.Stage(CorePerf::InterfaceScreen);
    if (!pScreens.empty())
    {
        pScreens.back()->Draw(GetRenderer());
        // pScreens.back()->DrawTips( GetRenderer() );
    }
    coreParts.Stage(CorePerf::InterfaceEffects);
    m_pEffects->Draw(&theRenderer);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// FPS is a complete repaint. Rasterize in CPU memory so updating the counter
// never acquires a GPU surface DC while the previous frame is using it.
static void UpdateFpsSurface(CInterface3D& interface3D, CI3DSurface& target, LPCSTR text)
{
    auto* surface = target.GetSurface();
    const UINT width = surface->GetWidth();
    const UINT height = surface->GetHeight();
    CDC dc;
    if (!dc.CreateCompatibleDC(nullptr))
        AfxThrowResourceException();

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -LONG(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    DWORD* pixels = nullptr;
    CBitmap bitmap;
    if (!bitmap.Attach(CreateDIBSection(dc.GetSafeHdc(), &info, DIB_RGB_COLORS,
                                       reinterpret_cast<void**>(&pixels), nullptr, 0)))
        AfxThrowResourceException();
    CBitmap* previousBitmap = dc.SelectObject(&bitmap);
    CFont* previousFont = dc.SelectObject(interface3D.GetFont(1));
    dc.FillSolidRect(0, 0, width, height, target.colorKey);
    dc.SetBkMode(TRANSPARENT);
    const auto drawText = [&](LPCSTR value, CRect rectangle, UINT format) {
        interface3D.VirtualToScreenLocal(rectangle);
        rectangle.bottom += 2;
        rectangle.right += 2;
        CRect shadow = rectangle + CPoint(1, 1);
        dc.SetTextColor(RGB(8, 8, 8));
        dc.DrawText(value, -1, shadow, format);
        dc.SetTextColor(RGB(255, 255, 255));
        dc.DrawText(value, -1, rectangle, format);
    };
    // Keep the original position for a single value, with room to the left for FG.
    const int labelLeft = strchr(text, '(') ? 0 : 70;
    drawText("FPS:", CRect(labelLeft, 0, labelLeft + 60, 18), DT_LEFT);
    drawText(text, CRect(0, 0, 140, 18), DT_RIGHT);
    drawText("FPS:", CRect(labelLeft, 0, labelLeft + 60, 18), DT_LEFT);
    drawText(text, CRect(0, 0, 140, 18), DT_RIGHT);
    const BOOL flushed = GdiFlush();
    dc.SelectObject(previousFont);
    dc.SelectObject(previousBitmap);
    IfFailedThrowHR(flushed ? S_OK : E_FAIL, "FPS CPU rasterization");
    for (size_t i = 0; i < size_t(width) * height; ++i)
        pixels[i] |= 0xff000000u;
    D3D11_SUBRESOURCE_DATA upload = {pixels, width * UINT(sizeof(DWORD)), 0};
    surface->UpdateTexture(0, nullptr, &upload);
}

void CInterface3D::Draw2D()
{
    CorePerf::Partition uiTiming(CorePerf::UiRefresh);
    OwnedRender::E3RefreshInterface();
    CDX11 *pDX11 = GetD3D();
    pDX11->GetSpriteBatch()->Begin();

    GetRenderer()->SetTexturingAlphaMode(AM_DEFAULT);
    CString szString;
    if (!pScreens.empty())
    {
        uiTiming.Stage(CorePerf::UiScreen);
        pScreens.back()->Draw2D();
        uiTiming.Stage(CorePerf::UiHudBoundary);
        OwnedRender::E3HudBoundary();
        uiTiming.Stage(CorePerf::UiTips);
        pScreens.back()->Draw2DTips();
    }
    uiTiming.Stage(CorePerf::UiEffects);
    m_pEffects->Draw2D();

    uiTiming.Stage(CorePerf::UiTextOver);
    if (pTextOver)
        pTextOver->Draw();
    uiTiming.Stage(CorePerf::UiFlush);
    pDX11->GetSpriteBatch()->End();

    uiTiming.Stage(CorePerf::UiBackground);
    if (eBGFlag == BG_FLAG_CREATE)
    { // if we have to create background screen
        pBGScreen = std::make_unique<CI3DSurface>(this, 0, 0, nScreenWidth, nScreenHeight, BS_VISIBLE, false);
        pBGScreen->GetFromScreen(true);
        if (auto texture = pBGScreen->GetSurface()->GetTexture())
            DirectX::SetDebugObjectName(texture.Get(), "pBGScreen");

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
        pBGScreen->GetFromScreen(true);
        if (auto texture = pBGScreen->GetSurface()->GetTexture())
            DirectX::SetDebugObjectName(texture.Get(), "pBGScreenSmall");

        eBGFlag = BG_FLAG_DRAW_S;
    }

    pDX11->GetSpriteBatch()->Begin();

    uiTiming.Stage(CorePerf::UiDebug);
    theDebugInfoSystem.Draw2D();
    uiTiming.Stage(CorePerf::UiFps);
    {
        nCountFrames++;
        DWORD dwNewTime = timeGetTime();
        if (dwNewTime - dwLastUpdateTime > 400)
        {
            if (pSurfaceFPS)
            {
                CString szFPS;
                double outputFps = 0, realFps = 0;
                if (pDX11->FrameGenerationFps(outputFps, realFps))
                    szFPS.Format("%.0f (%.0f)", outputFps, realFps);
                else
                    szFPS.Format("%.1f", nCountFrames * 1000.f / (dwNewTime - dwLastUpdateTime));
#ifndef NOGETDC
                UpdateFpsSurface(*this, *pSurfaceFPS, szFPS);
#endif // NOGETDC
            }
            dwLastUpdateTime = dwNewTime;
            nCountFrames = 0;
        }
        if (pSurfaceFPS)
            pSurfaceFPS->Draw();
    }
    uiTiming.Stage(CorePerf::UiConsole);
    if (pConsole && pConsole->IsVisible())
        pConsole->Draw2D();

    uiTiming.Stage(CorePerf::UiFlush);
    pDX11->GetSpriteBatch()->End();
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::ShowFPS(bool bOn)
{
    if (bOn)
    {
        if (!pSurfaceFPS)
            pSurfaceFPS = std::make_unique<CI3DSurface>(this, 660, 10, 800, 28);
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
            GameJournal::Event("SCREEN", "close", "class=%s", GameJournal::Quote(typeid(*pScreens.back()).name()).c_str());
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

    if (!pScreens.empty())
        pScreens.back()->OnIdle();

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
    OwnedRender::E1TextRetire(OwnedRender::E1TextKind::pane);
    OwnedRender::E1TextRetire(OwnedRender::E1TextKind::chat);
    pScreens.push_back(pScreen);

    GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, TRUE);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHAREF, 128);

    NGame::bNeedRender = true;
    bActivatingSavedScreen = bSavedScreenPending;
    bSavedScreenPending = false;

    GameJournal::Event("SCREEN", "open", "class=%s", GameJournal::Quote(typeid(*pScreen).name()).c_str());
    pScreen->OnActivate();

    bActivatingSavedScreen = false;
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
    OwnedRender::E1TextRetire(OwnedRender::E1TextKind::pane);
    OwnedRender::E1TextRetire(OwnedRender::E1TextKind::chat);
    pScreens.push_back(pScreen);

    GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, TRUE);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
    GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHAREF, 128);

    NGame::bNeedRender = true;
    GameJournal::Event("SCREEN", "open", "class=%s", GameJournal::Quote(typeid(*pScreen).name()).c_str());
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
void CInterface3D::RetireSavedScreens() noexcept
{
    // Acceptance has already succeeded. Each detached screen must be deleted
    // while its former world/party still exist, even if its deactivation fails.
    try {OwnedRender::E1TextRetire(OwnedRender::E1TextKind::pane);OwnedRender::E1TextRetire(OwnedRender::E1TextKind::chat);theCursor.SetMode(CT_WAIT);}
    catch(CException *e){e->Delete();GameJournal::AllocationFailureInCatch("Save64 screen retirement");}
    catch(...){GameJournal::AllocationFailureInCatch("Save64 screen retirement");}
    while(!pScreens.empty()){
        auto *screen=pScreens.back();pScreens.pop_back();
        try{screen->ResetInputState();screen->OnDeactivate();}
        catch(CException *e){e->Delete();GameJournal::AllocationFailureInCatch("Save64 screen deactivation");}
        catch(...){GameJournal::AllocationFailureInCatch("Save64 screen deactivation");}
        delete screen;
    }
    try{ResetInputState();NGame::DestroyInputMessages();}
    catch(CException *e){e->Delete();GameJournal::AllocationFailureInCatch("Save64 input retirement");}
    catch(...){GameJournal::AllocationFailureInCatch("Save64 input retirement");}
    bScreensMustDie=false;NGame::bNeedRender=false;
}
void CInterface3D::DestroyScreens()
{
    OwnedRender::E1TextRetire(OwnedRender::E1TextKind::pane);
    OwnedRender::E1TextRetire(OwnedRender::E1TextKind::chat);
    theCursor.SetMode(CT_WAIT);
    while (!pScreens.empty())
    {
        pScreens.back()->ResetInputState();
        GameJournal::Event("SCREEN", "close", "class=%s", GameJournal::Quote(typeid(*pScreens.back()).name()).c_str());
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
    GameJournal::Event("INPUT", "left_down", "screen=%s capture=%s x=%d y=%d flags=%u", GameJournal::Quote(m_pCapture?"captured":pScreens.empty()?"none":typeid(*pScreens.back()).name()).c_str(), GameJournal::Quote(m_pCapture?typeid(*m_pCapture).name():"none").c_str(), point.x, point.y, nFlags);
    bLDblClk = false;
    if (m_pCapture)
        m_pCapture->OnLButtonDown(nFlags, point);
    else if (!pScreens.empty())
        pScreens.back()->OnLButtonDown(nFlags, point);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::OnLButtonUp(UINT nFlags, CPoint point)
{
    GameJournal::Event("INPUT", "left_up", "screen=%s capture=%s x=%d y=%d flags=%u", GameJournal::Quote(m_pCapture?"captured":pScreens.empty()?"none":typeid(*pScreens.back()).name()).c_str(), GameJournal::Quote(m_pCapture?typeid(*m_pCapture).name():"none").c_str(), point.x, point.y, nFlags);
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
    GameJournal::Event("INPUT", "right_down", "screen=%s capture=%s x=%d y=%d flags=%u", GameJournal::Quote(m_pCapture?"captured":pScreens.empty()?"none":typeid(*pScreens.back()).name()).c_str(), GameJournal::Quote(m_pCapture?typeid(*m_pCapture).name():"none").c_str(), point.x, point.y, nFlags);
    bRButDown = true;
    if (m_pCapture)
        m_pCapture->OnRButtonDown(nFlags, point);
    else if (!pScreens.empty())
        pScreens.back()->OnRButtonDown(nFlags, point);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::OnRButtonUp(UINT nFlags, CPoint point)
{
    GameJournal::Event("INPUT", "right_up", "screen=%s capture=%s x=%d y=%d flags=%u", GameJournal::Quote(m_pCapture?"captured":pScreens.empty()?"none":typeid(*pScreens.back()).name()).c_str(), GameJournal::Quote(m_pCapture?typeid(*m_pCapture).name():"none").c_str(), point.x, point.y, nFlags);
    bRButDown = false;
    if (m_pCapture)
        m_pCapture->OnRButtonUp(nFlags, point);
    else if (!pScreens.empty())
        pScreens.back()->OnRButtonUp(nFlags, point);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    GameJournal::Event("INPUT", "left_double", "screen=%s capture=%s x=%d y=%d flags=%u", GameJournal::Quote(m_pCapture?"captured":pScreens.empty()?"none":typeid(*pScreens.back()).name()).c_str(), GameJournal::Quote(m_pCapture?typeid(*m_pCapture).name():"none").c_str(), point.x, point.y, nFlags);
    bLDblClk = true;
    if (m_pCapture)
        m_pCapture->OnLButtonDblClk(nFlags, point);
    else if (!pScreens.empty())
        pScreens.back()->OnLButtonDblClk(nFlags, point);
}
/////////////////////////////////////////////////////////////////////////////////////
void CInterface3D::OnRButtonDblClk(UINT nFlags, CPoint point)
{
    GameJournal::Event("INPUT", "right_double", "screen=%s capture=%s x=%d y=%d flags=%u", GameJournal::Quote(m_pCapture?"captured":pScreens.empty()?"none":typeid(*pScreens.back()).name()).c_str(), GameJournal::Quote(m_pCapture?typeid(*m_pCapture).name():"none").c_str(), point.x, point.y, nFlags);
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
void CInterface3D::VirtualToScreen(CRect &rect)
{
    owned::widescreen::native_surface_rect(WideScreen::Layout(nScreenWidth,nScreenHeight),WideScreen::currentAnchor,rect);
}
void CInterface3D::VirtualToScreen(CPoint &point)
{
    const auto layout=WideScreen::Layout(nScreenWidth,nScreenHeight);
    point.x=owned::widescreen::pixel_x(layout,owned::widescreen::native_x(layout,WideScreen::currentAnchor,float(point.x)));
    point.y=owned::widescreen::pixel_y(layout,owned::widescreen::native_y(layout,WideScreen::currentAnchor,float(point.y)));
}
void CInterface3D::VirtualToScreen(CSize &point)
{
    const auto layout=WideScreen::Layout(nScreenWidth,nScreenHeight);
    point.cx=owned::widescreen::local_pixels(layout,point.cx);
    point.cy=owned::widescreen::local_pixels(layout,point.cy);
}
void CInterface3D::ScreenToVirtual(CRect &rect)
{
    owned::widescreen::pixels_rect_to_placed(owned::widescreen::make_layout(float(nScreenWidth),float(nScreenHeight)),rect);
}
void CInterface3D::ScreenToVirtual(CPoint &pt)
{
    const auto layout=WideScreen::Layout(nScreenWidth,nScreenHeight);
    pt.x=owned::widescreen::cursor_x(layout,pt.x);
    pt.y=owned::widescreen::cursor_y(layout,pt.y);
}
void CInterface3D::ScreenToVirtual(CSize &pt)
{
    const auto layout=WideScreen::Layout(nScreenWidth,nScreenHeight);
    pt.cx=owned::widescreen::local_units(layout,pt.cx);
    pt.cy=owned::widescreen::local_units(layout,pt.cy);
}
// Placed coordinates above are already anchored by the source widget owner.
// Surface-local rectangles below never inherit a screen origin or HUD pivot.
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
CSize CInterface3D::TextSize(CString szStr, int nFont)
{
    const auto layout = owned::widescreen::make_layout(float(nScreenWidth), float(nScreenHeight));
    SIZE pixels{};
    if (!owned::character_pane_measure_text((HFONT)GetFont(nFont)->GetSafeHandle(),
        szStr, szStr.GetLength(), pixels)) return CSize(0, 0);
    return CSize(owned::widescreen::text_width(layout, pixels.cx),
        owned::widescreen::text_height(layout, pixels.cy));
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
    pScr->CaptureMission(pScreens.size()==1?pScreens.back():nullptr);
    DoModal(pScr, true, false);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CInterface3D::IsConsoleVisible() const { return pConsole && pConsole->IsVisible(); }
void CInterface3D::ResetInputState()
{
    Gamepad64::Cancel();
    bCtrlDown = false;
    bAltDown = false;
    bShiftDown = false;
    bRButDown = false;
    bLDblClk = false;
    if (!pScreens.empty())
        pScreens.back()->ResetInputState();
}
/////////////////////////////////////////////////////////////////////////////////////

void CInterface3D::VirtualToScreenLocal(CRect &r)
{
    owned::widescreen::local_rect_to_pixels(owned::widescreen::make_layout(float(nScreenWidth),float(nScreenHeight)),r);
}
void CInterface3D::VirtualToScreenLocal(CPoint &p)
{
    CSize size(p.x, p.y); VirtualToScreen(size); p = CPoint(size.cx, size.cy);
}
void CInterface3D::ScreenToVirtualLocal(CPoint &p)
{
    CSize size(p.x, p.y); ScreenToVirtual(size); p = CPoint(size.cx, size.cy);
}
