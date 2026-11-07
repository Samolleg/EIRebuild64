#pragma once

#include <stdint.h>

class CStringArray;

namespace MiniaudioOneShotSfx
{
// Native mixer facts for a request-driven observer. No new playback owners.
struct LiveAudioEntry {
    char file[260]{},name[128]{};
    uint64_t sequence=0,started=0;
    uint32_t id=0,sourceId=0;
    int priority=0;
    float gain=0,position[3]{};
    bool music=false,speech=false,dialog=false,spatial=false,looping=false;
    bool paused=false,playing=false,scenario=false;
};
struct LiveAudioSnapshot {
    LiveAudioEntry current[65]{},recent[256]{};
    unsigned currentCount=0,recentCount=0;
    uint64_t now=0,historySince=0,overwritten=0;
    bool engineInitialized=false,outputAvailable=false;
};
void ReadLiveAudioSnapshot(LiveAudioSnapshot&) noexcept;
// Host-thread device snapshot: display names and stable endpoint IDs, same indices.
bool GetPlaybackDevices(CStringArray& names, CStringArray& ids);
// Host-thread API: release after the video sound has detached.
void* AcquireVideoEngine();
void ReleaseVideoEngine();
bool VideoAudioEnabled();
bool OutputDeviceAvailable(); // Atomic query, also safe on decoder threads.
bool VideoSuspended();
float VideoGain();
double VideoTimeSeconds();
struct PartyVoiceBinding
{
    uint32_t speakerId;
    float nearDistance;
    float farDistance;
};

enum EStartResult
{
    START_NOT_HANDLED,
    STARTED,
    START_SUPPRESSED,
    START_FAILED
};

typedef float (*VirtualDynamicGainResolver)(
    void *soundManager,
    const char *soundName,
    bool spatial,
    bool looping,
    bool speech,
    bool dialog,
    int manualVolume,
    const float *position,
    float minDistance,
    float maxDistance);

bool CaptureEncoded(
    const char *soundName,
    const void *data,
    uint32_t size,
    const char *source = "stock_beginfxll",
    uintptr_t providerIdentity = 0);
bool IsCaptured(const char *soundName);
const char *LastStartReason();
void SetVirtualDynamicGainResolver(VirtualDynamicGainResolver resolver);
void RecordMissingAudioResource(
    const char *requestedName,
    const char *domain,
    const char *targetName,
    const char *path,
    const char *soundClass,
    const char *reason);
void DumpMissingAudioCatalog(const char *reason);

EStartResult TryStartFlat(
    void *soundManager,
    const char *soundName,
    int priority,
    int percentVolume,
    int percentPan,
    float gainFactor,
    float pitchFactor);

EStartResult TryStart3D(
    void *soundManager,
    const char *soundName,
    int priority,
    const void *source,
    float minDistance,
    float maxDistance,
    float gainFactor,
    float pitchFactor);


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
    const PartyVoiceBinding *partyVoice = nullptr,
    bool scenarioSpeech = false);

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
    float pitchFactor);

uint32_t CaptureVoiceWait(void *soundManager, uint32_t soundId);
bool IsVoiceWaitActive(uint32_t ticket);
bool IsVirtualSound(void *soundManager, uint32_t virtualId);
bool UpdateVirtualFlat(
    void *soundManager,
    uint32_t virtualId,
    int percentVolume,
    int percentPan);
bool UpdateVirtual3D(
    void *soundManager,
    uint32_t virtualId,
    const void *source,
    float minDistance,
    float maxDistance);
bool CancelVirtual(void *soundManager, uint32_t virtualId);
float GetVirtualPlaybackTime(void *soundManager, uint32_t virtualId);
unsigned int CountAudibleVirtualSpeech(void *soundManager);

bool SetStreamingResource(
    void *soundManager,
    const char *resourcePath,
    const char *markersPath);
bool BeginStreaming(void *soundManager, const char *soundName, int markerType);
void EndStreaming(int markerType, const char *reason);
void SetStreamingVolume(int percentVolume);
void FadeOutStreaming();
void FadeInStreaming();
void PauseStreaming(const char *reason);
void ResumeStreaming(const char *reason);

void PrepareNativeManager(void *soundManager);
const char* GetNativeStreamingName();
bool IsNativeStreamingSwitching();
void Tick(void *soundManager);
void StopAll(const char *reason);
void Pause(const char *reason);
void Resume(const char *reason);
void SetModalSpeechPaused(
    bool paused,
    bool gameplayPause,
    const char *reason);
void Shutdown(const char *reason);
} // namespace MiniaudioOneShotSfx
