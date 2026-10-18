#include "StdAfx.h"
#include "GameJournal.h"
#include "ModRuntime.h"
#include "ModSpellRules.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
/*==========================================================================================================

        Client specific player class implementation

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

#include "I3DMain.h"
#include "WorldInterface.h"
#include "WorldClient.h"
#include "PlayerClient.h"
#include "Unit.h"
#include "PlayerManager.h"
#include "MapObjectManager.h"
#include "Object3DClientSpecific.h"
#include "UnitClientSpecific.h"
#include "Effect.h"
#include "EffectManager.h"
#include "Net.h"
#include "Camera.h"
#include "ItemManager.h"
#include "MusicSystem.h"
#include "Particle.h"
#include "ParticleManager.h"
#include "Console.h"
#include "ChatServer.h"
#include "GameMain.h"

#include <unordered_set>
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// ----------------------------- Create/destroy
// ------------------------------------------------------------
CPlayerClient::CPlayerClient()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif
    Clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CPlayerClient::~CPlayerClient()
{
    Clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::Create()
{
    Clear();
    // Create parsers table
    parsers.SetSize(256);
    memset(parsers.GetData(), 0, parsers.GetSize() * sizeof(parsers[0]));
    parsers[PID_LOGGED] = &CPlayerClient::PrsLogged;
    parsers[PID_NEWZONE] = &CPlayerClient::PrsNewZone;
    parsers[PID_MOVECAMERA] = &CPlayerClient::PrsMoveCamera;
    parsers[PID_MOVECAMERA2] = &CPlayerClient::PrsMoveCamera2;
    parsers[PID_CREATE_MAPOBJ] = &CPlayerClient::PrsCreateMapobj;
    parsers[PID_UPDATE_MAPOBJ] = &CPlayerClient::PrsUpdateMapobj;
    parsers[PID_REMOVE_MAPOBJ] = &CPlayerClient::PrsRemoveMapobj;
    parsers[PID_CREATE_EFFECT] = &CPlayerClient::PrsCreateEffect;
    parsers[PID_UPDATE_EFFECT] = &CPlayerClient::PrsUpdateEffect;
    parsers[PID_REMOVE_EFFECT] = &CPlayerClient::PrsRemoveEffect;
    parsers[PID_ACTIONFAILURE] = &CPlayerClient::PrsFailedAction;
    parsers[PID_EVENT] = &CPlayerClient::PrsEvent;
    parsers[PID_STRINGEVENT] = &CPlayerClient::PrsStringEvent;
    parsers[PID_VISIBLE_UNITS] = &CPlayerClient::PrsVisibleUnits;
    parsers[PID_WEATHER] = &CPlayerClient::PrsWeather;
    parsers[PID_WORLDTIME] = &CPlayerClient::PrsWorldTime;
    parsers[PID_WIND] = &CPlayerClient::PrsWind;
    parsers[PID_NEWSPELLITEMS] = &CPlayerClient::PrsNewItems;
    parsers[PID_ADDSPELLITEMS] = &CPlayerClient::PrsAddItems;
    parsers[PID_ADDITEMSTOHOOVER] = &CPlayerClient::PrsNewHooverItems;
    parsers[PID_REMOVEITEM] = &CPlayerClient::PrsRemoveItem;
    parsers[PID_SETVAR] = &CPlayerClient::PrsGSSetVar;
    parsers[PID_DELVAR] = &CPlayerClient::PrsGSDelVar;
    parsers[PID_PARTY] = &CPlayerClient::PrsParty;
    parsers[PID_PARTYITEMS] = &CPlayerClient::PrsPartyItems;
    parsers[PID_NEWINTERFACE] = &CPlayerClient::PrsNewInterface;
    parsers[PID_SERVERCHANGE] = &CPlayerClient::PrsServerChange;
    parsers[PID_GAMEOVER] = &CPlayerClient::PrsGameOver;
    parsers[PID_ITEMSMANA] = &CPlayerClient::PrsItemsMana;
    parsers[PID_PLAYMUSIC] = &CPlayerClient::PrsMusic;
    parsers[PID_SHOWPATH] = &CPlayerClient::PrsShowPath;
    parsers[PID_UNIT_DEATH] = &CPlayerClient::PrsUnitDeath;
    parsers[PID_WATERLEVELS] = &CPlayerClient::PrsWaterLevels;
    parsers[PID_LOGOUT] = &CPlayerClient::PrsLogout;
    parsers[PID_ADDAITEM] = &CPlayerClient::PrsActiveItem;
    parsers[PID_GAINEXP] = &CPlayerClient::PrsGainExp;
    parsers[PID_REMOVEWEAPON] = &CPlayerClient::PrsRemoveWeapon;
    parsers[PID_REMOVEHOOVER] = &CPlayerClient::PrsRemoveHoover;
    parsers[PID_PERSGETEXP] = &CPlayerClient::PrsUnitGainExp;
    parsers[PID_SELECTUNIT] = &CPlayerClient::PrsSelect;

    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerClient::Destroy()
{
    Clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerClient::Clear()
{
    status = CPS_WAITING_FOR_CONNECTION;
    visibleUnitIDs.RemoveAll();
    pkt.SetSize(0);
    wpkt.SetSize(0);
    bIgnoreInterfaceChange = false;
    bSendParty = false;
    packerSend.Clear();
    packerRecv.Clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerClient::RemoveAllObjects()
{
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerClient::NewConnection()
{
    Clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerClient::OnDisconnect()
{
    // wpkt.Write( PID_LOGOUT );
    // SendUpdate();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// ------------------------------------- Segment
// -----------------------------------------------------------
int CPlayerClient::Segment()
{
    int rv;
    CConnection *pConnection = pPlayer->GetConnection();
    switch (status)
    {
    case CPS_WAITING_FOR_CONNECTION:
        if (pConnection && pConnection->CanSend())
        {
            if (bSendParty)
            {
                wpkt.Write(PID_CLIENT_PARTY);
                pPlayer->party.Store(wpkt);
            }
            SNP_LOGIN loginInfo;
            loginInfo.dwClientVersion = DW_REQUIRED_CLIENT_VERSION;
            strcpy(loginInfo.pszUser, "Tango");
            strcpy(loginInfo.pszPassword, "Very top secret");
            loginInfo.partyID = pPlayer->partyID;
            wpkt.Write(PID_LOGIN);
            wpkt.Write(&loginInfo, sizeof(loginInfo));
            status = CPS_WAITING_FOR_LOGIN_REPLY;
        }
        else
            return TRUE;
        break;
    case CPS_WAITING_FOR_LOGIN_REPLY:
        return TRUE;
        break;
    case CPS_ACTIVE: {
        CPlot3D pos;
        pos = theWorld.GetCamera()->GetObservationPosition();
        struct SNP_CAMERAPOS camPos;
        camPos.fpX = pos.x;
        camPos.fpY = pos.y;
        wpkt.Write(PID_CAMERAPOSITION);
        wpkt.Write(&camPos, sizeof(camPos));
    }
    break;
    case CPS_CAMP:
        break;
    }
    rv = SendUpdate();
    return rv;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::SendUpdate()
{
    BOOL rv;
    CConnection *pConnection = pPlayer->GetConnection();
    if (!pConnection)
        return TRUE;
    if (pConnection->CanSend())
    {
        CPacket packed;
        wpkt.Pack(packerSend, packed);
        if (pPlayer->bShowTraffic)
            NConsole::net << "client to server, size = " << wpkt.GetSize()
                          << " squeezed size = " << packed.GetSize() << endl;
        rv = pConnection->Send(packed);
    }
    else
        rv = FALSE;
    wpkt.SetSize(0);
    return rv;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// ----------------------------------- Packet parsing
// ------------------------------------------------------
BOOL CPlayerClient::ParseCurrentPkt()
{
    PACKET_ID packetID, prevPacketID;
    pkt.Seek(0);
    pkt.SetOk();
    packetID = -1;
    prevPacketID = -1;
    while (1)
    {
        if (!pkt.IsOk())
            return ReportProtocolError("Pkt read error prevID=%x curID=%x\n", prevPacketID,
                                       packetID);
        prevPacketID = packetID;
        if (!pkt.Read(&packetID, 1))
            break;
        if (packetID < 0 || packetID >= parsers.GetSize())
            return ReportProtocolError("Wrong packet ID prevID=%x curID=%x\n", prevPacketID,
                                       packetID);
        if (parsers[packetID] == NULL)
            return ReportProtocolError("Packet has no handler prevID=%x curID=%x\n", prevPacketID,
                                       packetID);
        if ((this->*parsers[packetID])() == FALSE)
            return ReportProtocolError("Error during parsing update prevID=%x curID=%x\n",
                                       prevPacketID, packetID);
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::ParseIncomingPackets()
{
    CConnection *pConnection = pPlayer->GetConnection();
    assert(pConnection);
    if (!pConnection)
        return FALSE;
    if (pConnection->HasPacket())
    {
        CPacket packed;
        if (!pConnection->Recv(packed))
            return FALSE;
        packed.Unpack(packerRecv, pkt);
        if (!ParseCurrentPkt())
            return FALSE;
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerClient::AppendPacket(CPacket &frame)
{
    int nPos = pkt.GetPosition();
    pkt.Seek(pkt.GetSize());
    pkt.Append(frame);
    pkt.Seek(nPos);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerClient::UnitHasChangedOwner(CUnit *pUnit)
{
    if (pUnit->GetOwner())
        theInterface.AddControlUnit(pUnit);
    else
        theInterface.RemoveControlUnit(pUnit);
    if (pChatClient->IsValid() && pPlayer->party.people.GetSize() == 1 &&
        pPlayer->party.people[0].nID == pUnit->GetID())
    {
        pChatClient->pSelf->nServerID = pUnit->GetOwner() ? pUnit->GetID() : -1;
        pChatClient->pSelf->SetUpdated();
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsMoveCamera()
{
    BOOL bRv;
    CPlot3D plot;
    float fAngle;
    EInterface eIFace;
    bRv = pkt.Read(&plot, sizeof(plot));
    bRv = pkt.Read(&fAngle, sizeof(fAngle));
    bRv = pkt.Read(&eIFace, sizeof(eIFace));
    // if ( !theWorld.CheckFlag( WC_CANT_SCROLL_CAMERA ) )
    theWorld.CenterOnLocation(plot);
    if (pPlayer->party.szCurrentZone == "gz20g")
    {
        SCameraShortcut pos;
        pos.fDistance = 23.f;
        pos.fPitch = ToRadian(30.f);
        theCamera.SetCameraAs(pos);
    }
    else if (eIFace == IFACE_BASE)
        theCamera.ActivateShortcut(6);
    else
        theCamera.ActivateShortcut(7);
    theCamera.InitRotate(fAngle);
    if (!theCamera.RestoreFieldCampCamera())
        theCamera.ReapplyFalloutAfterMove();
    return bRv;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsMoveCamera2()
{
    bool bLock;
    CPlot3D ptSrc, ptDst;
    pkt.Read(&ptSrc, sizeof(ptSrc));
    pkt.Read(&ptDst, sizeof(ptDst));
    pkt.Read(&bLock, sizeof(bLock));
    theCamera.MoveTo(ptSrc, ptDst, 0);
    theCamera.LockCameraControl(bLock);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CString CPlayerClient::ReadMsg()
{
    return pkt.ReadString();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// -------------------------------- Login/logout
// -----------------------------------------------------------
BOOL CPlayerClient::PrsLogged()
{
    SCP_LOGGED pktLogged;
    pkt.Read(&pktLogged, sizeof(pktLogged));
    status = CPS_ACTIVE;
    // Register player at player managers with new ID
    ((CPlayerManager *)pPlayer->GetManager())->ChangeID(pPlayer, pktLogged.nPlayerID);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsLogout()
{
    pPlayer->Disconnect();
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsFailedAction()
{
    SCP_ACTION_FAILURE failure;
    pkt.Read(&failure, sizeof(failure));
    theInterface.ActionFailure(failure.dwUnitID, failure.dwAction);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsEvent()
{
    SCP_EVENT pktEvent;
    pkt.Read(&pktEvent, sizeof(pktEvent));
    // player number = pktEvent.bPlayerNum
    GetWorldClient()->ReceiveEvent(pktEvent.dwEventID);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsStringEvent()
{
    CString msg = ReadMsg();
    theInterface.StringEvent(msg);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// ---------------------------------- Map objects
// ----------------------------------------------------------
static void BindParticle(CMapObject *pMO, ETypeIDs tID, CParticle::EBindPlace bindTo, float fpSize)
{
    CParticle *pP;
    pP = ((CParticleManager *)theWorld.GetParticleManager())->CreateParticleSystem(tID);
    pP->AttachCarrier(pMO);
    pP->SetScale(fpSize);
    pP->BindTo(bindTo);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsCreateMapobj()
{
    SCP_CREATE_MAPOBJ mapobjCreate;
    SComplection complection;
    CStringArray szNames;
    CString szUnitName, szAttachedQuest;
    DWORD dwVoiceIdx;

    pkt.Read(&mapobjCreate, sizeof(mapobjCreate));
    pkt.Read(&complection, sizeof(complection));
    pkt.Read(szAttachedQuest);
    if (!pkt.IsOk())
        return FALSE;

    char pszTemplate[256], pszPrimTexture[256], pszSecTexture[256];
    pkt.ReadString(pszTemplate, sizeof(pszTemplate));
    pkt.ReadString(pszPrimTexture, sizeof(pszPrimTexture));
    pkt.ReadString(pszSecTexture, sizeof(pszSecTexture));

    WORD wNamesSize;
    pkt.Read(wNamesSize);
    szNames.SetSize(wNamesSize);
    for (WORD q = 0; q < wNamesSize; q++)
    {
        char pszTemp[256];
        pkt.ReadString(pszTemp, sizeof(pszTemp));
        szNames[q] = pszTemp;
    }
    szUnitName = pkt.ReadString();
    pkt.Read(dwVoiceIdx);

    // Create new map object
    CMapObject *pMO = (CMapObject *)((CMapObjectManager *)GetWorldClient()->GetMapObjectManager())
                          ->CreateData(mapobjCreate.typeID, mapobjCreate.mapobjID);
    if (!pMO)
        return FALSE;
    pMO->SetMaskName(pszTemplate);

    BOOL bIsUnit = pMO->WhoAmI() == HPC_UNIT;
    pMO->SetSubtypeID(mapobjCreate.dwSubtype);
    pMO->SetComplection(complection);
    pMO->SetBaseComplection(complection);
    pMO->RestoreComplection();
    pMO->SetAttachedQuest(szAttachedQuest);
    if (bIsUnit)
    {
        DWORD dwRaceModel = theUnitDatabase.prototypes[mapobjCreate.dwSubtype]->dwRaceIndex;
        const SUDBRaceModel *pRace = theUnitDatabase.races[dwRaceModel];
        float fpCenterShift = pRace->fpCenterShift;
        pMO->SetCenterShift(CPlot3D(0, fpCenterShift, 0));
        ((CUnit *)pMO)->SetRaceAnimationCoeffs(pRace->fAnimationCoeffs);
        // set default voice prototype
        ((CUnit *)pMO)->SetVoicePrototype(dwVoiceIdx);
    }

    if (mapobjCreate.dwFlags & OBJ_NOSHADOW)
        pMO->SetFlag(OBJ_NOSHADOW);
    // CRAP set 'UNIT_OLDROTATE' for main and escape menu columns
    if ((strcmp(pszTemplate, "unmoco1") == 0) || (strcmp(pszTemplate, "unmoco2") == 0))
        pMO->SetFlag(UNIT_OLDROTATE);
    // get mask name and load figures
    // CRAP !
    if (pMO->GetTypeID() == MOTID_HUMAN) // preload all data
    {
        // CRAP - old hair
        if ((strcmp(pszTemplate, "unhuma") == 0) || (strcmp(pszTemplate, "unhufe") == 0))
        {
            for (int i = 0; i < szNames.GetSize(); ++i)
            {
                if (szNames[i] == "hr")
                {
                    DWORD dwHair = theUnitDatabase.prototypes[mapobjCreate.dwSubtype]->dwHair;
                    if (int(dwHair) == -1)
                    {
                        szNames.RemoveAt(i);
                        i--;
                    }
                    else
                        szNames[i].Format("hr.%.2d", dwHair);
                    break;
                }
            }
        }
        // CRAP end
        if (!pMO->LoadByTemplate(pszTemplate, szNames, TRUE))
            return FALSE;
    }
    else
    {
        // end of CRAP !
        if (!pMO->LoadByTemplate(pszTemplate, szNames, TRUE))
            return FALSE;
    }

    // CRAP - correct bodyparts for goblin (mask name == "unmogo")
    if (strcmp(pszTemplate, "unmogo") == 0)
    {
        CFigure *pFigure = pMO->FindFigure("rh3.axeth00");
        if (pFigure)
            pMO->RemoveFigure(pFigure);
        pFigure = pMO->FindFigure("rh3.sword00");
        if (pFigure)
            pMO->RemoveFigure(pFigure);
        const SUDBUnitPrototype *pUnitPrototype = theUnitDatabase.prototypes[pMO->GetSubtypeID()];
        if (pUnitPrototype->rpgWeaponType == RPGWST_BOW)
            pFigure = pMO->FindFigure("rh3.pike00");
        else
            pFigure = pMO->FindFigure("sling");
        if (pFigure)
            pMO->RemoveFigure(pFigure);
    }
    // CRAP ends

    if (strcmp(pszTemplate, "unmowi") == 0)
    {
        BindParticle(pMO, PTID_BALLOFSTARS, CParticle::bpCast, 0.05f);
    }
    if (!bIsUnit)
    {
        pMO->SetFlagsToAllFigures(FIG_STATIC);
        pMO->SetFlag(OBJ_STATIC);
    }
    CObject3DClientSpecific *lpObject3DSpecific = ((CObject3DClientSpecific *)
                                                       pMO->GetObjectSpecific());
    lpObject3DSpecific->SetMaterials();
    // get start animation name and load it
    theWorld.BeginWaitCursor();

    pMO->LoadAnimation(pszTemplate, NULL);
    pMO->InitAnimation();
    //
    if (pMO->GetTypeID() == MOTID_HUMAN) // preload all data
    {
        // NOTE: humans have assembled textures => compose and set their (assembled textures) names
        sprintf(pszPrimTexture, "mo%dp", pMO->GetID());
        sprintf(pszSecTexture, "mo%ds", pMO->GetID());
        lpObject3DSpecific->LoadTextures(pszPrimTexture, pszSecTexture);
    }
    else
    {
        if (strcmp(pszSecTexture, "none"))
            lpObject3DSpecific->LoadTextures(pszPrimTexture, pszSecTexture);
        else
            lpObject3DSpecific->LoadTextures(pszPrimTexture, NULL);
    }
    if (bIsUnit)
        ((CUnit *)pMO)->SetName(szUnitName);
    theWorld.EndWaitCursor();

    // Register map object
    pPlayer->visibleObjects.SetAt(mapobjCreate.mapobjID, pMO);
    pMO->AddRef();

    if (pMO->WhoAmI() == HPC_UNIT)
        pMO->LoadDefaultClientStats(static_cast<CUnit *>(pMO)->GetPrototype()->szName);
    else
        pMO->LoadDefaultClientStats(pszTemplate);
    pMO->UpdateInit();
    if (!pMO->UpdateFrom(utStart, pkt))
        return FALSE;
    // Add object to world
    theWorld.AddObject(pMO);
    if (pMO->GetAttachedQuest()[0] != 0)
        pMO->SetQuestVarValue(
            pPlayer->party.vars.GetVar(theWorld.GetFullQuestName(pMO->GetAttachedQuest())));

    if (pPlayer->curInterface == IFACE_BASE && pMO->WhoAmI() == HPC_UNIT)
        static_cast<CUnitClientSpecific *>(((CUnit *)pMO)->GetUnitSpecific())
            ->LoadMouthMimicTexture();

    // moshka test
    // if ( bIsUnit && ((CUnit*)pMO)->GetOwner() )
    //	theWorld.CreateMoshka();

    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsUpdateMapobj()
{
    SCP_UPDATE_MAPOBJ updMapobj;
    pkt.Read(&updMapobj, sizeof(updMapobj));

    CMapObject *pMO;
    if (!pPlayer->visibleObjects.Lookup(updMapobj.mapobjID, pMO))
        return FALSE;

    // Update unit
    if (!pMO->UpdateFrom(utFrame, pkt))
        return FALSE;

    // Special case - bridges, they should be updated in structures containing information on them
    if (pMO->WhoAmI() == HPC_BUILDING || pMO->WhoAmI() == HPC_LEVER)
        ((CWorldClient *)GetWorld())->UpdateBridgeInformation(pMO);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsRemoveMapobj()
{
    SCP_REMOVE_MAPOBJ remMapobj;
    pkt.Read(&remMapobj, sizeof(remMapobj));

    // Remove object from controlled hash
    CUnit *pUnit;
    if (pPlayer->unitsUnderControl.Lookup(remMapobj.mapobjID, pUnit))
        pUnit->SetOwner(NULL);
    // Remove object from visible hash
    CMapObject *pMO;
    if (!pPlayer->visibleObjects.Lookup(remMapobj.mapobjID, pMO))
    {
        ASSERT(0 && "Removing invisible unit");
        return FALSE;
    }

    pPlayer->visibleObjects.RemoveAt(remMapobj.mapobjID);
    pMO->Invalidate();
    pMO->Release();

    // Remove object from world
    theWorld.RemoveObject(pMO->GetID());

    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// ------------------------------------- Effects
// -----------------------------------------------------------
BOOL CPlayerClient::PrsCreateEffect()
{
    SCP_CREATE_EFFECT effCreate;
    pkt.Read(&effCreate, sizeof(effCreate));

    CEffect *pEff = (CEffect *)((CEffectManager *)GetWorldClient()->GetEffectManager())
                        ->CreateData(ETypeIDs(effCreate.classID), effCreate.effectID);
    if (!pEff)
        return FALSE;
    if (!pEff->UpdateFrom(utStart, pkt))
        return FALSE;

    pPlayer->visibleEffects.SetAt(effCreate.effectID, pEff);
    pEff->AddRef();

    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsUpdateEffect()
{
    SCP_UPDATE_EFFECT updEffect;
    pkt.Read(&updEffect, sizeof(updEffect));

    CEffect *pEff;
    if (!pPlayer->visibleEffects.Lookup(updEffect.effectID, pEff))
        return FALSE;

    if (!pEff->UpdateFrom(utFrame, pkt))
        return FALSE;

    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsRemoveEffect()
{
    SCP_REMOVE_EFFECT remEffect;
    pkt.Read(&remEffect, sizeof(remEffect));

    CEffect *pEff;
    if (!pPlayer->visibleEffects.Lookup(remEffect.effectID, pEff))
        return FALSE;

    pPlayer->visibleEffects.RemoveAt(remEffect.effectID);
    pEff->Invalidate();
    pEff->Release();

    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
static std::unordered_set<int> oldVisibleUnitIDs;
BOOL CPlayerClient::PrsVisibleUnits()
{
    char cPause;
    int i, nSize, nID, nLookFromID;
    EWorldDangerState dangerState;
    // remove visible flags from units
    for (i = 0; i < visibleUnitIDs.GetSize(); i++)
    {
        oldVisibleUnitIDs.insert(visibleUnitIDs[i]);
        CUnit *pUnit = GetWorldClient()->GetUnit(visibleUnitIDs[i]);
        if (pUnit)
            pUnit->ResetFlag(UNIT_VISIBLE);
    }
    // receive pause state
    pkt.Read(cPause);
    GetWorldClient()->SetCombatPause(cPause != 0);
    // receive map object to bind camera on
    pkt.Read(nLookFromID);
    // receive visible units list
    pkt.Read(nSize);
    visibleUnitIDs.SetSize(nSize); // ARRAY BOUNDS CHECK
    for (i = 0; i < nSize; i++)
    {
        pkt.Read(nID);
        visibleUnitIDs[i] = nID;
        if (CUnit *pUnit = GetWorldClient()->GetUnit(nID))
        {
            pUnit->SetFlag(UNIT_VISIBLE);
            // fade out new coming visible units
            if (oldVisibleUnitIDs.find(nID) == oldVisibleUnitIDs.end())
            {
                if ((GetPlayer()->curInterface == IFACE_MISSION) && (pUnit->GetOwner() == 0) &&
                    (theWorld.GetFirstSegmentsCounter() >= 1))
                    pUnit->GetUnitSpecific()->StartFadeOut();
            }
        }
    }
    oldVisibleUnitIDs.clear();
    pkt.Read(&dangerState, sizeof(dangerState));
    GetWorldClient()->SetDangerState(dangerState);
    pkt.Read(nSize);
    LMPCorpsesIDs.SetSize(nSize);
    for (i = 0; i < nSize; i++)
        pkt.Read(LMPCorpsesIDs[i]);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsNewZone()
{
    CMemorySave packet;
    if (!pkt.ReadPacket(packet))
        return FALSE;
    // removing everything from client side
    pPlayer->RemoveAllObjects();

    // loading information about new zone from received packet
    theWorld.NewZone(packet);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsNewItems()
{
    CMemorySave packet;
    if (!pkt.ReadPacket(packet))
        return FALSE;
    // loading information about new items
    theWorld.NewItems(packet);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsAddItems()
{
    CMemorySave packet;
    if (!pkt.ReadPacket(packet))
        return FALSE;

    // loading information about new items
    theWorld.AddItems(packet);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsRemoveItem()
{
    int nID;
    pkt.Read(nID);
    BOOL bOk = pPlayer->RemoveItemByID(nID);
    ASSERT(bOk);
    // notify interface that items set has changed
    CList<CItem *, CItem *> newItems;
    theInterface.NewHooveredItems(newItems, 0, false);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsRemoveHoover()
{
    pPlayer->RemoveAllHoover();
    // notify interface that items set has changed
    CList<CItem *, CItem *> newItems;
    theInterface.NewHooveredItems(newItems, 0, false);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsRemoveWeapon()
{
    int nUnitID;
    int nWeaponIdx;
    pkt.Read(nUnitID);
    pkt.Read(nWeaponIdx);
    CPersonage *pPers = pPlayer->party.GetPersonage(nUnitID);
    if (pPers && pPers->weapons.GetSize() > nWeaponIdx)
        pPers->RemoveWeapon(pPers->weapons[nWeaponIdx]);
    else
        ASSERT(0);
    CUnit *pUnit = theWorld.GetUnit(nUnitID);
    if (pUnit)
        pUnit->ChangeWeapon(-1);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsNewHooverItems()
{
    int nID, nOldMoney;
    bool bCrippled;
    CItem *pItem;
    CList<CItem *, CItem *> newItems;
    nOldMoney = pPlayer->party.nMoney;
    pkt.Read(pPlayer->party.nMoney);
    pkt.Read(&bCrippled, 1);
    for (;;)
    {
        pkt.Read(nID);
        if (nID == -1)
            break;
        if (!pkt.IsOk())
            return FALSE;
        pItem = (CItem *)theWorld.GetItemManager()->Find(nID);
        ASSERT(pItem);
        if (pItem)
            newItems.AddHead(pItem);
    }
    pPlayer->AddItemsToHoover(newItems, bCrippled);
    theInterface.NewHooveredItems(newItems, pPlayer->party.nMoney - nOldMoney, bCrippled);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsItemsMana()
{
    int nID;
    for (;;)
    {
        pkt.Read(nID);
        if (nID == -1)
            break;
        if (!pkt.IsOk())
            return FALSE;
        CItem *pItem = (CItem *)theWorld.GetItemManager()->Find(nID);
        ASSERT(pItem);
        if (pItem)
        {
            float fpMP;
            pkt.Read(fpMP);
            pItem->SetMP(fpMP);
        }
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// ------------------------------- Traffic storing
// ---------------------------------------------------
void CPlayerClient::StartTrafficStoring(const char *pszFilename)
{
    CConnection *pConnection = pPlayer->GetConnection();
    ASSERT(pConnection);
    if (!pConnection)
        return;
    wpkt.Write(PID_RECONNECT);
    /*	pConnection->Send( wpkt );
            wpkt.SetSize( 0 );*/
    // status = SPS_NONE;
    pConnection->StartTrafficSaving(pszFilename);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerClient::StopTrafficStoring()
{
    CConnection *pConnection = pPlayer->GetConnection();
    ASSERT(pConnection);
    if (!pConnection)
        return;
    pConnection->StopTrafficSaving();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// -------------------------------------- Weather
// ----------------------------------------------------------
BOOL CPlayerClient::PrsWeather()
{
    SCP_WEATHER weather;
    pkt.Read(&weather, sizeof(weather));
    theWorld.SetWeather(EWeather(weather.bWeather), weather.nTime);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsWorldTime()
{
    SCP_WORLDTIME time;
    pkt.Read(&time, sizeof(time));
    theWorld.SetWorldTimeScale(time.fpTimeScale);
    theWorld.SetWorldTime(time.fpTimeMod);
    theWorld.AdjustLightingWithTime();

    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsWind()
{
    CPlot3D ptWind;
    float fpStrength;
    pkt.Read(&ptWind, sizeof(ptWind));
    pkt.Read(fpStrength);
    theWorld.SetClientWind(ptWind, fpStrength);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsGSSetVar()
{
    GameJournal::Source journalSource("network");
    CString szName;
    float fpVal;
    pkt.Read(szName);
    pkt.Read(fpVal);
    float fpValOld = pPlayer->party.vars.GetVar(szName);
    pPlayer->party.vars.SetVar(szName, fpVal);
    if (szName.GetLength() > 2 && strncmp(szName, "q.", 2) == 0)
    {
        theInterface.QuestUpdate(szName, fpVal);
        theWorld.UpdateQuestState(szName, fpVal);
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsGSDelVar()
{
    GameJournal::Source journalSource("network");
    CString szName;
    pkt.Read(szName);
    pPlayer->party.vars.DelVar(szName);
    if (szName.GetLength() > 2 && strncmp(szName, "q.", 2) == 0)
    {
        theInterface.QuestUpdate(szName, 0);
        theWorld.UpdateQuestState(szName, 0);
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsParty()
{
    CGSVariableSet holdVars = pPlayer->party.vars;
    CMemorySave file;
    pkt.Read(file);
    file.Seek(0);
    if (pkt.IsOk())
    {
        pPlayer->party.Load(file);
        // refresh active items set
        pPlayer->activeItems.RemoveAll();
        CItemIterator i;
        CItem *pItem = i.GetFirst(pPlayer->party.GetItemManager());
        while (pItem)
        {
            if (pItem->GetMP() < pItem->GetMaxMP())
                pPlayer->activeItems.AddTail(pItem->GetID());
            pItem = i.GetNext();
        }
    }
    if (IWorldConnector.IsLMP())
        pPlayer->party.vars = holdVars;
    theWorld.RecheckAllQuestStates();
    NConsole::net << "party received on client" << endl;
    IWorldConnector.LMPClientRecvParty();
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsPartyItems()
{
    CMemorySave file;
    pkt.Read(file);
    file.Seek(0);
    if (pkt.IsOk())
        pPlayer->party.LoadItems(file);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsNewInterface()
{
    pPlayer->prevInterface = pPlayer->curInterface;
    int nInterface, nSegmentCounter;
    EInterface iface;
    pkt.Read(nInterface);
    pkt.Read(nSegmentCounter);

    if (bIgnoreInterfaceChange)
    {
        theInterface.OnKeyDown(0xffffffff, 13, 0);
        return TRUE;
    }
    iface = (EInterface)nInterface;
    pPlayer->curInterface = iface;
    theInterface.ActivateScreen(iface);
    theWorld.SetSegmentTimeCounter(nSegmentCounter);
    if (iface == IFACE_BASE)
        theWorld.LoadHeroesMimic();

    CUnitPtrHashIterator unitIter;
    for (CUnit *pUnit = unitIter.GetFirst(&pPlayer->unitsUnderControl); pUnit;
         pUnit = unitIter.GetNext())
        UnitHasChangedOwner(pUnit);

    if (IWorldConnector.IsLMP())
        IWorldConnector.LMPSyncBriefings();
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsServerChange()
{
    CString msg = ReadMsg();
    int nEntrance;
    pkt.Read(nEntrance);
    IWorldConnector.ReqMoveToZone(msg, nEntrance);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsGameOver()
{
    theInterface.GameOver();
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsMusic()
{
    CString szMusic = pkt.ReadString();
    theWorld.GetMusicSystem()->BeginScriptMusic(szMusic);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsShowPath()
{
    int nID;
    float fpLeng;
    CParticle *pParticle1 = 0, *pParticle2 = 0;
    CUpdateSingle update;
    CUpdateHistory *pPkt;
    CPlayer::EPathType type;
    CPlayer::EPathResult res;

    pkt.Read(&type, sizeof(type));
    pkt.Read(&res, sizeof(res));
    CPlot3D ptCenter(0, 0, 0);
    pkt.Read(ptCenter.x);
    pkt.Read(ptCenter.y);
    pkt.Read(nID);
    pkt.Read(fpLeng);
    //
    if (type != CPlayer::PPT_NOPATH)
    {
        CUnit *pFake = (CUnit *)theWorld.GetMapObjectManager()->CreateData(MOTID_HUMAN);
        pFake->AddRef();
        CAIFullRoute &route = *pFake->GetUnitSpecific()->GetRoute();
        pPkt = update.StartUpdateTo(utStart);
        route.UpdateTo(*pPkt);
        pPkt->FinishUpdate();
        pPkt = update.StartUpdateFrom(utStart, pkt);
        route.UpdateFrom(*pPkt);
        pPkt->FinishUpdate();
        //
        if (theSettings.nDrawPath)
        {
            // create path effect
            pFake->LowSetRouteSpeeds(FP_SHOW_PATH_SPEED, 1e10f);
            //
            pParticle1 = ((CParticleManager *)theWorld.GetParticleManager())
                             ->CreateParticleSystem(PTID_PATH);
            pParticle1->AttachCarrier(pFake);
            pParticle1->SetPower2(fpLeng);
            switch (type)
            {
            case CPlayer::PPT_MOVE:
                pParticle1->SetPower(1);
                break;
            case CPlayer::PPT_USE:
                pParticle1->SetPower(0);
                break;
            case CPlayer::PPT_ATTACK:
                pParticle1->SetPower(2);
                break;
            }
            pParticle1->Segment();
            pParticle1->AttachCarrier(0);
        }
        pFake->Release();
    }
    if (theSettings.nDrawPath == 0)
        return TRUE;
    // create destination effect
    ETypeIDs partType;
    partType = res == CPlayer::PPR_OK ? PTID_PATHDESTINATION : PTID_PATHFAILED;
    pParticle2 = ((CParticleManager *)theWorld.GetParticleManager())
                     ->CreateParticleSystem(partType);
    pParticle2->SetCP(ptCenter);
    //
    CUnit *pUnit = GetWorldClient()->GetUnit(nID);
    if (pUnit)
    {
        CUnitClientSpecific *pClient = (CUnitClientSpecific *)pUnit->GetUnitSpecific();
        pClient->SetPathParticles(pParticle1, pParticle2);
    }
    //
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsUnitDeath()
{
    int nID;
    pkt.Read(nID);
    // here some actions could be made
    CMapObject *pMO = (CMapObject *)theWorld.GetMapObjectManager()->Find(nID);
    if (pMO && (pMO->WhoAmI() == HPC_UNIT)) // 'последний путь' for the dead units
        ((CUnit *)pMO)->GetUnitSpecific()->Dead();
    //
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsWaterLevels()
{
    theWorld.NewWaterLevels(pkt);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsActiveItem()
{
    int nID;
    float fpMana;
    pkt.Read(nID);
    pkt.Read(fpMana);
    CItem *pItem = (CItem *)pPlayer->party.GetItemManager()->Find(nID);
    ASSERT(pItem);
    if (pItem)
    {
        pItem->SetMP(fpMana);
        pPlayer->AddActiveItem(pItem);
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsGainExp()
{
    float fpExp;
    pkt.Read(fpExp);
    // и полетели полетели полетели
    CUnitPtrHashIterator unitIter;
    for (CUnit *pUnit = unitIter.GetFirst(&pPlayer->unitsUnderControl); pUnit;
         pUnit = unitIter.GetNext())
    {
        float fpExpM = pUnit->GetUserUnitStats()->GetExpMultiplier() * fpExp;
        theWorld.ReceiveDamage(pUnit->GetID(), ModRuntime::Current()?ModSpells::DisplayExperience(fpExpM):Float2Int(fpExpM), HLDAMAGE_EXPERIENCE);
    }

    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsUnitGainExp()
{
    int nID;
    float fpExp;
    pkt.Read(nID);
    pkt.Read(fpExp);
    CUnit *pUnit = theWorld.GetUnit(nID);
    if (pUnit)
    {
        float fpExpM = pUnit->GetUserUnitStats()->GetExpMultiplier() * fpExp;
        theWorld.ReceiveDamage(nID, ModRuntime::Current()?ModSpells::DisplayExperience(fpExpM):Float2Int(fpExpM), HLDAMAGE_EXPERIENCE);
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::PrsSelect()
{
    EPCSelectCmd cmd = (EPCSelectCmd)0;
    int nID;
    pkt.Read(&cmd, 1);
    pkt.Read(nID);
    CUnit *pU = theWorld.GetUnit(nID);
    switch (cmd)
    {
    case PSEL_SELECT:
        if (pU)
            theWorld.SelectObject(pU);
        break;
    case PSEL_PRESELECT_TRUE:
        if (pU)
            pU->Preselect(TRUE);
        break;
    case PSEL_PRESELECT_FALSE:
        if (pU)
            pU->Preselect(FALSE);
        break;
    case PSEL_RESETALL:
        theWorld.ResetSelection();
        break;
    default:
        ASSERT(0);
        return FALSE;
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// -------------------------------------- Commands
// ---------------------------------------------------------
static void WriteUnitArray(CPacket &wpkt, CArray<CUnit *, CUnit *> &units)
{
    wpkt.Write(units.GetSize());
    for (int i = 0; i < units.GetSize(); i++)
    {
        CUnit *pUnit = units[i];
        ASSERT(pUnit);
        if (pUnit)
        {
            GameJournal::Event("CMD", "member", "id=%d prototype=%s player=%d", pUnit->GetID(), GameJournal::Quote(pUnit->GetPrototype() ? (const char*)pUnit->GetPrototype()->szName : "unknown").c_str(), pUnit->GetPlayerNum());
            wpkt.Write(pUnit->GetID());
        }
        else
            wpkt.Write(-1);
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::UnitCommandSwarm(CArray<CUnit *, CUnit *> &units, CPlot3D &ptTarget, BOOL bRun)
{
    GameJournal::Event("CMD", "swarm", "units=%d x=%g y=%g run=%d", units.GetSize(), ptTarget.x, ptTarget.y, bRun);
    wpkt.Write(PID_CMD_SWARM);
    WriteUnitArray(wpkt, units);
    wpkt.Write(bRun);
    wpkt.Write(&ptTarget, sizeof(ptTarget));
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::UnitCommandMoveTo(CArray<CUnit *, CUnit *> &units, CPlot3D &ptTarget, BOOL bRun)
{
    GameJournal::Event("CMD", "move", "units=%d x=%g y=%g run=%d", units.GetSize(), ptTarget.x, ptTarget.y, bRun);
    wpkt.Write(PID_CMD_MOVETO);
    WriteUnitArray(wpkt, units);
    wpkt.Write(bRun);
    wpkt.Write(&ptTarget, sizeof(ptTarget));
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::UnitCommandAttack(CArray<CUnit *, CUnit *> &units, CMapObject *pTarget,
                                      ERPGHitLocations hl, BOOL bRun)
{
    GameJournal::Event("CMD", "attack", "units=%d target=%d location=%d run=%d", units.GetSize(), pTarget->GetID(), int(hl), bRun);
    wpkt.Write(PID_CMD_ATTACK);
    WriteUnitArray(wpkt, units);
    wpkt.Write(bRun);
    wpkt.Write(&hl, sizeof(hl));
    wpkt.Write(pTarget->GetID());
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::UnitCommandSkill(CArray<CUnit *, CUnit *> &units, CMapObject *pTarget,
                                     int nSkill, int nItemID, BOOL bRun)
{
    GameJournal::ObserveObject(pTarget, nSkill==RPGMSKL_LOOT?"take":nSkill==RPGMSKL_STEALING?"steal":"use");
    GameJournal::Event("CMD", "skill", "units=%d target=%d skill=%d item=%d run=%d", units.GetSize(), pTarget->GetID(), nSkill, nItemID, bRun);
    wpkt.Write(PID_CMD_SKILL);
    WriteUnitArray(wpkt, units);
    wpkt.Write(bRun);
    wpkt.Write(nSkill);
    wpkt.Write(nItemID);
    wpkt.Write(pTarget->GetID());
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::UnitCommandSkill(CUnit *pWho, CMapObject *pTarget, int nSkill, int nItemID,
                                     BOOL bRun)
{
    CArray<CUnit *, CUnit *> units;
    units.Add(pWho);
    return UnitCommandSkill(units, pTarget, nSkill, nItemID, bRun);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::UnitCommandLoot(CArray<CUnit *, CUnit *> &units, CUnit *pTarget, BOOL bRun)
{
    return UnitCommandSkill(units, pTarget, RPGMSKL_LOOT, -1, bRun);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::UnitCommandCast(int nQSpellNum, CUnit *pUnit, CMapObject *pTarget, BOOL bRun)
{
    GameJournal::Event("CMD", "cast_target", "id=%d target=%d spell_slot=%d run=%d", pUnit->GetID(), pTarget->GetID(), nQSpellNum, bRun);
    SNP_CMD_CAST_UNIT cast;
    cast.spellNum = nQSpellNum;
    cast.mapobjID = pUnit->GetID();
    cast.targetID = pTarget->GetID();
    cast.bDoItFast = bRun;

    wpkt.Write(PID_CMD_CASTTARGET);
    wpkt.Write(&cast, sizeof(cast));
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::UnitCommandCast(int nQSpellNum, CUnit *pUnit, CPlot3D &targetCoord, BOOL bRun)
{
    GameJournal::Event("CMD", "cast_point", "id=%d spell_slot=%d x=%g y=%g run=%d", pUnit->GetID(), nQSpellNum, targetCoord.x, targetCoord.y, bRun);
    SNP_CMD_CAST_POINT cast;
    cast.spellNum = nQSpellNum;
    cast.mapobjID = pUnit->GetID();
    cast.targetCoord = targetCoord;
    cast.bDoItFast = bRun;

    wpkt.Write(PID_CMD_CASTPOINT);
    wpkt.Write(&cast, sizeof(cast));
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::UnitCommandWand(int nQItemNum, CUnit *pUnit, CMapObject *pTarget, BOOL bRun)
{
    CItemQuick *pItem = pUnit->GetQItem(nQItemNum);
    if (pItem && pItem->GetCount() > 0 && pItem->GetQuickItemType() == IDB_SCROLL)
        pItem->SetCount(pItem->GetCount() - 1);
    return UnitCommandCast(nQItemNum | 0x80000000, pUnit, pTarget, bRun);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::UnitCommandWand(int nQItemNum, CUnit *pUnit, CPlot3D &ptTarget, BOOL bRun)
{
    CItemQuick *pItem = pUnit->GetQItem(nQItemNum);
    if (pItem && pItem->GetCount() > 0 && pItem->GetQuickItemType() == IDB_SCROLL)
        pItem->SetCount(pItem->GetCount() - 1);
    return UnitCommandCast(nQItemNum | 0x80000000, pUnit, ptTarget, bRun);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::UnitCommandSelWeapon(CUnit *pUnit, int nQuickWeapSlotIndex)
{
    SNP_CMD_SELWEAPON cmd;
    cmd.mapobjID = pUnit->GetID();
    cmd.cSlotToSelect = BYTE(nQuickWeapSlotIndex);

    wpkt.Write(PID_CMD_SELWEAPON);
    wpkt.Write(&cmd, sizeof(cmd));
    pUnit->WantWeapon(nQuickWeapSlotIndex);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::UnitCommandPose(CUnit *pUnit, EUnitPose pose)
{
    SNP_CMD_PHASE cmd;
    wpkt.Write(PID_CMD_POSE);
    cmd.mapobjID = pUnit->GetID();
    cmd.bPose = (BYTE)pose;
    wpkt.Write(&cmd, sizeof(cmd));
    pUnit->SetWishPose(pose);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::UnitCommandAggression(CUnit *pWho, EUnitAggression aggression)
{
    pWho->SetAggression(aggression);
    char cAggr = (char)aggression;
    wpkt.Write(PID_CMD_AGGRESSION);
    wpkt.Write(pWho->GetID());
    wpkt.Write(cAggr);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::UnitCommandTrap(CUnit *pUnit, int nQItemNum)
{
    wpkt.Write(PID_CMD_TRAP);
    wpkt.Write(pUnit->GetID());
    wpkt.Write(&nQItemNum, 4);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::UnitFollow(CUnit *pWho, CUnit *pFollowTo)
{
    SNP_CMD_FOLLOW cmd;
    cmd.mapobjID = pWho->GetID();
    cmd.targetID = pFollowTo ? pFollowTo->GetID() : -1;
    wpkt.Write(PID_CMD_FOLLOW);
    wpkt.Write(&cmd, sizeof(cmd));
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::CommandCombatPause(bool bPause)
{
    char cPause = bPause ? 1 : 0;
    wpkt.Write(PID_COMBAT_PAUSE);
    wpkt.Write(cPause);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerClient::SendPartyToServer()
{
    CMemorySave file;
    pPlayer->party.StoreItems(file);
    wpkt.Write(PID_CAMPITEMS);
    wpkt.Write(file);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerClient::ExitZone(int nEntrance)
{
    if (status == CPS_CAMP)
        FinishCamp();
    wpkt.Write(PID_EXITZONE);
    wpkt.Write(nEntrance);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerClient::GoZone(const char *pszZone, int nEntrance)
{
    if (status == CPS_CAMP)
        FinishCamp();
    wpkt.Write(PID_GOZONE);
    wpkt.WriteString(pszZone);
    wpkt.Write(nEntrance);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerClient::BriefingComplete(const char *pszBrfVar)
{
    wpkt.Write(PID_BRIEFING_DONE);
    wpkt.WriteString(pszBrfVar);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerClient::GoCamp(int nInterface)
{
    wpkt.Write(PID_GOCAMP);
    wpkt.Write(nInterface);
    status = CPS_CAMP;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerClient::FinishCamp()
{
    ASSERT(status == CPS_CAMP);
    if (status != CPS_CAMP)
        return;
    IWorldConnector.LMPClientRecvParty();
    SendPartyToServer();
    wpkt.Write(PID_FINISHCAMP);
    status = CPS_ACTIVE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerClient::FinishZoneLoading()
{
    wpkt.Write(PID_ZONE_LOADED);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
