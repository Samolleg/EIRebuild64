exec(open(__file__.replace('test-mob-r22.py','implement-mob-r22.py'),encoding='utf-8').read().split('commands=json.loads')[0])
edit('Game/ModRuntime.cpp','#include "ModNpcRefresh.h"','#include "ModNpcRefresh.h"\n#include "ModMobCommands.h"')
edit('Game/ModRuntime.cpp','        Session session(package);','''        Session session(package);
        {
            require(_countof(ModMob::registry)==93&&ModMob::Find(385)&&!strcmp(ModMob::Find(385)->name,"RefreshUnitStats"),"Release MOB registry identity mismatch");
            CScript script;CString source="GlobalVars(a:float,b:float,c:float,d:float,e:float,f:float,g:float,h:float,i:float,u:object) WorldScript(a=Round(-2.5) b=Abs(-7) c=Ceil(1.2) d=Floor(-1.2) e=Sin(0) f=Cos(0) g=Pow(2,3) h=StrLen(\\"abcd\\") i=StrToFloat(\\"12.5\\") RefreshUnitStats(u))";
            if(!script.LoadText(source))throw std::runtime_error(std::string("MOB parser: ")+(LPCSTR)script.GetLastError().text);
            CString exported;script.StoreText(exported);require(exported.Find("RefreshUnitStats")>=0,"MOB text export lost custom command");CScript reparsed;require(reparsed.LoadText(exported,FALSE),"MOB text round-trip failed");
            script.Segment();const char*names[]={"a","b","c","d","e","f","g","h","i"};const float values[]={-3,7,2,-2,0,1,8,4,12.5f};
            for(int i=0;i<9;i++){float actual=999;require(script.GetGlobalVars().fVars.GetVar(names[i],actual)&&fabs(actual-values[i])<.0001,"Native parsed MOB numeric command mismatch");}
            CScript invalid;CString bad="GlobalVars(u:object) WorldScript(RefreshUnitStats(u,1))";require(!invalid.LoadText(bad,FALSE),"MOB parser accepts wrong arity");
            SSCCmd pending;pending.command=301;float value=0;bool rejected=false;try{ModMob::Float(pending,value);}catch(const std::runtime_error&){rejected=true;}require(rejected,"Pending addon command silently executed");
        }''')
edit('Game/ModRuntime.cpp','return {{"refreshQueueBatchDedupReset",true}', 'return {{"mobRegistryParserNumericRoundTrip",true},{"refreshQueueBatchDedupReset",true}')
print('R22 native parser/execution/round-trip fixture written')
