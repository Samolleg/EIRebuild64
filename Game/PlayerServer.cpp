#include "StdAfx.h"
#include "LiveObserver.h"
#include "ModEncounter.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
/*==========================================================================================================

        Server specific player class implementation

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

#include "WorldServer.h"
#include "WorldInterface.h"
#include "LiveObserver.h"
#include "PlayerServer.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "UnitServer.h"
#include "MapObjectServer.h"
#include "ServerConsts.h"
#include "Effect.h"
#include "Net.h"
#include "PacketIDs.h"
#include "ScenarioTracker.h"
#include "aiUnitLogic.h"
#include "console.h"
#include "aiSettings.h"
#include "RandomGen.h"
#include "ItemManager.h"
#include "UnitDatabase.h"
/////////////////////////////////////////////////////////////////////////////////////
// ----------------------------- Create/destroy
// ------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CPacket CPlayerServer::pkt;
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CPlayerServer::CPlayerServer()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif
    Clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CPlayerServer::~CPlayerServer()
{
    Clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::Create()
{
    Clear();
    // Create parsers table
    parsers.SetSize(256);
    memset(parsers.GetData(), 0, parsers.GetSize() * sizeof(parsers[0]));
    parsers[PID_LOGIN] = &CPlayerServer::PrsLogin;
    parsers[PID_LOGOUT] = &CPlayerServer::PrsLogout;
    parsers[PID_RECONNECT] = &CPlayerServer::PrsReconnect;
    parsers[PID_COMBAT_PAUSE] = &CPlayerServer::PrsCombatPause;
    parsers[PID_CMD_MOVETO] = &CPlayerServer::PrsUnitCommandMoveTo;
    parsers[PID_CMD_ATTACK] = &CPlayerServer::PrsUnitCommandAttack;
    parsers[PID_CMD_CASTTARGET] = &CPlayerServer::PrsUnitCommandCastTarget;
    parsers[PID_CMD_CASTPOINT] = &CPlayerServer::PrsUnitCommandCastPoint;
    parsers[PID_CMD_SELWEAPON] = &CPlayerServer::PrsUnitCommandSelWeapon;
    parsers[PID_CMD_POSE] = &CPlayerServer::PrsUnitCommandPose;
    parsers[PID_CMD_SKILL] = &CPlayerServer::PrsUnitCommandSkill;
    parsers[PID_CMD_LOOT] = &CPlayerServer::PrsUnitCommandLoot;
    parsers[PID_CMD_TRAP] = &CPlayerServer::PrsUnitCommandTrap;
    parsers[PID_CMD_FOLLOW] = &CPlayerServer::PrsUnitFollow;
    parsers[PID_CAMERAPOSITION] = &CPlayerServer::PrsCameraPosition;
    parsers[PID_CAMPITEMS] = &CPlayerServer::PrsCampItems;
    parsers[PID_EXITZONE] = &CPlayerServer::PrsExitZone;
    parsers[PID_GOCAMP] = &CPlayerServer::PrsGoCamp;
    parsers[PID_FINISHCAMP] = &CPlayerServer::PrsFinishCamp;
    parsers[PID_GOZONE] = &CPlayerServer::PrsGoZone;
    parsers[PID_BRIEFING_DONE] = &CPlayerServer::PrsBriefingComplete;
    parsers[PID_CMD_AGGRESSION] = &CPlayerServer::PrsUnitCommandAggresion;
    parsers[PID_CMD_SWARM] = &CPlayerServer::PrsUnitCommandSwarm;
    parsers[PID_ZONE_LOADED] = &CPlayerServer::PrsZoneLoaded;

    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::Destroy()
{
    Clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::Clear()
{
    wpkt.SetSize(0);
    ReleaseIDs(visibleObjects);
    ReleaseIDs(visibleEffects);
    status = PSS_WAITING_FOR_LOGIN;
    cameraCP = CPlot3D(0, 0, 0);
    nCameraUpdateCounter = -1;
    addToVisible.Clear();
    currentVisible.Clear();
    nLookFromID = -1;
    dangerState = WDS_NORMAL;
    bPartyDeployed = false;
    partyPlace.Clear();
    packerSend.Clear();
    packerRecv.Clear();
    nStorePartyCounter = (retiringSavedWorld || ObjectIdentity::RestoreNamespace::Active()) ? 0 : random.Get(60 * 15, 120 * 15);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::RemoveAllObjects()
{
    ReleaseIDs(visibleObjects);
    ReleaseIDs(visibleEffects);
    addToVisible.Clear();
    currentVisible.Clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::WantToDeployParty()
{
    if (!bPartyDeployed)
    {
        theWorldServer.DeployPlayerParty(pPlayer);
        bPartyDeployed = true;
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::WantToRemoveParty()
{
    if (bPartyDeployed)
    {
        theWorldServer.RemovePlayerParty(pPlayer);
        bPartyDeployed = false;
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::NewConnection()
{
    Clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::OnDisconnect()
{
    // CRAP - mode camp is not accounted for
    if (status == PSS_ACTIVE)
    {
        WantToRemoveParty(); // undeploy units of the player from server
        switch (theWorldServer.GetServerMode())
        {
        default:
            break;
        }
        // SendServerChangeCmd( pszZone );
    }
    if (status != PSS_LOGOUT)
    {
        status = PSS_LOGOUT;
        wpkt.Write(PID_LOGOUT);
    }
    SendUpdate();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::PrepareNewSegment()
{
    addToVisible.Clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::UnitHasChangedOwner(CUnit *pUnit)
{
    // RemoveMapObjectRefs( pUnit );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::SendUpdate()
{
    BOOL bRv;
    CConnection *pConnection = pPlayer->GetConnection();
    if (!pConnection)
        return FALSE;
    /*	CString szTemp;
            szTemp.Format( "Send packet of size %i\n", wpkt.GetSize() );
            OutputDebugString( szTemp );*/
    // NConsole::game << "update size = " << wpkt.GetSize() << endl;
    CPacket packed;
    wpkt.Pack(packerSend, packed);
    if (pPlayer->bShowTraffic)
        NConsole::net << "server to client, size = " << wpkt.GetSize() << " squeezed to "
                      << packed.GetSize() << endl;
    bRv = pConnection->Send(packed);
    wpkt.SetSize(0);
    return bRv;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::CopyUpdatePkt(CPacket &dst)
{
    dst.CopyFrom(wpkt);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::StartZoneLoading()
{
    ASSERT(status == PSS_ACTIVE);
    status = PSS_ZONELOADWAIT;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SetPartyIsDeployed(bool bIsDeployed)
{
    bPartyDeployed = bIsDeployed;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// ----------------------------------- Packet parsing
// ------------------------------------------------------
BOOL CPlayerServer::ParseIncomingPackets()
{
    if (status == PSS_LOGOUT)
    {
        /*while ( pConnection->HasPacket() )
        {
                CPacket packed;
                pConnection->Recv( packed );
        }*/
        return FALSE;
    }
    PACKET_ID packetID, prevPacketID;
    CConnection *pConnection = pPlayer->GetConnection();
    assert(pConnection);
    if (!pConnection)
        return FALSE;
    while (pConnection->HasPacket())
    {
        CPacket packed;
        if (!pConnection->Recv(packed))
            return FALSE;
        packed.Unpack(packerRecv, pkt);
        pkt.Seek(0);
        pkt.SetOk();
        packetID = -1;
        prevPacketID = -1;
        while (1)
        {
            if (!pkt.IsOk())
                return ReportProtocolError("Serv pkt read error prevID=%x curID=%x", prevPacketID,
                                           packetID);
            prevPacketID = packetID;
            if (!pkt.Read(&packetID, 1))
                break;
            if (status == PSS_WAITING_FOR_LOGIN && packetID != PID_LOGIN)
                break;
            // return FALSE;//ReportError( "Serv waiting for login but none arrived" );
            if (packetID < 0 || packetID >= parsers.GetSize())
                return ReportProtocolError("Serv wrong packet ID prevID=%x curID=%x", prevPacketID,
                                           packetID);
            if (parsers[packetID] == NULL)
                return ReportProtocolError("Serv packet has no handler prevID=%x curID=%x",
                                           prevPacketID, packetID);
            if ((this->*parsers[packetID])() == FALSE)
                return ReportProtocolError("Serv error during parsing command prevID=%x curID=%x",
                                           prevPacketID, packetID);
            if (status == PSS_LOGOUT)
                return FALSE;
        }
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// ---------------------------------- Messages
// -------------------------------------------------------------
void CPlayerServer::SendShowPath(CUnit *pWho, CAIFullRoute &route, const CPlot3D &ptDest,
                                 CPlayer::EPathType type, CPlayer::EPathResult res,
                                 float fpShowLeng)
{
    if (!pWho)
        return;
    wpkt.Write(PID_SHOWPATH);
    wpkt.Write(&type, sizeof(type));
    wpkt.Write(&res, sizeof(res));
    wpkt.Write(ptDest.x);
    wpkt.Write(ptDest.y);
    wpkt.Write(pWho->GetID());
    wpkt.Write(fpShowLeng);
    if (type != CPlayer::PPT_NOPATH)
    {
        CUpdateSingle update;
        CUpdateHistory *pPkt;
        pPkt = update.StartUpdateTo(utStart);
        route.UpdateTo(*pPkt);
        pPkt->FinishUpdate();
        update.AppendStart(wpkt);
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SendDeathEvent(CUnit *pWho)
{
    if (!pWho)
        return;
    wpkt.Write(PID_UNIT_DEATH);
    wpkt.Write(pWho->GetID());
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SendFailure(CMapObject *pObject, DWORD dwAction)
{
    SCP_ACTION_FAILURE failure;
    failure.dwUnitID = pObject->GetID();
    failure.dwAction = dwAction;
    wpkt.Write(PID_ACTIONFAILURE);
    wpkt.Write(&failure, sizeof(failure));
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SendEvent(char bPlayerNum, DWORD dwEvent)
{
    SCP_EVENT event;
    event.dwEventID = dwEvent;
    wpkt.Write(PID_EVENT);
    wpkt.Write(&event, sizeof(event));
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SendStringEvent(char bPlayerNum, const char *pszStringEvent)
{
    SendMsg(PID_STRINGEVENT, pszStringEvent);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SendMsg(PACKET_ID pID, LPCSTR pszMessage)
{
    wpkt.Write(pID);
    wpkt.WriteString(pszMessage);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// -------------------------------- Map objects
// ------------------------------------------------------------
BOOL CPlayerServer::AddMapObjects(CGameBasePtrArray &toAdd)
{
    CMapObject **ppMO = (CMapObject **)toAdd.GetData();
    for (int i = 0; i < toAdd.GetSize(); i++)
    {
        CMapObject *pMO = *ppMO++;
        // Send unit info to client
        BOOL bIsUnit = pMO->WhoAmI() == HPC_UNIT;

        SCP_CREATE_MAPOBJ mapobjCreate;
        SComplection complection;
        mapobjCreate.mapobjID = pMO->GetID();
        mapobjCreate.typeID = pMO->GetTypeID();
        mapobjCreate.dwSubtype = pMO->GetSubtypeID();
        mapobjCreate.dwFlags = pMO->GetFlags();
        wpkt.Write(PID_CREATE_MAPOBJ);
        wpkt.Write(&mapobjCreate, sizeof(mapobjCreate));
        pMO->GetComplection(complection);
        wpkt.Write(&complection, sizeof(complection));
        wpkt.WriteString(pMO->GetAttachedQuest());

        CMapObjectServer *pSpec = (CMapObjectServer *)pMO->GetMapObjectSpecific();
        wpkt.WriteString(pMO->GetMaskName());
        wpkt.WriteString(pSpec->szPrimTexture);
        wpkt.WriteString(pSpec->szSecTexture);
        wpkt.Write((WORD)pSpec->szNames.GetSize());
        for (int q = 0; q < pSpec->szNames.GetSize(); q++)
            wpkt.WriteString(pSpec->szNames[q]);
        DWORD dwVoice = 0;
        if (bIsUnit)
        {
            wpkt.WriteString(((CUnit *)pMO)->GetName());
            dwVoice = ((CUnit *)pMO)->GetVoicePrototypeIdx();
        }
        else
            wpkt.WriteString("");
        wpkt.Write(dwVoice);

        if (pMO->IsUpdated())
            pMO->UpdateFrame();
        wpkt.Append(pMO->update.startPkt);

        // Register Unit
        pPlayer->visibleObjects.SetAt(pMO->GetID(), pMO);
        pMO->AddRef();
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::RemoveMapObjects(CGameBasePtrArray &toRemove)
{
    CMapObject **ppMO = (CMapObject **)toRemove.GetData();
    for (int i = 0; i < toRemove.GetSize(); i++)
    {
        CMapObject *pMO = *ppMO++;
        // Send packet to kill unit

        SCP_REMOVE_MAPOBJ unitRem;
        unitRem.mapobjID = pMO->GetID();

        // ASSERT( pPlayer->unitsUnderControl.Seek( pMO->GetID() ) == NULL );
        assert(pPlayer->visibleObjects.Seek(pMO->GetID()));
        pPlayer->visibleObjects.RemoveAt(pMO->GetID());
        pMO->Release();

        wpkt.Write(PID_REMOVE_MAPOBJ);
        wpkt.Write(&unitRem, sizeof(unitRem));
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::UpdateMapObjects(CGameBasePtrArray &toUpdate)
{
    SCP_UPDATE_MAPOBJ upd;
    CMapObject **ppMO = (CMapObject **)toUpdate.GetData();
    int nUpdateCounter = GetWorldServer()->GetUpdateCounter();
    for (int i = 0; i < toUpdate.GetSize(); i++)
    {
        CMapObject *pMO = *ppMO++;
        if (pMO->nUpdatedSegment == nUpdateCounter)
        {
            if (pMO->IsUpdated())
                pMO->UpdateFrame();
            upd.mapobjID = pMO->GetID();
            wpkt.Write(PID_UPDATE_MAPOBJ);
            wpkt.Write(&upd, sizeof(upd));
            wpkt.Append(pMO->update.framePkt);
        }
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::RemoveMapObjectRefs(CMapObject *pObject)
{
    SCP_REMOVE_MAPOBJ unitRem;
    if (pPlayer->visibleObjects.Seek(pObject->GetID()))
    {
        // ASSERT( pPlayer->unitsUnderControl.Seek( pObject->GetID() ) == NULL );
        pPlayer->visibleObjects.RemoveAt(pObject->GetID());
        pObject->Release();
        for (int i = 0; i < visibleObjects.GetSize(); i++)
        {
            if (visibleObjects[i] == pObject)
            {
                visibleObjects.RemoveAt(i);
                pObject->Release();
                i--;
            }
        }

        unitRem.mapobjID = pObject->GetID();
        wpkt.Write(PID_REMOVE_MAPOBJ);
        wpkt.Write(&unitRem, sizeof(unitRem));
    }
    addToVisible.Remove(pObject);
    currentVisible.Remove(pObject);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// ---------------------------------- Effects
// --------------------------------------------------------------
BOOL CPlayerServer::AddEffects(CGameBasePtrArray &toAdd)
{
    SCP_CREATE_EFFECT upd;
    CEffect **ppEff = (CEffect **)toAdd.GetData();
    for (int i = 0; i < toAdd.GetSize(); i++)
    {
        CEffect *pEff = *ppEff++;
        if (pEff->IsUpdated())
            pEff->UpdateFrame();
        upd.classID = pEff->WhoAmI();
        upd.effectID = pEff->GetID();
        wpkt.Write(PID_CREATE_EFFECT);
        wpkt.Write(&upd, sizeof(upd));
        pEff->update.AppendStart(wpkt);
        pPlayer->visibleEffects.SetAt(pEff->GetID(), pEff);
        pEff->AddRef();
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::RemoveEffects(CGameBasePtrArray &toRemove)
{
    SCP_REMOVE_EFFECT remEffect;
    CEffect **ppEff = (CEffect **)toRemove.GetData();
    for (int i = 0; i < toRemove.GetSize(); i++)
    {
        CEffect *pEff = *ppEff++;

        wpkt.Write(PID_REMOVE_EFFECT);
        remEffect.effectID = pEff->GetID();
        wpkt.Write(&remEffect, sizeof(remEffect));

        assert(pPlayer->visibleEffects.Seek(pEff->GetID()));
        pPlayer->visibleEffects.RemoveAt(pEff->GetID());
        pEff->Release();
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::UpdateEffects(CGameBasePtrArray &toUpdate)
{
    CEffect **ppEff = (CEffect **)toUpdate.GetData();
    int nUpdateCounter = GetWorldServer()->GetUpdateCounter();
    for (int i = 0; i < toUpdate.GetSize(); i++)
    {
        CEffect *pEff = *ppEff++;
        if (pEff->nUpdatedSegment == nUpdateCounter)
        {
            if (pEff->IsUpdated())
                pEff->UpdateFrame();
            wpkt.Write(PID_UPDATE_EFFECT);
            wpkt.Write(pEff->GetID());
            pEff->update.AppendFrame(wpkt);
        }
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// -------------------------------- Visibility test
// -----------------------------------------------------
bool CPlayerServer::IsUnitVisibleToParty(CUnit *pTarget)
{
    return currentVisible.HasMapObject(pTarget) != FALSE;
    /*
            if ( addToVisible.HasMapObject( pTarget ) )
                    return true;
            CUnitPtrHashIterator unitIter;
            CUnit *pUnit = unitIter.GetFirst( &pPlayer->unitsUnderControl );
            while ( pUnit )
            {
                    if ( pTarget == pUnit )
                            return true;
                    if ( ((CUnitServer*)pUnit->GetUnitSpecific())->logic.GetVisibleList()->Find(
       pTarget ) ) return true; if (
       ((CUnitServer*)pUnit->GetUnitSpecific())->logic.environment.seenCorpses.Find( pTarget ) )
                            return true;
                    pUnit = unitIter.GetNext();
            }
            return false;*/
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// -------------------------------- Visible units list
// -----------------------------------------------------
void CPlayerServer::FormNewVisibleSet()
{
    // create the list of visible units
    CScriptObjectGroup deadUnits, unitsVis;

    unitsVis = addToVisible;

    if (nLookFromID == -1)
    {
        CUnitPtrHashIterator unitIter;
        CUnit *pUnit = unitIter.GetFirst(&pPlayer->unitsUnderControl);
        while (pUnit)
        {
            unitsVis.Add(pUnit);
            unitsVis.AddList(*((CUnitServer *)pUnit->GetUnitSpecific())->logic.GetVisibleList());
            deadUnits.AddList(
                ((CUnitServer *)pUnit->GetUnitSpecific())->logic.environment.seenCorpses);
            pUnit = unitIter.GetNext();
        }
        ModEncounter::PinVisible(*GetWorldServer(),pPlayer,unitsVis);
        currentVisible.Add(unitsVis, deadUnits);
        // make list of seen corpses common between all units under player control
        pUnit = unitIter.GetFirst(&pPlayer->unitsUnderControl);
        while (pUnit)
        {
            deadUnits.MakeList(
                ((CUnitServer *)pUnit->GetUnitSpecific())->logic.environment.seenCorpses);
            pUnit = unitIter.GetNext();
        }
    }
    else
    {
        ModEncounter::PinVisible(*GetWorldServer(),pPlayer,unitsVis);
        currentVisible = unitsVis;
        nLookFromID = -1;
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::SendVisibleUnitsList()
{
    int i, nSize;
    dangerState = WDS_NORMAL;

    float fpSafetyMulDist = 2;
    if (auto *package = ModRuntime::Current())
    {
        const auto &spellAddon = package->SpellAddon();
        if (spellAddon.Feature31() == 1)
            fpSafetyMulDist = spellAddon.Scalar(ModSpellAddon::ScalarKey::gameplay_safetymuldist);
    }

    CUnitPtrHashIterator unitIter;
    CUnit *pUnit = unitIter.GetFirst(&pPlayer->unitsUnderControl);
    while (pUnit)
    {
        // switch mode to warry if we are close to range
        if (!pUnit->sensesDetection.IsRatherSafe(fpSafetyMulDist))
            dangerState = WDS_WARRY;
        pUnit = unitIter.GetNext();
    }
    // send pause state
    // send visible units list
    // send game danger state
    nSize = currentVisible.GetSize();
    wpkt.Write(PID_VISIBLE_UNITS);
    char cPause = GetWorldServer()->IsCombatPause() ? 1 : 0;
    wpkt.Write(cPause);
    wpkt.Write(nLookFromID);
    wpkt.Write(nSize);

    int nPlayerNum = pPlayer->GetPlayerNumber();
    for (i = 0; i < nSize; i++)
    {
        CUnit *pUnit = currentVisible.GetAt(i).GetUnit();
        if (dangerState != WDS_COMBAT && !pUnit->IsDead())
        {
            CUnitServer *pServer = (CUnitServer *)pUnit->GetUnitSpecific();
            if (pServer->IsDanger())
                dangerState = WDS_COMBAT;
            // look if someone attack us
            CMapObjectPtrList &lst = pServer->logic.environment.units;
            for (POSITION pos = lst.GetHeadPosition(); pos;)
            {
                CUnit *pE = (CUnit *)lst.GetNext(pos);
                if (pE->IsEnemy(nPlayerNum))
                {
                    CUnitServer *pUS = (CUnitServer *)pE->GetUnitSpecific();
                    if (pUS->logic.GetAggressionTarget() == pUnit)
                        dangerState = WDS_COMBAT;
                }
            }
        }
        wpkt.Write(pUnit->GetID());
    }
    wpkt.Write(&dangerState, sizeof(dangerState));
    //
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
/*BOOL CPlayerServer::SendNewItems( CMemorySave &packet )
{
        wpkt.Write( PID_NEWSPELLITEMS );
        wpkt.WritePacket( packet );
        // transmit current hoover state
        AddItemsToHoover( pPlayer->items );
        return TRUE;
}*/
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::SendAddItems(CMemorySave &packet)
{
    wpkt.Write(PID_ADDSPELLITEMS);
    wpkt.WritePacket(packet);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::AddItemsToHoover(CList<CItem *, CItem *> &newItems, bool bCrippled)
{
    POSITION pos;
    if (status != PSS_ACTIVE)
        return;
    // form packet with new items
    CGameBaseList spellList, itemList;
    for (pos = newItems.GetHeadPosition(); pos;)
        AddItemToLists(itemList, spellList, newItems.GetNext(pos));
    CMemorySave packet;
    StoreList(spellList, packet);
    StoreList(itemList, packet);
    SendAddItems(packet);
    //
    wpkt.Write(PID_ADDITEMSTOHOOVER);
    wpkt.Write(pPlayer->party.nMoney);
    wpkt.Write(&bCrippled, 1);
    pos = newItems.GetHeadPosition();
    while (pos)
    {
        CItem *pItem = newItems.GetNext(pos);
        wpkt.Write((int)pItem->GetID());
    }
    wpkt.Write((int)-1);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::RemoveAllHoover()
{
    wpkt.Write(PID_REMOVEHOOVER);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::RemoveItemFromInventory(CItem *pItem)
{
    wpkt.Write(PID_REMOVEITEM);
    wpkt.Write(pItem->GetID());
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::RemoveWeaponFromUnit(int nUnitID, int nWeaponIdx)
{
    wpkt.Write(PID_REMOVEWEAPON);
    wpkt.Write(nUnitID);
    wpkt.Write(nWeaponIdx);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SendItemsMana(CList<CItem *, CItem *> &items)
{
    wpkt.Write(PID_ITEMSMANA);
    for (POSITION pos = items.GetHeadPosition(); pos;)
    {
        CItem *pItem = items.GetNext(pos);
        wpkt.Write((int)pItem->GetID());
        wpkt.Write(pItem->GetMP());
    }
    wpkt.Write((int)-1);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::AddActiveItem(CItem *pItem)
{
    wpkt.Write(PID_ADDAITEM);
    wpkt.Write(pItem->GetID());
    wpkt.Write(pItem->GetMP());
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::PlayMusic(const char *pszMusicName)
{
    wpkt.Write(PID_PLAYMUSIC);
    wpkt.WriteString(pszMusicName);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::SendNewZone(CMemorySave &packet)
{
    // send command and reset flag
    pPlayer->ResetChanged(DW_PCHANGED_ZONE);
    wpkt.Write(PID_NEWZONE);
    wpkt.WritePacket(packet);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::SendWeather(BYTE bWeather, int nTime)
{
    pPlayer->ResetChanged(DW_PCHANGED_WEATHER);
    SCP_WEATHER weather;
    weather.bWeather = bWeather;
    weather.nTime = nTime;
    wpkt.Write(PID_WEATHER);
    wpkt.Write(&weather, sizeof(weather));
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::SendWorldTime(float fpTimeModifier, float fpTimeScale)
{
    pPlayer->ResetChanged(DW_PCHANGED_TIME);
    SCP_WORLDTIME time;
    time.fpTimeMod = fpTimeModifier;
    time.fpTimeScale = fpTimeScale;
    wpkt.Write(PID_WORLDTIME);
    wpkt.Write(&time, sizeof(time));
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::SendWind(CPlot3D ptWind, float fpWindStrength)
{
    pPlayer->ResetChanged(DW_PCHANGED_WIND);
    wpkt.Write(PID_WIND);
    wpkt.Write(&ptWind, sizeof(ptWind));
    wpkt.Write(fpWindStrength);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::SendMoveCamera(CPlot3D &ptPos, float fAngle, EInterface eIFace)
{
    wpkt.Write(PID_MOVECAMERA);
    wpkt.Write(&ptPos, sizeof(ptPos));
    wpkt.Write(&fAngle, sizeof(fAngle));
    wpkt.Write(&eIFace, sizeof(eIFace));
    cameraCP = ptPos;
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SendMoveCamera(const CPlot3D &ptSrc, const CPlot3D &ptDst, bool bLock)
{
    wpkt.Write(PID_MOVECAMERA2);
    wpkt.Write(&ptSrc, sizeof(ptSrc));
    wpkt.Write(&ptDst, sizeof(ptDst));
    wpkt.Write(&bLock, sizeof(bLock));
    cameraCP = ptSrc;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SetVar(const char *pszName, float fpVal)
{
    pPlayer->party.vars.SetVar(pszName, fpVal);
    wpkt.Write(PID_SETVAR);
    wpkt.WriteString(pszName);
    wpkt.Write(fpVal);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SetVarMax(const char *pszName, float fpVal)
{
    float fpOldVal = pPlayer->party.vars.GetVar(pszName);
    fpVal = Max(fpVal, fpOldVal);
    if (fpVal != fpOldVal)
        SetVar(pszName, fpVal);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::DelVar(const char *pszName)
{
    pPlayer->party.vars.DelVar(pszName);
    wpkt.Write(PID_DELVAR);
    wpkt.WriteString(pszName);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SendParty()
{
    CMemorySave file;
    pPlayer->party.Store(file);
    wpkt.Write(PID_PARTY);
    wpkt.Write(file);
    nStorePartyCounter = (retiringSavedWorld || ObjectIdentity::RestoreNamespace::Active()) ? 0 : random.Get(60 * 15, 120 * 15);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SendActiveItems()
{
    for (POSITION pos = pPlayer->activeItems.GetHeadPosition(); pos;)
    {
        int nID = pPlayer->activeItems.GetNext(pos);
        CItem *pItem = (CItem *)pPlayer->party.GetItemManager()->Find(nID);
        ASSERT(pItem);
        if (pItem)
            AddActiveItem(pItem);
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SendPartyItems()
{
    CMemorySave file;
    pPlayer->party.StoreItems(file);
    wpkt.Write(PID_PARTYITEMS);
    wpkt.Write(file);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SendNewInterface()
{
    int nInterface = pPlayer->curInterface;
    wpkt.Write(PID_NEWINTERFACE);
    wpkt.Write(nInterface);
    wpkt.Write(theWorldServer.GetSegmentTimeCounter());
    pPlayer->ResetChanged(DW_PCHANGED_INTERFACE);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SendServerChangeCmd(const char *pszName, int nEntrance)
{
    SendMsg(PID_SERVERCHANGE, pszName);
    wpkt.Write(nEntrance);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::SendWaterLevels(CWaterLevels &water)
{
    wpkt.Write(PID_WATERLEVELS);
    water.UpdateTo(wpkt);
    pPlayer->ResetChanged(DW_PCHANGED_WATERLEVEL);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::GainExperience(float fpExp)
{
    wpkt.Write(PID_GAINEXP);
    wpkt.Write(fpExp);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::GainExp(int nID, float fpExp)
{
    wpkt.Write(PID_PERSGETEXP);
    wpkt.Write(nID);
    wpkt.Write(fpExp);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::LeaveToZone(const char *pszZone, int nZoneEntrance)
{
    ASSERT(status == PSS_ACTIVE);
    if (status != PSS_ACTIVE) {
        LiveObserver::RecordError("zone request refused: player is not active", "");
        LiveObserver::OperationApplied("enter_zone", false);
        return;
    }
    if (theWorldServer.curInterface == IFACE_MISSION || theWorldServer.curInterface == IFACE_BASE)
    {
        CUnit *pMainHero = theWorldServer.GetUnit(pPlayer->party.people[0].nID);
        if (!pMainHero || pMainHero->IsDead())
        {
            LiveObserver::RecordError("zone request refused: main hero is dead or absent", "");
            LiveObserver::OperationApplied("enter_zone", false);
            GameOver();
            return;
        }
    }
    WantToRemoveParty();
    // store server state to disk if its a single player case
    /*
    if ( (pPlayer->curInterface == IFACE_MISSION ||
         pPlayer->curInterface == IFACE_BASE) &&
         theWorldServer.GetServerMode() == SERV_MODE_LOCAL )
            IWorld.StoreZoneState( pPlayer->party );
            */
    status = PSS_LOGOUT;
    pPlayer->party.nEntrance = nZoneEntrance;
    pPlayer->party.szCurrentZone = pszZone;

    switch (theWorldServer.GetServerMode())
    {
    case SERV_MODE_LOCAL:
        theHat.SetSinglePlayerParty(pPlayer->party);
        break;
    case SERV_MODE_INTERFACE:
    default:
        ASSERT(0);
        break;
    }
    SendServerChangeCmd(pszZone, nZoneEntrance);
    SendUpdate();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::GameOver()
{
    {
        wpkt.Write(PID_GAMEOVER);
        int nID = pPlayer->party.people[0].nID;
        CUnit *pUnit = theWorldServer.GetUnit(nID);
        if (pUnit)
        {
            SendFailure(pUnit, ACK_CLEAR);
        }
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayerServer::Select(EPCSelectCmd cmd, int nID)
{
    wpkt.Write(PID_SELECTUNIT);
    wpkt.Write(&cmd, 1);
    wpkt.Write(nID);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// -------------------------------- Login/logout
// -----------------------------------------------------------
BOOL CPlayerServer::PrsLogin()
{
    SNP_LOGIN loginInfo;
    pkt.Read(&loginInfo, sizeof(loginInfo));
    if (status != PSS_WAITING_FOR_LOGIN)
        return FALSE;
    if (loginInfo.dwClientVersion != DW_REQUIRED_CLIENT_VERSION)
    {
        // error protocol message
        char pszMessBuf[256];
        char pszOutBuf[256 + 64];
        int nLen = sizeof(pszMessBuf);
        GetWorldServer()->messages.GetString("SrvErrMsg", "WrongClientVersion", pszMessBuf, nLen,
                                             "");
        sprintf(pszOutBuf, pszMessBuf, DW_REQUIRED_CLIENT_VERSION / 100,
                DW_REQUIRED_CLIENT_VERSION % 100);
        SendMsg(PID_ERR_MSG, pszOutBuf);
        status = PSS_LOGOUT;
        return FALSE;
    }
    // Check login
    if (!CheckSymbString(loginInfo.pszUser, N_SNP_LOGIN_NAME_LEN - 1, 1))
        return FALSE;
    if (!CheckSymbString(loginInfo.pszPassword, N_SNP_LOGIN_NAME_LEN - 1, 1))
        return FALSE;
    loginInfo.pszUser[N_SNP_LOGIN_NAME_LEN - 1] = 0;
    loginInfo.pszPassword[N_SNP_LOGIN_NAME_LEN - 1] = 0;
    // Search for requested party
    switch (theWorldServer.GetServerMode())
    {
    case SERV_MODE_LOCAL:
        theHat.GetSinglePlayerParty(pPlayer->party);
        pPlayer->party.AutoAvailableZones();
        break;
    case SERV_MODE_INTERFACE:
        pPlayer->party.Clear();
        break;
    default:
        // logins are not accepted in this server mode
        ASSERT(0);
        break;
    }
    SCP_LOGGED pktLogged;
    pktLogged.nPlayerID = pPlayer->GetID();
    wpkt.Write(PID_LOGGED);
    wpkt.Write(&pktLogged, sizeof(pktLogged));
    SendParty();

    status = PSS_ACTIVE;

    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsLogout()
{
    OnDisconnect();
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsReconnect()
{
    pPlayer->ResendEverything();
    SendParty();
    // status = PSS_ACTIVE;
    // status = SPS_NONE;
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsCombatPause()
{
    char cPause;
    pkt.Read(cPause);

    GetWorldServer()->RequestCombatPause(cPause != 0);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// ----------------------------------- Commands
// ------------------------------------------------------------
BOOL CPlayerServer::PrsUnitCommandAggresion()
{
    int nID;
    char cAggr;
    CUnit *pUnit;

    pkt.Read(nID);
    pUnit = GetWorldServer()->GetUnit(nID);
    pkt.Read(cAggr);
    if (!LiveObserver::CheckPlayerOrder(nID, PID_CMD_AGGRESSION)) return TRUE;
    if (pUnit)
        pUnit->SetAggression((EUnitAggression)cAggr);
    LiveObserver::PlayerOrderResult(nID, PID_CMD_AGGRESSION, pUnit ? nullptr : "unit_not_found_on_server");
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
static void ReadUnitArray(CPacket &pkt, CArray<CUnit *, CUnit *> &units, int packet = 0)
{
    int n;
    pkt.Read(n);
    for (int i = 0; i < n; i++)
    {
        int nID;
        pkt.Read(nID);
        if (!LiveObserver::CheckPlayerOrder(nID, packet)) continue;
        CUnit *pUnit = theWorldServer.GetUnit(nID);
        if (pUnit && !pUnit->CheckFlag(UNIT_BLOCKED))
            units.Add(pUnit);
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
static void CalcOffsets(CArray<CUnit *, CUnit *> &units, CArray<CPlot3D, CPlot3D &> &offsets)
{
    int i;
    if (units.GetSize() < 1)
        return;
    if (units.GetSize() == 1)
    {
        offsets.SetSize(1);
        offsets[0] = CPlot3D(0, 0, 0);
        return;
    }
    CPlot3D ptCenter(0, 0, 0);
    for (i = 0; i < units.GetSize(); i++)
        ptCenter += units[i]->GetCP();
    if (units.GetSize() > 0)
        ptCenter /= units.GetSize();
    offsets.SetSize(units.GetSize());
    for (i = 0; i < units.GetSize(); i++)
    {
        CPlot3D ptOffs = units[i]->GetCP() - ptCenter;
        ptOffs.z = 0;
        ptOffs.Normalize();
        offsets[i] = ptOffs;
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsUnitCommandSwarm()
{
    BOOL bRun;
    CPlot3D ptTarget;
    CArray<CUnit *, CUnit *> units;
    CArray<CPlot3D, CPlot3D &> offsets;
    ReadUnitArray(pkt, units);
    pkt.Read(bRun);
    pkt.Read(&ptTarget, sizeof(ptTarget));
    CalcOffsets(units, offsets);
    for (int i = 0; i < units.GetSize(); i++)
    {
        CUnit *pUnit = units[i];
        CAIUnitLogic &logic = ((CUnitServer *)pUnit->GetUnitSpecific())->logic;
        CUMPlayer *pUP = logic.GetPlayer();
        if (pUP)
            pUP->DoSwarm(logic, ptTarget, units.GetSize() > 1, bRun != 0, offsets[i]);
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsUnitCommandMoveTo()
{
    BOOL bRun;
    CPlot3D ptTarget;
    CArray<CUnit *, CUnit *> units;
    CArray<CPlot3D, CPlot3D &> offsets;
    ReadUnitArray(pkt, units, PID_CMD_MOVETO);
    pkt.Read(bRun);
    pkt.Read(&ptTarget, sizeof(ptTarget));
    CalcOffsets(units, offsets);
    for (int i = 0; i < units.GetSize(); i++)
    {
        CUnit *pUnit = units[i];
        CAIUnitLogic &logic = ((CUnitServer *)pUnit->GetUnitSpecific())->logic;
        CUMPlayer *pUP = logic.GetPlayer();
        if (pUP)
            pUP->DoMoveTo(logic, ptTarget, units.GetSize() > 1, bRun != 0, offsets[i]);
        LiveObserver::PlayerOrderResult(pUnit->GetID(), PID_CMD_MOVETO, pUP ? nullptr : "unit_has_no_player_logic");
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsUnitCommandAttack()
{
    int nID;
    BOOL bRun;
    ERPGHitLocations hl;
    CMapObject *pTarget;
    CArray<CUnit *, CUnit *> units;
    ReadUnitArray(pkt, units, PID_CMD_ATTACK);
    pkt.Read(bRun);
    pkt.Read(&hl, sizeof(hl));
    pkt.Read(nID);
    pTarget = theWorldServer.GetMapObject(nID);
    if (!pTarget) {
        for (int i=0;i<units.GetSize();++i) LiveObserver::PlayerOrderResult(units[i]->GetID(), PID_CMD_ATTACK, "target_not_found_on_server");
        return TRUE;
    }
    for (int i = 0; i < units.GetSize(); i++)
    {
        CUnit *pUnit = units[i];
        CAIUnitLogic &logic = ((CUnitServer *)pUnit->GetUnitSpecific())->logic;
        CUMPlayer *pUP = logic.GetPlayer();
        if (pUP)
            pUP->DoAttack(logic, pTarget, hl, bRun != 0);
        LiveObserver::PlayerOrderResult(pUnit->GetID(), PID_CMD_ATTACK, pUP ? nullptr : "unit_has_no_player_logic");
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// не учитывает points of use, suitable for loot generally
static CUnit *ChooseSkillExecutor(CArray<CUnit *, CUnit *> &units, CMapObject *pTarget, int nSkill)
{
    if (units.GetSize() < 2)
        return units.GetSize() ? units[0] : 0;
    SAIPath fakePath;
    SUnitBasicCmd cmd;
    cmd.MakeSkill(pTarget, nSkill);
    CPlot3D ptTarget = cmd.GetTargetCP();
    CUnit *pClosest = 0;
    while (units.GetSize())
    {
        int nBestIdx = 0;
        // pick closest to target and see if it can do action
        float fpBest = 1e30f, fpDist;
        CUnit *pRes = 0;
        for (int i = 0; i < units.GetSize(); i++)
        {
            fpDist = fabs2(ptTarget, units[i]->GetCP());
            if (fpDist < fpBest)
            {
                fpBest = fpDist;
                pRes = units[i];
                nBestIdx = i;
            }
        }
        if (pRes)
        {
            if (!pClosest)
                pClosest = pRes;
            CUnitServer *pUS = (CUnitServer *)pRes->GetUnitSpecific();
            if (pUS->CanAttack(cmd))
                return pRes;
            //
            CPlot3D ptFrom;
            if (!theWorldServer.aiMap.NewPath(pRes, ptTarget, fakePath))
                ptFrom = pRes->GetCP();
            else
                ptFrom = fakePath.GetFinalPoint();
            if (pUS->CanAttackFrom(cmd, ptFrom))
                return pRes;

            units.RemoveAt(nBestIdx);
        }
        else
            break;
    }
    return pClosest; // кто-то должен сказать, что это невозможно
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsUnitCommandSkill()
{
    BOOL bRun;
    int nSkill;
    int nID, nItemID;
    CMapObject *pTarget;
    CArray<CUnit *, CUnit *> units;
    ReadUnitArray(pkt, units, PID_CMD_SKILL);
    pkt.Read(bRun);
    pkt.Read(nSkill);
    pkt.Read(nItemID);
    pkt.Read(nID);
    pTarget = theWorldServer.GetMapObject(nID);
    //
    if (!pTarget && units.GetSize()==1 && LiveObserver::IsPlayerOrder(units[0]->GetID(), PID_CMD_SKILL)) {
        LiveObserver::PlayerOrderResult(units[0]->GetID(), PID_CMD_SKILL, "loot_target_not_found");
        return TRUE;
    }
    CUnit *pActor = ChooseSkillExecutor(units, pTarget, nSkill);
    if (pActor)
    {
        CAIUnitLogic &logic = ((CUnitServer *)pActor->GetUnitSpecific())->logic;
        CUMPlayer *pUP = logic.GetPlayer();
        if (pUP)
            pUP->DoSkill(logic, pTarget, nSkill, nItemID, bRun != 0);
        LiveObserver::PlayerOrderResult(pActor->GetID(), PID_CMD_SKILL, pUP ? nullptr : "unit_has_no_player_logic");
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsUnitCommandLoot()
{
    ASSERT(0);
    return FALSE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsUnitCommandCastTarget()
{
    SNP_CMD_CAST_UNIT cast;
    pkt.Read(&cast, sizeof(cast));
    if (!LiveObserver::CheckPlayerOrder(cast.mapobjID, PID_CMD_CASTTARGET)) return TRUE;

    CMapObject *pCastTarget = GetWorldServer()->GetMapObject(cast.targetID);
    CUnit *pUnit = GetWorldServer()->GetUnit(cast.mapobjID);
    if (!pCastTarget || !pUnit)
    {
        LiveObserver::PlayerOrderResult(cast.mapobjID, PID_CMD_CASTTARGET, "caster_or_target_not_found_on_server");
        return TRUE;
    }
    if (pUnit && pUnit->CheckFlag(UNIT_BLOCKED))
        return TRUE;
    if (pUnit && !pUnit->IsDead())
    {
        CAIUnitLogic &logic = ((CUnitServer *)pUnit->GetUnitSpecific())->logic;
        CUMPlayer *pUP = logic.GetPlayer();
        if (pUP)
            pUP->DoCast(logic, cast.spellNum, pCastTarget, cast.bDoItFast != 0);
        LiveObserver::PlayerOrderResult(cast.mapobjID, PID_CMD_CASTTARGET, pUP ? nullptr : "unit_has_no_player_logic");
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsUnitCommandCastPoint()
{
    SNP_CMD_CAST_POINT cast;
    pkt.Read(&cast, sizeof(cast));
    if (!LiveObserver::CheckPlayerOrder(cast.mapobjID, PID_CMD_CASTPOINT)) return TRUE;

    CUnit *pUnit = GetWorldServer()->GetUnit(cast.mapobjID);
    if (!pUnit)
    {
        LiveObserver::PlayerOrderResult(cast.mapobjID, PID_CMD_CASTPOINT, "caster_or_target_not_found_on_server");
        return TRUE;
    }
    if (pUnit && pUnit->CheckFlag(UNIT_BLOCKED))
        return TRUE;
    if (pUnit && !pUnit->IsDead())
    {
        CAIUnitLogic &logic = ((CUnitServer *)pUnit->GetUnitSpecific())->logic;
        CUMPlayer *pUP = logic.GetPlayer();
        if (pUP)
            pUP->DoCast(logic, cast.spellNum, cast.targetCoord, cast.bDoItFast != 0);
        LiveObserver::PlayerOrderResult(cast.mapobjID, PID_CMD_CASTPOINT, pUP ? nullptr : "unit_has_no_player_logic");
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsUnitCommandSelWeapon()
{
    SNP_CMD_SELWEAPON cmd;
    pkt.Read(&cmd, sizeof(cmd));
    if (!LiveObserver::CheckPlayerOrder(cmd.mapobjID, PID_CMD_SELWEAPON)) return TRUE;

    CUnit *pUnit = GetWorldServer()->GetUnit(cmd.mapobjID);
    if (!pUnit) {
        LiveObserver::PlayerOrderResult(cmd.mapobjID, PID_CMD_SELWEAPON, "unit_not_found_on_server");
        return TRUE;
    }
    if (pUnit && pUnit->CheckFlag(UNIT_BLOCKED))
        return TRUE;
    if (pUnit && !pUnit->IsDead())
        ((CUnitServer *)pUnit->GetUnitSpecific())->CmdNewWeapon(cmd.cSlotToSelect);
    LiveObserver::PlayerOrderResult(cmd.mapobjID, PID_CMD_SELWEAPON);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsUnitCommandPose()
{
    SNP_CMD_PHASE cmd;
    pkt.Read(&cmd, sizeof(cmd));
    if (!LiveObserver::CheckPlayerOrder(cmd.mapobjID, PID_CMD_POSE)) return TRUE;
    CUnit *pUnit = GetWorldServer()->GetUnit(cmd.mapobjID);
    if (!pUnit) {
        LiveObserver::PlayerOrderResult(cmd.mapobjID, PID_CMD_POSE, "unit_not_found_on_server");
        return TRUE;
    }
    // if ( pUnit && pUnit->CheckFlag( UNIT_BLOCKED ) )
    // return TRUE;
    if (pUnit && !pUnit->IsDead())
        ((CUnitServer *)pUnit->GetUnitSpecific())->CmdNewPose((EUnitPose)cmd.bPose);
    LiveObserver::PlayerOrderResult(cmd.mapobjID, PID_CMD_POSE);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsUnitCommandTrap()
{
    int nID, nQItemNum;
    pkt.Read(&nID, 4);
    pkt.Read(&nQItemNum, 4);
    CUnit *pUnit = GetWorldServer()->GetUnit(nID);
    if (!pUnit)
        return TRUE;
    if (pUnit && pUnit->CheckFlag(UNIT_BLOCKED))
        return TRUE;
    ASSERT(0);
    /*if ( pUnit && !pUnit->IsDead() )
    {
            ((CUnitServer*)pUnit->GetUnitSpecific())->logic.PlayerStopSwarm();
            ((CUnitServer*) pUnit->GetUnitSpecific() )->CmdUseTrap( nQItemNum );
    }*/
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsUnitFollow()
{
    SNP_CMD_FOLLOW cmd;
    pkt.Read(&cmd, sizeof(cmd));
    if (!LiveObserver::CheckPlayerOrder(cmd.mapobjID, PID_CMD_FOLLOW)) return TRUE;
    CUnit *pUnit = GetWorldServer()->GetUnit(cmd.mapobjID);
    if (!pUnit) {
        LiveObserver::PlayerOrderResult(cmd.mapobjID, PID_CMD_FOLLOW, "unit_not_found_on_server");
        return TRUE;
    }
    CUnit *pTarget = GetWorldServer()->GetUnit(cmd.targetID);
    if (cmd.targetID >= 0 && !pTarget && LiveObserver::IsPlayerOrder(cmd.mapobjID, PID_CMD_FOLLOW)) {
        LiveObserver::PlayerOrderResult(cmd.mapobjID, PID_CMD_FOLLOW, "follow_target_not_found");
        return TRUE;
    }
    CAIUnitLogic &logic = ((CUnitServer *)pUnit->GetUnitSpecific())->logic;
    CUMPlayer *pUP = logic.GetPlayer();
    if (pUP)
        pUP->DoFollow(logic, pTarget);
    LiveObserver::PlayerOrderResult(cmd.mapobjID, PID_CMD_FOLLOW);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// Camera
BOOL CPlayerServer::PrsCameraPosition()
{
    SNP_CAMERAPOS camPos;
    pkt.Read(&camPos, sizeof(camPos));

    cameraCP.x = camPos.fpX;
    cameraCP.y = camPos.fpY;
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsCampItems()
{
    CMemorySave file;
    pkt.Read(file);
    file.Seek(0);
    if (pkt.IsOk())
        pPlayer->party.LoadItems(file);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsExitZone()
{
    int nExit, nZoneEntrance;
    CString szZone;
    pkt.Read(nExit);
    // determine where should player leave via this exit
    if (!theWorldServer.GetExitDirection(nExit, szZone, nZoneEntrance))
        return FALSE;
    LeaveToZone(szZone, nZoneEntrance);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsGoCamp()
{
    if(ModEncounter::AccessLocked(*GetWorldServer()))return FALSE;
    ASSERT(status == PSS_ACTIVE);
    if (status != PSS_ACTIVE)
        return FALSE;
    int nInterface;
    pkt.Read(nInterface);
    pPlayer->curInterface = EInterface(nInterface);
    theWorldServer.RemovePlayerParty(pPlayer, &partyPlace); //.RemovePlayerItems( pPlayer );
    SendParty();
    SendNewInterface();
    status = PSS_CAMP;
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsFinishCamp()
{
    ASSERT(status == PSS_CAMP);
    if (status != PSS_CAMP)
        return FALSE;
    status = PSS_ACTIVE;
    pPlayer->curInterface = GetWorldServer()->curInterface;
    theWorldServer.DeployPlayerParty(pPlayer, &partyPlace); // Items( pPlayer );
    pPlayer->SetChanged(DW_PCHANGED_INTERFACE);
    // SendNewInterface();
    // pPlayer->ResendEverything();
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsGoZone()
{
    int nZoneEntrance;
    CString szZone;
    szZone = pkt.ReadString();
    pkt.Read(nZoneEntrance);
    LeaveToZone(szZone, nZoneEntrance);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsBriefingComplete()
{
    CString szBriefing;
    szBriefing = pkt.ReadString();
    theWorldServer.BriefingComplete(pPlayer->GetPlayerNumber(), szBriefing);
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// for local mp only, very vulnurable in all respects

///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerServer::PrsZoneLoaded()
{
    if (status != PSS_ZONELOADWAIT)
        return FALSE;
    status = PSS_ACTIVE;
    WantToDeployParty();
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
