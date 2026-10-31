#include "StdAfx.h"
#include "GameSave.h"
#include "Effect.h"
#include <set>
#include "CorePerf.h"
#include "GameJournal.h"
#include "ModNativeCrtRandom.h"
#include "ScenarioTracker.h"
#include "ModPackage.h"
#include "MapResources.h"
#include "SettingsProfile.h"
#include "ModNpcEquipment.h"
#include "ModHitHistory.h"
#include "ModCampStats.h"
#include "GeneralSettings.h"
#include "SocialStealthLua.h"
#include "EngineDiagnostics.h"
#include "ModCombat.h"
#include "ModConditionalCombat.h"
#include "ModCriticalLifecycle.h"
#include "ModAlchemy.h"
#include "dx11/RetailModalFade.h"
#include "ModAttackTiming.h"
#include "ModPreparedAttack.h"
#include "TextureManager.h"
#include "ModMobCommands.h"
#include "ModAudioConfig.h"
#include "ModRegisteredLoot.inl"
#include "ModNpcConsumables.inl"
#include "Unit.h"
#include "UnitDatabase.h"
#include "ModStatRules.h"
#include "ModStatInitialization.h"
#include "ModNpcRefresh.h"
#include "MapObjectServer.h"
#include "MapObjectManager.h"
#include "SpellManager.h"
#include "SpellDatabase.h"
#include "ModSpellTooltip.h"
#include "ModCampSpellInfo.h"
#include "ModItemInfo.h"
#include "ModItemDetails.h"
#include "ModConstructorLifetime.h"
#include "ModPlayerNumbers.h"
#include "SpellConstructor.h"
#include "ItemDatabase.h"
#include "ModDescriptionFont.h"
#include "WorldServer.h"
#include "WorldClient.h"
#include "Player.h"
#include "PlayerClient.h"
#include "PlayerServer.h"
#include "UnitServer.h"
#include "Item.h"
#include "Party.h"
#include "ItemManager.h"
#include "ModLuaBridge.h"
#include "ModInfoStatsCache.h"
#include "ModRuntime.h"
#include "RpgSystem.h"
#include "PerkDatabase.h"
#include <limits>
#include <cmath>
#include "FileRegistry.h"
#include "aiSettings.h"
#include "ItemDatabase.h"
#include "CMappedResource.h"
#include "GraphicsProcessorMMX.h"
#include "ModHDEquipmentLayer.h"
#include "mapFormat.h"
#include "adFormat.h"
#include <lua.hpp>
#include <filesystem>
#include <fstream>
void ModDeathFearReset();

#include <stdexcept>
#include <set>
#include <shellapi.h>
#include "ModSpellTextures.inl"
#include "ModTacticalSpells.h"
#include "ModGroupSearch/Native.h"
#include "ModConfiguration.h"
#include "ModItemUiPolicy.h"
#include "Console.h"
#include "ModNpcMigration.inl"
#include "ModOutOfCombatRegeneration.inl"
struct ModDelayedShot {SRPGAttackPortion ap;int target,start,due;const void*world;};
namespace fs=std::filesystem;
using Json=nlohmann::json;
namespace {
std::string Lower(std::string s){for(char &c:s){if(c=='/')c='\\';else if(c>='A'&&c<='Z')c+=32;}return s;}
std::string Ansi(const wchar_t *p){int n=WideCharToMultiByte(CP_ACP,0,p,-1,nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_ACP,0,p,-1,&s[0],n,nullptr,nullptr);s.pop_back();return s;}
std::string Join(const std::string &root,const std::string &relative){
 fs::path p(relative);if(p.empty()||p.is_absolute()||p.has_root_name())throw std::runtime_error("Invalid relative mod path: "+relative);
 for(const auto &part:p)if(part=="..")throw std::runtime_error("Parent traversal in mod path: "+relative);
 return (fs::path(root)/p).string();
}
const wchar_t* const partyVisualRegistry = L"Software\\Gipat.Ru\\EI_Starter\\Settings";
const wchar_t* const hideHelmetsValue = L"HidePartyHelmets";
const wchar_t* const hideUpperArmorValue = L"HidePartyUpperArmor";
bool ReadPartyVisualOption(const wchar_t* name) {
 return SettingsProfile::Dword(L"Settings",name)!=0;
}
bool WritePartyVisualOption(const wchar_t* name,bool enabled) {
 return SettingsProfile::SetDword(L"Settings",name,enabled?1u:0u);
}
int LuaInvalidateInfoStatsCache(lua_State* s){
 static_cast<CModPackage*>(lua_touserdata(s,lua_upvalueindex(1)))->InvalidateInfoStatsCache();return 0;
}
struct Stack {lua_State *s;int n;Stack(lua_State *p):s(p),n(lua_gettop(p)){}~Stack(){lua_settop(s,n);}};
Json Value(lua_State*s,int i){switch(lua_type(s,i)){case LUA_TNIL:return nullptr;case LUA_TBOOLEAN:return lua_toboolean(s,i)!=0;case LUA_TNUMBER:return lua_tonumber(s,i);case LUA_TSTRING:{size_t n;const char*p=lua_tolstring(s,i,&n);return std::string(p,n);}default:throw std::runtime_error("Non-scalar Lua lookup result");}}
void Push(lua_State*s,const Json&v){if(v.is_null())lua_pushnil(s);else if(v.is_boolean())lua_pushboolean(s,v.get<bool>());else if(v.is_number())lua_pushnumber(s,v.get<double>());else if(v.is_string()){auto a=v.get<std::string>();lua_pushlstring(s,a.data(),a.size());}else throw std::runtime_error("Non-scalar lookup argument");}
}
namespace {
const char* const staticLookupNames[]={"GetDualAnims","GetVampSpellAdds","GetNpcCombatConsumableConfig"};
}
void ModStaticLookup::State::Initialize(lua_State* state){
 *this=State{};
 lua_getglobal(state,"JmvStaticLookupContract");
 contract=lua_isnumber(state,-1) && lua_tointeger(state,-1)==1;
 lua_pop(state,1);
 for(int i=0;i<3;++i){lua_getglobal(state,staticLookupNames[i]);functions[i]=lua_isfunction(state,-1)?lua_topointer(state,-1):nullptr;lua_pop(state,1);}
}
bool ModStaticLookup::State::Eligible(lua_State* state,int index) const{
 if(!state || !contract || !functions[index])return false;
 lua_getglobal(state,staticLookupNames[index]);
 const bool same=lua_isfunction(state,-1) && lua_topointer(state,-1)==functions[index];
 lua_pop(state,1);return same;
}
CModPackage::CModPackage():initialization(std::make_unique<ModInitialization::Ledger>()),refreshQueue(std::make_unique<ModNpcRefreshQueue>()),infoStats(std::make_unique<ModInfoStats::State>()){}
CModPackage::~CModPackage(){perkHostLifetime.reset();if(state)lua_close(state);}
void CModPackage::Call(int args,int results){CorePerf::Scope coreTiming(CorePerf::Lua);GameJournal::Callback journalCallback;GameJournal::Source journalSource("Lua.mod");if(lua_pcall(state,args,results,0)){const char*p=lua_tostring(state,-1);GameJournal::Event("ERROR","lua","text=%s",GameJournal::Quote(p).c_str());throw std::runtime_error(p?p:"Lua package call failed");}}
void CModPackage::Require(const char *name){lua_getglobal(state,"require");lua_pushstring(state,name);Call(1,1);}
void CModPackage::Load(const std::string &directory){
 if(state)throw std::runtime_error("Package already loaded; create a fresh package for reload");
 root=fs::absolute(directory).lexically_normal().string();
 settlementCatalog.Load(root);
 SpellTextures::Load(root);
 CFileRegistry config;if(!config.Open(Join(root,"config.reg").c_str()))throw std::runtime_error("Cannot open mod config.reg");
 saveInMod=config.GetInt("MOD","GameSaves",1)!=0;
  forceTutorial=ModConfiguration::Bool(config,"MOD","ForceShowTutorial",false);
  gameDebugFile=(LPCSTR)config.GetString("Dev","GameDebugFile","");
  combatSettings["PreventZone1Animation"]=int(ModConfiguration::Bool(config,"MOD","PreventZone1Animation",false));
 singlePlayerAllowed=config.GetInt("MOD","Single",1)!=0;
 CStringArray entries;config.GetEntries("RES",entries);
 for(int i=0;i<entries.GetSize();i++){
   std::string key=Lower((LPCSTR)entries[i]);CStringArray paths;config.GetStringArray("RES",entries[i],paths);
   for(int j=0;j<paths.GetSize();j++){std::string path=(LPCSTR)paths[j];if(path.empty())throw std::runtime_error("Empty RES value");if(path[0]=='-')ignoreBase[key]=true;else archives[key].push_back(Join(root,path));}
 }
 entries.RemoveAll();config.GetEntries("CONFIG",entries);
 for(int i=0;i<entries.GetSize();i++){CString path=config.GetString("CONFIG",entries[i],"");if(!path.IsEmpty())configs[Lower((LPCSTR)entries[i])]=Join(root,(LPCSTR)path);}
 for(const auto &kv:configs)if(!fs::is_regular_file(kv.second))throw std::runtime_error("Missing config: "+kv.second);
 spellAddon.Load(ResolveConfig("SPELLADDON.INI",Join(root,"SPELLADDON.INI")));
 GameJournal::Event("SPELLADDON","config","path=%s keys=%zu sha256=%s",GameJournal::Quote(spellAddon.Path().c_str()).c_str(),ModSpellAddon::KeyCount,spellAddon.Sha256().c_str());
 fs::path cameraRoot=fs::path(root)/"camera";
 if(fs::exists(cameraRoot))for(const auto &entry:fs::recursive_directory_iterator(cameraRoot))if(entry.is_regular_file())cameras[Lower(fs::relative(entry.path(),cameraRoot).string())]=entry.path().string();
 entries.RemoveAll();config.GetEntries("CAMERA",entries);
 for(int i=0;i<entries.GetSize();i++){CString path=config.GetString("CAMERA",entries[i],"");if(!path.IsEmpty())cameras[Lower((LPCSTR)entries[i])]=Join(root,(LPCSTR)path);}
 for(const auto &kv:cameras)if(!fs::is_regular_file(kv.second))throw std::runtime_error("Missing camera: "+kv.second);
 mainScript="main.lua";combatSettings["JMVGoCamp"]=1;auto ai=configs.find("ai.reg");if(ai!=configs.end()){CFileRegistry r;if(!r.Open(ai->second.c_str()))throw std::runtime_error("Cannot read addon AI registry");worldBackground=ModConfiguration::Bool(r,"Addon","JMVWorldBackground",true);combatSettings["JMVGoCamp"]=int(ModConfiguration::Bool(r,"Addon","JMVGoCamp",true));mainScript=(LPCSTR)r.GetString("Addon","LuaMainScriptPath","main.lua");}
 if(!fs::is_regular_file(Join(root,mainScript)))throw std::runtime_error("Missing main script");
 fs::path mapRoot=fs::path(root)/MapResources::Directory;
 if(!fs::is_directory(mapRoot))throw std::runtime_error("Missing x64 map directory: "+mapRoot.string());
 for(const auto &entry:fs::recursive_directory_iterator(mapRoot))if(entry.is_regular_file()) {
  if(entry.is_symlink())throw std::runtime_error("Maps64 cannot contain resource links: "+entry.path().string());
  const auto key=MapResources::Key(fs::relative(entry.path(),mapRoot).string());
  if(!maps.emplace(key,entry.path().string()).second)throw std::runtime_error("Duplicate Maps64 resource: "+key);
 }
 // Load data first. InitializeScript separately registers typed engine bindings.
 state=luaL_newstate();if(!state)throw std::bad_alloc();luaL_openlibs(state);
 Stack stack(state);lua_getglobal(state,"package");std::string path=root+"/?.lua;"+root+"/lua/?.lua";
 for(char &c:path)if(c=='\\')c='/';lua_pushlstring(state,path.data(),path.size());lua_setfield(state,-2,"path");lua_pushliteral(state,"");lua_setfield(state,-2,"cpath");lua_pop(state,1);
 // Match main.lua require order: table callbacks must override default callbacks.
 Require("default");lua_pop(state,1);
 Require("table_indexes");indexRef=luaL_ref(state,LUA_REGISTRYINDEX);
 Require("static_table_values");lua_getfield(state,-1,"new");lua_rawgeti(state,LUA_REGISTRYINDEX,indexRef);Call(1,1);lua_getfield(state,-1,"last");lookupRef=luaL_ref(state,LUA_REGISTRYINDEX);
}

bool CModPackage::ConfigureAddonSettings(CFileRegistry& settings){
 ConfigureModAudio(settings,root);
 falloutSettings.yaw=ModConfiguration::Float(settings,"Addon","JMVFalloutCameraYaw",45.0f);
 falloutSettings.pitch=ModConfiguration::Float(settings,"Addon","JMVFalloutCameraPitch",35.264f);
 falloutSettings.telephoto=ModConfiguration::Float(settings,"Addon","JMVFalloutCameraTelephotoMultiplier",2.0f);
  settingsProfile.Read(settings);
  EngineDiagnostics::Event("SETTINGS P06 charge=%u factor=%g maxima=%u,%u,%u,%u,%u",settingsProfile.charge,settingsProfile.factor,settingsProfile.skills[0],settingsProfile.skills[1],settingsProfile.skills[3],settingsProfile.skills[4],settingsProfile.skills[5]);
 size_t count=0;const auto* rules=ModConfiguration::CombatRules(count);
 for(size_t i=0;i<count;++i)combatSettings[rules[i].key]=ModConfiguration::Bool(settings,"Addon",rules[i].key,rules[i].fallback)?1:0;
 combatSettings["JMVExpMult"]=int(settings.GetDWord("Addon","JMVExpMult",1));
 registeredLoot=ModConfiguration::Bool(settings,"Addon","JMVRegisterLoot",false);
 npcStatRefresh=ModConfiguration::Bool(settings,"Addon","JMVNpcStatRefresh",true);
 npcDatabaseMigration=ModConfiguration::Bool(settings,"Addon","JMVNpcDatabaseMigration",true);
 extendActiveObjectsArea=ModConfiguration::Bool(settings,"Addon","JMVExtendActiveObjectsArea",true);
 briefingWeapons=ModConfiguration::Bool(settings,"Addon","JMVBriefingWeapons",true);
 worldBackground=ModConfiguration::Bool(settings,"Addon","JMVWorldBackground",true);
 partyVisualOptions=ModConfiguration::Bool(settings,"Addon","JMVPartyVisualOptions",true);
 if(partyVisualOptions){hidePartyHelmets=ReadPartyVisualOption(hideHelmetsValue);hidePartyUpperArmor=ReadPartyVisualOption(hideUpperArmorValue);}
 npcArmorCalculation=ModConfiguration::Bool(settings,"Addon","JMVNpcArmorCalculation",true);
 npcTrickster=ModConfiguration::Bool(settings,"Addon","JMVNpcTrickster",true);
 npcMaterialEffects=ModConfiguration::Bool(settings,"Addon","JMVNpcArmorMaterialEffects",true);
 experienceLimit=ModConfiguration::Float(settings,"AddonExp","Limit",-1.0f);
 magicRandomLimit=ModConfiguration::Float(settings,"Addon","MagicRandom",-1.0f);
 return ModConfiguration::Bool(settings,"Addon","JMVSetItemsComplex",false);
}
void CModPackage::InitializeScript(){
 if(scriptInitialized)return;
 if(!state)throw std::runtime_error("Load package before initializing Lua script");
 staticLookups=ModStaticLookup::State{};
 scalarManaBonus=false;
 manaBonusCache.clear();
 spellAddonManaBonusCache.clear();
 Stack stack(state);
 ModLua::Register(state);
 if(!perkHostLifetime)perkHostLifetime=std::make_shared<ModLua::PerkHostLifetime>();
 ModLua::BindPerkDatabase(state,perkHostLifetime);
 lua_pushlightuserdata(state,this);lua_pushcclosure(state,LuaInvalidateInfoStatsCache,1);lua_setglobal(state,"InvalidateInfoStatsCache");
 {bool enabled=false;combatSettings["JMVFixConstructorShutdownRelease"]=1;auto ai=configs.find("ai.reg");if(ai!=configs.end()){CFileRegistry settings;if(!settings.Open(ai->second.c_str()))throw std::runtime_error("Cannot read addon settings");enabled=ConfigureAddonSettings(settings);}lua_pushboolean(state,enabled);lua_setglobal(state,"JMVSetItemsComplexEnabled");ModConstructorLifetime::BindPolicy(CombatSetting("JMVFixConstructorShutdownRelease")!=0);}
 // Build the missing derived index from main.lua's own local rows, without modifying installed Lua.
 std::ifstream input(Join(root,mainScript),std::ios::binary);if(!input)throw std::runtime_error("Cannot read main script");
 std::string source((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
 const std::string anchor="function GetDualAnims(";auto dual=source.find(anchor);
 if(source.find("local DualAnims = {")!=std::string::npos&&dual!=std::string::npos){
     if(source.find(anchor,dual+anchor.size())!=std::string::npos)throw std::runtime_error("Ambiguous dual animation callback");
     source.insert(dual,"if type(TableIndexes)=='table' and TableIndexes.DualAnims==nil then TableIndexes.DualAnims=TableIndexes.build(DualAnims,1) end\n");
 }
 if(source.compare(0,3,"\xEF\xBB\xBF")==0)source.erase(0,3);
 if(luaL_loadbuffer(state,source.data(),source.size(),("@"+Join(root,mainScript)).c_str()))throw std::runtime_error(lua_tostring(state,-1));
 Call(0,0);ModLua::InstallFactories(state);
  SocialStealth::LoadConfig(state,Join(root,"lua/social_stealth.lua"));
  disguiseProfiles.clear();
  lua_getglobal(state,"GetManaBonusFromIntelligence");
  scalarManaBonus=lua_isfunction(state,-1)!=0;
  lua_pop(state,1);
  lua_getglobal(state,"RegenerateHpAndMpJmvDue");
  scheduledRegeneration=lua_isfunction(state,-1)!=0;
  lua_pop(state,1);
  staticLookups.Initialize(state);
  infoStats->Initialize(state);
  scriptInitialized=true;
}

namespace {
struct StunEntry {
 lua_State*s;int table=LUA_NOREF,previous=LUA_NOREF,id;bool committed=false;
 StunEntry(lua_State*state,int unit):s(state),id(unit){
  int top=lua_gettop(s);lua_getglobal(s,"RecalcUnitStatsJmv");
  for(int i=1;;i++){const char*name=lua_getupvalue(s,-1,i);if(!name)break;if(strcmp(name,"stunSpell")==0){if(!lua_istable(s,-1)){lua_settop(s,top);throw std::runtime_error("Invalid stunSpell state");}table=luaL_ref(s,LUA_REGISTRYINDEX);break;}lua_pop(s,1);}
  lua_settop(s,top);if(table==LUA_NOREF)throw std::runtime_error("Full recalc lacks the required stunSpell upvalue");
  lua_rawgeti(s,LUA_REGISTRYINDEX,table);lua_rawgeti(s,-1,id);previous=luaL_ref(s,LUA_REGISTRYINDEX);lua_pop(s,1);
 }
 ~StunEntry(){if(!committed){lua_rawgeti(s,LUA_REGISTRYINDEX,table);if(previous==LUA_REFNIL)lua_pushnil(s);else lua_rawgeti(s,LUA_REGISTRYINDEX,previous);lua_rawseti(s,-2,id);lua_pop(s,1);}luaL_unref(s,LUA_REGISTRYINDEX,table);luaL_unref(s,LUA_REGISTRYINDEX,previous);}
};
struct CallbackFrame {
 lua_State *state;int top;int previousUnits=LUA_REFNIL;std::shared_ptr<ModLua::Scope> scope=std::make_shared<ModLua::Scope>();
 explicit CallbackFrame(lua_State*s):state(s),top(0){if(!s)throw std::runtime_error("Mod package not loaded");top=lua_gettop(s);previousUnits=ModLua::BeginCallbackScope(s);}
 ~CallbackFrame(){scope->active=false;ModLua::EndCallbackScope(state,previousUnits);lua_settop(state,top);}
};
double Number(lua_State*s,const char*name){
 if(lua_type(s,-1)!=LUA_TNUMBER)throw std::runtime_error(std::string(name)+": expected numeric result");
 double value=lua_tonumber(s,-1);
 if(!std::isfinite(value))throw std::runtime_error(std::string(name)+": non-finite result");
 return value;
}
int Price(lua_State*s,const char*name){double value=Number(s,name);if(value<std::numeric_limits<int>::lowest()||value>(std::numeric_limits<int>::max)()||floor(value)!=value)throw std::runtime_error(std::string(name)+": invalid integer price");return static_cast<int>(value);}
}
void CModPackage::BeginCallback(const char*name){
    GameJournal::SetCallback(name);
    GameJournal::Count("SCRIPT", "lua_callback", name);
 if(!scriptInitialized)throw std::runtime_error("Mod script has not been initialized");
 lua_getglobal(state,name);if(!lua_isfunction(state,-1))throw std::runtime_error(std::string("Missing mod callback: ")+name);
}
float CModPackage::StatsNumber(const char*name,const SRPGUnitStats&stats){
 CallbackFrame frame(state);SRPGUnitStats snapshot=stats;
 BeginCallback(name);ModLua::Push(state,&snapshot,frame.scope);Call(1,1);
 double value=Number(state,name);if(fabs(value)>(std::numeric_limits<float>::max)())throw std::runtime_error(std::string(name)+": float overflow");return static_cast<float>(value);
}
bool CModPackage::IsNpcManaCaster(CUnit*unit){
 if(!unit||unit->IsHero())return false;
 auto*server=dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());
 if(!server||server->logic.GetUnitServer()!=server)return false;
 // Native QI_MAX_SPELLS is eight; keep the NPC scan independent of physical storage.
 for(DWORD slot=0;slot<QI_NPC_SPELLS;++slot){
  CSpell*spell=unit->GetQSpell(slot);
  if(!spell)continue;
  const auto*prototype=spell->GetPrototype();
  const char*name=prototype?(LPCSTR)prototype->szCodeName:nullptr;
  std::string code=name?Lower(name):std::string();
  const char*gate=code=="powerhit"||code=="test"?"JMVPowerHit":
                   code=="doubleshot"?"JMVDoubleShot":
                   code=="preciseshot"?"JMVPreciseShot":
                   code=="explosiveshot"?"JMVExplosiveShot":nullptr;
  // Named stamina config and whitelisted magic classification are separate branches.
  ModSkills::Config stamina;
  bool configured=false;
  if(prototype&&(!gate||CombatSetting(gate))){
   configured=SkillConfig(name,stamina);
   if(!configured&&code=="test")configured=SkillConfig("powerhit",stamina);
  }
  if(configured&&stamina.allowNpc&&stamina.Cost(spell->GetManaCost())>0.f)return true;
  if(spell->GetManaCost()<=0.f)continue;
  ModSkills::Config attack;
  ModTactical::Type type=SkillConfig(spell,attack)?ModTactical::Direct:ModTactical::SpellType(spell);
  if(type>=ModTactical::Direct&&type<=ModTactical::Heal)return true;
 }
 return false;
}
bool CModPackage::Regenerate(SRPGCommonStats&common,SRPGUnitStats&stats,CUnit*unit,bool hpOnly){
 if(!unit||!unit->GetUnitSpecific())throw std::runtime_error("Mod regeneration requires a native unit-specific component");
 SyncRuntimeState();
 ModOutOfCombatRegeneration::Update(unit);
 if(scheduledRegeneration&&common.ssHP.fpCurrent<=0)return false;
 ModGroupSearch::Update(); // native awareness producer, before the regen cadence gate
 if(scheduledRegeneration){
  // The same serialized counter and unsigned increment as RegenerationSchedule.h.
  unsigned next=static_cast<unsigned>(common.nRegenerateCounter)+1u;
  if(next>0&&next<15){common.nRegenerateCounter=static_cast<int>(next);JMV_NpcConsumablesUpdate(&common,unit);return false;}
  common.nRegenerateCounter=0;
 }
 const unsigned epoch=initialization->epoch;bool ready=initialization->Ready(*unit,common,stats);
 ModInitialization::Inputs inputs(common,stats,*unit);
 if(IsNpcManaCaster(unit))hpOnly=false;
 CallbackFrame frame(state);SRPGCommonStats stagedCommon=common;SRPGUnitStats staged=stats;
 frame.scope->unit=unit;frame.scope->stagedStats=&staged;
 const bool safe=ModOutOfCombatRegeneration::IsSafe(unit);
 BeginCallback(scheduledRegeneration?"RegenerateHpAndMpJmvDue":"RegenerateHpAndMpJmv");ModLua::Push(state,&stagedCommon,frame.scope);ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,unit,frame.scope);lua_pushboolean(state,hpOnly);lua_pushboolean(state,safe);Call(5,1);
 if(!lua_isboolean(state,-1))throw std::runtime_error("Regeneration callback must return boolean");
 bool changed=lua_toboolean(state,-1)!=0;
 if(!std::isfinite(stagedCommon.ssHP.fpCurrent)||!std::isfinite(stagedCommon.ssHP.fpMax)||!std::isfinite(stagedCommon.ssMP.fpCurrent)||!std::isfinite(stagedCommon.ssMP.fpMax))throw std::runtime_error("Invalid regenerated resource");
 for(const auto&h:staged.hls)if(!std::isfinite(h.fpHP))throw std::runtime_error("Invalid regenerated hit location");
 frame.scope->CommitItems();stats=staged;common=stagedCommon;
 JMV_NpcConsumablesUpdate(&common,unit);
 // Publish both native masks when only MP changed; missing/changed proof uses full recalc.
 bool mpOnly=changed&&epoch==initialization->epoch&&ready&&inputs.MpOnly(common,stats,*unit);
 if(mpOnly){unit->SetUpdateMask(CMapObject::VLD_MASK_STATS);unit->SetUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);}
 return changed&&!mpOnly;
}
float CModPackage::MurderAwarenessValue(int property,float fallback){
 CallbackFrame frame(state);lua_getglobal(state,"GetMurderAwarenessConfigValue");
 if(!lua_isfunction(state,-1))return fallback;
 lua_pushinteger(state,property);lua_pushnumber(state,fallback);Call(2,1);
 return lua_isnumber(state,-1)?static_cast<float>(lua_tonumber(state,-1)):fallback;
}
void CModPackage::ResetRuntimeState(){ModOutOfCombatRegeneration::Reset();InvalidateInfoStatsCache();ModGroupSearch::Reset();ModDeathFearReset();ModTactical::Reset();delayedShots.clear();skillCooldowns.Clear();criticalAttacks.clear();customCriticals.Clear();JMV_NpcConsumablesReset();JMV_RegisteredLootReset("package-reset");refreshQueue->Reset();initialization->Reset();npcArmorCache.clear();cacheWorld=nullptr;cacheZone.clear();cacheSegment=-1;}
void CModPackage::SyncRuntimeState(){
 const void*world=theWorldServer.GetSpecificManager();std::string zone;
 zone=(LPCSTR)theWorldServer.szMobFileName;
 int segment=theWorldServer.GetSegmentTimeCounter();
 if(world!=cacheWorld||zone!=cacheZone||(cacheSegment>=0&&segment<cacheSegment)){ModOutOfCombatRegeneration::Reset();for(auto&i:delayedShots)ForgetAttack(i.second->ap);delayedShots.clear();skillCooldowns.Clear();JMV_RegisteredLootReset("world-epoch");npcArmorCache.clear();initialization->Reset();InvalidateInfoStatsCache();refreshQueue->Reset();}
 cacheWorld=world;cacheZone=zone;cacheSegment=segment;
}
namespace {
CUnitServer* AuthoritativeServer(CUnit*unit){
 if(!unit||unit->IsInvalid()||unit->IsUnreferenced()||!theWorldServer.GetMapObjectManager())return nullptr;
 if(theWorldServer.GetUnit(unit->GetID())!=unit||unit->WhoAmI()!=HPC_UNIT)return nullptr;
 return dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());
}
}
void CModPackage::InitializeLoadedHeroes(){
 if(!refreshQueue->heroesPending||!theWorldServer.IsMapLoaded())return;
 refreshQueue->heroesPending=false;CMapObjPtrHashIterator iter;
 for(auto*object=iter.GetFirst(&theWorldServer.dynamicObjs);object;object=iter.GetNext()){
  if(object->WhoAmI()!=HPC_UNIT)continue;auto*unit=static_cast<CUnit*>(object);auto*server=AuthoritativeServer(unit);
  if(!server||!unit->GetOwner()||unit->IsDead())continue;
  auto*common=server->GetMapObjectServer()->GetRPGStats();auto*stats=server->GetRPGStats();
  if(!std::isfinite(common->ssHP.fpCurrent)||!std::isfinite(common->ssHP.fpMax)||!std::isfinite(common->ssMP.fpCurrent)||!std::isfinite(common->ssMP.fpMax))continue;
  if(!initialization->Ready(*unit,*common,*stats))server->RecalcRPGStats(false);
 }
}
bool CModPackage::RefreshUnitStatsFromMob(CUnit*unit){
 if(!unit)return false;
 if(theWorldServer.GetMapObjectManager())if(auto*serverUnit=theWorldServer.GetUnit(unit->GetID()))unit=serverUnit;
 if(npcStatRefresh){SyncRuntimeState();auto*server=AuthoritativeServer(unit);if(!server||!server->GetUnit()||unit->GetOwner()||unit->IsDead())return false;refreshQueue->Add(unit->GetID());return true;}
 auto*manager=theWorldServer.GetSpellManager();if(!manager)return false;
 CSmartPtr<CSpell> spell=manager->CreateSpell("invisibility{d05;d05}");if(spell.IsEmpty())return false;
 theWorldServer.CastSpell(spell,nullptr,CPlot3D(0,0,0),unit,unit->GetCP());return true;
}
void CModPackage::TickStatRefresh(){
 SyncRuntimeState();TickDoubleShots();if(theWorldServer.IsMapLoaded())JMV_RegisteredLootTick();InitializeLoadedHeroes();MigrateNpcEquipment();if(!npcStatRefresh||!theWorldServer.IsMapLoaded())return;
 if(refreshQueue->scanPending){
  refreshQueue->scanPending=false;CMapObjPtrHashIterator iter;
  for(auto*object=iter.GetFirst(&theWorldServer.dynamicObjs);object;object=iter.GetNext()){
   if(object->WhoAmI()!=HPC_UNIT)continue;auto*unit=static_cast<CUnit*>(object);auto*server=AuthoritativeServer(unit);
   if(server&&server->GetUnit()&&!unit->GetOwner()&&!unit->IsDead())refreshQueue->Add(unit->GetID());
  }
 }
 refreshQueue->Process([&](int id){
  auto*unit=theWorldServer.GetMapObjectManager()?theWorldServer.GetUnit(id):nullptr;
  if(auto*server=AuthoritativeServer(unit))server->RecalcRPGStats(false);
 });
}
void CModPackage::MigrateNpcEquipment(){
 if(!npcDatabaseMigration||!npcEquipmentMigrationPending||!theWorldServer.IsMapLoaded())return;
 CPlayer*player=theWorldServer.GetPlayerManager()?theWorldServer.GetPlayer(0):nullptr;
 if(!player)return;
 CPlayerServer*server=dynamic_cast<CPlayerServer*>(player->GetPlayerSpecific());
 if(!server||server->GetStatus()!=CPlayerServer::PSS_ACTIVE)return;
 const CString&zone=player->party.szCurrentZone;
 if(zone.IsEmpty())return;
 CString marker;marker.Format("fix.npc.equipment.database.v5.%s",(LPCSTR)zone);
 if(player->party.vars.GetVar(marker)>=1.0f){npcEquipmentMigrationPending=false;return;}
 CItemManager*manager=theWorldServer.GetItemManager();if(!manager)return;
 ModNpcMigration::Counts counts;
 CMapObjPtrHashIterator iter;
 for(CMapObject*object=iter.GetFirst(&theWorldServer.dynamicObjs);object;object=iter.GetNext()){
  if(object->IsInvalid()||object->IsUnreferenced()||object->WhoAmI()!=HPC_UNIT)continue;
  ModNpcMigration::Migrate(static_cast<CUnit*>(object),manager,counts);
 }
 if(counts.failed==0)server->SetVarMax(marker,1.0f);
 CString message;message.Format("NPC database migration zone=%s scanned=%d updated=%d unchanged=%d failed=%d dead=%d owned=%d\n",
  (LPCSTR)zone,counts.scanned,counts.updated,counts.unchanged,counts.failed,counts.dead,counts.owned);
 OutputDebugStringA(message);
 // A failed batch is retried after the next successful saved-game load.
 npcEquipmentMigrationPending=false;
}
void CModPackage::SuccessfulWorldLoad(WorldLoadOrigin origin){ResetRuntimeState();SyncRuntimeState();LoadSkillCooldowns();JMV_RegisteredLootOnSuccessfulSaveLoad();JMV_NpcConsumablesLoaded(origin);npcEquipmentMigrationPending=npcDatabaseMigration&&origin==WorldLoadOrigin::SavedGame;InitializeLoadedHeroes();ApplyPartyVisualOptions();}
bool CModPackage::SuppressLegacyRefreshSpell(const char*name)const{return npcStatRefresh&&name&&_stricmp(name,"invisibility{d05;d05}")==0;}
bool CModPackage::CollectArmor(CUnit&unit,ArmorModifiers&mods){
 bool has=false;mods.subtype=unit.GetSubtypeID();
 auto value=[&](const char*table,const char*name,int column){auto v=Lookup(table,std::string(name),column,0);if(!v.is_number())throw std::runtime_error("Non-numeric NPC armor property");double n=v.get<double>();if(!std::isfinite(n)||fabs(n)>(std::numeric_limits<float>::max)())throw std::runtime_error("Invalid NPC armor property");return float(n);};
 for(int i=0;i<RPG_NUM_ARMOR_TYPES;i++){auto*a=unit.GetQArmor(i);if(!a)continue;has=true;
 if(a->GetMaterial()>=unsigned(theItemDatabase.materials.GetNumEntries())||a->GetShape()>=unsigned(theItemDatabase.armors.GetNumEntries()))throw std::runtime_error("NPC armor database index out of range");
 const char*material=theItemDatabase.materials.GetEntry(a->GetMaterial())->szName;const char*shape=theItemDatabase.armors.GetEntry(a->GetShape())->szName;
 mods.speed+=value("MaterialProp",material,9);mods.sight+=value("MaterialProp",material,13)+value("ArmorProp",shape,5);mods.hearing+=value("MaterialProp",material,14)+value("ArmorProp",shape,6);
 }return has;
}
bool CModPackage::NpcArmorPercepts(CUnit&unit,CUnit*fallback,float&sight,float&hearing){
 sight=hearing=0;
 if(!npcMaterialEffects || unit.GetOwner())return false;
 // Opening the information panel must not reset combat/lifecycle state.
 const void*world=theWorldServer.GetSpecificManager();
 const std::string zone=(LPCSTR)theWorldServer.szMobFileName;
 const int segment=theWorldServer.GetSegmentTimeCounter();
 if(world!=npcArmorViewWorld||zone!=npcArmorViewZone||(npcArmorViewSegment>=0&&segment<npcArmorViewSegment))npcArmorViewCache.clear();
 npcArmorViewWorld=world;npcArmorViewZone=zone;npcArmorViewSegment=segment;
 CUnit*candidates[]={&unit,theWorldServer.GetMapObjectManager()?theWorldServer.GetUnit(unit.GetID()):nullptr,
  fallback,theWorld.GetMapObjectManager()?theWorld.GetUnit(unit.GetID()):nullptr};
 for(int i=0;i<4;++i){auto*c=candidates[i];if(!c||c->GetID()!=unit.GetID()||c->GetSubtypeID()!=unit.GetSubtypeID())continue;
  bool duplicate=false;for(int j=0;j<i;++j)if(candidates[j]==c)duplicate=true;if(duplicate)continue;
  ArmorModifiers found;if(!CollectArmor(*c,found))continue;
  npcArmorViewCache[unit.GetID()]=found;sight=found.sight;hearing=found.hearing;return true;
 }
 // Reuse a current runtime observation before the panel's own fallback.
 if(world==cacheWorld&&zone==cacheZone&&!(cacheSegment>=0&&segment<cacheSegment)){
  auto live=npcArmorCache.find(unit.GetID());
  if(live!=npcArmorCache.end()&&live->second.subtype==unit.GetSubtypeID())npcArmorViewCache[unit.GetID()]=live->second;
 }
 auto it=npcArmorViewCache.find(unit.GetID());if(it!=npcArmorViewCache.end()){
  if(it->second.subtype==unit.GetSubtypeID()){sight=it->second.sight;hearing=it->second.hearing;return true;}
  npcArmorViewCache.erase(it);
 }
 return false;
}
bool CModPackage::RecalcUnitStats(SRPGUnitStats&stats,SRPGCommonStats&common,CUnit&unit){
 auto statsFailure=[&](const char* reason,const SRPGUnitStats& s,const SRPGCommonStats& c){char message[768];
 snprintf(message,sizeof(message),"%s\nUnit ID=%u prototype=%s\nHP=%g MP=%g bodyWeight=%g\nMove=%g sight=%g hearing=%g",reason,unsigned(unit.GetID()),static_cast<LPCSTR>(unit.GetPrototype()?unit.GetPrototype()->szName:"<none>"),double(c.ssHP.fpMax),double(c.ssMP.fpMax),double(s.hls[RPGHL_BODY].fpHPWeight),double(s.move.fpEffectiveValue),double(s.percepts[RPGS_SIGHT].fpEffectiveValue),double(s.percepts[RPGS_HEARING].fpEffectiveValue));return std::runtime_error(message);};
 CallbackFrame frame(state);StunEntry stun(state,unit.GetID());SyncRuntimeState();
 initialization->Invalidate(unit.GetID());const unsigned epoch=initialization->epoch;
 SRPGUnitStats staged=stats;SRPGCommonStats stagedCommon=common;frame.scope->unit=&unit;frame.scope->stagedStats=&staged;
 // SetupStats supplies prototype common HP before SetRPGStats performs its two passes.
 // A fresh skeleton has no previous distribution to preserve proportionally.
 bool used=false,fresh=true;for(const auto&h:staged.hls)if(h.state>RPGALS_NOTUSED){used=true;fresh=fresh&&h.fpHP==0&&h.fpMaxHP==0;}
 if(used&&fresh){
  if(!std::isfinite(stagedCommon.ssHP.fpMax)||stagedCommon.ssHP.fpMax<=0||!std::isfinite(staged.hls[RPGHL_BODY].fpHPWeight)||staged.hls[RPGHL_BODY].fpHPWeight<=0)throw statsFailure("Fresh unit requires prototype HP and body weight",staged,stagedCommon);
  staged.DistributeHP(stagedCommon.ssHP.fpMax);
 }
 ModStats::Armor(stagedCommon,unit,npcArmorCalculation);
 float effects[53]={};for(int i=0;i<unit.GetSpellEffects().GetSize();i++){const auto&e=unit.GetSpellEffects()[i];int id=e.GetEffectType();float power=e.GetPower();if(id>=0&&id<53){if(!std::isfinite(power))throw statsFailure("Non-finite spell effect",staged,stagedCommon);effects[id]=(std::max)(effects[id],power);}}
 BeginCallback("RecalcUnitStatsJmv");ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&stagedCommon,frame.scope);ModLua::Push(state,&unit,frame.scope);ModLua::PushEffects(state,effects,53,frame.scope);lua_pushboolean(state,true);lua_pushboolean(state,true);lua_pushstring(state,unit.GetPrototype()->szName);lua_pushboolean(state,true);Call(8,0);
 bool npc=unit.GetOwner()==nullptr;ArmorModifiers mods;bool hasMods=false;
 if(npc&&npcTrickster){int level=(std::min)(3,int(staged.cPerks[11]));if(level>0){const float day[]={0,1,3,6},night[]={0,.15f,.30f,.65f},speed[]={1,1.05f,1.1f,1.2f};staged.senses[RPGS_SIGHT].fpEffectiveValue=(std::max)(staged.senses[RPGS_SIGHT].fpEffectiveValue,staged.senses[RPGS_SIGHT].fpBaseValue+day[level]);staged.senses[RPGS_INFRA].fpEffectiveValue=(std::max)(staged.senses[RPGS_INFRA].fpEffectiveValue,staged.senses[RPGS_INFRA].fpBaseValue*(1+night[level]));staged.move.fpEffectiveValue*=speed[level];}}
 if(npc&&npcMaterialEffects){CUnit*candidates[]={&unit,theWorldServer.GetMapObjectManager()?theWorldServer.GetUnit(unit.GetID()):nullptr,theWorld.GetMapObjectManager()?theWorld.GetUnit(unit.GetID()):nullptr};
 for(auto*c:candidates)if(c&&c->GetID()==unit.GetID()&&c->GetSubtypeID()==unit.GetSubtypeID()){ArmorModifiers found;if(CollectArmor(*c,found)){mods=found;hasMods=true;break;}}
 if(!hasMods){auto it=npcArmorCache.find(unit.GetID());if(it!=npcArmorCache.end()&&it->second.subtype==unit.GetSubtypeID()){mods=it->second;hasMods=true;}}
 if(hasMods){staged.move.fpEffectiveValue*=(std::max)(0.f,1+mods.speed*.01f);staged.percepts[RPGS_SIGHT].fpEffectiveValue=(std::max)(0.f,staged.percepts[RPGS_SIGHT].fpEffectiveValue+mods.sight);staged.percepts[RPGS_HEARING].fpEffectiveValue=(std::max)(0.f,staged.percepts[RPGS_HEARING].fpEffectiveValue+mods.hearing);}}
 if(!std::isfinite(staged.move.fpEffectiveValue)||!std::isfinite(staged.percepts[RPGS_SIGHT].fpEffectiveValue)||!std::isfinite(staged.percepts[RPGS_HEARING].fpEffectiveValue)||!std::isfinite(stagedCommon.ssHP.fpMax)||!std::isfinite(stagedCommon.ssMP.fpMax))throw statsFailure("Invalid final unit stats",staged,stagedCommon);
 // Map allocation happens before publication. The remaining assignments/setters do not throw.
 if(hasMods)npcArmorCache[unit.GetID()]=mods;else if(npc)npcArmorCache.erase(unit.GetID());
 frame.scope->CommitItems();stats=staged;common=stagedCommon;stun.committed=true;if(epoch==initialization->epoch)initialization->Finish(unit,common,stats);return npc&&npcTrickster&&staged.cPerks[11]>0;
}
void CModPackage::PrepareUnitStats(SRPGUnitStats&stats,SRPGCommonStats&common,const CUnit&unit,const std::string&prototype){
 CallbackFrame frame(state);SRPGUnitStats staged=stats;SRPGCommonStats stagedCommon=common;
 frame.scope->unit=&unit;frame.scope->stagedStats=&staged;
 float effects[53]={};for(int i=0;i<unit.GetSpellEffects().GetSize();i++){const auto&e=unit.GetSpellEffects()[i];int id=e.GetEffectType();float power=e.GetPower();if(id>=0&&id<53){if(!std::isfinite(power))throw std::runtime_error("Non-finite spell effect");effects[id]=(std::max)(effects[id],power);}}
 // Match the arguments used inside the current full Lua callback, including its nil armorRecalc/forceRecalc globals.
 BeginCallback("RecalcInitialJmvStats");ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&stagedCommon,frame.scope);lua_getglobal(state,"armorRecalc");ModLua::Push(state,&unit,frame.scope);lua_pushstring(state,prototype.c_str());ModLua::Push(state,&unit,frame.scope);lua_getglobal(state,"forceRecalc");Call(7,0);
 BeginCallback("ApplySpellEffects");ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&stagedCommon,frame.scope);ModLua::PushEffects(state,effects,53,frame.scope);
 if(unit.GetID()>=1000000000u&&unit.GetID()<2000000000u)lua_pushnil(state);
 else{BeginCallback("GetUnitPerks");ModLua::Push(state,&unit,frame.scope);lua_pushstring(state,prototype.c_str());lua_getglobal(state,"UnitPerkType");lua_getfield(state,-1,"Trickster");lua_remove(state,-2);Call(3,1);}
 Call(4,0);
 BeginCallback("ApplyCustomEffects");ModLua::Push(state,&unit,frame.scope);ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&stagedCommon,frame.scope);ModLua::PushEffects(state,effects,53,frame.scope);ModLua::Push(state,&unit,frame.scope);Call(5,0);
 frame.scope->CommitItems();stats=staged;common=stagedCommon;
}
void CModPackage::RecalcInternalHeroStats(SRPGUnitStats&stats,float weight){
 CallbackFrame frame(state);SRPGUnitStats staged=stats;BeginCallback("RecalcInternalHeroStats");
 ModLua::Push(state,&staged,frame.scope);lua_pushnumber(state,weight);Call(2,0);stats=staged;
}
float CModPackage::GetModifiedHealth(SRPGUnitStats&stats,const CUnit&unit,float health){
 CallbackFrame frame(state);SRPGUnitStats staged=stats;BeginCallback("GetModifiedHealth");
 ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&unit,frame.scope);lua_pushnumber(state,health);Call(3,1);
 double value=Number(state,"GetModifiedHealth");if(fabs(value)>(std::numeric_limits<float>::max)())throw std::runtime_error("Modified HP overflow");stats=staged;return float(value);
}
float CModPackage::CalcBaseHealth(const SRPGUnitStats&s){return StatsNumber("CalcBaseHealth",s);}
float CModPackage::CalcBaseMana(const SRPGUnitStats&s){return StatsNumber("CalcBaseMana",s);}
void CModPackage::RedistributeResources(SRPGUnitStats&stats,SRPGCommonStats&common,CUnit&unit){
 CallbackFrame frame(state);SRPGUnitStats staged=stats;frame.scope->unit=&unit;frame.scope->stagedStats=&staged;
 auto npcNumber=[&](const char*callback){BeginCallback(callback);ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&unit,frame.scope);lua_pushstring(state,unit.GetPrototype()->szName);Call(3,1);double value=Number(state,callback);if(fabs(value)>(std::numeric_limits<float>::max)())throw std::runtime_error("NPC resource overflow");lua_pop(state,1);return float(value);};
 int carried=0,capacity=0;float jewelry=0,overload=1;
 auto resources=ModNpcEquipment::BaseResources(unit.GetOwner()!=nullptr,
  [&]{return CalcBaseHealth(staged);},[&]{return CalcBaseMana(staged);},
  [&]{return npcNumber("CalcBaseHealthJmv");},[&]{
   for(int i=0;i<4;++i)if(auto*item=unit.GetQItem(i))jewelry+=InfoProperty("GetJewelryPropsInside",item->GetShapeEntry()->szName,12)*.01f;
   capacity=ModNpcEquipment::Capacity(staged.GetMaxEncumbrance(),jewelry);carried=ModNpcEquipment::Truncate(unit.GetItemsWeight());
   BeginCallback("OverloadManacostModsOnServer");lua_pushinteger(state,carried);lua_pushinteger(state,capacity);Call(2,1);overload=float(Number(state,"NPC overload mana"));lua_pop(state,1);
   return ModNpcEquipment::Mana(npcNumber("CalcBaseManaJmv"),overload);
  },[&](float health){return GetModifiedHealth(staged,unit,health);});
 if(!std::isfinite(resources.hp)||!std::isfinite(resources.mp))throw std::runtime_error("Invalid redistributed resources");
 frame.scope->CommitItems();stats=staged;common.ssHP.fpMax=resources.hp;common.ssMP.fpMax=resources.mp;
 static unsigned notes=0;if(notes++<24)EngineDiagnostics::Event("P03 resources unit=%d hero=%d hp=%g mp=%g jewelry=%g weight=%d capacity=%d overload=%g",unit.GetID(),unit.GetOwner()!=nullptr,double(resources.hp),double(resources.mp),double(jewelry),carried,capacity,double(overload));
}
std::string CModPackage::GetQuickAdds(const char*prototype,int property){
 CallbackFrame frame(state);BeginCallback("GetQuickAdds");lua_pushstring(state,prototype);lua_pushinteger(state,property);Call(2,1);
 // Installed GetQuickAdds returns numeric zero when no extra item is configured.
 if(lua_type(state,-1)==LUA_TNUMBER && lua_tonumber(state,-1)==0)return {};
 const char*name=lua_tostring(state,-1);if(!name)throw std::runtime_error("GetQuickAdds requires a string-compatible result");return name;
}
int CModPackage::GetSkillPriceDiff(int from,int to){
 CallbackFrame frame(state);BeginCallback("GetSkillPriceDiff");lua_pushinteger(state,from);lua_pushinteger(state,to);Call(2,1);return Price(state,"GetSkillPriceDiff");
}
int CModPackage::GetPerkPrice(const SRPGUnitStats&stats,const SPDBPerk&perk){
 CallbackFrame frame(state);SRPGUnitStats snapshot=stats;BeginCallback("GetPerkPrice");
 ModLua::Push(state,&snapshot,frame.scope);ModLua::Push(state,&perk,frame.scope);Call(2,1);return Price(state,"GetPerkPrice");
}
void CModPackage::CalcRunningManaCost(SRPGUnitStats&stats,CUnit&unit,SRPGCommonStats&common){
 CallbackFrame frame(state);SRPGUnitStats stagedStats=stats;SRPGCommonStats stagedCommon=common;
 BeginCallback("CalcRunningManaCost");ModLua::Push(state,&stagedStats,frame.scope);ModLua::Push(state,&unit,frame.scope);ModLua::Push(state,&stagedCommon,frame.scope);Call(3,0);
 // Publish only after a successful callback. Exceptions leave live engine state intact.
 stats=stagedStats;common=stagedCommon;
}
std::string CModPackage::ResolveConfig(const std::string &name,const std::string &fallback)const{auto i=configs.find(Lower(name));return i==configs.end()?fallback:i->second;}
std::string CModPackage::ResolveCamera(const std::string &name,const std::string &fallback)const{auto i=cameras.find(Lower(name));return i==cameras.end()?fallback:i->second;}
std::string CModPackage::ResolveMovie(const std::string &name,const std::string &fallback)const{
 fs::path filename=fs::path(name).filename();
 if(filename.empty()||filename=="."||filename=="..")return fallback;
 fs::path candidate=fs::path(root)/"Movies"/filename;
 return fs::is_regular_file(candidate)?candidate.string():fallback;
}
std::string CModPackage::ResolveSaveRoot(const std::string &fallback)const{
 if(!saveInMod)return fallback;
 std::string path=root;
 if(!path.empty()&&path.back()!='\\'&&path.back()!='/')path+='\\';
 return path;
}
std::string CModPackage::ResolveMap(const std::string &relative,const std::string &fallback)const{auto i=maps.find(Lower(relative));return i==maps.end()?fallback:i->second;}
Json CModPackage::Lookup(const std::string &table,const Json &key,int column,const Json &fallback){
 if(!state||lookupRef<0)throw std::runtime_error("Lua data not initialized");if(!key.is_string()&&!key.is_number())throw std::runtime_error("Lookup key must be string or number");if(column<1)throw std::runtime_error("Lua columns start at one");
 Stack stack(state);lua_rawgeti(state,LUA_REGISTRYINDEX,lookupRef);lua_pushlstring(state,table.data(),table.size());Push(state,key);lua_pushinteger(state,column);Push(state,fallback);Call(4,1);return Value(state,-1);
}
bool CModPackage::ResourcePlan(const std::string&name,std::vector<std::string>&paths,bool&ignore)const{
 auto key=Lower(fs::path(name).filename().string());auto it=archives.find(key);auto skip=ignoreBase.find(key);ignore=skip!=ignoreBase.end()&&skip->second;if(it!=archives.end())paths=it->second;return it!=archives.end()||ignore;
}
CMappedResource &CModPackage::Open(const std::string &path){auto &slot=opened[path];if(!slot){if(!fs::is_regular_file(path))throw std::runtime_error("Missing archive: "+path);auto p=std::make_unique<CMappedResource>(true);if(!p->OpenResource(path.c_str()))throw std::runtime_error("Cannot open archive: "+path);slot=std::move(p);}return *slot;}
std::vector<unsigned char> CModPackage::ReadResource(const std::string &category,const std::string &entry,const std::string &base,std::string &owner){
 owner.clear();auto key=Lower(category);auto it=archives.find(key);std::vector<std::string> paths;if(it!=archives.end())paths=it->second;if(!ignoreBase[key]&&!base.empty())paths.push_back(base);
 for(const auto &path:paths){auto &res=Open(path);DWORD size=0;BYTE *data=res.GetFile(entry.c_str(),&size);if(!data)continue;std::vector<unsigned char> result;try{result.assign(data,data+size);}catch(...){res.CloseFile(data);throw;}res.CloseFile(data);owner=path;return result;}return {};
}
Json CModPackage::Describe(){
 Json report={{"stage","package-config-resources-lua-data"},{"gameplayActivated",false},{"fullMainExecuted",false},{"mainScript",mainScript},{"configCount",configs.size()},{"mapCount",maps.size()},{"resourceGroups",Json::array()},{"luaTables",Json::array()}};
 unsigned resourceProbes=0,overlapProbes=0;
 for(const auto &group:archives){Json files=Json::array();std::map<std::string,std::string> firstOwners;for(const auto &path:group.second){auto &res=Open(path);CStringArray names;res.GetFileList(names);files.push_back({{"file",fs::path(path).filename().string()},{"entries",names.GetSize()}});
   for(int n=0;n<names.GetSize();n++){std::string entry=(LPCSTR)names[n];auto inserted=firstOwners.emplace(Lower(entry),path);if(!inserted.second && n==0){std::string owner;ReadResource(group.first,entry,"",owner);if(owner!=inserted.first->second)throw std::runtime_error("RES override priority mismatch");overlapProbes++;}}
   if(names.GetSize()){std::string owner;auto data=ReadResource(group.first,(LPCSTR)names[0],"",owner);if(owner.empty())throw std::runtime_error("Archive entry cannot be resolved");resourceProbes++;}
 }report["resourceGroups"].push_back({{"category",group.first},{"files",files},{"ignoreBase",ignoreBase[group.first]}});}
 report["resourceReadProbes"]=resourceProbes;report["overlapOwnerProbes"]=overlapProbes;
 // Compare production C++ -> Lua lookup with the real index's final row, including missing properties.
 Stack stack(state);lua_rawgeti(state,LUA_REGISTRYINDEX,indexRef);int indexes=lua_gettop(state);lua_pushnil(state);unsigned comparisons=0;
 while(lua_next(state,indexes)){
   if(lua_type(state,-2)==LUA_TSTRING&&lua_istable(state,-1)){
     std::string table=lua_tostring(state,-2);int bucket=lua_gettop(state);unsigned keys=0;lua_pushnil(state);
     while(lua_next(state,bucket)){
       if(lua_istable(state,-1)){Json key=Value(state,-2);int count=int(lua_objlen(state,-1));if(count){lua_rawgeti(state,-1,count);int row=lua_gettop(state);int cols=int(lua_objlen(state,row));for(int col=1;col<=cols+1;col++){lua_rawgeti(state,row,col);int type=lua_type(state,-1);if(type==LUA_TNUMBER||type==LUA_TSTRING||type==LUA_TBOOLEAN||type==LUA_TNIL){Json expected=Value(state,-1);if(Lookup(table,key,col,-9999)!=expected)throw std::runtime_error("Lua table lookup mismatch: "+table);comparisons++;}lua_pop(state,1);}lua_pop(state,1);keys++;}}
       lua_pop(state,1);
     }
     if(table!="empty"&&Lookup(table,"__eirebuild_missing_key__",1,-9999)!=Json(-9999))throw std::runtime_error("Lua fallback mismatch");
     report["luaTables"].push_back({{"name",table},{"keys",keys}});
   }lua_pop(state,1);
 }
 report["luaValueComparisons"]=comparisons;
 bool rejected=false;try{Lookup("__missing_table__","x",1,nullptr);}catch(const std::runtime_error&){rejected=true;}
 if(!rejected||Lookup("UnitStat","__eirebuild_missing_key__",1,17)!=Json(17))throw std::runtime_error("Lua error recovery failure");
 report["luaErrorRecovery"]=true;
 if(ResolveMap("__missing_map__.mpr","fallback")!="fallback"||ResolveConfig("__missing_config__","fallback")!="fallback")throw std::runtime_error("Fallback contract mismatch");
 unsigned mapProbes=0;for(const auto &kv:maps){if(ResolveMap(kv.first,"")!=kv.second)throw std::runtime_error("Map index mismatch");mapProbes++;}report["mapLookupProbes"]=mapProbes;
 InitializeScript();
 report["fullMainExecuted"]=true;
 report["typedNativeBridge"]=ModLua::CheckNativeBridge(state);
 std::string perkOwner;ReadResource("database.res","perks.pdb","",perkOwner);
 if(perkOwner.empty())throw std::runtime_error("Package has no perks.pdb owner");
 std::string itemOwner;ReadResource("database.res","items.idb","",itemOwner);
 if(itemOwner.empty())throw std::runtime_error("Package has no items.idb owner");
 std::string unitOwner;ReadResource("database.res","units.udb","",unitOwner);if(unitOwner.empty())throw std::runtime_error("Package has no units.udb owner");
 report["engineDispatch"]=ModRuntime::CheckEngineDispatch(*this,state,perkOwner,itemOwner,unitOwner);
 report["success"]=true;return report;
}
namespace {
Json CheckDefaultStats(){
 CMapObjectManager manager;
 lua_State* lua=luaL_newstate();if(!lua)throw std::runtime_error("Cannot create default stats probe Lua");
 struct CloseLua{lua_State*s;~CloseLua(){lua_close(s);}} close{lua};
 luaL_openlibs(lua);ModLua::Register(lua);
 auto scope=std::make_shared<ModLua::Scope>();
 for(int poison:{0xff,0xcd}){
  CUnitServer server;
  memset(server.GetRPGStats(),poison,sizeof(SRPGUnitStats));
  if(!server.Create()||server.GetRPGStats()->fBaseHP!=0)throw std::runtime_error("Server recycled stats not initialized");
  SRPGCommonStats early={};
  ModLua::Push(lua,server.GetRPGStats(),scope);lua_setglobal(lua,"s");ModLua::Push(lua,&early,scope);lua_setglobal(lua,"c");
  if(luaL_loadstring(lua,"c.hp.max=s.baseHp; assert(c.hp.max==0)")||lua_pcall(lua,0,0,0))throw std::runtime_error(lua_tostring(lua,-1));
  server.GetRPGStats()->fBaseHP=137;
  if(luaL_loadstring(lua,"c.hp.max=s.baseHp; assert(c.hp.max==137)")||lua_pcall(lua,0,0,0))throw std::runtime_error(lua_tostring(lua,-1));
  SRPGUnitStats stats;memset(&stats,poison,sizeof(stats));stats.Clear();
  // Demonstrate the old path: Clear leaves the NaN race coefficient untouched.
  if(poison==0xff&&std::isfinite(stats.hls[5].fpVitality))throw std::runtime_error("Poisoned default reproduction changed");
  SRPGCommonStats common={};common.ssHP.fpCurrent=common.ssHP.fpMax=1;
  manager.SetupDefaultStats(&common,&stats);
  for(const auto&h:stats.hls)if(!std::isfinite(h.fpVitality)||h.fpVitality!=0||h.fpHPWeight!=1)throw std::runtime_error("Default race fields not initialized");
  stats.DistributeHP(1);
  ModLua::Push(lua,&stats,scope);lua_setglobal(lua,"s");ModLua::Push(lua,&common,scope);lua_setglobal(lua,"c");
  if(luaL_loadstring(lua,"c.hp.curr=s:collectHp(c.hp.max); assert(c.hp.curr==1)")||lua_pcall(lua,0,0,0))throw std::runtime_error(lua_tostring(lua,-1));
  // Once a real race is assigned, its damage contribution must still apply.
  stats.hls[5].fpVitality=.25f;stats.hls[5].fpHP=0;
  if(luaL_loadstring(lua,"c.hp.curr=s:collectHp(c.hp.max); assert(c.hp.curr==0.75)")||lua_pcall(lua,0,0,0))throw std::runtime_error(lua_tostring(lua,-1));
 }
 scope->active=false;return {{"success",true},{"poisonCases",2},{"serverCreateToLuaBaseHp",true},{"nativeDefaultToLuaHp",true},{"assignedRaceDamage",true},{"gameplayActivated",false}};
}
Json CheckStartup(const std::string&directory){
 CModPackage expected;expected.Load(directory);
 CFileRegistry activeAudioConfig;
 if(!activeAudioConfig.Open(expected.ResolveConfig("ai.reg","config\\ai.reg").c_str()))
  throw std::runtime_error("Installed audio registry could not be opened");
 ConfigureModAudio(activeAudioConfig,directory);
 bool showAllQuestsConfigured=activeAudioConfig.GetInt("Addon","ShowAllQuests",0)!=0;

 CMappedResource native;CWin32MappedResource mapped;int entries=0;std::string base=(fs::current_path()/"res/database.res").string();
 DWORD zoneXSectors=0,zoneYSectors=0;
 DWORD femaleAnimationCount=0,maleAnimationCount=0,twoHandedLootAxe=0,twoHandedLootSword=0;
 {
  ModRuntime::Host host(directory);
  if(!ModRuntime::Current()||!ModRuntime::Resources())throw std::runtime_error("Host did not bind runtime and resources");
  if((ModRuntime::Current()->CombatSetting("ShowAllQuests")!=0)!=showAllQuestsConfigured)
   throw std::runtime_error("ShowAllQuests setting differs from active registry");
  if(!native.OpenResource(base.c_str())||!mapped.OpenResource(base.c_str()))throw std::runtime_error("Native database resources failed to open");
  for(const char*name:{"units.udb","items.idb","perks.pdb"}){
   std::string owner;auto bytes=expected.ReadResource("database.res",name,base,owner);DWORD n=0,m=0;auto*a=native.GetFile(name,&n);auto*b=mapped.GetFile(name,&m);
   if(!a||!b||n!=bytes.size()||m!=n||memcmp(a,bytes.data(),n)||memcmp(b,bytes.data(),n))throw std::runtime_error("Native RES override bytes differ");
   CResourceBase*poly=&native;CResourceBase*polyMapped=&mapped;if(!poly->CheckFileExist(name)||!polyMapped->CheckFileExist(name))throw std::runtime_error("Native RES existence differs");
   if(!native.CloseFile(a)||!mapped.CloseFile(b))throw std::runtime_error("Resource owner failed to close entry");entries++;
  }
  CStringArray names;names.Add("__stale__");mapped.GetFileList(names,"*.udb");if(names.GetSize()==0)throw std::runtime_error("Overlay enumeration empty");
  for(int i=0;i<names.GetSize();i++)if(names[i]=="__stale__")throw std::runtime_error("Enumeration retained stale output");
  const char* ordinary[] = {"uspecial22","uspecial04","uspecial05","uspecial06","uspecial07","uspecial24"};
  const char* axe[] = {"2hauspecial22","2hauspecial04","2hauspecial05","2hauspecial06","2hauspecial07","2hauspecial24"};
  const char* sword[] = {"2hsuspecial22","2hsuspecial04","2hsuspecial05","2hsuspecial06","2hsuspecial07","2hsuspecial24"};
  for(const char* adbName : {"unhufe.adb","unhuma.adb"}){
   DWORD adbSize=0;LPBYTE adb=mapped.GetFile(adbName,&adbSize);
   if(!adb||adbSize<sizeof(SADBFileHeader))throw std::runtime_error("Human animation database missing");
   SADBFileHeader adbHeader;memcpy(&adbHeader,adb,sizeof(adbHeader));
   if(adbHeader.dwSignature!=ADB_SIGNATURE||
      sizeof(SADBFileHeader)+size_t(adbHeader.dwCount)*sizeof(SADEntry)>adbSize)
       throw std::runtime_error("Human animation database invalid");
   if(strcmp(adbName,"unhufe.adb")==0)femaleAnimationCount=adbHeader.dwCount;
   else maleAnimationCount=adbHeader.dwCount;
   bool ordinaryFound[6]={},sourceFound[6]={};
   for(DWORD i=0;i<adbHeader.dwCount;i++){
    const SADEntry*entry=reinterpret_cast<const SADEntry*>(adb+sizeof(SADBFileHeader)+i*sizeof(SADEntry));
    for(int n=0;n<6;n++){
     if(strncmp(entry->pszAnimationName,ordinary[n],16)==0)ordinaryFound[n]=true;
     if(strncmp(entry->pszAnimationName,axe[n],16)==0 || strncmp(entry->pszAnimationName,sword[n],16)==0)sourceFound[n]=true;
    }
    if(strcmp(adbName,"unhufe.adb")==0){
     if(strncmp(entry->pszAnimationName,"2hauspecial22",16)==0)twoHandedLootAxe=entry->dwEntryIndex;
     if(strncmp(entry->pszAnimationName,"2hsuspecial22",16)==0)twoHandedLootSword=entry->dwEntryIndex;
    }
   }
   for(int n=0;n<6;n++)if(sourceFound[n]&&!ordinaryFound[n])throw std::runtime_error("Two-handed interaction has no ordinary replacement");
   mapped.CloseFile(adb);
  }
  CFileRegistry config;if(!config.Open("config\\ai.reg"))throw std::runtime_error("Config consumer did not resolve mod registry");
  std::string actual=ModRuntime::ConfigPath("config\\ai.reg"),wanted=expected.ResolveConfig("ai.reg","config\\ai.reg");if(actual!=wanted)throw std::runtime_error("Config consumer mismatch");
  std::string baseCamera=(fs::current_path()/"camera/mainmenu.cam").string();
  std::string modCamera=ModRuntime::CameraPath(baseCamera.c_str());
  if(modCamera!=expected.ResolveCamera("mainmenu.cam",baseCamera)||!fs::is_regular_file(modCamera)||fs::equivalent(modCamera,baseCamera))throw std::runtime_error("Camera consumer mismatch");
  auto mapRoot=fs::path(directory)/"Maps";bool mapTest=false;for(const auto&f:fs::recursive_directory_iterator(mapRoot))if(f.is_regular_file()&&f.path().extension()==".mpr"){
   auto relative=fs::relative(f.path(),mapRoot).string();std::string candidate="maps\\"+relative,resolved=ModRuntime::MapPath(candidate.c_str());if(fs::path(resolved)!=f.path())throw std::runtime_error("Map path mismatch");CFileSave file;if(!file.OpenRead(candidate.c_str()))throw std::runtime_error("Native map read failed");file.CloseFile();mapTest=true;break;}
  if(!mapTest)throw std::runtime_error("No installed map fixture");
  CWin32MappedResource zone;
  if(!zone.OpenResource("maps\\zone1.mpr"))throw std::runtime_error("Native zone1 map failed to open");
  DWORD headerSize=0;LPBYTE data=zone.GetFile("zone1.mp",&headerSize);
  if(!data||headerSize<sizeof(SMapHeader))throw std::runtime_error("Native zone1 header missing");
  SMapHeader header;memcpy(&header,data,sizeof(header));
  if(header.dwSignature!=DW_SMAPHEADER_SIGNATURE)throw std::runtime_error("Native zone1 header invalid");
  zoneXSectors=header.dwXSectorsCount;zoneYSectors=header.dwYSectorsCount;
  zone.CloseFile(data);
  CMappedResource gameplayZone;
  if(!gameplayZone.OpenResource("maps\\zone1.mpr"))throw std::runtime_error("Gameplay map reader failed to open");
  DWORD gameplaySize=0;LPBYTE gameplayData=gameplayZone.GetFile("zone1.mp",&gameplaySize);
  if(!gameplayData||gameplaySize<sizeof(SMapHeader))throw std::runtime_error("Gameplay map header missing");
  SMapHeader gameplayHeader;memcpy(&gameplayHeader,gameplayData,sizeof(gameplayHeader));
  gameplayZone.CloseFile(gameplayData);
  if(gameplayHeader.dwSignature!=DW_SMAPHEADER_SIGNATURE||
     gameplayHeader.dwXSectorsCount!=zoneXSectors||gameplayHeader.dwYSectorsCount!=zoneYSectors)
   throw std::runtime_error("Gameplay terrain reader used different map dimensions");
 }
 if(ModRuntime::Current()||ModRuntime::Resources())throw std::runtime_error("Host leaked its binding");
 DWORD size=0;auto*p=mapped.GetFile("units.udb",&size);if(!p||!mapped.CloseFile(p))throw std::runtime_error("Resource handle lifetime depended on destroyed Host");
 return {{"success",true},{"hostLifecycle",true},{"nativeReaderEntries",entries},{"mappedReaderEntries",entries},{"resourceOwnership",true},{"enumeration",true},{"femaleAnimationCount",femaleAnimationCount},{"maleAnimationCount",maleAnimationCount},{"twoHandedLootAxeIndex",twoHandedLootAxe},{"twoHandedLootSwordIndex",twoHandedLootSword},{"configConsumer",true},{"showAllQuestsSetting",showAllQuestsConfigured},{"cameraConsumer",true},{"cameraPath",expected.ResolveCamera("mainmenu.cam","")},{"mapReadConsumer",true},{"gameplayMapReader",true},{"zone1XSectors",zoneXSectors},{"zone1YSectors",zoneYSectors},{"gameplayActivated",false}};
}
}
float CModPackage::ExperienceMultiplier(const SRPGUnitStats&stats){return StatsNumber("GetExpMultiplierOnServer",stats);}
float CModPackage::ExperienceMultiplier(const SRPGUserUnitStats&stats){CallbackFrame frame(state);BeginCallback("GetExpMultiplierOnClient");ModLua::Push(state,&stats,frame.scope);Call(1,1);double n=Number(state,"Client experience multiplier");if(fabs(n)>(std::numeric_limits<float>::max)())throw std::runtime_error("Experience multiplier overflow");return float(n);}
float CModPackage::ScalarManaBonus(float intelligence){
 CallbackFrame frame(state);
 if(!scriptInitialized)throw std::runtime_error("Mod script has not been initialized");
 const bool cacheable=std::isfinite(intelligence);
 const std::string key(reinterpret_cast<const char*>(&intelligence),sizeof(intelligence));
 if(cacheable){
  auto it=manaBonusCache.find(key);
  if(it!=manaBonusCache.end())return it->second;
 }
 BeginCallback("GetManaBonusFromIntelligence");
 lua_pushnumber(state,intelligence);
 Call(1,1);
 double n=Number(state,"GetManaBonusFromIntelligence");
 if(fabs(n)>(std::numeric_limits<float>::max)())
  throw std::runtime_error("GetManaBonusFromIntelligence: float overflow");
 const float value=static_cast<float>(n);
 if(cacheable){
  if(manaBonusCache.size()>=4096)manaBonusCache.clear();
  manaBonusCache[key]=value;
 }
 return value;
}
float CModPackage::ManaBonus(const SRPGUnitStats&stats){if(scalarManaBonus)return ScalarManaBonus(stats.psIntelligence.fpEffectiveValue);return StatsNumber("GetManaBonusOnServer",stats);}
float CModPackage::SpellAddonManaBonus(const SRPGUnitStats&stats){
 CallbackFrame frame(state);SRPGUnitStats snapshot=stats;
 if(!scriptInitialized)throw std::runtime_error("Mod script has not been initialized");
 const float intelligence=stats.psIntelligence.fpEffectiveValue;
 const bool cacheable=scalarManaBonus && std::isfinite(intelligence);
 const std::string key(reinterpret_cast<const char*>(&intelligence),sizeof(intelligence));
 if(cacheable){auto it=spellAddonManaBonusCache.find(key);if(it!=spellAddonManaBonusCache.end())return it->second;}
 const char*callback=scalarManaBonus?"GetManaBonusFromIntelligence":"GetManaBonusOnServer";
 BeginCallback(callback);
 if(scalarManaBonus)lua_pushnumber(state,stats.psIntelligence.fpEffectiveValue);
 else ModLua::Push(state,&snapshot,frame.scope);
 Call(1,1);
 if(lua_type(state,-1)!=LUA_TNUMBER)
  throw std::runtime_error(std::string(callback)+": expected numeric result");
 // Keep IEEE results separate from the strict R01 cache. Only successful
 // scalar calls with finite intelligence are cached, as in the addon reader.
 const float value=static_cast<float>(lua_tonumber(state,-1));
 if(cacheable){if(spellAddonManaBonusCache.size()>=4096)spellAddonManaBonusCache.clear();spellAddonManaBonusCache[key]=value;}
 return value;
}
float CModPackage::VampSpellValue(const char*prototype,int column){
 CallbackFrame frame(state);
 const bool validKey=ModStaticLookup::NameKey(prototype);std::string key;
 if(validKey){ModStaticLookup::KeyString(key,prototype);ModStaticLookup::KeyValue(key,column);}
 return staticLookups.vamp.Get(key,validKey && scriptInitialized && staticLookups.Eligible(state,1),[&](bool& valid){
  BeginCallback("GetVampSpellAdds");lua_pushstring(state,prototype);lua_pushinteger(state,column);Call(2,1);
  double n=Number(state,"Vamp spell property");if(fabs(n)>(std::numeric_limits<float>::max)())throw std::runtime_error("Vamp property overflow");
  valid=true;return float(n);
 });
}
ModSpellTooltip::Values CModPackage::SpellTooltip(const SRPGUserUnitStats &stats,
    const CSpell &spell, float manaIntelligence, int difficulty)
{
 const float additional = InfoProperty("GetSpellAdds", spell.GetPrototype()->szCodeName, 2);
 return ModSpellTooltip::Calculate(stats, spell, additional, manaIntelligence, difficulty);
}

void CModPackage::SpellTooltipOverrides(CUnit* server,ModSpellTooltip::Values& values,float& ticks)
{
 ModSettingsPolicy::Tooltip(server!=nullptr,CombatSetting("P100MCalcSpellManaCostTooltip")!=0,
  CombatSetting("P100MCalcSpellEffectTooltip")!=0,CombatSetting("P100MCalcSpellDurationTooltip")!=0,
  server?float(server->GetUserUnitStats()->cIntelligence):1.f,values.mana,values.effect,ticks,
  [&](int field){const char*names[]={"CalcSpellManaCostTooltip","CalcSpellEffectTooltip","CalcSpellDurationTooltip"};
   CallbackFrame frame(state);BeginCallback(names[field]);ModLua::Push(state,server->GetUserUnitStats(),frame.scope);
   ModLua::Push(state,server,frame.scope);Call(2,1);return float(Number(state,names[field]));});
}


float CModPackage::ManaBonus(const SRPGUserUnitStats&stats){if(scalarManaBonus)return ScalarManaBonus(static_cast<float>(stats.cIntelligence));CallbackFrame frame(state);BeginCallback("GetManaBonusOnClient");ModLua::Push(state,&stats,frame.scope);Call(1,1);double n=Number(state,"Client mana multiplier");if(fabs(n)>(std::numeric_limits<float>::max)())throw std::runtime_error("Mana multiplier overflow");return float(n);}

void CModPackage::RecalcShops(SShopSet&shops,bool lmp){
 const size_t expected=lmp?4:5;if(shops.shops.size()!=expected)throw std::runtime_error("Lua shop layout expects four LMP or five campaign shops");
 std::vector<float> values;values.reserve(expected*9);for(auto&shop:shops.shops)for(int j=0;j<9;j++)values.push_back(shop->fKoeffs[j]);
 CallbackFrame frame(state);BeginCallback("RecalcShopsPriceMultipliers");ModLua::PushFloats(state,values.data(),int(values.size()),frame.scope);lua_pushboolean(state,lmp);Call(2,0);
 for(float value:values)if(!std::isfinite(value))throw std::runtime_error("Invalid shop multiplier");
 size_t index=0;for(auto&shop:shops.shops)for(int j=0;j<9;j++)shop->fKoeffs[j]=values[index++];
}

bool CModPackage::GenocideMapConfig(const char* mobName, ModGenocideConfig& config){
 config=ModGenocideConfig{};
 if(!state||!mobName||!*mobName)return false;
 Stack stack(state);
 lua_getglobal(state,"GetGenocideMapConfig");
 if(!lua_isfunction(state,-1))return false;
 lua_pushstring(state,mobName);
 if(lua_pcall(state,1,5,0)!=0)return false;
 const int first=stack.n+1;
 if(!lua_isnumber(state,first)||!lua_isnumber(state,first+1)||
    !lua_isnumber(state,first+2)||!lua_isstring(state,first+3)||
    !lua_isstring(state,first+4))return false;
 const double population=lua_tonumber(state,first);
 const double stageOne=lua_tonumber(state,first+1);
 const double stageTwo=lua_tonumber(state,first+2);
 const char* resultVar=lua_tostring(state,first+3);
 const char* eligibleGroup=lua_tostring(state,first+4);
 if(population<1||population>10000||population!=int(population)||
    stageOne<1||stageOne>99||stageOne!=int(stageOne)||
    stageTwo<1||stageTwo>100||stageTwo!=int(stageTwo)||
    stageOne>=stageTwo||!resultVar||!*resultVar||
    !eligibleGroup||!*eligibleGroup)return false;
 config.population=int(population);
 config.stageOnePercent=int(stageOne);
 config.stageTwoPercent=int(stageTwo);
 config.resultVar=resultVar;
 config.eligibleGroup=eligibleGroup;
 return true;
}
std::string CModPackage::LoadVideo(const char* zoneName){
 if(!scriptInitialized||!CombatSetting("JMVLoadVideos"))return {};
 CallbackFrame frame(state);
 lua_getglobal(state,"GetLoadVideo");
 if(!lua_isfunction(state,-1))return {};
 lua_pushstring(state,zoneName?zoneName:"");
 Call(1,1);
 if(!lua_isstring(state,-1))return {};
 size_t length=0;
 const char*name=lua_tolstring(state,-1,&length);
 return name?std::string(name,length):std::string();
}
bool CModPackage::IsLocalPartyUnit(const CUnit&unit) const {
 if(!partyVisualOptions)return false;
 CPlayerClient*clientSpecific=theWorld.GetPlayer();
 CPlayer*client=clientSpecific?clientSpecific->GetPlayer():nullptr;
 if(!client)return false;
 CPlayer*server=theWorldServer.GetPlayer(client->GetPlayerNumber());
 CPlayer*owner=unit.GetOwner();
 const int id=unit.GetID();
 if(owner)return (owner==client&&client->party.GetPersonage(id)!=nullptr)
     ||(owner==server&&server&&server->party.GetPersonage(id)!=nullptr);
 return client->party.GetPersonage(id)!=nullptr
     ||(server&&server->party.GetPersonage(id)!=nullptr);
}
void CModPackage::FilterPartyVisualArmor(const CUnit&unit,unsigned long* armors) const {
 if(!IsLocalPartyUnit(unit))return;
 if(hidePartyHelmets)armors[RPGAST_HELM]=DWORD(-1);
 if(hidePartyUpperArmor){armors[RPGAST_PLATE]=DWORD(-1);armors[RPGAST_LEGGINGS]=DWORD(-1);}
}
void CModPackage::ApplyPartyVisualOptions(){
 if(!partyVisualOptions)return;
 CPlayerClient*clientSpecific=theWorld.GetPlayer();
 CPlayer*client=clientSpecific?clientSpecific->GetPlayer():nullptr;
 if(!client)return;
 for(int i=0;i<client->party.people.GetSize();++i){
  const int id=client->party.people[i].nID;
  CUnit*server=theWorldServer.GetUnit(id);
  CUnit*shown=theWorld.GetUnit(id);
  if(server){server->ShowHelmet(!hidePartyHelmets);server->SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);}
  if(shown){shown->ShowHelmet(!hidePartyHelmets);shown->RefreshPartyVisualArmor();}
 }
}
void CModPackage::SetPartyVisualOptions(bool helmets,bool upperArmor){
 if(!partyVisualOptions)return;
 hidePartyHelmets=helmets;hidePartyUpperArmor=upperArmor;
 WritePartyVisualOption(hideHelmetsValue,helmets);
 WritePartyVisualOption(hideUpperArmorValue,upperArmor);
 ApplyPartyVisualOptions();
}
// Native camp aggregate (Patch.cpp GetMaxManaAndComplexity), separate from
// individual spell casting and the mission/camp tooltip calculators.
void CModPackage::CampSpellLimits(int shop,int subtype,const std::vector<ModCampSpellLimitInput>& party,int& complexity,int& mana){
 int maxComplexity=0,maxMana=0;const int limit=ShopConstructLimit(shop);
 for(const auto& person:party){
  if(!person.partyStats)throw std::runtime_error("Camp spell limits require party stats");
  const auto& stats=*person.partyStats;
  const float skill=stats.GetBaseMagicSkill(ERPGMagicSubtypes(subtype))+stats.GetPerkModifier(RPGPRK_FIRE+subtype);
  const int intelligence=person.displayedIntelligence-25;
  const int candidate=int(skill+intelligence);
  maxComplexity=(std::max)(maxComplexity,candidate);
  maxMana=(std::max)(maxMana,int(CalcBaseMana(stats)));
  maxComplexity=(std::max)(limit,maxComplexity);
 }
 complexity=maxComplexity;mana=maxMana;
}
int CModPackage::ShopConstructLimit(int shop){
 CallbackFrame frame(state);
 BeginCallback("GetShopConstructLimit");
 lua_pushinteger(state,shop);
 lua_pushinteger(state,2);
 Call(2,1);
 const double value = Number(state,"GetShopConstructLimit");
 if (value < std::numeric_limits<int>::lowest() || value > (std::numeric_limits<int>::max)())
     throw std::runtime_error("GetShopConstructLimit: integer overflow");
 return int(value);
}
nlohmann::json CModPackage::CheckEconomy(){
 ModRuntime::Session session(*this);
 auto require=[](bool v,const char*msg){if(!v)throw std::runtime_error(msg);};
 auto number=[&](const char*fn,float n){CallbackFrame frame(state);BeginCallback(fn);lua_pushnumber(state,n);Call(1,1);return Number(state,fn);};
 auto run=[&](const char*code){if(luaL_loadstring(state,code)||lua_pcall(state,0,0,0))throw std::runtime_error(lua_tostring(state,-1));};
 Json shopResults=Json::array();int scalarCases=0;
 for(int intelligence:{10,25,50,100,150,255}){
  SRPGUnitStats server={};server.psIntelligence.fpEffectiveValue=float(intelligence);server.fpExperience=100;server.fExpPenalty=-50;
  SRPGUserUnitStats client={};client.cIntelligence=BYTE(intelligence);
  double expected=pow(1.01,intelligence-25);float multiplier=server.GetExpMultiplier();
  require(fabs(multiplier-expected)<.0001*expected&&multiplier==client.GetExpMultiplier(),"Server/client XP multiplier mismatch");
  double mana=number("GetManaBonus",float(intelligence));require(fabs(server.GetManaBonusMultiplier()-mana)<.0001&&server.GetManaBonusMultiplier()==client.GetManaBonusMultiplier(),"Server/client mana multiplier mismatch");
  server.GainExperience(10);double penalty=-50+10*expected;
  require(fabs(server.fpExperience-(100+(std::max)(0.0,penalty)))<.001&&fabs(server.fExpPenalty-(std::min)(0.0,penalty))<.001,"Experience penalty repayment mismatch");scalarCases++;
 }
 for(bool lmp:{false,true}){
  SShopSet shops;for(int i=0;i<(lmp?4:5);i++){CShop*shop=new CShop;for(int j=0;j<NUM_SHOP_KOEFFS;j++)shop->fKoeffs[j]=float(100+i*10+j);shops.shops.push_back(shop);}
  RecalcShops(shops,lmp);
  Json result=Json::array();for(auto&shop:shops.shops){Json row=Json::array();for(float v:shop->fKoeffs)row.push_back(v);result.push_back(row);}shopResults.push_back(result);
  // Read the installed table assignments as an independent oracle in the external checker;
  // native sentinels here check first entries, untouched campaign shop, and the tenth slot.
  require(shops.shops[0]->fKoeffs[4]==15&&shops.shops[1]->fKoeffs[2]==6.5f&&shops.shops[2]->fKoeffs[8]==.45f,"Installed shop coefficients not applied");
  for(int i=0;i<(lmp?4:5);i++)require(shops.shops[i]->fKoeffs[9]==float(109+i*10),"Tenth shop slot changed");
  if(!lmp)for(int j=0;j<9;j++)require(shops.shops[4]->fKoeffs[j]==float(140+j),"Untouched fifth shop changed");
  std::vector<float> before;for(auto&shop:shops.shops)for(float v:shop->fKoeffs)before.push_back(v);
  run("__r19_shop_callback=RecalcShopsPriceMultipliers; RecalcShopsPriceMultipliers=function(addr,lmp) local a=Array(addr,Memory.Type.Float,lmp and 36 or 45,false); __r19_expired=a; a:set(0,999); error('rollback probe') end");
  bool failed=false;try{RecalcShops(shops,lmp);}catch(const std::runtime_error&){failed=true;}
  run("RecalcShopsPriceMultipliers=__r19_shop_callback; __r19_shop_callback=nil; assert(not pcall(function() return __r19_expired:value(0) end)); __r19_expired=nil");
  require(failed,"Shop failed callback accepted");size_t n=0;for(auto&shop:shops.shops)for(float v:shop->fKoeffs)require(v==before[n++],"Partial shop write escaped rollback");
 }
 return {{"success",true},{"shopCoefficients",shopResults},{"scalarCases",scalarCases},{"shopModes",2},{"shopRollback",true},{"expiredShopArrayRejected",true},{"nativeExperiencePenalty",true},{"clientServerParity",true},{"gameplayActivated",false}};
}
namespace {
Json CheckItemDetails(const std::string &directory){
 // Presentation component only: installed DB/Lua, no world/window/save/audio device.
 ModRuntime::Host host(directory);auto*package=ModRuntime::Current();std::string owner;
 const std::string base=(fs::path(directory).parent_path().parent_path()/"Base/res/database.res").string();
 package->ReadResource("database.res","items.idb",base,owner);
 if(!theItemDatabase.Load("",owner.c_str()))throw std::runtime_error("Item details DB unavailable");
 Json report={{"success",true},{"gameplayActivated",false},{"itemOwner",owner},{"weapons",Json::array()},
  {"materials",Json::array()},{"armors",Json::array()},{"sets",Json::array()},{"flow",Json::array()}};
 for(int level=0;level<4;++level){Json row={{"level",level}};for(const char*kind:{"Item","Wand","Staff"}){
  std::string callback=std::string("Get")+kind+"RestBonusFromPerkFlow";float bonus=package->InfoPerkBonus(callback.c_str(),level);
  row[kind]={{"bonus",bonus},{"regen",ModItemDetails::EnergyRegen(100,level,bonus)}};
 }report["flow"].push_back(row);}
 auto properties=[&](const char*callback,const char*name,int first,int last){Json row=Json::array();for(int p=first;p<=last;++p)row.push_back(package->InfoProperty(callback,name,p));return row;};
 auto setInfo=[&](const char*name){int set=int(package->InfoProperty("GetSetItemStats",name,2));if(!set)return;
  Json row={{"name",name},{"set",set},{"size",package->InfoComplexProperty(set,3)},{"counts",Json::array()}};
  for(int count=0;count<=13;++count)row["counts"].push_back({{"count",count},{"item",package->InfoSetBonuses(name,count,set,false)},{"complex",package->InfoSetBonuses(name,count,set,true)}});
  report["sets"].push_back(row);
 };
 for(int i=0;i<theItemDatabase.weapons.GetNumEntries();++i){auto*p=theItemDatabase.weapons[i];CItemWeapon item;item.Setup(p);
  float chance=package->InfoProperty("GetWeaponPropsInside",p->szName,4),critical=package->InfoProperty("GetWeaponPropsInside",p->szName,5);
  float dps=ModItemDetails::Dps(item.GetDamageMin(),item.GetDamageRandomRange(),p->dwActionPoints,chance,critical);
  report["weapons"].push_back({{"id",i},{"name",std::string((LPCSTR)p->szName)},{"type",p->dwTypeID},{"minimum",item.GetDamageMin()},
   {"random",item.GetDamageRandomRange()},{"actions",p->dwActionPoints},{"range",item.GetRange()},{"distribution",Json::array()},
   {"dps",dps},{"dpsText",std::string((LPCSTR)ModItemDetails::DpsText(dps))},{"props",properties("GetWeaponPropsInside",p->szName,2,7)},{"unique",!p->bDeconstructable}});
  for(float value:p->fpDamageDistributions)report["weapons"].back()["distribution"].push_back(value);setInfo(p->szName);
 }
 for(int i=0;i<theItemDatabase.armors.GetNumEntries();++i){auto*p=theItemDatabase.armors[i];report["armors"].push_back({{"id",i},{"name",std::string((LPCSTR)p->szName)},
  {"props",properties("GetArmorPropsInside",p->szName,2,7)},{"unique",!p->bDeconstructable}});setInfo(p->szName);}
 for(int i=0;i<theItemDatabase.quickItems.GetNumEntries();++i)setInfo(theItemDatabase.quickItems[i]->szName);
 for(int i=0;i<theItemDatabase.materials.GetNumEntries();++i){auto*p=theItemDatabase.materials[i];report["materials"].push_back({{"id",i},{"name",std::string((LPCSTR)p->szName)},{"props",properties("GetMaterialPropsInside",p->szName,2,14)}});}
 return report;
}
Json CheckItemInfo(const std::string &directory){
 // Database/presentation component only, no window/world/save/audio device.
 ModRuntime::Host host(directory);auto*package=ModRuntime::Current();std::string spellOwner,itemOwner;
 const std::string base=(fs::path(directory).parent_path().parent_path()/"Base/res/database.res").string();
 package->ReadResource("database.res","spells.sdb",base,spellOwner);
 package->ReadResource("database.res","items.idb",base,itemOwner);
 if(!theSpellDatabase.Load("",spellOwner.c_str())||!theItemDatabase.Load("",itemOwner.c_str()))throw std::runtime_error("Item-info databases unavailable");
 Json report={{"success",true},{"gameplayActivated",false},{"spellOwner",spellOwner},{"itemOwner",itemOwner},
  {"spells",Json::array()},{"materials",Json::array()},{"weapons",Json::array()},{"armors",Json::array()},{"quickItems",Json::array()},
  {"compactRows",package->CombatSetting("JMVItemInfoLineHeight")}};
 auto forbidden=[&](const char*name){Json rows=Json::array();for(int shop=0;shop<=12;++shop)rows.push_back(package->InfoForbiddenSpell(name,shop));return rows;};
 auto absorption=[](const float*values){CString labels[RPG_NUM_ATTACK_TYPES-1];for(int i=0;i<RPG_NUM_ATTACK_TYPES-1;++i)labels[i].Format("d%d",i);
  CStringArray rows;ModItemInfo::Absorptions(values,labels,rows,false);Json result=Json::array();for(int i=0;i<rows.GetSize();++i)result.push_back(std::string((LPCSTR)rows[i]));return result;};
 for(int i=0;i<theSpellDatabase.spells.GetNumEntries();++i){auto*p=theSpellDatabase.spells[i];const float additional=package->InfoProperty("GetSpellAdds",p->szCodeName,2);
  report["spells"].push_back({{"id",i},{"name",std::string((LPCSTR)p->szCodeName)},{"mana",p->fpMana},{"effect",p->fpEffect},{"actions",p->dwActions},
   {"area",p->fpArea},{"subtype",int(p->mstSubtype)},{"effectRune",p->bRuneAllowed[SDBAR_EFFECT]},
   {"effectText",std::string((LPCSTR)ModItemInfo::PrototypeEffect(p->fpEffect,additional))},
   {"potionText",std::string((LPCSTR)ModItemInfo::PotionEffect(p->fpEffect,additional,p->bRuneAllowed[SDBAR_EFFECT],"fixed"))},
   {"castSeconds",ModItemInfo::PrototypeCast(p->dwActions)},{"radius",ModItemInfo::PrototypeRadius(p->fpArea)}});
 }
 for(int i=0;i<theItemDatabase.materials.GetNumEntries();++i){auto*p=theItemDatabase.materials[i];Json absorbs=Json::array();
  for(float value:p->fpAbsorbtionMultipliers)absorbs.push_back(value);
  report["materials"].push_back({{"id",i},{"name",std::string((LPCSTR)p->szName)},{"weight",p->fpWeight},{"mana",p->fpManaCapacity},
   {"slots",p->dwSpellSlots},{"durability",p->fpDurability},{"damageModifier",p->fpDamageModifier},{"absorbs",absorbs},{"absorptionText",absorption(p->fpAbsorbtionMultipliers)}});
 }
 for(int i=0;i<theItemDatabase.weapons.GetNumEntries();++i){auto*p=theItemDatabase.weapons[i];const int weight=int(package->InfoProperty("GetWeaponPropsInside",p->szName,7));
  const char*typeKey=ModItemInfo::WeaponTypeKey(p->dwTypeID);const char*weightKey=ModItemInfo::WeaponWeightKey(p->dwTypeID,weight);
  report["weapons"].push_back({{"id",i},{"name",std::string((LPCSTR)p->szName)},{"type",p->dwTypeID},{"weightClass",weight},
   {"typeKey",typeKey?typeKey:""},{"weightKey",weightKey?weightKey:"NOT SET"},{"forbidden",forbidden(p->szName)}});
 }
 for(int i=0;i<theItemDatabase.armors.GetNumEntries();++i){auto*p=theItemDatabase.armors[i];const char*weightKey=ModItemInfo::ArmorWeightKey(p->szName);
  CItemArmor item;item.Setup(p);float main[RPG_NUM_ATTACK_TYPES];item.GetAbsorbtions(main,0);Json values=Json::array(),recipe=Json::array();
  for(int n=0;n<RPG_NUM_ATTACK_TYPES;++n){values.push_back(main[n]);recipe.push_back(p->main.fpDistributions[n]*p->main.fpAbsorbtion*10);}
  float recipeValues[RPG_NUM_ATTACK_TYPES];for(int n=0;n<RPG_NUM_ATTACK_TYPES;++n)recipeValues[n]=p->main.fpDistributions[n]*p->main.fpAbsorbtion*10;
  report["armors"].push_back({{"id",i},{"name",std::string((LPCSTR)p->szName)},{"weightKey",weightKey?weightKey:std::string((LPCSTR)p->szName)},
   {"main",values},{"recipe",recipe},{"armorText",absorption(main)},{"recipeText",absorption(recipeValues)},{"forbidden",forbidden(p->szName)}});
 }
 for(int i=0;i<theItemDatabase.quickItems.GetNumEntries();++i){auto*p=theItemDatabase.quickItems[i];report["quickItems"].push_back({{"id",i},{"name",std::string((LPCSTR)p->szName)},{"weight",p->fpWeight},{"forbidden",forbidden(p->szName)}});}
 return report;
}
Json CheckCampSpellInfo(const std::string &directory){
 ModRuntime::Host host(directory);auto*package=ModRuntime::Current();
 std::string spellOwner,perkOwner;
 const std::string base=(fs::path(directory).parent_path().parent_path()/"Base/res/database.res").string();
 package->ReadResource("database.res","spells.sdb",base,spellOwner);
 package->ReadResource("database.res","perks.pdb",base,perkOwner);
 if(!theSpellDatabase.Load("",spellOwner.c_str())||!thePerkDatabase.LoadDatabase("",perkOwner.c_str()))throw std::runtime_error("Camp info databases unavailable");
 Json report={{"success",true},{"gameplayActivated",false},{"spellOwner",spellOwner},{"perkOwner",perkOwner},
  {"profiles",Json::array()},{"spells",Json::array()},{"skills",Json::array()},{"fonts",Json::array()},
  {"compactRows",package->CombatSetting("JMVItemInfoLineHeight")}};
 const int intellect[]={25,60,80,80,80,80,80,10},actions[]={15,19,17,0,200,11,17,15};
 const int wise[]={0,1,2,1,1,1,1,3},caster[]={0,1,1,0,1,0,1,0},difficulty[]={0,0,2,1,2,0,2,1};
 SRPGUserUnitStats profiles[8]={};
 for(int c=0;c<8;++c){auto&stats=profiles[c];stats.cIntelligence=BYTE(intellect[c]);stats.cAction=BYTE(actions[c]);
  stats.cAutoSkills[RPGASKL_ELEMENTAL]=37;stats.cAutoSkills[RPGASKL_SENSE]=61;stats.cAutoSkills[RPGASKL_ASTRAL]=93;
  stats.cPerks[31]=BYTE(wise[c]);stats.cPerks[21]=BYTE(caster[c]);
  if(c)for(int perk=RPGPRK_FIRE;perk<=RPGPRK_DOMINATION;++perk)stats.cPerks[perk]=1;
  stats.hls[0].state=RPGALS_ACTIVE;stats.hls[0].wHP=0;stats.hls[0].wMaxHP=0; // Camp never reads head HP.
  Json perks=Json::array();for(int i=0;i<RPG_NUM_PERKS;++i){auto*p=stats.GetPerk(i);perks.push_back({{"index",i},{"level",stats.cPerks[i]},{"modifier",p?p->dwModifier:0},{"exclusive",p?p->nPerkExclusive:-1}});}
  report["profiles"].push_back({{"intelligence",intellect[c]},{"action",actions[c]},{"wise",wise[c]},
   {"difficulty",difficulty[c]},{"ctrl",c==6},{"skills",{37,61,93}},{"perks",perks}});
 }
 for(int i=0;i<theSpellDatabase.spells.GetNumEntries();++i){auto*p=theSpellDatabase.spells[i];
  CSpell spell;spell.SetSubtypeID(DWORD(i)<<16);if(!spell.Setup(p))throw std::runtime_error("Camp spell setup failed");
  ModSkills::Config skill;
  if(package->SkillConfig(&spell,skill)){report["skills"].push_back({{"id",i},{"name",std::string((LPCSTR)p->szCodeName)},
   {"cost",skill.Cost(spell.GetManaCost())},{"requirement",skill.Requirement(float(spell.GetComplexity()))},
   {"archery",skill.requiredArchery>=0},{"damagePercent",skill.damageMultiplier*100.f},{"accuracy",skill.accuracyBonus},
   {"radius",skill.areaRadius},{"cooldown",skill.cooldownSeconds}});continue;}
  const float addition=package->InfoProperty("GetSpellAdds",p->szCodeName,2);Json values=Json::array(),forbidden=Json::array();
  for(int c=0;c<8;++c){auto v=ModCampSpellInfo::Calculate(profiles[c],spell,addition,difficulty[c],c==6);Json extras=Json::array();
   for(const auto&row:ModCampSpellInfo::Extras(spell,v))extras.push_back({{"label",row.label},{"value",std::string((LPCSTR)row.value)},{"perSecond",row.perSecond}});
   values.push_back({{"manaBonus",v.manaBonus},{"basePower",v.basePower},{"powerBonus",v.powerBonus},{"castBonus",v.castBonus},
    {"manaText",std::string((LPCSTR)ModCampSpellInfo::Mana(spell,v))},{"effectText",std::string((LPCSTR)ModCampSpellInfo::Effect(v))},
    {"castText",std::string((LPCSTR)ModCampSpellInfo::Cast(spell,v))},{"extras",extras}});
  }
  for(int shop=0;shop<=12;++shop)forbidden.push_back(package->InfoForbiddenSpell(p->szCodeName,shop));
  report["spells"].push_back({{"id",i},{"name",std::string((LPCSTR)p->szCodeName)},{"subtype",int(spell.GetMagicSubtype())},
   {"mana",spell.GetManaCost()},{"power",spell.GetPower()},{"actions",spell.GetActionPoints()},{"duration",spell.GetDuration()},
   {"prototypeDuration",p->dwDuration},{"range",spell.GetRange()},{"radius",spell.GetRadius()},
   {"values",values},{"forbidden",forbidden}});
 }
 for(int height:{600,1080,2160})for(int sign:{-1,1}){LOGFONTA originalData{};originalData.lfHeight=sign*(15*height/600);
  strcpy_s(originalData.lfFaceName,"Arial");CFont original;if(!original.CreateFontIndirect(&originalData))throw std::runtime_error("GDI component font creation failed");
  CFont*slot=&original;LOGFONTA during{};bool changed=false;
  {ModDescriptionFont smaller(&slot,height);changed=slot!=&original;GetObjectA(slot->GetSafeHandle(),sizeof(during),&during);}
  report["fonts"].push_back({{"screenHeight",height},{"before",originalData.lfHeight},{"after",during.lfHeight},
   {"changed",changed},{"restored",slot==&original},{"originalAlive",GetObjectType(original.GetSafeHandle())==OBJ_FONT}});
 }
 return report;
}
Json CheckSpellTooltips(const std::string &directory){
 // Component calculation only: no map, world tick, window, audio device or saved game.
 ModRuntime::Host host(directory);auto*package=ModRuntime::Current();
 std::string spellOwner,perkOwner;
 const std::string base=(fs::path(directory).parent_path().parent_path()/"Base/res/database.res").string();
 package->ReadResource("database.res","spells.sdb",base,spellOwner);
 package->ReadResource("database.res","perks.pdb",base,perkOwner);
 if(!theSpellDatabase.Load("",spellOwner.c_str()) || !thePerkDatabase.LoadDatabase("",perkOwner.c_str()))
  throw std::runtime_error("Spell tooltip component databases unavailable");
 Json report={{"success",true},{"gameplayActivated",false},{"spellOwner",spellOwner},
  {"perkOwner",perkOwner},{"profiles",Json::array()},{"spells",Json::array()},{"excludedSkills",Json::array()}};
 const int intellect[]={25,60,80,80,80,80,80,80},manaInt[]={25,60,80,80,80,99,80,80};
 const int actions[]={15,19,17,0,200,11,17,17},wise[]={0,1,2,1,1,1,1,1};
 const int caster[]={0,1,1,0,1,0,1,1},difficulty[]={0,0,2,1,2,0,2,1};
 const int headHp[]={100,50,0,100,100,25,100,0};
 SRPGUserUnitStats profiles[8]={};
 for(int c=0;c<8;++c){
  auto&stats=profiles[c];stats.cIntelligence=BYTE(intellect[c]);stats.cAction=BYTE(actions[c]);
  stats.cAutoSkills[RPGASKL_ELEMENTAL]=37;stats.cAutoSkills[RPGASKL_SENSE]=61;stats.cAutoSkills[RPGASKL_ASTRAL]=93;
  stats.cPerks[31]=BYTE(wise[c]);stats.cPerks[21]=BYTE(caster[c]);
  if(c)for(int perk=RPGPRK_FIRE;perk<=RPGPRK_DOMINATION;++perk)stats.cPerks[perk]=1;
  stats.hls[0].state=(c==0||c==5)?RPGALS_NOTUSED:RPGALS_ACTIVE;
  stats.hls[0].wHP=WORD(headHp[c]);stats.hls[0].wMaxHP=100;
  Json perks=Json::array();for(int i=0;i<RPG_NUM_PERKS;++i){auto*p=stats.GetPerk(i);perks.push_back({{"index",i},{"level",stats.cPerks[i]},{"modifier",p?p->dwModifier:0},{"exclusive",p?p->nPerkExclusive:-1}});}
  report["profiles"].push_back({{"intelligence",intellect[c]},{"manaIntelligence",manaInt[c]},
   {"action",actions[c]},{"wise",wise[c]},{"difficulty",difficulty[c]},
   {"headUsed",c!=0&&c!=5},{"headHp",headHp[c]},{"headMaxHp",100},
   {"skills",{37,61,93}},{"perks",perks}});
 }
 for(int i=0;i<theSpellDatabase.spells.GetNumEntries();++i){
  auto*p=theSpellDatabase.spells[i];ModSkills::Config skill;
  if(package->SkillConfig(p->szCodeName,skill)){report["excludedSkills"].push_back(std::string((LPCSTR)p->szCodeName));continue;}
  CSpell spell;spell.SetSubtypeID(DWORD(i)<<16);if(!spell.Setup(p))throw std::runtime_error("Spell setup failed");
  Json values=Json::array();
  for(int c=0;c<8;++c){auto v=package->SpellTooltip(profiles[c],spell,float(manaInt[c]),difficulty[c]);
   values.push_back({{"mana",v.mana},{"effect",v.effect},{"castSeconds",v.castSeconds}});}
  report["spells"].push_back({{"id",i},{"name",std::string((LPCSTR)p->szCodeName)},
   {"subtype",int(spell.GetMagicSubtype())},{"mana",spell.GetManaCost()},
   {"power",spell.GetPower()},{"actions",spell.GetActionPoints()},{"projectileSpeed",spell.GetSpeed()},
   {"effectRune",p->bRuneAllowed[SDBAR_EFFECT]},{"values",values}});
 }
 return report;
}
Json CheckUIRange(const std::string& directory){
 ModRuntime::Host host(directory);
 auto* package=ModRuntime::Resources();
 const std::string path=package->ResolveConfig("ai.reg", "config\\ai.reg");
 CAIGeneralSettings settings;settings.ReadFromFile(path.c_str());
 Json report={{"success",true},{"aiRegistry",path},{"settings",Json::object()}};
 report["settings"]["Logic/GuardStayTime"]=settings.fpGuardStayTime;
 report["settings"]["Logic/SentryStayTime"]=settings.fpSentryStayTime;
 report["settings"]["Logic/FollowMinDist"]=settings.fpFollowMinDist;
 report["settings"]["Logic/FollowMaxDist"]=settings.fpFollowMaxDist;
 report["settings"]["Logic/GuardRadius"]=settings.fpGuardRadius;
 report["settings"]["Logic/CallForHelpRadius"]=settings.fpCallForHelpRadius;
 report["settings"]["Logic/CallForHelpRadiusDefault"]=settings.fpCallForHelpRadiusDefault;
 report["settings"]["Logic/FollowLimit"]=settings.fpFollowLimit;
 report["settings"]["Logic/PlayerCallForHelp"]=settings.fpPlayerCallForHelp;
 report["settings"]["Logic/RangedRunDist"]=settings.fpRangedRunDist;
 report["settings"]["Logic/RangedAttackDist"]=settings.fpRangedAttackDist;
 report["settings"]["Logic/SwarmRadius"]=settings.fpSwarmRadius;
 report["settings"]["RPG/HumanCrossAnimationSpeed"]=settings.fpHumanCrossAnimationSpeed;
 report["settings"]["RPG/BackstabAdd"]=settings.fpBackstabAdd;
 report["settings"]["RPG/BackstabMult"]=settings.fpBackstabMult;
 report["settings"]["RPG/RotateSpeedMult"]=settings.fpRotateSpeedMult;
 report["settings"]["RPG/DamageLevel1"]=settings.fDamageLevel1;
 report["settings"]["RPG/DamageLevel1Value"]=settings.fDamageLevel1Value;
 report["settings"]["RPG/DamageLevel2"]=settings.fDamageLevel2;
 report["settings"]["RPG/DamageLevel2Value"]=settings.fDamageLevel2Value;
 report["settings"]["RPG/DamageLevelRunLimit"]=settings.fDamageLevelRunLimit;
 report["settings"]["RPG/Int1ExpBonus"]=settings.fInt1ExpBonus;
 report["settings"]["RPG/HP Val 1"]=settings.fHPVal1;
 report["settings"]["RPG/HP Val 2"]=settings.fHPVal2;
 report["settings"]["RPG/HP Val 3"]=settings.fHPVal3;
 report["settings"]["RPG/HP Val 4"]=settings.fHPVal4;
 report["settings"]["RPG/HP Val 5"]=settings.fHPVal5;
 report["settings"]["RPG/MP Val 1"]=settings.fMPVal1;
 report["settings"]["RPG/MP Val 2"]=settings.fMPVal2;
 report["settings"]["RPG/MP Val 3"]=settings.fMPVal3;
 report["settings"]["RPG/MP Val 4"]=settings.fMPVal4;
 report["settings"]["RPG/MP Val 5"]=settings.fMPVal5;
 report["settings"]["RPG/Skill Val 1"]=settings.fSkillVal1;
 report["settings"]["RPG/Skill Val 2"]=settings.fSkillVal2;
 report["settings"]["RPG/Perk Power Base"]=settings.fPerkPowerBase;
 report["settings"]["RPG/Consider Modifier"]=settings.fConsiderModifier;
 report["settings"]["RPG/Consider Log Base"]=settings.fConsiderLogBase;
 report["settings"]["RPG/Item Durability Critical"]=settings.fItemDurabilityCritical;
 report["settings"]["Scenario/QuestsToRespawn"]=settings.nQuestsToRespawn;
 report["settings"]["DifficultyLevels/Absorption"]={settings.fAbsorptionLevels[0],settings.fAbsorptionLevels[1],settings.fAbsorptionLevels[2]};
 report["settings"]["DifficultyLevels/Attack"]={settings.fAttackLevels[0],settings.fAttackLevels[1],settings.fAttackLevels[2]};
 report["settings"]["DifficultyLevels/Defence"]={settings.fDefenceLevels[0],settings.fDefenceLevels[1],settings.fDefenceLevels[2]};
 const fs::path game=fs::path(directory).parent_path().parent_path();
 const std::string resources=(game/"Base"/"Res").string()+"\\";
 if(!theItemDatabase.Load(resources.c_str(),"database.res"))
  throw std::runtime_error("Cannot load installed weapon database");
 std::string owner;package->ReadResource("database.res","items.idb",resources+"database.res",owner);
 report["itemsOwner"]=owner;report["weapons"]=Json::array();
 for(int i=0;i<theItemDatabase.weapons.GetNumEntries();++i){
  const auto* shape=theItemDatabase.weapons[i];
  CItemWeapon item;item.Setup(shape);
  if(item.GetRange()!=shape->fpRange || !std::isfinite(item.GetRange()))
   throw std::runtime_error("Weapon range differs after native Setup");
  report["weapons"].push_back({{"name",std::string((LPCSTR)shape->szName)},
   {"type",shape->dwTypeID},{"range",item.GetRange()},{"actions",shape->dwActionPoints}});
 }
 report["gameplayActivated"]=false;return report;
}

Json CheckConstructorLifetime(const std::string& directory)
{
 ModRuntime::Host host(directory);auto* package=ModRuntime::Current();
 Json report={{"success",true},{"gameplayActivated",false},{"guardEnabled",package->CombatSetting("JMVFixConstructorShutdownRelease")!=0},{"cases",Json::array()}};
 struct Probe {
  const void* vtable;int references;int* decrements;int* deletes;int* deletedReferences;
  int DecRefCount(){++*decrements;return --references;}
  ~Probe(){++*deletes;*deletedReferences=references;}
 };
 for(int site=0;site<2;++site)for(bool guarded:{false,true})for(bool table:{false,true}){
  int decrements=0,deletes=0,deletedReferences=-9,skips=0,skippedReferences=-9;
  auto* object=new Probe{table?reinterpret_cast<const void*>(1):nullptr,1,&decrements,&deletes,&deletedReferences};
  auto* retained=object;
  bool released=ModConstructorLifetime::Release(object,guarded,[&](const void* value,const void* vtable){
   if(value!=retained||vtable!=nullptr)throw std::runtime_error("Wrong shutdown observer input");
   ++skips;skippedReferences=retained->references;
  });
  bool expected=!guarded||table;
  if(object!=nullptr||decrements!=1||released!=expected||deletes!=int(expected)||skips!=int(!expected)||
    (expected?deletedReferences:skippedReferences)!=0)throw std::runtime_error("Constructor shutdown decision/order differs");
  report["cases"].push_back({{"owner",site},{"guarded",guarded},{"vtable",table},{"released",released},{"decrements",decrements},{"deletes",deletes},{"skips",skips},{"referencesAtDecision",expected?deletedReferences:skippedReferences},{"ownerCleared",object==nullptr}});
  // The deliberately invalid probe was skipped; restore its fixture ownership.
  if(!released)delete retained;
 }
 Probe* absent=nullptr;int notes=0;
 if(ModConstructorLifetime::Release(absent,true,[&](const void*,const void*){++notes;})||notes||absent)
  throw std::runtime_error("Null owner was changed");
 report["nullOwner"]={ {"released",false},{"observerCalls",notes} };
 for(bool guarded:{false,true}){
  auto* object=new CSpellConstructor;object->AddRef();int notes=0;
  if(!ModConstructorLifetime::Release(object,guarded,[&](const void*,const void*){++notes;})||object||notes)
   throw std::runtime_error("Real CSpellConstructor teardown changed");
 }
 report["realConstructorsDeleted"]=2;return report;
}

Json CheckConstructorLifetimeContext(const std::string& directory)
{
 Json report={{"success",true},{"gameplayActivated",false},{"initialPolicyBound",ModConstructorLifetime::PolicyBound()},{"initialGuardEnabled",ModConstructorLifetime::GuardEnabled()},{"paths",Json::array()}};
 if(ModRuntime::Current()||ModRuntime::Resources()||ModConstructorLifetime::PolicyBound())
  throw std::runtime_error("Context fixture requires a fresh inspection process");
 struct Probe {
  const void* vtable;int references;int* decrements;int* deletes;int* deletedReferences;
  int DecRefCount(){++*decrements;return --references;}
  ~Probe(){++*deletes;*deletedReferences=references;}
 };
 // Unbound/no-mod policy preserves the old delete body; the probe is nonvirtual.
 int plainDecrements=0,plainDeletes=0,plainReferences=-1;
 auto* plain=new Probe{nullptr,1,&plainDecrements,&plainDeletes,&plainReferences};
 if(!ModConstructorLifetime::ReleaseOwned(plain,[](const void*,const void*){})||plain||plainDecrements!=1||plainDeletes!=1||plainReferences!=0)
  throw std::runtime_error("Unbound constructor release changed");
 report["unboundDeletes"]=plainDeletes;
 for(bool interrupted:{false,true}){
  bool configured=false;CSpellConstructor* lateReal[4]={};
  struct Interrupted {};
  try {
   auto host=std::make_unique<ModRuntime::Host>(directory);
   auto* package=ModRuntime::Current();
   configured=package && package->CombatSetting("JMVFixConstructorShutdownRelease")!=0;
   if(!configured||!ModConstructorLifetime::PolicyBound()||!ModConstructorLifetime::GuardEnabled())
    throw std::runtime_error("Installed constructor policy was not bound");
   for(auto& object:lateReal){object=new CSpellConstructor;object->AddRef();}
   if(interrupted)throw Interrupted{};
   host.reset();
  }catch(const Interrupted&){}
  if(ModRuntime::Current()||ModRuntime::Resources()||!ModConstructorLifetime::PolicyBound()||!ModConstructorLifetime::GuardEnabled())
   throw std::runtime_error("Native shutdown policy lost on Host teardown");
  Json path={{"unwind",interrupted},{"installedEnabled",configured},{"currentNull",ModRuntime::Current()==nullptr},{"resourcesNull",ModRuntime::Resources()==nullptr},{"policyBound",ModConstructorLifetime::PolicyBound()},{"guardEnabled",ModConstructorLifetime::GuardEnabled()},{"cases",Json::array()}};
  for(int site=0;site<2;++site)for(bool table:{false,true}){
   int decrements=0,deletes=0,deletedReferences=-1,skips=0,skippedReferences=-1;
   auto* object=new Probe{table?reinterpret_cast<const void*>(1):nullptr,1,&decrements,&deletes,&deletedReferences};
   auto* retained=object;
   bool released=ModConstructorLifetime::ReleaseOwned(object,[&](const void* value,const void* vtable){
    if(value!=retained||vtable!=nullptr)throw std::runtime_error("Late release observer input differs");
    ++skips;skippedReferences=retained->references;
   });
   if(object||released!=table||decrements!=1||deletes!=int(table)||skips!=int(!table)||(table?deletedReferences:skippedReferences)!=0)
    throw std::runtime_error("Post-context constructor release differs from native");
   path["cases"].push_back({{"owner",site},{"vtable",table},{"released",released},{"decrements",decrements},{"deletes",deletes},{"skips",skips},{"referencesAtDecision",table?deletedReferences:skippedReferences},{"ownerCleared",object==nullptr},{"oldR163Guarded",ModRuntime::Current()!=nullptr}});
   if(!released)delete retained; // Clean only this deliberately invalid fixture.
   auto*& real=lateReal[site*2+int(table)];int realSkips=0;
   if(!ModConstructorLifetime::ReleaseOwned(real,[&](const void*,const void*){++realSkips;})||real||realSkips)
    throw std::runtime_error("Real late CSpellConstructor release failed");
  }
  path["realConstructorsDeleted"]=4;report["paths"].push_back(path);
 }
 report["worldActivated"]=false;report["audioDeviceActivated"]=false;return report;
}

Json CheckPlayerNumbers()
{
 // CPlayer constructs a Party world: use only the number/identity contract here.
 struct NumberRecord {
  int number,writes=0;
  int GetPlayerNumber(){return number;}
  void SetPlayerNumber(int value){number=value;++writes;}
 };
 struct NumberManager {std::vector<NumberRecord*> records;};
 struct NumberIterator {
  NumberManager* manager=nullptr;size_t index=0;
  NumberRecord* GetFirst(NumberManager* value){manager=value;index=0;return GetNext();}
  NumberRecord* GetNext(){return index<manager->records.size()?manager->records[index++]:nullptr;}
 };
 Json report={{"success",true},{"gameplayActivated",false},{"worldActivated",false},{"audioDeviceActivated",false},{"cases",Json::array()}};
 auto check=[&](const char* name,std::vector<int> existing,int self,int fallback,bool available,int expected){
  std::vector<NumberRecord> owners;for(int value:existing)owners.push_back({value});
  NumberRecord fresh{self};NumberManager manager;
  for(auto& owner:owners)manager.records.push_back(&owner);
  manager.records.insert(manager.records.begin()+manager.records.size()/2,&fresh);
  NumberIterator iter;
  ModPlayerNumbers::Assign(&fresh,fallback,available?&manager:nullptr,iter);
  bool untouched=true;for(size_t k=0;k<owners.size();++k)untouched&=owners[k].number==existing[k]&&owners[k].writes==0;
  if(fresh.number!=expected||fresh.writes!=1||!untouched)throw std::runtime_error("Player number reference mismatch");
  report["cases"].push_back({{"name",name},{"existing",existing},{"self",self},{"fallback",fallback},{"managerAvailable",available},{"actual",fresh.number},{"writes",fresh.writes},{"otherOwnersUnchanged",untouched}});
 };
 check("no-manager",{0,7},999,9,false,9);
 check("first-player",{},-1,0,true,0);
 check("empty-custom-fallback",{},999,5,true,5);
 check("negative-owners",{-3,-1},999,0,true,0);
 check("second-player",{0},999,0,true,1);
 check("gaps-and-unsorted",{7,0,2},999,0,true,8);
 check("existing-duplicates",{5,5,0},999,0,true,6);
 check("exclude-new-self",{2,0},999,0,true,3);
 check("removed-highest",{0,2},-1,0,true,3);
 check("fallback-not-a-minimum",{0},-1,9,true,1);
 check("maximum-representable",{2147483646},-1,0,true,2147483647);
 NumberManager manager;std::vector<NumberRecord> owners(3);
 Json numbers=Json::array();for(auto& owner:owners){
  owner.number=-1;manager.records.push_back(&owner);NumberIterator iter;
  ModPlayerNumbers::Assign(&owner,0,&manager,iter);numbers.push_back(owner.number);
 }
 if(owners[0].number!=0||owners[1].number!=1||owners[2].number!=2)throw std::runtime_error("Players are not individually addressable");
 int count=0;for(const auto& owner:owners)if(owner.number==1)++count;
 if(count!=1)throw std::runtime_error("Second player number is ambiguous");
 report["sequence"]=numbers;report["oldConstantSequence"]={0,0,0};report["secondPlayerMatches"]=count;
 return report;
}
}
// Borrowed stack stats + actual installed Lua/perks; no screen, unit/world,
// device/audio or save object is constructed by this component inspector.
Json CheckUiDescriptions(const std::string& directory){
 ModRuntime::Host host(directory);auto* package=ModRuntime::Current();std::string perkOwner;
 const std::string base=(fs::path(directory).parent_path().parent_path()/"Base/res/database.res").string();
 package->ReadResource("database.res","perks.pdb",base,perkOwner);
 if(!thePerkDatabase.LoadDatabase("",perkOwner.c_str()))throw std::runtime_error("UI-description perk database unavailable");
 Json report={{"success",true},{"gameplayActivated",false},{"worldCreated",false},{"deviceCreated",false},{"audioStarted",false},{"screenCreated",false},{"luaMainExecuted",true},{"perkOwner",perkOwner},{"profiles",Json::array()},{"cases",Json::array()}};
 size_t assertions=0;auto require=[&](bool good,const char* reason){++assertions;if(!good)throw std::runtime_error(reason);};
 SRPGUnitStats stats[7]={};const int effective[]={25,80,30,80,10,25,25},displayed[]={25,25,80,50,10,25,25};
 const int skills[][3]={{20,20,20},{20,20,20},{60,65,70},{120,90,90},{0,0,0},{39,39,39},{0,0,0}};
 const float experience[]={1500,1500,10000,200000,500,5000,100000};const int dexterity[]={25,25,25,70,10,25,95};
 const int manaPerks[]={0,0,1,2,0,0,3};const float weights[]={0,0,0,0,80,0,0};
 for(int i=0;i<7;++i){auto& st=stats[i];st.fpExperience=experience[i];st.psStrength.fpBaseValue=st.psStrength.fpEffectiveValue=25;
  st.psDexterity.fpBaseValue=st.psDexterity.fpEffectiveValue=float(dexterity[i]);st.psIntelligence.fpBaseValue=st.psIntelligence.fpEffectiveValue=float(effective[i]);
  st.fpMaxEncumbrance=100;st.fpCurrEncumbrance=weights[i];st.cPerks[RPGPRK_MANA]=BYTE(manaPerks[i]);
  st.cAutoSkills[RPGASKL_ELEMENTAL]=BYTE(skills[i][0]);st.cAutoSkills[RPGASKL_SENSE]=BYTE(skills[i][1]);st.cAutoSkills[RPGASKL_ASTRAL]=BYTE(skills[i][2]);
  if(i==2)for(int p=RPGPRK_FIRE;p<=RPGPRK_DOMINATION;++p)st.cPerks[p]=1;
  if(i==3){st.cPerks[RPGPRK_DIVINATION]=2;st.cPerks[RPGPRK_HEALING]=3;st.cPerks[RPGPRK_DOMINATION]=1;}
  Json profile={{"id",i},{"effectiveIntelligence",effective[i]},{"displayedIntelligence",displayed[i]},{"skills",{skills[i][0],skills[i][1],skills[i][2]}},{"experience",st.fpExperience},{"dexterity",dexterity[i]},{"currentEncumbrance",st.fpCurrEncumbrance},{"maxEncumbrance",st.fpMaxEncumbrance},{"manaPerkModifier",st.GetPerkModifier(RPGPRK_MANA)},{"baseMana",package->CalcBaseMana(st)},{"manaBonus",st.GetManaBonusMultiplier()},{"subtypes",Json::array()}};
  for(int t=0;t<RPG_NUM_MAGIC_SUBTYPES;++t)profile["subtypes"].push_back({{"subtype",t},{"baseSkill",st.GetBaseMagicSkill(ERPGMagicSubtypes(t))},{"perkModifier",st.GetPerkModifier(RPGPRK_FIRE+t)},{"oldComplexity",st.GetAvailableComplexity(ERPGMagicSubtypes(t))}});
  report["profiles"].push_back(profile);
 }
 const SRPGUnitStats original[7]={stats[0],stats[1],stats[2],stats[3],stats[4],stats[5],stats[6]};
 const std::vector<std::vector<int>> parties={{},{0},{1},{2},{3},{4},{0,3,6},{6,3,0}};
 for(int shop:{1,2,3,4,12})for(int t=0;t<RPG_NUM_MAGIC_SUBTYPES;++t)for(const auto& party:parties){
  std::vector<ModCampSpellLimitInput> inputs;int oldComplexity=0,oldMana=0;
  for(int i:party){inputs.push_back({&stats[i],displayed[i]});oldComplexity=(std::max)(oldComplexity,stats[i].GetAvailableComplexity(ERPGMagicSubtypes(t)));oldMana=(std::max)(oldMana,int(stats[i].RecalcMP()/stats[i].GetManaBonusMultiplier()));}
  int complexity=-999,mana=-999;package->CampSpellLimits(shop,t,inputs,complexity,mana);
  if(party.empty())require(complexity==0&&mana==0,"Empty party acquired shop capability");
  if(shop==1&&t==RPGMST_FIRE&&party==std::vector<int>{0})require(complexity==40&&oldComplexity==20,"Original missing shop floor was not reproduced");
  report["cases"].push_back({{"shop",shop},{"shopFloor",package->ShopConstructLimit(shop)},{"subtype",t},{"party",party},{"complexity",complexity},{"mana",mana},{"beforeComplexity",oldComplexity},{"beforeMana",oldMana}});
 }
 for(int i=0;i<7;++i)require(memcmp(&original[i],&stats[i],sizeof(stats[i]))==0,"UI limits changed party state");
 require(!theWorldServer.IsMapLoaded(),"UI-description component loaded a server map");
 report["assertions"]=assertions;return report;
}
// Detached camp display fixtures: borrowed stats + installed Lua/perks only.
Json CheckCampStatsG3(const std::string& directory){
 ModRuntime::Host host(directory);auto* package=ModRuntime::Current();std::string perkOwner;
 const std::string base=(fs::path(directory).parent_path().parent_path()/"Base/res/database.res").string();
 package->ReadResource("database.res","perks.pdb",base,perkOwner);
 if(!thePerkDatabase.LoadDatabase("",perkOwner.c_str()))throw std::runtime_error("Camp stat perks unavailable");
 Json report={{"success",true},{"gameplayActivated",false},{"worldCreated",false},{"unitCreated",false},{"deviceCreated",false},{"audioStarted",false},{"screenCreated",false},{"luaMainExecuted",true},{"perkOwner",perkOwner},{"profiles",Json::array()},{"perks",Json::array()},{"capacities",Json::array()}};
 size_t assertions=0;auto require=[&](bool good,const char* reason){++assertions;if(!good)throw std::runtime_error(reason);};
 SRPGUnitStats stats[8]={};SRPGUserUnitStats shown[8]={};
 const int effective[]={25,80,30,80,10,25,25,95},displayed[]={25,25,80,50,10,25,25,95};
 const int dexterity[]={25,25,25,70,10,25,95,60},skills[]={20,20,60,120,0,39,100,120};
 const float experience[]={1500,1500,10000,200000,500,5000,100000,200000};
 for(int i=0;i<8;++i){auto& st=stats[i];auto& us=shown[i];
  st.psStrength.fpBaseValue=st.psStrength.fpEffectiveValue=float(effective[i]);st.psDexterity.fpBaseValue=st.psDexterity.fpEffectiveValue=float(dexterity[i]);st.psIntelligence.fpBaseValue=st.psIntelligence.fpEffectiveValue=float(effective[i]);
  st.fpExperience=experience[i];st.fpMaxEncumbrance=100;st.fpCurrEncumbrance=i==4?160.0f:0;
  st.senses[RPGS_SIGHT].fpEffectiveValue=18.6f+i;st.cPerks[11]=BYTE(i%4);st.cPerks[23]=BYTE(i%4);
  us.cStrength=BYTE(displayed[i]);us.cDexterity=BYTE(displayed[(i+1)%8]);us.cIntelligence=BYTE(displayed[i]);us.cSenses[RPGS_SIGHT]=9;
  us.cManualSkills[RPGMSKL_SCIENCE]=BYTE(skills[i]);us.cAutoSkills[RPGASKL_MELEE]=BYTE(skills[i]);us.cAutoSkills[RPGASKL_ARCHERY]=BYTE(skills[(i+1)%8]);
  for(int s=RPGASKL_ELEMENTAL;s<=RPGASKL_ASTRAL;++s)st.cAutoSkills[s]=BYTE(skills[i]+(s-RPGASKL_ELEMENTAL));
  if(i==2||i==3)for(int p=RPGPRK_FIRE;p<=RPGPRK_DOMINATION;++p)st.cPerks[p]=BYTE(i==2?1:2);
 }
 const auto originalStats=std::vector<SRPGUnitStats>(stats,stats+8);const auto originalShown=std::vector<SRPGUserUnitStats>(shown,shown+8);
 for(int i=0;i<8;++i){const auto& st=stats[i];const auto& us=shown[i];
  Json p={{"id",i},{"effective",effective[i]},{"partyDexterity",dexterity[i]},{"displayStrength",us.cStrength},{"displayDexterity",us.cDexterity},{"displayIntelligence",us.cIntelligence},{"experience",st.fpExperience},{"partyWeight",st.fpCurrEncumbrance},{"partyCapacity",st.fpMaxEncumbrance},{"magicModifier",st.GetPerkModifier(23)},{"rawSight",st.senses[RPGS_SIGHT].fpEffectiveValue},{"sightModifier",st.GetPerkModifier(11)},{"shownSight",us.cSenses[RPGS_SIGHT]},{"sight",ModCampStats::Sight(st)},{"baseMana",st.RecalcMP()},{"scienceBase",us.cManualSkills[RPGMSKL_SCIENCE]},{"science",ModCampStats::ScienceSkill(us)},{"primary",Json::array()},{"mana",Json::array()},{"schools",Json::array()},{"damage",Json::array()}};
  const float exp=package->InfoPerkBonus("GetExpMultiplier2",us.cIntelligence);p["experienceMultiplier"]=exp;
  for(const auto& divisors:std::vector<std::pair<float,float>>{{25,25},{20,30}}){const auto primary=ModCampStats::PrimaryValues(us,divisors.first,divisors.second,exp);
   p["primary"].push_back({{"hpDivisor",divisors.first},{"mpDivisor",divisors.second},{"health",primary.healthPercent},{"stamina",primary.staminaPercent},{"exp",primary.experiencePercent},{"mana",primary.manaPercent}});
  }
  for(int carried:{0,240,300,375,450,480})for(bool baseStats:{false,true})p["mana"].push_back({{"carried",carried},{"capacity",300},{"baseStats",baseStats},{"displayWeight",ModCampStats::Weight(carried,baseStats)},{"value",package->CampDisplayMana(st,carried,300,baseStats)}});
  for(int t=0;t<RPG_NUM_MAGIC_SUBTYPES;++t){int skill=ModCampStats::MagicSkill(st,us.cIntelligence,t);p["schools"].push_back({{"subtype",t},{"base",st.GetBaseMagicSkill(ERPGMagicSubtypes(t))},{"perk",st.GetPerkModifier(RPGPRK_FIRE+t)},{"skill",skill},{"damage",ModCampStats::MagicDamagePercent(skill)}});}
  for(int s:{RPGASKL_MELEE,RPGASKL_ARCHERY})p["damage"].push_back({{"skill",us.cAutoSkills[s]},{"percent",package->InfoPerkBonus("GetSkillMod",us.cAutoSkills[s])*100.0f}});
  report["profiles"].push_back(p);
 }
 for(int index=0;index<thePerkDatabase.GetNumPerks();++index){const auto* perk=thePerkDatabase.GetPerk(index);if(!perk)continue;
  for(bool jmv136:{false,true})for(bool anatomy:{false,true}){
   Json rows=Json::array();for(const auto& row:ModCampStats::PerkRows(index/3,int(perk->dwModifier),jmv136,anatomy,3.0f))rows.push_back({{"label",row.label},{"value",row.value},{"format",row.format}});
   report["perks"].push_back({{"index",index},{"group",index/3},{"level",index%3+1},{"active",perk->bActive},{"code",std::string((LPCSTR)perk->szCode)},{"modifier",perk->dwModifier},{"jmv136",jmv136},{"anatomy",anatomy},{"backstab",3.0f},{"rows",rows}});
  }
 }
 const float factors[][5]={{25,0,1,1,1},{80,20,1.15f,1.3f,1.2f},{10,-20,1,1,1},{95,50,1.2f,1.4f,0.9f},{30,15,1.05f,1,1.1f}};
 for(const auto& f:factors)report["capacities"].push_back({{"strength",f[0]},{"lift",f[1]},{"jewelry",f[2]},{"complex",f[3]},{"sets",f[4]},{"value",ModCampStats::Capacity(f[0],f[1],f[2],f[3],f[4])},{"skillValue",ModCampStats::Capacity(f[0],f[1],f[2],1,f[4])}});
 for(int i=0;i<8;++i){require(memcmp(&originalStats[i],&stats[i],sizeof(stats[i]))==0,"Display mutated party stats");require(memcmp(&originalShown[i],&shown[i],sizeof(shown[i]))==0,"Display mutated shown stats");}
 require(ModCampStats::Weight(480,true)==0&&ModCampStats::Weight(480,false)==480,"Original base-mode load divergence remains");
 require(ModCampStats::Sight(stats[0])==19,"Native party sight rounding changed");
 require(!theWorldServer.IsMapLoaded(),"Camp component loaded server world");report["assertions"]=assertions;return report;
}
#include "ModCampDescriptions.inl"
#include "ModCampDescriptionsCheck.inl"
#include "ModIdleTooltipsCheck.inl"
#include "ModAssortmentCheck.inl"
std::optional<std::string> CModPackage::ProgressionRequirementCode(const char* code,int field){
 CallbackFrame frame(state);BeginCallback("GetPerkRequiredJmv");lua_pushstring(state,code);lua_pushinteger(state,field);Call(2,1);
 const char* text=lua_tostring(state,-1);
 return text?std::optional<std::string>(text):std::nullopt;
}
namespace {
Json CheckDownstreamOwnerG3(){
 struct Item {
  const char* name;int refs=0;std::vector<std::string>* events;
  int AddRef(){events->push_back(std::string("retain:")+name);return ++refs;}
  void Release(){events->push_back(std::string("release:")+name);--refs;}
 };
 Json cases=Json::array();
 auto run=[&](const char* id,int inputs,bool duplicateOK){
  std::vector<std::string>events,outputs;Item copy{"copy",0,&events},container{"container",0,&events};Item* result=nullptr;
  int consumed=0,money=100;bool selected=ModAlchemy::ScrollAllowed(true,true,true);
  bool success=ModAlchemy::Deconstruct<Item>(result,[&](Item*& spell){
   if(inputs!=1)return false;events.push_back("container:create");spell=&container;spell->AddRef();return true;
  },[&]()->Item*{events.push_back("duplicate");return duplicateOK?&copy:nullptr;},[&](){--inputs;++consumed;events.push_back("consume");},
  [&](Item*,Item* spell){
   outputs.push_back("Instruction Quick Item:21");events.push_back("instruction:21");
   for(int i=0;i<2;++i){outputs.push_back("material:2");events.push_back("material:2");}
   if(spell){outputs.push_back("spell:17");events.push_back("spell:17");}
  });
  if(success){money-=15;events.push_back("charge:15");}
  int returned=0;if(!success){while(inputs){--inputs;++returned;}events.push_back("cancel:return:"+std::to_string(returned));}
  cases.push_back({{"id",id},{"selected",selected},{"success",success},{"consumed",consumed},{"cancelReturned",returned},{"inputsRemaining",inputs},{"money",money},{"copyRefs",copy.refs},{"containerRefs",container.refs},{"resultCleared",result==nullptr},{"outputs",outputs},{"events",events}});
 };
 run("quick_deconstruct_owned_result",1,true);
 run("duplicate_failure_returns_input",1,false);
 run("multiple_input_rejected_then_cancelled",2,true);
 return {{"success",true},{"productionHelper",true},{"cases",cases},{"luaExecuted",false},{"unitCreated",false},{"worldCreated",false},{"hostCreated",false},{"deviceCreated",false},{"audioStarted",false},{"saveRead",false},{"uiExecuted",false},{"realItemsCreated",false}};
}
Json CheckAttackCreationG3(){
 using namespace ModConditionalCombat;
 Json cases=Json::array();
 auto run=[&](bool personage){
  bool weapon=RetailWeapon(personage,0,true);int skill=0,isaac=0,gameCrt=0,addonCrt=0;
  float hit=0,damage=0;std::vector<std::string> events;CreationBackend b;
  b.clear=[&](){events.push_back("clear");};
  b.native=[&](){events.push_back("naw");skill=0;hit=9.f*.5f;++isaac;damage=4.f+.5f;};
  b.weapon=[&](){events.push_back("weapon");skill=3;++gameCrt;hit=WeaponToHit(4,5);++isaac;damage=2.f+3.f;};
  b.modified=[&](){events.push_back("modified:0:-1");++addonCrt;damage*=2;};
  b.metadata=[&](){events.push_back("metadata");};
  CreateRetail(weapon,b);
  cases.push_back({{"id",personage?"personage_cached_weapon":"armed_npc_uses_naw"},{"weapon",weapon},{"skill",skill},{"toHit",hit},{"damage",damage},{"events",events},{"isaacDraws",isaac},{"gameCrtDraws",gameCrt},{"addonCrtDraws",addonCrt}});
 };
 run(false);run(true);
 cases.push_back({{"id","ordinary_owner_and_rear"},{"ordinary",OrdinaryCreation(true,false,false,false)},{"taggedSkill",OrdinaryCreation(true,false,true,false)},{"active",OrdinaryCreation(true,true,false,false)},{"left",OrdinaryCreation(true,false,false,true)},{"rear",RetailRear(true,false,-.75f)},{"front",RetailRear(true,false,0)},{"bowRear",RetailRear(true,true,-.75f)},{"unownedRear",RetailRear(false,false,-.75f)}});
 return {{"success",true},{"productionHelper",true},{"cases",cases},{"luaExecuted",false},{"unitCreated",false},{"worldCreated",false},{"hostCreated",false},{"deviceCreated",false},{"audioStarted",false},{"saveRead",false}};
}
Json CheckConditionalCombatG3(){
 using namespace ModConditionalCombat;
 Json cases=Json::array();
 auto producer=[&](Owner owner){
  int strength=owner==Owner::Inherited?25:40,attack=0,defense=0,cursor=0;
  const std::vector<int> draws=owner==Owner::Inherited?std::vector<int>{12,34,56}:std::vector<int>{7};
  std::vector<std::string> events;
  Backend b;
  b.retailSeed=[&](){events.push_back("retail");};
  b.roll=[&](){if(cursor>=int(draws.size()))throw std::runtime_error("Unexpected producer draw");return float(draws[cursor++]);};
  b.retailDamage=[&](bool maximum,float roll){events.push_back(std::string(maximum?"max:":"min:")+std::to_string(int(roll))+":0:shadow");};
  b.initialStats=[&](){events.push_back("initial");strength=30;};
  b.raw=[&](Owner selected){bool direct=selected==Owner::DirectDefender;events.push_back(direct?"raw:direct":"raw:inherited");attack=Unarmed(20,4,direct);defense=Unarmed(21,4,direct);};
  b.damagePair=[&](float roll){events.push_back("max:"+std::to_string(int(roll))+":-1:current");events.push_back("min:"+std::to_string(int(roll))+":-1:current");};
  b.difficulty=[&](){events.push_back("difficulty");};
  b.attackCallback=[&](int count){events.push_back("attack:"+std::to_string(count));};
  Produce(owner,b);
  if(cursor!=int(draws.size()))throw std::runtime_error("Producer omitted an explicit draw");
  cases.push_back({{"id",owner==Owner::Inherited?"inherited_order_writeback":"direct_unarmed_owner"},{"draws",draws},{"events",events},{"committedStrength",strength},{"attack",attack},{"defense",defense}});
 };
 producer(Owner::Inherited);producer(Owner::DirectDefender);
 float base=Base(20,0,4),called=Location(base,-5),bonus=called+2;
 int draws=0;bool avoided=Avoidance(false,90,1,false,[&](){++draws;return 33.f;});
 size_t count=0;auto*rules=ModConfiguration::CombatRules(count);bool found=false,defaultShit=true;
 for(size_t i=0;i<count;i++)if(strcmp(rules[i].key,"JMVShit")==0){found=true;defaultShit=rules[i].fallback;}
 if(!found)throw std::runtime_error("Existing native Shit rule missing");
 cases.push_back({{"id","conditional_score_and_routing"},{"base",base},{"called",called},{"withBonus",bonus},{"thresholdEqualityHit",bonus>=13},{"avoidanceDraws",draws},{"avoided",avoided},{"fallback",Fallback(true,false)},{"activeFallback",Fallback(true,true)},{"defaultShit",defaultShit}});
 return {{"success",true},{"productionHelper",true},{"cases",cases},{"luaExecuted",false},{"unitCreated",false},{"worldCreated",false},{"hostCreated",false},{"deviceCreated",false},{"audioStarted",false},{"saveRead",false}};
}
}

#include "ModHudAssignmentCheck.inl"
#include "ModProgressionCheck.inl"
#include "ModSafetyLookupCheck.inl"
#include "ModEquipmentSwitchCheck.inl"
#include "ModCampEquipmentCheck.inl"
#include "ModCampRepairCheck.inl"
#include "ModCampItemPriceCheck.inl"
#include "ModCampSaleCheck.inl"
#include "ModSecondaryAtlasCheck.inl"
#include "ModPrimaryAtlasCheck.inl"
#include "ModTextureMemoryCheck.inl"
#include "ModHDEquipmentLayerCheck.inl"
#include "ModSpellTargetingCheck.inl"
#include "ModSpellMappingCheck.inl"
#include "ModSpellOwnerCheck.inl"
#include "ModSpellDangerCheck.inl"
#include "ModSpellPhaseCheck.inl"
#include "ModCampReferencesCheck.inl"
static Json CheckPreparedAttackG3() {
    std::vector<std::string> twice, single, ordinary, rejected;
    int liveTarget = 42, metadata = 7;
    const int heldTarget = liveTarget;
    ModPreparedAttack::DeliverProjectiles(2,
        [&] { twice.push_back("retain:7"); return metadata; },
        [&](int index, int count, bool spell) {
            twice.push_back("apply:"+std::to_string(heldTarget)+":"+std::to_string(index)+":"+std::to_string(count)+":"+std::to_string(spell));
            metadata = 0; liveTarget = 0;
        },
        [&](int retained) { twice.push_back("publish:"+std::to_string(heldTarget)+":"+std::to_string(retained)); });
    ModPreparedAttack::CancelReady(true, [&] { twice.push_back("cancel"); });
    auto run = [&](bool ready, std::vector<std::string>& events) {
        ModPreparedAttack::DeliverProjectiles(1, [] { return 0; },
            [&](int index, int count, bool spell) { events.push_back("apply:"+std::to_string(index)+":"+std::to_string(count)+":"+std::to_string(spell)); },
            [&](int) { events.push_back("unexpected-schedule"); });
        ModPreparedAttack::CancelReady(ready, [&] { events.push_back("cancel"); });
    };
    run(true, single); run(false, ordinary);
    bool refused = ModPreparedAttack::RejectPreparation(-1, [&] { rejected.push_back("cancel"); });
    return {{"success",true},{"productionHelper",true},
        {"cases",Json::array({
            {{"id","ready-double"},{"slot",ModPreparedAttack::HitSlot(true,0,2)},{"events",twice},{"heldTarget",heldTarget},{"liveTarget",liveTarget},{"liveMetadata",metadata}},
            {{"id","ready-single"},{"slot",ModPreparedAttack::HitSlot(false,0,2)},{"invalidLeft",ModPreparedAttack::HitSlot(true,0,-1)},{"events",single}},
            {{"id","ordinary-rejected"},{"ordinaryEvents",ordinary},{"rejected",refused},{"rejectedEvents",rejected}}
        })},{"luaExecuted",false},{"unitCreated",false},{"worldCreated",false},
        {"hostCreated",false},{"deviceCreated",false},{"audioStarted",false},{"saveRead",false},{"uiExecuted",false}};
}

static Json CheckMeleeTimingG3() {
    auto speeds = ModAttackTiming::ScaleMovement({2.f, .8f}, .25f);
    auto sequence = [](bool enabled, bool alive) {
        std::vector<std::string> events;
        for (int tick=99; tick<=101; ++tick)
            ModAttackTiming::ExecuteImpact(enabled, alive, tick, 101,
                [&] { events.push_back(std::to_string(tick)+":build"); },
                [&] { events.push_back(std::to_string(tick)+":apply"); });
        return events;
    };
    return {{"success",true},{"productionHelper",true},
        {"cases",Json::array({
            {{"id","slowdown"},{"move",speeds.move},{"rotate",speeds.rotate}},
            {{"id","ordinary"},{"enabled",ModAttackTiming::UsesImpactFrame(true,true,false,"crsuattack01")},
             {"events",sequence(ModAttackTiming::UsesImpactFrame(true,true,false,"crsuattack01"),true)},
             {"deadEvents",sequence(ModAttackTiming::UsesImpactFrame(true,true,false,"uattack01"),false)}},
            {{"id","retained"},{"dual",ModAttackTiming::UsesImpactFrame(true,true,false,"dat01crcr37")},
             {"skill",ModAttackTiming::UsesImpactFrame(true,true,true,"uattack01")},
             {"cast",ModAttackTiming::UsesImpactFrame(true,false,false,"uattack01")},
             {"vanilla",ModAttackTiming::UsesImpactFrame(false,true,false,"uattack01")},
             {"nativeFailedRecheckCancel",ModAttackTiming::CancelFailedRecheck(true)},
             {"defaultFailedRecheckCancel",ModAttackTiming::CancelFailedRecheck(false)}}
        })},{"luaExecuted",false},{"unitCreated",false},{"worldCreated",false},
        {"hostCreated",false},{"deviceCreated",false},{"audioStarted",false},{"saveRead",false},{"uiExecuted",false}};
}

static Json CheckModalFadeG3() {
    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    HRESULT compiled = RetailModalFade::Compile(bytecode.GetAddressOf());
    Json cases = Json::array();
    struct Sample { const char* id; unsigned r, g, b, a; };
    const Sample samples[] = {{"black",0,0,0,0},{"white",255,255,255,255},{"colored",32,160,240,80}};
    for (const auto& sample : samples)
        cases.push_back({{"id",sample.id},{"rgb",{sample.r,sample.g,sample.b}},
            {"alpha",sample.a},{"gray",RetailModalFade::Gray(sample.r,sample.g,sample.b)}});
    return {{"success",SUCCEEDED(compiled)},{"productionHelper",true},
        {"shaderCompiled",SUCCEEDED(compiled)},{"cases",cases},
        {"luaExecuted",false},{"unitCreated",false},{"worldCreated",false},
        {"hostCreated",false},{"deviceCreated",false},{"audioStarted",false},
        {"saveRead",false},{"uiExecuted",false}};
}

bool InspectModPackageCommandLine(int &result){
 int argc=0;LPWSTR *argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)return false;
 bool mob=argc>=2&&wcscmp(argv[1],L"--inspect-mob-script")==0;
 bool defaults=argc>=2&&wcscmp(argv[1],L"--inspect-default-stats")==0;
 bool economy=argc>=2&&wcscmp(argv[1],L"--inspect-mod-economy")==0;
 bool uiRange=argc>=2&&wcscmp(argv[1],L"--inspect-ui-range")==0;
 bool playerNumbers=argc>=2&&wcscmp(argv[1],L"--inspect-player-numbers")==0;
 bool constructorContext=argc>=2&&wcscmp(argv[1],L"--inspect-constructor-lifetime-context")==0;
 bool constructorLifetime=argc>=2&&wcscmp(argv[1],L"--inspect-constructor-lifetime")==0;
 bool itemDetails=argc>=2&&wcscmp(argv[1],L"--inspect-item-details")==0;
 bool itemInfo=argc>=2&&wcscmp(argv[1],L"--inspect-item-info")==0;
 bool campSpellInfo=argc>=2&&wcscmp(argv[1],L"--inspect-camp-spell-info")==0;
 bool campStats=argc>=2&&wcscmp(argv[1],L"--inspect-camp-stats-g3")==0;
 bool fullDescriptions=argc>=2&&wcscmp(argv[1],L"--inspect-full-descriptions-g3")==0;
 bool idleTooltips=argc>=2&&wcscmp(argv[1],L"--inspect-idle-tooltips-g3")==0;
 bool assortment=argc>=2&&wcscmp(argv[1],L"--inspect-assortment-g3")==0;
 bool hudAssignment=argc>=2&&wcscmp(argv[1],L"--inspect-hud-assignment-g3")==0;
 bool progression=argc>=2&&wcscmp(argv[1],L"--inspect-progression-g3")==0;
 bool safetyLookup=argc>=2&&wcscmp(argv[1],L"--inspect-safety-lookup-g3")==0;
 bool equipmentSwitch=argc>=2&&wcscmp(argv[1],L"--inspect-equipment-switch-g3")==0;
 bool spellMapping=argc>=2&&wcscmp(argv[1],L"--inspect-spell-mapping-g3")==0;
 bool spellOwner=argc>=2&&wcscmp(argv[1],L"--inspect-spell-owner-g3")==0;
 bool spellLoad=argc>=2&&wcscmp(argv[1],L"--inspect-spell-load-g3")==0;
 bool spellPhase=argc>=2&&wcscmp(argv[1],L"--inspect-spell-phase-g3")==0;
 bool campReferences=argc>=2&&wcscmp(argv[1],L"--inspect-camp-references-g3")==0;
 bool spellTargeting=argc>=2&&wcscmp(argv[1],L"--inspect-spell-targeting-g3")==0;
 bool campEquipment=argc>=2&&wcscmp(argv[1],L"--inspect-camp-equipment-g3")==0;
 bool campRepair=argc>=2&&wcscmp(argv[1],L"--inspect-camp-repair-g3")==0;
 bool campItemPrice=argc>=2&&wcscmp(argv[1],L"--inspect-camp-item-price-g3")==0;
 bool campSale=argc>=2&&wcscmp(argv[1],L"--inspect-camp-sale-g3")==0;
 bool secondaryAtlas=argc>=2&&wcscmp(argv[1],L"--inspect-secondary-atlas-g3")==0;
 bool primaryAtlas=argc>=2&&wcscmp(argv[1],L"--inspect-primary-atlas-g3")==0;
 bool textureMemory=argc>=2&&wcscmp(argv[1],L"--inspect-texture-memory-g3")==0;
 bool memoryOutputAllocation=argc>=2&&wcscmp(argv[1],L"--inspect-memory-output-allocation-g3")==0;
 bool modalFade=argc>=2&&wcscmp(argv[1],L"--inspect-modal-fade-g3")==0;
 bool meleeTiming=argc>=2&&wcscmp(argv[1],L"--inspect-melee-timing-g3")==0;
 bool preparedAttack=argc>=2&&wcscmp(argv[1],L"--inspect-prepared-attack-g3")==0;
 bool lruAccounting=argc>=2&&wcscmp(argv[1],L"--inspect-lru-accounting-g3")==0;
 bool downstreamOwner=argc>=2&&wcscmp(argv[1],L"--inspect-downstream-owner-g3")==0;
 bool attackCreation=argc>=2&&wcscmp(argv[1],L"--inspect-attack-creation-g3")==0;
 bool conditionalCombat=argc>=2&&wcscmp(argv[1],L"--inspect-conditional-combat-g3")==0;
 bool consoleDrawAll=argc>=2&&wcscmp(argv[1],L"--inspect-console-drawall-g3")==0;
 bool uiDescriptions=argc>=2&&wcscmp(argv[1],L"--inspect-ui-descriptions-g3")==0;
 bool spellTooltips=argc>=2&&wcscmp(argv[1],L"--inspect-spell-tooltips")==0;
 bool startup=argc>=2&&wcscmp(argv[1],L"--inspect-mod-startup")==0;
 bool hdEquipmentLayer=argc>=2&&wcscmp(argv[1],L"--inspect-hd-equipment-layer")==0;
 if(argc<2||(!mob&&!defaults&&!economy&&!startup&&!hdEquipmentLayer&&!uiRange&&!spellTooltips&&!campSpellInfo&&!campStats&&!fullDescriptions&&!idleTooltips&&!assortment&&!hudAssignment&&!progression&&!safetyLookup&&!equipmentSwitch&&!campEquipment&&!campRepair&&!campItemPrice&&!campSale&&!secondaryAtlas&&!primaryAtlas&&!textureMemory&&!memoryOutputAllocation&&!conditionalCombat&&!attackCreation&&!downstreamOwner&&!modalFade&&!meleeTiming&&!preparedAttack&&!lruAccounting&&!consoleDrawAll&&!spellTargeting&&!spellMapping&&!spellOwner&&!spellLoad&&!spellPhase&&!campReferences&&!uiDescriptions&&!itemInfo&&!itemDetails&&!constructorLifetime&&!constructorContext&&!playerNumbers&&wcscmp(argv[1],L"--inspect-addon")!=0)){LocalFree(argv);return false;}
 result=2;Json report;std::wstring output;
 if(mob&&argc==5){output=argv[4];try{
  ModRuntime::Host host(Ansi(argv[2]));std::ifstream file(fs::path(argv[3]),std::ios::binary);
  if(!file)throw std::runtime_error("Cannot open script input");
  std::string bytes((std::istreambuf_iterator<char>(file)),{});CString source(bytes.c_str());CScript script;
  bool ok=script.LoadText(source,FALSE)!=FALSE;auto error=script.GetLastError();
  report={{"success",ok},{"line",error.nLine},{"error",std::string((LPCSTR)error.text)}};result=ok?0:1;
 }catch(const std::exception&e){report={{"success",false},{"error",e.what()}};}}
 else if(hdEquipmentLayer&&argc==4){output=argv[3];try{report=CheckHDEquipmentLayer(Ansi(argv[2]));result=0;}catch(const std::exception&e){report={{"success",false},{"error",e.what()}};}}
 else if(argc==4){output=argv[3];try{if(playerNumbers)report=CheckPlayerNumbers();else if(constructorContext)report=CheckConstructorLifetimeContext(Ansi(argv[2]));else if(constructorLifetime)report=CheckConstructorLifetime(Ansi(argv[2]));else if(itemDetails)report=CheckItemDetails(Ansi(argv[2]));else if(itemInfo)report=CheckItemInfo(Ansi(argv[2]));else if(equipmentSwitch)report=CheckEquipmentSwitchG3(Ansi(argv[2]));else if(campEquipment)report=CheckCampEquipmentG3(Ansi(argv[2]));else if(campRepair)report=CheckCampRepairG3(Ansi(argv[2]));else if(campItemPrice)report=CheckCampItemPriceG3(Ansi(argv[2]));else if(campSale)report=CheckCampSaleG3(Ansi(argv[2]));else if(secondaryAtlas)report=CheckSecondaryAtlasG3(Ansi(argv[2]));else if(primaryAtlas)report=CheckPrimaryAtlasG3(Ansi(argv[2]));else if(textureMemory)report=CheckTextureMemoryG3(Ansi(argv[2]));else if(memoryOutputAllocation)report=CheckMemoryOutputAllocationG3(Ansi(argv[2]));else if(conditionalCombat)report=CheckConditionalCombatG3();else if(attackCreation)report=CheckAttackCreationG3();else if(modalFade)report=CheckModalFadeG3();else if(meleeTiming)report=CheckMeleeTimingG3();else if(preparedAttack)report=CheckPreparedAttackG3();else if(lruAccounting)report=Json::parse(CTextureManager::CheckLRUAccountingG3());else if(downstreamOwner)report=CheckDownstreamOwnerG3();else if(consoleDrawAll)report=Json::parse(NConsole::CheckDrawAllCommand());else if(campReferences)report=CheckCampReferencesG3(Ansi(argv[2]));else if(spellPhase)report=CheckSpellPhaseG3(Ansi(argv[2]));else if(spellLoad)report=CheckSpellLoadG3(Ansi(argv[2]));else if(spellOwner)report=CheckSpellOwnerG3(Ansi(argv[2]));else if(spellMapping)report=CheckSpellMappingG3(Ansi(argv[2]));else if(spellTargeting)report=CheckSpellTargetingG3(Ansi(argv[2]));else if(safetyLookup)report=CheckSafetyLookupG3(Ansi(argv[2]));else if(progression)report=CheckProgressionG3(Ansi(argv[2]));else if(hudAssignment)report=CheckHudAssignmentG3(Ansi(argv[2]));else if(assortment)report=CheckAssortmentG3(Ansi(argv[2]));else if(idleTooltips)report=CheckIdleTooltipsG3(Ansi(argv[2]));else if(fullDescriptions)report=CheckFullDescriptionsG3(Ansi(argv[2]));else if(campStats)report=CheckCampStatsG3(Ansi(argv[2]));else if(uiDescriptions)report=CheckUiDescriptions(Ansi(argv[2]));else if(campSpellInfo)report=CheckCampSpellInfo(Ansi(argv[2]));else if(spellTooltips)report=CheckSpellTooltips(Ansi(argv[2]));else if(uiRange)report=CheckUIRange(Ansi(argv[2]));else if(defaults)report=CheckDefaultStats();else if(economy){CModPackage package;package.Load(Ansi(argv[2]));report=package.CheckEconomy();}else if(startup)report=CheckStartup(Ansi(argv[2]));else{CModPackage package;package.Load(Ansi(argv[2]));report=package.Describe();}result=0;}catch(const std::exception&e){report={{"success",false},{"error",e.what()}};}}
 else report={{"success",false},{"error","Expected --inspect-addon MOD_DIRECTORY OUTPUT_JSON"}};
 LocalFree(argv);if(!output.empty()){std::ofstream file{fs::path(output)};file<<report.dump(2,' ',false,Json::error_handler_t::replace);if(!file)result=3;}return true;
}

bool CModPackage::GetNpcCombatConsumableConfig(const char*zone,int id,const char*prototype,int kind,int variant,ModConsumableConfig&config){
 config={};if(!state||!zone||!zone[0]||id<0||kind<1||kind>5||variant<0||variant>2)return false;
 const char*name=prototype?prototype:"";
 const bool validKey=ModStaticLookup::NameKey(zone) && ModStaticLookup::NameKey(name);std::string key;
 if(validKey){ModStaticLookup::KeyString(key,zone);ModStaticLookup::KeyValue(key,id);ModStaticLookup::KeyString(key,name);ModStaticLookup::KeyValue(key,kind);ModStaticLookup::KeyValue(key,variant);}
 const auto value=staticLookups.consumables.Get(key,validKey && staticLookups.Eligible(state,2),[&](bool& valid){
  ModStaticLookup::ConsumableValue result;
  Stack stack(state);lua_getglobal(state,"GetNpcCombatConsumableConfig");if(!lua_isfunction(state,-1))return result;lua_pushstring(state,zone);lua_pushinteger(state,id);lua_pushstring(state,name);lua_pushinteger(state,kind);lua_pushinteger(state,variant);Call(5,4);
  if(lua_isnil(state,-4) && lua_isnil(state,-3) && lua_isboolean(state,-2) && !lua_toboolean(state,-2) && lua_isboolean(state,-1) && !lua_toboolean(state,-1)){valid=true;return result;}
  if(!lua_isstring(state,-4)||!lua_isnumber(state,-3))return result;const char*formula=lua_tostring(state,-4);const double chargeValue=lua_tonumber(state,-3);if(!formula||!formula[0]||!std::isfinite(chargeValue)||chargeValue<1||chargeValue>7||floor(chargeValue)!=chargeValue)return result;const int charges=static_cast<int>(chargeValue);
  result.config.spellFormula=formula;result.config.charges=charges;result.config.automatic=lua_toboolean(state,-2)!=0;result.config.prototypeFallback=lua_toboolean(state,-1)!=0;
  result.found=true;valid=true;return result;
 });
 config=value.config;return value.found;
}

namespace {
void CombatEffects(const CUnit&unit,float (&effects)[53]){
 for(int i=0;i<unit.GetSpellEffects().GetSize();i++){const auto&e=unit.GetSpellEffects()[i];int id=e.GetEffectType();float power=e.GetPower();if(id>=0&&id<53){if(!std::isfinite(power))throw std::runtime_error("Invalid combat effect");effects[id]=(std::max)(effects[id],power);}}
}
void CheckAttack(const SAttackParams&p){
 if(!std::isfinite(p.fDamageMin)||!std::isfinite(p.fDamageMax)||!std::isfinite(p.fWeaponWeight)||!std::isfinite(p.fToHitRandom))throw std::runtime_error("Invalid attack result");
}
void RawWeaponAttack(CUnit&unit,const SRPGUnitStats&stats,SAttackParams&p){
 auto*w=unit.GetCurrentQWeapon();
 if(w){p.fDamageMin=w->GetDamageMin();p.fDamageMax=w->GetDamageMax();p.rpgWeaponType=w->GetWeaponType();p.rpgAttackType=CItemWeapon::GetAttackType(w->GetSubtypeID());p.fWeaponWeight=w->CItem::GetWeight();p.nAttack=stats.GetWeaponSkillLevel(p.rpgWeaponType,&unit)+w->GetAtackBonus();p.nDefense=stats.GetWeaponSkillLevel(p.rpgWeaponType,&unit)+w->GetDefenceBonus();}
 else {p.fDamageMin=stats.fpNAWDamageMin;p.fDamageMax=stats.fpNAWDamageMin+stats.fpNAWDamageRnd;p.nAttack=stats.fpNAWToHit;p.nDefense=stats.fpNAWParry;p.fWeaponWeight=stats.fpNAWWeight;p.rpgWeaponType=stats.rpgNAWWeaponType;const auto*race=unit.GetRaceModel();for(int i=0;i<RPG_NUM_ATTACK_TYPES;i++)if(race->attacks[i]!=0){p.rpgAttackType=ERPGAttackTypes(i);break;}}
 p.fToHitRandom=stats.fpNAWToHitRandom;
}
}
float CModPackage::ModifiedDamage(const SRPGUnitStats&stats,const CUnit&unit,float damage,float roll,int leftSlot,bool left){
 if(!std::isfinite(damage)||!std::isfinite(roll))throw std::runtime_error("Invalid damage input");
 CallbackFrame frame(state);SRPGUnitStats staged=stats;frame.scope->unit=&unit;frame.scope->stagedStats=&staged;
 int slot=int(unit.GetCurrentWeaponSlotIndex());auto*weapon=slot>=0&&slot<int(QI_MAX_WEAPONS)?frame.scope->Weapon(unit,slot):nullptr;
 BeginCallback("GetModifiedDamageOnServerJmv");ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&unit,frame.scope);lua_pushnumber(state,damage);lua_pushnumber(state,roll);lua_pushinteger(state,leftSlot);lua_pushboolean(state,left);lua_pushstring(state,unit.GetPrototype()->szName);lua_pushinteger(state,weapon?weapon->GetShapeEntry()->dwTypeID:0);lua_pushinteger(state,weapon?slot:-1);lua_pushboolean(state,IsIDofPersonage(unit.GetID())&&unit.GetTypeID()!=MOTID_ANIMAL&&unit.GetTypeID()!=MOTID_MONSTER);lua_pushboolean(state,CombatSetting("JMVFixCriticalHitSoundTrigger")!=0);Call(11,1);
 double value=Number(state,"Modified server damage");if(fabs(value)>(std::numeric_limits<float>::max)())throw std::runtime_error("Damage overflow");return float(value);
}
float CModPackage::ModifiedDamage(const SRPGUserUnitStats&stats,const CUnit&unit,float damage){
 if(!std::isfinite(damage))throw std::runtime_error("Invalid damage input");CallbackFrame frame(state);
 int slot=int(unit.GetCurrentWeaponSlotIndex());auto*weapon=slot>=0&&slot<int(QI_MAX_WEAPONS)?frame.scope->Weapon(unit,slot):nullptr;
 BeginCallback("GetModifiedDamageOnClientJmv");ModLua::Push(state,&stats,frame.scope);ModLua::Push(state,&unit,frame.scope);lua_pushnumber(state,damage);lua_pushnumber(state,0);lua_pushstring(state,unit.GetPrototype()->szName);lua_pushinteger(state,weapon?weapon->GetShapeEntry()->dwTypeID:0);lua_pushinteger(state,weapon?slot:-1);lua_pushboolean(state,IsIDofPersonage(unit.GetID())&&unit.GetTypeID()!=MOTID_ANIMAL&&unit.GetTypeID()!=MOTID_MONSTER);lua_pushboolean(state,CombatSetting("JMVFixCriticalHitSoundTrigger")!=0);Call(9,1);
 double value=Number(state,"Modified client damage");if(fabs(value)>(std::numeric_limits<float>::max)())throw std::runtime_error("Damage overflow");return float(value);
}
void CModPackage::AttackParams(const SRPGUnitStats&stats,CUnit&unit,SAttackParams&result,float roll){
 CallbackFrame frame(state);SRPGUnitStats staged=stats;frame.scope->unit=&unit;frame.scope->stagedStats=&staged;SAttackParams p={};
 if(!IsIDofPersonage(unit.GetID())){auto*server=AuthoritativeServer(&unit);if(server&&server->GetMapObjectServer()){SRPGCommonStats common=*server->GetMapObjectServer()->GetRPGStats();BeginCallback("RecalcInitialJmvStatsExt");ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&common,frame.scope);ModLua::Push(state,&unit,frame.scope);lua_pushstring(state,unit.GetPrototype()->szName);Call(4,0);}}
 RawWeaponAttack(unit,staged,p);
 p.fDamageMax=ModifiedDamage(staged,unit,p.fDamageMax,roll);p.fDamageMin=ModifiedDamage(staged,unit,p.fDamageMin,roll);
 p.ApplyDifficultyLevel(IsIDofPersonage(unit.GetID()));float effects[53]={};CombatEffects(unit,effects);
 BeginCallback("CalcAttackParamsOnServer");ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&unit,frame.scope);ModLua::Push(state,&p,frame.scope);ModLua::PushEffects(state,effects,53,frame.scope);Call(4,0);CheckAttack(p);result=p;
}
float CModPackage::ConditionalRetailDamage(SRPGUnitStats& stats,CUnit& unit,float damage){
 CallbackFrame frame(state);frame.scope->unit=&unit;frame.scope->stagedStats=&stats;
 int slot=int(unit.GetCurrentWeaponSlotIndex());auto*weapon=slot>=0&&slot<int(QI_MAX_WEAPONS)?unit.GetQWeapon(slot):nullptr;
 bool hasOwner=IsIDofPersonage(unit.GetID())&&unit.GetTypeID()!=MOTID_ANIMAL&&unit.GetTypeID()!=MOTID_MONSTER;
 float roll=float(ModNativeCrtRandom::Random(100));
 BeginCallback("GetModifiedDamageOnServerJmv");ModLua::Push(state,&stats,frame.scope);ModLua::Push(state,&unit,frame.scope);lua_pushnumber(state,damage);lua_pushnumber(state,roll);lua_pushinteger(state,0);lua_pushboolean(state,false);lua_pushstring(state,unit.GetPrototype()->szName);lua_pushinteger(state,weapon?weapon->GetShapeEntry()->dwTypeID:0);lua_pushinteger(state,-1);lua_pushboolean(state,hasOwner);lua_pushboolean(state,CombatSetting("JMVFixCriticalHitSoundTrigger")!=0);Call(11,1);
 double value=Number(state,"Conditional retail damage");lua_pop(state,1);if(fabs(value)>(std::numeric_limits<float>::max)())throw std::runtime_error("Conditional retail damage overflow");return float(value);
}
void CModPackage::ConditionalAttackParams(SRPGUnitStats& stats,CUnit& unit,SAttackParams& result,bool inherited){
 using namespace ModConditionalCombat;
 CallbackFrame frame(state);frame.scope->unit=&unit;frame.scope->stagedStats=&stats;
 SAttackParams p={};
 auto raceAttack=[&](){const auto*race=unit.GetRaceModel();for(int i=0;i<RPG_NUM_ATTACK_TYPES;i++)if(race->attacks[i]!=0){p.rpgAttackType=ERPGAttackTypes(i);break;}};
 auto customSkill=[&](ERPGWeaponTypes type){bool archery=type==RPGWST_BOW||type==RPGWST_CROSSBOW||type==RPGWST_HANDS3||type==RPGWST_HANDS7;return stats.GetAutoSkillLevel(archery?RPGASKL_ARCHERY:RPGASKL_MELEE,&unit);};
 auto modified=[&](float damage,float roll,bool retail){
  int slot=int(unit.GetCurrentWeaponSlotIndex());auto*weapon=slot>=0&&slot<int(QI_MAX_WEAPONS)?unit.GetQWeapon(slot):nullptr;
  bool personage=IsIDofPersonage(unit.GetID()),living=unit.GetTypeID()!=MOTID_ANIMAL&&unit.GetTypeID()!=MOTID_MONSTER;
  bool hasOwner=personage&&living&&(retail||(unit.GetID()>=1000000000&&unit.GetOwner()!=nullptr));
  BeginCallback("GetModifiedDamageOnServerJmv");ModLua::Push(state,&stats,frame.scope);ModLua::Push(state,&unit,frame.scope);lua_pushnumber(state,retail?damage:(std::max)(1.f,damage));lua_pushnumber(state,roll);lua_pushinteger(state,retail?0:-1);lua_pushboolean(state,false);lua_pushstring(state,unit.GetPrototype()->szName);lua_pushinteger(state,weapon?weapon->GetShapeEntry()->dwTypeID:0);lua_pushinteger(state,retail?-1:slot);lua_pushboolean(state,hasOwner);lua_pushboolean(state,CombatSetting("JMVFixCriticalHitSoundTrigger")!=0);Call(11,1);
  double value=Number(state,"Conditional server damage");lua_pop(state,1);if(fabs(value)>(std::numeric_limits<float>::max)())throw std::runtime_error("Conditional damage overflow");return float(value);
 };
 Backend b;
 b.roll=[](){return float(ModNativeCrtRandom::Random(100));};
 b.retailSeed=[&](){
  auto*w=unit.GetCurrentQWeapon();
  if(IsIDofPersonage(unit.GetID())&&w){p.fDamageMin=w->GetDamageMin();p.fDamageMax=w->GetDamageMax();p.rpgWeaponType=w->GetWeaponType();p.rpgAttackType=CItemWeapon::GetAttackType(w->GetSubtypeID());p.fWeaponWeight=w->CItem::GetWeight();p.nAttack=stats.GetWeaponSkillLevel(p.rpgWeaponType,&unit)+w->GetAtackBonus();p.nDefense=stats.GetWeaponSkillLevel(p.rpgWeaponType,&unit)+w->GetDefenceBonus();}
  else {p.fDamageMin=stats.fpNAWDamageMin;p.fDamageMax=stats.fpNAWDamageMin+stats.fpNAWDamageRnd;p.nAttack=int(stats.fpNAWToHit);p.nDefense=int(stats.fpNAWParry);p.fWeaponWeight=stats.fpNAWWeight;p.rpgWeaponType=w?w->GetWeaponType():stats.rpgNAWWeaponType;if(w)p.rpgAttackType=CItemWeapon::GetAttackType(w->GetSubtypeID());else raceAttack();}
  if(p.rpgWeaponType==RPGWST_BOW||p.rpgWeaponType==RPGWST_CROSSBOW)p.nDefense=0;
 };
 b.retailDamage=[&](bool maximum,float roll){if(maximum)p.fDamageMax=modified(p.fDamageMax,roll,true);else p.fDamageMin=modified(p.fDamageMin,roll,true);};
 b.initialStats=[&](){if(!IsIDofPersonage(unit.GetID())){auto*server=AuthoritativeServer(&unit);if(!server||!server->GetMapObjectServer())throw std::runtime_error("Conditional attack needs server common stats");BeginCallback("RecalcInitialJmvStatsExt");ModLua::Push(state,&stats,frame.scope);ModLua::Push(state,server->GetMapObjectServer()->GetRPGStats(),frame.scope);ModLua::Push(state,&unit,frame.scope);lua_pushstring(state,unit.GetPrototype()->szName);Call(4,0);}};
 b.raw=[&](Owner owner){
  auto*w=unit.GetCurrentQWeapon();
  if(w){p.fDamageMin=CItemWeapon::GetDamageMin(w->GetSubtypeID());p.fDamageMax=CItemWeapon::GetDamageMax(w->GetSubtypeID());p.rpgWeaponType=w->GetWeaponType();p.rpgAttackType=CItemWeapon::GetAttackType(w->GetSubtypeID());p.fWeaponWeight=w->CItem::GetWeight();float skill=customSkill(p.rpgWeaponType);p.nAttack=int(skill+w->GetAtackBonus());p.nDefense=int(skill+w->GetDefenceBonus());}
  else {p.rpgWeaponType=stats.rpgNAWWeaponType;p.fDamageMin=stats.fpNAWDamageMin;p.fDamageMax=stats.fpNAWDamageMin+stats.fpNAWDamageRnd;bool direct=owner==Owner::DirectDefender;float skill=direct?customSkill(p.rpgWeaponType):0;p.nAttack=Unarmed(stats.fpNAWToHit,skill,direct);p.nDefense=Unarmed(stats.fpNAWParry,skill,direct);p.fWeaponWeight=stats.fpNAWWeight;raceAttack();}
 };
 b.damagePair=[&](float roll){p.fDamageMax=modified(p.fDamageMax,roll,false);p.fDamageMin=modified(p.fDamageMin,roll,false);};
 b.difficulty=[&](){p.fToHitRandom=stats.fpNAWToHitRandom;p.ApplyDifficultyLevel(IsIDofPersonage(unit.GetID()));};
 b.attackCallback=[&](int nativeCount){float effects[53]={};const auto&source=unit.GetSpellEffects();for(int i=0;i<source.GetSize();i++){const auto&e=source[i];int id=e.GetEffectType();if(id>=0&&id<nativeCount){if(!std::isfinite(e.GetPower()))throw std::runtime_error("Invalid conditional combat effect");effects[id]=(std::max)(effects[id],e.GetPower());}}BeginCallback("CalcAttackParamsOnServer");ModLua::Push(state,&stats,frame.scope);ModLua::Push(state,&unit,frame.scope);ModLua::Push(state,&p,frame.scope);ModLua::PushEffects(state,effects,53,frame.scope);Call(4,0);};
 Produce(inherited?Owner::Inherited:Owner::DirectDefender,b);CheckAttack(p);result=p;
}

void CModPackage::AttackParams(const SRPGUserUnitStats&stats,CUnit&unit,SAttackParams&result){
 CallbackFrame frame(state);SAttackParams p={};auto*w=frame.scope->Weapon(unit,int(unit.GetCurrentWeaponSlotIndex()));bool hero=IsIDofPersonage(unit.GetID());
 if(hero&&w){p.fDamageMin=w->GetDamageMin();p.fDamageMax=w->GetDamageMax();p.rpgWeaponType=w->GetWeaponType();p.rpgAttackType=CItemWeapon::GetAttackType(w->GetSubtypeID());p.fWeaponWeight=w->CItem::GetWeight();const auto*shape=static_cast<const SIDBWeaponShape*>(w->GetShapeEntry());p.nAttack=stats.GetWeaponSkillLevel(p.rpgWeaponType,shape)+w->GetAtackBonus();p.nDefense=stats.GetWeaponSkillLevel(p.rpgWeaponType,shape)+w->GetDefenceBonus();}
 else {p.fDamageMin=stats.cNAWDamageMin;p.fDamageMax=stats.cNAWDamageMin+stats.cNAWDamageRnd;p.nAttack=stats.cNAWToHit;p.nDefense=stats.cNAWParry;p.fWeaponWeight=stats.cNAWWeight;p.rpgWeaponType=w?w->GetWeaponType():RPGWST_HANDS;if(w)p.rpgAttackType=CItemWeapon::GetAttackType(w->GetSubtypeID());else for(int i=0;i<RPG_NUM_ATTACK_TYPES;i++)if(unit.GetRaceModel()->attacks[i]!=0){p.rpgAttackType=ERPGAttackTypes(i);break;}}
 if(!CombatSetting("ArchersDefence")&&(p.rpgWeaponType==RPGWST_BOW||p.rpgWeaponType==RPGWST_CROSSBOW))p.nDefense=0;
 p.fDamageMax=ModifiedDamage(stats,unit,p.fDamageMax);p.fDamageMin=ModifiedDamage(stats,unit,p.fDamageMin);p.fToHitRandom=stats.cNAWToHitRandom;p.ApplyDifficultyLevel(hero);
 float effects[53]={};CombatEffects(unit,effects);BeginCallback("CalcAttackParamsOnClient");ModLua::Push(state,&stats,frame.scope);ModLua::Push(state,&unit,frame.scope);ModLua::Push(state,&p,frame.scope);ModLua::PushEffects(state,effects,53,frame.scope);lua_pushinteger(state,frame.scope->LeftHand(unit));Call(5,0);
 // LuaScripts GetAttackParams_538B74 adds shield terms after Lua, for one-handed primary weapons.
 if(w&&(p.rpgWeaponType==RPGWST_SWORD||p.rpgWeaponType==RPGWST_AXE||p.rpgWeaponType==RPGWST_DAGGER||p.rpgWeaponType==RPGWST_SPEAR||p.rpgWeaponType==RPGWST_HAMMER))for(int i=0;i<int(QI_MAX_WEAPONS);i++){auto*shield=frame.scope->Weapon(unit,i);if(i!=int(unit.GetCurrentWeaponSlotIndex())&&shield&&shield->GetWeaponType()==RPGWST_HANDS1){p.nAttack+=int(shield->GetAtackBonus());p.nDefense+=int(shield->GetDefenceBonus());p.fDamageMin+=shield->GetDamageMin();p.fDamageMax+=shield->GetDamageMax();}}
 CheckAttack(p);result=p;
}

int CModPackage::CombatSetting(const char*key)const{auto i=combatSettings.find(key);return i==combatSettings.end()?0:i->second;}
bool CModPackage::AdvancedUI(const char*key)const{
 return ModItemUiPolicy::Advanced(key,[&](const char*name){return CombatSetting(name);});
}
std::string CModPackage::InstructionComponent(bool prototype,bool figure,const char*code,const char*fallback){
 const bool enabled=CombatSetting("JMVGraphics2")!=0;
 const auto result=ModItemUiPolicy::Instruction(enabled,prototype,figure,code,fallback,
  [&](const char*name,int column){CallbackFrame frame(state);BeginCallback("GetComponents");lua_pushstring(state,name);lua_pushinteger(state,column);Call(2,1);const char*value=lua_tostring(state,-1);std::string result=value?value:"";lua_pop(state,1);return result;});
 static unsigned notes=0;if(notes++<16)EngineDiagnostics::Event("P05 instruction graphics2=%d prototype=%d figure=%d code=%s resolved=%s",enabled,prototype,figure,code?code:"",result.c_str());
 return result;
}
float CModPackage::GoCampEnemyRadius(){
 CallbackFrame frame(state);lua_getglobal(state,"GetGoCampConfigValue");if(!lua_isfunction(state,-1))return 25.0f;
 lua_pushinteger(state,1);lua_pushnumber(state,25.0);Call(2,1);
 if(!lua_isnumber(state,-1))return 25.0f;const float value=float(lua_tonumber(state,-1));
 return value>=0.0f&&value<=500.0f?value:25.0f;
}
bool CModPackage::DeathFearSettings(float &enterHp,float &exitHp,float &fleeDistance,float &speedPercent){
 enterHp=15.0f;exitHp=30.0f;fleeDistance=25.0f;speedPercent=65.0f;CallbackFrame frame(state);
 auto read=[&](int property,float fallback){BeginCallback("GetDeathFearConfigValue");lua_pushinteger(state,property);lua_pushnumber(state,fallback);Call(2,1);return float(Number(state,"DeathFear configuration"));};
 enterHp=read(1,enterHp);fleeDistance=read(2,fleeDistance);speedPercent=read(3,speedPercent);exitHp=read(4,exitHp);return true;
}
bool CModPackage::DeathFearExcludedPrototype(const char*prototype){
 if(!prototype||!*prototype)return false;CallbackFrame frame(state);lua_getglobal(state,"IsDeathFearExcludedPrototype");
 if(!lua_isfunction(state,-1))return false;lua_pushstring(state,prototype);Call(1,1);
 return lua_toboolean(state,-1)!=0;
}
bool CModPackage::GoCampZoneExcluded(const char*zone){
 if(!zone||!*zone)return true;CallbackFrame frame(state);BeginCallback("IsGoCampExcludedZone");lua_pushstring(state,zone);Call(1,1);
 if(!lua_isboolean(state,-1))throw std::runtime_error("GoCamp zone policy must return boolean");return lua_toboolean(state,-1)!=0;
}
bool CModPackage::PartyDisguiseMatches(const char*name){
 if(!name||!*name)return false;
 auto*player=theWorldServer.GetPlayer(0);if(!player)return false;
 auto*server=dynamic_cast<CPlayerServer*>(player->GetPlayerSpecific());
 if(!server||!server->IsPartyDeployed()||server->GetStatus()==CPlayerServer::PSS_CAMP)return false;
 auto found=disguiseProfiles.find(name);
 if(found==disguiseProfiles.end()){
  auto utf8=[](const char*s){int n=MultiByteToWideChar(1251,0,s,-1,nullptr,0);std::vector<wchar_t>w(n);MultiByteToWideChar(1251,0,s,-1,w.data(),n);int k=WideCharToMultiByte(CP_UTF8,0,w.data(),-1,nullptr,0,nullptr,nullptr);std::string out(k,'\0');WideCharToMultiByte(CP_UTF8,0,w.data(),-1,&out[0],k,nullptr,nullptr);out.pop_back();return out;};
  SocialStealth::Config config;SocialStealth::Profile profile;
  if(SocialStealth::ReadConfig(state,utf8(name).c_str(),config,profile.error)){
   std::unordered_map<std::string,int> index;
   for(int i=0;i<theItemDatabase.armors.GetNumEntries();++i){auto inserted=index.emplace(utf8(theItemDatabase.armors.GetEntry(i)->szName),i);if(!inserted.second)inserted.first->second=-1;}
   SocialStealth::Compile(profile,config,index);
  }
  EngineDiagnostics::Event("PartyDisguiseMatches profile=%s valid=%d reason=%s",name,profile.valid,profile.error.c_str());
  found=disguiseProfiles.emplace(name,std::move(profile)).first;
 }
 const auto&profile=found->second;if(!profile.valid)return false;
 int living=0;bool matches=true;
 for(int i=0;i<player->party.people.GetSize();++i){
  CUnit*unit=theWorldServer.GetUnit(player->party.people[i].nID);if(!unit)return false;
  if(unit->IsDead())continue;if(unit->GetOwner()!=player)return false;++living;
  std::array<int,SocialStealth::ArmorSlots> armor;
  for(int slot=0;slot<SocialStealth::ArmorSlots;++slot){auto*item=unit->GetQArmor(slot);armor[slot]=!item?-1:item->GetShape()<unsigned(theItemDatabase.armors.GetNumEntries())?int(item->GetShape()):-2;}
  // Resolve current equipment directly; no stale member cache across camp, saves or zones.
  matches=SocialStealth::Evaluate(profile,armor).matches&&matches;
 }
 return living>0&&matches;
}
// Camp presentation follows addon CI3DObjectNameStats::UpdateStats, not combat wound modifiers.
float CModPackage::CampDisplayMana(const SRPGUnitStats&stats,int carried,int capacity,bool baseStats){
 CallbackFrame frame(state);BeginCallback("OverloadManacostModsOnServer");
 lua_pushinteger(state,ModCampStats::Weight(carried,baseStats));lua_pushinteger(state,capacity);Call(2,1);
 return stats.RecalcMP()*float(Number(state,"Camp overload mana"));
}
int CModPackage::CampSkillCapacity(CPersonage&pers,CUnit*unit){
 float jewelry=1,sets=1;float strength=pers.stats.psStrength.fpEffectiveValue;
 for(int i=0;i<pers.items.GetSize();++i)if(auto*q=pers.items[i])jewelry+=InfoProperty("GetJewelryPropsInside",q->GetShapeEntry()->szName,12)*.01f;
 if(unit){strength=unit->GetUserUnitStats()->cStrength;CallbackFrame frame(state);BeginCallback("GetSetsEncumbrance");ModLua::Push(state,unit,frame.scope);Call(1,1);sets=float(Number(state,"Skill sets encumbrance"));}
 return ModCampStats::Capacity(strength,pers.stats.GetPerkModifier(CombatSetting("JMVPerks")?1:RPGPRK_LIFT),jewelry,1,sets);
}
void CModPackage::CampInfo(CPersonage&pers,CUnit*unit,int&capacity,float&seconds,float*displayActions,float*displayMana,bool baseStats){
 const auto&stats=pers.stats;
 float jewelry=1,complex=1,sets=1;
 for(int i=0;i<pers.items.GetSize();++i)if(auto*q=pers.items[i])
  jewelry+=InfoProperty("GetJewelryPropsInside",q->GetShapeEntry()->szName,12)*.01f;
 float strength=stats.psStrength.fpEffectiveValue;
 if(unit){
  strength=unit->GetUserUnitStats()->cStrength; complex+=InfoComplex(*unit,8)*.01f;
  CallbackFrame frame(state);BeginCallback("GetSetsEncumbrance");ModLua::Push(state,unit,frame.scope);Call(1,1);sets=float(Number(state,"Camp sets encumbrance"));
 }
 capacity=ModCampStats::Capacity(strength,stats.GetPerkModifier(CombatSetting("JMVPerks")?1:RPGPRK_LIFT),jewelry,complex,sets);
 float actions=(10+stats.psDexterity.fpEffectiveValue*.2f)*(1+stats.GetPerkModifier(14)*.01f);
 int slot=int(pers.dwCurrentWeapon),left=unit?ModStats::LeftHand(*unit,[&](int index)->CItemWeapon*{return index>=0&&index<pers.weapons.GetSize()?pers.weapons[index]:nullptr;}):-1;
 auto*right=slot>=0&&slot<pers.weapons.GetSize()?pers.weapons[slot]:nullptr;
 auto*off=left>=0&&left<pers.weapons.GetSize()?pers.weapons[left]:nullptr;
 if(right&&left<0&&stats.cPerks[28]){
  int t=right->GetWeaponType();
  if(t!=RPGWST_HANDS2&&t!=RPGWST_HANDS4&&t!=RPGWST_HANDS5&&t!=RPGWST_HANDS6&&t!=RPGWST_HANDS7&&t!=RPGWST_BOW&&t!=RPGWST_CROSSBOW)actions*=1.15f;
 }
 auto weight=[&](CItemWeapon*w){return int(InfoProperty("GetWeaponPropsInside",w->GetShapeEntry()->szName,7));};
 if(CombatSetting("JMV136")&&right){int w=weight(right);actions*=1+stats.GetPerkModifier(w==3?6:w==2?24:0)*(w==3?3:1)*.01f;}
 float armor=0;for(int i=0;i<pers.armors.GetSize();++i)if(auto*a=pers.armors[i])armor+=InfoProperty("GetArmorPropsInside",a->GetShapeEntry()->szName,7);
 actions*=1+armor;if(unit)actions*=1+InfoComplex(*unit,18)*.01f;
 if(displayActions)*displayActions=actions;
 if(displayMana)*displayMana=CampDisplayMana(pers.stats,int(pers.GetItemsWeight()),capacity,baseStats);
 float points=actions;int attacks=1;
 if(right&&(CombatSetting("JMVAttacks")||(unit&&IsIDofPersonage(unit->GetID()))))points=float(right->GetActionPoints());
 if(CombatSetting("JMVAttacks")&&right&&off&&off->GetWeaponType()!=RPGWST_HANDS1){
  points=(right->GetActionPoints()+off->GetActionPoints())*.5f;attacks=2;
  int rt=right->GetWeaponType(),lt=off->GetWeaponType(),delta=abs(weight(right)-weight(off));
  if(rt==lt&&!delta){if(rt==RPGWST_DAGGER){points*=3;attacks=6;}else if(rt==RPGWST_SWORD||rt==RPGWST_AXE){points*=2;attacks=4;}}
  points*=1+delta*.125f;points*=1+float(rt!=lt)*.25f;
 }
 seconds=int(points*(fabs(actions)>.00001f?15/actions:1))/15.0f/attacks;
}
int CModPackage::AttackCycle(const SRPGUnitStats&stats,const CUnit&unit,bool attack){
 float cycle=15,action=stats.action.fpEffectiveValue;if(fabs(action)>.00001f)action=15/action;
 if(attack){float points=stats.fpNAWActionPoints;auto*right=unit.GetCurrentQWeapon();
 if(right&&(CombatSetting("JMVAttacks")||IsIDofPersonage(unit.GetID()))){points=float(right->GetActionPoints());
 if(CombatSetting("JMVAttacks")){int slot=ModStats::LeftHand(unit);auto*left=slot>=0?unit.GetQWeapon(slot):nullptr;if(left&&left->GetWeaponType()!=RPGWST_HANDS1){float rightPoints=points,leftPoints=float(left->GetActionPoints());int rt=right->GetWeaponType(),lt=left->GetWeaponType();points=(rightPoints+leftPoints)*.5f;if(rt==lt){if(lt==RPGWST_DAGGER)points*=3;else if(lt==RPGWST_SWORD||lt==RPGWST_AXE)points*=2;}
 auto weight=[&](CItemWeapon*w){auto value=Lookup("WeaponProp",std::string(w->GetShapeEntry()->szName),7,0);if(!value.is_number())throw std::runtime_error("Invalid weapon weight class");return int(value.get<double>());};int delta=abs(weight(right)-weight(left));if(delta>0)points=(rightPoints+leftPoints)*.5f;points+=points*(float(delta)*.125f);points+=points*(float(rt!=lt)*.25f);
 }}}
 cycle=points*action;}
 if(!std::isfinite(cycle)||cycle<INT_MIN||cycle>INT_MAX)throw std::runtime_error("Attack cycle out of range");return int(cycle);
}

namespace {
float CombatNumber(lua_State*s,const char*name){double n=Number(s,name);if(fabs(n)>(std::numeric_limits<float>::max)())throw std::runtime_error("Combat number overflow");return float(n);}
int CombatInt(float n){if(!std::isfinite(n)||double(n)<INT_MIN||double(n)>INT_MAX)throw std::runtime_error("Combat integer out of range");return int(n);}
}
ModCombat::Defense CModPackage::IncomingAttack(const SRPGUnitStats&stats,CUnit&unit,CUnit*attacker,const SRPGAttackPortion&ap,int roll){
 CallbackFrame frame(state);ModCombat::Defense d;SRPGUnitStats staged=stats;frame.scope->unit=&unit;frame.scope->stagedStats=&staged;
 auto finish=[&](const char*fn,int n){Call(n,1);float result=CombatNumber(state,fn);lua_pop(state,1);return result;};
 auto unitNumber=[&](const char*fn,CUnit&u){BeginCallback(fn);ModLua::Push(state,&u,frame.scope);return finish(fn,1);};
 auto clientNumber=[&](const char*fn,int slot){BeginCallback(fn);ModLua::Push(state,unit.GetUserUnitStats(),frame.scope);ModLua::Push(state,&unit,frame.scope);if(slot>=0)lua_pushinteger(state,slot);return finish(fn,slot>=0?3:2);};
 const int left=frame.scope->LeftHand(unit),current=int(unit.GetCurrentWeaponSlotIndex());auto*lw=left>=0?frame.scope->Weapon(unit,left):nullptr;auto*rw=frame.scope->Weapon(unit,current);
 if((lw&&lw->GetWeaponType()==RPGWST_HANDS1)||(CombatSetting("JMVWeaponBlocking")&&rw))d.block=clientNumber("CheckShieldBlock",-1);
 float jewelry=0;for(int i=0;i<4;i++)if(auto*q=unit.GetQItem(i)){auto value=Lookup("JewelryProp",std::string(q->GetShapeEntry()->szName),12,0);if(!value.is_number())throw std::runtime_error("Invalid jewelry encumbrance");jewelry+=float(value.get<double>())*.01f;}
 float capacity=float(unit.GetUserUnitStats()->cStrength)*12;capacity+=capacity*stats.GetPerkModifier(CombatSetting("JMVPerks")?1:RPGPRK_LIFT)*.01f;
 int maxWeight=CombatInt(capacity*(1+jewelry)*unitNumber("GetSetsEncumbrance",unit)),weight=CombatInt(unit.GetItemsWeight());
 if(rw){BeginCallback("GetOverloadParryOnServer");lua_pushinteger(state,weight);lua_pushinteger(state,maxWeight);float overload=finish("GetOverloadParryOnServer",2);float right=clientNumber("GetWeaponParryChance",current)*overload;d.parryChance=left<0?right:ModCombat::ParryUnion(right,clientNumber("GetWeaponParryChance",left)*overload);}
 d.parry=CombatInt(d.parryChance)>roll;
 if(ap.fBackStabMultiplier!=1||unit.HasSpellEffect(SPELL_ID_STUN)){d.block=0;d.parry=false;}
 if(attacker){
 auto complex=[&](int field){BeginCallback("GetComplexSummStats");ModLua::Push(state,attacker,frame.scope);lua_pushinteger(state,field);return finish("GetComplexSummStats",2);};
 if(ap.hl==RPGHL_ALL){int type=3;for(int i=3;i<RPG_NUM_ATTACK_TYPES;i++)if(ap.fpAttacks[i]>0){type=i;break;}int perkType=type==4?5:type==5?4:type;auto*client=attacker->GetUserUnitStats();float skill=float(client->cAutoSkills[(type-3)<=RPGMST_ACID?RPGASKL_ELEMENTAL:(type-3)<=RPGMST_DIVINATION?RPGASKL_SENSE:RPGASKL_ASTRAL])+client->GetPerkModifier(RPGPRK_FIRE+perkType-3)+int(client->cIntelligence)-25;
 auto*server=AuthoritativeServer(attacker);if(!server)throw std::runtime_error("Magic attacker has no server stats");SRPGUnitStats attackStats=*server->GetRPGStats();BeginCallback("GetWiseManDamageModiferLS");ModLua::Push(state,attacker,frame.scope);ModLua::Push(state,&attackStats,frame.scope);lua_pushnumber(state,attackStats.psIntelligence.fpEffectiveValue);float wise=finish("GetWiseManDamageModiferLS",3);
 d.damageMultiplier=wise*(.8f+(CombatInt(skill)*30.f/120.f)/100.f)*(attacker->GetOwner()?1+theSettings.nDifficultyLevel*.25f:1)*(1+complex(12));
 }else if(ap.hl==RPGHL_HEAD||(CombatSetting("JMVAnatomyLimbDamage")&&ap.hl>=RPGHL_LHAND&&ap.hl<=RPGHL_RLEG)){d.damageMultiplier=1+attacker->GetUserUnitStats()->GetPerkModifier(20)*.01f;if(ap.hl==RPGHL_HEAD)d.damageMultiplier*=1+complex(19)*.01f;}
 }
 if(!std::isfinite(d.block)||!std::isfinite(d.parryChance)||!std::isfinite(d.damageMultiplier))throw std::runtime_error("Invalid incoming combat result");return d;
}
ModCombat::Defense CModPackage::IncomingCustomAttack0(const SRPGUnitStats&stats,CUnit&unit,CUnit*attacker,const SRPGAttackPortion&ap){
 CallbackFrame frame(state);ModCombat::Defense d;d.custom0=true;SRPGUnitStats staged=stats;frame.scope->unit=&unit;frame.scope->stagedStats=&staged;
 auto finish=[&](const char*fn,int n){Call(n,1);float result=CombatNumber(state,fn);lua_pop(state,1);return result;};
 auto unitNumber=[&](const char*fn,CUnit&u){BeginCallback(fn);ModLua::Push(state,&u,frame.scope);return finish(fn,1);};
 auto clientNumber=[&](const char*fn,int slot){BeginCallback(fn);ModLua::Push(state,unit.GetUserUnitStats(),frame.scope);ModLua::Push(state,&unit,frame.scope);if(slot>=0)lua_pushinteger(state,slot);return finish(fn,slot>=0?3:2);};
 const int left=frame.scope->LeftHand(unit),current=int(unit.GetCurrentWeaponSlotIndex());auto*lw=left>=0?frame.scope->Weapon(unit,left):nullptr;auto*rw=frame.scope->Weapon(unit,current);
 if((lw&&lw->GetWeaponType()==RPGWST_HANDS1)||(CombatSetting("JMVWeaponBlocking")&&rw))d.block=clientNumber("CheckShieldBlock",-1);
 float jewelry=0;for(int i=0;i<4;i++)if(auto*q=unit.GetQItem(i))jewelry+=InfoProperty("GetJewelryPropsInside",q->GetShapeEntry()->szName,12)*.01f;
 float sets=unitNumber("GetSetsEncumbrance",unit);
 float capacity=float(unit.GetUserUnitStats()->cStrength)*12;capacity+=capacity*stats.GetPerkModifier(CombatSetting("JMVPerks")?1:RPGPRK_LIFT)*.01f;
 int maxWeight=CombatInt((capacity+capacity*jewelry)*sets),weight=CombatInt(unit.GetItemsWeight());
 auto overload=[&](){BeginCallback("GetOverloadParryOnServer");lua_pushinteger(state,weight);lua_pushinteger(state,maxWeight);return finish("GetOverloadParryOnServer",2);};
 d.parryChance=ModCustomLower::Parry(current,left,current>=0&&current<4,[&](int slot){return clientNumber("GetWeaponParryChance",slot);},overload,[](){return ModCustomLower::CrtRoll(100);},d.parry);
 if(ap.fBackStabMultiplier!=1||unit.HasSpellEffect(SPELL_ID_STUN)){d.block=0;d.parry=false;}
 if(attacker){
  if(ap.hl==RPGHL_ALL)d.scaleMagic=[this,attacker,ap](float damage){return ScaleCustomMagic0(*attacker,ap,damage);};
  else d.scaleSingle=[this,attacker,location=ap.hl](float damage){return ScaleCustomSingle0(*attacker,location,damage);};
 }
 d.rekeyCritical=[this](float original,float scaled){if(CombatSetting("JMVDaggerCritical"))customCriticals.Rekey(original,scaled);};
 d.resolveCritical=[this](float damage,float total){return customCriticals.Resolve(damage,total);};
 if(!std::isfinite(d.block)||!std::isfinite(d.parryChance)||!std::isfinite(d.damageMultiplier))throw std::runtime_error("Invalid custom0 combat result");return d;
}
float CModPackage::ScaleCustomMagic0(CUnit&attacker,const SRPGAttackPortion&ap,float damage){
 CallbackFrame frame(state);int type=3;for(int i=3;i<RPG_NUM_ATTACK_TYPES;i++)if(ap.fpAttacks[i]>0){type=i;break;}
 auto*client=attacker.GetUserUnitStats();int intelligence=int(client->cIntelligence)-25,perkType=type==4?5:type==5?4:type;
 float skill=float(client->cAutoSkills[(type-3)<=RPGMST_ACID?RPGASKL_ELEMENTAL:(type-3)<=RPGMST_DIVINATION?RPGASKL_SENSE:RPGASKL_ASTRAL]);skill+=client->GetPerkModifier(RPGPRK_FIRE+perkType-3);
 int totalSkill=CombatInt(skill+intelligence);float skillModifier=.8f+(totalSkill*30.f/120.f)/100.f,difficulty=attacker.GetOwner()?1+theSettings.nDifficultyLevel*.25f:1;
 auto*server=AuthoritativeServer(&attacker);if(!server)throw std::runtime_error("Custom0 magic attacker has no stats");SRPGUnitStats staged=*server->GetRPGStats();frame.scope->unit=&attacker;frame.scope->stagedStats=&staged;
 BeginCallback("GetWiseManDamageModiferLS");ModLua::Push(state,&attacker,frame.scope);ModLua::Push(state,&staged,frame.scope);lua_pushnumber(state,staged.psIntelligence.fpEffectiveValue);Call(3,1);float wise=CombatNumber(state,"GetWiseManDamageModiferLS");lua_pop(state,1);
 BeginCallback("GetComplexSummStats");ModLua::Push(state,&attacker,frame.scope);lua_pushinteger(state,12);Call(2,1);float complexModifier=1+CombatNumber(state,"GetComplexSummStats");lua_pop(state,1);
 return damage*wise*skillModifier*difficulty*complexModifier;
}
float CModPackage::ScaleCustomSingle0(CUnit&attacker,ERPGHitLocations location,float damage){
 const bool limbs=CombatSetting("JMVAnatomyLimbDamage")!=0;
 if(location!=RPGHL_HEAD&&(!limbs||location<RPGHL_LHAND||location>RPGHL_RLEG))return damage;
 float anatomy=1+attacker.GetUserUnitStats()->GetPerkModifier(20)*.01f;
 if(location!=RPGHL_HEAD)return damage*anatomy;
 CallbackFrame frame(state);BeginCallback("GetComplexSummStats");ModLua::Push(state,&attacker,frame.scope);lua_pushinteger(state,19);Call(2,1);float complex=1+CombatNumber(state,"GetComplexSummStats")*.01f;lua_pop(state,1);
 return limbs?damage*(anatomy*complex):damage*anatomy*complex;
}
int CModPackage::CustomLeft(const CUnit&unit)const{ModLua::Scope scope;return scope.LeftHand(unit);}
void CModPackage::RememberCustomAttack0(const SRPGUnitStats&stats,const CUnit&unit,const SRPGAttackPortion&ap,float roll){
 CallbackFrame frame(state);SRPGUnitStats staged=stats;frame.scope->unit=&unit;frame.scope->stagedStats=&staged;int current=int(unit.GetCurrentWeaponSlotIndex()),left=frame.scope->LeftHand(unit);auto*w=frame.scope->Weapon(unit,current);float chance=0;
 if(current>=0&&current<4&&w){BeginCallback("GetCritOnServerJmv");ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&unit,frame.scope);lua_pushinteger(state,left);lua_pushboolean(state,false);lua_pushstring(state,unit.GetPrototype()->szName);lua_pushinteger(state,w->GetWeaponType());lua_pushinteger(state,current);Call(7,1);chance=CombatNumber(state,"Custom0 critical chance");}
 customCriticals.Put(ap.fpDamage,roll<chance&&roll!=0,unit.GetTypeID()!=MOTID_HUMAN);
}
bool CModPackage::CustomCriticalHit(const SRPGAttackPortion&ap){return customCriticals.Hit(ap.fpDamage);}

std::string CModPackage::DualAnimation(const CUnit&unit)
{
 if(!CombatSetting("JMVAttacks"))return {};
 ModLua::Scope scope;auto*right=scope.Weapon(unit,int(unit.GetCurrentWeaponSlotIndex()));auto*left=scope.Weapon(unit,scope.LeftHand(unit));
 if(!right||!left||right==left||left->GetWeaponType()==RPGWST_HANDS1)return {};
 int rt=right->GetWeaponType(),lt=left->GetWeaponType();
 auto weight=[&](CItemWeapon*w){auto value=Lookup("WeaponProp",std::string(w->GetShapeEntry()->szName),7,0);if(!value.is_number())throw std::runtime_error("Invalid weapon weight class");return value.get<double>();};
 bool equal=weight(right)==weight(left);
 if(rt==lt){switch(rt){
 case RPGWST_SWORD:return equal?"dat01sws44":"dat01crcr37";
 case RPGWST_AXE:return equal?"dat01axes42":"dat01crcr37";
 case RPGWST_HAMMER:return equal?"dat01hams24":"dat01crcr37";
 case RPGWST_DAGGER:return equal?"dat01dagg75":"dat01sprs37";
 case RPGWST_SPEAR:return "dat01sprs37";
 case RPGWST_HANDS3:case RPGWST_HANDS7:return "dat01st237";
 }}
 auto crush=[](int t){return t==RPGWST_SWORD||t==RPGWST_AXE||t==RPGWST_HAMMER||t==RPGWST_HANDS4||t==RPGWST_HANDS5||t==RPGWST_HANDS6;};
 auto pike=[](int t){return t==RPGWST_SPEAR||t==RPGWST_HANDS2;};
 if(crush(rt)&&crush(lt))return "dat01crcr37";
 if(pike(rt)&&pike(lt))return "dat01sprs37";
 if(crush(rt)&&pike(lt))return "dat01crst37";
 if(pike(rt)&&crush(lt))return "dat01stcr37";
 if(rt==RPGWST_DAGGER&&pike(lt))return "dat01st237";
 if(pike(rt)&&lt==RPGWST_DAGGER)return "dat01sprs37";
 return "dat01crcr37";
}
std::vector<std::pair<int,bool>> CModPackage::DualStrikes(const char*animation,int length)
{
 CallbackFrame frame(state);std::vector<std::pair<int,bool>> result;
 const bool validKey=ModStaticLookup::NameKey(animation);
 double frameLimit=0;
 auto property=[&](int column){
  std::string key;if(validKey){ModStaticLookup::KeyString(key,animation);ModStaticLookup::KeyValue(key,column);}
  return staticLookups.dual.Get(key,validKey && scriptInitialized && staticLookups.Eligible(state,0),[&](bool& valid){
   BeginCallback("GetDualAnims");lua_pushstring(state,animation);lua_pushinteger(state,column);Call(2,1);double n=Number(state,"Dual animation property");lua_pop(state,1);
   if(column==4)valid=n==floor(n) && n>=0 && n<=6;
   else if(column==17)valid=n>0 && n<=INT_MAX;
   else if(column>=5 && column<=10)valid=n>=0 && n<=frameLimit && n==floor(n);
   else valid=n==0 || n==1;
   return n;
  });
 };
 double count=property(4);if(count==0)return result;
 double frames=property(17);frameLimit=frames;if(count!=floor(count)||count<1||count>6||frames<=0||frames>INT_MAX||length<1)throw std::runtime_error("Invalid dual animation timing");
 for(int i=0;i<int(count);i++){double at=property(i+5),left=property(i+11);
     if(at<0||at>frames||at!=floor(at)||(left!=0&&left!=1))throw std::runtime_error("Invalid dual animation strike");
     // Match reference float scaling followed by truncation; coincident frames remain separate strikes.
     int tick=int(float(at)*(float(length)/float(frames)));result.emplace_back(tick,left!=0);
 }
 return result;
}

int CModPackage::AttackSlot(const CUnit&unit,bool left){
 ModLua::Scope scope;int slot=int(unit.GetCurrentWeaponSlotIndex());if(left){int candidate=scope.LeftHand(unit);auto*w=scope.Weapon(unit,candidate);if(w&&w->GetWeaponType()!=RPGWST_HANDS1)slot=candidate;}return slot;
}
void ModHitHistory::ClearRuntimeHitHistory(const char* reason){
 const auto count=State().Size();State().Clear();static unsigned notes=0;if(count && notes++<16)EngineDiagnostics::Event("P04 hit history reset reason=%s records=%u",reason,unsigned(count));
}
void CModPackage::CompensateHitHistory(const SRPGUnitStats&stats,CUnit&victim,CUnit*attacker,SRPGAttackPortion&ap,int defense,bool directCustom){
 const float before=ap.fpToHit;auto result=ModHitHistory::State().Observe(ModHitHistory::Enabled(CombatSetting("JMVAttacks")!=0,directCustom),attacker!=nullptr,attacker?attacker->GetID():0,victim.GetID(),ap.hl==RPGHL_ALL,before,defense,[&]{return StatsNumber("GetWeaponsMasterLS",stats);});
 ap.fpToHit=result.score;
 static unsigned notes=0;if(result.reason!=ModHitHistory::Reason::Disabled && notes++<32)EngineDiagnostics::Event("P04 hit history attacker=%d victim=%d reason=%d all=%d score=%g->%g defense=%d counts=%d/%d->%d/%d zero=%d maximum=%d missCap=%d",attacker?int(attacker->GetID()):-1,victim.GetID(),int(result.reason),ap.hl==RPGHL_ALL,double(before),double(ap.fpToHit),defense,result.before.hits,result.before.misses,result.after.hits,result.after.misses,result.zero,result.maximum,result.missCap);
}
void CModPackage::RememberAttack(const SRPGUnitStats&stats,const CUnit&unit,const SRPGAttackPortion&ap,float roll,bool left){
 CallbackFrame frame(state);SRPGUnitStats staged=stats;frame.scope->unit=&unit;frame.scope->stagedStats=&staged;int slot=AttackSlot(unit,left);auto*w=frame.scope->Weapon(unit,slot);float chance=0;
 if(w){BeginCallback("GetCritOnServerJmv");ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&unit,frame.scope);lua_pushinteger(state,frame.scope->LeftHand(unit));lua_pushboolean(state,left);lua_pushstring(state,unit.GetPrototype()->szName);lua_pushinteger(state,w->GetWeaponType());lua_pushinteger(state,slot);Call(7,1);chance=CombatNumber(state,"Critical chance");}
 std::shared_ptr<CItemWeapon> weapon;if(w){w->AddRef();weapon=std::shared_ptr<CItemWeapon>(w,[](CItemWeapon*p){p->Release();});}
 criticalAttacks[&ap]={roll<chance&&roll!=0,weapon,left};
}
bool CModPackage::ConsumeCritical(const SRPGAttackPortion&ap){auto i=criticalAttacks.find(&ap);if(i==criticalAttacks.end())return false;bool result=i->second.critical;criticalAttacks.erase(i);return result;}
void CModPackage::ForgetAttack(const SRPGAttackPortion&ap){criticalAttacks.erase(&ap);}
void CModPackage::MoveAttack(const SRPGAttackPortion&from,const SRPGAttackPortion&to){if(&from==&to)return;ForgetAttack(to);auto i=criticalAttacks.find(&from);if(i!=criticalAttacks.end()){criticalAttacks[&to]=std::move(i->second);criticalAttacks.erase(i);}}
bool CModPackage::AttackIsLeft(const SRPGAttackPortion&ap)const{auto i=criticalAttacks.find(&ap);return i!=criticalAttacks.end()&&i->second.left;}
std::shared_ptr<CItemWeapon> CModPackage::AttackWeapon(const SRPGAttackPortion&ap)const{auto i=criticalAttacks.find(&ap);return i==criticalAttacks.end()?nullptr:i->second.weapon;}

// Active skill policy has its own native ConstantValueCache contract, independent of R23.
bool CModPackage::SkillConfig(const char*name,ModSkills::Config&config){
 config={};if(!name||!*name)return false;
 const bool validKey=state&&strlen(name)<=256;bool cacheable=true;
 if(validKey){auto cached=skillConfigs.find(name);if(cached!=skillConfigs.end()){config=cached->second.second;return cached->second.first;}}
 Stack stack(state);auto getValue=[&](int property,double fallback,double&value){
 lua_getglobal(state,"GetMeleeActiveSpellValue");if(!lua_isfunction(state,-1)){lua_pop(state,1);cacheable=false;return false;}
 lua_pushstring(state,name);lua_pushinteger(state,property);lua_pushnumber(state,fallback);Call(3,1);
 if(!lua_isnumber(state,-1)){lua_pop(state,1);cacheable=false;return false;}value=lua_tonumber(state,-1);lua_pop(state,1);
 if(!std::isfinite(value))throw std::runtime_error("Non-finite active skill policy");
 if(property==2||property==3||property==7||property==8||property==9||property==13||property==15||property==16){
  if(fabs(value)>(std::numeric_limits<float>::max)())throw std::runtime_error("Active skill float policy out of range");
 }return true;};
 auto read=[&](){
    double enabled = 0.0;
    double requiredMelee = 0.0;
    double staminaMultiplier = 1.0;
    double weaponMask = 0.0;
    double scaleDamageWithMelee = 0.0;
    double grantPrototype = 0.0;
    double staminaCost = -1.0;
    double cooldownSeconds = 0.0;
    double damageMultiplier = 1.0;
    double allowDualWield = 1.0;
    double allowDeconstruction = 0.0;
    double allowItemInsertion = 0.0;
    double allowSale = 1.0;
    double requiredArchery = -1.0;
    double projectileCount = 1.0;
    double accuracyBonus = 0.0;
    double areaRadius = 0.0;
    double allowNpc = 1.0;
    if (!getValue(1, 0.0, enabled) || enabled != 1.0 ||
        !getValue(2, 0.0, requiredMelee) ||
        !getValue(3, 1.0, staminaMultiplier) ||
        !getValue(4, 0.0, weaponMask) ||
        !getValue(5, 0.0, scaleDamageWithMelee) ||
        !getValue(6, 0.0, grantPrototype) ||
        !getValue(7, -1.0, staminaCost) ||
        !getValue(8, 0.0, cooldownSeconds) ||
        !getValue(9, 1.0, damageMultiplier) ||
        !getValue(10, 1.0, allowDualWield) ||
        !getValue(11, 0.0, allowDeconstruction) ||
        !getValue(12, 0.0, allowItemInsertion) ||
        !getValue(13, -1.0, requiredArchery) ||
        !getValue(14, 1.0, projectileCount) ||
        !getValue(15, 0.0, accuracyBonus) ||
        !getValue(16, 0.0, areaRadius) ||
        !getValue(17, 1.0, allowNpc) ||
        !getValue(18, 1.0, allowSale))
        return false;

    config.requiredMelee = static_cast<float>(requiredMelee);
    config.requiredArchery = static_cast<float>(requiredArchery);
    if (staminaMultiplier > 0.0)
        config.staminaMultiplier = static_cast<float>(staminaMultiplier);
    if (staminaCost >= 0.0)
        config.staminaCost = static_cast<float>(staminaCost);
    config.cooldownSeconds = (std::max)(0.0f, static_cast<float>(cooldownSeconds));
    if (damageMultiplier > 0.0)
        config.damageMultiplier = static_cast<float>(damageMultiplier);
    if (weaponMask > 0.0 && weaponMask <= 4294967295.0)
        config.weaponMask = static_cast<uint32_t>(weaponMask);
    config.scaleDamageWithMelee = scaleDamageWithMelee != 0.0;
    config.grantPrototype = grantPrototype != 0.0;
    config.allowDualWield = allowDualWield != 0.0;
    config.allowDeconstruction = allowDeconstruction != 0.0;
    config.allowItemInsertion = allowItemInsertion != 0.0;
    config.allowSale = allowSale != 0.0;
    config.projectileCount = static_cast<int>((std::max)(1.0, (std::min)(2.0, projectileCount)));
    config.accuracyBonus = (std::max)(0.0f, static_cast<float>(accuracyBonus));
    config.areaRadius = (std::max)(0.0f, static_cast<float>(areaRadius));
    config.allowNpc = allowNpc != 0.0;
    return true;
 };bool result=read();if(validKey&&cacheable){if(skillConfigs.size()>=4096)skillConfigs.clear();skillConfigs[name]={result,config};}return result;
}
bool CModPackage::SkillConfig(CSpell*spell,ModSkills::Config&config){
 if(!spell||!spell->GetPrototype())return false;const char*name=spell->GetPrototype()->szCodeName;
 std::string code=Lower(name);const char*setting=code=="powerhit"||code=="test"?"JMVPowerHit":code=="doubleshot"?"JMVDoubleShot":code=="preciseshot"?"JMVPreciseShot":code=="explosiveshot"?"JMVExplosiveShot":nullptr;
 if(setting&&!CombatSetting(setting))return false;if(!setting&&code!="precisehit"&&code!="cripplinghit")return false;
 return SkillConfig(name,config)||(code=="test"&&SkillConfig("powerhit",config));
}
bool CModPackage::TargetSkill(CSpell*spell)const{
 if(!spell||!spell->GetPrototype())return false;std::string code=Lower((LPCSTR)spell->GetPrototype()->szCodeName);
 return code=="powerhit"||code=="test"||code=="precisehit"||code=="doubleshot"||code=="preciseshot"||code=="cripplinghit"||code=="explosiveshot";
}
float CModPackage::InfoProperty(const char*callback,const char*name,int property){
 CallbackFrame frame(state);BeginCallback(callback);lua_pushstring(state,name);lua_pushinteger(state,property);Call(2,1);
 if(!strcmp(callback,"GetSetItemStats")){
  const double value=Number(state,callback);
  if(value<(std::numeric_limits<int32_t>::lowest)()||value>(std::numeric_limits<int32_t>::max)())
   throw std::runtime_error("Set item property exceeds int32 range");
  return float(static_cast<int32_t>(value)); // Existing truncation, after the range check.
 }
 double n=Number(state,callback);if(fabs(n)>(std::numeric_limits<float>::max)())throw std::runtime_error("Info property overflow");return float(n);
}
bool CModPackage::InfoForbiddenSpell(const char*name,int shop){
 CallbackFrame frame(state);BeginCallback("IsForbiddenItem");lua_pushstring(state,name);lua_pushinteger(state,1);lua_pushinteger(state,shop);Call(3,1);return Number(state,"IsForbiddenItem")!=0;
}
float CModPackage::InfoPerkBonus(const char*callback,int level){
 CallbackFrame frame(state);BeginCallback(callback);lua_pushinteger(state,level);Call(1,1);return float(Number(state,callback));
}
int CModPackage::InfoSetPosition(const char*name,int position,int property){
 CallbackFrame frame(state);BeginCallback("GetSetItemStatsPos");lua_pushstring(state,name);lua_pushinteger(state,position);lua_pushinteger(state,property);Call(3,1);
 // Native LuaPop<int32_t> converts a missing table field to zero.
 return lua_isnil(state,-1)?0:int(Number(state,"GetSetItemStatsPos"));
}
float CModPackage::InfoComplexProperty(int set,int property,int position){
 const char*callback=position<0?"GetComplex":"GetComplexPos";CallbackFrame frame(state);BeginCallback(callback);lua_pushinteger(state,set);
 if(position>=0)lua_pushinteger(state,position);lua_pushinteger(state,property);Call(position<0?2:3,1);
 return float(lua_tonumber(state,-1));
}
std::vector<std::pair<int,int>> CModPackage::InfoSetBonuses(const char*name,int count,int set,bool complex){
 std::vector<std::pair<int,int>> result;
 // Installed FormTextInfo loops 3..JMVUS_MAX(22), count1..equipped, and three columns.
 for(int stat=3;stat<=22;++stat){int sum=0;for(int c=1;c<=count;++c)for(int i=1;i<=3;++i){
  int type=complex?int(InfoComplexProperty(set,4+i,c)):InfoSetPosition(name,c,2+i);
  if(type&&type==stat)sum+=complex?int(InfoComplexProperty(set,7+i,c)):InfoSetPosition(name,c,5+i);
 }if(sum)result.emplace_back(stat,sum);}return result;
}
float CModPackage::InfoDisplayScalar(const char* callback, std::initializer_list<float> args) {
 CallbackFrame frame(state); BeginCallback(callback);
 for (float value : args) lua_pushnumber(state, value);
 Call(int(args.size()), 1); return float(Number(state, callback));
}
float CModPackage::InfoDisplayActor(const char* callback, CUnit& unit, int slot) {
 CallbackFrame frame(state); BeginCallback(callback);
 ModLua::Push(state, unit.GetUserUnitStats(), frame.scope); ModLua::Push(state, &unit, frame.scope);
 if (slot >= 0) lua_pushinteger(state, slot);
 Call(slot >= 0 ? 3 : 2, 1); return float(Number(state, callback));
}
float CModPackage::InfoDisplayWound(const SRPGUnitStats& stats) {
 CallbackFrame frame(state); BeginCallback("GetHitLocDecr");
 SRPGUnitStats displayStats = stats;
 ModLua::Push(state, &displayStats, frame.scope); lua_pushnil(state); lua_pushinteger(state, 1);
 Call(3, 1); return float(Number(state, "GetHitLocDecr"));
}
float CModPackage::InfoMagicBlood(int level){
 CallbackFrame frame(state);BeginCallback("GetRegenBonusFromPerkMagicBlood");lua_pushinteger(state,level);Call(1,1);return float(Number(state,"MagicBlood"));
}
float CModPackage::InfoComplex(CUnit&unit,int property){
 CallbackFrame frame(state);BeginCallback("GetComplexSummStats");ModLua::Push(state,&unit,frame.scope);lua_pushinteger(state,property);Call(2,1);return float(Number(state,"Complex stats"));
}
void CModPackage::SaveSkillCooldowns(CGSVariableSet&vars){
 SyncRuntimeState();const void*w=theWorldServer.GetSpecificManager();int tick=theWorldServer.GetSegmentTimeCounter();std::string zone=(LPCSTR)theWorldServer.szMobFileName;
 skillCooldowns.Save(w,tick,zone,vars);auto*p=theWorldServer.GetPlayer(0);if(p&&&p->party.vars!=&vars)skillCooldowns.Save(w,tick,zone,p->party.vars);
}
void CModPackage::LoadSkillCooldowns(){
 auto*p=theWorldServer.GetPlayer(0);if(p)skillCooldowns.Load(theWorldServer.GetSpecificManager(),theWorldServer.GetSegmentTimeCounter(),std::string((LPCSTR)theWorldServer.szMobFileName),p->party.vars);
}
int CModPackage::SkillCooldown(CUnit&unit,CSpell*spell){return !spell||!spell->GetPrototype()?0:skillCooldowns.Remaining(theWorldServer.GetSpecificManager(),theWorldServer.GetSegmentTimeCounter(),unit.GetID(),(LPCSTR)spell->GetPrototype()->szCodeName);}
void CModPackage::StartSkillCooldown(CUnit&unit,CSpell*spell,const ModSkills::Config&config){if(spell&&spell->GetPrototype())skillCooldowns.Start(theWorldServer.GetSpecificManager(),theWorldServer.GetSegmentTimeCounter(),unit.GetID(),(LPCSTR)spell->GetPrototype()->szCodeName,config.Duration(),config.requiredArchery<0);}
bool CModPackage::SkillReady(CUnit&unit,CSpell*spell,const ModSkills::Config&config,bool server){
 auto*w=unit.GetCurrentQWeapon();if(!w||!config.Weapon(w->GetWeaponType()))return false;
 int left=ModStats::LeftHand(unit);auto*off=left>=0&&left!=int(unit.GetCurrentWeaponSlotIndex())?unit.GetQWeapon(left):nullptr;
 if(!config.allowDualWield&&off&&off->GetWeaponType()!=RPGWST_HANDS1)return false;
 ERPGAutoSkills skill=config.requiredArchery>=0?RPGASKL_ARCHERY:RPGASKL_MELEE;
 auto*s=server?dynamic_cast<CUnitServer*>(unit.GetUnitSpecific()):nullptr;
 if(server&&!s)return false;float value=s?s->GetRPGStats()->GetAutoSkill(skill):unit.GetUserUnitStats()->GetAutoSkill(skill);
 return value>=config.Requirement(float(spell->GetComplexity()))&&SkillCooldown(unit,spell)<=0;
}
void CModPackage::CopyAttack(const SRPGAttackPortion&from,const SRPGAttackPortion&to){auto i=criticalAttacks.find(&from);if(i==criticalAttacks.end())criticalAttacks.erase(&to);else criticalAttacks[&to]=i->second;}
void CModPackage::ScheduleDoubleShot(CUnit&unit,CMapObject*target,const SRPGAttackPortion&ap){
 if(!target)return;if(delayedShots.size()>=64){for(auto&i:delayedShots)ForgetAttack(i.second->ap);delayedShots.clear();}
 auto old=delayedShots.find(unit.GetID());if(old!=delayedShots.end())ForgetAttack(old->second->ap);
 auto shot=std::make_shared<ModDelayedShot>();shot->ap=ap;shot->target=target->GetID();shot->start=theWorldServer.GetSegmentTimeCounter();shot->due=shot->start+12;shot->world=theWorldServer.GetSpecificManager();CopyAttack(ap,shot->ap);delayedShots[unit.GetID()]=shot;
}
void CModPackage::TickDoubleShots(){
 int now=theWorldServer.GetSegmentTimeCounter();for(auto i=delayedShots.begin();i!=delayedShots.end();){
 auto shot=i->second;bool valid=CombatSetting("JMVDoubleShot")&&shot->world==theWorldServer.GetSpecificManager()&&now>=shot->start;
 if(valid&&now<shot->due){++i;continue;}int id=i->first;i=delayedShots.erase(i);
 auto*unit=valid?theWorldServer.GetUnit(id):nullptr;auto*target=valid?theWorldServer.GetMapObject(shot->target):nullptr;auto*server=unit?dynamic_cast<CUnitServer*>(unit->GetUnitSpecific()):nullptr;
 if(server&&!unit->IsDead()&&target&&!target->IsInvalid()&&!target->IsUnreferenced()&&server->IsCurrentWeaponABow())server->UseAttack(target,shot->ap,false,1,2);
 ForgetAttack(shot->ap);
 }
}

float CModPackage::HitLocationModifier(int hl){Stack stack(state);BeginCallback("GetAttackModiferFromHL");lua_pushinteger(state,hl);Call(1,1);return CombatNumber(state,"GetAttackModiferFromHL");}

// Status identity comes from the currently loaded spell database (also on clients).
int CModPackage::CrippledSpellId() const {
 for(int i=0;i<theSpellDatabase.spells.GetNumEntries()&&i<256;++i)
  if(!_stricmp(theSpellDatabase.spells[i]->szCodeName,"crippled"))return i;
 return -1;
}
bool CModPackage::CanCripple(CUnit&unit){
 int id=CrippledSpellId();if(id<0||unit.IsDead()||!theWorldServer.GetSpellManager())return false;
 CSmartPtr<CSpell> spell=theWorldServer.GetSpellManager()->CreateSpell("crippled");if(spell.IsEmpty())return false;
 CSpellEffect effect;spell->GetSpellEffect(effect);
 if(effect.nSpellID!=id||effect.nDuration<=0||effect.fpPower<=0||effect.fpPower>=100)return false;
 const auto& effects=unit.GetSpellEffects();for(int i=0;i<effects.GetSize();++i)if(effects[i].nSpellID==id&&effects[i].nDuration>0)return true;
 return effects.GetSize()<20;
}
bool CModPackage::ApplyCripple(CUnit&unit){
 if(!CanCripple(unit))return false;
 CSmartPtr<CSpell> spell=theWorldServer.GetSpellManager()->CreateSpell("crippled");if(spell.IsEmpty())return false;
 CSpellEffect effect;spell->GetSpellEffect(effect);return unit.RefreshWeaponStatus(effect);
}
float CModPackage::CrippledSpeed(const CUnit&unit) const {
 int id=CrippledSpellId();if(id<0)return 1;
 const auto& effects=unit.GetSpellEffects();for(int i=0;i<effects.GetSize();++i)
  if(effects[i].nSpellID==id&&effects[i].nDuration>0)
   return (std::max)(.05f,(std::min)(1.f,1-theSpellDatabase.spells[id]->fpEffect/100.f));
 return 1;
}
bool CModPackage::PointSkill(CSpell*spell) const{return spell&&spell->GetPrototype()&&!_stricmp(spell->GetPrototype()->szCodeName,"explosiveshot");}
void CModPackage::MarkSkillImpact(const SRPGAttackPortion&ap,int kind){criticalAttacks[&ap].skillImpact=kind;}
int CModPackage::TakeSkillImpact(const SRPGAttackPortion&ap){auto i=criticalAttacks.find(&ap);if(i==criticalAttacks.end())return 0;int kind=i->second.skillImpact;i->second.skillImpact=0;return kind;}

bool CModPackage::SkillItemBlocked(CItem*item,int operation){
 if(!item)return false;auto*spell=item->GetSpell(0);ModSkills::Config config;if(!SkillConfig(spell,config))return false;
 if(operation==0)return !config.allowDeconstruction;if(operation==1)return !config.allowItemInsertion;
 // Sale guard applies only to public skill spell items, not the legacy test alias.
 if(operation!=2||item->GetTypeID()!=ITID_ITEM_SPELL)return false;
 return _stricmp(spell->GetPrototype()->szCodeName,"test")!=0&&!config.allowSale;
}
bool CModPackage::SkillContainerBlocked(CParty*party,int operation){if(party)for(int i=0;i<party->allWagonItems.GetSize();++i)if(SkillItemBlocked(party->allWagonItems[i],operation))return true;return false;}
void CModPackage::GrantSkillPrototypes(CParty&party){
 static const char*codes[]={"powerhit","doubleshot","preciseshot","precisehit","cripplinghit","explosiveshot"};
 static const char*markers[]={"prototype.melee_power_strike.granted.v1","prototype.ranged_double_shot.granted.v1","prototype.ranged_precise_shot.granted.v1","prototype.melee_precise_hit.granted.v1","prototype.melee_crippling_hit.granted.v1","prototype.ranged_explosive_shot.granted.v1"};
 for(int k=0;k<6;++k){int id=theSpellDatabase.spells.GetEntryIndex(codes[k]);if(id<0)continue;CSpell spell;spell.SetSubtypeID(DWORD(id)<<16);spell.Setup(theSpellDatabase.spells[id]);ModSkills::Config config;
  if(!SkillConfig(&spell,config)||!config.grantPrototype)continue;
  auto matches=[&](CItem*item){auto*s=item&&item->GetTypeID()==ITID_ITEM_SPELL?item->GetSpell(0):nullptr;return s&&s->GetPrototype()&&!_stricmp(s->GetPrototype()->szCodeName,codes[k]);};
  bool found=false;for(int p=0;p<party.people.GetSize();++p)for(int s=0;s<party.people[p].spells.GetSize();++s)found|=matches(party.people[p].spells[s]);
  for(int i=0;i<party.allWagonItems.GetSize();++i)found|=matches(party.allWagonItems[i]);
  if(found){party.vars.SetVar(markers[k],1);continue;}if(!party.GetItemManager())continue;
  CString definition;definition.Format("spell container [%s]",codes[k]);CSmartPtr<CItem> item=party.GetItemManager()->CreateItem(definition);
  if(!item.IsEmpty()&&item->GetTypeID()==ITID_ITEM_SPELL&&party.AddItemToWagon(item,false,false))party.vars.SetVar(markers[k],1);
 }
}
bool CModPackage::CompanionSkillAI(CUnit&unit) const{if(!unit.IsHero()||!unit.GetOwner())return false;CString key;key.Format("jmv.companion.ai.v1.%d",unit.GetID());return unit.GetOwner()->party.vars.GetVar(key)>=.5f;}

void CModPackage::BeginInfoStats(){infoStats->Begin(initialization->epoch,theWorld.GetMapObjectManager(),theWorldServer.GetSpecificManager(),(LPCSTR)theWorldServer.szMobFileName,theWorldServer.GetSegmentTimeCounter());}
void CModPackage::EndInfoStats(){infoStats->End();}
void CModPackage::InvalidateInfoStatsCache(){infoStats->Invalidate();}
float CModPackage::PanelDamage(CUnit&unit,float damage,int slot,bool left){
 CallbackFrame frame(state);auto*w=slot>=0?frame.scope->Weapon(unit,slot):nullptr;
 std::string args;
 if(infoStats->depth==1){
  ModInfoStats::KeyValue(args,damage);const float roll=-1;ModInfoStats::KeyValue(args,roll);
  const auto weaponType=w?w->GetWeaponType():0;ModInfoStats::KeyValue(args,weaponType);ModInfoStats::KeyValue(args,slot);
  const bool owner=IsIDofPersonage(unit.GetID())&&unit.GetTypeID()!=MOTID_ANIMAL&&unit.GetTypeID()!=MOTID_MONSTER;
  ModInfoStats::KeyValue(args,owner);const bool fix=CombatSetting("JMVFixCriticalHitSoundTrigger")!=0;ModInfoStats::KeyValue(args,fix);
  ModInfoStats::KeyString(args,unit.GetPrototype()->szName);
 }
 return ModInfoStats::Call(*infoStats,state,ModInfoStats::Damage,unit,*frame.scope,args,
  std::isfinite(damage)&&CombatSetting("JMVFixCriticalHitSoundTrigger")!=0,[&](){
 BeginCallback("GetModifiedDamageOnClientJmv");ModLua::Push(state,unit.GetUserUnitStats(),frame.scope);ModLua::Push(state,&unit,frame.scope);
 lua_pushnumber(state,damage);lua_pushnumber(state,-1);lua_pushstring(state,unit.GetPrototype()->szName);
 lua_pushinteger(state,w?w->GetWeaponType():0);lua_pushinteger(state,slot);
 lua_pushboolean(state,IsIDofPersonage(unit.GetID())&&unit.GetTypeID()!=MOTID_ANIMAL&&unit.GetTypeID()!=MOTID_MONSTER);
 lua_pushboolean(state,CombatSetting("JMVFixCriticalHitSoundTrigger")!=0);Call(9,1);return CombatNumber(state,"Panel damage");
 });
}
float CModPackage::PanelCritical(CUnit&unit,int slot,bool left,bool power){
 CallbackFrame frame(state);auto*w=slot>=0?frame.scope->Weapon(unit,slot):nullptr;
 const float modifier=unit.GetUserUnitStats()->GetPerkModifier(power?4:12);std::string args;
 if(infoStats->depth==1){
  ModInfoStats::KeyValue(args,modifier);const int hand=left?slot:-1;ModInfoStats::KeyValue(args,hand);ModInfoStats::KeyValue(args,left);
  ModInfoStats::KeyValue(args,slot);const auto weaponType=w?w->GetWeaponType():0;ModInfoStats::KeyValue(args,weaponType);
  ModInfoStats::KeyString(args,unit.GetPrototype()->szName);
 }
 return ModInfoStats::Call(*infoStats,state,power?ModInfoStats::CritDamage:ModInfoStats::CritChance,unit,*frame.scope,args,std::isfinite(modifier),[&](){
 BeginCallback(power?"GetCritDamageOnClientJmv":"GetCritOnClientJmv");ModLua::Push(state,&unit,frame.scope);
 lua_pushnumber(state,modifier);
 lua_pushinteger(state,left?slot:-1);lua_pushboolean(state,left);lua_pushstring(state,unit.GetPrototype()->szName);
 lua_pushinteger(state,w?w->GetWeaponType():0);lua_pushinteger(state,slot);Call(7,1);return CombatNumber(state,"Panel critical");
 });
}
void CModPackage::IdleShieldValues(CUnit& unit,float& power,float& block,float& parry){
 CallbackFrame frame(state);
 auto value=[&](const char* fn,bool hand){BeginCallback(fn);ModLua::Push(state,unit.GetUserUnitStats(),frame.scope);ModLua::Push(state,&unit,frame.scope);
  if(hand)lua_pushinteger(state,frame.scope->LeftHand(unit));Call(hand?3:2,1);float result=CombatNumber(state,fn);lua_pop(state,1);return result;};
 power=value("GetShieldBlock",false);block=value("GetShieldBlockChance",false);parry=value("GetWeaponParryChance",true);
}
void CModPackage::PanelDefense(CUnit&unit,int weight,int capacity,float&block,float&parry){
 CallbackFrame frame(state);
 auto finish=[&](const char*fn,int count){Call(count,1);float v=CombatNumber(state,fn);lua_pop(state,1);return v;};
 auto overload=[&](const char*fn){BeginCallback(fn);lua_pushinteger(state,weight);lua_pushinteger(state,capacity);return finish(fn,2);};
 auto value=[&](const char*fn,int slot){BeginCallback(fn);ModLua::Push(state,unit.GetUserUnitStats(),frame.scope);ModLua::Push(state,&unit,frame.scope);if(slot>=0)lua_pushinteger(state,slot);return finish(fn,slot>=0?3:2);};
 block=value("GetShieldBlockChance",-1)*overload("GetOverloadBlockOnServer");
 const int slot=int(unit.GetCurrentWeaponSlotIndex()),left=frame.scope->LeftHand(unit);
 parry=0;if(slot<0||slot>=4||!frame.scope->Weapon(unit,slot))return;
 const float factor=overload("GetOverloadParryOnServer");
 float first=value("GetWeaponParryChance",slot)*.01f*factor;
 if(left<0){parry=first*100;return;}
 float second=value("GetWeaponParryChance",left)*.01f*factor;
 parry=(first<.0001f?second:second<.0001f?first:first+second-2*first*second)*100;
}

// Returns before Host/WinMain startup. Exercises the actual settings stage and
// actual registry reader only: no Lua main, engine bindings, world/device/audio.
bool InspectModConfigurationCommandLine(int& result){
 int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return false;
 struct Free{LPWSTR* p;~Free(){LocalFree(p);}} free{args};
 if(count<2||wcscmp(args[1],L"--check-config-g3"))return false;
 result=1;Json report={{"success",false},{"gameplayActivated",false},{"worldCreated",false},{"deviceCreated",false},{"audioStarted",false},{"luaMainExecuted",false},{"cases",Json::array()}};
 fs::path output=count>=5?args[4]:L"config-g3.json";
 try{
  if(count!=5)throw std::runtime_error("Expected mod directory, configuration fixture spec and report path");
  std::ifstream specification(fs::path(args[3]),std::ios::binary);Json spec;specification>>spec;
  CModPackage package;package.Load(fs::path(args[2]).string());
  const auto ai=package.configs.find("ai.reg");if(ai==package.configs.end())throw std::runtime_error("Installed package has no AI registry");
  report["aiRegistry"]=ai->second;size_t assertions=0;
  auto require=[&](bool good,const char* reason){++assertions;if(!good)throw std::runtime_error(reason);};
  auto values=[&](CFileRegistry& reg){
   const bool complex=package.ConfigureAddonSettings(reg);Json out=Json::object();
   size_t n=0;const auto* rules=ModConfiguration::CombatRules(n);
   for(size_t i=0;i<n;++i)out[rules[i].key]=package.CombatSetting(rules[i].key);
   out["JMVExpMult"]=package.CombatSetting("JMVExpMult");
   out["JMVGoCamp"]=int(ModConfiguration::Bool(reg,"Addon","JMVGoCamp",true));
   out["JMVRegisterLoot"]=int(package.registeredLoot);out["JMVNpcStatRefresh"]=int(package.npcStatRefresh);
   out["JMVNpcDatabaseMigration"]=int(package.npcDatabaseMigration);out["JMVExtendActiveObjectsArea"]=int(package.extendActiveObjectsArea);
   out["JMVBriefingWeapons"]=int(package.briefingWeapons);out["JMVWorldBackground"]=int(package.worldBackground);
   out["JMVPartyVisualOptions"]=int(package.partyVisualOptions);out["JMVNpcArmorCalculation"]=int(package.npcArmorCalculation);
   out["JMVNpcTrickster"]=int(package.npcTrickster);out["JMVNpcArmorMaterialEffects"]=int(package.npcMaterialEffects);
   out["JMVSetItemsComplex"]=int(complex);out["AddonExp/Limit"]=package.experienceLimit;out["MagicRandom"]=package.magicRandomLimit;
   const auto& a=ModAudioSettings();
   out["JMVSpeechMusicDucking"]=int(a.JMVSpeechMusicDucking);out["JMVFixStepSoundVariantGenerator"]=int(a.JMVFixStepSoundVariantGenerator);
   out["JMVSuppressEmptyStartSfx"]=int(a.JMVSuppressEmptyStartSfx);
   out["JMVMiniaudioMissingAudioCatalogDiagnostic"]=int(a.JMVMiniaudioMissingAudioCatalogDiagnostic);
   out["JMVSfxReverbOutdoorLevel"]=a.JMVSfxReverbOutdoorLevel;out["JMVSfxReverbOutdoorReflection"]=a.JMVSfxReverbOutdoorReflection;out["JMVSfxReverbOutdoorDecay"]=a.JMVSfxReverbOutdoorDecay;
   out["JMVSfxReverbCaveLevel"]=a.JMVSfxReverbCaveLevel;out["JMVSfxReverbCaveReflection"]=a.JMVSfxReverbCaveReflection;out["JMVSfxReverbCaveDecay"]=a.JMVSfxReverbCaveDecay;
   out["JMVFalloutCameraYaw"]=ModConfiguration::Float(reg,"Addon","JMVFalloutCameraYaw",45.0f);
   out["JMVFalloutCameraPitch"]=ModConfiguration::Float(reg,"Addon","JMVFalloutCameraPitch",35.264f);
   out["JMVFalloutCameraTelephotoMultiplier"]=ModConfiguration::Float(reg,"Addon","JMVFalloutCameraTelephotoMultiplier",2.0f);
   require(!package.scriptInitialized,"Settings fixture executed Lua main");return out;
  };
  Json installed;
  for(const auto& item:spec.at("cases")){
   CFileRegistry reg;require(reg.Open(item.at("path").get<std::string>().c_str())!=FALSE,"Cannot open configuration fixture");
   const Json observed=values(reg);const Json& expected=item.at("expected");require(observed.size()==expected.size(),"Configuration fixture scope differs");
   for(auto it=expected.begin();it!=expected.end();++it){
    require(observed.contains(it.key()),"Mapped setting not observed");
    const double want=it.value().get<double>(),got=observed.at(it.key()).get<double>();
    if(!std::isfinite(got)||std::abs(got-want)>1e-6*(std::max)(1.0,std::abs(want)))throw std::runtime_error(item.at("name").get<std::string>()+": "+it.key()+" differs");++assertions;
   }
   if(item.at("name")=="actual installed AI registry"){
    require(fs::equivalent(item.at("path").get<std::string>(),ai->second),"Fixture does not use installed config owner");
    require(package.CombatSetting("JMVGoCamp")==observed.at("JMVGoCamp"),"Early Load GoCamp mapping differs");installed=observed;
   }
   report["cases"].push_back({{"name",item.at("name")},{"settings",observed.size()},{"observed",observed}});
  }
  CFileRegistry active;require(active.Open(ai->second.c_str())!=FALSE,"Cannot restore actual settings fixture");require(values(active)==installed,"Settings reload retained stale values");
  require(package.mainScript=="main.lua","Current main script selection changed");
  report["assertions"]=assertions;report["success"]=true;result=0;
 }catch(const std::exception& e){report["error"]=e.what();}
 std::ofstream stream(output,std::ios::binary);stream<<report.dump(2);return true;
}


struct CModPackage::SaveState {
 std::map<int,std::shared_ptr<ModDelayedShot>> shots;
 std::map<const SRPGAttackPortion *,CriticalRecord> attacks;
 ModCustomLower::CriticalCache custom;
 bool helmets=false,upper=false;
 // Fresh runtime owners are allocated before any reversible preparation begins.
 std::unique_ptr<ModNpcRefreshQueue> refresh=std::make_unique<ModNpcRefreshQueue>();
 std::unique_ptr<ModInitialization::Ledger> initialized=std::make_unique<ModInitialization::Ledger>();
 std::unique_ptr<ModInfoStats::State> info=std::make_unique<ModInfoStats::State>();
 ModSkills::Cooldowns cooldowns;
 std::map<int,ArmorModifiers> armor,armorView;
 const void *world=nullptr,*viewWorld=nullptr;
 std::string zone,viewZone;int segment=-1,viewSegment=-1;
 bool migrate=false;
 SRegisteredLootState loot;
 decltype(G_npcCombatConsumableStates) consumables;
 std::string consumableZone,consumableMob;
 int visitToken=0;bool freshVisit=false;CGSVariableSet *vars=nullptr;
};
SRPGAttackPortion *CModPackage::SavedAttack(CGameBase *object,unsigned kind){
 if(kind==0){auto *p=dynamic_cast<CUnitServer *>(object);return p?&p->level1AP:nullptr;}
 if(kind==1){auto *p=dynamic_cast<CEffectArrow *>(object);return p?&p->weaponAttack:nullptr;}
 if(kind==2){auto *p=dynamic_cast<CEffectArrowFigure *>(object);return p?&p->attackPortion:nullptr;}
 return nullptr;
}
void CModPackage::StoreSaveState(SaveSchema::Writer &w) const {
 using namespace SaveSchema;using namespace Fields;
 w.Bool(hidePartyHelmets);w.Bool(hidePartyUpperArmor);
 auto record=[&](const CriticalRecord &v){w.Bool(v.critical);w.Bool(v.left);w.Int32(v.skillImpact);w.GameReference(v.weapon.get());};
 std::set<const SRPGAttackPortion *> saved;
 std::vector<std::pair<int,std::shared_ptr<ModDelayedShot>>> pending;
 for(const auto &x:delayedShots){
  if(theWorldServer.GetUnit(x.first)&&theWorldServer.GetMapObject(x.second->target))pending.push_back(x);
  else if(criticalAttacks.count(&x.second->ap))saved.insert(&x.second->ap); // cancelled work
 }
 Count(w,pending.size());for(const auto &x:pending){const auto &s=*x.second;
  w.GameReference(theWorldServer.GetUnit(x.first));w.GameReference(theWorldServer.GetMapObject(s.target));
  w.Int32(s.start);w.Int32(s.due);w.Value(s.ap);
  auto it=criticalAttacks.find(&s.ap);w.Bool(it!=criticalAttacks.end());if(it!=criticalAttacks.end()){record(it->second);saved.insert(&s.ap);}
 }
 struct Entry {CGameBase *owner;unsigned kind;const CriticalRecord *value;};std::vector<Entry> entries;
 auto inspect=[&](CGameBase *o){for(unsigned k=0;k<3;++k)if(auto *ap=SavedAttack(o,k)){
  auto it=criticalAttacks.find(ap);if(it!=criticalAttacks.end()){entries.push_back({o,k,&it->second});saved.insert(ap);}}};
 theWorldServer.GetSpecificManager()->ForEach(inspect);theWorldServer.GetEffectManager()->ForEach(inspect);
 if(saved.size()!=criticalAttacks.size())throw Error(ErrorCode::Invalid,"save is inside a transient combat callback");
 Count(w,entries.size());for(const auto &x:entries){w.GameReference(x.owner);w.UInt32(x.kind);record(*x.value);}
 Count(w,customCriticals.SavedRecords().size());for(const auto &x:customCriticals.SavedRecords()){
  w.Float32(x.second.damage);w.Bool(x.second.critical);w.Bool(x.second.staff);
 }
}
std::shared_ptr<CModPackage::SaveState> CModPackage::LoadSaveState(SaveSchema::Reader &r){
 using namespace SaveSchema;
 auto state=std::make_shared<SaveState>();state->helmets=r.Bool();state->upper=r.Bool();
 auto record=[&](){CriticalRecord v{};v.critical=r.Bool();v.left=r.Bool();v.skillImpact=r.Int32();auto *weapon=r.GameReferenceAs<CItemWeapon>();
  if(weapon){weapon->AddRef();v.weapon={weapon,[](CItemWeapon *p){p->Release();}};}return v;};
 auto n=r.Count();if(n>64)throw Error(ErrorCode::Invalid,"too many delayed shots");
 for(UINT i=0;i<n;++i){auto *who=r.GameReferenceAs<CUnit>();auto *target=r.GameReferenceAs<CMapObject>();
  auto shot=std::make_shared<ModDelayedShot>();shot->target=target?target->GetID():-1;shot->start=r.Int32();shot->due=r.Int32();r.Value(shot->ap);shot->world=nullptr;
  bool critical=r.Bool();if(critical)state->attacks.emplace(&shot->ap,record());
  if(!who || !state->shots.emplace(who->GetID(),shot).second)throw Error(ErrorCode::Invalid,"invalid delayed shot source");
 }
 n=r.Count();for(UINT i=0;i<n;++i){auto *owner=r.GameReference();auto kind=r.UInt32();auto *ap=SavedAttack(owner,kind);
  if(!ap || !state->attacks.emplace(ap,record()).second)throw Error(ErrorCode::Invalid,"invalid critical attack owner");}
 n=r.Count();for(UINT i=0;i<n;++i){float damage=r.Float32();bool critical=r.Bool(),staff=r.Bool();state->custom.Put(damage,critical,staff);}
 return state;
}
void CModPackage::SwapSaveRuntime(SaveState &s) noexcept
{
 delayedShots.swap(s.shots);criticalAttacks.swap(s.attacks);std::swap(customCriticals,s.custom);
 std::swap(hidePartyHelmets,s.helmets);std::swap(hidePartyUpperArmor,s.upper);
 refreshQueue.swap(s.refresh);initialization.swap(s.initialized);infoStats.swap(s.info);
 std::swap(skillCooldowns,s.cooldowns);npcArmorCache.swap(s.armor);npcArmorViewCache.swap(s.armorView);
 std::swap(cacheWorld,s.world);cacheZone.swap(s.zone);std::swap(cacheSegment,s.segment);
 std::swap(npcArmorViewWorld,s.viewWorld);npcArmorViewZone.swap(s.viewZone);std::swap(npcArmorViewSegment,s.viewSegment);
 std::swap(npcEquipmentMigrationPending,s.migrate);
 std::swap(G_registeredLoot,s.loot);G_npcCombatConsumableStates.swap(s.consumables);
 G_npcCombatConsumableZone.swap(s.consumableZone);G_npcCombatConsumableRawMobName.swap(s.consumableMob);
 std::swap(G_npcCombatConsumableVisitToken,s.visitToken);std::swap(G_npcCombatConsumableFreshVisit,s.freshVisit);
 std::swap(G_npcCombatConsumableVars,s.vars);
}
void CModPackage::PrepareSaveState(const std::shared_ptr<SaveState> &saved,WorldLoadOrigin origin,const std::function<void()> &prepareClient)
{
 if(!saved)throw SaveSchema::Error(SaveSchema::ErrorCode::Invalid,"missing saved mod state");
 // SavedWorldScope routes all server lookups to the staged owner. The live
 // package's transient state is exchanged back on success and every exception.
 saved->world=theWorldServer.GetSpecificManager();saved->zone=(LPCSTR)theWorldServer.szMobFileName;
 saved->segment=theWorldServer.GetSegmentTimeCounter();
 SwapSaveRuntime(*saved);
 try {
  LoadSkillCooldowns();JMV_RegisteredLootOnSuccessfulSaveLoad();JMV_NpcConsumablesLoaded(origin);
  npcEquipmentMigrationPending=npcDatabaseMigration&&origin==WorldLoadOrigin::SavedGame;
  InitializeLoadedHeroes();
  // Apply only server visuals here. The accepted client's new-zone stream
  // recreates its figures; touching the displayed old world would break rollback.
  if(partyVisualOptions)if(auto *player=theWorldServer.GetPlayer(0))
   for(INT_PTR i=0;i<player->party.people.GetSize();++i)
    if(auto *unit=theWorldServer.GetUnit(player->party.people[i].nID)){
     unit->ShowHelmet(!hidePartyHelmets);unit->SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);
    }
  for(auto &x:delayedShots)x.second->world=theWorldServer.GetSpecificManager();
  prepareClient();
 } catch(...) {SwapSaveRuntime(*saved);throw;}
 SwapSaveRuntime(*saved);
}
void CModPackage::AdoptSaveState(const std::shared_ptr<SaveState> &saved) noexcept
{
 if(!saved)return;
 SwapSaveRuntime(*saved);
 // Pure invalidation: no Lua, packet construction, resource access or allocation.
 ModGroupSearch::Reset();ModDeathFearReset();ModTactical::Reset();
}
