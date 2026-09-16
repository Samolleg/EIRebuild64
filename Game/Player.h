#pragma once
/*==========================================================================================================

        Common player class header

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/
#include "GameBase.h"
#include "PacketIDs.h"
#include "Basic.h"
#include "Net.h"
#include "party.h"
#include "ServerConsts.h"

/////////////////////////////////////////////////////////////////////////////////////
class CPlayerSpecific;
class CConnection;

/////////////////////////////////////////////////////////////////////////////////////
extern BOOL ReportProtocolError(const char *pszText, PACKET_ID cur, PACKET_ID prev);

/////////////////////////////////////////////////////////////////////////////////////
const int DW_PCHANGED_ZONE = 1;
const int DW_PCHANGED_WEATHER = 2;
const int DW_PCHANGED_TIME = 4;
const int DW_PCHANGED_WIND = 8;
const int DW_PCHANGED_INTERFACE = 16;
const int DW_PCHANGED_WATERLEVEL = 32;
const int DW_PCHANGED_EVERYTHING = 0xFFFFFFFF;
/////////////////////////////////////////////////////////////////////////////////////
enum EPCSelectCmd
{
    PSEL_SELECT,
    PSEL_PRESELECT_TRUE,
    PSEL_PRESELECT_FALSE,
    PSEL_RESETALL
};
/////////////////////////////////////////////////////////////////////////////////////
// ------------------------------------- Player
// ------------------------------------------------------------
class CPlayer : public CGameBase
{
public:
    enum EPathType
    {
        PPT_MOVE,
        PPT_USE,
        PPT_ATTACK,
        PPT_NOPATH,
    };
    enum EPathResult
    {
        PPR_OK,
        PPR_FAILED,
    };

private:
    CPtr<CConnection> pConnection;
    CPlayerSpecific *pPlayerSpecific;

    int nPlayerNum; // номер этого human плеера для системы свой-чужой

    void ForceObjectsSending();

public:
    CMapObjPtrHash visibleObjects;  // visible map objects
    CUnitPtrHash unitsUnderControl; // controlable units
    CEffectPtrHash visibleEffects;  // visible effects

    CParty party;                  // люди принадлежащие данному player`u
    CList<CItem *, CItem *> items; // items in hoover
    SPartyID partyID;              // уникальный идентификатор нашего отряда
    EInterface curInterface;       // текущий интерфейс по мнению сервера
    EInterface prevInterface;      // предыдущей интерфейс
    BYTE bGod;                     // make life easier
    BOOL bShowTraffic;             // for debug purposes
    int nTimeWasAddedSegment;      // segment when time was added last time
    CList<int, int> activeItems;   // all active items for whom segment is called

    DWORD dwChangedMask;

public:
    CPlayer();
    virtual ~CPlayer();
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    void RemoveAllObjects();
    void ResendEverything();
    void SetConnection(CConnection *pCon);
    CConnection *GetConnection() { return pConnection; }
    // bool IsConnected() { return pConnection.IsValid(); }
    // specific data
    void SetPlayerSpecific(LPGAMEBASE pNewPlayerSpecific);
    CPlayerSpecific *GetPlayerSpecific() { return pPlayerSpecific; }
    void Disconnect();
    void SetPlayerNumber(int nNewNumber);
    int GetPlayerNumber() { return nPlayerNum; }
    //
    BOOL IsChanged(DWORD dwCheck) { return dwChangedMask & dwCheck; }
    void ResetChanged(DWORD dwReset) { dwChangedMask &= ~dwReset; }
    void SetChanged(DWORD dwSet) { dwChangedMask |= dwSet; }
    // this functions should be called only from CUnit::SetOwner
    void AddUnitUnderControl(CUnit *pUnit);
    void RemoveUnitFromControl(CUnit *pUnit);
    //
    void AddActiveItem(CItem *pItem); // item should belong to party
    void SetParty(CParty &_party);
    //
    void RemoveTamedFromControl();
    // this function is called before segments for different objects are processed
    void PrepareNewSegment();
    //
    void SetInterface(EInterface inter)
    {
        curInterface = inter;
        SetChanged(DW_PCHANGED_INTERFACE);
    }
    //
    void AddItemsToHoover(CList<CItem *, CItem *> &newItems, bool bCrippled = false);
    void RemoveAllHoover();
    // check if player got a quest item at inventory or one of his units has it
    BOOL HasQuestItem(DWORD dwScriptID);
    // remove quest item from inventroy and send information about it to client
    // this function does not remove quest items from units!
    BOOL RemoveQuestItem(DWORD dwScriptID);
    // this function is used to remove quest item with given ID
    BOOL RemoveItemByID(int nItemID);
    //
    BOOL IsMainHero(CUnit *pUnit);

    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CPlayer"; }
    virtual EClassIDs WhoAmI() const { return HPC_PLAYER; }

private:
    void Clear();
};

/////////////////////////////////////////////////////////////////////////////////////
// ------------------------------------- Player specific
// ---------------------------------------------------
class CPlayerSpecific : public CGameBase
{
protected:
    CPlayer *pPlayer;

public:
    CPlayerSpecific() { pPlayer = NULL; }
    virtual ~CPlayerSpecific() { RegisterPlayer(NULL); }
    void RegisterPlayer(CPlayer *pNewPlayer) { pPlayer = pNewPlayer; }

    virtual void RemoveAllObjects() = 0;
    virtual void UnitHasChangedOwner(CUnit *pUnit) {}
    virtual void NewConnection() {}
    virtual void OnDisconnect() {}
    virtual void AddItemsToHoover(CList<CItem *, CItem *> &newItems, bool bCrippled) {}
    virtual void RemoveAllHoover() {}
    virtual void RemoveItemFromInventory(CItem *pItem) {}
    virtual void GameOver() {}
    virtual void PrepareNewSegment() {}
    virtual void AddActiveItem(CItem *pItem) {}
    BOOL IsUnitUnderControl(CUnit *pUnit);
    CPlayer *GetPlayer() { return pPlayer; }
    // Packet parsing
    virtual BOOL ParseIncomingPackets() = 0;
};
/////////////////////////////////////////////////////////////////////////////////////
