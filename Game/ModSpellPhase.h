#pragma once
#include "TypeIDs.h"
#include "ModSpellIdentity.h"
namespace ModSpellPhase {
inline bool Enabled(bool current,bool particles,int raw){return current&&particles&&ModSpellIdentity::ExtendedLifecycle(raw);}
// Bound native Start compares Fireball against raw61, Column/Fog against raw65.
// Neither comparison is true for its registered family. Preserve that selection.
inline ETypeIDs StartParticle(int raw){
 if(raw==57||raw==58)return PTID_FIREBALLLIGHT;
 if(raw==71||raw==72)return PTID_NUKELIGHT;
 if(raw==73||raw==74)return PTID_POISONFOGLIGHT;
 return TID_EMPTY;
}
enum class Event {WallDamage,RadiusDamage,HideCaster,CaptureFirewallSoundCP,DangerRemove,SoundRemove,MainRemove,FirewallEndSound,PoisonEndSound,LightFade,InvokeStrike,Finish};
// Emit native callback order; mutate the caller's timer at the same transition.
// Only valid-domain scalar control is shared, never world/effect ownership.
template<class Emit> void Action(int raw,int& time,bool mainEffect,Emit emit){
 if(raw==55||raw==57||raw==58||raw==59||raw==60||raw==69||raw==70){
  if(time--<0)emit(Event::Finish);
 }else if(raw==56||raw==61||raw==62||raw==67||raw==68||raw==73||raw==74){
  bool fire=raw==56||raw==61||raw==62,fog=raw==73||raw==74;
  if(time>=0){
   if((time&(fire?7:3))==0&&(fog||mainEffect)){emit(fog?Event::RadiusDamage:Event::WallDamage);emit(Event::HideCaster);}
   --time;
  }else{
   if(time==-1){
    if(fire){emit(Event::CaptureFirewallSoundCP);emit(Event::DangerRemove);emit(Event::SoundRemove);emit(Event::MainRemove);emit(Event::FirewallEndSound);emit(Event::LightFade);}
    else{if(fog)emit(Event::PoisonEndSound);emit(Event::LightFade);emit(Event::DangerRemove);emit(Event::SoundRemove);emit(Event::MainRemove);}
   }
   if(time<-80)emit(Event::Finish);
   --time;
  }
 }else if(raw==63||raw==64){
  if(time==0){emit(Event::MainRemove);emit(Event::SoundRemove);emit(Event::LightFade);}
  if(time<-80)emit(Event::Finish);
  --time;
 }else if(raw==65||raw==66){
  emit(Event::MainRemove);if(time==5)emit(Event::InvokeStrike);
  if(time--<0)emit(Event::Finish);
 }else if(raw==71||raw==72){
  if(time--<0)emit(Event::Finish);
  if(time==15)emit(Event::RadiusDamage);
 }
}
}
