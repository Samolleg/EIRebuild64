#include "StdAfx.h"
#include "ModGolemReflect.h"
#include "ModFinalGolemState.h"
#include "ModPackage.h"
#include "Item.h"
#include <cfloat>

namespace ModGolemReflect {
using namespace ModFinalGolem;
namespace {
int WeaponType(CUnit* source,const SRPGAttackPortion& portion) {
 auto* mod=ModRuntime::Current();auto captured=mod?mod->AttackWeapon(portion):nullptr;
 auto* weapon=captured?captured.get():source?source->GetCurrentQWeapon():nullptr;return weapon?int(weapon->GetWeaponType()):-1;
}
void Apply(int slot) {
 if(Slot("Reflect",slot,"Due")<=0)return;
 Slot("Reflect",slot,"Due",0); // consume before nested native damage
 unsigned id=unsigned(Slot("Reflect",slot,"Lo"))|(unsigned(Slot("Reflect",slot,"Hi"))<<16);
 auto* hero=Actor(id);if(!Valid(hero)||!Hero(hero))return;auto* server=Server(hero);if(!server)return;
 SRPGAttackPortion p;p.Clear();for(int k=0;k<7;++k){char key[8];std::snprintf(key,sizeof(key),"D%d",k);p.fpAttacks[k]=Slot("Reflect",slot,key);}
 p.fpToHit=FLT_MAX;p.fpDamage=Slot("Reflect",slot,"Power");p.fpDamagePercentage=Slot("Reflect",slot,"Percent");
 p.hl=ERPGHitLocations(int(Slot("Reflect",slot,"HL")));p.bAreaEffect=TRUE;p.fBackStabMultiplier=1;
 if(auto* mod=ModRuntime::Current())mod->ForgetAttack(p);
 ModAttackEvents::PortionOriginScope origin(p,*hero,ModAttackEvents::PortionOrigin::Reflected);
 server->ProcessAttackPortion(&p,nullptr,nullptr);
 Set("FGL.ReflectApplied",Read("FGL.ReflectApplied")+1);
}
void Enqueue(const Delivery& delivery) {
 const auto& p=delivery.portion;int now=Now(),best=0;float oldest=1.e30f;
 for(int i=0;i<Slots;++i){float end=Slot("Reflect",i,"End");if(end<=now){best=i;break;}if(end<oldest){oldest=end;best=i;}}
 Apply(best); // bounded overflow settles the evicted return rather than dropping it
 const unsigned id=unsigned(delivery.source->GetID());const auto a=delivery.source->GetCP(),b=delivery.target->GetCP();
 Slot("Reflect",best,"Lo",float(id&65535));Slot("Reflect",best,"Hi",float(id>>16));
 Slot("Reflect",best,"X",b.x);Slot("Reflect",best,"Y",b.y);Slot("Reflect",best,"Z",b.z+2.5f);
 Slot("Reflect",best,"EX",a.x);Slot("Reflect",best,"EY",a.y);Slot("Reflect",best,"EZ",a.z+.9f);
 int type=-1;float greatest=0;
 for(int k=0;k<7;++k){char key[8];std::snprintf(key,sizeof(key),"D%d",k);Slot("Reflect",best,key,p.fpAttacks[k]);if(p.fpAttacks[k]>greatest){greatest=p.fpAttacks[k];type=k;}}
 if((p.nSkill==RPGASKL_ARCHERY||delivery.projectile)&&(p.fpAttacks[0]>0||p.fpAttacks[1]>0||p.fpAttacks[2]>0))
  type=p.fpAttacks[0]>=p.fpAttacks[1]&&p.fpAttacks[0]>=p.fpAttacks[2]?0:p.fpAttacks[1]>=p.fpAttacks[2]?1:2;
 const int travel=type==5?2:(std::max)(3,(std::min)(36,int(std::ceil(std::hypot(a.x-b.x,a.y-b.y)*18.f/24.f))));
 Slot("Reflect",best,"Kind",float(type));Slot("Reflect",best,"Power",p.fpDamage);Slot("Reflect",best,"Percent",p.fpDamagePercentage);Slot("Reflect",best,"HL",float(p.hl));
 Slot("Reflect",best,"Born",float(now));Slot("Reflect",best,"Due",float(now+travel));Slot("Reflect",best,"End",float(now+travel+4));
 Set("FGL.ReflectUsed",(std::max)(Read("FGL.ReflectUsed"),float(best+1)));Set("FGL.ReflectTotal",Read("FGL.ReflectTotal")+1);
 // The ordinary MOB stops ticking a dead boss. Settle its accepted killing hit.
 if(!Valid(delivery.target))Apply(best);
}
}
Delivery Begin(CUnit* target,CUnit* source,const SRPGAttackPortion& p) {
 Delivery d;if(!target||target->GetID()!=990400||!source||!Active()||!Valid(target)||!Valid(source)||!Hero(source)||Read("FGL.Phase")==4)return d;
 if(Read("DJW.Stage")<3&&HP(target)>1500.001f)return d;
 if(!std::isfinite(p.fpDamage)||p.fpDamage<=0||!std::isfinite(p.fpDamagePercentage)||p.fpDamagePercentage<=0||p.hl<0||p.hl>6)return d;
 bool any=false;for(float value:p.fpAttacks){if(!std::isfinite(value)||value<0)return d;any|=value>0;}if(!any)return d;
 const auto origin=ModAttackEvents::PortionOriginScope::Resolve(p,target);
 if(origin==ModAttackEvents::PortionOrigin::Reflected||origin==ModAttackEvents::PortionOrigin::MeleeItemSpell)return d;
 bool eligible=origin==ModAttackEvents::PortionOrigin::Projectile||origin==ModAttackEvents::PortionOrigin::Spell;
 if(origin==ModAttackEvents::PortionOrigin::Unknown) {
  const int type=WeaponType(source,p);
  eligible=p.nSkill==RPGASKL_ARCHERY||(p.hl==RPGHL_ALL&&(p.fpAttacks[3]>0||p.fpAttacks[4]>0||p.fpAttacks[5]>0||p.fpAttacks[6]>0))||
   ((p.fpAttacks[0]>0||p.fpAttacks[1]>0||p.fpAttacks[2]>0)&&(type==5||type==6))||((p.fpAttacks[3]>0||p.fpAttacks[4]>0||p.fpAttacks[5]>0)&&(type==11||type==15));
 }
 if(!eligible)return d;
 d.source=source;d.target=target;d.author=ModAttackEvents::Identify(*source);d.boss=ModAttackEvents::Identify(*target);d.portion=p;d.candidate=true;d.projectile=origin==ModAttackEvents::PortionOrigin::Projectile;return d;
}
void Complete(Delivery& d) {
 if(!d.candidate||!d.accepted)return;d.candidate=false; // exactly once, including nested deliveries
 if(!Valid(d.source,false)||!Valid(d.target,false)||!(ModAttackEvents::Identify(*d.source)==d.author)||!(ModAttackEvents::Identify(*d.target)==d.boss))return;
 Enqueue(d);
}
void Tick() {
 const int now=Now(),used=std::clamp(int(Read("FGL.ReflectUsed")),0,Slots);
 for(int i=0;i<used;++i)if(Slot("Reflect",i,"Due")>0&&Slot("Reflect",i,"Due")<=now)Apply(i);
}
void Reset() {
 for(int i=0;i<Slots;++i){Slot("Reflect",i,"End",0);Slot("Reflect",i,"Due",0);}
 Set("FGL.ReflectUsed",0);Set("FGL.ReflectTotal",0);Set("FGL.ReflectApplied",0);
}
}
