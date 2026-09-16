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
#include "CMMIOResource.h"
#include "CMappedResource.h"
#include "FileRegistry.h"
#include "Plot.h"
#include "MSS.h"
#include <string>
#include <unordered_map>

#include "SoundTypes.h"
class CSoundManager;
#include "MiniaudioOneShotSfx.h"

// Comment it to disable streaming sounds support
// FIXME: Streaming crashes for some reason. TBD
// #define SM_SUPPORT_STREAMING
// Comment it to disable MIDI sounds support
#ifndef _A4
// #define SM_SUPPORT_MIDI
#endif //_A4

// Comment it to get streaming files from ordinary files rather then resource
// #define STREAMING_FROM_RESOURCE

// Use SNDLIST_CYCLE_BIT in return value to make sound cycling
const int SNDLIST_CYCLE_BIT = 0x40000000;
typedef int (*SMGetNextPlayIndexFunc)(int nPrevIndex, int nPlayingNumber);

// -------------------------- Mixer constants ------------------------------------------------

const WORD W_MIXER_CHANNELS = 2;
const DWORD DW_MIXER_FREQ = 44100;
const WORD W_MIXER_BITS = 16;

// -------------------------- Control structures ---------------------------------------------

struct SFadeControl
{
    int nExternalVolume;
    int nMixerVolume;
    float fpFadeValue, fpFadeSpeed;
    enum
    {
        EFC_NONE,
        EFC_FIN,
        EFC_FOUT
    } fadeMode;
    BOOL bFadedOut;
    DWORD dwTimeToReset;

public:
    SFadeControl() { Clear(); }

    void SetVolume(int nPercentVolume);
    int GetVolume() { return nExternalVolume; }
    int GetMixerVolume() { return nMixerVolume; }

    void Segment();

    void BeginFadeOut(float fpFadeSpeed = 0);
    void BeginFadeIn(float fpFadeSpeed = 0);
    void ResetFade(DWORD dwTimeToReset = 0);
    BOOL IsFadedOut() { return bFadedOut; }
    BOOL IsFading() { return fadeMode != EFC_NONE; }

    void Clear();

private:
    void RecalcMixerVolume();
};

struct SStreamControl
{
    ESNDSTREAMSTATE state; // control block state

    HSAMPLE ailHSample;   // MSS sample handler
    HASISTREAM asiStream; // MSS ASI stream handler
    ASISTAGE *pASI;       // Pointer to MSS ASI provider

    LPBYTE buffers[3];     // sound buffers
    int nBufSize;          // size of the buffers
    int nSamplesPerBuffer; // number of samples per buffer

    int nBuf0I, nBuf1I, nBufMixI; // indexes of primary and back buffer
    int nMixingSample;            // the number of first sample currently int mixing buffer
    int nSampleToSwitch;          // the decided number of sample that is supposed to switch
    int nSampleToLoop;

    int nSampleSize;    // size of the sample in bytes
    BYTE cPaddingValue; // value to fill the remainings of buffer

    SFadeControl vlmCtrl; // volume control

    CString szMainSoundName;      // name of currently playing sound file
    CString szSwitchSoundName;    // name of sound file to switch
    CIntArray nMarkersFast;       // fast markers
    CIntArray nMarkersSafe;       // safe markers
    CCritSection nameCritSection; // safe get name critical section

    ESNDMARKERTYPE switchingType;

    DWORD dwFileSize; // MP3 file size and position
    DWORD dwReadPosition;

    HSAMPLE ailHDummySample; // dummy sample to obtain pointer to ASI provider

    void Clear();
    void SetVolume();
    BOOL Create(HDIGDRIVER ailDigDriver);
    BOOL Destroy();
    int GetBufferIndex(int);
};

struct SMIDIControl
{
    ESNDSTREAMSTATE state; // control block state

    LPVOID pData0, pData1; // Alloced data
    HSEQUENCE hSeq;        // MIDI sequence handler

    CString szMainSoundName;   // name of currently playing sound file
    CString szSwitchSoundName; // name of sound file to switch
    CIntArray nMarkersFast;    // fast markers
    CIntArray nMarkersSafe;    // safe markers

    int nSwitchingMsPosition; // time position to switch sequence
    int nMSTotal;             // total playing time of sequence

    SFadeControl vlmCtrl; // volume control

    ESNDMARKERTYPE switchingType;

    CCritSection midiCritSection; // critical section
};

struct SFxControl
{
    std::string szSoundName; // Name of sound file
    HSAMPLE ailHSample;      // AIL sound handler

    int nPercentVolume;     // volume in percent
    int nManualVolume;      // manually setted volume
    int nPercentPan;        // pan [-100..100]
    int nPriority;          // priority
    BOOL bIsUsed;           // that slot already used
    BOOL bIsLooped;         // that slot is playing looped sound
    BOOL bIsSpeech;         // is it speech sound ( for separate volume control only )
    float fpPlaybackLength; // full playbacklength in second

    BOOL bReqCleanup; // this slot ended playing and required cleanup

    BOOL bManualVolumes;                // turn off automatic volume calculations
    BOOL bAffectedWithHeight;           // set height affection for manual controlled
    CPlot3D sourceCP;                   // sound source
    float fpMinDistance, fpMaxDistance; // max hearing distance

    DWORD dwUniqueID;

    void Clear();                          // clear all data
    void Release(CSoundManager *);         // remove DS buffer
    void SetControls(BOOL bReverseStereo); // set DS volume and pan
};

struct SDisplacedCycledControl
{
    std::string szSoundName; // Name of sound file

    int nPercentVolume;     // volume in percent
    int nManualVolume;      // manually setted volume
    int nPercentPan;        // pan [-100..100]
    int nPriority;          // priority
    BOOL bIsSpeech;         // is it speech sound ( for separate volume control only )
    float fpPlaybackLength; // full playbacklength in second

    BOOL bManualVolumes;                // turn off automatic volume calculations
    BOOL bAffectedWithHeight;           // set height affection for manual controlled
    CPlot3D sourceCP;                   // sound source
    float fpMinDistance, fpMaxDistance; // max hearing distance

    DWORD dwUniqueID; // old unique ID

    U32 uPosAtDisplace; // playback position when displace occur
};

struct SSoundLRU
{
    void *ailMemBlock;       // ail sound buffer
    std::string szSoundName; // hash table index
    DWORD dwLastAccessTime;  // last access time
    DWORD dwSize;            // size of the sound data
    int nReferences;         // number of currently used references at this block
};

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
struct STransactionControl
{
    char pszSoundName[64];
    int nPriority;
    CPlot3D sourceCP;
    float fpMinDist, fpMaxDist;

    bool bCounted;
};

// ----------------------- Sound manager class -----------------------------------------------

class CSoundManager : public CGameBase
{
private:
    friend struct ModAudioLayout;
    HDIGDRIVER ailDigDriver;                                    // MSS digital sound driver
    HMDIDRIVER ailMidiDriver;                                   // MSS MIDI sound driver
    CMMIOResource cStreamingResource;                           // streaming resource
    CString szStreamingResource;                                // streaming directory
    CFileRegistry cStreamingMarkers;                            // streaming markers
    CMappedResource cMidiResource;                              // MIDI resource file
    CFileRegistry cMidiMarkers;                                 // MIDI markers
    CList<CMappedResource *, CMappedResource *> fxResourceList; // SFX resources

    SStreamControl streamMain;         // main streaming control block
    CCritSection streamingCritSection; // streaming critical section
    CWinThread *pStreamThread;         // streaming thread object pointer
    CWinEvent streamShutDownEvent;     // streaming shut down event

    SMIDIControl midiCtrl; // midi control block
    CWinThread *pMidiThread;

    volatile BOOL bShutDownReq;  // request for shutdown stream thread
    SFxControl *pFx;             // fx sounds
    CPlot3D listenerCP;          // fx listener position
    CPlot3D listenerOrientation; // sound source orientation
    DWORD dwUniqueCnt;           // unique IDs counter

    SAmbientControl ambients[N_SND_NUM_AMBIENTS]; // ambient sounds control structures
    DWORD dwAmbientUpdateTime;                    // previous ambiend sounds update time

    SFXListControl fxLists[N_SND_NUM_FXLISTS]; // FX list structures array

    DWORD dwCurBytesInLRU;                 // current number of wave data bytes stored in lru
    CList<SSoundLRU, SSoundLRU &> lruList; // lru list
    std::unordered_map<std::string, POSITION>
        addrToPos; // hash map to find list position by addresses
    std::unordered_map<DWORD, SDisplacedCycledControl *>
        displacedCycled; // hash map and storage for displaced cycled sounds

    BOOL bReady;         // is manager initialized successfully
    BOOL bMidiReady;     // is midi part of manager initialized successfully
    BOOL bAllowReverb;   // is reverb is allowed
    BOOL bReverseStereo; // is stereo reversed
    float fpReverbLevel, fpReverbReflectionTime, fpReverbDecayTime;
    int nFXMasterVolume, nSpeechMasterVolume;

    U32 uRandomStartPosition;

    int nTransactionCounter;
    CList<STransactionControl, STransactionControl &> transactions;

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
    // get direct sound object
    LPDIRECTSOUND GetDS();
    HDIGDRIVER GetDIGDriver() { return ailDigDriver; }
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
    BOOL IsStreamingSwitching() { return streamMain.state == ESMST_SWITCHING; }
    void PauseStreaming();
    void UnpauseStreaming();
    void FadeOutStreaming();
    void FadeInStreaming();
    // midi sounds
    BOOL
    SelectMIDIResource(LPCSTR pszResourceName,
                       LPCSTR pszMarkersRegFileName); // markers should be given in milliseconds
    BOOL BeginMIDI(LPCSTR pszSoundName, ESNDMARKERTYPE markerType = EM_INSTANT);
    void EndMIDI(ESNDMARKERTYPE markerType = EM_INSTANT);
    CString GetMIDIName();
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
    HSOUND BeginScenarioVoice(const char*,int,const MiniaudioOneShotSfx::PartyVoiceBinding* = nullptr);
    bool CaptureModSound(const char*);
    unsigned CountNativeAudibleSpeech(unsigned& mask) const;
    bool IsFXValid(HSOUND hSnd)
    {
        return (hSnd != SND_EMPTY) &&
               (MiniaudioOneShotSfx::IsVirtualSound(this,hSnd) || (GetFxControl(hSnd) != NULL) || (GetDisplaced(hSnd) != NULL));
    }
    float GetFXPlaybackTime(HSOUND hSnd);
    void SetRandomSampleStartPosition(U32 uMaxPosition) { uRandomStartPosition = uMaxPosition; }
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

    // Service functions
    int MssAsiFetchCallBack(void *pDest, int nDestBytes, int nOffset);
    void ReleaseRefTo(std::string &szSoundName);

private:
    // Initialization
    void Clear();
    // Streaming sounds
    void StreamingThread();
    friend UINT __cdecl StreamThreadStarter(LPVOID pParam);
    void FillMixBuffer(SStreamControl &);
    void LoadStreamingMarkers(SStreamControl &);
    void SetStreamingVolume(int nPercentVolume);
    int GetStreamingVolume() { return streamMain.vlmCtrl.GetVolume(); }
    // Midi sounds
    void MidiThread();
    friend UINT __cdecl MidiThreadStarter(LPVOID pParam);
    void LoadMidiMarkers(SMIDIControl &);
    void MidiCB();
    void PerformMidiSwitch();
    void SetMIDIVolume(int nPercentVolume);
    int GetMIDIDVolume() { return midiCtrl.vlmCtrl.GetVolume(); }
    // FX sounds
    SFxControl *GetFxControl(HSOUND hSnd) const;
    SDisplacedCycledControl *GetDisplaced(HSOUND hSnd);
    int GetFxSlot(int nPriority);
    void RecalcVolumes(SFxControl *pControl);
    BOOL BeginFXLL(SFxControl *pSnd, int nFxIndex, LPCSTR pszSoundName, BOOL bDialogPlayMode);
    void RecalcFXVolumes();
    int FinalCalcVolumes(BOOL bAffectWithHeights, BOOL bIsSpeech, int nOldVolume, float fpHeight);
    HSOUND BeginFXInt(LPCSTR pszSoundName, int nPriority, int nPercentVolume, int nPercentPan,
                      BOOL bLooping, BOOL bAffectedWithHeight, BOOL bIsSpeech,
                      BOOL bIsDialogPlayMode);
    HSOUND BeginFXInt(LPCSTR pszSoundName, int nPriority, const CPlot3D &sourceCP,
                      float fpMinDistance, float fpMaxDistance, BOOL bLooping, BOOL bIsSpeech);
    float GetPlaybackLength(void *pData, DWORD dwSize);
    // Sound lists
    SFXListControl *GetFXListControl(HSNDLIST);
    HSNDLIST BeginFXList(CStringArray &szSoundNames, int nPriority, const CPlot3D &sourceCP,
                         float fpMinDistance, float fpMaxDistance, BOOL bIsRandom, int nPlayCount,
                         SMGetNextPlayIndexFunc playingFunc);

    friend void AILCALLBACK EosCB(HSAMPLE);
    friend void AILEXPORT EndSeqCB(HSEQUENCE hSeq);
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
