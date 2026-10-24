#pragma once
namespace SaveSchema { class Writer; class Reader; }
/*==========================================================================================================

        Client specific player class header

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

#include "NetProtocol.h"
#include "Player.h"
#include "simplyarray.h"
#include "Cruncher.h"

class CPlayerClient;
class CWorldClient;

typedef BOOL (CPlayerClient::*fPlayerClientParser)();
typedef unsigned ESPStatus;

/////////////////////////////////////////////////////////////////////////////////////
// ----------------------------- Client player
// -------------------------------------------------------------
class CPlayerClient : public CPlayerSpecific
{
    friend class SaveSchema::Slot;
public:
    enum EPClientStates
    {
        CPS_WAITING_FOR_CONNECTION, // connection not established
        CPS_WAITING_FOR_LOGIN_REPLY,
        CPS_ACTIVE, // normal connected state
        CPS_CAMP,   // camp state - doing constructor/camp
    };

private:
    EPClientStates status;                                            // status of player
    CSimplyArray<fPlayerClientParser, fPlayerClientParser &> parsers; // packet parsers function

    CPacket pkt;                 // packet for reading incoming data
    CPacket wpkt;                // packet for updates
    bool bIgnoreInterfaceChange; // for autoplay mode. ATM: not using now!
    bool bSendParty;             // reserved P17 payload field; remote party upload removed
    CNetCompressor packerSend, packerRecv;

    CString ReadMsg();

public:
    CSimplyArray<int, int> visibleUnitIDs;

    CPlayerClient();
    virtual ~CPlayerClient();
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    virtual void RemoveAllObjects();
    virtual void NewConnection();
    virtual void OnDisconnect();
    virtual void UnitHasChangedOwner(CUnit *pUnit);
    //
    void StartTrafficStoring(const char *pszFilename);
    void StopTrafficStoring();
    void SetIgnoreInterfaceChange(bool bIgnore) { bIgnoreInterfaceChange = bIgnore; }
    bool IsIgnoreInterfaceChange() { return bIgnoreInterfaceChange; }
    EPClientStates GetStatus() const { return status; }
    void FinishZoneLoading();
    void AdoptSavedConnection(CConnection *) noexcept;
    // Get Client world
    CWorldClient *GetWorldClient() { return (CWorldClient *)pPlayer->GetWorld(); }
    // packets
    virtual BOOL ParseIncomingPackets();
    void AppendPacket(CPacket &frame); // add to current packet
    // commands
    BOOL UnitCommandSwarm(CArray<CUnit *, CUnit *> &, CPlot3D &, BOOL bRun);
    BOOL UnitCommandMoveTo(CArray<CUnit *, CUnit *> &, CPlot3D &, BOOL bRun);
    BOOL UnitCommandAttack(CArray<CUnit *, CUnit *> &, CMapObject *pTarget, ERPGHitLocations hl,
                           BOOL bRun);
    BOOL UnitCommandSkill(CArray<CUnit *, CUnit *> &, CMapObject *pTarget, int nSkill, int nItemID,
                          BOOL bRun);
    BOOL UnitCommandSkill(CUnit *, CMapObject *pTarget, int nSkill, int nItemID, BOOL bRun);
    BOOL UnitCommandLoot(CArray<CUnit *, CUnit *> &, CUnit *pTarget, BOOL bRun);
    BOOL UnitCommandCast(int nQSpellNum, CUnit *pUnit, CMapObject *pTarget, BOOL bRun);
    BOOL UnitCommandCast(int nQSpellNum, CUnit *pUnit, CPlot3D &, BOOL bRun);
    BOOL UnitCommandWand(int nQItemNum, CUnit *pUnit, CMapObject *pTarget, BOOL bRun);
    BOOL UnitCommandWand(int nQItemNum, CUnit *pUnit, CPlot3D &, BOOL bRun);
    BOOL UnitCommandSelWeapon(CUnit *, int nQuickWeapSlotIndex);
    BOOL UnitCommandPose(CUnit *, EUnitPose pose);
    BOOL UnitCommandTrap(CUnit *pUnit, int nQItemNum);
    BOOL UnitFollow(CUnit *pWho, CUnit *pFollowTo);
    BOOL UnitCommandAggression(CUnit *pWho, EUnitAggression aggression);
    // camp realization requires sending back CPlayer::party
    BOOL SendPartyToServer();
    // general commands
    BOOL CommandCombatPause(bool bPause);
    void ExitZone(int nEntrance); // выйти зоны через выход nEntrance
    void GoCamp(int nInterface);
    void FinishCamp();
    void GoZone(const char *pszZone, int nEntrance);
    void BriefingComplete(const char *pszBrfVar);
    // void Disconnect();
    // segment
    virtual int Segment();
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CPlayerClient"; }
    virtual EClassIDs WhoAmI() const { return HPC_PLAYERCLIENTSPECIFIC; }

private:
    void Clear();
    BOOL SendUpdate();

    BOOL ParseCurrentPkt();
    // parsers
    BOOL PrsLogged();
    BOOL PrsMoveCamera();
    BOOL PrsMoveCamera2();
    BOOL PrsCreateMapobj();
    BOOL PrsUpdateMapobj();
    BOOL PrsRemoveMapobj();
    BOOL PrsCreateEffect();
    BOOL PrsUpdateEffect();
    BOOL PrsRemoveEffect();
    BOOL PrsFailedAction();
    BOOL PrsEvent();
    BOOL PrsStringEvent();
    BOOL PrsVisibleUnits();
    BOOL PrsNewZone();
    BOOL PrsNewItems();
    BOOL PrsAddItems();
    BOOL PrsRemoveItem();
    BOOL PrsNewHooverItems();
    BOOL PrsWeather();
    BOOL PrsWorldTime();
    BOOL PrsWind();
    BOOL PrsGSSetVar();
    BOOL PrsGSDelVar();
    BOOL PrsParty();
    BOOL PrsPartyItems();
    BOOL PrsNewInterface();
    BOOL PrsServerChange();
    BOOL PrsGameOver();
    BOOL PrsItemsMana();
    BOOL PrsMusic();
    BOOL PrsShowPath();
    BOOL PrsUnitDeath();
    BOOL PrsWaterLevels();
    BOOL PrsLogout();
    BOOL PrsActiveItem();
    BOOL PrsGainExp();
    BOOL PrsRemoveWeapon();
    BOOL PrsRemoveHoover();
    BOOL PrsUnitGainExp();
    BOOL PrsSelect();
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
