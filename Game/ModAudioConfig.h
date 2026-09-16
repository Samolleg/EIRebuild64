#pragma once
#include <string>
class CFileRegistry;
struct ModAudioConfig {
 static constexpr bool JMVSfxVariation=true,JMVSfxDistanceCurve=true,JMVSfxSpeechDucking=true,JMVSfxSpeechPriorityProtection=true,JMVSfxCategoryPriorities=true,JMVSfxCombatPriorityHierarchy=true,JMVSfxOffensiveSpellDucking=true;
 bool JMVSpeechMusicDucking=false,JMVFixStepSoundVariantGenerator=false,JMVSuppressEmptyStartSfx=false;
 bool JMVMiniaudioModalSpeechPausePrototype=false,JMVMiniaudioGamePauseSpeechPrototype=false;
 bool JMVMiniaudioBinauralPrototype=true;
 bool JMVMiniaudioBinkPreciseWaitPrototype=false;
 bool JMVMiniaudioBinkReadyBackpressurePrototype=false;
 bool JMVMiniaudioFullBackendPrototype=true;
 bool JMVMiniaudioMissingAudioCatalogDiagnostic=false;
 bool JMVMiniaudioOcclusionPrototype=true;
 bool JMVMiniaudioOneShotSfxPrototype=true;
 bool JMVMiniaudioOneShotStrictTakeover=false;
 bool JMVMiniaudioReverbPrototype=true;
 bool JMVMiniaudioStreamingMarkersPrototype=true;
 bool JMVMiniaudioStreamingMusicPrototype=true;
 float JMVSfxReverbCaveDecay=1.8f;
 float JMVSfxReverbCaveLevel=.22f;
 float JMVSfxReverbCaveReflection=.12f;
 float JMVSfxReverbOutdoorDecay=.55f;
 float JMVSfxReverbOutdoorLevel=.06f;
 float JMVSfxReverbOutdoorReflection=.08f;
 std::string addonPath;
};
ModAudioConfig& ModAudioSettings();
void ConfigureModAudio(CFileRegistry&,const std::string&);
