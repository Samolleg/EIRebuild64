#pragma once
#include <cmath>
#include <stdexcept>
#include <functional>
// Installed source 2b4afbf: LS_ShieldsAndWeaponGroups incoming attack chain.
class CUnit;
namespace ModCombat {
struct Feedback {virtual ~Feedback(){};virtual int Random(int maximum)=0;virtual void Sound(CUnit*,const char*)=0;};
struct Defense { float block=0,parryChance=0,damageMultiplier=1; bool parry=false,critical=false;
 std::function<float(float)> scaleMagic,scaleSingle;std::function<void(float,float)> rekeyCritical;std::function<bool(float,float)> resolveCritical;bool custom0=false; };
inline float ParryUnion(float right,float left) {
 right*=.01f;left*=.01f;
 if(right<.0001f){if(left<.0001f)left=right*100;right=left*100;}
 else {if(left<.0001f)left=right*100;else right=(right+left-2*(right*left))*100;}
 return right;
}
inline float Passed(float damage,int type,float block,bool parry,bool attacker) {
 if(type>2)return attacker&&block>.01f&&damage>0?damage-block*.5f:damage;
 return parry?0:damage-(block>.01f?block:0);
}
inline bool CriticalFeedback(bool critical,bool damaged,float applied) {return critical&&damaged&&applied>=1;}
}
