#include "StdAfx.h"
#include "SaveSlot.h"
#include "CorePerf.h"
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
#include "TextManager.h"
#include <algorithm>
#include "QuestDatabase.h"
#include "Cruncher.h"
#include "CheckedSave.h"
#include "PreparedSave.h"
#include <memory>
#include "Streams.h"
#include "MoviePlayback.h"
#include "PerkDatabase.h"
#include "SpellDatabase.h"
#include "ItemDatabase.h"
#include "I3DScreenNewGameStats.h"

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
    theCamera.CaptureFieldCampCamera();
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
    if (CParty *view = theInterface.GetPartyView()) return *view;
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
float CWorldInterface::GSGetVar(const char *pszName)
{

    CPlayer *pPlayer = theWorldServer.GetPlayer(0);
    if (!pPlayer)
        return 0;
    return pPlayer->party.vars.GetVar(pszName);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldInterface::GSGetVars(const char *pszName, CGSVariableSet &vars)
{

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
    //
    bWasActive = bActive;
}
/////////////////////////////////////////////////////////////////////////////////////
// CWorldConnector
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::ConnectLocalServer()
{
    theWorld.ConnectLocal(theWorldServer.GetLocalNetDriver());
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
static BOOL SaveLoadFailure(const char *path, const char *reason, size_t offset, const char *at = "")
{
    GameJournal::Event("SAVE", "load_failed", "file=%s stream=%s at=%s reason=%s offset=%u",
        GameJournal::Quote(path).c_str(), GameJournal::Quote(path).c_str(),
        GameJournal::Quote(*at ? at : path).c_str(), reason, static_cast<unsigned int>(offset));
    if (strncmp(path, "stage=", 6) == 0)
        ReportError("Save loading failed at %s (%s)", path + 6, reason);
    else
        ReportError("Save file %s is corrupt", path);
    return FALSE;
}
static BOOL SaveReadFailure(const char *fallback, const CSaveReadError &error)
{
    const char *source = error.stream.empty() ? fallback : error.stream.c_str();
    const char *at = error.at.empty() ? fallback : error.at.c_str();
    GameJournal::Event("SAVE", "read_context", "stream=%s at=%s",
        GameJournal::Quote(source).c_str(), GameJournal::Quote(at).c_str());
    // Addresses refer to the matching EXE/PDB, captured before stack unwinding.
    for (USHORT i = 0; i < error.frames; ++i)
    {
        HMODULE module = nullptr;
        char name[MAX_PATH] = {};
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(error.stack[i]), &module);
        if (module) GetModuleFileNameA(module, name, sizeof(name));
        GameJournal::Event("SAVE", "read_stack", "frame=%u address=%p module=%s rva=%08x",
            static_cast<unsigned int>(i), error.stack[i], GameJournal::Quote(name).c_str(),
            module ? static_cast<unsigned int>(reinterpret_cast<ULONG_PTR>(error.stack[i]) -
                reinterpret_cast<ULONG_PTR>(module)) : 0);
    }
    return SaveLoadFailure(source, error.reason, error.offset, at);
}

}
BOOL CWorldConnector::LoadSave(CSave &file)
{
    return SaveSchema::Slot::LoadWorld(file);
}
//
void CWorldConnector::StoreSave(CSave &file)
{
    SaveSchema::Slot::StoreWorld(file,theWorldServer);
}
/////////////////////////////////////////////////////////////////////////////////////
// используется в movetozone, считывается зона при неподключенных плеерах
BOOL CWorldConnector::LoadZoneState(CParty &party, CMemorySave *prepared)
{
    CorePerf::Operation coreOperation("zone.enter", party.szCurrentZone);
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

    coreOperation.Stage("movies");
    // CRAP - movies has numbers
    switch ((int)party.vars.GetVar("movie"))
    {
    case 1:
        if (szZone == "bz3g")
        {
            Video::Movie::Play(IWorld.GetCDPath() + "Movies\\dcity.bik");
            party.vars.DelVar("movie");
        }
        break;
    default:
        party.vars.DelVar("movie");
    }
    //
    coreOperation.Stage("load_world");
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
            bRv = SaveSchema::Slot::LoadZone(szZone);
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
                coreOperation.Stage("simulate_absence");
                const DWORD simulateStart = GetTickCount();
                GameJournal::Event("ZONE", "simulate", "begin zone=%s segments=%d cur_time=%.9g zone_time=%.9g radius=%.9g",
                    GameJournal::Quote(szZone).c_str(), nSegments, fpCurTime, fpZoneTime, fpActivityRadius);
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
                        if ((i + 1) % 250 == 0)
                            GameJournal::Event("ZONE", "simulate", "progress segments=%d total=%d ms=%lu",
                                i + 1, nSegments, GetTickCount() - simulateStart);
                    }
                }
                GameJournal::Event("ZONE", "simulate", "end segments=%d ms=%lu",
                    nSegments > 0 ? nSegments : 0, GetTickCount() - simulateStart);
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
        coreOperation.Stage("script_and_exits");
        if (pZone->eType == GM_BASE)
            theWorldServer.LoadScriptFromMOB(pZone->szMobName + ".mob");
        else
            theWorldServer.script.RestartScript();
    }
    theWorldServer.SetExits(pZone->exits);
    { coreOperation.Result(bRv != FALSE); journal.Result(bRv != FALSE); return bRv; }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldConnector::StoreZoneState(CParty &party)
{
    const CString zone=theWorldServer.GetCurrentZoneName();
    if (!SaveSchema::Slot::StoreZone(zone)) return FALSE;
    party.SetVisitedZone(zone);
    party.vars.SetVar(CString("zt.")+zone,party.vars.GetVar(szPartyGlobalTimeVarName));
    return TRUE;
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
    return SaveSchema::Slot::Load(pszSaveName);
}
//
BOOL CWorldConnector::SaveGame(const char *pszSaveName)
{
    return SaveSchema::Slot::Save(pszSaveName);
}
/////////////////////////////////////////////////////////////////////////////////////
int CWorldConnector::GetSaveSize()
{
    CFileFind find;
    CString szCurrentPath;
    szCurrentPath.Format("%sSaves64\\current\\*.*", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str());
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
    SaveSchema::Slot::NewGame();
    // The party was created before the starting-attributes screen in DoGo.
    // clear directory with current saves
    CString szCurrentPath;
    szCurrentPath.Format("%sSaves64\\current\\", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str());
    ClearDirectory(szCurrentPath);
    // fetch party
    CParty party;
    theHat.GetSinglePlayerParty(party);
    //
    MoveToZone(party.szCurrentZone);
}
/////////////////////////////////////////////////////////////////////////////////////
// server side, spawning a new server (with disconnecting everyone from server)
BOOL CWorldConnector::MoveToZone(const char *pszZoneName, EInterface iface)
{
    CorePerf::Operation coreOperation("zone.move", pszZoneName);
    PreparedSave prepared;
    CString failedPath = "stage=move_to_zone";
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
                if (!SaveSchema::Slot::HasZone(pszZoneName)) return FALSE;
            }
        }
        coreOperation.Stage("release_and_previous_save");
        failedPath = "stage=move_to_zone_resources";
        applying = true;
        if (action == CD_MOVETOZONE)
        {
            theInterface.DestroyScreens();
        }
        if (action == CD_MOVETOZONE && (theWorldServer.GetServerMode() == SERV_MODE_LOCAL) && (theWorldServer.curInterface == IFACE_MISSION || theWorldServer.curInterface == IFACE_BASE))
        {
            CParty oldParty;
            theHat.GetSinglePlayerParty(oldParty);
            StoreZoneState(oldParty);
            theHat.SetSinglePlayerParty(oldParty);
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

            iface = IFACE_MAINMENU;
        }

        // theWorldServer.SetWorldTimeScale(theSettings.nDefaultTimeScale);
        theWorldServer.SetWorldTimeScale(theSettings.nDefaultTimeScale);
        coreOperation.Stage("create_world");
        switch (iface)
        {
        case IFACE_MAINMENU: {
            theWorldServer.NewZone("ZoneMainMenuNew.mpr", "ZoneMainMenuNew.mob");
            SYSTEMTIME time;
            GetLocalTime(&time);

            theWorldServer.SetWorldTime(time.wHour + time.wMinute / 60.0f);

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
            failedPath = havePrepared ? prepared.path : "stage=load_new_zone";
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
        coreOperation.Stage("connect");
        failedPath = "stage=start_server";
        theWorldServer.StartServer(servMode);
        ConnectLocalServer();
        coreOperation.Result(true);
        return TRUE;
    }
    catch (const CSaveReadError &e)
    { if (applying && iface != IFACE_MAINMENU) GoMainMenu(); return SaveReadFailure(failedPath, e); }
    catch (CException *e)
    { e->Delete(); if (applying && iface != IFACE_MAINMENU) GoMainMenu(); return SaveLoadFailure(failedPath, "file_or_memory_error", 0); }
    catch (const std::bad_alloc &)
    { if (applying && iface != IFACE_MAINMENU) GoMainMenu(); return SaveLoadFailure(failedPath, "allocation_failed", 0); }
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
        LoadGame(szConnectDest);
        action = CD_NONE;
        return;
    }
    if (action == CD_MOVETOZONE)
    {
        MoveToZone(szConnectDest);
        action = CD_NONE;
        return;
    }
    if (action == CD_NEWGAME)
    {
        // Prepare exactly once, before disconnecting the menu's local server.
        // An asynchronous screen lets the normal client/server loop continue.
        theHat.NewSingleGame();
        CParty freshParty;
        theHat.GetSinglePlayerParty(freshParty);
        action = CD_STARTPREPAREDNEWGAME;
        if (theInterface.GetCurrentScreen() &&
            !theWorld.IsFailedConnection() && freshParty.people.GetSize() > 0 &&
            !freshParty.szCurrentZone.IsEmpty())
        {
            action = CD_NONE;
            GameJournal::Event("NEWGAME", "attributes_open");
            theInterface.ActivateScreen(dbgnew CI3DScreenNewGameStats(&theInterface, freshParty));
            return;
        }
        GameJournal::Event("NEWGAME", "attributes_skipped", "reason=no_connected_fresh_party");
    }
    theInterface.DestroyScreens();
    // clear current server
    theWorldServer.RemoveEverythingDynamic();
    // while ( !theWorld.IsFailedConnection() )
    // theWorld.Segment();
    // start new server
    switch (action)
    {
    case CD_MAINMENU:
        GoMainMenu();
        break;
    case CD_STARTPREPAREDNEWGAME:
        NewGame();
        break;
    }
    action = CD_NONE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
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
    action = CD_MAINMENU;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::ReqNewGame()
{
    action = CD_NEWGAME;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldConnector::ReqStartPreparedNewGame()
{
    action = CD_STARTPREPAREDNEWGAME;
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
