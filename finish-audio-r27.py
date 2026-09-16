from pathlib import Path
r=Path(__file__).resolve().parent;pre=r/'evidence/addon-integration/pre-r27'
def edit(name,fn):
 p=r/name;b=p.read_bytes();q=pre/name;q.parent.mkdir(parents=True,exist_ok=True)
 if not q.exists():q.write_bytes(b)
 p.write_bytes(fn(b.decode('latin1')).encode('latin1'))
def one(t,a,b):
 assert t.count(a)==1,(a,t.count(a));return t.replace(a,b)
edit('Game/MiniaudioOneShotSfx.h',lambda t:one(t,'void Tick(void *soundManager);','void PrepareNativeManager(void *soundManager);\r\nconst char* GetNativeStreamingName();\r\nvoid Tick(void *soundManager);'))
edit('Game/MiniaudioOneShotSfx.cpp',lambda t:one(t,'void Tick(void *soundManager)\r\n{','void PrepareNativeManager(void *soundManager)\r\n{\r\n    if(CurrentSoundManager!=soundManager)ResetForManager(soundManager,"native_manager_change");\r\n    SynchronizeUniqueCounter(soundManager);\r\n}\r\nconst char* GetNativeStreamingName(){return Streaming.playing ? Streaming.currentName : "";}\r\nvoid Tick(void *soundManager)\r\n{'))
edit('Game/ModAudioConfig.h',lambda t:one(t,'struct ModAudioConfig {','struct ModAudioConfig {\r\n bool JMVMiniaudioModalSpeechPausePrototype=false,JMVMiniaudioGamePauseSpeechPrototype=false;'))
edit('Game/ModAudioConfig.cpp',lambda t:one(t,'c.addonPath=root+"\\\\";','c.addonPath=root+"\\\\";\r\nc.JMVMiniaudioModalSpeechPausePrototype=reg.GetInt("Addon","JMVMiniaudioModalSpeechPausePrototype",0)!=0;\r\nc.JMVMiniaudioGamePauseSpeechPrototype=reg.GetInt("Addon","JMVMiniaudioGamePauseSpeechPrototype",0)!=0;'))
edit('Game/I3DMain.h',lambda t:one(t,'    CRenderer *GetRenderer() { return pRend; }','    size_t GetAudioScreenDepth() const { return pScreens.size(); }\r\n    CRenderer *GetRenderer() { return pRend; }'))
def sound(t):
 t=one(t,'#include "ModRuntime.h"','#include "ModRuntime.h"\r\n#include "ModAudioConfig.h"\r\n#include "I3DMain.h"\r\n#include "WorldClient.h"')
 t=one(t,'    if(MiniaudioOneShotSfx::IsCaptured(name))return true;','    MiniaudioOneShotSfx::PrepareNativeManager(this);\r\n    if(MiniaudioOneShotSfx::IsCaptured(name))return true;')
 # Match reference counter ownership: reserve, start, then increment on success only.
 t=t.replace('const DWORD id=dwUniqueCnt++;','const DWORD id=dwUniqueCnt;')
 import re
 t,n=re.subn(r'return (MiniaudioOneShotSfx::TryStartVirtual(?:Flat|3D)\([^\r\n]+)==MiniaudioOneShotSfx::STARTED \? id : SND_EMPTY;',r'if(\1==MiniaudioOneShotSfx::STARTED){dwUniqueCnt=id+1;return id;}return SND_EMPTY;',t)
 assert n==3,n
 t=one(t,'bIsSpeech!=0,bIsDialogPlayMode!=0,1.f,1.f','(bIsSpeech||bIsDialogPlayMode)!=0,bIsDialogPlayMode!=0,1.f,1.f')
 pause='''if(ModRuntime::Current()) {
        const auto& cfg=ModAudioSettings();const auto depth=theInterface.GetAudioScreenDepth();
        const bool modal=cfg.JMVMiniaudioModalSpeechPausePrototype && depth>1 && theWorld.IsCombatPause();
        const bool game=cfg.JMVMiniaudioGamePauseSpeechPrototype && depth<=1 && theWorld.IsCombatPause();
        MiniaudioOneShotSfx::SetModalSpeechPaused(modal||game,game,"native_pause_selector");
        MiniaudioOneShotSfx::Tick(this);
    }'''.replace('\n','\r\n')
 t=one(t,'if(ModRuntime::Current())MiniaudioOneShotSfx::Tick(this);',pause)
 methods={
 'BOOL CSoundManager::SelectStreamingResource(LPCSTR pszResourceName, LPCSTR pszMarkersRegFileName)':'if(ModRuntime::Current())return MiniaudioOneShotSfx::SetStreamingResource(this,pszResourceName,pszMarkersRegFileName);',
 'CString CSoundManager::GetStreamingName()':'if(ModRuntime::Current())return MiniaudioOneShotSfx::GetNativeStreamingName();',
 'void CSoundManager::PauseStreaming()':'if(ModRuntime::Current()){MiniaudioOneShotSfx::PauseStreaming("native_stream_pause");return;}',
 'void CSoundManager::UnpauseStreaming()':'if(ModRuntime::Current()){MiniaudioOneShotSfx::ResumeStreaming("native_stream_resume");return;}',
 'void CSoundManager::FadeOutStreaming()':'if(ModRuntime::Current()){MiniaudioOneShotSfx::FadeOutStreaming();return;}',
 'void CSoundManager::FadeInStreaming()':'if(ModRuntime::Current()){MiniaudioOneShotSfx::FadeInStreaming();return;}',
 'BOOL CSoundManager::BeginStreaming(LPCSTR pszSoundName, ESNDMARKERTYPE markerType)':'if(ModRuntime::Current())return MiniaudioOneShotSfx::BeginStreaming(this,pszSoundName,int(markerType));',
 'void CSoundManager::EndStreaming(ESNDMARKERTYPE markerType)':'if(ModRuntime::Current()){MiniaudioOneShotSfx::EndStreaming(int(markerType),"native_stream_end");return;}',
 'void CSoundManager::SetStreamingVolume(int nPercentVolume)':'if(ModRuntime::Current()){streamMain.vlmCtrl.SetVolume(nPercentVolume);MiniaudioOneShotSfx::SetStreamingVolume(nPercentVolume);return;}' }
 for sig,code in methods.items():t=one(t,sig+'\r\n{',sig+'\r\n{\r\n    '+code)
 return t
edit('Game/SoundManager.cpp',sound)
# The mapped branch returned bytes without assigning length. All consumers need
# the same stored directory size as the heap-read branch.
edit('Game/CMappedResource.cpp',lambda t:one(t,'    if (pBaseAddress)\r\n        return pBaseAddress + pHashTable[dwIndex].dwOffset;','    if (pBaseAddress)\r\n    {\r\n        if(pnDataSize)*pnDataSize=pHashTable[dwIndex].dwSize;\r\n        return pBaseAddress + pHashTable[dwIndex].dwOffset;\r\n    }'))
print('Streaming and source pause selectors connected; counter/cache ownership fixed')
