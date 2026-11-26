#include "StdAfx.h"
#include "model_draw.h"
#include "OwnedRender/ModelPreviewContext.h"
#include "CorePerf.h"
#include "OwnedRender/E2Host.h"
#include "OwnedRender/E2Gdi.h"
#include "OwnedRender/E1TextLifetime.h"
#include "combat_ui.h"
#include <cmath>
#include "OwnedRender/E3Input.h"
#include "OwnedRender/E3Combat.h"
#include "OwnedRender/E1Scene.h"
#include "OwnedRender/E3Hud.h"
#include "OwnedRender/E3HudVisual.h"
#include "GameJournal.h"
#include "ModStatsFont.h"
#include "ModStatRules.h"
#include "ModInfoHitChance.h"
#include "EngineDiagnostics.h"
#include "GlobalMap.h"
#include "MusicSystem.h"
#include "ModPackage.h"
#include "ModItemUiPolicy.h"
#include "ModSpellTooltip.h"
#include "ModIdleTooltips.h"
#include "ModHudAssignment.h"
#include "ModSafetyLookup.h"
#include "ModSpellTargeting.h"
#include "ModSpellIdentity.h"
#include "ModRuntime.h"
#include "ModMessages.h"
#include "UnitServer.h"
#include "aiUnitLogic.h"
#include "WideScreen.h"
static const char LOCAL_FILE[] = __FILE__;
// Defined in I3DScreenEcsapeMenu.cpp; keep f44 limited to the ASI consumers.
bool ModSpellAddonAllowsSave();
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenMission.h"

#include "I3DScreenMessageBox.h"
#include "I3DScreenMainMenu.h"
#include "I3DScreenEcsapeMenu.h"
#include "I3DScreenObjectives.h"
#include "I3DScreenSaveLoad.h"
#include "I3DInventory.h"
#include "I3DMain.h"
#include "OwnedRender/PreviewComposition.h"
#include "A3Cursor.h"
#include "I3DScrollText.h"
#include "Console.h"
#include "randomGen.h"
#include "PerkDatabase.h"
#include "Object3DClientSpecific.h"
#include "ScenarioTracker.h"
#include "TextManager.h"
#include "ItemManager.h"
#include "LightManager.h"
#include "TextureManager.h"
#include "MapObjectManager.h"
#include "PlayerClient.h"
#include "WorldInterface.h"
#include "WorldClient.h"
#include "WorldServer.h"
#include "mmpFormat.h"
#include "Terrain.h"
#include "Camera.h"
#include "StringProcessing.h"
#include "AckDatabase.h"
#include "KeyMap.h"
#include "GameMain.h"
#include "SpellDatabase.h"
#include "Renderer.h"
#include <set>
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
const float SLIDE_TIME = .3f;
const float DISTANCE_DRAW_UNIT = 9.f;
const float RULER_UNIT_SCALE = 15.f;


static bool UseModInterfaceHD()
{
    return ModRuntime::Current() && ModRuntime::Current()->CombatSetting("JMVGraphics2");
}

const int HL_XS[6] = { 50, 50, 95, 5, 95, 5 };
const int HL_YS[6] = { 35, 105, 70, 70, 140, 140 };
const int N_HL_FIGURE = 14;
const COLORREF INFO_COLOR = RGB(228, 215, 167);

/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::StoreSpecific()
{
    CArray<int, int> ids;
    for (int i = 0; i < pUnits.GetSize(); i++)
        if (pUnits[i]->CheckFlag(OBJ_SELECTED))
            ids.Add(pUnits[i]->GetID());
    StoreCArray<int, int>(ids, interfaceState);
    interfaceState.Write(pMap->GetZoomValue());
    interfaceState.Write(pI3D->GetModalPause());
    interfaceState.Write(pOutput->GetMoney());
    interfaceState.Write(pOutput->GetOutput());
    interfaceState.Write(bDoCheckExit);
    interfaceState.Write(nCheckExit);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::OnActivate()
{

    float fZoom = 1.f;
    CString szInitOutput;
    int nTotalMoney = 0;

    nCheckExit = -1;
    bDoCheckExit = false;

    bNeedAutosave = false;
    if (interfaceState.GetSize())
    {
        // this means that we load save
        interfaceState.Seek(0);
        LoadCArray<int, int>(selectionIDs, interfaceState);
        interfaceState.Read(fZoom);
        bool bPause;
        interfaceState.Read(bPause);
        IWorld.CombatPause(bPause);
        if (interfaceState.GetVersion() >= 71)
        {
            interfaceState.Read(nTotalMoney);
            interfaceState.Read(szInitOutput);
        }
        if (interfaceState.GetVersion() >= 98)
        {
            interfaceState.Read(bDoCheckExit);
            interfaceState.Read(nCheckExit);
        }
        interfaceState.SetSize(0);
    }
    else if (IWorldClient.GetParty().people.GetSize() > 0)
    {
        selectionIDs.Add(IWorldClient.GetParty().people[0].nID);
        bNeedAutosave = !pI3D->IsRestoringSavedScreen();
    }
    AddHandler(dbgnew CI3DHandlerSystemKeys(pI3D));

    AddTexture(256, 256);   // 0: info-panel effect atlas
    AddTexture("battle00"); // 1: static HUD art
    const bool hdInterface = UseModInterfaceHD();
    OwnedRender::E3HudBeginScreen(this,hdInterface);
    if (hdInterface)
    {
        // This canvas is still consumed by CI3DObjectInfo (outside portrait ownership).
        AddTexture(2048, 2048); // 2: HD info-panel effects
    }

    AddObject(pWeapons = dbgnew CI3DObjectWeapons(pI3D, this)); // 0
    pWeapons->SetTexture(textures[1]); // Static carrier art; no icon atlas.

    AddObject(pItems = dbgnew CI3DObjectItems(pI3D, this)); // 1
    pItems->SetTexture(textures[1]); // Static carrier art; no icon atlas.

    AddObject(pSpells = dbgnew CI3DObjectSpells(pI3D, this)); // 2
    pSpells->SetTexture(textures[1]); // Static carrier art; no icon atlas.

    AddObject(pSkills = dbgnew CI3DObjectSkills(pI3D, this)); // 3
    pSkills->SetTexture(textures[1]); // Static carrier art; no icon atlas.

    AddObject(pFaces = dbgnew CI3DObjectFaces(pI3D, this)); // 4
    pFaces->SetTexture(textures[1]);

    AddObject(pInfo = dbgnew CI3DObjectInfo(pI3D, this)); // 5
    pInfo->SetTexture(textures[1]);
    pInfo->SetTexture(textures[0]);
    if (hdInterface) pInfo->SetTexture(textures[2]);
    pInfo->Init(pI3D->nInfoMode, pI3D->bInfoClosed);

    AddObject(pMap = dbgnew CI3DObjectMap(pI3D, this)); // 6
    pMap->SetTexture(textures[1]);
    pMap->Init(fZoom, pI3D->bMapClosed);

    AddObject(pClock = dbgnew CI3DObjectClock(pI3D, this)); // 7
    pClock->SetTexture(textures[1]);
    pClock->Init(pI3D->nSpeed);

    AddObject(pCommands = dbgnew CI3DObjectCommands(pI3D, this)); // 8
    pCommands->SetTexture(textures[1]);
    pCommands->Init();

    AddObject(pScrollText = dbgnew CI3DScrollText(pI3D, "", CRect(190, 25, 610, 89), 3)); // 9
    pScrollText->p_eAlign = AT_LEFT;

    AddObject(pOutput = dbgnew CI3DObjectOutput(pI3D, this)); // 10
    pOutput->SetTexture(textures[1]);
    pOutput->Init(pI3D->nTextMode, pI3D->bTextClosed);
    pOutput->RestoreOutput(szInitOutput);
    pOutput->SetMoney(nTotalMoney);

    AddHandler(dbgnew CI3DHandlerNewCamera(pI3D));

    theCamera.ResetFlag(CAM_CANT_SCROLL);

    nInputMode = MODE_SIMPLE;
    nQSlot = -1;

    IWorldServer.SetWeather(EW_CLEAR, 0);
    IWorldClient.EnableAmbientSounds();

    pUnits.RemoveAll();

    bLButDown = false;
    bRubberSel = false;

    for (int i = 0; i < 6; i++)
        bHLButtonDown[i] = false;

    nQuestSound = 0;

    CTextManager *pTxtMng = theWorld.GetTextManager();
    CStringArray szPersFiles;
    CString szTmp;
    pTxtMng->GetTextNames(szPersFiles, "pers *");

    for (int i = 0; i < szPersFiles.GetSize(); i++)
        if (pTxtMng->GetText(szPersFiles[i]))
        {
            pTxtMng->GetLine(szTmp);
            int id = GetPersonageIDByName(szPersFiles[i].Mid(5));
            localNames[id] = szTmp;
        }

    OwnedRender::E3HudCommitVisuals(this);
    OwnedRender::E3MissionEnter(this);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::OnDeactivate()
{
    CancelRidingApproach();
    OwnedRender::E1TextRetire(OwnedRender::E1TextKind::pane);
    OwnedRender::E3MissionLeave(this);
    DW_SEGMENT_TIME = DW_DEFAULT_SEGMENT_TIME;

    theCamera.SetFlag(CAM_CANT_SCROLL);
    theCursor.LockPos(false);

    IWorldClient.StopAllSounds();
    IWorldClient.DisableAmbientSounds();

    pUnits.RemoveAll();
    CI3DScreen::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::OnExitFromModal(DWORD dwRetValue)
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
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScreenMission::Draw(CRenderer *pRend)
{
    CorePerf::Scope coreTiming(CorePerf::ScreenMission);
    IWorldClient.Draw();
    if (bRubberSel)
        pRend->DrawFrameSelection(rcRubber.left, rcRubber.top, rcRubber.right, rcRubber.bottom);
    OnMouseMove(0, theCursor.GetPos());
    pI3D->GetRenderer()->Clear(0, 0, D3DCLEAR_ZBUFFER, 0, 1, 0);
    bool guiShow=true;
    if(NConsole::ApplyNoGui(guiShow))for(int i=0;i<pObjects.GetSize();++i)pObjects[i]->Show(guiShow,FALSE);
    CI3DScreen::Draw(pRend);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DScreenMission::Autosave()
{
    if (bNeedAutosave)
    {
        CenterCamera(0);
        if (theSettings.nDoAutosave)
            CI3DScreenSaveLoad::QuickSave("savea", true);
        bNeedAutosave = false;
        pI3D->CheckTutorial();
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::NewHooveredItems(CList<CItem *, CItem *> &newItems, int nArrivedBucks,
                                         bool bCrippled)
{
    CItem *pItem;
    POSITION pos = newItems.GetHeadPosition();
    while (pos)
    {
        pItem = newItems.GetNext(pos);
        pOutput->AddHoover(pItem, bCrippled);
    }
    pOutput->AddHoover(nArrivedBucks);
    pOutput->UpdateHoover();
    UpdateAllObjects();
    /*
            int nSelected = 0, nIndex = -1;
            CUnit* pUnit = GetSelection( nSelected, nIndex );
            if ( pUnit && nSelected == 1 && nIndex == 0 )
                    pItems->SetUnit( pUnit );
    */
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::AddControlUnit(CUnit *pUnit)
{

    CArray<CPersonage> *people = &IWorldClient.GetParty().people;
    int j = 0;
    bool bInParty = false;
    bool bIsSelected = false;
    for (int i = 0; i < people->GetSize(); i++)
    {
        if (pUnit->GetID() == (*people)[i].nID)
        {
            pUnits.InsertAt(j, pUnit);
            bInParty = true;
            bIsSelected = (*people)[i].bIsSelected;
            break;
        }
        if (j < pUnits.GetSize() && pUnits[j]->GetID() == (*people)[i].nID)
            j++;
    }
    if (!bInParty)
        pUnits.Add(pUnit);
    pFaces->AddFaces();
    if (bIsSelected)
    {
        theWorld.SelectObject(pUnit);
        pFaces->SelectFace(j, TRUE);
    }
    for (int i = 0; i < selectionIDs.GetSize(); i++)
        if (selectionIDs[i] == pUnit->GetID())
        {
            selectionIDs.RemoveAt(i);
            theWorld.SelectObject(pUnit);
            pFaces->SelectFace(j, TRUE);
            break;
        }
    UpdateAllObjects();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::RemoveControlUnit(CUnit *pUnit)
{

    for (int i = 0; i < pUnits.GetSize(); i++)
        if (pUnits[i] == pUnit)
        {
            pUnits.RemoveAt(i);
            pFaces->AddFaces();
            UpdateAllObjects();
            break;
        }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DScreenMission::IsOneOfUnits(CUnit *pU) const
{
    for (int i = 0; i < pUnits.GetSize(); i++)
        if (pU == pUnits[i])
            return true;
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::GameOver(bool bVictory)
{
    IWorldClient.StopAllSounds();
    IWorldClient.DisableAmbientSounds();
    if (!bVictory)
    {
        pI3D->BeginFX("buttons\\gameover.wav");
        CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, "game_over", "game_over_msg",
                                                                 0x00020000);
        pI3D->DoModal(pScr);
    }
    else
    {
        CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, "end_of_game",
                                                                 "end_of_game_msg", 0xFFFF0000);
        pI3D->DoModal(pScr);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::PlayAcknowlegment(DWORD dwUnitID, DWORD dwAck, const char *pszAckID)
{
    CUnit *pU = (CUnit *)theWorld.GetMapObjectManager()->Find(dwUnitID);
    if (!pU)
        return;

    const SAckEntry *pEntry;
    if ((dwAck == ACK_HERO_SCENARIO) || (dwAck == ACK_HERO_SCENARIO_BLOCK))
        pEntry = PlayScenarioAck(pU, pszAckID, dwAck == ACK_HERO_SCENARIO_BLOCK);
    else
        pEntry = PlayAck(pU, dwAck);

    if (pEntry)
    {
        CString szWho, szText;
        GetUnitName(pU, szWho);
        szWho += ":\n";
        CTextManager *pTxtMng = IWorld.GetTextManager();
        if (pTxtMng->GetText(CString("say ") + pEntry->szKey.c_str()))
        {
            pTxtMng->GetUntilNextCommand(szText);
            szText.TrimRight("\r\n");
            szText += "\n";
            pOutput->AddOutput(szWho);
            pOutput->AddOutput(szText);
        }
    }

    if (dwAck == ACK_UNIT_BECOME_KILLER)
    {
        for (int i = 0; i < pUnits.GetSize(); i++)
            if (pUnits[i]->GetID() == dwUnitID)
                pFaces->PlayTex(i, 1);
    }
    else if (IsHeroRefuse(dwAck))
    {
        for (int i = 0; i < pUnits.GetSize(); i++)
            if (pUnits[i]->GetID() == dwUnitID)
                pFaces->PlayAnim(i, 2);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::QuestUpdate(LPCTSTR pszQuestVar, float fState)
{
    // text update
    CTextManager *pTxtMng = IWorld.GetTextManager();
    CStringArray szVarParts;
    CString szOut1, szOut2;
    NStrProc::SplitString(pszQuestVar, szVarParts, '.');
    if (szVarParts.GetSize() == 3) // quest
    {
        if (fState == 2)
        {
            nQuestSound = Max(nQuestSound, 3);
            pTxtMng->GetSystemString("combat_complete_quest", szOut2);
        }
        else if (fState == 3)
        {
            fState = 1;
            pTxtMng->GetSystemString("combat_failed_quest", szOut2);
        }
        else
            pTxtMng->GetSystemString("combat_get_quest", szOut2);
        szOut1.Empty();
        if (pTxtMng->GetText(CString("quest ") + szVarParts[2]))
            pTxtMng->GetLine(szOut1);
        szOut1 = CString("\aC\x0ee\x0e3\x031 ") + szOut2 + " " + szOut1;
    }
    else if (szVarParts.GetSize() == 4) // subobj
    {
        if (IWorldClient.GSGetVar(szVarParts[0] + "." + szVarParts[1] + "." + szVarParts[2]) == 0)
            return;
        if (fState == 2)
            pTxtMng->GetSystemString("combat_complete_subobj", szOut2);
        else if (fState == 3)
        {
            fState = 1;
            pTxtMng->GetSystemString("combat_failed_subobj", szOut2);
        }
        else
            pTxtMng->GetSystemString("combat_get_subobj", szOut2);
        szOut1.Empty();
        if (pTxtMng->GetText(CString("quest ") + szVarParts[2]) &&
            pTxtMng->FindNextCommand(CString("#subobj ") + szVarParts[3]))
        {
            pTxtMng->SkipLine();
            pTxtMng->GetLine(szOut1);
        }
        szOut1 = CString("\aC\x0ff\x0b3\x031 ") + szOut2 + " " + szOut1;
    }
    else
        ASSERT(0); // not a quest var
    szOut1 = szOut1 + "\aC\x0e4\x0d7\x0a7\n";
    pOutput->AddOutput(szOut1);
    pClock->NewObjectives();
    nQuestSound = Max(nQuestSound, (int)fState);
}
/////////////////////////////////////////////////////////////////////////////////////
namespace {
bool Rideable(CUnit* unit) {
    return unit && !unit->IsDead() && unit->GetMaskName() &&
        _stricmp(unit->GetMaskName(),"unanhoho") == 0;
}
CUnitServer* RidingDriver(CUnit* unit) {
    return unit ? dynamic_cast<CUnitServer*>(unit->GetUnitSpecific()) : nullptr;
}
void WalkToRidingPoint(CUnit* rider,const Navigation::Location& side) {
    auto& logic=RidingDriver(rider)->logic;
    if(auto* player=logic.GetPlayer()) {
        // Register the order with the same AI owner as a ground click. The idle
        // player motivation can otherwise replace an isolated low-level order.
        player->DoMoveTo(logic,CPlot3D(side.position.x,side.position.y,0),false,true,CPlot3D(0,0,0));
    }
}
bool RidingSide(CUnit* rider,CUnit* horse,Navigation::Location& side) {
    auto& map=theWorldServer.aiMap;
    auto& nav=map.Navigation();
    const auto center=nav.Position(horse,map);
    const float inner=(std::max)(.8f,nav.CollisionRadius(horse)+nav.CollisionRadius(rider));
    return nav.FreePosition(rider,center,inner,inner+.65f,1.e6f,side,horse->GetID());
}
// Test a small patch at the withers on either side. A free point behind
// the tail or in front of the muzzle cannot start a mounting animation.
bool RidingFlank(CUnit* rider,CUnit* horse,Navigation::Location& side,int& sign) {
    auto& map=theWorldServer.aiMap;
    auto& nav=map.Navigation();
    auto center=nav.Position(horse,map);
    const auto axis=horse->GetXAxis();
    const auto offset=rider->GetCP()-horse->GetCP();
    const int nearSide=offset.x*axis.x+offset.y*axis.y >= 0 ? 1 : -1;
    // The contact position belongs to the flank, not the horse's long-axis
    // navigation circle. Keep a shoe-width of room outside the barrel.
    constexpr float radius=.76f;
    const float horizontal=std::hypot(axis.x,axis.y);
    if(horizontal<.01f)return false;
    for(int flank : {nearSide,-nearSide}) {
        const Navigation::Point goal{center.x+flank*radius*axis.x/horizontal,center.y+flank*radius*axis.y/horizontal,center.z};
        if(nav.FreePosition(rider,goal,0,.14f,1.e6f,side,horse->GetID()) &&
           std::hypot(side.position.x-goal.x,side.position.y-goal.y)<.16f) {
            sign=flank;
            return true;
        }
    }
    return false;
}
bool PlaceBesideHorse(CUnit* rider,CUnit* horse,bool landing=false) {
    Navigation::Location side;
    if(landing) {
        if(!RidingFlank(rider,horse,side,rider->rideFlank))return false;
    } else if(!RidingSide(rider,horse,side))return false;
    auto& map=theWorldServer.aiMap;
    CPlot3D point(side.position.x,side.position.y,0);
    point.z=side.position.z-map.GetTileHeight(point);
    if(!map.MoveObject(rider,point))return false;
    rider->navLocation=side;
    RidingDriver(rider)->InitializeUnit();
    rider->LowNewRoute();
    rider->SetUpdateMask(CMapObject::VLD_MASK_POSROT);
    return true;
}
CString RidingSaveKey(CUnit* rider) {
    CString key;
    key.Format("jmv.riding.%s.%d.",theWorldServer.GetCurrentZoneName().GetString(),rider->GetID());
    return key;
}
void StoreRiding(CUnit* rider) {
    if(!rider->GetOwner())return;
    auto& vars=rider->GetOwner()->party.vars;
    const CString key=RidingSaveKey(rider);
    if(rider->ridingHorse<0) {
        vars.DelVar(key+"hi"); vars.DelVar(key+"lo");
    } else {
        vars.SetVar(key+"hi",float(1+(rider->ridingHorse>>16)));
        vars.SetVar(key+"lo",float(rider->ridingHorse&65535));
    }
}
void RestoreRiding(CUnit* rider) {
    if(rider->ridingRestored || !rider->GetOwner())return;
    rider->ridingRestored=true;
    auto& vars=rider->GetOwner()->party.vars;
    const CString key=RidingSaveKey(rider);
    const int hi=int(vars.GetVar(key+"hi")),lo=int(vars.GetVar(key+"lo"));
    if(hi<=0 || hi>32768 || lo<0 || lo>65535)return;
    const int id=((hi-1)<<16)|lo;
    if(auto* horse=theWorldServer.GetUnit(id); Rideable(horse) && !rider->IsDead()) {
        rider->ridingHorse=id;horse->ridingPassenger=rider->GetID();
        rider->rideTransitionTick=-1000;
        GameJournal::Event("RIDING","restored","rider=%d horse=%d",rider->GetID(),id);
    } else StoreRiding(rider);
}
const int RidingSkill = -3;
}
bool CI3DScreenMission::Dismount(CUnit* client) {
    auto* rider = client ? theWorldServer.GetUnit(client->GetID()) : nullptr;
    if (!rider || rider->ridingHorse < 0 || rider->RidingTransition(theWorld.GetRenderTimeSegments())) return false;
    auto* horse = theWorldServer.GetUnit(rider->ridingHorse);
    if (horse && RidingDriver(horse)) {
        RidingDriver(horse)->CmdWait();
        if(!PlaceBesideHorse(rider,horse,true))return false;
        // Publish the landing heading to the same route that owns standing and
        // the next footstep. Otherwise the body snaps back to its pre-ride yaw.
        const float local=-18.f*rider->rideFlank*FP_PI/180.f;
        const auto fore=horse->GetYAxis(),side=horse->GetXAxis();
        const float heading=std::atan2(side.y*std::sin(local)-fore.y*std::cos(local),
            side.x*std::sin(local)-fore.x*std::cos(local));
        rider->SetOrientation(CQuaternion(CPlot3D(0,0,1),heading+FP_PI2));
        RidingDriver(rider)->FaceScriptRoute(heading);
        rider->SetUpdateMask(CMapObject::VLD_MASK_POSROT);
    }
    // Keep the mount still until both feet have landed.
    rider->dismountHorse = horse ? horse->GetID() : -1;
    rider->rideTransitionTick = theWorld.GetRenderTimeSegments();
    theCamera.SetCarrier(client);
    rider->ridingHorse = -1;
    StoreRiding(rider);
    GameJournal::Event("RIDING","dismount","rider=%d",rider->GetID());
    return true;
}
void CI3DScreenMission::CancelRidingApproach() {
    if (approachingHorse >= 0 && theWorldServer.GetMapObjectManager()) {
        auto* horse=theWorldServer.GetUnit(approachingHorse);
        if(horse && horse->ridingPassenger==approachingRider) horse->ridingPassenger=-1;
    }
    approachingHorse=approachingRider=-1;
    ridingClearanceStep=false;
}
void CI3DScreenMission::UpdateRidingApproach() {
    if (!theWorldServer.GetMapObjectManager()) return;
    if (approachingHorse < 0) {
        // Keep the actor within the ridden unit's perception and trigger area.
        // Its independent navigation cell remains beside the horse.
        for (int i=0;i<pUnits.GetSize();++i) {
            if (!pUnits[i]) continue;
            auto* rider=theWorldServer.GetUnit(pUnits[i]->GetID());
            if (!rider) continue;
            RestoreRiding(rider);
            if(rider->dismountHorse>=0 && !rider->RidingTransition(theWorld.GetRenderTimeSegments())) {
                auto* landed=theWorldServer.GetUnit(rider->dismountHorse);
                if(landed && landed->ridingPassenger==rider->GetID())landed->ridingPassenger=-1;
                rider->dismountHorse=-1;
            }
            if (rider->ridingHorse<0 || rider->RidingTransition(theWorld.GetRenderTimeSegments())) continue;
            auto* horse=theWorldServer.GetUnit(rider->ridingHorse);
            if (!Rideable(horse) || rider->IsDead()) { Dismount(pUnits[i]); continue; }
            auto delta=rider->GetCP()-horse->GetCP();
            if (delta.x*delta.x+delta.y*delta.y<6.25f) continue;
            PlaceBesideHorse(rider,horse);
        }
        return;
    }
    auto* rider = theWorldServer.GetUnit(approachingRider);
    auto* horse = theWorldServer.GetUnit(approachingHorse);
    if (!Rideable(horse) || !RidingDriver(rider) || rider->IsDead()) {
        CancelRidingApproach();
        return;
    }
    const auto gap=rider->GetCP()-ridingApproachPoint;
    if(std::hypot(gap.x,gap.y)>.55f) {
        if(theWorld.GetRenderTimeSegments()-ridingOrderTick>32 && RidingDriver(rider)->HasNoCommand() && !rider->IsMoving())
            CancelRidingApproach();
        return;
    }
    if(ridingClearanceStep) {
        ridingClearanceStep=false;
        ridingOrderTick=theWorld.GetRenderTimeSegments();
        ridingApproachPoint=CPlot3D(ridingFlankSupport.position.x,ridingFlankSupport.position.y,0);
        WalkToRidingPoint(rider,ridingFlankSupport);
        return;
    }
    if(std::abs(rider->GetCP().z-horse->GetCP().z)>1.4f){CancelRidingApproach();return;}
    auto& logic=RidingDriver(rider)->logic;
    if(auto* player=logic.GetPlayer())player->DoStop(logic);
    RidingDriver(rider)->CmdWait();
    RidingDriver(horse)->CmdWait();
    horse->ridingPassenger = rider->GetID();
    rider->rideTransitionTick = theWorld.GetRenderTimeSegments();
    rider->dismountHorse = -1;
    rider->ridingHorse = approachingHorse;
    StoreRiding(rider);
    theCamera.SetCarrier(DWORD(approachingHorse));
    approachingHorse = -1;
    GameJournal::Event("RIDING","mount","rider=%d horse=%d flank=%d",rider->GetID(),horse->GetID(),rider->rideFlank);
}
void CI3DScreenMission::AllMove(CPlot3D plot, bool bSwarm, bool bRun)
{
    // checking exits
    int i, nSelected, nIndex, nExit;
    GetSelection(nSelected, nIndex);
    bDoCheckExit = false;
    if (IWorldClient.IsExitPoint(plot, &nExit) && nSelected > 0)
    {
        CString szVarName = IWorldClient.GetExitZoneName(nExit);
        if (szVarName.CompareNoCase("none") != 0)
        {
            szVarName = CString("z.") + szVarName;
            if (IWorldClient.GSGetVar(szVarName) != 1.f)
            {
                nCheckExit = nExit;
                bDoCheckExit = true;
            }
        }
    }
    CArray<CUnit *, CUnit *> units;
    for (i = 0; i < pUnits.GetSize(); i++)
        if (pUnits[i] && pUnits[i]->CheckFlag(OBJ_SELECTED))
        {
            CancelRidingApproach();
            auto* rider = theWorldServer.GetMapObjectManager() ? theWorldServer.GetUnit(pUnits[i]->GetID()) : nullptr;
            if (rider && rider->RidingTransition(theWorld.GetRenderTimeSegments())) continue;
            auto* horse = rider && rider->ridingHorse >= 0 ? theWorldServer.GetUnit(rider->ridingHorse) : nullptr;
            if (Rideable(horse) && RidingDriver(horse)) {
                const auto delta=plot-horse->GetCP();
                const bool gallop=bRun || delta.x*delta.x+delta.y*delta.y>=16.f;
                RidingDriver(horse)->CmdMoveTo(plot,gallop);
                GameJournal::Event("RIDING","move","horse=%d gait=%s x=%.2f y=%.2f",horse->GetID(),gallop?"gallop":"walk",plot.x,plot.y);
                continue;
            }
            units.Add(pUnits[i]);
            pFaces->PlayAnim(i, 1);
        }
    if (units.GetSize())
    {
        if (bSwarm)
            theWorld.GetPlayer()->UnitCommandSwarm(units, plot, bRun);
        else
            theWorld.GetPlayer()->UnitCommandMoveTo(units, plot, bRun);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::AllAttack(CUnit *pUnit, bool bRun)
{
    ERPGHitLocations hl = GetCShotState();
    CArray<CUnit *, CUnit *> units;
    for (int i = 0; i < pUnits.GetSize(); i++)
        if (pUnits[i] && pUnits[i]->CheckFlag(OBJ_SELECTED))
        {
            units.Add(pUnits[i]);
            pFaces->PlayAnim(i, 1);
            int nDiff = pUnit->GetLevel() - pUnits[i]->GetLevel();
            if (nDiff > 5)
                PlayAcknowlegment(pUnits[i]->GetID(), ACK_HERO_ACK_ATTACK_TOO_BIG, 0);
        }
    if (units.GetSize())
        theWorld.GetPlayer()->UnitCommandAttack(units, pUnit, hl, bRun);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::AllLoot(CUnit *pUnit, bool bRun)
{
    CArray<CUnit *, CUnit *> units;
    for (int i = 0; i < pUnits.GetSize(); i++)
        if (pUnits[i] && pUnits[i]->CheckFlag(OBJ_SELECTED))
        {
            units.Add(pUnits[i]);
            pFaces->PlayAnim(i, 1);
        }
    if (units.GetSize())
        theWorld.GetPlayer()->UnitCommandLoot(units, pUnit, bRun);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::CastSpell(CUnit *pUnit, bool bWand, bool bRun)
{
    static unsigned castNotes = 0;
    if (castNotes++ < 64) EngineDiagnostics::Event("Spell target unit=%d slot=%d wand=%d run=%d", pUnit ? pUnit->GetID() : -1, nQSlot, int(bWand), int(bRun));
    for (int i = 0; i < pUnits.GetSize(); i++)
        if (pUnits[i] && pUnits[i]->CheckFlag(OBJ_SELECTED))
        {
            CPlot3D plot = pUnit->GetCP();
            bool bCanCast = false;
            if (bWand)
            {
                theWorld.GetPlayer()->UnitCommandWand(nQSlot, pUnits[i], pUnit, bRun);
                pItems->SetUnit(pUnits[i]);
                bCanCast = (pUnits[i]->CheckIsWandPossible(nQSlot, plot) == UCS_OK);
            }
            else
            {
                theWorld.GetPlayer()->UnitCommandCast(nQSlot, pUnits[i], pUnit, bRun);
                bCanCast = (pUnits[i]->CheckIsCastPossible(nQSlot, plot) == UCS_OK);
            }
            if (bCanCast)
                pFaces->PlayAnim(i, 1);
        }
    SetInputMode(MODE_SIMPLE);
}
/////////////////////////////////////////////////////////////////////////////////////
// wands are cast without run on dblclick
void CI3DScreenMission::CastSpell(CPlot3D plot, CUnit *pTarget, bool bWand)
{
    for (int i = 0; i < pUnits.GetSize(); i++)
        if (pUnits[i] && pUnits[i]->CheckFlag(OBJ_SELECTED))
        {
            if (pUnits[i] == pTarget)
            {
                CPlot3D dir = -pTarget->GetYAxis();
                plot = plot + dir * 0.1f;
            }
            bool bCanCast = false;
            if (bWand)
            {
                theWorld.GetPlayer()->UnitCommandWand(nQSlot, pUnits[i], plot, false);
                pItems->SetUnit(pUnits[i]);
                bCanCast = (pUnits[i]->CheckIsWandPossible(nQSlot, plot) == UCS_OK);
            }
            else
            {
                theWorld.GetPlayer()->UnitCommandCast(nQSlot, pUnits[i], plot, false);
                bCanCast = (pUnits[i]->CheckIsCastPossible(nQSlot, plot) == UCS_OK);
            }
            if (bCanCast)
                pFaces->PlayAnim(i, 1);
        }
    SetInputMode(MODE_SIMPLE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::CenterCamera(int num)
{
    if (num >= pUnits.GetSize())
        return;
    theCamera.SetCarrier(pUnits[num]);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::SelectionUpdate(int num)
{


    if (num >= pUnits.GetSize())
        return;

    CArray<int, int> bStates;
    for (int i = 0; i < pUnits.GetSize(); i++)
        if (pUnits[i])
            bStates.Add(pUnits[i]->CheckFlag(OBJ_SELECTED));
        else
            bStates.Add(FALSE);

    if (pI3D->IsShiftDown())
        bStates[num] = !bStates[num];
    else
    {
        int j = -1, n = 0;
        for (int i = 0; i < bStates.GetSize(); i++)
            if (bStates[i])
            {
                j = i;
                n++;
                bStates[i] = FALSE;
            }
        // if ( n != 1 || j != num )
        bStates[num] = TRUE;
    }

    theWorld.ResetSelection();
    for (int i = 0; i < pUnits.GetSize(); i++)
    {
        pFaces->SelectFace(i, bStates[i]);
        if (pUnits[i] && bStates[i])
        {
            theWorld.SelectObject(pUnits[i]);
            if (num == i)
                PlayAcknowlegment(pUnits[i]->GetID(), ACK_HERO_ACK_SELECTION, 0);
        }
    }

    // works in single player only - stores selection of personages
    if (theWorldServer.GetServerMode() == SERV_MODE_LOCAL)
    {
        CPlayer *pPlayer = theWorldServer.GetPlayer(0);
        if (pPlayer)
        {
            CArray<CPersonage> *people = &pPlayer->party.people;
            for (int i = 0; i < people->GetSize(); i++)
                if (i < pUnits.GetSize() && pUnits[i]->GetID() == (*people)[i].nID)
                    (*people)[i].bIsSelected = pUnits[i]->CheckFlag(OBJ_SELECTED) ? true : false;
        }
    }

    UpdateAllObjects();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::ResetSelection()
{

    theWorld.ResetSelection();
    for (int i = 0; i < pUnits.GetSize(); i++)
    {
        pFaces->SelectFace(i, FALSE);
    }
    // works in single player only - stores selection of personages
    if (theWorldServer.GetServerMode() == SERV_MODE_LOCAL)
    {
        CPlayer *pPlayer = theWorldServer.GetPlayer(0);
        if (pPlayer)
        {
            CArray<CPersonage> *people = &pPlayer->party.people;
            for (int i = 0; i < people->GetSize(); i++)
                if (i < pUnits.GetSize() && pUnits[i]->GetID() == (*people)[i].nID)
                    (*people)[i].bIsSelected = pUnits[i]->CheckFlag(OBJ_SELECTED) ? true : false;
        }
    }
    UpdateAllObjects();
}
/////////////////////////////////////////////////////////////////////////////////////
CUnit *CI3DScreenMission::GetSelection(int &nSelected, int &nIndex)
{
    nSelected = 0;
    nIndex = -1;
    CUnit *pUnit = 0;
    for (int i = 0; i < pUnits.GetSize(); i++)
        if (pUnits[i] && pUnits[i]->CheckFlag(OBJ_SELECTED))
        {
            if (!pUnit)
            {
                pUnit = pUnits[i];
                nIndex = i;
            }
            nSelected++;
        }
    return pUnit;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::UpdateAllObjects()
{
    int nSelected = 0, nIndex = -1;
    CUnit *pUnit = GetSelection(nSelected, nIndex);
    if (pUnit && !pUnit->IsDead() && nSelected == 1)
    {
        pWeapons->SetUnit(pUnit);
        pItems->SetUnit(pUnit);
        pSpells->SetUnit(pUnit);
        pSkills->SetUnit(pUnit);
        pInfo->SetUnit(pUnit);
    }
    else
    {
        pWeapons->SetUnit(0);
        pItems->SetUnit(0);
        pSpells->SetUnit(0);
        pSkills->SetUnit(0, nSelected > 0);
        pInfo->SetUnit(pUnit);
    }
    SetInputMode(MODE_SIMPLE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::SetInputMode(int nMode, int nSlot, int nPar)
{
    nQSlot = nSlot;
    nInputMode = nMode;
    nParam = nPar;
    pItems->DeselectItems();
    pSpells->DeselectSpells();
    pSkills->DeselectSkills();

    switch (nMode)
    {
    case MODE_SPELL_TARGET_UNIT:
    case MODE_SPELL_TARGET_TILE:
        pSpells->ShowSelectSpell(nSlot, TRUE);
        break;
    case MODE_RIDE_TARGET:
    case MODE_SKILL_TARGET_UNIT:
    case MODE_SKILL_TARGET_OBJECT:
        pSkills->ShowSelectSkill(nSlot, TRUE);
        break;
    case MODE_ITEM_TARGET_UNIT:
    case MODE_ITEM_TARGET_TILE:
    case MODE_ITEM_TARGET_OBJECT:
    case MODE_QUEST_TARGET_OBJECT:
        pItems->ShowSelectItem(nSlot, TRUE);
        break;
    default:
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::GetInputMode(int &nMode, int &nSlot)
{
    nSlot = nQSlot;
    nMode = nInputMode;
}
/////////////////////////////////////////////////////////////////////////////////////
ERPGHitLocations CI3DScreenMission::GetCShotState()
{
    ERPGHitLocations hl = RPGHL_ALL;
    int i;
    for (i = 0; i < 6; i++)
        if (bHLButtonDown[i])
            break;
    switch (i)
    {
    case 0:
        hl = RPGHL_HEAD;
        break;
    case 1:
        hl = RPGHL_BODY;
        break;
    case 2:
        hl = RPGHL_LHAND;
        break;
    case 3:
        hl = RPGHL_RHAND;
        break;
    case 4:
        hl = RPGHL_LLEG;
        break;
    case 5:
        hl = RPGHL_RLEG;
        break;
    default:
        hl = RPGHL_ALL;
        break;
    }
    return hl;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::OnIdle()
{
    UpdateRidingApproach();
    static int fieldCampHeroId = -1;
    static DWORD fieldCampFinalizeStart = 0;
    static bool fieldCampMusicReady = false;
    static bool fieldCampCameraReady = false;
    static bool fieldCampPauseRequested = false;
    const int returnHeroId = theWorldServer.FieldCampReturnHeroID();
    if (returnHeroId < 0) {
        fieldCampHeroId = -1;
        fieldCampFinalizeStart = 0;
        fieldCampMusicReady = fieldCampCameraReady = fieldCampPauseRequested = false;
    } else {
        if (fieldCampHeroId != returnHeroId) {
            fieldCampHeroId = returnHeroId;
            fieldCampFinalizeStart = GetTickCount();
            fieldCampMusicReady = fieldCampCameraReady = fieldCampPauseRequested = false;
        }
        if (GetTickCount() - fieldCampFinalizeStart > 30000) {
            theWorldServer.CompleteFieldCampReturn();
            fieldCampHeroId = -1;
            fieldCampFinalizeStart = 0;
            fieldCampMusicReady = fieldCampCameraReady = fieldCampPauseRequested = false;
        } else {
            CUnit *fieldCampHero = IWorldClient.GetUnit(returnHeroId);
            if (fieldCampHero) {
                if (!fieldCampMusicReady) {
                    CMusicSystem *music = theWorld.GetMusicSystem();
                    if (music) {
                        CString zone = IWorldClient.GetCurrentZoneName();
                        CString theme("gipat");
                        const SScenarioZone *zoneInfo = theMap.GetZoneByName(zone);
                        if (zoneInfo) theme = zoneInfo->szAllodName;
                        if (zone.CompareNoCase("gz20g") == 0) theme = "final";
                        music->SetZoneMode(theme, theWorld.IsCaveZone() ? TRUE : FALSE);
                        music->UnpauseZoneMode();
                        fieldCampMusicReady = true;
                    }
                }
                if (!fieldCampCameraReady) {
                    if (!theCamera.RestoreFieldCampCamera())
                        theCamera.SetCarrier(fieldCampHero);
                    fieldCampCameraReady = true;
                }
                if (fieldCampMusicReady && fieldCampCameraReady) {
                    if (!fieldCampPauseRequested && pClock) {
                        if (!IWorld.IsMissionCombatPause()) pClock->TogglePause();
                        fieldCampPauseRequested = true;
                    } else if (fieldCampPauseRequested && IWorld.IsMissionCombatPause()) {
                        theWorldServer.CompleteFieldCampReturn();
                        fieldCampHeroId = -1;
                        fieldCampFinalizeStart = 0;
                        fieldCampMusicReady = fieldCampCameraReady = fieldCampPauseRequested = false;
                    }
                }
            }
        }
    }
    int nSelected = 0, nIndex = -1;
    CUnit *pUnit = GetSelection(nSelected, nIndex);
    if (pUnit && pUnit->IsDead() && nSelected == 1)
    {
        pWeapons->SetUnit(0);
        pItems->SetUnit(0);
        pSpells->SetUnit(0);
        pSkills->SetUnit(0, false);
        pInfo->SetUnit(pUnit);
    }



    CI3DScreen::OnIdle();

    pI3D->CheckMissionTutorials();

    if (bDoCheckExit)
    {
        CPersonages personages, peopleOnExit;
        CParty &party = IWorldClient.GetParty();
        for (int i = 0; i < party.people.GetSize(); i++)
            personages.Add(&party.people[i]);
        if (IWorldClient.ArePersonagesAtExit(personages, nCheckExit, peopleOnExit))
        {
            CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, "leave_zone",
                                                                     "leave_zone_msg", 0x00010000);
            pI3D->DoModal(pScr);
            bDoCheckExit = false;
        }
    }

    if (bRubberSel)
    {
        std::list<CMapObject *> mobs;
        theWorld.PickMapObjects(rcRubber, mobs);

        std::vector<int> match;
        for (auto it = mobs.begin(); it != mobs.end(); ++it)
        {
            for (int i = 0; i < pUnits.GetSize(); i++)
                if (pUnits[i] == (*it))
                    match.push_back(i);
        }

        for (int i = 0; i < pUnits.GetSize(); i++)
            pUnits[i]->Preselect(FALSE);
        for (auto itMatch = match.begin(); itMatch != match.end(); ++itMatch)
            pUnits[*itMatch]->Preselect(TRUE);
    }

    if (nQuestSound)
    {
        switch (nQuestSound)
        {
        case 1:
            pI3D->BeginFX("buttons\\quest.wav");
            break;
        case 2:
            pI3D->BeginFX("buttons\\subcomplete.wav");
            break;
        case 3:
            pI3D->BeginFX("buttons\\complete.wav");
            break;
        }
        nQuestSound = 0;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::ResetInputState()
{
    for (int i = 0; i < 6; i++)
        bHLButtonDown[i] = false;
    bLButDown = false;
    bRubberSel = false;
    CI3DScreen::ResetInputState();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::OnLButtonDown(UINT nFlags, CPoint point)
{
    // Addon LS_MissionOnLButtonDown/DoubleClickExtendedSpells intercepts the
    // icon before native screen routing; single click only selects a target mode.
    if (ModRuntime::Current() && pSpells && pSpells->IsVisible()) {
        const int slot = pSpells->HitTest(point);
        if (slot >= 0 && slot < QI_MAX_SPELLS) {
            SetProcessed(); bLButDown = bRubberSel = false;
            pSpells->JournalHit(slot);
            pSpells->ChangeSpell(slot);
            return;
        }
    }


    if (!bLButDown)
    {
        CI3DScreen::OnLButtonDown(nFlags, point);
        if (GetProcessed())
            return;
    }
    bLButDown = true;
    startRubber = point;
    bRubberSel = false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    // Addon LS_MissionOnLButtonDown/DoubleClickExtendedSpells intercepts the
    // icon before native screen routing; single click only selects a target mode.
    if (ModRuntime::Current() && pSpells && pSpells->IsVisible()) {
        const int slot = pSpells->HitTest(point);
        if (slot >= 0 && slot < QI_MAX_SPELLS) {
            SetProcessed(); bLButDown = bRubberSel = false;
            pSpells->SelfCast(slot);
            return;
        }
    }


    if (!bLButDown)
    {
        CI3DScreen::OnLButtonDblClk(nFlags, point);
        if (GetProcessed())
            return;
    }
    bLButDown = true;
    startRubber = point;
    bRubberSel = false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::OnLButtonUp(UINT nFlags, CPoint point)
{

    if (!bLButDown)
    {
        CI3DScreen::OnLButtonUp(nFlags, point);
        return;
    }
    bLButDown = false;
    if (bRubberSel)
    {
        int nSomeoneFirst = -1;
        std::list<CMapObject *> mobs;
        theWorld.PickMapObjects(rcRubber, mobs);
        std::vector<int> match;
        for (auto it = mobs.begin(); it != mobs.end(); ++it)
        {
            for (int i = 0; i < pUnits.GetSize(); i++)
                if (pUnits[i] == (*it))
                {
                    match.push_back(i);
                    if (nSomeoneFirst < 0)
                        nSomeoneFirst = i;
                }
        }
        if (nSomeoneFirst >= 0)
        {
            theWorld.ResetSelection();
            for (int i = 0; i < pUnits.GetSize(); i++)
            {
                pUnits[i]->Preselect(FALSE);
                pFaces->SelectFace(i, FALSE);
            }
            for (auto itMatch = match.begin(); itMatch != match.end(); ++itMatch)
            {
                pFaces->SelectFace(*itMatch, TRUE);
                theWorld.SelectObject(pUnits[*itMatch]);
            }
            PlayAcknowlegment(pUnits[nSomeoneFirst]->GetID(), ACK_HERO_ACK_SELECTION, 0);
        }
        UpdateAllObjects();

        bRubberSel = false;
        return;
    }

    CUnit *pU = theWorld.CombinedPickFirstUnit(point);
    if (pU && !pU->CheckFlag(UNIT_VISIBLE))
        pU = 0;

    PerformAction(pU, point);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::PerformAction(CUnit *pU, CPoint point)
{
    GameJournal::ObserveObject(pU, "click");
    int i, nSelected = 0, nIndex = -1;
    CMapObject *pObj = 0;
    CUnit *pHero = 0;
    CPlot3D plot;
    CUnit *pLiveU = 0;

    if (pU && !pU->IsDead())
        pLiveU = pU;

    // if there is no selection
    pHero = GetSelection(nSelected, nIndex);
    if (!nSelected)
    {
        for (i = 0; i < pUnits.GetSize(); i++)
            if (pU == pUnits[i])
            {
                pI3D->BeginFX("buttons\\battle\\on_off.wav");
                if (pI3D->IsLDblClk())
                    CenterCamera(i);
                else
                    SelectionUpdate(i);
            }
        return;
    }
    // force move
    if (pI3D->IsAltDown())
    {
        if (pU)
            plot = pU->GetCP();
        else
            plot = theWorld.GetTerrainXYZFromScreenXYWithBridge(point.x, point.y);
        AllMove(plot, false, pI3D->IsLDblClk());
        return;
    }
    // force called shots
    for (i = 0; i < 6; i++)
        if (bHLButtonDown[i])
        {
            if (pU && !pU->IsDead())
                AllAttack(pU, pI3D->IsLDblClk());
            else
            {
                plot = theWorld.GetTerrainXYZFromScreenXYWithBridge(point.x, point.y);
                AllMove(plot, true, pI3D->IsLDblClk());
            }
            return;
        }
    // force attack or swarm
    if (pI3D->IsCtrlDown())
    {
        if (pU && !pU->IsDead())
            AllAttack(pU, pI3D->IsLDblClk());
        else
        {
            plot = theWorld.GetTerrainXYZFromScreenXYWithBridge(point.x, point.y);
            AllMove(plot, true, pI3D->IsLDblClk());
        }
        return;
    }

    switch (nInputMode)
    {
    case MODE_SIMPLE:
        if (pU)
        {
            if (pU->IsDead())
            {
                AllLoot(pU, pI3D->IsLDblClk());
                return;
            }
            for (i = 0; i < pUnits.GetSize(); i++)
                if (pU == pUnits[i])
                {
                    pI3D->BeginFX("buttons\\battle\\on_off.wav");
                    if (pI3D->IsLDblClk())
                        CenterCamera(i);
                    else
                        SelectionUpdate(i);
                    return;
                }
            if (pUnits.GetSize() && pUnits[0] && pUnits[0]->IsEnemy(pU))
                AllAttack(pU, pI3D->IsLDblClk());
            else
                AllMove(pU->GetCP(), false, pI3D->IsLDblClk());
        }
        else
        {
            pObj = theWorld.PickFirstMapObject(point);
            GameJournal::ObserveObject(pObj, "click");
            static unsigned pickSamples=0;
            if(ModRuntime::Current()&&pickSamples++<24)EngineDiagnostics::Event("Interaction click object=%d scientific=%d door=%d selected=%d",pObj?pObj->GetID():-1,pObj?pObj->IsScientific():0,pObj?pObj->IsScientificDoor():0,nSelected);
            if (pObj && pObj->IsScientific() && !pObj->IsScientificDoor() && nSelected == 1)
            {
                theWorld.GetPlayer()->UnitCommandSkill(pHero, pObj, (BYTE)RPGMSKL_SCIENCE, -1,
                                                       pI3D->IsLDblClk());
                pFaces->PlayAnim(nIndex, 1);
            }
            else
            {
                plot = theWorld.GetTerrainXYZFromScreenXYWithBridge(point.x, point.y);
                AllMove(plot, false, pI3D->IsLDblClk());
                // log for Zarus
                NConsole::graphics << "Terrain Coords " << plot.x << " " << plot.y << " " << plot.z
                                   << endl;
                //
            }
        }
        break;

    case MODE_SPELL_TARGET_UNIT:
        if (pU && !pU->IsDead())
        {
            plot = pU->GetCP();
            if (pHero) //&& pHero->CheckIsCastPossible( nQSlot, plot ) == UCS_OK )
                CastSpell(pU, false, pI3D->IsLDblClk());
        }
        break;

    case MODE_SPELL_TARGET_TILE:
        if (pU && !pU->IsDead())
            plot = pU->GetCP();
        else
            plot = theWorld.GetTerrainXYZFromScreenXYWithBridge(point.x, point.y);
        if ((plot.x != 0 || plot.y != 0) &&
            pHero) //&& pHero->CheckIsCastPossible( nQSlot, plot ) == UCS_OK )
            CastSpell(plot, pLiveU);
        break;

    case MODE_ITEM_TARGET_UNIT:
        if (pU && !pU->IsDead())
        {
            plot = pU->GetCP();
            if (pHero) //&& pHero->CheckIsWandPossible( nQSlot, plot ) == UCS_OK )
            {
                CItemQuick *pQuick = pHero->GetQItem(nQSlot);
                if (pQuick && pQuick->GetQuickItemType() == IDB_SCROLL && pU != pHero)
                    break;
                CastSpell(pU, true);
            }
        }
        break;

    case MODE_ITEM_TARGET_TILE:
        if (pU && !pU->IsDead())
            plot = pU->GetCP();
        else
            plot = theWorld.GetTerrainXYZFromScreenXYWithBridge(point.x, point.y);
        if ((plot.x != 0 || plot.y != 0) &&
            pHero) //&& pHero->CheckIsWandPossible( nQSlot, plot ) == UCS_OK )
            CastSpell(plot, pLiveU, true);
        break;

    case MODE_ITEM_TARGET_OBJECT:
        if (pU)
            pObj = pU;
        else
            pObj = theWorld.PickFirstMapObject(point);
            GameJournal::ObserveObject(pObj, "click");
        if (pObj && pHero && pObj->IsScientific())
        {
            theWorld.GetPlayer()->UnitCommandSkill(pHero, pObj, (BYTE)RPGMSKL_SCIENCE, nParam,
                                                   pI3D->IsLDblClk());
            pFaces->PlayAnim(nIndex, 1);
            SetInputMode(MODE_SIMPLE);
        }
        break;

    case MODE_QUEST_TARGET_OBJECT:
        if (pU)
            pObj = pU;
        else
            pObj = theWorld.PickFirstMapObject(point);
            GameJournal::ObserveObject(pObj, "click");
        if (pObj && pHero && pObj->IsScientific() && pObj->CanApplyScience(tool))
        {
            theWorld.GetPlayer()->UnitCommandSkill(pHero, pObj, (BYTE)RPGMSKL_SCIENCE, nParam,
                                                   pI3D->IsLDblClk());
            pFaces->PlayAnim(nIndex, 1);
            SetInputMode(MODE_SIMPLE);
        }
        break;

    case MODE_RIDE_TARGET:
        if (pHero && Rideable(pU) && theWorldServer.GetMapObjectManager()) {
            auto* rider = theWorldServer.GetUnit(pHero->GetID());
            auto* horse = theWorldServer.GetUnit(pU->GetID());
            if (RidingDriver(rider) && RidingDriver(horse) && rider->ridingHorse < 0 && !rider->RidingTransition(theWorld.GetRenderTimeSegments())) {
                CancelRidingApproach();
                if (horse->ridingPassenger >= 0) break;
                RidingDriver(horse)->CmdWait();
                approachingRider = rider->GetID();
                approachingHorse = horse->GetID();
                horse->ridingPassenger = rider->GetID();
                Navigation::Location side;
                if (RidingFlank(rider,horse,side,rider->rideFlank)) {
                    ridingOrderTick=theWorld.GetRenderTimeSegments();
                    ridingFlankSupport=side;
                    auto& map=theWorldServer.aiMap;
                    auto& nav=map.Navigation();
                    const auto center=nav.Position(horse,map);
                    const auto from=nav.Position(rider,map);
                    const float dx=from.x-center.x,dy=from.y-center.y;
                    const float distance=std::hypot(dx,dy);
                    const float radius=nav.CollisionRadius(horse)+nav.CollisionRadius(rider);
                    if(distance>.02f && distance<radius+.08f && (dx*horse->GetXAxis().x+dy*horse->GetXAxis().y)*rider->rideFlank<0) {
                        // A saved/placed actor may already overlap the horse's
                        // navigation circle. Walk out radially before rounding it.
                        const float scale=(radius+.55f)/distance;
                        // When two horses stand close, the radial exit itself
                        // may be occupied. Fan toward the selected flank first.
                        const float handed=dx*(side.position.y-center.y)-dy*(side.position.x-center.x)>=0 ? 1.f : -1.f;
                        for(float angle : {0.f,.92f,-.92f,.48f,-.48f}) {
                            const float c=std::cos(angle),s=std::sin(angle)*handed;
                            Navigation::Point outside{center.x+(dx*c-dy*s)*scale,center.y+(dy*c+dx*s)*scale,from.z};
                            Navigation::Location clear;
                            if(nav.FreePosition(rider,outside,0,.30f,1.e6f,clear,horse->GetID()) &&
                               (clear.position.x-from.x)*dx+(clear.position.y-from.y)*dy>0) {
                                ridingClearanceStep=true;
                                side=clear;
                                break;
                            }
                        }
                        if(!ridingClearanceStep) {
                            CancelRidingApproach();
                            SetInputMode(MODE_SIMPLE);
                            break;
                        }
                    }
                    ridingApproachPoint=CPlot3D(side.position.x,side.position.y,0);
                    const CPlot3D target(side.position.x,side.position.y,1.e6f);
                    GameJournal::Event("RIDING","approach","horse=%d side=%d clearance=%d target=(%.2f,%.2f) flank=(%.2f,%.2f)",horse->GetID(),rider->rideFlank,int(ridingClearanceStep),target.x,target.y,ridingFlankSupport.position.x,ridingFlankSupport.position.y);
                    WalkToRidingPoint(rider,side);
                    UpdateRidingApproach();
                } else {
                    GameJournal::Event("RIDING","flank_unavailable","horse=%d",horse->GetID());
                    CancelRidingApproach();
                }
                SetInputMode(MODE_SIMPLE);
            }
        }
        break;
    case MODE_SKILL_TARGET_OBJECT:
        if (pU)
            pObj = pU;
        else
            pObj = theWorld.PickFirstMapObject(point);
            GameJournal::ObserveObject(pObj, "click");
        if (pObj && pHero)
        {
            if (pU)
            {
                if (!IsOneOfUnits(pU))
                {
                    theWorld.GetPlayer()->UnitCommandSkill(pHero, pU, (BYTE)RPGMSKL_STEALING, -1,
                                                           pI3D->IsLDblClk());
                    pFaces->PlayAnim(nIndex, 1);
                    SetInputMode(MODE_SIMPLE);
                }
            }
            else if (pObj->IsScientific())
            {
                theWorld.GetPlayer()->UnitCommandSkill(pHero, pObj, (BYTE)RPGMSKL_SCIENCE, -1,
                                                       pI3D->IsLDblClk());
                pFaces->PlayAnim(nIndex, 1);
                SetInputMode(MODE_SIMPLE);
            }
        }
        break;

    case MODE_SKILL_TARGET_UNIT:
        if (pU && !pU->IsDead())
        {
            for (int i = 0; i < pUnits.GetSize(); i++)
                if (pUnits[i] && pUnits[i]->CheckFlag(OBJ_SELECTED))
                {
                    theWorld.GetPlayer()->UnitFollow(pUnits[i], pU);
                    PlayAcknowlegment(pUnits[i]->GetID(), ACK_HERO_ACK_FOLLOW, 0);
                    pFaces->PlayAnim(i, 1);
                }
            SetInputMode(MODE_SIMPLE);
        }
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::OnMouseMove(UINT nFlags, CPoint point)
{
    if (OwnedRender::E3EditorPaused()) return;

    theCursor.SetMode(CT_DEFAULT);

    if (bLButDown && theSettings.nCameraFrameSelectionSensetiveArea > 0)
    {
        if (abs(point.x - startRubber.x) > theSettings.nCameraFrameSelectionSensetiveArea ||
            abs(point.y - startRubber.y) > theSettings.nCameraFrameSelectionSensetiveArea)
            bRubberSel = true;
        if (bRubberSel)
        {
            pInfo->SetTempUnit(0);
            rcRubber.left = Min(point.x, startRubber.x);
            rcRubber.top = Min(point.y, startRubber.y);
            rcRubber.right = Max(point.x, startRubber.x);
            rcRubber.bottom = Max(point.y, startRubber.y);
            return;
        }
    }
    else
    {
        CI3DScreen::OnMouseMove(nFlags, point);
        if (GetProcessed())
            return;
    }

    CUnit *pU = theWorld.CombinedPickFirstUnit(point);
    CheckAction(pU, point);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::CheckAction(CUnit *pU, CPoint point)
{
    int nSelected = 0, nIndex = -1;
    CMapObject *pObj = 0;
    CUnit *pHero = 0;
    CPlot3D plot;
    struct HoverPublication {
        CUnit*& unit; CMapObject*& object;
        ~HoverPublication() {
            CMapObject* target = object ? object : unit;
            const auto identity = target ? OwnedRender::E1Scene().Identity(target->GetID()) : owned::LifecycleIdentity{};
            OwnedRender::E2SetHoveredObject(identity.id, identity.generation, theCursor.GetMode());
            const auto resource = unit && unit->CheckFlag(UNIT_VISIBLE) && !unit->IsDead()
                ? OwnedRender::E1Scene().Resource(unit) : owned::VisualResourceIncarnation{};
            OwnedRender::E3PublishHover(resource.signed_id, resource.serial);
        }
    } hoverPublication{pU, pObj};

#ifdef NODEBUGINFO
    if (pU && !pU->CheckFlag(UNIT_VISIBLE))
        pU = 0;
    if (pU && !pU->IsDead())
        pInfo->SetTempUnit(pU);
    else
        pInfo->SetTempUnit(0);
#else
    pInfo->SetTempUnit(pU);
    if (pU && !pU->CheckFlag(UNIT_VISIBLE))
        pU = 0;
#endif // NODEBUGINFO

    // if there is no selection
    pHero = GetSelection(nSelected, nIndex);
    if (!nSelected)
        return;
    // forcing move
    if (pI3D->IsAltDown())
    {
        int nExit;
        plot = theWorld.GetTerrainXYZFromScreenXYWithBridge(point.x, point.y);
        if (theWorld.IsExitPoint(plot, &nExit))
            theCursor.SetMode(CT_MOVE);
        return;
    }
    // forcing called shots
    for (int i = 0; i < 6; i++)
        if (bHLButtonDown[i])
        {
            theCursor.SetMode(CT_ATTACK_HD + i);
            return;
        }
    // forcing swarm or move
    if (pI3D->IsCtrlDown())
    {
        theCursor.SetMode(CT_ATTACK);
        return;
    }

    switch (nInputMode)
    {
    case MODE_SIMPLE:
        if (pU)
        {
            if (pU->IsDead())
            {
                theCursor.SetMode(CT_USE);
                return;
            }
            if (pUnits.GetSize() && pUnits[0] && pUnits[0]->IsEnemy(pU))
                theCursor.SetMode(CT_ATTACK);
        }
        else
        {
            pObj = theWorld.PickFirstMapObject(point);
            if (pObj && pObj->IsScientific() && !pObj->IsScientificDoor() && nSelected == 1)
                theCursor.SetMode(CT_USE);
            else
            {
                int nExit;
                plot = theWorld.GetTerrainXYZFromScreenXYWithBridge(point.x, point.y);
                if (theWorld.IsExitPoint(plot, &nExit))
                {
                    CString szVarName = IWorldClient.GetExitZoneName(nExit);
                    if (szVarName.CompareNoCase("none") != 0)
                    {
                        szVarName = CString("z.") + szVarName;
                        if (IWorldClient.GSGetVar(szVarName) != 1.f)
                            theCursor.SetMode(CT_MOVE);
                    }
                }
            }
        }
        break;

    case MODE_SPELL_TARGET_UNIT:
        if (pU && !pU->IsDead())
        {
            plot = pU->GetCP();
            if (pHero && pHero->CheckIsCastPossible(nQSlot, plot) == UCS_OK)
                theCursor.SetMode(CT_SPELL);
            else
                theCursor.SetMode(CT_SPELL_CANCEL);
        }
        else
            theCursor.SetMode(CT_SPELL_CANCEL);
        break;

    case MODE_SPELL_TARGET_TILE:
        if (pU && !pU->IsDead())
            plot = pU->GetCP();
        else
            plot = theWorld.GetTerrainXYZFromScreenXYWithBridge(point.x, point.y);
        if ((plot.x != 0 || plot.y != 0) && pHero &&
            pHero->CheckIsCastPossible(nQSlot, plot) == UCS_OK)
            theCursor.SetMode(CT_SPELL);
        else
            theCursor.SetMode(CT_SPELL_CANCEL);
        break;

    case MODE_ITEM_TARGET_UNIT:
        if (pU && !pU->IsDead())
        {
            plot = pU->GetCP();
            if (pHero && pHero->CheckIsWandPossible(nQSlot, plot) == UCS_OK)
            {
                CItemQuick *pQuick = pHero->GetQItem(nQSlot);
                if (pQuick && pQuick->GetQuickItemType() == IDB_SCROLL && pU != pHero)
                    theCursor.SetMode(CT_SPELL_CANCEL);
                else
                    theCursor.SetMode(CT_SPELL);
            }
            else
                theCursor.SetMode(CT_SPELL_CANCEL);
        }
        else
            theCursor.SetMode(CT_SPELL_CANCEL);
        break;

    case MODE_ITEM_TARGET_TILE:
        if (pU && !pU->IsDead())
            plot = pU->GetCP();
        else
            plot = theWorld.GetTerrainXYZFromScreenXYWithBridge(point.x, point.y);
        if ((plot.x != 0 || plot.y != 0) && pHero &&
            pHero->CheckIsWandPossible(nQSlot, plot) == UCS_OK)
            theCursor.SetMode(CT_SPELL);
        else
            theCursor.SetMode(CT_SPELL_CANCEL);
        break;

    case MODE_ITEM_TARGET_OBJECT:
        if (pU)
            pObj = pU;
        else
            pObj = theWorld.PickFirstMapObject(point);
        if (pObj && pHero && pObj->IsScientific())
            theCursor.SetMode(CT_USE);
        else
            theCursor.SetMode(CT_CANCEL);
        break;

    case MODE_QUEST_TARGET_OBJECT:
        if (pU)
            pObj = pU;
        else
            pObj = theWorld.PickFirstMapObject(point);
        if (pObj && pHero && pObj->IsScientific() && pObj->CanApplyScience(tool))
            theCursor.SetMode(CT_USE);
        else
            theCursor.SetMode(CT_CANCEL);
        break;

    case MODE_RIDE_TARGET:
        theCursor.SetMode(pHero && Rideable(pU) ? CT_USE : CT_CANCEL);
        break;
    case MODE_SKILL_TARGET_OBJECT:
        if (pU)
            pObj = pU;
        else
            pObj = theWorld.PickFirstMapObject(point);
        if (pU && pHero)
        {
            if (!IsOneOfUnits(pU))
                theCursor.SetMode(CT_STEAL);
            else
                theCursor.SetMode(CT_CANCEL);
        }
        else if (pObj && pHero && pObj->IsScientific())
            theCursor.SetMode(CT_USE);
        else
            theCursor.SetMode(CT_CANCEL);
        break;

    case MODE_SKILL_TARGET_UNIT:
        if (pHero && pU && !pU->IsDead())
            theCursor.SetMode(CT_MOVE);
        else
            theCursor.SetMode(CT_CANCEL);
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if (pI3D && pI3D->IsAltDown())
    {
        int extendedSpellSlot = -1;
        switch (nChar)
        {
        case 'Q': extendedSpellSlot = 8; break;
        case 'W': extendedSpellSlot = 9; break;
        case 'E': extendedSpellSlot = 10; break;
        case 'R': extendedSpellSlot = 11; break;
        case 'T': extendedSpellSlot = 12; break;
        case 'Y': extendedSpellSlot = 13; break;
        case 'U': extendedSpellSlot = 14; break;
        case 'I': extendedSpellSlot = 15; break;
        }
        if (extendedSpellSlot >= 0 && pSpells)
        {
            pSpells->ChangeSpell(extendedSpellSlot);
            SetProcessed();
            return;
        }
    }




    CI3DScreen::OnKeyDown(nChar, nRepCnt, nFlags);
    if (GetProcessed())
        return;

    if (nChar == 'N' && ModRuntime::Current() && pI3D->IsShiftDown() &&
        !pI3D->IsAltDown() && !pI3D->IsCtrlDown()) {
        if ((nFlags & 0x4000) == 0) theCamera.ToggleFalloutMode();
        SetProcessed();
        return;
    }

    if (nFlags & 0x4000)
        return;

    switch (nChar)
    {
    case VK_ESCAPE:
        if (nInputMode != MODE_SIMPLE)
            SetInputMode(MODE_SIMPLE);
        else
        {
            pI3D->DoModal(dbgnew CI3DScreenEscapeMenu(pI3D));
            SetProcessed();
        }
        return;
    case VK_END: {
        if (theCamera.CheckFlag(CAM_FPS_MODE))
        {
            theCamera.SetFPSMode(DWORD(-1));
            return;
        }
        for (int i = 0; i < pUnits.GetSize(); i++)
            if (pUnits[i]->CheckFlag(OBJ_SELECTED))
            {
                theCamera.SetFPSMode(pUnits[i]);
                return;
            }
        theCamera.SetFPSMode(DWORD(-1));
    }
    break;
    case VK_INSERT: {
        theCamera.SetFPSMode(DWORD(-1));
        return;
    }
    }

    switch (NKeyMap::keyMap[nFlags & 0x1ff])
    {
    case KA_CSHOT_HEAD:
        bHLButtonDown[0] = true;
        return;
    case KA_CSHOT_BODY:
        bHLButtonDown[1] = true;
        return;
    case KA_CSHOT_LHAND:
        bHLButtonDown[2] = true;
        return;
    case KA_CSHOT_RHAND:
        bHLButtonDown[3] = true;
        return;
    case KA_CSHOT_LLEG:
        bHLButtonDown[4] = true;
        return;
    case KA_CSHOT_RLEG:
        bHLButtonDown[5] = true;
        return;

    case KA_PAUSE:
        pClock->TogglePause();
        SetProcessed();
        return;
    case KA_ACCELERATE:
        pClock->AccelerateFromKeyboard();
        break;
    case KA_DECELERATE:
        pClock->DecelerateFromKeyboard();
        break;
    case KA_OBJECTIVES:
        pClock->ViewObjectives();
        SetProcessed();
        return;

    case KA_POSE_LAY:
        pCommands->ChangePose(0);
        SetProcessed();
        return;
    case KA_POSE_CRAWL:
        pCommands->ChangePose(1);
        SetProcessed();
        return;
    case KA_POSE_WALK:
        pCommands->ChangePose(2);
        SetProcessed();
        return;
    case KA_POSE_RUN:
        pCommands->ChangePose(3);
        SetProcessed();
        return;

    case KA_SWARM:
        pCommands->Swarm();
        SetProcessed();
        return;

    case KA_FOLLOW:
        pSkills->ChangeSkill("follow");
        break;

        //	case KA_SKILL_STEAL:
        //		pSkills->ChangeSkill( "stealing" );
        //		break;
    case KA_SKILL_SCIENCE:
        pSkills->ChangeSkill("science");
        break;
        //	case KA_SKILL_TAME:
        //		pSkills->ChangeSkill( "tame" );
        //		break;

    case KA_SPELL1:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            pSpells->SelfCast(0);
        else
            pSpells->ChangeSpell(0);
        break;
    case KA_SPELL2:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            pSpells->SelfCast(1);
        else
            pSpells->ChangeSpell(1);
        break;
    case KA_SPELL3:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            pSpells->SelfCast(2);
        else
            pSpells->ChangeSpell(2);
        break;
    case KA_SPELL4:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            pSpells->SelfCast(3);
        else
            pSpells->ChangeSpell(3);
        break;
    case KA_SPELL5:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            pSpells->SelfCast(4);
        else
            pSpells->ChangeSpell(4);
        break;
    case KA_SPELL6:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            pSpells->SelfCast(5);
        else
            pSpells->ChangeSpell(5);
        break;
    case KA_SPELL7:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            pSpells->SelfCast(6);
        else
            pSpells->ChangeSpell(6);
        break;
    case KA_SPELL8:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            pSpells->SelfCast(7);
        else
            pSpells->ChangeSpell(7);
        break;

    case KA_WEAPON1:
        pWeapons->ChangeWeapon(0);
        break;
    case KA_WEAPON2:
        pWeapons->ChangeWeapon(1);
        break;
    case KA_WEAPON3:
        pWeapons->ChangeWeapon(2);
        break;
    case KA_WEAPON4:
        pWeapons->ChangeWeapon(3);
        break;

    case KA_QUICK1:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            pItems->SelfActivateQItem(0);
        else
            pItems->ActivateQItem(0);
        break;
    case KA_QUICK2:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            pItems->SelfActivateQItem(1);
        else
            pItems->ActivateQItem(1);
        break;
    case KA_QUICK3:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            pItems->SelfActivateQItem(2);
        else
            pItems->ActivateQItem(2);
        break;
    case KA_QUICK4:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            pItems->SelfActivateQItem(3);
        else
            pItems->ActivateQItem(3);
        break;

    case KA_INFO1:
        pInfo->ActivateMode(0);
        break;
    case KA_INFO2:
        pInfo->ActivateMode(1);
        break;
    case KA_INFO3:
        pInfo->ActivateMode(2);
        break;
    case KA_INFO4:
        pInfo->ActivateMode(3);
        break;
    case KA_TEXT1:
        pOutput->ActivateMode(0);
        break;
    case KA_TEXT2:
        pOutput->ActivateMode(1);
        break;
    case KA_MINIMAP:
        pMap->InvertClosedState();
        break;

    case KA_QSAVE:
        if (ModSpellAddonAllowsSave())
            CI3DScreenSaveLoad::QuickSave("saveq");
        break;
    case KA_QLOAD:
        CI3DScreenSaveLoad::QuickLoad("saveq");
        break;

    case KA_SELECT1:
        pI3D->BeginFX("buttons\\battle\\on_off.wav");
        SelectionUpdate(0);
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            CenterCamera(0);
        break;
    case KA_SELECT2:
        pI3D->BeginFX("buttons\\battle\\on_off.wav");
        SelectionUpdate(1);
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            CenterCamera(1);
        break;
    case KA_SELECT3:
        pI3D->BeginFX("buttons\\battle\\on_off.wav");
        SelectionUpdate(2);
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            CenterCamera(2);
        break;
        /*
                case KA_SELECT4:
                        pI3D->BeginFX( "buttons\\battle\\on_off.wav" );
                        SelectionUpdate(3);
                        if ( pI3D->IsCtrlDown() || pI3D->IsAltDown() )
                                CenterCamera(3);
                        break;
                case KA_SELECT5:
                        pI3D->BeginFX( "buttons\\battle\\on_off.wav" );
                        SelectionUpdate(4);
                        if ( pI3D->IsCtrlDown() || pI3D->IsAltDown() )
                                CenterCamera(4);
                        break;
                case KA_SELECT6:
                        pI3D->BeginFX( "buttons\\battle\\on_off.wav" );
                        SelectionUpdate(5);
                        if ( pI3D->IsCtrlDown() || pI3D->IsAltDown() )
                                CenterCamera(5);
                        break;
        */
    case KA_SELECTALL:
        {
            pI3D->BeginFX("buttons\\battle\\on_off.wav");
            for (int i = 0; i < pUnits.GetSize(); i++)
            {
                pFaces->SelectFace(i, TRUE);
                if (pUnits[i])
                    theWorld.SelectObject(pUnits[i]);
            }
            if (pUnits.GetSize() && pUnits[0])
                PlayAcknowlegment(pUnits[0]->GetID(), ACK_HERO_ACK_SELECTION, 0);
            UpdateAllObjects();
        }
        break;

        //	case KA_CANCEL_TARGET:
        //		SetInputMode( MODE_SIMPLE );
        //		break;

    case KA_CAMERA_TRACK: {
        for (int i = 0; i < pUnits.GetSize(); i++)
            if (pUnits[i]->CheckFlag(OBJ_SELECTED))
            {
                theCamera.SetCarrier(pUnits[i]);
                return;
            }
        theCamera.SetCarrier(DWORD(-1));
    }
    break;

        /*
case KA_CAMERA_CENTER:
        {
                for ( int i=0; i<pUnits.GetSize(); i++ )
                        if ( pUnits[i]->CheckFlag( OBJ_SELECTED ) )
                        {
                                CenterCamera( i );
                                break;
                        }
        }
        break;
        */
    case KA_CAMERA_NORM:
        pMap->NormalizeCamera();
        break;

    case KA_TUTORIAL_SCRIPT:
        pI3D->CheckMissionTutorials(true);
        SetProcessed();
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{

    CI3DScreen::OnKeyUp(nChar, nRepCnt, nFlags);

    switch (NKeyMap::keyMap[nFlags & 0x1ff])
    {
    case KA_CSHOT_HEAD:
        bHLButtonDown[0] = false;
        return;
    case KA_CSHOT_BODY:
        bHLButtonDown[1] = false;
        return;
    case KA_CSHOT_LHAND:
        bHLButtonDown[2] = false;
        return;
    case KA_CSHOT_RHAND:
        bHLButtonDown[3] = false;
        return;
    case KA_CSHOT_LLEG:
        bHLButtonDown[4] = false;
        return;
    case KA_CSHOT_RLEG:
        bHLButtonDown[5] = false;
        return;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScreenMission::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{

    pMap->SetProcessed(false);
    pMap->OnMouseWheel(nFlags, zDelta, pt);
    if (pMap->GetProcessed())
        return TRUE;
    pInfo->SetProcessed(false);
    pInfo->OnMouseWheel(nFlags, zDelta, pt);
    if (pInfo->GetProcessed())
        return TRUE;
    pScrollText->SetProcessed(false);
    pScrollText->OnMouseWheel(nFlags, zDelta, pt);
    if (pScrollText->GetProcessed())
        return TRUE;
    pOutput->SetProcessed(false);
    pOutput->OnMouseWheel(nFlags, zDelta, pt);
    if (pOutput->GetProcessed())
        return TRUE;

    pMap->Show(FALSE, FALSE);
    pInfo->Show(FALSE, FALSE);
    pScrollText->Show(FALSE, FALSE);
    pOutput->Show(FALSE, FALSE);

    CI3DScreen::OnMouseWheel(nFlags, zDelta, pt);

    pMap->Show(TRUE, FALSE);
    pInfo->Show(TRUE, FALSE);
    pScrollText->Show(TRUE, FALSE);
    pOutput->Show(TRUE, FALSE);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenMission::GetUnitName(CUnit *pUnit, CString &szOut)
{
    CString szTmp;
    CTextManager *pTxtMng = theWorld.GetTextManager();
    szTmp = pUnit->GetName();
    if (szTmp.IsEmpty()) // common unit
    {
        if (IsIDofPersonage(pUnit->GetID()))
        {
            auto it = localNames.find(pUnit->GetID());
            if (it != localNames.end())
                szOut = it->second;
            else
                szOut.Empty();
        }
        else
        {
            szOut.Empty();
            szTmp = pUnit->GetPrototype()->szName;
            NStrProc::ReplaceChars(szTmp, ' ', '_');
            if (pTxtMng->GetText(CString("unit ") + szTmp))
                pTxtMng->GetLine(szOut);
        }
    }
    else
        szOut = szTmp;
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DObjectWeapons::CI3DObjectWeapons(CInterface3D *pI, CI3DScreenMission *pScr)
    : CI3DObject(pI)
    , pScreen(pScr)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    SetRect(WideScreen::PlacedRect(80, 520, 240, 600));
    for (int i = 0; i < 4; i++)
    {
        CI3DPolyArea *pArea = AddArea();
        pArea->MakeRect(80 + i * 40, 520, 120 + i * 40, 600);
    }
    RemoveAllWeapons();
    pUnit = 0;

    const int nStr[] = { 15, 20, 10, 5, -1 };
    CTextManager *pTxtMng = theWorld.GetTextManager();
    CString szTmp;
    const int *pnStr = nStr;
    while (*pnStr != -1)
    {
        szTmp.Format("infoitem_%d", *pnStr);
        pTxtMng->GetSystemString(szTmp, szTmp);
        szStrings.Add(szTmp);
        pnStr++;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectWeapons::AddWeapon(LPCTSTR pszFigureName, LPCTSTR pszTextureName)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    int x, y;
    const bool hdInterface = UseModInterfaceHD();
    const int iconSize = hdInterface ? 512 : 128;
    const int canvasSize = hdInterface ? 1024 : 256;

    switch (nWeapons)
    {
    case 0:
        x = 0;
        y = 0;
        break;
    case 1:
        x = 0;
        y = iconSize;
        break;
    case 2:
        x = hdInterface ? iconSize : 42;
        y = 0;
        break;
    case 3:
        x = hdInterface ? iconSize : 42;
        y = iconSize;
        break;
    }

    OwnedRender::E3HudIcon(OwnedRender::E3HudLane::weapon,pszTextureName,canvasSize,
        iconSize,iconSize,hdInterface?iconSize:42,iconSize,x,y,hdInterface);

    CI3DFigure *pFig = AddFigure(pszFigureName);
    if (!pFig) {
        ReportError("Cannot create HUD weapon figure: %s", pszFigureName);
        throw std::runtime_error("HUD weapon figure missing");
    }

    if (pFig)
    {
        if (hdInterface) pFig->SetTexture(textures[0]);
        pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), -FP_PI / 20) *
                             CQuaternion(CPlot3D(0, 1, 0), -FP_PI / 3) *
                             CQuaternion(CPlot3D(0, 0, 1), FP_PI * 3 / 4));
    }

    pFig->SetUShift(x / float(canvasSize));
    pFig->SetVShift((iconSize + y) / float(canvasSize));
    pFig->SetDefaultColor(0xffffffff);
    pFig->UseDefaultColors(TRUE);

    nWeapons++;
    ShowSelectWeapon(nWeapons - 1, FALSE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectWeapons::RemoveAllWeapons()
{
    RemoveAllSurfaces();
    RemoveAllFigures();
    nWeapons = 0;
    iSelected = -1;
    for (int i = 0; i < 4; i++)
        GetArea(i)->SetToolTip(0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectWeapons::ShowSelectWeapon(int num, BOOL bSelect)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    CI3DFigure *pFig;
    if (num < 0 || num >= nWeapons)
        return;
    pFig = GetFigure(num);
    if (bSelect)
    {
        pFig->SetRelativeToScreen(100 + num * 40.f, 540, 12);
        pFig->SetScale(.65f);
        pFig->SetDefaultSpecular(0xff282828);
        areas[num]->MakeRect(80 + num * 40, 495, 120 + num * 40, 600);
    }
    else
    {
        float sc = 1;
        for (int i = 0; i < nWeapons; i++)
            sc *= 1.2f;
        pFig->SetRelativeToScreen(100 + num * 40.f, 560, 24 * sc);
        pFig->SetScale(sc);
        pFig->SetDefaultSpecular(0xff000000);
        areas[num]->MakeRect(80 + num * 40, 520, 120 + num * 40, 600);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectWeapons::SetUnit(CUnit *pNewUnit)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    OwnedRender::E3HudLanePublication hudLane(this,OwnedRender::E3HudLane::weapon);
    CString szName, szDesc, szOut;
    auto* mod=ModRuntime::Current();
    const bool nativeAssignment=mod && mod->AdvancedUI("useLS_SetUnitW");
    pUnit = pNewUnit;
    RemoveAllWeapons();
    if(pUnit && UseModInterfaceHD()){
        WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
        // Stable actual slot ownership; no native RemoveAt-in-loop leak or hole indexing.
        for(int slot=0;slot<QI_MAX_WEAPONS;++slot){int x=-45+45*slot;AddSurface(new CI3DSurface(pI3D,x,560,x+100,660));}
    }
    if (pUnit)
    {
        CItemWeapon *pWeapon;
        for (int j = 0; j < QI_MAX_WEAPONS; j++)
        {
            pWeapon = pUnit->GetQWeapon(j);
            if (pWeapon)
            {
                const CString model=nativeAssignment
                    ? ModHudAssignment::WeaponModel(*theItemDatabase.weapons[pWeapon->GetShape()])
                    : pWeapon->GetI3DFigureName();
                AddWeapon(model, pWeapon->GetI3DTextureName());
                if(nativeAssignment){
                    FormToolTipW(j,szName);GetArea(j)->SetToolTip(szName,1,KA_WEAPON1+j);continue;
                }
                theWorld.GetTextManager()->GetItemNameAndDesc(pWeapon, szName, szDesc, szOut);

                szName += "\n";
                float damages[RPG_NUM_ATTACK_TYPES];
                pWeapon->GetDamageDistribution(damages);
                int i = 0;
                for (; i < RPG_NUM_ATTACK_TYPES; i++)
                    if (damages[i] != 0)
                        break;
                szDesc = "Unknown";
                if (i < RPG_NUM_ATTACK_TYPES)
                {
                    szOut.Format("damage_%d", i);
                    theWorld.GetTextManager()->GetSystemString(szOut, szDesc);
                }
                szOut.Format("%s %d-%d %s\n", static_cast<LPCSTR>(szStrings[DAMAGE]), (int)pWeapon->GetDamageMin(),
                             (int)pWeapon->GetDamageMax(), static_cast<LPCSTR>(szDesc));
                szName += szOut;
                szOut.Format("%s %d  %s %d\n", static_cast<LPCSTR>(szStrings[ACTIONPTS]), pWeapon->GetActionPoints(),
                             static_cast<LPCSTR>(szStrings[RANGE]), (int)pWeapon->GetRange());
                szName += szOut;

                CSpell *pSpell = 0;
                if (pWeapon->GetSpellsCount())
                    pSpell = pWeapon->GetSpell(0);
                if (pSpell)
                {
                    szDesc = "Unknown";
                    if (theWorld.GetTextManager()->GetText(CString("spell ") +
                                                           pSpell->GetPrototype()->szCodeName))
                        theWorld.GetTextManager()->GetLine(szDesc);
                    szOut.Format("%s %s\n", static_cast<LPCSTR>(szStrings[SPELL]), static_cast<LPCSTR>(szDesc));
                    szName += szOut;
                }

                GetArea(j)->SetToolTip(szName, 1, KA_WEAPON1 + j);
            }
        }
        iSelected = pUnit->GetCurrentWeaponIndex(); // current slot
        if (iSelected >= nWeapons)
            iSelected = -1;
        ShowSelectWeapon(iSelected, TRUE);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObjectWeapons::Draw(CRenderer *pRend)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    DWORD dwAlpha;
    DWORD dwAlphaEnable;
    pI3D->GetRenderer()->GetRenderState(D3DRENDERSTATE_ALPHAREF, &dwAlpha);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHAREF, 1);
    pI3D->GetRenderer()->GetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, &dwAlphaEnable);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, TRUE);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, TRUE);

    CI3DObject::Draw(pRend);

    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHAREF, dwAlpha);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, dwAlphaEnable);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, FALSE);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectWeapons::FormToolTipW(int slot,CString& text)
{
    auto* mod=ModRuntime::Current();auto* weapon=pUnit?pUnit->GetQWeapon(slot):nullptr;
    text.Empty();if(!mod||!weapon)return;
    auto* tm=theWorld.GetTextManager();CString description,tmp;
    ModIdleTooltips::WeaponInput in;tm->GetItemNameAndDesc(weapon,in.name,description,tmp);
    in.damage=szStrings[DAMAGE];in.actionsLabel=szStrings[ACTIONPTS];in.rangeLabel=szStrings[RANGE];in.spellLabel=szStrings[SPELL];
    auto label=[&](const char* key){CString result=key;if(tm->GetText(CString("string ")+key))tm->GetLine(result);return result;};
    in.blockPowerLabel=label("infoitem_46");in.blockLabel=label("infoitem_45");in.parryLabel=label("infoitem_47");
    in.hpLabel=label("infoitem_3");in.effectLabel=label("infoitem_8");in.manaLabel=label("infoitem_2");
    in.shield=weapon->GetWeaponType()==RPGWST_HANDS1;in.broken=weapon->IsBroken();
    in.minimum=weapon->GetDamageMin();in.maximum=weapon->GetDamageMax();in.actions=int(weapon->GetActionPoints());in.range=weapon->GetRange();
    float distribution[RPG_NUM_ATTACK_TYPES];weapon->GetDamageDistribution(distribution);
    for(int type=0;type<RPG_NUM_ATTACK_TYPES;++type)if(distribution[type]!=0){tmp.Format("damage_%d",type);tm->GetSystemString(tmp,in.damageType);break;}
    if(in.shield)mod->IdleShieldValues(*pUnit,in.blockPower,in.block,in.parry);
    CItemWeapon* durability=weapon;
    if(!in.broken&&theWorldServer.GetMapObjectManager())if(auto* server=theWorldServer.GetUnit(pUnit->GetID()))if(auto* current=server->GetQWeapon(slot))durability=current;
    in.hp=durability->GetHP();in.maxHP=durability->GetMaxHP();in.mp=weapon->GetMP();in.maxMP=weapon->GetMaxMP();
    in.spell=weapon->GetSpellsCount()?weapon->GetSpell(0):nullptr;in.stats=pUnit->GetUserUnitStats();in.difficulty=theSettings.nDifficultyLevel;
    if(in.spell){
        if(tm->GetText(CString("spell ")+in.spell->GetPrototype()->szCodeName))tm->GetLine(in.spellName);
        tmp.Format("damage_%d",ModIdleTooltips::DamageType(*in.spell));in.spellDamageType=label(tmp);
    }
    text=ModIdleTooltips::Weapon(in);
    if(in.spell&&slot>=0&&slot<surfaces.GetSize()){
        auto* surface=surfaces[slot];surface->GetDC();surface->Clear();
        if(in.spell){tmp.Format("%d",int(in.mp));surface->DrawText(tmp,CRect(0,10,185,25),0,RGB(228,215,167),DT_LEFT);}
        surface->ReleaseDC();
    }
}
void CI3DObjectWeapons::OnIdle()
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    if (pUnit && iSelected != pUnit->GetCurrentWeaponIndex())
    {
        ShowSelectWeapon(iSelected, FALSE);
        iSelected = pUnit->GetCurrentWeaponIndex(); // current slot
        ShowSelectWeapon(iSelected, TRUE);
    }
    if(auto* mod=ModRuntime::Current();pUnit&&mod&&mod->AdvancedUI("useLS_ObjectWeaponsOnIdle")){
        CString text;
        for(int slot=0;slot<QI_MAX_WEAPONS;++slot)if(pUnit->GetQWeapon(slot)){
            FormToolTipW(slot,text);GetArea(slot)->UpdateToolTip(text);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectWeapons::ChangeWeapon(int nSlot)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    if (nSlot >= 0 && nSlot < nWeapons)
        if (iSelected != nSlot)
        {
            if (iSelected >= 0 && iSelected < nWeapons)
                ShowSelectWeapon(iSelected, FALSE);
            ShowSelectWeapon(nSlot, TRUE);
            iSelected = nSlot;
            pI3D->BeginFX("buttons\\battle\\weapon.wav");

            // notify game
            if (pUnit)
                theWorld.GetPlayer()->UnitCommandSelWeapon(pUnit, nSlot);
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectWeapons::OnLButtonDown(UINT nFlags, CPoint point)
{
    int i = HitTest(point);
    JournalHit(i);
    if (i >= 0 && i < nWeapons)
    {
        SetProcessed();
        pI3D->SetCapture(this);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectWeapons::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (pI3D->GetCapture() != this)
        return;
    pI3D->ReleaseCapture();
    int i = HitTest(point);
    JournalHit(i);
    if (i >= 0 && i < nWeapons)
    {
        SetProcessed();
        ChangeWeapon(i);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectWeapons::OnMouseMove(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    if (ht >= 0 && ht < nWeapons)
        SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DObjectItems::CI3DObjectItems(CInterface3D *pI, CI3DScreenMission *pScr)
    : CI3DObject(pI)
    , pScreen(pScr)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomRight);
    SetRect(WideScreen::PlacedRect(520, 500, 720, 600));
    for (int i = 0; i < 4; i++)
    {
        AddArea()->MakeRect(670 - i * 50, 550, 720 - i * 50, 600);
    }
    for (int i = 0; i < 4; i++)
    {
        AddArea()->MakeRect(670 - i * 50, 500, 720 - i * 50, 550);
    }
    RemoveAllItems();
    pUnit = 0;

    const int nStr[] = { 7, 8, 9, 10, 11, 12, 13, 21, 22, 26, 27, 28, -1 };
    CTextManager *pTxtMng = theWorld.GetTextManager();
    CString szTmp;
    const int *pnStr = nStr;
    while (*pnStr != -1)
    {
        szTmp.Format("infoitem_%d", *pnStr);
        pTxtMng->GetSystemString(szTmp, szTmp);
        szStrings.Add(szTmp);
        pnStr++;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItems::AddItem(LPCTSTR pszFigureName, LPCTSTR pszTextureName)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomRight);
    int x, y;
    const bool hdInterface = UseModInterfaceHD();
    const int iconSize = hdInterface ? 512 : 64;
    const int canvasSize = hdInterface ? 2048 : 256;

    if (nItems > 7)
        return;

    switch (nItems)
    {
    case 0:
        x = 128;
        y = 0;
        break;
    case 1:
        x = 128;
        y = 64;
        break;
    case 2:
        x = 128;
        y = 128;
        break;
    case 3:
        x = 128;
        y = 192;
        break;
    case 4:
        x = 192;
        y = 0;
        break;
    case 5:
        x = 192;
        y = 64;
        break;
    case 6:
        x = 192;
        y = 128;
        break;
    case 7:
        x = 192;
        y = 192;
        break;
    }

    if (hdInterface)
    {
        x = (nItems % 4) * iconSize;
        y = (2 + nItems / 4) * iconSize;
    }
    OwnedRender::E3HudIcon(OwnedRender::E3HudLane::item,pszTextureName,canvasSize,
        iconSize,iconSize,iconSize,iconSize,x,y,hdInterface);

    CI3DFigure *pFig = AddFigure(pszFigureName);
    if (!pFig) {
        ReportError("Cannot create HUD item figure: %s", pszFigureName);
        throw std::runtime_error("HUD item figure missing");
    }

    if (hdInterface && pFig) pFig->SetTexture(textures[0]);
    if (pFig)
        pFig->SetOrientation(CQuaternion(CPlot3D(1, 0, 0), FP_PI));

    pFig->SetUShift(x / float(canvasSize));
    pFig->SetVShift((iconSize + y) / float(canvasSize));
    pFig->SetDefaultColor(0xffffffff);
    pFig->UseDefaultColors(TRUE);

    nItems++;
    //	ShowSelectItem( nItems-1, FALSE );
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItems::RemoveAllItems()
{
    RemoveAllFigures();
    nItems = 0;
    iSelected = -1;
    for (int i = 0; i < 8; i++)
    {
        GetArea(i)->SetToolTip(0);
        nSubst[i] = -1;
    }
    items.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItems::ShowSelectItem(int num, BOOL bSelect)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomRight);
    if (nSubst[num] < 0 || nSubst[num] >= nItems)
        return;
    CI3DFigure *pFig;
    pFig = GetFigure(nSubst[num]);
    if (num >= 4)
        pFig->SetScale(bSelect ? 1.f : .85f);
    else
        pFig->SetScale(bSelect ? .65f : .55f);
    if (bSelect)
    {
        pFig->SetDefaultSpecular(0xff404040);
        iSelected = num;
    }
    else
        pFig->SetDefaultSpecular(0xff000000);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObjectItems::Draw(CRenderer *pRend)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomRight);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, TRUE);
    CI3DObject::Draw(pRend);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, FALSE);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItems::PlaceItem(int num, int pos)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomRight);
    CI3DFigure *pFig;
    pFig = GetFigure(num);
    if (pos < 4)
        pFig->SetRelativeToScreen(695.f - pos * 50, 575, 20);
    else
        pFig->SetRelativeToScreen(695.f - (pos - 4) * 50, 525, 20);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItems::DeselectItems()
{
    for (int i = 0; i < 8; i++)
        ShowSelectItem(i, FALSE);
    iSelected = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItems::FormToolTip(int nQItem, CString &szName)
{
    const auto *mod = ModRuntime::Current();
    const bool improveInfoWindows = mod && mod->CombatSetting("improveInfoWindows");
    CItemQuick *pQuick = pUnit->GetQItem(nQItem);
    if (!pQuick)
        return;

    CString szOut;

    szName.Empty();
    theWorld.GetTextManager()->GetItemNameAndDesc(pQuick, szName, szOut, szOut);
    CSpell *pSpell = pQuick->GetSpell(0);
    if(auto* current=ModRuntime::Current();current&&current->AdvancedUI("useLS_ObjectItemsOnIdle")){
        using namespace ModIdleTooltips;
        const CString labels[QUICK_LABELS]={szStrings[CI3DObjectItems::MANA],szStrings[CI3DObjectItems::EFFECT],
            szStrings[CI3DObjectItems::SPEED],szStrings[CI3DObjectItems::RANGE],szStrings[CI3DObjectItems::AREA],
            szStrings[CI3DObjectItems::DURATION],szStrings[CI3DObjectItems::TARGETS],szStrings[CI3DObjectItems::METERS],
            szStrings[CI3DObjectItems::SECONDS],szStrings[CI3DObjectItems::INSTANT],szStrings[CI3DObjectItems::AREAUNIT],szStrings[CI3DObjectItems::FIXEDEFF]};
        const int type=pQuick->GetQuickItemType();
        const float additional=pSpell&&(type==IDB_WAND||type==IDB_SCROLL)?current->InfoProperty("GetSpellAdds",pSpell->GetPrototype()->szCodeName,2):0;
        szName=Quick(szName,type,pSpell,*pUnit->GetUserUnitStats(),pQuick->GetMP(),pQuick->GetMaxMP(),additional,labels);return;
    }
    if (pSpell && pQuick->GetQuickItemType() == IDB_WAND)
    {
        szName += " \n";
        szOut.Format("%s %d/%d (%d)\n", static_cast<LPCSTR>(szStrings[MANA]), (int)pSpell->GetManaCost(),
                     (int)pQuick->GetMaxMP(), (int)pQuick->GetMP());
        szName += szOut;

        if (!pSpell->GetPrototype()->bRuneAllowed[SDBAR_EFFECT])
            szOut.Format("%s %s  ", static_cast<LPCSTR>(szStrings[EFFECT]), static_cast<LPCSTR>(szStrings[FIXEDEFF]));
        else
            if (improveInfoWindows)
                szOut.Format("%s %.1f  ", static_cast<LPCSTR>(szStrings[EFFECT]), pSpell->GetPower());
            else
                szOut.Format("%s %d  ", static_cast<LPCSTR>(szStrings[EFFECT]), (int)pSpell->GetPower());
        szName += szOut;

        if (pSpell->GetSpeed() < 1.f)
            szOut.Format("%s %s \n", static_cast<LPCSTR>(szStrings[SPEED]), static_cast<LPCSTR>(szStrings[INSTANT]));
        else
            szOut.Format("%s %d \n", static_cast<LPCSTR>(szStrings[SPEED]), (int)pSpell->GetSpeed());
        szName += szOut;

        szOut.Format("%s %d%s  ", static_cast<LPCSTR>(szStrings[RANGE]), (int)pSpell->GetRange(), static_cast<LPCSTR>(szStrings[METERS]));
        szName += szOut;

        if (pSpell->IsOnUnit())
            szOut.Format("%s %s \n", static_cast<LPCSTR>(szStrings[AREA]), static_cast<LPCSTR>(szStrings[AREAUNIT]));
        else
            szOut.Format("%s %.1f%s \n", static_cast<LPCSTR>(szStrings[AREA]), pSpell->GetArea(), static_cast<LPCSTR>(szStrings[METERS]));
        szName += szOut;

        if (pSpell->GetDuration() < 2)
            szOut.Format("%s %s  ", static_cast<LPCSTR>(szStrings[DURATION]), static_cast<LPCSTR>(szStrings[INSTANT]));
        else
            szOut.Format("%s %.1f%s  ", static_cast<LPCSTR>(szStrings[DURATION]), pSpell->GetDuration() / 15.f,
                         static_cast<LPCSTR>(szStrings[SECONDS]));
        szName += szOut;

        szOut.Format("%s %d \n", static_cast<LPCSTR>(szStrings[TARGETS]), pSpell->GetNumTargets());
        szName += szOut;
    }

    if (pSpell && pQuick->GetQuickItemType() == IDB_SCROLL)
    {
        szName += " \n";
        if (!pSpell->GetPrototype()->bRuneAllowed[SDBAR_EFFECT])
            szOut.Format("%s %s\n", static_cast<LPCSTR>(szStrings[EFFECT]), static_cast<LPCSTR>(szStrings[FIXEDEFF]));
        else
            if (improveInfoWindows)
                szOut.Format("%s %.1f\n", static_cast<LPCSTR>(szStrings[EFFECT]), pSpell->GetPower());
            else
                szOut.Format("%s %d\n", static_cast<LPCSTR>(szStrings[EFFECT]), (int)pSpell->GetPower());
        szName += szOut;

        if (pSpell->GetDuration() < 2)
            szOut.Format("%s %s\n", static_cast<LPCSTR>(szStrings[DURATION]), static_cast<LPCSTR>(szStrings[INSTANT]));
        else
            szOut.Format("%s %.1f%s\n", static_cast<LPCSTR>(szStrings[DURATION]), pSpell->GetDuration() / 15.f,
                         static_cast<LPCSTR>(szStrings[SECONDS]));
        szName += szOut;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItems::SetUnit(CUnit *pNewUnit)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomRight);
    OwnedRender::E3HudLanePublication hudLane(this,OwnedRender::E3HudLane::item);
    CString szName, szDesc;
    pUnit = pNewUnit;
    RemoveAllItems();
    if (pUnit)
    {
        CItemQuick *pQuick;
        for (int j = 0; j < QI_MAX_ITEMS; j++)
        {
            pQuick = pUnit->GetQItem(j);
            if (pQuick)
            {
                AddItem(pQuick->GetI3DFigureName(), pQuick->GetI3DTextureName());
                items.SetSize(nItems);
                items[nItems - 1].bQuick = true;
                items[nItems - 1].nID = pQuick->GetID();
                items[nItems - 1].pItem = pQuick;
                FormToolTip(j, szName);
                GetArea(j)->SetToolTip(szName, 1, KA_QUICK1 + j);
                PlaceItem(nItems - 1, j);
                nSubst[j] = nItems - 1;
                ShowSelectItem(j, FALSE);
            }
        }
        if (IWorldClient.GetPlayer()->GetPlayer()->IsMainHero(pUnit))
        {
            int nQuest = 0;
            CItem *pItem;
            CList<CItem *, CItem *> *pItems = &IWorldClient.GetPlayer()->GetPlayer()->items;

            POSITION pos = pItems->GetTailPosition();
            while (pos && nQuest < 4)
            {
                pItem = pItems->GetPrev(pos);
                if (pItem && pItem->GetTypeID() == ITID_ITEM_QUEST)
                {
                    CItemQuest *pQuest = static_cast<CItemQuest *>(pItem);
                    AddItem(pQuest->GetI3DFigureName(), pQuest->GetI3DTextureName());
                    items.SetSize(nItems);
                    items[nItems - 1].bQuick = false;
                    items[nItems - 1].nID = pQuest->GetID();
                    items[nItems - 1].pItem = pQuest;
                    theWorld.GetTextManager()->GetItemNameAndDesc(pQuest, szName, szDesc, szDesc);
                    GetArea(nQuest + 4)->SetToolTip(szName, 1);
                    PlaceItem(nItems - 1, nQuest + 4);
                    nSubst[nQuest + 4] = nItems - 1;
                    ShowSelectItem(nQuest + 4, FALSE);
                    nQuest++;
                }
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItems::OnIdle()
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomRight);
    // const float f00R = 0, f00G = 1, f00B = 0,
    //            f05R = 1, f05G = 1, f05B = 0,
    //            f10R = 1, f10G = 0, f10B = 0;

    CString szTip;
    CItemQuick *pQuick;
    if (!pUnit)
        return;

    for (int j = 0; j < QI_MAX_ITEMS; j++)
    {
        pQuick = pUnit->GetQItem(j);
        if (pQuick && GetArea(j)->IsToolTipActive())
        {
            FormToolTip(j, szTip);
            GetArea(j)->UpdateToolTip(szTip);
        }
        /*
        if ( pQuick->GetTTIndex() == 2 ) // crap sensor of danger :)
        {
                float fR, fG, fB;
                SUnitVisibilityInfo &senses = pUnit->sensesDetection;
                float minSence = AI_MAX_VISIBILITY_INFO_MULT;
                minSence = Min( minSence, senses.fpTracking );
                minSence = Min( minSence, senses.fpSound );
                minSence = Min( minSence, senses.fpSmell );
                minSence = Min( minSence, senses.fpSight );
                float val = 1 - ( minSence / AI_MAX_VISIBILITY_INFO_MULT );
                if ( val < .5f )
                {
                        val *= 2;
                        fR = f00R * (1-val) + f05R * val;
                        fG = f00G * (1-val) + f05G * val;
                        fB = f00B * (1-val) + f05B * val;
                }
                else
                {
                        val = (val - .5f) * 2;
                        fR = f05R * (1-val) + f10R * val;
                        fG = f05G * (1-val) + f10G * val;
                        fB = f05B * (1-val) + f10B * val;
                }
                //if ( j < figures.GetSize() && GetFigure(j) )
                //	GetFigure(j)->SetAmbient( fR, fG, fB );
        }
        */
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItems::OnLButtonDown(UINT nFlags, CPoint point)
{
    if(!OwnedRender::E3HudItemEventInside(this,point))return;
    int ht = HitTest(point);
    JournalHit(ht);
    if (ht >= 0 && ht < 8 && nSubst[ht] >= 0)
    {
        SetProcessed();
        pI3D->SetCapture(this);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItems::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    if(!OwnedRender::E3HudItemEventInside(this,point))return;
    int ht = HitTest(point);
    JournalHit(ht);
    if (ht >= 0 && ht < 8 && nSubst[ht] >= 0)
    {
        SetProcessed();
        pI3D->SetCapture(this);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItems::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (pI3D->GetCapture() != this)
        return;
    pI3D->ReleaseCapture();
    if(!OwnedRender::E3HudItemEventInside(this,point))return;
    int i = HitTest(point);
    JournalHit(i);
    if (i >= 0 && i < 8 && nSubst[i] >= 0)
    {
        int j = nSubst[i];
        SetProcessed();
        if (i < 4 && items[j].bQuick)
        {
            if (pI3D->IsLDblClk())
                SelfActivateQItem(i);
            else
                ActivateQItem(i);
        }
        else if (iSelected != i)
        {
            pI3D->BeginFX("buttons\\battle\\click.wav");
            pScreen->tool = items[j].pItem->GetTool();
            pScreen->SetInputMode(MODE_QUEST_TARGET_OBJECT, i, items[j].nID);
        }
        else
        {
            pI3D->BeginFX("buttons\\battle\\click.wav");
            pScreen->SetInputMode(MODE_SIMPLE);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItems::ActivateQItem(int nItem)
{
    if (!pUnit)
        return;
    int i = nItem;
    CItemQuick *pQuick = pUnit->GetQItem(i);
    if (!pQuick)
    {
        pScreen->SetInputMode(MODE_SIMPLE);
        return;
    }
    pI3D->BeginFX("buttons\\battle\\click.wav");
    if (iSelected != i)
    {
        switch (pQuick->GetQuickItemType())
        {
            /*
                            case IDB_TRAP:
                                    for ( j=0; j<pScreen->GetNumUnits(); j++ )
                                    {
                                            CUnit *pU = pScreen->GetUnit(j);
                                            if ( pU && pU->CheckFlag( OBJ_SELECTED ) )
                                                    theWorld.GetPlayer()->UnitCommandTrap( pU, i );
                                    }
                                    break;

                            case IDB_PICKLOCK:
                            case IDB_CROWBAR:
                                    if ( pQuick->IsTool() )
                                            pScreen->SetInputMode( MODE_ITEM_TARGET_OBJECT, i,
               items[i].nID ); break;
            */
        case IDB_SCROLL:
        case IDB_WAND: {
            int nMode = MODE_SIMPLE;
            if (pQuick->GetSpellsCount() > 0)
                nMode = pQuick->GetSpell(0)->IsOnUnit() ? MODE_ITEM_TARGET_UNIT
                                                        : MODE_ITEM_TARGET_TILE;
            pScreen->SetInputMode(nMode, i);
        }
        break;
        }
    }
    else
    {
        pScreen->SetInputMode(MODE_SIMPLE);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItems::SelfActivateQItem(int nItem)
{
    if (!pUnit)
        return;
    int i = nItem;
    CItemQuick *pQuick = pUnit->GetQItem(i);
    if (!pQuick)
        return;
    pScreen->SetInputMode(MODE_ITEM_TARGET_UNIT, i);
    switch (pQuick->GetQuickItemType())
    {
    case IDB_SCROLL:
    case IDB_WAND:
        if (pQuick->GetSpellsCount() > 0)
        {
            CUnit *pTarget = 0;
            if (pQuick->GetSpell(0)->IsOffensive())
            {
                pTarget = theWorld.GetNearestEnemy(pUnit);
                if (!pTarget)
                {
                    // play refuse - no enemies around
                    pI3D->BeginFX("buttons\\battle\\nomagic.wav");
                    pScreen->SetInputMode(MODE_SIMPLE);
                    return;
                }
            }
            else
                pTarget = pUnit;
            if (!pTarget)
                break;
            if (pQuick->GetSpell(0)->IsOnUnit())
                pScreen->CastSpell(pTarget, true);
            else
            {
                CPlot3D plot = pTarget->GetCP();
                pScreen->CastSpell(plot, pUnit, true);
            }
        }
        break;
    }
    pI3D->BeginFX("buttons\\battle\\click.wav");
    pScreen->SetInputMode(MODE_SIMPLE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItems::OnMouseMove(UINT nFlags, CPoint point)
{
    if(!OwnedRender::E3HudItemEventInside(this,point))return;
    int ht = HitTest(point);
    if (ht >= 0 && ht < 8 && nSubst[ht] >= 0)
        SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DObjectSpells::CI3DObjectSpells(CInterface3D *pI, CI3DScreenMission *pScr)
    : CI3DObject(pI)
    , pScreen(pScr)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomRight);
    SetRect(WideScreen::PlacedRect(720, 190, 800, 510));
    RemoveAllSpells();
    pUnit = 0;

    const int nStr[] = { 7, 18, 8, 9, 10, 11, 12, 13, 21, 22, 26, 27, 28, -1 };
    CTextManager *pTxtMng = theWorld.GetTextManager();
    CString szTmp;
    const int *pnStr = nStr;
    while (*pnStr != -1)
    {
        szTmp.Format("infoitem_%d", *pnStr);
        pTxtMng->GetSystemString(szTmp, szTmp);
        szStrings.Add(szTmp);
        pnStr++;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSpells::AddSpell(LPCTSTR pszFigureName, LPCTSTR pszTextureName)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomRight);
    auto* mod=ModRuntime::Current();
    const bool extended=!mod || mod->CombatSetting("JMVExtendedSpellSlots");
    const int newCount = nSpells + 1;
    const bool hdInterface = UseModInterfaceHD();
    for (int i = 0; i < areas.GetSize(); i++)
    {
        const auto p=ModHudAssignment::SpellPlacement(i,newCount,extended,hdInterface);
        GetArea(i)->MakeRect(p.left,p.top,p.left+40,p.top+40);
    }
    for (int i = 0; i < figures.GetSize(); i++)
    {
        const auto p=ModHudAssignment::SpellPlacement(i,newCount,extended,hdInterface);
        figures[i]->SetRelativeToScreen(p.left+20,p.top+20,40);
    }
    const auto p=ModHudAssignment::SpellPlacement(nSpells,newCount,extended,hdInterface);
    AddArea()->MakeRect(p.left,p.top,p.left+40,p.top+40);
    const int iconSize=p.size,canvasSize=p.canvas;
    const int x=mod ? p.x : 128+((nSpells%8)/4)*64,y=p.y;
    OwnedRender::E3HudIcon(OwnedRender::E3HudLane::spell,pszTextureName,canvasSize,
        iconSize,iconSize,iconSize,iconSize,x,y,hdInterface);

    CI3DFigure *pFig = AddFigure(pszFigureName);
    if (hdInterface || (mod && extended)) pFig->SetTexture(textures[0]);
    pFig->SetUShift(x / float(canvasSize));
    pFig->SetVShift((iconSize + y) / float(canvasSize));
    pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), FP_PI));
    pFig->SetRelativeToScreen(p.left+20,p.top+20,40);
    pFig->SetDefaultColor(0xffffffff);
    pFig->UseDefaultColors(TRUE);

    nSpells++;
    if(!mod) ShowSelectSpell(nSpells - 1, FALSE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSpells::RemoveAllSpells()
{
    RemoveAllFigures();
    RemoveAllAreas();
    nSpells = 0;
    iSelected = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSpells::ShowSelectSpell(int num, BOOL bSelect)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomRight);
    CI3DFigure *pFig;
    pFig = GetFigure(num);
    pFig->SetScale(bSelect ? 1.15f : 1.f);
    if (bSelect)
        pFig->SetDefaultSpecular(0xff404040);
    else
        pFig->SetDefaultSpecular(0xff000000);
    iSelected = num;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSpells::DeselectSpells()
{
    for (int i = 0; i < nSpells; i++)
        ShowSelectSpell(i, FALSE);
    iSelected = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSpells::SetUnit(CUnit *pNewUnit)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomRight);
    OwnedRender::E3HudLanePublication hudLane(this,OwnedRender::E3HudLane::spell);
    auto *mod = ModRuntime::Current();
    const bool improveInfoWindows = mod && mod->CombatSetting("improveInfoWindows");
    const int maxSpellSlots=mod ? ModHudAssignment::SpellSlots(mod->CombatSetting("JMVExtendedSpellSlots")) : QI_MAX_SPELLS;
    auto* tooltipMod=mod && mod->AdvancedUI("useLS_SetUnitS")?mod:nullptr;
    CString szName, szDesc;
    pUnit = pNewUnit;
    RemoveAllSpells();
    if (pUnit)
    {
        for (int j = 0; j < maxSpellSlots; j++)
        {
            CItemSpell *pItemSpell = pUnit->GetQSpellItem(j);
            if (pItemSpell)
            {
                AddSpell(pItemSpell->GetI3DFigureName(), pItemSpell->GetI3DTextureName());
                theWorld.GetTextManager()->GetItemNameAndDesc(pItemSpell, szName, szDesc, szDesc);
                CSpell *pSpell = pItemSpell->GetSpell();
                if (pSpell)
                {
                    CString szOut;
                    if(tooltipMod) ModHudAssignment::AppendSpellHotkey(szName,j);
                    szName += " \n";
                    ModSkills::Config skillConfig;
                    if(tooltipMod && tooltipMod->SkillConfig(pSpell,skillConfig)) {
                        const char* requiredSkill=skillConfig.requiredArchery>=0.f
                            ? "\xD1\xF2\xF0\xE5\xEB\xFC\xE1\xE0"
                            : "\xC1\xEB\xE8\xE6\xED\xE8\xE9 \xE1\xEE\xE9";
                        szOut.Format("\xC7\xE0\xEF\xE0\xF1 \xF1\xE8\xEB: %.0f  \n"
                            "%s: %.0f  \n\xD3\xF0\xEE\xED: %.0f%%  \n"
                            "\xCF\xE5\xF0\xE5\xE7\xE0\xF0\xFF\xE4\xEA\xE0: %.0f \xF1\xE5\xEA.  ",
                            skillConfig.Cost(pSpell->GetManaCost()),requiredSkill,
                            skillConfig.Requirement(float(pSpell->GetComplexity())),
                            skillConfig.damageMultiplier*100.f,skillConfig.cooldownSeconds);
                        szName+=szOut;
                        if(skillConfig.areaRadius>0.f) {
                            szOut.Format("  \n\xD0\xE0\xE4\xE8\xF3\xF1: %.0f \xEC.  ",skillConfig.areaRadius);
                            szName+=szOut;
                        }
                        GetArea(j)->SetToolTip(szName,1,j<8?KA_SPELL1+j:KA_UNKNOWN);
                        continue;
                    }
                    float durationTicks=float(pSpell->GetDuration());
                    ModSpellTooltip::Values tooltip = {pSpell->GetManaCost(), pSpell->GetPower(), 0};
                    if (tooltipMod) {
                        CUnit *serverUnit = theWorldServer.GetUnit(pUnit->GetID());
                        const float manaIntelligence = serverUnit
                            ? serverUnit->GetUserUnitStats()->cIntelligence : pUnit->GetUserUnitStats()->cIntelligence;
                        tooltip = tooltipMod->SpellTooltip(*pUnit->GetUserUnitStats(), *pSpell,
                            manaIntelligence, theSettings.nDifficultyLevel);
                        tooltipMod->SpellTooltipOverrides(serverUnit,tooltip,durationTicks);
                        static unsigned tooltipNotes = 0;
                        if (tooltipNotes++ < 16) EngineDiagnostics::Event(
                            "SPELL_TOOLTIP R159 unit=%d slot=%d spell=%s mana=%g effect=%g castSeconds=%g",
                            pUnit->GetID(), j, (LPCSTR)pSpell->GetPrototype()->szCodeName,
                            double(tooltip.mana), double(tooltip.effect), double(tooltip.castSeconds));
                    }
                    szOut.Format("%s %d  ", static_cast<LPCSTR>(szStrings[MANA]), (int)tooltip.mana);
                    szName += szOut;
                    szOut = "Unknown";
                    szDesc.Format("school_%d", mod ? pSpell->GetMagicSubtype() : pSpell->GetMagicSchool());
                    theWorld.GetTextManager()->GetSystemString(szDesc, szOut);

                    szOut = szStrings[MAGICSCHOOL] + " " + szOut + " \n";
                    szName += szOut;

                    if (tooltipMod) {
                        if (!pSpell->GetPrototype()->bRuneAllowed[SDBAR_EFFECT])
                            szOut.Format("%s %.1f", static_cast<LPCSTR>(szStrings[EFFECT]), tooltip.effect);
                        else
                            szOut.Format("%s %.0f  ", static_cast<LPCSTR>(szStrings[EFFECT]), tooltip.effect);
                    } else if (!pSpell->GetPrototype()->bRuneAllowed[SDBAR_EFFECT])
                        szOut.Format("%s %s  ", static_cast<LPCSTR>(szStrings[EFFECT]), static_cast<LPCSTR>(szStrings[FIXEDEFF]));
                    else if (improveInfoWindows)
                        szOut.Format("%s %.1f  ", static_cast<LPCSTR>(szStrings[EFFECT]), pSpell->GetPower());
                    else
                        szOut.Format("%s %d  ", static_cast<LPCSTR>(szStrings[EFFECT]), (int)pSpell->GetPower());
                    szName += szOut;

                    if (mod ? pSpell->GetActionPoints() < 1 : pSpell->GetSpeed() < 1.f)
                        szOut.Format("%s %s \n", static_cast<LPCSTR>(szStrings[SPEED]), static_cast<LPCSTR>(szStrings[INSTANT]));
                    else if (mod)
                        szOut.Format("%s %.1f%s\n", static_cast<LPCSTR>(szStrings[SPEED]), tooltip.castSeconds, static_cast<LPCSTR>(szStrings[SECONDS]));
                    else
                        szOut.Format("%s %d \n", static_cast<LPCSTR>(szStrings[SPEED]), (int)pSpell->GetSpeed());
                    szName += szOut;

                    szOut.Format("%s %d%s  ", static_cast<LPCSTR>(szStrings[RANGE]), (int)pSpell->GetRange(),
                                 static_cast<LPCSTR>(szStrings[METERS]));
                    szName += szOut;

                    if (pSpell->IsOnUnit())
                        szOut.Format("%s %s \n", static_cast<LPCSTR>(szStrings[AREA]), static_cast<LPCSTR>(szStrings[AREAUNIT]));
                    else
                        szOut.Format("%s %.1f%s \n", static_cast<LPCSTR>(szStrings[AREA]), mod ? pSpell->GetRadius() : pSpell->GetArea(),
                                     static_cast<LPCSTR>(szStrings[METERS]));
                    szName += szOut;

                    if (durationTicks < 2)
                        szOut.Format("%s %s  ", static_cast<LPCSTR>(szStrings[DURATION]), static_cast<LPCSTR>(szStrings[INSTANT]));
                    else
                        szOut.Format("%s %.1f%s  ", static_cast<LPCSTR>(szStrings[DURATION]),
                                     durationTicks / 15.f, static_cast<LPCSTR>(szStrings[SECONDS]));
                    szName += szOut;

                    szOut.Format("%s %d \n", static_cast<LPCSTR>(szStrings[TARGETS]), pSpell->GetNumTargets());
                    szName += szOut;
                }
                GetArea(j)->SetToolTip(szName, 1, j < 8 ? KA_SPELL1 + j : KA_UNKNOWN);
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSpells::ChangeSpell(int nSlot)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomRight);
    static unsigned selectionNotes = 0;
    if (selectionNotes++ < 64) EngineDiagnostics::Event("Spell select unit=%d slot=%d count=%d previous=%d", pUnit ? pUnit->GetID() : -1, nSlot, nSpells, iSelected);
    if (nSlot >= 0 && nSlot < nSpells)
        if (iSelected != nSlot)
        {
            if (iSelected >= 0 && iSelected < nSpells)
                ShowSelectSpell(iSelected, FALSE);
            ShowSelectSpell(nSlot, TRUE);
            iSelected = nSlot;
            pI3D->BeginFX("buttons\\battle\\click.wav");
            // notify game
            CItemSpell *pSpell = pUnit->GetQSpellItem(nSlot);
            if (pSpell && pSpell->GetSpell())
            {
                int nMode = MODE_SIMPLE;
                nMode = pSpell->GetSpell()->IsOnUnit() ? MODE_SPELL_TARGET_UNIT
                                                       : MODE_SPELL_TARGET_TILE;
                pScreen->SetInputMode(nMode, nSlot);
            }
        }
        else
        {
            pI3D->BeginFX("buttons\\battle\\click.wav");
            pScreen->SetInputMode(MODE_SIMPLE);
        }
}
/////////////////////////////////////////////////////////////////////////////////////
CSpell* CI3DObjectSpells::GetAreaPreviewSpell(const CI3DScreenMission* owner,int slot) const
{
    if(pScreen!=owner||!pUnit||slot<0||slot>=16||slot>=int(pUnit->GetSpellSlotCount()))return nullptr;
    return pUnit->GetQSpell(slot);
}

bool CI3DScreenMission::GetAreaTargetPreview(CWorldClient& world,CPlot3D& center,float& radius) const
{
    radius=0.0f;
    auto* mod=ModRuntime::Current();
    if(!mod||!mod->CombatSetting("JMVAreaTargetPreview")||!pI3D||pI3D->GetCurrentScreen()!=this||
       !pSpells||nInputMode!=MODE_SPELL_TARGET_TILE||nQSlot<0||nQSlot>=16)return false;
    auto* spell=pSpells->GetAreaPreviewSpell(this,nQSlot);
    if(!spell||!spell->IsOnTile())return false;
    ModSkills::Config config;
    radius=mod->SkillConfig(spell,config)&&mod->PointSkill(spell)?config.areaRadius:spell->GetRadius();
    if(radius<=0.05f)return false;
    const CPoint cursor=theCursor.GetPos();
    CUnit* hovered=world.CombinedPickFirstUnit(cursor);
    CPlot3D location;
    if(hovered&&hovered->CheckFlag(UNIT_VISIBLE)&&!hovered->IsDead())location=hovered->GetCP();
    else location=world.GetTerrainXYZFromScreenXYWithBridge(cursor.x,cursor.y);
    center=location;
    // Context was validated through the typed current screen and bar owner.
    const std::array<bool,8> context={{true,true,true,true,true,true,true,true}};
    return ModSpellTargeting::PreviewSelected(context,nQSlot,radius,{center.x,center.y,center.z});
}

void CI3DObjectSpells::SelfCast(int nSlot)
{
    if(auto*mod=ModRuntime::Current();mod&&pUnit&&nSlot>=0&&nSlot<int(pUnit->GetSpellSlotCount())){
        auto*spell=pUnit->GetQSpell(nSlot);ModSkills::Config config;
        if(mod->SkillConfig(spell,config)){
            auto* player=theWorld.GetPlayer();if(!player)return;
            if(pUnit->IsDead()||!mod->SkillReady(*pUnit,spell,config,false)||config.Cost(spell->GetManaCost())>pUnit->GetMP())return;
            auto*target=theWorld.GetNearestEnemy(pUnit);if(!target||target==pUnit||target->IsDead())return;
            ModSpellIdentity::ActiveSelfCast([&](){player->UnitCommandCast(nSlot,pUnit,target,false);});
            return;
        }
    }
    if (pUnit && nSlot >= 0 && nSlot < nSpells)
    {
        pScreen->SetInputMode(MODE_SPELL_TARGET_UNIT, nSlot);
        CItemSpell *pSpell = pUnit->GetQSpellItem(nSlot);
        if (pSpell && pSpell->GetSpell())
        {
            CUnit *pTarget = 0;
            if (pSpell->GetSpell()->IsOffensive())
            {
                pTarget = theWorld.GetNearestEnemy(pUnit);
                if (!pTarget)
                {
                    // play refuse - no enemies around
                    pI3D->BeginFX("buttons\\battle\\nomagic.wav");
                    pScreen->SetInputMode(MODE_SIMPLE);
                    return;
                }
            }
            else
                pTarget = pUnit;
            if (pTarget)
            {
                if (pSpell->GetSpell()->IsOnUnit())
                    pScreen->CastSpell(pTarget);
                else
                {
                    CPlot3D plot = pTarget->GetCP();
                    pScreen->CastSpell(plot, pUnit);
                }
            }
        }
        pI3D->BeginFX("buttons\\battle\\click.wav");
        pScreen->SetInputMode(MODE_SIMPLE);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObjectSpells::Draw(CRenderer *pRend)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomRight);
    if(auto*mod=ModRuntime::Current())if(pUnit)for(int slot=0;slot<nSpells&&slot<int(pUnit->GetSpellSlotCount())&&slot<figures.GetSize();++slot){
        auto*spell=pUnit->GetQSpell(slot);ModSkills::Config config;if(!figures[slot]||!mod->SkillConfig(spell,config))continue;
        bool ready=mod->SkillReady(*pUnit,spell,config,false)&&config.Cost(spell->GetManaCost())<=pUnit->GetMP();
        figures[slot]->SetDefaultColor(ready?0xffffffff:RGBA_MAKE(128,128,128,255));figures[slot]->UseDefaultColors(TRUE);
    }
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, TRUE);
    CI3DObject::Draw(pRend);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, FALSE);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSpells::OnLButtonDown(UINT nFlags, CPoint point)
{
    int i = HitTest(point);
    JournalHit(i);
    if (i >= 0 && i < nSpells)
    {
        SetProcessed();
        pI3D->SetCapture(this);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSpells::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    int i = HitTest(point);
    JournalHit(i);
    if (i >= 0 && i < nSpells)
    {
        SetProcessed();
        pI3D->SetCapture(this);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSpells::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (pI3D->GetCapture() != this)
        return;
    pI3D->ReleaseCapture();
    int i = HitTest(point);
    JournalHit(i);
    if (i >= 0 && i < nSpells)
    {
        SetProcessed();
        if (pI3D->IsLDblClk())
            SelfCast(i);
        else
            ChangeSpell(i);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSpells::OnMouseMove(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    if (ht >= 0 && ht < nSpells)
        SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DObjectSkills::CI3DObjectSkills(CInterface3D *pI, CI3DScreenMission *pScr)
    : CI3DObject(pI)
    , pScreen(pScr)
    , nSkills(0)
    , iSelected(-1)
    , pUnit(0)
    , fieldCampButtonLastUpdate(0)
    , fieldCampButtonAvailability(-1)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    SetRect(WideScreen::PlacedRect(0, 250, 40, 510));
    for (int i = 0; i < 6; i++)
        AddArea()->MakeRect(0, (470 - i * 40), 40, (510 - i * 40));
    RemoveAllSkills();
    pUnit = 0;

    CTextManager *pTxtMng = theWorld.GetTextManager();
    CString szTmp;
    pTxtMng->GetSystemString("infoitem_16", szTmp);
    szStrings.Add(szTmp);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSkills::AddSkill(LPCTSTR pszFigureName, LPCTSTR pszTextureName, int nTypeSkill)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    int x, y;

    switch (nSkills)
    {
    case 0:
        x = 0;
        y = 0;
        break;
    case 1:
        x = 0;
        y = 64;
        break;
    case 2:
        x = 0;
        y = 128;
        break;
    case 3:
        x = 0;
        y = 192;
        break;
    case 4:
        x = 64;
        y = 0;
        break;
    case 5:
        x = 64;
        y = 64;
        break;
    case 6:
        x = 64;
        y = 128;
        break;
    case 7:
        x = 64;
        y = 192;
        break;
    }

    OwnedRender::E3HudIcon(OwnedRender::E3HudLane::skill,pszTextureName,256,64,64,64,64,x,y);

    CI3DFigure *pFig = AddFigure(pszFigureName);
    if (!pFig) {
        ReportError("Cannot create HUD skill figure: %s", pszFigureName);
        throw std::runtime_error("HUD skill figure missing");
    }

    pFig->SetUShift(x / 256.f);
    pFig->SetVShift((64 + y) / 256.f);
    pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), FP_PI));
    pFig->SetRelativeToScreen(20, 490.f - nSkills * 40, 40);
    pFig->SetDefaultColor(0xffffffff);
    pFig->UseDefaultColors(TRUE);

    nTypes.Add(nTypeSkill);

    nSkills++;
    ShowSelectSkill(nSkills - 1, FALSE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSkills::RemoveAllSkills()
{
    RemoveAllFigures();
    nTypes.RemoveAll();
    nSkills = 0;
    iSelected = -1;
    fieldCampButtonLastUpdate = 0;
    fieldCampButtonAvailability = -1;
    lastRideTip = -1;
    for (int i = 0; i < 6; i++)
        GetArea(i)->SetToolTip(0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSkills::ShowSelectSkill(int num, BOOL bSelect)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    CI3DFigure *pFig;
    pFig = GetFigure(num);
    pFig->SetScale(bSelect ? 1.15f : 1.f);
    if (bSelect)
        pFig->SetDefaultSpecular(0xff404040);
    else
        pFig->SetDefaultSpecular(0xff000000);
    iSelected = num;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSkills::DeselectSkills()
{
    for (int i = 0; i < nSkills; i++)
        ShowSelectSkill(i, FALSE);
    iSelected = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
enum LSCompanionFollowMenuCommand
{
	LS_COMPANION_MENU_CLOSE = -0x7101,
	LS_COMPANION_MENU_MEDIUM = -0x7102,
	LS_COMPANION_MENU_FAR = -0x7103,
	LS_COMPANION_MENU_AI = -0x7104,
	LS_COMPANION_MENU_BACK = -0x7105
};



static const char LS_COMPANION_TOOLTIP_CLOSE[] =
	"\xC1\xEB\xE8\xE7\xEA\xEE\x20\x28\x33\x2D\x35\x20\xEC\x29\n"
	"\xD1\xEB\xE5\xE4\xEE\xE2\xE0\xF2\xFC\x20\xF0\xFF\xE4\xEE\xEC\x20\xF1\x20\xF6\xE5\xEB\xFC\xFE\x2E";
static const char LS_COMPANION_TOOLTIP_MEDIUM[] =
	"\xD1\xF0\xE5\xE4\xED\xFF\xFF\x20\xE4\xE8\xF1\xF2\xE0\xED\xF6\xE8\xFF\x20\x28\x36\x2D\x39\x20\xEC\x29\n"
	"\xC4\xE5\xF0\xE6\xE0\xF2\xFC\xF1\xFF\x20\xEF\xEE\xE7\xE0\xE4\xE8\x20\xF6\xE5\xEB\xE8\x2E";
static const char LS_COMPANION_TOOLTIP_FAR[] =
	"\xC4\xE0\xEB\xE5\xEA\xEE\x20\x28\x31\x30\x2D\x31\x34\x20\xEC\x29\n"
	"\xC4\xE5\xF0\xE6\xE0\xF2\xFC\xF1\xFF\x20\xE2\x20\xF2\xFB\xEB\xF3\x20\xE3\xF0\xF3\xEF\xEF\xFB\x2E";
static const char LS_COMPANION_TOOLTIP_AI_ENABLED[] =
	"\xC1\xEE\xE5\xE2\xEE\xE9\x20\xC8\xC8\x3A\x20\xE2\xEA\xEB\xFE\xF7\xB8\xED\n"
	"\xC0\xE2\xF2\xEE\xEC\xE0\xF2\xE8\xF7\xE5\xF1\xEA\xE8\xE9\x20\xE2\xFB\xE1\xEE\xF0\x20\xE2\xEA\xEB\xFE\xF7\xE5\xED\x2E";
static const char LS_COMPANION_TOOLTIP_AI_DISABLED[] =
	"\xC1\xEE\xE5\xE2\xEE\xE9\x20\xC8\xC8\x3A\x20\xE2\xFB\xEA\xEB\xFE\xF7\xE5\xED\n"
	"\xC0\xE2\xF2\xEE\xEC\xE0\xF2\xE8\xF7\xE5\xF1\xEA\xE8\x20\xE2\xFB\xE1\xE8\xF0\xE0\xF2\xFC\x20\xED\xE0\xE2\xFB\xEA\xE8\x20\xE8\x20\xE7\xE0\xEA\xEB\xE8\xED\xE0\xED\xE8\xFF\x20\xE8\xE7\x20\xEF\xE5\xF0\xE2\xFB\xF5\x20\x38\x20\xFF\xF7\xE5\xE5\xEA\x2E";
static const char LS_COMPANION_TOOLTIP_BACK[] =
	"\xCD\xE0\xE7\xE0\xE4\n"
	"\xC2\xE5\xF0\xED\xF3\xF2\xFC\xF1\xFF\x20\xEA\x20\xED\xE0\xE2\xFB\xEA\xE0\xEC\x2E";
static const char LS_COMPANION_TOOLTIP_TACTICS[] =
	"\xD2\xE0\xEA\xF2\xE8\xEA\xE0\n"
	"\xCD\xE0\xF1\xF2\xF0\xEE\xE8\xF2\xFC\x20\xE4\xE8\xF1\xF2\xE0\xED\xF6\xE8\xFE\x20\xF1\xEB\xE5\xE4\xEE\xE2\xE0\xED\xE8\xFF\x20\xE8\x20\xE1\xEE\xE5\xE2\xEE\xE9\x20\xC8\xC8\x2E";


void CI3DObjectSkills::SetUnit(CUnit *pNewUnit, bool bIsSomeone)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    OwnedRender::E3HudLanePublication hudLane(this,OwnedRender::E3HudLane::skill);
    int i;
    pUnit = pNewUnit;
    RemoveAllSkills();
    if (bIsSomeone)
    {
        AddSkill("initqi1item", ModRuntime::Current() ? "jmv_follow" : "skill0011", -1);
        if (ModRuntime::Current()) GetArea(nSkills - 1)->SetToolTip(LS_COMPANION_TOOLTIP_TACTICS, 1, KA_FOLLOW);
        else GetArea(nSkills - 1)->SetToolTip(10510, 1, KA_FOLLOW);
    }
    if (pUnit)
    {
        CTextManager *pTxtMng = theWorld.GetTextManager();
        const SRPGUserUnitStats *pStats = pUnit->GetUserUnitStats();
        for (i = 0; i < RPG_NUM_ACTUAL_MANUAL_SKILLS; i++)
        // if ( pStats->cManualSkills[i] != 0 )
        {
            CString szSkill, szName, szDesc;
            switch (i)
            {
            case RPGMSKL_STEALING:
                AddSkill("initqi1item", "skill0008", i);
                break;
            case RPGMSKL_SCIENCE:
                AddSkill("initqi1item", ModRuntime::Current() ? "jmv_stealing" : "skill0009", i);
                szSkill = "Science";
                break;
            case RPGMSKL_TAME:
                AddSkill("initqi1item", "skill0010", i);
                break;
            }
            if (pTxtMng->GetText(CString("perk ") + szSkill))
                pTxtMng->GetLine(szName);
            if (ModRuntime::Current())
                szSkill.Format("%s %.0f\n", static_cast<LPCSTR>(szStrings[0]), ModHudAssignment::ManualSkill(*pStats, i));
            else
                szSkill.Format("%s %d\n", static_cast<LPCSTR>(szStrings[0]), pStats->cManualSkills[i]);
            if (pTxtMng->GetText("tip 10511"))
                pTxtMng->GetUntilNextCommand(szDesc);
            szName = szName + "\n" + szSkill + szDesc;
            GetArea(nSkills - 1)->SetToolTip(szName, 1, KA_SKILL_SCIENCE);
        }
    }
    CUnit* goCampContext = pUnit;
    if (!goCampContext && bIsSomeone && pScreen)
    {
        int count = 0, index = -1;
        goCampContext = pScreen->GetSelection(count, index);
        if (count < 2) goCampContext = nullptr;
    }
    if (goCampContext && goCampContext->GetOwner() && theWorld.GetPlayer() &&
        goCampContext->GetOwner() == theWorld.GetPlayer()->GetPlayer() &&
        ModRuntime::Current() && ModRuntime::Current()->CombatSetting("JMVGoCamp"))
    {
        const bool available = theWorldServer.CanBeginFieldCamp();
        AddSkill("initqi1item", available ? "jmv_gocamp" : "jmv_gocamp_disabled", -2);
        fieldCampButtonAvailability = available ? 1 : 0;
        fieldCampButtonLastUpdate = GetTickCount();
        CTextManager* text = theWorld.GetTextManager();
        CString goCampToolTip;
        if (text->GetText("SPELL jmv_gocamp"))
        {
            text->GetUntilNextCommand(goCampToolTip);
            goCampToolTip.TrimRight("\r\n");
        }
        else goCampToolTip = "Camp";
        text->CloseText();
        GetArea(nSkills - 1)->SetToolTip(goCampToolTip, 1);
        if (pUnit && theWorldServer.GetMapObjectManager() &&
            IWorldClient.GetPlayer()->GetPlayer()->IsMainHero(pUnit)) {
            AddSkill("initqi1item","jmv_mount",RidingSkill);
            GetArea(nSkills-1)->SetToolTip("\xd1\xe5\xf1\xf2\xfc\x20\xed\xe0\x20\xeb\xee\xf8\xe0\xe4\xfc",1);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSkills::ChangeSkill(LPCTSTR pszSkillName)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    const int type = strcmp(pszSkillName, "follow") == 0 ? -1 : RPGMSKL_SCIENCE;
    // Resolve the action, not a slot shifted by camp or a tactics submenu.
    if (nSkills && nTypes[0] <= LS_COMPANION_MENU_CLOSE) RestoreSkillBar();
    for (int i = 0; i < nSkills; ++i)
        if (nTypes[i] == type) { ChangeSkill(i); return; }

}
/////////////////////////////////////////////////////////////////////////////////////
static std::vector<CUnit *> SelectedTacticsUnits(CI3DScreenMission *screen)
{
    std::vector<CUnit *> result;
    if (!screen || !theWorldServer.GetMapObjectManager()) return result;
    for (int i = 0; i < screen->GetNumUnits(); ++i) {
        CUnit *client = screen->GetUnit(i);
        if (!client || !client->CheckFlag(OBJ_SELECTED)) continue;
        CUnit *unit = theWorldServer.GetUnit(client->GetID());
        if (unit && unit->IsHero() && unit->GetOwner()) result.push_back(unit);
    }
    return result;
}
void CI3DObjectSkills::RestoreSkillBar()
{
    int count = 0, index = -1;
    CUnit *unit = pScreen->GetSelection(count, index);
    SetUnit(count == 1 ? unit : nullptr, count > 0);
}
void CI3DObjectSkills::ShowTacticsMenu()
{
    const auto units = SelectedTacticsUnits(pScreen);
    if (units.empty()) return;
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    OwnedRender::E3HudLanePublication hudLane(this,OwnedRender::E3HudLane::skill);
    bool allAI = true;
    for (auto *unit : units) allAI = allAI && ModRuntime::Current()->CompanionSkillAI(*unit);
    pScreen->SetInputMode(MODE_SIMPLE);
    RemoveAllSkills();
    const char *icons[] = {"jmv_follow_close", "jmv_follow_medium", "jmv_follow_far",
        allAI ? "jmv_follow_ai_enabled" : "jmv_follow_ai_disabled", "jmv_follow_back"};
    const char *tips[] = {LS_COMPANION_TOOLTIP_CLOSE, LS_COMPANION_TOOLTIP_MEDIUM,
        LS_COMPANION_TOOLTIP_FAR, allAI ? LS_COMPANION_TOOLTIP_AI_ENABLED : LS_COMPANION_TOOLTIP_AI_DISABLED,
        LS_COMPANION_TOOLTIP_BACK};
    for (int i = 0; i < 5; ++i) {
        AddSkill("initqi1item", icons[i], LS_COMPANION_MENU_CLOSE - i);
        GetArea(nSkills - 1)->SetToolTip(tips[i], 1);
    }
}
void CI3DObjectSkills::ChangeSkill(int nSlot)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    if (ModRuntime::Current() && nSlot >= 0 && nSlot < nSkills) {
        const int command = nTypes[nSlot];
        if (command == RidingSkill && pUnit) {
            auto* rider = theWorldServer.GetUnit(pUnit->GetID());
            if (rider && rider->ridingHorse >= 0) pScreen->Dismount(pUnit);
            else pScreen->SetInputMode(MODE_RIDE_TARGET,nSlot,RidingSkill);
            return;
        }
        if (command == -1) { pI3D->BeginFX("buttons\\battle\\click.wav"); ShowTacticsMenu(); return; }
        if (command <= LS_COMPANION_MENU_CLOSE && command >= LS_COMPANION_MENU_BACK) {
            auto units = SelectedTacticsUnits(pScreen);
            pI3D->BeginFX("buttons\\battle\\click.wav");
            if (command == LS_COMPANION_MENU_BACK || units.empty()) {
                RestoreSkillBar(); pScreen->SetInputMode(MODE_SIMPLE); return;
            }
            if (command == LS_COMPANION_MENU_AI) {
                bool allAI = true;
                for (auto *unit : units) allAI = allAI && ModRuntime::Current()->CompanionSkillAI(*unit);
                for (auto *unit : units) {
                    CString key; key.Format("jmv.companion.ai.v1.%d", unit->GetID());
                    if (allAI) unit->GetOwner()->party.vars.DelVar(key);
                    else unit->GetOwner()->party.vars.SetVar(key, 1.f);
                    if (auto *server = dynamic_cast<CUnitServer *>(unit->GetUnitSpecific()))
                        server->logic.caster.RefreshSkillCache(unit);
                    GameJournal::Event("TACTICS", "ai", "unit=%d enabled=%d", unit->GetID(), !allAI);
                }
                ShowTacticsMenu(); return;
            }
            const int profile = LS_COMPANION_MENU_CLOSE - command + 1;
            for (auto *unit : units) {
                CString key; key.Format("jmv.companion.follow.v1.%d", unit->GetID());
                unit->GetOwner()->party.vars.SetVar(key, float(profile));
                EngineDiagnostics::Event("Tactics", "follow unit=%d profile=%d", unit->GetID(), profile);
            }
            RestoreSkillBar(); pScreen->SetInputMode(MODE_SKILL_TARGET_UNIT, 0, -1); return;
        }
    }

    if (nSlot >= 0 && nSlot < nSkills && nTypes[nSlot] == -2) {
        pI3D->BeginFX("buttons\\battle\\click.wav");
        IWorldClient.GoFieldCamp();
        return;
    }
    if (nSlot >= 0 && nSlot < nSkills)
        if (iSelected != nSlot)
        {
            if (iSelected >= 0 && iSelected < nSkills)
                ShowSelectSkill(iSelected, FALSE);
            ShowSelectSkill(nSlot, TRUE);
            iSelected = nSlot;
            pI3D->BeginFX("buttons\\battle\\click.wav");
            EngineDiagnostics::Event("Skills", "activate slot=%d type=%d", nSlot, nTypes[nSlot]);
            // notify game
            if (nTypes[nSlot] == RPGMSKL_SCIENCE)
                pScreen->SetInputMode(MODE_SKILL_TARGET_OBJECT, nSlot, nTypes[nSlot]);
            else
                pScreen->SetInputMode(MODE_SKILL_TARGET_UNIT, nSlot, nTypes[nSlot]);
        }
        else
        {
            pI3D->BeginFX("buttons\\battle\\click.wav");
            pScreen->SetInputMode(MODE_SIMPLE);
        }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObjectSkills::Draw(CRenderer *pRend)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    for(int i=0;i<nSkills;++i) {
        if(nTypes[i]!=RidingSkill || !pUnit || !theWorldServer.GetMapObjectManager()) continue;
        auto* rider=theWorldServer.GetUnit(pUnit->GetID());
        const int mounted=rider && rider->ridingHorse>=0;
        if(mounted!=lastRideTip && OwnedRender::E3HudIcon(OwnedRender::E3HudLane::skill,
            mounted ? "jmv_dismount" : "jmv_mount",256,64,64,64,64,(i/4)*64,(i%4)*64)) {
            GetArea(i)->SetToolTip(mounted?"\xd1\xeb\xe5\xe7\xf2\xfc\x20\xf1\x20\xeb\xee\xf8\xe0\xe4\xe8":"\xd1\xe5\xf1\xf2\xfc\x20\xed\xe0\x20\xeb\xee\xf8\xe0\xe4\xfc",1);
            lastRideTip=mounted;
        }
    }
    bool hasFieldCampButton = false;
    for (int i = 0; i < nSkills; ++i)
    {
        if (nTypes[i] != -2)
            continue;
        hasFieldCampButton = true;
        const DWORD now = GetTickCount();
        if (fieldCampButtonLastUpdate == 0 || now - fieldCampButtonLastUpdate >= 250)
        {
            fieldCampButtonLastUpdate = now;
            const int availability = theWorldServer.CanBeginFieldCamp() ? 1 : 0;
            if (availability != fieldCampButtonAvailability)
            {
                const int x = (i / 4) * 64;
                const int y = (i % 4) * 64;
                if (OwnedRender::E3HudIcon(OwnedRender::E3HudLane::skill,
                    availability ? "jmv_gocamp" : "jmv_gocamp_disabled",256,64,64,64,64,x,y))
                    fieldCampButtonAvailability = availability;
            }
        }
        break;
    }
    if (!hasFieldCampButton)
    {
        fieldCampButtonLastUpdate = 0;
        fieldCampButtonAvailability = -1;
    }
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, TRUE);
    CI3DObject::Draw(pRend);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, FALSE);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSkills::OnLButtonDown(UINT nFlags, CPoint point)
{
    int i = HitTest(point);
    JournalHit(i);
    if (i >= 0 && i < nSkills)
    {
        SetProcessed();
        pI3D->SetCapture(this);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSkills::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (pI3D->GetCapture() != this)
        return;
    pI3D->ReleaseCapture();
    int i = HitTest(point);
    JournalHit(i);
    if (i >= 0 && i < nSkills)
    {
        SetProcessed();
        ChangeSkill(i);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSkills::OnMouseMove(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    if (ht >= 0 && ht < nSkills)
        SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DObjectFaces::CI3DObjectFaces(CInterface3D *pI, CI3DScreenMission *pScr)
    : CI3DObject(pI)
    , pScreen(pScr)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomCentre);
    SetRect(WideScreen::PlacedRect(250, 440, 550, 600));
    nFaces = 0;
    AddFigure()->MakeBar(10, 80, 0, 0, 20, 20, 34, 150, 54, 170);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectFaces::AddFace(LPCTSTR pszFigureName, LPCTSTR pszTextureName, bool bIsMainHero,
                              bool bNotOnZone)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomCentre);
    int x, y, i = nFaces % 6, j = nFaces / 6;
    const bool hdInterface = UseModInterfaceHD();
    const int faceSize = hdInterface ? 512 : 128;
    const int canvasSize = hdInterface ? 1024 : 256;
    switch (nFaces % 4)
    {
    case 0:
        x = 0;
        y = 0;
        // Atlas cell is a value; texture ownership belongs to the renderer.
        break;
    case 1:
        x = faceSize;
        y = 0;
        break;
    case 2:
        x = 0;
        y = faceSize;
        break;
    case 3:
        x = faceSize;
        y = faceSize;
        break;
    }


    faces.emplace_back();
    faces[nFaces].bNotOnZone = bNotOnZone;

    CI3DFigure *pFig;
    pFig = AddFigure(pszFigureName);

    int w1 = 56, w2 = 40, h1 = 90, h2 = 65;
    int w, h, l, r, t, b;
    {
        w = w1;
        h = h1;
        l = 400 - pScreen->GetNumUnits() * w / 2 + i * w;
    }
    r = l + w;
    t = 600 - h;
    b = 600;

    if (pFig)
    {
        pFig->SetRelativeToScreen((l + r) / 2.f, t + (h * 38) / 90.f, 7.f);
        pFig->SetScale((w < 50) ? .22f : .3f);
        // faces[nFaces].qDef = CQuaternion( CPlot3D(0,1,0), ( - 125 + i * 50) * FP_PI2 / 1000.f ) *
        // CQuaternion( CPlot3D(1,0,0), FP_PI );
        faces[nFaces].qDef = CQuaternion(CPlot3D(1, 0, 0), FP_PI);
        pFig->SetOrientation(faces[nFaces].qDef);
    }
    else
    {
        pFig = AddFigure();
        pFig->MakeBar(7, 100, l, t, r, b - 20, 0, 0, 128, 128);
        faces[nFaces].qDef = QNULL;
    }
    faces[nFaces].qCur = faces[nFaces].qDef;
    faces[nFaces].nPlayAnim = 0;

    if (bNotOnZone)
        pFig->SetAmbient(.5f, .5f, .5f);
    pFig->SetUShift(x / float(canvasSize));
    pFig->SetVShift((faceSize + y) / float(canvasSize));
    pFig->SetTexture(NULL); // Selected logical face texture is submitted by E3Hud.

    pFig = AddFigure();
    pFig->MakeBar(7, r - l - 6, l + 3, b - 13, r - 3, b - 8, 9, 9, 73, 22);
    pFig->SetTexture(textures[0]);
    if (bNotOnZone)
        pFig->Show(FALSE);
    pFig = AddFigure();
    pFig->MakeBar(7, r - l - 6, l + 3, b - 8, r - 3, b - 3, 9, 35, 73, 48);
    pFig->SetTexture(textures[0]);
    if (bNotOnZone)
        pFig->Show(FALSE);

    pFig = AddFigure();
    pFig->MakeBarFrame(20, 80, l, t, r, b, 3, 27, 96, 90, 101);
    pFig->SetTexture(textures[0]);
    // pFig->Show( FALSE );

    pFig = AddFigure();
    pFig->MakeBar(22, 80, l, t, r, b);
    pFig->SetTexture(NULL);
    pFig->UseDefaultColors(TRUE);
    pFig->Show(FALSE);

    SelectFace(i, FALSE);
    nFaces++;

    AddArea()->MakeRect(l, t, r, b);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectFaces::ChangeFaceTexture(int nFace, int nTex)
{
    if (nFace >= 0 && static_cast<size_t>(nFace) < faces.size())
        faces[nFace].nCurTex = nTex;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectFaces::AddFaces(bool bIsMainHero)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomCentre);
    RemoveAllFigures();
    RemoveAllAreas();


    AddFigure()->MakeBar(10, 80, 0, 0, 20, 20, 34, 150, 54, 170);

    nFaces = 0;
    int nProt;
    CString szFigureName, szTextureName, szUnitName;
    CString szSex, szGrace;
    faces.clear();
    for (int i = 0; i < pScreen->GetNumUnits(); i++)
    {
        CUnit *pUnit = pScreen->GetUnit(i);
        //		if ( pUnit->GetTypeID() == MOTID_HUMAN )
        //		{
        nProt = pUnit->GetPrototype()->dwSkinIndex;
        if (strcmp(pUnit->GetMaskName(), "unhuma") == 0)
            szSex = "m";
        else if (strcmp(pUnit->GetMaskName(), "unhufe") == 0)
            szSex = "f";
        SComplection comp;
        pUnit->GetComplection(comp);
        szGrace = "me";
        if (comp.grace < 0.2f)
            szGrace = "th";
        else if (comp.grace > 0.8f)
            szGrace = "fa";
        // szFigureName.Format( "infa%s%dface", pUnit->GetMaskName(), nProt );
        szFigureName.Format("infa%s%s%dface", pUnit->GetMaskName(), static_cast<LPCSTR>(szGrace),
                            pUnit->GetPrototype()->dwHair + 1);
        szTextureName.Format("face%s%02d", static_cast<LPCSTR>(szSex), nProt);

        //		}
        //		else
        //		{
        //			szFigureName.Format( "infa%sface", pUnit->GetMaskName() );
        //			szTextureName.Format( "face_%s",
        // static_cast<CObject3DClientSpecific*>(pUnit->GetObjectSpecific())->GetPrimaryTextureName()
        //);
        //		}
        AddFace(szFigureName, szTextureName, bIsMainHero);
        if (pScreen->GetUnit(i)->CheckFlag(OBJ_SELECTED))
            SelectFace(i, TRUE);
        faces[i].fTime = 0;
        faces[i].nOldHP = pScreen->GetUnit(i)->GetHP();

        faces[i].szTexName = szTextureName;
        faces[i].nCurTex = 0;

        faces[i].szFigNameB.Format("infa%s", pUnit->GetMaskName());
        faces[i].szFigNameE.Format("%dface", pUnit->GetPrototype()->dwHair + 1);
        faces[i].fCurGrace = comp.grace;

        faces[i].nUniquePersonID = -1;

        pScreen->GetUnitName(pScreen->GetUnit(i), szUnitName);
        if (KA_SELECT1 + i < KA_SELECTALL)
            areas[i]->SetToolTip(szUnitName, 1, KA_SELECT1 + i);
        else
            areas[i]->SetToolTip(szUnitName, 1);
    }

}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectFaces::SelectFace(int numFace, BOOL bSelect)
{
    CI3DFigure *pFig;
    pFig = GetFigure(numFace * 5 + 4);
    /*
    if ( bSelect )
    {
            pFig->SetUShift(0);
            pFig->SetVShift(0);
    }
    else
    {
            pFig->SetUShift(160/256.f);
            pFig->SetVShift(-51/256.f);
    }
    */
    pFig->Show(bSelect);
    faces[numFace].qSel = bSelect ? CQuaternion(CPlot3D(-1, 0, 0), FP_PI2 / 7) : QNULL;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectFaces::PlayAnim(int nFace, int nAnim)
{
    faces[nFace].fAnimTime = -random.GetFloat(0, .3f);
    faces[nFace].nPlayAnim = nAnim;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectFaces::PlayTex(int nFace, int nTex)
{
    faces[nFace].fTexTime = 0;
    faces[nFace].nPlayTex = nTex;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectFaces::OnIdle()
{
    CI3DFigure *pFig = 0;

    float fFrameTime = IWorld.IsMissionCombatPause() ? 0.f : GetFrameTime();

    figures[0]->Show(FALSE);
    for (int i = 0; i < pScreen->GetNumUnits(); i++)
        if (i < nFaces)
        {
            // show hp & stamina
            float x, y;
            float fp, fMapX;
            int nNewHP;

            nNewHP = Max(0, pScreen->GetUnit(i)->GetHP());
            if (pScreen->GetUnit(i)->GetMaxHP() > 0)
                fp = float(nNewHP) / pScreen->GetUnit(i)->GetMaxHP();
            else
                fp = 0;
            fMapX = (9 + (73 - 9) * fp) / 256.f + 1 / 512.f;

            pFig = GetFigure(i * 5 + 2);
            pFig->GetPlot(0, 2, x, y);
            pFig->SetPlot(0, 2, fp - 0.5f, y);
            pFig->GetPlot(0, 3, x, y);
            pFig->SetPlot(0, 3, fp - 0.5f, y);
            pFig->SetMap(2, fMapX, 1 - 19 / 512.f);
            pFig->SetMap(3, fMapX, 1 - 45 / 512.f);

            if (pScreen->GetUnit(i)->GetMaxMP() > 0)
                fp = Max(0.0f, float(pScreen->GetUnit(i)->GetMP())) /
                     float(pScreen->GetUnit(i)->GetMaxMP());
            else
                fp = 0;
            fMapX = (9 + (73 - 9) * fp) / 256.f + 1 / 512.f;

            pFig = GetFigure(i * 5 + 3);
            pFig->GetPlot(0, 2, x, y);
            pFig->SetPlot(0, 2, fp - 0.5f, y);
            pFig->GetPlot(0, 3, x, y);
            pFig->SetPlot(0, 3, fp - 0.5f, y);
            pFig->SetMap(2, fMapX, 1 - 71 / 512.f);
            pFig->SetMap(3, fMapX, 1 - 97 / 512.f);

            // check hit
            if (faces[i].nOldHP > nNewHP)
            {
                faces[i].fTime = 1;
                faces[i].nOldHP = nNewHP;
                PlayTex(i, 2);
            }
            else if (faces[i].fTime > 0)
                faces[i].fTime -= fFrameTime;

            if (faces[i].fTime < 0)
                faces[i].fTime = 0;

            pFig = GetFigure(i * 5 + 5);
            if (faces[i].fTime > 0)
            {
                pFig->Show(TRUE);
                pFig->SetDefaultColor(RGBA_MAKE(255, 0, 0, int(faces[i].fTime * 200)));
            }
            else
                pFig->Show(FALSE);

            // processing textures
            int nWishTex = 0;
            if (faces[i].nPlayTex)
            {
                faces[i].fTexTime += fFrameTime;
                switch (faces[i].nPlayTex)
                {
                case 1:
                    if (faces[i].fTexTime > 4.f)
                        faces[i].nPlayTex = 0;
                    break;
                case 2:
                    if (faces[i].fTexTime > 2.f)
                        faces[i].nPlayTex = 0;
                    break;
                }
                nWishTex = faces[i].nPlayTex;
            }
            if (!faces[i].nPlayTex)
            {
                float fp;
                if (pScreen->GetUnit(i)->GetMaxMP() > 0)
                    fp = float(pScreen->GetUnit(i)->GetMP()) /
                         float(pScreen->GetUnit(i)->GetMaxMP());
                else
                    fp = 0;
                if (fp < .25f) // tired
                    nWishTex = 3;
            }
            if (nWishTex != faces[i].nCurTex)
            {
                faces[i].nCurTex = nWishTex;
                ChangeFaceTexture(i, nWishTex);
            }

            // processing figures
            SComplection comp;
            pScreen->GetUnit(i)->GetComplection(comp);
            if (faces[i].fCurGrace != comp.grace)
            {
                faces[i].fCurGrace = comp.grace;
                CString szGrace = "me";
                if (comp.grace < 0.2f)
                    szGrace = "th";
                else if (comp.grace > 0.8f)
                    szGrace = "fa";
                szGrace = faces[i].szFigNameB + szGrace + faces[i].szFigNameE;
                CI3DFigure *pFig = ChangeFigure(GetFigure(i * 5 + 1), szGrace);
            }

            // processing animation
            CQuaternion qTemp;
            if (!faces[i].nPlayAnim)
                if (fFrameTime > 0 && random.GetFloat(0, 1) < fFrameTime / 3)
                {
                    PlayAnim(i, 3);
                    faces[i].qIdle = CQuaternion(CPlot3D(1, 0, 0),
                                                 random.GetFloat(-1, 1) * FP_PI2 / 12) *
                                     CQuaternion(CPlot3D(0, 1, 0),
                                                 random.GetFloat(-1, 1) * FP_PI2 / 12);
                    faces[i].fIdleSpeed = random.GetFloat(1, 3);
                }

            if (faces[i].nPlayAnim)
            {
                faces[i].fAnimTime += fFrameTime;
                if (faces[i].fAnimTime >= 0)
                    switch (faces[i].nPlayAnim)
                    {
                    case 1: // yes
                        if (faces[i].fAnimTime >= FP_PI / 5)
                            faces[i].nPlayAnim = 0;
                        else
                        {
                            float fSin = (float)sin(faces[i].fAnimTime * 5);
                            qTemp = CQuaternion(CPlot3D(1, 0, 0), fSin * FP_PI2 / 10);
                        }
                        break;
                    case 2: // no
                        if (faces[i].fAnimTime >= FP_2PI / 5)
                            faces[i].nPlayAnim = 0;
                        else
                        {
                            float fSin = (float)sin(faces[i].fAnimTime * 5);
                            qTemp = CQuaternion(CPlot3D(0, 1, 0), fSin * FP_PI2 / 10);
                        }
                        break;
                    case 3: // idle random
                        if (faces[i].fAnimTime >= FP_2PI / faces[i].fIdleSpeed)
                            faces[i].nPlayAnim = 0;
                        else
                        {
                            float fFract = .5f -
                                           (float)cos(faces[i].fAnimTime * faces[i].fIdleSpeed) / 2;
                            qTemp.Slerp(fFract, QNULL, faces[i].qIdle);
                        }
                        break;
                    }
            }

            CQuaternion qNew, qNeed = faces[i].qDef * faces[i].qSel;
            const CQuaternion qOld = faces[i].qCur; // GetFigure(i*5+1)->GetOrientation();
            if (fFrameTime >= .5f)
                qNew = qNeed;
            else
                qNew.Slerp(fFrameTime * 2, qOld, qNeed);
            faces[i].qCur = qNew;
            if (faces[i].nPlayAnim)
                qNew = qNew * qTemp;
            GetFigure(i * 5 + 1)->SetOrientation(qNew);

            // camera tracking
            if (theCamera.GetCarrierID() == pScreen->GetUnit(i)->GetID())
            {
                figures[0]->Show(TRUE);
                float x, y, z;
                GetFigure(i * 5 + 4)->GetRelativeToScreen(x, y, z);
                figures[0]->SetRelativeToScreen(x, y - 60, 10);
            }
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectFaces::OnLButtonDown(UINT nFlags, CPoint point)
{
    int i = HitTest(point);
    JournalHit(i);


    if (i >= 0)
    {
        SetProcessed();
        if (i < pScreen->GetNumUnits())
            pI3D->SetCapture(this);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectFaces::OnLButtonUp(UINT nFlags, CPoint point)
{

    if (pI3D->GetCapture() != this)
        return;
    pI3D->ReleaseCapture();
    int i = HitTest(point);
    JournalHit(i);
    if (i >= 0)
    {
        SetProcessed();
        if (i < pScreen->GetNumUnits())
            pScreen->PerformAction(pScreen->GetUnit(i), CPoint(0, 0));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectFaces::OnMouseMove(UINT nFlags, CPoint point)
{
    int i = HitTest(point);


    if (i >= 0)
    {
        SetProcessed();
        if (i < pScreen->GetNumUnits())
            pScreen->CheckAction(pScreen->GetUnit(i), CPoint(0, 0));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectMap::Init(float fInitZoom, bool bClosed)
{
    WideScreen::Scope wideScope(owned::widescreen::kTopRight);
    SetRect(WideScreen::PlacedRect(615, 0, 800, 165));

    fPixInMeter = 80;

    SScenarioZone *pZ = theMap.GetZoneByName(IWorldClient.GetCurrentZoneName());
    if (pZ)
    {
        // Use the dimensions of the terrain actually loaded for this zone.
        // Mod map art and an old map.txt can otherwise disagree at runtime.
        CTerrain *terrain = theWorld.GetTerrain();
        const int sectorsX = terrain && terrain->GetXSectors() > 0
                                 ? terrain->GetXSectors() : pZ->nSizeX;
        const int sectorsY = terrain && terrain->GetYSectors() > 0
                                 ? terrain->GetYSectors() : pZ->nSizeY;
        fXMapSize = float(sectorsX) * 32;
        fYMapSize = float(sectorsY) * 32;
        AddTexture(pZ->szMinimapTexture);
    }
    else
    {
        fXMapSize = 32;
        fYMapSize = 32;
        AddTexture("battle00");
    }

    fMaxSize = Max(fXMapSize, fYMapSize);
    fTerr2FigScale = 2.f / fMaxSize;
    fMaxZoom = fMaxSize / 20.f;
    fZoom = Min(fInitZoom, fMaxZoom);

    fXCenter = 0;
    fYCenter = 0;
    fXDir = 0;
    fYDir = 1;

    slide_bIsSlide = TRUE;
    slide_nCurPixels = bClosed ? 180 : 0;
    slide_nTotalPixels = 180;
    slide_fCurTime = bClosed ? 1 : 0;
    slide_dir = bClosed ? 1 : -1;

    // close arrow
    CI3DPolyArea *pArea = AddArea();
    pArea->MakeRect(780, 0, 800, 30);
    pArea->SetToolTip(10300, 1, KA_MINIMAP);
    // magnify
    pArea = AddArea();
    for (int i = 0; i < 16; i++)
        pArea->AddPoint(int(700 - sin(FP_PI * i / 8) * 65), int(80 + cos(FP_PI * i / 8) * 65));
    // map itself
    AddArea()->MakeRect(620, 0, 780, 160);
    // zoom in
    pArea = AddArea();
    pArea->MakeRect(780, 100, 800, 130);
    pArea->SetToolTip(10301, 1);
    // zoom out
    pArea = AddArea();
    pArea->MakeRect(780, 130, 800, 160);
    pArea->SetToolTip(10302, 1);
    // normalize camera
    pArea = AddArea();
    pArea->MakeRect(780, 70, 800, 100);
    pArea->SetToolTip(10303, 1, KA_CAMERA_NORM);
    // map itself
    AddArea()->MakeRect(615, 0, 800, 165);

    CI3DFigure *pFig;
    // map
    pFig = AddFigure();
    pFig->MakeBar(18, 100, 620, 0, 780, 160);
    pFig->SetTexture(textures[1]);
    pFig->SetAmbient(.8f, .8f, .8f);
    pFig->SetScale(18 * fPixInMeter / 400 * VIEWING_ANGLE_TAN);
    // magnify
    pFig = AddFigure();
    pFig->MakeCircle(14, 80, 700, 80, 64, 16);
    pFig->SetTexture(textures[1]);
    pFig->SetAmbient(1, 1, 1);
    // units
    pFig = AddFigure();
    pFig->SetRelativeToScreen(700, 80, 12);
    pFig->SetScale(12 * fPixInMeter / 400 * VIEWING_ANGLE_TAN);
    pFig->SetFlatNorms();
    pFig->SetMapsSize(4);
    pFig->SetMap(0, 240.f / 256, 252.f / 256);
    pFig->SetMap(1, 244.f / 256, 252.f / 256);
    pFig->SetMap(2, 248.f / 256, 252.f / 256);
    pFig->SetMap(3, 252.f / 256, 252.f / 256);

    // map bg
    pFig = AddFigure();
    pFig->MakeBar(20, 100, 620, 0, 780, 160);
    pFig->SetTexture(NULL);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 80));
    // toolbar
    pFig = AddFigure();
    pFig->MakeBar(20, 100, 780, 0, 800, 160);
    pFig->SetTexture(NULL);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 180));
    // close arrow
    pFig = AddFigure("in25arrow");
    pFig->SetUShift(57 / 256.f);
    pFig->SetVShift(36 / 256.f);
    pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), -FP_PI / 2));
    pFig->SetRelativeToScreen(850, 20, 8.106086f);
    pFig->SetScale(.8f);
    // zoom in
    pFig = AddFigure();
    pFig->MakeBar(10, 80, 780, 100, 800, 130, 25, 62, 45, 92);
    // zoom out
    pFig = AddFigure();
    pFig->MakeBar(10, 80, 780, 130, 800, 160, 3, 62, 23, 92);
    // circle frame
    pFig = AddFigure();
    pFig->MakeCircleFrame(10, 60, 700, 80, 66, 16, 3, 27, 107, 91, 112);
    /*
    pFig->SetDefaultColor( RGBA_MAKE(209,144,79,255) );
*/
    pFig = AddFigure();
    pFig->MakeBarFrame(10, 80, 615, -5, 805, 165, 5, 4, 55, 90, 60);

    pFig = AddFigure("in25arrow");
    pFig->SetUShift(57 / 256.f);
    pFig->SetVShift(36 / 256.f);
    pFig->SetRelativeToScreen(900, 80, 8.106086f);
    pFig->SetScale(.8f);

    // normalize camera
    pFig = AddFigure();
    pFig->MakeBar(10, 80, 780, 70, 800, 100, 34, 118, 54, 148);

    CI3DSurface *pSurf = dbgnew CI3DSurface(pI3D, 700, 80, 705, 85);
    AddSurface(pSurf);
#ifdef DXGI_ENABLED
    CDC *pDC = pSurf->GetDC();
    OwnedRender::E2SetPixel(pDC,0, 0, RGB(255, 255, 255));
    pSurf->ReleaseDC();
#endif // NOGETDC

    int j;
    CPlot2D pt;

    pFig = figures[1];
    for (int i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
        {
            pFig->GetPlot(i, j, pt.x, pt.y);
            orig_edges.Add(pt);
        }

    ModifyMapFigure();
    PlaceUnits();

    nMode = 0;
    ShowCurrentMode();
    ScrollFigures();

    fNormTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectMap::ScrollFigures()
{
    WideScreen::Scope wideScope(owned::widescreen::kTopRight);
    // map
    figures[0]->SetRelativeToScreen(700 + float(slide_nCurPixels), 80, 18);
    // magnify
    figures[1]->SetRelativeToScreen(700 + float(slide_nCurPixels), 80, 14);
    // units
    figures[2]->SetRelativeToScreen(700 + float(slide_nCurPixels), 80, 12);
    // map bg
    figures[3]->SetRelativeToScreen(700 + float(slide_nCurPixels), 80, 20);
    // toolbar
    figures[4]->SetRelativeToScreen(790 + float(slide_nCurPixels), 80, 20);

    figures[6]->SetRelativeToScreen(790 + float(slide_nCurPixels), 115, 10);
    figures[7]->SetRelativeToScreen(790 + float(slide_nCurPixels), 145, 10);

    figures[8]->SetRelativeToScreen(700 + float(slide_nCurPixels), 80, 10);
    figures[9]->SetRelativeToScreen(710 + float(slide_nCurPixels), 80, 10);

    figures[10]->SetRelativeToScreen(694 + float(slide_nCurPixels), 84, 8.106086f);

    figures[11]->SetRelativeToScreen(790 + float(slide_nCurPixels), 85, 10);

    surfaces[0]->Move(CPoint(700 + slide_nCurPixels, 80));

    for (int i = 1; i < areas.GetSize(); i++)
        GetArea(i)->SetShift(slide_nCurPixels, 0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectMap::OnIdle()
{
    WideScreen::Scope wideScope(owned::widescreen::kTopRight);
    CI3DFigure *pFig;
    float fPart;
    if (nMode == 1)
    {
        float time = GetFrameTime();
        float zoom = (float)pow(2.5f, time);
        fZoom *= zoom;
        if (fZoom > fMaxZoom)
            fZoom = fMaxZoom;
    }
    if (nMode == 2)
    {
        float time = GetFrameTime();
        float zoom = (float)pow(2.5f, time);
        fZoom /= zoom;
        if (fZoom < 1.f)
            fZoom = 1.f;
    }

    ModifyMapFigure();
    PlaceUnits();

    if (slide_bIsSlide)
    {
        slide_fCurTime += slide_dir * GetFrameTime() / SLIDE_TIME;
        if (slide_fCurTime > 1.f)
            slide_fCurTime = 1.f;
        if (slide_fCurTime < 0.f)
            slide_fCurTime = 0.f;

        if (slide_fCurTime < .5f)
            fPart = 2.f * slide_fCurTime * slide_fCurTime;
        else
            fPart = 1.f - 2.f * (1.f - slide_fCurTime) * (1.f - slide_fCurTime);

        slide_nCurPixels = (int)(fPart * slide_nTotalPixels);

        if (slide_fCurTime == 1.f || slide_fCurTime == 0.f)
            slide_bIsSlide = FALSE;

        ScrollFigures();

        pFig = figures[5];
        if (slide_dir == 1)
        {
            pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), FP_PI2));
            pFig->SetRelativeToScreen(713, 20, 8.106086f);
        }
        else
        {
            pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), -FP_PI2));
            pFig->SetRelativeToScreen(850, 20, 8.106086f);
        }
    }
    pI3D->bMapClosed = slide_dir > 0;

    if (fNormTime > 0)
        fNormTime -= GetFrameTime();
    if (fNormTime > 0)
        figures[11]->SetAmbient(1, 1, 1);
    else
        figures[11]->SetAmbient(.5f, .5f, .5f);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectMap::ModifyMapFigure()
{
    WideScreen::Scope wideScope(owned::widescreen::kTopRight);
    int i, j, k, num;
    float d;
    CPlot2D p1, p2, p3;
    CI3DFigure *pFig;

    CBase3D *obj = theWorld.GetCamera();
    if (obj)
    {
        fXCenter = theCamera.GetAnchorPoint().x;
        fYCenter = theCamera.GetAnchorPoint().y;
        fXDir = obj->GetZAxis().x;
        fYDir = obj->GetZAxis().y;
    }

    d = fXDir * fXDir + fYDir * fYDir;
    if (d < 1e-6)
    {
        fXDir = 0;
        fYDir = 1;
    }
    else
    {
        d = float(sqrt(d));
        fXDir = fXDir / d;
        fYDir = fYDir / d;
    }

    pFig = figures[10];
    pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), (float)atan2(-fXDir, -fYDir)));

    pFig = figures[1];
    // fill temporary edges info due to original figure
    num = orig_edges.GetSize();
    map_edges.SetSize(num);
    for (i = 0; i < num; i++)
        map_edges[i] = FromFigureToTerrain(orig_edges[i], TRUE);

    // cut by four sides of map
    for (i = 0; i < num; i++)
    {
        p2 = map_edges[i];
        p1 = map_edges[i > 0 ? i - 1 : num - 1];
        if (p1.x < 0 && p2.x > 0 || p1.x > 0 && p2.x < 0)
        {
            p3.x = 0;
            p3.y = p1.y - p1.x * (p2.y - p1.y) / (p2.x - p1.x);
            map_edges.InsertAt(i, p3);
            i++;
            num++;
        }
    }
    for (i = 0; i < num; i++)
        if (map_edges[i].x < 0)
        {
            map_edges.RemoveAt(i);
            i--;
            num--;
        }

    for (i = 0; i < num; i++)
    {
        p2 = map_edges[i];
        p1 = map_edges[i > 0 ? i - 1 : num - 1];
        if (p1.y < 0 && p2.y > 0 || p1.y > 0 && p2.y < 0)
        {
            p3.x = p1.x - p1.y * (p2.x - p1.x) / (p2.y - p1.y);
            p3.y = 0;
            map_edges.InsertAt(i, p3);
            i++;
            num++;
        }
    }
    for (i = 0; i < num; i++)
        if (map_edges[i].y < 0)
        {
            map_edges.RemoveAt(i);
            i--;
            num--;
        }

    for (i = 0; i < num; i++)
    {
        p2 = map_edges[i];
        p1 = map_edges[i > 0 ? i - 1 : num - 1];
        if (p1.x < fXMapSize && p2.x > fXMapSize || p1.x > fXMapSize && p2.x < fXMapSize)
        {
            p3.x = fXMapSize;
            p3.y = p1.y + (fXMapSize - p1.x) * (p2.y - p1.y) / (p2.x - p1.x);
            map_edges.InsertAt(i, p3);
            i++;
            num++;
        }
    }
    for (i = 0; i < num; i++)
        if (map_edges[i].x > fXMapSize)
        {
            map_edges.RemoveAt(i);
            i--;
            num--;
        }

    for (i = 0; i < num; i++)
    {
        p2 = map_edges[i];
        p1 = map_edges[i > 0 ? i - 1 : num - 1];
        if (p1.y < fYMapSize && p2.y > fYMapSize || p1.y > fYMapSize && p2.y < fYMapSize)
        {
            p3.x = p1.x + (fYMapSize - p1.y) * (p2.x - p1.x) / (p2.y - p1.y);
            p3.y = fYMapSize;
            map_edges.InsertAt(i, p3);
            i++;
            num++;
        }
    }
    for (i = 0; i < num; i++)
        if (map_edges[i].y > fYMapSize)
        {
            map_edges.RemoveAt(i);
            i--;
            num--;
        }

    // fill components
    pFig->SetComponentsSize(num);
    for (i = 0; i < num; i++)
        pFig->SetComponent(i, i, i);
    // fill maps
    pFig->SetMapsSize(num);
    for (i = 0; i < num; i++)
    {
        p1 = FromTerrainToMap(map_edges[i]);
        pFig->SetMap(i, p1.x, p1.y);
    }
    // fill faces
    if (num > 2)
    {
        pFig->SetIndicesSize((num - 2) * 3);
        for (i = 0; i < num - 2; i++)
        {
            pFig->SetIndex(i * 3, 0);
            pFig->SetIndex(i * 3 + 1, i + 2);
            pFig->SetIndex(i * 3 + 2, i + 1);
        }
    }
    else
        pFig->SetIndicesSize(0);
    // fill vertices
    k = num;
    num = k / 4;
    if (k % 4)
    {
        num++;
        p1 = map_edges[0];
        for (i = 0; i < 4 - k % 4; i++)
            map_edges.Add(p1);
    }
    pFig->SetPlotsSize(num);
    k = 0;
    for (i = 0; i < num; i++)
        for (j = 0; j < 4; j++)
        {
            p1 = FromTerrainToFigure(map_edges[k], TRUE);
            pFig->SetPlot(i, j, p1.x, p1.y);
            k++;
        }

    pFig->MapAfterModify();

    pFig = figures[0];

    CPlot2D lb, rt;
    lb = FromTerrainToFigure(CPlot2D(0, 0));
    rt = FromTerrainToFigure(CPlot2D(fXMapSize, fYMapSize));
    if (lb.x < -1.f)
        lb.x = -1.f;
    if (lb.y > 1.f)
        lb.y = 1.f;
    if (rt.x > 1.f)
        rt.x = 1.f;
    if (rt.y < -1.f)
        rt.y = -1.f;

    if (lb.x >= rt.x || rt.y >= lb.y) // bug was found due to SWIRIDOFF efforts
    {
        pFig->Show(FALSE);
        return;
    }

    pFig->Show(TRUE);

    pFig->SetPlot(0, 0, lb.x, rt.y);
    pFig->SetPlot(0, 1, lb.x, lb.y);
    pFig->SetPlot(0, 2, rt.x, rt.y);
    pFig->SetPlot(0, 3, rt.x, lb.y);

    lb = FromTerrainToMap(FromFigureToTerrain(lb));
    rt = FromTerrainToMap(FromFigureToTerrain(rt));

    pFig->SetMap(0, lb.x, rt.y);
    pFig->SetMap(1, lb.x, lb.y);
    pFig->SetMap(2, rt.x, rt.y);
    pFig->SetMap(3, rt.x, lb.y);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectMap::PlaceUnits()
{
    WideScreen::Scope wideScope(owned::widescreen::kTopRight);
    int i, j, num = 0, numq = 0, nume = 0, numc = 0;
    CPlot2D pos, dir, dir2;
    CI3DFigure *pFig;
    CPlot3D unit_cp;
    float d;
    CUnit *pUnit;
    int nID;
    CMapObjectManager *pMobMng = (CMapObjectManager *)theWorld.GetMapObjectManager();

    pFig = figures[2];

    // alive units
    CPlayerClient *pPl = theWorld.GetPlayer();
    if (pPl)
        num = pPl->visibleUnitIDs.GetSize();

    // quest markers
    CLightManager *pLightManager = static_cast<CLightManager *>(theWorld.GetLightManager());
    const std::list<int> &qlights = pLightManager->GetQuestLightIDs();
    std::list<int>::const_iterator it;
    for (it = qlights.begin(); it != qlights.end(); ++it)
        numq++;

    // zone exits
    nume = theWorld.GetNumExits();

    // corpses


    // fill vertices
    pFig->SetPlotsSize(num + numq + nume + numc);
    pFig->SetComponentsSize((num + numq + nume + numc) * 4);
    pFig->SetIndicesSize(num * 3 + numq * 6 + nume * 6 + numc * 6);

    int nInd = 0;
    int nComp = 0;
    int nPlot = 0;

    // units
    for (i = 0; i < num; i++)
    {
        /*
                        pFig->SetIndex( nInd,     nComp     );
                        pFig->SetIndex( nInd + 1, nComp + 2 );
                        pFig->SetIndex( nInd + 2, nComp + 1 );
                        pFig->SetIndex( nInd + 3, nComp + 1 );
                        pFig->SetIndex( nInd + 4, nComp + 2 );
                        pFig->SetIndex( nInd + 5, nComp + 3 );
                        nInd += 6;

                        nID = pPl->visibleUnitIDs[i];
                        pUnit = (CUnit*)pMobMng->Find( nID );
                        if ( !pUnit || pUnit->IsDead() )
                        {
                                pFig->SetComponent( nComp,     0, 0 );
                                pFig->SetComponent( nComp + 1, 0, 0 );
                                pFig->SetComponent( nComp + 2, 0, 0 );
                                pFig->SetComponent( nComp + 3, 0, 0 );
                                nComp += 4;
                                continue;
                        }

                        pos.x = pUnit->GetCP().x;
                        pos.y = pUnit->GetCP().y;


                        CPlot2D pos2 = FromTerrainToFigure( pos, TRUE );
                        if ( pos2.x * pos2.x + pos2.y * pos2.y > 0.6f )
                        {
                                pos2 = FromTerrainToFigure( pos, FALSE );
                                if ( pos2.x * pos2.x + pos2.y * pos2.y < 0.68f || fabs( pos2.x ) >
           0.97f || fabs( pos2.y ) > 0.97f )
                                {
                                        pFig->SetComponent( nComp,     0, 0 );
                                        pFig->SetComponent( nComp + 1, 0, 0 );
                                        pFig->SetComponent( nComp + 2, 0, 0 );
                                        pFig->SetComponent( nComp + 3, 0, 0 );
                                        nComp += 4;
                                        continue;
                                }
                        }

                        pFig->SetPlot( i, 0, pos2.x - 0.03f, pos2.y - 0.03f );
                        pFig->SetPlot( i, 1, pos2.x + 0.03f, pos2.y - 0.03f );
                        pFig->SetPlot( i, 2, pos2.x - 0.03f, pos2.y + 0.03f );
                        pFig->SetPlot( i, 3, pos2.x + 0.03f, pos2.y + 0.03f );

                        pFig->SetComponent( nComp,     nComp,     3 );
                        pFig->SetComponent( nComp + 1, nComp + 1, 3 );
                        pFig->SetComponent( nComp + 2, nComp + 2, 3 );
                        pFig->SetComponent( nComp + 3, nComp + 3, 3 );
                        nComp += 4;
        */
        pFig->SetIndex(nInd, nComp + 1);
        pFig->SetIndex(nInd + 1, nComp + 2);
        pFig->SetIndex(nInd + 2, nComp + 3);
        nInd += 3;

        nID = pPl->visibleUnitIDs[i];
        pUnit = (CUnit *)pMobMng->Find(nID);
        if (!pUnit || pUnit->IsDead())
        {
            pFig->SetComponent(nComp, 0, 0);
            pFig->SetComponent(nComp + 1, 0, 0);
            pFig->SetComponent(nComp + 2, 0, 0);
            pFig->SetComponent(nComp + 3, 0, 0);
            nComp += 4;
            nPlot++;
            continue;
        }

        pos.x = pUnit->GetCP().x;
        pos.y = pUnit->GetCP().y;

        pos = FromTerrainToFigure(pos, TRUE);
        if (pos.x * pos.x + pos.y * pos.y > 0.56f)
        {
            pFig->SetComponent(nComp, 0, 0);
            pFig->SetComponent(nComp + 1, 0, 0);
            pFig->SetComponent(nComp + 2, 0, 0);
            pFig->SetComponent(nComp + 3, 0, 0);
            nComp += 4;
            nPlot++;
            continue;
        }

        dir.x = -pUnit->GetYAxis().x;
        dir.y = pUnit->GetYAxis().y;

        d = dir.x * dir.x + dir.y * dir.y;
        if (d < 1e-6)
        {
            dir.x = 0;
            dir.y = 1;
        }
        else
        {
            d = float(sqrt(d));
            dir.x = dir.x / d;
            dir.y = dir.y / d;
        }

        pFig->SetPlot(nPlot, 0, pos.x + dir.x * 0.07f, pos.y + dir.y * 0.07f);
        pFig->SetPlot(nPlot, 1, pos.x + dir.x * 0.07f, pos.y + dir.y * 0.07f);

        dir2.x = -0.70710678f * dir.x + 0.70710678f * dir.y;
        dir2.y = -0.70710678f * dir.x - 0.70710678f * dir.y;
        pFig->SetPlot(nPlot, 2, pos.x + dir2.x * 0.04f, pos.y + dir2.y * 0.04f);

        dir2.x = -0.70710678f * dir.x - 0.70710678f * dir.y;
        dir2.y = +0.70710678f * dir.x - 0.70710678f * dir.y;
        pFig->SetPlot(nPlot, 3, pos.x + dir2.x * 0.04f, pos.y + dir2.y * 0.04f);

        int t;
        int nPlayer = IWorldClient.GetPlayer()->GetPlayer()->GetPlayerNumber();
        // CRAP{ supercrap for final zone
        if (pUnit->GetID() == 666666 && IWorldClient.GetCurrentZoneName() == "gz20g")
            t = 2;
        else
            // CRAP}
            if (pUnit->IsEnemy(nPlayer))
                t = 2;
            else
                t = 1;
        for (j = 0; j < pScreen->GetNumUnits(); j++)
            if (pUnit == pScreen->GetUnit(j))
            {
                t = 0;
                break;
            }

        pFig->SetComponent(nComp, nComp, t);
        pFig->SetComponent(nComp + 1, nComp + 1, t);
        pFig->SetComponent(nComp + 2, nComp + 2, t);
        pFig->SetComponent(nComp + 3, nComp + 3, t);
        nComp += 4;
        nPlot++;
    }

    // quest lights
    for (it = qlights.begin(), i = 0; i < numq && it != qlights.end(); i++, ++it)
    {
        pFig->SetIndex(nInd, nComp);
        pFig->SetIndex(nInd + 1, nComp + 2);
        pFig->SetIndex(nInd + 2, nComp + 1);
        pFig->SetIndex(nInd + 3, nComp + 1);
        pFig->SetIndex(nInd + 4, nComp + 2);
        pFig->SetIndex(nInd + 5, nComp + 3);
        nInd += 6;

        CBase3D *pLight = static_cast<CBase3D *>(pLightManager->Find(*it));
        if (!pLight)
        {
            pFig->SetComponent(nComp, 0, 0);
            pFig->SetComponent(nComp + 1, 0, 0);
            pFig->SetComponent(nComp + 2, 0, 0);
            pFig->SetComponent(nComp + 3, 0, 0);
            nComp += 4;
            nPlot++;
            continue;
        }
        pos.x = pLight->GetCP().x;
        pos.y = pLight->GetCP().y;

        CPlot2D pos2 = FromTerrainToFigure(pos, TRUE);
        if (pos2.x * pos2.x + pos2.y * pos2.y > 0.6f)
        {
            pos2 = FromTerrainToFigure(pos, FALSE);
            if (pos2.x * pos2.x + pos2.y * pos2.y < 0.68f || fabs(pos2.x) > 0.97f ||
                fabs(pos2.y) > 0.97f)
            {
                pFig->SetComponent(nComp, 0, 0);
                pFig->SetComponent(nComp + 1, 0, 0);
                pFig->SetComponent(nComp + 2, 0, 0);
                pFig->SetComponent(nComp + 3, 0, 0);
                nComp += 4;
                nPlot++;
                continue;
            }
        }

        pFig->SetPlot(nPlot, 0, pos2.x - 0.03f, pos2.y - 0.03f);
        pFig->SetPlot(nPlot, 1, pos2.x + 0.03f, pos2.y - 0.03f);
        pFig->SetPlot(nPlot, 2, pos2.x - 0.03f, pos2.y + 0.03f);
        pFig->SetPlot(nPlot, 3, pos2.x + 0.03f, pos2.y + 0.03f);

        pFig->SetComponent(nComp, nComp, 3);
        pFig->SetComponent(nComp + 1, nComp + 1, 3);
        pFig->SetComponent(nComp + 2, nComp + 2, 3);
        pFig->SetComponent(nComp + 3, nComp + 3, 3);
        nComp += 4;
        nPlot++;
    }

    // zone exits
    for (i = 0; i < nume; i++)
    {
        pFig->SetIndex(nInd, nComp);
        pFig->SetIndex(nInd + 1, nComp + 2);
        pFig->SetIndex(nInd + 2, nComp + 1);
        pFig->SetIndex(nInd + 3, nComp + 1);
        pFig->SetIndex(nInd + 4, nComp + 2);
        pFig->SetIndex(nInd + 5, nComp + 3);
        nInd += 6;

        if (stricmp(theWorld.GetExitZoneName(i), "none") == 0)
        {
            pFig->SetComponent(nComp, 0, 0);
            pFig->SetComponent(nComp + 1, 0, 0);
            pFig->SetComponent(nComp + 2, 0, 0);
            pFig->SetComponent(nComp + 3, 0, 0);
            nComp += 4;
            nPlot++;
            continue;
        }

        pos = theWorld.GetExitCenter(i);

        CPlot2D pos2 = FromTerrainToFigure(pos, TRUE);
        if (pos2.x * pos2.x + pos2.y * pos2.y > 0.6f)
        {
            pos2 = FromTerrainToFigure(pos, FALSE);
            if (pos2.x * pos2.x + pos2.y * pos2.y < 0.68f || fabs(pos2.x) > 0.97f ||
                fabs(pos2.y) > 0.97f)
            {
                pFig->SetComponent(nComp, 0, 0);
                pFig->SetComponent(nComp + 1, 0, 0);
                pFig->SetComponent(nComp + 2, 0, 0);
                pFig->SetComponent(nComp + 3, 0, 0);
                nComp += 4;
                nPlot++;
                continue;
            }
        }

        pFig->SetPlot(nPlot, 0, pos2.x - 0.03f, pos2.y - 0.03f);
        pFig->SetPlot(nPlot, 1, pos2.x + 0.03f, pos2.y - 0.03f);
        pFig->SetPlot(nPlot, 2, pos2.x - 0.03f, pos2.y + 0.03f);
        pFig->SetPlot(nPlot, 3, pos2.x + 0.03f, pos2.y + 0.03f);

        pFig->SetComponent(nComp, nComp, 1);
        pFig->SetComponent(nComp + 1, nComp + 1, 1);
        pFig->SetComponent(nComp + 2, nComp + 2, 1);
        pFig->SetComponent(nComp + 3, nComp + 3, 1);
        nComp += 4;
        nPlot++;
    }

    // corpses

    // Resizing the marker arrays can move them; publish current streams/counts.
    pFig->MapAfterModify();
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot2D CI3DObjectMap::FromTerrainToFigure(CPlot2D pt, BOOL bZoom)
{
    CPlot2D res;
    res.x = (pt.x - fXCenter) * fTerr2FigScale;
    res.y = -(pt.y - fYCenter) * fTerr2FigScale;
    if (bZoom)
    {
        res.x *= fZoom;
        res.y *= fZoom;
    }
    return res;
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot2D CI3DObjectMap::FromFigureToTerrain(CPlot2D pt, BOOL bZoom)
{
    CPlot2D res = pt;
    if (bZoom)
    {
        res.x /= fZoom;
        res.y /= fZoom;
    }
    res.x = res.x / fTerr2FigScale + fXCenter;
    res.y = -res.y / fTerr2FigScale + fYCenter;
    return res;
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot2D CI3DObjectMap::FromTerrainToMap(CPlot2D pt)
{
    CPlot2D res;
    res.x = pt.x / fMaxSize;
    res.y = pt.y / fMaxSize;
    return res;
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot2D CI3DObjectMap::FromMapToTerrain(CPlot2D pt)
{
    CPlot2D res;
    res.x = pt.x * fMaxSize;
    res.y = pt.y * fMaxSize;
    return res;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectMap::ShowCurrentMode()
{
    WideScreen::Scope wideScope(owned::widescreen::kTopRight);
    for (int i = 0; i < 2; i++)
    {
        if (nMode == i + 1)
            figures[i + 6]->SetAmbient(1, 1, 1);
        else
            figures[i + 6]->SetAmbient(.5f, .5f, .5f);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectMap::InvertClosedState()
{
    WideScreen::Scope wideScope(owned::widescreen::kTopRight);
    pI3D->BeginFX("buttons\\battle\\sling.wav");
    slide_bIsSlide = TRUE;
    slide_dir = -slide_dir;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectMap::NormalizeCamera(bool cycleFallout)
{
    pI3D->BeginFX("buttons\\battle\\click.wav");
    if (cycleFallout) theCamera.ToggleFalloutMode();
    else theCamera.NormalizeNorth();
    fNormTime = 0.5f;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectMap::OnLButtonDown(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    JournalHit(ht);
    if (ht >= 0)
    {
        SetProcessed();
        pI3D->SetCapture(this);
    }
    switch (ht)
    {
    case 3:
        pI3D->BeginFX("buttons\\battle\\click.wav");
        nMode = 1;
        ShowCurrentMode();
        break;
    case 4:
        pI3D->BeginFX("buttons\\battle\\click.wav");
        nMode = 2;
        ShowCurrentMode();
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectMap::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (pI3D->GetCapture() != this)
        return;
    pI3D->ReleaseCapture();
    if (nMode == 0)
    {
        int ht = HitTest(point);
        JournalHit(ht);
        CPlot2D pt;
        CPlot3D pos;
        switch (ht)
        {
        case 5:
            NormalizeCamera(true);
            break;
        case 0:
            InvertClosedState();
            break;
        case 1:
            if(!OwnedRender::E3HudMapOffset(this,point,point))break;
            pt.x = point.x / fPixInMeter;
            pt.y = point.y / fPixInMeter;
            pt = FromFigureToTerrain(pt, TRUE);
            if (pt.x > 0 && pt.x < fXMapSize && pt.y > 0 && pt.y < fYMapSize)
            {
                theCamera.ResetTracking();
                pos = theCamera.GetAnchorPoint();
                pos.x = pt.x;
                pos.y = pt.y;
                theCamera.SetAnchorPoint(pos);
            }
            break;
        case 2:
            if(!OwnedRender::E3HudMapOffset(this,point,point))break;
            pt.x = point.x / fPixInMeter;
            pt.y = point.y / fPixInMeter;
            pt = FromFigureToTerrain(pt);
            if (pt.x > 0 && pt.x < fXMapSize && pt.y > 0 && pt.y < fYMapSize)
            {
                theCamera.ResetTracking();
                pos = theCamera.GetAnchorPoint();
                pos.x = pt.x;
                pos.y = pt.y;
                theCamera.SetAnchorPoint(pos);
            }
            break;
        }
        if (ht >= 0)
            SetProcessed();
    }
    else
    {
        nMode = 0;
        ShowCurrentMode();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectMap::OnMouseMove(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    /*
            if ( nMode > 0 )
            {
                    switch ( ht )
                    {
                    case 3:
                            if ( nMode != 1 )
                            {
                                    nMode = 0;
                                    pI3D->ReleaseCapture();
                            }
                            break;
                    case 4:
                            if ( nMode != 2 )
                            {
                                    nMode = 0;
                                    pI3D->ReleaseCapture();
                            }
                            break;
                    default:
                            nMode = 0;
                            pI3D->ReleaseCapture();
                            break;
                    }
                    ShowCurrentMode();
            }
    */
    if (ht >= 0)
        SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObjectMap::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    int ht = HitTest(pt);
    if ((slide_nCurPixels < slide_nTotalPixels && IsInside(pt)) || ht == 0)
    {
        float zoom = (float)pow(2.5f, .1f * zDelta / WHEEL_DELTA);
        fZoom *= zoom;
        if (fZoom > fMaxZoom)
            fZoom = fMaxZoom;
        if (fZoom < 1.f)
            fZoom = 1.f;
    }
    if (ht >= 0 || (slide_nCurPixels < slide_nTotalPixels && IsInside(pt)))
        SetProcessed();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInfo::Init(int iMode, bool bClosed)
{
    WideScreen::Scope wideScope(owned::widescreen::kTopLeft);
    SetRect(WideScreen::PlacedRect(0, 0, 185, 225));

    slide_bIsSlide = TRUE;
    slide_nCurPixels = bClosed ? 180 : 0;
    slide_nTotalPixels = 180;
    slide_fCurTime = bClosed ? 1 : 0;
    slide_dir = bClosed ? 1 : -1;

    CI3DPolyArea *pArea;
    // close arrow
    pArea = AddArea();
    pArea->MakeRect(0, 0, 20, 30);
    pArea->SetToolTip(10100, 1);
    // modes
    pArea = AddArea();
    pArea->MakeRect(0, 100, 20, 130);
    pArea->SetToolTip(10101, 1, KA_INFO1);
    pArea = AddArea();
    pArea->MakeRect(0, 130, 20, 160);
    pArea->SetToolTip(10102, 1, KA_INFO2);
    pArea = AddArea();
    pArea->MakeRect(0, 160, 20, 190);
    pArea->SetToolTip(10103, 1, KA_INFO3);
    pArea = AddArea();
    pArea->MakeRect(0, 190, 20, 220);
    pArea->SetToolTip(10104, 1, KA_INFO4);
    // all
    AddArea()->MakeRect(0, 0, 185, 225);

    CI3DFigure *pFig;
    // bg
    pFig = AddFigure(); // 0
    pFig->MakeBar(60, 100, 20, 0, 180, 220);
    pFig->SetTexture(NULL);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 80));
    pFig->Show(FALSE);
    // toolbar
    pFig = AddFigure(); // 1
    pFig->MakeBar(20, 100, 0, 0, 20, 220);
    pFig->SetTexture(NULL);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 180));
    // close arrow
    pFig = AddFigure("in25arrow"); // 2
    pFig->SetUShift(57 / 256.f);
    pFig->SetVShift(36 / 256.f);
    pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), FP_PI2));
    pFig->SetRelativeToScreen(-50, 20, 8.106086f);
    pFig->SetScale(.8f);

    pFig = AddFigure(); // 3
    pFig->MakeBar(10, 80, 0, 100, 20, 130, 47, 62, 67, 92);
    pFig = AddFigure(); // 4
    pFig->MakeBar(10, 80, 0, 130, 20, 160, 69, 62, 89, 92);
    pFig = AddFigure(); // 5
    pFig->MakeBar(10, 80, 0, 160, 20, 190, 3, 94, 23, 124);
    pFig = AddFigure(); // 6
    pFig->MakeBar(10, 80, 0, 190, 20, 220, 233, 112, 253, 142);

    pFig = AddFigure(); // 7
    pFig->MakeBarFrame(10, 80, -5, -5, 185, 225, 5, 4, 55, 90, 60);

    pFig = AddFigure(); // 8
    pFig->MakeTilingBar(4, 100 / VIEWING_ANGLE_TAN, 20, 0, 25, 220, 85, 2, 90, 42, 1, 20);
    pFig->Show(FALSE);

    pFig = AddFigure(); // 9
    pFig->MakeBar(4, 100, 164, 204, 180, 220, 211, 2, 227, 18);
    pFig->Show(FALSE);

    pFig = AddFigure(); // health bar
    pFig->MakeBar(4, 100, 40, 190, 140, 195, 64, 154, 164, 159);
    pFig = AddFigure();
    pFig->MakeBar(5, 100, 35, 190, 145, 195, 59, 147, 169, 152);

    pFig = AddFigure(); // stamina bar
    pFig->MakeBar(4, 100, 40, 205, 140, 210, 64, 161, 164, 166);
    pFig = AddFigure();
    pFig->MakeBar(5, 100, 35, 205, 145, 210, 59, 147, 169, 152);

    for (int n = 0; n < 6; n++) // HL bars
    {
        pFig = AddFigure();
        pFig->MakeBar(4, 50, 25 + HL_XS[n], 15 + HL_YS[n], 75 + HL_XS[n], 20 + HL_YS[n], 64, 126,
                      114, 131);
        pFig = AddFigure();
        pFig->MakeBar(5, 50, 20 + HL_XS[n], 15 + HL_YS[n], 80 + HL_XS[n], 20 + HL_YS[n], 59, 119,
                      119, 124);
    }
    for (int n = 0; n < 6; n++)
    {
        pFig = AddFigure();
        pFig->MakeBar(5, 30, 28, 38 + n * 30, 52, 62 + n * 30, 0, 0, 64, 64);
        pFig->SetTexture(textures[UseModInterfaceHD() ? 2 : 1]);
    }

    AddSurface(dbgnew CI3DSurface(pI3D, 20, 0, 180, 220));

    nMode = iMode;
    ShowCurrentMode();
    ScrollFigures();

    pUnit = 0;
    pTempUnit = 0;

    CTextManager *pTxtMng = theWorld.GetTextManager();
    CString szTmp, szID;
    for (int i = 0; i < TOTAL; i++)
    {
        szID.Format("infounit_%d", i);
        szTmp.Empty();
        pTxtMng->GetSystemString(szID, szTmp);
        szStrings.Add(szTmp);
    }
    pTxtMng->GetSystemString("hl_skull", szTmp);
    szHLNames.Add(szTmp);
    pTxtMng->GetSystemString("hl_torso", szTmp);
    szHLNames.Add(szTmp);
    pTxtMng->GetSystemString("hl_leg", szTmp);
    szHLNames.Add(szTmp);
    pTxtMng->GetSystemString("hl_arm", szTmp);
    szHLNames.Add(szTmp);

    for (int i = 0; i < 6; i++)
        nEffectsLoaded[i] = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInfo::ScrollFigures()
{
    WideScreen::Scope wideScope(owned::widescreen::kTopLeft);
    // bg
    figures[0]->SetRelativeToScreen(100 - (float)slide_nCurPixels, 110, 60);
    // toolbar
    figures[1]->SetRelativeToScreen(10 - (float)slide_nCurPixels, 110, 20);

    figures[3]->SetRelativeToScreen(10 - (float)slide_nCurPixels, 115, 10);
    figures[4]->SetRelativeToScreen(10 - (float)slide_nCurPixels, 145, 10);
    figures[5]->SetRelativeToScreen(10 - (float)slide_nCurPixels, 175, 10);
    figures[6]->SetRelativeToScreen(10 - (float)slide_nCurPixels, 205, 10);

    figures[7]->SetRelativeToScreen(90 - (float)slide_nCurPixels, 110, 10);

    figures[9]->SetRelativeToScreen(172 - (float)slide_nCurPixels, 212, 4);

    figures[N_HL_FIGURE - 4]->SetRelativeToScreen(90 - (float)slide_nCurPixels, 192.5f, 4);
    figures[N_HL_FIGURE - 3]->SetRelativeToScreen(90 - (float)slide_nCurPixels, 192.5f, 5);
    figures[N_HL_FIGURE - 2]->SetRelativeToScreen(90 - (float)slide_nCurPixels, 207.5f, 4);
    figures[N_HL_FIGURE - 1]->SetRelativeToScreen(90 - (float)slide_nCurPixels, 207.5f, 5);
    for (int n = 0; n < 6; n++)
    {
        figures[N_HL_FIGURE + n * 2]->SetRelativeToScreen(50 + HL_XS[n] - (float)slide_nCurPixels,
                                                          HL_YS[n] + 17.5f, 4);
        figures[N_HL_FIGURE + n * 2 + 1]->SetRelativeToScreen(50 + HL_XS[n] -
                                                                  (float)slide_nCurPixels,
                                                              HL_YS[n] + 17.5f, 5);
    }
    for (int n = 0; n < 6; n++)
        figures[N_HL_FIGURE + 12 + n]->SetRelativeToScreen(40 - (float)slide_nCurPixels,
                                                           50 + n * 30, 5);

    surfaces[0]->Move(CPoint(20 - slide_nCurPixels, 0));

    for (int i = 1; i < areas.GetSize(); i++)
        GetArea(i)->SetShift(-slide_nCurPixels, 0);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObjectInfo::Draw(CRenderer *pRend)
{
    OwnedRender::E1TextDraw textDraw(OwnedRender::E1TextKind::pane,this,surfaces[0]->GetSurface() ? surfaces[0]->GetSurface()->OwnedSurfaceId() : 0);
    WideScreen::Scope wideScope(owned::widescreen::kTopLeft);
    OwnedRender::E3HudCompositionScope hudBody(this);


    figures[8]->Show(FALSE);
    figures[9]->Show(FALSE);

    figures[0]->Show(TRUE);
    {
        OwnedRender::E3HudCompositionScope background(this,0);
        if(background.visible)figures[0]->Draw(pRend);
    }
    figures[0]->Show(FALSE);

    CUnit *pDrawUnit = 0;
    LPCTSTR pszName = 0;
    float fDistDrawUnit, fAltDrawUnit;
    if (slide_fCurTime < 1)
    {
        pDrawUnit = pTempUnit ? pTempUnit : pUnit;
        pszName = pTempUnit ? szTempUnitName : szUnitName;
        fDistDrawUnit = pTempUnit ? fDistTempUnit : fDistUnit;
        fAltDrawUnit = pTempUnit ? fAltTempUnit : fAltUnit;
        if (pTempUnit == pUnit)
            fDistDrawUnit = fDistUnit;
        if (pDrawUnit)
            fDistDrawUnit *= pDrawUnit->GetHeightComplectionScale();
    }
    for (int i = N_HL_FIGURE - 4; i < N_HL_FIGURE + 18; i++)
        figures[i]->Show(FALSE);

#ifndef NOGETDC
    CDC *pDC = surfaces[0]->GetDC();
    surfaces[0]->Clear();
#endif // NOGETDC

    if (pDrawUnit && pDrawUnit->GetObjectSpecific() && hudBody.visible)
    {
        if (nMode != 2 || (ModRuntime::Current() && ModRuntime::Current()->CombatSetting("JMVShowCharacterModelInStats")))
        {
            const float previewTime=theWorld.GetPreciseTime();
            OwnedRender::ModelPreviewTimeScope clock(owned::ModelPreviewClock::game,previewTime);
            const auto composition=OwnedRender::panel_model_preview(pDrawUnit->GetID(),WideScreen::Layout(),
                slide_nCurPixels,fDistDrawUnit,fAltDrawUnit,DISTANCE_DRAW_UNIT,nMode==0);
            OwnedRender::produce_model_preview_composition(composition,
                [&](auto& rect,bool local){SourcePreviewRect(pI3D,rect,local);},
                [&](const auto* rect,unsigned count,float depth){SourcePreviewDepth(pI3D->GetRenderer(),rect,count,depth);},
                SourcePreviewQuaternion,
                [&]{pDrawUnit->SetExactPosition(previewTime);},
                [&]{pDrawUnit->DrawWithoutBBox(&theRenderer);},[]{});
        }

#ifndef NOGETDC
        DrawName(pszName);

        if (nMode == 0)
        {
            DrawTotalHP(pDrawUnit);

#ifndef DONT_DISPLAY_DEBUG_IDS
            // DEBUG{
            CString szTmp;
            szTmp.Format("%d", pDrawUnit->GetID());
            surfaces[0]->DrawText(szTmp, CRect(5, 160, 155, 180), 0, INFO_COLOR,
                                  DT_CENTER | DT_WORDBREAK);
            // DEBUG}
#endif // DONT_DISPLAY_DEBUG_IDS

            float fCurScale = RULER_UNIT_SCALE / fDistDrawUnit;
            figures[8]->SetYScale(fCurScale);
            figures[8]->SetRelativeToScreen(20 - slide_nCurPixels, 220.f - fCurScale * 220.f, 4);
            figures[8]->Show(TRUE);
        }
        else if (nMode == 1)
        {
            DrawHLState(pDrawUnit);
            DrawTotalHP(pDrawUnit);
        }
        else if (nMode == 2)
        {
            const bool luaStats = ModRuntime::Current() && ModRuntime::Current()->AdvancedUI("useLS_DrawStats");
            const auto resource = OwnedRender::E1Scene().Resource(pDrawUnit);
            if (!luaStats || owned::owned_text_begin_stats(resource.serial, pszName, pI3D->IsAltDown() != FALSE)) {
                DrawStats(pDrawUnit);
                if (luaStats) owned::owned_text_complete_stats();
            }
        }
        else if (nMode == 3)
        {
            PublishSpellEffects(pDrawUnit);
            CRect viewport(0, 35, 160, 215);
            pI3D->VirtualToScreenLocal(viewport);
            const auto resource = OwnedRender::E1Scene().Resource(pDrawUnit);
            const auto stamp = textDraw.Stamp();
            owned::combat_ui_publish_spell_list(pDrawUnit->GetID(), resource.serial,
                stamp.epoch, stamp.frame, viewport);
        }
#endif // NOGETDC


    }
#ifndef NOGETDC
    surfaces[0]->ReleaseDC();
#endif // NOGETDC

    CI3DObject::Draw(pRend);

    textDraw.Complete();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInfo::DrawName(LPCTSTR pszName)
{
    const auto* nameMod=ModRuntime::Current();
    const int inset=ModItemUiPolicy::NameInset(nameMod && nameMod->AdvancedUI("useLS_ShowEnemyConsider"));
    CRect rc(inset, inset, 155, 20);
    COLORREF color = INFO_COLOR;

    // NAME
    surfaces[0]->DrawMultiText(pszName, rc, 2, 0, color, DT_CENTER | DT_WORDBREAK);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInfo::PublishSpellEffects(CUnit *pUnit)
{
    OwnedRender::E3CombatCatalog();
    OwnedRender::E3CombatPublishUnit(pUnit);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInfo::DrawStats(CUnit *pUnit)
{
    auto*mod=ModRuntime::Current();
    if(mod && mod->AdvancedUI("useLS_DrawStats")) {
        CModPackage::InfoStatsScope infoStatsScope(mod);
        static StatsFontCache cache;
        ModStatsFont font(pI3D->GetFontSlot(0),pI3D->GetHeight(),cache);
        auto*text=theWorld.GetTextManager();auto*surface=surfaces[0];
        const auto*stats=pUnit->GetUserUnitStats();
        CUnit*serverUnit=theWorldServer.GetUnit(pUnit->GetID());
        auto*server=serverUnit?dynamic_cast<CUnitServer*>(serverUnit->GetUnitSpecific()):nullptr;
        CUnit&data=serverUnit?*serverUnit:*pUnit;
        CString value,label;CRect row(5,35,155,50);CPoint step(0,12);
        auto draw=[&](const CString&line,COLORREF color=INFO_COLOR){surface->DrawText(line,row,0,color,DT_LEFT);row+=step;};
        auto get=[&](const char*key){CString result;text->GetSystemString(key,result);return result;};
        auto raw=[&](const char*key){CString result;if(text->GetText(key))text->GetLine(result);return result;};
        bool alien=true;for(int i=0;i<pScreen->GetNumUnits();++i)if(pScreen->GetUnit(i)==pUnit){alien=false;break;}
        value.Format("%s %d/%d",static_cast<LPCSTR>(szStrings[HEALTH]),(std::max)(0,pUnit->GetHP()),pUnit->GetMaxHP());draw(value);
        value.Format("%s %d/%d",static_cast<LPCSTR>(szStrings[MANA]),(std::max)(0,pUnit->GetMP()),pUnit->GetMaxMP());draw(value);
        if(!pI3D->IsAltDown()) {
            if(stats->cLocomotion>1&&pUnit->GetShowPose()>1){value.Format("move_%d",int(pUnit->GetShowPose())-1);text->GetSystemString(value,label);}
            value.Format("%s %.1f %s",static_cast<LPCSTR>(szStrings[SPEED]),(alien?pUnit->GetShowSpeed(UP_RUN):pUnit->GetShowSpeed())*FP_CL_MP_SIZE*7.5f,static_cast<LPCSTR>(label));draw(value);
            float night=theWorld.GetNightPowerSlow();
            const bool finalBattle=ModSafetyLookup::FinalBattle(theWorldServer.GetPlayerManager()!=nullptr,
                [](){return theWorldServer.GetPlayer(0);},
                [](CPlayer* player){return player->party.szCurrentZone.CompareNoCase("gz20g")==0;});
            night=ModSafetyLookup::NightPower(night,theWorld.IsCaveZone(),finalBattle);
            value.Format("%s %d%s(%d%%)",static_cast<LPCSTR>(szStrings[SIGHT]),MINT(stats->GetSightRange(night)),static_cast<LPCSTR>(szStrings[METERS]),int((stats->cSenses[RPGS_INFRA]*night*.01f+1-night)*100));draw(value);
            SAttackParams params;data.GetAttackParams(params);
            value.Format("%s %d",static_cast<LPCSTR>(szStrings[ATTACK]),params.nAttack);draw(value);
            value.Format("%s %d",static_cast<LPCSTR>(szStrings[DEFENSE]),params.nDefense);draw(value);
            int selectionCount=0,selectionIndex=-1;auto*selected=pScreen->GetSelection(selectionCount,selectionIndex);
            auto*comparison=selected?theWorldServer.GetUnit(selected->GetID()):nullptr;
            if(serverUnit&&comparison&&serverUnit!=comparison){
                SAttackParams other;comparison->GetAttackParams(other);float incoming=0,outgoing=0;
                float distance=sqrtf(sqr(serverUnit->GetCP().x-comparison->GetCP().x)+sqr(serverUnit->GetCP().y-comparison->GetCP().y)+sqr(serverUnit->GetCP().z-comparison->GetCP().z));
                if(LS_InfoPhysicalHitChance(serverUnit,comparison,params,other,distance,0,incoming)&&
                   LS_InfoPhysicalHitChance(comparison,serverUnit,other,params,distance,1,outgoing)){
                    value.Format("\xCF\xEE\xEF\xE0\xE4\xE0\xED\xE8\xE5 \xEF\xEE \xE2\xF0\xE0\xE3\xF3: %.0f%%",outgoing);draw(value);
                    value.Format("\xCF\xEE\xEF\xE0\xE4\xE0\xED\xE8\xE5 \xEF\xEE \xED\xE0\xEC: %.0f%%",incoming);draw(value);
                }
            }
            auto damage=[&](int slot,bool left,int type,float minimum,float maximum){
                if(type<RPG_NUM_ATTACK_TYPES){value.Format("dmg_%d",type);text->GetSystemString(value,label);}
                float lo=mod->PanelDamage(data,minimum,slot,left),hi=mod->PanelDamage(data,maximum,slot,left);
                float chance=mod->PanelCritical(data,slot,left,false),critical=mod->PanelCritical(data,slot,left,true)*.01f;
                if(fabs(lo-hi)<.0001f){
                    if(chance>0)value.Format("%s %s%.0f[%.0f%%]%.0f",static_cast<LPCSTR>(szStrings[DAMAGE]),static_cast<LPCSTR>(label),lo,chance,lo+lo*critical);
                    else value.Format("%s %s%.0f",static_cast<LPCSTR>(szStrings[DAMAGE]),static_cast<LPCSTR>(label),lo);
                }else {
                    if(chance>0)value.Format("%s %s%.0f-%.0f[%.0f%%]%.0f-%.0f",static_cast<LPCSTR>(szStrings[DAMAGE]),static_cast<LPCSTR>(label),lo,hi,chance,lo+lo*critical,hi+hi*critical);
                    else value.Format("%s %s%.0f-%.0f",static_cast<LPCSTR>(szStrings[DAMAGE]),static_cast<LPCSTR>(label),lo,hi);
                }draw(value);
            };
            if(pUnit->GetTypeID()==MOTID_HUMAN){
                auto*w=data.GetCurrentQWeapon();int slot=int(data.GetCurrentWeaponSlotIndex());
                if(w)damage(slot,false,CItemWeapon::GetAttackType(w->GetSubtypeID()),w->GetDamageMin(),w->GetDamageMax());
                else damage(-1,false,params.rpgAttackType,data.GetUserUnitStats()->cNAWDamageMin,data.GetUserUnitStats()->cNAWDamageMin+data.GetUserUnitStats()->cNAWDamageRnd);
                int left=ModStats::LeftHand(data);auto*off=left>=0?data.GetQWeapon(left):nullptr;
                if(off&&left!=slot&&off->GetWeaponType()!=RPGWST_HANDS1)damage(left,true,CItemWeapon::GetAttackType(off->GetSubtypeID()),off->GetDamageMin(),off->GetDamageMax());
            }else {
                const auto*race=pUnit->GetRaceModel();
                for(int i=0;i<RPG_NUM_ATTACK_TYPES;++i)if(race->attacks[i]!=0)
                    damage(-1,false,i,data.GetUserUnitStats()->cNAWDamageMin,data.GetUserUnitStats()->cNAWDamageMin+data.GetUserUnitStats()->cNAWDamageRnd);
            }
            // Read effective authoritative actions, not a client byte or UI-only recalculation.
            float actions=server?server->GetRPGStats()->action.fpEffectiveValue:float(stats->cAction);
            value.Format("%s %.1f(%d%%)",static_cast<LPCSTR>(szStrings[ACTIONS]),actions,int(actions*100/15));draw(value);
            float absorbs[RPG_NUM_ATTACK_TYPES]={};
            if(IsIDofPersonage(pUnit->GetID()))stats->GetAbsorbtions(&pUnit->GetUserCommonStats(),absorbs,true);
            else for(int i=0;i<RPG_NUM_ATTACK_TYPES;++i)absorbs[i]=pUnit->GetUserCommonStats().wAbsorbtions[i];
            for(int group=0;group<2;++group){CString line;for(int i=0;i<3;++i){int type=group*3+i;CString key;key.Format("dmg_%d",type);value.Format("%s%d",static_cast<LPCSTR>(get(key)),int(absorbs[type]));if(i)line+="|";line+=value;}value.Format("%s %s",static_cast<LPCSTR>(szStrings[ABSORPTION]),static_cast<LPCSTR>(line));draw(value);}
            if(mod->AdvancedUI("useLS_ShowEnemyConsider")&&!pUnit->GetOwner())alien=true;
            if(alien&&pScreen->GetNumUnits()>0){int delta=(std::max)(-25,(std::min)(25,pUnit->GetLevel()-pScreen->GetUnit(0)->GetLevel()));
                value.Format(delta>=0?"consider_more%d":"consider_less%d",abs(delta));text->GetSystemString(value,label);
                value.Format("%s %s",static_cast<LPCSTR>(szStrings[CONSIDER]),static_cast<LPCSTR>(label));
                draw(value,RGB((std::min)(255,delta>0?225+delta*6:0),(std::min)(255,(delta<0?185:215)-delta*6),0));
            }

        }else {
            value.Format("%s: %d",static_cast<LPCSTR>(raw("perk Strength")),stats->cStrength);draw(value);
            value.Format("%s: %d",static_cast<LPCSTR>(raw("perk Dexterity")),stats->cDexterity);draw(value);
            value.Format("%s: %d",static_cast<LPCSTR>(raw("perk Intelligence")),stats->cIntelligence);draw(value);
            float jewelry=1,extra=0;
            for(int i=0;i<4;++i)if(auto*q=data.GetQItem(i))jewelry+=mod->InfoProperty("GetJewelryPropsInside",q->GetShapeEntry()->szName,12)*.01f;
            if(server){const auto&effects=data.GetSpellEffects();for(int i=0;i<effects.GetSize();++i){const auto&e=effects[i];if(e.nSpellID==SPELL_ID_STRENGTHEN)extra+=e.fpPower;if(e.nSpellID==SPELL_ID_WEAKEN)extra-=e.fpPower;}}
            int capacity=int(stats->cStrength*12.f*(1+stats->GetPerkModifier(1)*.01f)*jewelry*(1+mod->InfoComplex(data,8)*.01f)+extra);
            int weight=int(data.GetItemsWeight());
            if(pUnit->GetTypeID()==MOTID_HUMAN){value.Format("%s: %d/%d(%d%%)",static_cast<LPCSTR>(raw("perk Encumbrance")),weight,capacity,capacity>0?int((weight+1)*100.f/capacity):0);draw(value);}
            value.Format("%s: %d",static_cast<LPCSTR>(raw("perk Experience")),alien?int(pUnit->GetPrototype()->fExp):int(stats->dwExperience));draw(value);
            float hearing=1.f,sight=1.f;
            if(server){
                hearing=server->GetFSSoundDetect();sight=server->GetVisionDetect();
                const auto&percepts=server->GetRPGStats()->percepts;
                if(hearing==0.f)hearing=percepts[RPGS_HEARING].fpEffectiveValue;
                if(sight==0.f)sight=percepts[RPGS_SIGHT].fpEffectiveValue;
                const bool recoverHearing=!pUnit->GetOwner()&&percepts[RPGS_HEARING].fpEffectiveValue==1.f;
                const bool recoverSight=!pUnit->GetOwner()&&percepts[RPGS_SIGHT].fpEffectiveValue==1.f;
                if(recoverHearing||recoverSight){float armorSight=0,armorHearing=0;
                    if(mod->NpcArmorPercepts(*serverUnit,pUnit,armorSight,armorHearing)){
                        if(recoverHearing)hearing=(1.f+armorHearing)*server->GetFSSoundDetect();
                        if(recoverSight)sight=(1.f+armorSight)*server->GetVisionDetect();
                    }
                }
            }
            value.Format("%s %.0f%%",static_cast<LPCSTR>(raw("string infoitem_51")),hearing*100);draw(value);
            value.Format("%s %.0f%%",static_cast<LPCSTR>(raw("string infoitem_53")),sight*100);draw(value);
            int count=0;if(auto*player=theWorldServer.GetPlayer(0))count=player->party.allWagonItems.GetSize()+player->items.GetCount();
            value.Format("%s: %d",static_cast<LPCSTR>(raw("camphelp 22")),count);draw(value);
            float block=0,parry=0;mod->PanelDefense(data,weight,capacity,block,parry);
            if(block>0){value.Format("%s %.0f%%",static_cast<LPCSTR>(raw("string infoitem_45")),block);draw(value);}
            value.Format("%s %.0f%%",static_cast<LPCSTR>(raw("string infoitem_47")),parry);draw(value);
        }
        return;
    }

    int i;
    const SRPGUserUnitStats *pStats = pUnit->GetUserUnitStats();

    CTextManager *pTxtMng = theWorld.GetTextManager();
    CI3DSurface *pSurf = surfaces[0];

    CString szTmp, szOut;
    CRect rc(5, 35, 155, 50);
    CRect rc2;
    CPoint shift(0, 15);
    COLORREF color = INFO_COLOR;

    // HEALTH
    szOut.Format("%s %d/%d", static_cast<LPCSTR>(szStrings[HEALTH]), pUnit->GetHP() > 0 ? pUnit->GetHP() : 0,
                 pUnit->GetMaxHP());
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    bool bAlien = true;
    for (i = 0; i < pScreen->GetNumUnits(); i++)
        if (pScreen->GetUnit(i) == pUnit)
        {
            bAlien = false;
            break;
        }

    // MANA
    // if ( !bAlien )
    //{
    szOut.Format("%s %d/%d", static_cast<LPCSTR>(szStrings[MANA]), pUnit->GetMP() > 0 ? pUnit->GetMP() : 0,
                 pUnit->GetMaxMP());
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;
    //}

    // SPEED
    if (pStats->cLocomotion > 1)
    {
        szOut.Format("move_%d", pStats->cLocomotion - 1);
        pTxtMng->GetSystemString(szOut, szTmp);
    }
    else
        szTmp.Empty();
    float fSp;
    if (bAlien)
        fSp = pUnit->GetShowSpeed(UP_RUN);
    else
        fSp = pUnit->GetShowSpeed();
    szOut.Format("%s %.1f %s", static_cast<LPCSTR>(szStrings[SPEED]), fSp * FP_CL_MP_SIZE * 7.5f, static_cast<LPCSTR>(szTmp));
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // SIGHT
    float nightPower = theWorld.GetNightPowerSlow();
    if (theWorld.IsCaveZone())
        nightPower = 1.0f;
    if (IWorldClient.GetCurrentZoneName().CompareNoCase("gz20g") == 0)
        nightPower = 0.0f;
    int nSight = MINT(pStats->GetSightRange(nightPower));
    //
    szOut.Format("%s %d%s", static_cast<LPCSTR>(szStrings[SIGHT]), nSight, static_cast<LPCSTR>(szStrings[METERS]));
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    SAttackParams attackParams;
    pUnit->GetAttackParams(attackParams);

    // ATTACK & DEFENSE
    szOut.Format("%s %d", static_cast<LPCSTR>(szStrings[ATTACK]), attackParams.nAttack);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;
    szOut.Format("%s %d", static_cast<LPCSTR>(szStrings[DEFENSE]), attackParams.nDefense);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // DAMAGE
    if (attackParams.rpgAttackType < RPG_NUM_ATTACK_TYPES)
    {
        szOut.Format("dmg_%d", attackParams.rpgAttackType);
        pTxtMng->GetSystemString(szOut, szTmp);
    }
    szOut.Format("%s %d-%d %s", static_cast<LPCSTR>(szStrings[DAMAGE]), static_cast<int>(attackParams.fDamageMin),
                 static_cast<int>(attackParams.fDamageMax), static_cast<LPCSTR>(szTmp));
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // ACTIONS
    szOut.Format("%s %d", static_cast<LPCSTR>(szStrings[ACTIONS]), pStats->cAction);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // ABSORPTION
    float absorbs[RPG_NUM_ATTACK_TYPES];
    pUnit->GetUserUnitStats()->GetAbsorbtions(&pUnit->GetUserCommonStats(), absorbs,
                                              IsIDofPersonage(pUnit->GetID()));
    //	for ( i=0; i<RPG_NUM_ATTACK_TYPES; i++ )
    //		absorbs[i] = pUnit->GetUserCommonStats().wAbsorbtions[i];

    CStringArray szResArray;
    CI3DObjectItemInfo::FormAbsorptionStrings(absorbs, szResArray);
    szOut = szStrings[ABSORPTION] + " " + szResArray[0];
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;
    if (bAlien)
    {
        if (!szResArray[1].IsEmpty())
        {
            szOut = szStrings[RESIST] + " " + szResArray[1];
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
        }
    }
    else
    {
        if (!szResArray[2].IsEmpty())
        {
            szOut = szStrings[VULNERABLE] + " " + szResArray[2];
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
        }
    }

    // USE MAGIC
    if (bAlien && pUnit->GetPrototype()->szSpells.size() > 0)
    {
        pSurf->DrawText(szStrings[USEMAGIC], rc, 0, color, DT_LEFT);
        rc += shift;
    }

    // CONSIDER
    if (bAlien && pScreen->GetNumUnits() > 0)
    {
        CString szConsider;
        int nDiff = pUnit->GetLevel() - pScreen->GetUnit(0)->GetLevel();
        if (nDiff > 25)
            nDiff = 25;
        if (nDiff < -25)
            nDiff = -25;
        if (nDiff >= 0)
            szTmp.Format("consider_more%d", nDiff);
        else
            szTmp.Format("consider_less%d", -nDiff);
        pTxtMng->GetSystemString(szTmp, szTmp);
        szOut.Format("%s %s", static_cast<LPCSTR>(szStrings[CONSIDER]), static_cast<LPCSTR>(szTmp));
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
    }


}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInfo::DrawTotalHP(CUnit *pUnit)
{
    figures[N_HL_FIGURE - 3]->Show(TRUE);
    CI3DFigure *pFig = figures[N_HL_FIGURE - 4];

    float state = 0;
    if (pUnit->GetMaxHP() != 0 && pUnit->GetHP() > 0)
        state = (float)pUnit->GetHP() / pUnit->GetMaxHP();
    if (state > 1)
        state = 1;
    if (state > 0)
    {
        pFig->Show(TRUE);

        float xc, yc;
        float fMapX = (64 + 100 * state) / 256.f + 1 / 512.f;

        pFig->GetPlot(0, 2, xc, yc);
        pFig->SetPlot(0, 2, state - 0.5f, yc);
        pFig->GetPlot(0, 3, xc, yc);
        pFig->SetPlot(0, 3, state - 0.5f, yc);
        pFig->SetMap(2, fMapX, 1 - 309 / 512.f);
        pFig->SetMap(3, fMapX, 1 - 319 / 512.f);
    }

    CString szTmp;
    szTmp.Format("%d", pUnit->GetHP() > 0 ? pUnit->GetHP() : 0);
    surfaces[0]->DrawText(szTmp, CRect(130, 185, 160, 200), 0, INFO_COLOR, DT_LEFT);

    bool bAlien = true;
    for (int i = 0; i < pScreen->GetNumUnits(); i++)
        if (pScreen->GetUnit(i) == pUnit)
        {
            bAlien = false;
            break;
        }

    const auto *mod = ModRuntime::Current();
    if(mod && !mod->AdvancedUI("useLS_DrawTotalHP"))mod=nullptr;
    if (mod && mod->AdvancedUI("useLS_DrawMonsterMana"))
        bAlien = false;

    if (!bAlien)
    {
        figures[N_HL_FIGURE - 1]->Show(TRUE);
        CI3DFigure *pFig = figures[N_HL_FIGURE - 2];

        state = 0;
        if (pUnit->GetMaxMP() != 0 && pUnit->GetMP() > 0)
            state = (float)pUnit->GetMP() / pUnit->GetMaxMP();
        if (state > 1)
            state = 1;
        if (state > 0)
        {
            pFig->Show(TRUE);

            float xc, yc;
            float fMapX = (64 + 100 * state) / 256.f + 1 / 512.f;

            pFig->GetPlot(0, 2, xc, yc);
            pFig->SetPlot(0, 2, state - 0.5f, yc);
            pFig->GetPlot(0, 3, xc, yc);
            pFig->SetPlot(0, 3, state - 0.5f, yc);
            pFig->SetMap(2, fMapX, 1 - 323 / 512.f);
            pFig->SetMap(3, fMapX, 1 - 333 / 512.f);
        }

        CString szTmp;
        szTmp.Format("%d", pUnit->GetMP() > 0 ? pUnit->GetMP() : 0);
        surfaces[0]->DrawText(szTmp, CRect(130, 200, 160, 215), 0, INFO_COLOR, DT_LEFT);
    }

    if (mod && mod->AdvancedUI("useLS_ShowEnemyConsider") && !pUnit->GetOwner())
        bAlien = true;
    if (mod && bAlien && pScreen->GetNumUnits() > 0)
    {
        int levelDifference = pUnit->GetLevel() - pScreen->GetUnit(0)->GetLevel();
        levelDifference = (std::max)(-25, (std::min)(25, levelDifference));
        CString key, consider;
        key.Format(levelDifference >= 0 ? "consider_more%d" : "consider_less%d", abs(levelDifference));
        theWorld.GetTextManager()->GetSystemString(key, consider);
        int red = levelDifference > 0 ? (std::min)(255, 225 + levelDifference * 6) : 0;
        int green = (std::min)(255, (levelDifference < 0 ? 185 : 215) - levelDifference * 6);
        surfaces[0]->DrawText(consider, CRect(5, 10, 155, 60), 0,
                              RGB(red, green, 0), DT_CENTER);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInfo::DrawHLState(CUnit *pUnit)
{
    const SRPGUserUnitStats *stats = pUnit->GetUserUnitStats();

    for (int n = 0; n < 6; n++)
    {
        int x = HL_XS[n];
        int y = HL_YS[n];
        ERPGHitLocations eHL = (ERPGHitLocations)n;
        const SRPGUserHitLocation *hl = &stats->hls[eHL];
        if (hl->state == RPGALS_NOTUSED)
            continue;
        figures[N_HL_FIGURE + n * 2 + 1]->Show(TRUE);
        CI3DFigure *pFig = figures[N_HL_FIGURE + n * 2];

        float state = 0;
        if (hl->state == RPGALS_ACTIVE && hl->wMaxHP != 0 && static_cast<signed short>(hl->wHP) > 0)
            state = (float)hl->wHP / hl->wMaxHP;

        if (state > 1)
            state = 1;

        if (state > 0)
        {
            pFig->Show(TRUE);

            float xc, yc;
            float fMapX = (64 + 50 * state) / 256.f + 1 / 512.f;

            pFig->GetPlot(0, 2, xc, yc);
            pFig->SetPlot(0, 2, state - 0.5f, yc);
            pFig->GetPlot(0, 3, xc, yc);
            pFig->SetPlot(0, 3, state - 0.5f, yc);
            pFig->SetMap(2, fMapX, 1 - 253 / 512.f);
            pFig->SetMap(3, fMapX, 1 - 263 / 512.f);
        }
        else
            pFig->Show(FALSE);

        if (state > 2 / 3.f)
            pFig->SetVShift(0);
        else if (state > 1 / 3.f)
            pFig->SetVShift(-7 / 256.f);
        else
            pFig->SetVShift(-14 / 256.f);

        CString szTmp;
        // theWorld.GetTextManager()->GetSystemString( CString("hl_") +
        // pUnit->GetRaceModel()->hls[n].szHLType, szTmp );
        szTmp = pUnit->GetRaceModel()->hls[n].szHLType;
        if (szTmp == "skull")
            szTmp = szHLNames[SKULL];
        else if (szTmp == "torso")
            szTmp = szHLNames[TORSO];
        else if (szTmp == "leg")
            szTmp = szHLNames[LEG];
        else if (szTmp == "arm")
            szTmp = szHLNames[ARM];

        if (hl->state == RPGALS_CRIPPLED)
            surfaces[0]->DrawText(szTmp, CRect(x, y, x + 60, y + 15), 0, RGB(255, 0, 0), DT_CENTER);
        else
        {
            surfaces[0]->DrawText(szTmp, CRect(x, y, x + 60, y + 15), 0, INFO_COLOR, DT_CENTER);

            const int currentHP = static_cast<signed short>(hl->wHP) > 0 ? hl->wHP : 0;
            auto *mod = ModRuntime::Current();
            if (mod && mod->AdvancedUI("useLS_DrawHLState"))
            {
                if (GetI3D() && GetI3D()->IsAltDown())
                {
                    const float modifier = mod->HitLocationModifier(int(eHL));
                    if (modifier > 0)
                        szTmp.Format("%d (+%.0f)", currentHP, modifier);
                    else
                        szTmp.Format("%d (%.0f)", currentHP, modifier);
                }
                else
                    szTmp.Format("%d", currentHP);
                surfaces[0]->DrawText(szTmp, CRect(x, y + 20, x + 60, y + 35), 0,
                                      RGB(255, 255, 255), DT_CENTER);
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInfo::ActivateMode(int nNewMode)
{
    WideScreen::Scope wideScope(owned::widescreen::kTopLeft);
    if (slide_dir == 1)
    {
        nMode = nNewMode;
        slide_dir = -1;
        slide_bIsSlide = TRUE;
        pI3D->BeginFX("buttons\\battle\\sling.wav");
    }
    else
    {
        if (nMode != nNewMode)
            nMode = nNewMode;
        else
        {
            slide_bIsSlide = TRUE;
            slide_dir = 1;
            pI3D->BeginFX("buttons\\battle\\sling.wav");
        }
    }
    ShowCurrentMode();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInfo::OnIdle()
{
    WideScreen::Scope wideScope(owned::widescreen::kTopLeft);
    float fPart;

    if (slide_bIsSlide)
    {
        slide_fCurTime += slide_dir * GetFrameTime() / SLIDE_TIME;
        if (slide_fCurTime > 1.f)
            slide_fCurTime = 1.f;
        if (slide_fCurTime < 0.f)
            slide_fCurTime = 0.f;

        if (slide_fCurTime < .5f)
            fPart = 2.f * slide_fCurTime * slide_fCurTime;
        else
            fPart = 1.f - 2.f * (1.f - slide_fCurTime) * (1.f - slide_fCurTime);

        slide_nCurPixels = (int)(fPart * slide_nTotalPixels);

        if (slide_fCurTime == 1.f || slide_fCurTime == 0.f)
            slide_bIsSlide = FALSE;

        ScrollFigures();

        CI3DFigure *pFig = figures[2];
        if (slide_dir == -1)
        {
            pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), FP_PI2));
            pFig->SetRelativeToScreen(-50, 20, 8.106086f);
        }
        else
        {
            pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), -FP_PI2));
            pFig->SetRelativeToScreen(87, 20, 8.106086f);
        }
    }

    if (pTempUnit && pTempUnit->IsDead())
    {
        pTempUnit = 0;
    }
    pI3D->bInfoClosed = slide_dir > 0;
    pI3D->nInfoMode = nMode;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInfo::ShowCurrentMode()
{
    WideScreen::Scope wideScope(owned::widescreen::kTopLeft);
    for (int i = 0; i < 4; i++)
    {
        // figures[i+3]->SetRelativeToScreen( 10 - (float)slide_nCurPixels, 145.f + i * 30, 10 );
        if (nMode == i)
            figures[i + 3]->SetAmbient(1, 1, 1);
        else
            figures[i + 3]->SetAmbient(.5f, .5f, .5f);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInfo::SetTempUnit(CUnit *pTmpUnit)
{
    float fMax = 0;
    if (pTmpUnit != pTempUnit)
    {
        pTempUnit = pTmpUnit;
        if (pTempUnit)
        {
            fDistTempUnit = pTempUnit->GetPrototype()->fInfoWindowScale;
            fAltTempUnit = pTempUnit->GetPrototype()->fAltitude;
            pScreen->GetUnitName(pTempUnit, szTempUnitName);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInfo::SetUnit(CUnit *pNewUnit)
{
    WideScreen::Scope wideScope(owned::widescreen::kTopLeft);
    float fMax = 0;
    if (pNewUnit != pUnit)
    {
        pUnit = pNewUnit;
        if (pUnit)
        {
            fDistUnit = pUnit->GetPrototype()->fInfoWindowScale;
            fAltUnit = pUnit->GetPrototype()->fAltitude;
            pScreen->GetUnitName(pUnit, szUnitName);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInfo::OnLButtonDown(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    JournalHit(ht);
    if (ht >= 0)
    {
        SetProcessed();
        pI3D->SetCapture(this);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInfo::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (pI3D->GetCapture() != this)
        return;
    pI3D->ReleaseCapture();
    int ht = HitTest(point);
    JournalHit(ht);
    switch (ht)
    {
    case 0:
        pI3D->BeginFX("buttons\\battle\\sling.wav");
        slide_bIsSlide = TRUE;
        slide_dir = -slide_dir;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
        pI3D->BeginFX("buttons\\battle\\click.wav");
        nMode = ht - 1;
        ShowCurrentMode();
        break;
    }
    if (ht >= 0)
        SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInfo::OnMouseMove(UINT nFlags, CPoint point)
{
    if (HitTest(point) >= 0)
        SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObjectInfo::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    if (HitTest(pt) >= 0)
        SetProcessed();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
enum class ClockSpeedInput { Init, Button, Keyboard };



static DWORD ClockSegmentTime(int mode, ClockSpeedInput input)
{
    const CModPackage *package = ModRuntime::Current();
    if (package)
    {
        const auto &settings = package->SpellAddon();
        using ModSpellAddon::ByteKey;
        using ModSpellAddon::IntKey;
        if (input == ClockSpeedInput::Init)
        {
            const int rewrite = settings.Mode(ByteKey::speed_rewriteotherspeed);
            if (rewrite == 1 || rewrite == 3)
            {
                // ASI patches AND AL,imm8 and ADD EAX,sign-extended imm8.
                // Keep byte wrapping and the high bytes of the nonzero flag.
                const unsigned normal = settings.Byte(ByteKey::speed_normal2);
                const unsigned fast = settings.Byte(ByteKey::speed_fast2);
                const unsigned mask = (fast - normal) & 255u;
                const DWORD flag = mode ? (0xffffff00u | mask) : 0u;
                return flag + DWORD(normal < 128 ? int(normal) : int(normal) - 256);
            }
        }
        else if (input == ClockSpeedInput::Button && settings.Mode(ByteKey::speed_rewritespeed) == 1)
            return DWORD(settings.Int(mode ? IntKey::speed_fast : IntKey::speed_normal));
        else if (input == ClockSpeedInput::Keyboard && settings.Mode(ByteKey::speed_rewritespeedkey) == 1)
            return DWORD(settings.Int(mode ? IntKey::speed_fastkey : IntKey::speed_normalkey));
    }
    return mode ? DW_DEFAULT_SEGMENT_TIME / 2 : DW_DEFAULT_SEGMENT_TIME;
}

void CI3DObjectClock::Init(int nInitSpeed)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomRight);
    SetRect(WideScreen::PlacedRect(720, 520, 800, 600));

    CI3DFigure *pFig;
    pFig = AddFigure(); // 0
    pFig->MakeCircle(10, 80, 800, 600, 65, 16, 174, 81, 65);

    pFig = AddFigure(); // 1
    pFig->MakeTriangle(8.f, 100, 0, 0, 17, 11, 17, -11, 104, 19, 115, 2, 93, 2);
    /*
            pFig = AddFigure("in25arrow");
            pFig->SetUShift( 57/256.f );
            pFig->SetVShift( 36/256.f );
            pFig->SetRelativeToScreen( 800, 600, 8.106086f );
            pFig->SetScale( .95f );
    */
    for (int i = 0; i < 4; i++) // 2 - 5
    {
        pFig = AddFigure("in25arrow");
        pFig->SetUShift(57 / 256.f);
        pFig->SetVShift(36 / 256.f);
        pFig->SetRelativeToScreen(1000, 1000, 8.206086f);
        pFig->SetScale(.4f);
    }

    pFig = AddFigure(); // 6
    pFig->MakeCircle(8, 80, 800, 600, 32, 16, 174, 81, 32);

    pFig = AddFigure(); // 7
    pFig->MakeCircle(12, 80, 800, 600, 80, 16, 174, 81, 80);

    CI3DPolyArea *pArea;
    for (int i = 0; i < 3; i++)
    {
        pArea = AddArea();
        pArea->AddPoint(800 - int(cos((i + 1) * FP_PI / 6) * 35),
                        600 - int(sin((i + 1) * FP_PI / 6) * 35));
        pArea->AddPoint(800 - int(cos(i * FP_PI / 6) * 35), 600 - int(sin(i * FP_PI / 6) * 35));
        pArea->AddPoint(800 - int(cos(i * FP_PI / 6) * 80), 600 - int(sin(i * FP_PI / 6) * 80));
        pArea->AddPoint(800 - int(cos((i + 1) * FP_PI / 6) * 80),
                        600 - int(sin((i + 1) * FP_PI / 6) * 80));
    }

    areas[0]->SetToolTip(10400, 1, KA_PAUSE);
    {
        areas[1]->SetToolTip(10402, 1, KA_DECELERATE);
        areas[2]->SetToolTip(10403, 1, KA_ACCELERATE);
    }

    pArea = AddArea();
    pArea->AddPoint(800, 600);
    for (int i = 0; i <= 3; i++)
        pArea->AddPoint(800 - int(cos(i * FP_PI / 6) * 34), 600 - int(sin(i * FP_PI / 6) * 34));
    pArea->SetToolTip(10401, 1, KA_OBJECTIVES);

    fIdleTime = 0;

    nMode = nInitSpeed;
    DW_SEGMENT_TIME = ClockSegmentTime(nMode, ClockSpeedInput::Init);
    pI3D->nSpeed = nMode;

    bNewObj = false;

    fAngleSeconds = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectClock::OnIdle()
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomRight);
    pI3D->nSpeed = nMode;
    float fWorldTime = theWorld.GetWorldTime();
    CI3DFigure *pFig = GetFigure(0);
    fIdleTime += GetFrameTime();
    while (fIdleTime >= 1)
        fIdleTime -= 1;
    float fHandleAngle;
    float fAmb = 0.7f + (float)cos(fIdleTime * FP_2PI) * 0.3f;
    if (IWorld.IsMissionCombatPause())
    {
        figures[0]->SetAmbient(1.4f - fAmb, 1.4f - fAmb, 1.4f - fAmb);
        fHandleAngle = FP_PI * 11 / 12;
        figures[1]->Show(fIdleTime > .5f);
    }
    else
    {
        figures[0]->SetAmbient(1, 1, 1);
        if (nMode)
        {
            fHandleAngle = FP_PI * 7 / 12;
            fAngleSeconds += GetFrameTime();
        }
        else
        {
            fHandleAngle = FP_PI * 9 / 12;
            fAngleSeconds += GetFrameTime() / 2;
        }
        figures[1]->Show(TRUE);
    }
    while (fAngleSeconds > FP_PI / 6.f)
        fAngleSeconds -= FP_PI / 6.f;

    figures[1]->SetOrientation(CQuaternion(CPlot3D(0, 0, -1), fHandleAngle));
    figures[1]->SetRelativeToScreen(800 + (float)cos(fHandleAngle) * 73,
                                    600 - (float)sin(fHandleAngle) * 73, 8);
    float angle = (fWorldTime + 15.f) * FP_PI / 12.f;
    pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), angle));

    angle = fAngleSeconds - FP_PI / 12.f;
    for (int i = 0; i < 4; i++)
    {
        pFig = GetFigure(i + 2);
        pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), angle));
        pFig->SetRelativeToScreen(800 + float(sin(angle)) * 30, 600 - float(cos(angle)) * 30,
                                  8.106086f);
        angle -= FP_PI / 6.f;
    }
    if (bNewObj)
        figures[6]->SetAmbient(fAmb, fAmb, fAmb);
    else
        figures[6]->SetAmbient(1, 1, 1);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectClock::OnLButtonDown(UINT nFlags, CPoint point)
{
    if (HitTest(point) >= 0)
    {
        SetProcessed();
        pI3D->SetCapture(this);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectClock::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (pI3D->GetCapture() != this)
        return;
    pI3D->ReleaseCapture();
    int ht = HitTest(point);
    JournalHit(ht);
    switch (ht)
    {
    case 0:
        SetProcessed();
        TogglePause();
        break;
    case 1:
        SetProcessed();

        if (IWorld.IsMissionCombatPause())
            IWorld.MissionCombatPause(false);
        nMode = 0;
        DW_SEGMENT_TIME = ClockSegmentTime(nMode, ClockSpeedInput::Button);
        pI3D->nSpeed = nMode;
        pI3D->BeginFX("buttons\\battle\\clock.wav");
        break;
    case 2:
        SetProcessed();

        if (IWorld.IsMissionCombatPause())
            IWorld.MissionCombatPause(false);
        nMode = 1;
        DW_SEGMENT_TIME = ClockSegmentTime(nMode, ClockSpeedInput::Button);
        pI3D->nSpeed = nMode;
        pI3D->BeginFX("buttons\\battle\\clock.wav");
        break;
    case 3:
        SetProcessed();
        ViewObjectives();
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectClock::TogglePause()
{
    pI3D->BeginFX("buttons\\battle\\clock.wav");
    IWorld.MissionCombatPause(!IWorld.IsMissionCombatPause());
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectClock::ViewObjectives()
{
    bNewObj = false;
    pI3D->BeginFX("buttons\\battle\\click.wav");
    CI3DScreenObjectives *pScr = dbgnew CI3DScreenObjectives(pI3D);
    pScr->SetParams(IWorldClient.GetCurrentZoneName());
    pI3D->DoModal(pScr);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectClock::NewObjectives()
{
    bNewObj = true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectClock::SetSpeedMode(int mode, bool fromKeyboard)
{
    pI3D->BeginFX("buttons\\battle\\clock.wav");
    nMode = mode;
    DW_SEGMENT_TIME = ClockSegmentTime(mode, fromKeyboard ? ClockSpeedInput::Keyboard : ClockSpeedInput::Button);
    pI3D->nSpeed = nMode;
}
void CI3DObjectClock::AccelerateFromKeyboard()
{
    if (IWorld.IsMissionCombatPause())
        return;
    SetSpeedMode(1, true);
}
void CI3DObjectClock::DecelerateFromKeyboard()
{
    if (IWorld.IsMissionCombatPause())
        return;
    SetSpeedMode(0, true);
}
void CI3DObjectClock::Accelerate()
{
    if (IWorld.IsMissionCombatPause())
        return;
    SetSpeedMode(1);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectClock::Decelerate()
{
    if (IWorld.IsMissionCombatPause())
        return;
    SetSpeedMode(0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectClock::OnMouseMove(UINT nFlags, CPoint point)
{
    if (HitTest(point) >= 0)
        SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectCommands::Init()
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    SetRect(WideScreen::PlacedRect(0, 520, 80, 600));

    CI3DPolyArea *pArea;
    pArea = AddArea();
    pArea->AddPoint(0, 600);
    for (int i = 4; i >= 0; i--)
        pArea->AddPoint(int(cos(i * FP_PI / 8) * 34), 600 - int(sin(i * FP_PI / 8) * 34));
    pArea->SetToolTip(10500, 1, KA_SWARM);

    for (int i = 0; i < 4; i++)
    {
        pArea = AddArea();
        pArea->AddPoint(int(cos(i * FP_PI / 8) * 35), 600 - int(sin(i * FP_PI / 8) * 35));
        pArea->AddPoint(int(cos((i + 1) * FP_PI / 8) * 35),
                        600 - int(sin((i + 1) * FP_PI / 8) * 35));
        pArea->AddPoint(int(cos((i + 1) * FP_PI / 8) * 80),
                        600 - int(sin((i + 1) * FP_PI / 8) * 80));
        pArea->AddPoint(int(cos(i * FP_PI / 8) * 80), 600 - int(sin(i * FP_PI / 8) * 80));
        pArea->SetToolTip(10501 + i, 1, KA_POSE_LAY + i);
    }

    CI3DFigure *pFig;

    pFig = AddFigure();
    pFig->MakeTriangle(8.f, 100, 0, 0, 17, 11, 17, -11, 104, 19, 115, 2, 93, 2);
    /*
            pFig = AddFigure("in25arrow");
            pFig->SetUShift( 57/256.f );
            pFig->SetVShift( 36/256.f );
            pFig->SetRelativeToScreen( 7, 593, 8.106086f );
            pFig->SetScale( .95f );
            pFig->SetOrientation( CQuaternion( CPlot3D(0,0,1), FP_PI ) );
    */
    for (int i = 0; i < 4; i++)
    {
        pFig = AddFigure();
        pFig->MakeTriangle(10, 100, 0, 600, (float)cos(i * FP_PI / 8) * 80,
                           600 - (float)sin(i * FP_PI / 8) * 80,
                           (float)cos((i + 1) * FP_PI / 8) * 80,
                           600 - (float)sin((i + 1) * FP_PI / 8) * 80, 2, 254,
                           2 + (float)cos(i * FP_PI / 8) * 80, 254 - (float)sin(i * FP_PI / 8) * 80,
                           2 + (float)cos((i + 1) * FP_PI / 8) * 80,
                           254 - (float)sin((i + 1) * FP_PI / 8) * 80);
        pFig->SetAmbient(.7f, .7f, .7f);
    }
    pFig = AddFigure(); // 5
    pFig->MakeCircle(8, 80, 0, 600, 32, 16, 2, 254, 32);
    /*
            for ( i=0; i<4; i++ )
            {
                    pFig = AddFigure();
                    pFig->MakeTriangle( 8.f, 100,
                            0, 600,
                            (float)cos(i*FP_PI/8)*32, 600 - (float)sin(i*FP_PI/8)*32,
                            (float)cos((i+1)*FP_PI/8)*32, 600 - (float)sin((i+1)*FP_PI/8)*32,
                            2, 254,
                            2 + (float)cos(i*FP_PI/8)*32, 254 - (float)sin(i*FP_PI/8)*32,
                            2 + (float)cos((i+1)*FP_PI/8)*32, 254 - (float)sin((i+1)*FP_PI/8)*32 );
            }
    */
    SetMode(-1);
    SetAggr(-1);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectCommands::OnIdle()
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    if (!theWorld.GetPlayer())
        return;

    int nPose = -1;
    int nAggression = -1;
    for (int i = 0; i < pScreen->GetNumUnits(); i++)
    {
        CUnit *pUnit = pScreen->GetUnit(i);
        if (pUnit->CheckFlag(OBJ_SELECTED))
        {
            if (nPose == -1)
                nPose = pUnit->GetShowPose();
            else if (nPose != pUnit->GetShowPose())
                nPose = -2;

            if (nAggression == -1)
                nAggression = pUnit->GetAggression();
            else if (nAggression != pUnit->GetAggression())
                nAggression = -2;
        }
    }
    SetMode(nPose);
    SetAggr(nAggression);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectCommands::OnLButtonDown(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    JournalHit(ht);
    if (ht >= 0)
    {
        SetProcessed();
        pI3D->SetCapture(this);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectCommands::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (pI3D->GetCapture() != this)
        return;
    pI3D->ReleaseCapture();
    int ht = HitTest(point);
    JournalHit(ht);
    if (ht == -1)
        return;
    SetProcessed();
    if (ht == 0)
        Swarm();
    else
        ChangePose(ht - 1);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectCommands::OnMouseMove(UINT nFlags, CPoint point)
{
    if (HitTest(point) >= 0)
        SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectCommands::ChangePose(int nPose)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    int i;
    if (!theWorld.GetPlayer())
        return;
    if (nPose >= 0 && nPose < 4)
    {
        pI3D->BeginFX("buttons\\battle\\click.wav");
        for (i = 0; i < pScreen->GetNumUnits(); i++)
            if (pScreen->GetUnit(i)->CheckFlag(OBJ_SELECTED))
            {
                theWorld.GetPlayer()->UnitCommandPose(pScreen->GetUnit(i), EUnitPose(nPose));
                // pScreen->PlayAcknowlegment( pScreen->GetUnit(i)->GetID(),
                // ACK_HERO_ACK_CHANGE_POSE, 0 );
            }
    }
    SetMode(nPose);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectCommands::Swarm()
{
    pI3D->BeginFX("buttons\\battle\\click.wav");
    int nAggression = -1;
    for (int i = 0; i < pScreen->GetNumUnits(); i++)
    {
        CUnit *pUnit = pScreen->GetUnit(i);
        if (pUnit->CheckFlag(OBJ_SELECTED))
        {
            if (nAggression == -1)
                nAggression = pUnit->GetAggression();
            else if (nAggression != pUnit->GetAggression())
                nAggression = -2;
        }
    }
    EUnitAggression eAggr;
    if ((EUnitAggression)nAggression == PAS_AGGRESSIVE)
        eAggr = PAS_STEALTH;
    else
        eAggr = PAS_AGGRESSIVE;

    for (int i = 0; i < pScreen->GetNumUnits(); i++)
        if (pScreen->GetUnit(i)->CheckFlag(OBJ_SELECTED))
            theWorld.GetPlayer()->UnitCommandAggression(pScreen->GetUnit(i), eAggr);

    SetAggr(eAggr);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectCommands::SetMode(int iMode)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    nMode = iMode;
    if (nMode < 0)
        figures[0]->Show(FALSE);
    else
        figures[0]->Show(TRUE);
    for (int i = 0; i < 4; i++)
        if (nMode == i)
        {
            float fHandleAngle = FP_PI * (1 + nMode * 2) / 16;
            figures[0]->SetOrientation(CQuaternion(CPlot3D(0, 0, -1), fHandleAngle));
            figures[0]->SetRelativeToScreen((float)cos(fHandleAngle) * 73,
                                            600 - (float)sin(fHandleAngle) * 73, 8);
            figures[i + 1]->SetAmbient(1, 1, 1);
        }
        else
        {
            figures[i + 1]->SetAmbient(.7f, .7f, .7f);
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectCommands::SetAggr(int iAggr)
{
    WideScreen::Scope wideScope(owned::widescreen::kBottomLeft);
    nAggr = iAggr;
    if ((EUnitAggression)nAggr == PAS_STEALTH)
        figures[5]->SetUShift(86.f / 256);
    else if ((EUnitAggression)nAggr == PAS_AGGRESSIVE)
        figures[5]->SetUShift(0);
    else
        figures[5]->SetUShift(123.f / 256);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectOutput::Init(int iMode, bool bClosed)
{
    ownedMessageLayout=false;

    slide_bIsSlide = TRUE;
    slide_nCurPixels = bClosed ? 95 : 0;
    slide_nTotalPixels = 95;
    slide_fCurTime = bClosed ? 1 : 0;
    slide_dir = bClosed ? 1 : -1;

    CI3DPolyArea *pArea;
    // close arrow
    pArea = AddArea();
    pArea->MakeRect(385, 0, 415, 20);
    pArea->SetToolTip(10200, 1);
    // modes
    pArea = AddArea();
    pArea->MakeRect(185, 0, 215, 20);
    pArea->SetToolTip(10201, 1, KA_TEXT1);
    pArea = AddArea();
    pArea->MakeRect(215, 0, 245, 20);
    pArea->SetToolTip(10202, 1, KA_TEXT2);
    // all
    AddArea()->MakeRect(180, 0, 620, 100);

    CI3DFigure *pFig;
    // bg
    pFig = AddFigure();
    // Background geometry is supplied by the renderer lifecycle.
    pFig->SetTexture(NULL);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 80));
    // toolbar
    pFig = AddFigure();
    // Toolbar geometry is supplied by the renderer lifecycle.
    pFig->SetTexture(NULL);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 180));
    // close arrow
    pFig = AddFigure("in25arrow");
    pFig->SetUShift(57 / 256.f);
    pFig->SetVShift(36 / 256.f);
    pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), FP_PI));
    pFig->SetRelativeToScreen(400, -52, 8.106086f);
    pFig->SetScale(.8f);
    // hoover
    pFig = AddFigure();
    pFig->MakeBar(10, 80, 185, 0, 215, 20, 2, 128, 32, 148);
    // text
    pFig = AddFigure();
    pFig->MakeBar(10, 80, 215, 0, 245, 20, 2, 150, 32, 170);

    pFig = AddFigure();
    // The frame is emitted by the renderer primitive pass.

    nMode = iMode;
    ScrollFigures();
    ShowCurrentMode();
    // Like StockOutputMessages, bind only after native panel initialization.
    if (ModRuntime::Current()) ModMessages::queue.Bind(this);
    /*
            for ( int j=0; j<6; j++ )
                    figures[j]->Show( FALSE );
    */
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectOutput::ScrollFigures()
{
    OwnedRender::E3RefreshMessages(this);
}
// Output geometry and child layout are owned by the renderer.
void CI3DObjectOutput::ActivateMode(int nNewMode)
{
    if (slide_dir == 1)
    {
        nMode = nNewMode;
        slide_dir = -1;
        slide_bIsSlide = TRUE;
        pI3D->BeginFX("buttons\\battle\\sling.wav");
    }
    else
    {
        if (nMode != nNewMode)
            nMode = nNewMode;
        else
        {
            slide_bIsSlide = TRUE;
            slide_dir = 1;
            pI3D->BeginFX("buttons\\battle\\sling.wav");
        }
    }
    ShowCurrentMode();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectOutput::OnIdle()
{
    auto messages=ModMessages::queue.Take(this);
    for(const auto&message:messages)AddOutput(message.c_str());
    float fPart;

    if (slide_bIsSlide)
    {
        slide_fCurTime += slide_dir * GetFrameTime() / SLIDE_TIME;
        if (slide_fCurTime > 1.f)
            slide_fCurTime = 1.f;
        if (slide_fCurTime < 0.f)
            slide_fCurTime = 0.f;

        if (slide_fCurTime < .5f)
            fPart = 2.f * slide_fCurTime * slide_fCurTime;
        else
            fPart = 1.f - 2.f * (1.f - slide_fCurTime) * (1.f - slide_fCurTime);

        slide_nCurPixels = (int)(fPart * slide_nTotalPixels);

        if (slide_fCurTime == 1.f || slide_fCurTime == 0.f)
            slide_bIsSlide = FALSE;

    }
    ScrollFigures();
    pI3D->bTextClosed = slide_dir > 0;
    pI3D->nTextMode = nMode;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectOutput::OnLButtonDown(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    JournalHit(ht);
    if (ht >= 0)
    {
        SetProcessed();
        pI3D->SetCapture(this);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectOutput::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (pI3D->GetCapture() != this)
        return;
    pI3D->ReleaseCapture();
    int ht = HitTest(point);
    JournalHit(ht);
    switch (ht)
    {
    case 0:
        pI3D->BeginFX("buttons\\battle\\sling.wav");
        slide_bIsSlide = TRUE;
        slide_dir = -slide_dir;
        break;
    case 1:
        nMode = 0;
        ShowCurrentMode();
        pI3D->BeginFX("buttons\\battle\\click.wav");
        break;
    case 2:
        nMode = 1;
        ShowCurrentMode();
        pI3D->BeginFX("buttons\\battle\\click.wav");
        break;
    }
    if (ht >= 0)
        SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectOutput::OnMouseMove(UINT nFlags, CPoint point)
{
    if (HitTest(point) >= 0)
        SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObjectOutput::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    if (HitTest(pt) >= 0)
        SetProcessed();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectOutput::ShowCurrentMode()
{
    for (int i = 0; i < 2; i++)
    {
        if (nMode == i)
            figures[i + 3]->SetAmbient(1, 1, 1);
        else
            figures[i + 3]->SetAmbient(.5f, .5f, .5f);
    }
    if (nMode == 0)
    {
        ((CI3DScrollText *)pScreen->GetObject(9))->SetText(szOutputText);
        ((CI3DScrollText *)pScreen->GetObject(9))->SetPosToEnd();
    }
    else
    {
        ((CI3DScrollText *)pScreen->GetObject(9))->SetText(szHooverText);
        ((CI3DScrollText *)pScreen->GetObject(9))->SetPos(0);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectOutput::SetOutputRect(CRect rc)
{
    OwnedRender::E3RefreshMessages(this);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectOutput::ClearOutput()
{
    szOutputText.Empty();
    if (nMode == 0)
        ((CI3DScrollText *)pScreen->GetObject(9))->SetText("");
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectOutput::RestoreOutput(LPCTSTR text)
{
    AddOutput(text);
}
void CI3DObjectOutput::AddOutput(LPCTSTR pszText)
{
    szOutputText += pszText;
    if (nMode == 0)
        ((CI3DScrollText*)pScreen->GetObject(9))->AddText(pszText);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectOutput::AddHoover(CItem *pNewItem, bool bCrippled)
{
    CTextManager *pTxtMng = theWorld.GetTextManager();

    CString szName, szTmp;
    pTxtMng->GetExtendedItemName(pNewItem, szName);
    if (bCrippled)
        pTxtMng->GetSystemString("combat_crippled", szTmp);
    else
        pTxtMng->GetSystemString("combat_picked_up", szTmp);
    szName = szTmp + " " + szName + "\n";
    AddOutput(szName);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectOutput::AddHoover(int nArrivedBucks)
{
    if (nArrivedBucks <= 0)
        return;
    CTextManager *pTxtMng = theWorld.GetTextManager();

    CString szName, szTmp, szTmp2;
    pTxtMng->GetSystemString("combat_picked_up", szTmp);
    pTxtMng->GetSystemString("format_money", szTmp2);
    szName.Format(szTmp2, nArrivedBucks);
    szName = szTmp + " " + szName + "\n";
    AddOutput(szName);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectOutput::UpdateHoover()
{
    CTextManager *pTxtMng = theWorld.GetTextManager();

    CItem *pItem, *pItem2;
    CList<CItem *, CItem *> items, *pItems = &IWorldClient.GetPlayer()->GetPlayer()->items;
    CString szName, szTmp;

    // form items list (stacked)
    POSITION pos = pItems->GetHeadPosition();
    for (pos = pItems->GetHeadPosition(); pos;)
    {
        pItem = pItems->GetNext(pos);

        bool bFound = false;
        for (POSITION pos2 = items.GetHeadPosition(); pos2;)
        {
            pItem2 = items.GetNext(pos2);
            if (pItem2->Compare(pItem))
            {
                bFound = true;
                pItem2->SetCount(pItem2->GetCount() + pItem->GetCount());
                break;
            }
        }
        if (!bFound)
        {
            pItem2 = theWorld.DuplicateItem(pItem);
            pItem2->SetCount(pItem->GetCount());
            pItem2->AddRef();
            items.AddTail(pItem2);
        }
    }

    // form text
    szHooverText.Empty();
    if (IWorldClient.GetParty().nMoney > 0)
    {
        pTxtMng->GetSystemString("format_money", szTmp);
        szName.Format(szTmp, IWorldClient.GetParty().nMoney);
        szHooverText += szName;
        szHooverText += "\n";
    }
    for (pos = items.GetHeadPosition(); pos;)
    {
        pItem = items.GetNext(pos);
        pTxtMng->GetExtendedItemName(pItem, szName);
        szHooverText += szName;
        szHooverText += "\n";
        pItem->Release();
    }
    items.RemoveAll();
    // update
    if (nMode == 1)
    {
        ((CI3DScrollText *)pScreen->GetObject(9))->SetText(szHooverText);
        ((CI3DScrollText *)pScreen->GetObject(9))->SetPos(0);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectOutput::SetMoney(int nBucks)
{
    UpdateHoover();
}
/////////////////////////////////////////////////////////////////////////////////////
const int CI3DObjectOutput::GetMoney()
{
    return IWorldClient.GetParty().nMoney;
}
/////////////////////////////////////////////////////////////////////////////////////

#include "GamepadMission64.inl"
