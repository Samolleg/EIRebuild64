#pragma once
#include "ModEncounter.h"
#include "UnitServer.h"
#include "MapObjectServer.h"
#include <cmath>

// Source policy: Dejavu 421604cc78b4. Native GS stores all battle state;
// actor pointers are resolved afresh for each call, including after load.
namespace ModCurseEncounter {
struct Point {float x,y;};
constexpr int Hz=18,Pool=12,PartyLimit=16,Waves=5;
constexpr int MarkFollow=2*Hz,MarkImpact=4*Hz,LanceWarn=4*Hz,LanceFlight=2*Hz,ConeWarn=3*Hz,Rest=4*Hz;
inline bool Inside(Point p,float margin=0) {return p.x>=54+margin&&p.x<=106-margin&&p.y>=46+margin&&p.y<=102-margin;}
inline float Distance(Point a,Point b) {return std::hypot(a.x-b.x,a.y-b.y);}
inline int Phase(float hp,float maximum) {if(!std::isfinite(hp)||!std::isfinite(maximum)||maximum<=0)return 0;return hp>maximum*.7f?1:hp>maximum*.35f?2:3;}
inline int WaveSize(int wave) {return wave==0?1:wave==1?2:wave>=2&&wave<Waves?3:0;}
inline int WaveOffset(int wave) {return wave==0?0:wave==1?1:wave>=2&&wave<Waves?3+(wave-2)*3:Pool;}
inline float SweptContact(Point from,Point to,Point center,float radius) {
    const float dx=to.x-from.x,dy=to.y-from.y,x=from.x-center.x,y=from.y-center.y;
    const float a=dx*dx+dy*dy,c=x*x+y*y-radius*radius;if(c<=0)return 0;
    if(a<1e-10f)return 2;const float b=x*dx+y*dy,d=b*b-a*c;if(d<0)return 2;
    const float t=(-b-std::sqrt(d))/a;return t>=0&&t<=1?t:2;
}
inline bool InCone(Point origin,Point direction,Point p,float radius) {
    const float x=p.x-origin.x,y=p.y-origin.y,d=std::hypot(x,y);if(d>radius)return false;if(d<.001f)return true;
    const float length=std::hypot(direction.x,direction.y);return length>.001f&&(x*direction.x+y*direction.y)/(d*length)>.55f;
}
inline float Read(const char* key) {return ModEncounter::Read(theWorldServer,key);}
inline void Set(const char* key,float value) {ModEncounter::Set(theWorldServer,key,value);}
inline int Now() {return theWorldServer.GetSegmentTimeCounter();}
inline bool Map() {return ModEncounter::Curse(theWorldServer);}
inline bool Active() {return Map()&&Read("ZC.Enabled")==1&&Read("ZC.Active")==1;}
inline float Slot(const char* kind,int index,const char* field) {char k[96];std::snprintf(k,sizeof(k),"ZC.%s%d.%s",kind,index,field);return Read(k);}
inline void Slot(const char* kind,int index,const char* field,float value) {char k[96];std::snprintf(k,sizeof(k),"ZC.%s%d.%s",kind,index,field);Set(k,value);}
inline CUnit* Actor(unsigned id) {return theWorldServer.GetUnit(int(id));}
inline bool Valid(CUnit* unit,bool alive=false) {return ModInvincible::UnitValid(unit,alive)&&ModInvincible::World(unit)==&theWorldServer;}
inline CUnitServer* Server(CUnit* unit) {return Valid(unit)?dynamic_cast<CUnitServer*>(unit->GetUnitSpecific()):nullptr;}
inline unsigned ID(CUnit* unit) {return unit?unsigned(unit->GetID()):0;}
inline CUnit* Party(int n) {return Actor(unsigned(Slot("Party",n,"Lo"))|(unsigned(Slot("Party",n,"Hi"))<<16));}
inline Point XY(CUnit* unit) {const auto p=unit->GetCP();return {p.x,p.y};}
inline float HP(CUnit* unit) {auto* s=Server(unit);return s?s->GetMapObjectServer()->GetCurrentHP():0;}
inline float Maximum(CUnit* unit) {auto* s=Server(unit);return s?s->GetMapObjectServer()->GetMaxHP():0;}
inline bool Live(CUnit* unit) {return Valid(unit,true)&&std::isfinite(HP(unit))&&HP(unit)>0;}
inline float Body(CUnit* unit) {const float r=unit->GetCollisionDistance();return std::isfinite(r)&&r>0&&r<8?r:.45f;}
inline float Range(CUnit* unit,CUnit* target) {auto* s=Server(unit);return s?s->EncounterAttackRange(target):Body(unit)+Body(target);}
inline void DirectDamage(CUnit* unit,float power) {if(auto* s=Server(unit))s->ApplyDirectDamage(power);}
}
