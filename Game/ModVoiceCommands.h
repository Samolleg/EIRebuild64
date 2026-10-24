#pragma once
#include "VoiceWaitState.h"
#include "SoundManager.h"
#include "EngineDiagnostics.h"
namespace ModMob {
inline thread_local unsigned synchronousDepth=0;
struct SynchronousScope {SynchronousScope(){++synchronousDepth;}~SynchronousScope(){--synchronousDepth;}};
inline bool Voice(SSCCmd& cmd,int* state,BOOL* next) {
 if(cmd.command!=387 && cmd.command!=390)return false;
 const bool wait=cmd.command==390;
 auto error=[](const char* text){EngineDiagnostics::Event("CreateVoiceFX rejected: %s",text);NConsole::game << "CreateVoiceFX: " << text << std::endl;};
 if(wait && (!state || !next || synchronousDepth)){error("Use outside synchronous loops");return true;}
 auto start=[&]()->uint32_t {
  EngineDiagnostics::Event("CreateVoiceFX request command=%d",cmd.command);
  auto* unit=UnitArg(cmd);auto* server=ServerArg(cmd);
  if(!server || !unit || unit->IsDead()){error("Speaker is absent or dead");return 0;}
  const char* name=cmd.szParams[0];float volume=cmd.params[0];int count=cmd.params.GetSize();
  if(!name || !*name || !std::isfinite(volume) || volume<0 || volume>100 || (count!=1 && count!=3)){error("Invalid name, volume or paired radii");return 0;}
  MiniaudioOneShotSfx::PartyVoiceBinding party={uint32_t(unit->GetID()),0,0};
  if(count==3){party.nearDistance=cmd.params[1];party.farDistance=cmd.params[2];if(!std::isfinite(party.nearDistance)||!std::isfinite(party.farDistance)||party.nearDistance<0||party.farDistance<=party.nearDistance){error("Expected finite 0 <= near < far");return 0;}}
  auto* manager=theWorld.GetSoundManager();
  if(!manager || !manager->IsActive()){error("Ready single-player sound manager required");return 0;}
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
