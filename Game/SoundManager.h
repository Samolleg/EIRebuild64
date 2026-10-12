#pragma once
/**********************************************************************
 *<
        FILE: SoundManager.H

        DESCRIPTION:  Sound manager header

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Entertainment
 **********************************************************************/
#include "ClassIDs.h"
#include "GameBase.h"
#include "CMappedResource.h"
#include "FileRegistry.h"
#include "Plot.h"
#include <string>

#include "SoundTypes.h"
class CSoundManager;
#include "MiniaudioOneShotSfx.h"

// Use SNDLIST_CYCLE_BIT in return value to make sound cycling
const int SNDLIST_CYCLE_BIT = 0x40000000;
typedef int (*SMGetNextPlayIndexFunc)(int nPrevIndex, int nPlayingNumber);

// -------------------------- Mixer constants ------------------------------------------------

const WORD W_MIXER_CHANNELS = 2;
const DWORD DW_MIXER_FREQ = 44100;
const WORD W_MIXER_BITS = 16;

// -------------------------- Control structures ---------------------------------------------

struct SAmbientControl
{
    BOOL bIsUsed;
    CStringArray szNames;
    CPlot3D sourceCP;
    float fpMinDistance, fpMaxDistance;
    int nMsToReplayMin, nMsToReplayMax;
    DWORD dwLastPlayTime;

    void Clear();
};

struct SFXListControl
{
    BOOL bIsUsed;

    CStringArray szNames;
    HSOUND hPlayingSnd; // currently playing sound

    CPlot3D sourceCP;                   // sound source
    float fpMinDistance, fpMaxDistance; // max hearing distance
    int nPlayCount;
    int nCurPlIndex;
    int nPriority;
    BOOL bIsRandom;
    SMGetNextPlayIndexFunc playingFunc;

    DWORD dwUniqueCnt;
    DWORD dwUniqueID;

    void Clear(); // clear all data
    BOOL StartNext(CSoundManager *);
};

const int N_SND_NUM_AMBIENTS = 16;
const int N_SND_NUM_FXLISTS = 8;

HSOUND BeginFX(LPCSTR pszSoundName, int nPriority, const CPlot3D &sourceCP, float fpMinDistance,
               float fpMaxDistance, BOOL bLooping = FALSE);
// ----------------------- Sound manager class -----------------------------------------------

class CSoundManager : public CGameBase
{
private:
    friend struct ModAudioLayout;
    CList<CMappedResource *, CMappedResource *> fxResourceList;
    CPlot3D listenerCP;          // fx listener position
    CPlot3D listenerOrientation; // sound source orientation
    DWORD dwUniqueCnt;           // unique IDs counter

    SAmbientControl ambients[N_SND_NUM_AMBIENTS]; // ambient sounds control structures
    DWORD dwAmbientUpdateTime;                    // previous ambiend sounds update time

    SFXListControl fxLists[N_SND_NUM_FXLISTS]; // FX list structures array

    BOOL bReady;         // is manager initialized successfully
    BOOL bAllowReverb;   // is reverb is allowed
    BOOL bReverseStereo; // is stereo reversed
    float fpReverbLevel, fpReverbReflectionTime, fpReverbDecayTime;
    int nFXMasterVolume, nSpeechMasterVolume, nMusicMasterVolume;

    DWORD uRandomStartPosition;

    int nTransactionCounter;

public:
    // Constr/desctr
    CSoundManager();
    ~CSoundManager();
    // class identification functions
    LPCSTR GetGameClassName() const { return "CSoundManager"; }
    EClassIDs WhoAmI() const { return HPC_SOUNDMANAGER; }
    // Create/destroy
    BOOL GetDrivers(CStringArray &szDrivers);
    BOOL Create(HWND hWnd, BOOL bAllowReverb = FALSE, int nDriver = -1,
                DWORD dwMixerFreq = DW_MIXER_FREQ, WORD wMixerBits = W_MIXER_BITS);
    virtual BOOL Create();
    virtual void Destroy();
    void StopAllSounds(bool bStopStreaming = false);
    void PauseAllSounds();
    void UnpauseAllSounds();
    // transactions
    void BeginTransaction(); // used to stack similar sounds in one stream, works only for BeginFX
                             // in 3D positioning mode, makes all starting sounds HSOUNDS invalid
    void EndTransaction();
    bool IsActive() { return bReady != 0; }
    // volumes
    void SetFXMasterVolume(int nPercentVolume);
    int GetFXMasterVolume() { return nFXMasterVolume; }
    void SetSpeechMasterVolume(int nPercentVolume);
    int GetSpeechMasterVolume() { return nSpeechMasterVolume; }
    void SetMusicMasterVolume(int nPercentVolume);
    int GetMusicMasterVolume();
    // reverb ( fpLevel - [0..1], fpReflectionTime - time in seconds between reflections,
    // fpDecayTime - time to reflection to die out
    void SetReverb(float fpLevel, float fpReflectionTime, float fpDecayTime);
    void ClearReverb() { fpReverbLevel = 0.0f; }
    // reverse stereo
    void SetStereoReverse(BOOL bOn) { bReverseStereo = bOn; }
    // streaming sounds
    BOOL
    SelectStreamingResource(LPCSTR pszResourceName,
                            LPCSTR pszMarkersRegFileName); // markers should be given in samples
    BOOL BeginStreaming(LPCSTR pszSoundName, ESNDMARKERTYPE markerType = EM_INSTANT);
    void EndStreaming(ESNDMARKERTYPE markerType = EM_INSTANT);
    CString GetStreamingName();
    BOOL IsStreamingSwitching() { return MiniaudioOneShotSfx::IsNativeStreamingSwitching(); }
    void PauseStreaming();
    void UnpauseStreaming();
    void FadeOutStreaming();
    void FadeInStreaming();
    // FX sounds
    BOOL SelectFXResource(LPCSTR pszResourceName)
    {
        ClearFXResources();
        return AddFXResource(pszResourceName);
    }
    BOOL AddFXResource(LPCSTR pszResourceName);
    void ClearFXResources();
    void *GetFXFileFromResources(LPCSTR pszSoundName, DWORD *pLen = NULL,
                                 CMappedResource **pResObject = NULL);
    BOOL IsFXFileExist(LPCSTR pszSoundName);
    void GetFXFileList(CStringArray &fileList, LPCSTR pszMask);
    HSOUND BeginFX(LPCSTR pszSoundName, int nPriority, int nPercentVolume, int nPercentPan = 0,
                   BOOL bLooping = FALSE, BOOL bAffectedWithHeight = FALSE);
    HSOUND BeginSpeech(LPCSTR pszSoundName, int nPriority, int nPercentVolume, int nPercentPan = 0,
                       BOOL bLooping = FALSE, BOOL bAffectedWithHeight = FALSE);
    HSOUND BeginMP3Dialog(LPCSTR pszSoundName, int nPriority, int nPercentVolume,
                          int nPercentPan = 0);
    BOOL UpdateFX(HSOUND hSnd, int nPercentVolume, int nPercentPan);
    void SetFXListener(const CPlot3D &listenerCP, const CPlot3D &listenerOrientation);
    void GetFXListener(CPlot3D *pListenerCP, CPlot3D *pListenerOrientation)
    {
        *pListenerCP = listenerCP;
        *pListenerOrientation = listenerOrientation;
    }
    HSOUND BeginFX(LPCSTR pszSoundName, int nPriority, const CPlot3D &sourceCP, float fpMinDistance,
                   float fpMaxDistance, BOOL bLooping = FALSE);
    HSOUND BeginSpeech(LPCSTR pszSoundName, int nPriority, const CPlot3D &sourceCP,
                       float fpMinDistance, float fpMaxDistance, BOOL bLooping = FALSE);
    BOOL UpdateFX(HSOUND hSnd, const CPlot3D &sourceCP, float fpMinDistance, float fpMaxDistance);
    HSOUND CancelFX(HSOUND hSnd);
    HSOUND BeginPartySignalFX(const char*,int,unsigned,float,float);
    HSOUND BeginScenarioVoice(const char*,int,const MiniaudioOneShotSfx::PartyVoiceBinding* = nullptr);
    bool CaptureModSound(const char*);
    unsigned CountNativeAudibleSpeech(unsigned& mask) const;
    bool IsFXValid(HSOUND hSnd)
    {
        return (hSnd != SND_EMPTY) &&
               MiniaudioOneShotSfx::IsVirtualSound(this,hSnd);
    }
    float GetFXPlaybackTime(HSOUND hSnd);
    void SetRandomSampleStartPosition(DWORD uMaxPosition) { uRandomStartPosition = uMaxPosition; }
    // Sound lists
    HSNDLIST BeginFXList(CStringArray &szSoundNames, int nPriority, const CPlot3D &sourceCP,
                         float fpMinDistance, float fpMaxDistance, BOOL bIsRandom,
                         int nPlayCount = -1)
    {
        return BeginFXList(szSoundNames, nPriority, sourceCP, fpMinDistance, fpMaxDistance,
                           bIsRandom, nPlayCount, NULL);
    }
    HSNDLIST BeginFXList(CStringArray &szSoundNames, int nPriority, const CPlot3D &sourceCP,
                         float fpMinDistance, float fpMaxDistance,
                         SMGetNextPlayIndexFunc playingFunc)
    {
        return BeginFXList(szSoundNames, nPriority, sourceCP, fpMinDistance, fpMaxDistance, FALSE,
                           -1, playingFunc);
    }
    BOOL UpdateFXList(HSNDLIST, const CPlot3D &sourceCP);
    BOOL UpdateFXList(HSNDLIST, const CPlot3D &sourceCP, float fpMinDistance, float fpMaxDistance,
                      int nPlayCount = -1);
    HSNDLIST CancelFXList(HSNDLIST);
    bool IsFXListValid(HSNDLIST hSnd)
    {
        return (hSnd != SND_EMPTY) && (GetFXListControl(hSnd) != NULL);
    }
    // Ambient sounds
    HSNDAMBIENT BeginAmbient(CStringArray &szNames, const CPlot3D &sourceCP, float fpMinDistance,
                             float fpMaxDistance, int nMsToReplayMin, int nMsToReplayMax);
    void UpdateAmbient(HSNDAMBIENT, const CPlot3D &sourceCP, float fpMinDistance,
                       float fpMaxDistance, int nMsToReplayMin, int nMsToReplayMax);
    HSNDAMBIENT CancelAmbient(HSNDAMBIENT);
    void SegmentAmbient();

private:
    // Initialization
    void Clear();
    HSOUND BeginFXInt(LPCSTR pszSoundName, int nPriority, int nPercentVolume, int nPercentPan,
                      BOOL bLooping, BOOL bAffectedWithHeight, BOOL bIsSpeech,
                      BOOL bIsDialogPlayMode);
    HSOUND BeginFXInt(LPCSTR pszSoundName, int nPriority, const CPlot3D &sourceCP,
                      float fpMinDistance, float fpMaxDistance, BOOL bLooping, BOOL bIsSpeech);
    // Sound lists
    SFXListControl *GetFXListControl(HSNDLIST);
    HSNDLIST BeginFXList(CStringArray &szSoundNames, int nPriority, const CPlot3D &sourceCP,
                         float fpMinDistance, float fpMaxDistance, BOOL bIsRandom, int nPlayCount,
                         SMGetNextPlayIndexFunc playingFunc);

};

// ---------------------------------------------------------------------------------------------------------

inline HSOUND CSoundManager::BeginFX(LPCSTR pszSoundName, int nPriority, int nPercentVolume,
                                     int nPercentPan, BOOL bLooping, BOOL bAffectedWithHeight)
{
    return BeginFXInt(pszSoundName, nPriority, nPercentVolume, nPercentPan, bLooping,
                      bAffectedWithHeight, FALSE, FALSE);
}

inline HSOUND CSoundManager::BeginMP3Dialog(LPCSTR pszSoundName, int nPriority, int nPercentVolume,
                                            int nPercentPan)
{
    return BeginFXInt(pszSoundName, nPriority, nPercentVolume, nPercentPan, FALSE, FALSE, TRUE,
                      TRUE);
}

inline HSOUND CSoundManager::BeginSpeech(LPCSTR pszSoundName, int nPriority, int nPercentVolume,
                                         int nPercentPan, BOOL bLooping, BOOL bAffectedWithHeight)
{
    return BeginFXInt(pszSoundName, nPriority, nPercentVolume, nPercentPan, bLooping,
                      bAffectedWithHeight, TRUE, FALSE);
}

inline HSOUND CSoundManager::BeginFX(LPCSTR pszSoundName, int nPriority, const CPlot3D &sourceCP,
                                     float fpMinDistance, float fpMaxDistance, BOOL bLooping)
{
    return BeginFXInt(pszSoundName, nPriority, sourceCP, fpMinDistance, fpMaxDistance, bLooping,
                      FALSE);
}

inline HSOUND CSoundManager::BeginSpeech(LPCSTR pszSoundName, int nPriority,
                                         const CPlot3D &sourceCP, float fpMinDistance,
                                         float fpMaxDistance, BOOL bLooping)
{
    return BeginFXInt(pszSoundName, nPriority, sourceCP, fpMinDistance, fpMaxDistance, bLooping,
                      TRUE);
}
