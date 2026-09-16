from pathlib import Path
import re
r=Path(__file__).resolve().parent
def edit(name,fn):
 p=r/name;b=p.read_bytes();dest=r/'evidence/addon-integration/pre-r26'/name;dest.parent.mkdir(parents=True,exist_ok=True);assert not dest.exists();dest.write_bytes(b);s=b.decode('cp1251');p.write_bytes(fn(s).encode('cp1251'))
def insert(s,a,b):
 assert s.count(a)==1,(a,s.count(a));return s.replace(a,b.replace('\n','\r\n')+a)
edit('Game/UnitServer.h',lambda s:insert(s,'    CAIUnitLogic logic;', '    EUnitPose GetScriptPose() const { return currentPose.pose; }\n'))
edit('Game/AIUnitLogic.h',lambda s:insert(s,'    CAISpellCaster caster;', '    EUnitState GetScriptState() const { return state; }\n    void ResetScriptTarget(){environment.Clear();if(nCurrentAlarm>=0 && nCurrentAlarm<alarmLogics.GetSize())SetLogic(alarmLogics[nCurrentAlarm],nCurrentAlarm);}\n'))
edit('Game/MusicSystem.h',lambda s:insert(s,'    // Constr/desctr','    DWORD GetScriptNextPlayTime() const { return dwNextPlayTime; }\n    BOOL GetScriptForced() const { return bForcedMusic; }\n    const CString& GetScriptMusic() const { return szForcedMusic; }\n'))
edit('Game/ModPackage.h',lambda s:insert(s,'    void Load(const std::string &directory);','    const std::string& Directory() const { return root; }\n'))
def header(s):
 names=['GetPlayerOfUnit','GetArmor','GetObjectStringID','DistancePointPoint','NearestUnit','UnitsNearPoint','GSSetPlayerVar','GSGetPlayerVar','GiveExp','GiveUnitExp','StartCameraSave','StopCameraSave','Say','ConsoleString','Random','RandStr','RandObj','ResetTarget','SendCurrentParty','PlayerExists','Loot','GameOver','GameOverMSG','IsFileExists','GSListPlayerVars','IsMLooted','AddQuick2Unit','CheckObjectFlag','DamageMainUnitWeapon','CastSomeSpell','CastSomeSpellTarget','GetUnitState','CheckObjectFlag2','IsUnitFFeebled','IsUnitFStun','IsUnitFDontmove','LTZ','InflictDamageEx','MusicNPT','MusicFM','MusicBFM','CmdSkill','SetUnitAggression']
 for n in names:
  s,count=re.subn(r'("'+n+r'","[^"]*",)false',r'\1true',s);assert count==1,n
 s=s.replace('"DistancePointPoint","Fff"','"DistancePointPoint","Fffff"')
 s=insert(s,'namespace ModMob {','''#include "WorldServer.h"
#include "WorldClient.h"
#include "WorldInterface.h"
#include "WorldConnector.h"
#include "PlayerServer.h"
#include "Player.h"
#include "MusicSystem.h"
#include "RandomGen.h"
#include "ChatServer.h"
#include "Console.h"
#include "ItemWeapon.h"
#include "ItemArmor.h"
#include "ItemQuick.h"
#include "aiSettings.h"
''')
 anchor='inline bool Float(SSCCmd&cmd,float&result){'
 s=insert(s,anchor,'''inline CMapObject* ObjectArg(const SSCCmd&cmd,int i=0){return i<cmd.oParams.GetSize()&&cmd.oParams[i]?cmd.oParams[i]->GetObject():nullptr;}
inline CUnit* UnitArg(const SSCCmd&cmd){auto*p=ObjectArg(cmd);return p&&p->WhoAmI()==HPC_UNIT?static_cast<CUnit*>(p):nullptr;}
inline CUnitServer* ServerArg(const SSCCmd&cmd){auto*u=UnitArg(cmd);return u?dynamic_cast<CUnitServer*>(u->GetUnitSpecific()):nullptr;}
inline CPlayerServer* PlayerServer(int number){auto*p=theWorldServer.GetPlayer(number);return p?dynamic_cast<CPlayerServer*>(p->GetPlayerSpecific()):nullptr;}
inline std::string Join(const SSCCmd&cmd){std::string s;for(int i=0;i<cmd.szParams.GetSize();i++)s+=(LPCSTR)cmd.szParams[i];return s;}
''')
 anchor=' if(!strcmp(n,"Add")'
 i=s.index(anchor);s=s[:i]+''' if(cmd.command==95){if(cmd.params.GetSize()==1)return false;result=a<cmd.params[1]?random.GetFloat(a,cmd.params[1]):a;return true;}
 if(cmd.command==311){float dx=cmd.params[2]-a,dy=cmd.params[3]-cmd.params[1];result=sqrtf(dx*dx+dy*dy);return true;}
 if(cmd.command==341){result=theWorldServer.GetPlayer(CheckedInt(a))?1.f:0.f;return true;}
 if(cmd.command==315){auto*p=theWorldServer.GetPlayer(CheckedInt(a));result=p?p->party.vars.GetVar(cmd.szParams[0]):0;return true;}
 if(cmd.command==373||cmd.command==375){auto*m=theWorldClient.GetMusicSystem();result=m?float(cmd.command==373?m->GetScriptNextPlayTime():m->GetScriptForced()):0;return true;}
 if(cmd.command==356){std::string name=cmd.szParams[0];std::replace(name.begin(),name.end(),'\\\\','/');if(name==".."||name.find("/../")!=std::string::npos||name.rfind("../",0)==0||(name.size()>=3&&name.substr(name.size()-3)=="/.."))throw std::runtime_error("MOB parent path forbidden");std::string path=ModRuntime::Current()->Directory()+"/"+name;auto attrs=GetFileAttributesA(path.c_str());result=attrs!=INVALID_FILE_ATTRIBUTES&&!(attrs&FILE_ATTRIBUTE_DIRECTORY)?1.f:0.f;return true;}
 if(cmd.command==358||cmd.command==368||cmd.command==369||cmd.command==370||cmd.command==362||cmd.command==367){auto*object=ObjectArg(cmd);DWORD flag=cmd.command==358?0x80000000u:cmd.command==368?UNIT_FEEBLED:cmd.command==369?UNIT_STUN:UNIT_DONTMOVE;if(cmd.command==362)flag=DWORD(CheckedInt(a));if(cmd.command==367)flag=1u<<Index(a,32);result=object&&object->CheckFlag(flag)?1.f:0.f;return true;}
 if(cmd.command==300){auto*u=UnitArg(cmd);result=u&&IsIDofPersonage(u->GetID())&&u->GetOwner()?float(u->GetOwner()->GetPlayerNumber()):-1.f;return true;}
 if(cmd.command==363){auto*u=UnitArg(cmd);int damage=CheckedInt(a);result=float(damage);auto*item=u&&ServerArg(cmd)?u->GetQWeapon(0):nullptr;if(item&&damage>0&&damage<1000){float old=item->GetHP(),critical=item->GetMaxHP()*AIGeneralSettings.fItemDurabilityCritical;item->ApplyDamage(float(damage));if(item->IsBroken()||(old>=critical&&item->GetHP()<critical))ServerArg(cmd)->InformFailure(0);}return true;}
'''.replace('\n','\r\n')+s[i:]
 s=s.replace('  if(!strcmp(n,"GetUnitPose")){result=float(unit->GetCurrentPose());return true;}\r\n','')
 anchor='  if(!strcmp(n,"GetUnitExp"))'
 s=insert(s,anchor,'''  if(cmd.command==361){result=float(server->GetScriptPose());return true;}
  if(cmd.command==366){result=float(server->logic.GetScriptState());return true;}
  if(cmd.command==302){int part=CheckedInt(a),type=Index(cmd.params[1],RPG_NUM_ATTACK_TYPES);if(part>=0){if(part>=RPG_NUM_HIT_LOCATIONS)throw std::runtime_error("MOB armor location out of range");for(auto&layer:s.hls[part].layers)result+=layer.fpAbsorbtions[type];}else{float values[RPG_NUM_ATTACK_TYPES];unit->GetUserUnitStats()->GetAbsorbtions(&unit->GetUserCommonStats(),values,IsIDofPersonage(unit->GetID()));result=values[type];}return true;}
''')
 anchor=' if(!strcmp(e->name,"StrCat"))'
 s=insert(s,anchor,''' if(cmd.command==303){auto*o=ObjectArg(cmd);if(o)result=std::to_string(o->GetID()).c_str();return true;}
 if(cmd.command==333){result=cmd.szParams[random.Get(cmd.szParams.GetSize()-1)];return true;}
 if(cmd.command==374){auto*m=theWorldClient.GetMusicSystem();if(m)result=m->GetScriptMusic();return true;}
 if(cmd.command==377||cmd.command==378)result="";
''')
 anchor='inline bool BranchForward('
 s=insert(s,anchor,'''inline bool Object(SSCCmd&cmd,CScriptObject&result){
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
 case314:{if(auto*p=PlayerServer(CheckedInt(a)))p->SetVar(cmd.szParams[0],cmd.params[1]);break;}
 case316:{auto*p=theWorldServer.GetPlayer(CheckedInt(a));if(p)theWorldServer.GainExperience(p,cmd.params[1]);break;}
 case317:if(server){server->GainExperience(a);if(auto*p=unit->GetOwner())if(auto*ps=dynamic_cast<CPlayerServer*>(p->GetPlayerSpecific()))ps->GainExp(unit->GetID(),a);}break;
 case318:IWorldClient.StartCameraSave(cmd.szParams[0]);break;
 case319:IWorldClient.StopCameraSave();break;
 case320:if(theWorldConnector.IsLMP()&&pChatServer&&pChatServer->IsValid())pChatServer->AddChatPhrase(Join(cmd).c_str());break;
 case126:NConsole::game<<Join(cmd).c_str()<<std::endl;break;
 case232:if(server)server->logic.ResetScriptTarget();break;
 case340:if(auto*p=PlayerServer(CheckedInt(a)))p->ResendParty();break;
 case342:{auto*p=theWorldServer.GetPlayer(CheckedInt(a));if(p&&unit&&unit->IsDead()){theWorldServer.HooverUnit(unit,p);theWorldServer.RemoveMapObject(unit->GetID());}break;}
 case343:if(auto*p=PlayerServer(0))p->SendStringEvent(0,"gameover");break;
 case344:if(auto*p=PlayerServer(0))p->SendStringEvent(0,(std::string("gameover ")+(LPCSTR)cmd.szParams[0]+" "+(LPCSTR)cmd.szParams[1]).c_str());break;
 case357:{auto*p=theWorldServer.GetPlayer(CheckedInt(a));if(p)for(const auto&v:p->party.vars)NConsole::game<<v.first.c_str()<<" = "<<v.second<<std::endl;break;}
 case360:if(server){auto*item=theWorldServer.CreateItem(cmd.szParams[0]);if(auto*quick=dynamic_cast<CItemQuick*>(item)){unit->AddQItem(quick);server->NotifyItemsUpdated();}else if(item)item->Release();}break;
 case364:if(server)server->logic.StartCast(CheckedInt(cmd.params[2]),CPlot3D(a,cmd.params[1],theWorldServer.GetZValue(a,cmd.params[1])),false);break;
 case365:if(server)server->logic.StartCast(CheckedInt(a),ObjectArg(cmd,1),false);break;
 case371:if(auto*p=PlayerServer(0))p->LeaveToZone(cmd.szParams[0],0);break;
 case372:if(server){SRPGAttackPortion portion;portion.Clear();portion.nSkill=0;portion.fpToHit=100;portion.fpAttacks[Index(cmd.params[1],RPG_NUM_ATTACK_TYPES)]=1;portion.hl=ERPGHitLocations(Index(cmd.params[2],RPG_NUM_HIT_LOCATIONS));portion.fBackStabMultiplier=cmd.params[3];portion.fpDamage=a;server->ProcessAttackPortion(&portion,nullptr,nullptr);}break;
 case376:if(server)server->CmdSkill(ObjectArg(cmd,1),CheckedInt(cmd.params[1]),DWORD(CheckedInt(cmd.params[2])),a!=0,DWORD(CheckedInt(cmd.params[3])));break;
 case382:if(unit)unit->SetAggression(EUnitAggression(CheckedInt(a)));break;
 default:return false;
 }return true;
}
''')
 anchor=' if(!e->ready||strcmp(e->name,"RefreshUnitStats"))'
 s=insert(s,anchor,' if(e->ready && cmd.command!=385){Validate(*e,cmd);if(Action(cmd))return true;}\n')
 return s
edit('Game/ModMobCommands.h',header)
def script(s):
 for a,b in [('    res[0] = pScript->GetTempObject();','    if(ModMob::Object(*this,*res[0]))return;'),('    res[0] = pScript->GetTempGroup();','    if(ModMob::Group(*this,*res[0]))return;')]:
  assert s.count(a)==1;s=s.replace(a,a+'\r\n'+b)
 return s
edit('Game/Script.cpp',script)
