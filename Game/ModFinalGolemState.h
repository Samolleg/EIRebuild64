#pragma once
#include "ModEncounter.h"
#include "UnitServer.h"
#include "MapObjectServer.h"
#include <cmath>

namespace ModFinalGolem {
constexpr int Fields=256,Shots=8,ShardLifetime=75*18;
inline float Read(const char* key) {return ModEncounter::Read(theWorldServer,key);}
inline void Set(const char* key,float value) {ModEncounter::Set(theWorldServer,key,value);}
inline int Now() {return theWorldServer.GetSegmentTimeCounter();}
inline bool Active() {return ModEncounter::Arena(theWorldServer)&&Read("FGL.Active")==1;}
inline bool Valid(CUnit* unit,bool alive=true) {return ModInvincible::UnitValid(unit,alive)&&ModInvincible::World(unit)==&theWorldServer;}
inline bool Hero(CUnit* unit) {return Valid(unit,false)&&ModEncounter::Hero(theWorldServer,unit);}
inline CUnitServer* Server(CUnit* unit) {return Valid(unit,false)?dynamic_cast<CUnitServer*>(unit->GetUnitSpecific()):nullptr;}
inline float HP(CUnit* unit) {auto* server=Server(unit);return server?server->GetMapObjectServer()->GetCurrentHP():0;}
inline float Body(CUnit* unit) {float r=unit->GetCollisionDistance();return std::isfinite(r)&&r>0&&r<8?r:.45f;}
inline float Slot(const char* kind,int index,const char* field) {char k[80];std::snprintf(k,sizeof(k),"FGL.%s%d.%s",kind,index,field);return Read(k);}
inline void Slot(const char* kind,int index,const char* field,float value) {char k[80];std::snprintf(k,sizeof(k),"FGL.%s%d.%s",kind,index,field);Set(k,value);}
inline float HeroVar(CUnit* unit,const char* field) {char k[80];std::snprintf(k,sizeof(k),"FGL.H%u.%s",unsigned(unit->GetID()),field);return Read(k);}
inline void HeroVar(CUnit* unit,const char* field,float value) {char k[80];std::snprintf(k,sizeof(k),"FGL.H%u.%s",unsigned(unit->GetID()),field);Set(k,value);}
inline CUnit* Actor(unsigned id) {return theWorldServer.GetUnit(int(id));}
inline CUnit* Party(int n) {char k[48];std::snprintf(k,sizeof(k),"FGL.Party%d.Lo",n);unsigned id=unsigned(Read(k));std::snprintf(k,sizeof(k),"FGL.Party%d.Hi",n);return Actor(id|(unsigned(Read(k))<<16));}
inline float Random() {unsigned s=unsigned(Read("FGL.Seed"));s=(1664525u*s+1013904223u)&0xffffffu;Set("FGL.Seed",float(s));return float(s)/16777216.f;}
inline float Clamp(float v,float lo,float hi) {return (std::max)(lo,(std::min)(hi,v));}
inline float Percent(float maximum,float rate) {return std::isfinite(maximum)&&maximum>0?maximum*rate:0;}
inline void DirectDamage(CUnit* unit,float power) {if(auto* server=Server(unit))server->ApplyDirectDamage(power);}
inline void Field(float x,float y,float z,float radius,int lifetime,int style) {
    int best=0;float earliest=1.e30f;const int now=Now();
    for(int i=0;i<Fields;++i){float end=Slot("Field",i,"End");if(end<=now){best=i;break;}if(end<earliest){earliest=end;best=i;}}
    Slot("Field",best,"X",x);Slot("Field",best,"Y",y);Slot("Field",best,"Z",z);Slot("Field",best,"R",radius);
    Slot("Field",best,"Born",float(now));Slot("Field",best,"End",float(now+lifetime));Slot("Field",best,"Style",float(style));
    Set("FGL.FieldUsed",(std::max)(Read("FGL.FieldUsed"),float(best+1)));Set("FGL.FieldCount",Read("FGL.FieldCount")+1);
}
inline bool InField(float x,float y,int now) {
    const int used=(std::min)(Fields,int(Read("FGL.FieldUsed")));
    for(int i=0;i<used;++i)if(Slot("Field",i,"End")>now&&std::hypot(x-Slot("Field",i,"X"),y-Slot("Field",i,"Y"))<=Slot("Field",i,"R"))return true;
    return false;
}
inline float Distance2(float x,float y,float ax,float ay,float bx,float by) {
    const float dx=bx-ax,dy=by-ay,d=dx*dx+dy*dy;
    const float t=Clamp(d>.000001f?((x-ax)*dx+(y-ay)*dy)/d:0,0,1);
    return std::hypot(x-ax-dx*t,y-ay-dy*t);
}
}
