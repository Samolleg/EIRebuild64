from pathlib import Path
import shutil,re
r=Path(__file__).resolve().parent
def edit(name,old,new):
 p=r/name;b=p.read_bytes();old=old.replace('\n','\r\n').encode();new=new.replace('\n','\r\n').encode()
 if old not in b:old=old.replace(b'\r\n',b'\n');new=new.replace(b'\r\n',b'\n')
 assert b.count(old)==1,(name,old[:65],b.count(old))
 backup=r/'evidence/addon-integration/pre-r23'/name
 if not backup.exists():backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,backup)
 p.write_bytes(b.replace(old,new))
p=r/'Game/ModMobCommands.h';s=p.read_text();old=s[s.index('inline void Validate'):s.index('inline bool Float')]
edit('Game/ModMobCommands.h',old,'''inline std::string Expand(const char*format,const SSCCmd&cmd){
 int counts[]={cmd.params.GetSize(),cmd.szParams.GetSize(),cmd.oParams.GetSize(),cmd.gParams.GetSize()};
 std::string result;char previous=0;
 auto total=[&](){return counts[0]+counts[1]+counts[2]+counts[3];};
 auto take=[&](char type){const char*types="fsog";auto*p=strchr(types,type);if(!p||counts[p-types]<=0)throw std::runtime_error("MOB argument type/count mismatch");--counts[p-types];result+=type;previous=type;};
 for(const char*p=format;*p;++p){
  if(*p=='?'){if(!total())break;continue;}
  if(*p=='+'){if(!previous||p[1])throw std::runtime_error("Invalid variadic MOB format");while(total())take(previous);}
  else take(*p);
 }
 if(total())throw std::runtime_error("Excess MOB arguments");return result;
}
inline void Validate(const Entry&e,const SSCCmd&cmd){const char*p=e.format;if(*p&&strchr("FSOG",*p))++p;Expand(p,cmd);}
''')
for name in ['Add','Mul','And','Or','Max','Min','FloatToStr','StrCat','StrHas','GetUnitExp']:
 s=p.read_text();line=next(x for x in s.splitlines() if ',"'+name+'",' in x);edit('Game/ModMobCommands.h',line,line.replace('false','true'))
edit('Game/ModMobCommands.h',' if(!strcmp(n,"Sin"))result=sin(a);',''' if(!strcmp(n,"Add")||!strcmp(n,"Mul")||!strcmp(n,"And")||!strcmp(n,"Or")||!strcmp(n,"Max")||!strcmp(n,"Min")){
  result=(!strcmp(n,"Mul")||!strcmp(n,"And"))?1:(!strcmp(n,"Max")||!strcmp(n,"Min"))?a:0;
  for(int i=0;i<cmd.params.GetSize();++i){float v=cmd.params[i];
   if(!strcmp(n,"Add"))result+=v;else if(!strcmp(n,"Mul"))result*=v;
   else if(!strcmp(n,"And")){if(int(v)==0){result=0;break;}}
   else if(!strcmp(n,"Or")){if(int(v)!=0){result=1;break;}}
   else if(!strcmp(n,"Max"))result=(std::max)(result,v);else result=(std::min)(result,v);
  }
 }
 else if(!strcmp(n,"StrHas"))result=strstr(cmd.szParams[0],cmd.szParams[1])?1.f:0.f;
 else if(!strcmp(n,"Sin"))result=sin(a);''')
edit('Game/ModMobCommands.h','  if(!strcmp(n,"GetDexterity"))result=s.psDexterity.fpEffectiveValue;','''  if(!strcmp(n,"GetUnitExp")){
   const char*type=cmd.szParams.GetSize()?(LPCSTR)cmd.szParams[0]:"max";
   if(!_stricmp(type,"max"))result=s.fpExperience;else if(!_stricmp(type,"spent"))result=s.fpUsedExperience;else if(!_stricmp(type,"penalty"))result=s.fExpPenalty;else if(!_stricmp(type,"real"))result=s.fpExperience+s.fExpPenalty;
  }
  else if(!strcmp(n,"GetDexterity"))result=s.psDexterity.fpEffectiveValue;''')
edit('Game/ModMobCommands.h','inline bool Void(SSCCmd&cmd){','''inline bool String(SSCCmd&cmd,CString&result){
 if(!ModRuntime::Current())return false;auto*e=Find(cmd.command);if(!e)return false;
 if(!e->ready||e->format[0]!='S')throw std::runtime_error(std::string("Unported MOB string command: ")+e->name);Validate(*e,cmd);
 if(!strcmp(e->name,"StrCat")){std::string value;for(int i=0;i<cmd.szParams.GetSize();i++)value.append((LPCSTR)cmd.szParams[i],cmd.szParams[i].GetLength());result=value.c_str();}
 else if(!strcmp(e->name,"FloatToStr")){
  int precision=cmd.params.GetSize()>=2?int(cmd.params[1]):3;precision=(std::max)(0,(std::min)(8,precision));
  char format[10],buf[100];sprintf_s(format,"%%.%df",precision);int len=sprintf_s(buf,format,cmd.params[0]);
  int i=len-1;while(i>0&&buf[i]=='0')--i;if(buf[i]=='.')buf[i]=0;else buf[i+1]=0;result=buf;
 }
 return true;
}
inline bool Void(SSCCmd&cmd){''')
# S-returning functions must be classified as functions by the token table.
edit('Game/ScriptParser.cpp',"e.format[0]=='F'?e.format:nullptr,e.format[0]=='F'?nullptr:e.format", "strchr(\"FSOG\",e.format[0])?e.format:nullptr,strchr(\"FSOG\",e.format[0])?nullptr:e.format")
edit('Game/Script.cpp','void SSCCmd::CalcFunction(CScript *pScript, SScriptVariablesSet &vars, CString &res)\n{\n    CalcParameters(pScript, vars);','void SSCCmd::CalcFunction(CScript *pScript, SScriptVariablesSet &vars, CString &res)\n{\n    CalcParameters(pScript, vars);\n    if(ModMob::String(*this,res))return;')
# Scope replacements to LoadFunction, preserving byte encoding outside it.
p=r/'Game/ScriptParser.cpp';b=p.read_bytes();start=b.index(b'BOOL CScript::LoadFunction(');end=b.index(b'BOOL CScript::LoadConditionsList(',start);old=b[start:end].decode('cp1251');new=old
new=new.replace('if (pszParams[0] != 0)',"if (pszParams[0] != 0 && pszParams[0] != '?' && pszParams[0] != '+')")
new=new.replace('                return TRUE;', '                res.params.SetSize(nFloatParam);res.szParams.SetSize(nStringParam);res.oParams.SetSize(nObjectParam);res.gParams.SetSize(nGroupParam);\r\n                return TRUE;')
new=new.replace('        bFailed = FALSE;', "        if(*pszParams=='?')++pszParams;else if(*pszParams=='+')--pszParams;\r\n        bFailed = FALSE;")
for typ,member,count in [('f','params','nFloatParam'),('s','szParams','nStringParam'),('o','oParams','nObjectParam'),('g','gParams','nGroupParam')]:
 new=new.replace("        case '"+typ+"':", "        case '"+typ+"':\r\n            if(res."+member+".GetSize()<="+count+")res."+member+".SetSize("+count+"+1);")
assert new!=old;p.write_bytes(b[:start]+new.encode('cp1251')+b[end:])
edit('Game/ScriptParser.cpp','    const char *pszParams = GetCommandParams((ESCCmd)cmd.command);','''    const char *pszParams = GetCommandParams((ESCCmd)cmd.command);
    std::string concreteModFormat;
    if(ModRuntime::Current())if(auto*entry=ModMob::Find(cmd.command))if(entry->ready){
        const char*format=entry->format;if(*format&&strchr("FSOG",*format))++format;
        concreteModFormat=ModMob::Expand(format,cmd);pszParams=concreteModFormat.c_str();
    }''')
print('R23 optional/variadic parser and10 handlers written')
