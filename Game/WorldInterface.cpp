#include "StdAfx.h"
#include "GameJournal.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "WorldInterface.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "ModAnimationIdSave.h"
#include "WorldClient.h"
#include "WorldServer.h"
#include "Camera.h"
#include "Terrain.h"
#include "PlayerManager.h"
#include "PlayerClient.h"
#include "Net.h"
#include "MapObjectManager.h"
#include "GlobalMap.h"
#include "ScenarioTracker.h"
#include "RandomGen.h"
#include "I3DMain.h"
#include "Console.h"
#include "UnitServer.h"
#include "PlayerServer.h"
#include "A3Cursor.h"
#include "ChatServer.h"
#include "TextManager.h"
#include <algorithm>
#include "QuestDatabase.h"
#include "Cruncher.h"
#include "CheckedSave.h"
#include "PreparedSave.h"
#include <memory>
#include "Streams.h"
#include "BinkVideo.h"
#include "PerkDatabase.h"
#include "SpellDatabase.h"
#include "ItemDatabase.h"

/////////////////////////////////////////////////////////////////////////////////////
CWorldServerInterface IWorldServer;
CWorldClientInterface IWorldClient;
CWorldInterface IWorld;
CWorldConnector IWorldConnector;
CMemorySave interfaceState;
/////////////////////////////////////////////////////////////////////////////////////
int N_CURRENT_SAVE_VERSION = 117;
int N_COMPRESS_SAVE_VERSION = 100;
int N_CURRENT_MINIMAL_SAVE_VERSION = 66;
/////////////////////////////////////////////////////////////////////////////////////
// CWorldServerInterface
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServerInterface::SetConstructorNumber(float fpN)
{
    CPlayer *pPlayer = theWorldServer.GetPlayer(0);
    if (pPlayer)
        ((CPlayerServer *)pPlayer->GetPlayerSpecific())->SetVar("constr.current", fpN);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServerInterface::LoadScriptFromMOB(const char *pszMobName)
{
    theWorldServer.LoadScriptFromMOB(pszMobName);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldServerInterface::LoadScript(const char *pszFileName)
{
    return theWorldServer.LoadScript(pszFileName);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldServerInterface::LoadScriptText(CString &szFile, bool bInstantExec, SScriptError *pError)
{
    BOOL bRv = theWorldServer.scriptTemp.LoadText(szFile, TRUE);
    if (bRv && bInstantExec)
        theWorldServer.scriptTemp.Segment();
    if (pError)
        pError[0] = theWorldServer.scriptTemp.GetLastError();
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldServerInterface::IsMapLoaded()
{
    return theWorldServer.IsMapLoaded();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServerInterface::SetWeather(EWeather newWeather, float fpTransTime)
{
    theWorldServer.SetWeather(newWeather, fpTransTime);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServerInterface::SetWorldTime(float fpTime)
{
    theWorldServer.SetWorldTime(fpTime);
}
/////////////////////////////////////////////////////////////////////////////////////
float CWorldServerInterface::GetWorldTime()
{
    return theWorldServer.GetWorldTime();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServerInterface::AddPlayersGlobalTime(float fpTimeShift)
{
    if (fpTimeShift < 0)
        return;
    theWorldServer.AddPlayersGlobalTime(fpTimeShift);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServerInterface::CheckZone(const char *pszMapName, const char *pszMobName)
{
    theWorldServer.CheckZone(pszMapName, pszMobName);
}
/////////////////////////////////////////////////////////////////////////////////////
CPacket &CWorldServerInterface::GetInitialUpdate()
{
    return theWorldServer.theWSInitialUpdate;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldServerInterface::SetUnitZ(int nID, float fpZ)
{
    CUnit *pUnit = theWorldServer.GetUnit(nID);
    if (!pUnit)
        return;
    float fpOldZ = pUnit->GetCP().z;
    if (fpZ == fpOldZ)
        return;
    theWorldServer.aiMap.MoveObject(pUnit, CPlot3D(pUnit->GetCP().x, pUnit->GetCP().y, fpZ));
    //	((CUnitServer*)pUnit->GetUnitSpecific())->InitializeUnit();
    pUnit->SetUpdateMask(CMapObject::VLD_MASK_POSROT);
}
/////////////////////////////////////////////////////////////////////////////////////
// CWorldClientInterface
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::StopCameraPlay()
{
    theWorld.GetCamera()->EndPlayCamera();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::StopCameraSave()
{
    theWorld.GetCamera()->EndCameraPlacementsSave();
    theWorld.ResetFlag(WC_SAVE_CAMERA);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::StartCameraSave(const char *pszFileName)
{
    theWorld.SetFlag(WC_SAVE_CAMERA);
    theWorld.GetCamera()->BeginCameraPlacementsSave(pszFileName);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldClientInterface::StartCameraPlay(const char *pszFileName, int nRemove)
{
    if (!theWorld.GetCamera()->LoadCameraPlacements(pszFileName, nRemove))
        return FALSE;
    theWorld.GetCamera()->BeginPlayCamera(theWorld.GetPreciseTime());
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::LoadCameraPosition(const char *pszFileName)
{
    if (theWorld.GetCamera()->LoadCameraPlacements(pszFileName))
        theWorld.GetCamera()->InitCameraPlacement();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldClientInterface::IsCameraPlaying()
{
    return theWorld.GetCamera()->IsPlaying();
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot3D CWorldClientInterface::GetCameraCP()
{
    return theWorld.GetCamera()->GetCP();
}
/////////////////////////////////////////////////////////////////////////////////////
int CWorldClientInterface::PickFirstUnit(const CPoint &point)
{
    CUnit *pUnit = (CUnit *)(theWorld.PickFirstUnit(point));
    if (pUnit)
        return pUnit->GetID();
    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::Draw()
{
    theWorld.Draw();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::StopAllSounds(bool bStopStreaming)
{
    theWorld.GetSoundManager()->StopAllSounds(bStopStreaming);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::DisableAmbientSounds()
{
    theWorld.GetTerrain()->DisableAmbientSounds();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::EnableAmbientSounds()
{
    theWorld.GetTerrain()->EnableAmbientSounds();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::BeginStreaming(const char *pszFilename)
{
    if (pszFilename)
        theWorld.GetSoundManager()->BeginStreaming(pszFilename);
    else
        theWorld.GetSoundManager()->BeginStreaming(IWorld.GetDefaultStreamingSound());
}
/////////////////////////////////////////////////////////////////////////////////////
CUnit *CWorldClientInterface::GetUnit(int nID)
{
    return theWorld.GetUnit(nID);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::ResetSelection(CUnit *pUnit)
{
    theWorld.ResetSelection(pUnit);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::DeployClientPersonages(CPersonages &people, bool bShowHelm)
{
    theWorld.DeployClientPersonages(people, bShowHelm);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::RemoveClientPersonages(CPersonages &people)
{
    theWorld.RemoveClientPersonages(people);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldClientInterface::IsExitPoint(const CPlot3D &ptCheck, int *nExitPoint)
{
    return theWorld.IsExitPoint(ptCheck, nExitPoint);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldClientInterface::ArePersonagesAtExit(CPersonages &people, int nExitPoint,
                                                CPersonages &exitingPeople)
{
    return theWorld.ArePersonagesAtExit(people, nExitPoint, exitingPeople);
}
/////////////////////////////////////////////////////////////////////////////////////
LPCTSTR CWorldClientInterface::GetExitZoneName(int nExitPoint)
{
    return theWorld.GetExitZoneName(nExitPoint);
}
/////////////////////////////////////////////////////////////////////////////////////
CString CWorldClientInterface::GetCurrentZoneName()
{
    return theWorld.GetPlayer()->GetPlayer()->party.szCurrentZone;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::ExitZone(int nExitPoint)
{
    GameJournal::Event("ZONE", "request", "reason=exit point=%d zone=%s", nExitPoint, GameJournal::Quote(theWorld.GetExitZoneName(nExitPoint)).c_str());
    if (pChatClient->IsValid())
        IWorldConnector.LMPExitZone();
    else
        theWorld.GetPlayer()->ExitZone(nExitPoint);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::GoCamp(EInterface iface)
{
    theInterface.SetScreensMustDie();
    theWorld.GetPlayer()->GoCamp(iface);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::GoFieldCamp()
{
    CPlayer *player = theWorldServer.GetPlayer(0);
    CPlayerServer *server = player ? static_cast<CPlayerServer *>(player->GetPlayerSpecific()) : nullptr;
    if (!server || !theWorldServer.BeginFieldCamp()) return;
    server->SetVar("Camp", 2.0f);
    GoCamp(IFACE_CAMP);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWorldClientInterface::BeginFieldCampCommerceSuppression()
{
    return theWorldServer.BeginFieldCampCommerceSuppression();
}
void CWorldClientInterface::EndFieldCampCommerceSuppression(bool active)
{
    theWorldServer.EndFieldCampCommerceSuppression(active);
}
void CWorldClientInterface::FinishConstructing()
{
    theInterface.SetScreensMustDie();
    theWorld.GetPlayer()->FinishCamp();
}
/////////////////////////////////////////////////////////////////////////////////////
float CWorldClientInterface::GetPreciseTime()
{
    return theWorld.GetPreciseTime();
}
/////////////////////////////////////////////////////////////////////////////////////
float CWorldClientInterface::GSGetVar(const char *pszName)
{
    return theWorld.GetPlayer()->GetPlayer()->party.vars.GetVar(pszName);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClientInterface::GSGetVars(const char *pszName, CGSVariableSet &vars)
{
    theWorld.GetPlayer()->GetPlayer()->party.vars.GetVars(pszName, vars);
}
/////////////////////////////////////////////////////////////////////////////////////
CParty &CWorldClientInterface::GetParty()
{
    return theWorld.GetPlayer()->GetPlayer()->party;
}
/////////////////////////////////////////////////////////////////////////////////////
CPlayerClient *CWorldClientInterface::GetPlayer()
{
    return theWorld.GetPlayer();
}
/////////////////////////////////////////////////////////////////////////////////////
EWorldDangerState CWorldClientInterface::GetDangerState()
{
    return theWorld.GetDangerState();
}
/////////////////////////////////////////////////////////////////////////////////////
// CWorldInterface
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
float CWorldInterface::GSGetVar(const char *pszName)
{
    if (IWorldConnector.IsLMP())
        return IWorldClient.GSGetVar(pszName);
    CPlayer *pPlayer = theWorldServer.GetPlayer(0);
    if (!pPlayer)
        return 0;
    return pPlayer->party.vars.GetVar(pszName);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldInterface::GSGetVars(const char *pszName, CGSVariableSet &vars)
{
    if (IWorldConnector.IsLMP())
    {
        IWorldClient.GSGetVars(pszName, vars);
        return;
    }
    CPlayer *pPlayer = theWorldServer.GetPlayer(0);
    if (pPlayer)
        pPlayer->party.vars.GetVars(pszName, vars);
    else
        vars.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWorldInterface::IsCombatPause()
{
    return theWorldServer.IsCombatPause();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldInterface::CombatPause(bool bPause)
{
    theWorldServer.RequestCombatPause(bPause);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldInterface::MissionCombatPause(bool bPause)
{
    theWorld.CombatPause(bPause);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWorldInterface::IsMissionCombatPause()
{
    return theWorld.IsCombatPause();
}
/////////////////////////////////////////////////////////////////////////////////////
float CWorldInterface::GetTerrainFogDistance()
{
    return theSettings.fpTerrainBorderFogDistance;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldInterface::SetTerrainFogDistance(float fpVal)
{
    theSettings.fpTerrainBorderFogDistance = fpVal;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldInterface::SetAdjustLighting(bool bAdjust)
{
    if (bAdjust)
        theWorld.SetFlag(WC_DO_ADJUST_LIGHTING);
    else
        theWorld.ResetFlag(WC_DO_ADJUST_LIGHTING);
}
/////////////////////////////////////////////////////////////////////////////////////
const CString &CWorldInterface::GetWorkPath()
{
    return CWorld::GetWorkPath();
}
/////////////////////////////////////////////////////////////////////////////////////
const CString &CWorldInterface::GetCDPath()
{
    return CWorld::GetCDPath();
}
/////////////////////////////////////////////////////////////////////////////////////
CString CWorldInterface::GetMobPath()
{
    return theWorld.GetMobPath();
}
/////////////////////////////////////////////////////////////////////////////////////
const CString &CWorldInterface::GetDefaultStreamingSound()
{
    return theSettings.szDefaultStreamingSound;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldInterface::FlipDrawVisibleOnly()
{
    theSettings.nDrawVisibleOnly = !theSettings.nDrawVisibleOnly;
}
/////////////////////////////////////////////////////////////////////////////////////
CTextManager *CWorldInterface::GetTextManager()
{
    return theWorld.GetTextManager();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldInterface::InvokeLightning(CPlot3D ptFrom, CPlot3D ptTo)
{
    CString szText;
    CPlot3D ptCamera = IWorldClient.GetCameraCP();
    int nDelay = int(sqr(ptFrom.x - ptCamera.x) + sqr(ptFrom.y - ptCamera.y)) / 40;
    nDelay = Max(0, nDelay - 20);
    CPlot3D ptCenter = (ptFrom + ptTo) * 0.5f;
    if (random.Get(3) == 0)
    {
        // thunder
        szText.Format("WorldScript("
                      "CreateFX( %f,%f,%f,60,150,\"nature\\thunder\\%i.wav\" ) )",
                      ptFrom.x, ptFrom.y, ptFrom.z, (int)random.Get(1, 4));
        IWorldServer.LoadScriptText(szText, false);
    }
    else
    {
        // lightning
        szText.Format("WorldScript("
                      "CreatePointLight(1,%f,%f,%f,80,255,255,255 ) "
                      "CreateFX( %f,%f,%f,60,150,\"nature\\lightning\\%i.wav\" ) "
                      "CreateLightning(1,%f,%f,%f,%f,%f,%f,-7)"
                      "Sleep(1) DeleteLightning(1) Sleep(1)"
                      "CreateLightning(2,%f,%f,%f,%f,%f,%f,-7)"
                      "Sleep(1) DeleteLightning(2) DeletePointLight(1) )",
                      ptCenter.x, ptCenter.y, ptCenter.z, ptFrom.x, ptFrom.y, ptFrom.z,
                      (int)random.Get(1, N_WEATHER_NUMBER_OF_LSOUNDS), ptFrom.x, ptFrom.y, ptFrom.z,
                      ptTo.x, ptTo.y, ptTo.z, ptFrom.x, ptFrom.y, ptFrom.z, ptTo.x, ptTo.y, ptTo.z);
        IWorldServer.LoadScriptText(szText, false);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
static void ShiftData(DWORD *pData, int nByteSize, DWORD dwRnd)
{
    DWORD *pFinish = pData + nByteSize / 4;
    while (pData < pFinish)
        *pData++ ^= dwRnd;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class T>
static void ShiftData(T &data, DWORD dwRnd)
{
    ShiftData((DWORD *)&data, sizeof(T), dwRnd);
}
/////////////////////////////////////////////////////////////////////////////////////
static void ShiftParty(CParty &party, DWORD dwRnd)
{
    party.nMoney ^= dwRnd;
    for (int i = 0; i < party.people.GetSize(); i++)
        ShiftData(party.people[i].stats, dwRnd);
}
/////////////////////////////////////////////////////////////////////////////////////
static void ShiftPlayers(CPlayerManager *pMan, bool bServer, DWORD dwRnd)
{
    if (!pMan)
        return;
    CPlayer *pPlayer;
    CPlayerIterator iter;
    for (pPlayer = iter.GetFirst(pMan); pPlayer; pPlayer = iter.GetNext())
    {
        ShiftParty(pPlayer->party, dwRnd);
        if (bServer)
        {
            CUnit *pUnit;
            CUnitPtrHashIterator unitIter;
            for (pUnit = unitIter.GetFirst(&pPlayer->unitsUnderControl); pUnit;
                 pUnit = unitIter.GetNext())
            {
                CUnitServer *pUS = (CUnitServer *)pUnit->GetUnitSpecific();
                SRPGUnitStats &stats = *pUS->GetRPGStats();
                ShiftData(stats, dwRnd);
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldInterface::SetActiveState(bool bActive)
{
    static bool bWasActive = true;
    static DWORD dwRnd = 0;
    if (bActive == bWasActive)
        return;
    if (!bActive)
        dwRnd = random.Get(0, 0xfffffffe);
    //
    ShiftPlayers((CPlayerManager *)theWorld.GetPlayerManager(), false, dwRnd);
    ShiftPlayers((CPlayerManager *)theWorldServer.GetPlayerManager(), true, dwRnd);
    ShiftParty(LMPPartySet.currentParty.party, dwRnd);
    //
    bWasActive = bActive;
}
/////////////////////////////////////////////////////////////////////////////////////
// CWorldConnector
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::ConnectLocalServer()
{
    theWorld.ConnectLocal(theWorldServer.GetLocalNetDriver());
    theWorld.GetPlayer()->SetSendParty(false);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::ConnectLocalLMPServer(CParty &party)
{
    theWorld.GetPlayer()->GetPlayer()->SetParty(party);
    theWorld.ConnectLocal(theWorldServer.GetLocalNetDriver());
    theWorld.GetPlayer()->SetSendParty(true);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::ConnectTcpAddress(const char *pszAddress, CParty &party)
{
    CPlayer *pPlayer = theWorld.GetPlayer()->GetPlayer();
    pPlayer->SetParty(party);
    theWorldServer.RemoveEverythingDynamic();
    if (!theWorld.ConnectTcp(pszAddress))
    {
        if (pChatClient->IsValid())
        {
            ASSERT(0);
            // CRAP inform about connection failure
            // failed to connect to specified zone - go back to base
            LMPConnected();
        }
        else
            GoMainMenu();
    }
    theWorld.GetPlayer()->SetSendParty(true);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::Disconnect()
{
    theWorld.Disconnect();
}
/////////////////////////////////////////////////////////////////////////////////////
// LOAD & SAVE
/////////////////////////////////////////////////////////////////////////////////////
// CRAP - function to determine if info on pZone to be stored
static bool IsToBeStored(const char *pszCurrentAllod, SScenarioZone *pZone)
{
    if (auto* mod = ModRuntime::Current())
        if (mod->SpellAddon().Feature11() >= 1)
            return true;
    if (pZone->szName == "bz7g")
        return true;
    return pZone->szAllodName == pszCurrentAllod;
}
/////////////////////////////////////////////////////////////////////////////////////
static int CalcCrc(const void *pData, int nSize)
{
    DWORD *pStart = (DWORD *)pData;
    DWORD *pFinish = pStart + (nSize >> 2);
    int nRes = 0;
    for (; pStart < pFinish; pStart++)
    {
        nRes ^= pStart[0];
        nRes *= (nRes & 0xff) | 3;
    }
    return nRes;
}
/////////////////////////////////////////////////////////////////////////////////////
static CheckedCruncher::Error lastSaveError;
bool DecompressTo(CMemorySave &dst, int nSignature, int nVersion, int nMinVersion, CSave &src)
{
    const unsigned int position = src.GetPosition();
    const unsigned int size = src.GetSize();
    if (position > size || size - position > 512u * 1024 * 1024)
    { lastSaveError = {"invalid_file_length", position}; return false; }
    std::vector<unsigned char> raw(size - position), payload;
    if (src.Read(raw.data(), static_cast<UINT>(raw.size())) != raw.size() || !src.IsOk())
    { lastSaveError = {"incomplete_file_read", src.GetPosition()}; return false; }
    int version = 0;
    if (!CheckedSave::Decode(raw.data(), raw.size(), nSignature, nVersion, nMinVersion,
                             payload, version, lastSaveError)) return false;
    src.SetVersion(version);
    dst.Clear();
    dst.SetVersion(version);
    if (!payload.empty()) dst.Write(payload.data(), static_cast<UINT>(payload.size()));
    dst.Seek(0);
    return true;
}
//
void CompressTo(CSave &dst, int nSignature, int nVersion, CMemorySave &src)
{
    ASSERT(nVersion > N_COMPRESS_SAVE_VERSION);
    CNetCompressor compr;
    CFixedMemStream srcMem(src.GetBuffer(), src.GetSize());
    CMemoryStream dstMem;
    bool bPacked = true;
    compr.Pack(srcMem, dstMem);
    dst.StoreSaveHeader(nSignature, nVersion);
    if (nVersion > 107)
    {
        int nCrc = CalcCrc(src.GetBuffer(), src.GetSize());
        dst.Write(nCrc);
    }
    // test compressed
    CMemoryStream tst;
    dstMem.Seek(0);
    CNetCompressor compr1;
    compr1.Unpack(dstMem, tst);
    if (tst.GetSize() != src.GetSize() ||
        memcmp(src.GetBuffer(), tst.GetBuffer(), src.GetSize()) != 0)
    {
        // shit happened -> packer made mistake or data was corrupted during pack
#ifndef FINALRELEASE
        ReportError("Cruncher error");
#endif
        bPacked = false;
        dst.Write(&bPacked, 1);
        dst.Write(src.GetBuffer(), src.GetSize());
    }
    else
    {
        dst.Write(&bPacked, 1);
        dst.Write(dstMem.GetBuffer(), dstMem.GetSize());
    }
}
/////////////////////////////////////////////////////////////////////////////////////
namespace
{
static bool PrepareSave(PreparedSave &save, const CString &path, int signature)
{
    return save.Prepare(path, signature, N_CURRENT_SAVE_VERSION,
                         N_CURRENT_MINIMAL_SAVE_VERSION, lastSaveError);
}
static PreparedSave::Result PrepareOptionalSave(PreparedSave &save, const CString &path)
{
    return save.PrepareOptional(path, 0x102, N_CURRENT_SAVE_VERSION,
                                N_CURRENT_MINIMAL_SAVE_VERSION, lastSaveError);
}
static void MissingSave(const CString &path, const char *event)
{
    GameJournal::Event("SAVE", event, "file=%s", GameJournal::Quote(path).c_str());
}
static BOOL SaveLoadFailure(const char *path, const char *reason, size_t offset)
{
    GameJournal::Event("SAVE", "load_failed", "file=%s reason=%s offset=%u",
        GameJournal::Quote(path).c_str(), reason, static_cast<unsigned int>(offset));
    ReportError("Save file %s is corrupt", path);
    return FALSE;
}
static BOOL ApplyMission(CMemorySave &f)
{
    f.Seek(0);
    if (f.GetVersion() < 82) theCamera.Load(f);
    if (f.GetVersion() < 71)
    {
        interfaceState.SetSize(0);
        theInterface.Load(f);
        f.Read(interfaceState);
        interfaceState.SetVersion(f.GetVersion());
    }
    return theWorldServer.LoadSave(f, WorldLoadOrigin::SavedGame);
}
}
BOOL CWorldConnector::LoadSave(CSave &file)
{
    CMemorySave f;
    bool applying = false;
    try
    {
        if (!DecompressTo(f, 0x102, N_CURRENT_SAVE_VERSION, N_CURRENT_MINIMAL_SAVE_VERSION, file))
            return SaveLoadFailure("mission.sav", lastSaveError.reason, lastSaveError.offset);
        CSaveReadScope strict;
        applying = true;
        IWorldClient.StopAllSounds();
        if (!ApplyMission(f)) throw CSaveReadError{"mission_apply_failed", f.GetPosition()};
        return TRUE;
    }
    catch (const CSaveReadError &e)
    { if (applying) GoMainMenu(); return SaveLoadFailure("mission.sav", e.reason, e.offset); }
    catch (CException *e)
    { e->Delete(); if (applying) GoMainMenu(); return SaveLoadFailure("mission.sav", "file_or_memory_error", 0); }
    catch (const std::bad_alloc &)
    { if (applying) GoMainMenu(); return SaveLoadFailure("mission.sav", "allocation_failed", 0); }
}
//
void CWorldConnector::StoreSave(CSave &file)
{
    CMemorySave f;
    f.SetVersion(N_CURRENT_SAVE_VERSION);
    theWorldServer.StoreSave(f);
    CompressTo(file, 0x102, N_CURRENT_SAVE_VERSION, f);
}
/////////////////////////////////////////////////////////////////////////////////////
// используется в movetozone, считывается зона при неподключенных плеерах
BOOL CWorldConnector::LoadZoneState(CParty &party, CMemorySave *prepared)
{
    GameJournal::CombatBoundary("zone_entry");
    GameJournal::Operation journal("ZONE", "enter", party.szCurrentZone);
    BOOL bRv = TRUE;
    CFileSave file;
    SScenarioZone *pZone;
    CString szCurrentPath;
    CString szZone = party.szCurrentZone;

    pZone = theMap.GetZoneByName(szZone);
    ASSERT(pZone);
    if (!pZone)
        return FALSE;

    // CRAP - movies has numbers
    switch ((int)party.vars.GetVar("movie"))
    {
    case 1:
        if (szZone == "bz3g")
        {
            CBinkVideo::Play(IWorld.GetCDPath() + "Movies\\dcity.bik");
            party.vars.DelVar("movie");
        }
        break;
    default:
        party.vars.DelVar("movie");
    }
    //
    if (pZone->szTerrainName != "")
    {
        // special hack for zone6 terrain
        const char *pszSpecialTerrain = 0;
        if (szZone.CompareNoCase("gz6g") == 0)
        {
            if (party.vars.GetVar("z.gz6g.frozen"))
                pszSpecialTerrain = "zone6_2.mpr";
            else
                pszSpecialTerrain = "zone6.mpr";
        }
        // check if we have to load zone from .mob or from save
        if (party.VisitedZone(szZone))
        {
            szCurrentPath.Format("%ssaves\\current\\", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str());
            CString szFileName = szZone + ".sav";
            CMemorySave f;
            if (!prepared)
            {
                if (!file.OpenRead(szCurrentPath + szFileName)) return FALSE;
                if (!DecompressTo(f, 0x102, N_CURRENT_SAVE_VERSION, N_CURRENT_MINIMAL_SAVE_VERSION, file))
                    return SaveLoadFailure(szCurrentPath + szFileName, lastSaveError.reason, lastSaveError.offset);
                file.CloseFile();
                prepared = &f;
            }
            prepared->Seek(0);
            bRv = theWorldServer.LoadSave(*prepared, WorldLoadOrigin::ZoneEntry, pszSpecialTerrain);
            if (!bRv) return FALSE;
            // process world for some time that should pass on zone during us being absent
            if (party.nEntrance >= 0 && party.nEntrance < pZone->exits.GetSize())
            {
                int nSegments = 1000;
                float fpCurTime = party.vars.GetVar(szPartyGlobalTimeVarName);
                float fpZoneTime = party.vars.GetVar(CString("zt.") + szZone);
                nSegments = (fpCurTime - fpZoneTime) * 100;
                float fpActivityRadius = 70.0f;
                if (auto* mod = ModRuntime::Current(); mod && mod->SpellAddon().Feature43() == 1)
                {
                    const auto& spellAddon = mod->SpellAddon();
                    nSegments = Min(nSegments, spellAddon.Int(ModSpellAddon::IntKey::gameplay_maxsegments));
                    // ASI adds after signed Min, with x86 32-bit wrap and no lower clamp.
                    nSegments = static_cast<int32_t>(static_cast<uint32_t>(nSegments) +
                        static_cast<uint32_t>(spellAddon.Int(ModSpellAddon::IntKey::gameplay_addsegments)));
                    fpActivityRadius = spellAddon.Scalar(ModSpellAddon::ScalarKey::gameplay_exitenteractivityradius);
                }
                else
                    nSegments = Min(nSegments, 1000);
                if (nSegments > 0)
                {
                    const SZoneExit &exit = pZone->exits[party.nEntrance];
                    CPlot3D ptActive = CPlot3D((exit.x1 + exit.x2) * 0.5f,
                                               (exit.y1 + exit.y2) * 0.5f, 0);
                    // make some world processing to make world like some time has elapsed
                    for (int i = 0; i < nSegments; i++)
                    {
                        theWorldServer.AddActiveSpot(ptActive, fpActivityRadius);
                        theWorldServer.WorldSegment(true);
                    }
                }
            }
        }
        else
        {
            CString szTerrain = pZone->szTerrainName + ".mpr";
            if (pszSpecialTerrain)
                szTerrain = pszSpecialTerrain;
            if (!theWorldServer.NewZone(szTerrain, pZone->szMobName + ".mob", pZone->szName))
                return FALSE;
            pZone = theMap.GetZoneByName(szZone);
            theWorldServer.SetWorldTime(4.0f);
        }
        if (pZone->eType == GM_BASE)
            theWorldServer.LoadScriptFromMOB(pZone->szMobName + ".mob");
        else
            theWorldServer.script.RestartScript();
    }
    theWorldServer.SetExits(pZone->exits);
    { journal.Result(bRv != FALSE); return bRv; }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldConnector::StoreZoneState(CParty &party)
{
    GameJournal::Operation journal("SAVE", "zone", party.szCurrentZone);
    CFileSave file;
    CString szCurrentPath;
    CString szZone = theWorldServer.GetCurrentZoneName();

    if (theWorldServer.curInterface == IFACE_MISSION)
        theCamera.SetCurrentAsShortcut(7);
    else if (theWorldServer.curInterface == IFACE_BASE)
        theCamera.SetCurrentAsShortcut(6);
    szCurrentPath.Format("%ssaves\\current\\", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str());
    szCurrentPath += szZone + ".sav";
    if (auto* mod = ModRuntime::Current()) mod->SaveSkillCooldowns(party.vars);
    if (!ModAnimationIdSave::PrepareSave(theWorldServer, party.vars))
        return FALSE;
    if (!file.OpenWrite(szCurrentPath))
    {
        ReportError("Can`t write to %s", (const char *)szCurrentPath);
        return FALSE;
    }
    theWorldServer.HealAllUnits();
    CMemorySave f;
    f.SetVersion(N_CURRENT_SAVE_VERSION);
    theWorldServer.StoreSave(f);
    CompressTo(file, 0x102, N_CURRENT_SAVE_VERSION, f);
    file.CloseFile();
    party.SetVisitedZone(szZone);
    // set time of leaving the zone
    float fpCurTime = party.vars.GetVar(szPartyGlobalTimeVarName);
    party.vars.SetVar(CString("zt.") + szZone, fpCurTime);
    { journal.Result(true); return TRUE; }
}
/////////////////////////////////////////////////////////////////////////////////////
void ClearDirectory(LPCTSTR pszPath)
{
    CreateDirectory(pszPath, 0);
    CFileFind find;
    BOOL bFind = find.FindFile(CString(pszPath) + "*.*");
    while (bFind)
    {
        bFind = find.FindNextFile();
        DeleteFile(find.GetFilePath());
    }
    find.Close();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldConnector::LoadGame(const char *pszSaveName)
{
    GameJournal::Operation journal("LOAD", "game", pszSaveName);
    if (theWorldServer.GetServerMode() == SERV_MODE_LMP ||
        theWorldServer.GetServerMode() == SERV_MODE_MMP) return FALSE;
    CString savePath, currentPath, failedPath;
    savePath.Format("%ssaves\\%s\\", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str(), pszSaveName);
    currentPath.Format("%ssaves\\current\\", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str());
    bool applying = false;
    try
    {
        PreparedSave scenario, mission;
        std::vector<std::unique_ptr<PreparedSave>> zones;
        failedPath = savePath + "scenario.sav";
        if (!PrepareSave(scenario, failedPath, 0xDEAD))
            return SaveLoadFailure(failedPath, lastSaveError.reason, lastSaveError.offset);
        CSaveReadScope strict;
        // Deserialize into a private hat to select the exact set of used files.
        CGlobalHat candidate;
        candidate.Load(scenario.payload);
        auto &partySet = candidate.GetSinglePlayerPartySet();
        for (const auto &entry : partySet.parties)
            if (!entry) throw CSaveReadError{"null_party_reference", scenario.payload.GetPosition()};
        if (!partySet.GetParty(partySet.szCurrentParty))
            throw CSaveReadError{"missing_current_party", scenario.payload.GetPosition()};
        CParty party;
        candidate.GetSinglePlayerParty(party);
        SScenarioZone *zone = theMap.GetZoneByName(party.szCurrentZone);
        if (!zone) throw CSaveReadError{"unknown_party_zone", scenario.payload.GetPosition()};
        CString allod = zone->szAllodName;
        for (int i = 0; i < theMap.GetZoneNum(); ++i)
        {
            SScenarioZone *visited = theMap.GetZone(i);
            if (!party.VisitedZone(visited->szName) || !IsToBeStored(allod, visited)) continue;
            auto prepared = std::make_unique<PreparedSave>();
            failedPath = savePath + visited->szName + ".sav";
            const auto result = PrepareOptionalSave(*prepared, failedPath);
            if (result == PreparedSave::Result::Missing)
            {
                party.ResetVisitedZone(visited->szName);
                MissingSave(failedPath, "zone_missing");
                continue;
            }
            if (result == PreparedSave::Result::Failed)
                return SaveLoadFailure(failedPath, lastSaveError.reason, lastSaveError.offset);
            zones.push_back(std::move(prepared));
        }
        bool hasMission = zone->eType == GM_REALZONE || zone->eType == GM_BASE;
        if (hasMission)
        {
            failedPath = savePath + "mission.sav";
            const auto result = PrepareOptionalSave(mission, failedPath);
            if (result == PreparedSave::Result::Missing)
            { MissingSave(failedPath, "mission_missing"); return FALSE; }
            if (result == PreparedSave::Result::Failed)
                return SaveLoadFailure(failedPath, lastSaveError.reason, lastSaveError.offset);
        }
        // Stage B starts here. Nothing above clears current or changes the live world.
        applying = true;
        GameJournal::CombatBoundary("saved_game");
        NConsole::RunAutoExec("exec autoexec.cfg");
        theCursor.SetMode(CT_WAIT);
        theInterface.SetScreensMustDie();
        theInterface.DestroyScreens();
        theWorldServer.RemoveEverythingDynamic();
        IWorldClient.StopAllSounds();
        LMPReloadDatabases(IsLMP());
        ConnectLocalServer();
        theWorldServer.StartServer(SERV_MODE_LOCAL);
        theWorld.Segment(); theWorldServer.Segment();
        theWorld.Segment(); theWorldServer.Segment();
        ClearDirectory(currentPath);
        failedPath = scenario.path;
        scenario.payload.Seek(0);
        theHat.Load(scenario.payload);
        if (scenario.payload.GetVersion() >= 71)
        {
            interfaceState.SetSize(0);
            theInterface.Load(scenario.payload);
            scenario.payload.Read(interfaceState);
            interfaceState.SetVersion(scenario.payload.GetVersion());
        }
        if (scenario.payload.GetVersion() >= 82) theCamera.Load(scenario.payload);
        for (const auto &prepared : zones)
        {
            failedPath = prepared->path;
            CString name = prepared->path.Mid(savePath.GetLength());
            CFileSave destination;
            if (!destination.OpenWrite(currentPath + name))
                throw CSaveReadError{"copy_zone_failed", 0};
            destination.Write(prepared->raw.GetBuffer(), prepared->raw.GetSize());
            destination.CloseFile();
        }
        for (int i = 0; i < theMap.GetZoneNum(); ++i)
        {
            SScenarioZone *visited = theMap.GetZone(i);
            if (!IsToBeStored(allod, visited)) party.ResetVisitedZone(visited->szName);
        }
        theHat.SetSinglePlayerParty(party);
        if (hasMission)
        {
            failedPath = mission.path;
            if (!ApplyMission(mission.payload))
                throw CSaveReadError{"mission_apply_failed", mission.payload.GetPosition()};
            theWorldServer.SetExits(zone->exits);
        }
        else if (!MoveToZone(party.szCurrentZone))
            throw CSaveReadError{"zone_apply_failed", 0};
        NConsole::game << "Loaded " << pszSaveName << endl;
        journal.Result(true);
        return TRUE;
    }
    catch (const CSaveReadError &e)
    {
        if (applying) GoMainMenu();
        return SaveLoadFailure(failedPath, e.reason, e.offset);
    }
    catch (CException *e)
    {
        e->Delete();
        if (applying) GoMainMenu();
        return SaveLoadFailure(failedPath, "file_or_memory_error", 0);
    }
    catch (const std::bad_alloc &)
    {
        if (applying) GoMainMenu();
        return SaveLoadFailure(failedPath, "allocation_failed", 0);
    }
}
//
BOOL CWorldConnector::SaveGame(const char *pszSaveName)
{
    GameJournal::Event("SAVE", "kind", "source=%s name=%s", GameJournal::CurrentSource(), GameJournal::Quote(pszSaveName).c_str());
    GameJournal::Operation journal("SAVE", "game", pszSaveName);
    // store current game state and copy files to specified path
    int i;
    CFileSave file;
    SScenarioZone *pZone;
    CString szTmp;
    CString szSavePath;
    CString szCurrentPath;

    if (theWorldServer.GetServerMode() != SERV_MODE_LOCAL)
        return FALSE;
    theCursor.SetMode(CT_WAIT);
    szSavePath.Format("%ssaves\\%s\\", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str(), pszSaveName);
    szCurrentPath.Format("%ssaves\\current\\", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str());
    // check directory existing
    // clear directory
    ClearDirectory(szSavePath);
    // some cheat for saving party when player is on global map, because server`s state is not
    // stored in the case, single player only
    // correct decision is to store worldserver when on global map too
    if (theWorldServer.curInterface == IFACE_GLOBAL_MAP)
    {
        CPlayer *pPlayer = theWorldServer.GetPlayer(0);
        ASSERT(pPlayer);
        if (pPlayer)
            theHat.SetSinglePlayerParty(pPlayer->party);
    }
    //
    CParty party;
    theHat.GetSinglePlayerParty(party);
    if (auto* mod = ModRuntime::Current()) mod->SaveSkillCooldowns(party.vars);
    if (!ModAnimationIdSave::PrepareSave(theWorldServer, party.vars))
        return FALSE;
    //
    // copy zone saves from current to save
    CString szCurrentAllod;
    szCurrentAllod = theMap.GetZoneByName(party.szCurrentZone)->szAllodName;
    for (i = 0; i < theMap.GetZoneNum(); i++)
    {
        pZone = theMap.GetZone(i);
        if (party.VisitedZone(pZone->szName) && IsToBeStored(szCurrentAllod, pZone))
        {
            szTmp = pZone->szName;
            if (!CopyFile(szCurrentPath + szTmp + ".sav", szSavePath + szTmp + ".sav", FALSE))
                party.ResetVisitedZone(pZone->szName);
        }
        else
            party.ResetVisitedZone(pZone->szName);
    }
    theHat.SetSinglePlayerParty(party);
    //
    // scenario saving
    if (!file.OpenWrite(szSavePath + "scenario.sav"))
    {
        GoMainMenu();
        return FALSE;
    }
    CMemorySave f;
    f.SetVersion(N_CURRENT_SAVE_VERSION);
    theHat.Store(f);
    // interface vars saving
    theInterface.Store(f);
    interfaceState.SetSize(0);
    theInterface.StoreSpecific();
    f.Write(interfaceState);
    interfaceState.SetSize(0);
    // camera storing
    theCamera.Store(f);
    //
    CompressTo(file, 0xDEAD, N_CURRENT_SAVE_VERSION, f);
    //
    file.CloseFile();
    //
    // store current zone
    pZone = theMap.GetZoneByName(party.szCurrentZone);
    if (pZone->eType == GM_REALZONE || pZone->eType == GM_BASE)
    {
        ASSERT(theWorldServer.IsMapLoaded());
        // store current zone
        if (!file.OpenWrite(szSavePath + "mission.sav"))
            return FALSE;
        StoreSave(file);
        file.CloseFile();
    }

    theCursor.SetMode(CT_DEFAULT);
    NConsole::game << "Saved " << pszSaveName << endl;
    { journal.Result(true); return TRUE; }
}
/////////////////////////////////////////////////////////////////////////////////////
int CWorldConnector::GetSaveSize()
{
    CFileFind find;
    CString szCurrentPath;
    szCurrentPath.Format("%ssaves\\current\\*.*", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str());
    BOOL bFind = find.FindFile(szCurrentPath);
    int nRes = 1000000; // this should be enough for single zone
    while (bFind)
    {
        bFind = find.FindNextFile();
        nRes += find.GetLength();
    }
    return nRes;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::GoMainMenu()
{
    GameJournal::Event("ZONE", "main_menu");
    theInterface.SetScreensMustDie();
    MoveToZone("MainMenu", IFACE_MAINMENU);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::NewGame()
{
    GameJournal::CombatBoundary("new_game");
    GameJournal::Event("ZONE", "reason", "reason=new_game");
    NConsole::RunAutoExec("exec autoexec.cfg");
    theInterface.SetScreensMustDie();
    // lets start a new game
    theHat.NewSingleGame();
    // clear directory with current saves
    CString szCurrentPath;
    szCurrentPath.Format("%ssaves\\current\\", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str());
    ClearDirectory(szCurrentPath);
    // fetch party
    CParty party;
    theHat.GetSinglePlayerParty(party);
    //
    MoveToZone(party.szCurrentZone);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldConnector::StartServer(const char *pszZone)
{
    SScenarioZone *pZone = 0;
    pZone = theMap.GetZoneByName(pszZone);
    if (!pZone || pZone->eType != GM_REALZONE)
        return FALSE;

    CString szCurrentPath;
    szCurrentPath.Format("%ssaves\\current\\", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str());
    PreparedSave prepared;
    prepared.path = szCurrentPath + pszZone + ".sav";
    bool applying = false;
    try
    {
        const auto result = PrepareOptionalSave(prepared, prepared.path);
        const bool existing = result == PreparedSave::Result::Ready;
        if (result == PreparedSave::Result::Missing) MissingSave(prepared.path, "zone_missing");
        if (result == PreparedSave::Result::Failed)
            return SaveLoadFailure(prepared.path, lastSaveError.reason, lastSaveError.offset);
        CSaveReadScope strict;
        applying = true;
        theInterface.SetScreensMustDie();
        theInterface.DestroyScreens();
        LMPReloadDatabases(IsLMP());
        theWorldServer.RemoveEverythingDynamic();
        if (existing)
        {
            if (!theWorldServer.LoadSave(prepared.payload, WorldLoadOrigin::ZoneEntry, 0))
                throw CSaveReadError{"zone_apply_failed", prepared.payload.GetPosition()};
        }
        else
        {
            if (!theWorldServer.NewZone(pZone->szTerrainName + ".mpr", pZone->szMobName + ".mob",
                                        pszZone))
                return FALSE;
            theWorldServer.SetWorldTime(4.0f);
        }
        CAArray<SZoneExit, SZoneExit &> exits = pZone->exits;
        for (int i = 0; i < exits.GetSize(); i++)
            exits[i].szZone = "MPGame1";
        theWorldServer.SetExits(exits);
        theWorldServer.SetWeatherChange(pZone->eWeatherType);
        theWorldServer.SetSky(pZone->eSkyType);

        theWorldServer.curInterface = IFACE_MISSION;
        theWorldServer.StartServer(SERV_MODE_LMP);
        theSettings.nDrawVisibleOnly = 1;
        if (pChatServer->IsValid())
        {
            char szHost[128];
            if (!gethostname(szHost, 100))
            {
                CCSNodeAddress addr;
                addr.SetInetName(szHost, 8000);
                CString szAddr(addr.GetFastName().c_str());
                pChatServer->pZone = dbgnew CChatZone;
                pChatServer->pZone->szAddress = szAddr; // CString(szHost) + ":8000";
                pChatServer->pZone->szZoneName = pszZone;
                pChatServer->InformAllButLeader(CMS_ZONEENABLED, pszZone);
            }
            theWorldServer.AddSecondaryMob(CString(pszZone) + ".mob");
        }
        return TRUE;
    }
    catch (const CSaveReadError &e)
    { if (applying) GoMainMenu(); return SaveLoadFailure(prepared.path, e.reason, e.offset); }
    catch (CException *e)
    { e->Delete(); if (applying) GoMainMenu(); return SaveLoadFailure(prepared.path, "file_or_memory_error", 0); }
    catch (const std::bad_alloc &)
    { if (applying) GoMainMenu(); return SaveLoadFailure(prepared.path, "allocation_failed", 0); }
}
/////////////////////////////////////////////////////////////////////////////////////
// server side, spawning a new server (with disconnecting everyone from server)
BOOL CWorldConnector::MoveToZone(const char *pszZoneName, EInterface iface)
{
    PreparedSave prepared;
    bool applying = false;
    try
    {
        bool havePrepared = false;
        CParty targetParty;
        if (iface == IFACE_NONE)
        {
            theHat.GetSinglePlayerParty(targetParty);
            targetParty.szCurrentZone = pszZoneName;
            SScenarioZone *target = theMap.GetZoneByName(pszZoneName);
            if (target && target->szTerrainName != "" && targetParty.VisitedZone(pszZoneName))
            {
                CString path;
                path.Format("%ssaves\\current\\%s.sav", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str(), pszZoneName);
                const auto result = PrepareOptionalSave(prepared, path);
                // Legacy MoveToZone/LoadZoneState returns false for an absent
                // visited-zone state; it does not invent a replacement world.
                if (result == PreparedSave::Result::Missing)
                { MissingSave(path, "zone_missing"); return FALSE; }
                if (result == PreparedSave::Result::Failed)
                    return SaveLoadFailure(path, lastSaveError.reason, lastSaveError.offset);
                havePrepared = true;
            }
        }
        CSaveReadScope strict;
        applying = true;
        if (action == CD_MOVETOZONE)
        {
            theInterface.DestroyScreens();
            LMPReloadDatabases(IsLMP());
        }
        if (action == CD_MOVETOZONE &&
            (theWorldServer.GetServerMode() == SERV_MODE_LOCAL || theWorldServer.GetServerMode() == SERV_MODE_LMP) &&
            (theWorldServer.curInterface == IFACE_MISSION || theWorldServer.curInterface == IFACE_BASE))
        {
            CParty oldParty;
            theHat.GetSinglePlayerParty(oldParty);
            StoreZoneState(oldParty);
            if (theWorldServer.GetServerMode() == SERV_MODE_LOCAL) theHat.SetSinglePlayerParty(oldParty);
        }
        EServerMode servMode = SERV_MODE_NONE;
        theCursor.SetMode(CT_WAIT);
        SScenarioZone *pZone = 0;
        theInterface.SetScreensMustDie();
        if (stricmp(pszZoneName, "endofgame") == 0)
            iface = IFACE_ENDGAME;
        else
            pZone = theMap.GetZoneByName(pszZoneName);
        // CRAP in case of multiplayer we have to connect to another server
        // in single we run server to connect to
        theWorldServer.RemoveEverythingDynamic();
        if (iface == IFACE_NONE && pZone)
        {
            switch (pZone->eType)
            {
            case GM_REALZONE:
                iface = IFACE_MISSION;
                break;
            case GM_BETWEENZONES:
                iface = IFACE_GLOBAL_MAP;
                break;
            case GM_BASE:
                iface = IFACE_BASE;
                break;
            }
        }

        if (iface != IFACE_ENDGAME && iface != IFACE_MAINMENU && !pZone)
        {
            ReportError("Zone %s not found", pszZoneName);
            if (IWorldConnector.IsLMP())
                IWorldConnector.StopLMP();
            iface = IFACE_MAINMENU;
        }

        // theWorldServer.SetWorldTimeScale(theSettings.nDefaultTimeScale);
        theWorldServer.SetWorldTimeScale(theSettings.nDefaultTimeScale);
        switch (iface)
        {
        case IFACE_MAINMENU: {
            theWorldServer.NewZone("ZoneMainMenuNew.mpr", "ZoneMainMenuNew.mob");
            SYSTEMTIME time;
            GetLocalTime(&time);

            // theWorldServer.SetWorldTime(time.wHour + time.wMinute / 60.0f);
            //  TODOALEX DEBUG!!
            theWorldServer.SetWorldTime(0);
            theWorldServer.SetWorldTimeScale(0);

            servMode = SERV_MODE_INTERFACE;
            // ATM: AllowScriptsInMainMenu patch
            theWorldServer.bCanRunScript = true;
        }
        break;
        case IFACE_ENDGAME:
            //			theWorldServer.NewZone( "zone20.mpr","Outro.mob" );
            servMode = SERV_MODE_LOCAL;
            break;
        default: {
            ASSERT(pZone);
            // starting server for single player only
            CParty party;
            theHat.GetSinglePlayerParty(party);
            if (!LoadZoneState(party, havePrepared ? &prepared.payload : nullptr))
                throw CSaveReadError{"zone_apply_failed", havePrepared ? prepared.payload.GetPosition() : 0};
            theHat.SetSinglePlayerParty(party);
            //
            theWorldServer.SetWorldTime(fmod(party.vars.GetVar(szPartyGlobalTimeVarName), 24));
            if (pZone)
            {
                theWorldServer.SetWeatherChange(pZone->eWeatherType);
                theWorldServer.SetSky(pZone->eSkyType);
            }
            servMode = SERV_MODE_LOCAL;
        }
        break;
        }
        theWorldServer.curInterface = iface;
        if (iface == IFACE_MISSION)
            theSettings.nDrawVisibleOnly = 1;
        // connect & start server
        theWorldServer.StartServer(servMode);
        ConnectLocalServer();
        return TRUE;
    }
    catch (const CSaveReadError &e)
    { if (applying && iface != IFACE_MAINMENU) GoMainMenu(); return SaveLoadFailure(prepared.path, e.reason, e.offset); }
    catch (CException *e)
    { e->Delete(); if (applying && iface != IFACE_MAINMENU) GoMainMenu(); return SaveLoadFailure(prepared.path, "file_or_memory_error", 0); }
    catch (const std::bad_alloc &)
    { if (applying && iface != IFACE_MAINMENU) GoMainMenu(); return SaveLoadFailure(prepared.path, "allocation_failed", 0); }
}
/////////////////////////////////////////////////////////////////////////////////////
// main state transition place
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::DoGo()
{
    if (action == CD_NONE)
        return;
    if (action == CD_LOADGAME)
    {
        if (LoadGame(szConnectDest)) LMPTimePassed();
        action = CD_NONE;
        return;
    }
    if (action == CD_MOVETOZONE)
    {
        if (MoveToZone(szConnectDest)) LMPTimePassed();
        action = CD_NONE;
        return;
    }
    if (action == CD_MAKELMPSERVER)
    {
        if (StartServer(szConnectDest)) { ConnectLocalLMPServer(party); LMPTimePassed(); }
        action = CD_NONE;
        return;
    }
    theInterface.DestroyScreens();
    // clear current server
    theWorldServer.RemoveEverythingDynamic();
    // while ( !theWorld.IsFailedConnection() )
    // theWorld.Segment();
    LMPReloadDatabases(IsLMP());
    // start new server
    switch (action)
    {
    case CD_MAINMENU:
        GoMainMenu();
        break;
    case CD_NEWGAME:
        NewGame();
        break;
    case CD_CONNECTLMP:
        ConnectTcpAddress(szConnectDest, party);
        break;
    }
    action = CD_NONE;

    LMPTimePassed();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
static void ReloadDatabases(const char *pszDatabase)
{
    theUnitDatabase.Clear();
    theUnitDatabase.Load(theSettings.szResourcePath, pszDatabase);
    theUnitDatabase.SetupWithSounds(theWorld.GetSoundManager());
    thePerkDatabase.Clear();
    thePerkDatabase.LoadDatabase(theSettings.szResourcePath, pszDatabase);
    theSpellDatabase.Clear();
    theSpellDatabase.Load(theSettings.szResourcePath, pszDatabase);
    theItemDatabase.Clear();
    theItemDatabase.Load(theSettings.szResourcePath, pszDatabase);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::LMPReloadDatabases(bool bLMP)
{
    static CString szCurrentRes("database.res");
    CString szRes = bLMP ? "databaseLMP.res" : "database.res";
    if (szRes == szCurrentRes)
        return;
    szCurrentRes = szRes;
    ReloadDatabases(szRes);
    // set difficulty level for LMP as 1 or restore for non-LMP
    if (bLMP)
    {
        theSettings.nDifficultyLevel2 = theSettings.nDifficultyLevel;
        theSettings.nDifficultyLevel = 0;
    }
    else
        theSettings.nDifficultyLevel = theSettings.nDifficultyLevel2;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
CString CWorldConnector::GetLMPBase()
{
    switch (pChatClient->allod)
    {
    case LMPA_GIPAT:
        return "bz1mpg";
    case LMPA_INGOS:
        return "bz2mpg";
    case LMPA_SUSLANGER:
        return "bz3mpg";
    case LMPA_RIK:
        return "bz4mpg";
    }
    ASSERT(0);
    return "bz1mpg";
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::StartLMPServer(const char *pszServerName, int nMaxClients, ELMPAllod allod,
                                     const char *pszPassword, bool bPrivate, unsigned int nClientID)
{
    LMPLoadQuestsInfo();
    pCSLog = &NConsole::net;
    pChatServer = dbgnew CChatServer(allod);
    pChatClient = dbgnew CChatClient;
    CCSNodeAddress addr;
    addr.SetInetName("127.0.0.1", NCSParams::nCSServerPort);
    ConnectLMPServer(addr, pszPassword);
    pChatClient->pSelf->bLocalServer = true;
    if (!pszServerName || pszServerName[0] == 0)
        pChatServer->szServerName = "Server";
    else
        pChatServer->szServerName = pszServerName;
    pChatServer->nMaxClients = nMaxClients;
    if (pszPassword && pszPassword[0])
        pChatServer->SetPassword(pszPassword);
    pChatServer->SetInformMaster(!bPrivate);
    pChatClient->allod = allod;
    pChatServer->nUniqueClientID = nClientID;
    InitLMPInfo();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::ConnectLMPServer(const CCSNodeAddress &addr, const char *pszPassword)
{
    CMemoryStream pkt;
    char szPwd[N_CHAT_PASSWORD_LENGTH];
    memset(szPwd, 0, sizeof(szPwd));
    if (pszPassword)
        strncpy(szPwd, pszPassword, N_CHAT_PASSWORD_LENGTH);
    pkt.Write(szPwd, sizeof(szPwd));
    pChatClient->ConnectServer(addr, pkt);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::StartLMPClient()
{
    GameJournal::Event("SESSION", "role", "role=client");
    pCSLog = &NConsole::net;
    pChatServer = 0;
    pChatClient = dbgnew CChatClient;
    InitLMPInfo();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::InitLMPInfo()
{
    ASSERT(pChatClient->IsValid());
    //
    LMPReloadDatabases(true);
    // clear party`s variables
    CParty &party = LMPPartySet.currentParty.party;
    ClearLMPPartyVariables(party.vars);
    party.RemoveAllQuestItems();
    if (party.people.GetSize() == 1)
    {
        CPersonage &pers = party.people[0];
        pChatClient->pSelf->szPrototype = pers.szPrototype;
        pChatClient->pSelf->szName = pers.szName;
    }
    else
    {
        ASSERT(0);
        pChatClient->pSelf->szPrototype = "Human Hero";
        pChatClient->pSelf->szName = "Bug";
    }
    pChatClient->pSelf->state = CPS_BASE; // CRAP , some kind of cps_connect
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::StopLMP()
{
    theInterface.SetCheckDisconnect(false);
    pChatServer = 0;
    pChatClient = 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::LMPConnectAbort()
{
    StopLMP();
    LMPReloadDatabases(false);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::SelectLMPZone(const char *pszZone)
{
    CString szCurrentPath;
    szCurrentPath.Format("%ssaves\\current\\", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str());
    // clear directory current dir
    ClearDirectory(szCurrentPath);
    //
    szLMPZone = pszZone;
    if (pChatServer->IsValid() && !pChatClient->IsValid())
    {
        // dedicated server
        if (pszZone)
            StartServer(pszZone);
        else
            ; // CRAP - stop server
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::LMPExitZone()
{
    if (bIsOnZone)
    {
        pChatClient->pSelf->state = CPS_BASE;
        pChatClient->pSelf->SetUpdated();
        // CRAP test if everyone has left the zone
        if (pChatServer->IsValid())
        {
            // some checks that everybody has left the server
            ClientGoZone(GetLMPBase(), 0);
            if (pChatServer->pZone->IsValid())
                pChatServer->InformAllButLeader(CMS_ZONEDISABLED,
                                                pChatServer->pZone->szZoneName.c_str());
            pChatServer->pZone = 0;
        }
        else
        {
            ClientGoZone(GetLMPBase(), 0);
        }
        bIsOnZone = false;
    }
    else
    {
        // CRAP
        if (pChatServer->IsValid())
        {
            const CCSBase::CClientList &clients = pChatServer->GetClients();
            for (auto i = clients.begin(); i != clients.end(); ++i)
            {
                (*i)->channelInBuf.Clear();
            }
        }
        // end of CRAP
        CParty party;
        CPlayer *pPlayer = theWorldServer.GetPlayer(0);
        ASSERT(pPlayer);
        if (pPlayer && ((CPlayerServer *)pPlayer->GetPlayerSpecific())->IsPartyDeployed())
        {
            theWorldServer.FakeRemoveParty(pPlayer);
            party = pPlayer->party;
            theWorldServer.FakeDeployParty(pPlayer);
        }
        else
            party = LMPPartySet.currentParty.party;
        // CParty &party = LMPPartySet.currentParty.party;
        party.szCurrentZone = szLMPZone;
        pChatClient->pSelf->state = CPS_CONNECTING;
        pChatClient->pSelf->SetUpdated();
        if (pChatServer->IsValid())
            ReqStartLMPServer(szLMPZone, party);
        else
        {
            ASSERT(pChatClient->IsValid());
            if (pChatClient->pZone->IsValid())
                party.szCurrentZone = pChatClient->pZone->szZoneName.c_str();
            if (pChatClient->pZone->IsValid())
                ReqConnectTcpAddress(pChatClient->pZone->szAddress.c_str(), party);
            else
                ASSERT(0);
        }
        bIsOnZone = true; // CRAP, should be set after connect
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
// during LMP always store party to disk
void CWorldConnector::LMPClientRecvParty()
{
    if (pChatClient->IsValid())
    {
        CParty &party = theWorld.GetPlayer()->GetPlayer()->party;
        LMPPartySet.currentParty.party = party;
        theHat.SetSinglePlayerParty(party);
        LMPPartySet.currentParty.Store(LMPPartySet.szDir);
        LMPSyncBriefings();
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::LMPClientRecvZone()
{
    if (pChatClient->IsValid())
    {
        LMPSyncBriefings();
        if (pChatClient->pSelf->state == CPS_CONNECTING)
        {
            pChatClient->pSelf->state = CPS_ZONE;
            pChatClient->pSelf->SetUpdated();
        }
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::LMPRecalc()
{
    CParty &party = theWorld.GetPlayer()->GetPlayer()->party;
    if (pChatClient->IsValid() && !pChatServer->IsValid())
    {
        if (pChatClient->pZone->IsValid())
            party.vars.DelVar("z.MPGame1");
        else
            party.vars.SetVar("z.MPGame1", 1);
    }
    if (pChatClient->IsValid() && pChatServer->IsValid())
    {
        if (bIsOnZone)
        {
            if (theWorldServer.GetPlayerManager()->GetNumElements() > 1)
                party.vars.SetVar("z.MPGame1", 1);
            else
                party.vars.DelVar("z.MPGame1");
        }
        else
        {
            if (pChatClient->pQuest->IsValid())
                party.vars.DelVar("z.MPGame1");
            else
                party.vars.SetVar("z.MPGame1", 1);
        }
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::LMPConnected()
{
    theHat.NewLMPGame();
    CParty &party = LMPPartySet.currentParty.party;
    // one more CRAP; main pers is to have 1000011214 ID
    if (party.people.GetSize() == 1)
        party.people[0].nID = 1000011214;
    party.szCurrentZone = GetLMPBase();
    party.vars.SetVar("z.MPGame1", 1);
    theHat.SetSinglePlayerParty(LMPPartySet.currentParty.party);
    ReqMoveToZone(GetLMPBase(), 0); // CRAP
    bIsOnZone = false;
    pChatClient->pSelf->state = CPS_BASE;
    pChatClient->pSelf->SetUpdated();
    theInterface.SetCheckDisconnect(true);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::LMPFinishOnTcpDisconnect()
{
    pChatClient = 0;
    pChatClient = dbgnew CChatClient;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
bool CWorldConnector::IsLMP()
{
    return (pChatClient->IsValid());
}
/////////////////////////////////////////////////////////////////////////////////////
struct SLMPQuest
{
    CString szQuest;
    CString szZone;
    ELMPAllod allod;
};
static std::vector<SLMPQuest> szLMPQuests;
/////////////////////////////////////////////////////////////////////////////////////
static bool IsOneOf(const char **pszTest, const char *pszString)
{
    while (*pszTest[0])
    {
        if (strcmpi(pszTest[0], pszString) == 0)
            return true;
        pszTest++;
    }
    return false;
}
ELMPAllod Terrain2Allod(const char *pszAllod)
{
    if (auto *mod = ModRuntime::Current(); mod && mod->CustomMpZones())
    {
        if (mod->MatchesMpZone(0, pszAllod)) return LMPA_GIPAT;
        if (mod->MatchesMpZone(1, pszAllod)) return LMPA_INGOS;
        if (mod->MatchesMpZone(2, pszAllod)) return LMPA_SUSLANGER;
        return LMPA_RIK;
    }
    static const char *szAllods[][10] = { { "zone2", "zone3", "zone3obr", "zone4", "zone5", "zone6",
                                            "zone7", "zone8", "zone9", "" },
                                          { "zone12", "zone13", "" },
                                          { "zone17", "zone18", "" },
                                          { "zone19", "" } };
    if (IsOneOf(szAllods[0], pszAllod))
        return LMPA_GIPAT;
    if (IsOneOf(szAllods[1], pszAllod))
        return LMPA_INGOS;
    if (IsOneOf(szAllods[2], pszAllod))
        return LMPA_SUSLANGER;
    return LMPA_RIK;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::LMPLoadQuestsInfo()
{
    if (szLMPQuests.size())
        return;
    CFileFind find;
    BOOL bFind = find.FindFile(theWorldServer.GetMobPath() + "*.mq");
    while (bFind)
    {
        bFind = find.FindNextFile();
        CString szQuest = find.GetFileTitle();
        LMPLoadQuestResources(szQuest);
        LMPFreeTextResources();
        //
        SScenarioZone *pZone;
        CString szZone;
        ELMPAllod allod;
        pZone = theMap.GetZoneByName(szQuest);
        if (pZone)
        {
            szZone = pZone->szTerrainName;
            allod = Terrain2Allod(szZone);
            //
            szLMPQuests.emplace_back();
            szLMPQuests.back().szQuest = szQuest;
            szLMPQuests.back().szZone = szZone;
            szLMPQuests.back().allod = allod;
        }
    }
    find.Close();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::LMPGetQuests(std::vector<std::string> &szQuests, ELMPAllod allod,
                                   const char *pszExclude)
{
    std::list<SLMPQuest> allodQuests, zoneQuests;
    for (auto i = szLMPQuests.begin(); i != szLMPQuests.end(); ++i)
    {
        if (i->allod != allod)
            continue;
        allodQuests.push_back(*i);
    }
    while (!allodQuests.empty())
    {
        // select quests for single zone
        CString szZone = allodQuests.front().szZone;
        zoneQuests.clear();
        for (auto i = allodQuests.begin(); i != allodQuests.end();)
        {
            if (i->szZone == szZone)
            {
                zoneQuests.push_back(*i);
                i = allodQuests.erase(i);
            }
            else
                ++i;
        }
        // erase exclude quest if more then one quest on this zone
        if (zoneQuests.size() > 1)
        {
            for (auto k = zoneQuests.begin(); k != zoneQuests.end();)
            {
                if (k->szQuest == pszExclude)
                {
                    k = zoneQuests.erase(k);
                    // The committed addon patch exits this exclusion scan after one erase.
                    if (auto *mod = ModRuntime::Current(); mod && mod->CombatSetting("ShowAllQuests"))
                        break;
                }
                else
                    ++k;
            }
        }
        // select one of quests
        if (zoneQuests.size() > 0)
        {
            int n = random.Get(0, zoneQuests.size() - 1);
            auto k = zoneQuests.begin();
            while (n > 0)
            {
                n--;
                ++k;
            }
            szQuests.push_back((const char *)k->szQuest);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::LMPBriefingComplete(const char *pszBriefing)
{
    if (!pChatClient->IsValid())
        return;
    pChatClient->Command(CC_COMPLETE_BRIEFING, pszBriefing);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWorldConnector::LMPQuestComplete(const char *pszQuest)
{
    return pChatServer->IsValid() && pChatServer->CompleteQuest(pszQuest);
}
/////////////////////////////////////////////////////////////////////////////////////
CString CWorldConnector::LMPGetPersByBriefing(const char *pszBriefing)
{
    CTextManager *pTxtMng = theWorld.GetTextManager();
    if (!pTxtMng->GetText(CString("briefing ") + pszBriefing))
        return CString("");

    CStringArray szTmpArray;
    while (pTxtMng->FindNextCommand("#show"))
    {
        pTxtMng->GetStringArray(szTmpArray);
        if (szTmpArray.GetSize() < 2)
            continue;
        if (szTmpArray[1].CompareNoCase("Hero") == 0)
            continue;
        return szTmpArray[1];
    }
    return CString("");
}
/////////////////////////////////////////////////////////////////////////////////////
CString CWorldConnector::LMPGetFullBriefing(const char *pszBriefing)
{
    CString szBrief = LMPGetPersByBriefing(pszBriefing);
    //	ASSERT( szBrief != "" );
    if (szBrief != "")
        szBrief = CString("b.") + szBrief + "." + pszBriefing;
    else
        szBrief = CString("b.elder.") + pszBriefing;
    return szBrief;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::LMPAddBriefing(const char *pszBriefing)
{
    CParty &party = theWorld.GetPlayer()->GetPlayer()->party;
    int nLeng = strlen(pszBriefing);
    if (nLeng > 2 && pszBriefing[nLeng - 2] == '_')
    {
        CString szQuest(pszBriefing);
        szQuest = szQuest.Left(szQuest.GetLength() - 2);
        LMPAddTextResource(szQuest);
    }
    //
    CString szBrief = LMPGetFullBriefing(pszBriefing);
    party.vars.SetVar(szBrief, 1);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::LMPDelBriefing(const char *pszBriefing)
{
    CParty &party = theWorld.GetPlayer()->GetPlayer()->party;
    //
    CString szBrief = LMPGetFullBriefing(pszBriefing);
    party.vars.DelVar(szBrief);
}
/////////////////////////////////////////////////////////////////////////////////////
// sync current client party briefings and global vars with chat client
static void ShowVarSet(CChatClient::CGSVars::iterator i)
{
    CString szVar = (*i)->szVarName.c_str();
    float fpVal = (*i)->fpVal;
    if (szVar.Left(2) == "q.")
    {
        {
            theInterface.QuestUpdate(szVar, fpVal);
            theWorld.UpdateQuestState(szVar, fpVal);
        }
    }
}
struct SShowCmp
{
    bool operator()(CChatClient::CGSVars::iterator a, CChatClient::CGSVars::iterator b) const
    {
        return (*a)->nOrder < (*b)->nOrder;
    }
};
void CWorldConnector::LMPSyncBriefings()
{
    if (!pChatClient->IsValid())
        return;
    LMPFreeTextResources();
    CParty &party = theWorld.GetPlayer()->GetPlayer()->party;
    CGSVariableSet briefs;
    float fpConstr = party.vars.GetVar("constr.current");
    CGSVariableSet oldVars = party.vars;
    ClearLMPPartyVariables(party.vars);
    party.vars.SetVar("constr.current", fpConstr);
    /*party.vars.GetVars( "b.", briefs );
    for ( auto i =  briefs.begin(); i != briefs.end(); ++i )
            party.vars.DelVar( i->first );*/
    if (pChatClient->pQuest->IsValid())
        LMPAddTextResource(pChatClient->pQuest->szQuestName.c_str());
    CString szIntro;
    switch (pChatClient->allod)
    {
    case LMPA_GIPAT:
        party.vars.SetVar("b.smith.constr_1", 1);
        szIntro = "intro_1";
        break;
    case LMPA_INGOS:
        party.vars.SetVar("b.Shopper.constr_3", 1);
        szIntro = "intro_2";
        break;
    case LMPA_SUSLANGER:
        party.vars.SetVar("b.kuzn.constr_4", 1);
        szIntro = "intro_3";
        break;
    case LMPA_RIK:
        party.vars.SetVar("b.golem.constr_5", 1);
        szIntro = "intro_4";
        break;
    default:
        ASSERT(0);
    }
    CString szIntroQ = LMPGetFullBriefing(szIntro + "q");
    CString szIntroK = LMPGetFullBriefing(szIntro + "k");
    party.vars.SetVar(szIntroQ, Max(1.0f, party.vars.GetVar(szIntroQ)));
    party.vars.SetVar(szIntroK, Max(1.0f, party.vars.GetVar(szIntroK)));

    for (auto k = pChatClient->briefings.begin(); k != pChatClient->briefings.end(); ++k)
        LMPAddBriefing((*k)->szBriefingName.c_str());
    //
    // load quest resources
    CChatClient::CGSVars::iterator i;
    for (i = pChatClient->vars.begin(); i != pChatClient->vars.end(); ++i)
    {
        CString szVar = (*i)->szVarName.c_str();
        float fpVal = (*i)->fpVal;
        if (szVar.Left(2) == "q.")
        {
            CString szZone = szVar.Right(szVar.GetLength() - 2);
            int nIdx = szZone.Find(".");
            if (nIdx > 0)
                LMPLoadQuestResources(szZone.Left(nIdx));
            //
        }
    }
    //
    // set variables
    static unsigned int nPrevMax = 0;    // index of last encountered var
    static unsigned int nAlreadySet = 0; // index of last set variable
    if (theWorld.GetPlayer()->GetPlayer()->curInterface == IFACE_MISSION)
    {
        //
        // update ordering system
        // static DWORD dwTimeSet;              //
        // DWORD dwCurTime = timeGetTime();
        unsigned int nShowMin = nAlreadySet;
        if (nPrevMax > nAlreadySet + 2) ///*dwCurTime - dwTimeSet > 10000 ||*/ oldVars.empty() )
            nAlreadySet = nPrevMax + 1;
        // dwTimeSet = dwCurTime;
        //
        std::vector<CChatClient::CGSVars::iterator> toShow;
        for (i = pChatClient->vars.begin(); i != pChatClient->vars.end(); ++i)
        {
            nPrevMax = Max(nPrevMax, (*i)->nOrder);
            if ((*i)->nOrder < nAlreadySet && (*i)->nOrder >= nShowMin)
                toShow.push_back(i);
        }
        std::sort(toShow.begin(), toShow.end(), SShowCmp());
        for (auto z = toShow.begin(); z != toShow.end(); ++z)
            ShowVarSet(*z);
        //
        for (;;)
        {
            bool bBreak = true;
            for (i = pChatClient->vars.begin(); i != pChatClient->vars.end(); ++i)
            {
                if ((*i)->nOrder <= nAlreadySet)
                {
                    if ((*i)->nOrder == nAlreadySet)
                    {
                        ShowVarSet(i);
                        bBreak = false;
                        nAlreadySet++;
                    }
                    party.vars.SetVar((*i)->szVarName.c_str(), (*i)->fpVal);
                }
            }
            if (bBreak)
                break;
        }
    }
    else
    {
        for (i = pChatClient->vars.begin(); i != pChatClient->vars.end(); ++i)
        {
            nPrevMax = Max(nPrevMax, (*i)->nOrder);
            CString szVar = (*i)->szVarName.c_str();
            float fpVal = (*i)->fpVal;
            party.vars.SetVar((*i)->szVarName.c_str(), (*i)->fpVal);
        }
        nAlreadySet = 0; // nPrevMax + 1;
    }
    //
    /*
    for ( i = pChatClient->vars.begin(); i != pChatClient->vars.end(); ++i )
    {
            CString szVar = (*i)->szVarName.c_str();
            float fpVal = party.vars//(*i)->fpVal;
            if ( szVar.Left(2) == "q." )
            {
                    if ( oldVars.GetVar( szVar ) != fpVal &&
    theWorld.GetPlayer()->GetPlayer()->curInterface == IFACE_MISSION )
                    {
                            theInterface.QuestUpdate( szVar, fpVal );
                            theWorld.UpdateQuestState( szVar, fpVal );
                    }
            }
    }*/
    LMPRecalc();
}
/////////////////////////////////////////////////////////////////////////////////////
static std::vector<std::string> szTextRes;
bool CWorldConnector::LMPAddTextResource(const char *pszQuest)
{
    CTextManager *pText = theWorld.GetTextManager();
    if (!pText)
        return false;
    CString szFile = CString(pszQuest) + ".mq";
    if (!HasLMPFile(szFile))
        return false;
    std::string szRes = (theWorldServer.GetMobPath() + szFile).GetString();
    if (pChatServer->IsValid())
    {
        CMemoryStream file;
        CFileStream r;
        r.OpenRead(szRes.c_str());
        r.ReadTo(&file, r.GetSize());
        pChatServer->AddFile((const char *)szFile, file);
    }
    if (std::find(szTextRes.begin(), szTextRes.end(), szRes) == szTextRes.end())
    {
        szTextRes.push_back(szRes);
        pText->SelectResource(szRes.c_str());
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::LMPFreeTextResources()
{
    CTextManager *pText = theWorld.GetTextManager();
    if (!pText)
        return;
    for (auto i = szTextRes.begin(); i != szTextRes.end(); ++i)
        pText->CloseResource(i->c_str());
    szTextRes.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
static std::vector<std::string> szLoadedQuestRes;
void CWorldConnector::LMPLoadQuestResources(const char *pszQuest)
{
    CTextManager *pText = theWorld.GetTextManager();
    if (!pText)
    {
        ASSERT(0);
        return;
    }
    if (!LMPAddTextResource(pszQuest))
        return;
    if (std::find(szLoadedQuestRes.begin(), szLoadedQuestRes.end(), pszQuest) !=
        szLoadedQuestRes.end())
        return;
    if (pText->GetText(CString(pszQuest) + "\\quest.reg"))
        theQuestDatabase.AddInfo((BYTE *)pText->GetPos());
    else
        return;
    theMap.LoadZones(CString(pszQuest) + "\\map.txt");
    szLoadedQuestRes.push_back(pszQuest);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWorldConnector::HasLMPFile(const char *pszFName)
{
    CFileFind find;
    return find.FindFile(theWorldServer.GetMobPath() + pszFName) != FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::LMPAddFile(const char *pszFName, CMemoryStream &file)
{
    CFileSave f;
    f.OpenWrite(theWorldServer.GetMobPath() + pszFName);
    f.Write(file.GetBuffer(), file.GetSize());
    f.CloseFile();
}
/////////////////////////////////////////////////////////////////////////////////////
CString CWorldConnector::LMPGetQuestNameLocalised()
{
    CString szQuest;
    if (!pChatClient->IsValid() || !pChatClient->pQuest->IsValid())
    {
        if (!pChatServer->IsValid() || !pChatServer->pQuest->IsValid())
            return "";
        szQuest = pChatServer->pQuest->szQuestName.c_str();
    }
    else
        szQuest = pChatClient->pQuest->szQuestName.c_str();
    CTextManager *pText = theWorld.GetTextManager();
    if (pText->GetText(CString("quest ") + szQuest))
        pText->GetLine(szQuest);
    return szQuest;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::ReqSaveGame(const char *pszSaveName)
{
    ASSERT(theWorldServer.GetServerMode() == SERV_MODE_LOCAL);
    SaveGame(pszSaveName);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::ClientSideDisconnect()
{
    theWorld.Disconnect();
}
/////////////////////////////////////////////////////////////////////////////////////
// client side command
void CWorldConnector::ClientGoZone(const char *pszZoneName, int nEntrance)
{
    GameJournal::Event("ZONE", "request", "reason=%s zone=%s entrance=%d", GameJournal::Quote(GameJournal::CurrentSource()).c_str(), GameJournal::Quote(pszZoneName).c_str(), nEntrance);
    theInterface.SetScreensMustDie();
    theWorld.GetPlayer()->GoZone(pszZoneName, nEntrance);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::ReqGoMainMenu()
{
    theCamera.ResetFalloutMode();
    // ATM: TODO: "check pers storing in this case"
    // CRAP - check pers storing in this case
    StopLMP();
    action = CD_MAINMENU;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::ReqNewGame()
{
    action = CD_NEWGAME;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::ReqLoadGame(const char *pszSaveName)
{
    action = CD_LOADGAME;
    szConnectDest = pszSaveName;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::ReqMoveToZone(const char *pszZoneName, int nEntrance)
{
    GameJournal::Event("ZONE", "request", "reason=server_zone_change zone=%s entrance=%d", GameJournal::Quote(pszZoneName).c_str(), nEntrance);
    action = CD_MOVETOZONE;
    szConnectDest = pszZoneName;
    // nParam = nEntrance; // ATM: wtf is nEntrance, why to autoplay nParam variable?
    //  TODO: remove nEntrance if true not needed!
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::ReqConnectTcpAddress(const char *pszAddress, CParty &_party)
{
    action = CD_CONNECTLMP;
    szConnectDest = pszAddress;
    party = _party;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::ReqStartLMPServer(const char *pszZone, CParty &_party)
{
    action = CD_MAKELMPSERVER;
    szConnectDest = pszZone;
    party = _party;
}
/////////////////////////////////////////////////////////////////////////////////////
