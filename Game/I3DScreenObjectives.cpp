#include "StdAfx.h"
#include "GameJournal.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenObjectives.h"
#include "I3DMain.h"
#include "I3DScrollText.h"
#include "TextManager.h"
#include "ScenarioTracker.h"
#include "StringProcessing.h"
#include "WorldInterface.h"
#include "WorldClient.h"
#include "GSVars.h"
#include "KeyMap.h"
#include "QuestDatabase.h"

#include <functional>
// #include <algorithm>
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
const int OBJ_NF = 16;
const int OBJ_NUM_VIS = 5;
const int NUM_AREAS = 4;

const COLORREF OBJ_COLOR_TEXT = RGB(228, 215, 167);
const COLORREF OBJ_COLOR_HEADER = RGB(255, 179, 49);

CString CI3DScreenObjectives::szCurrentQuest;
/////////////////////////////////////////////////////////////////////////////////////
class CObjectivesSortFunctional
{
public:
    bool operator()(const CString &szObj1, const CString &szObj2) const
    {
        const SQDBQuest *pQ1 = theQuestDatabase.quests[szObj1];
        const SQDBQuest *pQ2 = theQuestDatabase.quests[szObj2];
        if (!pQ1)
            return true;
        else if (!pQ2)
            return false;
        return (pQ1->dwNumber > pQ2->dwNumber);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenObjectives::SetParams(LPCTSTR pszZoneName, ZONE_ENTRANCES *pEntr,
                                     BOOL bFromGlobalMap, BOOL bFromBs)
{
    szBaseZone = pszZoneName;
    bFromGM = bFromGlobalMap;
    pEntrances = pEntr;
    bFromBase = bFromBs;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenObjectives::OnActivate()
{
    pI3D->GetRenderer()->GetRenderState(D3DRENDERSTATE_ALPHAREF, &dwTempAlpha);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHAREF, 1);

    CTextManager *pTxtMng = theWorld.GetTextManager();
    CString szTmp, szID;
    for (int i = 0; i < TOTAL; i++)
    {
        szID.Format("obj_%d", i);
        pTxtMng->GetSystemString(szID, szTmp);
        szStrings.Add(szTmp);
    }

    CI3DScrollText *pScrollText = dbgnew CI3DScrollText(pI3D, "", CRect(60, 190, 384, 510));
    AddObject(pScrollText);
    AddObject(pScroll = dbgnew CI3DScrollBar(pI3D, CRect(60, 30, 384, 150), 3));

    AddTexture("saveload");
    AddTexture("cross");

    AddSurface(dbgnew CI3DSurface(pI3D, 50, 20, 390, 160));
    AddSurface(dbgnew CI3DSurface(pI3D, 410, 380, 750, 520));
    AddSurface(dbgnew CI3DSurface(pI3D, 280, 540, 520, 580));

    CI3DFigure *pFig;
    pFig = AddFigure(); // ok
    if (bFromGM)
        pFig->MakeBar(3, 100, 163, 536, 237, 584, 81, 2, 155, 50);
    else
        pFig->MakeBar(3, 100, 363, 536, 437, 584, 81, 2, 155, 50);

    pFig = AddFigure(); // cancel
    pFig->MakeBar(3, 100, 563, 536, 637, 584, 160, 2, 234, 50);
    if (!bFromGM)
        pFig->Show(FALSE);

    pFig = AddFigure(); // selection
    pFig->MakeBar(3, 100, 60, 30, 370, 54);
    pFig->SetTexture(0);
    pFig->SetDefaultColor(RGBA_MAKE(160, 104, 0, 200));
    pFig->UseDefaultColors(TRUE);
    pFig->Show(FALSE);

    AddFigure(); // quest map

    // pass time bg
    pFig = AddFigure();
    pFig->MakeBar(15, 100, 285, 541, 515, 579);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));

    pFig = AddFigure();
    pFig->MakeBarFrame(3, 100, 280, 536, 520, 584, 5, 2, 254, 180, 249);

    // frames
    pFig = AddFigure();
    pFig->MakeBar(15, 100, 50, 20, 390, 160);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));

    pFig = AddFigure();
    pFig->MakeBar(15, 100, 50, 180, 390, 520);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));

    pFig = AddFigure();
    pFig->MakeBar(15, 100, 410, 20, 750, 360);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));

    pFig = AddFigure();
    pFig->MakeBar(15, 100, 410, 380, 750, 520);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));

    pFig = AddFigure();
    pFig->MakeBarFrame(3, 100, 45, 15, 395, 165, 5, 2, 254, 180, 249);

    pFig = AddFigure();
    pFig->MakeBarFrame(3, 100, 45, 175, 395, 525, 5, 2, 254, 180, 249);

    pFig = AddFigure();
    pFig->MakeBarFrame(3, 100, 405, 15, 755, 365, 5, 2, 254, 180, 249);

    pFig = AddFigure();
    pFig->MakeBarFrame(3, 100, 405, 375, 755, 525, 5, 2, 254, 180, 249);

    pFig = AddFigure(); // prev zone
    pFig->MakeBar(3, 100, 414, 384, 443, 404, 2, 114, 31, 134);
    pFig->Show(FALSE);

    pFig = AddFigure(); // next zone
    pFig->MakeBar(3, 100, 717, 384, 746, 404, 33, 114, 62, 134);
    pFig->Show(FALSE);

    // quest states
    for (int i = 0; i < OBJ_NUM_VIS; i++)
    {
        pFig = AddFigure();
        pFig->MakeBar(2.8f, 100, 62, 31 + i * 24, 79, 53 + i * 24, 4, 164, 72, 252);
        pFig->SetTexture(textures[1]);
        pFig->Show(FALSE);
    }

    CI3DPolyArea *pArea;
    pArea = AddArea();
    if (bFromGM)
        pArea->MakeRect(163, 536, 237, 584); // 0
    else
        pArea->MakeRect(363, 536, 437, 584); // 0
    pArea->SetToolTip(bFromGM ? 80100 : 80102);

    pArea = AddArea();
    pArea->MakeRect(563, 536, 637, 584); // 1
    pArea->SetToolTip(bFromGM ? 80101 : 0);

    pArea = AddArea();
    pArea->MakeRect(414, 384, 443, 404); // 2
    pArea = AddArea();
    pArea->MakeRect(717, 384, 746, 404); // 3

    for (int k = 0; k < OBJ_NUM_VIS; k++) // 2 - 6
    {
        pArea = AddArea();
        pArea->MakeRect(60, 30 + k * 24, 360, 54 + k * 24);
        pArea->SetToolTip(80111);
    }

    fTime = 0;

    if (!bFromBase)
        ChangeZone(szBaseZone);
    else
    {
        SScenarioZone *pBZ = theMap.GetZoneByName(szBaseZone);
        if (!pBZ)
        {
            pI3D->ExitFromModal();
            return;
        }
        CString szNeedSetZone;
        CStringArray szVarParts;
        CGSVariableSet vars;
        IWorldClient.GSGetVars("q.", vars);
        for (auto p = vars.begin(); p != vars.end(); ++p)
        {
            NStrProc::SplitString(p->first.c_str(), szVarParts, '.');
            if (szVarParts.GetSize() != 3)
                continue;
            if (pI3D->szLastReceivedQuest.CompareNoCase(szVarParts[2]) == 0)
                szNeedSetZone = szVarParts[1];
            if (p->second == SCN_OBJECTIVE_AVAILABLE)
            {
                SScenarioZone *pZ = theMap.GetZoneByName(szVarParts[1]);
                if (!pZ)
                    continue;
                if (pZ->szAllodName.CompareNoCase(pBZ->szAllodName) != 0)
                    continue;
                bool bIs = false;
                for (auto it = szAllZones.begin(); it != szAllZones.end(); ++it)
                    if (pZ->szName == (*it))
                    {
                        bIs = true;
                        break;
                    }
                if (!bIs)
                    szAllZones.push_back(pZ->szName);
            }
        }
        if (szAllZones.size() == 0 && !szNeedSetZone.IsEmpty())
            szAllZones.push_back(szNeedSetZone);
        if (szAllZones.size() == 0)
        {
            pI3D->ExitFromModal();
            return;
        }
        nCurZone = 0;
        for (int i = 0; i < szAllZones.size(); i++)
            if (szNeedSetZone.CompareNoCase(szAllZones[i]) == 0)
            {
                nCurZone = i;
                break;
            }
        if (szAllZones.size() > 1)
        {
            figures[14]->Show(TRUE);
            figures[15]->Show(TRUE);
            areas[2]->SetToolTip(80200);
            areas[3]->SetToolTip(80201);
        }
        ChangeZone(szAllZones[nCurZone]);
    }

    pI3D->CheckTutorial();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenObjectives::ChangeZone(LPCTSTR pszZoneName)
{
    szZoneName = pszZoneName;

    while (textures.GetSize() > 2)
        RemoveTexture(2);
    while (figures.GetSize() > OBJ_NF + OBJ_NUM_VIS)
        RemoveFigure(OBJ_NF + OBJ_NUM_VIS);
    while (areas.GetSize() > NUM_AREAS + OBJ_NUM_VIS)
        RemoveArea(NUM_AREAS + OBJ_NUM_VIS);

    SScenarioZone *pZ = theMap.GetZoneByName(szZoneName);
    if (!pZ)
    {
        pI3D->ExitFromModal();
        return;
    }

    AddTexture(pZ->szMapTexture);

    float fX = (float)pZ->nSizeX;
    float fY = (float)pZ->nSizeY;
    float fMax = Max(fX, fY);
    fX /= fMax;
    fY /= fMax;

    CI3DFigure *pFig = figures[3];
    pFig->MakeBar(10, 100, 580 - 150.f * fX, 190 - 150.f * fY, 580 + 150.f * fX, 190 + 150.f * fY,
                  0, int(256 * (1 - fY)), int(256 * fX), 256);
    pFig->SetTexture(textures[2]);

    nSelEntrance = -1;
    SEntranceDesc desc;
    float fMinTime = 1e16f;
    // fill info about entrances
    bool bCanEnter = false;
    descEntrances.RemoveAll();
    for (int nE = 0; nE < pZ->exits.GetSize(); nE++)
        if (pZ->exits[nE].szZone.CompareNoCase("none") != 0)
        {
            int n = descEntrances.GetSize();
            descEntrances.Add(desc);
            descEntrances[n].nE = nE;
            descEntrances[n].fX = (pZ->exits[nE].x1 + pZ->exits[nE].x2) / 2; // center of deploy
            descEntrances[n].fY = (pZ->exits[nE].y1 + pZ->exits[nE].y2) / 2;
            descEntrances[n].fTime = 0;
            descEntrances[n].bValid = false;

            if (pEntrances)
            {
                for (auto it = pEntrances->begin(); it != pEntrances->end(); it++)
                    if (it->first == nE)
                    {
                        bCanEnter = true;
                        descEntrances[n].fTime = it->second;
                        descEntrances[n].bValid = true;
                        if (it->second < fMinTime)
                        {
                            nSelEntrance = n;
                            fMinTime = it->second;
                        }
                        break;
                    }
            }
            else
            {
                CString szVarName;
                szVarName.Format("z.%s", pZ->exits[nE].szZone);
                if (IWorldClient.GSGetVar(szVarName) != 1.f)
                    descEntrances[n].bValid = true;
            }

            float fXSize = pZ->nSizeX * 32;
            float fYSize = pZ->nSizeY * 32;
            float fMaxSize = Max(fXSize, fYSize);
            float fXScr = descEntrances[n].fX - fXSize / 2;
            float fYScr = descEntrances[n].fY - fYSize / 2;
            float fXRel = fXScr / fXSize;
            float fYRel = fYScr / fYSize;
            fXScr = 580 + fXScr * 300 / fMaxSize;
            fYScr = 190 - fYScr * 300 / fMaxSize;
            descEntrances[n].fXScr = fXScr;
            descEntrances[n].fYScr = fYScr;

            CI3DFigure *pFig = AddFigure();
            pFig->MakeTriangle(5, 100, fXScr, fYScr, fXScr - 10, fYScr + 20, fXScr + 10, fYScr + 20,
                               240, 226, 227, 254, 253, 254);
            // pFig->MakeBar( 5, 100, fXScr - 10, fYScr - 10, fXScr + 10, fYScr + 10 );
            // pFig->BindTexture(0);
            fArrowScale = pFig->GetXScale();
            CRect click(int(fXScr - 15), int(fYScr - 15), int(fXScr + 15), int(fYScr + 15));
            CI3DPolyArea *pArea = AddArea();
            pArea->SetToolTip(80110);
            if (fabs(fXRel) > fabs(fYRel))
            {
                if (fXRel > 0)
                {
                    pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), -FP_PI2));
                    pArea->MakeRect(click + CPoint(10, 0));
                }
                else
                {
                    pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), FP_PI2));
                    pArea->MakeRect(click - CPoint(10, 0));
                }
            }
            else
            {
                if (fYRel > 0)
                {
                    pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), FP_PI));
                    pArea->MakeRect(click - CPoint(0, 10));
                }
                else
                {
                    pArea->MakeRect(click + CPoint(0, 10));
                }
            }
        }

    SetCurrentEntrance(nSelEntrance);

    if (bFromGM && !bCanEnter)
    {
        figures[0]->Show(FALSE);
        areas[0]->SetToolTip(0);
    }

    szObjectives.RemoveAll();

    CString szVarName;
    CStringArray szVarParts;
    CGSVariableSet vars;

    szVarName.Format("q.%s", szZoneName);
    IWorldClient.GSGetVars(szVarName, vars);
    CGSVariableSet::const_iterator p;
    std::vector<CString> quests;
    for (p = vars.begin(); p != vars.end(); ++p)
        if (p->second != SCN_OBJECTIVE_UNKNOWN)
        {
            NStrProc::SplitString(p->first.c_str(), szVarParts, '.');
            if (szVarParts.GetSize() == 3)
                quests.push_back(szVarParts[2]);
        }
    nOldObj = -1;
    nCurObj = 0;
    if (quests.size() == 0)
    {
        nCurObj = -1;
        szCurrentQuest.Empty();
    }
    else
    {
        // sort by number in quests table
        std::sort(quests.begin(), quests.end(), CObjectivesSortFunctional());
        // fill array
        for (auto it = quests.begin(); it != quests.end(); ++it)
        {
            szObjectives.Add(*it);
            if (!bFromBase && szCurrentQuest.CompareNoCase(*it) == 0)
                nCurObj = szObjectives.GetSize() - 1;
            if (bFromBase && pI3D->szLastReceivedQuest.CompareNoCase(*it) == 0)
                nCurObj = szObjectives.GetSize() - 1;
        }
        szCurrentQuest = szObjectives[nCurObj];
    }

    if (szObjectives.GetSize() > OBJ_NUM_VIS)
        pScroll->SetRange(szObjectives.GetSize() - OBJ_NUM_VIS);
    else
        pScroll->SetRange(0);

    pScroll->SetPos(0);
    nPos = 0;

    MoveScrollToSelection();

    CString szText, szTmp;
    CTextManager *pTxtMng = theWorld.GetTextManager();
    if (pTxtMng->GetText(CString("zone ") + szZoneName))
    {
        pTxtMng->GetLine(szTmp);
        pTxtMng->GetUntilNextCommand(szText);

#ifndef DXGI_ENABLED
        CI3DSurface *pSurf = surfaces[1];
        pSurf->GetDC();
        pSurf->Clear();
        pSurf->DrawText(szTmp, CRect(0, 5, 340, 25), 1, OBJ_COLOR_HEADER, DT_CENTER);

        pSurf->DrawText(szText, CRect(10, 25, 330, 140), 1, OBJ_COLOR_TEXT,
                        DT_CENTER | DT_WORDBREAK);

        pSurf->ReleaseDC();
#endif // NOGETDC
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenObjectives::OnDeactivate()
{
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHAREF, dwTempAlpha);
}
/////////////////////////////////////////////////////////////////////////////////////
LPCTSTR CI3DScreenObjectives::GetTutorialScreenName()
{
    if (bFromGM)
        return "quest_global_map";
    else
        return "quest_mission";
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenObjectives::RedrawObjectivesList()
{
    CTextManager *pTxtMng = theWorld.GetTextManager();
    CString szTmp;
    CString szVarName;

    figures[2]->Show(FALSE);

#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();
    pSurf->Clear();
    int i, n;

    for (i = OBJ_NF; i < OBJ_NF + OBJ_NUM_VIS; i++)
        figures[i]->Show(FALSE);
    for (i = 0, n = nPos; i < OBJ_NUM_VIS && n < szObjectives.GetSize(); i++, n++)
    {
        figures[OBJ_NF + i]->Show(TRUE);

        if (pTxtMng->GetText(CString("quest ") + szObjectives[n]))
            pTxtMng->GetLine(szTmp);
        else
            szTmp = szObjectives[n];

        szVarName.Format("q.%s.%s", szZoneName, szObjectives[n]);
        int state = (int)IWorldClient.GSGetVar(szVarName);
        switch (state)
        {
        case SCN_OBJECTIVE_COMPLETED:
            figures[OBJ_NF + i]->SetUShift(17.f / 64.f);
            break;
        case SCN_OBJECTIVE_FAILED:
            figures[OBJ_NF + i]->SetUShift(0);
            break;
        default:
            figures[OBJ_NF + i]->Show(FALSE);
            break;
        }
        if (n == nCurObj)
        {
            figures[2]->SetRelativeToScreen(215, 42 + i * 24, 3);
            figures[2]->Show(TRUE);
        }
        pSurf->DrawText(szTmp, CRect(36, 13 + i * 24, 320, 34 + i * 24), 1, OBJ_COLOR_TEXT,
                        DT_LEFT);
    }
    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenObjectives::RedrawObjective()
{
    CTextManager *pTxtMng = theWorld.GetTextManager();
    CString szTmp, szOut;
    CString szText;
    CStringArray szTmpArray;
    CString szVarName;

    if (nOldObj == nCurObj)
        return;
    nOldObj = nCurObj;

    ((CI3DScrollText *)pObjects[0])->RemoveAllText();

    if (nCurObj < 0 || nCurObj >= szObjectives.GetSize())
        return;

    szCurrentQuest = szObjectives[nCurObj];

    CString szCompletedText, szFailedText, szCostExp, szCostMoney;
    pTxtMng->GetSystemString("obj_completed", szCompletedText);
    pTxtMng->GetSystemString("obj_failed", szFailedText);
    pTxtMng->GetSystemString("obj_cost_exp", szCostExp);
    pTxtMng->GetSystemString("obj_cost_money", szCostMoney);

    if (!pTxtMng->GetText(CString("quest ") + szObjectives[nCurObj]))
        return;

    pTxtMng->SkipLine();
    pTxtMng->GetUntilNextCommand(szText);
    szText = CString("\aC\x0e4\x0d7\x0a7 ") + szText;
    szText += "\n ";

    const SQDBQuest *pQ = theQuestDatabase.quests[szObjectives[nCurObj]];
    if (pQ)
    {
        szText += "\aC\x0ff\x0ff\x0ff ";
        if (pQ->fExp > 0)
        {
            szOut.Format("%s %d\n ", szCostExp, (int)pQ->fExp);
            szText += szOut;
        }
        if (pQ->fMoney > 0)
        {
            szOut.Format("%s %d\n ", szCostMoney, (int)pQ->fMoney);
            szText += szOut;
        }
    }

    while (pTxtMng->FindNextCommand())
    {
        pTxtMng->GetStringArray(szTmpArray);
        if (szTmpArray.GetSize() < 2)
            continue;
        if (szTmpArray[0].CompareNoCase("#quest") == 0)
            break;
        if (szTmpArray[0].CompareNoCase("#subobj") == 0)
        {
            szVarName.Format("q.%s.%s.%s", szZoneName, szObjectives[nCurObj], szTmpArray[1]);
            int state = (int)IWorldClient.GSGetVar(szVarName);
            if (state == SCN_OBJECTIVE_UNKNOWN)
                continue;
            pTxtMng->GetLine(szTmp);
            szText += "\aC\x0ff\x0b3\x031 ";
            szText += szTmp;
            szText += " ";
            switch (state)
            {
            case SCN_OBJECTIVE_COMPLETED:
                szText += "\aC\x074\x0dc\x001 ";
                szText += szCompletedText;
                break;
            case SCN_OBJECTIVE_FAILED:
                szText += "\aC\x0ff\x060\x048 ";
                szText += szFailedText;
                break;
            }
            szText += "\n \n \aC\x0e4\x0d7\x0a7";
            pTxtMng->GetUntilNextCommand(szTmp);
            szText += szTmp;
            szText += "\n \n ";
        }
    }

    pTxtMng->CloseText();

    ((CI3DScrollText *)pObjects[0])->SetText(szText);

    SScenarioZone *pZ = theMap.GetZoneByName(szZoneName);
    if (!pZ)
        return;
    ReloadObjPos(szObjectives[nCurObj]);
    while (figures.GetSize() > OBJ_NF + OBJ_NUM_VIS + descEntrances.GetSize())
        RemoveFigure(OBJ_NF + OBJ_NUM_VIS + descEntrances.GetSize());
    while (areas.GetSize() > NUM_AREAS + OBJ_NUM_VIS + descEntrances.GetSize())
        RemoveArea(NUM_AREAS + OBJ_NUM_VIS + descEntrances.GetSize());
    for (int i = 0; i < crosses.size(); i++)
    {
        float fXSize = pZ->nSizeX * 32;
        float fYSize = pZ->nSizeY * 32;
        float fMaxSize = Max(fXSize, fYSize);
        float fXScr = crosses[i].x - fXSize / 2;
        float fYScr = crosses[i].y - fYSize / 2;
        fXScr = 580 + fXScr * 300 / fMaxSize;
        fYScr = 190 - fYScr * 300 / fMaxSize;

        CI3DFigure *pFig = AddFigure();
        pFig->MakeBar(5, 100, fXScr - 10, fYScr - 10, fXScr + 10, fYScr + 10, 0, 0, 128, 128);
        pFig->SetTexture(textures[1]);
        pFig->SetAmbient(1, 0, 0);
        CI3DPolyArea *pArea = AddArea();
        pArea->MakeRect(fXScr - 10, fYScr - 10, fXScr + 10, fYScr + 10);
        pArea->SetToolTip(80112);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenObjectives::OnLButtonDown(UINT nFlags, CPoint point)
{
    CI3DScreen::OnLButtonDown(nFlags, point);
    int ht;
    ht = HitTest(point);
    JournalHit(ht);
    switch (ht)
    {
    case 0:
        pI3D->BeginFX("buttons\\tutorial\\ok.wav");
        if (bFromGM)
            GoToZone();
        else
            pI3D->ExitFromModal();
        break;
    case 1:
        if (bFromGM)
        {
            pI3D->BeginFX("buttons\\messbox\\cancel.wav");
            pI3D->ExitFromModal();
        }
        break;
    case 2:
        if (bFromBase && szAllZones.size() > 1)
        {
            nCurZone--;
            if (nCurZone < 0)
                nCurZone = szAllZones.size() - 1;
            ChangeZone(szAllZones[nCurZone]);
        }
        break;
    case 3:
        if (bFromBase && szAllZones.size() > 1)
        {
            nCurZone++;
            if (nCurZone >= szAllZones.size())
                nCurZone = 0;
            ChangeZone(szAllZones[nCurZone]);
        }
        break;
    }
    if (ht >= NUM_AREAS && ht < NUM_AREAS + OBJ_NUM_VIS)
    {
        int n = nPos + ht - NUM_AREAS;
        if (n >= 0 && n < szObjectives.GetSize())
        {
            pI3D->BeginFX("buttons\\objectives\\select.wav");
            nCurObj = n;
            RedrawObjectivesList();
            RedrawObjective();
        }
    }

    if (ht >= NUM_AREAS + OBJ_NUM_VIS && ht < NUM_AREAS + OBJ_NUM_VIS + descEntrances.GetSize())
    {
        int i = ht - NUM_AREAS - OBJ_NUM_VIS;

        if (bFromGM && descEntrances[i].bValid)
        {
            pI3D->BeginFX("buttons\\objectives\\enter.wav");
            SetCurrentEntrance(i);
        }
        else
            pI3D->BeginFX("buttons\\messbox\\cancel.wav");
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenObjectives::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CI3DScreen::OnKeyDown(nChar, nRepCnt, nFlags);
    switch (nChar)
    {
    case VK_ESCAPE:
        if (bFromGM)
            pI3D->BeginFX("buttons\\messbox\\cancel.wav");
        else
            pI3D->BeginFX("buttons\\tutorial\\ok.wav");
        pI3D->ExitFromModal();
        SetProcessed();
        break;
    case 13:
        pI3D->BeginFX("buttons\\tutorial\\ok.wav");
        if (bFromGM)
            GoToZone();
        else
            pI3D->ExitFromModal();
        SetProcessed();
        break;
    case VK_UP:
        pI3D->BeginFX("buttons\\objectives\\select.wav");
        --nCurObj;
        MoveScrollToSelection();
        SetProcessed();
        break;
    case VK_DOWN:
        pI3D->BeginFX("buttons\\objectives\\select.wav");
        ++nCurObj;
        MoveScrollToSelection();
        SetProcessed();
        break;
    case VK_PRIOR:
        pI3D->BeginFX("buttons\\objectives\\select.wav");
        nCurObj -= OBJ_NUM_VIS;
        MoveScrollToSelection();
        SetProcessed();
        break;
    case VK_NEXT:
        pI3D->BeginFX("buttons\\objectives\\select.wav");
        nCurObj += OBJ_NUM_VIS;
        MoveScrollToSelection();
        SetProcessed();
        break;
    }
    if (GetProcessed())
        return;

    switch (NKeyMap::keyMap[nFlags & 0x1ff])
    {
    case KA_OBJECTIVES:
        if (!bFromGM)
        {
            pI3D->BeginFX("buttons\\messbox\\ok.wav");
            pI3D->ExitFromModal();
        }
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenObjectives::OnIdle()
{
    CI3DScreen::OnIdle();
    int n;
    if (pScroll->GetPos(n))
    {
        nPos = n;
        RedrawObjectivesList();
    }

    fTime += GetFrameTime();
    float fAmb = .5f + (float)cos(fTime * 2.5f) * .3f;
    int nSlow = (int)(fAmb * 255);
    fAmb = .6f + (float)cos(fTime * 5) * .4f;
    int nFast = (int)(fAmb * 255);

    for (int i = OBJ_NF + OBJ_NUM_VIS + descEntrances.GetSize(); i < figures.GetSize(); i++)
    {
        figures[i]->UseDefaultColors(TRUE);
        figures[i]->SetDefaultColor(RGBA_MAKE(255, 0, 0, nFast));
    }
    for (int i = 0; i < descEntrances.GetSize(); i++)
    {
        figures[OBJ_NF + OBJ_NUM_VIS + i]->UseDefaultColors(TRUE);
        if (i == nSelEntrance)
            figures[OBJ_NF + OBJ_NUM_VIS + i]->SetDefaultColor(RGBA_MAKE(255, 255, 255, nFast));
        else
            figures[OBJ_NF + OBJ_NUM_VIS + i]->SetDefaultColor(RGBA_MAKE(200, 200, 200, nSlow));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenObjectives::MoveScrollToSelection()
{
    if (!szObjectives.GetSize())
        return;
    if (nCurObj < 0)
        nCurObj = 0;
    if (nCurObj >= szObjectives.GetSize())
        nCurObj = szObjectives.GetSize() - 1;
    if (nPos + OBJ_NUM_VIS <= nCurObj)
        nPos = nCurObj - OBJ_NUM_VIS + 1;
    if (nPos > nCurObj)
        nPos = nCurObj;
    pScroll->SetPos(nPos);
    pScroll->GetPos(nPos);
    RedrawObjectivesList();
    RedrawObjective();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenObjectives::GoToZone()
{
    if (nSelEntrance < 0)
        return;
    int nE = descEntrances[nSelEntrance].nE;
    // add global time
    IWorldServer.AddPlayersGlobalTime(descEntrances[nSelEntrance].fTime);
    GameJournal::Source journalSource("global_map");
    IWorldConnector.ClientGoZone(szZoneName, nE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenObjectives::SetCurrentEntrance(int num)
{
    nSelEntrance = num;
    for (int i = 0; i < descEntrances.GetSize(); i++)
        if (i == num)
        {
            figures[OBJ_NF + OBJ_NUM_VIS + i]->SetScale(fArrowScale * 1.2f);
            // figures[ OBJ_NF + OBJ_NUM_VIS + i ]->SetAmbient( 1, 1, 1 );
            figures[OBJ_NF + OBJ_NUM_VIS + i]->SetUShift(0);
            fTime = 0;
        }
        else
        {
            figures[OBJ_NF + OBJ_NUM_VIS + i]->SetScale(fArrowScale);
            if (descEntrances[i].bValid)
            {
                // figures[ OBJ_NF + OBJ_NUM_VIS + i ]->SetAmbient( .9f, .9f, .9f );
                figures[OBJ_NF + OBJ_NUM_VIS + i]->SetUShift(0);
            }
            else
            {
                // figures[ OBJ_NF + OBJ_NUM_VIS + i ]->SetAmbient( .9f, .9f, .9f );
                figures[OBJ_NF + OBJ_NUM_VIS + i]->SetUShift(-.125f);
            }
        }

    // temporarily disconnected
    if (1 || num < 0)
    {
        figures[4]->Show(FALSE);
        figures[5]->Show(FALSE);
        return;
    }
    // testing
    CString szTmp;

    int nHours;
    if (descEntrances[num].fTime < 0)
        nHours = 0;
    else
        nHours = static_cast<int>(descEntrances[num].fTime);
    int nDays = nHours / 24;
    nHours -= nDays * 24;

    if (nDays > 0)
        szTmp.Format("%s  %d %s %d %s", szStrings[PASSTIME], nDays, szStrings[DAYS], nHours,
                     szStrings[HOURS]);
    else
        szTmp.Format("%s  %d %s", szStrings[PASSTIME], nHours, szStrings[HOURS]);

#ifndef NOGETDC
    surfaces[2]->GetDC();
    surfaces[2]->Clear();
    surfaces[2]->DrawText(szTmp, CRect(0, 11, 240, 30), 1, OBJ_COLOR_TEXT, DT_CENTER);
    surfaces[2]->ReleaseDC();
#endif // NOGETDC

    figures[4]->Show(TRUE);
    figures[5]->Show(TRUE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenObjectives::ReloadObjPos(LPCTSTR pszName)
{
    crosses.clear();
    CPlot2D res;

    {
        CString szTmp;
        CStringArray szTmpArray;
        CTextManager *pTxtMng = theWorld.GetTextManager();
        pTxtMng->GetText("map.txt");
        szTmp.Format("#quest %s", pszName);
        if (pTxtMng->FindNextCommand(szTmp))
        {
            pTxtMng->SkipLine();
            pTxtMng->SkipLine();
            if (pTxtMng->GetStringArray(szTmpArray))
            {
                for (int i = 0; i + 1 < szTmpArray.GetSize(); i += 2)
                {
                    res.x = (float)atof(szTmpArray[i]);
                    res.y = (float)atof(szTmpArray[i + 1]);
                    crosses.push_back(res);
                }
            }
        }
        pTxtMng->CloseText();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
