from pathlib import Path
import re,shutil,hashlib,json
r=Path(__file__).resolve().parent
s=r.parent/'src_addon_world_background_data_20260916'
e=r/'evidence/addon-integration'; pre=e/'pre-r27';pre.mkdir(exist_ok=True)
def edit(name,fn):
 p=r/name;b=p.read_bytes();q=pre/name;q.parent.mkdir(parents=True,exist_ok=True)
 if not q.exists():q.write_bytes(b)
 t=b.decode('cp1251');out=fn(t);p.write_bytes(out.encode('cp1251'))
def once(t,a,b):
 if t.count(a)!=1:raise RuntimeError((a,t.count(a)))
 return t.replace(a,b)
road=r.parent/'investigations/project-roadmap.md'
with road.open('a',encoding='utf-8') as f:f.write('\nGAME-EXE-EIREBUILD-ADDON-INTEGRATION-001 R27 ACTIVE: migrate reference miniaudio implementation with native class bindings, encoded RES capture and virtual sound ownership; final voice commands use actual lifetime tickets. Preserve full parent; no gameplay/audio/UI launch, install or publication. Expected one changed-code build and headless lifetime/no-device checks. Source snapshot Git N/A; unchanged reference57b8e0c provenance reused. Broader transfer remains open.\n')
for name in ['MiniaudioOneShotSfx.h','VoiceWaitState.h','ScenarioSpeechDucking.h']:
 shutil.copyfile(s/'src/addon/Patches'/name,r/'Game'/name)
(r/'third_party/miniaudio').mkdir(parents=True,exist_ok=True)
shutil.copyfile(s/'third_party/miniaudio/miniaudio.h',r/'third_party/miniaudio/miniaudio.h')
for p in (s/'third_party/miniaudio').glob('*LICENSE*'):shutil.copyfile(p,r/'third_party/miniaudio'/p.name)
fields={'LISTENER_X':'offsetof(CSoundManager, listenerCP)+offsetof(CPlot3D,x)','LISTENER_Y':'offsetof(CSoundManager, listenerCP)+offsetof(CPlot3D,y)','LISTENER_Z':'offsetof(CSoundManager, listenerCP)+offsetof(CPlot3D,z)','ORIENTATION_X':'offsetof(CSoundManager, listenerOrientation)+offsetof(CPlot3D,x)','ORIENTATION_Y':'offsetof(CSoundManager, listenerOrientation)+offsetof(CPlot3D,y)','ORIENTATION_Z':'offsetof(CSoundManager, listenerOrientation)+offsetof(CPlot3D,z)','UNIQUE_COUNTER':'offsetof(CSoundManager,dwUniqueCnt)','STREAM_SAMPLE':'offsetof(CSoundManager,streamMain)+offsetof(SStreamControl,ailHSample)','STREAM_SAMPLES_PER_BUFFER':'offsetof(CSoundManager,streamMain)+offsetof(SStreamControl,nSamplesPerBuffer)','READY':'offsetof(CSoundManager,bReady)','REVERSE_STEREO':'offsetof(CSoundManager,bReverseStereo)','FX_MASTER_VOLUME':'offsetof(CSoundManager,nFXMasterVolume)','SPEECH_MASTER_VOLUME':'offsetof(CSoundManager,nSpeechMasterVolume)'}
adapter='// Offsets are computed from this build, never retail EXE constants.\nstruct ModAudioLayout {\n'+''.join(' static constexpr size_t '+k+' = '+v+';\n' for k,v in fields.items())+'};\n'
src=(s/'src/addon/Patches/MiniaudioOneShotSfx.cpp').read_text(encoding='utf-8-sig')
src=src.replace('#include "main.h"\n#include "EIClasses/EIClasses.h"\n#include "DeTest/DeTest.h"','#include "ModAudioConfig.h"\n#include "SoundManager.h"\n#include "WorldClient.h"\n#include "WorldServer.h"\n#include "Player.h"\n#include "Unit.h"\n#include <cstdarg>\n#include <cstdio>\nstatic void NativeAudioLog(const char* fmt,...) { char buffer[2048]; va_list args;va_start(args,fmt);vsnprintf(buffer,sizeof(buffer),fmt,args);va_end(args);OutputDebugStringA(buffer);OutputDebugStringA("\\n"); }\n#define LOGINFO NativeAudioLog\n#define LOGWARN NativeAudioLog\n#define LOGERR NativeAudioLog\n'+adapter)
src='#include "StdAfx.h"\n'+src
src=src.replace('../../../third_party/miniaudio/miniaudio.h','../third_party/miniaudio/miniaudio.h')
for k in fields:src=re.sub(r'(const size_t SOUND_MANAGER_'+k+r'_OFFSET = )0x[0-9A-F]+;',r'\g<1>ModAudioLayout::'+k+';',src)
fx={'VOLUME':'nPercentVolume','PAN':'nPercentPan','IS_USED':'bIsUsed','IS_LOOPED':'bIsLooped','REQ_CLEANUP':'bReqCleanup','UNIQUE_ID':'dwUniqueID'}
for k,v in fx.items():src=re.sub(r'(const size_t FX_'+k+r'_OFFSET = )0x[0-9A-F]+;',r'\g<1>offsetof(SFxControl,'+v+');',src)
src=src.replace('EIClasses::WorldServer','(&theWorldServer)').replace('EIClasses::WorldClient','(&theWorld)').replace('EIClasses::','')
src=src.replace('worldServer->GetAIMap()','&worldServer->aiMap')
src=src.replace('world->GetMprFileName()->GetString()','(LPCSTR)world->szZoneFileName').replace('(&theWorldServer)->GetMprFileName()->GetString()','(LPCSTR)theWorldServer.szZoneFileName')
src=src.replace('g_addonCfg','ModAudioSettings()').replace('DeTest::Enabled()','false').replace('g_Log.Flush();','/* OutputDebugString is unbuffered. */')
(r/'Game/MiniaudioOneShotSfx.cpp').write_text(src,encoding='utf-8',newline='\r\n')
keys=sorted(set(re.findall(r'ModAudioSettings\(\)\.([A-Za-z0-9_]+)',src)))
defaults={'JMVSfxReverbOutdoorLevel':'.06f','JMVSfxReverbOutdoorReflection':'.08f','JMVSfxReverbOutdoorDecay':'.55f','JMVSfxReverbCaveLevel':'.22f','JMVSfxReverbCaveReflection':'.12f','JMVSfxReverbCaveDecay':'1.8f'}
fixed={'JMVMiniaudioOneShotStrictTakeover':False}
config='#pragma once\n#include <string>\nclass CFileRegistry;\nstruct ModAudioConfig {\n'
for k in keys:
 if k=='addonPath':config+=' std::string addonPath;\n'
 elif k in defaults:config+=' float '+k+'='+defaults[k]+';\n'
 else:config+=' bool '+k+'='+('false' if k in fixed or 'Bink' in k or 'Diagnostic' in k else 'true')+';\n'
config+='};\nModAudioConfig& ModAudioSettings();\nvoid ConfigureModAudio(CFileRegistry&,const std::string&);\n'
(r/'Game/ModAudioConfig.h').write_text(config,encoding='utf-8',newline='\r\n')
configcpp='#include "StdAfx.h"\n#include "ModAudioConfig.h"\n#include "FileRegistry.h"\nModAudioConfig& ModAudioSettings(){static ModAudioConfig cfg;return cfg;}\nvoid ConfigureModAudio(CFileRegistry& reg,const std::string& root){auto& c=ModAudioSettings();c=ModAudioConfig{};c.addonPath=root+"\\\\";\n'
for k,v in defaults.items():configcpp+='c.'+k+'=reg.GetFloat("Addon","'+k+'",'+v+');\n'
for k in keys:
 if 'Bink' in k or 'Diagnostic' in k:configcpp+='c.'+k+'=reg.GetInt("Addon","'+k+'",0)!=0;\n'
configcpp+='}\n'
(r/'Game/ModAudioConfig.cpp').write_text(configcpp,encoding='utf-8',newline='\r\n')
edit('Game/SoundManager.h',lambda t:once(once(t,'class CSoundManager;','class CSoundManager;\r\n#include "MiniaudioOneShotSfx.h"'), '    HDIGDRIVER ailDigDriver;', '    friend struct ModAudioLayout;\r\n    HDIGDRIVER ailDigDriver;'))
edit('EI_Rebuild.vcxproj',lambda t:once(t,'    <ClCompile Include="Game\\SoundManager.cpp" />','    <ClCompile Include="Game\\MiniaudioOneShotSfx.cpp" />\r\n    <ClCompile Include="Game\\ModAudioConfig.cpp" />\r\n    <ClCompile Include="Game\\SoundManager.cpp" />'))
edit('Game/ModPackage.cpp',lambda t:once(once(t,'#include "ModPackage.h"','#include "ModPackage.h"\r\n#include "ModAudioConfig.h"'),'registeredLoot=settings.GetInt','ConfigureModAudio(settings,root);registeredLoot=settings.GetInt'))
print('Imported backend and native configuration/layout bindings')
