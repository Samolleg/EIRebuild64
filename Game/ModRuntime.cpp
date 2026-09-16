#include "StdAfx.h"
#include "ModRuntime.h"
#include "ScenarioSpeechDucking.h"
#include "ModAudioPolicy.h"
#include "ModPackage.h"
#include "RpgSystem.h"
#include "Unit.h"
#include "PerkDatabase.h"
#include "ItemDatabase.h"
#include "Item.h"
#include "SpellSystem.h"
#include "UnitServer.h"
#include "UnitDatabase.h"
#include "GeneralSettings.h"
#include "ModStatRules.h"
#include "ModStatInitialization.h"
#include "ModNpcRefresh.h"
#include "ModMobCommands.h"
#include "SpellManager.h"
#include "WorldServer.h"
#include "ServerSpecificManager.h"
#include "luajit-x86/lua.hpp"
#include <stdexcept>
#include <filesystem>
#include <shellapi.h>

namespace ModRuntime {
namespace { thread_local CModPackage *current = nullptr;thread_local CModPackage *resources = nullptr; }
CModPackage *Current() { return current; }
CModPackage *Resources(){return resources;}
std::string ConfigPath(const char*path){return resources?resources->ResolveConfig(std::filesystem::path(path).filename().string(),path):path;}
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
Host::~Host(){resources=nullptr;session.reset();package.reset();}
std::unique_ptr<Host> Host::FromCommandLine(){
 int count=0;LPWSTR*args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)throw std::runtime_error("Cannot parse command line");
 struct Free{LPWSTR*p;~Free(){LocalFree(p);}} free{args};std::string directory;bool found=false;
 for(int i=1;i<count;i++)if(wcscmp(args[i],L"--mod")==0){if(found||i+1>=count)throw std::runtime_error("Expected one --mod directory");found=true;std::filesystem::path p(args[++i]);directory=p.string();}
 return found?std::make_unique<Host>(directory):nullptr;
}
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
        CPerkDatabase previous;SItemDatabase previousItems;SUnitDatabase previousUnits;
        DatabaseGuard() { std::swap(previous, thePerkDatabase);std::swap(previousItems,theItemDatabase);std::swap(previousUnits,theUnitDatabase); }
        ~DatabaseGuard() { std::swap(previous, thePerkDatabase);std::swap(previousItems,theItemDatabase);std::swap(previousUnits,theUnitDatabase); }
    } database;
    const fs::path path(perkArchive);
    if (!thePerkDatabase.LoadDatabase((path.parent_path().string()+"\\").c_str(), path.filename().string().c_str()) ||
        thePerkDatabase.GetNumPerks() == 0) throw std::runtime_error("Native perk database failed to load");
    const fs::path itemPath(itemArchive);
    if(!theItemDatabase.Load((itemPath.parent_path().string()+"\\").c_str(),itemPath.filename().string().c_str()))throw std::runtime_error("Native items DB failed to load");
    const fs::path unitPath(unitArchive);
    if(!theUnitDatabase.Load((unitPath.parent_path().string()+"\\").c_str(),unitPath.filename().string().c_str()))throw std::runtime_error("Native unit DB failed to load");
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
        require(!theWorldServer.IsMapLoaded(),"Headless lifecycle fixture requires unloaded world");package.SuccessfulWorldLoad();package.TickStatRefresh();
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
                // Use the native world-script owner required by the original binary thread loader.
                auto&worldScript=theWorldServer.script;worldScript.Clear();struct ClearWorldScript{CScript&s;~ClearWorldScript(){s.Clear();}} clearWorldScript{worldScript};
                CString suspended="GlobalVars(score:float) WorldScript(score=0 if(1) then(WaitSegment(2) score=5) else(score=99) score=Add(score,1))";
                require(worldScript.LoadText(suspended),"Suspended branch compilation failed");worldScript.Segment();require(worldScript.GetGlobalVars().fVars.GetVar("score",score)&&score==0,"Branch did not suspend");
                CMemorySave memory;memory.StartSaving();worldScript.StoreScript(memory);worldScript.Clear();memory.Seek(0);memory.StartLoading();worldScript.LoadScript(memory);require(memory.IsOk()&&memory.GetPosition()==memory.GetSize(),"Binary world script did not round-trip fully");worldScript.Segment();require(worldScript.GetGlobalVars().fVars.GetVar("score",score)&&score==6,"Loaded branch resumed at wrong program counter");
            }
            {
                CScript strings;CString code="GlobalVars(a:string,b:string,c:string,d:string) WorldScript(a=StrMid(\"abcdef\",-3,2) b=StrLower(\"AbC\") c=StrUpper(\"aBc\") d=StrMid(\"abcdef\",2,0))";
                require(strings.LoadText(code),"String query parsing failed");CString exported;strings.StoreText(exported);CScript again;require(again.LoadText(exported),"String query text round-trip failed");strings.Segment();again.Segment();
                for(auto*p:{&strings,&again}){CString value;require(p->GetGlobalVars().szVars.GetVar("a",value)&&value=="de","Negative string position mismatch");require(p->GetGlobalVars().szVars.GetVar("b",value)&&value=="abc","Lowercase mismatch");require(p->GetGlobalVars().szVars.GetVar("c",value)&&value=="ABC","Uppercase mismatch");require(p->GetGlobalVars().szVars.GetVar("d",value)&&value=="bcdef","Zero string length mismatch");}
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
                bool tameRejected=false;try{number(348,0,"tame");}catch(const std::runtime_error&){tameRejected=true;}require(tameRejected,"Invalid reference tame index silently mapped");
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
    return {{"audioPolicy",true},{"voiceLifetimeNoDevice",true},{"npcConsumablePacking",true},{"messageQueueLifecycle",true},{"registeredLootMarkerCodec",true},{"mobWorldCommands",true},{"mobUnitAndStringQueries",true},{"mobNestedBranchesAndSuspendedBinaryRoundTrip",true},{"mobOptionalVariadicStringRoundTrip",true},{"mobRegistryParserNumericRoundTrip",true},{"refreshQueueBatchDedupReset",true},{"regenerationCadenceHpMpRollback",true},{"armorLocationCases",armorCases},{"fullHeroRecalcCases",fullHeroCases},{"leftHandCases",leftHandCases},{"stunUpvalueRollback",stunRollback},{"fullNativeRecalcCases",fullCases},{"npcPreparedCases",npcCases},{"preparedStatCases",preparedCases},{"effectsEquipmentRollback",effectsRollback},{"nativeHpMpCallsites",true},{"modifiedHealthEquipmentSpell",true},{"nonzeroJewelryFixture",nonzeroJewelryFixture},{"modifiedHealthMutationSentinels",true},{"heroRecalculationCases",3},{"heroMutationRollback",true},{"skillPriceCases",6},{"realPerkPriceCases",perkCases},
            {"movementCallsite",true},{"failedMutationRolledBack",true},{"invalidNumberRejected",true},
            {"constStatsPreserved",true},{"callbackReferenceExpired",true},{"sessionOwnership",true},{"baseMovementPreserved",true},
            {"perkArchive",perkArchive},{"itemArchive",itemArchive}};
}
}
