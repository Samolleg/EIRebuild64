#pragma once
#include <map>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cfloat>
#include "RPGSystemConsts.h"
#include "ModMeleeCooldownCodec.h"
#include <vector>
namespace ModSkills {
enum ImpactKind { ImpactNone=0, ImpactCripple=1, ImpactPowerHit=2,
                  ImpactPreciseHit=3 };
struct Config {
 float requiredMelee=0,requiredArchery=-1,staminaMultiplier=1,staminaCost=-1,cooldownSeconds=0,damageMultiplier=1;
 uint32_t weaponMask=0;
 bool scaleDamageWithMelee=false,grantPrototype=false,allowDualWield=true,allowDeconstruction=false,allowItemInsertion=false,allowSale=true;
 int projectileCount=1;float accuracyBonus=0,areaRadius=0;bool allowNpc=true;
 float Cost(float prototypeCost)const {float c=staminaCost>=0?staminaCost:prototypeCost*staminaMultiplier;return c>0?c:0;}
 float Requirement(float complexity)const{return requiredArchery>=0?requiredArchery:requiredMelee>=0?requiredMelee:complexity;}
 int Duration()const{return static_cast<int>(MeleeCooldown::DurationTicks(cooldownSeconds));}
 bool Weapon(int type)const {
  if(type<0||type>=32)return false;
  if(requiredArchery>=0){if(type!=5&&type!=6)return false;}
  else switch(type){case 0:case 1:case 2:case 3:case 4:case 8:case 10:case 12:case 13:case 14:break;default:return false;}
  return (weaponMask&(1u<<type))!=0;
 }
};
constexpr uint32_t CommandTag=0x4D530000u;
inline int Slot(uint32_t param){return (param&0xFFFF0000u)==CommandTag&&(param&0xFFFFu)<16?int(param&0xFFFFu):-1;}
class Cooldowns {
 struct Entry { MeleeCooldown::State state; bool persistent; };
 std::map<uint64_t,Entry> entries; const void*world=nullptr; int last=-1;
 void Sync(const void*w,int tick){if(!w||w!=world||(last>=0&&tick<last))entries.clear();world=w;last=tick;}
 static uint64_t Key(int unit,const std::string&code){return (uint64_t(uint32_t(unit))<<32)|Hash(code);}
public:
 static uint32_t Hash(const std::string&s){uint32_t h=2166136261u;for(unsigned char c:s){if(c>='A'&&c<='Z')c+=32;h=(h^c)*16777619u;}return h;}
 void Clear(){entries.clear();world=nullptr;last=-1;}
 int Remaining(const void*w,int tick,int unit,const std::string&code){Sync(w,tick);auto i=entries.find(Key(unit,code));if(i==entries.end())return 0;auto n=MeleeCooldown::Remaining(i->second.state,uint32_t(tick));if(!n)entries.erase(i);return int(n);}
 void Start(const void*w,int tick,int unit,const std::string&code,int duration,bool persistent=true){Sync(w,tick);if(w&&duration>0)entries[Key(unit,code)]={{uint32_t(tick),uint32_t(duration),uint32_t(duration)},persistent};}
 template<class Vars> void Save(const void*w,int tick,const std::string&zone,Vars&vars){
  Sync(w,tick);std::vector<std::string> obsolete;
  for(const auto&v:vars)if(v.first.compare(0,sizeof(MeleeCooldown::MarkerPrefix)-1,MeleeCooldown::MarkerPrefix)==0)obsolete.push_back(v.first);
  for(const auto&name:obsolete)vars.DelVar(name.c_str());
  for(const auto&e:entries){auto remaining=MeleeCooldown::Remaining(e.second.state,uint32_t(tick));if(e.second.persistent&&remaining)vars.SetVar(MeleeCooldown::Marker(Hash(zone),e.first,e.second.state.totalTicks,remaining).c_str(),1.f);}
 }
 template<class Vars> void Load(const void*w,int tick,const std::string&zone,const Vars&vars){
  Clear();Sync(w,tick);if(!w)return;
  for(const auto&v:vars){uint64_t key=0;uint32_t total=0,remaining=0;if(v.second==1.f&&MeleeCooldown::ReadMarker(v.first.c_str(),Hash(zone),key,total,remaining))entries.emplace(key,Entry{{uint32_t(tick),total,remaining},true});}
 }
};
}

namespace ModSkills {
// Reference LS_CEffectArrowBaseCalcSpeed: XY speed, relief bisection, 150 ticks.
template<class Point,class Trace> bool ExplosiveStep(const Point& current,const Point& target,int age,Point& next,Trace trace){
 if(age<0||age>=150){next=current;return true;}
 Point delta=target-current;float distance=sqrtf(delta.x*delta.x+delta.y*delta.y);const float speed=(10.f/15.f)*2;
 next=distance<=speed?target:current+delta*(speed/distance);
 if(trace(current,next)<.1f){Point clear=current,blocked=next;for(int i=0;i<8;++i){Point middle=(clear+blocked)*.5f;if(trace(current,middle)<.1f)blocked=middle;else clear=middle;}next=clear;return true;}
 return distance<=speed;
}
template<class Attack> void ExplosiveDamage(Attack& ap){
 for(auto& share:ap.fpAttacks)share=0;
 ap.fpAttacks[RPGAT_TERMICAL]=1;ap.fpToHit=FLT_MAX;ap.hl=RPGHL_ALL;ap.bAreaEffect=TRUE;ap.fBackStabMultiplier=1;ap.bCalledShot=false;
}
}
