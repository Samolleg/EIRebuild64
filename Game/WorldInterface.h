#pragma once
// classes for access to worldserver and worldclient from interface etc
// this classes should be used instead of worldserver and worldclient whereever it is possible
#include "ServerConsts.h"
#include "Quaternion.h"
#include "ScriptErrors.h"
#include "GSVars.h"
#include "party.h"
/////////////////////////////////////////////////////////////////////////////////////
class CTextManager;
class CItem;
class CWorld;
class CSave;
class CUnit;
class CParty;
class CPlayerClient;
struct SScriptError;
struct SRPGUnitStats;
class CMemoryStream;
/////////////////////////////////////////////////////////////////////////////////////
extern int N_CURRENT_SAVE_VERSION;
extern int N_CURRENT_MINIMAL_SAVE_VERSION;
extern bool DecompressTo(CMemorySave &dst, int nSignature, int nVersion, int nMinVersion,
                         CSave &src);
extern void CompressTo(CSave &dst, int nSignature, int nVersion, CMemorySave &src);
/////////////////////////////////////////////////////////////////////////////////////
class CWorldServerInterface
{
public:
    BOOL IsMapLoaded();
    // script loading
    BOOL LoadScript(const char *pszFileName);
    BOOL LoadScriptText(CString &szFile, bool bInstantExec, SScriptError *pError = 0);
    // this function is used to restart script on zone after loading a saved zone on personages
    // reenter
    void LoadScriptFromMOB(const char *pszMobName);
    SScriptError GetLastScriptError();
    //
    void SetWeather(EWeather newWeather, float fpTransTime);
    void SetWorldTime(float fpTime);
    float GetWorldTime();
    void AddPlayersGlobalTime(float fpTimeShift);
    void CheckZone(const char *pszMapName,
                   const char *pszMobName); // for precaching only (on client)
    CPacket &GetInitialUpdate();
    void SetConstructorNumber(float fpN);
    void SetUnitZ(int nID, float fpZ);
    // interface to variables of global script
};
/////////////////////////////////////////////////////////////////////////////////////
class CWorldClientInterface
{
public:
    // some camera stuff
    void StopCameraPlay();
    void StopCameraSave();
    void StartCameraSave(const char *pszFileName);
    BOOL StartCameraPlay(const char *pszFileName, int nRemove = 0);
    void LoadCameraPosition(const char *pszFileName);
    BOOL IsCameraPlaying();
    CPlot3D GetCameraCP();
    // pick units from camera, returs ID
    int PickFirstUnit(const CPoint &point);
    // sound interface
    void StopAllSounds(bool bStopStreaming = true);
    void DisableAmbientSounds();
    void EnableAmbientSounds();
    void BeginStreaming(const char *pszFilename = 0);
    // exit points
    void DeployClientPersonages(CPersonages &people, bool bShowHelm = true);
    void RemoveClientPersonages(CPersonages &people);
    void ExitZone(int nExitPoint);
    BOOL IsExitPoint(const CPlot3D &ptCheck, int *nExitPoint = NULL);
    BOOL ArePersonagesAtExit(CPersonages &people, int nExitPoint, CPersonages &exitingPeople);
    LPCTSTR GetExitZoneName(int nExitPoint);

    CString GetCurrentZoneName();
    // camping
    void GoCamp(EInterface iface = IFACE_CAMP);
    void GoFieldCamp();
    bool BeginFieldCampCommerceSuppression();
    void EndFieldCampCommerceSuppression(bool active);
    void FinishConstructing();
    // rendering
    void Draw();
    // pointers on units
    CUnit *GetUnit(int nID);
    void ResetSelection(CUnit *pUnit); // CRAP
    // client time in segments
    float GetPreciseTime();
    // interface to variables of global script
    float GSGetVar(const char *pszName);
    void GSGetVars(const char *pszName, CGSVariableSet &vars);
    // access to party
    CParty &GetParty();
    // access to our player
    CPlayerClient *GetPlayer();
    EWorldDangerState GetDangerState();
};
/////////////////////////////////////////////////////////////////////////////////////
class CWorldInterface
{
public:
    //
    void CombatPause(bool bPause);
    bool IsCombatPause();
    void MissionCombatPause(bool bPause);
    bool IsMissionCombatPause();
    // settings
    void FlipDrawVisibleOnly();
    float GetTerrainFogDistance();
    void SetTerrainFogDistance(float fpVal);
    void SetAdjustLighting(bool bAdjust);
    const CString &GetWorkPath();
    const CString &GetCDPath();
    const CString &GetDefaultStreamingSound();
    CString GetMobPath();
    //
    float GSGetVar(const char *pszName);
    void GSGetVars(const char *pszName, CGSVariableSet &vars);
    // cheat protect
    void SetActiveState(bool bActive);
    // CRAP
    CTextManager *GetTextManager();
    void InvokeLightning(CPlot3D ptFrom, CPlot3D ptTo);
};
/////////////////////////////////////////////////////////////////////////////////////
class CWorldConnector
{
    enum EConnectAction
    {
        CD_NONE,
        CD_MAINMENU,
        CD_LOADGAME,
        CD_NEWGAME,
        CD_STARTPREPAREDNEWGAME,
        CD_MOVETOZONE,
    };
    EConnectAction action;
    CString szConnectDest;
    // load&store server&client states
    BOOL LoadSave(CSave &file);
    void StoreSave(CSave &file);
    // служебные функции для считывания/записи состояния зоны
    BOOL LoadZoneState(CParty &party, CMemorySave *prepared = nullptr);
    BOOL StoreZoneState(CParty &party);
    // connections
    void ConnectLocalServer();
    void Disconnect();
    // most general operations on game world
    void NewGame();
    void GoMainMenu();
    BOOL LoadGame(const char *pszSaveName);
    BOOL SaveGame(const char *pszSaveName);
    // переключить клиента на другой сервер, в случае сингла старый сервер убивается,
    // вместо него запускается новый, вызывается только из playerclient::prsserverchange()
    // для смены карты использовать IWorldClient::ExitZone() или GoZone()
    BOOL MoveToZone(const char *pszZoneName, EInterface iface = IFACE_NONE);

public:
    CWorldConnector() { action = CD_NONE; }
    void DoGo();
    bool HasReq() { return action != CD_NONE; }
    void ReqGoMainMenu();
    void ReqNewGame();
    // Resume only the already prepared new-game transaction after UI input.
    void ReqStartPreparedNewGame();
    void ReqLoadGame(const char *pszSaveName);
    void ReqSaveGame(const char *pszSaveName);
    void ReqMoveToZone(const char *pszZoneName, int nEntrance);
    int GetSaveSize();
    // client side funcs
    void ClientSideDisconnect();
    // эта функция может быть использована для перехода на другую карту в single player mode
    void ClientGoZone(const char *pszZoneName, int nEntrance);
};
/////////////////////////////////////////////////////////////////////////////////////
extern CWorldServerInterface IWorldServer;
extern CWorldClientInterface IWorldClient;
extern CWorldInterface IWorld;
extern CWorldConnector IWorldConnector;
extern CMemorySave interfaceState;
/////////////////////////////////////////////////////////////////////////////////////
