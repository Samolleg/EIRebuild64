#include "StdAfx.h"
#include "WideScreen.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenChoosePers.h"
#include "I3DScreenConnection.h"
#include "I3DScreenCamp.h"
#include "I3DScreenMessageBox.h"
#include "I3DMapObjTranslation.h"
#include "I3DMain.h"
#include "A3Cursor.h"
#include "WorldInterface.h"
#include "WorldClient.h"
#include "UnitDatabase.h"
#include "RandomGen.h"
#include "I3DScrollBar.h"
#include "TextManager.h"
#include "Party.h"

#include "AckDatabase.h"

namespace
{
int CreationPrimaryWeight(int value)
{
    // Integral of the native 15..32 step prices; start 25 has weight 10.
    return value - 15 + (value >= 26) + (value >= 27) + (value >= 29);
}
}
/////////////////////////////////////////////////////////////////////////////////////
const int PERS_NUM_SLOTS_VISIBLE = 10;
const int VOICE_NUM_SLOTS_VISIBLE = 11;
const COLORREF PERS_COLOR_TEXT = RGB(228, 215, 167);
const int NUM_AREAS = 9;
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::OnActivate()
{
    bPrimaryCreationActive = false;
    nPrimaryCreationWeight = 0;
    IWorldConnector.LMPReloadDatabases(true);

    int i;
    AddTexture("campinfo");

    CI3DMapObjTranslation *pObj = AddMapObjTranslation();
    pObj->Init("unmoco2", "MainMenu00", "MainMenu00labels");
    pObj->SetRelativeToScreen(400, 350, 8.f);
    pObj->SetOrientation(
        CQuaternion(CPlot3D(1, 0, 0),
                    FP_PI2 * 1.2f) /* CQuaternion( CPlot3D(0,0,1), g_PI_DIV_2 * 2.00f ) */);

    pObj->ShowAllBodyParts(false);
    pObj->ShowBodyPart("but03", true);
    pObj->ShowBodyPart("button03_multiplayer", true);

    CI3DPolyArea *pArea;
    pArea = AddArea(); // prev stage or ok
    pArea->MakeRect(600, 400, 680, 500);
    pArea = AddArea(); // next stage or cancel
    pArea->MakeRect(720, 400, 800, 500);

    pArea = AddArea(); // rotate pers
    pArea->MakeRect(300, 470, 330, 500);
    pArea = AddArea(); // rotate pers
    pArea->MakeRect(470, 470, 500, 500);

    pArea = AddArea(); // set to default
    pArea->MakeRect(680, 430, 720, 470);

    pArea = AddArea(); // new pers
    pArea->MakeRect(180, 112, 310, 136);
    pArea = AddArea(); // delete pers
    pArea->MakeRect(180, 136, 310, 160);
    pArea = AddArea(); // rename pers
    pArea->MakeRect(180, 160, 310, 184);
    pArea = AddArea(); // equip pers
    pArea->MakeRect(180, 184, 310, 208);

    AddBackground();

    AddTexture("saveload");

    CI3DFigure *pFig;
    pFig = AddFigure(); // ok 0
    pFig->MakeBar(3, 100, 163, 526, 237, 574, 81, 2, 155, 50);
    pFig->SetTexture(textures[3]);
    pFig = AddFigure(); // cancel 1
    pFig->MakeBar(3, 100, 563, 526, 637, 574, 160, 2, 234, 50);
    pFig->SetTexture(textures[3]);
    pFig = AddFigure(); // selection 2
    pFig->MakeBar(3, 100, 110, 250, 370, 274);
    pFig->SetTexture(0);
    pFig->SetDefaultColor(RGBA_MAKE(160, 104, 0, 200));
    pFig->UseDefaultColors(TRUE);
    pFig->Show(FALSE);

    AddTexture("campm1");
    AddTexture("campm2");
    AddTexture("campm3");
    AddTexture("campm4");

    pFig = AddFigure(); // 3
    pFig->MakeBar(7, 100, 200, 100, 400, 300, 28, 28, 228, 228);
    pFig->SetTexture(textures[4]);
    pFig = AddFigure(); // 4
    pFig->MakeBar(7, 100, 400, 100, 600, 300, 28, 28, 228, 228);
    pFig->SetTexture(textures[5]);
    pFig = AddFigure(); // 5
    pFig->MakeBar(7, 100, 200, 300, 400, 500, 28, 28, 228, 228);
    pFig->SetTexture(textures[6]);
    pFig = AddFigure(); // 6
    pFig->MakeBar(7, 100, 400, 300, 600, 500, 28, 28, 228, 228);
    pFig->SetTexture(textures[7]);

    // reset to default
    pFig = AddFigure(); // 7
    pFig->MakeBar(3, 100, 680, 430, 720, 470, 84, 152, 124, 192);
    // prev stage
    pFig = AddFigure(); // 8
    pFig->MakeBar(3, 100, 620, 430, 660, 470, 2, 152, 42, 192);
    // next stage
    pFig = AddFigure(); // 9
    pFig->MakeBar(3, 100, 740, 430, 780, 470, 43, 152, 83, 192);

    pFig = AddFigure(); // 10
    pFig->MakeBarFrame(2, 100, 95, 95, 395, 225, 5, 2, 254, 180, 249);
    pFig->SetTexture(textures[3]);
    pFig = AddFigure(); // 11
    pFig->MakeBarFrame(2, 100, 95, 235, 395, 505, 5, 2, 254, 180, 249);
    pFig->SetTexture(textures[3]);
    pFig = AddFigure(); // 12
    pFig->MakeBarFrame(2, 100, 405, 95, 705, 505, 5, 2, 254, 180, 249);
    pFig->SetTexture(textures[3]);
    pFig = AddFigure(); // 13
    pFig->MakeBar(5, 100, 100, 100, 390, 220);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));
    pFig = AddFigure(); // 14
    pFig->MakeBar(5, 100, 100, 240, 390, 500);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));

    for (i = 0; i < 4; i++)
    {
        pFig = AddFigure(); // 15 - 18
        pFig->MakeBar(3, 100, 180, 115 + i * 24, 310, 134 + i * 24, 122, 108, 252, 127);
        pFig->SetTexture(textures[3]);
    }

    AddObject(pItemInfo = dbgnew CI3DObjectItemInfo(pI3D, this));
    pItemInfo->Init(600, 100);

    AddObject(pNameStats = dbgnew CI3DObjectNameStats(pI3D, this));
    pNameStats->SetTexture(textures[0]);
    pNameStats->Init();

    AddObject(pSlots = dbgnew CI3DObjectQuickSlots(pI3D, this));
    pSlots->SetTexture(textures[2]);
    pSlots->Init(false);

    AddObject(pSpells = dbgnew CI3DObjectSpellSlots(pI3D, this));
    pSpells->SetTexture(textures[2]);
    pSpells->Init(false);

    AddObject(pPerks = dbgnew CI3DObjectPerks(pI3D, this));
    pPerks->SetTexture(textures[0]);
    pPerks->Init();

    AddObject(pScroll = dbgnew CI3DScrollBar(pI3D, CRect(110, 250, 384, 490), 3));

    CTextManager *pTxtMng = theWorld.GetTextManager();
    CString szMale, szFemale, szMaskName, szVoiceName;
    pTxtMng->GetSystemString("lmp_male_voice", szMale);
    pTxtMng->GetSystemString("lmp_female_voice", szFemale);
    int n = theUnitDatabase.npcs.GetNumEntries();
    for (i = 0; i < n; i++)
    {
        const SUDBUnitPrototype *pProt = theUnitDatabase
                                             .prototypes[theUnitDatabase.npcs[i]->szName];
        if (!pProt)
            continue;
        szMaskName = theUnitDatabase.races[pProt->szRace]->szMaskName;
        if (szMaskName.CompareNoCase("unhuma") == 0)
        {
            szVoiceName.Format("%s %d", szMale, maleVoices.size() + 1);
            if (theUnitDatabase.npcs[i]->bCanUseAsVoicePrototype)
                maleVoices.push_back(std::make_pair(pProt->szName, szVoiceName));
            males.push_back(pProt->szName);
        }
        if (szMaskName.CompareNoCase("unhufe") == 0)
        {
            szVoiceName.Format("%s %d", szFemale, femaleVoices.size() + 1);
            if (theUnitDatabase.npcs[i]->bCanUseAsVoicePrototype)
                femaleVoices.push_back(std::make_pair(pProt->szName, szVoiceName));
            females.push_back(pProt->szName);
        }
    }
    std::sort(males.begin(), males.end());
    std::sort(females.begin(), females.end());
    std::reverse(females.begin(), females.end());

    AddObject(pMaleFaces = dbgnew CI3DObjectInventoryFaces(pI3D, this));
    pMaleFaces->Init(&males, true);

    AddObject(pFemaleFaces = dbgnew CI3DObjectInventoryFaces(pI3D, this));
    pFemaleFaces->Init(&females);

    AddObject(pClanName = dbgnew CI3DEditBox(pI3D, CRect(558, 110, 695, 126)));
    pClanName->SetParams(EBF_DIGITS | EBF_LETTERS, false, 1, 10, PERS_COLOR_TEXT,
                         RGB(255, 179, 49));
    pClanName->Show(FALSE);
    pClanName->Activate(false);
    AddObject(pPersName = dbgnew CI3DEditBox(pI3D, CRect(0, 127, 200, 143)));
    pPersName->SetParams(EBF_DIGITS | EBF_LETTERS, TRUE, 1, 10, RGB(255, 255, 255),
                         RGB(255, 179, 49));
    pPersName->Show(FALSE);
    pPersName->Activate(false);

    AddObject(pScrollVoices = dbgnew CI3DScrollBar(pI3D, CRect(10, 325, 190, 490), 3));
    pScrollVoices->Show(FALSE);

    hVoiceSound = SND_EMPTY;
    nVoicePos = 0;
    nCurrentVoice = 0;
    pVoices = &maleVoices;

    fPersAngle = 0;
    nLock = 0;

    AddSurface(dbgnew CI3DSurface(pI3D, 100, 240, 390, 500));
    AddSurface(dbgnew CI3DSurface(pI3D, 100, 100, 390, 220));
    AddSurface(dbgnew CI3DSurface(pI3D, 0, 300, 200, 500));
    AddSurface(dbgnew CI3DSurface(pI3D, 425, 110, 555, 126));
    surfaces[3]->Show(FALSE);

    CString szTmp;
#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[1];
    pSurf->GetDC();
    pSurf->Clear();
    pTxtMng->GetSystemString("lmp_pers_new", szTmp);
    pSurf->DrawText(szTmp, CRect(0, 16, 290, 36), 1, RGB(255, 255, 255), DT_CENTER);
    pTxtMng->GetSystemString("lmp_pers_rename", szTmp);
    pSurf->DrawText(szTmp, CRect(0, 40, 290, 60), 1, RGB(255, 255, 255), DT_CENTER);
    pTxtMng->GetSystemString("lmp_pers_delete", szTmp);
    pSurf->DrawText(szTmp, CRect(0, 64, 290, 84), 1, RGB(255, 255, 255), DT_CENTER);
    pTxtMng->GetSystemString("lmp_pers_redress", szTmp);
    pSurf->DrawText(szTmp, CRect(0, 88, 290, 108), 1, RGB(255, 255, 255), DT_CENTER);
    pSurf->ReleaseDC();
#endif // NOGETDC

    LMPPartySet.szDir = IWorld.GetWorkPath() + "mp\\";
    LMPPartySet.Load();

    FormPersList();

    if (LMPPartySet.parties.size() > 0)
        nCurrentParty = 0;
    else
        nCurrentParty = -1;

    //	IWorld.CombatPause( false );
    SetMode(ISM_CHOOSEPERS);

    // WideScreen.cpp side fillers; keep the original center layout.
    CI3DFigure *wideLeft = AddFigure();
    wideLeft->MakeBar(2, 100, -float(WideScreen::VirtualShift()), 0, 0, 600, 227, 62, 256, 176);
    wideLeft->SetAmbient(.3f, .3f, .3f);
    CI3DFigure *wideRight = AddFigure();
    wideRight->MakeBar(2, 100, 800, 0, 800 + float(WideScreen::VirtualShift()), 600, 256, 176, 227, 62);
    wideRight->SetAmbient(.3f, .3f, .3f);
    wideLeft->Show(FALSE);
    wideRight->Show(FALSE);
}

/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::OnDeactivate()
{
    EndPrimaryCreation();
    LMPPartySet.Store();

    // remove
    CPersonages personages;
    CParty &party = IWorldClient.GetParty();
    for (int i = 0; i < party.people.GetSize(); i++)
        personages.Add(&party.people[i]);
    IWorldClient.RemoveClientPersonages(personages);
    pCurUnit = 0;

    pItemInfo->SetItem(0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::OnExitFromModal(DWORD dwRetValue)
{
    switch (dwRetValue)
    {
    case 0x00010001:
        DeleteParty();
        break;
    }
    if (nCurrentParty >= 0 && nCurrentParty < LMPPartySet.parties.size())
    {
        LMPPartySet.parties[nCurrentParty].party = IWorldClient.GetParty();
        LMPPartySet.parties[nCurrentParty].Store(LMPPartySet.szDir);
    }
    SetMode(ISM_CHOOSEPERS);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScreenChoosePers::Draw(CRenderer *pRend)
{
#ifndef NOGETDC
    if (eScreenMode == ISM_STATS)
    {
        CString szTmp, szOut;
        CI3DSurface *pSurf = surfaces[2];
        pSurf->GetDC();
        pSurf->Clear();
        theWorld.GetTextManager()->GetSystemString("camp_level_up", szTmp);
        pSurf->DrawText(szTmp, CRect(0, 130, 200, 150), 1, RGB(255, 255, 255), DT_CENTER);
        szTmp.Format("%+d", -pPerks->GetExpPrice());
        pSurf->DrawText(szTmp, CRect(0, 150, 200, 170), 1, RGB(255, 255, 255), DT_CENTER);
        theWorld.GetTextManager()->GetSystemString("camp_current_exp", szTmp);
        szOut.Format("%s %d", szTmp, pPerks->GetExpTotal());
        pSurf->DrawText(szOut, CRect(0, 110, 200, 130), 1, RGB(255, 255, 255), DT_CENTER);
        pSurf->ReleaseDC();
    }
#endif // NOGETDC

    CPoint point = theCursor.GetPos();

    pMouseItem = 0;
    int nSlotType = 0;

    if (!pMouseItem && !nSlotType)
        pMouseItem = pSlots->GetMouseItem(point, nSlotType);
    if (!pMouseItem && !nSlotType)
        pMouseItem = pSpells->GetMouseItem(point, nSlotType);

    if (pMouseItem)
    {
        if (pItemInfo)
            pItemInfo->SetItem(pMouseItem);
    }
    else if (nSlotType)
    {
        if (pItemInfo)
            pItemInfo->SetHelp(nSlotType);
    }
    else
    {
        SCampSkillMouseInfo skillInfo;
        skillInfo.nType = -1;
        if (pPerks->IsVisible())
            pPerks->GetMouseSkill(point, &skillInfo);
        if (skillInfo.nType < 0)
        {
            if (pNameStats->IsVisible())
                pNameStats->GetMouseSkill(point, &skillInfo);
        }
        if (skillInfo.nType < 0 && eScreenMode == ISM_SKIN)
        {
            int ht = HitTest(point);
            if (ht >= NUM_AREAS && ht < NUM_AREAS + VOICE_NUM_SLOTS_VISIBLE)
            {
                skillInfo.nType = 7;
                skillInfo.szNonlocalName = "lmp_select_voice";
            }
        }
        if (pItemInfo)
            pItemInfo->SetSkill(&skillInfo);
    }

    CI3DScreen::Draw(pRend);

    if (eScreenMode == ISM_CHOOSEPERS && nCurrentParty >= 0)
        DrawUnit(pRend, pCurUnit, fPersAngle, 555);
    else if (eScreenMode == ISM_SKIN)
        DrawUnit(pRend, pCurUnit, fPersAngle);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::SetMode(EItemScreenModes eNewMode)
{
    if (eNewMode == ISM_CHOOSEPERS)
        EndPrimaryCreation();
    else
        pNameStats->StopPrimaryRepeat();
    GetMapObjTranslation(0)->ShowAllBodyParts(false);
    pSlots->Show(FALSE);
    pSpells->Show(FALSE);
    pPerks->Show(FALSE);
    pNameStats->Show(FALSE);
    pMaleFaces->Show(FALSE);
    pFemaleFaces->Show(FALSE);
    pScroll->Show(FALSE);
    pScrollVoices->Show(FALSE);
    pCampBG->Show(FALSE);
    for (int i = 0; i < figures.GetSize(); i++)
        figures[i]->Show(FALSE);
    for (int i = 0; i < surfaces.GetSize(); i++)
        surfaces[i]->Show(FALSE, TRUE);
    while (areas.GetSize() > NUM_AREAS)
        RemoveArea(NUM_AREAS);
    for (int i = 0; i < areas.GetSize(); i++)
        areas[i]->SetToolTip(0);

    switch (eNewMode)
    {
    case ISM_CHOOSEPERS:
        GetMapObjTranslation(0)->ShowBodyPart("but03", true);
        GetMapObjTranslation(0)->ShowBodyPart("button03_multiplayer", true);
        pScroll->Show(TRUE);
        for (int i = 0; i < figures.GetSize(); i++)
            figures[i]->Show(TRUE);
        for (int i = 7; i < 10; i++)
            figures[i]->Show(FALSE);
        figures[3]->MakeBar(7, 100, 410, 100, 555, 300, 83, 28, 228, 228);
        figures[4]->MakeBar(7, 100, 555, 100, 700, 300, 28, 28, 173, 228);
        figures[5]->MakeBar(7, 100, 410, 300, 555, 500, 83, 28, 228, 228);
        figures[6]->MakeBar(7, 100, 555, 300, 700, 500, 28, 28, 173, 228);
        areas[0]->MakeRect(563, 526, 637, 574);
        areas[1]->MakeRect(163, 526, 237, 574);
        areas[2]->MakeRect(455, 470, 485, 500);
        areas[3]->MakeRect(625, 470, 655, 500);
        areas[0]->SetToolTip(40101);
        areas[1]->SetToolTip(40100);
        for (int i = 5; i < 9; i++)
            areas[i]->SetToolTip(40200 + i - 5);
        surfaces[0]->Show(TRUE, TRUE);
        surfaces[1]->Show(TRUE, TRUE);
        pPersName->Show(FALSE);
        pPersName->Activate(false);
        figures[2]->MakeBar(3, 100, 110, 250, 370, 274);
        for (int i = 0; i < PERS_NUM_SLOTS_VISIBLE; i++)
            AddArea()->MakeRect(110, 250 + i * 24, 370, 274 + i * 24);
        ChooseParty(nCurrentParty);
        break;
    case ISM_SKIN:
        pScrollVoices->Show(TRUE);
        for (int i = 3; i < 10; i++)
            figures[i]->Show(TRUE);
        figures[3]->MakeBar(7, 100, 200, 100, 400, 300, 28, 28, 228, 228);
        figures[4]->MakeBar(7, 100, 400, 100, 600, 300, 28, 28, 228, 228);
        figures[5]->MakeBar(7, 100, 200, 300, 400, 500, 28, 28, 228, 228);
        figures[6]->MakeBar(7, 100, 400, 300, 600, 500, 28, 28, 228, 228);
        pCampBG->Show(TRUE);
        pCampBG->SetHeight(300);
        pNameStats->Show(TRUE);
        pMaleFaces->Show(TRUE);
        pFemaleFaces->Show(TRUE);
        areas[0]->MakeRect(620, 430, 660, 470);
        areas[1]->MakeRect(740, 430, 780, 470);
        areas[2]->MakeRect(300, 470, 330, 500);
        areas[3]->MakeRect(470, 470, 500, 500);
        areas[0]->SetToolTip(40301);
        areas[1]->SetToolTip(40300);
        areas[4]->SetToolTip(40302);
        pPersName->Show(TRUE);
        pPersName->Activate(true);
        surfaces[2]->Show(TRUE, TRUE);
        figures[2]->MakeBar(3, 100, 15, 310, 180, 325);
        for (int i = 0; i < VOICE_NUM_SLOTS_VISIBLE; i++)
            AddArea()->MakeRect(10, 325 + i * 15, 180, 340 + i * 15);
        RedrawVoiceList();
        break;
    case ISM_STATS:
        for (int i = 7; i < 10; i++)
            figures[i]->Show(TRUE);
        pCampBG->Show(TRUE);
        pCampBG->SetHeight(400);
        pSlots->Show(TRUE);
        pSpells->Show(TRUE);
        pPerks->Show(TRUE);
        pNameStats->Show(TRUE);
        areas[0]->MakeRect(620, 430, 660, 470);
        areas[1]->MakeRect(740, 430, 780, 470);
        areas[0]->SetToolTip(40501);
        areas[1]->SetToolTip(40500);
        areas[4]->SetToolTip(40502);
        surfaces[2]->Show(TRUE, TRUE);
        pPersName->Show(FALSE);
        pPersName->Activate(false);
        pPerks->SetPersonage(0);
        break;
    }

    pItemInfo->SetItem(0);

    pNameStats->SetScreenMode(eNewMode);
    eScreenMode = eNewMode;
    if (figures.GetSize() == 21)
    {
        figures[19]->Show(eNewMode != ISM_CHOOSEPERS);
        figures[20]->Show(eNewMode != ISM_CHOOSEPERS);
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::RedrawVoiceList()
{
#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[2];
    CDC *pDC = pSurf->GetDC();
    pSurf->Clear();
#endif // NOGETDC
    int i, n;
    figures[2]->Show(FALSE);
    CRect rc(10, 5, 190, 20);
    CString szTmp;
    theWorld.GetTextManager()->GetSystemString("lmp_voices", szTmp);
    pSurf->DrawText(szTmp, rc, 0, RGB(255, 255, 255), DT_CENTER);
    rc = CRect(20, 25, 175, 40);
    CPoint shift(0, 15);
    for (i = 0, n = nVoicePos; i < VOICE_NUM_SLOTS_VISIBLE && n < pVoices->size(); i++, n++)
    {
        if (n == nCurrentVoice)
        {
            figures[2]->Show(TRUE);
            figures[2]->SetRelativeToScreen(97.5, 332.5f + i * 15, 3);
        }
#ifndef NOGETDC
        pSurf->DrawText((*pVoices)[n].second, rc, 0, PERS_COLOR_TEXT, DT_LEFT | DT_NOPREFIX);
#endif // NOGETDC
        rc += shift;
    }
#ifndef NOGETDC
    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::FormPersList()
{
    if (LMPPartySet.parties.size() > PERS_NUM_SLOTS_VISIBLE)
    {
        int n;
        pScroll->GetPos(n);
        pScroll->SetRange(LMPPartySet.parties.size() - PERS_NUM_SLOTS_VISIBLE);
        pScroll->SetPos(n);
        pScroll->GetPos(nPos);
    }
    else
    {
        pScroll->SetRange(0);
        nPos = 0;
    }
    RedrawPersList();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::RedrawPersList()
{
#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    CDC *pDC = pSurf->GetDC();
    pSurf->Clear();
#endif // NOGETDC
    int i, n;
    figures[2]->Show(FALSE);
    CRect rc(12, 13, 265, 33);
    CPoint shift(0, 24);
    for (i = 0, n = nPos; i < PERS_NUM_SLOTS_VISIBLE && n < LMPPartySet.parties.size(); i++, n++)
    {
        if (n == nCurrentParty)
        {
            figures[2]->Show(TRUE);
            figures[2]->SetRelativeToScreen(240, 262 + i * 24, 3);
        }
#ifndef NOGETDC
        pSurf->DrawText(LMPPartySet.parties[n].party.people[0].szName, rc, 1, PERS_COLOR_TEXT,
                        DT_LEFT | DT_NOPREFIX);
#endif // NOGETDC
        rc += shift;
    }
#ifndef NOGETDC
    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
CPersonage *CI3DScreenChoosePers::GetCurrentPers()
{
    return &IWorldClient.GetParty().people[0];
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::ChooseParty(int nParty)
{
    EndPrimaryCreation();
    // remove
    CPersonages personages;
    CParty &party = IWorldClient.GetParty();
    if (party.people.GetSize() > 0)
    {
        personages.Add(&party.people[0]);
        IWorldClient.RemoveClientPersonages(personages);
    }

    nCurrentParty = -1;
    if (nParty >= 0 && nParty < LMPPartySet.parties.size())
    {
        nCurrentParty = nParty;
        party = LMPPartySet.parties[nParty].party;
        LMPPartySet.SetCurrent(LMPPartySet.parties[nParty]);
    }

    DeployUnit();

    CPersonage *pPers = GetCurrentPers();
    CUnit *pUnit = IWorldClient.GetUnit(pPers->nID);
    for (int i = 0; i < QI_MAX_WEAPONS; i++)
        pUnit->RemoveWeapon(i);
    for (int i = 0; i < pPers->weapons.GetSize(); i++)
        pUnit->AddWeapon(pPers->weapons[i]);
    pUnit->ConvertWeaponsOnClient();
    pUnit->ChangeWeapon(pPers->dwCurrentWeapon);
    pUnit->GetUnitSpecific()->NotifyWeaponUpdated();

    fPersAngle = 0;

    RedrawPersList();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::SetPrototype(LPCTSTR pszPrototype)
{
    if (!bPrimaryCreationActive ||
        (eScreenMode != ISM_CHOOSEPERS && eScreenMode != ISM_SKIN))
        return;
    pNameStats->StopPrimaryRepeat();
    // deploy
    CPersonages personages;
    CParty &party = IWorldClient.GetParty();

    float fStr = 25, fDex = 25, fInt = 25, fTall = 0.7f;
    if (party.people.GetSize() > 0)
    {
        SRPGUnitStats *pStats = &party.people[0].stats;
        fStr = pStats->psStrength.fpBaseValue;
        fDex = pStats->psDexterity.fpBaseValue;
        fInt = pStats->psIntelligence.fpBaseValue;
        fTall = party.people[0].complection.tall;
        personages.Add(&party.people[0]);
        IWorldClient.RemoveClientPersonages(personages);
    }
    party.Clear();
    party.CreatePersonage("Hero", pszPrototype, pszPrototype);
    party.InitLMPPers(party.people[0]);

    CPersonage *pPers = &party.people[0];
    SRPGUnitStats *pStats = &pPers->stats;

    if (eScreenMode == ISM_CHOOSEPERS)
    {
        pStats->psStrength.fpEffectiveValue = pStats->psStrength.fpBaseValue = 25;
        pStats->psDexterity.fpEffectiveValue = pStats->psDexterity.fpBaseValue = 25;
        pStats->psIntelligence.fpEffectiveValue = pStats->psIntelligence.fpBaseValue = 25;
        pPers->complection.tall = 0.7f;
    }
    else
    {
        pStats->psStrength.fpEffectiveValue = pStats->psStrength.fpBaseValue = fStr;
        pStats->psDexterity.fpEffectiveValue = pStats->psDexterity.fpBaseValue = fDex;
        pStats->psIntelligence.fpEffectiveValue = pStats->psIntelligence.fpBaseValue = fInt;
        pPers->complection.tall = fTall;
    }

    bool bMale = true;
    pFemaleFaces->SetSelected(-1);
    pMaleFaces->SetSelected(-1);
    std::vector<CString>::iterator it;
    int j;
    for (it = males.begin(), j = 0; it != males.end(); ++it, ++j)
        if (it->CompareNoCase(pszPrototype) == 0)
        {
            pMaleFaces->SetSelected(j);
            break;
        }
    for (it = females.begin(), j = 0; it != females.end(); ++it, ++j)
        if (it->CompareNoCase(pszPrototype) == 0)
        {
            bMale = false;
            pFemaleFaces->SetSelected(j);
            break;
        }

    pSelWeapon = (pPers->weapons.GetSize() > 0) ? pPers->weapons[0] : 0;
    pSlots->SetCurrentWeapon(pSelWeapon);
    pSelQItem = (pPers->items.GetSize() > 0) ? pPers->items[0] : 0;
    pSlots->SetCurrentQItem(pSelQItem);
    pSelSpell = (pPers->spells.GetSize() > 0) ? pPers->spells[0] : 0;
    pSpells->SetCurrentSpell(pSelSpell);

    DeployUnit(false);

    pNameStats->SetPersonage(0, 0);
    pPerks->UpdateBackup();
    pPerks->SetPersonage(0);

    szPrototype = pszPrototype;
    nCurrentVoice = 0;
    nVoicePos = 0;
    if (bMale)
        pVoices = &maleVoices;
    else
        pVoices = &femaleVoices;
    pPers->szVoicePrototype = (*pVoices)[nCurrentVoice].first;

    if (pVoices->size() > VOICE_NUM_SLOTS_VISIBLE)
        pScrollVoices->SetRange(pVoices->size() - VOICE_NUM_SLOTS_VISIBLE);
    else
        pScrollVoices->SetRange(0);
    pScrollVoices->SetPos(0);

    if (eScreenMode == ISM_SKIN)
        RedrawVoiceList();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::DeployUnit(bool bShowHelm)
{
    pCurUnit = 0;

    CPersonages personages;
    CParty &party = IWorldClient.GetParty();

    if (party.people.GetSize() == 0)
    {
        party.Clear();
        party.CreatePersonage("Hero", "Human Hero", "");
    }

    personages.Add(&party.people[0]);
    IWorldClient.DeployClientPersonages(personages, bShowHelm);

    CUnit *pUnit = IWorldClient.GetUnit(party.people[0].nID);
    // CRAP - set default voice prototype
    ASSERT(pUnit);
    pUnit->SetVoicePrototype(pUnit->GetPrototype());
    // CRAP ends
    pCurUnit = pUnit;
    if (!pUnit)
        return;

    const SADEntry *pEntry = pUnit->GetSpecialAnimationEntry(33);
    if (pEntry)
        pUnit->LowStartAnimationExtended(0xffffffffu, pEntry->dwEntryIndex,
                                 (timeGetTime() / 1024.0f) * 15 - random.Get(0, 50), 0);
    pUnit->ChangeWeapon(-1);
    pUnit->GetUnitSpecific()->NotifyWeaponUpdated();
    pUnit->GetUnitSpecific()->NewAnimationWasReceived((timeGetTime() / 1024.0f) * 15 - 1);

    pUnit->SetupCampUnitWithUnitStats(&party.people[0].stats);
    pUnit->GetUnitSpecific()->NotifyHitPointsUpdated();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::OnLButtonDown(UINT nFlags, CPoint point)
{
    if (pClanName->IsActive())
        return;
    CI3DScreen::OnLButtonDown(nFlags, point);
    if (GetProcessed())
        return;
    int ht = HitTest(point);
    if (eScreenMode == ISM_SKIN)
    {
        switch (ht)
        {
        case 0:
            pI3D->BeginFX("buttons\\messbox\\cancel.wav");
            SetMode(ISM_CHOOSEPERS);
            break;
        case 1:
            CheckProceedToStats();
            break;
        case 2:
            pI3D->BeginFX("buttons\\camp\\move.wav");
            nLock = 1;
            pI3D->SetCapture(this);
            break;
        case 3:
            pI3D->BeginFX("buttons\\camp\\move.wav");
            nLock = 2;
            pI3D->SetCapture(this);
            break;
        case 4: {
            pI3D->BeginFX("buttons\\messbox\\cancel.wav");
            if (!bPrimaryCreationActive)
                break;
            BeginPrimaryCreation();
            ASSERT(males.size() > 0);
            SetPrototype(males[0]);
            CPersonage *pPers = GetCurrentPers();
            SRPGUnitStats *pStats = &pPers->stats;
            pStats->psStrength.fpEffectiveValue = pStats->psStrength.fpBaseValue = 25;
            pStats->psDexterity.fpEffectiveValue = pStats->psDexterity.fpBaseValue = 25;
            pStats->psIntelligence.fpEffectiveValue = pStats->psIntelligence.fpBaseValue = 25;
            pPers->complection.tall = 0.7f;
            pPersName->SetString("");
            pNameStats->SetPersonage(0, "");
            UpdateAttributes();
        }
        break;
        }
        if (ht >= NUM_AREAS && ht < NUM_AREAS + VOICE_NUM_SLOTS_VISIBLE)
        {
            int nVoice = nVoicePos + ht - NUM_AREAS;
            if (nVoice >= 0 && nVoice < pVoices->size())
            {
                pI3D->BeginFX("buttons\\save\\select.wav");
                nCurrentVoice = nVoice;
                GetCurrentPers()->szVoicePrototype = (*pVoices)[nCurrentVoice].first;
                PlayVoiceSample();
                RedrawVoiceList();
            }
        }
    }
    else if (eScreenMode == ISM_STATS)
    {
        switch (ht)
        {
        case 0:
            ReturnToSkinSelect();
            break;
        case 1:
            FinishCreateParty();
            break;
        case 4: {
            pI3D->BeginFX("buttons\\messbox\\cancel.wav");
            pPerks->Confirm(false);
            CPersonage *pPers = GetCurrentPers();
            pSelWeapon = (pPers->weapons.GetSize() > 0) ? pPers->weapons[0] : 0;
            pSlots->SetCurrentWeapon(pSelWeapon);
            pSelQItem = (pPers->items.GetSize() > 0) ? pPers->items[0] : 0;
            pSlots->SetCurrentQItem(pSelQItem);
            pSelSpell = (pPers->spells.GetSize() > 0) ? pPers->spells[0] : 0;
            pSpells->SetCurrentSpell(pSelSpell);
        }
        break;
        }
    }
    else if (eScreenMode == ISM_CHOOSEPERS)
    {
        if (ht >= 0 && pClanName->IsActive())
        {
            surfaces[3]->Show(FALSE);
            pClanName->Show(FALSE);
            pClanName->Activate(false);
        }
        switch (ht)
        {
        case 0: {
            CString szMsg;
            IWorld.GetTextManager()->GetSystemString("reserved_04", szMsg);
            theInterface.DrawTextOverFrame(szMsg);

            pI3D->BeginFX("buttons\\messbox\\cancel.wav");
            IWorldConnector.LMPConnectAbort();
            pI3D->ExitFromModal();
        }
        break;
        case 1:
            Connect();
            break;
        case 2:
            pI3D->BeginFX("buttons\\camp\\move.wav");
            nLock = 1;
            pI3D->SetCapture(this);
            break;
        case 3:
            pI3D->BeginFX("buttons\\camp\\move.wav");
            nLock = 2;
            pI3D->SetCapture(this);
            break;
        case 5:
            pI3D->BeginFX("buttons\\save\\select.wav");
            NewParty();
            break;
        case 6:
            pI3D->BeginFX("buttons\\save\\select.wav");
            RenameParty();
            break;
        case 7: {
            pI3D->BeginFX("buttons\\save\\select.wav");
            if (nCurrentParty < 0 || nCurrentParty >= LMPPartySet.parties.size())
                break;
            CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, "lmp_deletepers",
                                                                     "lmp_deletepers_msg",
                                                                     0x00010000);
            pI3D->DoModal(pScr);
        }
        break;
        case 8:
            pI3D->BeginFX("buttons\\save\\select.wav");
            Redress();
            break;
        }
        if (ht >= NUM_AREAS && ht < NUM_AREAS + PERS_NUM_SLOTS_VISIBLE)
        {
            int nParty = nPos + ht - NUM_AREAS;
            if (nParty >= 0 && nParty < LMPPartySet.parties.size())
            {
                pI3D->BeginFX("buttons\\save\\select.wav");
                ChooseParty(nParty);
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::OnLButtonUp(UINT nFlags, CPoint point)
{
    CI3DScreen::OnLButtonUp(nFlags, point);
    if (pI3D->GetCapture() == this)
        pI3D->ReleaseCapture();
    nLock = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::OnIdle()
{
    CI3DScreen::OnIdle();
    if (nLock == 1)
        fPersAngle = (float)fmod(fPersAngle + GetFrameTime() * 2.f, FP_2PI);
    else if (nLock == 2)
        fPersAngle = (float)fmod(fPersAngle - GetFrameTime() * 2.f, FP_2PI);
    int n;
    if (pScroll->GetPos(n))
    {
        nPos = n;
        RedrawPersList();
    }
    if (pScrollVoices->GetPos(n))
    {
        nVoicePos = n;
        if (eScreenMode == ISM_SKIN)
            RedrawVoiceList();
    }
    if (eScreenMode == ISM_SKIN)
    {
        if (CanProceedToStats())
            figures[9]->SetAmbient(1, 1, 1);
        else
            figures[9]->SetAmbient(.5f, .5f, .5f);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::NewParty()
{
    if (eScreenMode != ISM_CHOOSEPERS)
        return;
    BeginPrimaryCreation();
    ASSERT(males.size() > 0);
    SetPrototype(males[0]);
    pNameStats->SetPersonage(0, "");
#ifndef NOGETDC
    surfaces[0]->GetDC();
    surfaces[0]->Clear();
    surfaces[0]->ReleaseDC();
#endif // NOGETDC
    fPersAngle = 0;
    SetMode(ISM_SKIN);
    pPersName->SetString("");
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::FinishCreateParty()
{
    if (eScreenMode != ISM_STATS || !HasCompletePrimaryDistribution() ||
        GetCurrentPers()->szName.IsEmpty())
    {
        pI3D->BeginFX("buttons\\messbox\\cancel.wav");
        return;
    }
    pI3D->BeginFX("buttons\\messbox\\ok.wav");
    CPersonage *pPers = GetCurrentPers();

    for (int i = pPers->weapons.GetSize() - 1; i >= 0; i--)
        if (pPers->weapons[i] != pSelWeapon)
            pPers->RemoveWeapon(pPers->weapons[i]);
    for (int i = pPers->items.GetSize() - 1; i >= 0; i--)
        if (pPers->items[i] != pSelQItem)
            pPers->RemoveQuick(pPers->items[i]);
    for (int i = pPers->spells.GetSize() - 1; i >= 0; i--)
        if (pPers->spells[i] != pSelSpell)
            pPers->RemoveSpell(pPers->spells[i]);

    pPers->dwCurrentWeapon = (pPers->weapons.GetSize() > 0) ? 0 : -1;

    if (pPers->spells.GetSize() > 0 && pPers->spells[0]->GetSpell())
    {
        bool bCanCast = true;
        CSpell *pSpell = pPers->spells[0]->GetSpell();
        SRPGUnitStats *pStats = &pPers->stats;
        if (!pStats->CanCastSpell(pSpell->GetMagicSubtype(), pSpell->GetComplexity()))
            bCanCast = false;
        if ((int)(pStats->RecalcMP() / pStats->GetManaBonusMultiplier()) <
            (int)pSpell->GetManaCost())
            bCanCast = false;
        if (!bCanCast)
        {
            CItemSpell *pItem = pPers->spells[0];
            IWorldClient.GetParty().AddItemToWagon(pItem);
            pPers->RemoveSpell(pItem);
        }
    }

    LMPPartySet.parties.emplace_back();
    nCurrentParty = LMPPartySet.parties.size() - 1;
    CLMPPartySet::SOneParty &party = LMPPartySet.parties[nCurrentParty];

    CString szWildcard;
    szWildcard.Format("%s*.mp", LMPPartySet.szDir);
    CFileFind find;
    int nMax = 0;
    BOOL bFound = find.FindFile(szWildcard);
    while (bFound)
    {
        bFound = find.FindNextFile();
        if (find.IsDots())
            continue;
        nMax = Max(nMax, atoi(find.GetFileTitle()));
    }
    find.Close();
    party.szFileName.Format("%d.mp", nMax + 1);

    // The unit's gear is doubly referenced by e.g.
    // 1) newParty() -> InitUnit / InitLmpPers -> CPersonage pPers -> addQuick
    // 2) newParty() -> deployUnit -> pUnit->AddQItem
    // This causes exception at party.party = IWorldClient.GetParty(); :
    // Store: writes party to memory, non-selected items that are only referenced by interface for
    // drawing are written. Load restores it, gets these extra items. "Normal" items are referenced
    // by char's inventory, but non-selected are 0-ref thus exception. Try to fix via inverting
    // deployUnit -> decreasing refs - seems to work?
    CPersonages personages;
    CParty &partyTMP = IWorldClient.GetParty();
    for (int i = 0; i < partyTMP.people.GetSize(); i++)
        personages.Add(&partyTMP.people[i]);
    IWorldClient.RemoveClientPersonages(personages);
    // exception here
    party.party = IWorldClient.GetParty();
    party.Store(LMPPartySet.szDir);
    FormPersList();
    SetMode(ISM_CHOOSEPERS);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::DeleteParty()
{
    if (nCurrentParty < 0 || nCurrentParty >= LMPPartySet.parties.size())
        return;
    CString szFile = LMPPartySet.szDir + LMPPartySet.parties[nCurrentParty].szFileName;
    DeleteFile(szFile);
    LMPPartySet.parties.erase(LMPPartySet.parties.begin() + nCurrentParty);

    if (nCurrentParty >= LMPPartySet.parties.size())
        nCurrentParty = LMPPartySet.parties.size() - 1;
    FormPersList();
    ChooseParty(nCurrentParty);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::RenameParty()
{
    if (nCurrentParty < 0 || nCurrentParty >= LMPPartySet.parties.size())
        return;
    pClanName->Show(TRUE);
    pClanName->Activate(true);
    CString szName = LMPPartySet.parties[nCurrentParty].party.people[0].szName;
    int nSub = szName.Find("|");
    if (nSub >= 0)
    {
        CString szClan = szName.Mid(nSub + 1);
        szClan.TrimLeft();
        pClanName->SetString(szClan);
        szName = szName.Left(nSub);
        szName.TrimRight();
    }
    else
        pClanName->SetString("");
#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[3];
    pSurf->Show(TRUE);
    pSurf->GetDC();
    pSurf->Clear();
    pSurf->DrawText(szName + " | ", CRect(0, 0, 130, 16), 1, PERS_COLOR_TEXT, DT_RIGHT);
    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::Redress()
{
    if (nCurrentParty < 0 || nCurrentParty >= LMPPartySet.parties.size())
        return;
    CI3DScreenCamp *pScreen = dbgnew CI3DScreenCamp(pI3D, true);
    pI3D->DoModal(pScreen, false);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::Connect()
{
    pI3D->BeginFX("buttons\\messbox\\ok.wav");
    if (nCurrentParty < 0 || nCurrentParty >= LMPPartySet.parties.size())
    {
        CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, "lmp_no_pers",
                                                                 "lmp_no_pers_msg", 0x00020000,
                                                                 MBF_OK);
        pI3D->DoModal(pScr);
        return;
    }
    LMPPartySet.Store();
    CI3DScreenConnection *pScreen = dbgnew CI3DScreenConnection(pI3D);
    pI3D->DoModal(pScreen);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::ReturnToSkinSelect()
{
    pNameStats->StopPrimaryRepeat();
    pI3D->BeginFX("buttons\\messbox\\cancel.wav");
    pNameStats->SetPersonage(0, "");
#ifndef NOGETDC
    surfaces[0]->GetDC();
    surfaces[0]->Clear();
    surfaces[0]->ReleaseDC();
#endif // NOGETDC
    pPerks->Confirm(false);
    SetMode(ISM_SKIN);
    pPersName->SetString(GetCurrentPers()->szName);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::BeginPrimaryCreation()
{
    pNameStats->StopPrimaryRepeat();
    nPrimaryCreationWeight = 3 * CreationPrimaryWeight(25);
    bPrimaryCreationActive = true;
}

void CI3DScreenChoosePers::EndPrimaryCreation()
{
    if (pNameStats)
        pNameStats->StopPrimaryRepeat();
    bPrimaryCreationActive = false;
    nPrimaryCreationWeight = 0;
}

bool CI3DScreenChoosePers::GetCreationPrimaryValues(int values[3])
{
    if (!bPrimaryCreationActive || IWorldClient.GetParty().people.GetSize() != 1)
        return false;
    const SRPGUnitStats &stats = IWorldClient.GetParty().people[0].stats;
    const float base[3] = { stats.psStrength.fpBaseValue, stats.psDexterity.fpBaseValue,
                            stats.psIntelligence.fpBaseValue };
    for (int i = 0; i < 3; ++i)
    {
        // Check before the cast: rejects nonfinite and nonintegral values unchanged.
        if (!(base[i] >= 15 && base[i] <= 32))
            return false;
        values[i] = static_cast<int>(base[i]);
        if (base[i] != values[i])
            return false;
    }
    return true;
}

bool CI3DScreenChoosePers::GetCreationPrimaryPoints(int &points)
{
    points = -1;
    int values[3];
    if (!GetCreationPrimaryValues(values))
        return false;
    const int remaining = nPrimaryCreationWeight - CreationPrimaryWeight(values[0]) -
                          CreationPrimaryWeight(values[1]) - CreationPrimaryWeight(values[2]);
    if (remaining < 0)
        return false;
    points = remaining;
    return true;
}

bool CI3DScreenChoosePers::CanEditCreationPrimary()
{
    int points;
    return eScreenMode == ISM_SKIN && GetCreationPrimaryPoints(points);
}

bool CI3DScreenChoosePers::CanAdjustCreationPrimary(int primary, int delta)
{
    if (primary < 0 || primary >= 3 || (delta != -1 && delta != 1))
        return false;
    int points, values[3];
    if (!GetCreationPrimaryPoints(points) || !GetCreationPrimaryValues(values))
        return false;
    const int value = values[primary];
    if (delta < 0)
        return value > 15;
    return value < 32 &&
           points >= CreationPrimaryWeight(value + 1) - CreationPrimaryWeight(value);
}

bool CI3DScreenChoosePers::AdjustCreationPrimary(int primary, int delta)
{
    if (eScreenMode != ISM_SKIN || !CanAdjustCreationPrimary(primary, delta))
        return false;
    SRPGUnitStats &stats = IWorldClient.GetParty().people[0].stats;
    float *base[3] = { &stats.psStrength.fpBaseValue, &stats.psDexterity.fpBaseValue,
                       &stats.psIntelligence.fpBaseValue };
    float *effective[3] = { &stats.psStrength.fpEffectiveValue,
                            &stats.psDexterity.fpEffectiveValue,
                            &stats.psIntelligence.fpEffectiveValue };
    *base[primary] += delta;
    *effective[primary] = *base[primary];
    return true;
}

bool CI3DScreenChoosePers::HasCompletePrimaryDistribution()
{
    int points;
    return GetCreationPrimaryPoints(points) && points == 0;
}

bool CI3DScreenChoosePers::CanProceedToStats()
{
    CString name = pPersName->GetString();
    return eScreenMode == ISM_SKIN && HasCompletePrimaryDistribution() &&
           !name.IsEmpty();
}

void CI3DScreenChoosePers::CheckProceedToStats()
{
    CString szName = pPersName->GetString();
    if (CanProceedToStats())
    {
        CPersonage *pPers = GetCurrentPers();
        pI3D->BeginFX("buttons\\messbox\\ok.wav");
        pSelWeapon = (pPers->weapons.GetSize() > 0) ? pPers->weapons[0] : 0;
        pSlots->SetCurrentWeapon(pSelWeapon);
        pSelQItem = (pPers->items.GetSize() > 0) ? pPers->items[0] : 0;
        pSlots->SetCurrentQItem(pSelQItem);
        pSelSpell = (pPers->spells.GetSize() > 0) ? pPers->spells[0] : 0;
        pSpells->SetCurrentSpell(pSelSpell);
        pPers->szName = szName;
        pNameStats->SetPersonage(0, szName);
        UpdateAttributes();
        SetMode(ISM_STATS);
    }
    else
        pI3D->BeginFX("buttons\\messbox\\cancel.wav");
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::RefreshProgression()
{
    if(!pCurUnit || !pNameStats)return;
    CPersonage* pers=GetCurrentPers();if(!pers)return;
    pCurUnit->ChangeWeapon(-1);
    for(int i=0;i<QI_MAX_WEAPONS;++i)pCurUnit->RemoveWeapon(i);
    for(int i=0;i<pers->weapons.GetSize();++i)pCurUnit->AddWeapon(pers->weapons[i]);
    pCurUnit->ConvertWeaponsOnClient();
    pCurUnit->ChangeWeapon(pers->dwCurrentWeapon);
    pCurUnit->GetUnitSpecific()->NotifyWeaponUpdated();
    UpdateAttributes();
}

void CI3DScreenChoosePers::UpdateAttributes()
{
    if (pCurUnit)
    {
        CPersonage *pPers = GetCurrentPers();
        pPers->stats.ResurrectAll();
        pPers->stats.RecalcComplectionFromPrimaryStats(pPers->complection.strength,
                                                       pPers->complection.grace,
                                                       pPers->complection.tall);
        pCurUnit->SetComplection(pPers->complection);
        pCurUnit->SetupCampUnitWithUnitStats(&GetCurrentPers()->stats);
        pCurUnit->GetUnitSpecific()->NotifyHitPointsUpdated();
    }
    pNameStats->UpdateStats();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DScreenChoosePers::CanClickOnItem(CItem *pItem, EItemScreenParts eFrom)
{
    if (pItem->GetTypeID() == ITID_ITEM_SPELL && pItem->GetSpell(0))
    {
        CSpell *pSpell = pItem->GetSpell(0);
        bool bCanCast = true;
        SRPGUnitStats *pStats = &GetCurrentPers()->stats;
        if (!pStats->CanCastSpell(pSpell->GetMagicSubtype(), pSpell->GetComplexity()))
            bCanCast = false;
        if ((int)(pStats->RecalcMP() / pStats->GetManaBonusMultiplier()) <
            (int)pSpell->GetManaCost())
            bCanCast = false;
        return bCanCast;
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::ClickOnItem(CItem *pItem, EItemScreenParts eFrom)
{
    CPersonage *pPers = GetCurrentPers();
    switch (eFrom)
    {
    case ISP_CAMPSLOTS:
        if (pItem->GetTypeID() == ITID_ITEM_WEAPON)
        {
            pSelWeapon = pItem;
            pSlots->SetCurrentWeapon(pItem);
            pI3D->BeginFX("buttons\\messbox\\ok.wav");
        }
        else if (pItem->GetTypeID() == ITID_ITEM_QUICK)
        {
            pSelQItem = pItem;
            pSlots->SetCurrentQItem(pItem);
            pI3D->BeginFX("buttons\\messbox\\ok.wav");
        }
        else if (pItem->GetTypeID() == ITID_ITEM_SPELL)
        {
            pSelSpell = pItem;
            pSpells->SetCurrentSpell(pItem);
            pI3D->BeginFX("buttons\\messbox\\ok.wav");
        }
        break;
    }
    UpdateAttributes();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CI3DScreen::OnKeyDown(nChar, nRepCnt, nFlags);
    if (GetProcessed())
        return;
    switch (nChar)
    {
    case 13:
        if (pClanName->IsActive())
        {
            CString szNewName(pClanName->GetString());
            szNewName.TrimLeft();
            szNewName.TrimRight();
            if (!szNewName.IsEmpty())
                szNewName = CString(" | ") + szNewName;
            CString szName = LMPPartySet.parties[nCurrentParty].party.people[0].szName;
            CString szOldName;
            int nSub = szName.Find("|");
            if (nSub >= 0)
                szOldName = szName.Left(nSub);
            else
                szOldName = szName;
            szOldName.TrimLeft();
            szOldName.TrimRight();
            szNewName = szOldName + szNewName;
            LMPPartySet.parties[nCurrentParty].party.people[0].szName = szNewName;
            LMPPartySet.parties[nCurrentParty].Store(LMPPartySet.szDir);
            surfaces[3]->Show(FALSE);
            pClanName->Show(FALSE);
            pClanName->Activate(false);
            ChooseParty(nCurrentParty);
        }
        else if (eScreenMode == ISM_CHOOSEPERS)
            Connect();
        else if (eScreenMode == ISM_STATS)
            FinishCreateParty();
        else if (eScreenMode == ISM_SKIN)
            CheckProceedToStats();
        SetProcessed();
        break;
    case VK_ESCAPE:
        if (pClanName->IsActive())
        {
            surfaces[3]->Show(FALSE);
            pClanName->Show(FALSE);
            pClanName->Activate(false);
            RedrawPersList();
        }
        else if (eScreenMode == ISM_CHOOSEPERS)
        {
            CString szMsg;
            IWorld.GetTextManager()->GetSystemString("reserved_04", szMsg);
            theInterface.DrawTextOverFrame(szMsg);

            IWorldConnector.LMPConnectAbort();
            pI3D->BeginFX("buttons\\messbox\\cancel.wav");
            pI3D->ExitFromModal();
        }
        else if (eScreenMode == ISM_STATS)
            ReturnToSkinSelect();
        else if (eScreenMode == ISM_SKIN)
        {
            pI3D->BeginFX("buttons\\messbox\\cancel.wav");
            SetMode(ISM_CHOOSEPERS);
        }
        SetProcessed();
        break;
    case VK_UP:
        if (eScreenMode == ISM_CHOOSEPERS)
        {
            pI3D->BeginFX("buttons\\save\\select.wav");
            --nCurrentParty;
            MoveScrollToSelection();
            SetProcessed();
        }
        else if (eScreenMode == ISM_SKIN)
        {
            pI3D->BeginFX("buttons\\save\\select.wav");
            --nCurrentVoice;
            MoveScrollVoiceToSelection();
            PlayVoiceSample();
            SetProcessed();
        }
        break;
    case VK_DOWN:
        if (eScreenMode == ISM_CHOOSEPERS)
        {
            pI3D->BeginFX("buttons\\save\\select.wav");
            ++nCurrentParty;
            MoveScrollToSelection();
            SetProcessed();
        }
        else if (eScreenMode == ISM_SKIN)
        {
            pI3D->BeginFX("buttons\\save\\select.wav");
            ++nCurrentVoice;
            MoveScrollVoiceToSelection();
            PlayVoiceSample();
            SetProcessed();
        }
        break;
    case VK_PRIOR:
        if (eScreenMode == ISM_CHOOSEPERS)
        {
            pI3D->BeginFX("buttons\\save\\select.wav");
            nCurrentParty -= PERS_NUM_SLOTS_VISIBLE;
            MoveScrollToSelection();
            SetProcessed();
        }
        else if (eScreenMode == ISM_SKIN)
        {
            pI3D->BeginFX("buttons\\save\\select.wav");
            nCurrentVoice -= VOICE_NUM_SLOTS_VISIBLE;
            MoveScrollVoiceToSelection();
            PlayVoiceSample();
            SetProcessed();
        }
        break;
    case VK_NEXT:
        if (eScreenMode == ISM_CHOOSEPERS)
        {
            pI3D->BeginFX("buttons\\save\\select.wav");
            nCurrentParty += PERS_NUM_SLOTS_VISIBLE;
            MoveScrollToSelection();
            SetProcessed();
        }
        else if (eScreenMode == ISM_SKIN)
        {
            pI3D->BeginFX("buttons\\save\\select.wav");
            nCurrentVoice += VOICE_NUM_SLOTS_VISIBLE;
            MoveScrollVoiceToSelection();
            PlayVoiceSample();
            SetProcessed();
        }
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::MoveScrollToSelection()
{
    if (nCurrentParty < 0)
        nCurrentParty = 0;
    if (nCurrentParty >= LMPPartySet.parties.size())
        nCurrentParty = LMPPartySet.parties.size() - 1;
    if (nPos + PERS_NUM_SLOTS_VISIBLE <= nCurrentParty)
        nPos = nCurrentParty - PERS_NUM_SLOTS_VISIBLE + 1;
    if (nPos > nCurrentParty)
        nPos = nCurrentParty;
    pScroll->SetPos(nPos);
    pScroll->GetPos(nPos);
    ChooseParty(nCurrentParty);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::MoveScrollVoiceToSelection()
{
    if (nCurrentVoice < 0)
        nCurrentVoice = 0;
    if (nCurrentVoice >= pVoices->size())
        nCurrentVoice = pVoices->size() - 1;
    if (nVoicePos + VOICE_NUM_SLOTS_VISIBLE <= nCurrentVoice)
        nVoicePos = nCurrentVoice - VOICE_NUM_SLOTS_VISIBLE + 1;
    if (nVoicePos > nCurrentVoice)
        nVoicePos = nCurrentVoice;
    pScrollVoices->SetPos(nVoicePos);
    pScrollVoices->GetPos(nVoicePos);
    GetCurrentPers()->szVoicePrototype = (*pVoices)[nCurrentVoice].first;
    RedrawVoiceList();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenChoosePers::PlayVoiceSample()
{
    pI3D->StopFX(hVoiceSound);
    hVoiceSound = SND_EMPTY;
    const SUDBUnitPrototype *pPrototype = theUnitDatabase
                                              .prototypes[GetCurrentPers()->szVoicePrototype];
    if (!pPrototype)
        return;
    const SAckHeroAck *pHeroAcks = theAckDatabase.ackHeroAcks[pPrototype->szName];
    if (!pHeroAcks)
        return;
    const SAckEntryBoredVector *pBoredAcks = &(pHeroAcks->ackBored);
    if (!pBoredAcks)
        return;
    DWORD dwAckFlags = ACK_WALKING | ACK_DAY | ACK_OUTDOORS | ACK_SKYCLEAR;
    std::vector<const SAckEntry *> result;
    for (auto pos = pBoredAcks->begin(); pos != pBoredAcks->end(); ++pos)
    {
        if (pos->IsFlags(dwAckFlags) && pos->nProbability != 0)
            result.push_back(&*pos);
    }
    if (result.size() == 0)
        return;
    int i, j, num = result.size();
    for (i = 0, j = Random(num); i < num && hVoiceSound == SND_EMPTY; ++i, j = (j + 1) % num)
    {
        const SAckEntry *pAck = result[j];
        if (pAck)
            hVoiceSound = pI3D->BeginFX(pAck->szFileName.c_str());
    }
}
/////////////////////////////////////////////////////////////////////////////////////
float CI3DScreenChoosePers::GetItemsWeight()
{
    CPersonage *pPers = GetCurrentPers();
    float fItemsWeight = 0;
    int i;
    // weapons
    if (pSelWeapon)
        fItemsWeight += pSelWeapon->CItem::GetWeight();
    // quick items
    if (pSelQItem)
        fItemsWeight += pSelQItem->GetWeight();
    // spells
    if (pSelSpell)
        fItemsWeight += pSelSpell->GetWeight();
    // armors
    for (i = 0; i < pPers->armors.GetSize(); ++i)
    {
        if (pPers->armors[i])
            fItemsWeight += pPers->armors[i]->GetWeight();
    }
    return fItemsWeight;
}
/////////////////////////////////////////////////////////////////////////////////////
