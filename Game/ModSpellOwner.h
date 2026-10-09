#pragma once
#include "RPGSystem.h"
#include "ModSpellIdentity.h"
namespace ModSpellOwner {
// Native54 is a separate Start AP, not the generic magic-subtype/area AP.
template<class Heal,class Damage,class Particle,class NeedsFeedback,class Feedback,class Phase>
void Native54Start(bool valid,float power,bool quiet,Heal heal,Damage damage,Particle particle,
                   NeedsFeedback needsFeedback,Feedback feedback,Phase phase){
 if(!valid){phase(false,0);return;}
 SRPGAttackPortion ap;ap.Clear();ap.hl=RPGHL_ALL;ap.fpToHit=FLT_MAX;
 ap.fBackStabMultiplier=1;ap.bCalledShot=false;ap.fpAttacks[RPGAT_TERMICAL]=1;ap.fpDamage=power;
 heal(power*.5f);damage(ap);particle();
 // Evaluate target HP after both caster operations, including caster==target.
 if(!quiet||needsFeedback())feedback("magic\\Healing\\start.wav",-1.0f);
 phase(true,30);
}
inline bool ActionExpired(int&timeLeft){return timeLeft--<0;}
inline bool AdmitExtendedDangerAfterStart(int kind,bool action){
 return action&&!ModSpellIdentity::ExtendedLifecycle(kind);
}
struct SoundRequest {const char*resource;int priority;float nearDistance,farDistance;bool loop;};
template<class Manager,class Send> void ExplosiveSound(bool currentWorld,Manager*manager,Send send){
 if(currentWorld&&manager)send(manager,SoundRequest{"skills\\explosiveshot\\1.wav",8000,5,25,false});
}
}
