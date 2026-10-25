#include "StdAfx.h"
#include "ModRuntime.h"
#include "GameJournal.h"
#include "SettingsProfile.h"
#include "ModWorldBackground/Native.h"
#include "ScenarioSpeechDucking.h"
#include "ModAudioPolicy.h"
#include "ModPackage.h"
#include "ModCombat.h"
#include "RpgSystem.h"
#include "Unit.h"
#include "PerkDatabase.h"
#include "ItemDatabase.h"
#include "Item.h"
#include "SpellSystem.h"
#include "SpellDatabase.h"
#include "UnitServer.h"
#include "Effect.h"
#include "Particle.h"
#include "UnitDatabase.h"
#include "GeneralSettings.h"
#include "ModStatRules.h"
#include "ModStatInitialization.h"
#include "ModNpcRefresh.h"
#include "ModMobCommands.h"
#include "SpellManager.h"
#include "ModSpellRules.h"
#include "Party.h"
#include "ItemManager.h"
#include "WorldServer.h"
#include "ServerSpecificManager.h"
#include <lua.hpp>
#include <stdexcept>
#include <filesystem>
#include <shellapi.h>

bool JMV_CheckExtendedSpellLifecycle();

namespace ModRuntime {
namespace { thread_local CModPackage *current = nullptr;thread_local CModPackage *resources = nullptr; }
CModPackage *Current() { return current; }
CModPackage *Resources(){return resources;}
std::string ConfigPath(const char*path){return resources?resources->ResolveConfig(std::filesystem::path(path).filename().string(),path):path;}
std::string CameraPath(const char*path){return resources?resources->ResolveCamera(std::filesystem::path(path).filename().string(),path):path;}
std::string MoviePath(const char*path){return resources?resources->ResolveMovie(path,path):path;}
std::string SaveRoot(const char*fallback){return resources?resources->ResolveSaveRoot(fallback):fallback;}
bool SinglePlayerAllowed(){return !resources||resources->SinglePlayerAllowed();}
std::string MapPath(const char*path){
 if(!resources)return path;std::string normalized=path;for(char&c:normalized){if(c=='/')c='\\';else if(c>='A'&&c<='Z')c+=32;}
 auto ext=std::filesystem::path(normalized).extension().string();if(ext!=".mpr"&&ext!=".mob"&&ext!=".mq")return path;
 auto pos=normalized.rfind("\\maps\\");std::string key=pos==std::string::npos?std::filesystem::path(normalized).filename().string():normalized.substr(pos+6);
 if(normalized.rfind("maps\\",0)==0)key=normalized.substr(5);return resources->ResolveMap(key,path);
}
Host::Host(const std::string&directory){
 if(resources||current)throw std::runtime_error("A mod host is already active");
 package=std::make_unique<CModPackage>();package->Load(directory);session=std::make_unique<Session>(*package);resources=package.get();
}
Host::~Host(){ModWorldBackground::Unload();resources=nullptr;session.reset();package.reset();}
static std::unique_ptr<Host> SelectHost(bool commandLine){
 namespace fs=std::filesystem;
 int count=0;LPWSTR*args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)throw std::runtime_error("Cannot parse command line");
 struct Free{LPWSTR*p;~Free(){LocalFree(p);}} free{args};
 const auto root=fs::path(SettingsProfile::Path()).parent_path();
 std::string directory,source="none",rejectedDirectory,rejectedSource;bool found=false;
 auto select=[&](fs::path path,const char*origin,bool filePath){
  if(path.empty())return false;
  if(path.is_relative())path=root/path;
  path=path.lexically_normal();std::error_code error;
  if(filePath&&fs::is_regular_file(path,error))path=path.parent_path();
  error.clear();
  if(!fs::is_regular_file(path/L"config.reg",error)){
   if(rejectedSource.empty()){rejectedSource=origin;rejectedDirectory=path.string();}
   return false;
  }
  directory=path.string();source=origin;found=true;return true;
 };
 fs::path explicitMod;bool explicitFound=false;
 if(commandLine)for(int i=1;i<count;i++)if(wcscmp(args[i],L"--mod")==0){
  if(explicitFound||i+1>=count)throw std::runtime_error("Expected one --mod directory");
  explicitFound=true;explicitMod=fs::path(args[++i]);
 }
 if(explicitFound)select(explicitMod,"--mod",false);
 if(!found)select(fs::path(SettingsProfile::String(L"Settings",L"AddonPath")),"Settings\\AddonPath",false);
 if(!found)select(fs::path(SettingsProfile::String(L"",L"AddonPath")),"AddonPath",true);
 if(!found){
  auto config=root/L"restarter.config";std::error_code error;
  if(fs::is_regular_file(config,error)){
   wchar_t selection[32768]={};
   GetPrivateProfileStringW(L"Settings",L"ModSel",L"",selection,_countof(selection),config.c_str());
   if(selection[0]){
    auto selected=(fs::path(selection).is_relative()?root/selection:fs::path(selection)).lexically_normal();
    error.clear();
    if(fs::is_regular_file(selected,error))select(selected.parent_path(),"restarter.config ModSel",false);
    else if(rejectedSource.empty()){rejectedSource="restarter.config ModSel";rejectedDirectory=selected.parent_path().string();}
   }
  }
 }
 if(!found&&!rejectedSource.empty()){source=rejectedSource;directory=rejectedDirectory;}
 GameJournal::Event("SESSION","mod_selection","source=%s path=%s",GameJournal::Quote(source.c_str()).c_str(),GameJournal::Quote(directory.c_str()).c_str());
 if(!found&&!rejectedSource.empty())throw std::runtime_error("Cannot select mod: no source contains config.reg (first source: "+source+", directory: "+directory+")");
 return found?std::make_unique<Host>(directory):nullptr;
}
std::unique_ptr<Host> Host::FromCommandLine(){return SelectHost(true);}
std::unique_ptr<Host> Host::FromStarterSelection(){return SelectHost(false);}
Session::Session(CModPackage &package) {
    if (current) throw std::runtime_error("A mod simulation session is already active");
    package.InitializeScript();
    package.ResetRuntimeState();
    current = &package;
}
Session::~Session() { if(current)current->ResetRuntimeState();current = nullptr; }
void SpendRunningMana(SRPGUnitStats &stats, CUnit &unit, SRPGCommonStats &common) {
    if (current) current->CalcRunningManaCost(stats, unit, common);
    else common.ssMP.fpCurrent -= common.ssMP.fpMax * (1 / 15.0 / 10.0);
}

nlohmann::json CheckEngineDispatch(CModPackage &package, lua_State *lua, const std::string &perkArchive, const std::string &itemArchive,const std::string &unitArchive) {
    namespace fs = std::filesystem;
    // The headless test temporarily owns the real engine DB, restoring the prior database on exit.
    struct DatabaseGuard {
        CPerkDatabase previous;SItemDatabase previousItems;SUnitDatabase previousUnits;SSpellDatabase previousSpells;
        DatabaseGuard() { std::swap(previous, thePerkDatabase);std::swap(previousItems,theItemDatabase);std::swap(previousUnits,theUnitDatabase);std::swap(previousSpells,theSpellDatabase); }
        ~DatabaseGuard() { std::swap(previous, thePerkDatabase);std::swap(previousItems,theItemDatabase);std::swap(previousUnits,theUnitDatabase);std::swap(previousSpells,theSpellDatabase); }
    } database;
    const fs::path path(perkArchive);
    if (!thePerkDatabase.LoadDatabase((path.parent_path().string()+"\\").c_str(), path.filename().string().c_str()) ||
        thePerkDatabase.GetNumPerks() == 0) throw std::runtime_error("Native perk database failed to load");
    const fs::path itemPath(itemArchive);
    if(!theItemDatabase.Load((itemPath.parent_path().string()+"\\").c_str(),itemPath.filename().string().c_str()))throw std::runtime_error("Native items DB failed to load");
    const fs::path unitPath(unitArchive);
    if(!theUnitDatabase.Load((unitPath.parent_path().string()+"\\").c_str(),unitPath.filename().string().c_str()))throw std::runtime_error("Native unit DB failed to load");
    std::string spellOwner;package.ReadResource("database.res","spells.sdb","",spellOwner);if(spellOwner.empty())throw std::runtime_error("Package has no spells.sdb owner");
    const fs::path spellPath(spellOwner);if(!theSpellDatabase.Load((spellPath.parent_path().string()+"\\").c_str(),spellPath.filename().string().c_str()))throw std::runtime_error("Native spell DB failed to load");
    SRPGUnitStats stats = {};
    stats.fpExperience=2000; stats.psStrength={25,50}; stats.psDexterity={25,25};
    stats.fpMaxEncumbrance=100; stats.fpCurrEncumbrance=50;
    stats.move={10,10}; stats.fpMoveDamageModifier=.5f;
    SRPGCommonStats common = {}; common.ssMP={.1f,100,0}; CUnit unit;
    auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
    require(ModAudioPolicy::Check(),"Audio policy contract");
    // Does not construct CSoundManager or initialize an audio device.
    {
        VoiceWait::Registry registry;int state=-1;int starts=0;
        auto start=[&](){++starts;return registry.Publish(0,123,5);};
        auto active=[&](uint32_t t){return registry.Active(t);};
        require(VoiceWait::Step(state,start,active)==VoiceWait::StepResult::Started,"Voice wait start");
        const auto old=VoiceWait::Ticket(state);
        require(registry.Capture(123,5)==old && !registry.Capture(124,5),"Voice wait identity");
        require(VoiceWait::Step(state,start,active)==VoiceWait::StepResult::Waiting && starts==1,"Voice wait must not replay while active");
        registry.End(0);const auto fresh=registry.Publish(0,123,5);
        require(fresh!=old && !registry.Active(old),"Voice slot reuse ABA");
        require(VoiceWait::Step(state,start,active)==VoiceWait::StepResult::Finished && state==-1,"Voice EOS continuation");
        require(VoiceWait::Step(state,[](){return 0u;},active)==VoiceWait::StepResult::Failed && state==-1,"Voice failure continuation");
        state=VoiceWait::Encode(fresh);require(VoiceWait::AfterLoad(state)&&state==-1,"Voice load replay reset");
        require(!VoiceWait::AfterLoad(state),"Voice idle load");state=42;require(!VoiceWait::AfterLoad(state)&&state==42,"Native sleep preserved");
        require(MiniaudioOneShotSfx::TryStartVirtualFlat(nullptr,1,"absent.wav",1000,100,0,false,false,true,false,1,1)==MiniaudioOneShotSfx::START_NOT_HANDLED,"No-manager audio rejection");
        require(!MiniaudioOneShotSfx::CaptureVoiceWait(nullptr,1),"No-device voice ticket");
        const unsigned char pcmWave[]={82,73,70,70,44,0,0,0,87,65,86,69,102,109,116,32,16,0,0,0,1,0,1,0,64,31,0,0,128,62,0,0,2,0,16,0,100,97,116,97,8,0,0,0,0,0,1,0,255,255,0,0};
        require(MiniaudioOneShotSfx::CaptureEncoded("r27-headless-pcm.wav",pcmWave,sizeof(pcmWave),"headless_decoder"),"PCM decode capture");
        require(MiniaudioOneShotSfx::IsCaptured("r27-headless-pcm.wav"),"PCM cache lookup");
        require(!MiniaudioOneShotSfx::CaptureEncoded("r27-invalid.wav","invalid",7,"headless_decoder"),"Invalid audio rejection");
        MiniaudioOneShotSfx::Shutdown("headless_decoder_cleanup");
        require(!MiniaudioOneShotSfx::IsCaptured("r27-headless-pcm.wav"),"Decoded cache teardown");
        ScenarioSpeechDucking duck;duck.Update(1,100);duck.Update(2,250);require(duck.gain==.5f,"Overlapping speech single envelope");duck.Update(0,600);require(duck.gain==1.f,"Speech release envelope");
        require(ModMob::synchronousDepth==0,"Initial script loop depth");{ModMob::SynchronousScope a;{ModMob::SynchronousScope b;require(ModMob::synchronousDepth==2,"Nested loop guard");}}require(ModMob::synchronousDepth==0,"Loop guard restored");
    }

    {
        require(!theWorldServer.IsMapLoaded(),"Headless lifecycle fixture requires unloaded world");package.SuccessfulWorldLoad(WorldLoadOrigin::SavedGame);package.TickStatRefresh();
        ModNpcRefreshQueue queue;for(int id=0;id<29;id++)require(queue.Add(id),"Refresh enqueue failed");
        require(!queue.Add(0),"Refresh queue accepts duplicate");std::vector<int> seen;
        auto record=[&](int id){seen.push_back(id);};
        require(queue.Process(record)==12&&queue.Process(record)==12&&queue.Process(record)==5&&queue.Process(record)==0,"Refresh batch size mismatch");
        require(seen.size()==29&&!queue.Add(28),"Completed batch lost world deduplication");
        for(int id=0;id<29;id++)require(seen[id]==id,"Refresh order mismatch");
        queue.scanPending=false;queue.heroesPending=false;queue.Add(90);queue.Reset();
        require(queue.ids.empty()&&queue.scheduled.empty()&&queue.scanPending&&queue.heroesPending&&queue.Add(0),"Refresh reset retained prior world state");
        require(package.SuppressLegacyRefreshSpell("INVISIBILITY{D05;D05}")&&!package.SuppressLegacyRefreshSpell("invisibility")&&!package.SuppressLegacyRefreshSpell(nullptr),"Legacy refresh filter mismatch");
    }
    auto run=[&](const char *code){if(luaL_loadstring(lua,code)||lua_pcall(lua,0,0,0)){std::string error=lua_tostring(lua,-1);lua_pop(lua,1);throw std::runtime_error(error);}};
    int perkCases=0;bool nonzeroJewelryFixture=false;int preparedCases=0,npcCases=0,fullCases=0,fullHeroCases=0,leftHandCases=0;bool stunRollback=false;int armorCases=0;bool effectsRollback=false;
    {
        Session session(package);
        {
            struct SlotUnit : CUnit {void Bind(CUnitSpecific*p){lpUnitSpecific=p;} ~SlotUnit(){lpUnitSpecific=nullptr;}};
            for(int id:{4515,1000000001}){
                CUnitServer specific;SlotUnit actor;actor.SetID(id);actor.Bind(&specific);
                CItemSpell spells[17];for(auto&spell:spells)spell.AddRef();
                struct ReleaseSlots {CUnit&u;CItemSpell*s;~ReleaseSlots(){for(DWORD i=0;i<QI_MAX_SPELLS;++i)u.RemoveQSpell(i);for(int i=0;i<17;++i)s[i].DecRefCount();}} release{actor,spells};
                int count=id==4515?8:16;require(actor.GetSpellSlotCount()==count,"Logical spell slot count");
                require(actor.AddQSpell(nullptr)==-1,"Null spell insertion");
                for(int i=0;i<count;++i)require(actor.AddQSpell(&spells[i])==i,"Spell insertion index");
                require(actor.AddQSpell(&spells[count])==-1,"Spell insertion overflow");
                actor.RemoveQSpell(0);int refs=spells[count-1].GetRefCount();
                require(actor.AddQSpell(&spells[count-1])==count-1&&spells[count-1].GetRefCount()==refs,"Duplicate spell after a hole");
                require(actor.AddQSpell(&spells[count])==0,"Reuse empty spell slot");
                require(actor.GetQSpellItem(count-1)==&spells[count-1]&&!actor.GetQSpellItem(count)&&!actor.GetQSpellItem(DWORD(-1)),"Native spell read bounds");
            }
        }

        {
            require(_countof(ModMob::registry)==93&&ModMob::Find(385)&&!strcmp(ModMob::Find(385)->name,"RefreshUnitStats"),"Release MOB registry identity mismatch");
            CScript script;CString source="GlobalVars(a:float,b:float,c:float,d:float,e:float,f:float,g:float,h:float,i:float,u:object) WorldScript(a=Round(-2.5) b=Abs(-7) c=Ceil(1.2) d=Floor(-1.2) e=Sin(0) f=Cos(0) g=Pow(2,3) h=StrLen(\"abcd\") i=StrToFloat(\"12.5\") RefreshUnitStats(u))";
            if(!script.LoadText(source))throw std::runtime_error(std::string("MOB parser: ")+(LPCSTR)script.GetLastError().text);
            CString exported;script.StoreText(exported);require(exported.Find("RefreshUnitStats")>=0,"MOB text export lost custom command");CScript reparsed;require(reparsed.LoadText(exported,FALSE),"MOB text round-trip failed");
            script.Segment();const char*names[]={"a","b","c","d","e","f","g","h","i"};const float values[]={-3,7,2,-2,0,1,8,4,12.5f};
            for(int i=0;i<9;i++){float actual=999;require(script.GetGlobalVars().fVars.GetVar(names[i],actual)&&fabs(actual-values[i])<.0001,"Native parsed MOB numeric command mismatch");}
            CScript extended;CString text="GlobalVars(a:float,b:float,c:float,d:float,e:float,f:float,g:float,h:float,s:string,t:string,u:object) WorldScript(a=Add(1,2,3,Round(2.5)) b=Mul(2,3,4) c=And(1,2,0.5) d=Or(0,0,2) e=Max(-3,-2,-1) f=Min(3,2,1) g=GetUnitExp(u) h=GetUnitExp(u,\"real\") s=StrCat(\"A\",FloatToStr(12.5),\"B\",FloatToStr(1.2345,2)) t=FloatToStr(1.2,0))";
            if(!extended.LoadText(text))throw std::runtime_error(std::string("Variadic parser: ")+(LPCSTR)extended.GetLastError().text);
            CString roundTrip;extended.StoreText(roundTrip);CScript again;if(!again.LoadText(roundTrip))throw std::runtime_error(std::string("Variadic reparse: ")+(LPCSTR)again.GetLastError().text);
            extended.Segment();again.Segment();const char*extendedNames[]={"a","b","c","d","e","f","g","h"};const float extendedValues[]={9,24,0,1,-1,1,0,0};
            for(auto*check:{&extended,&again}){for(int i=0;i<8;i++){float actual=999;if(!check->GetGlobalVars().fVars.GetVar(extendedNames[i],actual)||actual!=extendedValues[i])throw std::runtime_error(std::string("Variadic execution mismatch ")+extendedNames[i]+" actual="+std::to_string(actual)+" expected="+std::to_string(extendedValues[i])+" reparse="+std::to_string(check==&again)+" text="+(LPCSTR)roundTrip);}CString joined;require(check->GetGlobalVars().szVars.GetVar("s",joined)&&joined=="A12.5B1.23","Nested optional string result mismatch");require(check->GetGlobalVars().szVars.GetVar("t",joined)&&joined=="1","Explicit optional zero argument lost");}
            for(const char*badText:{"GlobalVars(x:float) WorldScript(x=Add(1))","GlobalVars(s:string) WorldScript(s=FloatToStr())","GlobalVars(s:string) WorldScript(s=FloatToStr(1,2,3))","GlobalVars(s:string) WorldScript(s=StrCat(\"only\"))"}){CScript rejected;CString text=badText;require(!rejected.LoadText(text,FALSE),"Optional/variadic arity accepted invalid input");}
            {
                CScript branches;CString code="GlobalVars(x:float,score:float) WorldScript(x=0 if(x) then(score=99) else(score=3 if(1) then(score=Add(score,4)) else(score=99)) x=1 if(x) then(score=Mul(score,2)) else(score=99))";
                if(!branches.LoadText(code))throw std::runtime_error(std::string("Branch parser: ")+(LPCSTR)branches.GetLastError().text);branches.Segment();float score=0;require(branches.GetGlobalVars().fVars.GetVar("score",score)&&score==14,"Nested branch execution mismatch");
                int index=1;require(ModMob::BranchForward(index,5,true,3)&&index==4,"Branch-to-end offset mismatch");require(!ModMob::BranchForward(index,5,true,1)&&!ModMob::BranchForward(index,5,false,-1)&&!ModMob::BranchForward(index,5,true,.5f),"Invalid branch offset accepted");
                CScript malformed;CString invalidBranch="WorldScript(else())";require(!malformed.LoadText(invalidBranch,FALSE),"Unmatched else accepted");

            }
            {
                CScript strings;CString code="GlobalVars(a:string,b:string,c:string,d:string) WorldScript(a=StrMid(\"abcdef\",-3,2) b=StrLower(\"AbC\") c=StrUpper(\"aBc\") d=StrMid(\"abcdef\",2,0))";
                require(strings.LoadText(code),"String query parsing failed");CString exported;strings.StoreText(exported);CScript again;require(again.LoadText(exported),"String query text round-trip failed");strings.Segment();again.Segment();
                for(auto*p:{&strings,&again}){CString value;require(p->GetGlobalVars().szVars.GetVar("a",value)&&value=="de","Negative string position mismatch");require(p->GetGlobalVars().szVars.GetVar("b",value)&&value=="abc","Lowercase mismatch");require(p->GetGlobalVars().szVars.GetVar("c",value)&&value=="ABC","Uppercase mismatch");require(p->GetGlobalVars().szVars.GetVar("d",value)&&value=="bcdef","Zero string length mismatch");}
            }
            {
                CScript loops;CString code="GlobalVars(target:object,score:float) Script LoopArgs(value:float,members:group)(if(Add(1,0))then(For(target,members)(score=Add(score,value) ForIf(target,members,1)(score=Add(score,value)))))";
                if(!loops.LoadText(code,FALSE))throw std::runtime_error(std::string("Loop script parameters: ")+(LPCSTR)loops.GetLastError().text);
                CString text;loops.StoreText(text);CScript again;if(!again.LoadText(text,FALSE))throw std::runtime_error(std::string("Nested loop parameter export/reparse: ")+(LPCSTR)again.GetLastError().text+"; text="+(LPCSTR)text);
                CScript invalid;CString bad="GlobalVars(target:object,score:float) Script LoopArgs(value:float,members:group)(if(Add(1,0))then(For(target,members)(score=Add(score,missing))))";
                require(!invalid.LoadText(bad,FALSE),"Unknown loop parameter was accepted");
            }
            CScript invalid;CString bad="GlobalVars(u:object) WorldScript(RefreshUnitStats(u,1))";require(!invalid.LoadText(bad,FALSE),"MOB parser accepts wrong arity");
            {
                CScript commands;CString code="GlobalVars(d:float,r:float,f:float,g:group,o:object) WorldScript(d=DistancePointPoint(0,0,3,4) r=Random(7,7) f=IsFileExists(\"main.lua\") o=RandObj(g))";
                require(commands.LoadText(code),"World command parser failed");CString exported;commands.StoreText(exported);CScript again;require(again.LoadText(exported),"World command export failed");commands.Segment();again.Segment();for(auto*p:{&commands,&again}){float v=0;require(p->GetGlobalVars().fVars.GetVar("d",v)&&v==5,"Distance contract mismatch");require(p->GetGlobalVars().fVars.GetVar("r",v)&&v==7,"Random interval mismatch");require(p->GetGlobalVars().fVars.GetVar("f",v)&&v==1,"Package file lookup mismatch");}
                SSCCmd escape;escape.command=356;escape.szParams.Add(CString("../main.lua"));float v=0;bool rejected=false;try{ModMob::Float(escape,v);}catch(const std::runtime_error&){rejected=true;}require(rejected,"File lookup traversal accepted");
            }
            {
                std::string value;require(ScriptMessages::Format("A\r\nB",value)&&value.find("A\nB\n")!=std::string::npos,"Message newline normalization mismatch");require(!ScriptMessages::Format("\aF1hidden",value)&&!ScriptMessages::Format("   ",value),"Message controls/blank accepted");
                ScriptMessages::Queue queue;int owner=0,other=1;require(queue.Push("early")!=nullptr,"Message accepted without mission");queue.Bind(&owner);for(int i=0;i<64;i++)require(queue.Push("text")==nullptr,"Message queue admission failed");require(queue.Push("overflow")!=nullptr&&queue.Take(&other).empty(),"Message queue capacity/owner mismatch");require(queue.Take(&owner).size()==64,"Message queue drain mismatch");queue.Push("stale");queue.Unbind(&owner);queue.Bind(&other);require(queue.Take(&other).empty(),"Stale mission message leaked");
            }
            {ModConsumableConfig config;require(package.GetNpcCombatConsumableConfig("zone5_1",4515,"CyclopsA16",3,0,config)&&config.automatic&&config.charges==1&&config.spellFormula=="strength {e0;e0;e0;d0}","Installed NPC consumable profile mismatch");require(!package.GetNpcCombatConsumableConfig("zone5_1",4515,"__absent_fixture__",3,0,config),"Absent NPC profile was invented");}
            require(JMV_CheckNpcConsumablePacking(),"NPC consumable persistence packing mismatch");
            require(JMV_CheckNpcVisitLifecycle(),"Explicit save/zone visit lifecycle mismatch");
            require(JMV_CheckRegisteredLootCodec(),"Registered loot marker codec mismatch");
            SSCCmd pending;pending.command=387;float value=0;bool rejected=false;try{ModMob::Float(pending,value);}catch(const std::runtime_error&){rejected=true;}require(rejected,"Pending addon command silently executed");
        }
        require(fabs(stats.RecalcHP()-67.1850743387)<.0001,"Engine RecalcHP dispatch mismatch");
        require(fabs(stats.RecalcMP()-43.0310446032)<.0001,"Engine RecalcMP dispatch mismatch");
        // Compare native callsite to the actual callback over ascending/descending and equal levels.
        for(auto levels : {std::pair<int,int>{0,1},{10,20},{20,10},{15,15},{79,80},{80,81}})
            require(SRPGUnitStats::GetSkillPrice(levels.first,levels.second)==package.GetSkillPriceDiff(levels.first,levels.second),"Engine skill price dispatch mismatch");
        for(int i=0;i<thePerkDatabase.GetNumPerks();i++) {
            const auto *perk=thePerkDatabase.GetPerk(i);
            int observed=stats.GetPerkPrice(perk);
            require(observed==package.GetPerkPrice(stats,*perk),"Engine perk price dispatch mismatch");
            perkCases++;
        }
        // Native item/weapon records and real spell-effect storage, no fake Lua getters.
        struct EffectUnit : CUnit {
            void Bind(CUnitSpecific*p){lpUnitSpecific=p;}
            ~EffectUnit(){lpUnitSpecific=nullptr;spellEffects.RemoveAll();}
            void Power(int id,float power){CSpellEffect e;e.nSpellID=BYTE(id);e.fpPower=power;spellEffects.Add(e);}
            void SelectWeapon(int slot){dwCurrentWeapon=slot;}
            void StrongMan(int value){rpgUserUnitStats.cPerks[29]=BYTE(value);}
        };
        int weaponIndex=0,quickIndex=-1;double bonus=0;
        for(int i=0;i<theItemDatabase.quickItems.GetNumEntries();i++){
            auto v=package.Lookup("JewelryProp",std::string(theItemDatabase.quickItems.GetEntry(i)->szName),4,0);
            if(v.is_number() && v.get<double>()!=0){quickIndex=i;bonus=v.get<double>();break;}
        }
        require(quickIndex>=0 && theItemDatabase.weapons.GetNumEntries()>0,"No real quick-slot jewelry fixture");nonzeroJewelryFixture=true;
        CItemQuick jewelry;jewelry.SetTypeID(ITID_ITEM_QUICK);jewelry.SetSubtypeID(DWORD(quickIndex)<<16);jewelry.AddRef();
        CItemWeapon equipped;equipped.SetTypeID(ITID_ITEM_WEAPON);equipped.SetSubtypeID(DWORD(weaponIndex)<<16);equipped.AddRef();
        {
            CUnitServer specific;EffectUnit equippedUnit;equippedUnit.Bind(&specific);int slot=equippedUnit.AddWeapon(&equipped);equippedUnit.SelectWeapon(slot);int quickSlot=equippedUnit.AddQItem(&jewelry);
            struct Detach {EffectUnit&u;CItemWeapon&w;CItemQuick&q;int slot,quick;~Detach(){u.RemoveWeapon(slot);w.DecRefCount();u.RemoveQItem(quick);q.DecRefCount();}} detach{equippedUnit,equipped,jewelry,slot,quickSlot};
            equippedUnit.Power(SPELL_ID_STRENGTHEN,20);equippedUnit.Power(SPELL_ID_WEAKEN,10);
            SRPGUnitStats changed=stats;changed.psIntelligence={25,25};
            float hp=changed.GetModifiedHealth(&equippedUnit,100);
            require(fabs(hp-105)<.0001,"Native spell modified HP mismatch");
            require(fabs(changed.psStrength.fpEffectiveValue-(50+bonus))<.0001,"Equipment primary stat mutation was lost");
            // Complete initial/spell/custom stage on real equipped native objects.
            // Native Wear/Remove use the real unit-specific component and database armor slot.
            require(theItemDatabase.armors.GetNumEntries()>0,"No armor fixture");
            CItemArmor armor;armor.SetTypeID(ITID_ITEM_ARMOR);armor.SetSubtypeID(0);armor.Setup(theItemDatabase.armors.GetEntry(0));armor.AddRef();
            specific.RegisterUnit(&equippedUnit);*specific.GetRPGStats()={};equippedUnit.Wear(&armor);
            struct DetachArmor {EffectUnit&u;CUnitServer&s;CItemArmor&a;~DetachArmor(){u.Remove(a.GetArmorType());a.DecRefCount();s.RegisterUnit(nullptr);}} detachArmor{equippedUnit,specific,armor};
            require(equippedUnit.GetQArmor(armor.GetArmorType())==&armor,"Native armor fixture was not equipped");
            equippedUnit.SetID(1000000001);
            equippedUnit.Power(SPELL_ID_ANTIMAGIC,20);equippedUnit.Power(SPELL_ID_PROT_FIRE,30);
            equippedUnit.Power(SPELL_ID_PROT_FIRE,10); // strongest instance wins, never sum duplicates
            equippedUnit.Power(SPELL_ID_INVISIBILITY,50);equippedUnit.Power(SPELL_ID_SILENCE,25);
            auto luaNumber=[&](const char*code){if(luaL_loadstring(lua,code)||lua_pcall(lua,0,1,0))throw std::runtime_error(lua_tostring(lua,-1));double n=lua_tonumber(lua,-1);lua_pop(lua,1);return n;};
            int flow=int(luaNumber("return PerkType.Flow")),armory=int(luaNumber("return PerkType.Armory"));
            lua_pushstring(lua,theItemDatabase.armors.GetEntry(0)->szName);lua_setglobal(lua,"__r14_armor_name");
            lua_pushstring(lua,theItemDatabase.materials.GetEntry(0)->szName);lua_setglobal(lua,"__r14_material_name");
            double sightBonus=luaNumber("local t=require('table_indexes'); local v=0; for _,r in ipairs(t.ArmorProp[__r14_armor_name] or {}) do v=v+(r[ArmorPropType.Sight] or 0) end; for _,r in ipairs(t.MaterialProp[__r14_material_name] or {}) do v=v+(r[MaterialPropType.Sight] or 0) end; return v");
            double hearingBonus=luaNumber("local t=require('table_indexes'); local v=0; for _,r in ipairs(t.ArmorProp[__r14_armor_name] or {}) do v=v+(r[ArmorPropType.Hearing] or 0) end; for _,r in ipairs(t.MaterialProp[__r14_material_name] or {}) do v=v+(r[MaterialPropType.Hearing] or 0) end; return v");
            int staffType=int(luaNumber("return WeaponType.Staff")),largeStaffType=int(luaNumber("return WeaponType.StaffBig"));
            int staffIndex=-1;for(int i=0;i<theItemDatabase.weapons.GetNumEntries();i++)if(theItemDatabase.weapons.GetEntry(i)->dwTypeID==staffType){staffIndex=i;break;}
            require(staffIndex>=0,"No real staff fixture");
            for(int level=0;level<=3;level++){
                SRPGUnitStats prepared=stats;prepared.psIntelligence={25,999};prepared.cPerks[flow]=BYTE(level);prepared.cPerks[armory]=BYTE(level);
                for(int i=0;i<RPG_NUM_SENSES;i++){prepared.senses[i]={10,99};prepared.percepts[i]={1,99};}
                SRPGCommonStats result={};for(auto&a:result.psAbsorbtions)a={5,999};
                package.PrepareUnitStats(prepared,result,equippedUnit,"unused-hero-prototype");
                float flowMod=prepared.GetPerkModifier(flow)*.01f,armorMod=prepared.GetPerkModifier(armory)*.01f;
                require(fabs(result.psAbsorbtions[RPGAT_TERMICAL].fpEffectiveValue-35*(1+flowMod))<.0001,"Fire/antimagic/Flow stage mismatch");
                require(fabs(result.psAbsorbtions[RPGAT_ELECTRICAL].fpEffectiveValue-25*(1+flowMod))<.0001,"Electric protection stage mismatch");
                require(fabs(result.psAbsorbtions[RPGAT_IMPALLING].fpEffectiveValue-5*(1+armorMod))<.0001,"Armory stage mismatch");
                require(fabs(prepared.percepts[RPGS_SIGHT].fpEffectiveValue-(.5f+sightBonus))<.0001 && fabs(prepared.percepts[RPGS_HEARING].fpEffectiveValue-(.75f+hearingBonus))<.0001,"Stealth effect scaling mismatch");
                const float quickRates[]={.001f,.0012f,.0014f,.00175f};
                require(fabs(jewelry.GetManaRegenRate()-quickRates[level])<.000001,"Quick item regeneration did not commit");
                const float itemRates[]={.001f,.00115f,.0013f,.0015f},staffRates[]={.001f,.00125f,.0015f,.002f};
                bool isStaff=equipped.GetShapeEntry()->dwTypeID==staffType||equipped.GetShapeEntry()->dwTypeID==largeStaffType;
                require(fabs(armor.GetManaRegenRate()-itemRates[level])<.000001,"Armor regeneration did not commit");
                require(fabs(equipped.GetManaRegenRate()-(isStaff?staffRates[level]:itemRates[level]))<.000001,"Weapon regeneration mismatch");
                equipped.SetSubtypeID(DWORD(staffIndex)<<16);package.PrepareUnitStats(prepared,result,equippedUnit,"unused-hero-prototype");
                require(fabs(equipped.GetManaRegenRate()-staffRates[level])<.000001,"Staff-specific regeneration mismatch");
                equipped.SetSubtypeID(DWORD(weaponIndex)<<16);
                preparedCases++;
            }
            run("__r14_armor_name=nil; __r14_material_name=nil");
            // Original callbacks first mutate stats and item regeneration, then fail: nothing publishes.
            lua_getglobal(lua,"ApplyCustomEffects");int original=luaL_ref(lua,LUA_REGISTRYINDEX);
            struct RestoreCustom {lua_State*s;int ref;~RestoreCustom(){lua_rawgeti(s,LUA_REGISTRYINDEX,ref);lua_setglobal(s,"ApplyCustomEffects");luaL_unref(s,LUA_REGISTRYINDEX,ref);lua_pushnil(s);lua_setglobal(s,"__r14_original");}} restoreCustom{lua,original};
            lua_rawgeti(lua,LUA_REGISTRYINDEX,original);lua_setglobal(lua,"__r14_original");
            run("ApplyCustomEffects=function(...) __r14_original(...); error('intentional after item mutations') end");
            SRPGUnitStats before=stats;SRPGCommonStats beforeCommon=common;float rate=jewelry.GetManaRegenRate(),armorRate=armor.GetManaRegenRate(),weaponRate=equipped.GetManaRegenRate();
            bool rejected=false;try{package.PrepareUnitStats(stats,common,equippedUnit,"unused");}catch(const std::runtime_error&){rejected=true;}
            require(rejected && memcmp(&stats,&before,sizeof(stats))==0 && memcmp(&common,&beforeCommon,sizeof(common))==0 && jewelry.GetManaRegenRate()==rate && armor.GetManaRegenRate()==armorRate && equipped.GetManaRegenRate()==weaponRate,"Stat/equipment stage partially committed");effectsRollback=true;

        }
        // Pick installed NPC rows, preserving the real row values and native perk acquisition.
        run("__r14_npcs={}; local idx=require('table_indexes'); for name,rows in pairs(idx.UnitPerk) do local r=rows[#rows]; if r and type(name)=='string' and idx.UnitStat[name] then for col=13,44 do if type(r[col])=='number' and r[col]>0 then __r14_npcs[#__r14_npcs+1]=name; break end end end end; table.sort(__r14_npcs); assert(#__r14_npcs>0)");
        lua_getglobal(lua,"__r14_npcs");int count=(std::min)(8,int(lua_objlen(lua,-1)));
        for(int n=1;n<=count;n++){
            lua_rawgeti(lua,-1,n);std::string name=lua_tostring(lua,-1);lua_pop(lua,1);
            CUnitServer specific;EffectUnit npc;npc.Bind(&specific);npc.SetID(100+n);
            SRPGUnitStats prepared={};prepared.fpExperience=2000;prepared.psStrength={25,25};prepared.psDexterity={25,25};prepared.psIntelligence={25,25};prepared.move={10,10};
            for(auto&h:prepared.hls){h.state=RPGALS_ACTIVE;h.hlt=RPGHLT_BODY;h.fpHPWeight=1;h.fpVitality=1.f/RPG_NUM_HIT_LOCATIONS;h.fpHP=100;h.fpMaxHP=100;}
            for(int i=0;i<RPG_NUM_SENSES;i++){prepared.senses[i]={10,10};prepared.percepts[i]={1,1};}
            SRPGCommonStats commonNpc={};
            package.PrepareUnitStats(prepared,commonNpc,npc,name);
            for(int i=0;i<32;i++){auto value=package.Lookup("UnitPerk",name,i+13,0);int level=value.is_number()?int(value.get<double>()):0;require(prepared.cPerks[i]==level,"NPC perk did not reach staged native stats");}
            SRPGUnitStats once=prepared;SRPGCommonStats onceCommon=commonNpc;
            package.PrepareUnitStats(prepared,commonNpc,npc,name);
            require(memcmp(&prepared,&once,sizeof(prepared))==0 && memcmp(&commonNpc,&onceCommon,sizeof(commonNpc))==0,"NPC repeated preparation accumulates modifiers");npcCases++;
        }
        lua_pop(lua,1);lua_pushnil(lua);lua_setglobal(lua,"__r14_npcs");
        // Left-hand semantics: current client StrongMan perk, shield priority, dual staves.
        {
            auto shape=[&](int type){for(int i=0;i<theItemDatabase.weapons.GetNumEntries();i++)if(theItemDatabase.weapons.GetEntry(i)->dwTypeID==type)return i;throw std::runtime_error("Missing weapon type fixture");};
            CItemWeapon weapons[3];int ids[]={shape(RPGWST_HANDS4),shape(RPGWST_HANDS1),shape(RPGWST_HANDS1)};
            CUnitServer specific;EffectUnit actor;actor.Bind(&specific);actor.StrongMan(0);actor.SelectWeapon(0);
            for(int i=0;i<2;i++){weapons[i].SetTypeID(ITID_ITEM_WEAPON);weapons[i].SetSubtypeID(DWORD(ids[i])<<16);weapons[i].AddRef();actor.AddWeapon(&weapons[i]);}
            struct DetachWeapons {EffectUnit&u;CItemWeapon*w;int count=2;~DetachWeapons(){for(int i=0;i<count;i++){u.RemoveWeapon(i);w[i].DecRefCount();}}} detachWeapons{actor,weapons};
            require(ModStats::LeftHand(actor)==-1,"Two-handed sword unexpectedly permits shield");leftHandCases++;
            actor.StrongMan(1);require(ModStats::LeftHand(actor)==1,"Client StrongMan does not enable shield");leftHandCases++;
            int staff=shape(RPGWST_HANDS3);weapons[0].SetSubtypeID(DWORD(staff)<<16);weapons[1].SetSubtypeID(DWORD(staff)<<16);actor.StrongMan(0);
            require(ModStats::LeftHand(actor)==1,"Dual staves first slot mismatch");leftHandCases++;
            actor.SelectWeapon(1);require(ModStats::LeftHand(actor)==0,"Dual staves second slot mismatch");leftHandCases++;
            weapons[2].SetTypeID(ITID_ITEM_WEAPON);weapons[2].SetSubtypeID(DWORD(ids[2])<<16);weapons[2].AddRef();actor.AddWeapon(&weapons[2]);detachWeapons.count=3;
            actor.SelectWeapon(0);require(ModStats::LeftHand(actor)==2,"Shield priority from primary slot mismatch");leftHandCases++;
            actor.SelectWeapon(1);require(ModStats::LeftHand(actor)==0,"Dual slot must retain first staff despite shield");leftHandCases++;
        }
        // R35: current installed skill table, exact cost and admission boundary contracts.
        {
            ModSkills::Config power,precise,doubled,shot,explosive,cripple,missing;
            require(package.SkillConfig("powerhit",power)&&package.SkillConfig("precisehit",precise)&&package.SkillConfig("doubleshot",doubled)&&package.SkillConfig("preciseshot",shot)&&package.SkillConfig("explosiveshot",explosive)&&package.SkillConfig("cripplinghit",cripple),"Installed active skills unavailable");
            require(!package.SkillConfig("__absent_skill__",missing),"Unknown active skill accepted");
            require(power.Cost(999)==50&&power.Duration()==360&&power.damageMultiplier==1.25f,"PowerHit installed policy drift");
            require(precise.Cost(999)==35&&precise.accuracyBonus==20&&precise.damageMultiplier==1,"PreciseHit installed policy drift");
            require(doubled.projectileCount==2&&doubled.damageMultiplier==.75f&&doubled.Weapon(RPGWST_BOW)&&!doubled.Weapon(RPGWST_CROSSBOW),"DoubleShot bow-only policy");
            require(shot.accuracyBonus==20&&shot.projectileCount==1,"PreciseShot accuracy policy");
            require(power.Weapon(RPGWST_HAMMER)&&power.Weapon(RPGWST_HANDS6)&&!power.Weapon(RPGWST_AXE)&&!power.Weapon(RPGWST_SWORD)&&!power.Weapon(-1)&&!power.Weapon(32),"PowerHit weapon mask boundary");
            ModSkills::Config fallback;fallback.requiredMelee=-1;fallback.staminaMultiplier=1.5f;
            require(fallback.Cost(20)==30&&fallback.Requirement(17)==17,"Prototype fallback policy");
            ModSkills::Cooldowns clock;int world1,world2;
            clock.Start(&world1,100,7,"powerhit",power.Duration());require(clock.Remaining(&world1,459,7,"POWERHIT")==1&&clock.Remaining(&world1,459,8,"powerhit")==0,"Cooldown identity or final tick");
            require(clock.Remaining(&world1,460,7,"powerhit")==0,"Cooldown expiry");
            clock.Start(&world1,500,7,"powerhit",360);require(clock.Remaining(&world1,499,7,"powerhit")==0,"Cooldown clock rollback");
            clock.Start(&world1,500,7,"powerhit",360);require(clock.Remaining(&world2,500,7,"powerhit")==0,"Cooldown world replacement");
            require(ModSkills::Slot(ModSkills::CommandTag|15)==15&&ModSkills::Slot(ModSkills::CommandTag|16)==-1&&ModSkills::Slot(15)==-1,"Skill command slot bounds");
        }
        // R36: shared flight algorithm and native status owner, without an interactive world.
        {
            CPlot3D next;auto clear=[](const CPlot3D&,const CPlot3D&){return 1.f;};
            require(!ModSkills::ExplosiveStep(CPlot3D(0,0,0),CPlot3D(10,0,5),0,next,clear)&&fabs(next.x-4.f/3)<.0001&&fabs(next.z-2.f/3)<.0001,"Explosive XY flight speed");
            require(ModSkills::ExplosiveStep(CPlot3D(0,0,0),CPlot3D(1,0,1),1,next,clear)&&next.x==1&&next.z==1,"Explosive target arrival");
            int traces=0;auto wall=[&](const CPlot3D&,const CPlot3D&b){++traces;return b.x>=.5f?0.f:1.f;};
            require(ModSkills::ExplosiveStep(CPlot3D(0,0,0),CPlot3D(10,0,0),1,next,wall)&&next.x<.5f&&next.x>.49f&&traces==9,"Explosive relief bisection");
            require(ModSkills::ExplosiveStep(CPlot3D(2,3,4),CPlot3D(10,0,0),150,next,clear)&&next.x==2&&next.y==3,"Explosive timeout");
            require(ModSkills::ExplosiveStep(CPlot3D(2,3,4),CPlot3D(10,0,0),-1,next,clear)&&next.z==4,"Explosive rollback termination");
            SRPGAttackPortion portion;portion.Clear();portion.fpDamage=37;for(auto&v:portion.fpAttacks)v=.5f;portion.bCalledShot=true;portion.fBackStabMultiplier=3;
            ModSkills::ExplosiveDamage(portion);require(portion.fpDamage==37&&portion.fpToHit==FLT_MAX&&portion.hl==RPGHL_ALL&&portion.bAreaEffect&&!portion.bCalledShot&&portion.fBackStabMultiplier==1,"Explosive portion conversion");
            for(int i=0;i<RPG_NUM_ATTACK_TYPES;++i)require(portion.fpAttacks[i]==(i==RPGAT_TERMICAL?1.f:0.f),"Explosive fire distribution");
            package.MarkSkillImpact(portion,1);SRPGAttackPortion second=portion;package.MoveAttack(portion,second);
            require(package.TakeSkillImpact(portion)==0&&package.TakeSkillImpact(second)==1&&package.TakeSkillImpact(second)==0,"Skill impact delivered more than once");package.ForgetAttack(second);
            struct StatusUnit: CUnit {
                int speedChanges=0;
                BOOL RecalcRPGStats(bool=false) override{return TRUE;}
                void ChangeSpeedWithSpell() override{++speedChanges;}
                void Fill(){while(spellEffects.GetSize()<20){CSpellEffect e;e.nSpellID=255;e.nDuration=10;spellEffects.Add(e);}}
                void SetStatusPower(float value){spellEffects[0].fpPower=value;}
                void Expire(){spellEffects[0].nDuration=0;}
                ~StatusUnit(){spellEffects.RemoveAll();}
            } actor;
            int id=package.CrippledSpellId();require(id>=0,"Installed crippled prototype missing");CSpell spell;spell.SetSubtypeID(DWORD(id)<<16);spell.Setup(theSpellDatabase.spells[id]);CSpellEffect effect;spell.GetSpellEffect(effect);
            require(effect.nDuration>0&&effect.fpPower>0&&effect.fpPower<100,"Installed crippled template invalid");
            require(package.CrippledSpeed(actor)==1&&actor.RefreshWeaponStatus(effect)&&actor.GetSpellEffects().GetSize()==1,"Cripple status add");
            float expected=(std::max)(.05f,1-theSpellDatabase.spells[id]->fpEffect/100.f);actor.SetStatusPower(99);
            require(fabs(package.CrippledSpeed(actor)-expected)<.0001,"Client status power incorrectly used instead of prototype");
            actor.Fill();effect.nDuration+=15;require(actor.RefreshWeaponStatus(effect)&&actor.GetSpellEffects().GetSize()==20&&actor.GetSpellEffects()[0].nDuration==effect.nDuration,"Full capacity status refresh");
            CSpellEffect other=effect;other.nSpellID=254;require(!actor.RefreshWeaponStatus(other)&&actor.GetSpellEffects().GetSize()==20,"Status capacity overflow");
            actor.Expire();require(package.CrippledSpeed(actor)==1&&actor.speedChanges==2,"Status expiration speed factor");
            CParticle dust;dust.ConfigurePowerHit(nullptr,CPlot3D(1,2,3),CPlot3D(1,0,0),-1);require(!dust.IsUseAddEffect()&&!dust.IsPowerHitGlow(-1),"PowerHit contact configuration");
        }
        // R40: real phase owners and native particle configuration, without graphics creation.
        {
            require(JMV_CheckExtendedSpellLifecycle(),"Extended spell phase ownership failed");
            struct ParticleCase{ETypeIDs id;const char*texture;};
            const ParticleCase cases[]={
                {PTID_FIREBALLDARK,"FireSmoke2"},{PTID_FIREBALLLIGHT,"FireSmoke3"},
                {PTID_EYEBALLFIRE,"TendriSmoke"},{PTID_EYEBALLWATER,"TendriSmoke1"},
                {PTID_EYEBALLACID,"TendriSmoke2"},
                {PTID_FIREARROWDARK,"FireArrowSmoke2"},{PTID_FIREARROWLIGHT,"FireArrowSmoke3"},
                {PTID_FIREWALLDARK,"TendriSmoke2"},{PTID_FIREWALLLIGHT,"TendriSmoke3"},
                {PTID_FIREBLASTDARK,"Explosion2"},{PTID_FIREBLASTLIGHT,"Explosion3"},
                {PTID_ACIDRAYDARK,"AcidRay2"},{PTID_ACIDRAYLIGHT,"AcidRay3"},
                {PTID_ACIDSTARDARK,"AcidRay2"},{PTID_ACIDSTARLIGHT,"AcidRay3"},
                {PTID_NUKEDARK,"AcidRay"},{PTID_NUKELIGHT,"AcidRay"},
                {PTID_POISONFOGDARK,"PoisonCloud"},{PTID_POISONFOGLIGHT,"PoisonCloud"},
                {PTID_LIGHTNINGBLASTDARK,"TendriLight"},{PTID_LIGHTNINGBLASTLIGHT,"TendriLight"}};
            for(auto&test:cases){CParticle particle;particle.SetTypeID(test.id);char texture[256]={};require(particle.ConfigureExtendedSpell(test.id,texture)&&strcmp(texture,test.texture)==0,"Extended particle source texture mismatch");}
            CParticle base;char original[256]="untouched";require(!base.ConfigureExtendedSpell(PTID_FIREBALL,original)&&strcmp(original,"untouched")==0,"Extended factory stole base effect");
            struct Lifetime:CEffectParticle{int Length(){return GetParticleEffectLength();}};
            for(auto id:{PTID_FIREBLASTDARK,PTID_FIREBLASTLIGHT,PTID_LIGHTNINGBLASTDARK,PTID_LIGHTNINGBLASTLIGHT,PTID_NUKEDARK,PTID_NUKELIGHT,PTID_ACIDSTARDARK,PTID_ACIDSTARLIGHT}){Lifetime effect;effect.typeID=id;require(effect.Length()==60,"Unowned one-shot particle lifetime");}
        }
        // R39: source-derived elemental matrix and authoritative Feeble state.
        {
            const int bases[]={SPELL_ID_FIREBALL,SPELL_ID_FIREARROW,SPELL_ID_FIREWALL,SPELL_ID_LIGHTNING,SPELL_ID_INVOKE_LGHTNG,SPELL_ID_WALL_LGHTNG,SPELL_ID_ACIDRAY,SPELL_ID_COLUMN_ACID,SPELL_ID_POISONFOG};
            for(int id=57;id<=74;++id){
                int family=(id-57)/2;int element=family<3?RPGAT_TERMICAL:family<6?RPGAT_ELECTRICAL:RPGAT_CHEMICAL;
                CSpell spell;spell.SetSubtypeID(DWORD(id)<<16);
                require(ModSpells::ElementalBase(id)==bases[family]&&spell.GetAttackType()==element&&spell.IsOffensive(),"Extended native elemental identity");
                for(int duration:{0,1,20}){SRPGAttackPortion ap;ap.fpDamage=999;
                    require(ModSpells::ExtendedElementalPortion(id,100,duration,ap),"Extended attack not handled");
                    require(ap.fpDamage==(duration>1?20.f:100.f)&&!ap.bAreaEffect&&ap.fpToHit==FLT_MAX&&ap.fBackStabMultiplier==1&&!ap.bCalledShot,"Extended damage source mismatch");
                    if(duration>1)require(ap.fpDamagePercentage==1,"Source 5/3 integer arithmetic lost");
                    for(int i=0;i<RPG_NUM_ATTACK_TYPES;++i)require(ap.fpAttacks[i]==(i==element?1.f:0.f),"Extended element shares mismatch");
                }
            }
            SRPGAttackPortion untouched;untouched.fpDamage=123;require(!ModSpells::ExtendedElementalPortion(SPELL_ID_FIREBALL,100,20,untouched)&&untouched.fpDamage==123,"Base spell stolen by extended path");
            require(ModSpells::ExtendedElementalPortion(SPELL_ID_VAMP,100,20,untouched)&&untouched.fpDamage==0,"Invented generic Vamp damage");
            require(ModSpells::HasMana(10,10)&&ModSpells::HasMana(9.99995f,10)&&!ModSpells::HasMana(9.999f,10),"AI mana tolerance mismatch");
            require(ModSpells::ManaCost(20,.5f)==10&&ModSpells::ManaCost(-20,.5f)==0,"AI mana bonus/clamp mismatch");
            CUnitServer server;EffectUnit actor;actor.Bind(&server);server.RegisterUnit(&actor);
            struct UnbindMagic{CUnitServer&s;~UnbindMagic(){s.RegisterUnit(nullptr);}} unbind{server};
            *server.GetRPGStats()={};actor.SetFlag(UNIT_FEEBLED);server.SyncNpcMagicState();require(!actor.CheckFlag(UNIT_FEEBLED),"Stale NPC Feeble flag survived");
            actor.Power(SPELL_ID_FEEBLEMIND,1);server.SyncNpcMagicState();require(actor.CheckFlag(UNIT_FEEBLED),"NPC Feeble effect ignored");
            server.GetRPGStats()->rpgNAWWeaponType=RPGWST_MAGIC;server.SyncNpcMagicState();require(!actor.CheckFlag(UNIT_FEEBLED),"Natural magic exemption lost");
        }
        // R38: native constructor with controlled values, installed DB identity and native manager.
        {
            require(package.CombatSetting("NewRunes")!=0,"Installed new-rune policy disabled");
            CSpellManager manager;SSDBSpellPrototype prototype=*theSpellDatabase.spells[0];
            prototype.dwActions=60;prototype.fpEffect=100;prototype.dwDuration=101;prototype.fpMana=10;prototype.fpPrice=20;prototype.dwSlots=1;prototype.dwComplexity=2;
            struct RuneCase{const char*code;float value;DWORD actions,duration;float power;DWORD flags;};
            const RuneCase cases[]={{"xa",50,22,101,100,0},{"xb",.5f,30,101,100,0},{"xt",0,60,50,25,SPELL_ITEM_TRIGGER},{"xc",0,60,50,25,SPELL_ITEM_CONTINUOUS},{"xd",0,60,50,100,0},{"it",0,60,75,25,SPELL_ITEM_TRIGGER},{"ic",0,60,75,25,SPELL_ITEM_CONTINUOUS},{"fu",0,60,101,100,SPELL_FILTER_UNDEAD}};
            for(auto&tc:cases){
                SSDBModifier rune={};strcpy(rune.pszCode,tc.code);rune.fpValue=tc.value;rune.fpMana=3;rune.fpPrice=5;rune.dwComplexity=4;
                CSmartPtr<CSpellContainer> container=manager.CreateSpellContainer();container->SetPrototype(&prototype);container->AddRune(&rune);require(container->Recalculate()!=FALSE,"Native rune recalc failed");
                const CSpell&spell=container->GetSpell();require(spell.GetActionPoints()==tc.actions&&spell.GetDuration()==tc.duration&&spell.GetPower()==tc.power,"Native extended rune value mismatch");
                require((spell.GetResolve()&(SPELL_ITEM_TRIGGER|SPELL_ITEM_CONTINUOUS|SPELL_FILTER_UNDEAD))==tc.flags,"Native rune flags mismatch");
                require(spell.GetNumSlots()==2&&spell.GetComplexity()==6&&spell.GetManaCost()==13&&spell.GetPrice()==25,"Extended rune skipped common bookkeeping");
            }
            require(ModSpells::ExcludesUndead(SPELL_FILTER_UNDEAD,"unhusk")&&ModSpells::ExcludesUndead(SPELL_FILTER_UNDEAD,"unhuzf")&&ModSpells::ExcludesUndead(SPELL_FILTER_UNDEAD,"unhuzm")&&!ModSpells::ExcludesUndead(SPELL_FILTER_UNDEAD,"unhuma")&&!ModSpells::ExcludesUndead(0,"unhusk"),"Undead filter mismatch");
            CSpell spell;spell.Setup(&prototype);CUnitServer server;auto&st=*server.GetRPGStats();st={};st.action.fpEffectiveValue=15;st.hls[0].state=RPGALS_ACTIVE;st.hls[0].fpHP=st.hls[0].fpMaxHP=100;
            require(ModSpells::CastTime(st,&spell,false,theSettings.nDifficultyLevel)==60,"Shared cast time mismatch");st.hls[0].fpHP=50;require(ModSpells::CastTime(st,&spell,false,theSettings.nDifficultyLevel)==75,"Head injury cast time");
            require(ModSpells::CastTime(st,&spell,true,1)==60&&ModSpells::CastTime(st,&spell,true,0)==75,"Player difficulty cast time");
            st.cPerks[21]=1;auto*perk=st.GetPerk(21);require(perk&&perk->nPerkExclusive==21,"Installed Caster perk identity");
            require(ModSpells::CastTime(st,&spell,false,theSettings.nDifficultyLevel)==Float2Int(75*(1-perk->dwModifier*.01f)),"Caster perk not applied");
            st.cPerks[21]=0;st.action.fpEffectiveValue=0;require(ModSpells::CastTime(st,&spell,false,theSettings.nDifficultyLevel)==0&&ModSpells::CastTime(st,nullptr,false,theSettings.nDifficultyLevel)==0,"Zero/null cast time");
            require(ModSpells::ItemCanCast(10,10)&&!ModSpells::ItemCanCast(9.99f,10),"Continuous item exact charge boundary");
            require(ModSpells::KillExperience(1,1)==3&&ModSpells::KillExperience(7.9f,2)==15&&ModSpells::DisplayExperience(7.9f)==7&&ModSpells::DisplayExperience(-7.9f)==-7,"Experience truncation/minimum");
            SRPGUnitStats earned={};earned.psIntelligence.fpEffectiveValue=25;earned.fpExperience=100;earned.fExpPenalty=-50;float multiplier=earned.GetExpMultiplier();earned.GainExperience(1);
            require(earned.fpExperience==100&&fabs(earned.fExpPenalty-(-50+multiplier))<.0001f,"Experience penalty consumption");
            earned.GainExperience(100);require(fabs(earned.fpExperience-(100-50+101*multiplier))<.001f&&earned.fExpPenalty==0,"Experience native award path");
        }
        // R37: actual party/item owners, including stale grant marker recovery.
        {
            CParty party;require(party.Create()!=FALSE,"Skill grant party setup");
            int expected=0;for(const char*name:{"powerhit","doubleshot","preciseshot","precisehit","cripplinghit","explosiveshot"}){ModSkills::Config c;require(package.SkillConfig(name,c),"Skill grant config");expected+=c.grantPrototype?1:0;}
            package.GrantSkillPrototypes(party);require(party.allWagonItems.GetSize()==expected&&expected>0,"Skill grants not stored in wagon");
            package.GrantSkillPrototypes(party);require(party.allWagonItems.GetSize()==expected,"Duplicate skill grants");
            for(int i=0;i<party.allWagonItems.GetSize();++i){auto*item=party.allWagonItems[i];ModSkills::Config c;require(package.SkillConfig(item->GetSpell(0),c),"Granted wrong prototype");
                require(package.SkillItemBlocked(item,0)==!c.allowDeconstruction&&package.SkillItemBlocked(item,1)==!c.allowItemInsertion&&package.SkillItemBlocked(item,2)==!c.allowSale,"Skill item restrictions mismatch");
            }
            auto*lost=party.allWagonItems[0];party.RemoveItemFromWagon(lost,true);package.GrantSkillPrototypes(party);require(party.allWagonItems.GetSize()==expected,"Grant marker prevented missing item recovery");
            require(!package.SkillItemBlocked(nullptr,0)&&!package.SkillContainerBlocked(nullptr,2),"Null restriction admission");
            require(package.SkillContainerBlocked(&party,0)&&package.SkillContainerBlocked(&party,1),"Container restriction owner not connected");
        }
        // R31: real installed Lua callbacks through native combat entry points.
        {
            CUnitServer specific;EffectUnit actor;actor.Bind(&specific);specific.RegisterUnit(&actor);actor.SetSubtypeID(0);actor.SetID(1000000001);actor.SelectWeapon(-1);
            struct UnbindCombat{CUnitServer&s;~UnbindCombat(){s.RegisterUnit(nullptr);}} unbindCombat{specific};
            auto& live=*specific.GetRPGStats();live={};live.psStrength={25,25};live.psDexterity={25,25};live.psIntelligence={25,25};live.action={10,10};live.fpNAWActionPoints=30;live.fpMaxEncumbrance=100;live.fpNAWDamageMin=10;live.fpNAWDamageRnd=5;live.fpNAWToHit=20;live.fpNAWParry=20;
            live.cAutoSkills[RPGASKL_MELEE]=25;live.cAutoSkills[RPGASKL_ARCHERY]=25;
            auto& client=*const_cast<SRPGUserUnitStats*>(actor.GetUserUnitStats());client={};client.wEncumbrance=100;client.cStrength=25;client.cDexterity=25;client.cIntelligence=25;client.cNAWDamageMin=10;client.cNAWDamageRnd=5;client.cNAWToHit=20;client.cNAWParry=20;client.cAutoSkills[RPGASKL_MELEE]=25;client.cAutoSkills[RPGASKL_ARCHERY]=25;
            for(int i=0;i<RPG_NUM_HIT_LOCATIONS;i++){auto&h=live.hls[i];h.state=RPGALS_ACTIVE;h.hlt=RPGHLT_BODY;h.fpHP=h.fpMaxHP=100;client.hls[i].hlt=RPGHLT_BODY;client.hls[i].wHP=client.hls[i].wMaxHP=100;}
            require(package.AttackCycle(live,actor,true)==45&&package.AttackCycle(live,actor,false)==15,"Native unarmed cycle contract");
            float plain=package.ModifiedDamage(live,actor,10,0);require(fabs(plain-10*(1+theSettings.nDifficultyLevel*.25f))<.0001f,"Unarmed reference damage formula");
            SAttackParams serverAttack={},clientAttack={};package.AttackParams(live,actor,serverAttack,0);package.AttackParams(client,actor,clientAttack);require(std::isfinite(serverAttack.fDamageMax)&&std::isfinite(clientAttack.fDamageMax),"Native combat callback failed");
            for(int type:{int(RPGWST_SWORD),int(RPGWST_BOW),int(RPGWST_HANDS3),int(RPGWST_HANDS4)}){
                int shape=-1;for(int i=0;i<theItemDatabase.weapons.GetNumEntries();i++)if(theItemDatabase.weapons[i]->dwTypeID==type){shape=i;break;}require(shape>=0,"Combat weapon fixture missing");
                CItemWeapon weapon;weapon.SetTypeID(ITID_ITEM_WEAPON);weapon.SetSubtypeID(DWORD(shape)<<16);weapon.AddRef();int slot=actor.AddWeapon(&weapon);actor.SelectWeapon(slot);
                struct RemoveCombatWeapon{EffectUnit&u;CItemWeapon&w;int slot;~RemoveCombatWeapon(){u.RemoveWeapon(slot);w.DecRefCount();u.SelectWeapon(-1);}} removeCombatWeapon{actor,weapon,slot};
                SRPGAttackPortion incoming;incoming.Clear();incoming.hl=RPGHL_BODY;incoming.fpDamage=10;incoming.fBackStabMultiplier=1;incoming.fpAttacks[RPGAT_SLASHING]=1;auto defenseLua=package.IncomingAttack(live,actor,nullptr,incoming,99);require(std::isfinite(defenseLua.block)&&std::isfinite(defenseLua.parryChance),"Weapon defense Lua contract");
                SRPGAttackPortion other=incoming,flying=incoming;
                package.RememberAttack(live,actor,incoming,-2);package.RememberAttack(live,actor,other,0);
                require(package.ConsumeCritical(incoming)&&!package.ConsumeCritical(other),"Equal-damage attacks exchanged critical state");
                package.RememberAttack(live,actor,incoming,-2);package.MoveAttack(incoming,flying);
                package.RememberAttack(live,actor,incoming,0);actor.SelectWeapon(-1);
                require(package.AttackWeapon(flying).get()==&weapon,"Projectile lost its launch weapon on equipment change");
                require(package.ConsumeCritical(flying)&&!package.ConsumeCritical(flying)&&!package.ConsumeCritical(incoming),"Projectile reuse or duplicate critical delivery");actor.SelectWeapon(slot);
                package.RememberAttack(live,actor,incoming,-2);package.ForgetAttack(incoming);
                require(!package.ConsumeCritical(incoming)&&!package.AttackWeapon(incoming),"Cancelled attack retained metadata");
                package.RememberAttack(live,actor,incoming,-2);package.RememberAttack(live,actor,flying,0);package.MoveAttack(incoming,flying);package.MoveAttack(flying,flying);
                require(package.ConsumeCritical(flying)&&!package.ConsumeCritical(incoming),"Moved attack retained source or lost destination metadata");
                require(package.AttackSlot(actor,false)==slot,"Primary weapon slot changed");
                // Native server/client admission uses actual equipped objects and installed prototypes.
                int skillIndex=theSpellDatabase.spells.GetEntryIndex(type==RPGWST_BOW?"preciseshot":"precisehit");
                require(skillIndex>=0,"Skill prototype missing");CSpell activeSkill;activeSkill.SetSubtypeID(DWORD(skillIndex)<<16);activeSkill.Setup(theSpellDatabase.spells[skillIndex]);
                ModSkills::Config activeConfig;require(package.SkillConfig(&activeSkill,activeConfig),"Skill config through prototype failed");
                BYTE previousMelee=live.cAutoSkills[RPGASKL_MELEE],previousArchery=live.cAutoSkills[RPGASKL_ARCHERY];
                BYTE previousClientMelee=client.cAutoSkills[RPGASKL_MELEE],previousClientArchery=client.cAutoSkills[RPGASKL_ARCHERY];
                live.cAutoSkills[RPGASKL_MELEE]=live.cAutoSkills[RPGASKL_ARCHERY]=60;client.cAutoSkills[RPGASKL_MELEE]=client.cAutoSkills[RPGASKL_ARCHERY]=60;
                bool expectedSkill=type==RPGWST_SWORD||type==RPGWST_BOW||type==RPGWST_HANDS4;
                require(package.SkillReady(actor,&activeSkill,activeConfig,true)==expectedSkill&&package.SkillReady(actor,&activeSkill,activeConfig,false)==expectedSkill,"Native server/client weapon admission mismatch");
                auto autoSkill=activeConfig.requiredArchery>=0?RPGASKL_ARCHERY:RPGASKL_MELEE;BYTE savedSkill=live.cAutoSkills[autoSkill];live.cAutoSkills[autoSkill]=0;
                require(!package.SkillReady(actor,&activeSkill,activeConfig,true),"Under-skilled server admitted");live.cAutoSkills[autoSkill]=savedSkill;
                live.cAutoSkills[RPGASKL_MELEE]=previousMelee;live.cAutoSkills[RPGASKL_ARCHERY]=previousArchery;client.cAutoSkills[RPGASKL_MELEE]=previousClientMelee;client.cAutoSkills[RPGASKL_ARCHERY]=previousClientArchery;
                package.RememberAttack(live,actor,incoming,-2);package.CopyAttack(incoming,other);package.MoveAttack(incoming,flying);
                require(package.ConsumeCritical(other)&&package.ConsumeCritical(flying)&&!package.ConsumeCritical(incoming),"Double shot did not independently retain critical ownership");package.ForgetAttack(other);package.ForgetAttack(flying);
                // R34: call native delivery owners; world motion/rendering are outside this headless check.
                struct ImpactTarget:EffectUnit{
                    CModPackage&mod;int calls=0;float damage=0;bool critical=false;CItemWeapon*launch=nullptr;
                    explicit ImpactTarget(CModPackage&m):mod(m){rpgUserCommonStats.nHP=100;}
                    void Health(int hp){rpgUserCommonStats.nHP=hp;}
                    BOOL ProcessAttackPortion(SRPGAttackPortion*ap,CUnit*,CUnit*)override{calls++;damage=ap->fpDamage;launch=mod.AttackWeapon(*ap).get();critical=mod.ConsumeCritical(*ap);return TRUE;}
                } target(package);
                struct ArrivedArrow:CEffectArrowFigure{void MarkArrival(){bDelivered=TRUE;}} arrow;
                arrow.attackPortion=incoming;package.RememberAttack(live,actor,incoming,-2);package.MoveAttack(incoming,arrow.attackPortion);
                arrow.MarkArrival();require(arrow.DeliverAttack(&target,&actor)&&target.calls==1&&target.critical,"Arrival before Segment lost arrow impact");
                require(!arrow.DeliverAttack(&target,&actor)&&arrow.Segment()==0&&target.calls==1,"Arrow duplicate manager tick delivered twice");
                CEffectArrow staff;staff.SetCP(target.GetCP());package.RememberAttack(live,actor,incoming,-2);staff.SetWeaponAttack(incoming);
                float expected=incoming.fpDamage;incoming.fpDamage=777;actor.SelectWeapon(-1);
                require(staff.DeliverWeaponAttack(&target,&actor)&&target.calls==2&&target.damage==expected&&target.critical&&target.launch==&weapon,"Staff flight lost copied portion or launch weapon");
                require(!staff.DeliverWeaponAttack(&target,&actor)&&target.calls==2,"Staff double impact");incoming.fpDamage=expected;actor.SelectWeapon(slot);
                package.RememberAttack(live,actor,incoming,-2);staff.SetWeaponAttack(incoming);require(!staff.DeliverWeaponAttack(nullptr,&actor)&&!staff.DeliverWeaponAttack(&target,&actor),"Removed staff target retained pending hit");
                package.RememberAttack(live,actor,incoming,-2);staff.SetWeaponAttack(incoming);staff.Clear();require(!staff.DeliverWeaponAttack(&target,&actor),"Cleared staff retained hit");
                package.RememberAttack(live,actor,incoming,-2);staff.SetWeaponAttack(incoming);target.Health(0);require(!staff.DeliverWeaponAttack(&target,&actor),"Staff hit zero-HP target");target.Health(100);
                package.RememberAttack(live,actor,incoming,-2);staff.SetWeaponAttack(incoming);staff.SetCP(target.GetCP()+CPlot3D(2,0,0));require(!staff.DeliverWeaponAttack(&target,&actor),"Staff ignored impact distance");

                {CEffectArrowFigure arrow;arrow.attackPortion=incoming;package.RememberAttack(live,actor,incoming,-2);package.MoveAttack(incoming,arrow.attackPortion);arrow.Clear();require(!package.AttackWeapon(arrow.attackPortion)&&!package.ConsumeCritical(arrow.attackPortion),"Native arrow clear retained attack");}
                if(type==RPGWST_SWORD){CItemWeapon left;left.SetTypeID(ITID_ITEM_WEAPON);left.SetSubtypeID(DWORD(shape)<<16);left.AddRef();int leftSlot=actor.AddWeapon(&left);RemoveCombatWeapon removeLeft{actor,left,leftSlot};
                    require(package.AttackSlot(actor,true)==leftSlot,"Left hand selected primary weapon");
                    require(package.DualAnimation(actor)=="dat01sws44","Twin sword animation selection failed");
                    package.RememberAttack(live,actor,incoming,-2,true);require(package.AttackWeapon(incoming).get()==&left,"Offhand attack captured wrong weapon");package.ForgetAttack(incoming);
                    require(std::isfinite(package.ModifiedDamage(live,actor,10,1,leftSlot,true)),"Offhand Lua damage failed");
                    actor.SelectWeapon(leftSlot);require(package.AttackSlot(actor,true)==slot,"Swapped hands lost secondary weapon");
                }
                actor.SelectWeapon(slot);
                for(float roll:{0.f,1.f,99.f}){float damage=package.ModifiedDamage(live,actor,10,roll);require(std::isfinite(damage)&&damage>=1,"Invalid real weapon damage");package.AttackParams(live,actor,serverAttack,roll);package.AttackParams(client,actor,clientAttack);require(serverAttack.rpgWeaponType==type&&clientAttack.rpgWeaponType==type,"Weapon type lost by combat bridge");}
            }
            {const char*names[]={"dat01crcr37","dat01dagg75","dat01sws44","dat01axes42","dat01hams24","dat01axes37","dat01sprs37","dat01crst37","dat01stcr37","dat01st237"};
             const int lengths[]={37,75,44,42,24,25,37,37,37,37};
             const std::vector<std::vector<int>> expected={{12,24},{15,26,35,44,53,65},{15,16,34,35},{15,16,29,32},{15,16},{15,16},{11,23},{11,24},{11,24},{15,23}};
             for(int i=0;i<10;i++){auto strikes=package.DualStrikes(names[i],lengths[i]);require(strikes.size()==expected[i].size(),"Dual strike count differs from installed rows");
               for(size_t j=0;j<strikes.size();j++)require(strikes[j].first==expected[i][j]&&strikes[j].second==(j%2!=0),"Dual frame/hand differs from installed rows");
               auto fast=package.DualStrikes(names[i],1);require(fast.size()==strikes.size(),"Compressed animation discarded coincident strikes");
               for(auto hit:fast)require(hit.first==0,"Reference float timing truncation changed");
             }
             require(package.DualStrikes("unknown-animation",20).empty(),"Unknown dual animation synthesized");
             bool bad=false;try{package.DualStrikes(names[0],0);}catch(const std::runtime_error&){bad=true;}require(bad,"Zero animation duration admitted");}
            {CParticle particle;const ETypeIDs types[]={PTID_STAFFARROWFIRE,PTID_STAFFARROWLIGHT,PTID_STAFFARROWWATER,PTID_STAFFARROWACID,PTID_FIREARROW,PTID_RICKARROW};
             const char*names[]={"FireArrowSmoke","lightArrowSmoke","acidray","acidrayorig","FireArrowSmoke","RikArrowSmoke"};
             for(int i=0;i<6;i++)require(std::string(particle.ConfigureArrow(types[i]))==names[i],"Native arrow particle texture mismatch");
             require(particle.ConfigureArrow(TID_EMPTY)==nullptr,"Unknown arrow particle admitted");}
            // R32: invoke the actual native incoming damage owner without a world RNG/audio device.
            struct Feedback:ModCombat::Feedback {int draws=0,sounds=0;std::string last;int Random(int maximum)override{draws++;return maximum;}void Sound(CUnit*,const char*name)override{sounds++;last=name;}} feedback;
            SRPGCommonStats common={};common.ssHP.fpMax=common.ssHP.fpCurrent=100;
            auto resetHit=[&](){common.ssHP.fpCurrent=100;for(auto&a:common.psAbsorbtions)a={};live.bFlags=0;for(auto&h:live.hls){h={};h.state=RPGALS_ACTIVE;h.hlt=RPGHLT_BODY;h.fpHP=h.fpMaxHP=100;h.fpVitality=1.f/6;h.fpHPWeight=1;}feedback.sounds=0;feedback.last.clear();};
            SRPGAttackPortion hit;hit.Clear();hit.hl=RPGHL_BODY;hit.fBackStabMultiplier=1;hit.fpDamage=40;hit.fpAttacks[RPGAT_SLASHING]=1;
            ModCombat::Defense defense;
            resetHit();common.psAbsorbtions[RPGAT_SLASHING].fpEffectiveValue=10;require(live.ProcessModAttackPortion(&actor,&common,&hit,nullptr,nullptr,defense,&feedback)&&fabs(live.hls[RPGHL_BODY].fpHP-70)<.001f,"General absorption order mismatch");
            resetHit();defense.block=15;require(live.ProcessModAttackPortion(&actor,&common,&hit,nullptr,nullptr,defense,&feedback)&&live.hls[RPGHL_BODY].fpHP==75&&feedback.sounds==1&&(live.bFlags&RPGSTAT_BLOCK),"Physical block native path mismatch");
            resetHit();defense.parry=true;require(!live.ProcessModAttackPortion(&actor,&common,&hit,nullptr,nullptr,defense,&feedback)&&live.hls[RPGHL_BODY].fpHP==100&&(live.bFlags&RPGSTAT_PARRY),"Parried damage reached HP");
            resetHit();defense={};defense.critical=true;require(live.ProcessModAttackPortion(&actor,&common,&hit,nullptr,nullptr,defense,&feedback)&&(live.bFlags&RPGSTAT_CRIT)&&feedback.sounds==1,"Applied critical feedback missing");
            resetHit();common.psAbsorbtions[RPGAT_SLASHING].fpEffectiveValue=100;require(!live.ProcessModAttackPortion(&actor,&common,&hit,nullptr,nullptr,defense,&feedback)&&feedback.sounds==0&&!(live.bFlags&RPGSTAT_CRIT),"Absorbed hit produced critical feedback");
            resetHit();defense={};live.hls[RPGHL_LHAND].state=RPGALS_CRIPPLED;hit.hl=RPGHL_LHAND;require(live.ProcessModAttackPortion(&actor,&common,&hit,nullptr,nullptr,defense,&feedback)&&live.hls[RPGHL_BODY].fpHP==60&&live.hls[RPGHL_LHAND].fpHP==100,"Inactive limb not redirected to body");
            resetHit();hit.hl=RPGHL_BODY;live.hls[RPGHL_BODY].layers[2].state=RPGALS_ACTIVE;live.hls[RPGHL_BODY].layers[2].fpAbsorbtions[RPGAT_SLASHING]=12;require(live.ProcessModAttackPortion(&actor,&common,&hit,nullptr,nullptr,defense,&feedback)&&live.hls[RPGHL_BODY].fpHP==72,"Armor layer reduction mismatch");
            resetHit();hit.hl=RPGHL_ALL;hit.fpAttacks[RPGAT_SLASHING]=0;hit.fpAttacks[RPGAT_TERMICAL]=1;defense.block=10;require(live.ProcessModAttackPortion(&actor,&common,&hit,nullptr,nullptr,defense,&feedback)&&fabs(live.hls[RPGHL_BODY].fpHP-65)<.001f,"Distributed magic half block mismatch");
            resetHit();defense={};hit.hl=RPGHL_BODY;hit.fpAttacks[RPGAT_TERMICAL]=0;hit.fpAttacks[RPGAT_SLASHING]=1;
            struct WearWeapon:CItemWeapon{WearWeapon(){fpHP=fpMaxHP=1000;SetCount(1);}} wearWeapon;
            int wearShape=-1;for(int i=0;i<theItemDatabase.weapons.GetNumEntries();i++)if(theItemDatabase.weapons[i]->dwTypeID==RPGWST_SWORD){wearShape=i;break;}require(wearShape>=0,"Wear fixture missing");wearWeapon.SetTypeID(ITID_ITEM_WEAPON);wearWeapon.SetSubtypeID(DWORD(wearShape)<<16);
            common.psAbsorbtions[RPGAT_SLASHING].fpEffectiveValue=10;require(live.ProcessModAttackPortion(&actor,&common,&hit,&wearWeapon,nullptr,defense,&feedback)&&wearWeapon.GetHP()==960,"Weapon wear must use full incoming damage once");
            resetHit();hit.hl=RPGHL_BODY;hit.fpDamage=120;live.hls[RPGHL_BODY].dwCrippleMask=1<<RPGAT_SLASHING;require(live.ProcessModAttackPortion(&actor,&common,&hit,nullptr,nullptr,defense,&feedback)&&live.hls[RPGHL_BODY].state==RPGALS_CRIPPLED&&live.hls[RPGHL_BODY].fpHP==-1000,"Reference cripple threshold mismatch");
            resetHit();hit.fpDamage=40;
            require(ModCombat::Passed(40,3,10,true,true)==35&&ModCombat::Passed(40,0,10,true,true)==0,"Magic parry policy mismatch");
            require(fabs(ModCombat::ParryUnion(30,20)-38)<.001f&&fabs(ModCombat::ParryUnion(30,0)-.3f)<.001f,"Reference dual-parry branches changed");
            resetHit();hit.hl=RPGHL_UNKNOWN;bool invalidHit=false;try{live.ProcessModAttackPortion(&actor,&common,&hit,nullptr,nullptr,defense,&feedback);}catch(const std::runtime_error&){invalidHit=true;}require(invalidHit&&common.ssHP.fpCurrent==100,"Invalid location changed HP");
            hit.hl=RPGHL_BODY;defense=package.IncomingAttack(live,actor,nullptr,hit,99);require(std::isfinite(defense.block)&&std::isfinite(defense.parryChance),"Installed defense Lua failed");
            run("__combat_damage=GetModifiedDamageOnServerJmv; __combat_rolls={}; GetModifiedDamageOnServerJmv=function(s,u,d,roll,...) __combat_rolls[#__combat_rolls+1]=roll; return __combat_damage(s,u,d,roll,...) end");
            package.AttackParams(live,actor,serverAttack,37);
            run("GetModifiedDamageOnServerJmv=__combat_damage; __combat_damage=nil; assert(#__combat_rolls==2 and __combat_rolls[1]==37 and __combat_rolls[2]==37); __combat_rolls=nil");
            run("__combat_old=CalcAttackParamsOnServer; CalcAttackParamsOnServer=function(s,u,p,e) p=AttackParams(p); p.attack=123; error('combat rollback') end");
            SAttackParams sentinel={};sentinel.nAttack=77;bool rejected=false;try{package.AttackParams(live,actor,sentinel,0);}catch(const std::runtime_error&){rejected=true;}
            run("CalcAttackParamsOnServer=__combat_old; __combat_old=nil");require(rejected&&sentinel.nAttack==77,"Combat result escaped failed callback");
        }
        // Actual native RecalcStats callsite, with native ownership and real UDB/race records.
        CServerSpecificManager specificManager;specificManager.SetWorld(&theWorldServer);
        for(int i=0;i<theUnitDatabase.prototypes.GetNumEntries() && fullCases<8;i++){
            auto*prototype=theUnitDatabase.prototypes.GetEntry(i);std::string name=(LPCSTR)prototype->szName;
            // Use direct table lookup for positive XP and strength to select meaningful real NPC fixtures.
            lua_pushstring(lua,name.c_str());lua_setglobal(lua,"__r15_name");
            if(luaL_loadstring(lua,"local idx=require('table_indexes'); local rows=idx.UnitStat[__r15_name]; if not rows then return false end; local r=rows[#rows]; return (r[UnitStatType.Exp] or 0)>0 and (r[UnitStatType.StatStrength] or 0)>0")||lua_pcall(lua,0,1,0))throw std::runtime_error(lua_tostring(lua,-1));
            bool use=lua_toboolean(lua,-1)!=0;lua_pop(lua,1);if(!use)continue;
            CUnitServer specific;EffectUnit actor;actor.Bind(&specific);specific.RegisterUnit(&actor);specific.SetManager(&specificManager);actor.SetSubtypeID(i);actor.SetID(9000+fullCases);actor.SetManager(&specificManager);
            struct DetachOwner {CUnitServer&s;~DetachOwner(){s.SetManager(nullptr);s.RegisterUnit(nullptr);}} detachOwner{specific};
            auto&live=*specific.GetRPGStats();live={};live.fpExperience=2000;live.fBaseHP=100;live.psStrength={25,25};live.psDexterity={25,25};live.psIntelligence={25,25};live.move={10,10};live.fpMaxEncumbrance=300;
            for(auto&h:live.hls){h.state=RPGALS_ACTIVE;h.hlt=RPGHLT_BODY;h.fpHPWeight=1;h.fpVitality=1.f/RPG_NUM_HIT_LOCATIONS;h.fpHP=100;h.fpMaxHP=100;}
            for(int j=0;j<RPG_NUM_SENSES;j++){live.senses[j]={10,10};live.percepts[j]={1,1};}
            SRPGCommonStats result={};result.ssHP={100,100,1};result.ssMP={50,50,1};
            CItemArmor armorPieces[2];int slots[]={RPGAST_HELM,RPGAST_PLATE};
            struct DetachFullArmor {CUnit&u;CItemArmor*a;int count=0;~DetachFullArmor(){for(int j=0;j<count;j++){u.Remove(a[j].GetArmorType());a[j].DecRefCount();}}} detachFullArmor{actor,armorPieces};
            for(int j=0;j<2;j++){
                int index=-1;for(int k=0;k<theItemDatabase.armors.GetNumEntries();k++){auto*entry=theItemDatabase.armors.GetEntry(k);float total=0;for(float v:entry->main.fpDistributions)total+=v;if(entry->dwTypeID==slots[j]&&total>0){index=k;break;}}
                require(index>=0,"No nonempty armor location fixture");auto&a=armorPieces[j];a.SetTypeID(ITID_ITEM_ARMOR);a.SetSubtypeID(DWORD(index)<<16);a.Setup(theItemDatabase.armors.GetEntry(index));a.AddRef();actor.Wear(&a);detachFullArmor.count++;
                require(actor.GetQArmor(slots[j])==&a,"Full-recalc armor was not equipped");
            }
            // R25: execute native MOB queries against real server, prototype and worn armor.
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
                require(number(348,0,"tame")==10+RPGMSKL_TAME&&number(348,0,"TAME")==10+RPGMSKL_TAME,"Tame must read the raw manual skill, not autoSkills or intelligence bonus");
                auto string=[&](int id,int slot){SSCCmd cmd;cmd.command=id;cmd.oParams.Add(&object);if(id==377||id==378)cmd.params.Add(float(slot));CString value;cmd.CalcFunction(&context,vars,value);return value;};
                require(string(354,0)==prototype->szName&&string(355,0)==prototype->szRace,"Prototype/race query mismatch");
                for(int k=0;k<2;k++)require(string(378,slots[k])==armorPieces[k].GetShapeEntry()->szName,"Worn armor query mismatch");
                require(string(377,-1).IsEmpty(),"Missing weapon query mismatch");
                require(string(303,0)==std::to_string(actor.GetID()).c_str(),"Object string ID mismatch");
                auto query=[&](int id){SSCCmd cmd;cmd.command=id;cmd.oParams.Add(&object);float v=-1;cmd.CalcFunction(&context,vars,v);return v;};
                auto pose=specific.GetScriptPose();actor.LowSetPoses(pose==UP_STAND?UP_CRAWL:UP_STAND,UP_STAND);require(actor.GetCurrentPose()!=pose&&query(361)==pose,"Server pose authority mismatch");actor.LowSetPoses(pose,pose);
                require(query(366)==float(specific.logic.GetScriptState()),"Logic state query mismatch");
                actor.SetFlag(UNIT_STUN);require(query(369)==1,"Stun flag query mismatch");actor.ResetFlag(UNIT_STUN);require(query(369)==0,"Cleared stun flag mismatch");
                SSCCmd flag;flag.command=367;flag.oParams.Add(&object);flag.params.Add(31);actor.SetFlag(0x80000000u);float v=0;flag.CalcFunction(&context,vars,v);require(v==1&&query(358)==1,"Flag bit/high marker mismatch");actor.ResetFlag(0x80000000u);
                auto aggression=actor.GetAggression();SSCCmd change;change.command=382;change.oParams.Add(&object);change.params.Add(1);require(ModMob::Void(change)&&int(actor.GetAggression())==1,"Aggression action failed");actor.SetAggression(aggression);
                SSCCmd armor;armor.command=302;armor.oParams.Add(&object);armor.params.Add(0);armor.params.Add(0);live.hls[0].layers[0].fpAbsorbtions[0]=2;live.hls[0].layers[1].fpAbsorbtions[0]=3;live.hls[0].layers[2].fpAbsorbtions[0]=4;armor.CalcFunction(&context,vars,v);require(v==9,"Armor layers query mismatch");

            }
            SRPGCommonStats corrected={},legacy={};ModStats::Armor(corrected,actor,true);ModStats::Armor(legacy,actor,false);
            float head[RPG_NUM_ATTACK_TYPES],body[RPG_NUM_ATTACK_TYPES],hands[RPG_NUM_ATTACK_TYPES];armorPieces[0].GetAbsorbtions(head,0);armorPieces[1].GetAbsorbtions(body,0);armorPieces[1].GetAbsorbtions(hands,1);
            auto*race=theUnitDatabase.races.GetEntry(prototype->dwRaceIndex);bool distinct=false;
            for(int type=0;type<RPG_NUM_ATTACK_TYPES;type++){float base=race->defences[type]*prototype->fpAbsorbtionMultiplier;float expected=base+head[type]*.1f+body[type]*.3f+hands[type]*.3f;
                require(fabs(corrected.psAbsorbtions[type].fpBaseValue-expected)<.0001 && fabs(legacy.psAbsorbtions[type].fpBaseValue-base)<.0001,"Armor location aggregation mismatch");distinct|=fabs(expected-base)>.0001;}
            require(distinct,"Armor test failed to distinguish corrected and legacy formulas");armorCases++;
            actor.Power(SPELL_ID_STRENGTHEN,20);actor.Power(SPELL_ID_WEAKEN,10);
            live.RecalcStats(&result,&actor);
            require(fabs(result.ssHP.fpMax-105)<.0001 && std::isfinite(result.ssMP.fpMax) && live.move.fpEffectiveValue>0,"Full native NPC HP/MP/movement mismatch");
            // R20: native callsite executes unchanged installed HP/MP Lua on real equipment.
            if(fullCases==0){
                auto savedStats=live;auto savedCommon=result;
                result.ssMP.fpCurrent=0;result.ssMP.fpRegenerate=.01f;result.nRegenerateCounter=0;
                for(int tick=1;tick<15;tick++)require(!result.Regenerate(live,&actor,false)&&result.nRegenerateCounter==tick&&result.ssMP.fpCurrent==0,"Regeneration waiting cadence mismatch");
                require(result.Regenerate(live,&actor,false)&&result.nRegenerateCounter==0&&result.ssMP.fpCurrent>0,"Due regeneration did not restore MP");
                result.ssHP.fpCurrent=0;result.nRegenerateCounter=14;
                require(!result.Regenerate(live,&actor,false)&&result.nRegenerateCounter==14,"Dead unit advanced regeneration");
                result=savedCommon;live=savedStats;result.ssHP.fpCurrent*=.5f;for(auto&h:live.hls)h.fpHP*=.5f;result.nRegenerateCounter=14;float beforeHp=result.ssHP.fpCurrent;
                require(result.Regenerate(live,&actor,false)&&result.ssHP.fpCurrent>beforeHp,"Installed HP regeneration failed");
                for(int counter: {-1,14,15,2147483647}){result=savedCommon;live=savedStats;result.ssMP.fpCurrent=0;result.nRegenerateCounter=counter;result.Regenerate(live,&actor,true);require(result.ssMP.fpCurrent>0&&result.nRegenerateCounter==0,"Serialized counter boundary mismatch");}
                result=savedCommon;live=savedStats;result.ssHP.fpCurrent=result.ssHP.fpMax;for(auto&h:live.hls)h.fpHP=h.fpMaxHP;live.RecalcStats(&result,&actor);result.ssMP.fpCurrent=0;result.nRegenerateCounter=14;
                require(!result.Regenerate(live,&actor,false)&&result.ssMP.fpCurrent>0,"MP-only regeneration did not skip full recalc");
                ModInitialization::Ledger proof;proof.Finish(actor,result,live);require(proof.Ready(actor,result,live),"Completed stats proof missing");
                float hp=result.ssHP.fpCurrent;result.ssHP.fpCurrent-=1;require(!proof.Ready(actor,result,live),"Changed HP retained proof");result.ssHP.fpCurrent=hp;
                auto changedStats=live;require(!proof.Ready(actor,result,changedStats),"Different stats identity retained proof");
                result.ssMP.fpCurrent-=1;result.nRegenerateCounter=7;require(proof.Ready(actor,result,live),"MP/counter invalidated completed proof");proof.Reset();require(!proof.Ready(actor,result,live),"Reset retained completed proof");
                ModInitialization::Inputs captured(result,live,actor);result.ssMP.fpCurrent+=1;require(captured.MpOnly(result,live,actor),"Transient MP-only comparison failed");live.move.fpEffectiveValue+=1;require(!captured.MpOnly(result,live,actor),"Changed stats bypassed rebuild");live=savedStats;
                // Actual installed MagicBlood callback transfers available MP into an item spell.
                {
                    CSpell spell;SSDBSpellPrototype spellPrototype={};spellPrototype.fpMana=20;spell.Setup(&spellPrototype);spell.AddRef();armorPieces[0].AddFreeSpell(&spell);
                    struct DetachSpell{CItem&item;CSpell&spell;~DetachSpell(){item.AddFreeSpell(nullptr);spell.DecRefCount();}} detachSpell{armorPieces[0],spell};
                    lua_getglobal(lua,"PerkType");lua_getfield(lua,-1,"MagicBlood");int perk=lua_tointeger(lua,-1);lua_pop(lua,2);require(perk>=0&&perk<RPG_NUM_PERKS,"Invalid MagicBlood perk");
                    live=savedStats;live.cPerks[perk]=1;result=savedCommon;result.ssHP.fpCurrent=result.ssHP.fpMax;result.ssMP.fpCurrent=12;result.ssMP.fpRegenerate=0;result.nRegenerateCounter=14;float oldMana=armorPieces[0].GetMP();armorPieces[0].SetMP(0);
                    result.Regenerate(live,&actor,false);require(fabs(armorPieces[0].GetMP()-12)<.0001&&result.ssMP.fpCurrent==0,"Installed item spell charge transfer mismatch");armorPieces[0].SetMP(oldMana);
                }
                // Failure after both resource and equipment writes must publish nothing.
                lua_getglobal(lua,"RegenerateHpAndMpJmvDue");int original=luaL_ref(lua,LUA_REGISTRYINDEX);
                run("RegenerateHpAndMpJmvDue=function(c,s,u) c.hp.curr=1; u:armor(0).mp=19; error('intentional regen failure') end");
                result=savedCommon;live=savedStats;result.nRegenerateCounter=14;auto before=result;float itemMp=armorPieces[0].GetMP();bool rejected=false;
                try{result.Regenerate(live,&actor,false);}catch(const std::runtime_error&){rejected=true;}
                lua_rawgeti(lua,LUA_REGISTRYINDEX,original);lua_setglobal(lua,"RegenerateHpAndMpJmvDue");luaL_unref(lua,LUA_REGISTRYINDEX,original);
                require(rejected&&memcmp(&result,&before,sizeof(result))==0&&memcmp(&live,&savedStats,sizeof(live))==0&&armorPieces[0].GetMP()==itemMp,"Regeneration failure partially committed");
                result=savedCommon;live=savedStats;
            }
            auto once=live;auto commonOnce=result;live.RecalcStats(&result,&actor);
            require(memcmp(&live,&once,sizeof(live))==0 && memcmp(&result,&commonOnce,sizeof(result))==0,"Full NPC recalc accumulates modifiers");fullCases++;
            // Failure occurs after the original callback has written its shared stunSpell upvalue.
            if(fullCases==1){
                lua_getglobal(lua,"GetLeftHand");int original=luaL_ref(lua,LUA_REGISTRYINDEX);
                struct RestoreLeft {lua_State*s;int ref;~RestoreLeft(){lua_rawgeti(s,LUA_REGISTRYINDEX,ref);lua_setglobal(s,"GetLeftHand");luaL_unref(s,LUA_REGISTRYINDEX,ref);}} restoreLeft{lua,original};
                lua_pushinteger(lua,actor.GetID());lua_setglobal(lua,"__r15_id");
                run("for i=1,20 do local name,t=debug.getupvalue(RecalcUnitStatsJmv,i); if name=='stunSpell' then __r15_stun=t; break end end; assert(__r15_stun); __r15_stun[__r15_id]=17; GetLeftHand=function() error('intentional late recalc failure') end");
                auto oldStats=live;auto oldCommon=result;float oldArmorRate=armorPieces[0].GetManaRegenRate();bool rejected=false;try{live.RecalcStats(&result,&actor);}catch(const std::runtime_error&){rejected=true;}
                require(rejected && memcmp(&live,&oldStats,sizeof(live))==0 && memcmp(&result,&oldCommon,sizeof(result))==0 && armorPieces[0].GetManaRegenRate()==oldArmorRate,"Full stats failure partially commits");
                run("assert(__r15_stun[__r15_id]==17); __r15_stun[__r15_id]=nil; __r15_stun=nil; __r15_id=nil");stunRollback=true;
            }
            actor.SetID(1000000000+fullCases);live.RecalcStats(&result,&actor);
            float expectedHero=live.RecalcHP()*1.05f*(1+theSettings.nDifficultyLevel*.25f);
            require(fabs(result.ssHP.fpMax-expectedHero)<.001f && result.ssHP.fpCurrent>0 && live.fpVisionArc==-2 && live.fpSpeeds[0]>0,"Full hero HP/proportions/vision/speed mismatch");fullHeroCases++;

        }
        run("__r15_name=nil");require(fullCases==8,"Not enough full NPC fixtures");
        // Actual native hero recalculation, with live perk DB and injured limb states.
        for(float ratio : {0.0f,0.25f,0.75f}) {
            SRPGUnitStats hero={};hero.fpExperience=2000;hero.psStrength={25,999};hero.psDexterity={25,999};hero.psIntelligence={25,999};hero.move={10,999};
            hero.cPerks[0]=1;hero.cPerks[10]=1;hero.cPerks[17]=1;
            float strength=25+hero.GetPerkModifier(0),dexterity=25+hero.GetPerkModifier(10),intelligence=25+hero.GetPerkModifier(17);
            SRPGUnitStats expected=hero;expected.psStrength.fpEffectiveValue=strength;
            float maxHP=expected.RecalcHP();
            for(int i=0;i<RPG_NUM_HIT_LOCATIONS;i++){auto&h=hero.hls[i];h.state=RPGALS_ACTIVE;h.hlt=i==0?RPGHLT_HAND:(i==1?RPGHLT_LEG:RPGHLT_BODY);h.fpHPWeight=1;h.fpVitality=1.0f/RPG_NUM_HIT_LOCATIONS;h.fpMaxHP=maxHP;h.fpHP=maxHP*(i<2?ratio:1);}
            for(int i=0;i<RPG_NUM_SENSES;i++){hero.senses[i]={float(i+1),999};hero.percepts[i]={float(i+2),999};}
            hero.RecalcInternalHeroStats(42);
            float damage=ratio<=0?.33f:ratio<=.5f?.5f:1;
            require(hero.psStrength.fpEffectiveValue==strength && hero.psDexterity.fpEffectiveValue==dexterity && hero.psIntelligence.fpEffectiveValue==intelligence,"Hero perk stat reset mismatch");
            require(hero.fpCurrEncumbrance==42 && hero.fpMaxEncumbrance==strength*12 && hero.fpVisionArc==-2,"Hero encumbrance/vision mismatch");
            require(fabs(hero.move.fpEffectiveValue-10*damage)<.0001 && fabs(hero.action.fpEffectiveValue-(10+dexterity*.2f)*damage)<.0001,"Hero injured limb action/movement mismatch");
            for(int i=0;i<RPG_NUM_SENSES;i++)require(hero.senses[i].fpEffectiveValue==i+1 && hero.percepts[i].fpEffectiveValue==i+2,"Hero senses reset mismatch");
            require(fabs(hero.hls[0].fpMaxHP-maxHP)<.0001,"Hero native HP distribution mismatch");
            SRPGUnitStats empty=hero;for(auto&h:empty.hls)h.Clear();SRPGUnitStats beforeEmpty=empty;
            empty.RecalcInternalHeroStats(42);
            require(memcmp(empty.hls,beforeEmpty.hls,sizeof(empty.hls))==0,"Unused hit locations changed during HP distribution");
            hero.hls[RPGHL_BODY].fpHPWeight=0;float prior=hero.psStrength.fpEffectiveValue;hero.psStrength.fpBaseValue=999;
            bool invalid=false;try{hero.RecalcInternalHeroStats(1);}catch(const std::runtime_error&){invalid=true;}
            require(invalid && hero.psStrength.fpEffectiveValue==prior,"Invalid HP distribution did not roll back hero changes");
        }
        SpendRunningMana(stats,unit,common);
        require(fabs(common.ssMP.fpCurrent-(.1-pow(100,.7)*.00715))<.0001 && stats.move.fpEffectiveValue==1,"Movement callsite writeback mismatch");
        bool nestedRejected=false;try{Session duplicate(package);}catch(const std::runtime_error&){nestedRejected=true;}
        require(nestedRejected && Current()==&package,"Nested session ownership failed");

        // Restore exact Lua globals even if a negative case throws.
        struct Restore {
            lua_State *s;const char *name;int ref;
            Restore(lua_State *state,const char *n):s(state),name(n){lua_getglobal(s,name);ref=luaL_ref(s,LUA_REGISTRYINDEX);}
            ~Restore(){lua_rawgeti(s,LUA_REGISTRYINDEX,ref);lua_setglobal(s,name);luaL_unref(s,LUA_REGISTRYINDEX,ref);}
        } restoreRun(lua,"CalcRunningManaCost"), restoreHP(lua,"CalcBaseHealth"), restoreModifiedHP(lua,"GetModifiedHealth");
        // Sentinel tests cover commit/rollback even when the loaded DB has no nonzero jewelry item.
        run("GetModifiedHealth=function(s,u,hp) s.strength:addCurr(2); return hp+3 end");
        SRPGUnitStats mutation=stats;require(mutation.GetModifiedHealth(&unit,100)==103 && mutation.psStrength.fpEffectiveValue==52,"Modified HP lost primary mutation");
        run("GetModifiedHealth=function(s,u,hp) s.strength:addCurr(2); error('intentional modified HP failure') end");
        bool modifiedFailed=false;try{mutation.GetModifiedHealth(&unit,100);}catch(const std::runtime_error&){modifiedFailed=true;}
        require(modifiedFailed && mutation.psStrength.fpEffectiveValue==52,"Modified HP failure partially committed stats");
        run("CalcRunningManaCost=function(s,u,c) s.move.curr=999; c.mp.curr=999; error('intentional callback failure') end");
        float oldMP=common.ssMP.fpCurrent,oldMove=stats.move.fpEffectiveValue;
        bool failed=false;try{SpendRunningMana(stats,unit,common);}catch(const std::runtime_error&){failed=true;}
        require(failed && common.ssMP.fpCurrent==oldMP && stats.move.fpEffectiveValue==oldMove,"Failed callback mutated live stats or silently fell back");
        run("CalcBaseHealth=function(s) __dispatch_saved=s; s.strength.curr=999; return 0/0 end");
        failed=false;try{stats.RecalcHP();}catch(const std::runtime_error&){failed=true;}
        require(failed && stats.psStrength.fpEffectiveValue==50,"Invalid numeric callback changed const stats");
        run("assert(not pcall(function() return __dispatch_saved.strength.curr end)); __dispatch_saved=nil");
    }
    require(Current()==nullptr,"Session outlived its package binding");
    common.ssMP={100,100,0}; SpendRunningMana(stats,unit,common);
    require(fabs(common.ssMP.fpCurrent-(100-100.0/150))<.0001,"Base movement path changed without package session");
    return {{"extendedSpellNativePhases",true},{"extendedParticleConfigurations",18},{"extendedOneShotLifetimes",8},{"extendedElementalNativeCases",18},{"extendedDamageSharedCases",54},{"npcFeebleNativeState",true},{"npcManaSharedBoundaries",true},{"extendedRuneNativeCases",8},{"spellCastTimingShared",true},{"undeadRuneFilter",true},{"continuousManaBoundary",true},{"experienceAwardAndDisplay",true},{"foundationLoopArguments",true},{"foundationVisitLifecycle",true},{"foundationSpellSlots",true},{"foundationTame",true},{"combatInstalledLua",true},{"combatSharedRoll",true},{"combatRollback",true},{"combatWeaponClasses",4},{"combatAttackCycle",true},{"attackOwnershipEqualDamage",true},{"attackOwnershipMoveReuseCancel",true},{"attackLaunchWeaponRetained",true},{"explosiveFlightReliefTimeout",true},{"explosiveFireConversion",true},{"crippledAddRefreshCapacity",true},{"crippledClientPrototypeSpeed",true},{"skillImpactSingleDelivery",true},{"powerHitContactConfiguration",true},{"skillGrantIdempotenceRecovery",true},{"skillItemRestrictions",true},{"weaponSkillInstalledPolicies",6},{"weaponSkillCooldownBoundaries",true},{"weaponSkillNativeAdmission",true},{"doubleShotIndependentOwnership",true},{"weaponSkillCommandBounds",true},{"dualInstalledRows",10},{"dualCompressedCoincidentStrikes",true},{"dualSwordAnimationSelection",true},{"staffOwnedPayloadDelivery",true},{"staffCancelledMissingDeadDistant",true},{"arrowArrivalBeforeSegment",true},{"arrowNativeParticleConfiguration",6},{"attackNativeArrowClear",true},{"attackOffhandSelection",true},{"incomingDamageNative",true},{"incomingProtectionCases",11},{"audioPolicy",true},{"voiceLifetimeNoDevice",true},{"npcConsumablePacking",true},{"messageQueueLifecycle",true},{"registeredLootMarkerCodec",true},{"mobWorldCommands",true},{"mobUnitAndStringQueries",true},{"mobNestedBranchesAndSuspendedBinaryRoundTrip",true},{"mobOptionalVariadicStringRoundTrip",true},{"mobRegistryParserNumericRoundTrip",true},{"refreshQueueBatchDedupReset",true},{"regenerationCadenceHpMpRollback",true},{"armorLocationCases",armorCases},{"fullHeroRecalcCases",fullHeroCases},{"leftHandCases",leftHandCases},{"stunUpvalueRollback",stunRollback},{"fullNativeRecalcCases",fullCases},{"npcPreparedCases",npcCases},{"preparedStatCases",preparedCases},{"effectsEquipmentRollback",effectsRollback},{"nativeHpMpCallsites",true},{"modifiedHealthEquipmentSpell",true},{"nonzeroJewelryFixture",nonzeroJewelryFixture},{"modifiedHealthMutationSentinels",true},{"heroRecalculationCases",3},{"heroMutationRollback",true},{"skillPriceCases",6},{"realPerkPriceCases",perkCases},
            {"movementCallsite",true},{"failedMutationRolledBack",true},{"invalidNumberRejected",true},
            {"constStatsPreserved",true},{"callbackReferenceExpired",true},{"sessionOwnership",true},{"baseMovementPreserved",true},
            {"perkArchive",perkArchive},{"itemArchive",itemArchive}};
}
}
