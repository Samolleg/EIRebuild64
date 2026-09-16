from pathlib import Path
import shutil
r=Path(__file__).resolve().parent
def edit(name,fn):
 p=r/name;b=p.read_bytes();dest=r/'evidence/addon-integration/pre-r25'/name;dest.parent.mkdir(parents=True,exist_ok=True);assert not dest.exists();dest.write_bytes(b)
 s=b.decode('cp1251');s=fn(s);p.write_bytes(s.encode('cp1251'))
def header(s):
 names=['StrMid','StrLower','StrUpper','GetSkill','GetSkill2','GetSkill3','GetPerk','GetPerk2','GetPrototype','GetRace','GetUnitWeapon','GetUnitArmor']
 import re
 for n in names:
  s,count=re.subn(r'("'+n+r'","[^"]+",)false',r'\1true',s);assert count==1
 anchor='inline bool Float(SSCCmd&cmd,float&result){'
 helper='''inline int CheckedInt(float value){
 if(!std::isfinite(value)||value<-2147483648.f||value>=2147483648.f)throw std::runtime_error("Invalid MOB integer argument");return int(value);
}
inline int Index(float value,int count){int i=CheckedInt(value);if(i<0||i>=count)throw std::runtime_error("MOB index out of range");return i;}
'''.replace('\n','\r\n')
 assert anchor in s;s=s.replace(anchor,helper+anchor)
 anchor='  else if(!strcmp(n,"GetDexterity"))'
 block='''  else if(!strcmp(n,"GetSkill2"))result=s.cManualSkills[Index(a,RPG_NUM_MANUAL_SKILLS)];
  else if(!strcmp(n,"GetSkill3"))result=s.cAutoSkills[Index(a,RPG_NUM_AUTO_SKILLS)];
  else if(!strcmp(n,"GetPerk2"))result=s.cPerks[Index(a,RPG_NUM_PERKS)];
  else if(!strcmp(n,"GetSkill")){
   const char*skill=cmd.szParams[0];
   if(!_stricmp(skill,"science"))result=s.cManualSkills[RPGMSKL_SCIENCE];
   else if(!_stricmp(skill,"tame"))throw std::runtime_error("Addon GetSkill tame references unavailable autoSkills[8]");
   else {const char*names[]={"melee","archery","elemental","sense","astral"};const int indices[]={RPGASKL_MELEE,RPGASKL_ARCHERY,RPGASKL_ELEMENTAL,RPGASKL_SENSE,RPGASKL_ASTRAL};for(int i=0;i<5;i++)if(!_stricmp(skill,names[i]))result=s.cAutoSkills[indices[i]];}
  }
  else if(!strcmp(n,"GetPerk")){
   const char*names[]={"sword","axe","dagger","spear","hammer","bow","crossbow","fire","lightning","acid","illusion","divination","enchantments","healing","domination","infra","health","mana","vitality","spirit","quickness","lift","backstab","strength","dexterity","intelligence"};
   for(int i=0;i<26;i++)if(!_stricmp(cmd.szParams[0],names[i]))result=s.cPerks[i];
  }
'''.replace('\n','\r\n')
 assert anchor in s;s=s.replace(anchor,block+anchor)
 anchor=' else if(!strcmp(e->name,"FloatToStr"))'
 block=''' else if(!strcmp(e->name,"StrMid")){
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
'''.replace('\n','\r\n')
 assert anchor in s;s=s.replace(anchor,block+anchor);return s
edit('Game/ModMobCommands.h',header)
def fixture(s):
 anchor='            SRPGCommonStats corrected={},legacy={};'
 block='''            // R25: execute native MOB queries against real server, prototype and worn armor.
            {
                actor.AddRef();CScriptObject object;object.SetObject(&actor);
                struct ReleaseObject{CUnit&u;CScriptObject&o;~ReleaseObject(){o.SetObject(nullptr);u.DecRefCount();}} releaseObject{actor,object};
                auto saved=live;struct RestoreQueryStats{SRPGUnitStats&s;SRPGUnitStats saved;~RestoreQueryStats(){s=saved;}} restoreQueryStats{live,saved};
                for(int k=0;k<RPG_NUM_MANUAL_SKILLS;k++)live.cManualSkills[k]=BYTE(10+k);
                for(int k=0;k<RPG_NUM_AUTO_SKILLS;k++)live.cAutoSkills[k]=BYTE(20+k);
                for(int k=0;k<RPG_NUM_PERKS;k++)live.cPerks[k]=BYTE(30+k);
                CScript context;SScriptVariablesSet vars;
                auto number=[&](int id,float index,const char*name){SSCCmd cmd;cmd.command=id;cmd.oParams.Add(&object);if(name)cmd.szParams.Add(CString(name));else cmd.params.Add(index);float value=-1;cmd.CalcFunction(&context,vars,value);return value;};
                for(int k=0;k<RPG_NUM_MANUAL_SKILLS;k++)require(number(379,float(k),nullptr)==10+k,"Manual skill query mismatch");
                for(int k=0;k<RPG_NUM_AUTO_SKILLS;k++)require(number(380,float(k),nullptr)==20+k,"Auto skill query mismatch");
                for(int k=0;k<RPG_NUM_PERKS;k++)require(number(381,float(k),nullptr)==30+k,"Perk index query mismatch");
                require(number(348,0,"SCIENCE")==10&&number(348,0,"elemental")==23&&number(353,0,"INTELLIGENCE")==55&&number(353,0,"unknown")==0,"Named stat query mismatch");
                for(int id:{379,380,381}){bool rejected=false;try{number(id,1000,nullptr);}catch(const std::runtime_error&){rejected=true;}require(rejected,"Out-of-bounds stat index accepted");}
                bool tameRejected=false;try{number(348,0,"tame");}catch(const std::runtime_error&){tameRejected=true;}require(tameRejected,"Invalid reference tame index silently mapped");
                auto string=[&](int id,int slot){SSCCmd cmd;cmd.command=id;cmd.oParams.Add(&object);if(id==377||id==378)cmd.params.Add(float(slot));CString value;cmd.CalcFunction(&context,vars,value);return value;};
                require(string(354,0)==prototype->szName&&string(355,0)==prototype->szRace,"Prototype/race query mismatch");
                for(int k=0;k<2;k++)require(string(378,slots[k])==armorPieces[k].GetShapeEntry()->szName,"Worn armor query mismatch");
                require(string(377,-1).IsEmpty(),"Missing weapon query mismatch");
            }
'''
 assert anchor in s;s=s.replace(anchor,block.replace('\n','\r\n')+anchor)
 anchor='            CScript invalid;CString bad='
 block='''            {
                CScript strings;CString code="GlobalVars(a:string,b:string,c:string,d:string) WorldScript(a=StrMid(\\"abcdef\\",-3,2) b=StrLower(\\"AbC\\") c=StrUpper(\\"aBc\\") d=StrMid(\\"abcdef\\",2,0))";
                require(strings.LoadText(code),"String query parsing failed");CString exported;strings.StoreText(exported);CScript again;require(again.LoadText(exported),"String query text round-trip failed");strings.Segment();again.Segment();
                for(auto*p:{&strings,&again}){CString value;require(p->GetGlobalVars().szVars.GetVar("a",value)&&value=="de","Negative string position mismatch");require(p->GetGlobalVars().szVars.GetVar("b",value)&&value=="abc","Lowercase mismatch");require(p->GetGlobalVars().szVars.GetVar("c",value)&&value=="ABC","Uppercase mismatch");require(p->GetGlobalVars().szVars.GetVar("d",value)&&value=="bcdef","Zero string length mismatch");}
            }
'''
 assert anchor in s;s=s.replace(anchor,block.replace('\n','\r\n')+anchor)
 s=s.replace('return {{"mobNestedBranches','return {{"mobUnitAndStringQueries",true},{"mobNestedBranches');return s
edit('Game/ModRuntime.cpp',fixture)
p=r/'check-unit-commands-r25.py';p.write_text((r/'check-branches-r24.py').read_text().replace('r24-runtime','r25-runtime'),encoding='utf-8')
