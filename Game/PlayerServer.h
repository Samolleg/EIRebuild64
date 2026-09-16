#pragma once
/*==========================================================================================================

        Server specific player class header

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

#include "NetProtocol.h"
#include "Player.h"
#include "Script.h"
#include "Cruncher.h"

/////////////////////////////////////////////////////////////////////////////////////
class CPlayerServer;
class CWorldServer;
class CAIFullRoute;

/////////////////////////////////////////////////////////////////////////////////////
typedef BOOL (CPlayerServer::*fPlayerServerParser)();
typedef unsigned ESPStatus;

/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
// ----------------------------- Server player
// -------------------------------------------------------------
class CPlayerServer : public CPlayerSpecific
{
public:
    enum EPServerStates
    {
        PSS_WAITING_FOR_LOGIN, // connection is established but no login is passed
        PSS_ACTIVE,            // normal connected state
        PSS_CAMP,              // client is doing some camping
        PSS_LOGOUT,            // client was disconnected
        PSS_ZONELOADWAIT,      // waiting for zone loading on client side
    };

private:
    EPServerStates status; // status of player

    int nLookFromID; // != -1 if camera is to be placed on some map object

    CSimplyArray<fPlayerServerParser, fPlayerServerParser &> parsers; // packet parsers function

    CPlot3D cameraCP; // player's camera position
    int nCameraUpdateCounter;
    static CPacket pkt;           // packet for reading incoming data
    CPacket wpkt;                 // packet for updates
    bool bPartyDeployed;          // is party deployed on server or not
    SDeployPlayerInfo partyPlace; // information about units placement
    CNetCompressor packerSend, packerRecv;

    void WantToDeployParty();
    void WantToRemoveParty();

public:
    // this fields are modified by SelectID during CWorldServer::UpdatePlayer()
    // they have same pointers as appropriate fields of CPlayer
    // purpose of this array is to simplify SelectIDs()
    CGameBasePtrArray visibleObjects;  // current visible units
    CGameBasePtrArray visibleEffects;  // current visible effects
    CScriptObjectGroup addToVisible;   // additional visible units
    CScriptObjectGroup currentVisible; // currently visible for player units
    EWorldDangerState dangerState;     // current danger state
    int nStorePartyCounter;            // for periodic party store on client

public:
    CPlayerServer();
    virtual ~CPlayerServer();
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    virtual void RemoveAllObjects();
    virtual void NewConnection();
    virtual void OnDisconnect();
    virtual void PrepareNewSegment();
    //
    EPServerStates GetStatus() { return status; }
    bool NeedItemsUpdate() { return status == PSS_ACTIVE; }
    bool IsUnitVisibleToParty(CUnit *pTarget);
    // handling owner change
    virtual void UnitHasChangedOwner(CUnit *pUnit);
    // Get server world
    CWorldServer *GetWorldServer() { return (CWorldServer *)pPlayer->GetWorld(); }
    // packets
    virtual BOOL ParseIncomingPackets();
    BOOL SendUpdate();
    void CopyUpdatePkt(CPacket &dst);

    void SendMsg(PACKET_ID pID, LPCSTR pszMessage);

    BOOL AddMapObjects(CGameBasePtrArray &toAdd);
    BOOL RemoveMapObjects(CGameBasePtrArray &toRemove);
    BOOL UpdateMapObjects(CGameBasePtrArray &toUpdate);
    BOOL RemoveMapObjectRefs(CMapObject *pObject);

    BOOL AddEffects(CGameBasePtrArray &toAdd);
    BOOL UpdateEffects(CGameBasePtrArray &toUpdate);
    BOOL RemoveEffects(CGameBasePtrArray &toRemove);

    void FormNewVisibleSet();
    BOOL SendVisibleUnitsList();
    BOOL SendNewZone(CMemorySave &packet);
    // BOOL SendNewItems( CMemorySave &packet );
    BOOL SendAddItems(CMemorySave &packet);
    void SetPartyIsDeployed(bool bIsDeployed);
    bool IsPartyDeployed() const { return bPartyDeployed; }

    BOOL SendWeather(BYTE bWeather, int nTime);
    BOOL SendWorldTime(float fpTimeModifier, float fpTimeScale);
    BOOL SendMoveCamera(CPlot3D &ptPos, float fAngle = 0, EInterface eIFace = IFACE_MISSION);
    void SendMoveCamera(const CPlot3D &ptSrc, const CPlot3D &ptDst, bool bLock = true);
    BOOL SendWind(CPlot3D ptWind, float fpWindStrength);

    void SendShowPath(CUnit *pWho, CAIFullRoute &route, const CPlot3D &ptDest,
                      CPlayer::EPathType type, CPlayer::EPathResult res, float fpShowLeng = 1e6f);
    void SendDeathEvent(CUnit *pWho);
    void SendFailure(CMapObject *pObject, DWORD dwAction);
    void SendEvent(char bPlayerNum, DWORD dwEvent);
    void SendStringEvent(char bPlayerNum, const char *pszStringEvent);
    void SendWaterLevels(CWaterLevels &water);
    void SetVar(const char *pszName, float fpVal);
    void SetVarMax(const char *pszName, float fpVal);
    void DelVar(const char *pszName);
    void SendParty();
    void SendActiveItems();
    void SendPartyItems();
    void SendNewInterface();
    void SendServerChangeCmd(const char *pszName, int nEntrance);
    void LeaveToZone(const char *pszZone, int nZoneEntrance);
    void SendItemsMana(CList<CItem *, CItem *> &items);
    void AddActiveItem(CItem *pItem);
    void PlayMusic(const char *pszMusicName);
    virtual void GameOver();
    void StartZoneLoading();
    void GainExperience(float fpExp);
    void GainExp(int nID, float fpExp);
    void ResendParty();
    void Select(EPCSelectCmd cmd, int nID);

    virtual void AddItemsToHoover(CList<CItem *, CItem *> &newItems, bool bCrippled);
    virtual void RemoveAllHoover();
    virtual void RemoveItemFromInventory(CItem *pItem);
    void RemoveWeaponFromUnit(int nUnitID, int nWeaponIdx);
    // camera
    CPlot3D &GetCameraCP() { return cameraCP; }
    void SetCameraOn(int nID) { nLookFromID = nID; }
    bool IsCameraBound() const { return nLookFromID >= 0; }

    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CPlayerServer"; }
    virtual EClassIDs WhoAmI() const { return HPC_PLAYERSERVERSPECIFIC; }

private:
    void Clear();

    // parsers
    BOOL PrsLogin();
    BOOL PrsLogout();
    BOOL PrsReconnect();
    BOOL PrsCombatPause();
    BOOL PrsUnitCommandMoveTo();
    BOOL PrsUnitCommandSwarm();
    BOOL PrsUnitCommandAttack();
    BOOL PrsUnitCommandSkill();
    BOOL PrsUnitCommandLoot();
    BOOL PrsUnitCommandCastTarget();
    BOOL PrsUnitCommandCastPoint();
    BOOL PrsUnitCommandSelWeapon();
    BOOL PrsUnitCommandPose();
    BOOL PrsUnitCommandTrap();
    BOOL PrsUnitCommandAggresion();
    BOOL PrsUnitFollow();
    BOOL PrsCameraPosition();
    BOOL PrsCampItems();
    BOOL PrsExitZone();
    BOOL PrsGoCamp();
    BOOL PrsFinishCamp();
    BOOL PrsGoZone();
    BOOL PrsBriefingComplete();
    BOOL PrsClientParty();
    BOOL PrsZoneLoaded();
};
/////////////////////////////////////////////////////////////////////////////////////
