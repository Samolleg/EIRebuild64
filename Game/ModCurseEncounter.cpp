#include "StdAfx.h"
#include "ModCurseEncounter.h"
#include "ModCurseState.h"
#include "ModAttackEvents.h"
#include "ModScriptMotion.h"
#include "MusicSystem.h"
#include "WorldClient.h"
#include "I3DMain.h"
#include "I3DScreenMission.h"
#include <cfloat>

namespace ModCurseEncounter {
static void Remember(const char* kind,int i,unsigned id) {Slot(kind,i,"Lo",float(id&65535));Slot(kind,i,"Hi",float(id>>16));}
static unsigned Remembered(const char* kind,int i) {return unsigned(Slot(kind,i,"Lo"))|(unsigned(Slot(kind,i,"Hi"))<<16);}
static void Notice(int id) {char key[40];sprintf_s(key,"ZC.NoticeSeen%d",id);if(Read(key)==1)return;Set(key,1);Set("ZC.Notice",float(id));}
static void Facing(CUnit* u,float angle,bool) {ModScriptMotion::SetRotate(std::to_string(u->GetID()).c_str(),angle);}
static bool Gesture(CUnit* u) {auto* s=Server(u);return Live(u)&&s&&s->CurseGesture(false);}
static void Stop(CUnit* u) {if(Live(u))if(auto* s=Server(u))s->StopEncounterMovement();}
static void EndHeld(CUnit* u) {if(auto* s=Server(u))s->EndCurseGesture();}
static void HeldGesture(CUnit* u) {if(Live(u))if(auto* s=Server(u))s->CurseGesture(true);}
static void Move(CUnit* u,Point p) {
 if(!Live(u))return;auto* s=Server(u);if(!s)return;auto* player=s->logic.GetPlayer();if(!player)return;
 const auto origin=XY(u);const float length=Distance(origin,p);if(length>2)p={origin.x+(p.x-origin.x)*2/length,origin.y+(p.y-origin.y)*2/length};
 player->DoMoveTo(s->logic,CPlot3D(p.x,p.y,0),false,false,CPlot3D(0,0,0));
}
static bool Place(CUnit* u,Point p) {
 if(!Live(u))return false;auto& map=theWorldServer.aiMap;const CPlot3D point(p.x,p.y,map.GetTileHeight(CPlot3D(p.x,p.y,0)));
 if(!map.CanPutObject(u,point)||!map.MoveObject(u,point))return false;
 Server(u)->InitializeUnit();Stop(u);u->SetUpdateMask(CMapObject::VLD_MASK_POSROT);return true;
}
static void Attack(CUnit* u,CUnit* target) {
 if(!Live(u)||!Live(target))return;auto* s=Server(u);if(!s||s->HasAttack(target))return;
 s->logic.StartEncounterAttack(target,ERPGHitLocations(1));
}
static void Pursue(CUnit* u,CUnit* target) {
 if(!Live(u)||!Live(target))return;auto* s=Server(u);if(!s)return;const int id=int(ID(u));
 const auto a=XY(u),b=XY(target);const float distance=Distance(a,b);
 if(distance>Range(u,target)+.2f){
  const bool moving=s->GetScriptCommand().IsMove()||s->GetNextScriptCommand().IsMove();
  if(!moving||std::hypot(b.x-Slot("Chase",id,"X"),b.y-Slot("Chase",id,"Y"))>1){
   const float gap=Body(u)+Body(target)+.25f;Move(u,{b.x+(a.x-b.x)*gap/distance,b.y+(a.y-b.y)*gap/distance});
   Slot("Chase",id,"X",b.x);Slot("Chase",id,"Y",b.y);Slot("Chase",id,"On",1);
  }return;
 }
 if(Slot("Chase",id,"On")==1){Stop(u);Slot("Chase",id,"On",0);}Attack(u,target);
}
static void Damage(CUnit* target,float power,int element,CUnit* source=nullptr) {
 if(!Live(target)||!std::isfinite(power)||power<=0)return;auto* s=Server(target);if(!s)return;
 SRPGAttackPortion p;p.Clear();p.fpAttacks[element]=1;p.fpToHit=FLT_MAX;p.fpDamage=power;
 p.fpDamagePercentage=1;p.hl=RPGHL_ALL;p.bAreaEffect=TRUE;p.bCalledShot=true;p.fBackStabMultiplier=1;
 s->ProcessAttackPortion(&p,source,nullptr);
 if(Valid(target,true)&&HP(target)<=0)DirectDamage(target,1);
}
static void Heal(CUnit* u,float power) {if(Live(u)&&power>0)if(auto* s=Server(u))s->Heal(power);}
static ModAttackEvents::Event LastAttack(CUnit* attacker,CUnit* target) {
 auto& journal=ModAttackEvents::Journal();journal.Sync(theWorldServer);
 const auto a=ModAttackEvents::Identify(*attacker),t=ModAttackEvents::Identify(*target);
 for(size_t n=0;n<journal.count;++n){const auto& e=journal.entries[(journal.next+journal.entries.size()-1-n)%journal.entries.size()];if(e.attacker==a&&e.target==t)return e;}
 return {};
}
static int Count(){return (std::min)(PartyLimit,(std::max)(0,(int)Read("ZC.PartyCount")));}
static CUnit* ChooseHero(){
 int count=Count(),start=(int)Read("ZC.TargetCursor");for(int i=0;i<count;i++){int n=(start+i)%count;auto u=Party(n);if(Live(u)){Set("ZC.TargetCursor",float(n+1));return u;}}return nullptr;
}
static bool Clear(Point p,CUnit*who){
 if(!Inside(p,3))return false;
 for(int i=0;i<Count();i++)if(Live(Party(i))&&Distance(p,XY(Party(i)))<2.5f)return false;
 for(unsigned id:{666666u,49435758u,49435759u}){auto u=Actor(id);if(u!=who&&Live(u)&&Distance(p,XY(u))<3.5f)return false;}
 for(int i=0;i<Pool;i++){auto u=Actor(710001+i);if(u!=who&&Slot("Enemy",i,"On")==1&&Live(u)&&Distance(p,XY(u))<2.5f)return false;}
 for(int i=0;i<8;i++){float a=i*.785398163f,b=(i+1)*.785398163f;if(!TurnGuardSweep(who,p.x+std::cos(a),p.y+std::sin(a),p.x+std::cos(b),p.y+std::sin(b)))return false;}return true;
}
static bool Clear(Point p,CUnit* who) {
 if(!Inside(p,3))return false;
 for(int i=0;i<(std::min)(PartyLimit,(std::max)(0,int(Read("ZC.PartyCount"))));++i)if(Live(Party(i))&&Distance(p,XY(Party(i)))<2.5f)return false;
 for(unsigned id:{666666u,49435758u,49435759u}){auto* u=Actor(id);if(u!=who&&Live(u)&&Distance(p,XY(u))<3.5f)return false;}
 for(int i=0;i<Pool;++i){auto* u=Actor(710001+i);if(u!=who&&Slot("Enemy",i,"On")==1&&Live(u)&&Distance(p,XY(u))<2.5f)return false;}
 auto& map=theWorldServer.aiMap;auto& nav=map.Navigation();
 for(int i=0;i<8;++i){const float a=i*.785398163f,b=(i+1)*.785398163f;
  const float x=p.x+std::cos(a),y=p.y+std::sin(a),ex=p.x+std::cos(b),ey=p.y+std::sin(b);
  Navigation::Location end;if(!nav.CanPlace(who,{x,y,map.GetTileHeight(CPlot3D(x,y,0))},who->GetAIClass())||
   !nav.Step(who,{x,y,map.GetTileHeight(CPlot3D(x,y,0))},{ex,ey,map.GetTileHeight(CPlot3D(ex,ey,0))},end))return false;
 }return true;
}
static void Finish(bool won){
 Set("ZC.Active",0);Set("ZC.Threat",0);Set("ZC.ShotBorn",0);Set("ZC.CastBorn",0);Set("ZC.ShotRelease",0);Set("ZC.ShotEnd",1);Set("ZC.MageMode",0);Set("ZC.CastMage",0);Set("ZC.CastBoss",0);
 for(const char*k:{"ZC.SoundThreat","ZC.SoundImpact","ZC.SoundWard"})Set(k,0);
 Set(won?"ZC.VictoryReady":"ZC.Failed",1);Set("ZC.Music",won?4.f:0.f);
 for(unsigned id:{666666u,640706u,49435758u,49435759u}){auto u=Actor(id);if(Slot("Anim",id,"Until")>0)EndHeld(u);else Stop(u);}
 for(int i=0;i<Pool;i++)if(Slot("Enemy",i,"On")==1){auto u=Actor(710001+i);if(Slot("Anim",710001+i,"Until")>0)EndHeld(u);else Stop(u);if(won&&Live(u))DirectDamage(u,HP(u)+1);Slot("Enemy",i,"On",0);}
}
static void EnsureMusic() {
 auto* music=theWorld.GetMusicSystem();if(!music||music->GetMode()==EMM_ZONE)return;
 const bool mission=theInterface.GetScreensNum()==1&&dynamic_cast<CI3DScreenMission*>(theInterface.GetCurrentScreen())!=nullptr;
 if(!Active()&&!mission)return;music->SetZoneMode("gz20g",TRUE);if(music->GetMode()==EMM_ZONE)music->UnpauseZoneMode();
}
static void SpawnWave(int wave){
 // Five finite waves (1+2+3+3+3) of pre-authored actors. Never reuse dead actor IDs or depend
 // on friendly golem survival. An obstructed spawn is retried, not consumed.
 static const Point sites[]={{62,64},{98,64},{64,94},{96,94},{60,79},{100,79}};
 int first=WaveOffset(wave),count=WaveSize(wave);bool complete=true;
 for(int i=first;i<first+count;i++){
  if(Slot("Enemy",i,"Used")==1)continue;auto u=Actor(710001+i);bool placed=false;
  for(int j=0;Live(u)&&j<6;j++){Point p=sites[(i+j)%6];if(Clear(p,u)&&Place(u,p)){placed=true;break;}}
  if(placed){Slot("Enemy",i,"Used",1);Slot("Enemy",i,"On",1);Slot("Enemy",i,"Ready",float(Now()+3*18));Slot("Enemy",i,"Due",float(Now()+6*18));}
  else complete=false;
 }
 if(complete){Set("ZC.Waves",float(wave+1));Set("ZC.NextThreat",float(Now()+6*18));Notice(wave==0?2:3);}
}
static int Enemies(CUnit*mage){
 int alive=0,pressure=0,now=Now();Point m=XY(mage);
 for(int i=0;i<Pool;i++){
  if(Slot("Enemy",i,"On")!=1)continue;auto u=Actor(710001+i);
  if(!Live(u)){Slot("Enemy",i,"On",0);Slot("Enemy",i,"Channel",0);continue;}alive++;
  bool caster=i%3==2;float d=Distance(XY(u),m);if(now<Slot("Enemy",i,"Ready"))continue;
  if(!caster){
   // Real attack attempts draw attention even when armour absorbs the hit.
   for(int h=0;h<Count();h++){auto hero=Party(h);if(!Live(hero))continue;auto e=LastAttack(hero,u);
    if(e.sequence&&e.tick>Slot("Enemy",i,"Hit")){Slot("Enemy",i,"Hit",float(e.tick));Remember("Aggro",i,ID(hero));Slot("Enemy",i,"AggroUntil",float(now+8*18));Stop(u);}
   }
   auto target=Actor(Remembered("Aggro",i));
   if(!Live(target)||now>=Slot("Enemy",i,"AggroUntil")||!Inside(XY(target)))target=nullptr;
   // Standing in the knight's approach also works as ordinary bodyguard play.
   if(!target)for(int h=0;h<Count();h++){auto hero=Party(h);if(Live(hero)&&Distance(XY(u),XY(hero))<3.5f){target=hero;Remember("Aggro",i,ID(hero));Slot("Enemy",i,"AggroUntil",float(now+4*18));break;}}
   if(!target)target=mage;if(target==mage&&d<5)pressure++;
   if(now%18==0)Pursue(u,target);continue;
  }
  // Pressure breaks concentration even when armour absorbs the attack.
  for(int h=0;h<Count();h++)if(Live(Party(h))){auto e=LastAttack(Party(h),u);if(e.sequence&&e.tick>Slot("Enemy",i,"Hit")){Slot("Enemy",i,"Hit",float(e.tick));Slot("Enemy",i,"Interrupted",float(now+4*18));}}
  bool channel=d<15&&now>=Slot("Enemy",i,"Interrupted");if(!channel&&Slot("Enemy",i,"Channel")==1)EndHeld(u);if(channel&&Slot("Enemy",i,"Channel")!=1)Stop(u);Slot("Enemy",i,"Channel",channel?1.f:0.f);
  if(channel){pressure++;if(now>=Slot("Enemy",i,"Due")){HeldGesture(u);Damage(mage,60,5,u);if(Live(mage))DirectDamage(mage,Maximum(mage)*.01f);Slot("Enemy",i,"Due",float(now+18));Set("ZC.CastEnemy",float(i+1));}}
  else if(now%18==0){if(d>13)Move(u,m);else Stop(u);}
 }
 Set("ZC.Enemies",float(alive));Set("ZC.Pressure",float(pressure));return alive;
}
static void Frontline(CUnit*boss){
 CUnit*target=nullptr;int alive=0;
 for(unsigned id:{49435758u,49435759u}){auto u=Actor(id);if(!Live(u))continue;alive++;if(!target)target=u;Pursue(u,boss);}
 Set("ZC.Guardians",float(alive));if(!target){
  // A defender can intercept the boss after the stone frontline falls.
  for(int i=0;i<Count();i++){auto h=Party(i);if(Live(h)&&Distance(XY(h),XY(boss))<Range(boss,h)+2){target=h;break;}}
  if(!target)target=Actor(640706);Notice(8);
 }
 if(target){Remember("BossTarget",0,ID(target));if(Read("ZC.Threat")==0)Pursue(boss,target);}
}
static void EndThreat(){Set("ZC.Threat",0);Set("ZC.NextThreat",float(Now()+Rest));}
static void Impact(Point at,float radius,CUnit*mage,CUnit*boss,bool hurtMage=false){
 for(int i=0;i<Count();i++){auto h=Party(i);if(Live(h)&&Distance(XY(h),at)<=radius){Damage(h,110,5,boss);if(Live(h))DirectDamage(h,Maximum(h)*.28f);}}
 // Hero marks must not silently punish the escort objective as collateral.
 if(hurtMage&&Live(mage)&&Distance(XY(mage),at)<=radius)Damage(mage,130,5,boss);
 Set("ZC.ImpactX",at.x);Set("ZC.ImpactY",at.y);Set("ZC.ImpactAt",float(Now()));
 Set("ZC.SoundImpact",1);
}
static void Threat(CUnit*boss,CUnit*mage){
 int now=Now(),type=(int)Read("ZC.Threat"),born=(int)Read("ZC.ThreatBorn"),age=now-born;
 if(type==0){
  if(now<Read("ZC.NextThreat"))return;int seq=(int)Read("ZC.ThreatSeq"),phase=(int)Read("ZC.Phase");
  type=Read("ZC.Enemies")>0?1:seq%2==0?1:2;if(Read("ZC.Enemies")==0&&phase==3&&seq%3==2)type=4;
  auto hero=ChooseHero();if(!hero)return;Remember("Target",0,ID(hero));Point p=type==3?XY(mage):XY(hero);
  Set("ZC.X",p.x);Set("ZC.Y",p.y);Set("ZC.Threat",float(type));Set("ZC.ThreatBorn",float(now));Set("ZC.ThreatSeq",float(seq+1));Set("ZC.CastBoss",1);
  Set("ZC.SoundThreat",float(type));
  Notice(type==1?4:type==2?5:type==3?6:7);
  Stop(boss);
  if(type==2){Point a=XY(boss),b=XY(hero);Set("ZC.LanceAX",a.x);Set("ZC.LanceAY",a.y);Set("ZC.LanceBX",b.x);Set("ZC.LanceBY",b.y);}
  if(type==3){Set("ZC.CastMage",0);static const Point pads[]={{72,79},{88,79},{80,90}};int pad=((int)Read("ZC.Pad")+1)%3;for(int j=0;j<3;j++){auto p2=pads[(pad+j)%3];if(Distance(p2,XY(mage))>5&&Clear(p2,mage)){Set("ZC.Pad",float((pad+j)%3));Set("ZC.PadX",p2.x);Set("ZC.PadY",p2.y);Move(mage,p2);break;}}}
  return;
 }
 if(type==1){
  if(age<MarkFollow){unsigned id=(unsigned)Slot("Target",0,"Lo")|((unsigned)Slot("Target",0,"Hi")<<16);auto u=Actor(id);if(Live(u)){Set("ZC.X",XY(u).x);Set("ZC.Y",XY(u).y);}}
  if(age>=MarkImpact){Impact({Read("ZC.X"),Read("ZC.Y")},3,mage,boss);EndThreat();}
 }
 if(type==2&&age>=LanceWarn){
  Point a={Read("ZC.LanceAX"),Read("ZC.LanceAY")},b={Read("ZC.LanceBX"),Read("ZC.LanceBY")};
  float previous=(std::max)(0.f,(std::min)(1.f,float(age-LanceWarn-1)/LanceFlight));
  float t=(std::min)(1.f,float(age-LanceWarn)/LanceFlight);
  Point p={a.x+(b.x-a.x)*previous,a.y+(b.y-a.y)*previous},q={a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};
  float first=2;CUnit*hit=nullptr;
  for(int i=0;i<Count();i++){auto h=Party(i);if(!Live(h))continue;float contact=SweptContact(p,q,XY(h),Body(h)+.65f);if(contact<first){first=contact;hit=h;}}
  if(hit){Damage(hit,95,5,boss);Set("ZC.BoltHits",Read("ZC.BoltHits")+1);Set("ZC.SoundImpact",1);Set("ZC.ImpactAt",float(now));Set("ZC.ImpactX",q.x);Set("ZC.ImpactY",q.y);EndThreat();}
  else if(t>=1){Set("ZC.LanceMisses",Read("ZC.LanceMisses")+1);EndThreat();}
 }
 if(type==3&&age>=5*18){Impact({Read("ZC.X"),Read("ZC.Y")},5,mage,boss,true);EndThreat();}
 if(type==4&&age>=ConeWarn){
  auto a=XY(boss);Point d={Read("ZC.X")-a.x,Read("ZC.Y")-a.y};
  for(int i=0;i<Count();i++){auto h=Party(i);if(Live(h)&&InCone(a,d,XY(h),10)){Damage(h,110,3,boss);if(Live(h))DirectDamage(h,Maximum(h)*.1f);}}EndThreat();
 }
}
static void Tick(CUnit*boss,CUnit*mage){
 if(!Map()||Read("ZC.Enabled")!=1)return;EnsureMusic();int now=Now();
 // Idempotent per simulation tick, even if a script resumes twice.
 if(Read("ZC.LastTick")==now)return;Set("ZC.LastTick",float(now));
 for(unsigned id:{640706u,710003u,710006u,710009u,710012u})if(Slot("Anim",id,"Until")>0&&now>=Slot("Anim",id,"Until"))EndHeld(Actor(id));
 if(!Active()){
  if(Read("ZC.VictoryReady")||Read("ZC.Failed"))return;bool triggered=false,all=true;int living=0;
  for(int i=0;i<Count();i++){auto h=Party(i);if(!Live(h))continue;living++;auto p=XY(h);triggered=triggered||p.y<79;all=all&&Inside(p,2);}
  if(!living||!triggered||!all)return;Set("ZC.Active",1);Set("ZC.Started",float(now));Set("ZC.Music",1);Set("ZC.NextThreat",float(now+12*18));Set("ZC.WaveDue",float(now+8*18));Set("ZC.MageDue",float(now+2*18));Notice(1);
 }
 if(!Live(mage)){Finish(false);return;}if(!Live(boss)){Finish(true);return;}
 bool any=false;for(int i=0;i<Count();i++)any=any||Live(Party(i));if(!any){Finish(false);return;}
 int phase=(std::max)((int)Read("ZC.Phase"),Phase(HP(boss),Maximum(boss)));
 if(phase!=Read("ZC.Phase")){Set("ZC.Phase",float(phase));Set("ZC.Music",float(phase));}
 int enemies=Enemies(mage),wave=(int)Read("ZC.Waves");if(!Live(mage)){Finish(false);return;}
 bool wasEnemies=Read("ZC.HadEnemies")==1;Set("ZC.HadEnemies",enemies>0?1.f:0.f);
 if(wasEnemies&&enemies==0){Set("ZC.WaveDue",float(now+20*18));}
 if(enemies==0&&wave<Waves&&Read("ZC.Threat")==0&&now>=Read("ZC.WaveDue"))SpawnWave(wave);
 if(now%18==0)Frontline(boss);
 if(Read("ZC.ShotBorn")>0&&now>=Read("ZC.ShotBorn")+18){Damage(boss,320,3,mage);Set("ZC.ShotBorn",0);Set("ZC.ShotEnd",1);}
 if(!Live(boss)){Finish(true);return;}
 bool moving=Read("ZC.Threat")==3&&Distance(XY(mage),{Read("ZC.PadX"),Read("ZC.PadY")})>1;
 if(moving&&now%18==0)Move(mage,{Read("ZC.PadX"),Read("ZC.PadY")});
 int mode=moving?3:Read("ZC.Pressure")>0?2:1;Set("ZC.MageMode",float(mode));
 if(mode!=1&&Read("ZC.CastBorn")>0){Set("ZC.CastBorn",0);EndHeld(mage);}
 if(mode==1&&Read("ZC.CastBorn")>0&&now>=Read("ZC.ReleaseDue")){
  Set("ZC.CastBorn",0);Set("ZC.ShotBorn",float(now));Set("ZC.ShotRelease",1);Set("ZC.ShotX",XY(boss).x);Set("ZC.ShotY",XY(boss).y);Set("ZC.MageAttacks",Read("ZC.MageAttacks")+1);
 }
 if(mode==1&&now>=Read("ZC.MageDue")&&Read("ZC.CastBorn")==0&&Read("ZC.ShotBorn")==0){
  Facing(mage,std::atan2(XY(boss).x-XY(mage).x,-(XY(boss).y-XY(mage).y))*57.295779513f,true);Stop(mage);HeldGesture(mage);
  const auto* e=mage->GetAnimationEntry("uattack");const float coeff=e?mage->GetAnimationCoeff(e):1;
  int hit=e&&coeff>0?int(std::ceil(e->dwHit/coeff)):18;hit=(std::max)(12,(std::min)(18,hit));
  Set("ZC.CastBorn",float(now));Set("ZC.ReleaseDue",float(now+hit));Set("ZC.MageDue",float(now+4*18));if(HP(mage)<Maximum(mage)*.75f)Heal(mage,8);
 }
 if(mode!=1)Set("ZC.MageDue",float(now+18));
 // Reinforcements coexist with a simple mark. More complex lance/cone
 // patterns wait until the escort pressure is cleared.
 if(Read("ZC.Waves")==0||now<Read("ZC.Started")+18*18)Set("ZC.NextThreat",float(now+4*18));
 else Threat(boss,mage);
 if(!Live(mage))Finish(false);
}
void Action(CUnit*boss,CUnit*other,int op){
 if(!Map()||ID(boss)!=666666||!Valid(boss))return;
 ArmVisual();
 if(op==0){Tick(boss,other);return;}
 if(op==1){
  if(Read("ZC.Enabled")==1&&Read("ZC.Version")==3)return;
  for(const char*k:{"Active","VictoryReady","Failed","Waves","Phase","Threat","ThreatSeq","TargetCursor","PartyCount","Music","MageMode","ShotBorn","CastMage","CastBoss","CastEnemy","Intercepts","LanceMisses","MageAttacks","Pad","HadEnemies","WaveDue","Notice","BoltHits","Guardians","Started","CastBorn","ShotRelease","ShotEnd","ReleaseDue"}){char key[64];sprintf_s(key,"ZC.%s",k);Set(key,0);}
  for(int i=1;i<=8;i++){char key[40];sprintf_s(key,"ZC.NoticeSeen%d",i);Set(key,0);}
  for(int i=0;i<Pool;i++)for(const char*k:{"On","Used","Hit","Interrupted","Channel","Due","Ready","AggroUntil"})Slot("Enemy",i,k,0);
  Set("ZC.LastTick",-1);Set("ZC.Enabled",1);Set("ZC.Version",3);return;
 }
 if(op==2&&Live(other)&&ModEncounter::Hero(theWorldServer,other)){
  for(int i=0;i<Count();i++)if(Party(i)==other)return;int n=Count();if(n<PartyLimit){Remember("Party",n,ID(other));Set("ZC.PartyCount",float(n+1));}
 }
 if(op==3&&Active()&&(ID(other)==666666||ID(other)==640706))Gesture(other);
}
}
namespace ModCurseEncounter {
DamageScope::DamageScope(CUnit* target,CUnit* author,SRPGAttackPortion& attack)
 :portion(attack),power(attack.fpDamage),toHit(attack.fpToHit) {
 if(!Active()||!Valid(target,true))return;
 const unsigned id=ID(target),source=Valid(author,true)?ID(author):0;float factor=1;
 if(id==640706&&source==666666){
  const bool shield=Live(Actor(49435758))||Live(Actor(49435759));
  portion.fpDamage=shield?0.f:(std::min)(Maximum(target)*.35f,power);changed=true;return;
 }
 if(id==640706)factor=Read("ZC.MageMode")==2?.35f:.5f;
 else if(id==666666&&(source==49435758||source==49435759))factor=.1f;
 else if(source==666666&&(id==49435758||id==49435759)){portion.fpDamage=0;portion.fpToHit=FLT_MAX;changed=true;return;}
 else if(source>=710001&&source<710001+Pool&&(source-710001)%3!=2)
  for(int i=0;i<Count();++i)if(Party(i)==target){factor=.55f;break;}
 if(factor!=1){portion.fpDamage*=factor;changed=true;}
}
DamageScope::~DamageScope() {if(changed){portion.fpDamage=power;portion.fpToHit=toHit;}}
void Contact(CUnit* attacker,CUnit* target) {
 if(!Active()||!Valid(attacker,true)||!Valid(target,true)||ID(attacker)!=666666)return;
 const unsigned id=ID(target);if(id!=49435758&&id!=49435759)return;
 char key[64];sprintf_s(key,"ZC.GuardContact%u",id);if(Read(key)==Now())return;
 Set(key,float(Now()));DirectDamage(target,Maximum(target)*.04f);
}
}
