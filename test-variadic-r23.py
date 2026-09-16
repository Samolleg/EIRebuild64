exec(open(__file__.replace('test-variadic-r23.py','implement-variadic-r23.py'),encoding='utf-8').read().split("p=r/'Game/ModMobCommands.h'")[0])
edit('Game/ModMobCommands.h','s.fpUsedExperience','s.fSpentExp')
edit('Game/ModRuntime.cpp','pending.command=301','pending.command=302')
edit('Game/ModRuntime.cpp','            CScript invalid;CString bad=', '''            CScript extended;CString text="GlobalVars(a:float,b:float,c:float,d:float,e:float,f:float,g:float,h:float,s:string,t:string,u:object) WorldScript(a=Add(1,2,3,Round(2.5)) b=Mul(2,3,4) c=And(1,2,0.5) d=Or(0,0,2) e=Max(-3,-2,-1) f=Min(3,2,1) g=GetUnitExp(u) h=GetUnitExp(u,\\"real\\") s=StrCat(\\"A\\",FloatToStr(12.5),\\"B\\",FloatToStr(1.2345,2)) t=FloatToStr(1.2,0))";
            if(!extended.LoadText(text))throw std::runtime_error(std::string("Variadic parser: ")+(LPCSTR)extended.GetLastError().text);
            CString roundTrip;extended.StoreText(roundTrip);CScript again;if(!again.LoadText(roundTrip))throw std::runtime_error(std::string("Variadic reparse: ")+(LPCSTR)again.GetLastError().text);
            extended.Segment();again.Segment();const char*extendedNames[]={"a","b","c","d","e","f","g","h"};const float extendedValues[]={9,24,0,1,-1,1,0,0};
            for(auto*check:{&extended,&again}){for(int i=0;i<8;i++){float actual=999;require(check->GetGlobalVars().fVars.GetVar(extendedNames[i],actual)&&actual==extendedValues[i],"Variadic execution mismatch");}CString joined;require(check->GetGlobalVars().szVars.GetVar("s",joined)&&joined=="A12.5B1.23","Nested optional string result mismatch");require(check->GetGlobalVars().szVars.GetVar("t",joined)&&joined=="1","Explicit optional zero argument lost");}
            for(const char*badText:{"GlobalVars(x:float) WorldScript(x=Add(1))","GlobalVars(s:string) WorldScript(s=FloatToStr())","GlobalVars(s:string) WorldScript(s=FloatToStr(1,2,3))","GlobalVars(s:string) WorldScript(s=StrCat(\\"only\\"))"}){CScript rejected;CString text=badText;require(!rejected.LoadText(text,FALSE),"Optional/variadic arity accepted invalid input");}
            CScript invalid;CString bad=''')
edit('Game/ModRuntime.cpp','return {{"mobRegistryParserNumericRoundTrip",true}', 'return {{"mobOptionalVariadicStringRoundTrip",true},{"mobRegistryParserNumericRoundTrip",true}')
print('R23 nested optional/variadic tests written')
