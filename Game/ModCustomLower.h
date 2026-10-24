#pragma once
#include "ModNativeCrtRandom.h"
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
// Native lower-call protocol. Ephemeral critical values; never saved origin data.
namespace ModCustomLower {
inline bool Custom0(bool attacks,bool explicitCustom){return !attacks&&explicitCustom;}
inline int CrtRoll(int maximum){return ModNativeCrtRandom::Random(maximum);}
template<class Draw,class Better> bool SelectLeft(bool current,bool left,Draw draw,Better better){
 if(!current||!left)return false;
 if(draw()!=0)return true;
 int second=draw();return better()&&second!=0;
}
template<class Chance,class Overload,class Roll>
float Parry(int current,int left,bool validCurrent,Chance chance,Overload overload,Roll roll,bool& parried){
 float right=0,leftValue=0;
 if(validCurrent){
  if(left<0)right=chance(current)*overload();
  else {
   right=float(chance(current)*overload()*.01);
   leftValue=float(chance(left)*overload()*.01);
   if(right<.0001f){if(leftValue<.0001f)leftValue=right*100;right=leftValue*100;}
   else {if(leftValue<.0001f)leftValue=right*100;else right=(right+leftValue-2*(right*leftValue))*100;}
  }
 }
 int sample=roll();parried=int(right)>sample;return right;
}
inline int DamageKey(float damage){
#if defined(_M_IX86)
 int result;
 __asm {
  fld dword ptr damage
  fistp result
 }
 return result;
#else
 return int(std::nearbyint(damage));
#endif
}
struct Critical {float damage;bool critical,staff;};
class CriticalCache {
 std::map<int,Critical> records;
 static bool Match(float a,float b){return std::fabs(a-b)<(std::numeric_limits<float>::epsilon)();}
public:
 const std::map<int,Critical>& SavedRecords()const{return records;}
 void Clear(){records.clear();}
 size_t Size()const{return records.size();}
 void Put(float damage,bool critical,bool staff){records[DamageKey(damage)]={damage,critical,staff};}
 bool Hit(float damage){
  auto i=records.find(DamageKey(damage));if(i==records.end())return false;
  Critical value=i->second;bool physical=Match(value.damage,damage)&&value.staff;
  if(physical||!value.critical)records.erase(i);return physical;
 }
 void Rekey(float original,float scaled){
  auto i=records.find(DamageKey(original));if(i==records.end()||!Match(i->second.damage,original))return;
  Critical value=i->second;value.damage=scaled;records.erase(i);records[DamageKey(scaled)]=value;
 }
 bool Resolve(float damage,float absorbedTotal){
  auto i=records.find(DamageKey(damage));
  if(i==records.end()||!i->second.critical||absorbedTotal<1||!Match(i->second.damage,damage))return false;
  records.erase(i);return true;
 }
};
}
