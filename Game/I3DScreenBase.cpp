#include "StdAfx.h"
#include "GameJournal.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenBase.h"
#include "OwnedRender/E2Host.h"
#include "OwnedRender/E1Scene.h"
#include "I3DScreenGlobalMap.h"
#include "I3DScreenEcsapeMenu.h"
#include "I3DScreenMessageBox.h"
#include "I3DScreenSaveLoad.h"
#include "I3DScrollText.h"
#include "I3DMain.h"
#include "Camera.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "A3Cursor.h"
#include "ScenarioTracker.h"
#include "WorldInterface.h"
#include "WorldClient.h"
#include "WorldServer.h"
#include "TextManager.h"
#include "StringProcessing.h"
#include "PlayerClient.h"
#include "PlayerServer.h"
#include "UnitServer.h"
#include "MusicSystem.h"
#include "QuestDatabase.h"
#include "UnitDatabase.h"
#include "ItemManager.h"
#include "randomGen.h"
#include "GameMain.h"
#include "KeyMap.h"
#include "UnitClientSpecific.h"
#include "FlyingHitPoints.h"
/////////////////////////////////////////////////////////////////////////////////////
const int BASE_NUM_BRIEFS_VISIBLE = 5;

const COLORREF BASE_COLOR_TEXT = RGB(228, 215, 167);

// const int HUMAN_TALK_ANIMS[4] = { 8, 9, 6, 7 };
// const int HUMAN_IDLE_ANIMS[4] = { 12, 10, 11, 13 };

const int HUMAN_TALK_ANIMS[4] = { 1, 6, 7, 8 };
const int HUMAN_IDLE_ANIMS[4] = { 2, 10, 12, 13 };

const int BRIEFING_ITEM_XS[7] = { 300, 500, 200, 400, 600, 75, 725 };
const int BRIEFING_ITEM_YS[7] = { 300, 300, 300, 300, 300, 500, 500 };

/////////////////////////////////////////////////////////////////////////////////////
void SwitchCallback2(CUnit *pU)
{
    if (!pU)
        return;
    CUnit *pUnit = theWorld.GetUnit(pU->GetID());
    if (!pUnit)
        return;
    CI3DScreenBase *pScreen = ((CI3DScreenBase *)theInterface.GetCurrentScreen());
    for (int i = 0; i < pScreen->parts.size(); i++)
        if (pUnit == pScreen->parts[i].pUnit)
            pScreen->PlayRndAnimation(pUnit, false);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::StoreSpecific()
{
    interfaceState.Write(0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::OnActivate()
{
    journalTopicTarget = -1;
    eKickMode = KM_NONE;

    bNeedAutosave = false;
    if (interfaceState.GetSize())
        interfaceState.SetSize(0);
    else if (theWorld.GetPlayer()->GetPlayer()->prevInterface != IFACE_CAMP)
        bNeedAutosave = true;

    SScenarioZone *pB = theMap.GetZoneByName(IWorldClient.GetCurrentZoneName());
    ASSERT(pB);

    GetPersLocalNames();

    AddTexture("saveload");

    CI3DFigure *pFig;

    pFig = AddFigure(); // bg topics
    pFig->MakeBar(20, 100, 200, 430, 600, 570);
    pFig->SetTexture(NULL);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 128));

    pFig = AddFigure(); // frame topics
    pFig->MakeBarFrame(10, 100, 195, 425, 605, 575, 5, 2, 254, 180, 249);

    pFig = AddFigure(); // bg dialog
    pFig->MakeBar(20, 100, 155, 416, 645, 580);
    pFig->SetTexture(NULL);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));

    pFig = AddFigure(); // frame dialog
    pFig->MakeBarFrame(20, 100, 150, 411, 650, 585, 5, 2, 254, 180, 249);

    AddSurface(dbgnew CI3DSurface(pI3D, 200, 430, 600, 570)); // topics
    AddSurface(dbgnew CI3DSurface(pI3D, 0, 0, 250, 30));      // name tip

    AddObject(pScrollTopics = dbgnew CI3DScrollBar(pI3D, CRect(584, 440, 594, 560), 3)); // dialog
    AddObject(pScroll = dbgnew CI3DScrollText(pI3D, "", CRect(165, 426, 635, 570)));

    for (int i = 0; i < figures.GetSize(); i++)
        figures[i]->Show(FALSE);
    for (int i = 0; i < surfaces.GetSize(); i++)
        surfaces[i]->Show(FALSE);
    for (int i = 0; i < pObjects.GetSize(); i++)
        pObjects[i]->Show(FALSE);

    eMode = BSM_MOVEMENT;
    bDoCheckExit = true;
    nCheckExit = 0;

    pMainHero = 0;

    fOldFogDist = theSettings.fpTerrainBorderFogDistance;
    theSettings.fpTerrainBorderFogDistance = 4;

    const CString szMusicZone = IWorldClient.GetCurrentZoneName();
    const SScenarioZone *pZone = theMap.GetZoneByName(szMusicZone);
    const CString szMusicSelector = pZone ?
        (CMusicSystem::UseZoneSpecificMusic() ? szMusicZone : pZone->szAllodName) : CString("gipat");
    GameJournal::Event("music", "briefing_selector", "zone=%s selector=%s zone_found=%d",
                       GameJournal::Quote(szMusicZone).c_str(),
                       GameJournal::Quote(szMusicSelector).c_str(), pZone != nullptr);
    theWorld.GetMusicSystem()->SetBriefingMode(szMusicSelector);
    if (pZone)
        bCageBriefing = pZone->bCageBriefings;
    else
        bCageBriefing = false;

    IWorldClient.EnableAmbientSounds();
    IWorldServer.SetWeather(EW_CLEAR, 0);

    AddHandler(dbgnew CI3DHandlerSystemKeys(pI3D));

    AddHandler(dbgnew CI3DHandlerNewCamera(pI3D));
    theCamera.ResetFlag(CAM_CANT_SCROLL);

    if (pB->bRestrictCamera)
        theCamera.SetScrollRestriction(CPlot3D(pB->fRestrictX, pB->fRestrictY, 0), pB->fRadius);

    theCamera.SetFlag(CAM_ALTERNATIVE_RESTRICTIONS);
    theCamera.SetCarrier(DWORD(-1));

    bWasForcedBriefing = false;
    hPhraseSound = SND_EMPTY;
    pWhosTalking = 0;

    if (IWorldConnector.IsLMP())
    {
        AddObject(pFaces = dbgnew CI3DBaseFaces(pI3D));
        LMPPersonsUpdate(true);
    }
    else
        pFaces = 0;

    CMapObjPtrHash &objects = theWorld.GetPlayer()->GetPlayer()->visibleObjects;
    CMapObjPtrHashIterator mapobjIter;
    CMapObject *pMO = mapobjIter.GetFirst(&objects);
    while (pMO)
    {
        if (IsIDofPersonage(pMO->GetID()))
            nBriefingPersIDs.push_back(pMO->GetID());
        pMO = mapobjIter.GetNext();
    }

    fIdleTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::OnDeactivate()
{
    briefingWeaponsHidden = false;
    RestoreBriefingWeapons();
    theSettings.fpTerrainBorderFogDistance = fOldFogDist;

    IWorldClient.StopAllSounds();
    IWorldClient.DisableAmbientSounds();

    theCamera.SetFlag(CAM_CANT_SCROLL);
    theCamera.ResetScrollRestriction();
    theCamera.ResetFlag(CAM_ALTERNATIVE_RESTRICTIONS);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScreenBase::Draw(CRenderer *pRend)
{
    IWorldClient.Draw();
    OnMouseMove(0, theCursor.GetPos());
    pI3D->GetRenderer()->Clear(0, 0, D3DCLEAR_ZBUFFER, 0, 1, 0);
    return CI3DScreen::Draw(pRend);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::AddControlUnit(CUnit *pUnit)
{
    CArray<CPersonage> *people = &IWorldClient.GetParty().people;
    if (people->GetSize() == 0)
        return;

    int j = 0;
    bool bInParty = false;
    for (int i = 0; i < people->GetSize(); i++)
    {
        if (pUnit->GetID() == (*people)[i].nID)
        {
            pUnits.InsertAt(j, pUnit);
            bInParty = true;
            CUnit *pU = theWorldServer.GetUnit(pUnit->GetID());
            if (pU)
            {
                CUnitServer *pUS = (CUnitServer *)pU->GetUnitSpecific();
                //				pUS->ChangeWeapon( (*people)[i].dwCurrentWeapon );
            }
            break;
        }
        if (j < pUnits.GetSize() && pUnits[j]->GetID() == (*people)[i].nID)
            j++;
    }
    if (!bInParty)
        pUnits.Add(pUnit);

    if (pUnit->GetID() == (*people)[0].nID)
    {
        pMainHero = pUnit;
        theWorld.SelectObject(pUnit);
        theCamera.SetCarrier(pUnit);

        SScenarioZone *pB = theMap.GetZoneByName(IWorldClient.GetCurrentZoneName());
        ASSERT(pB);
        CGSVariableSet vars;
        CString szVarName;
        CStringArray szVarParts;
        szVarName.Format("b.%s", pB->szName);
        IWorld.GSGetVars(szVarName, vars);
        CGSVariableSet::const_iterator p;
        for (p = vars.begin(); p != vars.end(); ++p)
            if (p->second == SCN_BRIEFING_AVAILABLE)
            {
                NStrProc::SplitString(p->first.c_str(), szVarParts, '.');
                if (szVarParts.GetSize() == 3)
                {
                    szBriefing = szVarParts[2];
                    bWasForcedBriefing = true;
                    PlayBriefing();
                    break;
                }
            }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::RemoveControlUnit(CUnit *pUnit)
{
    if (pUnit == pMainHero)
        pMainHero = 0;
    for (int i = 0; i < pUnits.GetSize(); i++)
        if (pUnit->GetID() == pUnits[i]->GetID())
        {
            pUnits.RemoveAt(i);
            break;
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::LMPPersonsUpdate(bool bAddDelete)
{
    if (!IWorldConnector.IsLMP())
        return;
    if (!bAddDelete)
        return;
    if (pFaces)
        pFaces->AddFaces();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::ForceBriefing(LPCTSTR pszBriefing)
{
    szBriefing = pszBriefing;
    PlayBriefing();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::OnLButtonDown(UINT nFlags, CPoint point)
{
    // CRAP{ - for advanced kick/ban
    if (eKickMode != KM_NONE)
    {
        if (pFaces)
            pFaces->OnLButtonDown(nFlags, point);
        return;
    }
    // CRAP} - for advanced kick/ban

    CI3DScreen::OnLButtonDown(nFlags, point);
    if (GetProcessed())
        return;
    switch (eMode)
    {
    case BSM_BRIEFING:
        if (GetProcessed())
            return;
        else
        {
            pI3D->BeginFX("buttons\\base\\click1.wav");
            NextCommand();
        }
        break;

    case BSM_SUMMARY:
        if (GetProcessed())
            return;
        else
        {
            pI3D->BeginFX("buttons\\base\\click1.wav");
            EndBriefing();
        }
        break;

    case BSM_MOVEMENT: {
        CUnit *pUnit = theWorld.PickFirstUnit(point);
        if (pUnit && !pUnit->IsDead())
        {
            // for ( auto it =  walks.begin(); it != walks.end(); ++it )
            //	if ( it->pUnit->GetID() == pUnit->GetID() )
            //		return;
            // if walking then no processing
            CUnit *pU = theWorldServer.GetUnit(pUnit->GetID());
            if (pU)
            {
                CUnitServer *pUS = (CUnitServer *)pU->GetUnitSpecific();
                if (pUS && !pUS->HasNoCommand())
                    return;
            }
            pI3D->BeginFX("buttons\\base\\click1.wav");
            GameJournal::ObserveObject(pUnit, "talk");
            InitTopicMode(pUnit);
            return;
        }

        if (bCageBriefing)
            return;

        CPlot3D plot;
        plot = theWorld.GetTerrainXYZFromScreenXYWithBridge(point.x, point.y);

        // checking exits
        /*
        int nExit;
        bDoCheckExit = false;
        if ( IWorldClient.IsExitPoint( plot, &nExit ) )
        {
                nCheckExit = nExit;
                bDoCheckExit = true;
        }
        */
        if (pMainHero)
        {
            CUnit *pU = theWorldServer.GetUnit(pMainHero->GetID());
            if (pU && !pU->CheckFlag(UNIT_BLOCKED))
            {
                CUnitServer *pUS = (CUnitServer *)pU->GetUnitSpecific();
                pUS->logic.StartMove(plot);
            }
        }
    }
    break;

    case BSM_TOPIC: {
        int ht = HitTest(point);
        JournalHit(ht);
        if (ht >= 0 && ht < BASE_NUM_BRIEFS_VISIBLE && ht + nPos < szBriefings.GetSize())
        {
            pI3D->BeginFX("buttons\\base\\click1.wav");
            szBriefing = szBriefings[ht + nPos];
            GameJournal::Event("WORLD", "interaction", "action=talk topic=%s target=%d", GameJournal::Quote(szBriefing).c_str(), journalTopicTarget);
            RemoveAllAreas();
            PlayBriefing(false);
        }
    }
    break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    // CRAP{ - for advanced kick/ban
    if (eKickMode != KM_NONE)
        return;
    // CRAP} - for advanced kick/ban

    CI3DScreen::OnLButtonDblClk(nFlags, point);
    if (GetProcessed())
        return;
    switch (eMode)
    {
    case BSM_MOVEMENT: {
        CUnit *pUnit = theWorld.PickFirstUnit(point);
        if (pUnit && !pUnit->IsDead())
        {
            for (auto it = walks.begin(); it != walks.end(); ++it)
                if (it->pUnit->GetID() == pUnit->GetID())
                    return;

            OnLButtonDown(nFlags, point);
            return;
        }

        if (bCageBriefing)
            return;

        CPlot3D plot;
        plot = theWorld.GetTerrainXYZFromScreenXYWithBridge(point.x, point.y);

        // checking exits
        /*
        int nExit;
        bDoCheckExit = false;
        if ( IWorldClient.IsExitPoint( plot, &nExit ) )
        {
                nCheckExit = nExit;
                bDoCheckExit = true;
        }
        */
        if (pMainHero)
        {
            CUnit *pU = theWorldServer.GetUnit(pMainHero->GetID());
            if (pU && !pU->CheckFlag(UNIT_BLOCKED))
            {
                CUnitServer *pUS = (CUnitServer *)pU->GetUnitSpecific();
                pUS->logic.StartMove(plot, true);
            }
        }
    }
    break;

    default:
        OnLButtonDown(nFlags, point);
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    // CRAP{ - for advanced kick/ban
    if (eKickMode != KM_NONE)
    {
        if (nChar == VK_ESCAPE)
            eKickMode = KM_NONE;
        return;
    }
    // CRAP} - for advanced kick/ban

    CI3DScreen::OnKeyDown(nChar, nRepCnt, nFlags);
    if (GetProcessed()) return;
    if (nChar == 'N' && ModRuntime::Current() &&
        !pI3D->IsAltDown() && !pI3D->IsCtrlDown()) {
        if ((nFlags & 0x4000) == 0) {
            if (pI3D->IsShiftDown()) theCamera.ToggleFalloutMode();
            else theCamera.NormalizeNorth();
        }
        SetProcessed();
        return;
    }
    switch (nChar)
    {
    case VK_ESCAPE:
        if (eMode == BSM_MOVEMENT)
            pI3D->DoModal(dbgnew CI3DScreenEscapeMenu(pI3D));
        else if (eMode == BSM_BRIEFING)
        {
            pI3D->BeginFX("buttons\\base\\click1.wav");
            while (eMode == BSM_BRIEFING)
                NextCommand(true);
        }
        else if (eMode == BSM_SUMMARY)
        {
            pI3D->BeginFX("buttons\\base\\click1.wav");
            EndBriefing();
        }
        else if (eMode == BSM_APPROACH)
            PlayBriefing(true, false);
        else if (eMode == BSM_TOPIC)
            eMode = BSM_MOVEMENT;
        SetProcessed();
        return;
    case 13:
        if (IWorldConnector.IsLMP())
        {
            pI3D->BeginChatLine();
            SetProcessed();
            return;
        }
        if (eMode == BSM_BRIEFING)
        {
            pI3D->BeginFX("buttons\\base\\click1.wav");
            while (eMode == BSM_BRIEFING)
                NextCommand(true);
        }
        else if (eMode == BSM_SUMMARY)
        {
            pI3D->BeginFX("buttons\\base\\click1.wav");
            EndBriefing();
        }
        SetProcessed();
        return;
    case VK_BACK:
        if (IWorldConnector.IsLMP())
        {
            pI3D->ClearChatLines();
            SetProcessed();
            return;
        }
        break;
    case VK_SPACE:
        if (eMode == BSM_BRIEFING)
        {
            pI3D->BeginFX("buttons\\base\\click1.wav");
            NextCommand();
        }
        else if (eMode == BSM_SUMMARY)
        {
            pI3D->BeginFX("buttons\\base\\click1.wav");
            EndBriefing();
        }
        else if (eMode == BSM_APPROACH)
            PlayBriefing(true, false);
        SetProcessed();
        return;
    }
    if (eMode != BSM_MOVEMENT)
        return;
    switch (NKeyMap::keyMap[nFlags & 0x1ff])
    {
    case KA_QSAVE:
        CI3DScreenSaveLoad::QuickSave("saveq");
        break;
    case KA_QLOAD:
        CI3DScreenSaveLoad::QuickLoad("saveq");
        break;
    case KA_CAMERA_TRACK:
        if (pMainHero)
            theCamera.SetCarrier(pMainHero);
        SetProcessed();
        return;
    /*
    case KA_TUTORIAL_SCRIPT:
            pI3D->CheckMissionTutorials( true );
            SetProcessed();
            break;
    */
    case KA_OBJECTIVES: {
        SScenarioZone *pBZ = theMap.GetZoneByName(IWorldClient.GetCurrentZoneName());
        if (!pBZ)
            break;
        bool bAreQuests = false;
        CStringArray szVarParts;
        CGSVariableSet vars;
        IWorld.GSGetVars("q.", vars);
        for (auto p = vars.begin(); p != vars.end(); ++p)
        {
            NStrProc::SplitString(p->first.c_str(), szVarParts, '.');
            if (szVarParts.GetSize() != 3)
                continue;
            if (p->second == SCN_OBJECTIVE_AVAILABLE)
            {
                SScenarioZone *pZ = theMap.GetZoneByName(szVarParts[1]);
                if (!pZ)
                    continue;
                if (!IWorldConnector.IsLMP() &&
                    pZ->szAllodName.CompareNoCase(pBZ->szAllodName) != 0)
                    continue;
                bAreQuests = true;
                break;
            }
        }
        if (bAreQuests)
        {
            CI3DScreenObjectives *pScr = dbgnew CI3DScreenObjectives(pI3D);
            pScr->SetParams(IWorldClient.GetCurrentZoneName(), 0, FALSE, TRUE);
            pI3D->DoModal(pScr);
        }
    }
    break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
class CRemoveWalkingUnitFunctional
{
public:
    bool operator()(const SWalkingUnit &unit) { return (unit.nMode == 0); }
};
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::OnMouseMove(UINT nFlags, CPoint point)
{
    // CRAP{ - for advanced kick/ban
    if (eKickMode != KM_NONE)
    {
        theCursor.SetMode(CT_CANCEL);
        if (pFaces)
            pFaces->OnMouseMove(nFlags, point);
        return;
    }
    // CRAP} - for advanced kick/ban

    theCursor.SetMode(CT_DEFAULT);
    CI3DScreen::OnMouseMove(nFlags, point);

    if (eMode == BSM_TOPIC)
    {
        int ht = HitTest(point);
        int nNewTopic = -1;
        if (ht >= 0 && ht < BASE_NUM_BRIEFS_VISIBLE && ht + nPos < szBriefings.GetSize())
            nNewTopic = ht + nPos;
        if (nNewTopic != nCurrentTopic)
        {
            nCurrentTopic = nNewTopic;
            RedrawTopics();
        }
    }
    else if (eMode == BSM_BRIEFING)
        theCursor.SetMode(CT_TALK);
    else if (eMode == BSM_SUMMARY)
        theCursor.SetMode(CT_DEFAULT);

    if (pI3D->IsRButDown())
        return;

    if (eMode != BSM_MOVEMENT)
        return;

    CPlot3D plot;

    CUnit *pUnit = theWorld.PickFirstUnit(point);
    if (pUnit && !pUnit->IsDead())
    {
        int nUnitID = pUnit->GetID();
        if (IWorldConnector.IsLMP() && pMainHero && pMainHero->GetID() == nUnitID)
            return;
        // for ( auto it =  walks.begin(); it != walks.end(); ++it )
        //	if ( it->pUnit->GetID() == nUnitID )
        //		return;
        // if walking then no processing
        CUnit *pU = theWorldServer.GetUnit(nUnitID);
        if (pU)
        {
            CUnitServer *pUS = (CUnitServer *)pU->GetUnitSpecific();
            if (pUS && !pUS->HasNoCommand())
                return;
        }

        FillBriefingsList(nUnitID);

        if (szBriefings.GetSize() > 0) {
            theCursor.SetMode(CT_TALK);
            const auto identity = OwnedRender::E1Scene().Identity(nUnitID);
            OwnedRender::E2SetHoveredObject(identity.id, identity.generation, CT_TALK);
        }

        auto it2 = localNames.find(nUnitID);
        if (it2 != localNames.end())
        {
            CI3DSurface *pSurf = surfaces[1];
#ifndef NOGETDC
            pSurf->GetDC();
            pSurf->Clear();
            pSurf->DrawText(localNames[nUnitID], CRect(0, 0, 0, 0), 1, BASE_COLOR_TEXT, DT_CENTER);
            pSurf->ReleaseDC();
#endif // NOGETDC
            pI3D->ScreenToVirtual(point);
            point -= CPoint(125, 20);
            pSurf->Move(point);
            pSurf->Show(TRUE);
        }

        return;
    }

    int nExit;
    plot = theWorld.GetTerrainXYZFromScreenXYWithBridge(point.x, point.y);
    if (theWorld.IsExitPoint(plot, &nExit))
    {
        CString szVarName = IWorldClient.GetExitZoneName(nCheckExit);
        if (szVarName.CompareNoCase("none") != 0)
        {
            szVarName = CString("z.") + szVarName;
            if (IWorld.GSGetVar(szVarName) != 1.f)
                theCursor.SetMode(CT_MOVE);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::OnIdle()
{
    fIdleTime += GetFrameTime();
    while (fIdleTime >= 1.f)
        fIdleTime -= 1.f;

    CI3DScreen::OnIdle();

    // pI3D->CheckMissionTutorials();

    for (int i = 0; i < figures.GetSize(); i++)
        figures[i]->Show(FALSE);
    for (int i = 0; i < surfaces.GetSize(); i++)
        surfaces[i]->Show(FALSE);
    for (int i = 0; i < 2; i++)
        pObjects[i]->Show(FALSE);

    // if ( pMainHero )
    //	theCamera.SetCarrier( pMainHero );

    for (auto it = walks.begin(); it != walks.end(); ++it)
    {
        it->fTime += GetFrameTime();
        if (it->fTime > 30.f) // move immediately
        {
            CUnit *pU = theWorldServer.GetUnit(it->pUnit->GetID());
            if (pU)
            {
                CUnitServer *pUS = (CUnitServer *)pU->GetUnitSpecific();
                theWorldServer.aiMap.MoveObject(pU, it->ptDest);
                CPlot3D dir = it->rotDest;
                dir.Normalize();
                pU->SetOrientationXY(dir);
                it->pUnit->SetOrientationXY(dir);
                pUS->InitializeUnit();
            }
            it->nMode = 0;
            continue;
        }

        switch (it->nMode)
        {
        case 1: {
            CPlot3D pos1 = it->pUnit->GetCP();
            CPlot3D pos2 = it->ptDest;
            if ((pos2.x - pos1.x) * (pos2.x - pos1.x) + (pos2.y - pos1.y) * (pos2.y - pos1.y) <
                1e-2)
            {
                it->nMode = 2;
                CUnit *pU = theWorldServer.GetUnit(it->pUnit->GetID());
                if (pU)
                {
                    CPlot3D dir = it->rotDest;
                    dir.Normalize();
                    CUnitServer *pUS = (CUnitServer *)pU->GetUnitSpecific();
                    pUS->logic.StartRotate(dir, 1);
                }
            }
        }
        break;

        case 2: {
            CPlot3D rot1 = -it->pUnit->GetYAxis();
            CPlot3D rot2 = it->rotDest;
            rot2.Normalize();
            if ((rot2.x - rot1.x) * (rot2.x - rot1.x) + (rot2.y - rot1.y) * (rot2.y - rot1.y) <
                1e-2)
                it->nMode = 0;
        }
        break;
        }
    }

    walks.remove_if(CRemoveWalkingUnitFunctional());

    switch (eMode)
    {
    case BSM_TOPIC: {
        surfaces[0]->Show(TRUE);
        figures[0]->Show(TRUE);
        figures[1]->Show(TRUE);
        pScrollTopics->Show(TRUE);
        int n;
        if (pScrollTopics->GetPos(n))
        {
            nPos = n;
            RedrawTopics();
        }
    }
    case BSM_MOVEMENT: {
#ifndef __DEMO__
        if (1 || fIdleTime > 0.5f)
        {
            // set "!" above the personages who has topics to discuss
            CGSVariableSet vars;
            CStringArray szVarParts;
            IWorld.GSGetVars("b.", vars);
            CGSVariableSet::const_iterator p;
            std::vector<int>::iterator r;
            std::vector<int> resultIDs;
            for (p = vars.begin(); p != vars.end(); ++p)
                if (p->second == SCN_BRIEFING_AVAILABLE)
                {
                    NStrProc::SplitString(p->first.c_str(), szVarParts, '.');
                    if (szVarParts.GetSize() != 3)
                        continue;
                    if (szVarParts[2].Left(6).CompareNoCase("constr") == 0)
                        continue;
                    int nUnitID = GetPersonageIDByName(szVarParts[1]);
                    if (std::find(nBriefingPersIDs.begin(), nBriefingPersIDs.end(), nUnitID) !=
                        nBriefingPersIDs.end())
                        resultIDs.push_back(nUnitID);
                }
            std::sort(resultIDs.begin(), resultIDs.end());
            r = std::unique(resultIDs.begin(), resultIDs.end());
            resultIDs.erase(r, resultIDs.end());
            for (r = nBriefingPersIDs.begin(); r != nBriefingPersIDs.end(); ++r)
            {
                CUnit *pU = theWorld.GetUnit((*r));
                if (!pU)
                    continue;
                if (std::find(resultIDs.begin(), resultIDs.end(), (*r)) != resultIDs.end())
                {
                    CPlayer *pP = theWorldServer.GetPlayer(0);
                    if (pP)
                    {
                        CPlayerServer *pPS = ((CPlayerServer *)pP->GetPlayerSpecific());
                        if (pPS)
                            pPS->Select(PSEL_PRESELECT_TRUE, (*r));
                    }
                    //						pU->Preselect( TRUE );
                }
                else
                {
                    CPlayer *pP = theWorldServer.GetPlayer(0);
                    if (pP)
                    {
                        CPlayerServer *pPS = ((CPlayerServer *)pP->GetPlayerSpecific());
                        if (pPS)
                            pPS->Select(PSEL_PRESELECT_FALSE, (*r));
                    }
                    //						pU->Preselect( FALSE );
                }
            }
        }
#endif // __DEMO__

        CParty &party = IWorldClient.GetParty();
        if (bDoCheckExit && party.people.GetSize() > 0)
        {
            CString szVarName = IWorldClient.GetExitZoneName(nCheckExit);
            if (szVarName.CompareNoCase("none") == 0)
                return;
            szVarName = CString("z.") + szVarName;
            if (IWorld.GSGetVar(szVarName) == 1.f)
                return;

            CPersonages personages, peopleOnExit;
            personages.Add(&party.people[0]);
            if (IWorldClient.ArePersonagesAtExit(personages, nCheckExit, peopleOnExit))
            {
                NGame::bNeedRender = false;
                IWorldClient.ExitZone(nCheckExit);
                return;
            }
        }
    }
    break;

    case BSM_APPROACH: {
        eMode = BSM_BRIEFING;
        for (auto it = walks.begin(); it != walks.end(); ++it)
            if (it->nMode > 0 && it->bIsForBriefing)
            {
                eMode = BSM_APPROACH;
                break;
            }
        if (eMode == BSM_BRIEFING)
            BeginBriefing();
    }
    break;

    case BSM_BRIEFING: {
        for (int i = 2; i < figures.GetSize(); i++)
            figures[i]->Show(TRUE);
        pScroll->Show(TRUE);
        if (hPhraseSound != SND_EMPTY)
        {
            if (!theWorld.GetSoundManager()->IsFXValid(hPhraseSound))
                NextCommand();
        }
        else
        {
            fPhraseTime -= GetFrameTime();
            if (fPhraseTime < 0)
                NextCommand();
        }
    }
    break;

    case BSM_SUMMARY: {
        for (int i = 2; i < figures.GetSize(); i++)
            figures[i]->Show(TRUE);
        pScroll->Show(TRUE);
        if (hPhraseSound != SND_EMPTY)
        {
            if (pWhosTalking && !theWorld.GetSoundManager()->IsFXValid(hPhraseSound))
                ((CUnitClientSpecific *)pWhosTalking->GetUnitSpecific())->MouthSpeak(false);
        }
        else
        {
            fPhraseTime -= GetFrameTime();
            if (pWhosTalking && fPhraseTime < 0)
                ((CUnitClientSpecific *)pWhosTalking->GetUnitSpecific())->MouthSpeak(false);
        }
    }
    break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
#ifdef __DOUBLE_RESOURCES__
static const char *pszDirNames[] = { "t", "s" };
#endif // __DOUBLE_RESOURCES__
void CI3DScreenBase::NextCommand(bool bSkip)
{
    CStringArray szTmpArray;

    CString szWho, szPhrase;

    CTextManager *pTxtMng = theWorld.GetTextManager();

    BOOL bLastPhrase = TRUE;
    BOOL bIsPhrase = FALSE;
    int nAnimation = -1, nCamera = -1;
    bool bLips = true;
    float fAngle, fDistance, fHeight;
    bool bSpecialCamera = false;
    while (pTxtMng->FindNextCommand())
    {
        pTxtMng->GetStringArray(szTmpArray);
        if (szTmpArray[0].CompareNoCase("#nolips") == 0)
            bLips = false;
        else if (szTmpArray.GetSize() < 2)
            continue;
        else if (szTmpArray[0].CompareNoCase("#show") == 0)
        {
            if (szTmpArray.GetSize() > 3 && szTmpArray[3].CompareNoCase("weapon") == 0)
                CommandShow(szTmpArray[1], atoi(szTmpArray[2]) - 1, false);
            if (szTmpArray.GetSize() > 2)
                CommandShow(szTmpArray[1], atoi(szTmpArray[2]) - 1);
            else
                CommandShow(szTmpArray[1]);
        }
        else if (szTmpArray[0].CompareNoCase("#hide") == 0)
        {
            if (szTmpArray.GetSize() > 2)
                CommandHide(szTmpArray[1], bSkip, true);
            else
                CommandHide(szTmpArray[1], bSkip);
        }
        else if (szTmpArray[0].CompareNoCase("#animation") == 0)
        {
            nAnimation = atoi(szTmpArray[1]);
        }
        else if (szTmpArray[0].CompareNoCase("#camera") == 0)
        {
            if (szTmpArray.GetSize() >= 5)
            {
                fAngle = ToRadian((float)atof(szTmpArray[2]));
                fDistance = (float)atof(szTmpArray[3]);
                fHeight = (float)atof(szTmpArray[4]);
                bSpecialCamera = true;
            }
            nCamera = atoi(szTmpArray[1]);
        }
        else if (szTmpArray[0].CompareNoCase("#phrase") == 0)
        {
            LPUNIT pUnit = IWorldClient.GetUnit(GetPersonageIDByName(szTmpArray[1]));
            if (!pUnit)
            {
                szWho = szTmpArray[1];
            }
            else
            {
                szWho = pUnit->GetName();
                if (szWho.IsEmpty()) // quest personage
                {
                    auto it = localNames.find(pUnit->GetID());
                    if (it != localNames.end())
                        szWho = it->second;
                    else
                        szWho = szTmpArray[1];
                }
            }
            pTxtMng->GetUntilNextCommand(szPhrase);
            if (bLips)
                szPhrase = CString("\aC\x0ff\x0b3\x031 ") + szWho + ":\n\aC\x0e4\x0d7\x0a7 " +
                           szPhrase + "\n";
            else
                szPhrase = CString("\aC\x0ff\x0b3\x031 ") + szWho + ":\n\aC\x0a8\x0a8\x0a8 " +
                           szPhrase + "\n";
            szAllTalk += szPhrase;
            bIsPhrase = TRUE;
            LPCTSTR pszPos = pTxtMng->GetPos();
            if (pTxtMng->FindNextCommand("#phrase"))
                bLastPhrase = FALSE;
            pTxtMng->SetPos(pszPos);

            if (hPhraseSound != SND_EMPTY)
                theWorld.GetSoundManager()->CancelFX(hPhraseSound);
            hPhraseSound = SND_EMPTY;

            if (!bSkip)
            {
                // set phrase sound
                if (szTmpArray.GetSize() > 2)
                {
                    int nPhrase = atoi(szTmpArray[2]);
                    if (nPhrase > 0)
                    {
                        CString szSoundName;
#ifdef __DOUBLE_RESOURCES__
                        szSoundName.Format("%s\\briefing\\%s\\%d.mp3", pszDirNames[Random(2)],
                                           szBriefing, nPhrase);
#else
                        szSoundName.Format("briefing\\%s\\%d.mp3", szBriefing, nPhrase);
#endif // __DOUBLE_RESOURCES__
                        hPhraseSound = theWorld.GetSoundManager()->BeginMP3Dialog(szSoundName, 256,
                                                                                  100);
                    }
                }
                fPhraseTime = szPhrase.GetLength() / 20.f;
                if (bIsFirstPhrase)
                    fPhraseTime += 1.f;
            }
        }

        if (bIsPhrase)
            break;
    }

    if (!bSkip)
        pScroll->AddText(szPhrase);

    if (bLastPhrase)
    {
        eMode = BSM_SUMMARY;

        // output information about given items and money
        const SQDBBriefing *pBr = theQuestDatabase.briefings[szBriefing];
        if (pBr)
        {
            CStringArray szVarParts;
            CString szName, szOut, szTmp;
            //
            if (pBr->fExp > 0)
            {
                pTxtMng->GetSystemString("brief_get_exp", szTmp);
                szOut.Format("\aC\x0ff\x0ff\x0ff %s %d\n", szTmp, (int)pBr->fExp);
                szAllTalk += szOut;
                if (!bSkip)
                    pScroll->AddText(szOut);
            }
            //
            if (pBr->fMoney > 0)
            {
                pTxtMng->GetSystemString("brief_get_money", szTmp);
                szOut.Format("\aC\x0ff\x0ff\x0ff %s %d\n", szTmp, (int)pBr->fMoney);
                szAllTalk += szOut;
                if (!bSkip)
                    pScroll->AddText(szOut);
            }
            //
            pTxtMng->GetSystemString("brief_get_item", szTmp);
            std::vector<CString>::const_iterator it;
            for (it = pBr->szItems.begin(); it != pBr->szItems.end(); it++)
            {
                CItem *pItem = theWorld.GetItemManager()->CreateItem(*it);
                if (pItem)
                {
                    pItem->AddRef();
                    pTxtMng->GetExtendedItemName(pItem, szName);
                    szOut.Format("\aC\x0ff\x0ff\x0ff %s %s\n", szTmp, szName);
                    szAllTalk += szOut;
                    if (!bSkip)
                        pScroll->AddText(szOut);
                    pItem->Release();
                }
            }
            //
            pTxtMng->GetSystemString("brief_lost_item", szTmp);
            if (IWorldConnector.IsLMP() && szBriefing.GetLength() > 0 &&
                szBriefing.Left(1).CompareNoCase("z") == 0)
            {
                CPlayer *pPlayer = theWorldServer.GetPlayer(0);
                if (pPlayer)
                    for (POSITION pos = pPlayer->items.GetHeadPosition(); pos;)
                    {
                        CItem *pItem = pPlayer->items.GetNext(pos);
                        if (pItem->WhoAmI() == HPC_ITEM_QUEST)
                        {
                            pTxtMng->GetExtendedItemName(pItem, szName);
                            szOut.Format("\aC\x0ff\x0ff\x0ff %s %s\n", szTmp, szName);
                            szAllTalk += szOut;
                            if (!bSkip)
                                pScroll->AddText(szOut);
                        }
                    }
            }
            for (it = pBr->szRemoveItems.begin(); it != pBr->szRemoveItems.end(); it++)
            {
                CItem *pItem = theWorld.GetItemManager()->CreateItem(*it);
                if (pItem)
                {
                    pItem->AddRef();
                    pTxtMng->GetExtendedItemName(pItem, szName);
                    szOut.Format("\aC\x0ff\x0ff\x0ff %s %s\n", szTmp, szName);
                    szAllTalk += szOut;
                    if (!bSkip)
                        pScroll->AddText(szOut);
                    pItem->Release();
                }
            }
            pTxtMng->GetSystemString("brief_get_quest", szTmp);
            for (it = pBr->szGiveQuests.begin(); it != pBr->szGiveQuests.end(); it++)
            {
                NStrProc::SplitString((*it), szVarParts, '.');
                if (szVarParts.GetSize() == 3 &&
                    pTxtMng->GetText(CString("quest ") + szVarParts[2]))
                {
                    pTxtMng->GetLine(szName);
                    szOut.Format("\aC\x0ff\x0ff\x0ff %s %s\n", szTmp, szName);
                    szAllTalk += szOut;
                    if (!bSkip)
                        pScroll->AddText(szOut);
                }
            }
            pTxtMng->GetSystemString("brief_complete_quest", szTmp);
            for (it = pBr->szCompleteQuests.begin(); it != pBr->szCompleteQuests.end(); it++)
            {
                NStrProc::SplitString((*it), szVarParts, '.');
                if (szVarParts.GetSize() == 3 &&
                    pTxtMng->GetText(CString("quest ") + szVarParts[2]))
                {
                    pTxtMng->GetLine(szName);
                    szOut.Format("\aC\x0ff\x0ff\x0ff %s %s\n", szTmp, szName);
                    szAllTalk += szOut;
                    if (!bSkip)
                        pScroll->AddText(szOut);
                }
            }
            if (pBr->nSpecial > 0)
            {
                szTmp.Format("brief_get_specials_%d", pBr->nSpecial);
                pTxtMng->GetSystemString(szTmp, szTmp);
                szOut.Format("\aC\x0ff\x0ff\x0ff %s\n", szTmp);
                szAllTalk += szOut;
                if (!bSkip)
                    pScroll->AddText(szOut);
            }
        }
    }
    if (!bSkip || bLastPhrase)
    {
        if (pMainHero)
            IWorldServer.SetUnitZ(pMainHero->GetID(), 0);
        if (pPersonToTalk)
            IWorldServer.SetUnitZ(pPersonToTalk->GetID(), 0);
        for (int i = 0; i < parts.size(); i++)
            if (parts[i].szName.CompareNoCase(szTmpArray[1]) == 0)
            {
                if (bSpecialCamera)
                {
                    switch (nCamera)
                    {
                    case 2:
                        if (pMainHero)
                            PlaceCamera(pMainHero, fAngle, fDistance, fHeight);
                        break;
                    case 3:
                        if (pPersonToTalk)
                            PlaceCamera(pPersonToTalk, fAngle, fDistance, fHeight);
                        break;
                    case 4:
                        if (pThirdPerson)
                            PlaceCamera(pThirdPerson, fAngle, fDistance, fHeight);
                        break;
                    default:
                        PlaceCamera(0, fAngle, fDistance, fHeight);
                        break;
                    }
                }
                else if (nCamera > 0)
                    PlaceDefinedCamera(nCamera);
                else
                {
                    if (bIsFirstPhrase)
                        PlaceDefinedCamera(1);
                    else
                    {
                        if (parts[i].pUnit == pMainHero)
                            PlaceDefinedCamera(2);
                        else if (parts[i].pUnit == pThirdPerson)
                            PlaceDefinedCamera(4);
                        else
                            PlaceDefinedCamera(3);
                    }
                }
            }
    }
    if (!bSkip && bIsPhrase)
    {
        for (int i = 0; i < parts.size(); i++)
            if (parts[i].szName.CompareNoCase(szTmpArray[1]) == 0)
            {
                if (nAnimation > 0)
                    PlayAnimation(parts[i].pUnit, nAnimation);
                else
                    PlayRndAnimation(parts[i].pUnit, true);
                ((CUnitClientSpecific *)parts[i].pUnit->GetUnitSpecific())->MouthSpeak(bLips);
                pWhosTalking = parts[i].pUnit;
            }
            else
            {
                PlayRndAnimation(parts[i].pUnit, false);
                ((CUnitClientSpecific *)parts[i].pUnit->GetUnitSpecific())->MouthSpeak(false);
            }
    }
    bIsFirstPhrase = false;

    if (bSkip && bLastPhrase)
    {
        pScroll->SetText(szAllTalk);
        pScroll->SetPosToEnd();

        for (int i = 0; i < parts.size(); i++)
        {
            ((CUnitClientSpecific *)parts[i].pUnit->GetUnitSpecific())->MouthSpeak(false);
            PlayRndAnimation(parts[i].pUnit);
        }
        pWhosTalking = 0;

        for (auto it = items.begin(); it != items.end(); ++it)
        {
            if (it->pFigure)
            {
                RemoveTexture(it->pFigure->GetTexture());
                RemoveFigure(it->pFigure);
            }
            if (it->pFigFrame)
                RemoveFigure(it->pFigFrame);
            if (it->pFigBack)
                RemoveFigure(it->pFigBack);
        }
        items.clear();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::CommandShow(LPCTSTR pszWhat, int nSlot, bool bHideWeapon)
{
    CUnit *pUnit = IWorldClient.GetUnit(GetPersonageIDByName(pszWhat));
    if (pUnit)
    {
        std::vector<SBriefingUnit>::iterator it;
        for (it = parts.begin(); it != parts.end(); ++it)
            if (pUnit == it->pUnit)
            {
                parts.erase(it);
                break;
            }

        SBriefingUnit part, *pPart;
        parts.push_back(part);
        pPart = &parts.back();
        pPart->pUnit = pUnit;
        pPart->szName = pszWhat;
        if (briefingWeaponsHidden) HideBriefingUnitWeapon(pUnit);
        CPlayerServer *pPS = ((CPlayerServer *)theWorldServer.GetPlayer(0)->GetPlayerSpecific());
        if (pPS)
            pPS->Select(PSEL_PRESELECT_TRUE, pUnit->GetID());
        //		pUnit->Preselect( TRUE );
        CUnit *pU = theWorldServer.GetUnit(pUnit->GetID());
        if (pU)
        {
            if (pMainHero && pMainHero->GetID() == pUnit->GetID())
                pU->ShowHelmet(false);
            CUnitServer *pUS = (CUnitServer *)pU->GetUnitSpecific();
            pUS->SetAnimationSwitchCallback(SwitchCallback2);
            //			if ( bHideWeapon )
            //				pUS->ChangeWeapon(-1);
        }
    }
    else
    {
        if (nSlot < 0)
            return;

        for (auto it = items.begin(); it != items.end(); ++it)
            if (it->szName.CompareNoCase(pszWhat) == 0)
            {
                CommandHide(pszWhat, false);
                break;
            }

        // may be it's quest item
        CItemManager *pItmMng = theWorld.GetItemManager();
        CString szName(pszWhat);
        NStrProc::ReplaceChars(szName, '_', ' ');
        szName.MakeLower();

        const SIDBQuestItemShape *pShape = pItmMng->GetQuestItemShape(szName);
        if (!pShape)
            return;

        CString szFigName, szTexName;
        CI3DFigure *pFig;
        SBriefingQuestItem item;
        float x = BRIEFING_ITEM_XS[nSlot % 7];
        float y = BRIEFING_ITEM_YS[nSlot % 7];

        if (nSlot < 7)
        {
            pFig = AddFigure();
            pFig->MakeBar(20, 100, x - 60, y - 60, x + 60, y + 60);
            pFig->SetTexture(NULL);
            pFig->UseDefaultColors(TRUE);
            pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));
            item.pFigBack = pFig;

            pFig = AddFigure();
            pFig->MakeBarFrame(20, 100, x - 65, y - 65, x + 65, y + 65, 5, 2, 254, 180, 249);
            item.pFigFrame = pFig;
        }
        else
        {
            item.pFigBack = 0;
            item.pFigFrame = 0;
        }

        szFigName.Format("initqu%ditem", pShape->dwTextureTypeIndex);
        pFig = AddFigure(szFigName);
        if (!pFig)
        {
            if (item.pFigFrame)
                RemoveFigure(item.pFigFrame);
            if (item.pFigBack)
                RemoveFigure(item.pFigBack);
            return;
        }
        if (nSlot > 7)
        {
            pFig->SetDefaultColor(RGBA_MAKE(255, 255, 255, 130));
            pFig->UseDefaultColors(TRUE);
        }

        szTexName.Format("quitem%04d", pShape->dwTextureTypeIndex);
        CTexture *pTex = AddTexture(256, 256);
        PasteTexture(pTex, szTexName, 64, 64, 64, 64, 0, 192);

        pFig->SetTexture(pTex);
        pFig->SetRelativeToScreen(x, y, 12);
        pFig->SetOrientation(CQuaternion(CPlot3D(1, 0, 0), FP_PI));

        item.szName = pszWhat;
        item.pFigure = pFig;

        items.push_back(item);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::CommandHide(LPCTSTR pszWhat, bool bSkip, bool bRun)
{
    CUnit *pUnit = IWorldClient.GetUnit(GetPersonageIDByName(pszWhat));
    if (pUnit)
    {
        std::vector<SBriefingUnit>::iterator it;
        for (it = parts.begin(); it != parts.end(); ++it)
            if (pUnit == it->pUnit)
            {
                CPlayerServer *pPS = ((CPlayerServer *)theWorldServer.GetPlayer(0)
                                          ->GetPlayerSpecific());
                if (pPS)
                    pPS->Select(PSEL_PRESELECT_FALSE, pUnit->GetID());
                //				pUnit->Preselect( FALSE );
                ((CUnitClientSpecific *)pUnit->GetUnitSpecific())->MouthSpeak(false);
                CUnit *pU = theWorldServer.GetUnit(pUnit->GetID());
                if (pU)
                {
                    pU->ShowHelmet(true);
                    IWorldServer.SetUnitZ(pUnit->GetID(), 0);
                    CUnitServer *pUS = (CUnitServer *)pU->GetUnitSpecific();
                    pUS->SetAnimationSwitchCallback(0);
                    pUS->InterruptCommand();
                    //					pUS->ChangeWeapon(0);
                    for (int k = 0; k < pUnits.GetSize(); k++)
                        if (pUnit == pUnits[k] && k < IWorldClient.GetParty().people.GetSize())
                        {
                            //							pUS->ChangeWeapon(
                            // IWorldClient.GetParty().people[k].dwCurrentWeapon );
                            break;
                        }
                }
                parts.erase(it);
                break;
            }
        if (pUnit == pThirdPerson)
        {
            AddWalkingUnit(pUnit, ptMidPersOld, dirMidPersOld, false, bSkip, bRun);
            pThirdPerson = 0;
        }
    }
    else
    {
        for (auto it = items.begin(); it != items.end(); ++it)
            if (it->szName.CompareNoCase(pszWhat) == 0)
            {
                if (it->pFigure)
                {
                    RemoveTexture(it->pFigure->GetTexture());
                    RemoveFigure(it->pFigure);
                }
                if (it->pFigFrame)
                    RemoveFigure(it->pFigFrame);
                if (it->pFigBack)
                    RemoveFigure(it->pFigBack);
                items.erase(it);
                break;
            }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::PlayBriefing(bool bForceBr, bool bNeedMemorizePos)
{
    bForce = bForceBr;
    if (!pMainHero)
    {
        eMode = BSM_MOVEMENT;
        if (bWasForcedBriefing)
            bWasForcedBriefing = false;
        return;
    }
    if (szBriefing.CompareNoCase("goodbye") == 0)
    {
        eMode = BSM_MOVEMENT;
        return;
    }
    if (szBriefing.Left(6).CompareNoCase("constr") == 0)
    {
        int nShop = atoi(szBriefing.Mid(7));
        if (theHat.shops.GetShop(nShop))
        {
            IWorldServer.SetConstructorNumber(nShop);
            IWorldClient.GoCamp();
        }
        eMode = BSM_MOVEMENT;
    }
    else
    {
        // get all participants from briefing text
        pPersonToTalk = 0;
        pThirdPerson = 0;
        CTextManager *pTxtMng = theWorld.GetTextManager();
        if (!pTxtMng->GetText(CString("briefing ") + szBriefing))
        {
            eMode = BSM_MOVEMENT;
            if (bWasForcedBriefing)
                bWasForcedBriefing = false;
            return;
        }

        CStringArray szTmpArray;
        while (pTxtMng->FindNextCommand("#show"))
        {
            pTxtMng->GetStringArray(szTmpArray);
            if (szTmpArray.GetSize() < 2)
                continue;
            int nUnitID = GetPersonageIDByName(szTmpArray[1]);
            if (nUnitID == pMainHero->GetID())
                continue;
            CUnit *pUnit = IWorldClient.GetUnit(nUnitID);
            if (pUnit)
            {
                if (!pPersonToTalk)
                    pPersonToTalk = pUnit;
                else if (pPersonToTalk != pUnit)
                    pThirdPerson = pUnit;
            }
        }

        if (!pPersonToTalk)
        {
            eMode = BSM_MOVEMENT;
            if (bWasForcedBriefing)
                bWasForcedBriefing = false;
            return;
        }

        // check pos where briefing take place
        float fDist = pPersonToTalk->GetPrototype()->fDialogCameraDistance + 2.5f;

        if (bCageBriefing)
        {
            ptLeftPers = pMainHero->GetCP() - pMainHero->GetYAxis() * fDist;
            ptRightPers = pMainHero->GetCP();
            ptMidPers = (ptRightPers + ptLeftPers) / 2.f;
            ptMidPers += CPlot3D(-ptMidPers.y + ptLeftPers.y, ptMidPers.x - ptLeftPers.x, 0);
            if (bNeedMemorizePos)
            {
                ptLeftPersOld = pPersonToTalk->GetCP();
                dirLeftPersOld = -pPersonToTalk->GetYAxis();
            }
        }
        else
        {
            ptRightPers = pPersonToTalk->GetCP() - pPersonToTalk->GetYAxis() * fDist;
            ptLeftPers = pPersonToTalk->GetCP();
            ptMidPers = (ptRightPers + ptLeftPers) / 2.f;
            ptMidPers += CPlot3D(-ptMidPers.y + ptLeftPers.y, ptMidPers.x - ptLeftPers.x, 0);
            AddWalkingUnit(pMainHero, ptRightPers, ptLeftPers - ptRightPers, true, bForce, false);
        }
        if (pThirdPerson && bNeedMemorizePos)
        {
            ptMidPersOld = pThirdPerson->GetCP();
            dirMidPersOld = -pThirdPerson->GetYAxis();
        }
        dirLeftPers = ptRightPers - ptLeftPers;
        dirLeftPers.z = 0;
        dirRightPers = ptLeftPers - ptRightPers;
        dirRightPers.z = 0;
        dirMidPers = (ptLeftPers + ptRightPers) / 2.f - ptMidPers;
        dirMidPers.z = 0;
        dirLeftPers.Normalize();
        dirRightPers.Normalize();
        dirMidPers.Normalize();

        std::list<SWalkingUnit>::iterator it;
        for (it = walks.begin(); it != walks.end(); ++it)
            if (pThirdPerson && it->pUnit == pThirdPerson)
            {
                if (!it->bIsForBriefing)
                {
                    ptMidPersOld = it->ptDest;
                    dirMidPersOld = it->rotDest;
                }
                walks.erase(it);
                break;
            }
        for (it = walks.begin(); it != walks.end(); ++it)
            if (bCageBriefing && it->pUnit == pPersonToTalk)
            {
                if (!it->bIsForBriefing)
                {
                    ptLeftPersOld = it->ptDest;
                    dirLeftPersOld = it->rotDest;
                }
                walks.erase(it);
                break;
            }

        if (pThirdPerson)
            AddWalkingUnit(pThirdPerson, ptMidPers, (ptLeftPers + ptRightPers) / 2.f - ptMidPers,
                           true, bForce);
        if (bCageBriefing)
            AddWalkingUnit(pPersonToTalk, ptLeftPers, ptRightPers - ptLeftPers, true, bForce);

        if (bForce)
            BeginBriefing();
        else
            eMode = BSM_APPROACH;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::OnExitFromModal(DWORD dwRetValue)
{
    switch (dwRetValue)
    {
    case 0x00010001:
        IWorldClient.ExitZone(nCheckExit);
        break;
    case 0x00020002:
    case 0x00100001:
    case 0x00090001:
        pI3D->SetScreensMustDie();
        IWorldConnector.StopLMP();
        IWorldConnector.ReqGoMainMenu();
        break;
    case 0x00020001: {
        CI3DScreenSaveLoad *pScreenLoad = 0;
        pScreenLoad = dbgnew CI3DScreenSaveLoad(pI3D);
        pScreenLoad->SetParams(false, false);
        pI3D->DoModal(pScreenLoad);
    }
    break;
    }
    if (IWorldConnector.IsLMP())
        LMPPersonsUpdate(true);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::RedrawTopics()
{
    CString szPrefix;
    IWorld.GetTextManager()->GetSystemString("topic_prefix", szPrefix);
#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();
    pSurf->Clear();
    int i, n;
    for (i = 0, n = nPos; i < BASE_NUM_BRIEFS_VISIBLE && n < szBriefings.GetSize(); i++, n++)
    {
        COLORREF cr = (n == nCurrentTopic) ? RGB(255, 255, 255) : BASE_COLOR_TEXT;
#ifdef NODEBUGINFO
        pSurf->DrawText(szPrefix + " " + GetBriefingLocalName(szBriefings[n]),
                        CRect(15, 12 + i * 24, 380, 34 + i * 24), 1, cr, DT_LEFT);
#else
        pSurf->DrawText(szBriefings[n] + " - " + GetBriefingLocalName(szBriefings[n]),
                        CRect(15, 12 + i * 24, 380, 34 + i * 24), 1, cr, DT_LEFT);
#endif // NODEBUGINFO
    }
    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
struct SCompareTopics
{
    bool operator()(const std::string &a, const std::string &b) const
    {
        return (stricmp(a.c_str(), b.c_str()) < 0);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::FillBriefingsList(int nUnitID)
{
    CGSVariableSet vars;
    CStringArray szVarParts;
    CString szBr, szConstr;

    std::vector<std::string> szTopics;
    IWorld.GSGetVars("b.", vars);
    CGSVariableSet::const_iterator p;
    for (p = vars.begin(); p != vars.end(); ++p)
        if (p->second == SCN_BRIEFING_AVAILABLE)
        {
            NStrProc::SplitString(p->first.c_str(), szVarParts, '.');
            if (szVarParts.GetSize() == 3 && GetPersonageIDByName(szVarParts[1]) == nUnitID)
            {
                szBr = szVarParts[2];
                if (szBr.Left(6).CompareNoCase("constr") == 0)
                    szConstr = szBr;
                else
                    szTopics.push_back((const char *)szBr);
            }
        }
    if (IWorldConnector.IsLMP())
        std::sort(szTopics.begin(), szTopics.end(), SCompareTopics());
    szBriefings.RemoveAll();
    std::vector<std::string>::iterator i;
    for (i = szTopics.begin(); i != szTopics.end(); ++i)
        szBriefings.Add(i->c_str());
    if (!szConstr.IsEmpty())
        szBriefings.Add(szConstr);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::GetPersLocalNames()
{
    CString szTmp;
    CStringArray szPersFiles;
    CTextManager *pTxtMng = IWorld.GetTextManager();
    pTxtMng->GetTextNames(szPersFiles, "pers *");

    for (int i = 0; i < szPersFiles.GetSize(); i++)
        if (pTxtMng->GetText(szPersFiles[i]))
        {
            pTxtMng->GetLine(szTmp);
            int id = GetPersonageIDByName(szPersFiles[i].Mid(5));
            localNames[id] = szTmp;
        }
}
/////////////////////////////////////////////////////////////////////////////////////
CString CI3DScreenBase::GetBriefingLocalName(LPCTSTR pszName)
{
    CString szRes;
    CString szBriefName(pszName);
    szBriefName.MakeLower();
    if (szBriefName.Left(6) == "constr")
        szBriefName = "constr";
    CTextManager *pTxtMng = IWorld.GetTextManager();
    if (pTxtMng->GetText(CString("briefing ") + szBriefName))
        pTxtMng->GetLine(szRes);
    return szRes;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::PlayAnimation(CUnit *pUnitClient, int nAnim)
{
    CUnit *pU = theWorldServer.GetUnit(pUnitClient->GetID());
    if (pU)
    {
        CUnitServer *pUS = (CUnitServer *)pU->GetUnitSpecific();
        pUS->InterruptCommand();
        pUS->SetAnimationSwitchCallback(SwitchCallback2);
        const SADEntry *pEntry = pUnitClient->GetSpecialAnimationEntry(nAnim);
        if (pEntry)
            pUS->CmdPlayAnimation(pEntry->pszAnimationName);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::PlayRndAnimation(CUnit *pUnit, bool bTalk, bool bShortPhrase)
{
    CString szMaskName = pUnit->GetMaskName();
    bool bIsHuman = false;
    if (szMaskName == "unhuma")
        bIsHuman = true;
    int nAnim;
    if (bTalk)
    {
        if (bShortPhrase)
            nAnim = 3;
        else if (bIsHuman)
            nAnim = HUMAN_TALK_ANIMS[random.Get(0, 3)];
        else
            nAnim = 1;
    }
    else
    {
        if (bIsHuman)
            nAnim = HUMAN_IDLE_ANIMS[random.Get(0, 3)];
        else
            nAnim = 2;
    }
    PlayAnimation(pUnit, nAnim);
    if (briefingWeaponsHidden)
    {
        HideBriefingUnitWeapon(pUnit);
        HideCurrentBriefingWeapons();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::PlaceCamera(CUnit *pUnit, float fAngle, float fDistance, float fHeight)
{
    CPlot3D ptFrom(0, 0, 0), ptTo(0, 0, 0);

    float fCos = (float)cos(fAngle), fSin = (float)sin(fAngle);
    CPlot3D ptTmp(0, 0, 0);

    if (!pUnit)
    {
        ptTo = (ptRightPers + ptLeftPers) / 2;
        ptTmp.x = ptRightPers.y - ptLeftPers.y;
        ptTmp.y = -ptRightPers.x + ptLeftPers.x;
        ptTmp.z = 0;
    }
    else
    {
        CPlot3D pt(pUnit->GetCP()), dir(-pUnit->GetYAxis());
        if (pUnit == pMainHero)
        {
            pt = ptRightPers;
            dir = dirRightPers;
        }
        else if (pUnit == pPersonToTalk)
        {
            pt = ptLeftPers;
            dir = dirLeftPers;
        }
        else if (pUnit == pThirdPerson)
        {
            pt = ptMidPers;
            dir = dirMidPers;
        }
        float fDist = pUnit->GetPrototype()->fDialogCameraDistance;
        ptTo = pt + dir * fDist;
        ptTo.z = pt.z + pUnit->GetPrototype()->fDialogCameraHeight;
        ptTmp = dir;
    }
    ptTmp.z = 0;
    ptTmp.Normalize();
    ptFrom.x = fCos * ptTmp.x + fSin * ptTmp.y;
    ptFrom.y = -fSin * ptTmp.x + fCos * ptTmp.y;

    ptFrom *= fDistance;
    ptFrom.z = fHeight;
    ptFrom += ptTo;

    ((CPlayerServer *)theWorldServer.GetPlayer(0)->GetPlayerSpecific())
        ->SendMoveCamera(ptFrom, ptTo, true);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::PlaceDefinedCamera(int nCamera)
{
    CPlot3D p = ptRightPers - ptLeftPers;
    float d = (float)sqrt(p.x * p.x + p.y * p.y);
    switch (nCamera)
    {
    case 1:
        PlaceCamera(0, 0, d * 3.f, 1.5f);
        break;
    case 2:
        if (pPersonToTalk)
            IWorldServer.SetUnitZ(pPersonToTalk->GetID(), -100);
        if (pMainHero)
            PlaceCamera(pMainHero, -FP_PI / 6.f, 4.f, 1.5f);
        break;
    case 3:
        if (pMainHero)
            IWorldServer.SetUnitZ(pMainHero->GetID(), -100);
        if (pPersonToTalk)
            PlaceCamera(pPersonToTalk, FP_PI / 6.f, 4.f, 1.5f);
        break;
    case 4:
        if (pThirdPerson)
            PlaceCamera(pThirdPerson, 0, 4.f, 1.5f);
        break;
    case 5:
        PlaceCamera(0, FP_PI / 12.f, d * 2.7f);
        break;
    case 6:
        if (pPersonToTalk)
            IWorldServer.SetUnitZ(pPersonToTalk->GetID(), -100);
        if (pMainHero)
            PlaceCamera(pMainHero, -FP_PI / 4.f, 3.5f);
        break;
    case 7:
        if (pMainHero)
            IWorldServer.SetUnitZ(pMainHero->GetID(), -100);
        if (pPersonToTalk)
            PlaceCamera(pPersonToTalk, FP_PI / 4.f, 3.5f);
        break;
    case 8:
        if (pThirdPerson)
            PlaceCamera(pThirdPerson, FP_PI / 12.f, 3.5f);
        break;
    case 9:
        PlaceCamera(0, -FP_PI / 12.f, d * 2.4f);
        break;
    case 10:
        if (pMainHero)
            PlaceCamera(pMainHero, -FP_PI / 12.f, 3.f, 1.f);
        break;
    case 11:
        if (pPersonToTalk)
            PlaceCamera(pPersonToTalk, FP_PI / 12.f, 3.f, 1.f);
        break;
    case 12:
        if (pPersonToTalk)
            IWorldServer.SetUnitZ(pPersonToTalk->GetID(), -100);
        if (pMainHero)
            IWorldServer.SetUnitZ(pMainHero->GetID(), -100);
        if (pThirdPerson)
            PlaceCamera(pThirdPerson, -FP_PI / 12.f, 3.f, 1.f);
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::HideBriefingUnitWeapon(CUnit *unit)
{
    if (!unit) return;
    const int id = unit->GetID();
    bool known = false;
    for (int saved : briefingWeaponUnitIds)
        if (saved == id) { known = true; break; }
    CUnitClientSpecific *specific = static_cast<CUnitClientSpecific *>(unit->GetUnitSpecific());
    if (!specific) return;
    if (!known) briefingWeaponUnitIds.push_back(id);
    specific->SetBriefingWeaponHidden(true);
}

void CI3DScreenBase::HideCurrentBriefingWeapons()
{
    if (!briefingWeaponsHidden) return;
    HideBriefingUnitWeapon(pMainHero);
    HideBriefingUnitWeapon(pPersonToTalk);
    HideBriefingUnitWeapon(pThirdPerson);
    for (const SBriefingUnit &part : parts)
        HideBriefingUnitWeapon(part.pUnit);
}

void CI3DScreenBase::RestoreBriefingWeapons()
{
    for (int id : briefingWeaponUnitIds)
    {
        CUnit *unit = IWorldClient.GetUnit(id);
        if (!unit) continue;
        CUnitClientSpecific *specific = static_cast<CUnitClientSpecific *>(unit->GetUnitSpecific());
        if (specific) specific->SetBriefingWeaponHidden(false);
    }
    briefingWeaponUnitIds.clear();
}

void CI3DScreenBase::BeginBriefing()
{
    briefingWeaponsHidden = false;
    RestoreBriefingWeapons();
    if (hPhraseSound != SND_EMPTY)
        theWorld.GetSoundManager()->CancelFX(hPhraseSound);
    hPhraseSound = SND_EMPTY;
    theWorld.GetMusicSystem()->FadeOutMusic();
    CStringArray szTmpArray;
    CTextManager *pTxtMng = theWorld.GetTextManager();
    if (pTxtMng->GetText(CString("briefing ") + szBriefing))
    {
        theCamera.SuspendFalloutForBriefing();
        CPlayerServer *pPS = ((CPlayerServer *)theWorldServer.GetPlayer(0)->GetPlayerSpecific());
        if (pPS)
            pPS->Select(PSEL_RESETALL, 0);
        //		theWorld.ResetSelection();
        ptCameraFrom = theCamera.GetCP();
        ptCameraTo = theCamera.GetCP() + theCamera.GetZAxis();
        szAllTalk.Empty();
        pScroll->SetText("");
        eMode = BSM_BRIEFING;
        bIsFirstPhrase = true;
        briefingWeaponsHidden = ModRuntime::Current() &&
            ModRuntime::Current()->BriefingWeaponsEnabled();
        NextCommand();
        HideCurrentBriefingWeapons();
    }
    else
        eMode = BSM_MOVEMENT;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::EndBriefing()
{
    briefingWeaponsHidden = false;
    if (hPhraseSound != SND_EMPTY)
        theWorld.GetSoundManager()->CancelFX(hPhraseSound);
    hPhraseSound = SND_EMPTY;
    theWorld.GetMusicSystem()->FadeInMusic();
    for (auto it = items.begin(); it != items.end(); ++it)
    {
        if (it->pFigure)
        {
            RemoveTexture(it->pFigure->GetTexture());
            RemoveFigure(it->pFigure);
        }
        if (it->pFigFrame)
            RemoveFigure(it->pFigFrame);
        if (it->pFigBack)
            RemoveFigure(it->pFigBack);
    }
    items.clear();

    for (int i = 0; i < parts.size(); i++)
    {
        CPlayerServer *pPS = ((CPlayerServer *)theWorldServer.GetPlayer(0)->GetPlayerSpecific());
        if (pPS)
            pPS->Select(PSEL_PRESELECT_FALSE, parts[i].pUnit->GetID());
        //		parts[i].pUnit->Preselect( FALSE );
        ((CUnitClientSpecific *)parts[i].pUnit->GetUnitSpecific())->MouthSpeak(false);
        CUnit *pU = theWorldServer.GetUnit(parts[i].pUnit->GetID());
        if (pU)
        {
            pU->ShowHelmet(true);
            IWorldServer.SetUnitZ(parts[i].pUnit->GetID(), 0);
            CUnitServer *pUS = (CUnitServer *)pU->GetUnitSpecific();
            pUS->SetAnimationSwitchCallback(0);
            pUS->InterruptCommand();
            //			pUS->ChangeWeapon(0);
            for (int k = 0; k < pUnits.GetSize(); k++)
                if (parts[i].pUnit == pUnits[k] && k < IWorldClient.GetParty().people.GetSize())
                {
                    //					pUS->ChangeWeapon(
                    // IWorldClient.GetParty().people[k].dwCurrentWeapon
                    //);
                    break;
                }
        }
    }
    parts.clear();
    RestoreBriefingWeapons();
    pWhosTalking = 0;

    if (pMainHero)
        IWorldServer.SetUnitZ(pMainHero->GetID(), 0);
    if (pPersonToTalk)
        IWorldServer.SetUnitZ(pPersonToTalk->GetID(), 0);

    if (pThirdPerson)
    {
        AddWalkingUnit(pThirdPerson, ptMidPersOld, dirMidPersOld, false, false);
        pThirdPerson = 0;
    }
    if (bCageBriefing && pPersonToTalk)
        AddWalkingUnit(pPersonToTalk, ptLeftPersOld, dirLeftPersOld, false, false);

    // theCamera.LockCameraControl( false );

    // complete vars
    CGSVariableSet vars;
    CStringArray szVarParts;
    IWorld.GSGetVars("b.", vars);
    CGSVariableSet::const_iterator p;
    for (p = vars.begin(); p != vars.end(); ++p)
    {
        NStrProc::SplitString(p->first.c_str(), szVarParts, '.');
        if (szVarParts.GetSize() == 3 && szBriefing.CompareNoCase(szVarParts[2]) == 0)
        {
            theWorldServer.BriefingComplete(0, p->first.c_str());
            // IWorldClient.GetPlayer()->BriefingComplete( p->first );
            break;
        }
    }

    eMode = BSM_MOVEMENT;

    CPlayerServer *pPS = ((CPlayerServer *)theWorldServer.GetPlayer(0)->GetPlayerSpecific());
    if (pPS)
        pPS->Select(PSEL_SELECT, pMainHero->GetID());
    //	theWorld.SelectObject( pMainHero );

    CPlot3D pt(pMainHero->GetCP());
    if (bWasForcedBriefing)
    {
        ((CPlayerServer *)theWorldServer.GetPlayer(0)->GetPlayerSpecific())->SendMoveCamera(pt);
        theCamera.ResetFlag(CAM_CANT_SCROLL);
        bWasForcedBriefing = false;
    }
    else
        ((CPlayerServer *)theWorldServer.GetPlayer(0)->GetPlayerSpecific())
            ->SendMoveCamera(ptCameraFrom, ptCameraTo, false);
    theCamera.ResumeFalloutAfterBriefing();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::AddWalkingUnit(CUnit *pUClient, CPlot3D ptDest, CPlot3D rotDest,
                                    bool bIsForBriefing, bool bNow, bool bRun)
{
    CUnit *pU = theWorldServer.GetUnit(pUClient->GetID());
    CUnitServer *pUS;
    if (pU)
        pUS = (CUnitServer *)pU->GetUnitSpecific();
    if (!pU || !pUS)
        return;

    ptDest.z = pU->GetCPZ();

    if (bNow)
    {
        theWorldServer.aiMap.MoveObject(pU, ptDest);
        CPlot3D dir = rotDest;
        dir.Normalize();
        pU->SetOrientationXY(dir);
        pUClient->SetOrientationXY(dir);
        pUS->InitializeUnit();
        return;
    }

    if (pU->CheckFlag(UNIT_BLOCKED))
        return;

    SWalkingUnit walk;
    walk.bIsForBriefing = bIsForBriefing;
    walk.ptDest = ptDest;
    walk.rotDest = rotDest;
    walk.pUnit = pUClient;
    walk.nMode = 1;
    walk.fTime = 0;
    walks.push_back(walk);

    pUS->logic.StartMove(ptDest, bRun);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::InitTopicMode(CUnit *pUnit)
{
    int nUnitID = pUnit->GetID();
    journalTopicTarget = nUnitID;
    FillBriefingsList(nUnitID);
    if (szBriefings.GetSize() > 0)
    {
        if (pMainHero && !bCageBriefing)
        {
            CUnit *pU = theWorldServer.GetUnit(pMainHero->GetID());
            if (pU && !pU->CheckFlag(UNIT_BLOCKED))
            {
                CUnitServer *pUS = (CUnitServer *)pU->GetUnitSpecific();
                pUS->logic.StartIdle();
            }
        }

        for (int k = 0; k < BASE_NUM_BRIEFS_VISIBLE; k++)
            AddArea()->MakeRect(210, 440 + k * 24, 570, 464 + k * 24);

        szBriefings.Add("goodbye");

        if (szBriefings.GetSize() > BASE_NUM_BRIEFS_VISIBLE)
            pScrollTopics->SetRange(szBriefings.GetSize() - BASE_NUM_BRIEFS_VISIBLE);
        else
            pScrollTopics->SetRange(0);

        pScrollTopics->SetPos(0);
        nPos = 0;

        nCurrentTopic = -1;
        RedrawTopics();
        eMode = BSM_TOPIC;
    }
    else
        eMode = BSM_MOVEMENT;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenBase::GameOver(bool bVictory)
{
    pI3D->BeginFX("buttons\\gameover.wav");
    CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, "game_over", "game_over_msg",
                                                             0x00020000);
    pI3D->DoModal(pScr);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DScreenBase::Autosave()
{
    /*
          if ( bNeedAutosave )
          {
                  CI3DScreenSaveLoad::QuickSave( "savea", true );
                  bNeedAutosave = false;
                  pI3D->CheckTutorial();
          }
    */
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
CI3DBaseFaces::CI3DBaseFaces(CInterface3D *pI)
    : CI3DObject(pI)
{
    SetRect(464, 0, 800, 160);
    AddSurface(dbgnew CI3DSurface(pI3D, 464, 75, 800, 105));
    nFaces = 0;

    const char *pszStr[] = { "lmp_status_zone",
                             "lmp_status_base",
                             "lmp_status_camp",
                             "lmp_status_connect",
                             "lmp_exch_we_offer",
                             "lmp_exch_they_offer",
                             0 };
    CTextManager *pTxtMng = theWorld.GetTextManager();
    CString szTmp;
    const char **ppszStr = pszStr;
    while (*ppszStr)
    {
        pTxtMng->GetSystemString(*ppszStr, szTmp);
        szStatusStrings.Add(szTmp);
        ppszStr++;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DBaseFaces::AddFace(LPCTSTR pszFigureName, LPCTSTR pszTextureName)
{
    int x, y;
    switch (nFaces % 4)
    {
    case 0:
        x = 0;
        y = 0;
        AddTexture(256, 256);
        break;
    case 1:
        x = 128;
        y = 0;
        break;
    case 2:
        x = 0;
        y = 128;
        break;
    case 3:
        x = 128;
        y = 128;
        break;
    }

    PasteTexture(textures[textures.GetSize() - 1], pszTextureName, 128, 128, 128, 128, x, y);

    CI3DFigure *pFig;
    pFig = AddFigure(pszFigureName);

    int w = 56, h = 80;
    int l, r, t, b;
    // l = 400 - nPersons * w / 2 + nFaces * w;
    l = 800 - (nFaces + 1) * w;
    r = l + w;
    t = 0;
    b = h;

    if (pFig)
    {
        pFig->SetRelativeToScreen((l + r) / 2.f, t + 32.f, 7.f);
        pFig->SetScale(.3f);
        pFig->SetOrientation(CQuaternion(CPlot3D(1, 0, 0), FP_PI * 1.05f));
    }
    else
    {
        pFig = AddFigure();
        pFig->MakeBar(7, 100, l, t, r, b - 20, 0, 0, 128, 128);
    }

    pFig->SetUShift(x / 256.f);
    pFig->SetVShift((128 + y) / 256.f);
    pFig->SetTexture(textures[textures.GetSize() - 1]);

    nFaces++;

    AddArea()->MakeRect(l, t, r, b);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DBaseFaces::AddFaces()
{
    RemoveAllFigures();
    RemoveAllAreas();
    RemoveAllTextures();

    nFaces = 0;
    CString szFigureName, szTextureName, szUnitName, szSex, szGrace;
    CChatClient::CChatPersonList &people = pChatClient->people;
    CChatClient::CChatPersonList::const_iterator it;
    nPersons = 0;
    pPersons.clear();
    for (it = people.begin(); it != people.end(); ++it)
    {
        if (!pChatClient->pState->IsValid())
            continue;
        if (pChatClient->pState->pPerson == (*it))
            pPersons.insert(pPersons.begin(), *it);
        else
            pPersons.push_back(*it);
        nPersons++;
    }
    std::vector<CPtr<CChatPerson>>::const_iterator itt;
    for (itt = pPersons.begin(); itt != pPersons.end(); ++itt)
    {
        const SUDBUnitPrototype *pProt = theUnitDatabase.prototypes[(*itt)->szPrototype];
        const SUDBRaceModel *pRace = theUnitDatabase.races[pProt->szRace];
        if (strcmp(pRace->szMaskName, "unhuma") == 0)
            szSex = "m";
        else if (strcmp(pRace->szMaskName, "unhufe") == 0)
            szSex = "f";
        /*
        SComplection compl;
        pUnit->GetComplection( compl );
        szGrace = "me";
        if ( compl.grace < 0.2f )
                szGrace = "th";
        else if ( compl.grace > 0.8f )
                szGrace = "fa";
        */
        szGrace = "me";
        szFigureName.Format("infa%s%s%dface", pRace->szMaskName, szGrace, pProt->dwHair + 1);
        szTextureName.Format("face%s%02d", szSex, pProt->dwSkinIndex);
        AddFace(szFigureName, szTextureName);
        areas[nFaces - 1]->SetToolTip((*itt)->szName.c_str());
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DBaseFaces::OnIdle()
{
    if (pChatClient->pItemXCHG->IsValid())
    {
        IWorldClient.GoCamp(IFACE_EXCHANGE);
        return;
    }

    CI3DSurface *pSurf = surfaces[0];
    int w = 56;
    // int l = 168 - pPersons.size() * w / 2;
    int l = 336 - w;
    pSurf->GetDC();
    pSurf->Clear();

    CString szStatus;
    CString szRequestTrade;
    std::vector<CPtr<CChatPerson>>::const_iterator it;
    int i;
    for (i = 0, it = pPersons.begin(); it != pPersons.end(); ++it, ++i)
    {
        szStatus.Empty();
        switch ((*it)->state)
        {
        case CPS_ZONE:
            szStatus = szStatusStrings[0];
            break;
        case CPS_BASE:
            szStatus = szStatusStrings[1];
            break;
        case CPS_CAMP:
            szStatus = szStatusStrings[2];
            break;
        case CPS_CONNECTING:
        case CPS_START:
            szStatus = szStatusStrings[3];
            break;
        }
        szRequestTrade.Empty();
        if (pChatClient->pProposal.IsValid() && pChatClient->pProposal->pWhom == (*it))
            szRequestTrade = szStatusStrings[4];
        for (auto itt = pChatClient->trades.begin(); itt != pChatClient->trades.end(); ++itt)
            if ((*itt)->pWho == (*it))
            {
                szRequestTrade = szStatusStrings[5];
                break;
            }
        if (!szStatus.IsEmpty())
            pSurf->DrawText(szStatus, CRect(l, 0, l + w, 15), 0, RGB(255, 255, 255), DT_CENTER);
        if (!szRequestTrade.IsEmpty())
            pSurf->DrawText(szRequestTrade, CRect(l, 15, l + w, 30), 0, RGB(255, 255, 255),
                            DT_CENTER);
        // l += w;
        l -= w;
    }
    pSurf->ReleaseDC();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DBaseFaces::OnLButtonDown(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    JournalHit(ht);
    if (ht >= 0 && ht < pPersons.size())
    {
        SetProcessed();
        // CRAP{ - for advanced kick/ban
        EKickMode eKickMode = ((CI3DScreenBase *)pI3D->GetCurrentScreen())->eKickMode;
        if (eKickMode != KM_NONE)
        {
            char szBuf[128];
            sprintf(szBuf, "%d", pPersons[ht]->nUniquePersonID);
            if (eKickMode == KM_KICK)
                pChatClient->Command(CC_KICK_ID, szBuf);
            else if (eKickMode == KM_BAN)
                pChatClient->Command(CC_BAN_ID, szBuf);
            ((CI3DScreenBase *)pI3D->GetCurrentScreen())->eKickMode = KM_NONE;
            return;
        }
        // CRAP} - for advanced kick/ban
        pI3D->BeginFX("buttons\\battle\\on_off.wav");
        if (pChatClient->pState->pPerson != pPersons[ht])
        {
            if (pChatClient->pProposal.IsValid() && pChatClient->pProposal->pWhom == pPersons[ht])
                pChatClient->CancelTradeRequest();
            else
                pChatClient->RequestTrade(pPersons[ht]);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DBaseFaces::OnMouseMove(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    if (((CI3DScreenBase *)pI3D->GetCurrentScreen())->eKickMode != KM_NONE)
    {
        if (ht >= 0 && ht < pPersons.size())
            theCursor.SetMode(CT_DEFAULT);
        return;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
