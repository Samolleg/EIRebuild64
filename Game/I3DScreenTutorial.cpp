#include "StdAfx.h"
#include "WideScreen.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenTutorial.h"
#include "WorldClient.h"
#include "TextManager.h"
#include "I3DMain.h"
#include "KeyMap.h"
/////////////////////////////////////////////////////////////////////////////////////
const int TUT_HEADER_HEIGHT = 50;
const int TUT_BUTTONS_HEIGHT = 70;

const int FRAME_R_FROM = 80;
const int FRAME_G_FROM = 80;
const int FRAME_B_FROM = 80;
const int FRAME_A_FROM = 0;

const int FRAME_R_TO = 255;
const int FRAME_G_TO = 0;
const int FRAME_B_TO = 0;
const int FRAME_A_TO = 255;
using namespace std;
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenTutorial::OnActivate()
{
    AddTexture("saveload");
    AddTexture(256, 256);

    CTextManager *pTxtMng = theWorld.GetTextManager();
    if (!pTxtMng->GetText(CString("tutorial ") + mszTutorial))
    {
        pI3D->ExitFromModal();
        return;
    }
    pTxtMng->GetLine(mszHeader);
    CString szTmp;
    while (pTxtMng->FindNextCommand("#screen"))
    {
        pTxtMng->SkipLine();
        pTxtMng->GetLine(szTmp);
        mszPages.Add(szTmp);
    }
    if (!mszPages.GetSize())
    {
        pI3D->ExitFromModal();
        return;
    }

    if (pTxtMng->GetText(CString("tutor ") + mszHeader))
    {
        pTxtMng->GetUntilNextCommand(mszHeader);
        mszHeader.TrimRight("\r\n");
    }
    for (int i = 0; i < mszPages.GetSize(); i++)
        if (pTxtMng->GetText(CString("tutor ") + mszPages[i]))
        {
            pTxtMng->GetUntilNextCommand(mszPages[i]);
            mszPages[i].TrimRight("\r\n");
        }

    if (mnCurrentPage < 0)
        mnCurrentPage = 0;
    if (mnCurrentPage >= mszPages.GetSize())
        mnCurrentPage = mszPages.GetSize() - 1;

    fTime = 0;
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHAREF, 1);

    RedrawPage();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenTutorial::OnDeactivate()
{
    mpRects.clear();
    mpIcons.clear();

    RemoveAllSurfaces();
    RemoveAllAreas();
    RemoveAllFigures();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenTutorial::OnLButtonDown(UINT nFlags, CPoint point)
{
    CI3DScreen::OnLButtonDown(nFlags, point);
    int ht = HitTest(point);

    switch (ht)
    {
    case 0:
        if (mnCurrentPage > 0)
        {
            pI3D->BeginFX("buttons\\tutorial\\next.wav");
            mnCurrentPage--;
            RedrawPage();
        }
        break;
    case 1:
        Exit();
        break;
    case 2:
        if (mnCurrentPage < mszPages.GetSize() - 1)
        {
            pI3D->BeginFX("buttons\\tutorial\\next.wav");
            mnCurrentPage++;
            RedrawPage();
        }
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenTutorial::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CI3DScreen::OnKeyDown(nChar, nRepCnt, nFlags);
    switch (nChar)
    {
    case VK_ESCAPE:
        Exit();
        SetProcessed();
        return;
    case 13:
        if (mnCurrentPage < mszPages.GetSize() - 1)
        {
            pI3D->BeginFX("buttons\\tutorial\\next.wav");
            mnCurrentPage++;
            RedrawPage();
        }
        else
            Exit();
        SetProcessed();
        return;
    }
    switch (NKeyMap::keyMap[nFlags & 0x1ff])
    {
    case KA_TUTORIAL_SCRIPT:
        Exit();
        SetProcessed();
        return;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenTutorial::RedrawPage()
{
    mpRects.clear();
    mpIcons.clear();

    RemoveAllSurfaces();
    RemoveAllAreas();
    RemoveAllFigures();

    CTextManager *pTxtMng = theWorld.GetTextManager();
    CStringArray szTmpArray;

    if (!pTxtMng->GetText(CString("tutorial ") + mszTutorial))
    {
        pI3D->ExitFromModal();
        return;
    }
    int nCount = mnCurrentPage + 1;
    while (nCount--)
    {
        if (!pTxtMng->FindNextCommand("#screen"))
        {
            pI3D->ExitFromModal();
            return;
        }
        pTxtMng->SkipLine();
    }

    pTxtMng->SkipLine();

    LPCTSTR pszPos = pTxtMng->GetPos();

    CRect rcWindow(200, 50, 600, 560);
    bool bFixedHeight = false;
    if (pTxtMng->FindNextCommand("#position"))
    {
        pTxtMng->GetStringArray(szTmpArray);
        if (szTmpArray.GetSize() > 3)
        {
            rcWindow.left = atoi(szTmpArray[1]);
            rcWindow.top = atoi(szTmpArray[2]);
            rcWindow.right = atoi(szTmpArray[3]);
            if (szTmpArray.GetSize() > 4)
            {
                rcWindow.bottom = atoi(szTmpArray[4]);
                bFixedHeight = true;
            }
            else
                rcWindow.bottom = 1;
            if (rcWindow.Width() < 300)
                rcWindow.right = rcWindow.left + 300;
            rcWindow.bottom = rcWindow.top + 110 + rcWindow.bottom * 16;
            if (rcWindow.Height() > 600)
                rcWindow.bottom = rcWindow.top + 600;
        }
    }
    pTxtMng->SetPos(pszPos);

    CI3DFigure *pFig;

    pFig = AddFigure();
    pFig->SetTexture(NULL);
    pFig->UseDefaultColors(TRUE);
    if (mbScript)
        pFig->SetDefaultColor(RGBA_MAKE(40, 35, 19, 255));
    else
        pFig->SetDefaultColor(RGBA_MAKE(33, 27, 17, 255));

    pFig = AddFigure();
    if (mbScript)
        pFig->SetVShift(7 / 256.f);

    pFig = AddFigure();
    if (mnCurrentPage <= 0)
        pFig->Show(FALSE);
    pFig->SetAmbient(.6f, .6f, .6f);
    pFig = AddFigure();
    if (mnCurrentPage < mszPages.GetSize() - 1)
        pFig->SetAmbient(.6f, .6f, .6f);
    pFig = AddFigure();
    if (mnCurrentPage >= mszPages.GetSize() - 1)
        pFig->Show(FALSE);

    CI3DSurface *pSurf;
    CRect rcOut;
    CString szOut;

    AddSurface(pSurf = dbgnew CI3DSurface(pI3D, rcWindow.left, rcWindow.top, rcWindow.right,
                                          rcWindow.top + 55));
#ifndef NOGETDC
    pSurf->GetDC();
    pSurf->Clear();
    rcOut = CRect(0, 8, rcWindow.Width(), 30);
    pSurf->DrawText(mszPages[mnCurrentPage], rcOut, 2, RGB(255, 245, 130), DT_CENTER);
    rcOut = CRect(0, 30, rcWindow.Width(), 50);
    szOut.Format("%s (%d/%d)", mszHeader, mnCurrentPage + 1, mszPages.GetSize());
    pSurf->DrawText(szOut, rcOut, 1, RGB(255, 245, 130), DT_CENTER);
    pSurf->ReleaseDC();
#endif // NOGETDC

    vector<pair<string, string>> texts;
    while (pTxtMng->FindNextCommand())
    {
        pTxtMng->GetStringArray(szTmpArray);
        if (szTmpArray[0].CompareNoCase("#screen") == 0)
            break;
        else if (szTmpArray[0].CompareNoCase("#rectangle") == 0 && szTmpArray.GetSize() > 4)
        {
            // rectangle
            int l, t, r, b;
            l = atoi(szTmpArray[1]);
            t = atoi(szTmpArray[2]);
            r = atoi(szTmpArray[3]);
            b = atoi(szTmpArray[4]);

            if (mbScript)
            {
                CString pageKey; pageKey.Format("%s:%d", (LPCSTR)mszTutorial, mnCurrentPage);
                const char *left[] = { "zt9:0", "zt12:0", "zt12:1", "zt13:0", "zt14:0", "zt14:2", "zt17:0", "zt20:0", "zt22:0" };
                const char *right[] = { "zt3:2", "zt4:0", "zt4:1", "zt7:0", "zt10:0", "zt10:1", "zt18:0" };
                for (const char *key : left) if (pageKey == key) { l -= WideScreen::VirtualShift(); r -= WideScreen::VirtualShift(); }
                for (const char *key : right) if (pageKey == key) { l += WideScreen::VirtualShift(); r += WideScreen::VirtualShift(); }
            }

            CI3DFigure *pFig = AddFigure();
            pFig->MakeBarFrame(3, 100, l, t, r, b, 5);
            // pFig->MakeBar( 3, 100, l, t, r, b );
            pFig->SetTexture(0);
            pFig->UseDefaultColors(TRUE);
            pFig->SetDefaultColor(RGBA_MAKE(255, 0, 0, 130));
            mpRects.push_back(pFig);
        }
        else if (szTmpArray[0].CompareNoCase("#text") == 0)
        {
            std::string szIcon;
            if (szTmpArray.GetSize() > 1)
                szIcon = szTmpArray[1];
            pTxtMng->GetStringArray(szTmpArray);
            if (szTmpArray.GetSize() > 0)
                texts.push_back(make_pair(string(szTmpArray[0]), szIcon));
        }
    }
    int nCurPos;
    int nIcon = 0;

    int nFirstPass = bFixedHeight ? 1 : 0;
    // two passes for calculating height of text
    for (int nPass = nFirstPass; nPass < 2; nPass++)
    {
        AddSurface(pSurf = dbgnew CI3DSurface(pI3D, rcWindow.left, rcWindow.top + 55,
                                              rcWindow.right, rcWindow.bottom - 55));
        nCurPos = 0;
        rcOut = CRect(15, 0, rcWindow.Width() - 30, 16);
#ifndef NOGETDC
        pSurf->GetDC();
        pSurf->Clear();
        for (auto it = texts.begin(); it != texts.end(); ++it)
        {
            bool bIcon = false;
            if (!it->second.empty() && nIcon < 16)
            {
                if (nPass == 0)
                {
                    // icon
                    int x = (nIcon / 4) * 64;
                    int y = (nIcon % 4) * 64;
                    PasteTexture(textures[1], CString("tutorial_") + it->second.c_str(), 64, 64, 64,
                                 64, x, y);

                    CI3DFigure *pFig = AddFigure();
                    pFig->MakeBar(3, 100, 0, 0, 64, 64, 0, 0, 64, 64);
                    pFig->SetUShift(x / 256.f);
                    pFig->SetVShift((64 + y) / 256.f);
                    pFig->SetRelativeToScreen(rcWindow.left + 15 + 32,
                                              rcWindow.top + nCurPos + 55 + 40, 3);
                    pFig->SetTexture(textures[1]);
                    mpIcons.push_back(pFig);
                    nIcon++;
                }
                rcOut.left = 90;
                bIcon = true;
            }
            else
                rcOut.left = 15;
            rcOut.top = nCurPos;
            rcOut.bottom = nCurPos + 16;

            szOut.Empty();
            if (pTxtMng->GetText(CString("tutor ") + it->first.c_str()))
                pTxtMng->GetUntilNextCommand(szOut);
            szOut.TrimRight("\n\r");
            int nPix;
            nPix = pSurf->DrawMultiText(szOut, rcOut, 100, 1, RGB(255, 249, 187),
                                        DT_WORDBREAK | DT_EXPANDTABS);
            nPix *= 16;
            if (bIcon)
                nCurPos += Max(nPix, 80);
            else
                nCurPos += nPix;
        }
        pSurf->ReleaseDC();
#endif // NOGETDC

        if (nPass == 0)
        {
            RemoveSurface(1);
            rcWindow.bottom = rcWindow.top + nCurPos + 110;
        }
    }

    figures[0]->MakeBar(3, 100, rcWindow.left, rcWindow.top, rcWindow.right, rcWindow.bottom);
    figures[1]->MakeBarFrame(3, 100, rcWindow.left - 5, rcWindow.top - 5, rcWindow.right + 5,
                             rcWindow.bottom + 5, 5, 2, 245, 180, 240);

    CRect rcButton(0, rcWindow.bottom - 30, 50, rcWindow.bottom - 10);
    int nBorder = (rcWindow.Width() - 3 * rcButton.Width()) / 6;
    CPoint shift(rcWindow.left + nBorder, 0);
    rcButton += shift;
    shift.x = rcButton.Width() + 2 * nBorder;

    CI3DPolyArea *pArea;
    pArea = AddArea();
    pArea->MakeRect(rcButton);
    figures[2]->MakeBar(3, 100, rcButton.left, rcButton.top, rcButton.right, rcButton.bottom, 2, 51,
                        52, 71);
    rcButton += shift;

    pArea = AddArea();
    pArea->MakeRect(rcButton);
    pArea->SetToolTip(30100);
    figures[3]->MakeBar(3, 100, rcButton.left, rcButton.top, rcButton.right, rcButton.bottom, 2, 93,
                        52, 113);
    rcButton += shift;

    pArea = AddArea();
    pArea->MakeRect(rcButton);
    figures[4]->MakeBar(3, 100, rcButton.left, rcButton.top, rcButton.right, rcButton.bottom, 2, 72,
                        52, 92);

    AddSurface(pSurf = dbgnew CI3DSurface(pI3D, rcWindow.left, rcWindow.bottom - 55, rcWindow.right,
                                          rcWindow.bottom));
#ifndef NOGETDC
    pSurf->GetDC();
    pSurf->Clear();
    if (mnCurrentPage > 0)
    {
        rcOut = CRect(0, 5, rcWindow.Width() / 3, 25);
        pSurf->DrawText(mszPages[mnCurrentPage - 1], rcOut, 0, RGB(255, 245, 130), DT_CENTER);
        areas[0]->SetToolTip(30101);
    }

    const char *pszKeyName = NKeyMap::GetKeyNameByAction(KA_TUTORIAL_SCRIPT);
    if (pszKeyName)
    {
        CString szKey;
        pTxtMng->GetSystemString("show_tutorial_key", szKey);
        szKey += " ";
        szKey += pszKeyName;
        rcOut = CRect(rcWindow.Width() / 3, 5, rcWindow.Width() * 2 / 3, 25);
        pSurf->DrawText(szKey, rcOut, 0, RGB(255, 245, 130), DT_CENTER);
    }

    if (mnCurrentPage < mszPages.GetSize() - 1)
    {
        rcOut = CRect(rcWindow.Width() * 2 / 3, 5, rcWindow.Width(), 25);
        pSurf->DrawText(mszPages[mnCurrentPage + 1], rcOut, 0, RGB(255, 245, 130), DT_CENTER);
        areas[2]->SetToolTip(30102);
    }
    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenTutorial::Exit()
{
    pI3D->BeginFX("buttons\\tutorial\\ok.wav");
    if (!mbScript)
        pI3D->tutorials[mszTutorial] = mnCurrentPage;
    else
        pI3D->nLastMissionTutorialPage = mnCurrentPage;
    pI3D->ExitFromModal();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenTutorial::OnIdle()
{
    CI3DScreen::OnIdle();
    fTime += GetFrameTime();
    float fPart = .5f + (float)cos(fTime * 5) * .5f;

    DWORD color = RGBA_MAKE(
        Max(0, Min(255, static_cast<int>(fPart * FRAME_R_TO + (1 - fPart) * FRAME_R_FROM))),
        Max(0, Min(255, static_cast<int>(fPart * FRAME_G_TO + (1 - fPart) * FRAME_G_FROM))),
        Max(0, Min(255, static_cast<int>(fPart * FRAME_B_TO + (1 - fPart) * FRAME_B_FROM))),
        Max(0, Min(255, static_cast<int>(fPart * FRAME_A_TO + (1 - fPart) * FRAME_A_FROM))));

    for (auto it = mpRects.begin(); it != mpRects.end(); ++it)
        (*it)->SetDefaultColor(color);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScreenTutorial::Draw(CRenderer *pRend)
{
    pI3D->GetRenderer()->FlushBuffers();
    DWORD dwAlpha;
    pI3D->GetRenderer()->GetRenderState(D3DRENDERSTATE_ALPHAREF, &dwAlpha);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHAREF, 1);

    CI3DScreen::Draw(pRend);

    pI3D->GetRenderer()->FlushBuffers();
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHAREF, dwAlpha);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
