#include "StdAfx.h"
#include "ModAudioPolicy.h"
#include "ModAudioConfig.h"
#include "SoundManager.h"
#include "GeneralSettings.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include "ackFormat.h"
namespace ModAudioPolicy {
namespace {thread_local MiniaudioOneShotSfx::PartyVoiceBinding ackBinding={};thread_local bool ackActive=false;}
AckScope::AckScope(unsigned unit,unsigned type,bool enabled):previous(ackBinding),wasActive(ackActive){
 ackBinding={unit,0,0};ackActive=false;
 if(!enabled)return;
 switch(type){
 case ACK_UNIT_WHILE_IN_REST:ackBinding.nearDistance=3;ackBinding.farDistance=10;break;
 case ACK_UNIT_BECOME_SUSPECTIOUS:case ACK_UNIT_BECOME_KILLER:ackBinding.nearDistance=10;ackBinding.farDistance=25;break;
 case ACK_UNIT_BECOME_AGGRESSIVE:case ACK_UNIT_WHILE_IN_AGGRESSIVE:ackBinding.nearDistance=15;ackBinding.farDistance=35;break;
 }
 ackActive=ackBinding.farDistance>0;
}
AckScope::~AckScope(){ackBinding=previous;ackActive=wasActive;}
bool TakeAck(bool speech,bool dialog,bool looping,MiniaudioOneShotSfx::PartyVoiceBinding& binding){
 if(!ackActive||!speech||dialog||looping)return false;
 binding=ackBinding;ackActive=false;return true;
}

const float SFX_HEIGHT_LOW = 30.0f;
const float SFX_HEIGHT_HIGH = 70.0f;
const int SFX_VARIATION_VOLUME_PERCENT = 4;
const int SFX_VARIATION_RATE_PERCENT = 2;
const int SFX_DISTANCE_CURVE_BLEND_PERCENT = 50;
const int SFX_OCCLUSION_ATTENUATION_PERCENT = 35;
const DWORD SFX_OCCLUSION_SLOT_TRACE_MS = 250;
const DWORD SFX_OCCLUSION_GLOBAL_TRACE_MS = 25;
const DWORD SFX_OCCLUSION_ATTACK_MS = 180;
const DWORD SFX_OCCLUSION_RELEASE_MS = 350;
const float SFX_SPEECH_DUCKING_FACTOR = 0.75f;
const float SFX_OFFENSIVE_SPELL_DUCKING_FACTOR = 0.90f;
const int SFX_SPEECH_DUCKING_QUIET_VOLUME = 12;
const int SFX_SPEECH_DUCKING_FULL_VOLUME = 28;
const DWORD SFX_SPEECH_DUCKING_ATTACK_MS = 120;
const DWORD SFX_SPEECH_DUCKING_RELEASE_MS = 350;
const int SFX_SPEECH_PROTECTED_PRIORITY = 0x7FFFFFFF;
const DWORD SFX_SPEECH_END_TOLERANCE_MS = 150;
const int SFX_INTERFACE_PRIORITY_FLOOR = 12000;
const int SFX_PRIMARY_ATTACK_PRIORITY_FLOOR = 9000;
const int SFX_COMBAT_PRIORITY_FLOOR = 8000;
const int SFX_BLOCK_IMPACT_PRIORITY_FLOOR = 7000;
const int SFX_STEPS_PRIORITY_FLOOR = 1000;
const float DUCKING_FACTOR = 0.40f;
const DWORD DUCKING_ATTACK_MS = 150;
const DWORD DUCKING_RELEASE_MS = 500;
const DWORD MAX_SEGMENT_DELTA_MS = 1000;
const unsigned int MAX_STEP_SOUND_FAMILIES = 128;
const unsigned int MAX_STEP_SOUND_VARIANTS = 32;
struct SStepSoundFamily
{
    bool used;
    char prefix[192];
    unsigned int variants[MAX_STEP_SOUND_VARIANTS];
    unsigned int count;
};
struct SDuckingState
{
    void *soundManager;
    DWORD lastTick;
    float currentVolume;
    int lastAppliedVolume;
    int lastBaseVolume;
    bool speechActive;
};
struct SSfxSpeechDuckingState
{
    void *soundManager;
    DWORD lastTick;
    float currentFactor;
    bool speechActive;
    unsigned int speechMask;
};
enum ESfxCategory
{
    SFX_CATEGORY_SPEECH = 0,
    SFX_CATEGORY_INTERFACE,
    SFX_CATEGORY_WEAPON,
    SFX_CATEGORY_MAGIC,
    SFX_CATEGORY_COMBAT_VOICE,
    SFX_CATEGORY_STEPS,
    SFX_CATEGORY_AMBIENT,
    SFX_CATEGORY_OTHER,
    SFX_CATEGORY_COUNT
};
SStepSoundFamily StepSoundFamilies[MAX_STEP_SOUND_FAMILIES]={};
SDuckingState DuckingState={};
SSfxSpeechDuckingState SfxSpeechDuckingState={};
uint32_t SfxVariationState=0;
float ClampUnit(float v){return !(v>=0)?0:(v>1?1:v);}
int GetSfxMasterVolume(void* m){return m?static_cast<CSoundManager*>(m)->GetFXMasterVolume():0;}
bool IsFxResourcePresent(void* m,const char* n){return m && n && *n && static_cast<CSoundManager*>(m)->IsFXFileExist(n);}
float GetLinearDistanceGain(void* m,const void* source,float nearDistance,float farDistance){if(!m||!source)return 1.f;CPlot3D listener,orientation;static_cast<CSoundManager*>(m)->GetFXListener(&listener,&orientation);const float* p=static_cast<const float*>(source);float dx=p[0]-listener.x,dy=p[1]-listener.y;float distance=sqrtf(dx*dx+dy*dy);if(!(distance>=0)||!(farDistance>nearDistance))return 1.f;if(distance<=nearDistance)return 1.f;if(distance>=farDistance)return 0.f;return 1.f-(distance-nearDistance)/(farDistance-nearDistance);}
int ClampVolume(int volume)
{
    if (volume < 0)
        return 0;
    if (volume > 100)
        return 100;
    return volume;
}
int ClampFxVolume(int volume)
{
    if (volume < 0)
        return 0;
    if (volume > 127)
        return 127;
    return volume;
}
float MoveTowards(float current, float target, float step)
{
    if (current < target)
    {
        current += step;
        return current > target ? target : current;
    }
    if (current > target)
    {
        current -= step;
        return current < target ? target : current;
    }
    return current;
}
uint32_t NextSfxVariationRandom()
{
    if (SfxVariationState == 0)
    {
        SfxVariationState = static_cast<uint32_t>(GetTickCount()) ^ 0xA341316Cu;
        if (SfxVariationState == 0)
            SfxVariationState = 0xC8013EA4u;
    }

    uint32_t value = SfxVariationState;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    SfxVariationState = value;
    return value;
}
int NextSfxVariationDelta(int maxAbsoluteDelta)
{
    const unsigned int range = static_cast<unsigned int>(maxAbsoluteDelta * 2 + 1);
    return static_cast<int>(NextSfxVariationRandom() % range) - maxAbsoluteDelta;
}
bool IsInterfaceSfx(const char *soundName)
{
    if (soundName == nullptr)
        return false;
    return _strnicmp(soundName, "buttons\\", 8) == 0 ||
        _strnicmp(soundName, "buttons/", 8) == 0;
}
bool IsPathSeparator(char value)
{
    return value == '\\' || value == '/';
}
bool StartsWithPathComponent(const char *soundName, const char *component)
{
    if (soundName == nullptr || component == nullptr)
        return false;

    const size_t componentLength = strlen(component);
    return _strnicmp(soundName, component, componentLength) == 0 &&
        IsPathSeparator(soundName[componentLength]);
}
bool ContainsPathComponent(const char *soundName, const char *component)
{
    if (soundName == nullptr || component == nullptr)
        return false;

    const size_t componentLength = strlen(component);
    for (const char *cursor = soundName; *cursor != '\0'; ++cursor)
    {
        const bool startsComponent = cursor == soundName || IsPathSeparator(cursor[-1]);
        if (!startsComponent || _strnicmp(cursor, component, componentLength) != 0)
            continue;

        const char trailing = cursor[componentLength];
        if (trailing == '\0' || IsPathSeparator(trailing))
            return true;
    }
    return false;
}
bool IsCreatureVoiceSfx(const char *soundName)
{
    // Creature vocalizations live directly under these roots. Footsteps use
    // steps\animals and therefore keep their normal SFX variation.
    return StartsWithPathComponent(soundName, "monsters") ||
        StartsWithPathComponent(soundName, "animals");
}
bool IsRangedLaunchSfx(const char *soundName)
{
    if (soundName == nullptr || _strnicmp(soundName, "weapons", 7) != 0 ||
        !IsPathSeparator(soundName[7]))
    {
        return false;
    }

    const char *cursor = soundName + 8;
    const char *digits = cursor;
    while (*cursor >= '0' && *cursor <= '9')
        ++cursor;
    return cursor != digits && _stricmp(cursor, ".wav") == 0;
}
bool IsOffensiveSpellSfx(const char *soundName)
{
    if (!StartsWithPathComponent(soundName, "magic"))
        return false;

    const char *spellName = soundName + 6;
    return StartsWithPathComponent(spellName, "Firearrow") ||
        StartsWithPathComponent(spellName, "Acidray") ||
        StartsWithPathComponent(spellName, "lightning") ||
        StartsWithPathComponent(spellName, "Fireball") ||
        StartsWithPathComponent(spellName, "InvokeLight") ||
        StartsWithPathComponent(spellName, "Acidcolumn") ||
        StartsWithPathComponent(spellName, "Firewall") ||
        StartsWithPathComponent(spellName, "Lightwall") ||
        StartsWithPathComponent(spellName, "poison");
}
float GetMiniaudioDistanceCurveFactor(
    void *soundManager,
    const void *source,
    float minDistance,
    float maxDistance)
{
    if (!ModAudioSettings().JMVSfxDistanceCurve)
        return 1.0f;
    const float linearGain = GetLinearDistanceGain(
        soundManager, source, minDistance, maxDistance);
    if (!(linearGain > 0.0f) || linearGain >= 1.0f)
        return 1.0f;
    const float normalized = 1.0f - linearGain;
    const float smoothstep =
        normalized * normalized * (3.0f - 2.0f * normalized);
    const float smoothGain = 1.0f - smoothstep;
    const float blend =
        static_cast<float>(SFX_DISTANCE_CURVE_BLEND_PERCENT) / 100.0f;
    const float curvedGain = linearGain + (smoothGain - linearGain) * blend;
    return curvedGain / linearGain;
}
float GetMiniaudioDuckingFactor(
    void *soundManager,
    const char *soundName,
    bool interfaceSfx,
    float audibleGain)
{
    if (!ModAudioSettings().JMVSfxSpeechDucking || interfaceSfx ||
        SfxSpeechDuckingState.soundManager != soundManager ||
        SfxSpeechDuckingState.currentFactor >= 1.0f)
    {
        return 1.0f;
    }

    const int baselineVolume = ClampFxVolume(static_cast<int>(
        127.0f * static_cast<float>(GetSfxMasterVolume(soundManager)) * 0.01f *
        audibleGain + 0.5f));
    if (baselineVolume <= SFX_SPEECH_DUCKING_QUIET_VOLUME)
        return 1.0f;

    const bool offensiveSpell = ModAudioSettings().JMVSfxOffensiveSpellDucking &&
        IsOffensiveSpellSfx(soundName);
    const float selectedFactor = offensiveSpell &&
        SfxSpeechDuckingState.currentFactor < SFX_OFFENSIVE_SPELL_DUCKING_FACTOR
        ? SFX_OFFENSIVE_SPELL_DUCKING_FACTOR
        : SfxSpeechDuckingState.currentFactor;
    const float loudness = ClampUnit(
        static_cast<float>(baselineVolume - SFX_SPEECH_DUCKING_QUIET_VOLUME) /
        static_cast<float>(
            SFX_SPEECH_DUCKING_FULL_VOLUME -
            SFX_SPEECH_DUCKING_QUIET_VOLUME));
    return 1.0f - (1.0f - selectedFactor) * loudness;
}
float ResolveMiniaudioVirtualDynamicGain(
    void *soundManager,
    const char *soundName,
    bool spatial,
    bool looping,
    bool speech,
    bool dialog,
    int manualVolume,
    const float *position,
    float minDistance,
    float maxDistance)
{
    if (looping || speech || dialog)
        return 1.0f;

    float distanceCurveFactor = 1.0f;
    float audibleGain = static_cast<float>(ClampVolume(manualVolume)) *
        0.01f;
    if (spatial)
    {
        const float linearGain = GetLinearDistanceGain(
            soundManager, position, minDistance, maxDistance);
        distanceCurveFactor = GetMiniaudioDistanceCurveFactor(
            soundManager, position, minDistance, maxDistance);
        audibleGain = linearGain * distanceCurveFactor;
    }

    return distanceCurveFactor * GetMiniaudioDuckingFactor(
        soundManager,
        soundName,
        IsInterfaceSfx(soundName),
        audibleGain);
}
void GetMiniaudioVariationFactors(
    const char *soundName,
    float &volumeFactor,
    float &pitchFactor)
{
    volumeFactor = 1.0f;
    pitchFactor = 1.0f;
    if (IsCreatureVoiceSfx(soundName))
    {
#if JMV_DETEST_BUILD
        LogCreatureVoiceVariationBypass(soundName, "miniaudio");
#endif
        return;
    }
    if (!ModAudioSettings().JMVSfxVariation || IsInterfaceSfx(soundName) ||
        !MiniaudioOneShotSfx::IsCaptured(soundName))
    {
        return;
    }
    volumeFactor = static_cast<float>(
        100 + NextSfxVariationDelta(SFX_VARIATION_VOLUME_PERCENT)) * 0.01f;
    pitchFactor = static_cast<float>(
        100 + NextSfxVariationDelta(SFX_VARIATION_RATE_PERCENT)) * 0.01f;
}
bool IsBlockImpactSfx(const char *soundName)
{
    if (!StartsWithPathComponent(soundName, "weapons"))
        return false;
    return StartsWithPathComponent(soundName + 8, "Blocks");
}
ESfxCategory ClassifySfx(
    const char *soundName,
    BOOL looping,
    BOOL isSpeech,
    BOOL dialogPlayMode)
{
    if (isSpeech != FALSE || dialogPlayMode != FALSE)
        return SFX_CATEGORY_SPEECH;
    if (StartsWithPathComponent(soundName, "buttons"))
        return SFX_CATEGORY_INTERFACE;
    if (StartsWithPathComponent(soundName, "weapons"))
        return SFX_CATEGORY_WEAPON;
    if (StartsWithPathComponent(soundName, "magic"))
        return SFX_CATEGORY_MAGIC;
    if (ContainsPathComponent(soundName, "attack") ||
        ContainsPathComponent(soundName, "hit"))
    {
        return SFX_CATEGORY_COMBAT_VOICE;
    }
    if (StartsWithPathComponent(soundName, "steps"))
        return SFX_CATEGORY_STEPS;
    if (looping != FALSE || StartsWithPathComponent(soundName, "ambient") ||
        StartsWithPathComponent(soundName, "nature"))
    {
        return SFX_CATEGORY_AMBIENT;
    }
    return SFX_CATEGORY_OTHER;
}
const char *GetSfxCategoryName(ESfxCategory category)
{
    switch (category)
    {
    case SFX_CATEGORY_SPEECH:
        return "speech";
    case SFX_CATEGORY_INTERFACE:
        return "interface";
    case SFX_CATEGORY_WEAPON:
        return "weapon";
    case SFX_CATEGORY_MAGIC:
        return "magic";
    case SFX_CATEGORY_COMBAT_VOICE:
        return "combat_voice";
    case SFX_CATEGORY_STEPS:
        return "steps";
    case SFX_CATEGORY_AMBIENT:
        return "ambient";
    default:
        return "other";
    }
}
int GetSfxCategoryPriorityFloor(ESfxCategory category)
{
    switch (category)
    {
    case SFX_CATEGORY_INTERFACE:
        return SFX_INTERFACE_PRIORITY_FLOOR;
    case SFX_CATEGORY_WEAPON:
    case SFX_CATEGORY_MAGIC:
    case SFX_CATEGORY_COMBAT_VOICE:
        return SFX_COMBAT_PRIORITY_FLOOR;
    case SFX_CATEGORY_STEPS:
        return SFX_STEPS_PRIORITY_FLOOR;
    default:
        return 0;
    }
}
int ApplySfxCategoryPriorityFloor(int originalPriority, ESfxCategory category)
{
    const int priorityFloor = GetSfxCategoryPriorityFloor(category);
    return originalPriority < priorityFloor ? priorityFloor : originalPriority;
}
int ApplySfxPriorityPolicy(
    int originalPriority,
    ESfxCategory category,
    const char *soundName,
    BOOL looping,
    const char *&tier)
{
    tier = "baseline";
    if (ModAudioSettings().JMVSfxCombatPriorityHierarchy)
    {
        int priorityFloor = 0;
        if (looping == FALSE &&
            (IsRangedLaunchSfx(soundName) || IsOffensiveSpellSfx(soundName)))
        {
            priorityFloor = SFX_PRIMARY_ATTACK_PRIORITY_FLOOR;
            tier = "primary_attack";
        }
        else if (IsBlockImpactSfx(soundName))
        {
            priorityFloor = SFX_BLOCK_IMPACT_PRIORITY_FLOOR;
            tier = "block_impact";
        }

        if (priorityFloor != 0)
            return originalPriority < priorityFloor ? priorityFloor : originalPriority;
    }

    if (ModAudioSettings().JMVSfxCategoryPriorities)
    {
        tier = "category";
        return ApplySfxCategoryPriorityFloor(originalPriority, category);
    }
    return originalPriority;
}
SStepSoundFamily *GetStepSoundFamily(
    void *soundManager,
    const char *prefix,
    size_t prefixLength)
{
    if (prefix == nullptr || prefixLength == 0 ||
        prefixLength >= sizeof(StepSoundFamilies[0].prefix))
    {
        return nullptr;
    }

    SStepSoundFamily *freeFamily = nullptr;
    for (unsigned int i = 0; i < MAX_STEP_SOUND_FAMILIES; ++i)
    {
        SStepSoundFamily &family = StepSoundFamilies[i];
        if (!family.used)
        {
            if (freeFamily == nullptr)
                freeFamily = &family;
            continue;
        }
        if (strlen(family.prefix) == prefixLength &&
            _strnicmp(family.prefix, prefix, prefixLength) == 0)
        {
            return &family;
        }
    }
    if (freeFamily == nullptr)
        return nullptr;

    memcpy(freeFamily->prefix, prefix, prefixLength);
    freeFamily->prefix[prefixLength] = '\0';
    freeFamily->count = 0;
    for (unsigned int variant = 1;
         variant <= MAX_STEP_SOUND_VARIANTS;
         ++variant)
    {
        char candidate[256] = {};
        const int written = _snprintf_s(
            candidate,
            sizeof(candidate),
            _TRUNCATE,
            "%s%u.wav",
            freeFamily->prefix,
            variant);
        if (written > 0 && IsFxResourcePresent(soundManager, candidate))
        {
            freeFamily->variants[freeFamily->count++] = variant;
        }
    }
    freeFamily->used = true;
    return freeFamily;
}
bool ApplyAudioResourceNameFixes(
    void *soundManager,
    const char *requestPath,
    const char *requestedName,
    char *correctedName,
    size_t correctedCapacity,
    const char *&effectiveName)
{
    effectiveName = requestedName;
    if (requestedName == nullptr || correctedName == nullptr ||
        correctedCapacity == 0)
    {
        return true;
    }

    if (ModAudioSettings().JMVSuppressEmptyStartSfx &&
        (_stricmp(requestedName, "\\start.wav") == 0 ||
         _stricmp(requestedName, "/start.wav") == 0))
    {
#if JMV_AUDIO_RESOURCE_NAME_FIX_DIAGNOSTICS
        if (ShouldLogAudioResourceNameFix(EmptyStartSuppressDiagnosticCount))
        {
            LOGINFO("AUDIO_NAME_EMPTY_START_SUPPRESS path=%s requested=%s action=suppress miles_called=0 total=%u",
                    requestPath != nullptr ? requestPath : "unknown",
                    requestedName,
                    EmptyStartSuppressDiagnosticCount);
        }
#endif
        return false;
    }

    if (!ModAudioSettings().JMVFixStepSoundVariantGenerator ||
        !StartsWithPathComponent(requestedName, "steps"))
    {
        return true;
    }

    const char *lastSeparator = nullptr;
    for (const char *cursor = requestedName; *cursor != '\0'; ++cursor)
    {
        if (IsPathSeparator(*cursor))
            lastSeparator = cursor;
    }
    if (lastSeparator == nullptr || lastSeparator[1] == '\0')
        return true;

    char *numberEnd = nullptr;
    const unsigned long requestedVariant = strtoul(
        lastSeparator + 1, &numberEnd, 10);
    if (requestedVariant == 0 || requestedVariant > 0xFFFFFFFFul ||
        numberEnd == lastSeparator + 1 || _stricmp(numberEnd, ".wav") != 0)
    {
        return true;
    }

    const size_t prefixLength =
        static_cast<size_t>(lastSeparator + 1 - requestedName);
    SStepSoundFamily *family = GetStepSoundFamily(
        soundManager, requestedName, prefixLength);
    if (family == nullptr)
        return true;

    if (family->count == 0)
    {
        if (prefixLength == 6 &&
            _strnicmp(requestedName, "steps", 5) == 0)
        {
#if JMV_AUDIO_RESOURCE_NAME_FIX_DIAGNOSTICS
            if (ShouldLogAudioResourceNameFix(StepSoundSuppressDiagnosticCount))
            {
                LOGINFO("AUDIO_NAME_STEP_SUPPRESS path=%s requested=%s reason=empty_family variants=0 miles_called=0 total=%u",
                        requestPath != nullptr ? requestPath : "unknown",
                        requestedName,
                        StepSoundSuppressDiagnosticCount);
            }
#endif
            return false;
        }
        return true;
    }

    const unsigned int selected = family->variants[
        static_cast<unsigned int>((requestedVariant - 1) % family->count)];
    if (selected == requestedVariant)
        return true;

    const int written = _snprintf_s(
        correctedName,
        correctedCapacity,
        _TRUNCATE,
        "%s%u.wav",
        family->prefix,
        selected);
    if (written <= 0)
        return true;

    effectiveName = correctedName;
#if JMV_AUDIO_RESOURCE_NAME_FIX_DIAGNOSTICS
    if (ShouldLogAudioResourceNameFix(StepSoundRewriteDiagnosticCount))
    {
        LOGINFO("AUDIO_NAME_STEP_REWRITE path=%s requested=%s replacement=%s variants=%u selected=%u target_present=1 total=%u",
                requestPath != nullptr ? requestPath : "unknown",
                requestedName,
                effectiveName,
                family->count,
                selected,
                StepSoundRewriteDiagnosticCount);
    }
#endif
    return true;
}
void ResetSfxSpeechDuckingState(void *soundManager, DWORD now)
{
    SfxSpeechDuckingState.soundManager = soundManager;
    SfxSpeechDuckingState.lastTick = now;
    SfxSpeechDuckingState.currentFactor = 1.0f;
    SfxSpeechDuckingState.speechActive = false;
    SfxSpeechDuckingState.speechMask = 0;
#if JMV_SFX_SPEECH_DUCKING_DIAGNOSTICS
    LOGINFO("SFXDUCK_MANAGER manager=0x%p", soundManager);
#endif
}
void UpdateSfxSpeechDuckingState(
    void *soundManager,
    DWORD now,
    bool speechActive,
    unsigned int speechCount,
    unsigned int speechMask)
{
    if (SfxSpeechDuckingState.soundManager != soundManager)
        ResetSfxSpeechDuckingState(soundManager, now);

    const float targetFactor = speechActive
        ? SFX_SPEECH_DUCKING_FACTOR
        : 1.0f;
    if (speechActive != SfxSpeechDuckingState.speechActive)
    {
#if JMV_SFX_SPEECH_DUCKING_DIAGNOSTICS
        LOGINFO("SFXDUCK_STATE active=%d count=%u mask=0x%04X target_percent=%d current_percent=%d",
                speechActive ? 1 : 0,
                speechCount,
                speechMask,
                static_cast<int>(targetFactor * 100.0f + 0.5f),
                static_cast<int>(SfxSpeechDuckingState.currentFactor * 100.0f + 0.5f));
#endif
        SfxSpeechDuckingState.speechActive = speechActive;
    }
    SfxSpeechDuckingState.speechMask = speechMask;

    DWORD deltaMs = now - SfxSpeechDuckingState.lastTick;
    SfxSpeechDuckingState.lastTick = now;
    if (deltaMs > MAX_SEGMENT_DELTA_MS)
        deltaMs = MAX_SEGMENT_DELTA_MS;

    const DWORD transitionMs =
        SfxSpeechDuckingState.currentFactor > targetFactor
        ? SFX_SPEECH_DUCKING_ATTACK_MS
        : SFX_SPEECH_DUCKING_RELEASE_MS;
    const float transitionSpan = 1.0f - SFX_SPEECH_DUCKING_FACTOR;
    const float step = transitionSpan * static_cast<float>(deltaMs) /
        static_cast<float>(transitionMs);
    SfxSpeechDuckingState.currentFactor = MoveTowards(
        SfxSpeechDuckingState.currentFactor, targetFactor, step);
}
void ResetDuckingState(void *soundManager, int baseVolume, DWORD now)
{
    DuckingState.soundManager = soundManager;
    DuckingState.lastTick = now;
    DuckingState.currentVolume = static_cast<float>(baseVolume);
    DuckingState.lastAppliedVolume = baseVolume;
    DuckingState.lastBaseVolume = baseVolume;
    DuckingState.speechActive = false;
#if JMV_SPEECH_MUSIC_DUCKING_DIAGNOSTICS
    LOGINFO("AUDIODUCK_MANAGER manager=0x%p base=%d", soundManager, baseVolume);
#endif
}
int GetBaseMusicVolume(){return ClampVolume(theSettings.nAudioStreaming);}
void ApplyMusicVolume(void* m,int volume){static_cast<CSoundManager*>(m)->SetMusicMasterVolume(ClampVolume(volume));}
void Tick(CSoundManager* soundManager){if(!soundManager||!soundManager->IsActive()){SfxSpeechDuckingState={};DuckingState={};return;}const DWORD now=GetTickCount();unsigned int speechMask=0;const unsigned int speechCount=soundManager->CountNativeAudibleSpeech(speechMask);const bool speechActive=speechCount!=0;
    if (ModAudioSettings().JMVSpeechMusicDucking)
    {
        const int baseVolume = GetBaseMusicVolume();
        if (DuckingState.soundManager != soundManager)
            ResetDuckingState(soundManager, baseVolume, now);

        const float targetVolume = speechActive
            ? static_cast<float>(baseVolume) * DUCKING_FACTOR
            : static_cast<float>(baseVolume);

        if (baseVolume != DuckingState.lastBaseVolume)
        {
            if (DuckingState.speechActive)
            {
                DuckingState.currentVolume = speechActive
                    ? targetVolume
                    : static_cast<float>(baseVolume);
                DuckingState.lastAppliedVolume = -1;
            }
            else
            {
                // The game already applied the new slider value before this segment.
                DuckingState.currentVolume = static_cast<float>(baseVolume);
                DuckingState.lastAppliedVolume = baseVolume;
            }
            DuckingState.lastBaseVolume = baseVolume;
            DuckingState.lastTick = now;
        }

        if (speechActive != DuckingState.speechActive)
        {
#if JMV_SPEECH_MUSIC_DUCKING_DIAGNOSTICS
            LOGINFO("AUDIODUCK_STATE active=%d count=%u mask=0x%04X base=%d target=%d current=%d",
                    speechActive ? 1 : 0,
                    speechCount,
                    speechMask,
                    baseVolume,
                    static_cast<int>(targetVolume + 0.5f),
                    static_cast<int>(DuckingState.currentVolume + 0.5f));
#endif
            DuckingState.speechActive = speechActive;
        }

        DWORD deltaMs = now - DuckingState.lastTick;
        DuckingState.lastTick = now;
        if (deltaMs > MAX_SEGMENT_DELTA_MS)
            deltaMs = MAX_SEGMENT_DELTA_MS;

        const DWORD transitionMs = DuckingState.currentVolume > targetVolume
            ? DUCKING_ATTACK_MS
            : DUCKING_RELEASE_MS;
        float transitionSpan =
            static_cast<float>(baseVolume) * (1.0f - DUCKING_FACTOR);
        if (transitionSpan < 1.0f)
            transitionSpan = 1.0f;
        const float step = transitionSpan * static_cast<float>(deltaMs) /
            static_cast<float>(transitionMs);
        DuckingState.currentVolume = MoveTowards(
            DuckingState.currentVolume, targetVolume, step);

        const int requestedVolume = ClampVolume(
            static_cast<int>(DuckingState.currentVolume + 0.5f));
        if (requestedVolume != DuckingState.lastAppliedVolume)
        {
            ApplyMusicVolume(soundManager, requestedVolume);
            DuckingState.lastAppliedVolume = requestedVolume;
        }
    }

    if (ModAudioSettings().JMVSfxSpeechDucking)
    {
        UpdateSfxSpeechDuckingState(
            soundManager, now, speechActive, speechCount, speechMask);
    }
}
void Reset(){SfxSpeechDuckingState={};DuckingState={};memset(StepSoundFamilies,0,sizeof(StepSoundFamilies));}
void Connect(){MiniaudioOneShotSfx::SetVirtualDynamicGainResolver(ResolveMiniaudioVirtualDynamicGain);}
int Priority(int priority,const char* name,bool looping,bool speech,bool dialog){const char* tier=nullptr;const int category=ApplySfxPriorityPolicy(priority,ClassifySfx(name,looping,speech,dialog),name,looping,tier);return ModAudioSettings().JMVSfxSpeechPriorityProtection&&(speech||dialog)?SFX_SPEECH_PROTECTED_PRIORITY:category;}
void Variation(const char* name,bool looping,bool speech,bool dialog,float& volume,float& pitch){volume=pitch=1.f;if(!looping&&!speech&&!dialog)GetMiniaudioVariationFactors(name,volume,pitch);}
bool Name(CSoundManager* manager,const char* requested,char* buffer,size_t size,const char*& effective){return ApplyAudioResourceNameFixes(manager,"native",requested,buffer,size,effective);}

bool ReuseIdle(CSoundManager* manager,unsigned id,const char* name){return manager && IsCreatureVoiceSfx(name) && MiniaudioOneShotSfx::IsVirtualSound(manager,id);}

bool Check(){
 auto require=[](bool b,const char* why){if(!b)throw std::runtime_error(why);};
 require(Priority(5,"buttons\\click.wav",false,false,false)==12000,"Interface priority");
 require(Priority(5,"weapons\\1.wav",false,false,false)==9000,"Ranged launch priority");
 require(Priority(5,"magic\\Fireball\\start.wav",false,false,false)==9000,"Offensive spell priority");
 require(Priority(5,"weapons\\Blocks\\1.wav",false,false,false)==7000,"Block priority");
 require(Priority(5,"steps\\animals\\1.wav",false,false,false)==1000,"Steps priority");
 require(Priority(15000,"weapons\\1.wav",false,false,false)==15000,"Preserve higher priority");
 require(Priority(5,"voice.wav",false,true,false)==0x7fffffff,"Speech protection");
 require(!IsCreatureVoiceSfx("steps\\animals\\1.wav") && IsCreatureVoiceSfx("animals\\1.wav"),"Creature versus step variation");
 MiniaudioOneShotSfx::PartyVoiceBinding binding={};
 {AckScope outer(123,ACK_UNIT_WHILE_IN_REST,true);
  {AckScope nested(456,ACK_UNIT_BECOME_AGGRESSIVE,true);require(TakeAck(true,false,false,binding)&&binding.speakerId==456&&binding.nearDistance==15&&binding.farDistance==35,"Nested aggressive ack");require(!TakeAck(true,false,false,binding),"Ack consumed once");}
  require(!TakeAck(true,true,false,binding)&&!TakeAck(true,false,true,binding),"Dialog and loop bypass");
  require(TakeAck(true,false,false,binding)&&binding.speakerId==123&&binding.nearDistance==3&&binding.farDistance==10,"Restore outer rest ack");
 }
 require(!TakeAck(true,false,false,binding),"Ack scope cleared");
 {AckScope disabled(123,ACK_UNIT_WHILE_IN_REST,false);require(!TakeAck(true,false,false,binding),"Nonmod and multiplayer bypass");}
 require(!ReuseIdle(nullptr,0,"animals\\1.wav"),"Absent idle manager");
 float volume,pitch;Variation("animals\\1.wav",false,false,false,volume,pitch);require(volume==1 && pitch==1,"Creature voice untouched");
 const unsigned char wav[]={82,73,70,70,44,0,0,0,87,65,86,69,102,109,116,32,16,0,0,0,1,0,1,0,64,31,0,0,128,62,0,0,2,0,16,0,100,97,116,97,8,0,0,0,0,0,1,0,255,255,0,0};
 require(MiniaudioOneShotSfx::CaptureEncoded("r28.wav",wav,sizeof(wav),"headless_policy"),"Variation resource capture");
 const auto oldRandom=SfxVariationState;SfxVariationState=123;bool varied=false;
 for(int i=0;i<32;++i){Variation("r28.wav",false,false,false,volume,pitch);require(volume>=.9599f&&volume<=1.0401f&&pitch>=.9799f&&pitch<=1.0201f,"Variation bounds");varied|=volume!=1||pitch!=1;}
 SfxVariationState=oldRandom;require(varied,"Captured SFX variation active");
 Variation("r28.wav",true,false,false,volume,pitch);require(volume==1&&pitch==1,"Loop bypass");
 auto previous=SfxSpeechDuckingState;ResetSfxSpeechDuckingState(nullptr,100);UpdateSfxSpeechDuckingState(nullptr,220,true,2,0x80000000u);require(fabs(SfxSpeechDuckingState.currentFactor-.75f)<.00001,"Speech SFX attack");UpdateSfxSpeechDuckingState(nullptr,570,false,0,0);require(SfxSpeechDuckingState.currentFactor==1,"Speech SFX release");SfxSpeechDuckingState=previous;
 auto& cfg=ModAudioSettings();bool suppress=cfg.JMVSuppressEmptyStartSfx;cfg.JMVSuppressEmptyStartSfx=true;char buffer[256]={};const char* effective=nullptr;bool allowed=Name(nullptr,"\\start.wav",buffer,sizeof(buffer),effective);cfg.JMVSuppressEmptyStartSfx=suppress;require(!allowed,"Empty root start suppression");
 MiniaudioOneShotSfx::Shutdown("headless_policy_cleanup");return true;
}
}
