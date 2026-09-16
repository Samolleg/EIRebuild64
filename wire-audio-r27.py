from pathlib import Path
r=Path(__file__).resolve().parent;pre=r/'evidence/addon-integration/pre-r27'
def edit(name,fn):
 p=r/name;b=p.read_bytes();q=pre/name;q.parent.mkdir(parents=True,exist_ok=True)
 if not q.exists():q.write_bytes(b)
 p.write_bytes(fn(b.decode('latin1')).encode('latin1'))
def one(t,a,b):
 assert t.count(a)==1,(a,t.count(a))
 return t.replace(a,b)
def cr(s):return s.replace('\n','\r\n')
def header(t):
 t=one(t,'    bool IsFXValid(HSOUND hSnd)','    HSOUND BeginScenarioVoice(const char*,int,const MiniaudioOneShotSfx::PartyVoiceBinding* = nullptr);\r\n    bool CaptureModSound(const char*);\r\n    bool IsFXValid(HSOUND hSnd)')
 return one(t,'((GetFxControl(hSnd) != NULL) || (GetDisplaced(hSnd) != NULL))','(MiniaudioOneShotSfx::IsVirtualSound(this,hSnd) || (GetFxControl(hSnd) != NULL) || (GetDisplaced(hSnd) != NULL))')
# Header was applied before the first checked source anchor failed.
def sound(t):
 t=one(t,'#include "SoundManager.h"','#include "SoundManager.h"\r\n#include "ModRuntime.h"')
 # Own the data only for CaptureEncoded; release the mapped resource on every path.
 pos=t.index('HSOUND CSoundManager::BeginFXInt(')
 block='''bool CSoundManager::CaptureModSound(const char* name)
{
    if(!bReady || !name || !*name)return false;
    if(MiniaudioOneShotSfx::IsCaptured(name))return true;
    DWORD size=0;CMappedResource* resource=nullptr;
    void* bytes=GetFXFileFromResources(name,&size,&resource);
    if(!bytes)return false;
    struct Release {CMappedResource* r;void* b;~Release(){r->CloseFile((BYTE*)b);}} release{resource,bytes};
    return MiniaudioOneShotSfx::CaptureEncoded(name,bytes,size,"native_res");
}
HSOUND CSoundManager::BeginScenarioVoice(const char* name,int volume,const MiniaudioOneShotSfx::PartyVoiceBinding* party)
{
    if(!ModRuntime::Current() || !CaptureModSound(name))return SND_EMPTY;
    const DWORD id=dwUniqueCnt++;if(id==DWORD(SND_EMPTY))return SND_EMPTY;
    return MiniaudioOneShotSfx::TryStartVirtualFlat(this,id,name,1000,volume,0,false,false,true,false,1.f,1.f,party,true)==MiniaudioOneShotSfx::STARTED ? id : SND_EMPTY;
}

'''
 t=t[:pos]+cr(block)+t[pos:]
 flat='''    if(ModRuntime::Current()) {
        if(!CaptureModSound(pszSoundName))return SND_EMPTY;
        const DWORD id=dwUniqueCnt++;if(id==DWORD(SND_EMPTY))return SND_EMPTY;
        return MiniaudioOneShotSfx::TryStartVirtualFlat(this,id,pszSoundName,nPriority,nPercentVolume,nPercentPan,bLooping!=0,bAffectedWithHeight!=0,bIsSpeech!=0,bIsDialogPlayMode!=0,1.f,1.f)==MiniaudioOneShotSfx::STARTED ? id : SND_EMPTY;
    }
'''
 t=t.replace('    // Get free FX slot\r\n    int nFxIndex',cr(flat)+'    // Get free FX slot\r\n    int nFxIndex',1)
 start=t.index('HSOUND CSoundManager::BeginFXInt(LPCSTR pszSoundName, int nPriority, const CPlot3D')
 p=t.index('    if (!bReady)',start)
 spatial='''    if(ModRuntime::Current()) {
        if(!CaptureModSound(pszSoundName))return SND_EMPTY;
        const DWORD id=dwUniqueCnt++;if(id==DWORD(SND_EMPTY))return SND_EMPTY;
        return MiniaudioOneShotSfx::TryStartVirtual3D(this,id,pszSoundName,nPriority,&sourceCP,fpMinDistance,fpMaxDistance,bLooping!=0,bIsSpeech!=0,1.f,1.f)==MiniaudioOneShotSfx::STARTED ? id : SND_EMPTY;
    }
'''
 t=t[:p]+cr(spatial)+t[p:]
 methods={
 'void CSoundManager::Destroy()':'MiniaudioOneShotSfx::Shutdown("native_destroy");',
 'void CSoundManager::StopAllSounds(bool bStopStreaming)':'MiniaudioOneShotSfx::StopAll("native_stop_all");',
 'void CSoundManager::PauseAllSounds()':'MiniaudioOneShotSfx::Pause("native_pause");',
 'void CSoundManager::UnpauseAllSounds()':'MiniaudioOneShotSfx::Resume("native_resume");',
 'void CSoundManager::SegmentAmbient()':'if(ModRuntime::Current())MiniaudioOneShotSfx::Tick(this);',
 'BOOL CSoundManager::UpdateFX(HSOUND hSnd, int nPercentVolume, int nPercentPan)':'if(MiniaudioOneShotSfx::IsVirtualSound(this,hSnd))return MiniaudioOneShotSfx::UpdateVirtualFlat(this,hSnd,nPercentVolume,nPercentPan);',
 'BOOL CSoundManager::UpdateFX(HSOUND hSnd, const CPlot3D &sourceCP, float fpMinDistance,\r\n                             float fpMaxDistance)':'if(MiniaudioOneShotSfx::IsVirtualSound(this,hSnd))return MiniaudioOneShotSfx::UpdateVirtual3D(this,hSnd,&sourceCP,fpMinDistance,fpMaxDistance);',
 'HSOUND CSoundManager::CancelFX(HSOUND hSnd)':'if(MiniaudioOneShotSfx::CancelVirtual(this,hSnd))return SND_EMPTY;',
 'float CSoundManager::GetFXPlaybackTime(HSOUND hSnd)':'if(MiniaudioOneShotSfx::IsVirtualSound(this,hSnd))return MiniaudioOneShotSfx::GetVirtualPlaybackTime(this,hSnd);'}
 for sig,code in methods.items():t=one(t,sig+'\r\n{',sig+'\r\n{\r\n    '+code)
 return t
edit('Game/SoundManager.cpp',sound)
# Native loop recursion has no existing counter. A scoped per-thread depth guards
# synchronous loop bodies without changing serialized thread layout.
voice='''#pragma once
#include "VoiceWaitState.h"
#include "SoundManager.h"
namespace ModMob {
inline thread_local unsigned synchronousDepth=0;
struct SynchronousScope {SynchronousScope(){++synchronousDepth;}~SynchronousScope(){--synchronousDepth;}};
inline bool Voice(SSCCmd& cmd,int* state,BOOL* next) {
 if(cmd.command!=387 && cmd.command!=390)return false;
 const bool wait=cmd.command==390;
 auto error=[](const char* text){NConsole::game << "CreateVoiceFX: " << text << std::endl;};
 if(wait && (!state || !next || synchronousDepth)){error("Use outside synchronous loops");return true;}
 auto start=[&]()->uint32_t {
  auto* unit=UnitArg(cmd);auto* server=Server(unit);
  if(!server || !unit || unit->IsDead()){error("Speaker is absent or dead");return 0;}
  const char* name=cmd.szParams[0];float volume=cmd.params[0];int count=cmd.params.GetSize();
  if(!name || !*name || !std::isfinite(volume) || volume<0 || volume>100 || (count!=1 && count!=3)){error("Invalid name, volume or paired radii");return 0;}
  MiniaudioOneShotSfx::PartyVoiceBinding party={uint32_t(unit->GetID()),0,0};
  if(count==3){party.nearDistance=cmd.params[1];party.farDistance=cmd.params[2];if(!std::isfinite(party.nearDistance)||!std::isfinite(party.farDistance)||party.nearDistance<0||party.farDistance<=party.nearDistance){error("Expected finite 0 <= near < far");return 0;}}
  auto* manager=theWorld.GetSoundManager();
  if(IWorldConnector.IsLMP() || !manager || !manager->IsActive()){error("Ready single-player sound manager required");return 0;}
  auto id=manager->BeginScenarioVoice(name,int(volume+.5f),count==3?&party:nullptr);
  if(id==SND_EMPTY){error("Unable to start voice resource");return 0;}
  return MiniaudioOneShotSfx::CaptureVoiceWait(manager,id);
 };
 if(!wait){start();return true;}
 auto result=VoiceWait::Step(*state,start,[](uint32_t ticket){return MiniaudioOneShotSfx::IsVoiceWaitActive(ticket);});
 *next=(result==VoiceWait::StepResult::Started || result==VoiceWait::StepResult::Waiting)?FALSE:TRUE;
 if(result==VoiceWait::StepResult::Failed)error("No live audio ticket; continuing script");
 return true;
}
}
'''
# Include outside ModMob after helper definitions, then reopen namespace.
edit('Game/ModMobCommands.h',lambda t:one(one(one(t,',"CreateVoiceFX","osf?ff",false}',',"CreateVoiceFX","osf?ff",true}'),',"CreateVoiceFXWait","osf?ff",false}',',"CreateVoiceFXWait","osf?ff",true}'),'inline bool Void(SSCCmd&cmd,int*index=nullptr,int count=0){','}\r\n#include "ModVoiceCommands.h"\r\nnamespace ModMob {\r\ninline bool Void(SSCCmd&cmd,int*index=nullptr,int count=0,int*sleep=nullptr,BOOL*next=nullptr){').replace(' if(e->ready&&(cmd.command==391',' if(cmd.command==387||cmd.command==390){Validate(*e,cmd);return Voice(cmd,sleep,next);}\r\n if(e->ready&&(cmd.command==391'))
(r/'Game/ModVoiceCommands.h').write_text(voice,encoding='utf-8',newline='\r\n')
edit('Game/Script.cpp',lambda t:one(one(one(t,'ModMob::Void(*pCmd,&nCurCommand,pThread->commands.GetSize())','ModMob::Void(*pCmd,&nCurCommand,pThread->commands.GetSize(),&nSleepStart,&bNextCommand)'), '    int nSaveCurCommand = nCurCommand, nSaveSegment = nSegment, nSaveSleepStart = nSleepStart;','    ModMob::SynchronousScope loopScope;\r\n    int nSaveCurCommand = nCurCommand, nSaveSegment = nSegment, nSaveSleepStart = nSleepStart;'),'    file.Read(&nSleepStart, sizeof(nSleepStart));','    file.Read(&nSleepStart, sizeof(nSleepStart));\r\n    if(ModRuntime::Current())VoiceWait::AfterLoad(nSleepStart);'))
print('Native virtual sound and voice command routes connected')
