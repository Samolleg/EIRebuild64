#include "StdAfx.h"
#include "ModFinalGolem.h"
#include "ModFinalGolemState.h"
#include "ModArenaBoundary.h"
#include "ModScriptMotion.h"
#include "UMPlayer.h"
#include <cfloat>
#include <stdexcept>

namespace ModFinalGolem {
namespace FGX {using namespace ModFinalGolem;}
namespace {
void Facing(CUnit* unit,float angle,bool) {ModScriptMotion::SetRotate(std::to_string(unit->GetID()).c_str(),angle);}
CPlot3D Rotate(const float* q,CPlot3D v) {
 const CPlot3D u(q[1],q[2],q[3]);const float dot=u.x*v.x+u.y*v.y+u.z*v.z,n=u.x*u.x+u.y*u.y+u.z*u.z;
 return CPlot3D(2*dot*u.x+(q[0]*q[0]-n)*v.x+2*q[0]*(u.y*v.z-u.z*v.y),
  2*dot*u.y+(q[0]*q[0]-n)*v.y+2*q[0]*(u.z*v.x-u.x*v.z),2*dot*u.z+(q[0]*q[0]-n)*v.z+2*q[0]*(u.x*v.y-u.y*v.x));
}
static const float golemThrowOrigins[2][3]={{1.944029596f,1.883236350f,4.109739893f},{-1.944029596f,1.883236350f,4.109739893f}};
static const float golemThrowRotations[2][4]={{.985990027f,-.101141793f,.132574457f,-.004244622f},{.985990027f,-.101141793f,-.132574457f,.004244622f}};
#include "ModFinalGolemStages.inl"
int Stage(float hp) {return hp>2000.001f?1:hp>1500.001f?2:hp>1000.001f?3:hp>500.001f?4:5;}
void Prepare(CUnit* boss,CUnit* target) {
 if(!Valid(target))return;
 const auto p=boss->GetCP(),t=target->GetCP();float dx=t.x-p.x,dy=t.y-p.y,len=std::hypot(dx,dy);
 if(len<.01f){dx=0;dy=-1;}else{dx/=len;dy/=len;}
 auto& nav=theWorldServer.aiMap.Navigation();const auto from=nav.Position(boss,theWorldServer.aiMap);
 Navigation::StraightRequest request;request.source=from;request.direction={dx,dy,0};request.distance=99.92f;
 request.bodyRadius=(std::max)(.95f,Body(boss));request.boundaryInset=request.bodyRadius+.16f;request.bodies=ModArena::ChargeBodies();
 const auto trace=nav.TraceStraight(boss,request);
 if(trace.status==Navigation::Status::Updating)throw std::runtime_error("golem ram corridor is updating; retry before committing charge");
 if(trace.status!=Navigation::Status::Complete)throw std::runtime_error("golem ram has no valid straight corridor");
 Set("FGL.OX",p.x);Set("FGL.OY",p.y);Set("FGL.PX",p.x);Set("FGL.PY",p.y);Set("FGL.DX",dx);Set("FGL.DY",dy);
 Set("FGL.EX",trace.endpoint.position.x);Set("FGL.EY",trace.endpoint.position.y);Set("FGL.EZ",trace.endpoint.position.z);
 Set("FGL.Charge",Read("FGL.Charge")+1);Set("FGL.Blocked",0);Facing(boss,std::atan2(dx,-dy)*57.295779513f,true);
 Server(boss)->ScriptSpeedChanged();
}
void Contact(CUnit* boss,CUnit* hero) {
 if(!Valid(hero)||!Hero(hero)||Read("FGL.Phase")!=3)return;
 char key[80];std::snprintf(key,sizeof(key),"FGL.Hit.%u",unsigned(hero->GetID()));const float sequence=Read("FGL.Charge");if(Read(key)==sequence)return;
 const auto p=boss->GetCP(),h=hero->GetCP();
 // The actual accepted step is a segment of the one committed corridor.
 if(Distance2(h.x,h.y,Read("FGL.PX"),Read("FGL.PY"),p.x,p.y)>1.1f+Body(hero))return;
 Set(key,sequence);Set("FGL.Hits",Read("FGL.Hits")+1);
 for(int location:{4,5,1}) {
  if(!Valid(hero))break;auto* server=Server(hero);if(!server)break;SRPGAttackPortion ap;ap.Clear();
  ap.fpAttacks[2]=1;ap.fpToHit=FLT_MAX;ap.fpDamage=100;ap.hl=ERPGHitLocations(location);ap.bAreaEffect=TRUE;ap.bCalledShot=true;ap.fBackStabMultiplier=1;
  server->ProcessAttackPortion(&ap,nullptr,nullptr);
 }
 if(Valid(hero))DirectDamage(hero,50);
}
void Melee(CUnit* boss,CUnit* target) {
 target=Engaged(boss,target);if(target==boss||!Valid(target))return;auto* s=Server(boss);if(!s||s->HasAttack(target))return;
 unsigned count=unsigned(Read("FGL.MeleeCount"))+1;Set("FGL.MeleeCount",float(count));
 s->CmdAttack(target,ERPGHitLocations(2+count%4),false,0);
}
}

void Action(CUnit* boss,CUnit* other,int op) {
 if(op<0||op>28)throw std::runtime_error("FinalGolemAction operation must be 0..28");
 if(op>=24)throw std::runtime_error("FinalGolemAction hard operation is not implemented until packages 16-18");
 if(!ModEncounter::Arena(theWorldServer)||!Valid(boss,false)||boss->GetID()!=990400||!Server(boss))
  throw std::runtime_error("FinalGolemAction requires arena boss 990400 on the server");
 if(ModEncounter::Hard(theWorldServer)&&op<21)throw std::runtime_error("hard golem controller is not implemented until package 18");
 auto* s=Server(boss);ArmVisual();
 if(op==0) {
  for(int i=0;i<int(Read("FGL.PartyCount"))&&i<16;++i)if(auto* hero=Party(i);Valid(hero,false))if(auto* server=Server(hero))server->FinishEncounterPerformance();
  if(Valid(boss)){if(!(ModInvincible::Mask(boss)&ModInvincible::Bad))ModInvincible::Change(boss,"effects",1);Set("DJW.Stage",float((std::max)(Stage(HP(boss)),int(Read("DJW.Stage")))));TickExtras(boss);}return;
 }
 if(op==1){Melee(boss,other);return;}
 if(op==2){Prepare(boss,other);return;}
 if(op==3){Contact(boss,other);return;}
 if(op==4){Set("FGL.PX",boss->GetCP().x);Set("FGL.PY",boss->GetCP().y);return;}
 if(op==5){bool arrived=std::hypot(boss->GetCP().x-Read("FGL.EX"),boss->GetCP().y-Read("FGL.EY"))<.65f||Read("FGL.Blocked")!=0;Set("FGL.Arrived",arrived?1.f:0.f);Set("FGL.Resume",!arrived&&Read("FGL.Timer")>1&&!s->GetScriptCommand().IsMove()?1.f:0.f);return;}
 if(op==6){s->ScriptSpeedChanged();return;}
 if(op==7){if(Valid(boss)){ModInvincible::Change(boss,"effects",1);s->Heal(2500);s->RecalcRPGStats(false);Set("DJW.Stage",1);}return;}
 if(op==8){if(Valid(other)&&Hero(other)){int n=int(Read("FGL.PartyCount"));if(n>=0&&n<16){char key[48];const unsigned id=unsigned(other->GetID());std::snprintf(key,sizeof(key),"FGL.Party%d.Lo",n);Set(key,float(id&65535));std::snprintf(key,sizeof(key),"FGL.Party%d.Hi",n);Set(key,float(id>>16));Set("FGL.PartyCount",float(n+1));}}return;}
 if(op==9){s->CmdMoveTo(CPlot3D(Read("FGL.EX"),Read("FGL.EY"),0),false);return;}
 if(op==10){s->StopEncounterMovement();return;}
 if(op==11){Plan(boss,other);return;}
 if(op==12){ResetExtras();return;}
 if(op==13){Circle(boss);return;}
 if(op==14){BeginThrow(boss);return;}
 if(op==15){LaunchThrow(boss);return;}
 if(op==16){StartPursuit();return;}
 if(op==17){BeginLaser(boss);return;}
 if(op==18){AimLaser(boss);return;}
 if(op==19){BeginFire(boss);return;}
 if(op==20){HeroEffects(boss,other);return;}
 if(!Valid(other,false)||!Server(other)||!Server(other)->EncounterPerformance(op))throw std::runtime_error("encounter animation could not be applied");
}
void CaptureDamageTypes(CUnit* source,const SRPGAttackPortion& portion) {
 if(!source||source->GetID()!=990400||!Active())return;
 for(int i=0;i<3;++i){char key[40];std::snprintf(key,sizeof(key),"FGL.DamageType%d",i);Set(key,portion.fpAttacks[i]);}Set("FGL.DamageTypesKnown",1);
}
void MeleeDelivered(CUnit* source,CUnit* target,const SRPGAttackPortion& portion) {
 if(!source||!target||(source->GetID()!=990400&&target->GetID()!=990400)||!Active()||!Valid(source,false)||!Valid(target,false)||Read("FGL.Phase")!=1||portion.bAreaEffect||!(portion.fpAttacks[0]>0||portion.fpAttacks[1]>0||portion.fpAttacks[2]>0))return;
 if(target->GetID()==990400&&Hero(source)&&std::hypot(source->GetCP().x-target->GetCP().x,source->GetCP().y-target->GetCP().y)<=Range(target,source)+1)HeroVar(source,"FlankAt",float(Now()));
 if(source->GetID()!=990400||!Hero(target))return;
 Set("FGL.Timer",0);Set("FGL.ChaseTicks",0);Set("FGL.LastMelee",float(Now()));
 const unsigned id=unsigned(target->GetID());Set("FGL.TankLo",float(id&65535));Set("FGL.TankHi",float(id>>16));Set("FGL.TankAt",float(Now()));
}
float Speed(CUnit& unit,float native) {
 if(!Active()||unit.IsDead()||!std::isfinite(native)||native<=0)return native;
 if(unit.GetID()==990400) {
  float bonus=.25f*(std::max)(0,int(Read("DJW.Stage"))-2);if(bonus>0)native=(std::max)(.1f/7.5f,native+bonus/7.5f);
 }
 return native;
}
bool RamSpeed(CUnit& unit,float& speed) {
 if(!Active()||unit.GetID()!=990400||unit.IsDead()||speed<=0||Read("FGL.Phase")!=3)return false;
 speed=(ModEncounter::Hard(theWorldServer)?24.f:16.f)/18.f;return true;
}
float Slow(CUnit& unit) {
 return Active()&&unit.GetID()!=990400&&!unit.IsDead()&&HeroVar(&unit,"Slow")==1?.5f:1.f;
}
}
