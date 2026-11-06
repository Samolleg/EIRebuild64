// Port of 421604cc78b4 final_golem_stages.h. GS owns all timers/RNG.
static float Range(CUnit* boss,CUnit* hero) {
 auto* s=Server(boss);return s&&Valid(hero)?s->EncounterAttackRange(hero):Body(boss)+Body(hero);
}
static CUnit* Engaged(CUnit*boss,CUnit*fallback){
 unsigned id=(unsigned)Read("FGL.TankLo")|((unsigned)Read("FGL.TankHi")<<16);auto h=id?FGX::Actor(id):nullptr;
 if(Valid(h,true)&&FGX::Now()-Read("FGL.TankAt")<=108&&std::hypot(h->GetCP().x-boss->GetCP().x,h->GetCP().y-boss->GetCP().y)<=Range(boss,h)+.3f)return h;
 auto s=Server(boss);h=s&&s->GetScriptCommand().IsAttack()?dynamic_cast<CUnit*>((CMapObject*)s->GetScriptCommand().pTarget):nullptr;
 if(Valid(h,true)&&Hero(h)&&std::hypot(h->GetCP().x-boss->GetCP().x,h->GetCP().y-boss->GetCP().y)<=Range(boss,h)+.2f)return h;
 return fallback;
}
static CUnit* RandomHero(CUnit*exclude=nullptr){
 CUnit*list[16];int count=0;for(int i=0;i<(int)Read("FGL.PartyCount")&&i<16;i++){auto h=FGX::Party(i);if(h!=exclude&&Valid(h,true)&&Hero(h))list[count++]=h;}
 return count?list[(std::min)(count-1,(int)(FGX::Random()*count))]:exclude;
}
static void Slow(CUnit*h,bool on){
 bool before=FGX::HeroVar(h,"Slow")==1;if(before==on)return;FGX::HeroVar(h,"Slow",on?1.f:0.f);auto s=Server(h);if(s)s->ScriptSpeedChanged();
}
static void ResetExtras(){
 for(int i=0;i<(int)Read("FGL.PartyCount")&&i<16;i++){auto h=FGX::Party(i);if(Valid(h,false)){Slow(h,false);FGX::HeroVar(h,"ZoneNext",0);FGX::HeroVar(h,"LaserNext",0);FGX::HeroVar(h,"FlankAt",0);}}
 for(int i=0;i<FGX::Fields;i++)FGX::Slot("Field",i,"End",0);
 for(int i=0;i<FGX::Shots;i++)FGX::Slot("Shot",i,"End",0);
 for(const char*k:{"FGL.ChaseTicks","FGL.SpecialTicks","FGL.Choice","FGL.CircleDone","FGL.TrailActive","FGL.BladeARelease","FGL.BladeBRelease","FGL.BladeAReady","FGL.BladeBReady","FGL.FieldCount","FGL.FieldUsed","FGL.ShotTotal","FGL.BeamAngle","FGL.CircleTotal","FGL.TankLo","FGL.TankHi","FGL.TankAt","FGL.LaserRingRadius"})Set(k,0);
 Set("FGL.Epoch",(float)FGX::Now());Set("FGL.Seed",(float)((FGX::Now()+12345)&0xffffff));
}
static void StartPursuit(){Set("FGL.ChaseTicks",0);Set("FGL.SpecialTicks",0);Set("FGL.Timer",0);Set("FGL.ChaseMode",0);Set("FGL.TankLo",0);Set("FGL.TankHi",0);}
static void Ray(float x,float y,float angle,float&ex,float&ey){
 float dx=std::sin(angle*.01745329252f),dy=-std::cos(angle*.01745329252f),t=200;
 if(dx>.000001f)t=(std::min)(t,(100-x)/dx);if(dx<-.000001f)t=(std::min)(t,(28-x)/dx);
 if(dy>.000001f)t=(std::min)(t,(62-y)/dy);if(dy<-.000001f)t=(std::min)(t,(26-y)/dy);
 ex=x+dx*t;ey=y+dy*t;
}
static void HeroEffects(CUnit*boss,CUnit*h){
 if(!Valid(h,true)||h==boss||!Hero(h))return;int now=FGX::Now();
 float x=h->GetCP().x,y=h->GetCP().y;bool zone=FGX::InField(x,y,now);Slow(h,zone);
 auto* hs=Server(h);float maxHP=hs?hs->GetMapObjectServer()->GetMaxHP():0;
 float next=FGX::HeroVar(h,"ZoneNext");
 if(!zone)FGX::HeroVar(h,"ZoneNext",0);
 else if(next<=0)FGX::HeroVar(h,"ZoneNext",(float)(now+18));
 else if(now>=next){DirectDamage(h,FGX::Percent(maxHP,.02f));FGX::HeroVar(h,"ZoneNext",(float)(now+18));}
 if(!Valid(h,true))return;
 bool beam=Read("FGL.Phase")==9&&Distance2(x,y,boss->GetCP().x,boss->GetCP().y,Read("FGL.BeamEX"),Read("FGL.BeamEY"))<=.5f;
 bool halo=Read("DJW.Stage")==5&&Read("FGL.Phase")==9&&Read("FGL.LaserRingRadius")>0&&std::hypot(x-boss->GetCP().x,y-boss->GetCP().y)<=Read("FGL.LaserRingRadius");
 // A hero inside both the beam and halo receives one shared laser pulse.
 if(!beam&&!halo){FGX::HeroVar(h,"LaserNext",0);return;}
 next=FGX::HeroVar(h,"LaserNext");if(next<=0||next<now-4)next=(float)now;
 for(int safety=0;safety<3&&next<=now+.0001f&&Valid(h,true);safety++){DirectDamage(h,FGX::Percent(maxHP,.10f));next+=1.8f;Set("FGL.LaserTicks",Read("FGL.LaserTicks")+1);}
 FGX::HeroVar(h,"LaserNext",next);
}
static void TickExtras(CUnit*boss){
 int now=FGX::Now(),phase=(int)Read("FGL.Phase"),stage=(int)Read("DJW.Stage");
 auto s=Server(boss);if(Read("FGL.SpeedStage")!=stage){Set("FGL.SpeedStage",(float)stage);if(s)s->ScriptSpeedChanged();}
 Set("FGL.RamDelay",stage==1?270.f:360.f);
 if(phase==1||phase==5||phase==6||phase==8||phase==9||phase==10){Set("FGL.ChaseTicks",Read("FGL.ChaseTicks")+1);Set("FGL.SpecialTicks",Read("FGL.SpecialTicks")+1);}
 for(int i=0;i<FGX::Shots;i++){float end=FGX::Slot("Shot",i,"End");if(end>0&&now>=end){
  FGX::Field(FGX::Slot("Shot",i,"EX"),FGX::Slot("Shot",i,"EY"),FGX::Slot("Shot",i,"EZ"),5,FGX::ShardLifetime,1);FGX::Slot("Shot",i,"End",0);
 }}
 float x=boss->GetCP().x,y=boss->GetCP().y,z=boss->GetCP().z;
 if(phase==3&&stage>=4){
  float radius=FGX::Clamp(Body(boss),1.f,2.2f);
  if(Read("FGL.TrailActive")!=1){Set("FGL.TrailX",x);Set("FGL.TrailY",y);Set("FGL.TrailActive",1);FGX::Field(x,y,z,radius,FGX::ShardLifetime,2);}
  float px=Read("FGL.TrailX"),py=Read("FGL.TrailY"),distance=std::hypot(x-px,y-py),spacing=radius*.75f;
  for(int i=0;distance>=spacing&&i<12;i++){
   px+=(x-px)*spacing/distance;py+=(y-py)*spacing/distance;FGX::Field(px,py,z,radius,FGX::ShardLifetime,2);distance=std::hypot(x-px,y-py);
  }Set("FGL.TrailX",px);Set("FGL.TrailY",py);
 }else Set("FGL.TrailActive",0);
 if(phase==10)Facing(boss,Read("FGL.LaserStart"),true);
 if(phase==9){
  float angle=Read("FGL.LaserStart")+Read("FGL.LaserSign")*150.f*FGX::Clamp((Read("FGL.Timer")+1)/270.f,0,1);Set("FGL.BeamAngle",angle);
  float ex,ey;Ray(x,y,angle,ex,ey);Set("FGL.BeamEX",ex);Set("FGL.BeamEY",ey);Facing(boss,angle,true);
 }
}
static void Plan(CUnit*boss,CUnit*tank){
 tank=Engaged(boss,tank);
 Set("FGL.Choice",0);if(!Valid(tank,true))return;
 auto s=Server(boss);int now=FGX::Now();float bx=boss->GetCP().x,by=boss->GetCP().y,tx=tank->GetCP().x-bx,ty=tank->GetCP().y-by,td=std::hypot(tx,ty);
 bool ready=s&&s->EncounterAttackReady();
 if(ready&&td<=Range(boss,tank)+.12f){
  for(int i=0;i<(int)Read("FGL.PartyCount")&&i<16;i++){
   auto h=FGX::Party(i);if(h==tank||!Valid(h,true)||!Hero(h))continue;
   float attack=FGX::HeroVar(h,"FlankAt"),hx=h->GetCP().x-bx,hy=h->GetCP().y-by,hd=std::hypot(hx,hy);
   if(attack>=Read("FGL.Epoch")&&attack>0&&now-attack<=108&&hd<=Range(boss,h)+1&&td*hd>.01f&&(tx*hx+ty*hy)/(td*hd)<.5f){unsigned id=unsigned(tank->GetID());Set("FGL.TankLo",(float)(id&65535));Set("FGL.TankHi",(float)(id>>16));Set("FGL.Choice",1);Set("FGL.CircleDone",0);return;}
  }
 }
 int stage=(int)Read("DJW.Stage");if(Read("FGL.SpecialTicks")<180)return;
 if(stage==5){Set("FGL.Choice",3);return;}
 if(stage>=2&&td>=5)Set("FGL.Choice",2);
}
static void Circle(CUnit* boss) {
 if(Read("FGL.CircleDone")==1)return;Set("FGL.CircleDone",1);
 auto* bs=Server(boss);const float power=bs?bs->GetRPGStats()->fpNAWDamageMin:250;
 for(int i=0;i<int(Read("FGL.PartyCount"))&&i<16;++i) {
  auto* h=Party(i);if(!Valid(h)||!Hero(h)||std::hypot(h->GetCP().x-boss->GetCP().x,h->GetCP().y-boss->GetCP().y)>Range(boss,h)+1)continue;
  auto* s=Server(h);if(!s)continue;SRPGAttackPortion p;p.Clear();
  if(Read("FGL.DamageTypesKnown")==1)for(int k=0;k<3;++k){char key[40];std::snprintf(key,sizeof(key),"FGL.DamageType%d",k);p.fpAttacks[k]=Read(key);}else p.fpAttacks[2]=1;
  p.fpToHit=FLT_MAX;p.fpDamage=power;p.hl=ERPGHitLocations(2+i%4);p.bAreaEffect=TRUE;p.bCalledShot=true;p.fBackStabMultiplier=1;
  s->ProcessAttackPortion(&p,nullptr,nullptr);
 }
 Set("FGL.ChaseTicks",0);Set("FGL.LastMelee",float(Now()));Set("FGL.CircleTotal",Read("FGL.CircleTotal")+1);Set("FGL.TankAt",float(Now()));
 for(int i=0;i<int(Read("FGL.PartyCount"))&&i<16;++i)if(auto* h=Party(i);Valid(h,false))HeroVar(h,"FlankAt",0);
 if(bs)bs->EncounterCooldown(Now()+68);
}
static void AimPoint(CUnit*boss,CUnit*h,bool map,float&x,float&y){
 float bx=boss->GetCP().x,by=boss->GetCP().y;
 for(int attempt=0;attempt<32;attempt++){
  if(map){x=29+FGX::Random()*70;y=27+FGX::Random()*34;}
  else{float a=FGX::Random()*6.283185307f,r=std::sqrt(FGX::Random())*5;x=h->GetCP().x+std::cos(a)*r;y=h->GetCP().y+std::sin(a)*r;}
  x=FGX::Clamp(x,29,99);y=FGX::Clamp(y,27,61);if(std::hypot(x-bx,y-by)>=5)return;
 }x=bx<64?95.f:33.f;y=44;
}
static void BeginThrow(CUnit*boss){
 auto first=RandomHero();if(!Valid(first,true))return;int count=Read("DJW.Stage")>=3?2:1;
 Set("FGL.ThrowCount",(float)count);Set("FGL.ThrowLaunched",0);Set("FGL.SpecialTicks",0);
 for(int i=0;i<count;i++){
  auto target=i?RandomHero(first):first;bool map=i&&target==first;float x,y;AimPoint(boss,target,map,x,y);
  FGX::Slot("Cast",i,"X",x);FGX::Slot("Cast",i,"Y",y);
 }float dx=FGX::Slot("Cast",0,"X")-boss->GetCP().x,dy=FGX::Slot("Cast",0,"Y")-boss->GetCP().y;Facing(boss,std::atan2(dx,-dy)*57.295779513f,true);
}
static void LaunchThrow(CUnit*boss){
 if(Read("DJW.Stage")>=5)return;
 if(Read("FGL.ThrowLaunched")==1)return;Set("FGL.ThrowLaunched",1);int now=FGX::Now();
 float bx=boss->GetCP().x,by=boss->GetCP().y,bz=boss->GetCP().z;float q[4];boss->GetOrientation().GetComponents(q[0],q[1],q[2],q[3]);
 for(int hand=0;hand<(int)Read("FGL.ThrowCount");hand++){
  int slot=0;for(int i=0;i<FGX::Shots;i++)if(FGX::Slot("Shot",i,"End")<=now){slot=i;break;}
  // Exact authored release pose: the projectile starts where the rear blade
  // is at the launch frame, with the same orientation, before it tumbles.
  auto origin=Rotate(q,{golemThrowOrigins[hand][0],golemThrowOrigins[hand][1],golemThrowOrigins[hand][2]});
  FGX::Slot("Shot",slot,"X",bx+origin.x);FGX::Slot("Shot",slot,"Y",by+origin.y);FGX::Slot("Shot",slot,"Z",bz+origin.z);
  const float*r=golemThrowRotations[hand];float world[]={q[0]*r[0]-q[1]*r[1]-q[2]*r[2]-q[3]*r[3],q[0]*r[1]+q[1]*r[0]+q[2]*r[3]-q[3]*r[2],q[0]*r[2]-q[1]*r[3]+q[2]*r[0]+q[3]*r[1],q[0]*r[3]+q[1]*r[2]-q[2]*r[1]+q[3]*r[0]};
  for(int k=0;k<4;k++){char field[8];sprintf_s(field,"Q%d",k);FGX::Slot("Shot",slot,field,world[k]);}
  FGX::Slot("Shot",slot,"EX",FGX::Slot("Cast",hand,"X"));FGX::Slot("Shot",slot,"EY",FGX::Slot("Cast",hand,"Y"));FGX::Slot("Shot",slot,"EZ",bz);
  FGX::Slot("Shot",slot,"Born",(float)now);FGX::Slot("Shot",slot,"End",(float)(now+45));FGX::Slot("Shot",slot,"Hand",(float)hand);
  FGX::Slot("Shot",slot,"Stage",Read("DJW.Stage"));
  Set(hand?"FGL.BladeBRelease":"FGL.BladeARelease",(float)now);Set(hand?"FGL.BladeBReady":"FGL.BladeAReady",(float)(now+72));Set("FGL.ShotTotal",Read("FGL.ShotTotal")+1);
 }
}
static CUnit* LaserTarget(){unsigned id=(unsigned)Read("FGL.LaserLo")|((unsigned)Read("FGL.LaserHi")<<16);return FGX::Actor(id);}
static void BeginLaser(CUnit*boss){
 auto h=RandomHero();if(!Valid(h,true))return;unsigned id=unsigned(h->GetID());Set("FGL.LaserLo",(float)(id&65535));Set("FGL.LaserHi",(float)(id>>16));
 Set("FGL.LaserMotion",0);Set("FGL.LaserPrevX",h->GetCP().x);Set("FGL.LaserPrevY",h->GetCP().y);Set("FGL.SpecialTicks",0);
 float radius=Body(boss)+1;
 for(int i=0;i<(int)Read("FGL.PartyCount")&&i<16;i++){auto u=FGX::Party(i);if(Valid(u,true))radius=(std::max)(radius,Range(boss,u)+1);}
 Set("FGL.LaserRingRadius",radius);
}
static void AimLaser(CUnit*boss){
 auto h=LaserTarget();if(!Valid(h,true)){BeginLaser(boss);h=LaserTarget();}if(!Valid(h,true))return;
 float bx=boss->GetCP().x,by=boss->GetCP().y,dx=h->GetCP().x-bx,dy=h->GetCP().y-by,px=Read("FGL.LaserPrevX")-bx,py=Read("FGL.LaserPrevY")-by;
 if(Read("FGL.Timer")>18){float turn=std::atan2(px*dy-py*dx,px*dx+py*dy);Set("FGL.LaserMotion",Read("FGL.LaserMotion")+turn);Facing(boss,std::atan2(dx,-dy)*57.295779513f,true);}
 Set("FGL.LaserPrevX",dx+bx);Set("FGL.LaserPrevY",dy+by);Set("FGL.LaserAimX",dx+bx);Set("FGL.LaserAimY",dy+by);
}
static void BeginFire(CUnit*boss){
 // Called once at the end of aim. Freeze the last sampled point and sweep
 // direction before the nine-tick escape window. Firing must never re-read
 // the target's position, even if it moves or dies during that window.
 float dx=Read("FGL.LaserAimX")-boss->GetCP().x,dy=Read("FGL.LaserAimY")-boss->GetCP().y;Set("FGL.LaserStart",std::atan2(dx,-dy)*57.295779513f);
 Facing(boss,Read("FGL.LaserStart"),true);Set("FGL.BeamAngle",Read("FGL.LaserStart"));
 float motion=Read("FGL.LaserMotion");Set("FGL.LaserSign",std::abs(motion)>.001f?(motion>0?1.f:-1.f):(FGX::Random()<.5f?-1.f:1.f));Set("FGL.LaserTicks",0);
 for(int i=0;i<(int)Read("FGL.PartyCount")&&i<16;i++){auto u=FGX::Party(i);if(Valid(u,false))FGX::HeroVar(u,"LaserNext",0);}
}
