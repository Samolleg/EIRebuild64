#include "StdAfx.h"
#include "ModAudioConfig.h"
#include "FileRegistry.h"
#include "ModConfiguration.h"
ModAudioConfig& ModAudioSettings(){static ModAudioConfig cfg;return cfg;}
void ConfigureModAudio(CFileRegistry& reg,const std::string& root){auto& c=ModAudioSettings();c=ModAudioConfig{};
c.JMVSpeechMusicDucking=ModConfiguration::Bool(reg,"Addon","JMVSpeechMusicDucking",false);
c.JMVFixStepSoundVariantGenerator=ModConfiguration::Bool(reg,"Addon","JMVFixStepSoundVariantGenerator",false);
c.JMVSuppressEmptyStartSfx=ModConfiguration::Bool(reg,"Addon","JMVSuppressEmptyStartSfx",false);c.addonPath=root+"\\";
c.JMVMiniaudioModalSpeechPausePrototype=ModConfiguration::Bool(reg,"Addon","JMVMiniaudioModalSpeechPausePrototype",false);
c.JMVMiniaudioGamePauseSpeechPrototype=ModConfiguration::Bool(reg,"Addon","JMVMiniaudioGamePauseSpeechPrototype",false);
c.JMVSfxReverbOutdoorLevel=ModConfiguration::Float(reg,"Addon","JMVSfxReverbOutdoorLevel",.06f);
c.JMVSfxReverbOutdoorReflection=ModConfiguration::Float(reg,"Addon","JMVSfxReverbOutdoorReflection",.08f);
c.JMVSfxReverbOutdoorDecay=ModConfiguration::Float(reg,"Addon","JMVSfxReverbOutdoorDecay",.55f);
c.JMVSfxReverbCaveLevel=ModConfiguration::Float(reg,"Addon","JMVSfxReverbCaveLevel",.22f);
c.JMVSfxReverbCaveReflection=ModConfiguration::Float(reg,"Addon","JMVSfxReverbCaveReflection",.12f);
c.JMVSfxReverbCaveDecay=ModConfiguration::Float(reg,"Addon","JMVSfxReverbCaveDecay",1.8f);
c.JMVMiniaudioBinkOutputPrototype=ModConfiguration::Bool(reg,"Addon","JMVMiniaudioBinkOutputPrototype",false);
c.JMVMiniaudioBinkPreciseWaitPrototype=ModConfiguration::Bool(reg,"Addon","JMVMiniaudioBinkPreciseWaitPrototype",false);
c.JMVMiniaudioBinkReadyBackpressurePrototype=ModConfiguration::Bool(reg,"Addon","JMVMiniaudioBinkReadyBackpressurePrototype",false);
c.JMVMiniaudioMissingAudioCatalogDiagnostic=ModConfiguration::Bool(reg,"Addon","JMVMiniaudioMissingAudioCatalogDiagnostic",false);
}
