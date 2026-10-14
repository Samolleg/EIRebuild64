/**********************************************************************
 *<
        FILE: SoundManager.CPP

        DESCRIPTION:  Sound manager implementation

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Entertainment
 **********************************************************************/
#include "StdAfx.h"
#include "EngineDiagnostics.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "SoundManager.h"
#include "ModRuntime.h"
#include "ModAudioConfig.h"
#include "ModAudioPolicy.h"
#include "I3DMain.h"
#include "WorldClient.h"
#include "RandomGen.h"
#include "GeneralSettings.h"

#ifdef _A4
#define dbgnew new
#endif //_A4
/////////////////////////////////////////////////////////////////////////////////////
// ------------------------- Mixer setup constants -------------------------------------------

const float FP_AMBIENT_RANGE_MIN = 5.f;
const float FP_AMBIENT_RANGE_MAX = 30.f;
const int N_AMBIENT_MS_TO_UPDATE = 100;

// ----------------------------------- Initialization
// ------------------------------------------------------

CSoundManager::CSoundManager()
{
    Create();
}

CSoundManager::~CSoundManager()
{
    Destroy();
}

BOOL CALLBACK DriversCallbackForNames(LPGUID, LPCSTR lpcstrDescription, LPCSTR, LPVOID lpContext)
{
    ((CStringArray *)lpContext)->Add(CString(lpcstrDescription));
    return TRUE;
}
BOOL CSoundManager::GetDrivers(CStringArray &szDrivers)
{
    return DS_OK == DirectSoundEnumerate(DriversCallbackForNames, &szDrivers);
}

// volumes

void CSoundManager::SetReverb(float fpLevel, float fpReflectionTime, float fpDecayTime)
{
    ASSERT(fpLevel >= 0.0f && fpLevel <= 1.0f);
    ASSERT(fpReflectionTime > 1e-3f);
    ASSERT(fpDecayTime > 1e-3f && fpDecayTime < 100.0f);

    fpReverbLevel = fpLevel;
    fpReverbReflectionTime = fpReflectionTime;
    fpReverbDecayTime = fpDecayTime;
}

// ----------------------------------- Create/destroy
// ------------------------------------------------------

BOOL CSoundManager::Create(HWND hWnd, BOOL _bAllowReverb, int nDriver, DWORD dwMixerFreq,
                           WORD wMixerBits)
{
    Destroy();
    bAllowReverb = _bAllowReverb;
    bReady = TRUE;
    ModAudioPolicy::Connect();
    MiniaudioOneShotSfx::PrepareNativeManager(this);
    return TRUE;
}

BOOL CSoundManager::Create()
{
    Clear();
    return TRUE;
}

void CSoundManager::Destroy()
{
    ModAudioPolicy::Reset();
    MiniaudioOneShotSfx::Shutdown("native_destroy");
    ClearFXResources();
    Clear();
}

void CSoundManager::ClearFXResources()
{
    while (fxResourceList.IsEmpty() == FALSE)
        delete fxResourceList.RemoveHead();
}

void CSoundManager::GetFXFileList(CStringArray &fileList, LPCSTR pszMask)
{
    fileList.RemoveAll();
    CStringArray stTemp;
    POSITION pos = fxResourceList.GetHeadPosition();
    while (pos)
    {
        CMappedResource *pRes = fxResourceList.GetNext(pos);
        pRes->GetFileList(stTemp, pszMask);
        for (int i = 0; i < stTemp.GetSize(); i++)
            fileList.Add(stTemp[i]);
    }
}

void CSoundManager::StopAllSounds(bool bStopStreaming)
{
    MiniaudioOneShotSfx::StopAll("native_stop_all");
    dwUniqueCnt = 0;
    if (bStopStreaming) EndStreaming();
}

void CSoundManager::PauseAllSounds()
{
    MiniaudioOneShotSfx::Pause("native_pause");
    PauseStreaming();
}

void CSoundManager::UnpauseAllSounds()
{
    MiniaudioOneShotSfx::Resume("native_resume");
    UnpauseStreaming();
}

void CSoundManager::Clear()
{
    for (int i = 0; i < N_SND_NUM_AMBIENTS; i++) ambients[i].Clear();
    for (int i = 0; i < N_SND_NUM_FXLISTS; i++) { fxLists[i].Clear(); fxLists[i].dwUniqueCnt = 0; }
    dwUniqueCnt = dwAmbientUpdateTime = 0;
    bReady = bAllowReverb = bReverseStereo = FALSE;
    fpReverbLevel = fpReverbReflectionTime = fpReverbDecayTime = 0.0f;
    nFXMasterVolume = nSpeechMasterVolume = nMusicMasterVolume = 100;
    uRandomStartPosition = 0;
    nTransactionCounter = 0;
}

// ------------------------------- Volumes ---------------------------------------------------

void CSoundManager::SetFXMasterVolume(int nPercentVolume)
{
    if (!bReady)
        return;
    nFXMasterVolume = Min<int>(100, Max<int>(0, nPercentVolume));
}

void CSoundManager::SetSpeechMasterVolume(int nPercentVolume)
{
    if (!bReady)
        return;
    nSpeechMasterVolume = Min<int>(100, Max<int>(0, nPercentVolume));
}

void CSoundManager::SetMusicMasterVolume(int nPercentVolume)
{
    nMusicMasterVolume = Min<int>(100, Max<int>(0, nPercentVolume));
    MiniaudioOneShotSfx::SetStreamingVolume(nMusicMasterVolume);
}

int CSoundManager::GetMusicMasterVolume()
{
    return nMusicMasterVolume;
}

// --------------------------------- FX sounds -----------------------------------------------

BOOL CSoundManager::AddFXResource(LPCSTR pszResourceName)
{
    if (!bReady)
        return TRUE;
    CMappedResource *pFXResource = dbgnew CMappedResource;
    if (!pFXResource->OpenResource(pszResourceName))
    {
        delete pFXResource;
        return FALSE;
    }
    fxResourceList.AddTail(pFXResource);
    return TRUE;
}

bool CSoundManager::CaptureModSound(const char* name)
{
    if(!bReady || !name || !*name)return false;
    ModAudioPolicy::Connect();
    MiniaudioOneShotSfx::PrepareNativeManager(this);
    if(MiniaudioOneShotSfx::IsCaptured(name))return true;
    DWORD size=0;CMappedResource* resource=nullptr;
    void* bytes=GetFXFileFromResources(name,&size,&resource);
    if(!bytes)return false;
    struct Release {CMappedResource* r;void* b;~Release(){r->CloseFile((BYTE*)b);}} release{resource,bytes};
    return MiniaudioOneShotSfx::CaptureEncoded(name,bytes,size,"native_res",
        reinterpret_cast<uintptr_t>(resource));
}
HSOUND CSoundManager::BeginPartySignalFX(const char* name,int priority,unsigned sourceId,float nearDistance,float farDistance)
{
    if(!ModRuntime::Current() || !CaptureModSound(name))return SND_EMPTY;
    const DWORD id=dwUniqueCnt;if(id==DWORD(SND_EMPTY))return SND_EMPTY;
    const MiniaudioOneShotSfx::PartyVoiceBinding binding={sourceId,nearDistance,farDistance};
    float gain=1.f,pitch=1.f;ModAudioPolicy::Variation(name,false,false,false,gain,pitch);
    // Party binding changes gain only; priority, master volume and pause remain FX.
    if(MiniaudioOneShotSfx::TryStartVirtualFlat(this,id,name,priority,100,0,false,false,false,false,gain,pitch,&binding,false)==MiniaudioOneShotSfx::STARTED){dwUniqueCnt=id+1;return id;}
    return SND_EMPTY;
}

HSOUND CSoundManager::BeginScenarioVoice(const char* name,int volume,const MiniaudioOneShotSfx::PartyVoiceBinding* party)
{
    EngineDiagnostics::Event("ScenarioVoice file=%s volume=%d speechMaster=%d ready=%d",name,volume,nSpeechMasterVolume,bReady);
    if(!ModRuntime::Current() || !CaptureModSound(name)){EngineDiagnostics::Event("ScenarioVoice capture failed: %s",name);return SND_EMPTY;}
    const DWORD id=dwUniqueCnt;if(id==DWORD(SND_EMPTY))return SND_EMPTY;
    if(MiniaudioOneShotSfx::TryStartVirtualFlat(this,id,name,ModAudioPolicy::Priority(1000,name,false,true,false),volume,0,false,false,true,false,1.f,1.f,party,true)==MiniaudioOneShotSfx::STARTED){EngineDiagnostics::Event("ScenarioVoice started id=%lu",id);dwUniqueCnt=id+1;return id;}EngineDiagnostics::Event("ScenarioVoice start failed: %s",MiniaudioOneShotSfx::LastStartReason());return SND_EMPTY;
}

HSOUND CSoundManager::BeginFXInt(LPCSTR pszSoundName, int nPriority, int nPercentVolume,
                             int nPercentPan, BOOL bLooping, BOOL bAffectedWithHeight,
                             BOOL bIsSpeech, BOOL bIsDialogPlayMode)
{
    if (!bReady) return SND_EMPTY;
    char corrected[256]={};const char* effective=pszSoundName;
    if(!ModAudioPolicy::Name(this,pszSoundName,corrected,sizeof(corrected),effective))return SND_EMPTY;
    pszSoundName=effective;
    if(!CaptureModSound(pszSoundName)){if(bIsDialogPlayMode)EngineDiagnostics::Event("BriefingVoice capture failed file=%s",pszSoundName);return SND_EMPTY;}
    nPriority=ModAudioPolicy::Priority(nPriority,pszSoundName,bLooping!=0,bIsSpeech!=0,bIsDialogPlayMode!=0);
    float gain=1.f,pitch=1.f;ModAudioPolicy::Variation(pszSoundName,bLooping!=0,bIsSpeech!=0,bIsDialogPlayMode!=0,gain,pitch);
    const DWORD id=dwUniqueCnt;if(id==DWORD(SND_EMPTY))return SND_EMPTY;
    MiniaudioOneShotSfx::PartyVoiceBinding party={};
    const bool bound=ModAudioPolicy::TakeAck(bIsSpeech!=0,bIsDialogPlayMode!=0,bLooping!=0,party);
    const auto started=MiniaudioOneShotSfx::TryStartVirtualFlat(this,id,pszSoundName,nPriority,nPercentVolume,nPercentPan,bLooping!=0,bAffectedWithHeight!=0,(bIsSpeech||bIsDialogPlayMode)!=0,bIsDialogPlayMode!=0,gain,pitch,bound?&party:nullptr,false);
    if(bIsDialogPlayMode)EngineDiagnostics::Event("BriefingVoice file=%s id=%lu started=%d speechMaster=%d reason=%s",pszSoundName,id,started==MiniaudioOneShotSfx::STARTED,nSpeechMasterVolume,started==MiniaudioOneShotSfx::STARTED?"started":MiniaudioOneShotSfx::LastStartReason());
    if(started==MiniaudioOneShotSfx::STARTED){dwUniqueCnt=id+1;return id;}return SND_EMPTY;

}

HSOUND CSoundManager::BeginFXInt(LPCSTR pszSoundName, int nPriority, const CPlot3D &sourceCP,
                             float fpMinDistance, float fpMaxDistance, BOOL bLooping,
                             BOOL bIsSpeech)
{
    if (!bReady) return SND_EMPTY;
    char corrected[256]={};const char* effective=pszSoundName;
    if(!ModAudioPolicy::Name(this,pszSoundName,corrected,sizeof(corrected),effective))return SND_EMPTY;
    pszSoundName=effective;
    if(!CaptureModSound(pszSoundName))return SND_EMPTY;
    nPriority=ModAudioPolicy::Priority(nPriority,pszSoundName,bLooping!=0,bIsSpeech!=0,false);
    float gain=1.f,pitch=1.f;ModAudioPolicy::Variation(pszSoundName,bLooping!=0,bIsSpeech!=0,false,gain,pitch);
    const DWORD id=dwUniqueCnt;if(id==DWORD(SND_EMPTY))return SND_EMPTY;
    if(MiniaudioOneShotSfx::TryStartVirtual3D(this,id,pszSoundName,nPriority,&sourceCP,fpMinDistance,fpMaxDistance,bLooping!=0,bIsSpeech!=0,gain,pitch)==MiniaudioOneShotSfx::STARTED){dwUniqueCnt=id+1;return id;}return SND_EMPTY;

}

void *CSoundManager::GetFXFileFromResources(LPCSTR pszSoundName, DWORD *pLen,
                                            CMappedResource **pResObject)
{
    POSITION pos = fxResourceList.GetHeadPosition();
    while (pos)
    {
        CMappedResource *pRes = fxResourceList.GetNext(pos);
        void *pRet = pRes->GetFile(pszSoundName, pLen);
        if (pRet)
        {
            if (pResObject)
                *pResObject = pRes;
            return pRet;
        }
    }
    return NULL;
}

BOOL CSoundManager::IsFXFileExist(LPCSTR pszSoundName)
{
    POSITION pos = fxResourceList.GetHeadPosition();
    while (pos)
    {
        CMappedResource *pRes = fxResourceList.GetNext(pos);
        if (pRes->IsFileExist(pszSoundName))
            return TRUE;
    }
    return FALSE;
}

BOOL CSoundManager::UpdateFX(HSOUND hSnd, int nPercentVolume, int nPercentPan)
{
    return MiniaudioOneShotSfx::UpdateVirtualFlat(this,hSnd,nPercentVolume,nPercentPan);
}

BOOL CSoundManager::UpdateFX(HSOUND hSnd, const CPlot3D &sourceCP, float fpMinDistance,
                             float fpMaxDistance)
{
    return MiniaudioOneShotSfx::UpdateVirtual3D(this,hSnd,&sourceCP,fpMinDistance,fpMaxDistance);
}

HSOUND CSoundManager::CancelFX(HSOUND hSnd)
{
    MiniaudioOneShotSfx::CancelVirtual(this,hSnd);
    return SND_EMPTY;
}

float CSoundManager::GetFXPlaybackTime(HSOUND hSnd)
{
    return MiniaudioOneShotSfx::GetVirtualPlaybackTime(this,hSnd);
}

void CSoundManager::SetFXListener(const CPlot3D &_listenerCP, const CPlot3D &_listenerOrientation)
{
    if (!bReady)
        return;

    listenerCP = _listenerCP;
    listenerOrientation = _listenerOrientation;
    // Normalize orientation ignoring Z coordinate
    float fpDiv = FSqrt(listenerOrientation.x * listenerOrientation.x +
                        listenerOrientation.y * listenerOrientation.y);
    if (fpDiv == 0)
        listenerOrientation.SetPlot(1, 0, 0);
    else
        listenerOrientation *= (1.f / fpDiv);
}
// --------------------------------- Transactions-- -----------------------------------------------

void CSoundManager::BeginTransaction()
{
    nTransactionCounter++;
}

void CSoundManager::EndTransaction()
{
    ASSERT(nTransactionCounter > 0);
    if (nTransactionCounter > 0) --nTransactionCounter;
}

// --------------------------------- Ambient sounds -----------------------------------------------

void SAmbientControl::Clear()
{
    bIsUsed = FALSE;
    nMsToReplayMin = nMsToReplayMax = -1;
    sourceCP = CPlot3D(0, 0, 0);
    fpMinDistance = fpMaxDistance = 0;
    dwLastPlayTime = 0;
}

HSNDAMBIENT CSoundManager::BeginAmbient(CStringArray &szNames, const CPlot3D &sourceCP,
                                        float fpMinDistance, float fpMaxDistance,
                                        int nMsToReplayMin, int nMsToReplayMax)
{
    assert(nMsToReplayMin < nMsToReplayMax);
    assert(fpMinDistance < fpMaxDistance);
    assert(nMsToReplayMin >= 0 && fpMinDistance >= 0);

    if (!bReady || szNames.GetSize() < 1)
        return SND_EMPTY;

    int i;
    for (i = 0; i < N_SND_NUM_AMBIENTS; i++)
        if (ambients[i].bIsUsed == FALSE)
            break;

    if (i == N_SND_NUM_AMBIENTS)
        return SND_EMPTY;

    ambients[i].bIsUsed = TRUE;
    ambients[i].szNames.Copy(szNames);
    UpdateAmbient(HSNDAMBIENT(i), sourceCP, fpMinDistance, fpMaxDistance, nMsToReplayMin,
                  nMsToReplayMax);
    ambients[i].dwLastPlayTime = GetTickCount();

    return HSNDAMBIENT(i);
}

void CSoundManager::UpdateAmbient(HSNDAMBIENT hAmb, const CPlot3D &sourceCP, float fpMinDistance,
                                  float fpMaxDistance, int nMsToReplayMin, int nMsToReplayMax)
{
    int nInd = int(hAmb);
    if (nInd < 0 || nInd > N_SND_NUM_AMBIENTS || ambients[nInd].bIsUsed == FALSE)
        return;
    ambients[nInd].sourceCP = sourceCP;
    ambients[nInd].fpMinDistance = fpMinDistance;
    ambients[nInd].fpMaxDistance = fpMaxDistance;
    ambients[nInd].nMsToReplayMin = nMsToReplayMin;
    ambients[nInd].nMsToReplayMax = nMsToReplayMax;
}

HSNDAMBIENT CSoundManager::CancelAmbient(HSNDAMBIENT hAmbient)
{
    int nInd = int(hAmbient);
    if (nInd < 0 || nInd >= N_SND_NUM_AMBIENTS || ambients[nInd].bIsUsed == FALSE)
        return SND_EMPTY;
    ambients[nInd].Clear();
    return SND_EMPTY;
}

void CSoundManager::SegmentAmbient()
{
    if (!bReady) return;
    const auto depth=theInterface.GetAudioScreenDepth();
    const bool modal=depth>1 && theWorld.IsCombatPause();
    const bool game=depth<=1 && theWorld.IsCombatPause();
    MiniaudioOneShotSfx::SetModalSpeechPaused(modal||game,game,"native_pause_selector");
    MiniaudioOneShotSfx::Tick(this);
    ModAudioPolicy::Tick(this);
    // Sound lists
    for (int i = 0; i < N_SND_NUM_FXLISTS; i++)
        if (fxLists[i].bIsUsed)
        {
            if (!IsFXValid(fxLists[i].hPlayingSnd))
                if (!fxLists[i].StartNext(this))
                {
                    fxLists[i].dwUniqueID = DWORD(SND_EMPTY);
                    fxLists[i].bIsUsed = FALSE;
                }
        }

    // Ambient sounds
    DWORD dwCurTime = GetTickCount();
    if (dwCurTime - dwAmbientUpdateTime < N_AMBIENT_MS_TO_UPDATE)
        return;
    dwAmbientUpdateTime = dwCurTime;

    for (int i = 0; i < N_SND_NUM_AMBIENTS; i++)
    {
        if (ambients[i].bIsUsed == FALSE)
            continue;
        // Check for need to generate sound
        DWORD dwReplayTimeMin, dwReplayTimeMax;
        CPlot3D diff = listenerCP - ambients[i].sourceCP;
        float fpDist = FSqrt(diff.x * diff.x + diff.y * diff.y);
        if (fpDist < ambients[i].fpMinDistance)
        {
            dwReplayTimeMin = ambients[i].nMsToReplayMin;
            dwReplayTimeMax = ambients[i].nMsToReplayMax;
        }
        else if (fpDist > ambients[i].fpMaxDistance - 1e-3f)
            continue;
        else
        {
            float fpFactor = (ambients[i].fpMaxDistance - ambients[i].fpMinDistance) /
                             (ambients[i].fpMaxDistance - fpDist);
            if (fpFactor > float(0xFF))
                continue;
            dwReplayTimeMin = Float2Int(ambients[i].nMsToReplayMin * fpFactor);
            dwReplayTimeMax = Float2Int(ambients[i].nMsToReplayMax * fpFactor);
        }
        DWORD dwPassedTime = dwCurTime - ambients[i].dwLastPlayTime;
        if (dwPassedTime < dwReplayTimeMin)
            continue;
        if (dwPassedTime < dwReplayTimeMax)
        {
            DWORD dwRndSeed = (dwReplayTimeMax - dwReplayTimeMin) / N_AMBIENT_MS_TO_UPDATE;
            if (dwRndSeed > 1 && random.Get(dwRndSeed - 1) != 0)
                continue;
        }
        ambients[i].dwLastPlayTime = dwCurTime;
        // Play sound
        int nIndex = 0;
        if (ambients[i].szNames.GetSize() > 1)
            nIndex = random.Get(ambients[i].szNames.GetSize() - 1);
        BeginFX(ambients[i].szNames[nIndex], ESNDPR_AMBIENT, random.Get(50, 100),
                random.Get(0, 100));
    }
}

// ------------------------------------ Sound lists
// --------------------------------------------------------

void SFXListControl::Clear()
{
    bIsUsed = FALSE;
    hPlayingSnd = SND_EMPTY;

    sourceCP = CPlot3D(0, 0, 0);
    fpMinDistance = fpMaxDistance = 0;

    nPlayCount = -1;
    nCurPlIndex = -1;
    nPriority = -1;

    bIsRandom = FALSE;
    playingFunc = NULL;

    dwUniqueID = DWORD(SND_EMPTY);
}

BOOL SFXListControl::StartNext(CSoundManager *pSndMng)
{
    if (playingFunc)
    {
        nCurPlIndex = playingFunc(nCurPlIndex, nPlayCount++);
        if (nCurPlIndex < 0)
            return FALSE;
        BOOL bCycle = (nCurPlIndex & SNDLIST_CYCLE_BIT);
        nCurPlIndex = nCurPlIndex & (~SNDLIST_CYCLE_BIT);
        ASSERT(nCurPlIndex < szNames.GetSize());
        hPlayingSnd = pSndMng->BeginFX(szNames[nCurPlIndex], nPriority, sourceCP, fpMinDistance,
                                       fpMaxDistance, bCycle);
        if (hPlayingSnd == SND_EMPTY)
            return FALSE;
        return TRUE;
    }
    if (nPlayCount == 0)
        return FALSE;
    if (bIsRandom)
        nCurPlIndex = random.Get(0, szNames.GetSize() - 1);
    hPlayingSnd = pSndMng->BeginFX(szNames[nCurPlIndex], nPriority, sourceCP, fpMinDistance,
                                   fpMaxDistance, FALSE);
    if (hPlayingSnd == SND_EMPTY)
        return FALSE;
    nCurPlIndex = (nCurPlIndex + 1) % szNames.GetSize();
    if (nPlayCount > 0)
        nPlayCount--;
    return TRUE;
}

SFXListControl *CSoundManager::GetFXListControl(HSNDLIST hSnd)
{
    if (!bReady)
        return NULL;

    int nChannel = hSnd & 0xFF;

    if (nChannel < 0 || nChannel >= N_SND_NUM_FXLISTS)
        return NULL;

    SFXListControl *rv = fxLists + nChannel;

    if (rv->dwUniqueID == DWORD(hSnd))
        return rv;

    return NULL;
}

HSNDLIST CSoundManager::BeginFXList(CStringArray &szNames, int nPriority, const CPlot3D &sourceCP,
                                    float fpMinDistance, float fpMaxDistance, BOOL bIsRandom,
                                    int nPlayCount, SMGetNextPlayIndexFunc playingFunc)
{
    ASSERT(fpMinDistance < fpMaxDistance);
    ASSERT(fpMinDistance >= 0);
    ASSERT(szNames.GetSize() > 0);

    if (!bReady || szNames.GetSize() < 1)
        return SND_EMPTY;

    int i;
    for (i = 0; i < N_SND_NUM_FXLISTS; i++)
        if (fxLists[i].bIsUsed == FALSE)
            break;

    if (i == N_SND_NUM_FXLISTS)
        return SND_EMPTY;

    fxLists[i].dwUniqueCnt++;
    fxLists[i].dwUniqueID = (fxLists[i].dwUniqueCnt << 8) | DWORD(i);

    fxLists[i].bIsUsed = TRUE;
    fxLists[i].szNames.Copy(szNames);

    fxLists[i].sourceCP = sourceCP;
    fxLists[i].fpMinDistance = fpMinDistance;
    fxLists[i].fpMaxDistance = fpMaxDistance;
    fxLists[i].bIsRandom = bIsRandom;
    fxLists[i].nPlayCount = nPlayCount;
    fxLists[i].nPriority = nPriority;
    fxLists[i].nCurPlIndex = 0;
    fxLists[i].playingFunc = playingFunc;
    if (playingFunc)
    {
        fxLists[i].nPlayCount = 0;
        fxLists[i].nCurPlIndex = -1;
    }

    if (fxLists[i].StartNext(this) == FALSE)
    {
        fxLists[i].dwUniqueID = DWORD(SND_EMPTY);
        fxLists[i].bIsUsed = FALSE;
        return SND_EMPTY;
    }

    return HSNDLIST(fxLists[i].dwUniqueID);
}

BOOL CSoundManager::UpdateFXList(HSNDLIST hSnd, const CPlot3D &sourceCP)
{
    SFXListControl *pCtrl = GetFXListControl(hSnd);
    if (pCtrl == NULL)
        return FALSE;

    pCtrl->sourceCP = sourceCP;

    UpdateFX(pCtrl->hPlayingSnd, sourceCP, pCtrl->fpMinDistance, pCtrl->fpMaxDistance);

    return TRUE;
}

BOOL CSoundManager::UpdateFXList(HSNDLIST hSnd, const CPlot3D &sourceCP, float fpMinDistance,
                                 float fpMaxDistance, int nPlayCount)
{
    SFXListControl *pCtrl = GetFXListControl(hSnd);
    if (pCtrl == NULL)
        return FALSE;

    pCtrl->sourceCP = sourceCP;
    pCtrl->fpMinDistance = fpMinDistance;
    pCtrl->fpMaxDistance = fpMaxDistance;
    pCtrl->nPlayCount = nPlayCount;

    UpdateFX(pCtrl->hPlayingSnd, sourceCP, fpMinDistance, fpMaxDistance);

    return TRUE;
}

HSNDLIST CSoundManager::CancelFXList(HSNDLIST hSnd)
{
    SFXListControl *pCtrl = GetFXListControl(hSnd);
    if (pCtrl)
    {
        pCtrl->bIsUsed = FALSE;
        pCtrl->dwUniqueID = DWORD(SND_EMPTY);
        CancelFX(pCtrl->hPlayingSnd);
    }
    return SND_EMPTY;
}

// -------------------------- Streaming sounds -----------------------------------------------

BOOL CSoundManager::SelectStreamingResource(LPCSTR pszResourceName, LPCSTR pszMarkersRegFileName)
{
    std::string markerPath=ModRuntime::ConfigPath(pszMarkersRegFileName);
    return MiniaudioOneShotSfx::SetStreamingResource(this,pszResourceName,markerPath.c_str());
}
CString CSoundManager::GetStreamingName() { return MiniaudioOneShotSfx::GetNativeStreamingName(); }
void CSoundManager::PauseStreaming() { MiniaudioOneShotSfx::PauseStreaming("native_stream_pause"); }
void CSoundManager::UnpauseStreaming() { MiniaudioOneShotSfx::ResumeStreaming("native_stream_resume"); }
void CSoundManager::FadeOutStreaming() { MiniaudioOneShotSfx::FadeOutStreaming(); }
void CSoundManager::FadeInStreaming() { MiniaudioOneShotSfx::FadeInStreaming(); }
BOOL CSoundManager::BeginStreaming(LPCSTR pszSoundName, ESNDMARKERTYPE markerType)
{
    return MiniaudioOneShotSfx::BeginStreaming(this,pszSoundName,int(markerType));
}
void CSoundManager::EndStreaming(ESNDMARKERTYPE markerType)
{
    MiniaudioOneShotSfx::EndStreaming(int(markerType),"native_stream_end");
}
unsigned CSoundManager::CountNativeAudibleSpeech(unsigned& mask) const
{
    unsigned count=MiniaudioOneShotSfx::CountAudibleVirtualSpeech(const_cast<CSoundManager*>(this));
    mask=count?0x80000000u:0;
    return count;
}
