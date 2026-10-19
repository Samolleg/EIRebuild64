// I3DScreenMainMenu.cpp: implementation of the CI3DScreenMainMenu class.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "CorePerf.h"
#include "GameJournal.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "GameMain.h"
#include "ModRuntime.h"

#include "I3DMain.h"
#include "I3DScreenMainMenu.h"
#include "I3DScreenSaveLoad.h"
#include "I3DScreenOptions.h"
#include "I3DScreenMessageBox.h"
#include "I3DScreenChoosePers.h"

#include "I3DScreenEndGame.h"

#include "I3DMapObjTranslation.h"
#include "I3DScrollText.h"
#include "MapObjectManager.h"
#include "WorldClient.h"
#include "WorldInterface.h"
#include "adFormat.h"
#include "ScenarioTracker.h"
#include "RandomGen.h"
#include "BinkVideo.h"
#include "TextManager.h"
#include "MusicSystem.h"
#include "Renderer.h"
#include "I3DMenuRects.h"
/////////////////////////////////////////////////////////////////////////////////////
const unsigned char VERSION_CHAR_0 = 19;
const unsigned char VERSION_CHAR_3 = 166;
const unsigned char VERSION_CHAR_6 = 14;
const unsigned char VERSION_CHAR_9 = 80;

const unsigned char VERSION_CODE_1 = 65;
const unsigned char VERSION_CODE_4 = 17;
const unsigned char VERSION_CODE_7 = 173;
const unsigned char VERSION_CODE_10 = 19;
/////////////////////////////////////////////////////////////////////////////////////
const int N_COLUMN_ID = 2;
enum
{
    MMBT_NEWGAME = 0,
    MMBT_LOAD,
    MMBT_EXIT,
    MMBT_OPTIONS,
    MMBT_CREDITS,
    MMBT_MULTIPLAYER
};
/////////////////////////////////////////////////////////////////////////////////////
SI3DMenuRects theMenuRects;
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMainMenu::OnActivate()
{
    pI3D->ClearStates();

    IWorldConnector.LMPReloadDatabases(false);

    AddHandler(dbgnew CI3DHandlerSystemKeys(pI3D));

    m_nPrevHitTest = -1;

    SetRect(0, 0, 800, 600);

    LoadSurfaces();

    CI3DPolyArea *pArea;

    pArea = AddArea();
    pArea->MakeRect(theMenuRects.rcMMNewGame);
    pArea = AddArea();
    pArea->MakeRect(theMenuRects.rcMMLoad);
    pArea = AddArea();
    pArea->MakeRect(theMenuRects.rcMMExit);
    pArea = AddArea();
    pArea->MakeRect(theMenuRects.rcMMOptions);
    pArea = AddArea();
    pArea->MakeRect(theMenuRects.rcMMCredits);
    pArea = AddArea();
    pArea->MakeRect(theMenuRects.rcMMMultiplayer);

#ifdef _DEBUG
    // AlexK - draw rects to debug texture loading
    AddTexture("numbers"); // 4444
    AddTexture("boar00");  // DXT1
    // AddTexture(128, 128, TFTID_8888);  //PNT3
    // PasteTexture(textures[2], "footprints", 128, 128, 128, 128, 0, 0);
    AddTexture("footprints");  // DXT3
    AddTexture("footprints1"); // DXT3
    CI3DFigure *pFigure;
    pFigure = AddFigure(); // ltrb tcs ltrb
    pFigure->MakeBar(10, 70, 0, 0, 100, 100, 0, 0, 128, 128);
    pFigure->SetTexture(textures[0]);
    pFigure = AddFigure();
    pFigure->MakeBar(10, 70, 100, 0, 200, 100, 0, 0, 256, 256);
    pFigure->SetTexture(textures[1]);
    pFigure = AddFigure();
    pFigure->MakeBar(10, 70, 200, 0, 300, 100, 0, 0, 128, 128);
    pFigure->SetTexture(textures[2]); // not visible for some reason?
    // pFigure = AddFigure();
    // pFigure->MakeBar(10, 70, 300, 0, 400, 100, 0, 0, 128, 128);
    // pFigure->BindTexture(textures[3]);
#endif
    IWorldClient.BeginStreaming();

    m_fFogDist = IWorld.GetTerrainFogDistance();
    IWorld.SetTerrainFogDistance(6);

    m_isRain = random.Get(0, 1) == 1;
    m_isDay = IWorldServer.GetWorldTime() > 7 && IWorldServer.GetWorldTime() < 21;
    if (IWorldServer.GetWorldTime() < 7 && m_isRain)
        IWorldServer.SetWeather(EW_RAIN, N_WEATHER_CLEAR_TO_RAIN);

    IWorldClient.EnableAmbientSounds();

    IWorldClient.LoadCameraPosition("MainMenu.cam");

    m_fTime = 0;

    theWorld.GetMusicSystem()->SetMainMenuMode();

    // AddHandler( dbgnew CI3DHandlerNewCamera(pI3D) );
    // theCamera.ResetFlag( CAM_CANT_SCROLL );
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMainMenu::OnDeactivate()
{
    IWorld.SetTerrainFogDistance(m_fFogDist);
    IWorldClient.StopAllSounds();
    IWorldClient.DisableAmbientSounds();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMainMenu::OnExitFromModal(DWORD dwRetValue)
{
    IWorldConnector.LMPReloadDatabases(false);
    IWorld.CombatPause(false);
    switch (dwRetValue)
    {
    case 0x00010001:
        // CRAP{
        CBinkVideo::Play(IWorld.GetCDPath() + "Movies\\Exit1.bik", 0, true, false);
        CBinkVideo::Play(IWorld.GetCDPath() + "Movies\\Exit2.bik", 30000, false, true);
        // CRAP}
        NGame::QuitProgram(0);
        break;

    case 0x00020001:
        CBinkVideo::Play(IWorld.GetCDPath() + "Movies\\Intro.bik");
        NGame::bNeedRender = false;
        //		IWorldClient.BeginStreaming();
        IWorldConnector.ReqNewGame();
        break;

    case 0xCCCCCCCC:
        //		theWorld.GetMusicSystem()->SetMainMenuMode();
        IWorldClient.EnableAmbientSounds();
        LoadSurfaces();
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScreenMainMenu::Draw(CRenderer *pRend)
{
    CorePerf::Scope coreTiming(CorePerf::ScreenMenu);
    IWorldClient.Draw();
    pI3D->GetRenderer()->Clear(0, 0, D3DCLEAR_ZBUFFER, 0, 1, 0);
    return CI3DScreen::Draw(pRend);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMainMenu::OnIdle()
{
    CI3DScreen::OnIdle();
    int nTimeInHour = int(IWorldServer.GetWorldTime());

    if (nTimeInHour != int(m_fTime) && m_isRain)
    {
        if (nTimeInHour == 0)
            IWorldServer.SetWeather(EW_RAIN, N_WEATHER_CLEAR_TO_RAIN);
        if (nTimeInHour == 7)
            IWorldServer.SetWeather(EW_CLEAR, N_WEATHER_RAIN_TO_CLEAR);
    }

    if (!m_isDay && m_isRain && nTimeInHour < 6 && theRenderer.IsActive())
    {
        static DWORD dwBorder = 0;
        DWORD dwCurTime = GetTickCount();
        if (dwCurTime > dwBorder)
        {
            dwBorder = dwCurTime + 100;
            if (random.Get(40) == 4)
            {
                CPlot3D ptFrom;
                CPlot3D ptTo;

                // ptFrom.x = random.GetFloat( -15, 15 ) + 10;
                // ptFrom.y = random.GetFloat( -15, 15 ) + 55;
                ptFrom.x = random.GetFloat(0, 20);
                ptFrom.y = random.GetFloat(0, 60);
                ptFrom.z = 17.0f;

                ptTo.x = ptFrom.x + random.GetFloat(-2, 2);
                ptTo.y = ptFrom.y + random.GetFloat(-2, 2);
                ptTo.z = 1.0f;
                IWorld.InvokeLightning(ptFrom, ptTo);
            }
        }
    }

    m_fTime = IWorldServer.GetWorldTime();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMainMenu::OnLButtonDown(UINT nFlags, CPoint point)
{
    CI3DScreen *pVideoScr = NULL;
    int nHit = HitTest(point);
    JournalHit(nHit);
    if (((nHit == MMBT_NEWGAME || nHit == MMBT_LOAD) && !ModRuntime::SinglePlayerAllowed()) ||
        (nHit == MMBT_MULTIPLAYER && !ModRuntime::MultiplayerAllowed()))
        return;

    switch (nHit)
    {
    case MMBT_NEWGAME: {
        pI3D->BeginFX("buttons\\menu\\ok.wav");
        CI3DLevelMessageBox *pScr = dbgnew CI3DLevelMessageBox(pI3D, "option_difficulty", "",
                                                               0x00020000);
        pI3D->DoModal(pScr);
    }
    break;

    case MMBT_LOAD: {
        pI3D->BeginFX("buttons\\menu\\ok.wav");
        CI3DScreenSaveLoad *pScreenLoad = dbgnew CI3DScreenSaveLoad(pI3D);
        pScreenLoad->SetParams(false, false);
        pI3D->DoModal(pScreenLoad);
    }
    break;

    case MMBT_EXIT:
        // CRAP{
        CBinkVideo::Play(IWorld.GetCDPath() + "Movies\\Exit1.bik", 0, true, false);
        CBinkVideo::Play(IWorld.GetCDPath() + "Movies\\Exit2.bik", 30000, false, true);
        // CRAP}
        NGame::QuitProgram(0);
        break;

    case MMBT_OPTIONS: {
        pI3D->BeginFX("buttons\\menu\\ok.wav");
        CI3DScreenOptions *pScreen = dbgnew CI3DScreenOptions(pI3D);
        pScreen->SetParams(false);
        pI3D->DoModal(pScreen);
    }
    break;

    case MMBT_CREDITS: {
        IWorldClient.StopAllSounds(false);
        IWorldClient.DisableAmbientSounds();
        pI3D->BeginFX("buttons\\menu\\ok.wav");
        RemoveAllSurfaces();
        CI3DScreenEndGame *pScreen = dbgnew CI3DScreenEndGame(pI3D, true, "Titles");
        pI3D->DoModal(pScreen, false);
    }
    break;

#ifndef __SINGLEONLY__
    case MMBT_MULTIPLAYER: {
        CString szMsg;
        IWorld.GetTextManager()->GetSystemString("reserved_04", szMsg);
        theInterface.DrawTextOverFrame(szMsg);

        IWorld.CombatPause(true);

        pI3D->BeginFX("buttons\\menu\\ok.wav");
        CI3DScreenChoosePers *pScreen = dbgnew CI3DScreenChoosePers(pI3D);
        pI3D->DoModal(pScreen);
    }
    break;
#else
    case MMBT_MULTIPLAYER: {
        pI3D->BeginFX("buttons\\menu\\no.wav");
        CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, "reserved_10", "reserved_11",
                                                                 0x00050000, MBF_OK);
        pI3D->DoModal(pScr);
    }

    break;
#endif // __SINGLEONLY__
    }
    CI3DScreen::OnLButtonDown(nFlags, point);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMainMenu::OnMouseMove(UINT nFlags, CPoint point)
{
    CMapObjectManager *lpMapObjectManager = (CMapObjectManager *)theWorld.GetMapObjectManager();
    CUnit *lpUnit = (CUnit *)lpMapObjectManager->Find(N_COLUMN_ID);
    if (lpUnit)
    {
        int nHitTest = HitTest(point);
        if (((nHitTest == MMBT_NEWGAME || nHitTest == MMBT_LOAD) && !ModRuntime::SinglePlayerAllowed()) ||
            (nHitTest == MMBT_MULTIPLAYER && !ModRuntime::MultiplayerAllowed()))
            nHitTest = -1;
        CString szUnitMaskName = lpUnit->GetMaskName();
        if (nHitTest != m_nPrevHitTest)
            switch (nHitTest)
            {
            case MMBT_NEWGAME: {
                const SADEntry *pEntry = lpUnit->GetSpecialAnimationEntry(1);
                lpUnit->SetNextAnimation(pEntry, TRUE);
            }
                pI3D->BeginFX("buttons\\menu\\stone.wav");
                break;
            case MMBT_LOAD: {
                const SADEntry *pEntry = lpUnit->GetSpecialAnimationEntry(3);
                lpUnit->SetNextAnimation(pEntry, TRUE);
            }
                pI3D->BeginFX("buttons\\menu\\stone.wav");
                break;
            case MMBT_EXIT: {
                const SADEntry *pEntry = lpUnit->GetSpecialAnimationEntry(5);
                lpUnit->SetNextAnimation(pEntry, TRUE);
            }
                pI3D->BeginFX("buttons\\menu\\stone.wav");
                break;
            case MMBT_OPTIONS: {
                const SADEntry *pEntry = lpUnit->GetSpecialAnimationEntry(7);
                lpUnit->SetNextAnimation(pEntry, TRUE);
            }
                pI3D->BeginFX("buttons\\menu\\stone.wav");
                break;

            case MMBT_CREDITS: {
                const SADEntry *pEntry = lpUnit->GetSpecialAnimationEntry(11);
                lpUnit->SetNextAnimation(pEntry, TRUE);
            }
                pI3D->BeginFX("buttons\\menu\\stone.wav");
                break;
                /*
        case MMBT_CUTSCENES:
                {
                        const SADEntry *pEntry = lpUnit->GetSpecialAnimationEntry( 15 );
                        lpUnit->SetNextAnimation( pEntry, TRUE );
                }
                pI3D->BeginFX( "buttons\\menu\\stone.wav" );
                break;
        case MMBT_HALLOFFAME:
                {
                        const SADEntry *pEntry = lpUnit->GetSpecialAnimationEntry( 11 );
                        lpUnit->SetNextAnimation( pEntry, TRUE );
                }
                pI3D->BeginFX( "buttons\\menu\\stone.wav" );
                break;
                */
            case MMBT_MULTIPLAYER: {
                const SADEntry *pEntry = lpUnit->GetSpecialAnimationEntry(9);
                lpUnit->SetNextAnimation(pEntry, TRUE);
            }
                pI3D->BeginFX("buttons\\menu\\stone.wav");
                break;

            default: {
                SADEntry *lpEntry = lpMapObjectManager->GetAnimationEntry(
                    szUnitMaskName,
                    ADB_STATE_ATTACK | ADB_STAGE_CYCLE | ADB_ACTION_IDLE | ADB_MODIFIER_IDLE_STAY);
                lpUnit->SetNextAnimation(lpEntry, TRUE);
                pI3D->BeginFX("buttons\\menu\\stone.wav");
            }
            break;
            }
        m_nPrevHitTest = nHitTest;
    }
    CI3DScreen::OnMouseMove(nFlags, point);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMainMenu::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CI3DScreen::OnKeyDown(nChar, nRepCnt, nFlags);
    if (nChar == VK_ESCAPE)
    {
        CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, "quit", "quit_msg",
                                                                 0x00010000);
        pI3D->DoModal(pScr);
    }

    switch (NKeyMap::keyMap[nFlags & 0x1ff])
    {
    case KA_QLOAD:
        CI3DScreenSaveLoad::QuickLoad("saveq");
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMainMenu::LoadSurfaces()
{
    CI3DSurface *pSurface;
    pSurface = dbgnew CI3DSurface(pI3D, 100, 480, 780, 600);
#ifndef NOGETDC
    pSurface->GetDC();
    pSurface->Clear();
    CString szVersion;
    theWorld.GetTextManager()->GetSystemString("version_name", szVersion);
    pSurface->DrawText(szVersion, CRect(0, 0, 675, 120), 2, RGB(228, 215, 167), DT_RIGHT);
    pSurface->ReleaseDC();
#endif // NOGETDC
    AddSurface(pSurface);
}
/////////////////////////////////////////////////////////////////////////////////////
const int MB_FRAME_WIDTH = 15;
CI3DLevelMessageBox::CI3DLevelMessageBox(CInterface3D *pI, LPCTSTR pszTitleID, LPCTSTR pszTextID,
                                         DWORD dwPar)
    : CI3DScreenMessageBox(pI, pszTitleID, pszTextID, dwPar, MBF_OK | MBF_CANCEL)
{
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DLevelMessageBox::OnActivate()
{
    const bool spellAddonDifficulty = NKeyMap::SpellAddonDifficultyLevels();
    AddTexture("saveload");

    CI3DSurface *pSurf;
    AddSurface(pSurf = dbgnew CI3DSurface(pI3D, 200, 100, 600, 500));

    int nSurfaceShift = 100;
    int nMBHeight = 100, nTop = 225;

    pSurf->GetDC();

    int nTitleHeight = pSurf->DrawText(szTitle, CRect(MB_FRAME_WIDTH, 0, 400 - MB_FRAME_WIDTH, 200),
                                       2, RGB(0, 0, 0), DT_CENTER | DT_WORDBREAK | DT_CALCRECT);

    CString szDamnCasualHelp;
    int nHelpHeight = 0;
    if (theWorld.GetTextManager()->GetText("tip option_difficulty"))
    {
        theWorld.GetTextManager()->GetUntilNextCommand(szDamnCasualHelp);
        if (!spellAddonDifficulty)
            szDamnCasualHelp.TrimRight("\r\n");
        nHelpHeight = pSurf->DrawText(szDamnCasualHelp,
                                      CRect(MB_FRAME_WIDTH, 0, 400 - MB_FRAME_WIDTH, 300), 1,
                                      RGB(0, 0, 0), DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
    }

    const int nChoiceHeight = spellAddonDifficulty ? 48 : 72;
    nMBHeight = nTitleHeight + nChoiceHeight + 4 * MB_FRAME_WIDTH + nHelpHeight;
    nTop = (550 - nMBHeight) / 2;

    pSurf->DrawText(szTitle,
                    CRect(MB_FRAME_WIDTH, nTop - nSurfaceShift + MB_FRAME_WIDTH,
                          400 - MB_FRAME_WIDTH, 450),
                    2, RGB(255, 179, 49), DT_CENTER | DT_WORDBREAK);

    pSurf->DrawText(szDamnCasualHelp,
                    CRect(MB_FRAME_WIDTH,
                          nTop - nSurfaceShift + nChoiceHeight + 3 * MB_FRAME_WIDTH + nTitleHeight,
                          400 - MB_FRAME_WIDTH, 450),
                    1, RGB(228, 215, 167), DT_LEFT | DT_WORDBREAK);

    CString szEasy, szNormal, szHard;
    theWorld.GetTextManager()->GetSystemString("difficulty_norm", szNormal);
    theWorld.GetTextManager()->GetSystemString("difficulty_easy", szEasy);
    theWorld.GetTextManager()->GetSystemString("difficulty_hard", szHard);
    int nTempWidth = Max(pI3D->TextSize(szNormal, 2).cx,
        Max(pI3D->TextSize(szEasy, 2).cx, pI3D->TextSize(szHard, 2).cx));
    // ASI measures only easy/normal and averages their widths in mode 1.
    int nWidth = spellAddonDifficulty
        ? ((pI3D->TextSize(szEasy, 2).cx + pI3D->TextSize(szNormal, 2).cx) / 2 + 30) / 2
        : (nTempWidth + 30) / 2;

    int nLevel1 = nTop + 2 * MB_FRAME_WIDTH + nTitleHeight;
    pSurf->DrawText(szEasy, CRect(230 - nWidth, nLevel1 - nSurfaceShift, 400 - MB_FRAME_WIDTH, 450),
                    2, RGB(228, 215, 167), DT_LEFT);
    pSurf->DrawText(spellAddonDifficulty ? szHard : szNormal,
                    CRect(230 - nWidth, nLevel1 - nSurfaceShift + (spellAddonDifficulty ? 48 : 24),
                          400 - MB_FRAME_WIDTH, 450), 2, RGB(228, 215, 167), DT_LEFT);
    pSurf->DrawText(spellAddonDifficulty ? szNormal : szHard,
                    CRect(230 - nWidth, nLevel1 - nSurfaceShift + (spellAddonDifficulty ? 24 : 48),
                          400 - MB_FRAME_WIDTH, 450), 2, RGB(228, 215, 167), DT_LEFT);
    pSurf->ReleaseDC();

    nDifficulty = spellAddonDifficulty ? 1 : 0;

    CI3DFigure *pFig = AddFigure();
    pFig->MakeBar(2.5f, 100, 400 - nWidth, nLevel1, 421 - nWidth, nLevel1 + 21, 209, 55, 230, 76);

    pFig = AddFigure();
    pFig->MakeBar(2.5f, 100, 400 - nWidth, nLevel1 + 24, 421 - nWidth, nLevel1 + 45, 209, 55, 230,
                  76);
    if (spellAddonDifficulty)
        pFig->SetUShift(-23 / 256.f);
    pFig = AddFigure();
    pFig->MakeBar(2.5f, 100, 400 - nWidth, nLevel1 + 48, 421 - nWidth, nLevel1 + 69, 209, 55, 230, 76);
    if (!spellAddonDifficulty)
        pFig->SetUShift(-23 / 256.f);

    pFig = AddFigure();
    pFig->MakeBar(3, 100, 200, nTop, 600, nTop + nMBHeight);
    pFig->SetTexture(NULL);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));

    pFig = AddFigure();
    pFig->MakeBarFrame(3, 100, 195, nTop - 5, 605, nTop + nMBHeight + 5, 5, 2, 254, 180, 249);

    pFig = AddFigure(); // ok
    pFig->MakeBar(3, 100, 263, nTop + nMBHeight + 26, 337, nTop + nMBHeight + 74, 81, 2, 155, 50);

    pFig = AddFigure(); // cancel
    pFig->MakeBar(3, 100, 463, nTop + nMBHeight + 26, 537, nTop + nMBHeight + 74, 160, 2, 234, 50);

    CI3DPolyArea *pArea;
    pArea = AddArea();
    pArea->MakeRect(263, nTop + nMBHeight + 26, 337, nTop + nMBHeight + 74); // ok
    pArea->SetToolTip(50100);

    pArea = AddArea();
    pArea->MakeRect(463, nTop + nMBHeight + 26, 537, nTop + nMBHeight + 74); // cancel
    pArea->SetToolTip(50101);

    pArea = AddArea();
    pArea->MakeRect(400 - nWidth, nLevel1, 421 - nWidth, nLevel1 + 21);
    pArea = AddArea();
    const int nSecondOffset = spellAddonDifficulty ? 48 : 24;
    const int nThirdOffset = spellAddonDifficulty ? 24 : 48;
    pArea->MakeRect(400 - nWidth, nLevel1 + nSecondOffset, 421 - nWidth,
                    nLevel1 + nSecondOffset + 21);
    pArea = AddArea();
    pArea->MakeRect(400 - nWidth, nLevel1 + nThirdOffset, 421 - nWidth,
                    nLevel1 + nThirdOffset + 21);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DLevelMessageBox::OnLButtonDown(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    JournalHit(ht);
    switch (ht)
    {
    case 0:
        theSettings.nDifficultyLevel = nDifficulty;
        pI3D->BeginFX("buttons\\messbox\\ok.wav");
        pI3D->ExitFromModal(dwParam | 0x00000001);
        break;
    case 1:
        pI3D->BeginFX("buttons\\messbox\\cancel.wav");
        pI3D->ExitFromModal(dwParam | 0x00000002);
        break;
    case 2:
    case 3:
    case 4:
        {
            const bool spellAddonDifficulty = NKeyMap::SpellAddonDifficultyLevels();
            const int nSelected = spellAddonDifficulty && ht >= 3 ? 5 - ht : ht - 2;
            for (int i = 0; i < 3; ++i)
                figures[i]->SetUShift(i == nSelected ? -23 / 256.f : 0);
            nDifficulty = spellAddonDifficulty ? (ht == 2 ? 2 : ht - 3) : 4 - ht;
        }
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DLevelMessageBox::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CI3DScreen::OnKeyDown(nChar, nRepCnt, nFlags);
    if (GetProcessed())
        return;
    switch (nChar)
    {
    case VK_ESCAPE:
        pI3D->BeginFX("buttons\\messbox\\cancel.wav");
        pI3D->ExitFromModal(dwParam | 0x00000002);
        SetProcessed();
        break;
    case 13:
        theSettings.nDifficultyLevel = nDifficulty;
        pI3D->BeginFX("buttons\\messbox\\ok.wav");
        pI3D->ExitFromModal(dwParam | 0x00000001);
        SetProcessed();
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
