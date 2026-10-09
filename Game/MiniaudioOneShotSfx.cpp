#include "StdAfx.h"
#include "GameJournal.h"
#include "MiniaudioOneShotSfx.h"
#include "VoiceWaitState.h"
#include "EngineDiagnostics.h"
#include "ScenarioSpeechDucking.h"
#include "WorldSfxPause.h"

#ifndef JMV_MINIAUDIO_WORLD_SFX_PAUSE_DIAGNOSTICS
#define JMV_MINIAUDIO_WORLD_SFX_PAUSE_DIAGNOSTICS 0
#endif

#include "ModAudioConfig.h"
#include "SoundManager.h"
#include "WorldClient.h"
#include "WorldServer.h"
#include "Player.h"
#include "Unit.h"
#include <cstdarg>
#include <cstdio>
static void NativeAudioLog(const char* fmt,...) { char buffer[2048]; va_list args;va_start(args,fmt);vsnprintf(buffer,sizeof(buffer),fmt,args);va_end(args);OutputDebugStringA(buffer);OutputDebugStringA("\n"); }
#define LOGINFO NativeAudioLog
#define LOGWARN NativeAudioLog
#define LOGERR NativeAudioLog
// Offsets are computed from this build, never retail EXE constants.
struct ModAudioLayout {
 static constexpr size_t LISTENER_X = offsetof(CSoundManager, listenerCP)+offsetof(CPlot3D,x);
 static constexpr size_t LISTENER_Y = offsetof(CSoundManager, listenerCP)+offsetof(CPlot3D,y);
 static constexpr size_t LISTENER_Z = offsetof(CSoundManager, listenerCP)+offsetof(CPlot3D,z);
 static constexpr size_t ORIENTATION_X = offsetof(CSoundManager, listenerOrientation)+offsetof(CPlot3D,x);
 static constexpr size_t ORIENTATION_Y = offsetof(CSoundManager, listenerOrientation)+offsetof(CPlot3D,y);
 static constexpr size_t ORIENTATION_Z = offsetof(CSoundManager, listenerOrientation)+offsetof(CPlot3D,z);
 static constexpr size_t UNIQUE_COUNTER = offsetof(CSoundManager,dwUniqueCnt);
 static constexpr size_t STREAM_SAMPLE = offsetof(CSoundManager,streamMain)+offsetof(SStreamControl,ailHSample);
 static constexpr size_t STREAM_SAMPLES_PER_BUFFER = offsetof(CSoundManager,streamMain)+offsetof(SStreamControl,nSamplesPerBuffer);
 static constexpr size_t READY = offsetof(CSoundManager,bReady);
 static constexpr size_t REVERSE_STEREO = offsetof(CSoundManager,bReverseStereo);
 static constexpr size_t FX_MASTER_VOLUME = offsetof(CSoundManager,nFXMasterVolume);
 static constexpr size_t SPEECH_MASTER_VOLUME = offsetof(CSoundManager,nSpeechMasterVolume);
};


#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <exception>
#include <new>
#include <string>
#include <vector>
#include <windows.h>

#pragma push_macro("f32")
#undef f32
#pragma push_macro("s32")
#undef s32
#pragma push_macro("s16")
#undef s16
#pragma push_macro("u32")
#undef u32
#pragma push_macro("u16")
#undef u16
#pragma push_macro("s8")
#undef s8
#pragma push_macro("u8")
#undef u8
#pragma push_macro("f64")
#undef f64
#define MA_NO_ENCODING
#define MA_NO_FLAC
#define MA_NO_VORBIS
#define MINIAUDIO_IMPLEMENTATION
#include "../third_party/miniaudio/miniaudio.h"
#pragma pop_macro("f32")
#pragma pop_macro("s32")
#pragma pop_macro("s16")
#pragma pop_macro("u32")
#pragma pop_macro("u16")
#pragma pop_macro("s8")
#pragma pop_macro("u8")
#pragma pop_macro("f64")
#include "AudioResourceManager.h"


static const char *LogSec = "MiniaudioOneShotSfx";

#ifndef JMV_SCENARIO_SPEECH_DUCK_DIAGNOSTICS
#define JMV_SCENARIO_SPEECH_DUCK_DIAGNOSTICS 0
#endif

#ifndef JMV_CREATEVOICEFX_DIAGNOSTICS
#define JMV_CREATEVOICEFX_DIAGNOSTICS 0
#endif

#ifndef JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
#define JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS 0
#endif

#ifndef JMV_MINIAUDIO_OCCLUSION_DIAGNOSTICS
#define JMV_MINIAUDIO_OCCLUSION_DIAGNOSTICS 0
#endif

#ifndef JMV_MINIAUDIO_REVERB_DIAGNOSTICS
#define JMV_MINIAUDIO_REVERB_DIAGNOSTICS 0
#endif

#ifndef JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
#define JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS 0
#endif

#ifndef JMV_MINIAUDIO_BINAURAL_DIAGNOSTICS
#define JMV_MINIAUDIO_BINAURAL_DIAGNOSTICS 0
#endif

#ifndef JMV_MINIAUDIO_BINK_DIAGNOSTICS
#define JMV_MINIAUDIO_BINK_DIAGNOSTICS 0
#endif

#ifndef JMV_MINIAUDIO_MODAL_SPEECH_PAUSE_DIAGNOSTICS
#define JMV_MINIAUDIO_MODAL_SPEECH_PAUSE_DIAGNOSTICS 0
#endif

#ifndef JMV_MINIAUDIO_GAME_PAUSE_SPEECH_DIAGNOSTICS
#define JMV_MINIAUDIO_GAME_PAUSE_SPEECH_DIAGNOSTICS 0
#endif

namespace MiniaudioOneShotSfx
{
namespace
{
const size_t SOUND_MANAGER_LISTENER_X_OFFSET = ModAudioLayout::LISTENER_X;
const size_t SOUND_MANAGER_LISTENER_Y_OFFSET = ModAudioLayout::LISTENER_Y;
const size_t SOUND_MANAGER_LISTENER_Z_OFFSET = ModAudioLayout::LISTENER_Z;
const size_t SOUND_MANAGER_ORIENTATION_X_OFFSET = ModAudioLayout::ORIENTATION_X;
const size_t SOUND_MANAGER_ORIENTATION_Y_OFFSET = ModAudioLayout::ORIENTATION_Y;
const size_t SOUND_MANAGER_ORIENTATION_Z_OFFSET = ModAudioLayout::ORIENTATION_Z;
const size_t SOUND_MANAGER_UNIQUE_COUNTER_OFFSET = ModAudioLayout::UNIQUE_COUNTER;
const size_t SOUND_MANAGER_STREAM_SAMPLE_OFFSET = ModAudioLayout::STREAM_SAMPLE;
const size_t SOUND_MANAGER_STREAM_SAMPLES_PER_BUFFER_OFFSET = ModAudioLayout::STREAM_SAMPLES_PER_BUFFER;
const size_t SOUND_MANAGER_READY_OFFSET = ModAudioLayout::READY;
const size_t SOUND_MANAGER_REVERSE_STEREO_OFFSET = ModAudioLayout::REVERSE_STEREO;
const size_t SOUND_MANAGER_FX_MASTER_VOLUME_OFFSET = ModAudioLayout::FX_MASTER_VOLUME;
const size_t SOUND_MANAGER_SPEECH_MASTER_VOLUME_OFFSET = ModAudioLayout::SPEECH_MASTER_VOLUME;
const size_t FX_VOLUME_OFFSET = offsetof(SFxControl,nPercentVolume);
const size_t FX_PAN_OFFSET = offsetof(SFxControl,nPercentPan);
const size_t FX_IS_USED_OFFSET = offsetof(SFxControl,bIsUsed);
const size_t FX_IS_LOOPED_OFFSET = offsetof(SFxControl,bIsLooped);
const size_t FX_REQ_CLEANUP_OFFSET = offsetof(SFxControl,bReqCleanup);
const size_t FX_UNIQUE_ID_OFFSET = offsetof(SFxControl,dwUniqueID);
const unsigned int MAX_ACTIVE_VOICES = 64;
#if JMV_DETEST_BUILD
const unsigned int DUPLICATE_DIAGNOSTIC_THRESHOLD = 3;
#endif
const DWORD ENGINE_INIT_RETRY_MS = 2000;
const DWORD DEVICE_RECOVERY_RETRY_MS = 2000;
const float MINIAUDIO_OCCLUSION_ATTENUATION = 0.35f;
const double MINIAUDIO_OCCLUSION_LPF_CUTOFF_HZ = 2200.0;
const ma_uint32 MINIAUDIO_OCCLUSION_LPF_ORDER = 2;
const DWORD MINIAUDIO_OCCLUSION_VOICE_TRACE_MS = 250;
const DWORD MINIAUDIO_OCCLUSION_GLOBAL_TRACE_MS = 25;
const DWORD MINIAUDIO_OCCLUSION_ATTACK_MS = 180;
const DWORD MINIAUDIO_OCCLUSION_RELEASE_MS = 350;
const DWORD MINIAUDIO_OCCLUSION_MAX_DELTA_MS = 1000;
const unsigned int MINIAUDIO_REVERB_PROFILE_COUNT = 2;
const unsigned int MINIAUDIO_REVERB_COMB_COUNT = 6;
const unsigned int MINIAUDIO_REVERB_ALLPASS_COUNT = 3;
const ma_uint32 MINIAUDIO_REVERB_MAX_CHANNELS = 2;
const DWORD MINIAUDIO_REVERB_TRANSITION_MS = 500;
const float MINIAUDIO_REVERB_OUTDOOR_DAMPING = 0.35f;
const float MINIAUDIO_REVERB_CAVE_DAMPING = 0.50f;
const float MINIAUDIO_REVERB_STEREO_CROSSFEED = 0.12f;
const ma_uint32 MINIAUDIO_BINAURAL_CHANNELS = 2;
const ma_uint32 MINIAUDIO_BINAURAL_DELAY_BUFFER_FRAMES = 256;
const float MINIAUDIO_BINAURAL_MAX_ITD_SECONDS = 0.00065f;
const float MINIAUDIO_BINAURAL_FAR_CUTOFF_MIN_HZ = 2500.0f;
const float MINIAUDIO_BINAURAL_FAR_CUTOFF_MAX_HZ = 12000.0f;
const float MINIAUDIO_BINAURAL_REAR_CUTOFF_HZ = 5000.0f;
const float MINIAUDIO_BINAURAL_SMOOTH_SECONDS = 0.025f;
const unsigned int MISSING_PATH_FLAT = 1u << 0;
const unsigned int MISSING_PATH_3D = 1u << 1;
const unsigned int MISSING_PATH_LL = 1u << 2;
const unsigned int MISSING_PATH_STREAM = 1u << 3;
const unsigned int MISSING_PATH_OTHER = 1u << 31;

const unsigned int MISSING_CLASS_ONESHOT = 1u << 0;
const unsigned int MISSING_CLASS_LOOP = 1u << 1;
const unsigned int MISSING_CLASS_SPEECH = 1u << 2;
const unsigned int MISSING_CLASS_DIALOG = 1u << 3;
const unsigned int MISSING_CLASS_MUSIC = 1u << 4;
const unsigned int MISSING_CLASS_OTHER = 1u << 31;
const size_t MISSING_AUDIO_LOG_CHUNK_BYTES = 512;
const uint32_t STREAMING_MARKERS_SIGNATURE = 0x45AB3EFBu;
const DWORD MAX_STREAMING_MARKERS_FILE_BYTES = 1024u * 1024u;
const unsigned int MAX_STREAMING_MARKERS_PER_VALUE = 256;

enum EFileProbeResult
{
    FILE_PROBE_FILE,
    FILE_PROBE_MISSING,
    FILE_PROBE_DIRECTORY,
    FILE_PROBE_ERROR
};

enum EStreamingResolveResult
{
    STREAM_RESOLVE_FILE,
    STREAM_RESOLVE_MISSING,
    STREAM_RESOLVE_INCOMPLETE,
    STREAM_RESOLVE_SENTINEL
};

struct SMiniaudioBinauralNode
{
    ma_node_base baseNode;
    float delayLines[MINIAUDIO_BINAURAL_CHANNELS]
                    [MINIAUDIO_BINAURAL_DELAY_BUFFER_FRAMES];
    float farLowPassState[MINIAUDIO_BINAURAL_CHANNELS];
    float rearLowPassState[MINIAUDIO_BINAURAL_CHANNELS];
    float currentDelayFrames[MINIAUDIO_BINAURAL_CHANNELS];
    float currentShadowMix[MINIAUDIO_BINAURAL_CHANNELS];
    float currentFarGain[MINIAUDIO_BINAURAL_CHANNELS];
    float currentRearMix;
    float currentRearGain;
    ma_uint32 cursor;
    ma_uint32 sampleRate;
    volatile LONG enabled;
    volatile LONG targetSidePermille;
    volatile LONG targetRearPermille;
    volatile LONG processedBlocks;
    volatile LONG inputBlocks;
    volatile LONG outputBlocks;
    volatile LONG peakInputPermille;
    volatile LONG peakOutputPermille;
    bool audioEnabled;
};

struct SVoice
{
    AudioResources::Handle resource;
    ma_sound sound;
    SMiniaudioBinauralNode binauralNode;
    ma_splitter_node occlusionSplitter;
    ma_lpf_node occlusionLowPass;
    char name[128];
    unsigned int sequence;
    int priority;
    DWORD startTick;
    float gainWithoutMaster;
    float virtualGainFactor;
    float virtualPosition[3];
    float virtualMinDistance;
    float virtualMaxDistance;
    PartyVoiceBinding partyVoice;
    void *partyVoiceWorld;
    char partyVoiceZone[MAX_PATH];
    bool partyVoiceEnabled;
    bool scenarioSpeech;
    bool diagnosticPartyVoiceLogged;
#if JMV_CREATEVOICEFX_DIAGNOSTICS
    DWORD partyVoiceLogTick;
    int partyVoiceLogGain;
    uint32_t partyVoiceLogListener;
#endif
    float occlusionVisibility;
    float occlusionVolumeFactor;
    float occlusionTargetVolumeFactor;
    float occlusionWet;
    float occlusionTargetWet;
    DWORD occlusionLastTraceTick;
    DWORD occlusionLastUpdateTick;
    LONG binauralLastDiagnosticBlocks;
    int binauralLastSector;
    bool soundInitialized;
    bool binauralNodeInitialized;
    bool binauralGraphInitialized;
    bool binauralFilterAttempted;
    bool binauralApplyLogged;
    bool occlusionSplitterInitialized;
    bool occlusionLowPassInitialized;
    bool occlusionGraphInitialized;
    bool occlusionFilterAttempted;
    bool occlusionTraceInitialized;
    bool occlusionLastBlocked;
    bool occlusionApplyLogged;
    bool reverbRouted;
    bool active;
    bool spatial;
    bool pausedByApp;
    bool pausedByModalSpeech;
    bool pausedByWorld;
    bool worldSfx;
    bool managed;
    bool looping;
    bool speech;
    bool dialog;
    bool virtualManaged;
    bool virtualManualVolumes;
    bool virtualAffectedWithHeight;
    void *fxControl;
    void *milesSample;
    void *virtualSoundManager;
    uint32_t managedUniqueId;
    uint32_t virtualId;
    int virtualManualVolume;
    int virtualPercentPan;
};

struct SMiniaudioReverbDelayLine
{
    float *frames;
    ma_uint32 frameCount;
    ma_uint32 cursor;
    float feedback;
    float damping;
    float filterState[MINIAUDIO_REVERB_MAX_CHANNELS];
};

struct SMiniaudioReverbBank
{
    SMiniaudioReverbDelayLine preDelay;
    SMiniaudioReverbDelayLine combs[MINIAUDIO_REVERB_COMB_COUNT];
    SMiniaudioReverbDelayLine allpasses[MINIAUDIO_REVERB_ALLPASS_COUNT];
    float level;
    float reflectionSeconds;
    float decaySeconds;
    float damping;
};

struct SMiniaudioReverbNode
{
    ma_node_base baseNode;
    SMiniaudioReverbBank banks[MINIAUDIO_REVERB_PROFILE_COUNT];
    ma_uint32 channels;
    ma_uint32 sampleRate;
    float profileBlend;
    volatile LONG targetProfile;
    volatile LONG settledProfile;
    volatile LONG processedBlocks;
    volatile LONG inputBlocks;
    volatile LONG outputBlocks;
    volatile LONG tailBlocks;
    volatile LONG peakInputPermille;
    volatile LONG peakOutputPermille;
};

struct SVirtualStartInfo
{
    const PartyVoiceBinding *partyVoice;
    bool scenarioSpeech;
    void *soundManager;
    uint32_t id;
    int manualVolume;
    int percentPan;
    float gainFactor;
    bool manualVolumes;
    bool affectedWithHeight;
    bool dialog;
};

struct SStreamingSound
{
    char resourcePath[MAX_PATH];
    char markersPath[MAX_PATH];
    char currentName[128];
    char currentPath[MAX_PATH];
    char pendingName[128];
    char pendingPath[MAX_PATH];
    ma_uint64 slotLoopEnd[2];
    ma_uint64 slotLoopMarkerFrame[2];
    ma_uint64 slotLength[2];
    ma_uint64 fastSwitchEngineFrame;
    ma_uint64 fastSwitchSourceFrame;
    ma_uint64 fastSwitchOutputFrame;
    ma_uint32 slotSampleRate[2];
    ma_uint32 slotMarkerRate[2];
    int pendingMarkerType;
    int volumePercent;
    float fadeGain;
    float fadeTarget;
    DWORD lastTick;
    unsigned int activeSlot;
    unsigned int fastSwitchSlot;
    bool volumeInitialized;
    bool slotInitialized[2];
    bool slotCustomLoop[2];
    bool initialized;
    bool playing;
    bool paused;
    bool fastSwitchScheduled;
    bool pendingStop;
    bool pendingNameTruncated;
    bool resourcePathTruncated;
    bool markersPathTruncated;
    bool markersLoaded;
};

struct SStreamingMarkerSet
{
    std::vector<uint32_t> fast;
    ma_uint64 loopFrame;
    bool hasLoop;

    SStreamingMarkerSet()
        : loopFrame(0)
        , hasLoop(false)
    {
    }
};

struct SMissingAudioRecord
{
    std::string key;
    std::string canonicalTarget;
    std::string requestedName;
    std::string targetName;
    std::string domain;
    std::string firstPath;
    std::string firstClass;
    std::string reason;
    std::vector<std::string> requestedAliases;
    std::vector<std::string> canonicalAliases;
    uint64_t count;
    DWORD firstTick;
    DWORD lastTick;
    unsigned int pathMask;
    unsigned int classMask;
};

ma_engine Engine = {};
ma_sound_group ReverbSourceGroup = {};
ma_splitter_node ReverbSplitter = {};
SMiniaudioReverbNode ReverbNode = {};
SVoice Voices[MAX_ACTIVE_VOICES] = {};
static_assert(MAX_ACTIVE_VOICES == 64, "Voice wait slot encoding");
VoiceWait::Registry VoiceLifetimes;
ScenarioSpeechDucking ScenarioDucking;
ma_sound StreamingSounds[2] = {};
SStreamingSound Streaming = {};
std::vector<unsigned char> StreamingMarkersData;
void *CurrentSoundManager = nullptr;
DWORD LastUniqueCounter = 0;
ma_device_state LastDeviceState = ma_device_state_uninitialized;
unsigned int StartSequence = 0;
unsigned int ActiveVoiceCount = 0;
unsigned int CaptureDiagnosticCount = 0;
unsigned int EndDiagnosticCount = 0;
unsigned int FallbackDiagnosticCount = 0;
unsigned int SuppressDiagnosticCount = 0;
#if JMV_DETEST_BUILD
unsigned int DuplicateAllowedDiagnosticCount = 0;
#endif
unsigned int DeviceErrorDiagnosticCount = 0;
unsigned int DeviceRecoveryDiagnosticCount = 0;
unsigned int ModalSpeechResumeFailureDiagnosticCount = 0;
unsigned int EqualRangeDiagnosticCount = 0;
unsigned int EvictDiagnosticCount = 0;
unsigned int StealDiagnosticCount = 0;
unsigned int MissingAudioStreamIncompleteDiagnosticCount = 0;
unsigned int OcclusionFilterReadyDiagnosticCount = 0;
unsigned int OcclusionFilterErrorDiagnosticCount = 0;
unsigned int OcclusionTraceDiagnosticCount = 0;
unsigned int OcclusionApplyDiagnosticCount = 0;
unsigned int OcclusionTraceCount = 0;
unsigned __int64 OcclusionTotalTraceTicks = 0;
unsigned __int64 OcclusionMaxTraceTicks = 0;
unsigned int ReverbRouteDiagnosticCount = 0;
unsigned int ReverbErrorDiagnosticCount = 0;
unsigned int BinauralFilterReadyDiagnosticCount = 0;
unsigned int BinauralFilterErrorDiagnosticCount = 0;
unsigned int BinauralApplyDiagnosticCount = 0;
unsigned int BinauralProcessDiagnosticCount = 0;
unsigned int BinkSystemEntryDiagnosticCount = 0;
unsigned int BinkSystemDiagnosticCount = 0;
unsigned int BinkTrackEntryDiagnosticCount = 0;
unsigned int BinkTrackPhaseDiagnosticCount = 0;
unsigned int BinkReadyDiagnosticCount = 0;
unsigned int BinkLockDiagnosticCount = 0;
unsigned int BinkStartDeferredDiagnosticCount = 0;
unsigned int BinkBufferDiagnosticCount = 0;
unsigned int BinkFallbackDiagnosticCount = 0;
LONG ReverbLastDiagnosticBlocks = 0;
int ReverbLastRequestedProfile = -1;
int ReverbLastSettledProfile = -1;
bool ReverbNodeInitialized = false;
bool ReverbSplitterInitialized = false;
bool ReverbSourceGroupInitialized = false;
bool ReverbGraphInitialized = false;
bool ReverbGraphAttempted = false;
bool ReverbInterfaceBypassLogged = false;
bool ReverbSpeechBypassLogged = false;
bool ReverbDialogBypassLogged = false;
bool BinauralSpeechBypassLogged = false;
bool BinauralDialogBypassLogged = false;
bool BinauralFlatBypassLogged = false;
bool EngineInitialized = false;
bool InitAttempted = false;
bool AppAudioSuspended = false;
bool WorldSfxSuspended = false;
bool ModalSpeechSuspended = false;
bool GamePauseSpeechSelectorActive = false;
bool BinkDeferredEngineShutdown = false;
volatile LONG BinkActiveVoiceCount = 0;
volatile LONG BinkVoiceSequence = 0;
VirtualDynamicGainResolver DynamicGainResolver = nullptr;
DWORD LastInitAttemptTick = 0;
DWORD LastDeviceRecoveryAttemptTick = 0;
unsigned int EngineInitAttemptCount = 0;
const char *LastStartReasonText = "none";
std::vector<SMissingAudioRecord> MissingAudioRecords;
SRWLOCK MissingAudioCatalogLock = SRWLOCK_INIT;
SRWLOCK MissingAudioLogLock = SRWLOCK_INIT;
uint64_t MissingAudioTotalRequests = 0;
uint64_t MissingAudioCatalogRevision = 0;
uint64_t MissingAudioLastDumpRevision = static_cast<uint64_t>(-1);
void *OcclusionWorld = nullptr;
bool OcclusionWorldInitialized = false;
bool OcclusionNoWorldLogged = false;
DWORD OcclusionLastGlobalTraceTick = 0;
LARGE_INTEGER OcclusionPerformanceFrequency = {};

class SExclusiveSrwLockGuard
{
public:
    explicit SExclusiveSrwLockGuard(SRWLOCK &lock)
        : lock_(&lock)
    {
        AcquireSRWLockExclusive(lock_);
    }

    ~SExclusiveSrwLockGuard()
    {
        ReleaseSRWLockExclusive(lock_);
    }

private:
    SExclusiveSrwLockGuard(const SExclusiveSrwLockGuard &);
    SExclusiveSrwLockGuard &operator=(const SExclusiveSrwLockGuard &);

    SRWLOCK *lock_;
};

bool IsStrictTakeover()
{
    return ModAudioSettings().JMVMiniaudioFullBackendPrototype ||
        (ModAudioSettings().JMVMiniaudioOneShotSfxPrototype &&
         ModAudioSettings().JMVMiniaudioOneShotStrictTakeover);
}

const char *FailureRoute()
{
    return IsStrictTakeover() ? "drop" : "miles";
}

const char *CacheFailureRoute()
{
    return IsStrictTakeover() ? "drop" : "miles_capture";
}

void SetLastStartReason(const char *reason)
{
    LastStartReasonText = reason != nullptr ? reason : "unknown";
}

bool ShouldLogLimited(unsigned int &counter)
{
    ++counter;
    return counter <= 32 || (counter % 64) == 0;
}

void CopyName(char *destination, size_t capacity, const char *source)
{
    if (destination == nullptr || capacity == 0)
        return;
    if (source == nullptr)
    {
        destination[0] = '\0';
        return;
    }
    strncpy(destination, source, capacity - 1);
    destination[capacity - 1] = '\0';
}

void ClearPendingStreamingName()
{
    Streaming.pendingName[0] = '\0';
    Streaming.pendingPath[0] = '\0';
    Streaming.pendingNameTruncated = false;
}

void SetPendingStreamingName(const char *soundName)
{
    Streaming.pendingNameTruncated = soundName != nullptr &&
        strlen(soundName) >= sizeof(Streaming.pendingName);
    CopyName(
        Streaming.pendingName,
        sizeof(Streaming.pendingName),
        soundName);
}

bool IsStreamingMarkersEnabled()
{
    return ModAudioSettings().JMVMiniaudioFullBackendPrototype &&
        ModAudioSettings().JMVMiniaudioStreamingMusicPrototype &&
        ModAudioSettings().JMVMiniaudioStreamingMarkersPrototype;
}

bool StreamingMarkerRangeIsValid(size_t offset, size_t bytes)
{
    return offset <= StreamingMarkersData.size() &&
        bytes <= StreamingMarkersData.size() - offset;
}

uint16_t ReadStreamingMarkerU16(size_t offset)
{
    const unsigned char *data = &StreamingMarkersData[offset];
    return static_cast<uint16_t>(data[0]) |
        (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t ReadStreamingMarkerU32(size_t offset)
{
    const unsigned char *data = &StreamingMarkersData[offset];
    return static_cast<uint32_t>(data[0]) |
        (static_cast<uint32_t>(data[1]) << 8) |
        (static_cast<uint32_t>(data[2]) << 16) |
        (static_cast<uint32_t>(data[3]) << 24);
}

uint16_t HashStreamingMarkerName(
    const char *name,
    size_t length,
    uint16_t tableSize)
{
    if (name == nullptr || tableSize == 0)
        return 0;
    uint16_t hash = 0;
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char value = static_cast<unsigned char>(name[i]);
        if (value >= 'A' && value <= 'Z')
            value = static_cast<unsigned char>(value + ('a' - 'A'));
        hash = static_cast<uint16_t>(hash + value);
    }
    return static_cast<uint16_t>(hash % tableSize);
}

bool StreamingMarkerNameEquals(
    size_t offset,
    uint16_t storedLength,
    const char *expected)
{
    if (expected == nullptr || strlen(expected) != storedLength ||
        !StreamingMarkerRangeIsValid(offset, storedLength))
    {
        return false;
    }
    return _memicmp(
               &StreamingMarkersData[offset], expected, storedLength) == 0;
}

bool ValidateStreamingMarkerValue(
    size_t sectionOffset,
    uint32_t relativeOffset)
{
    if (relativeOffset > StreamingMarkersData.size() - sectionOffset)
        return false;
    const size_t valueOffset = sectionOffset + relativeOffset;
    if (!StreamingMarkerRangeIsValid(valueOffset, 3))
        return false;
    const unsigned char type = StreamingMarkersData[valueOffset];
    const int16_t nameLength = static_cast<int16_t>(
        ReadStreamingMarkerU16(valueOffset + 1));
    if (nameLength <= 0 || nameLength > 128 ||
        !StreamingMarkerRangeIsValid(
            valueOffset + 3, static_cast<size_t>(nameLength)))
    {
        return false;
    }
    const size_t dataOffset = valueOffset + 3 + nameLength;
    if (type == 0)
        return StreamingMarkerRangeIsValid(dataOffset, 4);
    if (type != 0x80 || !StreamingMarkerRangeIsValid(dataOffset, 2))
        return false;
    const uint16_t count = ReadStreamingMarkerU16(dataOffset);
    if (count > MAX_STREAMING_MARKERS_PER_VALUE)
        return false;
    return StreamingMarkerRangeIsValid(
        dataOffset + 2, static_cast<size_t>(count) * 4);
}

bool ValidateStreamingMarkersCatalog(uint16_t &sectionCount)
{
    sectionCount = 0;
    if (!StreamingMarkerRangeIsValid(0, 6) ||
        ReadStreamingMarkerU32(0) != STREAMING_MARKERS_SIGNATURE)
    {
        return false;
    }
    sectionCount = ReadStreamingMarkerU16(4);
    if (sectionCount == 0 ||
        !StreamingMarkerRangeIsValid(
            6, static_cast<size_t>(sectionCount) * 6))
    {
        return false;
    }
    for (uint16_t sectionIndex = 0;
         sectionIndex < sectionCount;
         ++sectionIndex)
    {
        const size_t hashOffset = 6 +
            static_cast<size_t>(sectionIndex) * 6;
        const int16_t nextEntry = static_cast<int16_t>(
            ReadStreamingMarkerU16(hashOffset));
        const uint32_t sectionDataOffset =
            ReadStreamingMarkerU32(hashOffset + 2);
        if ((nextEntry < -1 || nextEntry >= sectionCount) ||
            !StreamingMarkerRangeIsValid(sectionDataOffset, 4))
        {
            return false;
        }
        const int16_t valueCount = static_cast<int16_t>(
            ReadStreamingMarkerU16(sectionDataOffset));
        const int16_t nameLength = static_cast<int16_t>(
            ReadStreamingMarkerU16(sectionDataOffset + 2));
        if (valueCount <= 0 || valueCount > 128 ||
            nameLength <= 0 || nameLength > 128)
        {
            return false;
        }
        const size_t valueTableOffset = sectionDataOffset + 4 +
            static_cast<size_t>(nameLength);
        if (!StreamingMarkerRangeIsValid(
                sectionDataOffset + 4,
                static_cast<size_t>(nameLength)) ||
            !StreamingMarkerRangeIsValid(
                valueTableOffset,
                static_cast<size_t>(valueCount) * 6))
        {
            return false;
        }
        for (int valueIndex = 0; valueIndex < valueCount; ++valueIndex)
        {
            const size_t valueHashOffset = valueTableOffset +
                static_cast<size_t>(valueIndex) * 6;
            const int16_t nextValue = static_cast<int16_t>(
                ReadStreamingMarkerU16(valueHashOffset));
            const uint32_t valueDataOffset =
                ReadStreamingMarkerU32(valueHashOffset + 2);
            if (nextValue < -1 || nextValue >= valueCount ||
                !ValidateStreamingMarkerValue(
                    sectionDataOffset, valueDataOffset))
            {
                return false;
            }
        }
    }
    return true;
}

bool FindStreamingMarkerSection(
    const char *sectionName,
    size_t &sectionOffset,
    uint16_t &valueCount,
    size_t &valueTableOffset)
{
    sectionOffset = 0;
    valueCount = 0;
    valueTableOffset = 0;
    if (!Streaming.markersLoaded || sectionName == nullptr ||
        !StreamingMarkerRangeIsValid(0, 6))
    {
        return false;
    }
    const uint16_t sectionCount = ReadStreamingMarkerU16(4);
    if (sectionCount == 0)
        return false;
    int index = HashStreamingMarkerName(
        sectionName, strlen(sectionName), sectionCount);
    for (uint16_t traversed = 0;
         index >= 0 && index < sectionCount && traversed < sectionCount;
         ++traversed)
    {
        const size_t hashOffset = 6 + static_cast<size_t>(index) * 6;
        const uint32_t candidateOffset =
            ReadStreamingMarkerU32(hashOffset + 2);
        const int16_t candidateValueCount = static_cast<int16_t>(
            ReadStreamingMarkerU16(candidateOffset));
        const uint16_t candidateNameLength =
            ReadStreamingMarkerU16(candidateOffset + 2);
        if (StreamingMarkerNameEquals(
                candidateOffset + 4,
                candidateNameLength,
                sectionName))
        {
            sectionOffset = candidateOffset;
            valueCount = static_cast<uint16_t>(candidateValueCount);
            valueTableOffset = candidateOffset + 4 +
                candidateNameLength;
            return true;
        }
        index = static_cast<int16_t>(
            ReadStreamingMarkerU16(hashOffset));
    }
    return false;
}

bool ReadStreamingMarkerArray(
    size_t sectionOffset,
    uint16_t valueCount,
    size_t valueTableOffset,
    const char *valueName,
    std::vector<uint32_t> &values)
{
    values.clear();
    if (valueName == nullptr || valueCount == 0)
        return false;
    int index = HashStreamingMarkerName(
        valueName, strlen(valueName), valueCount);
    for (uint16_t traversed = 0;
         index >= 0 && index < valueCount && traversed < valueCount;
         ++traversed)
    {
        const size_t hashOffset = valueTableOffset +
            static_cast<size_t>(index) * 6;
        const uint32_t relativeOffset =
            ReadStreamingMarkerU32(hashOffset + 2);
        const size_t valueOffset = sectionOffset + relativeOffset;
        const unsigned char type = StreamingMarkersData[valueOffset];
        const uint16_t nameLength =
            ReadStreamingMarkerU16(valueOffset + 1);
        if (StreamingMarkerNameEquals(
                valueOffset + 3, nameLength, valueName))
        {
            const size_t dataOffset = valueOffset + 3 + nameLength;
            if (type == 0)
            {
                values.push_back(ReadStreamingMarkerU32(dataOffset));
                return true;
            }
            if (type != 0x80)
                return false;
            const uint16_t count = ReadStreamingMarkerU16(dataOffset);
            values.reserve(count);
            for (uint16_t markerIndex = 0;
                 markerIndex < count;
                 ++markerIndex)
            {
                values.push_back(ReadStreamingMarkerU32(
                    dataOffset + 2 +
                    static_cast<size_t>(markerIndex) * 4));
            }
            return true;
        }
        index = static_cast<int16_t>(
            ReadStreamingMarkerU16(hashOffset));
    }
    return false;
}

bool ResolveStreamingMarkerSet(
    const char *soundName,
    SStreamingMarkerSet &markers)
{
    markers.fast.clear();
    markers.loopFrame = 0;
    markers.hasLoop = false;
    if (!IsStreamingMarkersEnabled() || !Streaming.markersLoaded ||
        soundName == nullptr || *soundName == '\0')
    {
        return false;
    }
    size_t sectionOffset = 0;
    size_t valueTableOffset = 0;
    uint16_t valueCount = 0;
    if (!FindStreamingMarkerSection(
            soundName, sectionOffset, valueCount, valueTableOffset))
    {
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGINFO("MINIAUDIO_STREAM_MARKERS_RESOLVE name=%s section=missing fast_count=0 loop=none fallback=physical_track",
                soundName);
#endif
        return false;
    }
    ReadStreamingMarkerArray(
        sectionOffset,
        valueCount,
        valueTableOffset,
        "Markers1",
        markers.fast);
    std::vector<uint32_t> loopMarkers;
    if (ReadStreamingMarkerArray(
            sectionOffset,
            valueCount,
            valueTableOffset,
            "Markers2",
            loopMarkers) &&
        !loopMarkers.empty())
    {
        markers.loopFrame = loopMarkers[0];
        markers.hasLoop = true;
    }
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
    LOGINFO("MINIAUDIO_STREAM_MARKERS_RESOLVE name=%s section=found fast_count=%u loop=%s loop_frame=%I64u",
            soundName,
            static_cast<unsigned int>(markers.fast.size()),
            markers.hasLoop ? "found" : "none",
            static_cast<unsigned __int64>(markers.loopFrame));
#endif
    return true;
}

void ClearStreamingMarkersCatalog()
{
    StreamingMarkersData.clear();
    Streaming.markersLoaded = false;
    Streaming.markersPath[0] = '\0';
    Streaming.markersPathTruncated = false;
}

bool LoadStreamingMarkersCatalog(const char *markersPath)
{
    ClearStreamingMarkersCatalog();
    if (!IsStreamingMarkersEnabled())
        return false;
    if (markersPath == nullptr || *markersPath == '\0' ||
        strlen(markersPath) >= sizeof(Streaming.markersPath))
    {
        Streaming.markersPathTruncated = markersPath != nullptr &&
            strlen(markersPath) >= sizeof(Streaming.markersPath);
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=catalog_path reason=%s fallback=legacy_miniaudio",
               Streaming.markersPathTruncated ? "truncated" : "empty");
#endif
        return false;
    }
    CopyName(
        Streaming.markersPath,
        sizeof(Streaming.markersPath),
        markersPath);
    HANDLE file = CreateFileA(
        markersPath,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=catalog_open path=%s win_error=%lu fallback=legacy_miniaudio",
               markersPath,
               static_cast<unsigned long>(GetLastError()));
#endif
        return false;
    }
    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < 6 ||
        fileSize.QuadPart > MAX_STREAMING_MARKERS_FILE_BYTES)
    {
        const DWORD sizeError = GetLastError();
        CloseHandle(file);
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=catalog_size path=%s size=%I64d win_error=%lu fallback=legacy_miniaudio",
               markersPath,
               static_cast<__int64>(fileSize.QuadPart),
               static_cast<unsigned long>(sizeError));
#endif
        return false;
    }
    try
    {
        StreamingMarkersData.resize(
            static_cast<size_t>(fileSize.QuadPart));
    }
    catch (const std::exception &)
    { GameJournal::AllocationFailureInCatch("MiniaudioOneShotSfx");
        CloseHandle(file);
        StreamingMarkersData.clear();
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=catalog_allocate path=%s bytes=%I64d fallback=legacy_miniaudio",
               markersPath,
               static_cast<__int64>(fileSize.QuadPart));
#endif
        return false;
    }
    DWORD bytesRead = 0;
    const BOOL readResult = ReadFile(
        file,
        &StreamingMarkersData[0],
        static_cast<DWORD>(StreamingMarkersData.size()),
        &bytesRead,
        nullptr);
    const DWORD readError = GetLastError();
    CloseHandle(file);
    if (readResult == FALSE || bytesRead != StreamingMarkersData.size())
    {
        StreamingMarkersData.clear();
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=catalog_read path=%s read=%lu expected=%u win_error=%lu fallback=legacy_miniaudio",
               markersPath,
               static_cast<unsigned long>(bytesRead),
               static_cast<unsigned int>(fileSize.QuadPart),
               static_cast<unsigned long>(readError));
#endif
        return false;
    }
    uint16_t sectionCount = 0;
    if (!ValidateStreamingMarkersCatalog(sectionCount))
    {
        StreamingMarkersData.clear();
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=catalog_validate path=%s bytes=%u fallback=legacy_miniaudio",
               markersPath,
               static_cast<unsigned int>(fileSize.QuadPart));
#endif
        return false;
    }
    Streaming.markersLoaded = true;
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
    LOGINFO("MINIAUDIO_STREAM_MARKERS_CATALOG path=%s bytes=%u sections=%u status=ready",
            markersPath,
            static_cast<unsigned int>(fileSize.QuadPart),
            static_cast<unsigned int>(sectionCount));
#endif
    return true;
}

bool CaptureMissingAudioText(
    const char *source,
    std::string &exact,
    std::string &canonical)
{
    exact.clear();
    canonical.clear();
    if (source == nullptr)
        return false;

    exact.assign(source);
    canonical.assign(source);
    const size_t length = canonical.size();
    for (size_t i = 0; i < length; ++i)
    {
        char &value = canonical[i];
        if (value >= 'A' && value <= 'Z')
            value = static_cast<char>(value - 'A' + 'a');
    }
    return !canonical.empty();
}

void EscapeMissingAudioLogText(
    const std::string &source,
    std::string &escaped)
{
    static const char HexDigits[] = "0123456789ABCDEF";
    escaped.clear();
    escaped.reserve(source.size());
    for (size_t i = 0; i < source.size(); ++i)
    {
        const unsigned char value =
            static_cast<unsigned char>(source[i]);
        switch (value)
        {
        case '\\':
            escaped.append("\\\\");
            break;
        case '\t':
            escaped.append("\\t");
            break;
        case '\r':
            escaped.append("\\r");
            break;
        case '\n':
            escaped.append("\\n");
            break;
        default:
            if (value < 0x20 || value == 0x7F)
            {
                escaped.append("\\x");
                escaped.push_back(HexDigits[(value >> 4) & 0x0F]);
                escaped.push_back(HexDigits[value & 0x0F]);
            }
            else
            {
                escaped.push_back(static_cast<char>(value));
            }
            break;
        }
    }
}

void EmitMissingAudioLogField(
    const char *eventName,
    uint64_t recordId,
    const char *fieldName,
    const std::string &escapedValue)
{
    const size_t chunkCount = escapedValue.empty()
        ? 1
        : ((escapedValue.size() - 1) / MISSING_AUDIO_LOG_CHUNK_BYTES) + 1;
    for (size_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex)
    {
        const size_t offset = chunkIndex * MISSING_AUDIO_LOG_CHUNK_BYTES;
        const size_t remaining = offset < escapedValue.size()
            ? escapedValue.size() - offset
            : 0;
        const size_t length = remaining < MISSING_AUDIO_LOG_CHUNK_BYTES
            ? remaining
            : MISSING_AUDIO_LOG_CHUNK_BYTES;
        const std::string chunk = escapedValue.substr(offset, length);
        LOGINFO("MINIAUDIO_MISSING_AUDIO_FIELD\tv=2\tpid=%lu\tevent=%s\trecord=%llu\tfield=%s\tchunk=%llu\tchunks=%llu\tvalue=%s",
                static_cast<unsigned long>(GetCurrentProcessId()),
                eventName,
                static_cast<unsigned long long>(recordId),
                fieldName,
                static_cast<unsigned long long>(chunkIndex + 1),
                static_cast<unsigned long long>(chunkCount),
                chunk.c_str());
    }
}

unsigned int MissingAudioPathMask(const char *path)
{
    if (path == nullptr)
        return MISSING_PATH_OTHER;
    if (_stricmp(path, "flat") == 0)
        return MISSING_PATH_FLAT;
    if (_stricmp(path, "3d") == 0)
        return MISSING_PATH_3D;
    if (_stricmp(path, "ll") == 0)
        return MISSING_PATH_LL;
    if (_stricmp(path, "stream") == 0)
        return MISSING_PATH_STREAM;
    return MISSING_PATH_OTHER;
}

unsigned int MissingAudioClassMask(const char *soundClass)
{
    if (soundClass == nullptr)
        return MISSING_CLASS_OTHER;
    if (_stricmp(soundClass, "oneshot") == 0)
        return MISSING_CLASS_ONESHOT;
    if (_stricmp(soundClass, "loop") == 0)
        return MISSING_CLASS_LOOP;
    if (_stricmp(soundClass, "speech") == 0)
        return MISSING_CLASS_SPEECH;
    if (_stricmp(soundClass, "dialog") == 0)
        return MISSING_CLASS_DIALOG;
    if (_stricmp(soundClass, "music") == 0)
        return MISSING_CLASS_MUSIC;
    return MISSING_CLASS_OTHER;
}

float ClampUnit(float value)
{
    if (!_finite(value) || value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

bool IsManagerReady(void *soundManager)
{
    if (soundManager == nullptr)
        return false;
    const uint8_t *manager = static_cast<const uint8_t *>(soundManager);
    return *reinterpret_cast<const BOOL *>(manager + SOUND_MANAGER_READY_OFFSET) != FALSE;
}

int GetMasterVolume(void *soundManager)
{
    if (soundManager == nullptr)
        return 0;
    const uint8_t *manager = static_cast<const uint8_t *>(soundManager);
    int volume = *reinterpret_cast<const int *>(
        manager + SOUND_MANAGER_FX_MASTER_VOLUME_OFFSET);
    if (volume < 0)
        return 0;
    if (volume > 100)
        return 100;
    return volume;
}

int GetSpeechMasterVolume(void *soundManager)
{
    if (soundManager == nullptr)
        return 0;
    const uint8_t *manager = static_cast<const uint8_t *>(soundManager);
    int volume = *reinterpret_cast<const int *>(
        manager + SOUND_MANAGER_SPEECH_MASTER_VOLUME_OFFSET);
    if (volume < 0)
        return 0;
    if (volume > 100)
        return 100;
    return volume;
}

float GetHeightFactor(void *soundManager, bool affectedWithHeight)
{
    if (!affectedWithHeight || soundManager == nullptr)
        return 1.0f;
    const float height = *reinterpret_cast<const float *>(
        static_cast<const uint8_t *>(soundManager) +
        SOUND_MANAGER_LISTENER_Z_OFFSET);
    if (!_finite(height) || height >= 70.0f)
        return 0.0f;
    if (height <= 30.0f)
        return 1.0f;
    return 1.0f - (height - 30.0f) / 40.0f;
}

float MoveTowards(float current, float target, float maximumDelta)
{
    if (current < target)
    {
        current += maximumDelta;
        return current < target ? current : target;
    }
    if (current > target)
    {
        current -= maximumDelta;
        return current > target ? current : target;
    }
    return target;
}

const char *MiniaudioReverbProfileName(int profile)
{
    return profile == 1 ? "cave" : "outdoor";
}

bool IsMiniaudioReverbEnabled()
{
    return ModAudioSettings().JMVMiniaudioFullBackendPrototype &&
        ModAudioSettings().JMVMiniaudioReverbPrototype;
}

bool IsMiniaudioReverbInterfaceSound(const char *soundName)
{
    return soundName != nullptr &&
        (_strnicmp(soundName, "buttons\\", 8) == 0 ||
         _strnicmp(soundName, "buttons/", 8) == 0);
}

float SanitizeMiniaudioReverbLevel(float value, float fallback)
{
    return _finite(value) && value >= 0.0f && value <= 1.0f
        ? value
        : fallback;
}

float SanitizeMiniaudioReverbReflection(float value, float fallback)
{
    return _finite(value) && value >= 0.005f && value <= 0.50f
        ? value
        : fallback;
}

float SanitizeMiniaudioReverbDecay(float value, float fallback)
{
    return _finite(value) && value >= 0.10f && value <= 10.0f
        ? value
        : fallback;
}

ma_uint32 MiniaudioReverbFramesFromSeconds(
    ma_uint32 sampleRate,
    float seconds)
{
    const double frameCount = static_cast<double>(sampleRate) * seconds;
    if (frameCount <= 1.0)
        return 1;
    if (frameCount >= static_cast<double>(0xFFFFFFFFu))
        return 0xFFFFFFFFu;
    return static_cast<ma_uint32>(frameCount + 0.5);
}

bool InitMiniaudioReverbDelayLine(
    SMiniaudioReverbDelayLine &line,
    ma_uint32 channels,
    ma_uint32 frameCount)
{
    memset(&line, 0, sizeof(line));
    if (channels == 0 || channels > MINIAUDIO_REVERB_MAX_CHANNELS ||
        frameCount == 0)
    {
        return false;
    }
    const size_t sampleCount =
        static_cast<size_t>(channels) * static_cast<size_t>(frameCount);
    if (sampleCount > static_cast<size_t>(-1) / sizeof(float))
        return false;
    line.frames = static_cast<float *>(
        ma_calloc(sampleCount * sizeof(float), nullptr));
    if (line.frames == nullptr)
        return false;
    line.frameCount = frameCount;
    return true;
}

void UninitMiniaudioReverbDelayLine(SMiniaudioReverbDelayLine &line)
{
    if (line.frames != nullptr)
        ma_free(line.frames, nullptr);
    memset(&line, 0, sizeof(line));
}

void AdvanceMiniaudioReverbDelayLine(SMiniaudioReverbDelayLine &line)
{
    ++line.cursor;
    if (line.cursor >= line.frameCount)
        line.cursor = 0;
}

void UninitMiniaudioReverbBank(SMiniaudioReverbBank &bank)
{
    UninitMiniaudioReverbDelayLine(bank.preDelay);
    for (unsigned int i = 0; i < MINIAUDIO_REVERB_COMB_COUNT; ++i)
        UninitMiniaudioReverbDelayLine(bank.combs[i]);
    for (unsigned int i = 0; i < MINIAUDIO_REVERB_ALLPASS_COUNT; ++i)
        UninitMiniaudioReverbDelayLine(bank.allpasses[i]);
    memset(&bank, 0, sizeof(bank));
}

bool InitMiniaudioReverbBank(
    SMiniaudioReverbBank &bank,
    ma_uint32 channels,
    ma_uint32 sampleRate,
    float level,
    float reflectionSeconds,
    float decaySeconds,
    float damping)
{
    static const float combDelaySeconds[MINIAUDIO_REVERB_COMB_COUNT] = {
        0.0297f, 0.0329f, 0.0371f, 0.0411f, 0.0437f, 0.0479f
    };
    static const float allpassDelaySeconds[
        MINIAUDIO_REVERB_ALLPASS_COUNT] = {
        0.0050f, 0.0017f, 0.0007f
    };

    memset(&bank, 0, sizeof(bank));
    bank.level = level;
    bank.reflectionSeconds = reflectionSeconds;
    bank.decaySeconds = decaySeconds;
    bank.damping = damping;

    if (!InitMiniaudioReverbDelayLine(
            bank.preDelay,
            channels,
            MiniaudioReverbFramesFromSeconds(
                sampleRate, reflectionSeconds)))
    {
        UninitMiniaudioReverbBank(bank);
        return false;
    }

    for (unsigned int i = 0; i < MINIAUDIO_REVERB_COMB_COUNT; ++i)
    {
        SMiniaudioReverbDelayLine &line = bank.combs[i];
        if (!InitMiniaudioReverbDelayLine(
                line,
                channels,
                MiniaudioReverbFramesFromSeconds(
                    sampleRate, combDelaySeconds[i])))
        {
            UninitMiniaudioReverbBank(bank);
            return false;
        }
        float feedback = powf(
            0.001f, combDelaySeconds[i] / decaySeconds);
        if (feedback > 0.97f)
            feedback = 0.97f;
        if (feedback < 0.0f)
            feedback = 0.0f;
        line.feedback = feedback;
        line.damping = damping;
    }

    for (unsigned int i = 0; i < MINIAUDIO_REVERB_ALLPASS_COUNT; ++i)
    {
        SMiniaudioReverbDelayLine &line = bank.allpasses[i];
        if (!InitMiniaudioReverbDelayLine(
                line,
                channels,
                MiniaudioReverbFramesFromSeconds(
                    sampleRate, allpassDelaySeconds[i])))
        {
            UninitMiniaudioReverbBank(bank);
            return false;
        }
        line.feedback = 0.50f;
    }
    return true;
}

void ProcessMiniaudioReverbBankFrame(
    SMiniaudioReverbBank &bank,
    ma_uint32 channels,
    const float *input,
    float *output)
{
    float preDelayed[MINIAUDIO_REVERB_MAX_CHANNELS] = {};
    float diffused[MINIAUDIO_REVERB_MAX_CHANNELS] = {};
    for (ma_uint32 channel = 0; channel < channels; ++channel)
    {
        const size_t index =
            static_cast<size_t>(bank.preDelay.cursor) * channels + channel;
        preDelayed[channel] = bank.preDelay.frames[index];
        bank.preDelay.frames[index] = input[channel];
    }
    AdvanceMiniaudioReverbDelayLine(bank.preDelay);

    float crossed[MINIAUDIO_REVERB_MAX_CHANNELS] = {};
    if (channels == 2)
    {
        crossed[0] = preDelayed[0] *
                (1.0f - MINIAUDIO_REVERB_STEREO_CROSSFEED) +
            preDelayed[1] * MINIAUDIO_REVERB_STEREO_CROSSFEED;
        crossed[1] = preDelayed[1] *
                (1.0f - MINIAUDIO_REVERB_STEREO_CROSSFEED) +
            preDelayed[0] * MINIAUDIO_REVERB_STEREO_CROSSFEED;
    }
    else
    {
        crossed[0] = preDelayed[0];
    }

    for (unsigned int comb = 0;
         comb < MINIAUDIO_REVERB_COMB_COUNT;
         ++comb)
    {
        SMiniaudioReverbDelayLine &line = bank.combs[comb];
        for (ma_uint32 channel = 0; channel < channels; ++channel)
        {
            const size_t index =
                static_cast<size_t>(line.cursor) * channels + channel;
            const float delayed = line.frames[index];
            float filtered = delayed * (1.0f - line.damping) +
                line.filterState[channel] * line.damping;
            if (fabsf(filtered) < 1.0e-20f)
                filtered = 0.0f;
            line.filterState[channel] = filtered;
            line.frames[index] = crossed[channel] +
                filtered * line.feedback;
            diffused[channel] += delayed;
        }
        AdvanceMiniaudioReverbDelayLine(line);
    }

    for (ma_uint32 channel = 0; channel < channels; ++channel)
        diffused[channel] /= static_cast<float>(MINIAUDIO_REVERB_COMB_COUNT);

    for (unsigned int allpass = 0;
         allpass < MINIAUDIO_REVERB_ALLPASS_COUNT;
         ++allpass)
    {
        SMiniaudioReverbDelayLine &line = bank.allpasses[allpass];
        for (ma_uint32 channel = 0; channel < channels; ++channel)
        {
            const size_t index =
                static_cast<size_t>(line.cursor) * channels + channel;
            const float delayed = line.frames[index];
            const float current = diffused[channel];
            diffused[channel] = delayed - current;
            line.frames[index] = current + delayed * line.feedback;
        }
        AdvanceMiniaudioReverbDelayLine(line);
    }

    for (ma_uint32 channel = 0; channel < channels; ++channel)
        output[channel] = diffused[channel] * bank.level;
}

void UpdateMiniaudioReverbAtomicMaximum(
    volatile LONG &destination,
    LONG value)
{
    LONG current = InterlockedCompareExchange(&destination, 0, 0);
    while (value > current)
    {
        const LONG observed = InterlockedCompareExchange(
            &destination, value, current);
        if (observed == current)
            break;
        current = observed;
    }
}

void ProcessMiniaudioReverbNode(
    ma_node *node,
    const float **inputFrames,
    ma_uint32 *inputFrameCount,
    float **outputFrames,
    ma_uint32 *outputFrameCount)
{
    SMiniaudioReverbNode *reverb =
        reinterpret_cast<SMiniaudioReverbNode *>(node);
    if (reverb == nullptr || outputFrames == nullptr ||
        outputFrames[0] == nullptr || outputFrameCount == nullptr)
    {
        return;
    }

    const ma_uint32 frames = *outputFrameCount;
    const float *input = inputFrames != nullptr ? inputFrames[0] : nullptr;
    float *output = outputFrames[0];
    const LONG targetProfile = InterlockedCompareExchange(
        &reverb->targetProfile, 0, 0) == 1 ? 1 : 0;
    const float targetBlend = targetProfile == 1 ? 1.0f : 0.0f;
    const float transitionFrames =
        static_cast<float>(reverb->sampleRate) *
        static_cast<float>(MINIAUDIO_REVERB_TRANSITION_MS) * 0.001f;
    const float blendStep = transitionFrames > 1.0f
        ? 1.0f / transitionFrames
        : 1.0f;
    float inputPeak = 0.0f;
    float outputPeak = 0.0f;

    for (ma_uint32 frame = 0; frame < frames; ++frame)
    {
        float source[MINIAUDIO_REVERB_MAX_CHANNELS] = {};
        float outdoor[MINIAUDIO_REVERB_MAX_CHANNELS] = {};
        float cave[MINIAUDIO_REVERB_MAX_CHANNELS] = {};
        for (ma_uint32 channel = 0;
             channel < reverb->channels;
             ++channel)
        {
            source[channel] = input != nullptr
                ? input[static_cast<size_t>(frame) * reverb->channels +
                        channel]
                : 0.0f;
            const float absolute = fabsf(source[channel]);
            if (absolute > inputPeak)
                inputPeak = absolute;
        }

        ProcessMiniaudioReverbBankFrame(
            reverb->banks[0], reverb->channels, source, outdoor);
        ProcessMiniaudioReverbBankFrame(
            reverb->banks[1], reverb->channels, source, cave);
        reverb->profileBlend = MoveTowards(
            reverb->profileBlend, targetBlend, blendStep);

        for (ma_uint32 channel = 0;
             channel < reverb->channels;
             ++channel)
        {
            const float sample = outdoor[channel] *
                    (1.0f - reverb->profileBlend) +
                cave[channel] * reverb->profileBlend;
            output[static_cast<size_t>(frame) * reverb->channels +
                   channel] = sample;
            const float absolute = fabsf(sample);
            if (absolute > outputPeak)
                outputPeak = absolute;
        }
    }

    if (inputFrameCount != nullptr)
        *inputFrameCount = frames;
    InterlockedIncrement(&reverb->processedBlocks);
    if (inputPeak > 1.0e-6f)
        InterlockedIncrement(&reverb->inputBlocks);
    if (outputPeak > 1.0e-6f)
    {
        InterlockedIncrement(&reverb->outputBlocks);
        if (inputPeak <= 1.0e-6f)
            InterlockedIncrement(&reverb->tailBlocks);
    }
    LONG inputPermille = static_cast<LONG>(inputPeak * 1000.0f + 0.5f);
    LONG outputPermille = static_cast<LONG>(outputPeak * 1000.0f + 0.5f);
    if (inputPermille > 100000)
        inputPermille = 100000;
    if (outputPermille > 100000)
        outputPermille = 100000;
    UpdateMiniaudioReverbAtomicMaximum(
        reverb->peakInputPermille, inputPermille);
    UpdateMiniaudioReverbAtomicMaximum(
        reverb->peakOutputPermille, outputPermille);

    if (fabsf(reverb->profileBlend - targetBlend) <= 0.0001f)
    {
        reverb->profileBlend = targetBlend;
        InterlockedExchange(
            &reverb->settledProfile, targetProfile);
    }
}

ma_node_vtable MiniaudioReverbNodeVtable = {
    ProcessMiniaudioReverbNode,
    nullptr,
    1,
    1,
    MA_NODE_FLAG_CONTINUOUS_PROCESSING
};

void UninitMiniaudioReverbNode(SMiniaudioReverbNode &node)
{
    ma_node_uninit(&node.baseNode, nullptr);
    for (unsigned int profile = 0;
         profile < MINIAUDIO_REVERB_PROFILE_COUNT;
         ++profile)
    {
        UninitMiniaudioReverbBank(node.banks[profile]);
    }
    memset(&node, 0, sizeof(node));
}

bool InitMiniaudioReverbNode(
    ma_uint32 channels,
    ma_uint32 sampleRate,
    SMiniaudioReverbNode &node)
{
    memset(&node, 0, sizeof(node));
    node.channels = channels;
    node.sampleRate = sampleRate;

    const float outdoorLevel = SanitizeMiniaudioReverbLevel(
        ModAudioSettings().JMVSfxReverbOutdoorLevel, 0.06f);
    const float outdoorReflection = SanitizeMiniaudioReverbReflection(
        ModAudioSettings().JMVSfxReverbOutdoorReflection, 0.08f);
    const float outdoorDecay = SanitizeMiniaudioReverbDecay(
        ModAudioSettings().JMVSfxReverbOutdoorDecay, 0.55f);
    const float caveLevel = SanitizeMiniaudioReverbLevel(
        ModAudioSettings().JMVSfxReverbCaveLevel, 0.22f);
    const float caveReflection = SanitizeMiniaudioReverbReflection(
        ModAudioSettings().JMVSfxReverbCaveReflection, 0.12f);
    const float caveDecay = SanitizeMiniaudioReverbDecay(
        ModAudioSettings().JMVSfxReverbCaveDecay, 1.80f);

    if (!InitMiniaudioReverbBank(
            node.banks[0],
            channels,
            sampleRate,
            outdoorLevel,
            outdoorReflection,
            outdoorDecay,
            MINIAUDIO_REVERB_OUTDOOR_DAMPING) ||
        !InitMiniaudioReverbBank(
            node.banks[1],
            channels,
            sampleRate,
            caveLevel,
            caveReflection,
            caveDecay,
            MINIAUDIO_REVERB_CAVE_DAMPING))
    {
        for (unsigned int profile = 0;
             profile < MINIAUDIO_REVERB_PROFILE_COUNT;
             ++profile)
        {
            UninitMiniaudioReverbBank(node.banks[profile]);
        }
        memset(&node, 0, sizeof(node));
        return false;
    }

    ma_node_config config = ma_node_config_init();
    config.vtable = &MiniaudioReverbNodeVtable;
    config.pInputChannels = &node.channels;
    config.pOutputChannels = &node.channels;
    const ma_result result = ma_node_init(
        ma_engine_get_node_graph(&Engine),
        &config,
        nullptr,
        &node.baseNode);
    if (result != MA_SUCCESS)
    {
        for (unsigned int profile = 0;
             profile < MINIAUDIO_REVERB_PROFILE_COUNT;
             ++profile)
        {
            UninitMiniaudioReverbBank(node.banks[profile]);
        }
        memset(&node, 0, sizeof(node));
        return false;
    }
    return true;
}

void LogMiniaudioReverbError(
    const char *phase,
    ma_result result)
{
#if JMV_MINIAUDIO_REVERB_DIAGNOSTICS
    if (ShouldLogLimited(ReverbErrorDiagnosticCount))
    {
        LOGERR("MINIAUDIO_REVERB_ERROR phase=%s result=%d description=%s route=unprocessed_miniaudio total=%u",
               phase != nullptr ? phase : "unknown",
               result,
               ma_result_description(result),
               ReverbErrorDiagnosticCount);
    }
#else
    (void)phase;
    (void)result;
#endif
}

void DestroyMiniaudioReverbGraph(
    bool allowRetry,
    const char *reason)
{
    const bool wasInitialized = ReverbGraphInitialized;
    if (ReverbSourceGroupInitialized)
        ma_sound_group_uninit(&ReverbSourceGroup);
    if (ReverbSplitterInitialized)
        ma_splitter_node_uninit(&ReverbSplitter, nullptr);
    if (ReverbNodeInitialized)
        UninitMiniaudioReverbNode(ReverbNode);
    memset(&ReverbSourceGroup, 0, sizeof(ReverbSourceGroup));
    memset(&ReverbSplitter, 0, sizeof(ReverbSplitter));
    if (!ReverbNodeInitialized)
        memset(&ReverbNode, 0, sizeof(ReverbNode));
    ReverbNodeInitialized = false;
    ReverbSplitterInitialized = false;
    ReverbSourceGroupInitialized = false;
    ReverbGraphInitialized = false;
    if (allowRetry)
        ReverbGraphAttempted = false;
    ReverbLastRequestedProfile = -1;
    ReverbLastSettledProfile = -1;
    ReverbLastDiagnosticBlocks = 0;
#if JMV_MINIAUDIO_REVERB_DIAGNOSTICS
    if (wasInitialized)
    {
        LOGINFO("MINIAUDIO_REVERB_RESET reason=%s retry=%d",
                reason != nullptr ? reason : "reset",
                allowRetry ? 1 : 0);
    }
#else
    (void)reason;
#endif
}

int DetectMiniaudioReverbProfile()
{
    return (&theWorld) != nullptr &&
            (&theWorld)->IsCaveZone()
        ? 1
        : 0;
}

bool EnsureMiniaudioReverbGraph()
{
    if (!IsMiniaudioReverbEnabled() || !EngineInitialized)
        return false;
    if (ReverbGraphInitialized)
        return true;
    if (ReverbGraphAttempted)
        return false;
    ReverbGraphAttempted = true;

    const ma_uint32 channels = ma_engine_get_channels(&Engine);
    const ma_uint32 sampleRate = ma_engine_get_sample_rate(&Engine);
    if (channels == 0 || channels > MINIAUDIO_REVERB_MAX_CHANNELS ||
        sampleRate == 0)
    {
        LogMiniaudioReverbError("format", MA_INVALID_ARGS);
        return false;
    }

    if (!InitMiniaudioReverbNode(
            channels, sampleRate, ReverbNode))
    {
        LogMiniaudioReverbError("node_init", MA_OUT_OF_MEMORY);
        DestroyMiniaudioReverbGraph(false, "node_init_failed");
        return false;
    }
    ReverbNodeInitialized = true;

    ma_splitter_node_config splitterConfig =
        ma_splitter_node_config_init(channels);
    ma_result result = ma_splitter_node_init(
        ma_engine_get_node_graph(&Engine),
        &splitterConfig,
        nullptr,
        &ReverbSplitter);
    if (result != MA_SUCCESS)
    {
        LogMiniaudioReverbError("splitter_init", result);
        DestroyMiniaudioReverbGraph(false, "splitter_init_failed");
        return false;
    }
    ReverbSplitterInitialized = true;

    result = ma_sound_group_init(
        &Engine,
        MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT,
        nullptr,
        &ReverbSourceGroup);
    if (result != MA_SUCCESS)
    {
        LogMiniaudioReverbError("source_group_init", result);
        DestroyMiniaudioReverbGraph(false, "source_group_init_failed");
        return false;
    }
    ReverbSourceGroupInitialized = true;

    result = ma_node_set_output_bus_volume(
        &ReverbSplitter, 0, 1.0f);
    if (result == MA_SUCCESS)
        result = ma_node_set_output_bus_volume(&ReverbSplitter, 1, 1.0f);
    if (result == MA_SUCCESS)
    {
        result = ma_node_attach_output_bus(
            &ReverbSplitter,
            0,
            ma_engine_get_endpoint(&Engine),
            0);
    }
    if (result == MA_SUCCESS)
    {
        result = ma_node_attach_output_bus(
            &ReverbSplitter,
            1,
            &ReverbNode.baseNode,
            0);
    }
    if (result == MA_SUCCESS)
    {
        result = ma_node_attach_output_bus(
            &ReverbNode.baseNode,
            0,
            ma_engine_get_endpoint(&Engine),
            0);
    }
    if (result == MA_SUCCESS)
    {
        result = ma_node_attach_output_bus(
            &ReverbSourceGroup,
            0,
            &ReverbSplitter,
            0);
    }
    if (result != MA_SUCCESS)
    {
        LogMiniaudioReverbError("graph_attach", result);
        DestroyMiniaudioReverbGraph(false, "graph_attach_failed");
        return false;
    }

    const int profile = DetectMiniaudioReverbProfile();
    ReverbNode.profileBlend = profile == 1 ? 1.0f : 0.0f;
    InterlockedExchange(&ReverbNode.targetProfile, profile);
    InterlockedExchange(&ReverbNode.settledProfile, profile);
    ReverbLastRequestedProfile = profile;
    ReverbLastSettledProfile = profile;
    ReverbGraphInitialized = true;
#if JMV_MINIAUDIO_REVERB_DIAGNOSTICS
    LOGINFO("MINIAUDIO_REVERB_GRAPH_READY channels=%u rate=%u profile=%s transition_ms=%lu combs=%u allpasses=%u outdoor=%.3f,%.3f,%.3f,%.3f cave=%.3f,%.3f,%.3f,%.3f dry=1.000 fail_open=unprocessed_miniaudio",
            channels,
            sampleRate,
            MiniaudioReverbProfileName(profile),
            static_cast<unsigned long>(MINIAUDIO_REVERB_TRANSITION_MS),
            MINIAUDIO_REVERB_COMB_COUNT,
            MINIAUDIO_REVERB_ALLPASS_COUNT,
            ReverbNode.banks[0].level,
            ReverbNode.banks[0].reflectionSeconds,
            ReverbNode.banks[0].decaySeconds,
            ReverbNode.banks[0].damping,
            ReverbNode.banks[1].level,
            ReverbNode.banks[1].reflectionSeconds,
            ReverbNode.banks[1].decaySeconds,
            ReverbNode.banks[1].damping);
#endif
    return true;
}

bool ShouldRouteMiniaudioReverb(
    const char *soundName,
    bool speech,
    bool dialog)
{
    if (!IsMiniaudioReverbEnabled())
        return false;
    if (speech)
    {
#if JMV_MINIAUDIO_REVERB_DIAGNOSTICS
        if (!ReverbSpeechBypassLogged)
        {
            LOGINFO("MINIAUDIO_REVERB_BYPASS kind=speech route=direct");
            ReverbSpeechBypassLogged = true;
        }
#endif
        return false;
    }
    if (dialog)
    {
#if JMV_MINIAUDIO_REVERB_DIAGNOSTICS
        if (!ReverbDialogBypassLogged)
        {
            LOGINFO("MINIAUDIO_REVERB_BYPASS kind=dialog route=direct");
            ReverbDialogBypassLogged = true;
        }
#endif
        return false;
    }
    if (IsMiniaudioReverbInterfaceSound(soundName))
    {
#if JMV_MINIAUDIO_REVERB_DIAGNOSTICS
        if (!ReverbInterfaceBypassLogged)
        {
            LOGINFO("MINIAUDIO_REVERB_BYPASS kind=interface name=%s route=direct",
                    soundName != nullptr ? soundName : "<null>");
            ReverbInterfaceBypassLogged = true;
        }
#endif
        return false;
    }
    return soundName != nullptr && EnsureMiniaudioReverbGraph();
}

ma_node *GetMiniaudioVoiceDestination(const SVoice &voice)
{
    return voice.reverbRouted && ReverbGraphInitialized
        ? reinterpret_cast<ma_node *>(&ReverbSourceGroup)
        : ma_engine_get_endpoint(&Engine);
}

void UpdateMiniaudioReverbProfileAndDiagnostics()
{
    if (!ReverbGraphInitialized)
        return;
    const int profile = DetectMiniaudioReverbProfile();
    if (profile != ReverbLastRequestedProfile)
    {
        InterlockedExchange(&ReverbNode.targetProfile, profile);
        ReverbLastRequestedProfile = profile;
#if JMV_MINIAUDIO_REVERB_DIAGNOSTICS
        LOGINFO("MINIAUDIO_REVERB_PROFILE profile=%s transition_ms=%lu selector=WorldClient.IsCaveZone",
                MiniaudioReverbProfileName(profile),
                static_cast<unsigned long>(MINIAUDIO_REVERB_TRANSITION_MS));
#endif
    }

    const int settled = static_cast<int>(InterlockedCompareExchange(
        &ReverbNode.settledProfile, 0, 0));
    if (settled != ReverbLastSettledProfile)
    {
        ReverbLastSettledProfile = settled;
#if JMV_MINIAUDIO_REVERB_DIAGNOSTICS
        LOGINFO("MINIAUDIO_REVERB_TRANSITION_COMPLETE profile=%s",
                MiniaudioReverbProfileName(settled));
#endif
    }

#if JMV_MINIAUDIO_REVERB_DIAGNOSTICS
    const LONG blocks = InterlockedCompareExchange(
        &ReverbNode.processedBlocks, 0, 0);
    if ((ReverbLastDiagnosticBlocks == 0 && blocks >= 64) ||
        blocks - ReverbLastDiagnosticBlocks >= 2048)
    {
        ReverbLastDiagnosticBlocks = blocks;
        LOGINFO("MINIAUDIO_REVERB_PROCESS blocks=%ld input_blocks=%ld output_blocks=%ld tail_blocks=%ld peak_input_permille=%ld peak_output_permille=%ld target=%s settled=%s routes=%u",
                blocks,
                InterlockedCompareExchange(&ReverbNode.inputBlocks, 0, 0),
                InterlockedCompareExchange(&ReverbNode.outputBlocks, 0, 0),
                InterlockedCompareExchange(&ReverbNode.tailBlocks, 0, 0),
                InterlockedCompareExchange(
                    &ReverbNode.peakInputPermille, 0, 0),
                InterlockedCompareExchange(
                    &ReverbNode.peakOutputPermille, 0, 0),
                MiniaudioReverbProfileName(profile),
                MiniaudioReverbProfileName(settled),
                ReverbRouteDiagnosticCount);
    }
#endif
}

float ClampBinauralSignedUnit(float value)
{
    if (!_finite(value))
        return 0.0f;
    if (value < -1.0f)
        return -1.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

float MiniaudioBinauralOnePoleCoefficient(
    float cutoffHz,
    ma_uint32 sampleRate)
{
    if (!_finite(cutoffHz) || cutoffHz <= 0.0f || sampleRate == 0)
        return 1.0f;
    const float nyquist = static_cast<float>(sampleRate) * 0.5f;
    if (cutoffHz >= nyquist)
        return 1.0f;
    return 1.0f - expf(
        -6.28318530717958647692f * cutoffHz /
        static_cast<float>(sampleRate));
}

float ReadMiniaudioBinauralDelay(
    const SMiniaudioBinauralNode &node,
    ma_uint32 channel,
    float delayFrames)
{
    if (channel >= MINIAUDIO_BINAURAL_CHANNELS)
        return 0.0f;
    if (!_finite(delayFrames) || delayFrames < 0.0f)
        delayFrames = 0.0f;
    const float maximumDelay = static_cast<float>(
        MINIAUDIO_BINAURAL_DELAY_BUFFER_FRAMES - 2);
    if (delayFrames > maximumDelay)
        delayFrames = maximumDelay;

    const ma_uint32 wholeFrames = static_cast<ma_uint32>(delayFrames);
    const float fraction = delayFrames - static_cast<float>(wholeFrames);
    const ma_uint32 newerIndex =
        (node.cursor + MINIAUDIO_BINAURAL_DELAY_BUFFER_FRAMES -
         wholeFrames) %
        MINIAUDIO_BINAURAL_DELAY_BUFFER_FRAMES;
    const ma_uint32 olderIndex =
        (newerIndex + MINIAUDIO_BINAURAL_DELAY_BUFFER_FRAMES - 1) %
        MINIAUDIO_BINAURAL_DELAY_BUFFER_FRAMES;
    return node.delayLines[channel][newerIndex] * (1.0f - fraction) +
        node.delayLines[channel][olderIndex] * fraction;
}

void ProcessMiniaudioBinauralNode(
    ma_node *node,
    const float **inputFrames,
    ma_uint32 *inputFrameCount,
    float **outputFrames,
    ma_uint32 *outputFrameCount)
{
    SMiniaudioBinauralNode *binaural =
        reinterpret_cast<SMiniaudioBinauralNode *>(node);
    if (binaural == nullptr || outputFrames == nullptr ||
        outputFrames[0] == nullptr || outputFrameCount == nullptr)
    {
        return;
    }

    ma_uint32 frames = *outputFrameCount;
    const float *input = inputFrames != nullptr ? inputFrames[0] : nullptr;
    if (input != nullptr && inputFrameCount != nullptr &&
        *inputFrameCount < frames)
    {
        frames = *inputFrameCount;
    }
    float *output = outputFrames[0];
    const bool enabled = InterlockedCompareExchange(
        &binaural->enabled, 0, 0) != 0;
    if (!enabled)
    {
        for (ma_uint32 frame = 0; frame < frames; ++frame)
        {
            const size_t offset =
                static_cast<size_t>(frame) * MINIAUDIO_BINAURAL_CHANNELS;
            output[offset] = input != nullptr ? input[offset] : 0.0f;
            output[offset + 1] = input != nullptr ? input[offset + 1] : 0.0f;
        }
        if (inputFrameCount != nullptr)
            *inputFrameCount = frames;
        *outputFrameCount = frames;
        binaural->audioEnabled = false;
        return;
    }

    const float side = ClampBinauralSignedUnit(
        static_cast<float>(InterlockedCompareExchange(
            &binaural->targetSidePermille, 0, 0)) *
        0.001f);
    const float rear = ClampUnit(
        static_cast<float>(InterlockedCompareExchange(
            &binaural->targetRearPermille, 0, 0)) *
        0.001f);
    const float absoluteSide = fabsf(side);
    float maximumDelayFrames =
        static_cast<float>(binaural->sampleRate) *
        MINIAUDIO_BINAURAL_MAX_ITD_SECONDS;
    const float delayLimit = static_cast<float>(
        MINIAUDIO_BINAURAL_DELAY_BUFFER_FRAMES - 2);
    if (maximumDelayFrames > delayLimit)
        maximumDelayFrames = delayLimit;

    float targetDelay[MINIAUDIO_BINAURAL_CHANNELS] = {};
    float targetShadow[MINIAUDIO_BINAURAL_CHANNELS] = {};
    float targetFarGain[MINIAUDIO_BINAURAL_CHANNELS] = {1.0f, 1.0f};
    if (side > 0.0f)
    {
        targetDelay[0] = maximumDelayFrames * side;
        targetShadow[0] = 0.72f * side;
        targetFarGain[0] = 1.0f - 0.22f * side;
    }
    else if (side < 0.0f)
    {
        const float leftAmount = -side;
        targetDelay[1] = maximumDelayFrames * leftAmount;
        targetShadow[1] = 0.72f * leftAmount;
        targetFarGain[1] = 1.0f - 0.22f * leftAmount;
    }
    const float targetRearMix = 0.42f * rear;
    const float targetRearGain = 1.0f - 0.08f * rear;
    const float farCutoff =
        MINIAUDIO_BINAURAL_FAR_CUTOFF_MAX_HZ -
        (MINIAUDIO_BINAURAL_FAR_CUTOFF_MAX_HZ -
         MINIAUDIO_BINAURAL_FAR_CUTOFF_MIN_HZ) *
            absoluteSide;
    const float farCoefficient = MiniaudioBinauralOnePoleCoefficient(
        farCutoff, binaural->sampleRate);
    const float rearCoefficient = MiniaudioBinauralOnePoleCoefficient(
        MINIAUDIO_BINAURAL_REAR_CUTOFF_HZ, binaural->sampleRate);
    const float smoothingCoefficient = binaural->sampleRate != 0
        ? 1.0f - expf(
              -1.0f /
              (static_cast<float>(binaural->sampleRate) *
               MINIAUDIO_BINAURAL_SMOOTH_SECONDS))
        : 1.0f;

    if (!binaural->audioEnabled)
    {
        memset(
            binaural->delayLines, 0, sizeof(binaural->delayLines));
        memset(
            binaural->farLowPassState,
            0,
            sizeof(binaural->farLowPassState));
        memset(
            binaural->rearLowPassState,
            0,
            sizeof(binaural->rearLowPassState));
        binaural->cursor = 0;
        for (ma_uint32 channel = 0;
             channel < MINIAUDIO_BINAURAL_CHANNELS;
             ++channel)
        {
            binaural->currentDelayFrames[channel] = targetDelay[channel];
            binaural->currentShadowMix[channel] = targetShadow[channel];
            binaural->currentFarGain[channel] = targetFarGain[channel];
        }
        binaural->currentRearMix = targetRearMix;
        binaural->currentRearGain = targetRearGain;
        binaural->audioEnabled = true;
    }

    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    for (ma_uint32 frame = 0; frame < frames; ++frame)
    {
        float inputSample[MINIAUDIO_BINAURAL_CHANNELS] = {};
        const size_t offset =
            static_cast<size_t>(frame) * MINIAUDIO_BINAURAL_CHANNELS;
        for (ma_uint32 channel = 0;
             channel < MINIAUDIO_BINAURAL_CHANNELS;
             ++channel)
        {
            inputSample[channel] =
                input != nullptr ? input[offset + channel] : 0.0f;
            binaural->delayLines[channel][binaural->cursor] =
                inputSample[channel];
            const float absolute = fabsf(inputSample[channel]);
            if (absolute > inputPeak)
                inputPeak = absolute;
            binaural->currentDelayFrames[channel] +=
                (targetDelay[channel] -
                 binaural->currentDelayFrames[channel]) *
                smoothingCoefficient;
            binaural->currentShadowMix[channel] +=
                (targetShadow[channel] -
                 binaural->currentShadowMix[channel]) *
                smoothingCoefficient;
            binaural->currentFarGain[channel] +=
                (targetFarGain[channel] -
                 binaural->currentFarGain[channel]) *
                smoothingCoefficient;
        }
        binaural->currentRearMix +=
            (targetRearMix - binaural->currentRearMix) *
            smoothingCoefficient;
        binaural->currentRearGain +=
            (targetRearGain - binaural->currentRearGain) *
            smoothingCoefficient;

        for (ma_uint32 channel = 0;
             channel < MINIAUDIO_BINAURAL_CHANNELS;
             ++channel)
        {
            const float delayed = ReadMiniaudioBinauralDelay(
                *binaural,
                channel,
                binaural->currentDelayFrames[channel]);
            binaural->farLowPassState[channel] +=
                farCoefficient *
                (delayed - binaural->farLowPassState[channel]);
            const float shadowed =
                (delayed * (1.0f - binaural->currentShadowMix[channel]) +
                 binaural->farLowPassState[channel] *
                     binaural->currentShadowMix[channel]) *
                binaural->currentFarGain[channel];
            binaural->rearLowPassState[channel] +=
                rearCoefficient *
                (shadowed - binaural->rearLowPassState[channel]);
            const float sample =
                (shadowed * (1.0f - binaural->currentRearMix) +
                 binaural->rearLowPassState[channel] *
                     binaural->currentRearMix) *
                binaural->currentRearGain;
            output[offset + channel] = sample;
            const float absolute = fabsf(sample);
            if (absolute > outputPeak)
                outputPeak = absolute;
        }
        binaural->cursor =
            (binaural->cursor + 1) %
            MINIAUDIO_BINAURAL_DELAY_BUFFER_FRAMES;
    }

    if (inputFrameCount != nullptr)
        *inputFrameCount = frames;
    *outputFrameCount = frames;
    InterlockedIncrement(&binaural->processedBlocks);
    if (inputPeak > 1.0e-6f)
        InterlockedIncrement(&binaural->inputBlocks);
    if (outputPeak > 1.0e-6f)
        InterlockedIncrement(&binaural->outputBlocks);
    LONG inputPermille = static_cast<LONG>(inputPeak * 1000.0f + 0.5f);
    LONG outputPermille = static_cast<LONG>(outputPeak * 1000.0f + 0.5f);
    if (inputPermille > 100000)
        inputPermille = 100000;
    if (outputPermille > 100000)
        outputPermille = 100000;
    UpdateMiniaudioReverbAtomicMaximum(
        binaural->peakInputPermille, inputPermille);
    UpdateMiniaudioReverbAtomicMaximum(
        binaural->peakOutputPermille, outputPermille);
}

ma_node_vtable MiniaudioBinauralNodeVtable = {
    ProcessMiniaudioBinauralNode,
    nullptr,
    1,
    1,
    0
};

void LogMiniaudioBinauralError(
    const SVoice &voice,
    const char *phase,
    ma_result result,
    ma_result restoreResult)
{
#if JMV_MINIAUDIO_BINAURAL_DIAGNOSTICS
    if (ShouldLogLimited(BinauralFilterErrorDiagnosticCount))
    {
        LOGERR("MINIAUDIO_BINAURAL_ERROR name=%s id=%lu phase=%s result=%d description=%s restore_result=%d route=stock_miniaudio_spatializer mode=approximate_not_measured_hrtf total=%u",
               voice.name[0] != '\0' ? voice.name : "<pending>",
               static_cast<unsigned long>(voice.virtualId),
               phase != nullptr ? phase : "unknown",
               result,
               ma_result_description(result),
               restoreResult,
               BinauralFilterErrorDiagnosticCount);
    }
#else
    (void)voice;
    (void)phase;
    (void)result;
    (void)restoreResult;
#endif
}

void UninitMiniaudioBinauralNode(SVoice &voice)
{
    if (voice.binauralNodeInitialized)
        ma_node_uninit(&voice.binauralNode.baseNode, nullptr);
    memset(&voice.binauralNode, 0, sizeof(voice.binauralNode));
    voice.binauralNodeInitialized = false;
    voice.binauralGraphInitialized = false;
}

// A stopped sound still feeds silence to attached processing nodes. Freeze only
// its private graph; the shared reverb must keep rendering its existing tail.
// Node state changes are atomic. Do not reset DSP history from the game thread.
void SetVoiceProcessingState(SVoice &voice, ma_node_state state)
{
    if (voice.binauralNodeInitialized)
        ma_node_set_state(&voice.binauralNode.baseNode, state);
    if (voice.occlusionSplitterInitialized)
        ma_node_set_state(&voice.occlusionSplitter, state);
    if (voice.occlusionLowPassInitialized)
        ma_node_set_state(&voice.occlusionLowPass, state);
}

ma_result StopVoiceSound(SVoice &voice)
{
    const ma_result result = ma_sound_stop(&voice.sound);
    if (result == MA_SUCCESS)
        SetVoiceProcessingState(voice, ma_node_state_stopped);
    return result;
}

ma_result StartVoiceSound(SVoice &voice)
{
    const ma_result result = ma_sound_start(&voice.sound);
    if (result == MA_SUCCESS)
        SetVoiceProcessingState(voice, ma_node_state_started);
    return result;
}

ma_result InitMiniaudioBinauralNode(
    ma_uint32 sampleRate,
    SMiniaudioBinauralNode &node)
{
    memset(&node, 0, sizeof(node));
    node.sampleRate = sampleRate;
    node.currentFarGain[0] = 1.0f;
    node.currentFarGain[1] = 1.0f;
    node.currentRearGain = 1.0f;
    ma_uint32 channels = MINIAUDIO_BINAURAL_CHANNELS;
    ma_node_config config = ma_node_config_init();
    config.vtable = &MiniaudioBinauralNodeVtable;
    config.pInputChannels = &channels;
    config.pOutputChannels = &channels;
    return ma_node_init(
        ma_engine_get_node_graph(&Engine),
        &config,
        nullptr,
        &node.baseNode);
}

bool IsVirtualBinauralEligible(const SVoice &voice)
{
    return ModAudioSettings().JMVMiniaudioFullBackendPrototype &&
        ModAudioSettings().JMVMiniaudioBinauralPrototype &&
        voice.virtualManaged && voice.spatial &&
        !voice.speech && !voice.dialog;
}

ma_node *GetMiniaudioVoiceBinauralDestination(SVoice &voice)
{
    return voice.occlusionGraphInitialized
        ? reinterpret_cast<ma_node *>(&voice.occlusionSplitter)
        : GetMiniaudioVoiceDestination(voice);
}

ma_node *GetMiniaudioVoiceProcessingSource(SVoice &voice)
{
    return voice.binauralGraphInitialized
        ? &voice.binauralNode.baseNode
        : reinterpret_cast<ma_node *>(&voice.sound);
}

bool EnsureMiniaudioBinauralGraph(SVoice &voice)
{
    if (voice.binauralGraphInitialized)
        return true;
    if (voice.binauralFilterAttempted || !voice.soundInitialized ||
        !IsVirtualBinauralEligible(voice))
    {
        return false;
    }
    voice.binauralFilterAttempted = true;

    const ma_uint32 channels = ma_node_get_output_channels(&voice.sound, 0);
    const ma_uint32 sampleRate = ma_engine_get_sample_rate(&Engine);
    if (channels != MINIAUDIO_BINAURAL_CHANNELS || sampleRate == 0)
    {
        LogMiniaudioBinauralError(
            voice, "format_stereo_required", MA_INVALID_ARGS, MA_SUCCESS);
        return false;
    }

    ma_result result = InitMiniaudioBinauralNode(
        sampleRate, voice.binauralNode);
    if (result != MA_SUCCESS)
    {
        LogMiniaudioBinauralError(
            voice, "node_init", result, MA_SUCCESS);
        return false;
    }
    voice.binauralNodeInitialized = true;
    SetVoiceProcessingState(voice, ma_node_get_state(&voice.sound));

    ma_node *destination = GetMiniaudioVoiceBinauralDestination(voice);
    result = ma_node_attach_output_bus(
        &voice.binauralNode.baseNode, 0, destination, 0);
    if (result == MA_SUCCESS)
    {
        result = ma_node_attach_output_bus(
            &voice.sound, 0, &voice.binauralNode.baseNode, 0);
    }
    if (result != MA_SUCCESS)
    {
        const ma_result restoreResult = ma_node_attach_output_bus(
            &voice.sound, 0, destination, 0);
        LogMiniaudioBinauralError(
            voice, "graph_attach", result, restoreResult);
        UninitMiniaudioBinauralNode(voice);
        return false;
    }

    voice.binauralGraphInitialized = true;
#if JMV_MINIAUDIO_BINAURAL_DIAGNOSTICS
    if (ShouldLogLimited(BinauralFilterReadyDiagnosticCount))
    {
        LOGINFO("MINIAUDIO_BINAURAL_FILTER_READY name=%s id=%lu channels=%u rate=%u max_itd_us=650 far_cutoff_hz=2500..12000 rear_cutoff_hz=5000 smoothing_ms=25 route=%s mode=approximate_not_measured_hrtf fail_open=stock_miniaudio_spatializer total=%u",
                voice.name,
                static_cast<unsigned long>(voice.virtualId),
                channels,
                sampleRate,
                voice.occlusionGraphInitialized
                    ? "binaural_to_occlusion"
                    : "binaural_to_destination",
                BinauralFilterReadyDiagnosticCount);
    }
#endif
    return true;
}

bool CalculateMiniaudioBinauralTargets(
    const SVoice &voice,
    float &side,
    float &rear,
    float &elevation)
{
    side = 0.0f;
    rear = 0.0f;
    elevation = 0.0f;
    if (voice.virtualSoundManager == nullptr)
        return false;
    const uint8_t *manager = static_cast<const uint8_t *>(
        voice.virtualSoundManager);
    const float listenerX = *reinterpret_cast<const float *>(
        manager + SOUND_MANAGER_LISTENER_X_OFFSET);
    const float listenerY = *reinterpret_cast<const float *>(
        manager + SOUND_MANAGER_LISTENER_Y_OFFSET);
    const float listenerZ = *reinterpret_cast<const float *>(
        manager + SOUND_MANAGER_LISTENER_Z_OFFSET);
    float forwardX = *reinterpret_cast<const float *>(
        manager + SOUND_MANAGER_ORIENTATION_X_OFFSET);
    float forwardY = *reinterpret_cast<const float *>(
        manager + SOUND_MANAGER_ORIENTATION_Y_OFFSET);
    if (!_finite(listenerX) || !_finite(listenerY) || !_finite(listenerZ) ||
        !_finite(forwardX) || !_finite(forwardY))
    {
        return false;
    }

    const float forwardLength = sqrtf(
        forwardX * forwardX + forwardY * forwardY);
    if (forwardLength < 0.001f)
    {
        forwardX = 0.0f;
        forwardY = 1.0f;
    }
    else
    {
        forwardX /= forwardLength;
        forwardY /= forwardLength;
    }
    const float rightX = forwardY;
    const float rightY = -forwardX;
    const float relativeX = voice.virtualPosition[0] - listenerX;
    const float relativeY = voice.virtualPosition[1] - listenerY;
    const float relativeZ = voice.virtualPosition[2] - listenerZ;
    const float horizontalLength = sqrtf(
        relativeX * relativeX + relativeY * relativeY);
    const float totalLength = sqrtf(
        horizontalLength * horizontalLength + relativeZ * relativeZ);
    if (!_finite(horizontalLength) || !_finite(totalLength))
        return false;
    if (horizontalLength >= 0.001f)
    {
        const float horizontalX = relativeX / horizontalLength;
        const float horizontalY = relativeY / horizontalLength;
        side = ClampBinauralSignedUnit(
            horizontalX * rightX + horizontalY * rightY);
        const float front = ClampBinauralSignedUnit(
            horizontalX * forwardX + horizontalY * forwardY);
        rear = front < 0.0f ? -front : 0.0f;
    }
    if (totalLength >= 0.001f)
        elevation = ClampBinauralSignedUnit(relativeZ / totalLength);
    return true;
}

int MiniaudioBinauralSector(float side, float rear)
{
    if (rear >= 0.55f && fabsf(side) < 0.75f)
        return 2;
    if (side >= 0.35f)
        return 1;
    if (side <= -0.35f)
        return 3;
    return 0;
}

const char *MiniaudioBinauralSectorName(int sector)
{
    switch (sector)
    {
    case 1:
        return "right";
    case 2:
        return "rear";
    case 3:
        return "left";
    default:
        return "front";
    }
}

void UpdateVirtualVoiceBinaural(SVoice &voice)
{
    if (!IsVirtualBinauralEligible(voice))
    {
        if (voice.binauralNodeInitialized)
            InterlockedExchange(&voice.binauralNode.enabled, 0);
#if JMV_MINIAUDIO_BINAURAL_DIAGNOSTICS
        if (ModAudioSettings().JMVMiniaudioFullBackendPrototype &&
            ModAudioSettings().JMVMiniaudioBinauralPrototype)
        {
            if (voice.speech && !BinauralSpeechBypassLogged)
            {
                LOGINFO("MINIAUDIO_BINAURAL_BYPASS kind=speech route=stock_miniaudio_nonspatial");
                BinauralSpeechBypassLogged = true;
            }
            else if (voice.dialog && !BinauralDialogBypassLogged)
            {
                LOGINFO("MINIAUDIO_BINAURAL_BYPASS kind=dialog route=stock_miniaudio_nonspatial");
                BinauralDialogBypassLogged = true;
            }
            else if (!voice.spatial && !BinauralFlatBypassLogged)
            {
                LOGINFO("MINIAUDIO_BINAURAL_BYPASS kind=flat route=stock_miniaudio_pan");
                BinauralFlatBypassLogged = true;
            }
        }
#endif
        return;
    }
    if (!EnsureMiniaudioBinauralGraph(voice))
        return;

    float side = 0.0f;
    float rear = 0.0f;
    float elevation = 0.0f;
    if (!CalculateMiniaudioBinauralTargets(
            voice, side, rear, elevation))
    {
        InterlockedExchange(&voice.binauralNode.enabled, 0);
        LogMiniaudioBinauralError(
            voice, "listener_transform", MA_INVALID_ARGS, MA_SUCCESS);
        return;
    }

    InterlockedExchange(
        &voice.binauralNode.targetSidePermille,
        static_cast<LONG>(side * 1000.0f));
    InterlockedExchange(
        &voice.binauralNode.targetRearPermille,
        static_cast<LONG>(rear * 1000.0f));
    InterlockedExchange(&voice.binauralNode.enabled, 1);

#if JMV_MINIAUDIO_BINAURAL_DIAGNOSTICS
    const int sector = MiniaudioBinauralSector(side, rear);
    if ((!voice.binauralApplyLogged ||
         sector != voice.binauralLastSector) &&
        ShouldLogLimited(BinauralApplyDiagnosticCount))
    {
        const int leftDelayUs = side > 0.0f
            ? static_cast<int>(side * 650.0f + 0.5f)
            : 0;
        const int rightDelayUs = side < 0.0f
            ? static_cast<int>(-side * 650.0f + 0.5f)
            : 0;
        const int farCutoff = static_cast<int>(
            MINIAUDIO_BINAURAL_FAR_CUTOFF_MAX_HZ -
            (MINIAUDIO_BINAURAL_FAR_CUTOFF_MAX_HZ -
             MINIAUDIO_BINAURAL_FAR_CUTOFF_MIN_HZ) *
                fabsf(side) +
            0.5f);
        LOGINFO("MINIAUDIO_BINAURAL_APPLY name=%s id=%lu sector=%s side_percent=%d rear_percent=%d elevation_percent=%d itd_left_us=%d itd_right_us=%d far_cutoff_hz=%d mode=approximate_not_measured_hrtf total=%u",
                voice.name,
                static_cast<unsigned long>(voice.virtualId),
                MiniaudioBinauralSectorName(sector),
                static_cast<int>(side * 100.0f),
                static_cast<int>(rear * 100.0f),
                static_cast<int>(elevation * 100.0f),
                leftDelayUs,
                rightDelayUs,
                farCutoff,
                BinauralApplyDiagnosticCount);
        voice.binauralLastSector = sector;
        voice.binauralApplyLogged = true;
    }

    const LONG blocks = InterlockedCompareExchange(
        &voice.binauralNode.processedBlocks, 0, 0);
    if ((voice.binauralLastDiagnosticBlocks == 0 && blocks >= 64) ||
        blocks - voice.binauralLastDiagnosticBlocks >= 2048)
    {
        voice.binauralLastDiagnosticBlocks = blocks;
        if (ShouldLogLimited(BinauralProcessDiagnosticCount))
        {
            LOGINFO("MINIAUDIO_BINAURAL_PROCESS name=%s id=%lu blocks=%ld input_blocks=%ld output_blocks=%ld peak_input_permille=%ld peak_output_permille=%ld total=%u",
                    voice.name,
                    static_cast<unsigned long>(voice.virtualId),
                    blocks,
                    InterlockedCompareExchange(
                        &voice.binauralNode.inputBlocks, 0, 0),
                    InterlockedCompareExchange(
                        &voice.binauralNode.outputBlocks, 0, 0),
                    InterlockedCompareExchange(
                        &voice.binauralNode.peakInputPermille, 0, 0),
                    InterlockedCompareExchange(
                        &voice.binauralNode.peakOutputPermille, 0, 0),
                    BinauralProcessDiagnosticCount);
        }
    }
#endif
}

void LogOcclusionFilterError(
    const SVoice &voice,
    const char *phase,
    ma_result result,
    ma_result restoreResult)
{
#if JMV_MINIAUDIO_OCCLUSION_DIAGNOSTICS
    if (ShouldLogLimited(OcclusionFilterErrorDiagnosticCount))
    {
        LOGERR("MINIAUDIO_OCCLUSION_FILTER_ERROR name=%s id=%lu phase=%s result=%d description=%s restore_result=%d route=unfiltered total=%u",
               voice.name[0] != '\0' ? voice.name : "<pending>",
               static_cast<unsigned long>(voice.virtualId),
               phase != nullptr ? phase : "unknown",
               result,
               ma_result_description(result),
               restoreResult,
               OcclusionFilterErrorDiagnosticCount);
    }
#else
    (void)voice;
    (void)phase;
    (void)result;
    (void)restoreResult;
#endif
}

void UninitOcclusionNodes(SVoice &voice)
{
    if (voice.occlusionSplitterInitialized)
        ma_splitter_node_uninit(&voice.occlusionSplitter, nullptr);
    if (voice.occlusionLowPassInitialized)
        ma_lpf_node_uninit(&voice.occlusionLowPass, nullptr);
    voice.occlusionSplitterInitialized = false;
    voice.occlusionLowPassInitialized = false;
    voice.occlusionGraphInitialized = false;
}

bool ApplyOcclusionNodeMix(SVoice &voice, float wet)
{
    if (!voice.occlusionGraphInitialized)
        return false;
    wet = ClampUnit(wet);
    const ma_result dryResult = ma_node_set_output_bus_volume(
        &voice.occlusionSplitter, 0, 1.0f - wet);
    const ma_result wetResult = ma_node_set_output_bus_volume(
        &voice.occlusionSplitter, 1, wet);
    if (dryResult == MA_SUCCESS && wetResult == MA_SUCCESS)
        return true;

    const ma_result failure = dryResult != MA_SUCCESS
        ? dryResult
        : wetResult;
    const ma_result restoreResult = voice.soundInitialized
        ? ma_node_attach_output_bus(
              GetMiniaudioVoiceProcessingSource(voice),
              0,
              GetMiniaudioVoiceDestination(voice),
              0)
        : MA_INVALID_OPERATION;
    LogOcclusionFilterError(
        voice, "mix_volume", failure, restoreResult);
    UninitOcclusionNodes(voice);
    voice.occlusionWet = 0.0f;
    voice.occlusionTargetWet = 0.0f;
    voice.occlusionVisibility = 1.0f;
    voice.occlusionVolumeFactor = 1.0f;
    voice.occlusionTargetVolumeFactor = 1.0f;
    return false;
}

void ResetVoiceOcclusion(SVoice &voice, DWORD now)
{
    voice.occlusionVisibility = 1.0f;
    voice.occlusionVolumeFactor = 1.0f;
    voice.occlusionTargetVolumeFactor = 1.0f;
    voice.occlusionWet = 0.0f;
    voice.occlusionTargetWet = 0.0f;
    voice.occlusionLastTraceTick = 0;
    voice.occlusionLastUpdateTick = now;
    voice.occlusionTraceInitialized = false;
    voice.occlusionLastBlocked = false;
    voice.occlusionApplyLogged = false;
    if (voice.occlusionGraphInitialized)
        ApplyOcclusionNodeMix(voice, 0.0f);
}

void ResetOcclusionWorld(void *worldServer)
{
    if (OcclusionWorldInitialized && OcclusionWorld == worldServer)
        return;

    const DWORD now = GetTickCount();
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
    {
        if (Voices[i].active)
            ResetVoiceOcclusion(Voices[i], now);
    }
    OcclusionWorld = worldServer;
    OcclusionWorldInitialized = true;
    OcclusionLastGlobalTraceTick = 0;
#if JMV_MINIAUDIO_OCCLUSION_DIAGNOSTICS
    LOGINFO("MINIAUDIO_OCCLUSION_WORLD world=0x%p active=%u",
            worldServer,
            ActiveVoiceCount);
#endif
}

bool IsVirtualOcclusionEligible(const SVoice &voice)
{
    return ModAudioSettings().JMVMiniaudioFullBackendPrototype &&
        ModAudioSettings().JMVMiniaudioOcclusionPrototype &&
        voice.virtualManaged && voice.spatial &&
        !voice.speech && !voice.dialog;
}

bool EnsureOcclusionGraph(SVoice &voice)
{
    if (voice.occlusionGraphInitialized)
        return true;
    if (voice.occlusionFilterAttempted || !voice.soundInitialized ||
        !IsVirtualOcclusionEligible(voice))
    {
        return false;
    }
    voice.occlusionFilterAttempted = true;

    const ma_uint32 channels = ma_node_get_output_channels(&voice.sound, 0);
    const ma_uint32 sampleRate = ma_engine_get_sample_rate(&Engine);
    if (channels == 0 || sampleRate == 0)
    {
        LogOcclusionFilterError(
            voice, "format", MA_INVALID_ARGS, MA_SUCCESS);
        return false;
    }

    ma_splitter_node_config splitterConfig =
        ma_splitter_node_config_init(channels);
    ma_result result = ma_splitter_node_init(
        ma_engine_get_node_graph(&Engine),
        &splitterConfig,
        nullptr,
        &voice.occlusionSplitter);
    if (result != MA_SUCCESS)
    {
        LogOcclusionFilterError(
            voice, "splitter_init", result, MA_SUCCESS);
        return false;
    }
    voice.occlusionSplitterInitialized = true;

    ma_lpf_node_config lowPassConfig = ma_lpf_node_config_init(
        channels,
        sampleRate,
        MINIAUDIO_OCCLUSION_LPF_CUTOFF_HZ,
        MINIAUDIO_OCCLUSION_LPF_ORDER);
    result = ma_lpf_node_init(
        ma_engine_get_node_graph(&Engine),
        &lowPassConfig,
        nullptr,
        &voice.occlusionLowPass);
    if (result != MA_SUCCESS)
    {
        LogOcclusionFilterError(
            voice, "lpf_init", result, MA_SUCCESS);
        UninitOcclusionNodes(voice);
        return false;
    }
    voice.occlusionLowPassInitialized = true;
    SetVoiceProcessingState(voice, ma_node_get_state(&voice.sound));

    result = ma_node_set_output_bus_volume(
        &voice.occlusionSplitter, 0, 1.0f);
    if (result == MA_SUCCESS)
    {
        result = ma_node_set_output_bus_volume(
            &voice.occlusionSplitter, 1, 0.0f);
    }
    if (result == MA_SUCCESS)
    {
        result = ma_node_attach_output_bus(
            &voice.occlusionSplitter,
            0,
            GetMiniaudioVoiceDestination(voice),
            0);
    }
    if (result == MA_SUCCESS)
    {
        result = ma_node_attach_output_bus(
            &voice.occlusionSplitter,
            1,
            &voice.occlusionLowPass,
            0);
    }
    if (result == MA_SUCCESS)
    {
        result = ma_node_attach_output_bus(
            &voice.occlusionLowPass,
            0,
            GetMiniaudioVoiceDestination(voice),
            0);
    }
    if (result == MA_SUCCESS)
    {
        result = ma_node_attach_output_bus(
            GetMiniaudioVoiceProcessingSource(voice),
            0,
            &voice.occlusionSplitter,
            0);
    }
    if (result != MA_SUCCESS)
    {
        const ma_result restoreResult = ma_node_attach_output_bus(
            GetMiniaudioVoiceProcessingSource(voice),
            0,
            GetMiniaudioVoiceDestination(voice),
            0);
        LogOcclusionFilterError(
            voice, "graph_attach", result, restoreResult);
        UninitOcclusionNodes(voice);
        return false;
    }

    voice.occlusionGraphInitialized = true;
    ResetVoiceOcclusion(voice, GetTickCount());
    if (OcclusionPerformanceFrequency.QuadPart == 0)
        QueryPerformanceFrequency(&OcclusionPerformanceFrequency);
#if JMV_MINIAUDIO_OCCLUSION_DIAGNOSTICS
    if (ShouldLogLimited(OcclusionFilterReadyDiagnosticCount))
    {
        LOGINFO("MINIAUDIO_OCCLUSION_FILTER_READY name=%s id=%lu channels=%u rate=%u cutoff_hz=%d order=%u route=dry total=%u",
                voice.name,
                static_cast<unsigned long>(voice.virtualId),
                channels,
                sampleRate,
                static_cast<int>(MINIAUDIO_OCCLUSION_LPF_CUTOFF_HZ),
                MINIAUDIO_OCCLUSION_LPF_ORDER,
                OcclusionFilterReadyDiagnosticCount);
    }
#endif
    return true;
}

void LogOcclusionTraceCost(unsigned __int64 elapsedTicks)
{
#if JMV_MINIAUDIO_OCCLUSION_DIAGNOSTICS
    ++OcclusionTraceCount;
    OcclusionTotalTraceTicks += elapsedTicks;
    if (elapsedTicks > OcclusionMaxTraceTicks)
        OcclusionMaxTraceTicks = elapsedTicks;
    if (OcclusionPerformanceFrequency.QuadPart > 0 &&
        (OcclusionTraceCount == 1 || (OcclusionTraceCount % 64) == 0))
    {
        const unsigned __int64 frequency =
            static_cast<unsigned __int64>(
                OcclusionPerformanceFrequency.QuadPart);
        const unsigned __int64 averageUs =
            (OcclusionTotalTraceTicks * 1000000ull) /
            (frequency * OcclusionTraceCount);
        const unsigned __int64 maxUs =
            (OcclusionMaxTraceTicks * 1000000ull) / frequency;
        LOGINFO("MINIAUDIO_OCCLUSION_PERF traces=%u average_us=%llu max_us=%llu",
                OcclusionTraceCount,
                averageUs,
                maxUs);
    }
#else
    (void)elapsedTicks;
#endif
}

bool IsVirtualVoiceInAudibleRange(const SVoice &voice)
{
    if (voice.virtualSoundManager == nullptr ||
        GetMasterVolume(voice.virtualSoundManager) <= 0)
    {
        return false;
    }
    if (!(voice.virtualMaxDistance >= 0.0f))
        return true;
    const uint8_t *manager = static_cast<const uint8_t *>(
        voice.virtualSoundManager);
    const float deltaX = voice.virtualPosition[0] -
        *reinterpret_cast<const float *>(
            manager + SOUND_MANAGER_LISTENER_X_OFFSET);
    const float deltaY = voice.virtualPosition[1] -
        *reinterpret_cast<const float *>(
            manager + SOUND_MANAGER_LISTENER_Y_OFFSET);
    return deltaX * deltaX + deltaY * deltaY <=
        voice.virtualMaxDistance * voice.virtualMaxDistance;
}

void UpdateVirtualVoiceOcclusion(SVoice &voice, DWORD now)
{
    if (!IsVirtualOcclusionEligible(voice))
    {
        if (voice.occlusionGraphInitialized &&
            (voice.occlusionWet != 0.0f ||
             voice.occlusionVolumeFactor != 1.0f))
        {
            ResetVoiceOcclusion(voice, now);
        }
        return;
    }
    if (!EnsureOcclusionGraph(voice))
        return;

    CWorldServer *worldServer = (&theWorldServer);
    CAIMap *aiMap = worldServer != nullptr
        ? &worldServer->aiMap
        : nullptr;
    ResetOcclusionWorld(aiMap != nullptr ? worldServer : nullptr);
    if (aiMap == nullptr)
    {
        voice.occlusionTargetWet = 0.0f;
        voice.occlusionTargetVolumeFactor = 1.0f;
#if JMV_MINIAUDIO_OCCLUSION_DIAGNOSTICS
        if (!OcclusionNoWorldLogged)
        {
            LOGINFO("MINIAUDIO_OCCLUSION_BYPASS reason=%s",
                    worldServer != nullptr ? "no_ai_map" : "no_world");
            OcclusionNoWorldLogged = true;
        }
#endif
    }
    else
    {
        OcclusionNoWorldLogged = false;
        const bool voiceTraceReady = voice.occlusionLastTraceTick == 0 ||
            now - voice.occlusionLastTraceTick >=
                MINIAUDIO_OCCLUSION_VOICE_TRACE_MS;
        const bool globalTraceReady = OcclusionLastGlobalTraceTick == 0 ||
            now - OcclusionLastGlobalTraceTick >=
                MINIAUDIO_OCCLUSION_GLOBAL_TRACE_MS;
        if (voiceTraceReady && globalTraceReady &&
            IsVirtualVoiceInAudibleRange(voice))
        {
            const uint8_t *manager = static_cast<const uint8_t *>(
                voice.virtualSoundManager);
            const CPlot3D listener(
                *reinterpret_cast<const float *>(
                    manager + SOUND_MANAGER_LISTENER_X_OFFSET),
                *reinterpret_cast<const float *>(
                    manager + SOUND_MANAGER_LISTENER_Y_OFFSET),
                *reinterpret_cast<const float *>(
                    manager + SOUND_MANAGER_LISTENER_Z_OFFSET));
            const CPlot3D source(
                voice.virtualPosition[0],
                voice.virtualPosition[1],
                voice.virtualPosition[2]);
            LARGE_INTEGER traceStart = {};
            LARGE_INTEGER traceEnd = {};
#if JMV_MINIAUDIO_OCCLUSION_DIAGNOSTICS
            QueryPerformanceCounter(&traceStart);
#endif
            const float visibility = ClampUnit(
                aiMap->TraceRelief(listener, source));
#if JMV_MINIAUDIO_OCCLUSION_DIAGNOSTICS
            QueryPerformanceCounter(&traceEnd);
            LogOcclusionTraceCost(static_cast<unsigned __int64>(
                traceEnd.QuadPart - traceStart.QuadPart));
#endif
            voice.occlusionLastTraceTick = now;
            OcclusionLastGlobalTraceTick = now;
            voice.occlusionVisibility = visibility;
            voice.occlusionTargetWet = 1.0f - visibility;
            voice.occlusionTargetVolumeFactor = 1.0f -
                MINIAUDIO_OCCLUSION_ATTENUATION *
                    voice.occlusionTargetWet;
            const bool blocked = visibility < 0.50f;
#if JMV_MINIAUDIO_OCCLUSION_DIAGNOSTICS
            if ((!voice.occlusionTraceInitialized ||
                 blocked != voice.occlusionLastBlocked) &&
                ShouldLogLimited(OcclusionTraceDiagnosticCount))
            {
                LOGINFO("MINIAUDIO_OCCLUSION_TRACE state=%s name=%s id=%lu visibility_percent=%d volume_target_percent=%d wet_target_percent=%d listener=(%.2f,%.2f,%.2f) source=(%.2f,%.2f,%.2f) total=%u",
                        blocked ? "blocked" : "clear",
                        voice.name,
                        static_cast<unsigned long>(voice.virtualId),
                        static_cast<int>(visibility * 100.0f + 0.5f),
                        static_cast<int>(
                            voice.occlusionTargetVolumeFactor * 100.0f +
                            0.5f),
                        static_cast<int>(
                            voice.occlusionTargetWet * 100.0f + 0.5f),
                        listener.x,
                        listener.y,
                        listener.z,
                        source.x,
                        source.y,
                        source.z,
                        OcclusionTraceDiagnosticCount);
            }
#endif
            voice.occlusionTraceInitialized = true;
            voice.occlusionLastBlocked = blocked;
        }
    }

    DWORD deltaMs = now - voice.occlusionLastUpdateTick;
    voice.occlusionLastUpdateTick = now;
    if (deltaMs > MINIAUDIO_OCCLUSION_MAX_DELTA_MS)
        deltaMs = MINIAUDIO_OCCLUSION_MAX_DELTA_MS;
    const DWORD transitionMs =
        voice.occlusionWet < voice.occlusionTargetWet
        ? MINIAUDIO_OCCLUSION_ATTACK_MS
        : MINIAUDIO_OCCLUSION_RELEASE_MS;
    const float maximumDelta = transitionMs > 0
        ? static_cast<float>(deltaMs) /
            static_cast<float>(transitionMs)
        : 1.0f;
    voice.occlusionWet = MoveTowards(
        voice.occlusionWet,
        voice.occlusionTargetWet,
        maximumDelta);
    voice.occlusionVolumeFactor = 1.0f -
        MINIAUDIO_OCCLUSION_ATTENUATION * voice.occlusionWet;
    if (!ApplyOcclusionNodeMix(voice, voice.occlusionWet))
        return;

#if JMV_MINIAUDIO_OCCLUSION_DIAGNOSTICS
    if (voice.occlusionWet >= 0.05f && !voice.occlusionApplyLogged &&
        ShouldLogLimited(OcclusionApplyDiagnosticCount))
    {
        LOGINFO("MINIAUDIO_OCCLUSION_APPLY state=filtered name=%s id=%lu visibility_percent=%d volume_percent=%d wet_percent=%d cutoff_hz=%d total=%u",
                voice.name,
                static_cast<unsigned long>(voice.virtualId),
                static_cast<int>(
                    voice.occlusionVisibility * 100.0f + 0.5f),
                static_cast<int>(
                    voice.occlusionVolumeFactor * 100.0f + 0.5f),
                static_cast<int>(voice.occlusionWet * 100.0f + 0.5f),
                static_cast<int>(MINIAUDIO_OCCLUSION_LPF_CUTOFF_HZ),
                OcclusionApplyDiagnosticCount);
        voice.occlusionApplyLogged = true;
    }
    else if (voice.occlusionWet <= 0.01f &&
             voice.occlusionApplyLogged &&
             ShouldLogLimited(OcclusionApplyDiagnosticCount))
    {
        LOGINFO("MINIAUDIO_OCCLUSION_APPLY state=clear name=%s id=%lu volume_percent=100 wet_percent=0 total=%u",
                voice.name,
                static_cast<unsigned long>(voice.virtualId),
                OcclusionApplyDiagnosticCount);
        voice.occlusionApplyLogged = false;
    }
#endif
}

void ReleaseVoice(SVoice &voice, const char *reason);

SVoice *FindVirtualVoice(void *soundManager, uint32_t virtualId)
{
    if (soundManager == nullptr || virtualId == 0xFFFFFFFFu)
        return nullptr;
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
    {
        SVoice &voice = Voices[i];
        if (voice.active && voice.virtualManaged &&
            voice.virtualSoundManager == soundManager &&
            voice.virtualId == virtualId)
        {
            if (!voice.looping && voice.soundInitialized &&
                ma_sound_at_end(&voice.sound))
            {
                ReleaseVoice(voice, "virtual_lookup_eos");
                continue;
            }
            return &voice;
        }
    }
    return nullptr;
}

// Called on the existing game-thread audio update, never the mixer callback.
// Keep only IDs and a world identity: NPC/party objects may disappear or move.
float GetPartyVoiceGain(SVoice &voice)
{
    if (!voice.partyVoiceEnabled)
        return 1.0f;
    CWorldServer *world = (&theWorldServer);
    float nearestSquared = FLT_MAX;
    uint32_t listenerId = 0;
    const bool sameWorld = world != nullptr && world == voice.partyVoiceWorld &&
        strcmp(voice.partyVoiceZone, (LPCSTR)world->szZoneFileName) == 0;
    if (!sameWorld)
        voice.partyVoiceWorld = nullptr; // Never rebind a line across zone changes.
    CUnit *speaker = sameWorld
        ? world->GetUnit(voice.partyVoice.speakerId) : nullptr;
    CPlayer *player = sameWorld ? world->GetPlayer(0) : nullptr;
    if (speaker != nullptr && !speaker->IsDead() && player != nullptr)
    {
        const CPlot3D &source = speaker->GetCP();
        for (int i = 0; i < player->party.people.GetSize(); ++i)
        {
            CUnit *member = world->GetUnit(player->party.people[i].nID);
            if (member == nullptr || member->IsDead())
                continue;
            const CPlot3D &position = member->GetCP();
            const float dx = position.x - source.x;
            const float dy = position.y - source.y;
            const float squared = dx * dx + dy * dy;
            if (_finite(squared) && squared < nearestSquared)
            {
                nearestSquared = squared;
                listenerId = member->GetID();
            }
        }
    }
    const float distance = nearestSquared == FLT_MAX ? FLT_MAX : sqrtf(nearestSquared);
    if(!voice.diagnosticPartyVoiceLogged){voice.diagnosticPartyVoiceLogged=true;EngineDiagnostics::Event("ScenarioVoice audibility id=%u speaker=%u listener=%u sameWorld=%d party=%d distance=%g near=%g far=%g appPaused=%d modalPaused=%d",voice.virtualId,voice.partyVoice.speakerId,listenerId,sameWorld,player?player->party.people.GetSize():-1,distance,voice.partyVoice.nearDistance,voice.partyVoice.farDistance,voice.pausedByApp,voice.pausedByModalSpeech);}
    const float gain = distance <= voice.partyVoice.nearDistance ? 1.0f :
        (distance >= voice.partyVoice.farDistance ? 0.0f :
            (voice.partyVoice.farDistance - distance) /
            (voice.partyVoice.farDistance - voice.partyVoice.nearDistance));
#if JMV_CREATEVOICEFX_DIAGNOSTICS
    const DWORD now = GetTickCount();
    const int percent = static_cast<int>(gain * 100.0f + 0.5f);
    if (voice.partyVoiceLogGain < 0 ||
        ((percent != voice.partyVoiceLogGain || listenerId != voice.partyVoiceLogListener) &&
         now - voice.partyVoiceLogTick >= 500))
    {
        LOGINFO("CREATEVOICEFX_DISTANCE sound=%u speaker=%u listener=%u distance=%.2f near=%.2f far=%.2f gain=%d world=%d speaker_alive=%d",
            voice.virtualId, voice.partyVoice.speakerId, listenerId,
            nearestSquared == FLT_MAX ? -1.0f : distance,
            voice.partyVoice.nearDistance, voice.partyVoice.farDistance, percent,
            sameWorld ? 1 : 0, speaker != nullptr && !speaker->IsDead() ? 1 : 0);
        voice.partyVoiceLogTick = now;
        voice.partyVoiceLogGain = percent;
        voice.partyVoiceLogListener = listenerId;
    }
#endif
    return gain;
}

void UpdateScenarioSpeechDucking(void *soundManager)
{
    unsigned int audible = 0;
    if (!AppAudioSuspended && !ModalSpeechSuspended &&
        GetSpeechMasterVolume(soundManager) > 0)
    {
        for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
        {
            SVoice &voice = Voices[i];
            if (!voice.active || !voice.virtualManaged || !voice.scenarioSpeech ||
                voice.virtualSoundManager != soundManager || !voice.soundInitialized ||
                voice.pausedByApp || voice.pausedByModalSpeech ||
                ma_sound_at_end(&voice.sound) || !ma_sound_is_playing(&voice.sound))
                continue;
            // Fresh party distance, never the already ducked output of other voices.
            const float gain = voice.virtualGainFactor * GetPartyVoiceGain(voice) *
                (voice.virtualManualVolumes ? voice.virtualManualVolume * 0.01f : 1.0f);
            if (gain > 0.0f)
                ++audible;
        }
    }
#if JMV_SCENARIO_SPEECH_DUCK_DIAGNOSTICS
    const unsigned int previousActive = ScenarioDucking.active;
    const float previousGain = ScenarioDucking.gain;
#endif
    ScenarioDucking.Update(audible, GetTickCount());
#if JMV_SCENARIO_SPEECH_DUCK_DIAGNOSTICS
    const float target = audible != 0 ? 0.5f : 1.0f;
    if (previousActive != audible ||
        (previousGain != ScenarioDucking.gain && ScenarioDucking.gain == target))
        LOGINFO("SCENARIO_SPEECH_DUCK active=%u other_gain=%.3f target=%.3f",
            audible, ScenarioDucking.gain, target);
#endif
}

void ApplyVirtualVoiceMix(SVoice &voice)
{
    if (!voice.virtualManaged || !voice.soundInitialized)
        return;
    int masterVolume = voice.speech
        ? GetSpeechMasterVolume(voice.virtualSoundManager)
        : GetMasterVolume(voice.virtualSoundManager);
    const float dynamicGain = DynamicGainResolver != nullptr
        ? DynamicGainResolver(
              voice.virtualSoundManager,
              voice.name,
              voice.spatial,
              voice.looping,
              voice.speech,
              voice.dialog,
              voice.virtualManualVolume,
              voice.spatial ? voice.virtualPosition : nullptr,
              voice.virtualMinDistance,
              voice.virtualMaxDistance)
        : 1.0f;
    float gain = GetPartyVoiceGain(voice) * voice.virtualGainFactor * ClampUnit(dynamicGain) *
        ClampUnit(voice.occlusionVolumeFactor) *
        GetHeightFactor(
            voice.virtualSoundManager,
            voice.virtualAffectedWithHeight);
    if (voice.virtualManualVolumes)
    {
        int manualVolume = voice.virtualManualVolume;
        if (manualVolume < 0)
            manualVolume = 0;
        if (manualVolume > 100)
            manualVolume = 100;
        gain *= static_cast<float>(manualVolume) * 0.01f;

        int percentPan = voice.virtualPercentPan;
        if (percentPan < -100)
            percentPan = -100;
        if (percentPan > 100)
            percentPan = 100;
        const bool reverseStereo = voice.virtualSoundManager != nullptr &&
            *reinterpret_cast<const BOOL *>(
                static_cast<const uint8_t *>(voice.virtualSoundManager) +
                SOUND_MANAGER_REVERSE_STEREO_OFFSET) != FALSE;
        if (!reverseStereo)
            percentPan = -percentPan;
        ma_sound_set_pan(
            &voice.sound,
            static_cast<float>(percentPan) * 0.01f);
    }
    if (voice.speech && !voice.scenarioSpeech)
        gain *= ScenarioDucking.gain;
    voice.gainWithoutMaster = ClampUnit(gain);
    ma_sound_set_volume(
        &voice.sound,
        ClampUnit(
            static_cast<float>(masterVolume) * 0.01f *
            voice.gainWithoutMaster));
}

void ResetVoice(SVoice &voice)
{
    memset(&voice, 0, sizeof(voice));
    voice.binauralLastSector = -1;
    voice.occlusionVisibility = 1.0f;
    voice.occlusionVolumeFactor = 1.0f;
    voice.occlusionTargetVolumeFactor = 1.0f;
}

bool ManagedSlotMatches(const SVoice &voice)
{
    if (!voice.managed || voice.fxControl == nullptr)
        return false;
    const uint8_t *fx = static_cast<const uint8_t *>(voice.fxControl);
    return *reinterpret_cast<const BOOL *>(fx + FX_IS_USED_OFFSET) != FALSE &&
        *reinterpret_cast<const uint32_t *>(fx + FX_UNIQUE_ID_OFFSET) ==
            voice.managedUniqueId;
}

void RequestManagedCleanup(SVoice &voice)
{
    if (!ManagedSlotMatches(voice) || voice.looping)
        return;
    uint8_t *fx = static_cast<uint8_t *>(voice.fxControl);
    *reinterpret_cast<BOOL *>(fx + FX_REQ_CLEANUP_OFFSET) = TRUE;
}

void ReleaseVoice(SVoice &voice, const char *reason)
{
    if (!voice.active && !voice.soundInitialized &&
        voice.resource == nullptr)
        return;

    const bool wasActive = voice.active;
    const unsigned int sequence = voice.sequence;
    char name[sizeof(voice.name)] = {};
    CopyName(name, sizeof(name), voice.name);

    RequestManagedCleanup(voice);

    if (voice.soundInitialized)
    {
        StopVoiceSound(voice);
        ma_sound_uninit(&voice.sound);
    }
    UninitOcclusionNodes(voice);
    UninitMiniaudioBinauralNode(voice);
    AudioResources::Unpin(voice.resource);
    voice.resource = nullptr;
    if (wasActive && ActiveVoiceCount > 0)
        --ActiveVoiceCount;

#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    if (wasActive && ShouldLogLimited(EndDiagnosticCount))
    {
        LOGINFO("MINIAUDIO_ONESHOT_END seq=%u name=%s reason=%s active=%u total=%u",
                sequence,
                name,
                reason != nullptr ? reason : "unknown",
                ActiveVoiceCount,
                EndDiagnosticCount);
    }
#endif
    const uint32_t ticket = VoiceLifetimes.End(static_cast<unsigned>(&voice - Voices));
#if JMV_VOICE_WAIT_DIAGNOSTICS
    if (ticket)
        LOGINFO("VOICE_WAIT_RELEASE ticket=%u sound=%u reason=%s tick=%u",
            ticket, voice.virtualId, reason ? reason : "unknown", GetTickCount());
#else
    (void)ticket;
#endif
    ResetVoice(voice);
}

void ReleaseAllVoices(const char *reason)
{
#if JMV_SCENARIO_SPEECH_DUCK_DIAGNOSTICS
    if (ScenarioDucking.active != 0 || ScenarioDucking.gain != 1.0f)
        LOGINFO("SCENARIO_SPEECH_DUCK_RESET reason=%s active=%u other_gain=%.3f",
            reason != nullptr ? reason : "release_all", ScenarioDucking.active,
            ScenarioDucking.gain);
#endif
    ScenarioDucking = ScenarioSpeechDucking();
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
        ReleaseVoice(Voices[i], reason);
}

void ReleaseDecodedSounds()
{
    AudioResources::ClearCache();
}

void ResetForManager(void *soundManager, const char *reason)
{
    ReleaseAllVoices(reason);
    if (EngineInitialized &&
        (ReverbGraphInitialized || ReverbGraphAttempted))
    {
        DestroyMiniaudioReverbGraph(true, reason);
    }
    if (CurrentSoundManager != nullptr)
        ReleaseDecodedSounds();
    CurrentSoundManager = soundManager;
    if (soundManager != nullptr)
        BinkDeferredEngineShutdown = false;
    LastUniqueCounter = soundManager != nullptr
        ? *reinterpret_cast<const DWORD *>(
              static_cast<const uint8_t *>(soundManager) +
              SOUND_MANAGER_UNIQUE_COUNTER_OFFSET)
        : 0;
}

void SynchronizeUniqueCounter(void *soundManager)
{
    if (soundManager == nullptr)
        return;
    const DWORD uniqueCounter = *reinterpret_cast<const DWORD *>(
        static_cast<const uint8_t *>(soundManager) +
        SOUND_MANAGER_UNIQUE_COUNTER_OFFSET);
    if (uniqueCounter < LastUniqueCounter)
    {
        ReleaseAllVoices("unique_counter_reset");
        if (ReverbGraphInitialized || ReverbGraphAttempted)
        {
            DestroyMiniaudioReverbGraph(
                true, "unique_counter_reset");
        }
        ReleaseDecodedSounds();
    }
    LastUniqueCounter = uniqueCounter;
}

unsigned int PauseVoicesForApp()
{
    unsigned int paused = 0;
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
    {
        SVoice &voice = Voices[i];
        if (!voice.active || !voice.soundInitialized || voice.pausedByApp)
            continue;
        if (!WorldSfxPause::CanResume(false, voice.pausedByModalSpeech,
                voice.pausedByWorld))
        {
            voice.pausedByApp = true;
            ++paused;
            continue;
        }
        const ma_result result = StopVoiceSound(voice);
        if (result == MA_SUCCESS)
        {
            voice.pausedByApp = true;
            ++paused;
        }
        else if (ShouldLogLimited(DeviceErrorDiagnosticCount))
        {
            LOGERR("MINIAUDIO_ONESHOT_DEVICE_ERROR phase=app_pause action=voice_stop name=%s result=%d description=%s total=%u",
                   voice.name,
                   result,
                   ma_result_description(result),
                   DeviceErrorDiagnosticCount);
        }
    }
    return paused;
}

unsigned int ResumeVoicesForApp(unsigned int &ended)
{
    unsigned int resumed = 0;
    ended = 0;
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
    {
        SVoice &voice = Voices[i];
        if (!voice.active || !voice.soundInitialized || !voice.pausedByApp)
            continue;
        voice.pausedByApp = false;
        if (!WorldSfxPause::CanResume(false, voice.pausedByModalSpeech,
                voice.pausedByWorld))
            continue;
        if (ma_sound_at_end(&voice.sound))
        {
            ++ended;
            ReleaseVoice(voice, "paused_eos");
            continue;
        }
        const ma_result result = StartVoiceSound(voice);
        if (result == MA_SUCCESS)
        {
            ++resumed;
        }
        else
        {
            if (ShouldLogLimited(DeviceErrorDiagnosticCount))
            {
                LOGERR("MINIAUDIO_ONESHOT_DEVICE_ERROR phase=app_resume action=voice_start name=%s result=%d description=%s total=%u",
                       voice.name,
                       result,
                       ma_result_description(result),
                       DeviceErrorDiagnosticCount);
            }
            ReleaseVoice(voice, "resume_failed");
        }
    }
    return resumed;
}

unsigned int CountActiveSpeechVoices()
{
    unsigned int count = 0;
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
    {
        if (Voices[i].active && Voices[i].speech)
            ++count;
    }
    return count;
}

unsigned int PauseVoicesForModalSpeech(unsigned int &alreadyPausedByApp)
{
    unsigned int paused = 0;
    alreadyPausedByApp = 0;
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
    {
        SVoice &voice = Voices[i];
        if (!voice.active || !voice.soundInitialized || !voice.speech ||
            voice.pausedByModalSpeech)
        {
            continue;
        }
        if (!WorldSfxPause::CanResume(voice.pausedByApp, false,
                voice.pausedByWorld))
        {
            voice.pausedByModalSpeech = true;
            ++alreadyPausedByApp;
            continue;
        }
        const ma_result result = StopVoiceSound(voice);
        if (result == MA_SUCCESS)
        {
            voice.pausedByModalSpeech = true;
            ++paused;
        }
        else if (ShouldLogLimited(DeviceErrorDiagnosticCount))
        {
            LOGERR("MINIAUDIO_ONESHOT_DEVICE_ERROR phase=modal_speech_pause action=voice_stop name=%s result=%d description=%s total=%u",
                   voice.name,
                   result,
                   ma_result_description(result),
                   DeviceErrorDiagnosticCount);
        }
    }
    return paused;
}

unsigned int ResumeVoicesForModalSpeech(
    unsigned int &ended,
    unsigned int &stillPausedByApp)
{
    unsigned int resumed = 0;
    ended = 0;
    stillPausedByApp = 0;
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
    {
        SVoice &voice = Voices[i];
        if (!voice.active || !voice.soundInitialized ||
            !voice.pausedByModalSpeech)
        {
            continue;
        }
        voice.pausedByModalSpeech = false;
        if (!WorldSfxPause::CanResume(voice.pausedByApp, false,
                voice.pausedByWorld))
        {
            ++stillPausedByApp;
            continue;
        }
        if (ma_sound_at_end(&voice.sound))
        {
            ++ended;
            ReleaseVoice(voice, "modal_speech_paused_eos");
            continue;
        }
        const ma_result result = StartVoiceSound(voice);
        if (result == MA_SUCCESS)
        {
            ++resumed;
        }
        else
        {
            if (ShouldLogLimited(DeviceErrorDiagnosticCount))
            {
                LOGERR("MINIAUDIO_ONESHOT_DEVICE_ERROR phase=modal_speech_resume action=voice_start name=%s result=%d description=%s total=%u",
                       voice.name,
                       result,
                       ma_result_description(result),
                       DeviceErrorDiagnosticCount);
            }
            ReleaseVoice(voice, "modal_speech_resume_failed");
        }
    }
    return resumed;
}

bool IsGameAudioForeground()
{
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr)
        return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId();
}

void RenderMiniaudioOutput(ma_device *device, void *output,
                            const void *input, ma_uint32 frames)
{
    // Render once so voices, music and Bink keep their playback positions.
    ma_engine_data_callback_internal(device, output, input, frames);
    if (!IsGameAudioForeground())
        ma_silence_pcm_frames(output, frames, device->playback.format,
                              device->playback.channels);
}

bool EnsureEngine()
{
    if (EngineInitialized)
        return true;

    const DWORD now = GetTickCount();
    if (InitAttempted && LastInitAttemptTick != 0 &&
        now - LastInitAttemptTick < ENGINE_INIT_RETRY_MS)
    {
        return false;
    }

    InitAttempted = true;
    LastInitAttemptTick = now;
    ++EngineInitAttemptCount;
    memset(&Engine, 0, sizeof(Engine));
    ma_engine_config config = ma_engine_config_init();
    config.listenerCount = 1;
    config.pResourceManager = AudioResources::Manager();
    if (config.pResourceManager == nullptr)
        return false;
    config.dataCallback = RenderMiniaudioOutput;
    const ma_result result = ma_engine_init(&config, &Engine);
    EngineDiagnostics::Event("Audio engine init attempt=%u result=%d description=%s",EngineInitAttemptCount,int(result),ma_result_description(result));
    if (result != MA_SUCCESS)
    {
        if (ShouldLogLimited(DeviceErrorDiagnosticCount))
        {
            LOGERR("MINIAUDIO_ONESHOT_DEVICE_ERROR phase=init attempt=%u retry_ms=%lu result=%d description=%s total=%u",
                   EngineInitAttemptCount,
                   ENGINE_INIT_RETRY_MS,
                   result,
                   ma_result_description(result),
                   DeviceErrorDiagnosticCount);
        }
        return false;
    }

    EngineInitialized = true;
    LastInitAttemptTick = 0;
    LastDeviceRecoveryAttemptTick = 0;
    EnsureMiniaudioReverbGraph();
    ma_device *device = ma_engine_get_device(&Engine);
    LastDeviceState = device != nullptr
        ? ma_device_get_state(device)
        : ma_device_state_uninitialized;
    EngineDiagnostics::Event("Audio engine ready backend=%s state=%d manager=%p bink=%ld",
        device && device->pContext ? ma_get_backend_name(device->pContext->backend):"none",
        int(LastDeviceState),CurrentSoundManager,InterlockedCompareExchange(&BinkActiveVoiceCount,0,0));
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    const char *backend = device != nullptr && device->pContext != nullptr
        ? ma_get_backend_name(device->pContext->backend)
        : "none";
    LOGINFO("MINIAUDIO_ONESHOT_INIT backend=%s state=%d attempt=%u voices=%u cache_entries=%u cache_bytes=%u capture=%s",
            backend,
            static_cast<int>(LastDeviceState),
            EngineInitAttemptCount,
            MAX_ACTIVE_VOICES,
            AudioResources::GetStatistics().entries,
            static_cast<unsigned int>(AudioResources::CacheLimitBytes),
            IsStrictTakeover() ? "resource_bootstrap" : "stock_beginfxll");
#endif
    return true;
}

bool EnsureDeviceStarted(const char *phase)
{
    if (!EngineInitialized)
        return false;
    ma_device *device = ma_engine_get_device(&Engine);
    if (device != nullptr &&
        ma_device_get_state(device) == ma_device_state_started)
    {
        LastDeviceRecoveryAttemptTick = 0;
        return true;
    }

    const DWORD now = GetTickCount();
    if (LastDeviceRecoveryAttemptTick != 0 &&
        now - LastDeviceRecoveryAttemptTick < DEVICE_RECOVERY_RETRY_MS)
    {
        return false;
    }
    LastDeviceRecoveryAttemptTick = now;
    const ma_device_state before = device != nullptr
        ? ma_device_get_state(device)
        : ma_device_state_uninitialized;
    const ma_result result = ma_engine_start(&Engine);
    device = ma_engine_get_device(&Engine);
    const ma_device_state after = device != nullptr
        ? ma_device_get_state(device)
        : ma_device_state_uninitialized;
    const bool recovered = result == MA_SUCCESS &&
        after == ma_device_state_started;
    static unsigned recoveryNotes=0;
    if(recoveryNotes++<24)EngineDiagnostics::Event("Audio device recovery phase=%s backend=%s before=%d after=%d result=%d description=%s recovered=%d voices=%u bink=%ld deferred=%d",
        phase?phase:"unknown",device && device->pContext?ma_get_backend_name(device->pContext->backend):"none",
        int(before),int(after),int(result),ma_result_description(result),recovered?1:0,ActiveVoiceCount,
        InterlockedCompareExchange(&BinkActiveVoiceCount,0,0),BinkDeferredEngineShutdown?1:0);
    if (recovered)
        LastDeviceRecoveryAttemptTick = 0;
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    if (ShouldLogLimited(DeviceRecoveryDiagnosticCount))
    {
        LOGINFO("MINIAUDIO_ONESHOT_DEVICE_RECOVERY phase=%s action=engine_start before=%d after=%d result=%d recovered=%d retry_ms=%lu total=%u",
                phase != nullptr ? phase : "unknown",
                static_cast<int>(before),
                static_cast<int>(after),
                result,
                recovered ? 1 : 0,
                DEVICE_RECOVERY_RETRY_MS,
                DeviceRecoveryDiagnosticCount);
    }
#endif
    if (!recovered && ShouldLogLimited(DeviceErrorDiagnosticCount))
    {
        LOGERR("MINIAUDIO_ONESHOT_DEVICE_ERROR phase=%s action=engine_start before=%d after=%d result=%d description=%s total=%u",
               phase != nullptr ? phase : "device_recovery",
               static_cast<int>(before),
               static_cast<int>(after),
               result,
               ma_result_description(result),
               DeviceErrorDiagnosticCount);
    }
    return recovered;
}

void SyncWorldSfxPause()
{
    // The simulation freezes on combat pause, while miniaudio renders on its
    // own thread. Keep world voices aligned without pausing UI/music/speech.
    const bool paused = theWorld.IsCombatPause();
#if JMV_MINIAUDIO_WORLD_SFX_PAUSE_DIAGNOSTICS
    if (WorldSfxSuspended != paused)
        LOGINFO("MINIAUDIO_WORLD_SFX_STATE paused=%d world_ready=%d active=%u",
            paused ? 1 : 0, 1,
            ActiveVoiceCount);
#endif
    WorldSfxSuspended = paused;
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
    {
        SVoice &voice = Voices[i];
        if (!voice.active || !voice.soundInitialized ||
            !voice.worldSfx ||
            voice.pausedByWorld == paused)
            continue;

        if (!WorldSfxPause::CanResume(voice.pausedByApp,
                voice.pausedByModalSpeech, false))
        {
            // Ownership transfer only: another pause owner still holds it.
            voice.pausedByWorld = paused;
            continue;
        }
        if (!paused && ma_sound_at_end(&voice.sound))
        {
            ReleaseVoice(voice, "world_paused_eos");
            continue;
        }
        if (!paused && !EnsureDeviceStarted("world_sfx_resume"))
            continue; // Retain ownership and retry on a later tick.
        const ma_result result = paused
            ? StopVoiceSound(voice) : StartVoiceSound(voice);
        if (result == MA_SUCCESS)
        {
            voice.pausedByWorld = paused;
        }
        else if (ShouldLogLimited(DeviceErrorDiagnosticCount))
        {
            LOGERR("MINIAUDIO_WORLD_SFX_ERROR name=%s id=%u paused=%d result=%d retry=next_tick",
                voice.name, voice.virtualId, paused ? 1 : 0, result);
        }
    }
}

SVoice *FindVoice(int priority, const char *soundName)
{
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
    {
        if (!Voices[i].active && !Voices[i].soundInitialized &&
            Voices[i].resource == nullptr)
        {
            ResetVoice(Voices[i]);
            return &Voices[i];
        }
    }

    int victim = -1;
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
    {
        if (!Voices[i].active)
            continue;
        // A virtual loop owns a stable game-visible HSOUND until CancelFX or
        // StopAll. Never invalidate that contract merely to reuse its voice.
        if (Voices[i].virtualManaged && Voices[i].looping)
            continue;
        if (victim < 0 || Voices[i].priority < Voices[victim].priority ||
            (Voices[i].priority == Voices[victim].priority &&
             Voices[i].startTick < Voices[victim].startTick))
        {
            victim = static_cast<int>(i);
        }
    }
    if (victim < 0 || priority <= Voices[victim].priority)
        return nullptr;

#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    if (ShouldLogLimited(StealDiagnosticCount))
    {
        LOGINFO("MINIAUDIO_ONESHOT_STEAL old_name=%s old_priority=%d new_name=%s new_priority=%d total=%u",
                Voices[victim].name,
                Voices[victim].priority,
                soundName != nullptr ? soundName : "<null>",
                priority,
                StealDiagnosticCount);
    }
#endif
    ReleaseVoice(Voices[victim], "priority_steal");
    return &Voices[victim];
}

#if JMV_DETEST_BUILD
unsigned int CountActiveSameNameForDiagnostic(const char *soundName)
{
    unsigned int count = 0;
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
    {
        if (Voices[i].active && _stricmp(Voices[i].name, soundName) == 0)
            ++count;
    }
    return count;
}
#endif

bool UpdateListener(void *soundManager)
{
    if (!EngineInitialized || soundManager == nullptr)
        return false;
    const uint8_t *manager = static_cast<const uint8_t *>(soundManager);
    const float x = *reinterpret_cast<const float *>(manager + SOUND_MANAGER_LISTENER_X_OFFSET);
    const float y = *reinterpret_cast<const float *>(manager + SOUND_MANAGER_LISTENER_Y_OFFSET);
    const float z = *reinterpret_cast<const float *>(manager + SOUND_MANAGER_LISTENER_Z_OFFSET);
    if (!_finite(x) || !_finite(y) || !_finite(z))
        return false;
    float dx = *reinterpret_cast<const float *>(manager + SOUND_MANAGER_ORIENTATION_X_OFFSET);
    float dy = *reinterpret_cast<const float *>(manager + SOUND_MANAGER_ORIENTATION_Y_OFFSET);
    float dz = *reinterpret_cast<const float *>(manager + SOUND_MANAGER_ORIENTATION_Z_OFFSET);
    if (!_finite(dx) || !_finite(dy) || !_finite(dz) ||
        fabsf(dx) + fabsf(dy) + fabsf(dz) < 0.001f)
    {
        dx = 0.0f;
        dy = 1.0f;
        dz = 0.0f;
    }
    ma_engine_listener_set_position(&Engine, 0, x, y, z);
    ma_engine_listener_set_direction(&Engine, 0, dx, dy, dz);
    ma_engine_listener_set_world_up(&Engine, 0, 0.0f, 0.0f, 1.0f);
    return true;
}

EStartResult StartDecoded(
    void *soundManager,
    const char *soundName,
    int priority,
    bool spatial,
    const float *position,
    float minDistance,
    float maxDistance,
    int percentVolume,
    int percentPan,
    float gainFactor,
    float pitchFactor,
    bool managed = false,
    void *fxControl = nullptr,
    void *milesSample = nullptr,
    uint32_t managedUniqueId = 0,
    bool looping = false,
    bool speech = false,
    const SVirtualStartInfo *virtualStart = nullptr)
{
    if (CurrentSoundManager != soundManager)
        ResetForManager(soundManager, "manager_change");
    SynchronizeUniqueCounter(soundManager);
    SyncWorldSfxPause();

    if (AppAudioSuspended)
    {
        SetLastStartReason("app_inactive");
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
        if (ShouldLogLimited(SuppressDiagnosticCount))
        {
            LOGINFO("MINIAUDIO_ONESHOT_START_SUPPRESSED name=%s reason=app_inactive route=%s total=%u",
                    soundName != nullptr ? soundName : "<null>",
                    IsStrictTakeover() ? "strict_suppress" : "miles",
                    SuppressDiagnosticCount);
        }
#endif
        return IsStrictTakeover() ? START_SUPPRESSED : START_FAILED;
    }

    if (!AudioResources::Contains(soundName))
    {
        SetLastStartReason("cache_miss");
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
        if (ShouldLogLimited(FallbackDiagnosticCount))
        {
            LOGINFO("MINIAUDIO_ONESHOT_FALLBACK name=%s phase=cache reason=miss route=%s total=%u",
                    soundName,
                    CacheFailureRoute(),
                    FallbackDiagnosticCount);
        }
#endif
        return START_FAILED;
    }

    if (!EnsureEngine())
    {
        SetLastStartReason("init_unavailable");
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
        if (ShouldLogLimited(FallbackDiagnosticCount))
        {
            LOGWARN("MINIAUDIO_ONESHOT_FALLBACK name=%s phase=init reason=unavailable route=%s total=%u",
                    soundName,
                    FailureRoute(),
                    FallbackDiagnosticCount);
        }
#endif
        return START_FAILED;
    }

    Tick(soundManager);
    if (!EnsureDeviceStarted("start_request"))
    {
        ma_device *device = ma_engine_get_device(&Engine);
        SetLastStartReason("device_not_started");
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
        if (ShouldLogLimited(FallbackDiagnosticCount))
        {
            LOGWARN("MINIAUDIO_ONESHOT_FALLBACK name=%s phase=device state=%d route=%s total=%u",
                    soundName,
                    device != nullptr
                        ? static_cast<int>(ma_device_get_state(device))
                        : static_cast<int>(ma_device_state_uninitialized),
                    FailureRoute(),
                    FallbackDiagnosticCount);
        }
#endif
        return START_FAILED;
    }

#if JMV_DETEST_BUILD
    const unsigned int activeSameName =
        CountActiveSameNameForDiagnostic(soundName);
    if (activeSameName >= DUPLICATE_DIAGNOSTIC_THRESHOLD &&
        ShouldLogLimited(DuplicateAllowedDiagnosticCount))
    {
        LOGINFO("MINIAUDIO_DUPLICATE_ALLOWED name=%s already_active=%u incoming_priority=%d total=%u",
                soundName,
                activeSameName,
                priority,
                DuplicateAllowedDiagnosticCount);
    }
#endif

    if (managed)
    {
        for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
        {
            if (Voices[i].managed &&
                (Voices[i].fxControl == fxControl ||
                 Voices[i].milesSample == milesSample))
            {
                ReleaseVoice(Voices[i], "managed_slot_reuse");
            }
        }
    }

    SVoice *voice = FindVoice(priority, soundName);
    if (voice == nullptr)
    {
        SetLastStartReason("voice_pool");
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
        if (ShouldLogLimited(FallbackDiagnosticCount))
        {
            LOGWARN("MINIAUDIO_ONESHOT_FALLBACK name=%s phase=voice_pool active=%u priority=%d route=%s total=%u",
                    soundName,
                    ActiveVoiceCount,
                    priority,
                    FailureRoute(),
                    FallbackDiagnosticCount);
        }
#endif
        return START_FAILED;
    }

    // Pin before opening the VFS URI. Failed starts release the handle only
    // after miniaudio has detached the sound and its asynchronous data source.
    voice->resource = AudioResources::Pin(soundName);
    const bool reverbRouted = ShouldRouteMiniaudioReverb(
        soundName, speech, virtualStart != nullptr && virtualStart->dialog);
    ma_sound_group *initialGroup = reverbRouted ? &ReverbSourceGroup : nullptr;
    ma_result result = static_cast<ma_result>(AudioResources::InitSound(
        &Engine, voice->resource, looping, initialGroup, &voice->sound));
    if (result != MA_SUCCESS)
    {
        SetLastStartReason("resource_sound_init");
        LOGERR("AUDIO_RESOURCE_START_ERROR result=%d description=%s", result,
               ma_result_description(result));
        ReleaseVoice(*voice, "resource_sound_init_failed");
        return START_FAILED;
    }
    voice->soundInitialized = true;
    voice->reverbRouted = reverbRouted;
    ma_sound_set_looping(&voice->sound, looping ? MA_TRUE : MA_FALSE);

    gainFactor = ClampUnit(gainFactor);
    if (!_finite(pitchFactor) || pitchFactor <= 0.0f)
        pitchFactor = 1.0f;
    if (pitchFactor < 0.5f)
        pitchFactor = 0.5f;
    if (pitchFactor > 2.0f)
        pitchFactor = 2.0f;
    if (percentVolume < 0)
        percentVolume = 0;
    const int maximumVolume = managed ? 127 : 100;
    if (percentVolume > maximumVolume)
        percentVolume = maximumVolume;
    if (percentPan < -100)
        percentPan = -100;
    if (percentPan > 100)
        percentPan = 100;

    if (spatial)
    {
        if (!UpdateListener(soundManager))
        {
            SetLastStartReason("listener_invalid");
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
            if (ShouldLogLimited(FallbackDiagnosticCount))
            {
                LOGWARN("MINIAUDIO_ONESHOT_FALLBACK name=%s phase=listener reason=invalid route=%s total=%u",
                        soundName,
                        FailureRoute(),
                        FallbackDiagnosticCount);
            }
#endif
            ReleaseVoice(*voice, "listener_failed");
            return START_FAILED;
        }
        ma_sound_set_spatialization_enabled(&voice->sound, MA_TRUE);
        ma_sound_set_position(&voice->sound, position[0], position[1], position[2]);
        ma_sound_set_positioning(&voice->sound, ma_positioning_absolute);
        ma_sound_set_attenuation_model(&voice->sound, ma_attenuation_model_linear);
        ma_sound_set_min_distance(&voice->sound, minDistance);
        ma_sound_set_max_distance(&voice->sound, maxDistance);
        ma_sound_set_rolloff(&voice->sound, 1.0f);
        ma_sound_set_min_gain(&voice->sound, 0.0f);
        ma_sound_set_max_gain(&voice->sound, 1.0f);
        voice->gainWithoutMaster = gainFactor;
    }
    else
    {
        ma_sound_set_spatialization_enabled(&voice->sound, MA_FALSE);
        ma_sound_set_pan(&voice->sound, static_cast<float>(percentPan) * 0.01f);
        voice->gainWithoutMaster = managed
            ? static_cast<float>(percentVolume) / 127.0f
            : static_cast<float>(percentVolume) * 0.01f * gainFactor;
    }
    ma_sound_set_pitch(&voice->sound, pitchFactor);
    if (virtualStart != nullptr)
    {
        CopyName(voice->name, sizeof(voice->name), soundName);
        voice->virtualManaged = true;
        voice->scenarioSpeech = virtualStart->scenarioSpeech;
        voice->virtualSoundManager = virtualStart->soundManager;
        voice->virtualId = virtualStart->id;
        voice->virtualManualVolume = virtualStart->manualVolume;
        voice->virtualPercentPan = virtualStart->percentPan;
        voice->virtualGainFactor = ClampUnit(virtualStart->gainFactor);
        voice->virtualManualVolumes = virtualStart->manualVolumes;
        voice->virtualAffectedWithHeight =
            virtualStart->affectedWithHeight;
        if (virtualStart->partyVoice != nullptr)
        {
            voice->partyVoiceEnabled = true;
            voice->partyVoice = *virtualStart->partyVoice;
            voice->partyVoiceWorld = (&theWorldServer);
            CopyName(voice->partyVoiceZone, sizeof(voice->partyVoiceZone),
                (&theWorldServer) != nullptr
                    ? (LPCSTR)theWorldServer.szZoneFileName : "");
#if JMV_CREATEVOICEFX_DIAGNOSTICS
            voice->partyVoiceLogGain = -1;
#endif
        }
        voice->dialog = virtualStart->dialog;
        voice->speech = speech;
        voice->spatial = spatial;
        voice->looping = looping;
        if (spatial && position != nullptr)
        {
            voice->virtualPosition[0] = position[0];
            voice->virtualPosition[1] = position[1];
            voice->virtualPosition[2] = position[2];
            voice->virtualMinDistance = minDistance;
            voice->virtualMaxDistance = maxDistance;
        }
        UpdateVirtualVoiceBinaural(*voice);
        UpdateVirtualVoiceOcclusion(*voice, GetTickCount());
        ApplyVirtualVoiceMix(*voice);
    }
    else
    {
        const int masterVolume = speech
            ? GetSpeechMasterVolume(soundManager)
            : GetMasterVolume(soundManager);
        ma_sound_set_volume(
            &voice->sound,
            managed
                ? voice->gainWithoutMaster
                : static_cast<float>(masterVolume) * 0.01f *
                      voice->gainWithoutMaster);
    }

    const bool holdForModalSpeech =
        speech && ModalSpeechSuspended;
    const bool worldSfx = WorldSfxPause::Applies(spatial ||
        (virtualStart != nullptr && virtualStart->partyVoice != nullptr), speech,
        virtualStart != nullptr && virtualStart->dialog);
    const bool holdForWorld = WorldSfxSuspended && worldSfx;
    result = (holdForModalSpeech || holdForWorld)
        ? StopVoiceSound(*voice)
        : StartVoiceSound(*voice);
    if (result != MA_SUCCESS)
    {
        SetLastStartReason("start_failed");
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
        if (ShouldLogLimited(FallbackDiagnosticCount))
        {
            LOGWARN("MINIAUDIO_ONESHOT_FALLBACK name=%s phase=start result=%d description=%s route=%s total=%u",
                    soundName,
                    result,
                    ma_result_description(result),
                    FailureRoute(),
                    FallbackDiagnosticCount);
        }
#endif
        ReleaseVoice(*voice, "start_failed");
        return START_FAILED;
    }

    voice->active = true;
    voice->spatial = spatial;
    voice->pausedByModalSpeech = holdForModalSpeech;
    voice->pausedByWorld = holdForWorld;
    voice->worldSfx = worldSfx;
    voice->managed = managed;
    voice->looping = looping;
    voice->speech = speech;
    voice->fxControl = fxControl;
    voice->milesSample = milesSample;
    voice->managedUniqueId = managedUniqueId;
    voice->priority = priority;
    voice->startTick = GetTickCount();
    voice->sequence = ++StartSequence;
    CopyName(voice->name, sizeof(voice->name), soundName);
    ++ActiveVoiceCount;
    SetLastStartReason("started");
    if (voice->virtualManaged && voice->scenarioSpeech)
        VoiceLifetimes.Publish(static_cast<unsigned>(voice - Voices),
            reinterpret_cast<uintptr_t>(soundManager), voice->virtualId);
#if JMV_SCENARIO_SPEECH_DUCK_DIAGNOSTICS
    if (voice->virtualManaged && speech)
        LOGINFO("SCENARIO_SPEECH_START sound=%u name=%s foreground=%d duck=%.3f gain=%.3f",
            voice->virtualId, soundName, voice->scenarioSpeech ? 1 : 0,
            voice->scenarioSpeech ? 1.0f : ScenarioDucking.gain, voice->gainWithoutMaster);
#endif
#if JMV_MINIAUDIO_MODAL_SPEECH_PAUSE_DIAGNOSTICS
    if (holdForModalSpeech && !GamePauseSpeechSelectorActive)
    {
        LOGINFO("MINIAUDIO_MODAL_SPEECH_START_HELD seq=%u name=%s reason=modal_active active=%u speech_active=%u",
                voice->sequence,
                voice->name,
                ActiveVoiceCount,
                CountActiveSpeechVoices());
    }
#endif
#if JMV_MINIAUDIO_GAME_PAUSE_SPEECH_DIAGNOSTICS
    if (holdForModalSpeech && GamePauseSpeechSelectorActive)
    {
        LOGINFO("MINIAUDIO_GAME_PAUSE_SPEECH_START_HELD seq=%u name=%s reason=game_pause_active active=%u speech_active=%u",
                voice->sequence,
                voice->name,
                ActiveVoiceCount,
                CountActiveSpeechVoices());
    }
#endif
#if JMV_MINIAUDIO_REVERB_DIAGNOSTICS
    if (voice->reverbRouted &&
        ShouldLogLimited(ReverbRouteDiagnosticCount))
    {
        const int profile = static_cast<int>(InterlockedCompareExchange(
            &ReverbNode.targetProfile, 0, 0));
        LOGINFO("MINIAUDIO_REVERB_ROUTE seq=%u name=%s id=%lu spatial=%d looping=%d profile=%s dry=1 wet_level=%.3f total=%u",
                voice->sequence,
                voice->name,
                static_cast<unsigned long>(voice->virtualId),
                voice->spatial ? 1 : 0,
                voice->looping ? 1 : 0,
                MiniaudioReverbProfileName(profile),
                ReverbNode.banks[profile == 1 ? 1 : 0].level,
                ReverbRouteDiagnosticCount);
    }
#endif
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    if (StartSequence <= 32 || (StartSequence % 64) == 0)
    {
        LOGINFO("MINIAUDIO_ONESHOT_START seq=%u name=%s path=%s virtual_id=%u priority=%d gain_percent=%d pitch_percent=%d active=%u miles=0",
                voice->sequence,
                soundName,
                virtualStart != nullptr
                    ? (looping ? "virtual_loop" :
                       (speech ? "virtual_speech" :
                        (spatial ? "virtual_3d" : "virtual_flat")))
                    : (managed ? (looping ? "managed_loop" :
                                 (speech ? "managed_speech" : "managed_fx"))
                               : (spatial ? "3d" : "flat")),
                virtualStart != nullptr ? virtualStart->id : 0xFFFFFFFFu,
                priority,
                static_cast<int>(voice->gainWithoutMaster * 100.0f + 0.5f),
                static_cast<int>(pitchFactor * 100.0f + 0.5f),
                ActiveVoiceCount);
    }
#endif
    return STARTED;
}

struct SBinkSoundInterface;

typedef int(__stdcall *BinkTrackOpenProc)(
    SBinkSoundInterface *soundInterface,
    uint32_t frequency,
    int bits,
    int channels,
    uint32_t flags,
    void *bink);
typedef BinkTrackOpenProc(__stdcall *BinkSystemOpenProc)(uintptr_t param);
typedef int(__stdcall *BinkReadyProc)(SBinkSoundInterface *soundInterface);
typedef int(__stdcall *BinkLockProc)(
    SBinkSoundInterface *soundInterface,
    uint8_t **address,
    uint32_t *length);
typedef int(__stdcall *BinkUnlockProc)(
    SBinkSoundInterface *soundInterface,
    uint32_t filledBytes);
typedef void(__stdcall *BinkVolumeProc)(
    SBinkSoundInterface *soundInterface,
    int volume);
typedef void(__stdcall *BinkPanProc)(
    SBinkSoundInterface *soundInterface,
    int pan);
typedef int(__stdcall *BinkStateProc)(
    SBinkSoundInterface *soundInterface,
    int status);
typedef void(__stdcall *BinkCloseProc)(SBinkSoundInterface *soundInterface);
typedef HANDLE(WINAPI *CreateWaitableTimerExWProc)(
    LPSECURITY_ATTRIBUTES timerAttributes,
    LPCWSTR timerName,
    DWORD flags,
    DWORD desiredAccess);

struct SBinkSoundInterface
{
    BinkReadyProc Ready;
    BinkLockProc Lock;
    BinkUnlockProc Unlock;
    BinkVolumeProc Volume;
    BinkPanProc Pan;
    BinkStateProc Pause;
    BinkStateProc SetOnOff;
    BinkCloseProc Close;
    uint32_t preferredBytes;
};

static_assert(sizeof(SBinkSoundInterface) == 0x24,
              "The supported Bink sound callback table is x86-only");
static_assert(offsetof(SBinkSoundInterface, preferredBytes) == 0x20,
              "Bink 1.0t reads the preferred PCM block size at +0x20");

const size_t BINK_RETAIL_SOUND_WORKSPACE_BYTES = 0xC0;
const DWORD BINK_READY_BACKPRESSURE_MS = 1;
const uint64_t BINK_READY_PRECISE_WAIT_US = 1000;
const LONGLONG BINK_READY_PRECISE_WAIT_100NS = -10000LL;
const DWORD BINK_READY_PRECISE_WAIT_TIMEOUT_MS = 50;
const DWORD BINK_READY_WAITABLE_TIMER_HIGH_RESOLUTION = 0x00000002;

enum EBinkReadyWaitBackend
{
    BINK_READY_WAIT_LEGACY_SLEEP = 0,
    BINK_READY_WAIT_HIGH_RES_TIMER,
    BINK_READY_WAIT_YIELD_FALLBACK,
};

struct SBinkVoice
{
    SBinkSoundInterface *soundInterface;
    HANDLE readyWaitTimer;
    ma_pcm_rb ring;
    ma_sound sound;
    ma_format format;
    uint32_t frequency;
    uint32_t flags;
    uint32_t bytesPerFrame;
    ma_uint32 preferredFrames;
    ma_uint32 lockedFrames;
    uint64_t committedBytes;
    void *bink;
    int bits;
    int channels;
    int sequence;
    int volume;
    int pan;
    uint32_t readyCalls;
    uint32_t readyAccepts;
    uint32_t lockCalls;
    uint32_t unlockCalls;
    uint32_t readyBackpressureWaits;
    uint32_t readyBackpressureHighResolutionWaits;
    uint32_t readyBackpressureFallbackYields;
    uint32_t readyBackpressureWaitFailures;
    uint32_t readyNoProgressStreak;
    uint32_t readyMaxNoProgressStreak;
    uint32_t readyEmptyRingProbes;
    uint32_t readyEmptyRingStreak;
    uint32_t readyMaxEmptyRingStreak;
    uint32_t lastReadyUnlockCalls;
    uint64_t readyBackpressureRequestedUs;
    uint64_t readyBackpressureElapsedUs;
    uint64_t readyBackpressureMaxElapsedUs;
    LONGLONG readyWaitQpcFrequency;
    DWORD readyWaitTimerCreateError;
    DWORD readyWaitLastError;
    int lastReadyState;
    bool ringInitialized;
    bool soundInitialized;
    bool locked;
    bool started;
    bool paused;
    bool onOff;
    bool registered;
    bool readyWaitTimerHighResolution;
};

#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
bool ShouldLogBinkReadyState(SBinkVoice *voice, int state)
{
    if (voice == nullptr)
        return false;

    const uint32_t calls = voice->readyCalls;
    const bool firstCalls = calls <= 4;
    const bool stateChanged = voice->lastReadyState != state;
    const bool exponentialSample =
        calls != 0 && (calls & (calls - 1)) == 0;
    voice->lastReadyState = state;
    if (!firstCalls && !stateChanged && !exponentialSample)
        return false;

    ++BinkReadyDiagnosticCount;
    return true;
}

bool ShouldLogBinkReadyBackpressure(const SBinkVoice *voice)
{
    if (voice == nullptr)
        return false;

    const uint32_t waits = voice->readyBackpressureWaits;
    return waits <= 4 || (waits != 0 && (waits & (waits - 1)) == 0);
}
#endif

const char *GetBinkReadyWaitBackendName(EBinkReadyWaitBackend backend)
{
    switch (backend)
    {
    case BINK_READY_WAIT_HIGH_RES_TIMER:
        return "high_res_timer";
    case BINK_READY_WAIT_YIELD_FALLBACK:
        return "yield_fallback";
    default:
        return "legacy_sleep";
    }
}

const char *GetConfiguredBinkReadyWaitBackend(const SBinkVoice *voice)
{
    if (!ModAudioSettings().JMVMiniaudioBinkReadyBackpressurePrototype)
        return "disabled";
    if (!ModAudioSettings().JMVMiniaudioBinkPreciseWaitPrototype)
        return "legacy_sleep";
    return voice != nullptr && voice->readyWaitTimer != nullptr
        ? "high_res_timer"
        : "yield_fallback";
}

void CloseBinkReadyWaitTimer(SBinkVoice *voice)
{
    if (voice == nullptr || voice->readyWaitTimer == nullptr)
        return;

    CancelWaitableTimer(voice->readyWaitTimer);
    CloseHandle(voice->readyWaitTimer);
    voice->readyWaitTimer = nullptr;
}

void InitializeBinkReadyPreciseWait(SBinkVoice *voice)
{
    if (voice == nullptr)
        return;

    LARGE_INTEGER qpcFrequency = {};
    if (QueryPerformanceFrequency(&qpcFrequency))
        voice->readyWaitQpcFrequency = qpcFrequency.QuadPart;

    if (!ModAudioSettings().JMVMiniaudioBinkReadyBackpressurePrototype ||
        !ModAudioSettings().JMVMiniaudioBinkPreciseWaitPrototype)
    {
        return;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    CreateWaitableTimerExWProc createTimer = kernel32 != nullptr
        ? reinterpret_cast<CreateWaitableTimerExWProc>(
              GetProcAddress(kernel32, "CreateWaitableTimerExW"))
        : nullptr;
    if (createTimer == nullptr)
    {
        voice->readyWaitTimerCreateError = kernel32 == nullptr
            ? GetLastError()
            : ERROR_PROC_NOT_FOUND;
        voice->readyWaitLastError = voice->readyWaitTimerCreateError;
    }
    else
    {
        SetLastError(ERROR_SUCCESS);
        voice->readyWaitTimer = createTimer(
            nullptr,
            nullptr,
            BINK_READY_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (voice->readyWaitTimer == nullptr)
        {
            voice->readyWaitTimerCreateError = GetLastError();
            voice->readyWaitLastError = voice->readyWaitTimerCreateError;
        }
        else
        {
            voice->readyWaitTimerHighResolution = true;
        }
    }

#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    LOGINFO("MINIAUDIO_BINK_PRECISE_WAIT_INIT seq=%d result=%s backend=%s requested_us=%I64u create_error=%lu qpc_frequency=%I64d fail_open=yield",
            voice->sequence,
            voice->readyWaitTimer != nullptr ? "ready" : "fallback",
            GetConfiguredBinkReadyWaitBackend(voice),
            BINK_READY_PRECISE_WAIT_US,
            static_cast<unsigned long>(voice->readyWaitTimerCreateError),
            voice->readyWaitQpcFrequency);
#endif
}

void DisableBinkReadyWaitTimer(
    SBinkVoice *voice,
    const char *phase,
    DWORD error)
{
    if (voice == nullptr)
        return;

    ++voice->readyBackpressureWaitFailures;
    voice->readyWaitLastError = error;
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    const uint32_t failures = voice->readyBackpressureWaitFailures;
    if (failures <= 4 || (failures & (failures - 1)) == 0)
    {
        LOGWARN("MINIAUDIO_BINK_PRECISE_WAIT_FAILURE seq=%d phase=%s error=%lu failures=%u fail_open=yield",
                voice->sequence,
                phase != nullptr ? phase : "unknown",
                static_cast<unsigned long>(error),
                failures);
    }
#endif
    CloseBinkReadyWaitTimer(voice);
}

EBinkReadyWaitBackend YieldBinkReadyWait(SBinkVoice *voice)
{
    if (voice != nullptr)
        ++voice->readyBackpressureFallbackYields;
    if (!SwitchToThread())
        Sleep(0);
    return BINK_READY_WAIT_YIELD_FALLBACK;
}

EBinkReadyWaitBackend WaitBinkReadyBackpressure(SBinkVoice *voice)
{
    if (voice == nullptr ||
        !ModAudioSettings().JMVMiniaudioBinkPreciseWaitPrototype)
    {
        Sleep(BINK_READY_BACKPRESSURE_MS);
        return BINK_READY_WAIT_LEGACY_SLEEP;
    }

    if (voice->readyWaitTimer == nullptr)
        return YieldBinkReadyWait(voice);

    LARGE_INTEGER dueTime = {};
    dueTime.QuadPart = BINK_READY_PRECISE_WAIT_100NS;
    if (!SetWaitableTimer(
            voice->readyWaitTimer,
            &dueTime,
            0,
            nullptr,
            nullptr,
            FALSE))
    {
        DisableBinkReadyWaitTimer(
            voice, "set_timer", GetLastError());
        return YieldBinkReadyWait(voice);
    }

    const DWORD waitResult = WaitForSingleObject(
        voice->readyWaitTimer, BINK_READY_PRECISE_WAIT_TIMEOUT_MS);
    if (waitResult == WAIT_OBJECT_0)
    {
        ++voice->readyBackpressureHighResolutionWaits;
        return BINK_READY_WAIT_HIGH_RES_TIMER;
    }

    DWORD error = ERROR_GEN_FAILURE;
    if (waitResult == WAIT_TIMEOUT)
        error = ERROR_TIMEOUT;
    else if (waitResult == WAIT_FAILED)
        error = GetLastError();
    DisableBinkReadyWaitTimer(voice, "wait_timer", error);
    return YieldBinkReadyWait(voice);
}

uint64_t MeasureBinkReadyWaitMicroseconds(
    const SBinkVoice *voice,
    const LARGE_INTEGER &qpcStarted,
    bool qpcReady,
    DWORD tickStarted)
{
    LARGE_INTEGER qpcFinished = {};
    if (voice != nullptr && qpcReady &&
        voice->readyWaitQpcFrequency > 0 &&
        QueryPerformanceCounter(&qpcFinished) &&
        qpcFinished.QuadPart >= qpcStarted.QuadPart)
    {
        const uint64_t delta = static_cast<uint64_t>(
            qpcFinished.QuadPart - qpcStarted.QuadPart);
        return delta * 1000000ULL /
            static_cast<uint64_t>(voice->readyWaitQpcFrequency);
    }
    return static_cast<uint64_t>(GetTickCount() - tickStarted) * 1000ULL;
}

void ApplyBinkReadyBackpressure(SBinkVoice *voice)
{
    if (voice == nullptr ||
        !ModAudioSettings().JMVMiniaudioBinkReadyBackpressurePrototype)
    {
        return;
    }

    // Bink calls Ready immediately after every successful Unlock so that it
    // can fill another output block without delay. Only repeated probes with
    // no new producer progress are throttled; the first probe and every probe
    // following a new PCM commit remain immediate.
    if (voice->lastReadyUnlockCalls != voice->unlockCalls)
    {
        voice->lastReadyUnlockCalls = voice->unlockCalls;
        voice->readyNoProgressStreak = 0;
        return;
    }

    ++voice->readyNoProgressStreak;
    if (voice->readyNoProgressStreak > voice->readyMaxNoProgressStreak)
        voice->readyMaxNoProgressStreak = voice->readyNoProgressStreak;

    LARGE_INTEGER qpcStarted = {};
    const bool qpcReady = voice->readyWaitQpcFrequency > 0 &&
        QueryPerformanceCounter(&qpcStarted) != FALSE;
    const DWORD tickStarted = GetTickCount();
    const EBinkReadyWaitBackend backend =
        WaitBinkReadyBackpressure(voice);
    const uint64_t elapsedUs = MeasureBinkReadyWaitMicroseconds(
        voice, qpcStarted, qpcReady, tickStarted);
    ++voice->readyBackpressureWaits;
    voice->readyBackpressureRequestedUs += BINK_READY_PRECISE_WAIT_US;
    voice->readyBackpressureElapsedUs += elapsedUs;
    if (elapsedUs > voice->readyBackpressureMaxElapsedUs)
        voice->readyBackpressureMaxElapsedUs = elapsedUs;

#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    if (ShouldLogBinkReadyBackpressure(voice))
    {
        LOGINFO("MINIAUDIO_BINK_BACKPRESSURE seq=%d reason=no_producer_progress backend=%s requested_us=%I64u elapsed_us=%I64u call=%u wait=%u unlocks=%u streak=%u high_res_waits=%u fallback_yields=%u wait_failures=%u last_error=%lu",
                voice->sequence,
                GetBinkReadyWaitBackendName(backend),
                BINK_READY_PRECISE_WAIT_US,
                elapsedUs,
                voice->readyCalls,
                voice->readyBackpressureWaits,
                voice->unlockCalls,
                voice->readyNoProgressStreak,
                voice->readyBackpressureHighResolutionWaits,
                voice->readyBackpressureFallbackYields,
                voice->readyBackpressureWaitFailures,
                static_cast<unsigned long>(voice->readyWaitLastError));
    }
#endif
}

SRWLOCK BinkVoiceRegistryLock = SRWLOCK_INIT;
std::vector<SBinkVoice *> BinkVoiceRegistry;

BinkSystemOpenProc BinkMilesSystemOpen = nullptr;
BinkTrackOpenProc BinkMilesTrackOpen = nullptr;
uintptr_t BinkMilesSystemParam = 0;

int __stdcall BinkMiniaudioReady(SBinkSoundInterface *soundInterface);
int __stdcall BinkMiniaudioLock(
    SBinkSoundInterface *soundInterface,
    uint8_t **address,
    uint32_t *length);
int __stdcall BinkMiniaudioUnlock(
    SBinkSoundInterface *soundInterface,
    uint32_t filledBytes);
void __stdcall BinkMiniaudioVolume(
    SBinkSoundInterface *soundInterface,
    int volume);
void __stdcall BinkMiniaudioPan(
    SBinkSoundInterface *soundInterface,
    int pan);
int __stdcall BinkMiniaudioPause(
    SBinkSoundInterface *soundInterface,
    int status);
int __stdcall BinkMiniaudioSetOnOff(
    SBinkSoundInterface *soundInterface,
    int status);
void __stdcall BinkMiniaudioClose(SBinkSoundInterface *soundInterface);
int __stdcall BinkMiniaudioTrackOpen(
    SBinkSoundInterface *soundInterface,
    uint32_t frequency,
    int bits,
    int channels,
    uint32_t flags,
    void *bink);
BinkTrackOpenProc __stdcall BinkMiniaudioSystemOpen(uintptr_t param);

SBinkVoice *GetBinkVoice(SBinkSoundInterface *soundInterface)
{
    if (soundInterface == nullptr)
        return nullptr;

    AcquireSRWLockShared(&BinkVoiceRegistryLock);
    SBinkVoice *result = nullptr;
    for (size_t i = 0; i < BinkVoiceRegistry.size(); ++i)
    {
        SBinkVoice *const voice = BinkVoiceRegistry[i];
        if (voice != nullptr && voice->soundInterface == soundInterface)
        {
            result = voice;
            break;
        }
    }
    ReleaseSRWLockShared(&BinkVoiceRegistryLock);
    return result;
}

bool RegisterBinkVoice(SBinkVoice *voice)
{
    if (voice == nullptr || voice->soundInterface == nullptr)
        return false;

    bool registered = false;
    try
    {
        SExclusiveSrwLockGuard registryLock(BinkVoiceRegistryLock);
        bool duplicate = false;
        for (size_t i = 0; i < BinkVoiceRegistry.size(); ++i)
        {
            SBinkVoice *const current = BinkVoiceRegistry[i];
            if (current == voice ||
                (current != nullptr &&
                 current->soundInterface == voice->soundInterface))
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
        {
            BinkVoiceRegistry.push_back(voice);
            registered = true;
        }
    }
    catch (const std::exception &error)
    { GameJournal::AllocationFailureInCatch("MiniaudioOneShotSfx");
        LOGERR("MINIAUDIO_BINK_REGISTRY_ERROR reason=std_exception detail=%s",
            error.what());
    }
    return registered;
}

void UnregisterBinkVoice(SBinkVoice *voice)
{
    if (voice == nullptr)
        return;

    AcquireSRWLockExclusive(&BinkVoiceRegistryLock);
    for (size_t i = 0; i < BinkVoiceRegistry.size(); ++i)
    {
        if (BinkVoiceRegistry[i] == voice)
        {
            BinkVoiceRegistry.erase(BinkVoiceRegistry.begin() + i);
            break;
        }
    }
    ReleaseSRWLockExclusive(&BinkVoiceRegistryLock);
}

void FinalizeDeferredBinkEngineShutdown()
{
    if (!BinkDeferredEngineShutdown || CurrentSoundManager != nullptr ||
        InterlockedCompareExchange(&BinkActiveVoiceCount, 0, 0) != 0 ||
        !EngineInitialized)
    {
        return;
    }

#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    LOGINFO("MINIAUDIO_BINK_ENGINE_RELEASE reason=deferred_sound_manager_shutdown active=0");
#endif
    EngineDiagnostics::Event("Audio engine release phase=bink_deferred manager=%p bink=0",CurrentSoundManager);
    ma_engine_uninit(&Engine);
    AudioResources::Shutdown();
    memset(&Engine, 0, sizeof(Engine));
    EngineInitialized = false;
    InitAttempted = false;
    LastInitAttemptTick = 0;
    LastDeviceRecoveryAttemptTick = 0;
    LastDeviceState = ma_device_state_uninitialized;
    BinkDeferredEngineShutdown = false;
}

void DestroyBinkVoice(SBinkVoice *voice, const char *reason)
{
    if (voice == nullptr)
        return;

    const char *readyWaitBackend = GetConfiguredBinkReadyWaitBackend(voice);
    LONG active = InterlockedCompareExchange(&BinkActiveVoiceCount, 0, 0);
    if (voice->registered)
    {
        UnregisterBinkVoice(voice);
        active = InterlockedDecrement(&BinkActiveVoiceCount);
        voice->registered = false;
    }
    if (voice->soundInitialized)
    {
        ma_sound_stop(&voice->sound);
        ma_sound_uninit(&voice->sound);
        voice->soundInitialized = false;
    }
    if (voice->ringInitialized)
    {
        ma_pcm_rb_uninit(&voice->ring);
        voice->ringInitialized = false;
    }
    CloseBinkReadyWaitTimer(voice);
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    LOGINFO("MINIAUDIO_BINK_CLOSE seq=%d reason=%s bytes=%I64u ready_calls=%u ready_accepts=%u lock_calls=%u unlock_calls=%u ready_backpressure=%d precise_wait=%d wait_backend=%s backpressure_waits=%u backpressure_requested_us=%I64u backpressure_elapsed_us=%I64u backpressure_max_wait_us=%I64u high_res_waits=%u fallback_yields=%u wait_failures=%u timer_create_error=%lu last_wait_error=%lu max_no_progress_streak=%u empty_ring_probes=%u max_empty_ring_streak=%u active=%ld",
            voice->sequence,
            reason != nullptr ? reason : "close",
            voice->committedBytes,
            voice->readyCalls,
            voice->readyAccepts,
            voice->lockCalls,
            voice->unlockCalls,
            ModAudioSettings().JMVMiniaudioBinkReadyBackpressurePrototype ? 1 : 0,
            ModAudioSettings().JMVMiniaudioBinkPreciseWaitPrototype ? 1 : 0,
            readyWaitBackend,
            voice->readyBackpressureWaits,
            voice->readyBackpressureRequestedUs,
            voice->readyBackpressureElapsedUs,
            voice->readyBackpressureMaxElapsedUs,
            voice->readyBackpressureHighResolutionWaits,
            voice->readyBackpressureFallbackYields,
            voice->readyBackpressureWaitFailures,
            static_cast<unsigned long>(voice->readyWaitTimerCreateError),
            static_cast<unsigned long>(voice->readyWaitLastError),
            voice->readyMaxNoProgressStreak,
            voice->readyEmptyRingProbes,
            voice->readyMaxEmptyRingStreak,
            active);
#endif
    delete voice;
    FinalizeDeferredBinkEngineShutdown();
}

int OpenBinkMilesFallback(
    SBinkSoundInterface *soundInterface,
    uint32_t frequency,
    int bits,
    int channels,
    uint32_t flags,
    void *bink,
    const char *phase,
    const char *reason)
{
    if (soundInterface == nullptr)
        return 0;

    const BinkTrackOpenProc fallback = BinkMilesTrackOpen;
    const int result = fallback != nullptr
        ? fallback(soundInterface, frequency, bits, channels, flags, bink)
        : 0;
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    if (ShouldLogLimited(BinkFallbackDiagnosticCount))
    {
        LOGWARN("MINIAUDIO_BINK_FALLBACK phase=%s reason=%s result=%d route=%s total=%u",
                phase != nullptr ? phase : "unknown",
                reason != nullptr ? reason : "unknown",
                result,
                result != 0 ? "miles" : "none",
                BinkFallbackDiagnosticCount);
    }
#endif
    return result;
}

bool SwitchBinkVoiceToMiles(
    SBinkSoundInterface *soundInterface,
    SBinkVoice *voice,
    const char *phase,
    const char *reason,
    int desiredOnOff)
{
    if (soundInterface == nullptr || voice == nullptr)
        return false;

    const uint32_t frequency = voice->frequency;
    const int bits = voice->bits;
    const int channels = voice->channels;
    const uint32_t flags = voice->flags;
    void *const bink = voice->bink;
    DestroyBinkVoice(voice, "promote_to_miles");

    const int opened = OpenBinkMilesFallback(
        soundInterface,
        frequency,
        bits,
        channels,
        flags,
        bink,
        phase,
        reason);
    if (opened == 0)
        return false;

    if ((desiredOnOff == 0 || desiredOnOff == 1) &&
        soundInterface->SetOnOff != nullptr)
    {
        soundInterface->SetOnOff(soundInterface, desiredOnOff);
    }
    return true;
}

void LogBinkTrackPhase(
    int sequence,
    const char *phase,
    const char *result)
{
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    if (ShouldLogLimited(BinkTrackPhaseDiagnosticCount))
    {
        LOGINFO("MINIAUDIO_BINK_TRACK_PHASE seq=%d phase=%s result=%s total=%u",
                sequence,
                phase != nullptr ? phase : "unknown",
                result != nullptr ? result : "unknown",
                BinkTrackPhaseDiagnosticCount);
    }
#endif
}

enum EBinkStartOutcome
{
    BINK_START_NOT_NEEDED,
    BINK_START_DEFERRED,
    BINK_START_STARTED,
    BINK_START_DEVICE_FAILED,
    BINK_START_SOUND_FAILED,
};

EBinkStartOutcome TryStartBinkVoice(
    SBinkVoice *voice,
    const char *phase)
{
    if (voice == nullptr || !voice->onOff || voice->paused ||
        voice->started)
    {
        return BINK_START_NOT_NEEDED;
    }

    const ma_uint32 queuedFrames = ma_pcm_rb_available_read(&voice->ring);
    if (queuedFrames == 0)
    {
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
        if (ShouldLogLimited(BinkStartDeferredDiagnosticCount))
        {
            LOGINFO("MINIAUDIO_BINK_START_DEFER seq=%d phase=%s reason=no_pcm queued_frames=0 total=%u",
                    voice->sequence,
                    phase != nullptr ? phase : "unknown",
                    BinkStartDeferredDiagnosticCount);
        }
#endif
        return BINK_START_DEFERRED;
    }

    if (!EnsureDeviceStarted("bink_pcm_start"))
    {
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
        LOGERR("MINIAUDIO_BINK_DEVICE_ERROR phase=%s seq=%d reason=device_not_started queued_frames=%u",
               phase != nullptr ? phase : "start",
               voice->sequence,
               queuedFrames);
#endif
        return BINK_START_DEVICE_FAILED;
    }

    const ma_result result = ma_sound_start(&voice->sound);
    if (result != MA_SUCCESS)
    {
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
        LOGERR("MINIAUDIO_BINK_DEVICE_ERROR phase=%s seq=%d result=%d description=%s queued_frames=%u",
               phase != nullptr ? phase : "start",
               voice->sequence,
               result,
               ma_result_description(result),
               queuedFrames);
#endif
        return BINK_START_SOUND_FAILED;
    }

    voice->started = true;
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    LOGINFO("MINIAUDIO_BINK_START seq=%d phase=%s queued_frames=%u active=%ld miles=0",
            voice->sequence,
            phase != nullptr ? phase : "start",
            queuedFrames,
            InterlockedCompareExchange(&BinkActiveVoiceCount, 0, 0));
#endif
    return BINK_START_STARTED;
}

int __stdcall BinkMiniaudioTrackOpen(
    SBinkSoundInterface *soundInterface,
    uint32_t frequency,
    int bits,
    int channels,
    uint32_t flags,
    void *bink)
{
    const int sequence = static_cast<int>(
        InterlockedIncrement(&BinkVoiceSequence));
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    if (ShouldLogLimited(BinkTrackEntryDiagnosticCount))
    {
        LOGINFO("MINIAUDIO_BINK_TRACK_ENTRY seq=%d interface=0x%p frequency=%u bits=%d channels=%d flags=0x%08X bink=0x%p total=%u",
                sequence,
                soundInterface,
                frequency,
                bits,
                channels,
                flags,
                bink,
                BinkTrackEntryDiagnosticCount);
    }
#endif

    ma_format format = ma_format_unknown;
    if (bits == 16)
        format = ma_format_s16;
    else if (bits == 8)
        format = ma_format_u8;

    if (soundInterface == nullptr || frequency < 4000 ||
        frequency > 192000 || (channels != 1 && channels != 2) ||
        format == ma_format_unknown)
    {
        LogBinkTrackPhase(sequence, "format_validate", "rejected");
        return OpenBinkMilesFallback(
            soundInterface,
            frequency,
            bits,
            channels,
            flags,
            bink,
            "track_open",
            "unsupported_format");
    }
    LogBinkTrackPhase(sequence, "format_validate", "accepted");

    LogBinkTrackPhase(sequence, "engine_device", "begin");
    const bool engineReady = EnsureEngine();
    const bool deviceReady = engineReady &&
        EnsureDeviceStarted("bink_track_open");
    LogBinkTrackPhase(
        sequence,
        "engine_device",
        deviceReady ? "ready" : (engineReady ? "device_failed" : "engine_failed"));
    if (!deviceReady)
    {
        return OpenBinkMilesFallback(
            soundInterface,
            frequency,
            bits,
            channels,
            flags,
            bink,
            "track_open",
            "device");
    }

    LogBinkTrackPhase(sequence, "voice_allocation", "begin");
    SBinkVoice *voice = new (std::nothrow) SBinkVoice();
    if (voice == nullptr)
    {
        LogBinkTrackPhase(sequence, "voice_allocation", "failed");
        return OpenBinkMilesFallback(
            soundInterface,
            frequency,
            bits,
            channels,
            flags,
            bink,
            "track_open",
            "allocation");
    }
    LogBinkTrackPhase(sequence, "voice_allocation", "ready");

    voice->soundInterface = soundInterface;
    voice->format = format;
    voice->frequency = frequency;
    voice->bits = bits;
    voice->channels = channels;
    voice->flags = flags;
    voice->bink = bink;
    voice->sequence = sequence;
    voice->volume = 32768;
    voice->pan = 32768;
    // The retail Miles track opener initializes its output voice before it
    // publishes SetOnOff. Bink only calls SetOnOff for later explicit state
    // changes, so a freshly opened track is active by default.
    voice->onOff = true;
    voice->lastReadyState = -1;
    voice->lastReadyUnlockCalls = 0xFFFFFFFFu;
    voice->bytesPerFrame = ma_get_bytes_per_frame(
        format, static_cast<ma_uint32>(channels));
    voice->preferredFrames = 16384u / voice->bytesPerFrame;
    if (voice->preferredFrames < 256)
        voice->preferredFrames = 256;
    const ma_uint32 capacityFrames = voice->preferredFrames * 2;

    // Video PCM is charged to the same 64 MiB resource budget.
    if (!AudioResources::PrepareStreamMemory(
            static_cast<size_t>(capacityFrames) * voice->bytesPerFrame))
    {
        delete voice;
        return OpenBinkMilesFallback(soundInterface, frequency, bits, channels,
            flags, bink, "track_open", "resource_budget");
    }
    LogBinkTrackPhase(sequence, "ring_init", "begin");
    ma_result result = ma_pcm_rb_init(
        format,
        static_cast<ma_uint32>(channels),
        capacityFrames,
        nullptr,
        AudioResources::Allocator(),
        &voice->ring);
    if (result != MA_SUCCESS)
    {
        LogBinkTrackPhase(sequence, "ring_init", "failed");
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
        LOGERR("MINIAUDIO_BINK_DEVICE_ERROR phase=ring_init seq=%d result=%d description=%s",
               voice->sequence,
               result,
               ma_result_description(result));
#endif
        delete voice;
        return OpenBinkMilesFallback(
            soundInterface,
            frequency,
            bits,
            channels,
            flags,
            bink,
            "track_open",
            "ring_init");
    }
    voice->ringInitialized = true;
    ma_pcm_rb_set_sample_rate(&voice->ring, frequency);
    LogBinkTrackPhase(sequence, "ring_init", "ready");

    LogBinkTrackPhase(sequence, "sound_init", "begin");
    result = ma_sound_init_from_data_source(
        &Engine,
        reinterpret_cast<ma_data_source *>(&voice->ring),
        MA_SOUND_FLAG_NO_SPATIALIZATION,
        nullptr,
        &voice->sound);
    if (result != MA_SUCCESS)
    {
        LogBinkTrackPhase(sequence, "sound_init", "failed");
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
        LOGERR("MINIAUDIO_BINK_DEVICE_ERROR phase=sound_init seq=%d result=%d description=%s",
               voice->sequence,
               result,
               ma_result_description(result));
#endif
        ma_pcm_rb_uninit(&voice->ring);
        delete voice;
        return OpenBinkMilesFallback(
            soundInterface,
            frequency,
            bits,
            channels,
            flags,
            bink,
            "track_open",
            "sound_init");
    }
    voice->soundInitialized = true;
    ma_sound_set_spatialization_enabled(&voice->sound, MA_FALSE);
    ma_sound_set_volume(&voice->sound, 1.0f);
    ma_sound_set_pan(&voice->sound, 0.0f);
    LogBinkTrackPhase(sequence, "sound_init", "ready_waiting_for_pcm");
    InitializeBinkReadyPreciseWait(voice);

    // The exact supported retail track opener clears 0x30 dwords before
    // publishing this callback table. The ABI prefix gate prevents this
    // workspace size from being used with an unknown Bink build.
    LogBinkTrackPhase(sequence, "callback_publish", "begin");
    memset(soundInterface, 0, BINK_RETAIL_SOUND_WORKSPACE_BYTES);
    soundInterface->preferredBytes =
        voice->preferredFrames * voice->bytesPerFrame;
    if (!RegisterBinkVoice(voice))
    {
        LogBinkTrackPhase(sequence, "callback_publish", "registry_failed");
        DestroyBinkVoice(voice, "registry_failed");
        return OpenBinkMilesFallback(
            soundInterface,
            frequency,
            bits,
            channels,
            flags,
            bink,
            "track_open",
            "registry");
    }
    soundInterface->Ready = BinkMiniaudioReady;
    soundInterface->Lock = BinkMiniaudioLock;
    soundInterface->Unlock = BinkMiniaudioUnlock;
    soundInterface->Volume = BinkMiniaudioVolume;
    soundInterface->Pan = BinkMiniaudioPan;
    soundInterface->Pause = BinkMiniaudioPause;
    soundInterface->SetOnOff = BinkMiniaudioSetOnOff;
    soundInterface->Close = BinkMiniaudioClose;
    voice->registered = true;
    const LONG active = InterlockedIncrement(&BinkActiveVoiceCount);
    LogBinkTrackPhase(sequence, "callback_publish", "ready");

#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    LOGINFO("MINIAUDIO_BINK_OPEN seq=%d frequency=%u bits=%d channels=%d flags=0x%08X chunk_bytes=%u ring_bytes=%u bink=0x%p route=miniaudio initial_on=%d ready_backpressure=%d precise_wait=%d wait_backend=%s requested_us=%I64u timer_create_error=%lu active=%ld miles=0",
             voice->sequence,
            frequency,
            bits,
            channels,
            flags,
            voice->preferredFrames * voice->bytesPerFrame,
             capacityFrames * voice->bytesPerFrame,
             bink,
             voice->onOff ? 1 : 0,
             ModAudioSettings().JMVMiniaudioBinkReadyBackpressurePrototype ? 1 : 0,
             ModAudioSettings().JMVMiniaudioBinkPreciseWaitPrototype ? 1 : 0,
             GetConfiguredBinkReadyWaitBackend(voice),
             BINK_READY_PRECISE_WAIT_US,
             static_cast<unsigned long>(voice->readyWaitTimerCreateError),
             active);
#endif
    return 1;
}

BinkTrackOpenProc __stdcall BinkMiniaudioSystemOpen(uintptr_t param)
{
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    if (ShouldLogLimited(BinkSystemEntryDiagnosticCount))
    {
        LOGINFO("MINIAUDIO_BINK_SYSTEM_ENTRY param=0x%08X miles_system_open=0x%p configured_param=0x%08X total=%u",
                static_cast<unsigned int>(param),
                BinkMilesSystemOpen,
                static_cast<unsigned int>(BinkMilesSystemParam),
                BinkSystemEntryDiagnosticCount);
    }
#endif
    BinkMilesTrackOpen = BinkMilesSystemOpen != nullptr
        ? BinkMilesSystemOpen(BinkMilesSystemParam)
        : nullptr;
    const bool engineReady = EnsureEngine();
    const bool deviceReady = engineReady && EnsureDeviceStarted("bink_system_open");
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    if (ShouldLogLimited(BinkSystemDiagnosticCount))
    {
        LOGINFO("MINIAUDIO_BINK_SYSTEM engine=%d device=%d miles_fallback=%d route=%s total=%u",
                engineReady ? 1 : 0,
                deviceReady ? 1 : 0,
                BinkMilesTrackOpen != nullptr ? 1 : 0,
                deviceReady ? "miniaudio" : "miles",
                BinkSystemDiagnosticCount);
    }
#endif
    return deviceReady ? BinkMiniaudioTrackOpen : BinkMilesTrackOpen;
}

int __stdcall BinkMiniaudioReady(SBinkSoundInterface *soundInterface)
{
    SBinkVoice *voice = GetBinkVoice(soundInterface);
    if (voice == nullptr)
        return 0;
    ++voice->readyCalls;
    if (voice->locked)
    {
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
        if (ShouldLogBinkReadyState(voice, 2))
        {
            LOGINFO("MINIAUDIO_BINK_READY seq=%d result=busy reason=locked queued_frames=%u call=%u total=%u",
                    voice->sequence,
                    ma_pcm_rb_available_read(&voice->ring),
                    voice->readyCalls,
                    BinkReadyDiagnosticCount);
        }
#endif
        return 0;
    }
    ApplyBinkReadyBackpressure(voice);
    if (!EnsureDeviceStarted("bink_ready"))
    {
        if (SwitchBinkVoiceToMiles(
                soundInterface,
                voice,
                "ready",
                "device",
                voice->onOff ? 1 : 0) &&
            soundInterface->Ready != nullptr)
        {
            return soundInterface->Ready(soundInterface);
        }
        return 0;
    }
    const ma_uint32 writableFrames = ma_pcm_rb_available_write(&voice->ring);
    const ma_uint32 queuedFrames = ma_pcm_rb_available_read(&voice->ring);
    const int ready = writableFrames >= voice->preferredFrames ? 1 : 0;
    if (ready != 0)
        ++voice->readyAccepts;
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    if (voice->started && voice->onOff && !voice->paused && queuedFrames == 0)
    {
        ++voice->readyEmptyRingProbes;
        ++voice->readyEmptyRingStreak;
        if (voice->readyEmptyRingStreak > voice->readyMaxEmptyRingStreak)
            voice->readyMaxEmptyRingStreak = voice->readyEmptyRingStreak;
        const uint32_t probes = voice->readyEmptyRingProbes;
        if (probes <= 4 || (probes & (probes - 1)) == 0)
        {
            LOGWARN("MINIAUDIO_BINK_RING_EMPTY seq=%d probe=%u streak=%u max_streak=%u ready_call=%u unlocks=%u wait_backend=%s",
                    voice->sequence,
                    probes,
                    voice->readyEmptyRingStreak,
                    voice->readyMaxEmptyRingStreak,
                    voice->readyCalls,
                    voice->unlockCalls,
                    GetConfiguredBinkReadyWaitBackend(voice));
        }
    }
    else
    {
        voice->readyEmptyRingStreak = 0;
    }
    if (ShouldLogBinkReadyState(voice, ready != 0 ? 1 : 0))
    {
        LOGINFO("MINIAUDIO_BINK_READY seq=%d result=%s writable_frames=%u required_frames=%u queued_frames=%u on=%d call=%u accepted=%u total=%u",
                voice->sequence,
                ready != 0 ? "ready" : "full",
                writableFrames,
                voice->preferredFrames,
                queuedFrames,
                voice->onOff ? 1 : 0,
                voice->readyCalls,
                voice->readyAccepts,
                BinkReadyDiagnosticCount);
    }
#endif
    return ready;
}

int __stdcall BinkMiniaudioLock(
    SBinkSoundInterface *soundInterface,
    uint8_t **address,
    uint32_t *length)
{
    SBinkVoice *voice = GetBinkVoice(soundInterface);
    if (voice == nullptr || address == nullptr || length == nullptr ||
        voice->locked)
    {
        return 0;
    }
    ++voice->lockCalls;

    ma_uint32 frames = voice->preferredFrames;
    void *buffer = nullptr;
    const ma_result result = ma_pcm_rb_acquire_write(
        &voice->ring, &frames, &buffer);
    if (result != MA_SUCCESS || frames == 0 || buffer == nullptr)
        return 0;

    voice->locked = true;
    voice->lockedFrames = frames;
    *address = static_cast<uint8_t *>(buffer);
    *length = frames * voice->bytesPerFrame;
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    if (ShouldLogLimited(BinkLockDiagnosticCount))
    {
        LOGINFO("MINIAUDIO_BINK_LOCK seq=%d frames=%u bytes=%u call=%u total=%u",
                voice->sequence,
                frames,
                *length,
                voice->lockCalls,
                BinkLockDiagnosticCount);
    }
#endif
    return 1;
}

int __stdcall BinkMiniaudioUnlock(
    SBinkSoundInterface *soundInterface,
    uint32_t filledBytes)
{
    SBinkVoice *voice = GetBinkVoice(soundInterface);
    if (voice == nullptr || !voice->locked)
        return 0;
    ++voice->unlockCalls;

    const uint32_t lockedBytes = voice->lockedFrames * voice->bytesPerFrame;
    if (filledBytes > lockedBytes)
        filledBytes = lockedBytes;
    filledBytes -= filledBytes % voice->bytesPerFrame;
    const ma_uint32 filledFrames = filledBytes / voice->bytesPerFrame;
    const ma_result result = ma_pcm_rb_commit_write(
        &voice->ring, filledFrames);
    voice->locked = false;
    voice->lockedFrames = 0;
    if (result != MA_SUCCESS)
    {
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
        LOGERR("MINIAUDIO_BINK_DEVICE_ERROR phase=buffer_commit seq=%d result=%d description=%s",
               voice->sequence,
               result,
               ma_result_description(result));
#endif
        return SwitchBinkVoiceToMiles(
                   soundInterface,
                   voice,
                   "buffer_commit",
                   "ring_commit",
                   voice->onOff ? 1 : 0)
            ? 1
            : 0;
    }

    voice->committedBytes += filledBytes;
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    if (ShouldLogLimited(BinkBufferDiagnosticCount))
    {
        LOGINFO("MINIAUDIO_BINK_BUFFER seq=%d filled=%u queued_frames=%u committed_bytes=%I64u active=%ld total=%u",
                voice->sequence,
                filledBytes,
                ma_pcm_rb_available_read(&voice->ring),
                voice->committedBytes,
                InterlockedCompareExchange(&BinkActiveVoiceCount, 0, 0),
                BinkBufferDiagnosticCount);
    }
#endif

    const EBinkStartOutcome startOutcome = TryStartBinkVoice(
        voice, "first_pcm");
    if (startOutcome == BINK_START_DEVICE_FAILED ||
        startOutcome == BINK_START_SOUND_FAILED)
    {
        return SwitchBinkVoiceToMiles(
                   soundInterface,
                   voice,
                   "first_pcm",
                   startOutcome == BINK_START_DEVICE_FAILED
                       ? "device"
                       : "sound_start",
                   1)
            ? 1
            : 0;
    }
    return 1;
}

void __stdcall BinkMiniaudioVolume(
    SBinkSoundInterface *soundInterface,
    int volume)
{
    SBinkVoice *voice = GetBinkVoice(soundInterface);
    if (voice == nullptr)
        return;
    if (volume < 0)
        volume = 0;
    if (volume > 32768)
        volume = 32768;
    voice->volume = volume;
    ma_sound_set_volume(
        &voice->sound, static_cast<float>(volume) / 32768.0f);
}

void __stdcall BinkMiniaudioPan(
    SBinkSoundInterface *soundInterface,
    int pan)
{
    SBinkVoice *voice = GetBinkVoice(soundInterface);
    if (voice == nullptr)
        return;
    if (pan < 0)
        pan = 0;
    if (pan > 65536)
        pan = 65536;
    voice->pan = pan;
    ma_sound_set_pan(
        &voice->sound,
        static_cast<float>(pan - 32768) / 32768.0f);
}

int __stdcall BinkMiniaudioPause(
    SBinkSoundInterface *soundInterface,
    int status)
{
    SBinkVoice *voice = GetBinkVoice(soundInterface);
    if (voice == nullptr)
        return status;

    if (status == 1)
    {
        ma_sound_stop(&voice->sound);
        voice->paused = true;
        voice->started = false;
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
        LOGINFO("MINIAUDIO_BINK_PAUSE seq=%d queued_frames=%u",
                voice->sequence,
                ma_pcm_rb_available_read(&voice->ring));
#endif
        return status;
    }

    voice->paused = false;
    const EBinkStartOutcome startOutcome = TryStartBinkVoice(
        voice, "resume");
    if (startOutcome == BINK_START_DEVICE_FAILED ||
        startOutcome == BINK_START_SOUND_FAILED)
    {
        if (SwitchBinkVoiceToMiles(
                soundInterface,
                voice,
                "resume",
                startOutcome == BINK_START_DEVICE_FAILED
                    ? "device"
                    : "sound_start",
                1) &&
            soundInterface->Pause != nullptr)
        {
            return soundInterface->Pause(soundInterface, status);
        }
        return status;
    }
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    LOGINFO("MINIAUDIO_BINK_RESUME seq=%d on=%d started=%d queued_frames=%u",
            voice->sequence,
            voice->onOff ? 1 : 0,
            voice->started ? 1 : 0,
            ma_pcm_rb_available_read(&voice->ring));
#endif
    return status;
}

int __stdcall BinkMiniaudioSetOnOff(
    SBinkSoundInterface *soundInterface,
    int status)
{
    SBinkVoice *voice = GetBinkVoice(soundInterface);
    if (voice == nullptr)
        return 0;

    if (status == 1)
    {
        voice->onOff = true;
        voice->paused = false;
        const EBinkStartOutcome startOutcome = TryStartBinkVoice(
            voice, "set_on_off");
        if (startOutcome == BINK_START_DEVICE_FAILED ||
            startOutcome == BINK_START_SOUND_FAILED)
        {
            return SwitchBinkVoiceToMiles(
                       soundInterface,
                       voice,
                       "set_on_off",
                       startOutcome == BINK_START_DEVICE_FAILED
                           ? "device"
                           : "sound_start",
                       1)
                ? 1
                : 0;
        }
        return 1;
    }
    if (status == 0)
    {
        ma_sound_stop(&voice->sound);
        voice->started = false;
        voice->onOff = false;
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
        LOGINFO("MINIAUDIO_BINK_STOP seq=%d queued_frames=%u",
                voice->sequence,
                ma_pcm_rb_available_read(&voice->ring));
#endif
        return 0;
    }
    return voice->onOff ? 1 : 0;
}

void __stdcall BinkMiniaudioClose(SBinkSoundInterface *soundInterface)
{
    DestroyBinkVoice(GetBinkVoice(soundInterface), "bink_close");
}
} // namespace

bool IsSupportedBinkAudioAbi(
    const void *setSoundSystem,
    const void *milesSystemOpen)
{
    static const uint8_t setSoundSystemPrefix[] = {
        0x8B, 0x4C, 0x24, 0x04, 0x85, 0xC9, 0x75,
        0x05, 0x33, 0xC0, 0xC2, 0x08, 0x00,
    };
    static const uint8_t milesSystemOpenPrefix[] = {
        0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0,
        0x75, 0x03, 0xC2, 0x04, 0x00,
    };
    return setSoundSystem != nullptr && milesSystemOpen != nullptr &&
        memcmp(setSoundSystem,
               setSoundSystemPrefix,
               sizeof(setSoundSystemPrefix)) == 0 &&
        memcmp(milesSystemOpen,
               milesSystemOpenPrefix,
               sizeof(milesSystemOpenPrefix)) == 0;
}

void ConfigureBinkMilesFallback(void *milesSystemOpen, uintptr_t param)
{
    BinkMilesSystemOpen = reinterpret_cast<BinkSystemOpenProc>(
        milesSystemOpen);
    BinkMilesSystemParam = param;
}

void *GetBinkMiniaudioSystemOpen()
{
    return reinterpret_cast<void *>(BinkMiniaudioSystemOpen);
}

unsigned int CountActiveBinkVoices()
{
    return static_cast<unsigned int>(InterlockedCompareExchange(
        &BinkActiveVoiceCount, 0, 0));
}

void SetVirtualDynamicGainResolver(VirtualDynamicGainResolver resolver)
{
    DynamicGainResolver = resolver;
}

void RecordMissingAudioResource(
    const char *requestedName,
    const char *domain,
    const char *targetName,
    const char *path,
    const char *soundClass,
    const char *reason)
{
    if (!(ModAudioSettings().JMVMiniaudioMissingAudioCatalogDiagnostic || false))
        return;

    std::string exactRequest;
    std::string canonicalRequest;
    std::string exactTarget;
    std::string canonicalTarget;
    std::string exactDomain;
    std::string canonicalDomain;
    std::string exactPath;
    std::string ignoredCanonical;
    std::string exactClass;
    std::string exactReason;
    try
    {
        if (!CaptureMissingAudioText(
                requestedName, exactRequest, canonicalRequest))
            return;
        if (!CaptureMissingAudioText(
                targetName != nullptr ? targetName : requestedName,
                exactTarget,
                canonicalTarget))
            return;
        CaptureMissingAudioText(
            domain != nullptr ? domain : "other",
            exactDomain,
            canonicalDomain);
        CaptureMissingAudioText(
            path != nullptr ? path : "other",
            exactPath,
            ignoredCanonical);
        CaptureMissingAudioText(
            soundClass != nullptr ? soundClass : "other",
            exactClass,
            ignoredCanonical);
        CaptureMissingAudioText(
            reason != nullptr ? reason : "not_found",
            exactReason,
            ignoredCanonical);
    }
    catch (const std::exception &)
    { GameJournal::AllocationFailureInCatch("MiniaudioOneShotSfx");
        LOGERR("MINIAUDIO_MISSING_AUDIO_UNTRACKED\tphase=normalize\treason=std_exception\ttext_omitted=1");
        /* OutputDebugString is unbuffered. */
        return;
    }

    if (canonicalDomain == "stream_music" && canonicalRequest == "none")
        return;

    const unsigned int pathMask = MissingAudioPathMask(exactPath.c_str());
    const unsigned int classMask = MissingAudioClassMask(exactClass.c_str());
    const DWORD now = GetTickCount();
    SMissingAudioRecord loggedRecord = {};
    size_t loggedRecordId = 0;
    size_t loggedUniqueCount = 0;
    size_t loggedAliasCount = 0;
    uint64_t loggedTotalRequests = 0;
    bool newRecord = false;
    bool newContext = false;
    bool newAlias = false;
    std::string loggedAlias;

    try
    {
        SExclusiveSrwLockGuard lock(MissingAudioCatalogLock);
        ++MissingAudioTotalRequests;
        ++MissingAudioCatalogRevision;
        const std::string key = canonicalDomain + "\x1f" + canonicalTarget;
        size_t recordIndex = MissingAudioRecords.size();
        for (size_t i = 0; i < MissingAudioRecords.size(); ++i)
        {
            if (MissingAudioRecords[i].key == key)
            {
                recordIndex = i;
                break;
            }
        }

        if (recordIndex < MissingAudioRecords.size())
        {
            loggedRecordId = recordIndex + 1;
            SMissingAudioRecord &record = MissingAudioRecords[recordIndex];
            const unsigned int previousPathMask = record.pathMask;
            const unsigned int previousClassMask = record.classMask;
            ++record.count;
            record.lastTick = now;
            record.pathMask |= pathMask;
            record.classMask |= classMask;
            bool aliasKnown = false;
            for (size_t i = 0; i < record.canonicalAliases.size(); ++i)
            {
                if (record.canonicalAliases[i] == canonicalRequest)
                {
                    aliasKnown = true;
                    break;
                }
            }
            if (!aliasKnown)
            {
                record.requestedAliases.push_back(exactRequest);
                record.canonicalAliases.push_back(canonicalRequest);
                loggedAlias = exactRequest;
                newAlias = true;
            }
            newContext = previousPathMask != record.pathMask ||
                previousClassMask != record.classMask;
            if (newContext || newAlias)
                loggedRecord = record;
        }
        else
        {
            SMissingAudioRecord record = {};
            record.key = key;
            record.canonicalTarget = canonicalTarget;
            record.requestedName = exactRequest;
            record.targetName = exactTarget;
            record.domain = exactDomain;
            record.firstPath = exactPath;
            record.firstClass = exactClass;
            record.reason = exactReason;
            record.requestedAliases.push_back(exactRequest);
            record.canonicalAliases.push_back(canonicalRequest);
            record.count = 1;
            record.firstTick = now;
            record.lastTick = now;
            record.pathMask = pathMask;
            record.classMask = classMask;
            MissingAudioRecords.push_back(record);
            loggedRecordId = MissingAudioRecords.size();
            loggedRecord = record;
            newRecord = true;
        }
        loggedUniqueCount = MissingAudioRecords.size();
        loggedAliasCount = loggedRecord.requestedAliases.size();
        loggedTotalRequests = MissingAudioTotalRequests;
    }
    catch (const std::exception &)
    { GameJournal::AllocationFailureInCatch("MiniaudioOneShotSfx");
        LOGERR("MINIAUDIO_MISSING_AUDIO_UNTRACKED\tphase=record\treason=std_exception\ttext_omitted=1");
        /* OutputDebugString is unbuffered. */
        return;
    }

    if (newRecord)
    {
        try
        {
            SExclusiveSrwLockGuard logLock(MissingAudioLogLock);
            std::string logDomain;
            std::string logRequest;
            std::string logTarget;
            std::string logCanonicalTarget;
            std::string logPath;
            std::string logClass;
            std::string logReason;
            EscapeMissingAudioLogText(loggedRecord.domain, logDomain);
            EscapeMissingAudioLogText(loggedRecord.requestedName, logRequest);
            EscapeMissingAudioLogText(loggedRecord.targetName, logTarget);
            EscapeMissingAudioLogText(
                loggedRecord.canonicalTarget, logCanonicalTarget);
            EscapeMissingAudioLogText(loggedRecord.firstPath, logPath);
            EscapeMissingAudioLogText(loggedRecord.firstClass, logClass);
            EscapeMissingAudioLogText(loggedRecord.reason, logReason);
            LOGINFO("MINIAUDIO_MISSING_AUDIO_NEW\tv=2\tpid=%lu\trecord=%llu\tcount=%llu\tunique=%llu\ttotal=%llu\talias_count=%llu\tpath_mask=0x%08X\tclass_mask=0x%08X\tfirst_tick=%lu\tlast_tick=%lu\tfields=7\tfield_encoding=escaped_chunks",
                    static_cast<unsigned long>(GetCurrentProcessId()),
                    static_cast<unsigned long long>(loggedRecordId),
                    static_cast<unsigned long long>(loggedRecord.count),
                    static_cast<unsigned long long>(loggedUniqueCount),
                    static_cast<unsigned long long>(loggedTotalRequests),
                    static_cast<unsigned long long>(loggedAliasCount),
                    loggedRecord.pathMask,
                    loggedRecord.classMask,
                    static_cast<unsigned long>(loggedRecord.firstTick),
                    static_cast<unsigned long>(loggedRecord.lastTick));
            EmitMissingAudioLogField(
                "new", loggedRecordId, "domain", logDomain);
            EmitMissingAudioLogField(
                "new", loggedRecordId, "requested", logRequest);
            EmitMissingAudioLogField(
                "new", loggedRecordId, "target", logTarget);
            EmitMissingAudioLogField(
                "new", loggedRecordId, "canonical_target", logCanonicalTarget);
            EmitMissingAudioLogField(
                "new", loggedRecordId, "path", logPath);
            EmitMissingAudioLogField(
                "new", loggedRecordId, "class", logClass);
            EmitMissingAudioLogField(
                "new", loggedRecordId, "reason", logReason);
            /* OutputDebugString is unbuffered. */
        }
        catch (const std::exception &)
        { GameJournal::AllocationFailureInCatch("MiniaudioOneShotSfx");
            LOGERR("MINIAUDIO_MISSING_AUDIO_UNTRACKED\tphase=emit_new\treason=std_exception\trecord=%llu\ttext_omitted=1",
                   static_cast<unsigned long long>(loggedRecordId));
            /* OutputDebugString is unbuffered. */
        }
    }
    if (!newRecord && newAlias)
    {
        try
        {
            SExclusiveSrwLockGuard logLock(MissingAudioLogLock);
            std::string logAlias;
            EscapeMissingAudioLogText(loggedAlias, logAlias);
            LOGINFO("MINIAUDIO_MISSING_AUDIO_ALIAS\tv=2\tpid=%lu\trecord=%llu\talias_count=%llu\tfields=1\tfield_encoding=escaped_chunks",
                    static_cast<unsigned long>(GetCurrentProcessId()),
                    static_cast<unsigned long long>(loggedRecordId),
                    static_cast<unsigned long long>(loggedAliasCount));
            EmitMissingAudioLogField(
                "alias", loggedRecordId, "requested", logAlias);
            /* OutputDebugString is unbuffered. */
        }
        catch (const std::exception &)
        { GameJournal::AllocationFailureInCatch("MiniaudioOneShotSfx");
            LOGERR("MINIAUDIO_MISSING_AUDIO_UNTRACKED\tphase=emit_alias\treason=std_exception\trecord=%llu\ttext_omitted=1",
                   static_cast<unsigned long long>(loggedRecordId));
            /* OutputDebugString is unbuffered. */
        }
    }

    if (!newRecord && newContext)
    {
        try
        {
            SExclusiveSrwLockGuard logLock(MissingAudioLogLock);
            std::string logDomain;
            std::string logRequest;
            std::string logTarget;
            std::string logPath;
            std::string logClass;
            EscapeMissingAudioLogText(loggedRecord.domain, logDomain);
            EscapeMissingAudioLogText(exactRequest, logRequest);
            EscapeMissingAudioLogText(exactTarget, logTarget);
            EscapeMissingAudioLogText(exactPath, logPath);
            EscapeMissingAudioLogText(exactClass, logClass);
            LOGINFO("MINIAUDIO_MISSING_AUDIO_CONTEXT\tv=2\tpid=%lu\trecord=%llu\tcount=%llu\tpath_mask=0x%08X\tclass_mask=0x%08X\tlast_tick=%lu\tfields=5\tfield_encoding=escaped_chunks",
                    static_cast<unsigned long>(GetCurrentProcessId()),
                    static_cast<unsigned long long>(loggedRecordId),
                    static_cast<unsigned long long>(loggedRecord.count),
                    loggedRecord.pathMask,
                    loggedRecord.classMask,
                    static_cast<unsigned long>(loggedRecord.lastTick));
            EmitMissingAudioLogField(
                "context", loggedRecordId, "domain", logDomain);
            EmitMissingAudioLogField(
                "context", loggedRecordId, "requested", logRequest);
            EmitMissingAudioLogField(
                "context", loggedRecordId, "target", logTarget);
            EmitMissingAudioLogField(
                "context", loggedRecordId, "path", logPath);
            EmitMissingAudioLogField(
                "context", loggedRecordId, "class", logClass);
            /* OutputDebugString is unbuffered. */
        }
        catch (const std::exception &)
        { GameJournal::AllocationFailureInCatch("MiniaudioOneShotSfx");
            LOGERR("MINIAUDIO_MISSING_AUDIO_UNTRACKED\tphase=emit_context\treason=std_exception\trecord=%llu\ttext_omitted=1",
                   static_cast<unsigned long long>(loggedRecordId));
            /* OutputDebugString is unbuffered. */
        }
    }
}

void DumpMissingAudioCatalog(const char *reason)
{
    if (!(ModAudioSettings().JMVMiniaudioMissingAudioCatalogDiagnostic || false))
        return;

    std::vector<SMissingAudioRecord> snapshot;
    uint64_t totalRequests = 0;
    uint64_t revision = 0;
    try
    {
        SExclusiveSrwLockGuard lock(MissingAudioCatalogLock);
        revision = MissingAudioCatalogRevision;
        if (revision == MissingAudioLastDumpRevision)
            return;
        snapshot = MissingAudioRecords;
        totalRequests = MissingAudioTotalRequests;
        MissingAudioLastDumpRevision = revision;
    }
    catch (const std::exception &)
    { GameJournal::AllocationFailureInCatch("MiniaudioOneShotSfx");
        LOGERR("MINIAUDIO_MISSING_AUDIO_UNTRACKED\tphase=snapshot\treason=std_exception");
        /* OutputDebugString is unbuffered. */
        return;
    }

    std::string escapedDumpReason;
    const char *escapedDumpReasonText = "escape_failed";
    try
    {
        EscapeMissingAudioLogText(
            std::string(reason != nullptr ? reason : "unknown"),
            escapedDumpReason);
        escapedDumpReasonText = escapedDumpReason.c_str();
    }
    catch (const std::exception &)
    { GameJournal::AllocationFailureInCatch("MiniaudioOneShotSfx");
        LOGERR("MINIAUDIO_MISSING_AUDIO_UNTRACKED\tphase=snapshot_reason\treason=std_exception");
        /* OutputDebugString is unbuffered. */
    }

    SExclusiveSrwLockGuard logLock(MissingAudioLogLock);
    LOGINFO("MINIAUDIO_MISSING_AUDIO_CATALOG_BEGIN\tv=2\tpid=%lu\treason=%s\tunique=%llu\ttotal=%llu\tstorage=dynamic\tfixed_limit=0\trevision=%llu\tfield_encoding=escaped_chunks",
            static_cast<unsigned long>(GetCurrentProcessId()),
            escapedDumpReasonText,
            static_cast<unsigned long long>(snapshot.size()),
            static_cast<unsigned long long>(totalRequests),
            static_cast<unsigned long long>(revision));
    for (size_t i = 0; i < snapshot.size(); ++i)
    {
        const SMissingAudioRecord &record = snapshot[i];
        try
        {
            std::string logDomain;
            std::string logRequest;
            std::string logTarget;
            std::string logCanonicalTarget;
            std::string logPath;
            std::string logClass;
            std::string logReason;
            EscapeMissingAudioLogText(record.domain, logDomain);
            EscapeMissingAudioLogText(record.requestedName, logRequest);
            EscapeMissingAudioLogText(record.targetName, logTarget);
            EscapeMissingAudioLogText(
                record.canonicalTarget, logCanonicalTarget);
            EscapeMissingAudioLogText(record.firstPath, logPath);
            EscapeMissingAudioLogText(record.firstClass, logClass);
            EscapeMissingAudioLogText(record.reason, logReason);
            const size_t aliasCount = record.requestedAliases.size();
            const size_t fieldCount = 7 + (aliasCount > 0 ? aliasCount - 1 : 0);
            LOGINFO("MINIAUDIO_MISSING_AUDIO_CATALOG_ENTRY\tv=2\tpid=%lu\tindex=%llu\tcount=%llu\talias_count=%llu\tpath_mask=0x%08X\tclass_mask=0x%08X\tfirst_tick=%lu\tlast_tick=%lu\tfields=%llu\tfield_encoding=escaped_chunks",
                    static_cast<unsigned long>(GetCurrentProcessId()),
                    static_cast<unsigned long long>(i + 1),
                    static_cast<unsigned long long>(record.count),
                    static_cast<unsigned long long>(aliasCount),
                    record.pathMask,
                    record.classMask,
                    static_cast<unsigned long>(record.firstTick),
                    static_cast<unsigned long>(record.lastTick),
                    static_cast<unsigned long long>(fieldCount));
            EmitMissingAudioLogField(
                "snapshot", i + 1, "domain", logDomain);
            EmitMissingAudioLogField(
                "snapshot", i + 1, "requested", logRequest);
            EmitMissingAudioLogField(
                "snapshot", i + 1, "target", logTarget);
            EmitMissingAudioLogField(
                "snapshot", i + 1, "canonical_target", logCanonicalTarget);
            EmitMissingAudioLogField(
                "snapshot", i + 1, "path", logPath);
            EmitMissingAudioLogField(
                "snapshot", i + 1, "class", logClass);
            EmitMissingAudioLogField(
                "snapshot", i + 1, "reason", logReason);
            for (size_t aliasIndex = 1;
                 aliasIndex < record.requestedAliases.size();
                 ++aliasIndex)
            {
                std::string logAlias;
                EscapeMissingAudioLogText(
                    record.requestedAliases[aliasIndex], logAlias);
                char aliasFieldName[64] = {};
                sprintf_s(
                    aliasFieldName,
                    sizeof(aliasFieldName),
                    "requested_alias_%llu",
                    static_cast<unsigned long long>(aliasIndex + 1));
                EmitMissingAudioLogField(
                    "snapshot", i + 1, aliasFieldName, logAlias);
            }
        }
        catch (const std::exception &)
        { GameJournal::AllocationFailureInCatch("MiniaudioOneShotSfx");
            LOGERR("MINIAUDIO_MISSING_AUDIO_UNTRACKED\tphase=snapshot_entry\treason=std_exception\tindex=%llu",
                   static_cast<unsigned long long>(i + 1));
        }
    }
    LOGINFO("MINIAUDIO_MISSING_AUDIO_CATALOG_END\tv=2\tpid=%lu\treason=%s\tunique=%llu\ttotal=%llu\trevision=%llu",
            static_cast<unsigned long>(GetCurrentProcessId()),
            escapedDumpReasonText,
            static_cast<unsigned long long>(snapshot.size()),
            static_cast<unsigned long long>(totalRequests),
            static_cast<unsigned long long>(revision));
    /* OutputDebugString is unbuffered. */
}

bool CaptureEncoded(
    const char *soundName, const void *data, uint32_t size, const char *source,
    uintptr_t providerIdentity)
{
    (void)source;
    return AudioResources::Capture(soundName, data, size, providerIdentity);
}

bool IsCaptured(const char *soundName)
{
    return AudioResources::Contains(soundName);
}

const char *LastStartReason()
{
    return LastStartReasonText;
}

EStartResult TryStartFlat(
    void *soundManager,
    const char *soundName,
    int priority,
    int percentVolume,
    int percentPan,
    float gainFactor,
    float pitchFactor)
{
    if (!IsManagerReady(soundManager) || soundName == nullptr ||
        percentVolume <= 0)
    {
        SetLastStartReason(!IsManagerReady(soundManager)
                               ? "manager_not_ready"
                               : (soundName == nullptr ? "name_invalid"
                                                       : "volume_zero"));
        return START_NOT_HANDLED;
    }
    return StartDecoded(
        soundManager,
        soundName,
        priority,
        false,
        nullptr,
        0.0f,
        0.0f,
        percentVolume,
        percentPan,
        gainFactor,
        pitchFactor);
}

EStartResult TryStart3D(
    void *soundManager,
    const char *soundName,
    int priority,
    const void *source,
    float minDistance,
    float maxDistance,
    float gainFactor,
    float pitchFactor)
{
    if (!IsManagerReady(soundManager) || soundName == nullptr || source == nullptr ||
        !_finite(minDistance) || !_finite(maxDistance) ||
        minDistance < 0.0f || maxDistance < minDistance)
    {
        SetLastStartReason(!IsManagerReady(soundManager)
                               ? "manager_not_ready"
                               : (soundName == nullptr
                                      ? "name_invalid"
                                      : (source == nullptr ? "source_invalid"
                                                           : "distance_invalid")));
        return START_NOT_HANDLED;
    }
    const float *position = static_cast<const float *>(source);
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    if (maxDistance == minDistance &&
        ShouldLogLimited(EqualRangeDiagnosticCount))
    {
        LOGINFO("MINIAUDIO_ONESHOT_RANGE_EQUAL name=%s radius=%.3f mode=hard_cutoff total=%u",
                soundName,
                maxDistance,
                EqualRangeDiagnosticCount);
    }
#endif
    if (!_finite(position[0]) || !_finite(position[1]) || !_finite(position[2]))
    {
        SetLastStartReason("position_invalid");
        return START_NOT_HANDLED;
    }
    const uint8_t *manager = static_cast<const uint8_t *>(soundManager);
    const float deltaX = position[0] -
        *reinterpret_cast<const float *>(manager + SOUND_MANAGER_LISTENER_X_OFFSET);
    const float deltaY = position[1] -
        *reinterpret_cast<const float *>(manager + SOUND_MANAGER_LISTENER_Y_OFFSET);
    if (deltaX * deltaX + deltaY * deltaY > maxDistance * maxDistance)
    {
        SetLastStartReason("distance");
        return START_SUPPRESSED;
    }
    return StartDecoded(
        soundManager,
        soundName,
        priority,
        true,
        position,
        minDistance,
        maxDistance,
        100,
        0,
        gainFactor,
        pitchFactor);
}

EStartResult TryStartManaged(
    void *soundManager,
    void *fxControl,
    void *milesSample,
    const char *soundName,
    int priority,
    int finalVolume,
    int percentPan,
    int playbackRate,
    bool looping,
    bool speech)
{
    if (!IsManagerReady(soundManager) || fxControl == nullptr ||
        milesSample == nullptr || soundName == nullptr)
    {
        SetLastStartReason(!IsManagerReady(soundManager)
                               ? "manager_not_ready"
                               : (fxControl == nullptr
                                      ? "fx_control_invalid"
                                      : (milesSample == nullptr
                                             ? "sample_invalid"
                                             : "name_invalid")));
        return START_NOT_HANDLED;
    }

    AudioResources::Handle resource = AudioResources::Pin(soundName);
    const uint32_t sampleRate = AudioResources::SampleRate(resource);
    const float pitchFactor = playbackRate > 0 && sampleRate > 0
        ? static_cast<float>(playbackRate) / sampleRate : 1.0f;
    AudioResources::Unpin(resource);
    const uint8_t *fx = static_cast<const uint8_t *>(fxControl);
    const uint32_t uniqueId = *reinterpret_cast<const uint32_t *>(
        fx + FX_UNIQUE_ID_OFFSET);
    return StartDecoded(
        soundManager,
        soundName,
        priority,
        false,
        nullptr,
        0.0f,
        0.0f,
        finalVolume,
        percentPan,
        1.0f,
        pitchFactor,
        true,
        fxControl,
        milesSample,
        uniqueId,
        looping,
        speech);
}

EStartResult TryStartVirtualFlat(
    void *soundManager,
    uint32_t virtualId,
    const char *soundName,
    int priority,
    int percentVolume,
    int percentPan,
    bool looping,
    bool affectedWithHeight,
    bool speech,
    bool dialog,
    float gainFactor,
    float pitchFactor,
    const PartyVoiceBinding *partyVoice,
    bool scenarioSpeech)
{
    if (!IsManagerReady(soundManager) || virtualId == 0xFFFFFFFFu ||
        soundName == nullptr || FindVirtualVoice(soundManager, virtualId) != nullptr)
    {
        SetLastStartReason(!IsManagerReady(soundManager)
                               ? "manager_not_ready"
                               : (soundName == nullptr
                                      ? "name_invalid"
                                      : "virtual_id_invalid"));
        return START_NOT_HANDLED;
    }
    if (!looping && percentVolume <= 0)
    {
        SetLastStartReason("volume_zero");
        return START_SUPPRESSED;
    }

    SVirtualStartInfo virtualStart = {};
    virtualStart.soundManager = soundManager;
    virtualStart.id = virtualId;
    virtualStart.manualVolume = percentVolume;
    virtualStart.percentPan = percentPan;
    virtualStart.gainFactor = gainFactor;
    virtualStart.manualVolumes = true;
    virtualStart.affectedWithHeight = affectedWithHeight;
    virtualStart.dialog = dialog;
    virtualStart.partyVoice = partyVoice;
    virtualStart.scenarioSpeech = scenarioSpeech;
    return StartDecoded(
        soundManager,
        soundName,
        priority,
        false,
        nullptr,
        0.0f,
        0.0f,
        100,
        percentPan,
        1.0f,
        pitchFactor,
        false,
        nullptr,
        nullptr,
        0,
        looping,
        speech,
        &virtualStart);
}

EStartResult TryStartVirtual3D(
    void *soundManager,
    uint32_t virtualId,
    const char *soundName,
    int priority,
    const void *source,
    float minDistance,
    float maxDistance,
    bool looping,
    bool speech,
    float gainFactor,
    float pitchFactor)
{
    if (!IsManagerReady(soundManager) || virtualId == 0xFFFFFFFFu ||
        soundName == nullptr || source == nullptr ||
        !_finite(minDistance) || !_finite(maxDistance) ||
        minDistance < 0.0f || maxDistance < minDistance ||
        FindVirtualVoice(soundManager, virtualId) != nullptr)
    {
        SetLastStartReason(!IsManagerReady(soundManager)
                               ? "manager_not_ready"
                               : (soundName == nullptr
                                      ? "name_invalid"
                                      : (source == nullptr
                                             ? "source_invalid"
                                             : "distance_or_id_invalid")));
        return START_NOT_HANDLED;
    }
    const float *position = static_cast<const float *>(source);
    if (!_finite(position[0]) || !_finite(position[1]) ||
        !_finite(position[2]))
    {
        SetLastStartReason("position_invalid");
        return START_NOT_HANDLED;
    }
    if (!looping)
    {
        const uint8_t *manager = static_cast<const uint8_t *>(soundManager);
        const float deltaX = position[0] -
            *reinterpret_cast<const float *>(
                manager + SOUND_MANAGER_LISTENER_X_OFFSET);
        const float deltaY = position[1] -
            *reinterpret_cast<const float *>(
                manager + SOUND_MANAGER_LISTENER_Y_OFFSET);
        if (deltaX * deltaX + deltaY * deltaY >
            maxDistance * maxDistance)
        {
            SetLastStartReason("distance");
            return START_SUPPRESSED;
        }
    }

    SVirtualStartInfo virtualStart = {};
    virtualStart.soundManager = soundManager;
    virtualStart.id = virtualId;
    virtualStart.manualVolume = 100;
    virtualStart.percentPan = 0;
    virtualStart.gainFactor = gainFactor;
    virtualStart.manualVolumes = false;
    virtualStart.affectedWithHeight = true;
    virtualStart.dialog = false;
    return StartDecoded(
        soundManager,
        soundName,
        priority,
        true,
        position,
        minDistance,
        maxDistance,
        100,
        0,
        1.0f,
        pitchFactor,
        false,
        nullptr,
        nullptr,
        0,
        looping,
        speech,
        &virtualStart);
}

uint32_t CaptureVoiceWait(void *soundManager, uint32_t soundId)
{
    return VoiceLifetimes.Capture(reinterpret_cast<uintptr_t>(soundManager), soundId);
}

bool IsVoiceWaitActive(uint32_t ticket)
{
    return VoiceLifetimes.Active(ticket);
}

bool IsVirtualSound(void *soundManager, uint32_t virtualId)
{
    return FindVirtualVoice(soundManager, virtualId) != nullptr;
}

bool UpdateVirtualFlat(
    void *soundManager,
    uint32_t virtualId,
    int percentVolume,
    int percentPan)
{
    SVoice *voice = FindVirtualVoice(soundManager, virtualId);
    if (voice == nullptr)
        return false;
    voice->virtualManualVolumes = true;
    voice->virtualManualVolume = percentVolume;
    voice->virtualPercentPan = percentPan;
    voice->spatial = false;
    ma_sound_set_spatialization_enabled(&voice->sound, MA_FALSE);
    UpdateVirtualVoiceBinaural(*voice);
    ResetVoiceOcclusion(*voice, GetTickCount());
    ApplyVirtualVoiceMix(*voice);
    return true;
}

bool UpdateVirtual3D(
    void *soundManager,
    uint32_t virtualId,
    const void *source,
    float minDistance,
    float maxDistance)
{
    SVoice *voice = FindVirtualVoice(soundManager, virtualId);
    if (voice == nullptr || source == nullptr || !_finite(minDistance) ||
        !_finite(maxDistance) || minDistance < 0.0f ||
        maxDistance < minDistance)
    {
        return false;
    }
    const float *position = static_cast<const float *>(source);
    if (!_finite(position[0]) || !_finite(position[1]) ||
        !_finite(position[2]))
    {
        return false;
    }
    voice->virtualManualVolumes = false;
    voice->virtualAffectedWithHeight = true;
    voice->spatial = true;
    const bool becameWorldSfx = !voice->worldSfx &&
        WorldSfxPause::Applies(true, voice->speech, voice->dialog);
    voice->worldSfx = voice->worldSfx || becameWorldSfx;
    voice->virtualPosition[0] = position[0];
    voice->virtualPosition[1] = position[1];
    voice->virtualPosition[2] = position[2];
    voice->virtualMinDistance = minDistance;
    voice->virtualMaxDistance = maxDistance;
    ma_sound_set_spatialization_enabled(&voice->sound, MA_TRUE);
    ma_sound_set_position(
        &voice->sound, position[0], position[1], position[2]);
    ma_sound_set_min_distance(&voice->sound, minDistance);
    ma_sound_set_max_distance(&voice->sound, maxDistance);
    UpdateVirtualVoiceBinaural(*voice);
    UpdateVirtualVoiceOcclusion(*voice, GetTickCount());
    ApplyVirtualVoiceMix(*voice);
    if (becameWorldSfx)
        SyncWorldSfxPause();
    return true;
}

bool CancelVirtual(void *soundManager, uint32_t virtualId)
{
    SVoice *voice = FindVirtualVoice(soundManager, virtualId);
    if (voice == nullptr)
        return false;
    ReleaseVoice(*voice, "virtual_cancel");
    return true;
}

float GetVirtualPlaybackTime(void *soundManager, uint32_t virtualId)
{
    SVoice *voice = FindVirtualVoice(soundManager, virtualId);
    return voice != nullptr ? AudioResources::Duration(voice->resource) : 0.0f;
}

unsigned int CountAudibleVirtualSpeech(void *soundManager)
{
    unsigned int count = 0;
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
    {
        const SVoice &voice = Voices[i];
        if (!voice.active || !voice.virtualManaged || !voice.speech ||
            voice.virtualSoundManager != soundManager ||
            voice.pausedByApp || voice.gainWithoutMaster <= 0.0f ||
            GetSpeechMasterVolume(soundManager) <= 0)
        {
            continue;
        }
        if (voice.spatial && voice.virtualMaxDistance >= 0.0f)
        {
            const uint8_t *manager = static_cast<const uint8_t *>(
                soundManager);
            const float deltaX = voice.virtualPosition[0] -
                *reinterpret_cast<const float *>(
                    manager + SOUND_MANAGER_LISTENER_X_OFFSET);
            const float deltaY = voice.virtualPosition[1] -
                *reinterpret_cast<const float *>(
                    manager + SOUND_MANAGER_LISTENER_Y_OFFSET);
            if (deltaX * deltaX + deltaY * deltaY >=
                voice.virtualMaxDistance * voice.virtualMaxDistance)
            {
                continue;
            }
        }
        ++count;
    }
    return count;
}

bool IsManagedSample(void *milesSample)
{
    if (milesSample == nullptr)
        return false;
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
    {
        if (Voices[i].managed && Voices[i].milesSample == milesSample)
            return true;
    }
    return false;
}

void StopManagedSample(void *milesSample, const char *reason)
{
    if (milesSample == nullptr)
        return;
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
    {
        if (Voices[i].managed && Voices[i].milesSample == milesSample)
        {
            ReleaseVoice(
                Voices[i], reason != nullptr ? reason : "managed_stop");
        }
    }
}

bool FileExists(const char *path)
{
    if (path == nullptr || *path == '\0')
        return false;
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

EFileProbeResult ProbeStreamingFile(
    const char *path,
    DWORD &winError)
{
    winError = ERROR_SUCCESS;
    if (path == nullptr || *path == '\0')
    {
        winError = ERROR_INVALID_PARAMETER;
        return FILE_PROBE_ERROR;
    }

    SetLastError(ERROR_SUCCESS);
    const DWORD attributes = GetFileAttributesA(path);
    if (attributes != INVALID_FILE_ATTRIBUTES)
    {
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            winError = ERROR_DIRECTORY;
            return FILE_PROBE_DIRECTORY;
        }
        return FILE_PROBE_FILE;
    }

    winError = GetLastError();
    if (winError == ERROR_FILE_NOT_FOUND ||
        winError == ERROR_PATH_NOT_FOUND)
    {
        return FILE_PROBE_MISSING;
    }
    if (winError == ERROR_SUCCESS)
        winError = ERROR_GEN_FAILURE;
    return FILE_PROBE_ERROR;
}

bool DirectoryExists(const char *path)
{
    if (path == nullptr || *path == '\0')
        return false;
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

ma_sound *ActiveStreamingSound()
{
    if (!Streaming.initialized || Streaming.activeSlot >= 2 ||
        !Streaming.slotInitialized[Streaming.activeSlot])
    {
        return nullptr;
    }
    return &StreamingSounds[Streaming.activeSlot];
}

void StopAndUninitStreamingSlot(unsigned int slot)
{
    if (slot >= 2)
        return;
    if (Streaming.slotInitialized[slot])
    {
        ma_sound_stop(&StreamingSounds[slot]);
        ma_sound_uninit(&StreamingSounds[slot]);
    }
    Streaming.slotInitialized[slot] = false;
    Streaming.slotCustomLoop[slot] = false;
    Streaming.slotLoopEnd[slot] = 0;
    Streaming.slotLoopMarkerFrame[slot] = 0;
    Streaming.slotLength[slot] = 0;
    Streaming.slotSampleRate[slot] = 0;
    Streaming.slotMarkerRate[slot] = 0;
}

void ReleaseStreamingSound(const char *reason)
{
    const bool hadSound = Streaming.playing || Streaming.initialized;
    char endedName[sizeof(Streaming.currentName)] = {};
    CopyName(endedName, sizeof(endedName), Streaming.currentName);
    for (unsigned int slot = 0; slot < 2; ++slot)
    {
        StopAndUninitStreamingSlot(slot);
    }
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    if (hadSound)
    {
        LOGINFO("MINIAUDIO_FULL_STREAM_END name=%s reason=%s",
                endedName[0] != '\0'
                    ? endedName
                    : "<none>",
                reason != nullptr ? reason : "unknown");
    }
#else
    (void)reason;
#endif
    Streaming.initialized = false;
    Streaming.playing = false;
    Streaming.paused = false;
    Streaming.fastSwitchScheduled = false;
    Streaming.fastSwitchEngineFrame = 0;
    Streaming.fastSwitchSourceFrame = 0;
    Streaming.fastSwitchOutputFrame = 0;
    Streaming.fastSwitchSlot = 0;
    Streaming.pendingStop = false;
    Streaming.pendingMarkerType = 0;
    Streaming.currentName[0] = '\0';
    Streaming.currentPath[0] = '\0';
    ClearPendingStreamingName();
    Streaming.fadeGain = 1.0f;
    Streaming.fadeTarget = 1.0f;
}

EStreamingResolveResult ResolveStreamingPath(
    const char *soundName,
    char *resolvedPath,
    size_t resolvedCapacity,
    const char **resultReason,
    DWORD *resultWinError)
{
    if (resultReason != nullptr)
        *resultReason = "unknown";
    if (resultWinError != nullptr)
        *resultWinError = ERROR_SUCCESS;
    if (resolvedPath == nullptr || resolvedCapacity == 0)
    {
        if (resultReason != nullptr)
            *resultReason = "invalid_output";
        if (resultWinError != nullptr)
            *resultWinError = ERROR_INVALID_PARAMETER;
        return STREAM_RESOLVE_INCOMPLETE;
    }
    resolvedPath[0] = '\0';
    if (soundName == nullptr || *soundName == '\0')
    {
        if (resultReason != nullptr)
            *resultReason = "invalid_name";
        if (resultWinError != nullptr)
            *resultWinError = ERROR_INVALID_NAME;
        return STREAM_RESOLVE_INCOMPLETE;
    }
    if (_stricmp(soundName, "none") == 0)
    {
        if (resultReason != nullptr)
            *resultReason = "sentinel_none";
        return STREAM_RESOLVE_SENTINEL;
    }

    const char *incompleteReason = nullptr;
    DWORD incompleteWinError = ERROR_SUCCESS;
    char addonPath[MAX_PATH] = {};
    if (!ModAudioSettings().addonPath.empty())
    {
        const int addonFormatResult = _snprintf_s(
            addonPath,
            sizeof(addonPath),
            _TRUNCATE,
            "%sStream\\%s",
            ModAudioSettings().addonPath.c_str(),
            soundName);
        if (addonFormatResult < 0)
        {
            incompleteReason = "addon_path_truncated";
            incompleteWinError = ERROR_BUFFER_OVERFLOW;
        }
        else
        {
            DWORD addonWinError = ERROR_SUCCESS;
            const EFileProbeResult addonProbe =
                ProbeStreamingFile(addonPath, addonWinError);
            if (addonProbe == FILE_PROBE_FILE)
            {
                CopyName(resolvedPath, resolvedCapacity, addonPath);
                if (resultReason != nullptr)
                    *resultReason = "addon_override";
                return STREAM_RESOLVE_FILE;
            }
            if (addonProbe == FILE_PROBE_DIRECTORY)
            {
                incompleteReason = "addon_path_directory";
                incompleteWinError = addonWinError;
            }
            else if (addonProbe == FILE_PROBE_ERROR)
            {
                incompleteReason = "addon_path_error";
                incompleteWinError = addonWinError;
            }
        }
    }

    if (Streaming.resourcePathTruncated)
    {
        if (resultReason != nullptr)
            *resultReason = incompleteReason != nullptr
                ? incompleteReason
                : "resource_root_truncated";
        if (resultWinError != nullptr)
            *resultWinError = incompleteReason != nullptr
                ? incompleteWinError
                : ERROR_BUFFER_OVERFLOW;
        return STREAM_RESOLVE_INCOMPLETE;
    }
    if (Streaming.resourcePath[0] == '\0')
    {
        if (resultReason != nullptr)
            *resultReason = incompleteReason != nullptr
                ? incompleteReason
                : "resource_root_empty";
        if (resultWinError != nullptr)
            *resultWinError = incompleteReason != nullptr
                ? incompleteWinError
                : ERROR_PATH_NOT_FOUND;
        return STREAM_RESOLVE_INCOMPLETE;
    }

    const int baseFormatResult = _snprintf_s(
        resolvedPath,
        resolvedCapacity,
        _TRUNCATE,
        "%s%s",
        Streaming.resourcePath,
        soundName);
    if (baseFormatResult < 0)
    {
        resolvedPath[0] = '\0';
        if (resultReason != nullptr)
            *resultReason = "base_path_truncated";
        if (resultWinError != nullptr)
            *resultWinError = ERROR_BUFFER_OVERFLOW;
        return STREAM_RESOLVE_INCOMPLETE;
    }

    DWORD baseWinError = ERROR_SUCCESS;
    const EFileProbeResult baseProbe =
        ProbeStreamingFile(resolvedPath, baseWinError);
    if (baseProbe == FILE_PROBE_FILE)
    {
        if (resultReason != nullptr)
            *resultReason = "selected_resource";
        return STREAM_RESOLVE_FILE;
    }
    if (baseProbe == FILE_PROBE_MISSING && incompleteReason == nullptr)
    {
        if (resultReason != nullptr)
            *resultReason = "file_not_found";
        if (resultWinError != nullptr)
            *resultWinError = baseWinError;
        return STREAM_RESOLVE_MISSING;
    }

    if (resultReason != nullptr)
    {
        if (incompleteReason != nullptr)
            *resultReason = incompleteReason;
        else if (baseProbe == FILE_PROBE_DIRECTORY)
            *resultReason = "base_path_directory";
        else
            *resultReason = "base_path_error";
    }
    if (resultWinError != nullptr)
        *resultWinError = incompleteReason != nullptr
            ? incompleteWinError
            : baseWinError;
    resolvedPath[0] = '\0';
    return STREAM_RESOLVE_INCOMPLETE;
}

void ApplyStreamingGain()
{
    float gain = static_cast<float>(Streaming.volumePercent) * 0.01f *
        Streaming.fadeGain;
    gain = ClampUnit(gain);
    for (unsigned int slot = 0; slot < 2; ++slot)
    {
        if (Streaming.slotInitialized[slot])
            ma_sound_set_volume(&StreamingSounds[slot], gain);
    }
}

void RecordStreamingResolveDiagnostic(
    const char *soundName,
    EStreamingResolveResult resolveResult,
    const char *resolveReason,
    DWORD resolveWinError,
    const char *requestPhase,
    bool catalogMissing)
{
    if (resolveResult == STREAM_RESOLVE_MISSING)
    {
        if (catalogMissing)
        {
            try
            {
                std::string logicalTarget("Stream\\");
                logicalTarget.append(soundName != nullptr ? soundName : "");
                RecordMissingAudioResource(
                    soundName,
                    "stream_music",
                    logicalTarget.c_str(),
                    "stream",
                    "music",
                    "file_not_found");
            }
            catch (const std::exception &)
            { GameJournal::AllocationFailureInCatch("MiniaudioOneShotSfx");
                LOGERR("MINIAUDIO_MISSING_AUDIO_UNTRACKED\tphase=stream_target\treason=std_exception\tdomain=stream_music\trequest_phase=%s",
                       requestPhase != nullptr ? requestPhase : "unknown");
                /* OutputDebugString is unbuffered. */
            }
        }
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
        LOGERR("MINIAUDIO_FULL_STREAM_ERROR\tphase=resolve\trequest_phase=%s\tstatus=missing\treason=%s\twin_error=%lu\tlogical_root=Stream\tcatalog_candidate=%d\troute=keep_current",
               requestPhase != nullptr ? requestPhase : "unknown",
               resolveReason != nullptr ? resolveReason : "unknown",
               static_cast<unsigned long>(resolveWinError),
               catalogMissing ? 1 : 0);
#endif
        return;
    }
    if (resolveResult == STREAM_RESOLVE_INCOMPLETE)
    {
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
        if (ShouldLogLimited(MissingAudioStreamIncompleteDiagnosticCount))
        {
            LOGERR("MINIAUDIO_MISSING_AUDIO_STREAM_INCOMPLETE\trequest_phase=%s\treason=%s\twin_error=%lu\tcatalog_recorded=0\ttotal=%u",
                   requestPhase != nullptr ? requestPhase : "unknown",
                   resolveReason != nullptr ? resolveReason : "unknown",
                   static_cast<unsigned long>(resolveWinError),
                   MissingAudioStreamIncompleteDiagnosticCount);
            /* OutputDebugString is unbuffered. */
        }
#endif
    }
}

void AuditQueuedStreamingResource(
    const char *soundName,
    const char *requestPhase)
{
    if (!(ModAudioSettings().JMVMiniaudioMissingAudioCatalogDiagnostic || false) ||
        soundName == nullptr || *soundName == '\0')
        return;

    char resolvedPath[MAX_PATH] = {};
    const char *resolveReason = "unknown";
    DWORD resolveWinError = ERROR_SUCCESS;
    const EStreamingResolveResult resolveResult = ResolveStreamingPath(
        soundName,
        resolvedPath,
        sizeof(resolvedPath),
        &resolveReason,
        &resolveWinError);
    if (resolveResult != STREAM_RESOLVE_FILE &&
        resolveResult != STREAM_RESOLVE_SENTINEL)
    {
        RecordStreamingResolveDiagnostic(
            soundName,
            resolveResult,
            resolveReason,
            resolveWinError,
            requestPhase,
            true);
    }
}

bool ScaleStreamingMarkerFrame(
    ma_uint64 markerFrame,
    ma_uint32 markerRate,
    ma_uint32 outputRate,
    ma_uint64 &outputFrame)
{
    outputFrame = 0;
    if (markerRate == 0 || outputRate == 0 ||
        markerFrame >
            (~(static_cast<ma_uint64>(0)) - markerRate / 2) /
                outputRate)
    {
        return false;
    }
    outputFrame =
        (markerFrame * outputRate + markerRate / 2) / markerRate;
    return true;
}

bool ConfigureStreamingSlotMarkers(
    unsigned int slot,
    const char *soundName,
    const char *resolvedPath)
{
    if (slot >= 2 || !Streaming.slotInitialized[slot])
        return false;
    Streaming.slotCustomLoop[slot] = false;
    Streaming.slotLoopEnd[slot] = 0;
    Streaming.slotLoopMarkerFrame[slot] = 0;
    Streaming.slotLength[slot] = 0;
    Streaming.slotSampleRate[slot] = 0;
    Streaming.slotMarkerRate[slot] = 0;

    ma_sound *sound = &StreamingSounds[slot];
    ma_uint32 sampleRate = 0;
    ma_uint64 length = 0;
    const ma_result formatResult = ma_sound_get_data_format(
        sound, nullptr, nullptr, &sampleRate, nullptr, 0);
    const ma_result lengthResult =
        ma_sound_get_length_in_pcm_frames(sound, &length);
    if (formatResult == MA_SUCCESS)
        Streaming.slotSampleRate[slot] = sampleRate;
    if (lengthResult == MA_SUCCESS)
        Streaming.slotLength[slot] = length;

    if (!IsStreamingMarkersEnabled())
        return false;
    if (formatResult != MA_SUCCESS || sampleRate == 0 ||
        lengthResult != MA_SUCCESS || length == 0 ||
        resolvedPath == nullptr || *resolvedPath == '\0')
    {
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=stream_format name=%s format_result=%d length_result=%d sample_rate=%u length=%I64u fallback=physical_track",
               soundName != nullptr ? soundName : "<null>",
               formatResult,
               lengthResult,
               static_cast<unsigned int>(sampleRate),
               static_cast<unsigned __int64>(length));
#endif
        return false;
    }

    ma_decoder markerDecoder = {};
    const ma_result decoderInitResult = static_cast<ma_result>(
        AudioResources::InitFileDecoder(resolvedPath, &markerDecoder));
    ma_result decoderFormatResult = MA_INVALID_OPERATION;
    ma_uint32 markerRate = 0;
    if (decoderInitResult == MA_SUCCESS)
    {
        decoderFormatResult = ma_decoder_get_data_format(
            &markerDecoder,
            nullptr,
            nullptr,
            &markerRate,
            nullptr,
            0);
        ma_decoder_uninit(&markerDecoder);
    }
    if (decoderInitResult != MA_SUCCESS ||
        decoderFormatResult != MA_SUCCESS || markerRate == 0)
    {
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=marker_format name=%s path=%s init_result=%d format_result=%d marker_rate=%u output_rate=%u fallback=physical_track",
               soundName != nullptr ? soundName : "<null>",
               resolvedPath,
               decoderInitResult,
               decoderFormatResult,
               static_cast<unsigned int>(markerRate),
               static_cast<unsigned int>(sampleRate));
#endif
        return false;
    }
    Streaming.slotMarkerRate[slot] = markerRate;

    SStreamingMarkerSet markers;
    if (!ResolveStreamingMarkerSet(soundName, markers) ||
        !markers.hasLoop)
    {
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGINFO("MINIAUDIO_STREAM_MARKERS_LOOP_APPLY name=%s mode=physical_track length=%I64u output_rate=%u marker_rate=%u",
                soundName != nullptr ? soundName : "<null>",
                static_cast<unsigned __int64>(length),
                static_cast<unsigned int>(sampleRate),
                static_cast<unsigned int>(markerRate));
#endif
        return false;
    }
    ma_uint64 loopOutputFrame = 0;
    if (markers.loopFrame == 0 ||
        !ScaleStreamingMarkerFrame(
            markers.loopFrame,
            markerRate,
            sampleRate,
            loopOutputFrame) ||
        loopOutputFrame == 0 || loopOutputFrame > length)
    {
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=loop_validate name=%s marker_frame=%I64u marker_rate=%u output_frame=%I64u output_length=%I64u output_rate=%u fallback=physical_track",
               soundName != nullptr ? soundName : "<null>",
               static_cast<unsigned __int64>(markers.loopFrame),
               static_cast<unsigned int>(markerRate),
               static_cast<unsigned __int64>(loopOutputFrame),
               static_cast<unsigned __int64>(length),
               static_cast<unsigned int>(sampleRate));
#endif
        return false;
    }
    ma_data_source *dataSource = ma_sound_get_data_source(sound);
    if (dataSource == nullptr)
    {
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=loop_data_source name=%s fallback=physical_track",
               soundName != nullptr ? soundName : "<null>");
#endif
        return false;
    }
    const ma_result loopResult =
        ma_data_source_set_loop_point_in_pcm_frames(
            dataSource, 0, loopOutputFrame);
    if (loopResult != MA_SUCCESS)
    {
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=loop_apply name=%s marker_frame=%I64u output_frame=%I64u result=%d description=%s fallback=physical_track",
               soundName != nullptr ? soundName : "<null>",
               static_cast<unsigned __int64>(markers.loopFrame),
               static_cast<unsigned __int64>(loopOutputFrame),
               loopResult,
               ma_result_description(loopResult));
#endif
        return false;
    }
    Streaming.slotCustomLoop[slot] = true;
    Streaming.slotLoopEnd[slot] = loopOutputFrame;
    Streaming.slotLoopMarkerFrame[slot] = markers.loopFrame;
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
    LOGINFO("MINIAUDIO_STREAM_MARKERS_LOOP_APPLY name=%s mode=markers2 marker_begin=0 marker_end=%I64u marker_rate=%u output_begin=0 output_end=%I64u output_length=%I64u output_rate=%u result=success",
            soundName != nullptr ? soundName : "<null>",
            static_cast<unsigned __int64>(markers.loopFrame),
            static_cast<unsigned int>(markerRate),
            static_cast<unsigned __int64>(loopOutputFrame),
            static_cast<unsigned __int64>(length),
            static_cast<unsigned int>(sampleRate));
#endif
    return true;
}

void FinalizeFastStreamingSwitchIfDue()
{
    if (!Streaming.fastSwitchScheduled || !EngineInitialized)
        return;
    const ma_uint64 now = ma_engine_get_time_in_pcm_frames(&Engine);
    if (now < Streaming.fastSwitchEngineFrame)
        return;

    const unsigned int oldSlot = Streaming.activeSlot;
    const unsigned int newSlot = Streaming.fastSwitchSlot;
    if (newSlot >= 2 || !Streaming.slotInitialized[newSlot])
    {
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=fast_finalize pending=%s slot=%u reason=slot_unavailable fallback=keep_current",
               Streaming.pendingName[0] != '\0'
                   ? Streaming.pendingName
                   : "<none>",
               newSlot);
#endif
        Streaming.fastSwitchScheduled = false;
        Streaming.fastSwitchEngineFrame = 0;
        Streaming.fastSwitchSourceFrame = 0;
        Streaming.fastSwitchOutputFrame = 0;
        Streaming.pendingMarkerType = 0;
        ClearPendingStreamingName();
        return;
    }

    char oldName[sizeof(Streaming.currentName)] = {};
    CopyName(oldName, sizeof(oldName), Streaming.currentName);
    Streaming.activeSlot = newSlot;
    Streaming.initialized = true;
    Streaming.playing = true;
    Streaming.paused = false;
    CopyName(
        Streaming.currentName,
        sizeof(Streaming.currentName),
        Streaming.pendingName);
    CopyName(
        Streaming.currentPath,
        sizeof(Streaming.currentPath),
        Streaming.pendingPath);
    const ma_uint64 scheduledFrame = Streaming.fastSwitchEngineFrame;
    const ma_uint64 sourceFrame = Streaming.fastSwitchSourceFrame;
    const ma_uint64 outputFrame = Streaming.fastSwitchOutputFrame;
    Streaming.fastSwitchScheduled = false;
    Streaming.fastSwitchEngineFrame = 0;
    Streaming.fastSwitchSourceFrame = 0;
    Streaming.fastSwitchOutputFrame = 0;
    Streaming.fastSwitchSlot = 0;
    Streaming.pendingMarkerType = 0;
    Streaming.pendingStop = false;
    ClearPendingStreamingName();
    if (oldSlot != newSlot)
        StopAndUninitStreamingSlot(oldSlot);
    ApplyStreamingGain();
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
    LOGINFO("MINIAUDIO_STREAM_MARKERS_FAST_COMPLETE previous=%s current=%s marker_frame=%I64u marker_output_frame=%I64u scheduled_engine_frame=%I64u observed_engine_frame=%I64u overshoot_frames=%I64u miles=muted",
            oldName[0] != '\0' ? oldName : "<none>",
            Streaming.currentName,
            static_cast<unsigned __int64>(sourceFrame),
            static_cast<unsigned __int64>(outputFrame),
            static_cast<unsigned __int64>(scheduledFrame),
            static_cast<unsigned __int64>(now),
            static_cast<unsigned __int64>(now - scheduledFrame));
#endif
}

void CancelFastStreamingSwitch(const char *reason, bool clearPending)
{
    FinalizeFastStreamingSwitchIfDue();
    if (!Streaming.fastSwitchScheduled)
    {
        if (clearPending && Streaming.pendingMarkerType == 3)
        {
            Streaming.pendingMarkerType = 0;
            Streaming.pendingStop = false;
            ClearPendingStreamingName();
        }
        return;
    }
    ma_sound *active = ActiveStreamingSound();
    if (active != nullptr)
    {
        ma_sound_set_stop_time_in_pcm_frames(
            active, ~(static_cast<ma_uint64>(0)));
    }
    const unsigned int scheduledSlot = Streaming.fastSwitchSlot;
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
    LOGINFO("MINIAUDIO_STREAM_MARKERS_FAST_CANCEL current=%s pending=%s reason=%s clear_pending=%d",
            Streaming.currentName[0] != '\0'
                ? Streaming.currentName
                : "<none>",
            Streaming.pendingName[0] != '\0'
                ? Streaming.pendingName
                : "<none>",
            reason != nullptr ? reason : "unknown",
            clearPending ? 1 : 0);
#endif
    Streaming.fastSwitchScheduled = false;
    Streaming.fastSwitchEngineFrame = 0;
    Streaming.fastSwitchSourceFrame = 0;
    Streaming.fastSwitchOutputFrame = 0;
    Streaming.fastSwitchSlot = 0;
    if (scheduledSlot < 2 && scheduledSlot != Streaming.activeSlot)
        StopAndUninitStreamingSlot(scheduledSlot);
    if (clearPending)
    {
        Streaming.pendingMarkerType = 0;
        Streaming.pendingStop = false;
        ClearPendingStreamingName();
    }
}

unsigned int GetStreamingMarkerLeadFrames()
{
    const int stockLead = CurrentSoundManager != nullptr
        ? *reinterpret_cast<const int *>(
              static_cast<const uint8_t *>(CurrentSoundManager) +
              SOUND_MANAGER_STREAM_SAMPLES_PER_BUFFER_OFFSET)
        : 0;
    if (stockLead > 0 && stockLead <= 1024 * 1024)
        return static_cast<unsigned int>(stockLead);
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
    LOGWARN("MINIAUDIO_STREAM_MARKERS_FALLBACK phase=lead_frames stock_value=%d fallback=4096",
            stockLead);
#endif
    return 4096;
}

bool ScheduleFastStreamingSwitch(const char *soundName)
{
    ma_sound *active = ActiveStreamingSound();
    if (active == nullptr || soundName == nullptr || *soundName == '\0' ||
        AppAudioSuspended || Streaming.paused)
    {
        return false;
    }
    SStreamingMarkerSet markers;
    if (!ResolveStreamingMarkerSet(Streaming.currentName, markers) ||
        markers.fast.empty())
    {
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGINFO("MINIAUDIO_STREAM_MARKERS_FALLBACK phase=fast_select current=%s pending=%s reason=no_markers1 fallback=legacy_immediate",
                Streaming.currentName,
                soundName);
#endif
        return false;
    }

    const unsigned int activeSlot = Streaming.activeSlot;
    ma_uint64 cursor = 0;
    ma_uint64 length = Streaming.slotLength[activeSlot];
    ma_uint32 outputRate = Streaming.slotSampleRate[activeSlot];
    const ma_uint32 markerRate =
        Streaming.slotMarkerRate[activeSlot];
    const ma_result cursorResult =
        ma_sound_get_cursor_in_pcm_frames(active, &cursor);
    if (length == 0)
        ma_sound_get_length_in_pcm_frames(active, &length);
    if (outputRate == 0)
    {
        ma_sound_get_data_format(
            active, nullptr, nullptr, &outputRate, nullptr, 0);
    }
    const ma_uint64 cycleEnd = Streaming.slotCustomLoop[activeSlot]
        ? Streaming.slotLoopEnd[activeSlot]
        : length;
    const unsigned int markerLeadFrames =
        GetStreamingMarkerLeadFrames();
    ma_uint64 outputLeadFrames = 0;
    const bool leadScaled = ScaleStreamingMarkerFrame(
        markerLeadFrames,
        markerRate,
        outputRate,
        outputLeadFrames);
    if (cursorResult != MA_SUCCESS || outputRate == 0 ||
        markerRate == 0 || !leadScaled || outputLeadFrames == 0 ||
        cycleEnd == 0 || cursor > cycleEnd)
    {
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=fast_cursor current=%s pending=%s cursor_result=%d cursor=%I64u cycle_end=%I64u marker_lead=%u output_lead=%I64u marker_rate=%u output_rate=%u fallback=legacy_immediate",
               Streaming.currentName,
               soundName,
               cursorResult,
               static_cast<unsigned __int64>(cursor),
               static_cast<unsigned __int64>(cycleEnd),
               markerLeadFrames,
               static_cast<unsigned __int64>(outputLeadFrames),
               static_cast<unsigned int>(markerRate),
               static_cast<unsigned int>(outputRate));
#endif
        return false;
    }
    const ma_uint64 threshold = cursor + outputLeadFrames;
    ma_uint64 markerFrame = 0;
    ma_uint64 markerOutputFrame = 0;
    bool wrapped = true;
    for (size_t markerIndex = 0;
         markerIndex < markers.fast.size();
         ++markerIndex)
    {
        ma_uint64 candidateOutputFrame = 0;
        if (!ScaleStreamingMarkerFrame(
                markers.fast[markerIndex],
                markerRate,
                outputRate,
                candidateOutputFrame))
        {
            return false;
        }
        if (candidateOutputFrame >= threshold)
        {
            markerFrame = markers.fast[markerIndex];
            markerOutputFrame = candidateOutputFrame;
            wrapped = false;
            break;
        }
    }
    if (wrapped)
    {
        markerFrame = markers.fast[0];
        if (!ScaleStreamingMarkerFrame(
                markerFrame,
                markerRate,
                outputRate,
                markerOutputFrame))
        {
            return false;
        }
    }
    if (markerOutputFrame > cycleEnd ||
        (!wrapped && markerOutputFrame < cursor))
    {
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=fast_validate current=%s pending=%s marker_frame=%I64u marker_output_frame=%I64u marker_rate=%u cursor=%I64u cycle_end=%I64u output_rate=%u wrapped=%d fallback=legacy_immediate",
               Streaming.currentName,
               soundName,
               static_cast<unsigned __int64>(markerFrame),
               static_cast<unsigned __int64>(markerOutputFrame),
               static_cast<unsigned int>(markerRate),
               static_cast<unsigned __int64>(cursor),
               static_cast<unsigned __int64>(cycleEnd),
               static_cast<unsigned int>(outputRate),
               wrapped ? 1 : 0);
#endif
        return false;
    }
    const ma_uint64 outputDelta = wrapped
        ? (cycleEnd - cursor) + markerOutputFrame
        : markerOutputFrame - cursor;
    const ma_uint32 engineRate = ma_engine_get_sample_rate(&Engine);
    if (outputDelta == 0 || engineRate == 0)
        return false;
    const ma_uint64 engineDelta =
        (outputDelta * engineRate + outputRate - 1) / outputRate;
    const ma_uint64 engineNow =
        ma_engine_get_time_in_pcm_frames(&Engine);
    const ma_uint64 switchEngineFrame = engineNow + engineDelta;

    char resolvedPath[MAX_PATH] = {};
    const char *resolveReason = "unknown";
    DWORD resolveWinError = ERROR_SUCCESS;
    const EStreamingResolveResult resolveResult = ResolveStreamingPath(
        soundName,
        resolvedPath,
        sizeof(resolvedPath),
        &resolveReason,
        &resolveWinError);
    if (resolveResult != STREAM_RESOLVE_FILE)
    {
        if (resolveResult != STREAM_RESOLVE_SENTINEL)
        {
            RecordStreamingResolveDiagnostic(
                soundName,
                resolveResult,
                resolveReason,
                resolveWinError,
                "fast_marker_prepare",
                true);
        }
        return false;
    }
    if (!EnsureEngine() || !EnsureDeviceStarted("fast_marker_prepare"))
        return false;

    const unsigned int nextSlot = activeSlot ^ 1u;
    StopAndUninitStreamingSlot(nextSlot);
    ma_sound *candidate = &StreamingSounds[nextSlot];
    memset(candidate, 0, sizeof(*candidate));
    const ma_uint32 flags = MA_SOUND_FLAG_STREAM |
        MA_SOUND_FLAG_NO_SPATIALIZATION;
    const ma_result initResult = static_cast<ma_result>(AudioResources::InitFileSound(
        &Engine, resolvedPath, flags, candidate));
    if (initResult != MA_SUCCESS)
    {
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=fast_init name=%s path=%s result=%d description=%s fallback=legacy_immediate",
               soundName,
               resolvedPath,
               initResult,
               ma_result_description(initResult));
#endif
        return false;
    }
    Streaming.slotInitialized[nextSlot] = true;
    ConfigureStreamingSlotMarkers(nextSlot, soundName, resolvedPath);
    ma_sound_set_looping(candidate, MA_TRUE);
    ma_sound_set_volume(
        candidate,
        ClampUnit(
            static_cast<float>(Streaming.volumePercent) * 0.01f *
            Streaming.fadeGain));
    ma_sound_set_start_time_in_pcm_frames(
        candidate, switchEngineFrame);
    const ma_result startResult = ma_sound_start(candidate);
    if (startResult != MA_SUCCESS)
    {
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        LOGERR("MINIAUDIO_STREAM_MARKERS_ERROR phase=fast_start name=%s path=%s result=%d description=%s fallback=legacy_immediate",
               soundName,
               resolvedPath,
               startResult,
               ma_result_description(startResult));
#endif
        StopAndUninitStreamingSlot(nextSlot);
        return false;
    }
    ma_sound_set_stop_time_in_pcm_frames(active, switchEngineFrame);
    SetPendingStreamingName(soundName);
    CopyName(
        Streaming.pendingPath,
        sizeof(Streaming.pendingPath),
        resolvedPath);
    Streaming.pendingMarkerType = 3;
    Streaming.pendingStop = false;
    Streaming.fastSwitchSlot = nextSlot;
    Streaming.fastSwitchEngineFrame = switchEngineFrame;
    Streaming.fastSwitchSourceFrame = markerFrame;
    Streaming.fastSwitchOutputFrame = markerOutputFrame;
    Streaming.fastSwitchScheduled = true;
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
    LOGINFO("MINIAUDIO_STREAM_MARKERS_FAST_SCHEDULE current=%s pending=%s marker_frame=%I64u marker_output_frame=%I64u marker_rate=%u cursor=%I64u marker_lead=%u output_lead=%I64u wrapped=%d cycle_end=%I64u output_rate=%u engine_rate=%u engine_now=%I64u switch_engine_frame=%I64u output_delta=%I64u no_overlap=1 miles=muted",
            Streaming.currentName,
            soundName,
            static_cast<unsigned __int64>(markerFrame),
            static_cast<unsigned __int64>(markerOutputFrame),
            static_cast<unsigned int>(markerRate),
            static_cast<unsigned __int64>(cursor),
            markerLeadFrames,
            static_cast<unsigned __int64>(outputLeadFrames),
            wrapped ? 1 : 0,
            static_cast<unsigned __int64>(cycleEnd),
            static_cast<unsigned int>(outputRate),
            static_cast<unsigned int>(engineRate),
            static_cast<unsigned __int64>(engineNow),
            static_cast<unsigned __int64>(switchEngineFrame),
            static_cast<unsigned __int64>(outputDelta));
#endif
    return true;
}

bool StartStreamingNow(
    const char *soundName,
    const char *reason,
    bool resetFade,
    bool catalogMissing = true)
{
    if (soundName == nullptr || *soundName == '\0')
        return false;

    char resolvedPath[MAX_PATH] = {};
    const char *resolveReason = "unknown";
    DWORD resolveWinError = ERROR_SUCCESS;
    const EStreamingResolveResult resolveResult = ResolveStreamingPath(
        soundName,
        resolvedPath,
        sizeof(resolvedPath),
        &resolveReason,
        &resolveWinError);
    if (resolveResult == STREAM_RESOLVE_SENTINEL)
        return false;
    if (resolveResult != STREAM_RESOLVE_FILE)
    {
        RecordStreamingResolveDiagnostic(
            soundName,
            resolveResult,
            resolveReason,
            resolveWinError,
            "start_now",
            catalogMissing);
        return false;
    }

    const bool shouldRemainPaused =
        AppAudioSuspended || Streaming.paused;
    if (!EnsureEngine() ||
        (!shouldRemainPaused && !EnsureDeviceStarted("stream_start")))
        return false;

    const unsigned int nextSlot = Streaming.initialized
        ? (Streaming.activeSlot ^ 1u)
        : 0u;
    StopAndUninitStreamingSlot(nextSlot);
    ma_sound *candidate = &StreamingSounds[nextSlot];
    memset(candidate, 0, sizeof(*candidate));
    const ma_uint32 flags = MA_SOUND_FLAG_STREAM |
        MA_SOUND_FLAG_NO_SPATIALIZATION;
    const ma_result initResult = static_cast<ma_result>(AudioResources::InitFileSound(
        &Engine, resolvedPath, flags, candidate));
    if (initResult != MA_SUCCESS)
    {
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
        LOGERR("MINIAUDIO_FULL_STREAM_ERROR phase=init name=%s path=%s result=%d description=%s route=keep_current",
               soundName,
               resolvedPath,
               initResult,
               ma_result_description(initResult));
#endif
        return false;
    }
    Streaming.slotInitialized[nextSlot] = true;
    ConfigureStreamingSlotMarkers(nextSlot, soundName, resolvedPath);
    ma_sound_set_looping(candidate, MA_TRUE);
    ma_sound_set_volume(candidate, 0.0f);
    const ma_result startResult = shouldRemainPaused
        ? MA_SUCCESS
        : ma_sound_start(candidate);
    if (startResult != MA_SUCCESS)
    {
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
        LOGERR("MINIAUDIO_FULL_STREAM_ERROR phase=start name=%s path=%s result=%d description=%s route=keep_current",
               soundName,
               resolvedPath,
               startResult,
               ma_result_description(startResult));
#endif
        StopAndUninitStreamingSlot(nextSlot);
        return false;
    }

    const bool replaced = Streaming.initialized;
    char replacedName[sizeof(Streaming.currentName)] = {};
    CopyName(replacedName, sizeof(replacedName), Streaming.currentName);
    const unsigned int replacedSlot = Streaming.activeSlot;
    if (replaced && replacedSlot != nextSlot)
        StopAndUninitStreamingSlot(replacedSlot);

    Streaming.activeSlot = nextSlot;
    Streaming.initialized = true;
    Streaming.playing = true;
    Streaming.paused = shouldRemainPaused;
    Streaming.pendingStop = false;
    Streaming.pendingMarkerType = 0;
    Streaming.fastSwitchScheduled = false;
    Streaming.fastSwitchEngineFrame = 0;
    Streaming.fastSwitchSourceFrame = 0;
    Streaming.fastSwitchOutputFrame = 0;
    Streaming.fastSwitchSlot = 0;
    ClearPendingStreamingName();
    Streaming.lastTick = GetTickCount();
    if (resetFade)
    {
        Streaming.fadeGain = 1.0f;
        Streaming.fadeTarget = 1.0f;
    }
    CopyName(
        Streaming.currentName,
        sizeof(Streaming.currentName),
        soundName);
    CopyName(
        Streaming.currentPath,
        sizeof(Streaming.currentPath),
        resolvedPath);
    ApplyStreamingGain();
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    if (replaced)
    {
        LOGINFO("MINIAUDIO_FULL_STREAM_END name=%s reason=replace",
                replacedName[0] != '\0' ? replacedName : "<none>");
    }
    LOGINFO("MINIAUDIO_FULL_STREAM_START name=%s path=%s reason=%s volume=%d fade_percent=%d paused=%d miles=muted",
            soundName,
            resolvedPath,
            reason != nullptr ? reason : "unknown",
            Streaming.volumePercent,
            static_cast<int>(Streaming.fadeGain * 100.0f + 0.5f),
            Streaming.paused ? 1 : 0);
#endif
    return true;
}

void TickStreaming()
{
    FinalizeFastStreamingSwitchIfDue();
    ma_sound *sound = ActiveStreamingSound();
    if (sound == nullptr)
        return;

    const DWORD now = GetTickCount();
    if (AppAudioSuspended || Streaming.paused)
    {
        Streaming.lastTick = now;
        return;
    }
    DWORD elapsed = now - Streaming.lastTick;
    Streaming.lastTick = now;
    if (elapsed > 1000)
        elapsed = 1000;
    if (Streaming.fadeGain != Streaming.fadeTarget)
    {
        const float step = static_cast<float>(elapsed) / 6000.0f;
        if (Streaming.fadeGain < Streaming.fadeTarget)
        {
            Streaming.fadeGain += step;
            if (Streaming.fadeGain > Streaming.fadeTarget)
                Streaming.fadeGain = Streaming.fadeTarget;
        }
        else
        {
            Streaming.fadeGain -= step;
            if (Streaming.fadeGain < Streaming.fadeTarget)
                Streaming.fadeGain = Streaming.fadeTarget;
        }
        ApplyStreamingGain();
    }

    if (Streaming.pendingMarkerType == 2 &&
        Streaming.fadeGain <= 0.0f)
    {
        char pendingName[sizeof(Streaming.pendingName)] = {};
        CopyName(pendingName, sizeof(pendingName), Streaming.pendingName);
        const bool pendingStop = Streaming.pendingStop;
        const bool pendingNameTruncated = Streaming.pendingNameTruncated;
        ClearPendingStreamingName();
        Streaming.pendingStop = false;
        Streaming.pendingMarkerType = 0;
        if (pendingStop)
        {
            ReleaseStreamingSound("safe_fade_stop");
            return;
        }
        if (pendingName[0] != '\0')
        {
            if (StartStreamingNow(
                    pendingName,
                    "safe_fade_switch",
                    false,
                    !pendingNameTruncated))
            {
                Streaming.fadeTarget = 1.0f;
                Streaming.lastTick = now;
            }
            else
            {
                Streaming.fadeTarget = 1.0f;
                ApplyStreamingGain();
            }
            return;
        }
    }

    sound = ActiveStreamingSound();
    if (sound != nullptr && ma_sound_at_end(sound))
    {
        char pendingName[sizeof(Streaming.pendingName)] = {};
        CopyName(
            pendingName,
            sizeof(pendingName),
            Streaming.pendingName);
        const bool pendingStop = Streaming.pendingStop;
        const bool pendingNameTruncated = Streaming.pendingNameTruncated;
        ClearPendingStreamingName();
        Streaming.pendingStop = false;
        Streaming.pendingMarkerType = 0;
        if (pendingName[0] != '\0')
        {
            if (!StartStreamingNow(
                    pendingName,
                    "end_of_track_switch",
                    true,
                    !pendingNameTruncated))
                ReleaseStreamingSound("end_of_track_switch_failed");
        }
        else if (pendingStop)
            ReleaseStreamingSound("end_of_track_stop");
        else
            ReleaseStreamingSound("eos");
    }
}

bool SetStreamingResource(
    void *soundManager,
    const char *resourcePath,
    const char *markersPath)
{
    if (CurrentSoundManager != soundManager)
        ResetForManager(soundManager, "stream_manager_change");

    Streaming.resourcePath[0] = '\0';
    Streaming.resourcePathTruncated = false;
    if (resourcePath != nullptr && *resourcePath != '\0')
    {
        Streaming.resourcePathTruncated =
            strlen(resourcePath) >= sizeof(Streaming.resourcePath);
        CopyName(
            Streaming.resourcePath,
            sizeof(Streaming.resourcePath),
            resourcePath);
        const size_t length = strlen(Streaming.resourcePath);
        if (length > 0 &&
            Streaming.resourcePath[length - 1] != '\\' &&
            Streaming.resourcePath[length - 1] != '/' &&
            length + 1 < sizeof(Streaming.resourcePath))
        {
            Streaming.resourcePath[length] = '\\';
            Streaming.resourcePath[length + 1] = '\0';
        }
        else if (length > 0 &&
                 Streaming.resourcePath[length - 1] != '\\' &&
                 Streaming.resourcePath[length - 1] != '/')
        {
            Streaming.resourcePathTruncated = true;
        }
    }

    char addonStreamPath[MAX_PATH] = {};
    _snprintf_s(
        addonStreamPath,
        sizeof(addonStreamPath),
        _TRUNCATE,
        "%sStream\\",
        ModAudioSettings().addonPath.c_str());
    const bool selected = DirectoryExists(Streaming.resourcePath) ||
        DirectoryExists(addonStreamPath);
    const bool markersLoaded = LoadStreamingMarkersCatalog(markersPath);
    if (!Streaming.volumeInitialized)
    {
        Streaming.volumePercent = 100;
    }
    if (Streaming.fadeGain <= 0.0f && Streaming.fadeTarget <= 0.0f)
    {
        Streaming.fadeGain = 1.0f;
        Streaming.fadeTarget = 1.0f;
    }
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    LOGINFO("MINIAUDIO_FULL_STREAM_RESOURCE path=%s active_override=%sStream\\ markers=%s markers_loaded=%d",
            Streaming.resourcePath,
            ModAudioSettings().addonPath.c_str(),
            markersPath != nullptr ? markersPath : "<null>",
            markersLoaded ? 1 : 0);
#endif
    return selected;
}

bool BeginStreaming(void *soundManager, const char *soundName, int markerType)
{
    if (CurrentSoundManager != soundManager)
        ResetForManager(soundManager, "stream_begin_manager_change");
    if (soundName == nullptr)
        return false;
    FinalizeFastStreamingSwitchIfDue();
    ma_sound *sound = ActiveStreamingSound();
    if (Streaming.playing &&
        _stricmp(Streaming.currentName, soundName) == 0 &&
        *soundName != '\0' && sound != nullptr)
    {
        CancelFastStreamingSwitch("same_current", true);
        ma_sound_set_looping(sound, MA_TRUE);
        ClearPendingStreamingName();
        Streaming.pendingStop = false;
        Streaming.pendingMarkerType = 0;
        Streaming.fadeTarget = 1.0f;
        return true;
    }
    if (Streaming.playing && Streaming.pendingName[0] != '\0' &&
        _stricmp(Streaming.pendingName, soundName) == 0 &&
        Streaming.pendingMarkerType >= markerType)
    {
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        if (IsStreamingMarkersEnabled())
        {
            LOGINFO("MINIAUDIO_STREAM_MARKERS_SWITCH_CONTINUE current=%s pending=%s active_marker=%d requested_marker=%d",
                    Streaming.currentName,
                    Streaming.pendingName,
                    Streaming.pendingMarkerType,
                    markerType);
        }
#endif
        return true;
    }
    CancelFastStreamingSwitch("new_request", true);
    sound = ActiveStreamingSound();
    if (markerType == 1 && Streaming.playing)
    {
        AuditQueuedStreamingResource(soundName, "pending_end_of_track");
        SetPendingStreamingName(soundName);
        Streaming.pendingMarkerType = markerType;
        Streaming.pendingStop = false;
        Streaming.fadeTarget = 1.0f;
        if (sound != nullptr)
        {
            const bool preserveMarkerLoop =
                IsStreamingMarkersEnabled() &&
                Streaming.slotCustomLoop[Streaming.activeSlot];
            ma_sound_set_looping(
                sound,
                preserveMarkerLoop ? MA_TRUE : MA_FALSE);
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
            if (IsStreamingMarkersEnabled())
            {
                LOGINFO("MINIAUDIO_STREAM_MARKERS_END_REQUEST current=%s pending=%s marker=1 custom_loop=%d behavior=%s",
                        Streaming.currentName,
                        Streaming.pendingName,
                        preserveMarkerLoop ? 1 : 0,
                        preserveMarkerLoop
                            ? "preserve_markers2_loop"
                            : "physical_eof");
            }
#endif
        }
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
        LOGINFO("MINIAUDIO_FULL_STREAM_SWITCH current=%s pending=%s marker=%d mode=end_of_track",
                Streaming.currentName,
                Streaming.pendingName,
                markerType);
#endif
        return true;
    }
    if (markerType == 2 && Streaming.playing)
    {
        AuditQueuedStreamingResource(soundName, "pending_safe_fade");
        SetPendingStreamingName(soundName);
        Streaming.pendingMarkerType = markerType;
        Streaming.pendingStop = false;
        Streaming.fadeTarget = 0.0f;
        Streaming.lastTick = GetTickCount();
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
        LOGINFO("MINIAUDIO_FULL_STREAM_SWITCH current=%s pending=%s marker=%d mode=safe_fade",
                Streaming.currentName,
                Streaming.pendingName,
                markerType);
#endif
        return true;
    }
    if (markerType == 3 && Streaming.playing &&
        IsStreamingMarkersEnabled())
    {
        AuditQueuedStreamingResource(soundName, "pending_fast_marker");
        if (AppAudioSuspended || Streaming.paused)
        {
            SetPendingStreamingName(soundName);
            Streaming.pendingMarkerType = markerType;
            Streaming.pendingStop = false;
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
            LOGINFO("MINIAUDIO_STREAM_MARKERS_FAST_DEFER current=%s pending=%s reason=paused",
                    Streaming.currentName,
                    Streaming.pendingName);
#endif
            return true;
        }
        if (ScheduleFastStreamingSwitch(soundName))
            return true;
    }

#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    if (Streaming.playing && markerType != 4)
    {
        LOGINFO("MINIAUDIO_FULL_STREAM_SWITCH current=%s pending=%s marker=%d mode=diagnostic_immediate",
                Streaming.currentName,
                soundName,
                markerType);
    }
#endif
    ClearPendingStreamingName();
    Streaming.pendingStop = false;
    Streaming.pendingMarkerType = 0;
    return StartStreamingNow(
        soundName,
        markerType == 4
            ? "instant"
            : (IsStreamingMarkersEnabled()
                   ? "stream_markers_fail_open_immediate"
                   : "fast_marker_approximation"),
        true);
}

void EndStreaming(int markerType, const char *reason)
{
    FinalizeFastStreamingSwitchIfDue();
    CancelFastStreamingSwitch("end_request", true);
    ClearPendingStreamingName();
    Streaming.pendingStop = false;
    Streaming.pendingMarkerType = 0;
    if (!Streaming.initialized)
        return;
    if (markerType == 4 || markerType == 3)
    {
        ReleaseStreamingSound(
            reason != nullptr
                ? reason
                : (markerType == 4 ? "instant_end" : "fast_end"));
        return;
    }
    Streaming.pendingStop = true;
    Streaming.pendingMarkerType = markerType;
    ma_sound *sound = ActiveStreamingSound();
    if (markerType == 2)
    {
        Streaming.fadeTarget = 0.0f;
        Streaming.lastTick = GetTickCount();
    }
    else if (sound != nullptr)
    {
        Streaming.fadeTarget = 1.0f;
        const bool preserveMarkerLoop =
            IsStreamingMarkersEnabled() &&
            Streaming.slotCustomLoop[Streaming.activeSlot];
        ma_sound_set_looping(
            sound,
            preserveMarkerLoop ? MA_TRUE : MA_FALSE);
#if JMV_MINIAUDIO_STREAMING_MARKERS_DIAGNOSTICS
        if (IsStreamingMarkersEnabled())
        {
            LOGINFO("MINIAUDIO_STREAM_MARKERS_END_REQUEST current=%s pending=<stop> marker=%d custom_loop=%d behavior=%s",
                    Streaming.currentName,
                    markerType,
                    preserveMarkerLoop ? 1 : 0,
                    preserveMarkerLoop
                        ? "preserve_markers2_loop"
                        : "physical_eof");
        }
#endif
    }
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    LOGINFO("MINIAUDIO_FULL_STREAM_SWITCH current=%s pending=<stop> marker=%d mode=%s",
            Streaming.currentName,
            markerType,
            markerType == 2 ? "safe_fade" : "end_of_track");
#endif
}

void SetStreamingVolume(int percentVolume)
{
    if (percentVolume < 0)
        percentVolume = 0;
    if (percentVolume > 100)
        percentVolume = 100;
    Streaming.volumePercent = percentVolume;
    Streaming.volumeInitialized = true;
    ApplyStreamingGain();
}

void FadeOutStreaming()
{
    Streaming.fadeTarget = 0.0f;
    Streaming.lastTick = GetTickCount();
}

void FadeInStreaming()
{
    Streaming.fadeTarget = 1.0f;
    Streaming.lastTick = GetTickCount();
}

void PauseStreaming(const char *reason)
{
    FinalizeFastStreamingSwitchIfDue();
    CancelFastStreamingSwitch("pause", false);
    ma_sound *sound = ActiveStreamingSound();
    if (sound == nullptr || Streaming.paused)
        return;
    const ma_result result = ma_sound_stop(sound);
    if (result == MA_SUCCESS)
        Streaming.paused = true;
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    LOGINFO("MINIAUDIO_FULL_STREAM_PAUSE name=%s reason=%s result=%d",
            Streaming.currentName,
            reason != nullptr ? reason : "pause",
            result);
#endif
}

void ResumeStreaming(const char *reason)
{
    ma_sound *sound = ActiveStreamingSound();
    if (sound == nullptr || !Streaming.paused)
        return;
    const ma_result result = ma_sound_start(sound);
    if (result == MA_SUCCESS)
    {
        Streaming.paused = false;
        Streaming.lastTick = GetTickCount();
        if (Streaming.pendingMarkerType == 3 &&
            Streaming.pendingName[0] != '\0' &&
            IsStreamingMarkersEnabled())
        {
            char pendingName[sizeof(Streaming.pendingName)] = {};
            CopyName(
                pendingName,
                sizeof(pendingName),
                Streaming.pendingName);
            if (!ScheduleFastStreamingSwitch(pendingName))
            {
                ClearPendingStreamingName();
                Streaming.pendingMarkerType = 0;
                Streaming.pendingStop = false;
                StartStreamingNow(
                    pendingName,
                    "stream_markers_resume_fail_open_immediate",
                    true);
            }
        }
    }
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    LOGINFO("MINIAUDIO_FULL_STREAM_RESUME name=%s reason=%s result=%d",
            Streaming.currentName,
            reason != nullptr ? reason : "resume",
            result);
#endif
}

bool IsStreamingSample(void *sample)
{
    if (sample == nullptr || CurrentSoundManager == nullptr)
        return false;
    const uint8_t *manager = static_cast<const uint8_t *>(
        CurrentSoundManager);
    return *reinterpret_cast<void *const *>(
               manager + SOUND_MANAGER_STREAM_SAMPLE_OFFSET) == sample;
}

void PrepareNativeManager(void *soundManager)
{
    if(CurrentSoundManager!=soundManager)ResetForManager(soundManager,"native_manager_change");
    SynchronizeUniqueCounter(soundManager);
}
const char* GetNativeStreamingName(){return Streaming.playing ? Streaming.currentName : "";}
void Tick(void *soundManager)
{
    SyncWorldSfxPause();
    if (!EngineInitialized)
        return;
    if (!IsManagerReady(soundManager))
    {
        if (CurrentSoundManager != nullptr)
            ResetForManager(nullptr, "manager_not_ready");
        return;
    }
    if (CurrentSoundManager != soundManager)
        ResetForManager(soundManager, "manager_change");

    SynchronizeUniqueCounter(soundManager);

    if (!AppAudioSuspended && EnsureDeviceStarted("tick"))
    {
        unsigned int ended = 0;
        ResumeVoicesForApp(ended);
    }

    UpdateListener(soundManager);
    UpdateMiniaudioReverbProfileAndDiagnostics();
    UpdateScenarioSpeechDucking(soundManager);
    const int masterVolume = GetMasterVolume(soundManager);
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
    {
        if (!Voices[i].active)
            continue;
        if (Voices[i].managed && !ManagedSlotMatches(Voices[i]))
        {
            ReleaseVoice(Voices[i], "managed_slot_released");
            continue;
        }
        if (ma_sound_at_end(&Voices[i].sound))
        {
            ReleaseVoice(Voices[i], "eos");
            continue;
        }
        if (Voices[i].virtualManaged)
        {
            UpdateVirtualVoiceBinaural(Voices[i]);
            UpdateVirtualVoiceOcclusion(Voices[i], GetTickCount());
            ApplyVirtualVoiceMix(Voices[i]);
            continue;
        }
        if (Voices[i].managed)
        {
            const uint8_t *fx = static_cast<const uint8_t *>(
                Voices[i].fxControl);
            int finalVolume = *reinterpret_cast<const int *>(
                fx + FX_VOLUME_OFFSET);
            int percentPan = *reinterpret_cast<const int *>(
                fx + FX_PAN_OFFSET);
            if (finalVolume < 0)
                finalVolume = 0;
            if (finalVolume > 127)
                finalVolume = 127;
            if (percentPan < -100)
                percentPan = -100;
            if (percentPan > 100)
                percentPan = 100;
            Voices[i].gainWithoutMaster =
                static_cast<float>(finalVolume) / 127.0f;
            ma_sound_set_volume(
                &Voices[i].sound, Voices[i].gainWithoutMaster);
            ma_sound_set_pan(
                &Voices[i].sound,
                static_cast<float>(percentPan) * 0.01f);
            continue;
        }
        ma_sound_set_volume(
            &Voices[i].sound,
            static_cast<float>(masterVolume) * 0.01f *
                  Voices[i].gainWithoutMaster);
    }

    if (ModAudioSettings().JMVMiniaudioFullBackendPrototype &&
        ModAudioSettings().JMVMiniaudioStreamingMusicPrototype)
    {
        TickStreaming();
    }

    ma_device *device = ma_engine_get_device(&Engine);
    const ma_device_state state = device != nullptr
        ? ma_device_get_state(device)
        : ma_device_state_uninitialized;
    if (state != LastDeviceState)
    {
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
        LOGINFO("MINIAUDIO_ONESHOT_DEVICE_STATE old=%d new=%d app_suspended=%d active=%u",
                static_cast<int>(LastDeviceState),
                static_cast<int>(state),
                AppAudioSuspended ? 1 : 0,
                ActiveVoiceCount);
#endif
        if (!AppAudioSuspended &&
            state != ma_device_state_started &&
            ShouldLogLimited(DeviceErrorDiagnosticCount))
        {
            LOGERR("MINIAUDIO_ONESHOT_DEVICE_ERROR phase=state state=%d active=%u total=%u",
                   static_cast<int>(state),
                   ActiveVoiceCount,
                   DeviceErrorDiagnosticCount);
        }
        LastDeviceState = state;
    }
}

void StopAll(const char *reason)
{
    ReleaseAllVoices(reason != nullptr ? reason : "stop_all");
    if (ReverbGraphInitialized || ReverbGraphAttempted)
    {
        DestroyMiniaudioReverbGraph(
            true, reason != nullptr ? reason : "stop_all");
    }
    ReleaseDecodedSounds();
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    if (EngineInitialized)
    {
        LOGINFO("MINIAUDIO_ONESHOT_STOP_ALL reason=%s cache_bytes=%u",
                reason != nullptr ? reason : "stop_all",
                static_cast<unsigned int>(AudioResources::GetStatistics().bytes));
    }
#endif
}

void Pause(const char *reason)
{
    if (AppAudioSuspended)
        return;
    AppAudioSuspended = true;
    const unsigned int paused = EngineInitialized
        ? PauseVoicesForApp()
        : 0;
    if (ModAudioSettings().JMVMiniaudioFullBackendPrototype &&
        ModAudioSettings().JMVMiniaudioStreamingMusicPrototype)
    {
        PauseStreaming(reason);
    }
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    LOGINFO("MINIAUDIO_ONESHOT_APP_AUDIO_STATE suspended=1 event=pause reason=%s affected=%u active=%u",
            reason != nullptr ? reason : "app_pause",
            paused,
            ActiveVoiceCount);
#endif
}

void Resume(const char *reason)
{
    if (!AppAudioSuspended)
        return;
    SyncWorldSfxPause();
    AppAudioSuspended = false;
    if (!EngineInitialized && InitAttempted)
        LastInitAttemptTick = 0;
    const bool deviceReady = !EngineInitialized ||
        EnsureDeviceStarted("app_resume");
    unsigned int ended = 0;
    const unsigned int resumed = EngineInitialized && deviceReady
        ? ResumeVoicesForApp(ended)
        : 0;
    if (ModAudioSettings().JMVMiniaudioFullBackendPrototype &&
        ModAudioSettings().JMVMiniaudioStreamingMusicPrototype)
    {
        ResumeStreaming(reason);
    }
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
    LOGINFO("MINIAUDIO_ONESHOT_APP_AUDIO_STATE suspended=0 event=resume reason=%s device_ready=%d affected=%u ended=%u active=%u",
            reason != nullptr ? reason : "app_resume",
            deviceReady ? 1 : 0,
            resumed,
            ended,
            ActiveVoiceCount);
#endif
}

void SetModalSpeechPaused(
    bool paused,
    bool gameplayPause,
    const char *reason)
{
    const bool diagnosticGameplayPause = paused
        ? gameplayPause
        : GamePauseSpeechSelectorActive;
    GamePauseSpeechSelectorActive = gameplayPause;
    if (ModalSpeechSuspended == paused)
        return;
    ModalSpeechSuspended = paused;

    if (paused)
    {
        unsigned int alreadyPausedByApp = 0;
        const unsigned int affected = EngineInitialized
            ? PauseVoicesForModalSpeech(alreadyPausedByApp)
            : 0;
#if JMV_MINIAUDIO_MODAL_SPEECH_PAUSE_DIAGNOSTICS
        if (!diagnosticGameplayPause)
        {
            LOGINFO("MINIAUDIO_MODAL_SPEECH_STATE paused=1 event=pause reason=%s affected=%u already_app_paused=%u speech_active=%u active=%u app_suspended=%d",
                    reason != nullptr ? reason : "modal_pause",
                    affected,
                    alreadyPausedByApp,
                    CountActiveSpeechVoices(),
                    ActiveVoiceCount,
                    AppAudioSuspended ? 1 : 0);
        }
#endif
#if JMV_MINIAUDIO_GAME_PAUSE_SPEECH_DIAGNOSTICS
        if (diagnosticGameplayPause)
        {
            LOGINFO("MINIAUDIO_GAME_PAUSE_SPEECH_STATE paused=1 event=pause reason=%s affected=%u already_app_paused=%u speech_active=%u active=%u app_suspended=%d",
                    reason != nullptr ? reason : "game_pause",
                    affected,
                    alreadyPausedByApp,
                    CountActiveSpeechVoices(),
                    ActiveVoiceCount,
                    AppAudioSuspended ? 1 : 0);
        }
#endif
        return;
    }

    if (!EngineInitialized && InitAttempted)
        LastInitAttemptTick = 0;
    const bool deviceReady = !EngineInitialized || AppAudioSuspended ||
        EnsureDeviceStarted("modal_speech_resume");
    if (EngineInitialized && !deviceReady)
    {
        ModalSpeechSuspended = true;
#if JMV_MINIAUDIO_MODAL_SPEECH_PAUSE_DIAGNOSTICS
        if (!diagnosticGameplayPause &&
            ShouldLogLimited(ModalSpeechResumeFailureDiagnosticCount))
        {
            LOGWARN("MINIAUDIO_MODAL_SPEECH_STATE paused=1 event=resume_deferred reason=%s device_ready=0 speech_active=%u active=%u total=%u",
                    reason != nullptr ? reason : "modal_resume",
                    CountActiveSpeechVoices(),
                    ActiveVoiceCount,
                    ModalSpeechResumeFailureDiagnosticCount);
        }
#endif
#if JMV_MINIAUDIO_GAME_PAUSE_SPEECH_DIAGNOSTICS
        if (diagnosticGameplayPause &&
            ShouldLogLimited(ModalSpeechResumeFailureDiagnosticCount))
        {
            LOGWARN("MINIAUDIO_GAME_PAUSE_SPEECH_STATE paused=1 event=resume_deferred reason=%s device_ready=0 speech_active=%u active=%u total=%u",
                    reason != nullptr ? reason : "game_pause_resume",
                    CountActiveSpeechVoices(),
                    ActiveVoiceCount,
                    ModalSpeechResumeFailureDiagnosticCount);
        }
#endif
        return;
    }
    unsigned int ended = 0;
    unsigned int stillPausedByApp = 0;
    const unsigned int resumed = EngineInitialized && deviceReady
        ? ResumeVoicesForModalSpeech(ended, stillPausedByApp)
        : 0;
#if JMV_MINIAUDIO_MODAL_SPEECH_PAUSE_DIAGNOSTICS
    if (!diagnosticGameplayPause)
    {
        LOGINFO("MINIAUDIO_MODAL_SPEECH_STATE paused=0 event=resume reason=%s device_ready=%d affected=%u ended=%u still_app_paused=%u speech_active=%u active=%u app_suspended=%d",
                reason != nullptr ? reason : "modal_resume",
                deviceReady ? 1 : 0,
                resumed,
                ended,
                stillPausedByApp,
                CountActiveSpeechVoices(),
                ActiveVoiceCount,
                AppAudioSuspended ? 1 : 0);
    }
#endif
#if JMV_MINIAUDIO_GAME_PAUSE_SPEECH_DIAGNOSTICS
    if (diagnosticGameplayPause)
    {
        LOGINFO("MINIAUDIO_GAME_PAUSE_SPEECH_STATE paused=0 event=resume reason=%s device_ready=%d affected=%u ended=%u still_app_paused=%u speech_active=%u active=%u app_suspended=%d",
                reason != nullptr ? reason : "game_pause_resume",
                deviceReady ? 1 : 0,
                resumed,
                ended,
                stillPausedByApp,
                CountActiveSpeechVoices(),
                ActiveVoiceCount,
                AppAudioSuspended ? 1 : 0);
    }
#endif
}

void Shutdown(const char *reason)
{
    ReleaseStreamingSound(reason != nullptr ? reason : "shutdown");
    ReleaseAllVoices(reason != nullptr ? reason : "shutdown");
    if (ReverbGraphInitialized || ReverbGraphAttempted)
    {
        DestroyMiniaudioReverbGraph(
            true, reason != nullptr ? reason : "shutdown");
    }
    ReleaseDecodedSounds();
    const LONG activeBink = InterlockedCompareExchange(
        &BinkActiveVoiceCount, 0, 0);
    const bool keepEngineForBink = EngineInitialized && activeBink != 0;
    EngineDiagnostics::Event("Audio shutdown reason=%s initialized=%d bink=%ld deferred=%d manager=%p",
        reason?reason:"unknown",EngineInitialized?1:0,activeBink,keepEngineForBink?1:0,CurrentSoundManager);
    if (EngineInitialized && !keepEngineForBink)
    {
#if JMV_MINIAUDIO_ONESHOT_SFX_DIAGNOSTICS
        LOGINFO("MINIAUDIO_ONESHOT_SHUTDOWN reason=%s",
                reason != nullptr ? reason : "shutdown");
#endif
        ma_engine_uninit(&Engine);
    }
    if (!keepEngineForBink)
    {
        AudioResources::Shutdown();
        memset(&Engine, 0, sizeof(Engine));
    }
#if JMV_MINIAUDIO_BINK_DIAGNOSTICS
    else
    {
        LOGINFO("MINIAUDIO_BINK_ENGINE_DEFER reason=%s active=%ld",
                reason != nullptr ? reason : "shutdown",
                activeBink);
    }
#endif
    for (unsigned int i = 0; i < MAX_ACTIVE_VOICES; ++i)
        ResetVoice(Voices[i]);
    EngineInitialized = keepEngineForBink;
    InitAttempted = keepEngineForBink;
    AppAudioSuspended = false;
    ModalSpeechSuspended = false;
    WorldSfxSuspended = false;
    GamePauseSpeechSelectorActive = false;
    LastInitAttemptTick = 0;
    LastDeviceRecoveryAttemptTick = 0;
    ModalSpeechResumeFailureDiagnosticCount = 0;
    if (!keepEngineForBink)
        EngineInitAttemptCount = 0;
    CurrentSoundManager = nullptr;
    LastUniqueCounter = 0;
    LastDeviceState = ma_device_state_uninitialized;
    ClearStreamingMarkersCatalog();
    memset(StreamingSounds, 0, sizeof(StreamingSounds));
    Streaming = SStreamingSound();
    OcclusionWorld = nullptr;
    OcclusionWorldInitialized = false;
    OcclusionNoWorldLogged = false;
    OcclusionLastGlobalTraceTick = 0;
    ReverbGraphAttempted = false;
    ReverbInterfaceBypassLogged = false;
    ReverbSpeechBypassLogged = false;
    ReverbDialogBypassLogged = false;
    BinkDeferredEngineShutdown = keepEngineForBink;
}
} // namespace MiniaudioOneShotSfx
