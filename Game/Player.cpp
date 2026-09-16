#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
/*==========================================================================================================

        Common player class implementation

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/
#include "Player.h"
#include "Unit.h"
#include "World.h"
#include "Net.h"
#include "Effect.h"
#include "PlayerManager.h"
#include "NetProtocol.h"
#include "ItemManager.h"

/////////////////////////////////////////////////////////////////////////////////////
// если это defined то все ошибки обработки сообщений между клиентом и сервером будут сопровождатьс
// комментарием
#define PROTOCOL_TEST
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL ReportProtocolError(const char *pszText, PACKET_ID cur, PACKET_ID prev)
{
#ifdef PROTOCOL_TEST
    CString error;
    error.Format(pszText, cur, prev);
    return ReportError(error);
#else
    return FALSE;
#endif
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// ---------------------------------- Create/destroy
// -------------------------------------------------------
CPlayer::CPlayer()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif
    pPlayerSpecific = NULL;
    bShowTraffic = FALSE;
    party.Create();
    Clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CPlayer::~CPlayer()
{
    Destroy();
    party.Destroy();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayer::Create()
{
    Clear();
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::Destroy()
{
    Clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::ForceObjectsSending()
{
    ReleaseHashMap((GameBaseHashMap &)visibleEffects);
    ReleaseHashMap((GameBaseHashMap &)visibleObjects);
    if (pPlayerSpecific)
        pPlayerSpecific->RemoveAllObjects();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::ResendEverything()
{
    ForceObjectsSending();
    dwChangedMask = DW_PCHANGED_EVERYTHING;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::RemoveAllObjects()
{
    POSITION pos;
    pos = items.GetHeadPosition();
    while (pos)
    {
        CItem *pItem = items.GetNext(pos);
        pItem->Release();
    }
    items.RemoveAll();
    ReleaseHashMap((GameBaseHashMap &)visibleEffects);
    ReleaseHashMap((GameBaseHashMap &)visibleObjects);
    // ReleaseHashMap( (GameBaseHashMap&)unitsUnderControl );
    while (1)
    {
        CUnitPtrHashIterator unitIter;
        CUnit *pUnit = unitIter.GetFirst(&unitsUnderControl);
        if (!pUnit)
            break;
        pUnit->SetOwner(NULL);
    }
    unitsUnderControl.RemoveAll();
    activeItems.RemoveAll();
    if (pPlayerSpecific)
        pPlayerSpecific->RemoveAllObjects();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::Clear()
{
    activeItems.RemoveAll();
    RemoveAllObjects();
    party.Destroy();
    party.Create();
    // Clear
    dwChangedMask = 0;
    SetPlayerNumber(-1);
    bGod = 0;

    if (pConnection)
        pConnection->Disconnect();
    pConnection = 0;
    SetPlayerSpecific(NULL);
    curInterface = IFACE_NONE;
    prevInterface = IFACE_NONE;
    nTimeWasAddedSegment = -1;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayer::IsMainHero(CUnit *pUnit)
{
    if (party.people.GetSize() == 0 || pUnit == 0)
        return FALSE;
    return pUnit->GetID() == party.people[0].nID;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::AddItemsToHoover(CList<CItem *, CItem *> &newItems, bool bCrippled)
{
    pPlayerSpecific->AddItemsToHoover(newItems, bCrippled);
    POSITION pos = newItems.GetHeadPosition();
    while (pos)
    {
        CItem *pItem = newItems.GetNext(pos);
        pItem->AddRef();
        items.AddTail(pItem);
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::RemoveAllHoover()
{
    pPlayerSpecific->RemoveAllHoover();
    while (items.GetCount())
        items.RemoveHead()->Release();
    items.RemoveAll();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayer::HasQuestItem(DWORD dwScriptID)
{
    POSITION pos = items.GetHeadPosition();
    while (pos)
    {
        CItem *pItem = items.GetNext(pos);
        if (pItem->GetTypeID() == ITID_ITEM_QUEST &&
            ((CItemQuest *)pItem)->GetScriptID() == dwScriptID)
            return TRUE;
    }
    CUnitPtrHashIterator unitIter;
    for (CUnit *pUnit = unitIter.GetFirst(&unitsUnderControl); pUnit; pUnit = unitIter.GetNext())
    {
        for (int k = 0; k < pUnit->GetQuestItemsNum(); k++)
        {
            CItemQuest *pItem = pUnit->GetQuestItem(k);
            if (pItem->GetScriptID() == dwScriptID)
                return TRUE;
        }
    }
    return FALSE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayer::RemoveQuestItem(DWORD dwScriptID)
{
    POSITION pos = items.GetHeadPosition(), delPos;
    while (pos)
    {
        delPos = pos;
        CItem *pItem = items.GetNext(pos);
        if (pItem->GetTypeID() == ITID_ITEM_QUEST &&
            ((CItemQuest *)pItem)->GetScriptID() == dwScriptID)
        {
            pPlayerSpecific->RemoveItemFromInventory(pItem);
            items.RemoveAt(delPos);
            pItem->Release();
            return TRUE;
        }
    }
    return FALSE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayer::RemoveItemByID(int nItemID)
{
    POSITION pos = items.GetHeadPosition(), delPos;
    while (pos)
    {
        delPos = pos;
        CItem *pItem = items.GetNext(pos);
        if (pItem->GetID() == nItemID)
        {
            pPlayerSpecific->RemoveItemFromInventory(pItem);
            items.RemoveAt(delPos);
            pItem->Release();
            return TRUE;
        }
    }
    return FALSE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::SetPlayerSpecific(LPGAMEBASE pNewPlayerSpecific)
{
    if (pPlayerSpecific)
    {
        pPlayerSpecific->RegisterPlayer(NULL);
        pPlayerSpecific->Release();
    }
    pPlayerSpecific = (CPlayerSpecific *)pNewPlayerSpecific;
    if (pPlayerSpecific)
    {
        pPlayerSpecific->RegisterPlayer(this);
        pPlayerSpecific->AddRef();
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// ---------------------------------------------------------------------------------------------------------
void CPlayer::SetConnection(CConnection *pCon)
{
    Disconnect();
    pConnection = pCon;
    pPlayerSpecific->NewConnection();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::Disconnect()
{
    if (pConnection)
        pPlayerSpecific->OnDisconnect();
    RemoveAllObjects();
    if (pConnection)
        pConnection->Disconnect();
    pConnection = 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::SetPlayerNumber(int nNewNumber)
{
    /*	CPlayerManager *pManager = ( CPlayerManager* ) lpManager;
            if( pManager )
            {
                    if( pManager->FindByNumber( nPlayerNum ) == this )
                            pManager->SetPlayer( NULL, nPlayerNum );
                    pManager->SetPlayer( this, nNewNumber );
            }*/
    nPlayerNum = nNewNumber;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::AddUnitUnderControl(CUnit *pUnit)
{
    ASSERT(pUnit);
    if (!pUnit)
        return;
    if (unitsUnderControl.Seek(pUnit->GetID()))
    {
        ASSERT(0); // multiple add unit under control
        return;
    }
    pUnit->AddRef();
    unitsUnderControl.SetAt(pUnit->GetID(), pUnit);
    GetPlayerSpecific()->UnitHasChangedOwner(pUnit);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::RemoveUnitFromControl(CUnit *pUnit)
{
    ASSERT(pUnit);
    if (!pUnit)
        return;
    if (!unitsUnderControl.Seek(pUnit->GetID()))
    {
        ASSERT(0); // trying to erase from control unit that is not under control of this player
                   // already
        return;
    }
    unitsUnderControl.RemoveAt(pUnit->GetID());
    pUnit->Release();
    GetPlayerSpecific()->UnitHasChangedOwner(pUnit);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::AddActiveItem(CItem *pItem)
{
    ASSERT(pItem->GetManager() == party.GetItemManager());
    if (!activeItems.Find(pItem->GetID()))
        activeItems.AddTail(pItem->GetID());
    pPlayerSpecific->AddActiveItem(pItem);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::SetParty(CParty &_party)
{
    activeItems.RemoveAll();
    party = _party;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::RemoveTamedFromControl()
{
    CUnitPtrHashIterator unitIter;
    for (CUnit *pUnit = unitIter.GetFirst(&unitsUnderControl); pUnit; pUnit = unitIter.GetNext())
    {
        if (pUnit->GetUnitSpecific()->GetCurrentTameLevel() != UTL_NONE)
            pUnit->SetOwner(0);
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayer::PrepareNewSegment()
{
    // update item`s mana
    for (POSITION pos = activeItems.GetHeadPosition(); pos;)
    {
        POSITION delPos = pos;
        int nID = activeItems.GetNext(pos);
        CItem *pItem = (CItem *)party.GetItemManager()->Find(nID);
        if (!pItem || !pItem->Segment())
            activeItems.RemoveAt(delPos);
    }
    //
    pPlayerSpecific->PrepareNewSegment();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// CPlayerSpecific
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPlayerSpecific::IsUnitUnderControl(CUnit *pUnit)
{
    if (!pUnit)
        return FALSE;
    return pPlayer->unitsUnderControl.Seek(pUnit->GetID()) != 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
