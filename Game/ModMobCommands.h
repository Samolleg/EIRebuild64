#pragma once
#include "Script.h"
#include "Unit.h"
#include "UnitServer.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include "ModMobFlagArguments.h"
#include "ModMessages.h"
#include "ModAnimationLoop.h"
#include "ModAttackEvents.h"
#include "ModInvincible.h"
#include "MapObjectManager.h"
#include "ModRegisteredLoot.h"
#include "ZoneIce.h"
#include <cmath>
#include <stdexcept>
#include "WorldServer.h"
#include "WorldClient.h"
#include "WorldInterface.h"
#include "PlayerServer.h"
#include "Player.h"
#include "MusicSystem.h"
#include "RandomGen.h"
#include "Console.h"
#include "Item.h"
#include "aiSettings.h"
namespace ModMob {
// Release registry from57b8e0c, original IDs checked against EXE token table78BCA0.
struct Entry {int id;const char*name;const char*format;bool ready;};
inline const Entry registry[]={
 {291,"SetInvincible","osf",true},
 {292,"ApplyDirectDamage","of",true},
 {275,"GetCloseAttackEvent","Foof",true},
 {285,"GetPhysicalAttackEvent","Foof",true},
 {294,"GetAttackEvent","Foof",true},
 {295,"WasAttacked","Foof",true},
 {296,"HasAttack","Foof",true},
 {280,"GetArenaTick","F",true},
 {288,"DirectionAngle","Fff",true},
 {297,"ApplyTrapParalysis","of",true},
 {298,"SetWeather","sf",true},
 {300,"GetPlayerOfUnit","Fo",true},
 {301,"GetUnitExp","Fo?s",true},
 {302,"GetArmor","Foff",true},
 {303,"GetObjectStringID","So",true},
 {304,"FloatToStr","Sf?f",true},
 {305,"StrToFloat","Fs",true},
 {306,"StrCat","Sss+",true},
 {307,"StrLen","Fs",true},
 {308,"Sin","Ff",true},
 {309,"Cos","Ff",true},
 {310,"Pow","Fff",true},
 {311,"DistancePointPoint","Fffff",true},
 {312,"NearestUnit","Offff",true},
 {313,"UnitsNearPoint","Gffff",true},
 {314,"GSSetPlayerVar","fsf",true},
 {315,"GSGetPlayerVar","Ffs",true},
 {316,"GiveExp","ff",true},
 {317,"GiveUnitExp","of",true},
 {318,"StartCameraSave","s",true},
 {319,"StopCameraSave","",true},
 {320,"Say","s+",true},
 {126,"ConsoleString","s+",true},
 {73,"Add","Fff+",true},
 {75,"Mul","Fff+",true},
 {324,"And","Fff+",true},
 {325,"Or","Fff+",true},
 {326,"Max","Fff+",true},
 {327,"Min","Fff+",true},
 {328,"Abs","Ff",true},
 {329,"Round","Ff",true},
 {330,"Ceil","Ff",true},
 {331,"Floor","Ff",true},
 {95,"Random","Ff?f",true},
 {333,"RandStr","Sss+",true},
 {334,"RandObj","Og",true},
 {335,"StrHas","Fss",true},
 {336,"StrMid","Ssff",true},
 {337,"StrLower","Ss",true},
 {338,"StrUpper","Ss",true},
 {232,"ResetTarget","o",true},
 {340,"SendCurrentParty","f",true},
 {341,"PlayerExists","Ff",true},
 {342,"Loot","fo",true},
 {343,"GameOver","",true},
 {344,"GameOverMSG","ss",true},
 {345,"GetDexterity","Fo",true},
 {346,"GetIntelligence","Fo",true},
 {347,"GetStrength","Fo",true},
 {348,"GetSkill","Fos",true},
 {349,"GetActions","Fo",true},
 {350,"GetActionsBase","Fo",true},
 {351,"GetEncumbrance","Fo",true},
 {352,"GetMaxEncumbrance","Fo",true},
 {353,"GetPerk","Fos",true},
 {354,"GetPrototype","So",true},
 {355,"GetRace","So",true},
 {356,"IsFileExists","Fs",true},
 {357,"GSListPlayerVars","f",true},
 {358,"IsMLooted","Fo",true},
 {359,"AckPotion","Fo",true},
 {360,"AddQuick2Unit","os",true},
 {361,"GetUnitPose","Fo",true},
 {362,"CheckObjectFlag","Ffo",true},
 {363,"DamageMainUnitWeapon","Fof",true},
 {364,"CastSomeSpell","offf",true},
 {365,"CastSomeSpellTarget","oof",true},
 {366,"GetUnitState","Fo",true},
 {367,"CheckObjectFlag2","Ffo",true},
 {368,"IsUnitFFeebled","Fo",true},
 {369,"IsUnitFStun","Fo",true},
 {370,"IsUnitFDontmove","Fo",true},
 {371,"LTZ","s",true},
 {372,"InflictDamageEx","offff",true},
 {373,"MusicNPT","F",true},
 {374,"MusicFM","S",true},
 {375,"MusicBFM","F",true},
 {376,"CmdSkill","ooffff",true},
 {377,"GetUnitWeapon","Sof",true},
 {378,"GetUnitArmor","Sof",true},
 {379,"GetSkill2","Fof",true},
 {380,"GetSkill3","Fof",true},
 {381,"GetPerk2","Fof",true},
 {382,"SetUnitAggression","of",true},
 {383,"GetUnitAggression","Fo",true},
 {384,"NpcCombatConsumable","ssof",true},
 {385,"RefreshUnitStats","o",true},
 {386,"JMVRegisterLoot","Fos",true},
 {387,"CreateVoiceFX","osf?ff",true},
 {388,"RegisterLoot","os",true},
 {389,"ShowMessage","s",true},
 {390,"CreateVoiceFXWait","osf?ff",true},
 {391,"JmvBranchUnless","ff",true},
 {392,"JmvBranchSkip","f",true},
 {393,"PartyDisguiseMatches","Fs",true},
 {394,"PlayAnimationLoop","os",true},
 {395,"StopAnimationLoop","o",true},
 {396,"ZoneIceSync","",true},
 {397,"ZoneIceFreeze","",true},
};
inline const Entry* Find(int id){for(const auto&e:registry)if(e.id==id)return &e;return nullptr;}
// Native added command 372 owns the direct custom consumer, not recipient virtual dispatch.
template<class Server> inline void InflictDamageExConsumer(Server* server,SRPGAttackPortion& portion){
 server->ProcessCustomAttackPortion(&portion,nullptr,nullptr);
}
struct ArgumentError:std::runtime_error{using std::runtime_error::runtime_error;};
inline bool Reject(const char*command,const char*reason){NConsole::game<<"MOB "<<command<<": "<<reason<<std::endl;return true;}
inline std::string Expand(const char*format,const SSCCmd&cmd){
 int counts[]={cmd.params.GetSize(),cmd.szParams.GetSize(),cmd.oParams.GetSize(),cmd.gParams.GetSize()};
 std::string result;char previous=0;
 auto total=[&](){return counts[0]+counts[1]+counts[2]+counts[3];};
 auto take=[&](char type){const char*types="fsog";auto*p=strchr(types,type);if(!p||counts[p-types]<=0)throw ArgumentError("MOB argument type/count mismatch");--counts[p-types];result+=type;previous=type;};
 for(const char*p=format;*p;++p){
  if(*p=='?'){if(!total())break;continue;}
  if(*p=='+'){if(!previous||p[1])throw std::runtime_error("Invalid variadic MOB format");while(total())take(previous);}
  else take(*p);
 }
 if(total())throw ArgumentError("Excess MOB arguments");return result;
}
inline void Validate(const Entry&e,const SSCCmd&cmd){const char*p=e.format;if(*p&&strchr("FSOG",*p))++p;Expand(p,cmd);}
inline bool ValidateDispatch(const Entry&e,const SSCCmd&cmd){
 switch(cmd.command){case 302:case 311:case 365:case 372:case 384:case 388:case 389:case 391:case 392:case 394:case 395:
  try{Validate(e,cmd);return true;}catch(const ArgumentError&error){Reject(e.name,error.what());return false;}
 default:Validate(e,cmd);return true;}
}
inline bool IntegralArgument(float value,double lower,double upper){return std::isfinite(value)&&value==floorf(value)&&double(value)>=lower&&double(value)<upper;}
inline int CheckedInt(float value){
 if(!std::isfinite(value)||value<-2147483648.f||value>=2147483648.f)throw std::runtime_error("Invalid MOB integer argument");return int(value);
}
inline int Index(float value,int count){int i=CheckedInt(value);if(i<0||i>=count)throw std::runtime_error("MOB index out of range");return i;}
inline CMapObject* ObjectArg(const SSCCmd&cmd,int i=0){return i<cmd.oParams.GetSize()&&cmd.oParams[i]?cmd.oParams[i]->GetObject():nullptr;}
inline CUnit* UnitArg(const SSCCmd&cmd,int i=0){auto*p=ObjectArg(cmd,i);return p&&p->WhoAmI()==HPC_UNIT?static_cast<CUnit*>(p):nullptr;}
inline CUnitServer* ServerArg(const SSCCmd&cmd){auto*u=UnitArg(cmd);return u?dynamic_cast<CUnitServer*>(u->GetUnitSpecific()):nullptr;}
inline CPlayerServer* PlayerServer(int number){auto*p=theWorldServer.GetPlayer(number);return p?dynamic_cast<CPlayerServer*>(p->GetPlayerSpecific()):nullptr;}
inline std::string Join(const SSCCmd&cmd){std::string s;for(int i=0;i<cmd.szParams.GetSize();i++)s+=(LPCSTR)cmd.szParams[i];return s;}
inline bool Float(SSCCmd&cmd,float&result){
 if(!ModRuntime::Current())return false;auto*e=Find(cmd.command);if(!e)return false;
 if(!e->ready||e->format[0]!='F')throw std::runtime_error(std::string("Unported MOB float command: ")+e->name);if(!ValidateDispatch(*e,cmd)){result=0;return true;}
 const char*n=e->name;float a=cmd.params.GetSize()?cmd.params[0]:0;
 if(cmd.command==275||cmd.command==285||cmd.command==294||cmd.command==295||cmd.command==296){result=ModAttackEvents::Query(theWorldServer,cmd.command,UnitArg(cmd),UnitArg(cmd,1),a);return true;}
 if(cmd.command==280){result=float(theWorldServer.GetSegmentTimeCounter());return true;}
 if(cmd.command==288){float y=cmd.params[1];result=std::isfinite(a)&&std::isfinite(y)&&std::hypot(a,y)>.001f?std::atan2(a,-y)*57.2957795130823f:0.f;return true;}
 if(cmd.command==361){auto*unit=UnitArg(cmd);auto*server=ServerArg(cmd);result=unit&&!unit->IsInvalid()&&!unit->IsDead()&&server?float(server->GetScriptPose()):-1.f;return true;}
 if(cmd.command==95){if(cmd.params.GetSize()==1)return false;result=a<cmd.params[1]?random.GetFloat(a,cmd.params[1]):a;return true;}
 if(cmd.command==311){float dx=cmd.params[2]-a,dy=cmd.params[3]-cmd.params[1];result=sqrtf(dx*dx+dy*dy);return true;}
 if(cmd.command==341){result=theWorldServer.GetPlayer(CheckedInt(a))?1.f:0.f;return true;}
 if(cmd.command==315){auto*p=theWorldServer.GetPlayer(CheckedInt(a));result=p?p->party.vars.GetVar(cmd.szParams[0]):0;return true;}
 if(cmd.command==373||cmd.command==375){auto*m=theWorld.GetMusicSystem();result=m?float(cmd.command==373?m->GetScriptNextPlayTime():m->GetScriptForced()):0;return true;}
 if(cmd.command==356){std::string name=cmd.szParams[0];std::replace(name.begin(),name.end(),'\\','/');if(name==".."||name.find("/../")!=std::string::npos||name.rfind("../",0)==0||(name.size()>=3&&name.substr(name.size()-3)=="/.."))throw std::runtime_error("MOB parent path forbidden");std::string path=ModRuntime::Current()->Directory()+"/"+name;auto attrs=GetFileAttributesA(path.c_str());result=attrs!=INVALID_FILE_ATTRIBUTES&&!(attrs&FILE_ATTRIBUTE_DIRECTORY)?1.f:0.f;return true;}
 if(cmd.command==358||cmd.command==368||cmd.command==369||cmd.command==370||cmd.command==362||cmd.command==367){auto*object=ObjectArg(cmd);DWORD flag=cmd.command==358?UNIT_ALREADY_MHOOVER:cmd.command==368?UNIT_FEEBLED:cmd.command==369?UNIT_STUN:UNIT_DONTMOVE;if(cmd.command==362||cmd.command==367){std::uint32_t decoded;if(!(cmd.command==362?ModMobFlagArguments::Mask(a,decoded):ModMobFlagArguments::Bit(a,decoded))){result=0;return true;}flag=decoded;}result=object&&object->CheckFlag(flag)?1.f:0.f;return true;}
 if(cmd.command==393){result=ModRuntime::Current()->PartyDisguiseMatches(cmd.szParams[0])?1.f:0.f;return true;}
 if(cmd.command==386){result=JMV_RegisterLoot(ObjectArg(cmd),cmd.szParams[0])?1.f:0.f;return true;}
 if(cmd.command==359){auto*s=ServerArg(cmd);auto*u=UnitArg(cmd);if(s&&u){s->InformFailure(ACK_HERO_ACK_USE_POTION);theWorld.ReceiveDamage(u->GetID(),0,HLDAMAGE_POTION);}result=0;return true;}
 if(cmd.command==300){auto*u=UnitArg(cmd);result=u&&(u->GetID()>=1000000000)&&u->GetOwner()?float(u->GetOwner()->GetPlayerNumber()):-1.f;return true;}
 if(cmd.command==363){auto*u=UnitArg(cmd);int damage=CheckedInt(a);result=float(damage);auto*item=u&&ServerArg(cmd)?u->GetQWeapon(0):nullptr;if(item&&damage>0&&damage<1000){float old=item->GetHP(),critical=item->GetMaxHP()*theAIGeneralSettings.fItemDurabilityCritical;item->ApplyDamage(float(damage));if(item->IsBroken()||(old>=critical&&item->GetHP()<critical))ServerArg(cmd)->InformFailure(0);}return true;}
 if(!strcmp(n,"Add")||!strcmp(n,"Mul")||!strcmp(n,"And")||!strcmp(n,"Or")||!strcmp(n,"Max")||!strcmp(n,"Min")){
  result=(!strcmp(n,"Mul")||!strcmp(n,"And"))?1:(!strcmp(n,"Max")||!strcmp(n,"Min"))?a:0;
  for(int i=(!strcmp(n,"Max")||!strcmp(n,"Min"))?1:0;i<cmd.params.GetSize();++i){float v=cmd.params[i];
   if(!strcmp(n,"Add"))result+=v;else if(!strcmp(n,"Mul"))result*=v;
   else if(!strcmp(n,"And")){if(int(v)==0){result=0;break;}}
   else if(!strcmp(n,"Or")){if(int(v)!=0){result=1;break;}}
   else if(!strcmp(n,"Max"))result=result>v?result:v;else result=result<v?result:v;
  }
 }
 else if(!strcmp(n,"StrHas"))result=strstr(cmd.szParams[0],cmd.szParams[1])?1.f:0.f;
 else if(!strcmp(n,"Sin"))result=sin(a);
 else if(!strcmp(n,"Cos"))result=cos(a);
 else if(!strcmp(n,"Pow"))result=pow(a,cmd.params[1]);
 else if(!strcmp(n,"Abs"))result=a<0?-a:a;
 else if(!strcmp(n,"Round"))result=a<0?ceilf(a-.5f):floorf(a+.5f);
 else if(!strcmp(n,"Ceil"))result=ceilf(a);
 else if(!strcmp(n,"Floor"))result=floorf(a);
 else if(!strcmp(n,"StrToFloat"))result=float(atof(cmd.szParams[0]));
 else if(!strcmp(n,"StrLen"))result=float(cmd.szParams[0].GetLength());
 else {
  result=0;auto*object=cmd.oParams[0]?cmd.oParams[0]->GetObject():nullptr;
  if(!object||object->WhoAmI()!=HPC_UNIT)return true;auto*unit=static_cast<CUnit*>(object);
  if(!strcmp(n,"GetUnitAggression")){result=float(unit->GetAggression());return true;}
  auto*server=dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());if(!server)return true;auto&s=*server->GetRPGStats();
  if(cmd.command==366){result=float(server->logic.GetScriptState());return true;}
  if(cmd.command==302){if(!std::isfinite(a)||a<-2147483648.f||a>=2147483648.f||!std::isfinite(cmd.params[1])||cmd.params[1]<-2147483648.f||cmd.params[1]>=2147483648.f)return Reject(n,"Invalid armor argument");int part=int(a),type=int(cmd.params[1]);if(part>=RPG_NUM_HIT_LOCATIONS||type<0||type>=RPG_NUM_ATTACK_TYPES)return Reject(n,"Armor index out of range");if(part>=0){for(auto&layer:s.hls[part].layers)result+=layer.fpAbsorbtions[type];}else{float values[RPG_NUM_ATTACK_TYPES];unit->GetUserUnitStats()->GetAbsorbtions(&unit->GetUserCommonStats(),values,(unit->GetID()>=1000000000));result=values[type];}return true;}
  if(!strcmp(n,"GetUnitExp")){
   const char*type=cmd.szParams.GetSize()?(LPCSTR)cmd.szParams[0]:"max";
   if(!_stricmp(type,"max"))result=s.fpExperience;else if(!_stricmp(type,"spent"))result=s.fSpentExp;else if(!_stricmp(type,"penalty"))result=s.fExpPenalty;else if(!_stricmp(type,"real"))result=s.fpExperience+s.fExpPenalty;
  }
  else if(!strcmp(n,"GetSkill2"))result=s.cManualSkills[Index(a,RPG_NUM_MANUAL_SKILLS)];
  else if(!strcmp(n,"GetSkill3"))result=s.cAutoSkills[Index(a,RPG_NUM_AUTO_SKILLS)];
  else if(!strcmp(n,"GetPerk2"))result=s.cPerks[Index(a,RPG_NUM_PERKS)];
  else if(!strcmp(n,"GetSkill")){
   const char*skill=cmd.szParams[0];
   if(!_stricmp(skill,"science"))result=s.cManualSkills[RPGMSKL_SCIENCE];
   else if(!_stricmp(skill,"tame"))result=s.cManualSkills[RPGMSKL_TAME];
   else {const char*names[]={"melee","archery","elemental","sense","astral"};const int indices[]={RPGASKL_MELEE,RPGASKL_ARCHERY,RPGASKL_ELEMENTAL,RPGASKL_SENSE,RPGASKL_ASTRAL};for(int i=0;i<5;i++)if(!_stricmp(skill,names[i]))result=s.cAutoSkills[indices[i]];}
  }
  else if(!strcmp(n,"GetPerk")){
   const char*names[]={"sword","axe","dagger","spear","hammer","bow","crossbow","fire","lightning","acid","illusion","divination","enchantments","healing","domination","infra","health","mana","vitality","spirit","quickness","lift","backstab","strength","dexterity","intelligence"};
   for(int i=0;i<26;i++)if(!_stricmp(cmd.szParams[0],names[i]))result=s.cPerks[i];
  }
  else if(!strcmp(n,"GetDexterity"))result=s.psDexterity.fpEffectiveValue;
  else if(!strcmp(n,"GetIntelligence"))result=s.psIntelligence.fpEffectiveValue;
  else if(!strcmp(n,"GetStrength"))result=s.psStrength.fpEffectiveValue;
  else if(!strcmp(n,"GetActions"))result=s.action.fpEffectiveValue;
  else if(!strcmp(n,"GetActionsBase"))result=s.action.fpBaseValue;
  else if(!strcmp(n,"GetEncumbrance"))result=s.fpCurrEncumbrance;
  else if(!strcmp(n,"GetMaxEncumbrance"))result=s.fpMaxEncumbrance;
 }
 return true;
}
inline bool String(SSCCmd&cmd,CString&result){
 if(!ModRuntime::Current())return false;auto*e=Find(cmd.command);if(!e)return false;
 if(!e->ready||e->format[0]!='S')throw std::runtime_error(std::string("Unported MOB string command: ")+e->name);Validate(*e,cmd);
 if(cmd.command==303){auto*o=ObjectArg(cmd);if(o)result=std::to_string(o->GetID()).c_str();return true;}
 if(cmd.command==333){result=cmd.szParams[random.Get(cmd.szParams.GetSize()-1)];return true;}
 if(cmd.command==374){auto*m=theWorld.GetMusicSystem();if(m)result=m->GetScriptMusic();return true;}
 if(cmd.command==377||cmd.command==378)result="";
 if(!strcmp(e->name,"StrCat")){std::string value;for(int i=0;i<cmd.szParams.GetSize();i++)value.append((LPCSTR)cmd.szParams[i],cmd.szParams[i].GetLength());result=value.c_str();}
 else if(!strcmp(e->name,"StrMid")){
  int pos=CheckedInt(cmd.params[0]),len=CheckedInt(cmd.params[1]),size=cmd.szParams[0].GetLength();
  if(pos<0)pos=size+pos+1;if(len==0)len=size;else if(len<0)return true;
  if(!size){result="";return true;}pos=(std::max)(1,(std::min)(size,pos));len=(std::min)(len,size-pos+1);
  result=std::string((LPCSTR)cmd.szParams[0]+pos-1,size_t(len)).c_str();
 }
 else if(!strcmp(e->name,"StrLower")||!strcmp(e->name,"StrUpper")){
  std::vector<char> buf(cmd.szParams[0].GetLength()+1);memcpy(buf.data(),(LPCSTR)cmd.szParams[0],buf.size());
  if(!strcmp(e->name,"StrLower"))CharLowerA(buf.data());else CharUpperA(buf.data());result=buf.data();
 }
 else if(cmd.oParams.GetSize()){
  auto*object=cmd.oParams[0]?cmd.oParams[0]->GetObject():nullptr;if(!object||object->WhoAmI()!=HPC_UNIT)return true;
  auto*unit=static_cast<CUnit*>(object);if(!dynamic_cast<CUnitServer*>(unit->GetUnitSpecific()))return true;
  if(!strcmp(e->name,"GetPrototype"))result=unit->GetPrototype()->szName;
  else if(!strcmp(e->name,"GetRace"))result=unit->GetPrototype()->szRace;
  else if(!strcmp(e->name,"GetUnitWeapon")){int slot=CheckedInt(cmd.params[0]);auto*item=slot<0?nullptr:unit->GetQWeapon(slot);if(item)result=item->GetShapeEntry()->szName;}
  else if(!strcmp(e->name,"GetUnitArmor")){int slot=Index(cmd.params[0],RPG_NUM_ARMOR_TYPES);auto*item=unit->GetQArmor(slot);if(item)result=item->GetShapeEntry()->szName;}
 }
 else if(!strcmp(e->name,"FloatToStr")){
  int precision=cmd.params.GetSize()>=2?CheckedInt(cmd.params[1]):3;precision=(std::max)(0,(std::min)(8,precision));
  char buf[100];int len=sprintf_s(buf,"%.*f",precision,cmd.params[0]);
  if(len<0)throw std::runtime_error("FloatToStr formatting failed");
  // Precision is the number of fractional digits, including under a non-C locale.
  if(precision>0 && len>precision){
   int decimal=len-precision-1;
   if(buf[decimal]=='.'||buf[decimal]==','){
    while(len>decimal+1&&buf[len-1]=='0')--len;
    if(len==decimal+1)--len;buf[len]=0;
   }
  }
  result=buf;
 }
 return true;
}
inline bool Object(SSCCmd&cmd,CScriptObject&result){
 if(!ModRuntime::Current())return false;auto*e=Find(cmd.command);if(!e)return false;if(!e->ready||e->format[0]!='O')throw std::runtime_error("Unported MOB object command");Validate(*e,cmd);
 if(cmd.command==334){auto*g=cmd.gParams[0];if(g&&g->GetSize())result.SetObject(g->GetAt(random.Get(g->GetSize()-1)).GetObject());return true;}
 CUnitList list;CPlot3D pos(cmd.params[0],cmd.params[1],0);float distance=cmd.params[2];int flags=CheckedInt(cmd.params[3]);CUnit*nearest=nullptr;
 theWorldServer.aiMap.GetUnitsNear(pos,list,distance);POSITION it=list.GetHeadPosition();while(it){auto*u=list.GetNext(it);if(!(flags&1))continue;auto p=u->GetCP();float dx=p.x-pos.x,dy=p.y-pos.y,d=sqrtf(dx*dx+dy*dy);if(d<distance){distance=d;nearest=u;}}result.SetObject(nearest);return true;
}
inline bool Group(SSCCmd&cmd,CScriptObjectGroup&result){
 if(!ModRuntime::Current())return false;auto*e=Find(cmd.command);if(!e)return false;if(!e->ready||e->format[0]!='G')throw std::runtime_error("Unported MOB group command");Validate(*e,cmd);
 result.Clear();CUnitList list;theWorldServer.aiMap.GetUnitsNear(CPlot3D(cmd.params[0],cmd.params[1],0),list,cmd.params[2]);int flags=CheckedInt(cmd.params[3]);POSITION it=list.GetHeadPosition();while(it){auto*u=list.GetNext(it);if(flags&1)result.Add(u);}return true;
}
inline bool Action(SSCCmd&cmd){
 auto*unit=UnitArg(cmd);auto*server=ServerArg(cmd);float a=cmd.params.GetSize()?cmd.params[0]:0;
 switch(cmd.command){
 case 291:if(!ModInvincible::Change(unit,cmd.szParams[0],a))return Reject("SetInvincible","Invalid unit, channel or flag");break;
 case 292:if(server)server->ApplyDirectDamage(a);break;
 case 298:{
  if(!std::isfinite(a)||a<0||a>3600||!theWorldServer.IsMapLoaded())break;
  const char*name=cmd.szParams[0];EWeather weather;
  if(!_stricmp(name,"clear"))weather=EW_CLEAR;else if(!_stricmp(name,"rain"))weather=EW_RAIN;else if(!_stricmp(name,"snow"))weather=EW_SNOW;else break;
  theWorldServer.SetScriptWeather(weather,std::round(a*18.f));break;
 }
 case 297:{
  if(!unit||!server||unit->IsInvalid()||unit->IsDead()||unit->CheckFlag(UNIT_STUN)||!std::isfinite(a)||a<=0||a>60)break;
  CSpellEffect effect;effect.nSpellID=SPELL_ID_STUN;effect.nDuration=long(std::round(a*18.f));effect.fpPower=1;effect.bQuiet=true;
  unit->AddSpellEffect(effect);unit->UpdateFrame();break;
 }
 case 396:ZoneIce::Sync();break;
 case 397:ZoneIce::Freeze();break;
 case 314:{if(auto*p=PlayerServer(CheckedInt(a)))p->SetVar(cmd.szParams[0],cmd.params[1]);break;}
 case 316:{auto*p=theWorldServer.GetPlayer(CheckedInt(a));if(p)theWorldServer.GainExperience(p,cmd.params[1]);break;}
 case 317:if(server){server->GainExperience(a);if(auto*p=unit->GetOwner())if(auto*ps=dynamic_cast<CPlayerServer*>(p->GetPlayerSpecific()))ps->GainExp(unit->GetID(),a);}break;
 case 318:IWorldClient.StartCameraSave(cmd.szParams[0]);break;
 case 319:IWorldClient.StopCameraSave();break;
 case 126:NConsole::game<<Join(cmd).c_str()<<std::endl;break;
 case 232:if(server)server->logic.ResetScriptTarget();break;
 case 342:{auto*p=theWorldServer.GetPlayer(CheckedInt(a));if(p&&unit&&unit->IsDead()){theWorldServer.HooverUnit(unit,p);theWorldServer.RemoveMapObject(unit->GetID());}break;}
 case 343:if(auto*p=PlayerServer(0))p->SendStringEvent(0,"gameover");break;
 case 344:if(auto*p=PlayerServer(0))p->SendStringEvent(0,(std::string("gameover ")+(LPCSTR)cmd.szParams[0]+" "+(LPCSTR)cmd.szParams[1]).c_str());break;
 case 357:{auto*p=theWorldServer.GetPlayer(CheckedInt(a));if(p)for(const auto&v:p->party.vars)NConsole::game<<v.first.c_str()<<" = "<<v.second<<std::endl;break;}
 case 360:if(server){auto*item=theWorldServer.CreateItem(cmd.szParams[0]);if(auto*quick=dynamic_cast<CItemQuick*>(item)){unit->AddQItem(quick);server->NotifyItemsUpdated();}else if(item)item->Release();}break;
 case 364:if(server)server->logic.StartCast(CheckedInt(cmd.params[2]),CPlot3D(a,cmd.params[1],theWorldServer.GetZValue(a,cmd.params[1])),false);break;
 case 365:if(!server||!ObjectArg(cmd,1)||!IntegralArgument(a,0.0,2147483648.0))return Reject("CastSomeSpellTarget","Invalid server, target or spell");server->logic.StartCast(int(a),ObjectArg(cmd,1),false);break;
 case 371:if(auto*p=PlayerServer(0))p->LeaveToZone(cmd.szParams[0],0);break;
 case 372:if(!server||!IntegralArgument(cmd.params[1],0.0,RPG_NUM_ATTACK_TYPES)||!IntegralArgument(cmd.params[2],0.0,RPGHL_ALL+1.0))return Reject("InflictDamageEx","Invalid server, attack type or hit location");if(server){SRPGAttackPortion portion;portion.Clear();portion.nSkill=0;portion.fpToHit=100;portion.fpAttacks[int(cmd.params[1])]=1;portion.hl=ERPGHitLocations(int(cmd.params[2]));portion.fBackStabMultiplier=cmd.params[3];portion.fpDamage=a;InflictDamageExConsumer(server,portion);}break;
 case 376:if(server)server->CmdSkill(ObjectArg(cmd,1),CheckedInt(cmd.params[1]),DWORD(CheckedInt(cmd.params[2])),a!=0,DWORD(CheckedInt(cmd.params[3])));break;
 case 388:if(!JMV_RegisterLoot(ObjectArg(cmd),cmd.szParams[0]))return Reject("RegisterLoot","Registration rejected");break;
 case 389:if(const char*error=ModMessages::Show(cmd.szParams[0]))return Reject("ShowMessage",error);break;
 case 384:if(!std::isfinite(a)||a<-2147483648.f||a>=2147483648.f)return Reject("NpcCombatConsumable","Invalid charges");if(server&&!JMV_RegisterNpcCombatConsumable(unit,cmd.szParams[0],cmd.szParams[1],int(a)))return Reject("NpcCombatConsumable","Profile rejected");break;
 case 382:if(unit)unit->SetAggression(EUnitAggression(CheckedInt(a)));break;
 default:return false;
 }return true;
}
inline bool BranchForward(int&index,int count,bool skip,float amount){
 if(!std::isfinite(amount)||amount<0||amount>16777215.f||amount!=floorf(amount)||index<0||index>=count||int(amount)>count-index-1)return false;
 if(skip)index+=int(amount);return true;
}
}
#include "ModVoiceCommands.h"
namespace ModMob {
inline bool Void(SSCCmd&cmd,int*index=nullptr,int count=0,int*sleep=nullptr,BOOL*next=nullptr,bool canDefer=true){
 auto*mod=ModRuntime::Current();if(!mod)return false;auto*e=Find(cmd.command);if(!e)return false;
 if(cmd.command==387||cmd.command==390){Validate(*e,cmd);return Voice(cmd,sleep,next);}
 if(cmd.command==394||cmd.command==395){
  if(!ValidateDispatch(*e,cmd))return true;
  auto defer=[&](){if(next&&canDefer){*next=FALSE;return true;}return Reject(e->name,"Unit is not ready; defer outside synchronous loops");};
  auto* unit=UnitArg(cmd);
  if(!unit){if(cmd.command==394&&!ObjectArg(cmd))return defer();return Reject(e->name,"Invalid unit");}
  if(cmd.command==394&&unit->IsDead())return Reject(e->name,"Dead unit");
  std::string key;auto* vars=ModAnimationLoop::Variables(unit,key);
  if(!vars)return defer();
  auto* serverUnit=theWorldServer.GetUnit(unit->GetID());
  auto* server=serverUnit?dynamic_cast<CUnitServer*>(serverUnit->GetUnitSpecific()):nullptr;
  if(cmd.command==395){if(server)server->StopAnimationLoop();else vars->DelVar(key.c_str());return true;}
  const char* name=cmd.szParams[0].GetString();
  if(!name||!*name)return Reject(e->name,"Empty animation name");
  auto* manager=static_cast<CMapObjectManager*>(theWorldServer.GetMapObjectManager());if(!manager)return defer();
  const auto* entry=manager->GetAnimationEntry(unit->GetMaskName(),name);
  if(!entry||entry->dwEntryIndex>=16777215u)return Reject(e->name,"Invalid animation");
  vars->SetVar(key.c_str(),float(entry->dwEntryIndex+1));
  if(server&&!serverUnit->IsInvalid())server->QueueAnimationLoop(entry->dwEntryIndex);
  return true;
 }
 if(e->ready&&(cmd.command==391||cmd.command==392)){
  if(!ValidateDispatch(*e,cmd))return true;bool skip=cmd.command==392||cmd.params[0]==0;float amount=cmd.params[cmd.command==391?1:0];
  if(!index||!BranchForward(*index,count,skip,amount))return Reject(e->name,"Invalid branch state/offset");return true;
 }
 if(e->ready && cmd.command!=385){if(!ValidateDispatch(*e,cmd))return true;if(Action(cmd))return true;}
 if(!e->ready||strcmp(e->name,"RefreshUnitStats"))throw std::runtime_error(std::string("Unported MOB action: ")+e->name);Validate(*e,cmd);
 auto*object=cmd.oParams[0]?cmd.oParams[0]->GetObject():nullptr;
 if(object&&object->WhoAmI()==HPC_UNIT)mod->RefreshUnitStatsFromMob(static_cast<CUnit*>(object));return true;
}
}
