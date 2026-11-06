// Hard encounter policy from plugin 421604cc78b4. Timers/LCG remain in GS.
static bool Hard(){return ModEncounter::Hard(theWorldServer);}
static bool Inside(float x,float y,float radius=0){return x>=28+radius&&x<=100-radius&&y>=26+radius&&y<=62-radius;}
static void HardReset(){
 if(!Hard())return;
 for(int i=0;i<int(Read("FGL.PartyCount"))&&i<16;++i)if(auto* h=Party(i);Valid(h,false)){HeroVar(h,"SlamHit",0);HeroVar(h,"PulseHit",0);}
 for(const char* k:{"HG.RamCycle","HG.RamChain","HG.RamAgain","HG.RamTowerHit","HG.SlamClock","HG.SlamSeq","HG.SlamBorn","HG.SlamFired","HG.ObeliskID","HG.ObeliskBorn","HG.ObeliskDue","HG.ObeliskRole","HG.PreviousRole","HG.GrowBorn","HG.PulseBorn","HG.PulseSeq","HG.PulseDue","HG.TowerDead","HG.GuardDead","HG.GuardID","HG.GuardDue","HG.GuardTotal","HG.GuardHits","HG.HealNext","HG.ObeliskTotal"})Set(k,0);
}
static void HardRamBegin(){if(!Hard())return;if(Read("HG.RamChain")==0){Set("HG.RamCycle",Read("HG.RamCycle")+1);Set("HG.RamChain",1);Set("HG.RamTowerHit",0);}Set("HG.RamAgain",0);}
static void HardRamEnd(){Set("HG.RamAgain",0);if(!Hard())return;bool second=Read("DJW.Stage")>=3&&Read("HG.RamChain")==1&&int(Read("HG.RamCycle"))%2==0&&Read("HG.RamTowerHit")==0;Set("HG.RamAgain",second?1.f:0.f);Set("HG.RamChain",second?2.f:0.f);}
static void HardSlamBegin(CUnit* boss){
 if(!Hard())return;auto* h=RandomHero();if(!Valid(h))return;const auto b=boss->GetCP(),p=h->GetCP();float angle=std::atan2(p.x-b.x,-(p.y-b.y))*57.295779513f;
 Set("HG.SlamX",b.x);Set("HG.SlamY",b.y);Set("HG.SlamAngle",angle);Set("HG.SlamBorn",float(Now()));Set("HG.SlamSeq",Read("HG.SlamSeq")+1);Set("HG.SlamFired",0);Set("HG.SlamClock",0);Facing(boss,angle,true);
}
static void HardSlamTick(){
 int age=Now()-int(Read("HG.SlamBorn"));if(Read("HG.SlamBorn")<=0||age<22||age>112)return;Set("HG.SlamFired",1);
 float radius=(age-22)*12.f/18.f,previous=(std::max)(0.f,radius-12.f/18.f),seq=Read("HG.SlamSeq");
 for(int i=0;i<int(Read("FGL.PartyCount"))&&i<16;++i){auto* h=Party(i);if(!Valid(h)||!Hero(h)||HeroVar(h,"SlamHit")==seq)continue;const auto p=h->GetCP();float x=p.x-Read("HG.SlamX"),y=p.y-Read("HG.SlamY");bool hit=false;
  for(float spread:{-24.f,0.f,24.f}){float a=(Read("HG.SlamAngle")+spread)*.01745329252f,along=x*std::sin(a)-y*std::cos(a),across=std::abs(x*std::cos(a)+y*std::sin(a));hit=hit||(along>=previous-.6f&&along<=radius+.6f&&across<=.55f+Body(h)&&Inside(p.x,p.y));}
  if(hit){HeroVar(h,"SlamHit",seq);if(auto* s=Server(h))DirectDamage(h,Percent(s->GetMapObjectServer()->GetMaxHP(),.20f));}
 }
}
static CUnit* HardTower(){return Actor(unsigned(Read("HG.ObeliskID")));}
static bool HardClear(CUnit* boss,float x,float y,float radius){
 if(!Inside(x,y,radius+1))return false;auto avoid=[&](CUnit* u){return !Valid(u)||std::hypot(x-u->GetCP().x,y-u->GetCP().y)>radius+Body(u)+1.25f;};
 if(!avoid(boss)||!avoid(Actor(unsigned(Read("HG.GuardID")))))return false;
 for(int i=0;i<int(Read("FGL.PartyCount"))&&i<16;++i)if(!avoid(Party(i)))return false;
 auto& nav=theWorldServer.aiMap.Navigation();const float z=nav.Position(boss,theWorldServer.aiMap).z;
 for(int i=0;i<12;++i){float a=i*.5235987756f;if(!nav.CanPlace(boss,{x+radius*std::cos(a),y+radius*std::sin(a),z},boss->GetAIClass()))return false;}return true;
}
static void HardEntitiesReset(){if(!Hard())return;for(const char* key:{"HG.ObeliskID","HG.GuardID"}){unsigned id=unsigned(Read(key));if(id)ModUnitSpawns::Despawn(float(id));Set(key,0);}}
static void HardTowerRam(CUnit* boss){auto* t=HardTower();if(Read("FGL.Phase")!=3||!Valid(t))return;const auto p=boss->GetCP(),q=t->GetCP();if(Distance2(q.x,q.y,Read("FGL.PX"),Read("FGL.PY"),p.x,p.y)>Body(boss)+Body(t)+.55f)return;DirectDamage(t,10000);DirectDamage(boss,100);Set("HG.RamTowerHit",1);Set("FGL.Blocked",1);Set("HG.PulseBorn",0);}
static void HardPulse(CUnit* tower){
 int now=Now(),phase=int(Read("FGL.Phase"));if(phase==2||phase==3||phase==8||phase==9||phase==10){Set("HG.PulseBorn",0);Set("HG.PulseDue",float(now+27));return;}
 float born=Read("HG.PulseBorn");if(born<=0){if(now<Read("HG.PulseDue"))return;born=float(now);Set("HG.PulseBorn",born);Set("HG.PulseSeq",Read("HG.PulseSeq")+1);}int age=now-int(born);if(age<27)return;
 const auto p=tower->GetCP();float limit=0;for(float x:{28.f,100.f})for(float y:{26.f,62.f})limit=(std::max)(limit,std::hypot(x-p.x,y-p.y));float r=(age-27)*2.5f/18.f,prev=(std::max)(0.f,r-2.5f/18.f);Set("HG.PulseRadius",r);
 if(r>limit+2){Set("HG.PulseBorn",0);Set("HG.PulseDue",float(now+6*18));return;}
 for(int i=0;i<int(Read("FGL.PartyCount"))&&i<16;++i){auto* h=Party(i);if(!Valid(h)||!Hero(h)||HeroVar(h,"PulseHit")==Read("HG.PulseSeq"))continue;float d=std::hypot(h->GetCP().x-p.x,h->GetCP().y-p.y),reach=Body(h)+.55f;if(d<prev-reach||d>r+reach)continue;
  HeroVar(h,"PulseHit",Read("HG.PulseSeq"));if(h->GetCurrentPose()==UP_LAY)continue;auto* server=Server(h);if(!server)continue;SRPGAttackPortion portion;portion.Clear();portion.fpAttacks[2]=1;portion.fpToHit=FLT_MAX;portion.fpDamage=100;portion.hl=RPGHL_ALL;portion.bAreaEffect=TRUE;portion.bCalledShot=true;portion.fBackStabMultiplier=1;server->ProcessAttackPortion(&portion,nullptr,nullptr);if(Valid(h))DirectDamage(h,20);
 }
}
static void HardGuard(CUnit* boss){
 int now=Now();unsigned id=unsigned(Read("HG.GuardID"));auto* g=Actor(id);
 if(id&&!Valid(g)){if(Read("HG.GuardDead")==0){Set("HG.GuardDead",float(now));Set("HG.GuardDue",float(now+10*18));}if(!g||now-Read("HG.GuardDead")>=90){if(g)ModUnitSpawns::Despawn(float(id));Set("HG.GuardID",0);}return;}
 if(!Valid(g))return;auto* server=Server(g);if(!server)return;CUnit* target=nullptr;float distance=1.e10f;
 for(int i=0;i<int(Read("FGL.PartyCount"))&&i<16;++i){auto* h=Party(i);if(!Valid(h)||!Hero(h))continue;float d=std::hypot(h->GetCP().x-g->GetCP().x,h->GetCP().y-g->GetCP().y);if(d<distance){distance=d;target=h;}}
 if(!target)return;const float reach=server->EncounterAttackRange(target);
 if(distance>reach+.20f){const auto p=target->GetCP();bool changed=Read("HG.GuardChase")!=1||std::hypot(p.x-Read("HG.GuardX"),p.y-Read("HG.GuardY"))>1;bool stopped=!server->GetScriptCommand().IsMove()&&!server->IsL1Moving();
  if((changed||stopped)&&now>=Read("HG.GuardMoveDue")){auto& nav=theWorldServer.aiMap.Navigation();Navigation::Location goal;if(nav.AttackPosition(g,target,nav.Position(target,theWorldServer.aiMap),reach-.05f,1.e6f,goal)){
    const auto from=nav.Position(g,theWorldServer.aiMap);auto route=nav.Find(g,from,goal.position,1.e6f,0,nullptr,g->GetAIClass(),Navigation::GoalPolicy::Exact,&goal);
    if(route.status==Navigation::Status::Complete){auto point=goal.position;for(const auto& step:route.points){point=step.position;if(std::hypot(point.x-from.x,point.y-from.y)>=3.25f)break;}server->CmdMoveTo(CPlot3D(point.x,point.y,0),false);Set("HG.GuardX",p.x);Set("HG.GuardY",p.y);Set("HG.GuardChase",1);}
   }Set("HG.GuardMoveDue",float(now+9));}return;
 }
 if(Read("HG.GuardChase")==1){server->StopEncounterMovement();Set("HG.GuardChase",0);}if(!server->HasAttack(target))server->CmdAttack(target,RPGHL_BODY,false,0);
}
static void HardObelisks(CUnit* boss){
 if(!Hard())return;int now=Now();HardGuard(boss);HardTowerRam(boss);unsigned id=unsigned(Read("HG.ObeliskID"));auto* tower=HardTower();
 if(id&&!Valid(tower)){if(Read("HG.TowerDead")==0){Set("HG.TowerDead",float(now));Set("HG.ObeliskDue",float(now+25*18));Set("HG.PulseBorn",0);}if(!tower||now-Read("HG.TowerDead")>=54){if(tower)ModUnitSpawns::Despawn(float(id));Set("HG.ObeliskID",0);}return;}
 if(!id){if(Read("DJW.Stage")<2||now<Read("HG.ObeliskDue"))return;float born=Read("HG.GrowBorn"),x=Read("HG.ObeliskX"),y=Read("HG.ObeliskY");
  if(born<=0){bool free=false;for(int n=0;n<48&&!free;++n){x=32+Random()*64;y=30+Random()*28;free=HardClear(boss,x,y,1.1f);}if(!free){Set("HG.ObeliskDue",float(now+18));return;}Set("HG.ObeliskX",x);Set("HG.ObeliskY",y);Set("HG.GrowBorn",float(now));return;}
  if(!HardClear(boss,x,y,1.1f)){Set("HG.GrowBorn",0);Set("HG.ObeliskDue",float(now+18));return;}if(now-born<36)return;
  unsigned created=ModUnitSpawns::Spawn(0,"RubyResonator",float(boss->GetPlayerNum()),x,y,boss->GetCP().z);Set("HG.GrowBorn",0);if(!created){Set("HG.ObeliskDue",float(now+36));return;}
  Set("HG.ObeliskID",float(created));Set("HG.ObeliskBorn",float(now));Set("HG.TowerDead",0);Set("HG.HealNext",float(now+18));Set("HG.PulseDue",float(now));Set("HG.PulseBorn",0);int previous=int(Read("HG.PreviousRole")),role=1+int(Random()*3);if(role==previous)role=1+role%3;Set("HG.PreviousRole",float(role));Set("HG.ObeliskRole",float(role));Set("HG.ObeliskTotal",Read("HG.ObeliskTotal")+1);return;
 }
 if(auto* server=Server(tower);server&&(server->GetScriptCommand().IsMove()||server->IsL1Moving()))server->StopEncounterMovement();int role=int(Read("HG.ObeliskRole"));
 if(role==1&&now>=Read("HG.HealNext")){float cap=(6-int(Read("DJW.Stage")))*1000.f,hp=HP(boss);if(auto* server=Server(boss);server&&hp<cap){server->Heal((std::min)(20.f,cap-hp));server->RecalcRPGStats(false);}Set("HG.HealNext",float(now+18));}
 if(role==2&&Read("HG.GuardID")==0&&now>=Read("HG.GuardDue")){float x=Read("HG.ObeliskX"),y=Read("HG.ObeliskY");bool clear=false;for(int n=0;n<24&&!clear;++n){float a=Random()*6.283185307f;x=Read("HG.ObeliskX")+std::cos(a)*3.4f;y=Read("HG.ObeliskY")+std::sin(a)*3.4f;clear=HardClear(boss,x,y,.75f);}if(clear){unsigned guard=ModUnitSpawns::Spawn(0,"RubyMechanismGuard",float(boss->GetPlayerNum()),x,y,boss->GetCP().z);if(guard){Set("HG.GuardID",float(guard));Set("HG.GuardDead",0);Set("HG.GuardChase",0);Set("HG.GuardMoveDue",0);Set("HG.GuardTotal",Read("HG.GuardTotal")+1);}}Set("HG.GuardDue",float(now+10*18));}
 if(role==3)HardPulse(tower);
}
static void HardTick(CUnit*){if(!Hard())return;if(Read("FGL.Phase")==1)Set("HG.SlamClock",Read("HG.SlamClock")+1);HardSlamTick();}
static void HardEnsureMusic(){
 if(!Hard()||Read("EFC.Enabled")!=1||Read("EF.Playing")==1)return;auto* music=theWorld.GetMusicSystem();if(!music||music->GetMode()==EMM_ZONE)return;
 const float stage=Read("EFC.Stage");const bool mission=theInterface.GetScreensNum()==1&&dynamic_cast<CI3DScreenMission*>(theInterface.GetCurrentScreen())!=nullptr;
 if(!(stage>=1&&stage<8)&&!mission)return;music->SetZoneMode("gz19g",TRUE);if(music->GetMode()==EMM_ZONE){music->UnpauseZoneMode();Set("EM.ModeRecovered",Read("EM.ModeRecovered")+1);}
}
