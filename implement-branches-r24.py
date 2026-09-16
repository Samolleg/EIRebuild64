from pathlib import Path
import shutil
r=Path(__file__).resolve().parent
def edit(name,old,new):
 p=r/name;b=p.read_bytes();old=old.replace('\n','\r\n').encode();new=new.replace('\n','\r\n').encode()
 if old not in b:old=old.replace(b'\r\n',b'\n');new=new.replace(b'\r\n',b'\n')
 assert b.count(old)==1,(name,old[:65],b.count(old))
 backup=r/'evidence/addon-integration/pre-r24'/name
 if not backup.exists():backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,backup)
 p.write_bytes(b.replace(old,new))
shutil.copy2(r.parent/'src_addon_world_background_data_20260916/src/addon/MobIfElse.h',r/'Game/ModMobIfElse.h')
edit('Game/ScriptParser.cpp','#include "ModMobCommands.h"','#include "ModMobCommands.h"\n#include "ModMobIfElse.h"')
edit('Game/ScriptParser.cpp','    PreProcess(szFile);','''    if(ModRuntime::Current()){
        auto lowered=MobIfElse::Lower(std::string((LPCSTR)szFile,szFile.GetLength()));
        if(!lowered.error.empty())return ScriptError(lowered.line,SC_UNEXPECTED_SYMBOL,"if/else: %s",lowered.error.c_str());
        if(lowered.changed)szFile=lowered.text.c_str();
    }
    PreProcess(szFile);''')
edit('Game/ScriptParser.cpp','        if (strnicmp(pszLexeme, pInfo->pszAscii, nValidLexLen) == 0)','''        unsigned char next=static_cast<unsigned char>(pszLexeme[nValidLexLen]);
        if(ModRuntime::Current()&&isalpha(static_cast<unsigned char>(pInfo->pszAscii[0]))&&(isalnum(next)||next=='_'||next=='#'))continue;
        if (strnicmp(pszLexeme, pInfo->pszAscii, nValidLexLen) == 0)''')
# The boundary byte is only accessible for a keyword no longer than the input.
edit('Game/ScriptParser.cpp','        if (nValidLexLen < nMaxFoundLen)','        if (nValidLexLen < nMaxFoundLen || nValidLexLen > int(strlen(pszLexeme)))')
p=r/'Game/ScriptParser.cpp';b=p.read_bytes()
for fun,arg in [('isdigit','*pszLexeme'),('isspace','szLine[0]'),('isspace','*bufPtr')]:b=b.replace(f'{fun}({arg})'.encode(),f'{fun}(static_cast<unsigned char>({arg}))'.encode())
p.write_bytes(b)
for id,name in [(391,'JmvBranchUnless'),(392,'JmvBranchSkip')]:
 p=r/'Game/ModMobCommands.h';line=next(x for x in p.read_text().splitlines() if f'{{{id},' in x);edit('Game/ModMobCommands.h',line,line.replace('false','true'))
edit('Game/ModMobCommands.h','inline bool Void(SSCCmd&cmd){','''inline bool BranchForward(int&index,int count,bool skip,float amount){
 if(!std::isfinite(amount)||amount<0||amount>16777215.f||amount!=floorf(amount)||index<0||index>=count||int(amount)>count-index-1)return false;
 if(skip)index+=int(amount);return true;
}
inline bool Void(SSCCmd&cmd,int*index=nullptr,int count=0){''')
edit('Game/ModMobCommands.h',' if(!e->ready||strcmp(e->name,"RefreshUnitStats"))throw', ''' if(e->ready&&(cmd.command==391||cmd.command==392)){
  Validate(*e,cmd);bool skip=cmd.command==392||cmd.params[0]==0;float amount=cmd.params[cmd.command==391?1:0];
  if(!index||!BranchForward(*index,count,skip,amount))throw std::runtime_error("Invalid MOB branch state/offset");return true;
 }
 if(!e->ready||strcmp(e->name,"RefreshUnitStats"))throw''')
edit('Game/Script.cpp','if(!ModMob::Void(*pCmd))switch','if(!ModMob::Void(*pCmd,&nCurCommand,pThread->commands.GetSize()))switch')
edit('Game/ModRuntime.cpp','            CScript invalid;CString bad=', '''            {
                CScript branches;CString code="GlobalVars(x:float,score:float) WorldScript(x=0 if(x) then(score=99) else(score=3 if(1) then(score=Add(score,4)) else(score=99)) x=1 if(x) then(score=Mul(score,2)) else(score=99))";
                if(!branches.LoadText(code))throw std::runtime_error(std::string("Branch parser: ")+(LPCSTR)branches.GetLastError().text);branches.Segment();float score=0;require(branches.GetGlobalVars().fVars.GetVar("score",score)&&score==14,"Nested branch execution mismatch");
                int index=1;require(ModMob::BranchForward(index,5,true,3)&&index==4,"Branch-to-end offset mismatch");require(!ModMob::BranchForward(index,5,true,1)&&!ModMob::BranchForward(index,5,false,-1)&&!ModMob::BranchForward(index,5,true,.5f),"Invalid branch offset accepted");
                CScript malformed;CString invalidBranch="WorldScript(else())";require(!malformed.LoadText(invalidBranch,FALSE),"Unmatched else accepted");
                // Use the native world-script owner required by the original binary thread loader.
                auto&worldScript=theWorldServer.script;worldScript.Clear();struct ClearWorldScript{CScript&s;~ClearWorldScript(){s.Clear();}} clearWorldScript{worldScript};
                CString suspended="GlobalVars(score:float) WorldScript(score=0 if(1) then(WaitSegment(2) score=5) else(score=99) score=Add(score,1))";
                require(worldScript.LoadText(suspended),"Suspended branch compilation failed");worldScript.Segment();require(worldScript.GetGlobalVars().fVars.GetVar("score",score)&&score==0,"Branch did not suspend");
                CMemorySave memory;memory.StartSaving();worldScript.StoreScript(memory);worldScript.Clear();memory.Seek(0);memory.StartLoading();worldScript.LoadScript(memory);require(memory.IsOk()&&memory.GetPosition()==memory.GetSize(),"Binary world script did not round-trip fully");worldScript.Segment();require(worldScript.GetGlobalVars().fVars.GetVar("score",score)&&score==6,"Loaded branch resumed at wrong program counter");
            }
            CScript invalid;CString bad=''')
edit('Game/ModRuntime.cpp','return {{"mobOptionalVariadicStringRoundTrip",true}', 'return {{"mobNestedBranchesAndSuspendedBinaryRoundTrip",true},{"mobOptionalVariadicStringRoundTrip",true}')
print('R24 branch lowering, native jumps and binary suspension fixture integrated')
