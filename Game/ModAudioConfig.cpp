#include "StdAfx.h"
#include "ModAudioConfig.h"
#include "FileRegistry.h"
ModAudioConfig& ModAudioSettings(){static ModAudioConfig cfg;return cfg;}
void ConfigureModAudio(CFileRegistry& reg,const std::string& root){auto& c=ModAudioSettings();c=ModAudioConfig{};
c.JMVSpeechMusicDucking=reg.GetInt("Addon","JMVSpeechMusicDucking",0)!=0;
c.JMVFixStepSoundVariantGenerator=reg.GetInt("Addon","JMVFixStepSoundVariantGenerator",0)!=0;
c.JMVSuppressEmptyStartSfx=reg.GetInt("Addon","JMVSuppressEmptyStartSfx",0)!=0;c.addonPath=root+"\\";
c.JMVMiniaudioModalSpeechPausePrototype=reg.GetInt("Addon","JMVMiniaudioModalSpeechPausePrototype",0)!=0;
c.JMVMiniaudioGamePauseSpeechPrototype=reg.GetInt("Addon","JMVMiniaudioGamePauseSpeechPrototype",0)!=0;
c.JMVSfxReverbOutdoorLevel=reg.GetFloat("Addon","JMVSfxReverbOutdoorLevel",.06f);
c.JMVSfxReverbOutdoorReflection=reg.GetFloat("Addon","JMVSfxReverbOutdoorReflection",.08f);
c.JMVSfxReverbOutdoorDecay=reg.GetFloat("Addon","JMVSfxReverbOutdoorDecay",.55f);
c.JMVSfxReverbCaveLevel=reg.GetFloat("Addon","JMVSfxReverbCaveLevel",.22f);
c.JMVSfxReverbCaveReflection=reg.GetFloat("Addon","JMVSfxReverbCaveReflection",.12f);
c.JMVSfxReverbCaveDecay=reg.GetFloat("Addon","JMVSfxReverbCaveDecay",1.8f);
c.JMVMiniaudioBinkOutputPrototype=reg.GetInt("Addon","JMVMiniaudioBinkOutputPrototype",0)!=0;
c.JMVMiniaudioBinkPreciseWaitPrototype=reg.GetInt("Addon","JMVMiniaudioBinkPreciseWaitPrototype",0)!=0;
c.JMVMiniaudioBinkReadyBackpressurePrototype=reg.GetInt("Addon","JMVMiniaudioBinkReadyBackpressurePrototype",0)!=0;
c.JMVMiniaudioMissingAudioCatalogDiagnostic=reg.GetInt("Addon","JMVMiniaudioMissingAudioCatalogDiagnostic",0)!=0;
}
