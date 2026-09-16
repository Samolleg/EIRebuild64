from pathlib import Path
import json,shutil
r=Path(__file__).resolve().parent
def edit(name,old,new):
 p=r/name;b=p.read_bytes();old=old.replace('\n','\r\n').encode();new=new.replace('\n','\r\n').encode()
 if old not in b:old=old.replace(b'\r\n',b'\n');new=new.replace(b'\r\n',b'\n')
 assert b.count(old)==1,(name,old[:60],b.count(old))
 backup=r/'evidence/addon-integration/pre-r22'/name
 if not backup.exists():backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,backup)
 p.write_bytes(b.replace(old,new))
commands=json.loads((r/'evidence/addon-integration/r22-command-registry.json').read_text())['customCommands']
ready={'RefreshUnitStats','Sin','Cos','Pow','Abs','Round','Ceil','Floor','StrToFloat','StrLen','GetDexterity','GetIntelligence','GetStrength','GetActions','GetActionsBase','GetEncumbrance','GetMaxEncumbrance','GetUnitPose','GetUnitAggression'}
rows='\n'.join(' {%d,"%s","%s",%s},'%(c['id'],c['name'],c['format'],'true' if c['name'] in ready else 'false') for c in commands)
(r/'Game/ModMobCommands.h').write_text('''#pragma once
#include "Script.h"
#include "Unit.h"
#include "UnitServer.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include <cmath>
#include <stdexcept>
namespace ModMob {
// Release registry from57b8e0c, original IDs checked against EXE token table78BCA0.
struct Entry {int id;const char*name;const char*format;bool ready;};
inline const Entry registry[]={
'''+rows+'''
};
inline const Entry* Find(int id){for(const auto&e:registry)if(e.id==id)return &e;return nullptr;}
inline void Validate(const Entry&e,const SSCCmd&cmd){
 int f=0,s=0,o=0,g=0;const char*p=e.format;if(*p=='F')++p;
 for(;*p;++p){f+=*p=='f';s+=*p=='s';o+=*p=='o';g+=*p=='g';}
 if(cmd.params.GetSize()!=f||cmd.szParams.GetSize()!=s||cmd.oParams.GetSize()!=o||cmd.gParams.GetSize()!=g)throw std::runtime_error(std::string("Invalid MOB arguments: ")+e.name);
}
inline bool Float(SSCCmd&cmd,float&result){
 if(!ModRuntime::Current())return false;auto*e=Find(cmd.command);if(!e)return false;
 if(!e->ready||e->format[0]!='F')throw std::runtime_error(std::string("Unported MOB float command: ")+e->name);Validate(*e,cmd);
 const char*n=e->name;float a=cmd.params.GetSize()?cmd.params[0]:0;
 if(!strcmp(n,"Sin"))result=sin(a);
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
  if(!strcmp(n,"GetUnitPose")){result=float(unit->GetCurrentPose());return true;}
  if(!strcmp(n,"GetUnitAggression")){result=float(unit->GetAggression());return true;}
  auto*server=dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());if(!server)return true;auto&s=*server->GetRPGStats();
  if(!strcmp(n,"GetDexterity"))result=s.psDexterity.fpEffectiveValue;
  else if(!strcmp(n,"GetIntelligence"))result=s.psIntelligence.fpEffectiveValue;
  else if(!strcmp(n,"GetStrength"))result=s.psStrength.fpEffectiveValue;
  else if(!strcmp(n,"GetActions"))result=s.action.fpEffectiveValue;
  else if(!strcmp(n,"GetActionsBase"))result=s.action.fpBaseValue;
  else if(!strcmp(n,"GetEncumbrance"))result=s.fpCurrEncumbrance;
  else if(!strcmp(n,"GetMaxEncumbrance"))result=s.fpMaxEncumbrance;
 }
 return true;
}
inline bool Void(SSCCmd&cmd){
 auto*mod=ModRuntime::Current();if(!mod)return false;auto*e=Find(cmd.command);if(!e)return false;
 if(!e->ready||strcmp(e->name,"RefreshUnitStats"))throw std::runtime_error(std::string("Unported MOB action: ")+e->name);Validate(*e,cmd);
 auto*object=cmd.oParams[0]?cmd.oParams[0]->GetObject():nullptr;
 if(object&&object->WhoAmI()==HPC_UNIT)mod->RefreshUnitStatsFromMob(static_cast<CUnit*>(object));return true;
}
}
''',encoding='utf-8')
edit('Game/ModPackage.h','    void TickStatRefresh();','    void TickStatRefresh();\n    bool RefreshUnitStatsFromMob(CUnit*);')
edit('Game/ModPackage.cpp','#include "MapObjectServer.h"','#include "MapObjectServer.h"\n#include "SpellManager.h"')
edit('Game/ModPackage.cpp','void CModPackage::TickStatRefresh(){','''bool CModPackage::RefreshUnitStatsFromMob(CUnit*unit){
 if(!unit)return false;
 if(theWorldServer.GetMapObjectManager())if(auto*serverUnit=theWorldServer.GetUnit(unit->GetID()))unit=serverUnit;
 if(npcStatRefresh){SyncRuntimeState();auto*server=AuthoritativeServer(unit);if(!server||!server->GetUnit()||unit->GetOwner()||unit->IsDead())return false;refreshQueue->Add(unit->GetID());return true;}
 auto*manager=theWorldServer.GetSpellManager();if(!manager)return false;
 CSmartPtr<CSpell> spell=manager->CreateSpell("invisibility{d05;d05}");if(spell.IsEmpty())return false;
 theWorldServer.CastSpell(spell,nullptr,CPlot3D(0,0,0),unit,unit->GetCP());return true;
}
void CModPackage::TickStatRefresh(){''')
edit('Game/ScriptParser.cpp','#include "RandomGen.h"','#include "RandomGen.h"\n#include "ModMobCommands.h"\n#include <vector>')
edit('Game/ScriptParser.cpp','static SLexemeInfo validLexemes[] = {','static SLexemeInfo baseLexemes[] = {')
# Insert before first function that uses the table, after its closing brace.
p=r/'Game/ScriptParser.cpp';b=p.read_bytes();start=b.index(b'static SLexemeInfo baseLexemes[]');end=b.index(b'\r\n};',start)+5
insert='''
static SLexemeInfo* ModLexemes(){
 if(!ModRuntime::Current())return baseLexemes;
 static std::vector<SLexemeInfo> entries=[](){
  std::vector<SLexemeInfo> result;for(auto*p=baseLexemes;p->pszAscii;++p)result.push_back(*p);
  for(const auto&e:ModMob::registry)if(e.ready){
   SLexemeInfo value={e.name,static_cast<ESCCmd>(e.id),e.format[0]=='F'?e.format:nullptr,e.format[0]=='F'?nullptr:e.format};
   bool replaced=false;for(auto&old:result)if(!_stricmp(old.pszAscii,e.name)){old=value;replaced=true;break;}if(!replaced)result.push_back(value);
  }
  result.push_back({nullptr,SC_UNKNOWN,nullptr,nullptr});return result;
 }();return entries.data();
}
#define validLexemes ModLexemes()
'''.replace('\n','\r\n').encode();p.write_bytes(b[:end]+insert+b[end:])
edit('Game/Script.cpp','#include "StdAfx.h"','#include "StdAfx.h"\n#include "ModMobCommands.h"')
edit('Game/Script.cpp','    CalcParameters(pScript, vars);\n    switch (command)\n    {\n    case SC_ISEQUAL:', '    CalcParameters(pScript, vars);\n    if(ModMob::Float(*this,res))return;\n    switch (command)\n    {\n    case SC_ISEQUAL:')
edit('Game/Script.cpp','        switch (pCmd->command)\n        {\n        case SC_WAITSEGMENT:', '        if(!ModMob::Void(*pCmd))switch (pCmd->command)\n        {\n        case SC_WAITSEGMENT:')
print('R22 registry and19 fixed-format handlers integrated')
