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
#include "MilesCodecDirectory.h"
#include "MilesTestPluginFilter.h"
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

const int N_SECONDARY_CHANNELS = 15;

const int N_MAX_BYTES_IN_LRU = 8 * 1024 * 1024;
const int N_MS_TO_KEEP_IN_LRU = 60000;

const int N_STREAMING_MS_UPDATE = 100;

const float FP_AMBIENT_RANGE_MIN = 5.f;
const float FP_AMBIENT_RANGE_MAX = 30.f;
const int N_AMBIENT_MS_TO_UPDATE = 100;

const float FP_LOW_HEIGHT_VALUE = 30.f;
const float FP_HIGH_HEIGHT_VALUE = 70.f;

const int N_MIDI_SWITCHDELAY = 100;
const int N_MIDI_SWITCHGAPTIME = 2;

// ------------------------- Fx control methods ----------------------------------------------

void SFxControl::Clear()
{
    ailHSample = NULL;
    nPercentVolume = -1;
    nManualVolume = -1;
    nPercentPan = -1;
    nPriority = -1;
    bIsUsed = FALSE;
    bIsLooped = FALSE;
    bIsSpeech = FALSE;
    bReqCleanup = FALSE;
    bAffectedWithHeight = FALSE;
    dwUniqueID = DWORD(SND_EMPTY);
}

void SFxControl::Release(CSoundManager *pSndMng)
{
    if (bIsUsed)
    {
        assert(ailHSample);

        pSndMng->ReleaseRefTo(szSoundName);
        AIL_end_sample(ailHSample);
        bIsUsed = FALSE;
        bReqCleanup = FALSE;

        dwUniqueID = DWORD(SND_EMPTY);
    }
}

inline void SFxControl::SetControls(BOOL bReverseStereo)
{
    AIL_set_sample_volume(ailHSample, nPercentVolume);
    AIL_set_sample_pan(ailHSample,
                       (((bReverseStereo ? nPercentPan : -nPercentPan) + 100) * 127) / 200);
}

// -------------------------------- Fade control methods
// ---------------------------------------------------

const int N_SECONDS_TO_FADE = 6;
const float FP_FADE_ADD = 1.0f / float(N_SECONDS_TO_FADE * 16);
const int N_SECONDS_TO_FADE2 = 6;
const float FP_FADE_ADD2 = 1.0f / float(N_SECONDS_TO_FADE2 * 16);

void SFadeControl::Clear()
{
    SetVolume(100);
    ResetFade(0);
}

void SFadeControl::SetVolume(int nPercentVolume)
{
    nExternalVolume = nPercentVolume;
    RecalcMixerVolume();
}

void SFadeControl::BeginFadeOut(float _fpFadeSpeed)
{
    fadeMode = EFC_FOUT;
    bFadedOut = FALSE;
    fpFadeSpeed = _fpFadeSpeed;
    if (fpFadeSpeed == 0)
        fpFadeSpeed = FP_FADE_ADD;
    dwTimeToReset = 0;
}

void SFadeControl::BeginFadeIn(float _fpFadeSpeed)
{
    fadeMode = EFC_FIN;
    bFadedOut = FALSE;
    fpFadeSpeed = _fpFadeSpeed;
    if (fpFadeSpeed == 0)
        fpFadeSpeed = FP_FADE_ADD;
    dwTimeToReset = 0;
}

void SFadeControl::ResetFade(DWORD _dwTimeToReset)
{
    dwTimeToReset = _dwTimeToReset;
    if (dwTimeToReset == 0)
    {
        fpFadeValue = 1.0f;
        fadeMode = EFC_NONE;
        bFadedOut = FALSE;
        RecalcMixerVolume();
    }
}

void SFadeControl::Segment()
{
    if (dwTimeToReset > 0 && timeGetTime() >= dwTimeToReset)
    {
        ResetFade(0);
        return;
    }

    switch (fadeMode)
    {
    case EFC_NONE:
        return;
    case EFC_FIN:
        fpFadeValue += fpFadeSpeed;
        if (fpFadeValue >= 1.0f)
        {
            fadeMode = EFC_NONE;
            fpFadeValue = 1.0f;
        }
        break;
    case EFC_FOUT:
        fpFadeValue -= fpFadeSpeed;
        if (fpFadeValue <= 0.0f)
        {
            fadeMode = EFC_NONE;
            fpFadeValue = 0.0f;
            bFadedOut = TRUE;
        }
        break;
    }
    RecalcMixerVolume();
}

void SFadeControl::RecalcMixerVolume()
{
    nMixerVolume = MINT((float(nExternalVolume * fpFadeValue) * 127.0f) / 100.0f);
    nMixerVolume = Max<int>(0, Min<int>(127, nMixerVolume));
}

// ----------------------------------- Initialization
// ------------------------------------------------------

CSoundManager::CSoundManager()
{
    const std::string codecs = EIRebuildMilesCodecDirectory();
    MilesTestPluginFilter::Install();
    AIL_set_redist_directory(codecs.c_str());
    AIL_startup();
    Create();
}

CSoundManager::~CSoundManager()
{
    Destroy();
    AIL_shutdown();
}

LPDIRECTSOUND CSoundManager::GetDS()
{
    LPDIRECTSOUND lpDS = NULL;
    AIL_get_DirectSound_info(NULL, (void **)&lpDS, NULL);
    return lpDS;
}

typedef CSimplyArray<LPGUID, LPGUID &> CGUIDPtrArray;
BOOL CALLBACK DriversCallbackForNames(LPGUID, LPCSTR lpcstrDescription, LPCSTR, LPVOID lpContext)
{
    ((CStringArray *)lpContext)->Add(CString(lpcstrDescription));
    return TRUE;
}
BOOL CALLBACK DriversCallbackForGUIDs(LPGUID lpGuid, LPCSTR, LPCSTR, LPVOID lpContext)
{
    ((CGUIDPtrArray *)lpContext)->Add(lpGuid);
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

// threads starter functions
UINT AFX_CDECL StreamThreadStarter(LPVOID pParam)
{
    ((CSoundManager *)pParam)->StreamingThread();
    return 0;
}

UINT AFX_CDECL MidiThreadStarter(LPVOID pParam)
{
    ((CSoundManager *)pParam)->MidiThread();
    return 0;
}

// ----------------------------------- Create/destroy
// ------------------------------------------------------

BOOL CSoundManager::Create(HWND hWnd, BOOL _bAllowReverb, int nDriver, DWORD dwMixerFreq,
                           WORD wMixerBits)
{
    Destroy();
    bool bUseDSound = theSettings.nAudio != 2;
    // Get driver guid
    LPGUID lpGuid = (bUseDSound) ? 0 : (LPGUID)WAVE_MAPPER;
    if (nDriver >= 0)
    {
        CGUIDPtrArray guidArray;
        if (DS_OK == DirectSoundEnumerate(DriversCallbackForGUIDs, &guidArray))
        {
            assert(nDriver < guidArray.GetSize());
            if (nDriver < guidArray.GetSize())
                lpGuid = guidArray[nDriver];
        }
    }
    // reverb
    bAllowReverb = _bAllowReverb;
    if (bAllowReverb)
    {
        AIL_set_preference(DIG_REVERB_BUFFER_SIZE, 256 * 1024);
    }
    // Load MSS digital driver
    PCMWAVEFORMAT mixerFormat;
    mixerFormat.wf.wFormatTag = WAVE_FORMAT_PCM;
    mixerFormat.wf.nChannels = 2;
    mixerFormat.wf.nSamplesPerSec = DW_MIXER_FREQ > dwMixerFreq ? dwMixerFreq : DW_MIXER_FREQ;
    mixerFormat.wBitsPerSample = W_MIXER_BITS;
    mixerFormat.wf.nBlockAlign = (W_MIXER_BITS / 8) * mixerFormat.wf.nChannels;
    mixerFormat.wf.nAvgBytesPerSec = mixerFormat.wf.nSamplesPerSec * mixerFormat.wf.nBlockAlign;

    AIL_set_preference(DIG_MIXER_CHANNELS,
                       N_SECONDARY_CHANNELS + 2 /* for streaming */ + 1 /* for bink */);
    if (!bUseDSound)
        AIL_set_preference(DIG_USE_WAVEOUT, TRUE);

    if (AIL_waveOutOpen(&ailDigDriver, NULL, (DWORD)lpGuid, (LPWAVEFORMAT)&mixerFormat))
    {
        ReportWarning(AIL_last_error());
        return FALSE;
    }
    AIL_set_DirectSound_HWND(ailDigDriver, hWnd);
    // AIL_set_digital_master_volume( ailDigDriver, 127 );
    // Create main streaming thread
#ifdef SM_SUPPORT_STREAMING
    if (streamMain.Create(ailDigDriver))
        pStreamThread = AfxBeginThread(StreamThreadStarter, this, THREAD_PRIORITY_TIME_CRITICAL);
#endif // SM_SUPPORT_STREAMING
       // Create fx controls
    pFx = dbgnew SFxControl[N_SECONDARY_CHANNELS];
    for (int i = 0; i < N_SECONDARY_CHANNELS; i++)
    {
        pFx[i].Clear();
        pFx[i].ailHSample = AIL_allocate_sample_handle(ailDigDriver);
        AIL_set_sample_user_data(pFx[i].ailHSample, 0, S32(this));
        AIL_set_sample_user_data(pFx[i].ailHSample, 1, S32(&pFx[i]));
    }
#ifdef SM_SUPPORT_MIDI
    if (AIL_midiOutOpen(&ailMidiDriver, NULL, MIDI_MAPPER))
        ReportWarning(AIL_last_error());
    else
    {
        midiCtrl.hSeq = AIL_allocate_sequence_handle(ailMidiDriver);
        if (midiCtrl.hSeq)
            pMidiThread = AfxBeginThread(MidiThreadStarter, this, THREAD_PRIORITY_HIGHEST);
        bMidiReady = (midiCtrl.hSeq != NULL);
    }
#endif // SM_SUPPORT_MIDI
       // Success
    bReady = TRUE;
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
    if (pStreamThread)
    {
        bShutDownReq = TRUE; // Signal to exit streaming thread
        streamShutDownEvent.Wait();
    }
    if (pMidiThread)
    {
        bShutDownReq = TRUE;
        WaitForSingleObject(pMidiThread->m_hThread, INFINITE);
    }
    if (pFx)
    {
        for (int i = 0; i < N_SECONDARY_CHANNELS; i++)
        {
            pFx[i].Release(this);
            if (pFx[i].ailHSample)
                AIL_release_sample_handle(pFx[i].ailHSample);
        }
        delete[] pFx;
    }
    for (auto dpI = displacedCycled.begin(); dpI != displacedCycled.end(); dpI++)
        delete dpI->second;
    displacedCycled.clear();

    while (!lruList.IsEmpty())
    {
        AIL_mem_free_lock(lruList.GetHead().ailMemBlock);
        lruList.RemoveHead();
    }
    addrToPos.clear();

    if (midiCtrl.hSeq)
    {
        AIL_end_sequence(midiCtrl.hSeq);
        AIL_release_sequence_handle(midiCtrl.hSeq);
    }
    if (midiCtrl.pData0)
        AIL_mem_free_lock(midiCtrl.pData0);
    if (midiCtrl.pData1)
        AIL_mem_free_lock(midiCtrl.pData1);

    if (ailDigDriver)
        AIL_waveOutClose(ailDigDriver);
    if (ailMidiDriver)
        AIL_midiOutClose(ailMidiDriver);

    ClearFXResources();
    cStreamingResource.CloseResource();
    cMidiResource.CloseResource();
    cStreamingMarkers.Close();
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
    // Stop all currently played FXes
    if (pFx)
    {
        for (int i = 0; i < N_SECONDARY_CHANNELS; i++)
            pFx[i].Release(this);
    }
    dwUniqueCnt = 0;
    // Free all displaced cycled sounds
    for (auto dpI = displacedCycled.begin(); dpI != displacedCycled.end(); dpI++)
        delete dpI->second;
    displacedCycled.clear();
    // Stop streaming if required
    if (bStopStreaming)
    {
        EndStreaming();
        EndMIDI();
    }
}

void CSoundManager::PauseAllSounds()
{
    MiniaudioOneShotSfx::Pause("native_pause");
    if (pFx)
    {
        for (int i = 0; i < N_SECONDARY_CHANNELS; i++)
            if (pFx[i].bIsUsed)
                AIL_stop_sample(pFx[i].ailHSample);
    }
    PauseStreaming();
}

void CSoundManager::UnpauseAllSounds()
{
    MiniaudioOneShotSfx::Resume("native_resume");
    if (pFx)
    {
        for (int i = 0; i < N_SECONDARY_CHANNELS; i++)
            if (pFx[i].bIsUsed)
                AIL_resume_sample(pFx[i].ailHSample);
    }
    UnpauseStreaming();
}

void CSoundManager::Clear()
{
    ailDigDriver = NULL;
    ailMidiDriver = NULL;
    streamMain.Clear();
    pStreamThread = NULL;
    pMidiThread = NULL;
    bShutDownReq = FALSE;
    pFx = NULL;
    dwCurBytesInLRU = 0;
    for (int i = 0; i < N_SND_NUM_AMBIENTS; i++)
        ambients[i].Clear();
    for (int i = 0; i < N_SND_NUM_FXLISTS; i++)
        fxLists[i].Clear();
    dwUniqueCnt = 0;
    dwAmbientUpdateTime = 0;
    midiCtrl.hSeq = NULL;
    midiCtrl.state = ESMST_EMPTY;
    midiCtrl.pData0 = NULL;
    midiCtrl.pData1 = NULL;
    midiCtrl.vlmCtrl.Clear();
    bReady = FALSE;
    bMidiReady = FALSE;
    bAllowReverb = FALSE;
    bReverseStereo = FALSE;
    fpReverbLevel = fpReverbReflectionTime = fpReverbDecayTime = 0.0f;
    nFXMasterVolume = nSpeechMasterVolume = 100;
    szStreamingResource = "";
    uRandomStartPosition = 0;
    nTransactionCounter = 0;
    transactions.RemoveAll();
}

void AILCALLBACK EosCB(HSAMPLE s)
{
    CSoundManager *pSndMng = (CSoundManager *)AIL_sample_user_data(s, 0);
    SFxControl *pControl = (SFxControl *)AIL_sample_user_data(s, 1);
    pControl->bReqCleanup = TRUE;
}

// ------------------------------- Volumes ---------------------------------------------------

inline int CSoundManager::FinalCalcVolumes(BOOL bAffectWithHeights, BOOL bIsSpeech, int nOldVolume,
                                           float fpHeight)
{
    float fpVol = float(nOldVolume);
    if (bAffectWithHeights)
    {
        if (fpHeight >= FP_HIGH_HEIGHT_VALUE)
            fpVol = 0;
        else if (fpHeight > FP_LOW_HEIGHT_VALUE)
            fpVol *= (1 - (fpHeight - FP_LOW_HEIGHT_VALUE) /
                              (FP_HIGH_HEIGHT_VALUE - FP_LOW_HEIGHT_VALUE));
    }
    if (bIsSpeech)
        fpVol *= float(nSpeechMasterVolume);
    else
        fpVol *= float(nFXMasterVolume);
    return Min<int>(127, Max<int>(0, Float2Int(fpVol * 127.f / (100.f * 100.f))));
}

void CSoundManager::RecalcFXVolumes()
{
    SFxControl *pSnd = pFx;
    for (int i = 0; i < N_SECONDARY_CHANNELS; i++, pSnd++)
        if (pSnd->bIsUsed)
        {
            if (pSnd->bManualVolumes == FALSE)
                RecalcVolumes(pSnd);
            else
                pSnd->nPercentVolume = FinalCalcVolumes(pSnd->bAffectedWithHeight, pSnd->bIsSpeech,
                                                        pSnd->nManualVolume, listenerCP.z);
            pSnd->SetControls(bReverseStereo);
        }
}

void CSoundManager::SetFXMasterVolume(int nPercentVolume)
{
    if (!bReady)
        return;
    nFXMasterVolume = Min<int>(100, Max<int>(0, nPercentVolume));
    RecalcFXVolumes();
}

void CSoundManager::SetSpeechMasterVolume(int nPercentVolume)
{
    if (!bReady)
        return;
    nSpeechMasterVolume = Min<int>(100, Max<int>(0, nPercentVolume));
    RecalcFXVolumes();
}

void CSoundManager::SetMusicMasterVolume(int nPercentVolume)
{
    nPercentVolume = Min<int>(100, Max<int>(0, nPercentVolume));
#ifdef SM_SUPPORT_STREAMING
    SetStreamingVolume(nPercentVolume);
#endif // SM_SUPPORT_STREAMING
#ifdef SM_SUPPORT_MIDI
    SetMIDIVolume(nPercentVolume);
#endif // SM_SUPPORT_MIDI
}

int CSoundManager::GetMusicMasterVolume()
{
#ifdef SM_SUPPORT_STREAMING
    return GetMIDIDVolume();
#endif // SM_SUPPORT_STREAMING
#ifdef SM_SUPPORT_MIDI
    return GetStreamingVolume();
#endif // SM_SUPPORT_MIDI
    return 0;
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

void CSoundManager::ReleaseRefTo(std::string &szSoundName)
{
#ifdef _DEBUG
    auto hashPos = addrToPos.find(szSoundName);
    ASSERT(hashPos != addrToPos.end());
#endif
    SSoundLRU &lru = lruList.GetAt(addrToPos[szSoundName]);
    lru.nReferences--;
}

SFxControl *CSoundManager::GetFxControl(HSOUND hSnd) const
{
    if (!bReady || hSnd == SND_EMPTY)
        return NULL;

    for (int i = 0; i < N_SECONDARY_CHANNELS; i++)
        if (pFx[i].dwUniqueID == DWORD(hSnd))
        {
            if (pFx[i].bReqCleanup)
            {
                pFx[i].Release((CSoundManager *)this);
                return NULL;
            }
            return pFx + i;
        }
    return NULL;
}

SDisplacedCycledControl *CSoundManager::GetDisplaced(HSOUND hSnd)
{
    if (!bReady)
        return NULL;
    auto iter = displacedCycled.find(DWORD(hSnd));
    if (iter != displacedCycled.end())
        return iter->second;
    return NULL;
}

int CSoundManager::GetFxSlot(int nPriority)
{
    // Find empty slot or slot with minimal priority
    SFxControl *pRet = pFx;
    int nMinPrior = 0x7FFFFFFF, nMinIndex = 0;

    for (int i = 0; i < N_SECONDARY_CHANNELS; i++)
    {
        if (pRet->bIsUsed == FALSE)
            return i;
        if (pRet->nPriority < nMinPrior)
        {
            nMinPrior = pRet->nPriority;
            nMinIndex = i;
        }
        pRet++;
    }
    if (nMinPrior >= nPriority)
        return -1;
    if (pFx[nMinIndex].bIsLooped)
    {
        // Store displaced cycled sound
        SDisplacedCycledControl *pDCC = dbgnew SDisplacedCycledControl;
        SFxControl *pR = pFx + nMinIndex;

        pDCC->szSoundName = pR->szSoundName;

        pDCC->nPercentVolume = pR->nPercentVolume;
        pDCC->nManualVolume = pR->nManualVolume;
        pDCC->nPercentPan = pR->nPercentPan;
        pDCC->nPriority = pR->nPriority;
        pDCC->bIsSpeech = pR->bIsSpeech;
        pDCC->fpPlaybackLength = pR->fpPlaybackLength;

        pDCC->bManualVolumes = pR->bManualVolumes;
        pDCC->bAffectedWithHeight = pR->bAffectedWithHeight;
        pDCC->sourceCP = pR->sourceCP;
        pDCC->fpMinDistance = pR->fpMinDistance;
        pDCC->fpMaxDistance = pR->fpMaxDistance;

        pDCC->dwUniqueID = pR->dwUniqueID;

        pDCC->uPosAtDisplace = AIL_sample_position(pR->ailHSample);

        displacedCycled[pDCC->dwUniqueID] = pDCC;
    }
    pFx[nMinIndex].Release(this);
    return nMinIndex;
}

void CSoundManager::RecalcVolumes(SFxControl *pControl)
{
    // Calculate volume
    CPlot3D cpD = pControl->sourceCP - listenerCP;
    float fpDistance2 = cpD.x * cpD.x + cpD.y * cpD.y;
    if (fpDistance2 > pControl->fpMaxDistance * pControl->fpMaxDistance)
    {
        pControl->nPercentVolume = 0;
        return;
    }
    float fpDistance = FSqrt(fpDistance2);
    if (fpDistance < pControl->fpMinDistance)
        pControl->nPercentVolume = FinalCalcVolumes(TRUE, pControl->bIsSpeech, 100, listenerCP.z);
    else
    {
        float fpAttDistance = pControl->fpMaxDistance - pControl->fpMinDistance;
        pControl->nPercentVolume = 100 - Float2Int(((fpDistance - pControl->fpMinDistance) /
                                                    fpAttDistance) *
                                                   100.f);
        pControl->nPercentVolume = FinalCalcVolumes(TRUE, pControl->bIsSpeech,
                                                    pControl->nPercentVolume, listenerCP.z);
    }
    // Calculate pan
    float fpScalPro = fabs(cpD.x * listenerOrientation.x + cpD.y * listenerOrientation.y);
    if (fpScalPro > fpDistance)
        pControl->nPercentPan = 0;
    else
    {
        if (fpDistance == 0)
            pControl->nPercentPan = 0; // this really happens
        else
        {
            float fpAngle = ACos(fpScalPro / fpDistance);
            if (fpAngle > FP_PI2)
                fpAngle = FP_PI - fpAngle;
            pControl->nPercentPan = Float2Int((100.f / FP_PI2) * fpAngle);
            if (cpD.x * listenerOrientation.y < cpD.y * listenerOrientation.x)
                pControl->nPercentPan = -pControl->nPercentPan;
        }
    }
    assert(pControl->nPercentPan >= -100 && pControl->nPercentPan <= 100);
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
    return MiniaudioOneShotSfx::CaptureEncoded(name,bytes,size,"native_res");
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
    if (!bReady)
        return SND_EMPTY;
    if(ModRuntime::Current()) {
        char corrected[256]={};const char* effective=pszSoundName;
        if(!ModAudioPolicy::Name(this,pszSoundName,corrected,sizeof(corrected),effective))return SND_EMPTY;
        pszSoundName=effective;
        if(!CaptureModSound(pszSoundName))return SND_EMPTY;
        nPriority=ModAudioPolicy::Priority(nPriority,pszSoundName,bLooping!=0,bIsSpeech!=0,bIsDialogPlayMode!=0);
        float gain=1.f,pitch=1.f;ModAudioPolicy::Variation(pszSoundName,bLooping!=0,bIsSpeech!=0,bIsDialogPlayMode!=0,gain,pitch);
        const DWORD id=dwUniqueCnt;if(id==DWORD(SND_EMPTY))return SND_EMPTY;
        MiniaudioOneShotSfx::PartyVoiceBinding party={};
        const bool bound=ModAudioPolicy::TakeAck(bIsSpeech!=0,bIsDialogPlayMode!=0,bLooping!=0,party);
        if(MiniaudioOneShotSfx::TryStartVirtualFlat(this,id,pszSoundName,nPriority,nPercentVolume,nPercentPan,bLooping!=0,bAffectedWithHeight!=0,(bIsSpeech||bIsDialogPlayMode)!=0,bIsDialogPlayMode!=0,gain,pitch,bound?&party:nullptr,false)==MiniaudioOneShotSfx::STARTED){dwUniqueCnt=id+1;return id;}return SND_EMPTY;
    }
    // Get free FX slot
    int nFxIndex = GetFxSlot(nPriority);
    if (nFxIndex == -1)
    {
        if (bLooping)
        {
            // Store displaced cycled sound
            SDisplacedCycledControl *pDCC = dbgnew SDisplacedCycledControl;

            pDCC->szSoundName = pszSoundName;

            pDCC->nPercentVolume = 0;
            pDCC->nManualVolume = nPercentVolume;
            pDCC->nPercentPan = nPercentPan;
            pDCC->nPriority = nPriority;
            pDCC->bIsSpeech = bIsSpeech;
            pDCC->fpPlaybackLength = 0;
            pDCC->bManualVolumes = TRUE;
            pDCC->bAffectedWithHeight = bAffectedWithHeight;

            pDCC->dwUniqueID = dwUniqueCnt++;
            pDCC->uPosAtDisplace = 0;

            displacedCycled[pDCC->dwUniqueID] = pDCC;
            return pDCC->dwUniqueID;
        }
        return SND_EMPTY;
    }
    SFxControl *pSnd = pFx + nFxIndex;

    AIL_init_sample(pSnd->ailHSample);

    pSnd->bIsLooped = bLooping;
    pSnd->nManualVolume = nPercentVolume;
    pSnd->nPercentPan = nPercentPan;
    pSnd->nPriority = nPriority;
    pSnd->bManualVolumes = TRUE;
    pSnd->bAffectedWithHeight = bAffectedWithHeight;
    pSnd->bIsSpeech = bIsSpeech;
    pSnd->nPercentVolume = FinalCalcVolumes(bAffectedWithHeight, pSnd->bIsSpeech, nPercentVolume,
                                            listenerCP.z);
    // Begin playing sound
    if (nPercentVolume == 0 && bLooping == FALSE)
        return SND_EMPTY;
    if (!BeginFXLL(pSnd, nFxIndex, pszSoundName, bIsDialogPlayMode))
        return SND_EMPTY;

    if (bLooping)
        AIL_set_sample_loop_count(pSnd->ailHSample, 0);

    pSnd->SetControls(bReverseStereo);
    AIL_register_EOS_callback(pSnd->ailHSample, EosCB);
    if (bAllowReverb)
        AIL_set_sample_reverb(pSnd->ailHSample, fpReverbLevel, fpReverbReflectionTime,
                              fpReverbDecayTime);
    if (uRandomStartPosition)
        AIL_set_sample_ms_position(pSnd->ailHSample, random.Get(uRandomStartPosition));
    AIL_start_sample(pSnd->ailHSample);

    // Return sound ID
    return pSnd->dwUniqueID;
}

HSOUND CSoundManager::BeginFXInt(LPCSTR pszSoundName, int nPriority, const CPlot3D &sourceCP,
                                 float fpMinDistance, float fpMaxDistance, BOOL bLooping,
                                 BOOL bIsSpeech)
{
    if(ModRuntime::Current()) {
        char corrected[256]={};const char* effective=pszSoundName;
        if(!ModAudioPolicy::Name(this,pszSoundName,corrected,sizeof(corrected),effective))return SND_EMPTY;
        pszSoundName=effective;
        if(!CaptureModSound(pszSoundName))return SND_EMPTY;
        nPriority=ModAudioPolicy::Priority(nPriority,pszSoundName,bLooping!=0,bIsSpeech!=0,false);
        float gain=1.f,pitch=1.f;ModAudioPolicy::Variation(pszSoundName,bLooping!=0,bIsSpeech!=0,false,gain,pitch);
        const DWORD id=dwUniqueCnt;if(id==DWORD(SND_EMPTY))return SND_EMPTY;
        if(MiniaudioOneShotSfx::TryStartVirtual3D(this,id,pszSoundName,nPriority,&sourceCP,fpMinDistance,fpMaxDistance,bLooping!=0,bIsSpeech!=0,gain,pitch)==MiniaudioOneShotSfx::STARTED){dwUniqueCnt=id+1;return id;}return SND_EMPTY;
    }
    if (!bReady)
        return SND_EMPTY;

    if (nTransactionCounter > 0 && !bLooping)
    {
        STransactionControl tControl;
        strcpy(tControl.pszSoundName, pszSoundName);
        tControl.nPriority = nPriority;
        tControl.sourceCP = sourceCP;
        tControl.fpMinDist = fpMinDistance;
        tControl.fpMaxDist = fpMaxDistance;
        tControl.bCounted = false;
        transactions.AddTail(tControl);
        return SND_EMPTY;
    }

    // Get free FX slot
    int nFxIndex = GetFxSlot(nPriority);
    if (nFxIndex == -1)
    {
        if (bLooping)
        {
            // Store displaced cycled sound
            SDisplacedCycledControl *pDCC = dbgnew SDisplacedCycledControl;

            pDCC->szSoundName = pszSoundName;
            pDCC->sourceCP = sourceCP;
            pDCC->fpMinDistance = fpMinDistance;
            pDCC->fpMaxDistance = fpMaxDistance;
            pDCC->bManualVolumes = FALSE;
            pDCC->bAffectedWithHeight = FALSE;
            pDCC->bIsSpeech = bIsSpeech;
            pDCC->nPriority = nPriority;
            pDCC->dwUniqueID = dwUniqueCnt++;

            pDCC->uPosAtDisplace = 0;

            displacedCycled[pDCC->dwUniqueID] = pDCC;
            return pDCC->dwUniqueID;
        }
        return SND_EMPTY;
    }
    SFxControl *pSnd = pFx + nFxIndex;

    AIL_init_sample(pSnd->ailHSample);

    pSnd->bIsLooped = bLooping;
    pSnd->sourceCP = sourceCP;
    pSnd->fpMinDistance = fpMinDistance;
    pSnd->fpMaxDistance = fpMaxDistance;
    pSnd->bManualVolumes = FALSE;
    pSnd->bAffectedWithHeight = FALSE;
    pSnd->bIsSpeech = bIsSpeech;
    pSnd->nPriority = nPriority;
    RecalcVolumes(pSnd);
    // Begin playing sound
    if (pSnd->nPercentVolume == 0 && bLooping == FALSE)
        return SND_EMPTY;
    if (!BeginFXLL(pSnd, nFxIndex, pszSoundName, FALSE))
        return SND_EMPTY;

    if (bLooping)
        AIL_set_sample_loop_count(pSnd->ailHSample, 0);

    pSnd->SetControls(bReverseStereo);
    AIL_register_EOS_callback(pSnd->ailHSample, EosCB);
    if (bAllowReverb)
        AIL_set_sample_reverb(pSnd->ailHSample, fpReverbLevel, fpReverbReflectionTime,
                              fpReverbDecayTime);
    if (uRandomStartPosition)
        AIL_set_sample_ms_position(pSnd->ailHSample, random.Get(uRandomStartPosition));
    AIL_start_sample(pSnd->ailHSample);

    // Return sound ID
    return pSnd->dwUniqueID;
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

S32 AILCALLBACK DecompCallBack(U32 state, U32 value)
{
    switch (state)
    {
    case AIL_LENGTHY_INIT:
    case AIL_LENGTHY_DONE:
    case AIL_LENGTHY_UPDATE:
        break;
    case AIL_LENGTHY_SET_PREFERENCE:
        ((AILCODECSETPREF)value)("Requested bit width", 16);
        ((AILCODECSETPREF)value)("Requested sample rate", 44100);
        ((AILCODECSETPREF)value)("Requested # of channels", 2);
        break;
    }
    return 1;
}

BOOL CSoundManager::BeginFXLL(SFxControl *pSnd, int nFxIndex, LPCSTR pszSoundName,
                              BOOL bDialogPlayMode)
{
    POSITION pos;
    std::string szSoundName = std::string(pszSoundName);
    auto hashPos = addrToPos.find(szSoundName);
    BOOL bInLRU = (hashPos != addrToPos.end());
    if (bInLRU && !bDialogPlayMode)
    {
        pos = hashPos->second;
        SSoundLRU lru = lruList.GetAt(pos);
        lruList.RemoveAt(pos);
        lru.dwLastAccessTime = GetTickCount();
        lru.nReferences++;
        lruList.AddHead(lru);
        addrToPos[szSoundName] = lruList.GetHeadPosition();
        pSnd->fpPlaybackLength = GetPlaybackLength(lru.ailMemBlock, lru.dwSize);
        AIL_set_named_sample_file(pSnd->ailHSample, (char *)pszSoundName, lru.ailMemBlock,
                                  lru.dwSize, 0);
    }
    if (bInLRU == FALSE)
    {
        U32 uLen;
        CMappedResource *pResObject;
        void *pStart = GetFXFileFromResources(pszSoundName, (DWORD *)&uLen, &pResObject);
        if (!pStart)
            return FALSE;

        DWORD dwCurTime = GetTickCount();
        // Process lru list
        POSITION pos = lruList.GetTailPosition();
        while (pos && !bDialogPlayMode)
        {
            POSITION oldPos = pos;
            SSoundLRU lru = lruList.GetPrev(pos);
            if (dwCurBytesInLRU + uLen < N_MAX_BYTES_IN_LRU &&
                dwCurTime - lru.dwLastAccessTime < N_MS_TO_KEEP_IN_LRU)
                break;
            if (lru.nReferences <= 0)
            {
                AIL_mem_free_lock(lru.ailMemBlock);
                dwCurBytesInLRU -= lru.dwSize;
                addrToPos.erase(lru.szSoundName);
                lruList.RemoveAt(oldPos);
            }
        }
        // Decompress .MP3 file if in dialog play mode
        if (bDialogPlayMode)
        {
            void *pOutData;
            int nOutLen;
            S32 nRv = AIL_decompress_ASI(pStart, uLen, ".mp3", &pOutData, (U32 *)&nOutLen,
                                         DecompCallBack);
            pResObject->CloseFile((BYTE *)pStart);
            if (nRv == 0)
                return FALSE;
            uLen = nOutLen;
            pStart = pOutData;
        }
        // Add file to lru list
        SSoundLRU lru;
        lru.szSoundName = szSoundName;
        lru.dwLastAccessTime = dwCurTime;
        lru.dwSize = uLen;
        dwCurBytesInLRU += uLen;
        lru.nReferences = 1;
        if (!bDialogPlayMode)
        {
            lru.ailMemBlock = AIL_mem_alloc_lock(uLen);
            memcpy(lru.ailMemBlock, pStart, uLen);
            pResObject->CloseFile((BYTE *)pStart);
            lruList.AddHead(lru);
            addrToPos[szSoundName] = lruList.GetHeadPosition();
            AIL_set_named_sample_file(pSnd->ailHSample, (char *)pszSoundName, lru.ailMemBlock,
                                      lru.dwSize, 0);
        }
        else
        {
            lru.ailMemBlock = pStart;
            lruList.AddTail(lru);
            addrToPos[szSoundName] = lruList.GetTailPosition();
            AIL_set_sample_file(pSnd->ailHSample, lru.ailMemBlock, lru.dwSize);
        }
        pSnd->fpPlaybackLength = GetPlaybackLength(lru.ailMemBlock, lru.dwSize);
    }
    assert(addrToPos.size() == lruList.GetCount());
    // Fill fx control variables
    pSnd->szSoundName = szSoundName;
    pSnd->bIsUsed = TRUE;
    pSnd->dwUniqueID = dwUniqueCnt++;
    return TRUE;
}

BOOL CSoundManager::UpdateFX(HSOUND hSnd, int nPercentVolume, int nPercentPan)
{
    if(MiniaudioOneShotSfx::IsVirtualSound(this,hSnd))return MiniaudioOneShotSfx::UpdateVirtualFlat(this,hSnd,nPercentVolume,nPercentPan);
    SFxControl *pSnd = GetFxControl(hSnd);
    if (pSnd)
    {
        pSnd->nManualVolume = nPercentVolume;
        pSnd->nPercentVolume = FinalCalcVolumes(pSnd->bAffectedWithHeight, pSnd->bIsSpeech,
                                                pSnd->nManualVolume, listenerCP.z);
        pSnd->nPercentPan = nPercentPan;
        pSnd->bManualVolumes = TRUE;
        pSnd->SetControls(bReverseStereo);
        return TRUE;
    }
    SDisplacedCycledControl *pDsp = GetDisplaced(hSnd);
    if (pDsp)
    {
        pDsp->nManualVolume = nPercentVolume;
        pDsp->nPercentPan = nPercentPan;
        pDsp->bManualVolumes = TRUE;
        return TRUE;
    }
    return FALSE;
}

BOOL CSoundManager::UpdateFX(HSOUND hSnd, const CPlot3D &sourceCP, float fpMinDistance,
                             float fpMaxDistance)
{
    if(MiniaudioOneShotSfx::IsVirtualSound(this,hSnd))return MiniaudioOneShotSfx::UpdateVirtual3D(this,hSnd,&sourceCP,fpMinDistance,fpMaxDistance);
    SFxControl *pSnd = GetFxControl(hSnd);
    if (pSnd)
    {
        pSnd->sourceCP = sourceCP;
        pSnd->fpMinDistance = fpMinDistance;
        pSnd->fpMaxDistance = fpMaxDistance;
        pSnd->bManualVolumes = FALSE;
        RecalcVolumes(pSnd);
        pSnd->SetControls(bReverseStereo);
        return TRUE;
    }
    SDisplacedCycledControl *pDsp = GetDisplaced(hSnd);
    if (pDsp)
    {
        pDsp->sourceCP = sourceCP;
        pDsp->fpMinDistance = fpMinDistance;
        pDsp->fpMaxDistance = fpMaxDistance;
        pDsp->bManualVolumes = FALSE;
        return TRUE;
    }
    return FALSE;
}

HSOUND CSoundManager::CancelFX(HSOUND hSnd)
{
    if(MiniaudioOneShotSfx::CancelVirtual(this,hSnd))return SND_EMPTY;
    SFxControl *pSnd = GetFxControl(hSnd);
    if (pSnd)
        pSnd->Release(this);
    else
    {
        SDisplacedCycledControl *pDsp = GetDisplaced(hSnd);
        if (pDsp)
        {
            displacedCycled.erase(pDsp->dwUniqueID);
            delete pDsp;
        }
    }
    return SND_EMPTY;
}

float CSoundManager::GetFXPlaybackTime(HSOUND hSnd)
{
    if(MiniaudioOneShotSfx::IsVirtualSound(this,hSnd))return MiniaudioOneShotSfx::GetVirtualPlaybackTime(this,hSnd);
    SFxControl *pSnd = GetFxControl(hSnd);
    if (pSnd)
        return pSnd->fpPlaybackLength;
    SDisplacedCycledControl *pDsp = GetDisplaced(hSnd);
    if (pDsp)
        return pDsp->fpPlaybackLength;
    return 0.0f;
}

float CSoundManager::GetPlaybackLength(void *pData, DWORD dwSize)
{
    AILSOUNDINFO si;
    AIL_WAV_info(pData, &si);

    int nSamples;
    if (si.format == WAVE_FORMAT_IMA_ADPCM)
        nSamples = si.samples;
    else
    {
        if (si.bits * si.channels == 0)
            return 0.0f;
        nSamples = (si.data_len * 8) / (si.bits * si.channels);
    }
    return float(nSamples) / float(si.rate);
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
    RecalcFXVolumes();
}
// --------------------------------- Transactions-- -----------------------------------------------

void CSoundManager::BeginTransaction()
{
    nTransactionCounter++;
}

void CSoundManager::EndTransaction()
{
    ASSERT(nTransactionCounter > 0);
    nTransactionCounter--;
    if (nTransactionCounter > 0)
        return;

    POSITION pos = transactions.GetHeadPosition();
    while (pos)
    {
        STransactionControl &ctrl = transactions.GetNext(pos);
        if (ctrl.bCounted)
            continue;
        ctrl.bCounted = true;

        int nTotal = 1;
        CPlot3D cpMid = ctrl.sourceCP;
        float fpMinMid = ctrl.fpMinDist;
        float fpMaxMid = ctrl.fpMaxDist;

        POSITION inPos = pos;
        while (inPos)
        {
            STransactionControl &inCtrl = transactions.GetNext(inPos);
            if (inCtrl.bCounted)
                continue;
            if (stricmp(ctrl.pszSoundName, inCtrl.pszSoundName))
                continue;
            inCtrl.bCounted = true;
            cpMid += inCtrl.sourceCP;
            fpMinMid += inCtrl.fpMinDist;
            fpMaxMid += inCtrl.fpMaxDist;
            ++nTotal;
        }
        float fpTotalInv = 1.0f / float(nTotal);
        cpMid *= fpTotalInv;
        fpMinMid *= fpTotalInv;
        fpMaxMid *= fpTotalInv;
        BeginFX(ctrl.pszSoundName, ctrl.nPriority, cpMid, fpMinMid, fpMaxMid, FALSE);
    }
    transactions.RemoveAll();
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
    if(ModRuntime::Current()) {
        const auto& cfg=ModAudioSettings();const auto depth=theInterface.GetAudioScreenDepth();
        const bool modal=cfg.JMVMiniaudioModalSpeechPausePrototype && depth>1 && theWorld.IsCombatPause();
        const bool game=cfg.JMVMiniaudioGamePauseSpeechPrototype && depth<=1 && theWorld.IsCombatPause();
        MiniaudioOneShotSfx::SetModalSpeechPaused(modal||game,game,"native_pause_selector");
        MiniaudioOneShotSfx::Tick(this);
        ModAudioPolicy::Tick(this);
    }
    if (!bReady)
        return;

        // Volume controls
#ifdef SM_SUPPORT_MIDI
    midiCtrl.vlmCtrl.Segment();
    AIL_set_XMIDI_master_volume(ailMidiDriver, midiCtrl.vlmCtrl.GetMixerVolume());
#endif // SM_SUPPORT_MIDI

    // FX Cleanup
    for (int i = 0; i < N_SECONDARY_CHANNELS; i++)
        if (pFx[i].bReqCleanup)
            pFx[i].Release(this);

    // Cycled sounds restarting
    for (int i = 0; i < N_SECONDARY_CHANNELS && !displacedCycled.empty(); i++)
    {
        if (pFx[i].bIsUsed == FALSE)
        {
            int nMaxPri = displacedCycled.begin()->second->nPriority;
            DWORD nMaxUID = displacedCycled.begin()->second->dwUniqueID;
            // Find cycled sound to restart with maximum priority
            for (auto dpI = displacedCycled.begin(); dpI != displacedCycled.end(); dpI++)
                if (dpI->second->nPriority > nMaxPri)
                {
                    nMaxPri = dpI->second->nPriority;
                    nMaxUID = dpI->second->dwUniqueID;
                }
            SDisplacedCycledControl *pDCC = displacedCycled[nMaxUID];
            displacedCycled.erase(nMaxUID);

            // Restart cycled sound
            SFxControl *pR = pFx + i;

            pR->nPercentVolume = pDCC->nPercentVolume;
            pR->nManualVolume = pDCC->nManualVolume;
            pR->nPercentPan = pDCC->nPercentPan;
            pR->nPriority = pDCC->nPriority;
            pR->bIsSpeech = pDCC->bIsSpeech;
            pR->bIsLooped = TRUE;
            pR->fpPlaybackLength = pDCC->fpPlaybackLength;

            pR->bManualVolumes = pDCC->bManualVolumes;
            pR->bAffectedWithHeight = pDCC->bAffectedWithHeight;
            pR->sourceCP = pDCC->sourceCP;
            pR->fpMinDistance = pDCC->fpMinDistance;
            pR->fpMaxDistance = pDCC->fpMaxDistance;

            AIL_init_sample(pR->ailHSample);

            if (pR->bManualVolumes)
                pR->nPercentVolume = FinalCalcVolumes(pR->bAffectedWithHeight, pR->bIsSpeech,
                                                      pR->nPercentVolume, listenerCP.z);
            else
                RecalcVolumes(pR);

            if (BeginFXLL(pR, i, pDCC->szSoundName.c_str(), FALSE))
            {
                pR->dwUniqueID = pDCC->dwUniqueID;

                AIL_set_sample_loop_count(pR->ailHSample, 0);
                pR->SetControls(bReverseStereo);
                AIL_register_EOS_callback(pR->ailHSample, EosCB);
                if (bAllowReverb)
                    AIL_set_sample_reverb(pR->ailHSample, fpReverbLevel, fpReverbReflectionTime,
                                          fpReverbDecayTime);
                AIL_set_sample_position(pR->ailHSample, pDCC->uPosAtDisplace);
                AIL_start_sample(pR->ailHSample);
            }
            delete pDCC;
        }
    }
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

inline void SStreamControl::SetVolume()
{
    AIL_set_sample_volume(ailHSample, vlmCtrl.GetMixerVolume());
}

S32 AILCALLBACK MssAsiFetchCallBack(U32 user, void *pDest, S32 nDestBytes, S32 nOffset)
{
    return ((CSoundManager *)user)->MssAsiFetchCallBack(pDest, nDestBytes, nOffset);
}

void SStreamControl::Clear()
{
    state = ESMST_EMPTY;
    ailHSample = NULL;
    ailHDummySample = NULL;
    asiStream = NULL;
    pASI = NULL;
    buffers[0] = buffers[1] = buffers[2] = NULL;
    nBufSize = -1;
    nBuf0I = nBuf1I = nBufMixI = -1;
    nMixingSample = -1;
    nSampleToSwitch = -1;
    nSampleToLoop = -1;
    nSampleSize = -1;
    cPaddingValue = 0;
    szMainSoundName = "";
    szSwitchSoundName = "";

    dwFileSize = dwReadPosition = 0;

    switchingType = EM_INSTANT;
    vlmCtrl.Clear();
}

BOOL SStreamControl::Create(HDIGDRIVER ailDigDriver)
{
    HPROVIDER HP = RIB_find_file_provider("ASI codec", "Input file types", ".mp3");
    ailHSample = AIL_allocate_sample_handle(ailDigDriver);
    ailHDummySample = AIL_allocate_sample_handle(ailDigDriver);
    if (!HP || !ailHSample || !ailHDummySample)
        return Destroy();

    nBufSize = 2 * AIL_minimum_sample_buffer_size(ailDigDriver, 44100, DIG_F_STEREO_16);
    buffers[0] = (LPBYTE)AIL_mem_alloc_lock(nBufSize);
    buffers[1] = (LPBYTE)AIL_mem_alloc_lock(nBufSize);
    buffers[2] = (LPBYTE)AIL_mem_alloc_lock(nBufSize);
    AIL_init_sample(ailHSample);
    AIL_set_sample_type(ailHSample, DIG_F_16BITS_MASK | DIG_F_STEREO_MASK, DIG_PCM_SIGN);
    AIL_set_sample_playback_rate(ailHSample, 44100);
    AIL_set_sample_volume(ailHSample, 0);
    AIL_set_sample_pan(ailHSample, 0);
    memset(buffers[0], 0, nBufSize);
    memset(buffers[1], 0, nBufSize);
    memset(buffers[2], 0, nBufSize);

    AIL_set_sample_processor(ailHDummySample, DP_ASI_DECODER, HP);
    AIL_init_sample(ailHDummySample);
    pASI = &ailHDummySample->pipeline[DP_ASI_DECODER].TYPE.ASI;

    if (!pASI)
        return Destroy();

    nBuf0I = 0;
    nBuf1I = 1;
    nBufMixI = 2;
    nMixingSample = 0;
    state = ESMST_EMPTY;
    vlmCtrl.ResetFade();

    return TRUE;
}

BOOL SStreamControl::Destroy()
{
    if (asiStream && pASI)
        pASI->ASI_stream_close(asiStream);
    if (ailHSample)
        AIL_release_sample_handle(ailHSample);
    if (ailHDummySample)
        AIL_release_sample_handle(ailHDummySample);
    if (buffers[0])
        AIL_mem_free_lock(buffers[0]);
    if (buffers[1])
        AIL_mem_free_lock(buffers[1]);
    if (buffers[2])
        AIL_mem_free_lock(buffers[2]);

    Clear();
    return FALSE;
}

int SStreamControl::GetBufferIndex(int nNum)
{
    int nRv = nBufMixI;
    if (nNum == 0)
    {
        nBufMixI = nBuf0I;
        nBuf0I = nRv;
    }
    else
    {
        nBufMixI = nBuf1I;
        nBuf1I = nRv;
    }
    nMixingSample += nSamplesPerBuffer;
    return nRv;
}

void CSoundManager::SetStreamingVolume(int nPercentVolume)
{
    if(ModRuntime::Current()){streamMain.vlmCtrl.SetVolume(nPercentVolume);MiniaudioOneShotSfx::SetStreamingVolume(nPercentVolume);return;}
    if (!bReady)
        return;

    assert(nPercentVolume >= 0 || nPercentVolume <= 100);

    streamMain.vlmCtrl.SetVolume(nPercentVolume);
    streamMain.SetVolume();
    /*VOLUME=0
    if ( nPercentVolume == 0 )
            EndStreaming( EM_INSTANT );
    */
}

void CSoundManager::LoadStreamingMarkers(SStreamControl &sm)
{
    cStreamingMarkers.GetIntArray(sm.szMainSoundName, "Markers1", sm.nMarkersFast);
    cStreamingMarkers.GetIntArray(sm.szMainSoundName, "Markers2", sm.nMarkersSafe);
    if (sm.nMarkersSafe.GetSize() > 0)
        sm.nSampleToLoop = sm.nMarkersSafe[0];
    else
        sm.nSampleToLoop = 0x7FFFFFFF;
}

BOOL CSoundManager::SelectStreamingResource(LPCSTR pszResourceName, LPCSTR pszMarkersRegFileName)
{
    if(ModRuntime::Current()){
        std::string markerPath=ModRuntime::ConfigPath(pszMarkersRegFileName);
        return MiniaudioOneShotSfx::SetStreamingResource(this,pszResourceName,markerPath.c_str());
    }
#ifndef SM_SUPPORT_STREAMING
    return FALSE;
#endif // SM_SUPPORT_STREAMING
    if (!bReady)
        return TRUE;
#ifdef STREAMING_FROM_RESOURCE
    if (!cStreamingResource.OpenResource(pszResourceName))
        return FALSE;
#else
    szStreamingResource = pszResourceName;
    if (szStreamingResource.Right(1) != "\\")
        szStreamingResource += "\\";
#endif // STREAMING_FROM_RESOURCE
    if (!cStreamingMarkers.Open(pszMarkersRegFileName))
        return FALSE;
    return TRUE;
}

CString CSoundManager::GetStreamingName()
{
    if(ModRuntime::Current())return MiniaudioOneShotSfx::GetNativeStreamingName();
#ifndef SM_SUPPORT_STREAMING
    return "";
#endif // SM_SUPPORT_STREAMING

    streamMain.nameCritSection.Enter();
    CString szRet;

    if (streamMain.state == ESMST_PLAYING || streamMain.state == ESMST_SWITCHING)
        szRet = streamMain.szMainSoundName;
    //	else
    //	if ( streamMain.state == ESMST_SWITCHING )
    //		szRet = streamMain.szSwitchSoundName;
    else
        szRet = "";

    streamMain.nameCritSection.Leave();
    return szRet;
}

void CSoundManager::PauseStreaming()
{
    if(ModRuntime::Current()){MiniaudioOneShotSfx::PauseStreaming("native_stream_pause");return;}
#ifndef SM_SUPPORT_STREAMING
    return;
#endif // SM_SUPPORT_STREAMING
    if (bReady && streamMain.ailHSample)
        AIL_stop_sample(streamMain.ailHSample);
}

void CSoundManager::UnpauseStreaming()
{
    if(ModRuntime::Current()){MiniaudioOneShotSfx::ResumeStreaming("native_stream_resume");return;}
#ifndef SM_SUPPORT_STREAMING
    return;
#endif // SM_SUPPORT_STREAMING
    if (bReady && streamMain.ailHSample)
        AIL_resume_sample(streamMain.ailHSample);
}

void CSoundManager::FadeOutStreaming()
{
    if(ModRuntime::Current()){MiniaudioOneShotSfx::FadeOutStreaming();return;}
#ifndef SM_SUPPORT_STREAMING
    return;
#endif // SM_SUPPORT_STREAMING
    if (bReady)
        streamMain.vlmCtrl.BeginFadeOut();
}

void CSoundManager::FadeInStreaming()
{
    if(ModRuntime::Current()){MiniaudioOneShotSfx::FadeInStreaming();return;}
#ifndef SM_SUPPORT_STREAMING
    return;
#endif // SM_SUPPORT_STREAMING
    if (bReady)
        streamMain.vlmCtrl.BeginFadeIn();
}

BOOL CSoundManager::BeginStreaming(LPCSTR pszSoundName, ESNDMARKERTYPE markerType)
{
    if(ModRuntime::Current())return MiniaudioOneShotSfx::BeginStreaming(this,pszSoundName,int(markerType));
#ifndef SM_SUPPORT_STREAMING
    return FALSE;
#endif // SM_SUPPORT_STREAMING

    if (!bReady /*VOLUME=0 || streamMain.vlmCtrl.GetVolume() == 0 */)
        return TRUE;

    if (streamMain.pASI == NULL)
        return FALSE;

    streamingCritSection.Enter();

    if (streamMain.state == ESMST_EMPTY)
    {
        markerType = EM_INSTANT;
    }
    if (streamMain.state != ESMST_EMPTY && streamMain.szMainSoundName == pszSoundName &&
        *pszSoundName)
    {
        streamMain.state = ESMST_PLAYING; // Reset switching if exist
        if (streamMain.switchingType == EM_SAFE)
            streamMain.vlmCtrl.BeginFadeIn();
        streamingCritSection.Leave();
        return TRUE;
    }
    if (streamMain.state == ESMST_SWITCHING && streamMain.szSwitchSoundName == pszSoundName &&
        streamMain.switchingType >= markerType)
    {
        streamingCritSection.Leave(); // Continue switching
        return TRUE;
    }
    // switching
    // get switching marker
    int i, nSwitchSample = -1;
    switch (markerType)
    {
    case EM_INSTANT:
        nSwitchSample = streamMain.nMixingSample + streamMain.nSamplesPerBuffer;
        break;
    case EM_FAST:
        for (i = 0; i < streamMain.nMarkersFast.GetSize(); i++)
            if (streamMain.nMarkersFast[i] >=
                streamMain.nMixingSample + streamMain.nSamplesPerBuffer)
            {
                nSwitchSample = streamMain.nMarkersFast[i];
                break;
            }
        if (nSwitchSample == -1)
            if (streamMain.nMarkersFast.GetSize())
                nSwitchSample = streamMain.nMarkersFast[0];
            else
                markerType = EM_ENDOFTRACK;
        break;
    case EM_SAFE:
        nSwitchSample = 0x7FFFFFFF;
        streamMain.vlmCtrl.BeginFadeOut();
        break;
    case EM_ENDOFTRACK:
        break;
    }
    if (markerType == EM_ENDOFTRACK)
        nSwitchSample = 0x7FFFFFFF;
    if (nSwitchSample == -1)
    {
        streamingCritSection.Leave();
        return TRUE;
    }
    // Begin switching
    streamMain.state = ESMST_SWITCHING;
    streamMain.switchingType = markerType;
    streamMain.szSwitchSoundName = pszSoundName;
    streamMain.nSampleToSwitch = nSwitchSample;

    streamingCritSection.Leave();

    return TRUE;
}

void CSoundManager::EndStreaming(ESNDMARKERTYPE markerType)
{
    if(ModRuntime::Current()){MiniaudioOneShotSfx::EndStreaming(int(markerType),"native_stream_end");return;}
#ifndef SM_SUPPORT_STREAMING
    return;
#endif // SM_SUPPORT_STREAMING

    if (!bReady)
        return;

    streamingCritSection.Enter();
    if (streamMain.state != ESMST_EMPTY)
    {
        if (markerType == EM_INSTANT)
        {
            if (streamMain.asiStream)
            {
                streamMain.pASI->ASI_stream_close(streamMain.asiStream);
                streamMain.asiStream = NULL;
            }
            streamMain.state = ESMST_EMPTY;
            streamMain.szMainSoundName = "";
            streamMain.vlmCtrl.BeginFadeOut(FP_FADE_ADD2);
            streamMain.vlmCtrl.ResetFade(timeGetTime() + 2000);
        }
        else
            BeginStreaming("", markerType);
    }
    streamingCritSection.Leave();
}

// --------------------------- Fill streaming mixing buffers
// -----------------------------------------------

void CSoundManager::FillMixBuffer(SStreamControl &sm)
{
    if (sm.state == ESMST_EMPTY)
    {
        memset(sm.buffers[sm.nBufMixI], sm.cPaddingValue, sm.nBufSize);
        return;
    }
    BOOL bSwitchingReasonLoop;
    int nFBefore = sm.nSamplesPerBuffer, nFAfter = 0;

    if (sm.state == ESMST_SWITCHING && sm.switchingType == EM_SAFE && sm.vlmCtrl.IsFadedOut())
    {
        nFAfter = nFBefore;
        nFBefore = 0;
        bSwitchingReasonLoop = FALSE;
        sm.vlmCtrl.ResetFade(timeGetTime() + 4000);
    }
    else if ((sm.state == ESMST_PLAYING ||
              (sm.state == ESMST_SWITCHING && sm.nSampleToSwitch > sm.nSampleToLoop)) &&
             sm.nSampleToLoop >= sm.nMixingSample &&
             sm.nSampleToLoop < sm.nMixingSample + sm.nSamplesPerBuffer)
    {
        nFBefore = sm.nSampleToLoop - sm.nMixingSample;
        nFAfter = sm.nSamplesPerBuffer - nFBefore;
        bSwitchingReasonLoop = TRUE;
    }
    else if (sm.state == ESMST_SWITCHING && sm.nSampleToSwitch >= sm.nMixingSample &&
             sm.nSampleToSwitch < sm.nMixingSample + sm.nSamplesPerBuffer)
    {
        nFBefore = sm.nSampleToSwitch - sm.nMixingSample;
        nFAfter = sm.nSamplesPerBuffer - nFBefore;
        bSwitchingReasonLoop = FALSE;
    }

    nFBefore *= sm.nSampleSize;
    nFAfter *= sm.nSampleSize;
    S32 nFilled;
    if (sm.asiStream)
        nFilled = sm.pASI->ASI_stream_process(sm.asiStream, sm.buffers[sm.nBufMixI], nFBefore);
    else
    {
        nFBefore = 0;
        nFilled = 0;
        nFAfter = sm.nBufSize;
        bSwitchingReasonLoop = FALSE;
    }
    if (nFilled < nFBefore)
    {
        if (sm.state == ESMST_SWITCHING && sm.switchingType == EM_ENDOFTRACK)
        {
            nFBefore = nFilled;
            nFAfter = sm.nBufSize - nFBefore;
        }
        else
        {
            sm.pASI->ASI_stream_seek(sm.asiStream, 0);
            sm.nMixingSample = 0;
            S32 nF2 = sm.pASI->ASI_stream_process(sm.asiStream, sm.buffers[sm.nBufMixI] + nFilled,
                                                  nFBefore - nFilled);
            if (nF2 < nFBefore - nFilled)
                memset(sm.buffers[sm.nBufMixI] + nFilled + nF2, sm.cPaddingValue,
                       nFBefore - nFilled - nF2);
        }
    }
    if (nFAfter)
    {
        if (bSwitchingReasonLoop == TRUE)
        {
            sm.pASI->ASI_stream_seek(sm.asiStream, 0);
            sm.nMixingSample = 0;
            S32 nF2 = sm.pASI->ASI_stream_process(sm.asiStream, sm.buffers[sm.nBufMixI] + nFBefore,
                                                  nFAfter);
            if (nF2 < nFAfter)
                memset(sm.buffers[sm.nBufMixI] + nFBefore + nF2, sm.cPaddingValue, nFAfter - nF2);
        }
        else
        {
            sm.dwReadPosition = 0;
            sm.state = ESMST_EMPTY;

#ifdef STREAMING_FROM_RESOURCE
            CString szOpenName = sm.szSwitchSoundName;
#else
            CString szOpenName = szStreamingResource + sm.szSwitchSoundName;
#endif // STREAMING_FROM_RESOURCE

            if (sm.szSwitchSoundName == "" ||
                !cStreamingResource.OpenFile(szOpenName, &sm.dwFileSize))
            {
                memset(sm.buffers[sm.nBufMixI] + nFBefore, sm.cPaddingValue, nFAfter);
                sm.state = ESMST_EMPTY;
                if (sm.asiStream)
                {
                    sm.pASI->ASI_stream_close(sm.asiStream);
                    sm.asiStream = NULL;
                }
                streamMain.nameCritSection.Enter();
                sm.szMainSoundName = "";
                streamMain.nameCritSection.Leave();
            }
            else
            {
                sm.state = ESMST_PLAYING;
                streamMain.nameCritSection.Enter();
                sm.szMainSoundName = sm.szSwitchSoundName;
                streamMain.nameCritSection.Leave();
                LoadStreamingMarkers(sm);
                sm.nSampleToSwitch = -1;

                // Open stream and init sample
                ASISTAGE *pASI = sm.pASI;
                if (sm.asiStream)
                {
                    sm.pASI->ASI_stream_close(sm.asiStream);
                    sm.asiStream = NULL;
                }
                sm.asiStream = pASI->ASI_stream_open((U32)this, ::MssAsiFetchCallBack, 0);
                int nSampleRate = pASI->ASI_stream_attribute(sm.asiStream,
                                                             pASI->OUTPUT_SAMPLE_RATE);
                U32 nChannels = pASI->ASI_stream_attribute(sm.asiStream, pASI->OUTPUT_CHANNELS);
                U32 nBits = pASI->ASI_stream_attribute(sm.asiStream, pASI->OUTPUT_BITS);
                S32 nFileType, nFileFlags;

                if (nBits == 16)
                {
                    nFileFlags = DIG_PCM_SIGN;
                    nFileType = DIG_F_16BITS_MASK;
                    sm.cPaddingValue = 0;
                }
                else
                {
                    nFileFlags = 0;
                    nFileType = 0;
                    sm.cPaddingValue = 127;
                }
                if (nChannels == 2)
                    nFileType |= DIG_F_STEREO_MASK;

                AIL_set_sample_type(sm.ailHSample, nFileType, nFileFlags);
                AIL_set_sample_playback_rate(sm.ailHSample, nSampleRate);
                AIL_set_sample_volume(sm.ailHSample, 127);
                AIL_set_sample_pan(sm.ailHSample, 128 / 2);

                sm.nSampleSize = (nChannels * nBits) / 8;
                sm.nSamplesPerBuffer = streamMain.nBufSize / streamMain.nSampleSize;
                sm.nMixingSample = 0;

                sm.pASI->ASI_stream_seek(sm.asiStream, -1);
                S32 nF2 = sm.pASI->ASI_stream_process(sm.asiStream,
                                                      sm.buffers[sm.nBufMixI] + nFBefore, nFAfter);
                if (nF2 < nFAfter)
                    memset(sm.buffers[sm.nBufMixI] + nFBefore + nF2, sm.cPaddingValue,
                           nFAfter - nF2);
            }
        }
    }
}

int CSoundManager::MssAsiFetchCallBack(void *pDest, int nDestBytes, int nOffset)
{
    if (nOffset >= 0)
    {
        cStreamingResource.Seek(nOffset);
        streamMain.dwReadPosition = nOffset;
    }
    int nFileRest = streamMain.dwFileSize - streamMain.dwReadPosition;
    if (nDestBytes > nFileRest)
        nDestBytes = nFileRest;
    if (cStreamingResource.Read(pDest, nDestBytes) < 0)
        return 0;
    streamMain.dwReadPosition += nDestBytes;
    return nDestBytes;
}

// ------------------------ Main streaming thread -------------------------------------------

void CSoundManager::StreamingThread()
{
    DWORD dwLastTime = timeGetTime();
    while (!bShutDownReq)
    {
        DWORD dwCurTime = timeGetTime();
        DWORD dwPastTime = dwCurTime - dwLastTime;
        dwLastTime = dwCurTime;
        if (dwPastTime < N_STREAMING_MS_UPDATE)
            Sleep(N_STREAMING_MS_UPDATE - dwPastTime);

        // if ( streamMain.state == ESMST_EMPTY )
        //	continue;

        streamingCritSection.Enter();

        streamMain.vlmCtrl.Segment();
        streamMain.SetVolume();

        S32 nBufferReady;
        do
        {
            nBufferReady = AIL_sample_buffer_ready(streamMain.ailHSample);
            if (nBufferReady != -1)
            {
                AIL_load_sample_buffer(streamMain.ailHSample, nBufferReady,
                                       streamMain.buffers[streamMain.GetBufferIndex(nBufferReady)],
                                       streamMain.nBufSize);
                FillMixBuffer(streamMain);
            }
        } while (nBufferReady != -1);
        streamingCritSection.Leave();
    }
    streamShutDownEvent.Set();
    AfxEndThread(0);
}

// ------------------------------- MIDI sounds
// -------------------------------------------------------------

void CSoundManager::LoadMidiMarkers(SMIDIControl &sm)
{
    cMidiMarkers.GetIntArray(sm.szMainSoundName, "Markers1", sm.nMarkersFast);
    cMidiMarkers.GetIntArray(sm.szMainSoundName, "Markers2", sm.nMarkersSafe);
}

void CSoundManager::SetMIDIVolume(int nPercentVolume)
{
#ifndef SM_SUPPORT_MIDI
    return;
#endif // SM_SUPPORT_MIDI

    ASSERT(nPercentVolume >= 0 && nPercentVolume <= 127);

    if (bMidiReady)
    {
        midiCtrl.vlmCtrl.SetVolume(nPercentVolume);
        AIL_set_XMIDI_master_volume(ailMidiDriver, midiCtrl.vlmCtrl.GetMixerVolume());
    }
}

BOOL CSoundManager::SelectMIDIResource(LPCSTR pszResourceName, LPCSTR pszMarkersRegFileName)
{
#ifndef SM_SUPPORT_MIDI
    return FALSE;
#endif // SM_SUPPORT_MIDI
    if (!bMidiReady)
        return TRUE;

    if (!cMidiResource.OpenResource(pszResourceName))
        return FALSE;
    if (!cMidiMarkers.Open(pszMarkersRegFileName))
        return FALSE;

    return TRUE;
}

void AILEXPORT EndSeqCB(HSEQUENCE hSeq)
{
    CSoundManager *pSndMng = (CSoundManager *)AIL_sequence_user_data(hSeq, 0);
    pSndMng->MidiCB();
}

BOOL CSoundManager::BeginMIDI(LPCSTR pszSoundName, ESNDMARKERTYPE markerType)
{
#ifndef SM_SUPPORT_MIDI
    return FALSE;
#endif // SM_SUPPORT_MIDI
    if (!bMidiReady)
        return TRUE;

    DWORD dwLen;
    LPVOID pXMI;
    midiCtrl.midiCritSection.Enter();
    if (midiCtrl.state == ESMST_EMPTY)
    {
        if (midiCtrl.pData0)
        {
            AIL_mem_free_lock(midiCtrl.pData0);
            midiCtrl.pData0 = NULL;
        }

        pXMI = cMidiResource.GetFile(pszSoundName, &dwLen);
        if (pXMI == NULL || (midiCtrl.pData0 = AIL_mem_alloc_lock(dwLen)) == NULL)
        {
            midiCtrl.midiCritSection.Leave();
            return FALSE;
        }
        memcpy(midiCtrl.pData0, pXMI, dwLen);
        cMidiResource.CloseFile((BYTE *)pXMI);
        pXMI = 0;

        if (AIL_init_sequence(midiCtrl.hSeq, midiCtrl.pData0, 0) <= 0)
        {
            midiCtrl.midiCritSection.Leave();
            return FALSE;
        }
        AIL_set_sequence_user_data(midiCtrl.hSeq, 0, S32(this));
        AIL_register_sequence_callback(midiCtrl.hSeq, EndSeqCB);

        AIL_start_sequence(midiCtrl.hSeq);
        AIL_sequence_ms_position(midiCtrl.hSeq, (S32 *)&midiCtrl.nMSTotal, NULL);
        midiCtrl.state = ESMST_PLAYING;
        midiCtrl.szMainSoundName = pszSoundName;
        LoadMidiMarkers(midiCtrl);
        midiCtrl.midiCritSection.Leave();
        return TRUE;
    }
    if (midiCtrl.state != ESMST_EMPTY && midiCtrl.szMainSoundName == pszSoundName)
    {
        midiCtrl.state = ESMST_PLAYING; // Reset switching if exist
        if (midiCtrl.switchingType == EM_SAFE)
            midiCtrl.vlmCtrl.BeginFadeIn();
        midiCtrl.midiCritSection.Leave();
        return TRUE;
    }
    if (midiCtrl.state == ESMST_SWITCHING && midiCtrl.szSwitchSoundName == pszSoundName &&
        midiCtrl.switchingType >= markerType)
    {
        midiCtrl.midiCritSection.Leave(); // Continue switching
        return TRUE;
    }
    // get currently playing position
    int nMSCurrent;
    AIL_sequence_ms_position(midiCtrl.hSeq, NULL, (S32 *)&nMSCurrent);
    // get switching marker
    int i, nSwitchTime = -1;
    switch (markerType)
    {
    case EM_INSTANT:
        nSwitchTime = nMSCurrent + N_MIDI_SWITCHDELAY;
        break;
    case EM_FAST:
        for (i = 0; i < midiCtrl.nMarkersFast.GetSize(); i++)
            if (midiCtrl.nMarkersFast[i] >= nMSCurrent + N_MIDI_SWITCHDELAY)
            {
                nSwitchTime = midiCtrl.nMarkersFast[i];
                break;
            }
        if (nSwitchTime == -1)
            if (midiCtrl.nMarkersFast.GetSize())
                nSwitchTime = midiCtrl.nMarkersFast[0] + midiCtrl.nMSTotal;
            else
                markerType = EM_ENDOFTRACK;
        break;
    case EM_SAFE:
        nSwitchTime = 0x7FFFFFFF;
        midiCtrl.vlmCtrl.BeginFadeOut();
        break;
    case EM_ENDOFTRACK:
        nSwitchTime = midiCtrl.nMSTotal;
        break;
    }
    if (nSwitchTime == -1)
    {
        midiCtrl.midiCritSection.Leave();
        return TRUE;
    }
    // Begin switching
    if (midiCtrl.pData1)
    {
        AIL_mem_free_lock(midiCtrl.pData1);
        midiCtrl.pData1 = NULL;
    }
    pXMI = cMidiResource.GetFile(pszSoundName, &dwLen);
    if (pXMI == NULL || (midiCtrl.pData1 = AIL_mem_alloc_lock(dwLen)) == NULL)
    {
        midiCtrl.midiCritSection.Leave();
        return FALSE;
    }
    memcpy(midiCtrl.pData1, pXMI, dwLen);
    cMidiResource.CloseFile((BYTE *)pXMI);
    pXMI = 0;

    midiCtrl.state = ESMST_SWITCHING;
    midiCtrl.switchingType = markerType;
    midiCtrl.szSwitchSoundName = pszSoundName;
    midiCtrl.nSwitchingMsPosition = nSwitchTime;

    midiCtrl.midiCritSection.Leave();
    return TRUE;
}

void CSoundManager::EndMIDI(ESNDMARKERTYPE markerType)
{
#ifndef SM_SUPPORT_MIDI
    return;
#endif // SM_SUPPORT_MIDI
    if (!bMidiReady)
        return;

    midiCtrl.midiCritSection.Enter();
    if (midiCtrl.state != ESMST_EMPTY)
    {
        if (markerType == EM_INSTANT)
        {
            AIL_end_sequence(midiCtrl.hSeq);
            midiCtrl.state = ESMST_EMPTY;
            midiCtrl.szMainSoundName = "";
        }
        else
            BeginMIDI("", markerType);
    }
    midiCtrl.midiCritSection.Leave();
}

CString CSoundManager::GetMIDIName()
{
    midiCtrl.midiCritSection.Enter();
    CString szRet;
    if (streamMain.state != ESMST_PLAYING)
        szRet = streamMain.szSwitchSoundName;
    else
        szRet = "";
    midiCtrl.midiCritSection.Enter();
    return szRet;
}

// ------------------------------- Midi thread
// -------------------------------------------------------------

void CSoundManager::PerformMidiSwitch()
{
    ASSERT(midiCtrl.pData1);
    AIL_end_sequence(midiCtrl.hSeq);
    if (AIL_init_sequence(midiCtrl.hSeq, midiCtrl.pData1, 0) <= 0)
        midiCtrl.state = ESMST_EMPTY;
    else
    {
        midiCtrl.state = ESMST_PLAYING;
        midiCtrl.szMainSoundName = midiCtrl.szSwitchSoundName;

        AIL_mem_free_lock(midiCtrl.pData0);
        midiCtrl.pData0 = midiCtrl.pData1;
        midiCtrl.pData1 = NULL;

        AIL_set_sequence_user_data(midiCtrl.hSeq, 0, S32(this));
        AIL_register_sequence_callback(midiCtrl.hSeq, EndSeqCB);
        AIL_start_sequence(midiCtrl.hSeq);
        AIL_sequence_ms_position(midiCtrl.hSeq, (S32 *)&midiCtrl.nMSTotal, NULL);

        LoadMidiMarkers(midiCtrl);
    }
}

void CSoundManager::MidiCB()
{
    midiCtrl.midiCritSection.Enter();

    AIL_start_sequence(midiCtrl.hSeq);
    midiCtrl.nSwitchingMsPosition -= midiCtrl.nMSTotal;

    midiCtrl.midiCritSection.Leave();
}

void CSoundManager::MidiThread()
{
    while (!bShutDownReq)
    {
        midiCtrl.midiCritSection.Enter();
        if (midiCtrl.state != ESMST_SWITCHING)
        {
            midiCtrl.midiCritSection.Leave();
            Sleep(N_MIDI_SWITCHDELAY);
            continue;
        }

        if (midiCtrl.switchingType == EM_SAFE && midiCtrl.vlmCtrl.IsFadedOut())
        {
            PerformMidiSwitch();
            midiCtrl.vlmCtrl.ResetFade(0);
            midiCtrl.midiCritSection.Leave();
            continue;
        }

        int nMSCurrent;
        AIL_sequence_ms_position(midiCtrl.hSeq, NULL, (S32 *)&nMSCurrent);

        int nPlayToSwitch = midiCtrl.nSwitchingMsPosition - nMSCurrent;
        if (nPlayToSwitch < N_MIDI_SWITCHGAPTIME)
        {
            PerformMidiSwitch();
            midiCtrl.midiCritSection.Leave();
            continue;
        }
        if (nPlayToSwitch + N_MIDI_SWITCHGAPTIME >= N_MIDI_SWITCHDELAY)
        {
            midiCtrl.midiCritSection.Leave();
            Sleep(N_MIDI_SWITCHDELAY);
            continue;
        }
        midiCtrl.midiCritSection.Leave();
        Sleep(nPlayToSwitch - N_MIDI_SWITCHGAPTIME);
        continue;
    }
    AfxEndThread(0);
}

/*REQS:
1. No branches in XMI file
2. XMIDI(.xmi) files only. Can bee converted from MIDI(.mdi ) using MSS utility
*/
unsigned CSoundManager::CountNativeAudibleSpeech(unsigned& mask) const {
 unsigned count=MiniaudioOneShotSfx::CountAudibleVirtualSpeech(const_cast<CSoundManager*>(this));mask=count?0x80000000u:0;
 if(pFx)for(unsigned i=0;i<N_SECONDARY_CHANNELS;++i){const auto& f=pFx[i];if(f.bIsUsed&&f.bIsSpeech&&!f.bReqCleanup&&f.nPercentVolume>0){++count;mask|=1u<<i;}}return count;
}
