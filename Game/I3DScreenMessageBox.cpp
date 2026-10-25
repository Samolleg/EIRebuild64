#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenMessageBox.h"
#include "WorldInterface.h"
#include "TextManager.h"
#include "I3DMain.h"
#include "I3DScrollBar.h"

// CRAP{
#include "MoviePlayback.h"
#include "GameMain.h"
// CRAP}
/////////////////////////////////////////////////////////////////////////////////////
const int MB_FRAME_WIDTH = 15;
/////////////////////////////////////////////////////////////////////////////////////
CI3DScreenMessageBox::CI3DScreenMessageBox(CInterface3D *pI, LPCTSTR pszTitleID, LPCTSTR pszTextID,
                                           DWORD dwPar, DWORD _dwFlags, int nFormat)
    : CI3DScreen(pI)
    , dwParam(dwPar)
    , dwFlags(_dwFlags)
    , nTextFormat(nFormat)
{
    CString szTmp;
    CTextManager *pTxtMng = IWorld.GetTextManager();
    pTxtMng->GetSystemString(pszTitleID, szTmp);
    SetTitle(szTmp);
    if (dwFlags & MBF_ENTER)
        pszTextBuffer = pszTextID;
    else
    {
        pszTextBuffer = 0;
        pTxtMng->GetSystemString(pszTextID, szTmp);
        SetText(szTmp);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMessageBox::OnActivate()
{
    AddTexture("saveload");

    CI3DSurface *pSurf;
    AddSurface(pSurf = dbgnew CI3DSurface(pI3D, 200, 50, 600, 500));

    int nMBHeight = 100, nTop = 225;
#ifndef NOGETDC
    pSurf->GetDC();

    int nFont = (dwParam == 0xFFFF0000) ? 1 : 2;
    int nTitleHeight = pSurf->DrawText(szTitle, CRect(MB_FRAME_WIDTH, 0, 400 - MB_FRAME_WIDTH, 200),
                                       2, RGB(0, 0, 0), DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
    int nTextHeight = pSurf->DrawText(szText, CRect(MB_FRAME_WIDTH, 0, 400 - MB_FRAME_WIDTH, 400),
                                      nFont, RGB(0, 0, 0), DT_CENTER | DT_WORDBREAK | DT_CALCRECT);

    if (dwFlags & MBF_ENTER)
        nTextHeight = pI3D->TextSize("A", nFont).cy;

    nMBHeight = nTitleHeight + nTextHeight + 4 * MB_FRAME_WIDTH;
    nTop = (550 - nMBHeight) / 2;

    pSurf->DrawText(szTitle,
                    CRect(MB_FRAME_WIDTH, nTop - 50 + MB_FRAME_WIDTH, 400 - MB_FRAME_WIDTH, 450), 2,
                    RGB(255, 179, 49), DT_CENTER | DT_WORDBREAK);

    if (dwFlags & MBF_ENTER)
    {
        pEdit = dbgnew CI3DEditBox(pI3D,
                                   CRect(200 + MB_FRAME_WIDTH,
                                         nTop + 2 * MB_FRAME_WIDTH + nTitleHeight,
                                         600 - MB_FRAME_WIDTH,
                                         nTop + 2 * MB_FRAME_WIDTH + nTitleHeight + nTextHeight));
        pEdit->SetParams(EBF_ALL, false, nFont, 50, RGB(228, 215, 167), RGB(255, 179, 49));
        pEdit->SetString(pszTextBuffer);
        AddObject(pEdit);
    }
    else
    {
        pSurf->DrawText(szText,
                        CRect(MB_FRAME_WIDTH, nTop - 50 + 2 * MB_FRAME_WIDTH + nTitleHeight,
                              400 - MB_FRAME_WIDTH, 450),
                        nFont, RGB(228, 215, 167), nTextFormat | DT_WORDBREAK);
    }

    pSurf->ReleaseDC();
#endif // NOGETDC

    CI3DFigure *pFig = AddFigure();
    pFig->MakeBar(3, 100, 200, nTop, 600, nTop + nMBHeight);
    pFig->SetTexture(NULL);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));

    pFig = AddFigure();
    pFig->MakeBarFrame(3, 100, 195, nTop - 5, 605, nTop + nMBHeight + 5, 5, 2, 254, 180, 249);

    if ((dwFlags & MBF_CANCEL) && (dwFlags & MBF_OK))
    {
        pFig = AddFigure(); // ok
        pFig->MakeBar(3, 100, 263, nTop + nMBHeight + 26, 337, nTop + nMBHeight + 74, 81, 2, 155,
                      50);

        pFig = AddFigure(); // cancel
        pFig->MakeBar(3, 100, 463, nTop + nMBHeight + 26, 537, nTop + nMBHeight + 74, 160, 2, 234,
                      50);

        CI3DPolyArea *pArea;
        pArea = AddArea();
        pArea->MakeRect(263, nTop + nMBHeight + 26, 337, nTop + nMBHeight + 74); // ok
        pArea->SetToolTip(50100);

        pArea = AddArea();
        pArea->MakeRect(463, nTop + nMBHeight + 26, 537, nTop + nMBHeight + 74); // cancel
        pArea->SetToolTip(50101);
    }
    else if (dwFlags & MBF_OK)
    {
        pFig = AddFigure(); // ok
        pFig->MakeBar(3, 100, 363, nTop + nMBHeight + 26, 437, nTop + nMBHeight + 74, 81, 2, 155,
                      50);

        CI3DPolyArea *pArea;
        pArea = AddArea();
        pArea->MakeRect(363, nTop + nMBHeight + 26, 437, nTop + nMBHeight + 74); // ok
        pArea->SetToolTip(50100);
    }
    else
    {
        pFig = AddFigure(); // cancel
        pFig->MakeBar(3, 100, 363, nTop + nMBHeight + 26, 437, nTop + nMBHeight + 74, 160, 2, 234,
                      50);

        CI3DPolyArea *pArea;
        pArea = AddArea();
        pArea = AddArea();
        pArea->MakeRect(363, nTop + nMBHeight + 26, 437, nTop + nMBHeight + 74); // ok
        pArea->SetToolTip(50101);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMessageBox::OnDeactivate()
{
    RemoveAllFigures();
    RemoveAllSurfaces();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMessageBox::OnLButtonDown(UINT nFlags, CPoint point)
{
    CI3DScreen::OnLButtonDown(nFlags, point);
    int ht = HitTest(point);

    // CRAP{
    if (ht == 0 && dwParam == 0xFFFF0000)
    {
        Video::Movie::Play(IWorld.GetCDPath() + "Movies\\Exit1.bik", 0, true, false);
        Video::Movie::Play(IWorld.GetCDPath() + "Movies\\Exit2.bik", 30000, false, true);
        NGame::QuitProgram(0);
        return;
    }
    // CRAP}

    if (ht == 0)
        pI3D->BeginFX("buttons\\messbox\\ok.wav");
    else if (ht == 1)
        pI3D->BeginFX("buttons\\messbox\\cancel.wav");

    if (ht == 0 && (dwFlags & MBF_ENTER))
    {
        CString szRes(pEdit->GetString());
        pszTextBuffer = szRes.GetString();
    }

    if (ht > -1)
        pI3D->ExitFromModal(dwParam | (ht + 1));
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMessageBox::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
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
        // CRAP{
        if (dwParam == 0xFFFF0000)
        {
            Video::Movie::Play(IWorld.GetCDPath() + "Movies\\Exit1.bik", 0, true, false);
            Video::Movie::Play(IWorld.GetCDPath() + "Movies\\Exit2.bik", 30000, false, true);
            NGame::QuitProgram(0);
        }
        else
        // CRAP}
        {
            if (dwFlags & MBF_ENTER)
            {
                CString szRes(pEdit->GetString());
                pszTextBuffer = szRes.GetString();
            }
            pI3D->BeginFX("buttons\\messbox\\ok.wav");
            pI3D->ExitFromModal(dwParam | 0x00000001);
        }
        SetProcessed();
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
