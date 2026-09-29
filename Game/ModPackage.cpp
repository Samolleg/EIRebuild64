#include "StdAfx.h"
#include "ScenarioTracker.h"
#include "ModPackage.h"
#include "SocialStealthLua.h"
#include "EngineDiagnostics.h"
#include "ModCombat.h"
#include "ModAudioConfig.h"
#include "BinkVideo.h"
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
#include "ModRuntime.h"
#include "RpgSystem.h"
#include "PerkDatabase.h"
#include <limits>
#include <cmath>
#include "FileRegistry.h"
#include "CMappedResource.h"
#include "mapFormat.h"
#include "adFormat.h"
#include "luajit-x86/lua.hpp"
#include <filesystem>
#include <fstream>
void ModDeathFearReset();

#include <stdexcept>
#include <set>
#include <shellapi.h>
#include "ModSpellTextures.inl"
#include "ModTacticalSpells.h"
#include "ModGroupSearch/Native.h"
#include "ModNpcMigration.inl"
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
bool ReadPartyVisualOption(const wchar_t* name){
 HKEY key=nullptr;
 if(RegOpenKeyExW(HKEY_CURRENT_USER,partyVisualRegistry,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return false;
 DWORD type=0,value=0,size=sizeof(value);
 LSTATUS status=RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<LPBYTE>(&value),&size);
 RegCloseKey(key);
 return status==ERROR_SUCCESS&&type==REG_DWORD&&size==sizeof(value)&&value!=0;
}
bool WritePartyVisualOption(const wchar_t* name,bool enabled){
 HKEY key=nullptr;
 if(RegCreateKeyExW(HKEY_CURRENT_USER,partyVisualRegistry,0,nullptr,REG_OPTION_NON_VOLATILE,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
 DWORD value=enabled?1u:0u;
 LSTATUS status=RegSetValueExW(key,name,0,REG_DWORD,reinterpret_cast<const BYTE*>(&value),sizeof(value));
 RegCloseKey(key);
 return status==ERROR_SUCCESS;
}
struct Stack {lua_State *s;int n;Stack(lua_State *p):s(p),n(lua_gettop(p)){}~Stack(){lua_settop(s,n);}};
Json Value(lua_State*s,int i){switch(lua_type(s,i)){case LUA_TNIL:return nullptr;case LUA_TBOOLEAN:return lua_toboolean(s,i)!=0;case LUA_TNUMBER:return lua_tonumber(s,i);case LUA_TSTRING:{size_t n;const char*p=lua_tolstring(s,i,&n);return std::string(p,n);}default:throw std::runtime_error("Non-scalar Lua lookup result");}}
void Push(lua_State*s,const Json&v){if(v.is_null())lua_pushnil(s);else if(v.is_boolean())lua_pushboolean(s,v.get<bool>());else if(v.is_number())lua_pushnumber(s,v.get<double>());else if(v.is_string()){auto a=v.get<std::string>();lua_pushlstring(s,a.data(),a.size());}else throw std::runtime_error("Non-scalar lookup argument");}
}
CModPackage::CModPackage():initialization(std::make_unique<ModInitialization::Ledger>()),refreshQueue(std::make_unique<ModNpcRefreshQueue>()){}
CModPackage::~CModPackage(){if(state)lua_close(state);}
void CModPackage::Call(int args,int results){if(lua_pcall(state,args,results,0)){const char*p=lua_tostring(state,-1);throw std::runtime_error(p?p:"Lua package call failed");}}
void CModPackage::Require(const char *name){lua_getglobal(state,"require");lua_pushstring(state,name);Call(1,1);}
void CModPackage::Load(const std::string &directory){
 if(state)throw std::runtime_error("Package already loaded; create a fresh package for reload");
 root=fs::absolute(directory).lexically_normal().string();
 SpellTextures::Load(root);
 CFileRegistry config;if(!config.Open(Join(root,"config.reg").c_str()))throw std::runtime_error("Cannot open mod config.reg");
 saveInMod=config.GetInt("MOD","GameSaves",1)!=0;
 singlePlayerAllowed=config.GetInt("MOD","Single",1)!=0;
 multiplayerAllowed=config.GetInt("MOD","Multi",1)!=0;
 CStringArray entries;config.GetEntries("RES",entries);
 for(int i=0;i<entries.GetSize();i++){
   std::string key=Lower((LPCSTR)entries[i]);CStringArray paths;config.GetStringArray("RES",entries[i],paths);
   for(int j=0;j<paths.GetSize();j++){std::string path=(LPCSTR)paths[j];if(path.empty())throw std::runtime_error("Empty RES value");if(path[0]=='-')ignoreBase[key]=true;else archives[key].push_back(Join(root,path));}
 }
 entries.RemoveAll();config.GetEntries("CONFIG",entries);
 for(int i=0;i<entries.GetSize();i++){CString path=config.GetString("CONFIG",entries[i],"");if(!path.IsEmpty())configs[Lower((LPCSTR)entries[i])]=Join(root,(LPCSTR)path);}
 for(const auto &kv:configs)if(!fs::is_regular_file(kv.second))throw std::runtime_error("Missing config: "+kv.second);
 fs::path cameraRoot=fs::path(root)/"camera";
 if(fs::exists(cameraRoot))for(const auto &entry:fs::recursive_directory_iterator(cameraRoot))if(entry.is_regular_file())cameras[Lower(fs::relative(entry.path(),cameraRoot).string())]=entry.path().string();
 entries.RemoveAll();config.GetEntries("CAMERA",entries);
 for(int i=0;i<entries.GetSize();i++){CString path=config.GetString("CAMERA",entries[i],"");if(!path.IsEmpty())cameras[Lower((LPCSTR)entries[i])]=Join(root,(LPCSTR)path);}
 for(const auto &kv:cameras)if(!fs::is_regular_file(kv.second))throw std::runtime_error("Missing camera: "+kv.second);
 mainScript="main.lua";combatSettings["JMVGoCamp"]=1;auto ai=configs.find("ai.reg");if(ai!=configs.end()){CFileRegistry r;if(!r.Open(ai->second.c_str()))throw std::runtime_error("Cannot read addon AI registry");combatSettings["JMVGoCamp"]=r.GetInt("Addon","JMVGoCamp",1);mainScript=(LPCSTR)r.GetString("Addon","LuaMainScriptPath","main.lua");}
 if(!fs::is_regular_file(Join(root,mainScript)))throw std::runtime_error("Missing main script");
 fs::path mapRoot=fs::path(root)/"Maps";
 if(fs::exists(mapRoot))for(const auto &entry:fs::recursive_directory_iterator(mapRoot))if(entry.is_regular_file())maps[Lower(fs::relative(entry.path(),mapRoot).string())]=entry.path().string();
 // Load data first. InitializeScript separately registers typed engine bindings.
 state=luaL_newstate();if(!state)throw std::bad_alloc();luaL_openlibs(state);
 Stack stack(state);lua_getglobal(state,"package");std::string path=root+"/?.lua;"+root+"/lua/?.lua";
 for(char &c:path)if(c=='\\')c='/';lua_pushlstring(state,path.data(),path.size());lua_setfield(state,-2,"path");lua_pushliteral(state,"");lua_setfield(state,-2,"cpath");lua_pop(state,1);
 // Match main.lua require order: table callbacks must override default callbacks.
 Require("default");lua_pop(state,1);
 Require("table_indexes");indexRef=luaL_ref(state,LUA_REGISTRYINDEX);
 Require("static_table_values");lua_getfield(state,-1,"new");lua_rawgeti(state,LUA_REGISTRYINDEX,indexRef);Call(1,1);lua_getfield(state,-1,"last");lookupRef=luaL_ref(state,LUA_REGISTRYINDEX);
}
void CModPackage::InitializeScript(){
 if(scriptInitialized)return;
 if(!state)throw std::runtime_error("Load package before initializing Lua script");
 Stack stack(state);
 ModLua::Register(state);
 {bool enabled=false;auto ai=configs.find("ai.reg");if(ai!=configs.end()){CFileRegistry settings;if(!settings.Open(ai->second.c_str()))throw std::runtime_error("Cannot read addon settings");enabled=(settings.GetInt("Addon","JMVSetItemsComplex",0)!=0);ConfigureModAudio(settings,root);for(const char*key:{"useLS_DrawStats","useLS_ShowEnemyConsider","useLS_NewFont","JMVGraphics","JMVGraphics2","AllowMagicNegative","JMVParticles","NewRunes","JMVExpMult","ArchersDefence","DeagrPlayerOnLoose","DisableRunaway","DistanceModifier","WeightBonus","DisableCrippledHL","UsePrototypeActionsForMobAttack","LoadSpellAnimationFromSlotsField","JMVPowerHit","JMVDoubleShot","JMVPreciseShot","JMVExplosiveShot","JMVAttacks","JMVDaggerCritical","JMV136","JMVPerks","JMVWeaponBlocking","JMVAnatomyLimbDamage","JMVFixCriticalHitSoundTrigger","JMVRangedBackstab","JMVBackstabDefenderEquipment","JMVHealAfterHit","usePotionAcks","AttackAnimSpeed","UsePrototypeActionsForMobCast","RemoveSpellConstructReqs","improveInfoWindows","JMVItemInfoLineHeight","JMVRegenerationTooltip","JMVArmorMaterialStats","JMVShowCharacterModelInStats","JMVShopUpdate","JMVLoadVideos","JMVGenocidePlayerKillTracking","ShowAllQuests","FastTeleport","UnlockExperimentalSpells","JMVSteals"})combatSettings[key]=settings.GetInt("Addon",key,strcmp(key,"JMVExpMult")==0||strcmp(key,"JMVPowerHit")==0||strcmp(key,"JMVDoubleShot")==0||strcmp(key,"JMVPreciseShot")==0||strcmp(key,"JMVExplosiveShot")==0||strcmp(key,"JMVDaggerCritical")==0||strcmp(key,"JMVFixCriticalHitSoundTrigger")==0||strcmp(key,"JMVWeaponBlocking")==0||strcmp(key,"JMVAnatomyLimbDamage")==0||strcmp(key,"JMVRangedBackstab")==0||strcmp(key,"JMVBackstabDefenderEquipment")==0||strcmp(key,"usePotionAcks")==0||strcmp(key,"JMVShowCharacterModelInStats")==0||strcmp(key,"JMVItemInfoLineHeight")==0||strcmp(key,"JMVRegenerationTooltip")==0||strcmp(key,"JMVArmorMaterialStats")==0||strcmp(key,"JMVLoadVideos")==0?1:0);registeredLoot=settings.GetInt("Addon","JMVRegisterLoot",0)!=0;npcStatRefresh=settings.GetInt("Addon","JMVNpcStatRefresh",1)!=0;npcDatabaseMigration=settings.GetInt("Addon","JMVNpcDatabaseMigration",0)!=0;extendActiveObjectsArea=settings.GetInt("Addon","JMVExtendActiveObjectsArea",1)!=0;briefingWeapons=settings.GetInt("Addon","JMVBriefingWeapons",1)!=0;partyVisualOptions=settings.GetInt("Addon","JMVPartyVisualOptions",1)!=0;if(partyVisualOptions){hidePartyHelmets=ReadPartyVisualOption(hideHelmetsValue);hidePartyUpperArmor=ReadPartyVisualOption(hideUpperArmorValue);}npcArmorCalculation=settings.GetInt("Addon","JMVNpcArmorCalculation",1)!=0;npcTrickster=settings.GetInt("Addon","JMVNpcTrickster",1)!=0;npcMaterialEffects=settings.GetInt("Addon","JMVNpcArmorMaterialEffects",1)!=0;experienceLimit=settings.GetFloat("AddonExp","Limit",-1.0f);magicRandomLimit=settings.GetFloat("Addon","MagicRandom",-1.0f);}lua_pushboolean(state,enabled);lua_setglobal(state,"JMVSetItemsComplexEnabled");}
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
 lua_State *state;int top;std::shared_ptr<ModLua::Scope> scope=std::make_shared<ModLua::Scope>();
 explicit CallbackFrame(lua_State*s):state(s),top(0){if(!s)throw std::runtime_error("Mod package not loaded");top=lua_gettop(s);}
 ~CallbackFrame(){scope->active=false;lua_settop(state,top);}
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
 if(!scriptInitialized)throw std::runtime_error("Mod script has not been initialized");
 lua_getglobal(state,name);if(!lua_isfunction(state,-1))throw std::runtime_error(std::string("Missing mod callback: ")+name);
}
float CModPackage::StatsNumber(const char*name,const SRPGUnitStats&stats){
 CallbackFrame frame(state);SRPGUnitStats snapshot=stats;
 BeginCallback(name);ModLua::Push(state,&snapshot,frame.scope);Call(1,1);
 double value=Number(state,name);if(fabs(value)>(std::numeric_limits<float>::max)())throw std::runtime_error(std::string(name)+": float overflow");return static_cast<float>(value);
}
bool CModPackage::Regenerate(SRPGCommonStats&common,SRPGUnitStats&stats,CUnit*unit,bool hpOnly){
 if(!unit||!unit->GetUnitSpecific())throw std::runtime_error("Mod regeneration requires a native unit-specific component");
 if(common.ssHP.fpCurrent<=0)return false;
 // The same serialized counter and unsigned increment as RegenerationSchedule.h.
 unsigned next=static_cast<unsigned>(common.nRegenerateCounter)+1u;
 if(next>0&&next<15){common.nRegenerateCounter=static_cast<int>(next);JMV_NpcConsumablesUpdate(&common,unit);return false;}
 SyncRuntimeState();const unsigned epoch=initialization->epoch;bool ready=initialization->Ready(*unit,common,stats);
 ModInitialization::Inputs inputs(common,stats,*unit);
 CallbackFrame frame(state);SRPGCommonStats stagedCommon=common;SRPGUnitStats staged=stats;
 stagedCommon.nRegenerateCounter=0;frame.scope->unit=unit;frame.scope->stagedStats=&staged;
 bool safe=false;
 if(theWorldServer.GetPlayerManager())if(auto*player=theWorldServer.GetPlayer(0)){
  if(player==unit->GetOwner()){
   auto*specific=player->GetPlayerSpecific();
   if(auto*server=dynamic_cast<CPlayerServer*>(specific))safe=server->dangerState!=WDS_COMBAT;
  }else if(auto*server=dynamic_cast<CUnitServer*>(unit->GetUnitSpecific()))safe=!server->IsAggressive();
 }
 BeginCallback("RegenerateHpAndMpJmvDue");ModLua::Push(state,&stagedCommon,frame.scope);ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,unit,frame.scope);lua_pushboolean(state,hpOnly);lua_pushboolean(state,safe);Call(5,1);
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
void CModPackage::ResetRuntimeState(){ModGroupSearch::Reset();ModDeathFearReset();ModTactical::Reset();delayedShots.clear();skillCooldowns.Clear();criticalAttacks.clear();JMV_NpcConsumablesReset();JMV_RegisteredLootReset("package-reset");refreshQueue->Reset();initialization->Reset();npcArmorCache.clear();cacheWorld=nullptr;cacheZone.clear();cacheSegment=-1;}
void CModPackage::SyncRuntimeState(){
 const void*world=theWorldServer.GetSpecificManager();std::string zone;
 zone=(LPCSTR)theWorldServer.szMobFileName;
 int segment=theWorldServer.GetSegmentTimeCounter();
 if(world!=cacheWorld||zone!=cacheZone||(cacheSegment>=0&&segment<cacheSegment)){for(auto&i:delayedShots)ForgetAttack(i.second->ap);delayedShots.clear();skillCooldowns.Clear();JMV_RegisteredLootReset("world-epoch");npcArmorCache.clear();initialization->Reset();refreshQueue->Reset();}
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
bool CModPackage::RecalcUnitStats(SRPGUnitStats&stats,SRPGCommonStats&common,CUnit&unit){
 auto statsFailure=[&](const char* reason,const SRPGUnitStats& s,const SRPGCommonStats& c){char message[768];
 snprintf(message,sizeof(message),"%s\nUnit ID=%u prototype=%s\nHP=%g MP=%g bodyWeight=%g\nMove=%g sight=%g hearing=%g",reason,unsigned(unit.GetID()),unit.GetPrototype()?unit.GetPrototype()->szName:"<none>",double(c.ssHP.fpMax),double(c.ssMP.fpMax),double(s.hls[RPGHL_BODY].fpHPWeight),double(s.move.fpEffectiveValue),double(s.percepts[RPGS_SIGHT].fpEffectiveValue),double(s.percepts[RPGS_HEARING].fpEffectiveValue));return std::runtime_error(message);};
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
 bool binkOutputConfigured=activeAudioConfig.GetInt("Addon","JMVMiniaudioBinkOutputPrototype",0)!=0;
 bool showAllQuestsConfigured=activeAudioConfig.GetInt("Addon","ShowAllQuests",0)!=0;
 if(ModAudioSettings().JMVMiniaudioBinkOutputPrototype!=binkOutputConfigured)
  throw std::runtime_error("Bink output setting differs from active registry");
 if(!CBinkVideo::DecoderAvailable())throw std::runtime_error("Game Bink decoder was not found");
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
 return {{"success",true},{"hostLifecycle",true},{"nativeReaderEntries",entries},{"mappedReaderEntries",entries},{"resourceOwnership",true},{"enumeration",true},{"femaleAnimationCount",femaleAnimationCount},{"maleAnimationCount",maleAnimationCount},{"twoHandedLootAxeIndex",twoHandedLootAxe},{"twoHandedLootSwordIndex",twoHandedLootSword},{"configConsumer",true},{"binkMiniaudioSetting",binkOutputConfigured},{"showAllQuestsSetting",showAllQuestsConfigured},{"binkMiniaudioConfigMatched",true},{"binkDecoderAvailable",true},{"cameraConsumer",true},{"cameraPath",expected.ResolveCamera("mainmenu.cam","")},{"mapReadConsumer",true},{"gameplayMapReader",true},{"zone1XSectors",zoneXSectors},{"zone1YSectors",zoneYSectors},{"gameplayActivated",false}};
}
}
float CModPackage::ExperienceMultiplier(const SRPGUnitStats&stats){return StatsNumber("GetExpMultiplierOnServer",stats);}
float CModPackage::ExperienceMultiplier(const SRPGUserUnitStats&stats){CallbackFrame frame(state);BeginCallback("GetExpMultiplierOnClient");ModLua::Push(state,&stats,frame.scope);Call(1,1);double n=Number(state,"Client experience multiplier");if(fabs(n)>(std::numeric_limits<float>::max)())throw std::runtime_error("Experience multiplier overflow");return float(n);}
float CModPackage::ManaBonus(const SRPGUnitStats&stats){return StatsNumber("GetManaBonusOnServer",stats);}
float CModPackage::VampSpellValue(const char*prototype,int column){CallbackFrame frame(state);BeginCallback("GetVampSpellAdds");lua_pushstring(state,prototype);lua_pushinteger(state,column);Call(2,1);double n=Number(state,"Vamp spell property");if(fabs(n)>(std::numeric_limits<float>::max)())throw std::runtime_error("Vamp property overflow");return float(n);}
float CModPackage::ManaBonus(const SRPGUserUnitStats&stats){CallbackFrame frame(state);BeginCallback("GetManaBonusOnClient");ModLua::Push(state,&stats,frame.scope);Call(1,1);double n=Number(state,"Client mana multiplier");if(fabs(n)>(std::numeric_limits<float>::max)())throw std::runtime_error("Mana multiplier overflow");return float(n);}
float CModPackage::DistributeExperience(float experience,int players){
 if(!std::isfinite(experience)||players<=0)throw std::runtime_error("Invalid LMP experience input");
 CallbackFrame frame(state);BeginCallback("DistributeLmpExp");lua_pushnumber(state,experience);lua_pushinteger(state,players);Call(2,1);double n=Number(state,"LMP experience");if(fabs(n)>(std::numeric_limits<float>::max)())throw std::runtime_error("LMP experience overflow");return float(n);
}
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
 Json shopResults=Json::array();int scalarCases=0,shareCases=0;
 for(int intelligence:{10,25,50,100,150,255}){
  SRPGUnitStats server={};server.psIntelligence.fpEffectiveValue=float(intelligence);server.fpExperience=100;server.fExpPenalty=-50;
  SRPGUserUnitStats client={};client.cIntelligence=BYTE(intelligence);
  double expected=pow(1.01,intelligence-25);float multiplier=server.GetExpMultiplier();
  require(fabs(multiplier-expected)<.0001*expected&&multiplier==client.GetExpMultiplier(),"Server/client XP multiplier mismatch");
  double mana=number("GetManaBonus",float(intelligence));require(fabs(server.GetManaBonusMultiplier()-mana)<.0001&&server.GetManaBonusMultiplier()==client.GetManaBonusMultiplier(),"Server/client mana multiplier mismatch");
  server.GainExperience(10);double penalty=-50+10*expected;
  require(fabs(server.fpExperience-(100+(std::max)(0.0,penalty)))<.001&&fabs(server.fExpPenalty-(std::min)(0.0,penalty))<.001,"Experience penalty repayment mismatch");scalarCases++;
 }
 for(int players:{1,2,4,8})for(float exp:{0.f,100.f,1001.f}){require(fabs(DistributeExperience(exp,players)-exp/players)<.0001,"LMP XP split mismatch");shareCases++;}
 bool badPlayers=false;try{DistributeExperience(10,0);}catch(const std::runtime_error&){badPlayers=true;}require(badPlayers,"Zero player count accepted");
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
 return {{"success",true},{"shopCoefficients",shopResults},{"scalarCases",scalarCases},{"shareCases",shareCases},{"shopModes",2},{"shopRollback",true},{"expiredShopArrayRejected",true},{"nativeExperiencePenalty",true},{"clientServerParity",true},{"gameplayActivated",false}};
}
bool InspectModPackageCommandLine(int &result){
 int argc=0;LPWSTR *argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)return false;
 bool mob=argc>=2&&wcscmp(argv[1],L"--inspect-mob-script")==0;
 bool defaults=argc>=2&&wcscmp(argv[1],L"--inspect-default-stats")==0;
 bool economy=argc>=2&&wcscmp(argv[1],L"--inspect-mod-economy")==0;
 bool startup=argc>=2&&wcscmp(argv[1],L"--inspect-mod-startup")==0;
 if(argc<2||(!mob&&!defaults&&!economy&&!startup&&wcscmp(argv[1],L"--inspect-addon")!=0)){LocalFree(argv);return false;}
 result=2;Json report;std::wstring output;
 if(mob&&argc==5){output=argv[4];try{
  ModRuntime::Host host(Ansi(argv[2]));std::ifstream file(fs::path(argv[3]),std::ios::binary);
  if(!file)throw std::runtime_error("Cannot open script input");
  std::string bytes((std::istreambuf_iterator<char>(file)),{});CString source(bytes.c_str());CScript script;
  bool ok=script.LoadText(source,FALSE)!=FALSE;auto error=script.GetLastError();
  report={{"success",ok},{"line",error.nLine},{"error",std::string((LPCSTR)error.text)}};result=ok?0:1;
 }catch(const std::exception&e){report={{"success",false},{"error",e.what()}};}}
 else if(argc==4){output=argv[3];try{if(defaults)report=CheckDefaultStats();else if(economy){CModPackage package;package.Load(Ansi(argv[2]));report=package.CheckEconomy();}else if(startup)report=CheckStartup(Ansi(argv[2]));else{CModPackage package;package.Load(Ansi(argv[2]));report=package.Describe();}result=0;}catch(const std::exception&e){report={{"success",false},{"error",e.what()}};}}
 else report={{"success",false},{"error","Expected --inspect-addon MOD_DIRECTORY OUTPUT_JSON"}};
 LocalFree(argv);if(!output.empty()){std::ofstream file{fs::path(output)};file<<report.dump(2,' ',false,Json::error_handler_t::replace);if(!file)result=3;}return true;
}

bool CModPackage::GetNpcCombatConsumableConfig(const char*zone,int id,const char*prototype,int kind,int variant,ModConsumableConfig&config){
 config={};if(!state||!zone||!zone[0]||id<0||kind<1||kind>5||variant<0||variant>2)return false;
 Stack stack(state);lua_getglobal(state,"GetNpcCombatConsumableConfig");if(!lua_isfunction(state,-1))return false;lua_pushstring(state,zone);lua_pushinteger(state,id);lua_pushstring(state,prototype?prototype:"");lua_pushinteger(state,kind);lua_pushinteger(state,variant);Call(5,4);
 if(!lua_isstring(state,-4)||!lua_isnumber(state,-3))return false;const char*formula=lua_tostring(state,-4);int charges=int(lua_tointeger(state,-3));if(!formula||!formula[0]||charges<1||charges>7)return false;
 config.spellFormula=formula;config.charges=charges;config.automatic=lua_toboolean(state,-2)!=0;config.prototypeFallback=lua_toboolean(state,-1)!=0;return true;
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
float CModPackage::GoCampEnemyRadius(){
 CallbackFrame frame(state);BeginCallback("GetGoCampConfigValue");lua_pushinteger(state,1);lua_pushnumber(state,25.0);Call(2,1);
 const double value=Number(state,"GoCamp enemy radius");if(value<0.0||value>500.0)throw std::runtime_error("GoCamp enemy radius is outside 0..500");return static_cast<float>(value);
}
bool CModPackage::DeathFearSettings(float &enterHp,float &exitHp,float &fleeDistance,float &speedPercent){
 enterHp=15.0f;exitHp=30.0f;fleeDistance=25.0f;speedPercent=65.0f;CallbackFrame frame(state);
 auto read=[&](int property,float fallback){BeginCallback("GetDeathFearConfigValue");lua_pushinteger(state,property);lua_pushnumber(state,fallback);Call(2,1);return float(Number(state,"DeathFear configuration"));};
 enterHp=read(1,enterHp);fleeDistance=read(2,fleeDistance);speedPercent=read(3,speedPercent);exitHp=read(4,exitHp);return true;
}
bool CModPackage::DeathFearExcludedPrototype(const char*prototype){
 if(!prototype||!*prototype)return false;CallbackFrame frame(state);BeginCallback("IsDeathFearExcludedPrototype");lua_pushstring(state,prototype);Call(1,1);
 if(!lua_isboolean(state,-1))throw std::runtime_error("DeathFear exclusion policy must return boolean");return lua_toboolean(state,-1)!=0;
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
void CModPackage::CampInfo(CPersonage&pers,CUnit*unit,int&capacity,float&seconds,float*displayActions,float*displayMana){
 const auto&stats=pers.stats;
 float jewelry=1,complex=1,sets=1;
 for(int i=0;i<pers.items.GetSize();++i)if(auto*q=pers.items[i])
  jewelry+=InfoProperty("GetJewelryPropsInside",q->GetShapeEntry()->szName,12)*.01f;
 float strength=stats.psStrength.fpEffectiveValue;
 if(unit){
  strength=unit->GetUserUnitStats()->cStrength; complex+=InfoComplex(*unit,8)*.01f;
  CallbackFrame frame(state);BeginCallback("GetSetsEncumbrance");ModLua::Push(state,unit,frame.scope);Call(1,1);sets=float(Number(state,"Camp sets encumbrance"));
 }
 capacity=int(strength*12*(1+stats.GetPerkModifier(CombatSetting("JMVPerks")?1:RPGPRK_LIFT)*.01f)*jewelry*complex*sets);
 float actions=(10+stats.psDexterity.fpEffectiveValue*.2f)*(1+stats.GetPerkModifier(14)*.01f);
 int slot=int(pers.dwCurrentWeapon),left=unit?ModStats::LeftHand(*unit):-1;
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
 if(displayMana){
  CallbackFrame frame(state);BeginCallback("OverloadManacostModsOnServer");
  lua_pushinteger(state,int(pers.GetItemsWeight()));lua_pushinteger(state,capacity);Call(2,1);
  *displayMana=pers.stats.RecalcMP()*float(Number(state,"Camp overload mana"));
 }
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
 auto property=[&](int column){BeginCallback("GetDualAnims");lua_pushstring(state,animation);lua_pushinteger(state,column);Call(2,1);double n=Number(state,"Dual animation property");lua_pop(state,1);return n;};
 double count=property(4);if(count==0)return result;
 double frames=property(17);if(count!=floor(count)||count<1||count>6||frames<=0||frames>INT_MAX||length<1)throw std::runtime_error("Invalid dual animation timing");
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

// Installed Lua policy; cache belongs to this package's immutable script instance.
bool CModPackage::SkillConfig(const char*name,ModSkills::Config&config){
 config={};if(!name||!*name)return false;
 auto cached=skillConfigs.find(name);if(cached!=skillConfigs.end()){config=cached->second.second;return cached->second.first;}
 Stack stack(state);auto getValue=[&](int property,double fallback,double&value){
 lua_getglobal(state,"GetMeleeActiveSpellValue");if(!lua_isfunction(state,-1)){lua_pop(state,1);return false;}
 lua_pushstring(state,name);lua_pushinteger(state,property);lua_pushnumber(state,fallback);Call(3,1);
 if(!lua_isnumber(state,-1))throw std::runtime_error("Non-numeric active skill policy");value=lua_tonumber(state,-1);lua_pop(state,1);
 if(!std::isfinite(value)||fabs(value)>float(INT_MAX)/32)throw std::runtime_error("Active skill policy out of range");return true;};
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
    config.projectileCount = (std::max)(1, (std::min)(2, static_cast<int>(projectileCount)));
    config.accuracyBonus = (std::max)(0.0f, static_cast<float>(accuracyBonus));
    config.areaRadius = (std::max)(0.0f, static_cast<float>(areaRadius));
    config.allowNpc = allowNpc != 0.0;
    return true;
 };bool result=read();skillConfigs[name]={result,config};return result;
}
bool CModPackage::SkillConfig(CSpell*spell,ModSkills::Config&config){
 if(!spell||!spell->GetPrototype())return false;const char*name=spell->GetPrototype()->szCodeName;
 std::string code=Lower(name);const char*setting=code=="powerhit"||code=="test"?"JMVPowerHit":code=="doubleshot"?"JMVDoubleShot":code=="preciseshot"?"JMVPreciseShot":code=="explosiveshot"?"JMVExplosiveShot":nullptr;
 if(setting&&!CombatSetting(setting))return false;if(!setting&&code!="precisehit"&&code!="cripplinghit")return false;
 return SkillConfig(name,config)||(code=="test"&&SkillConfig("powerhit",config));
}
bool CModPackage::TargetSkill(CSpell*spell)const{
 if(!spell||!spell->GetPrototype())return false;std::string code=Lower((LPCSTR)spell->GetPrototype()->szCodeName);
 return code=="powerhit"||code=="test"||code=="precisehit"||code=="doubleshot"||code=="preciseshot"||code=="cripplinghit";
}
float CModPackage::InfoProperty(const char*callback,const char*name,int property){
 CallbackFrame frame(state);BeginCallback(callback);lua_pushstring(state,name);lua_pushinteger(state,property);Call(2,1);double n=Number(state,callback);if(fabs(n)>(std::numeric_limits<float>::max)())throw std::runtime_error("Info property overflow");return float(n);
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

float CModPackage::PanelDamage(CUnit&unit,float damage,int slot,bool left){
 CallbackFrame frame(state);auto*w=slot>=0?frame.scope->Weapon(unit,slot):nullptr;
 BeginCallback("GetModifiedDamageOnClientJmv");ModLua::Push(state,unit.GetUserUnitStats(),frame.scope);ModLua::Push(state,&unit,frame.scope);
 lua_pushnumber(state,damage);lua_pushnumber(state,-1);lua_pushstring(state,unit.GetPrototype()->szName);
 lua_pushinteger(state,w?w->GetWeaponType():0);lua_pushinteger(state,slot);
 lua_pushboolean(state,IsIDofPersonage(unit.GetID())&&unit.GetTypeID()!=MOTID_ANIMAL&&unit.GetTypeID()!=MOTID_MONSTER);
 lua_pushboolean(state,CombatSetting("JMVFixCriticalHitSoundTrigger")!=0);Call(9,1);return CombatNumber(state,"Panel damage");
}
float CModPackage::PanelCritical(CUnit&unit,int slot,bool left,bool power){
 CallbackFrame frame(state);auto*w=slot>=0?frame.scope->Weapon(unit,slot):nullptr;
 BeginCallback(power?"GetCritDamageOnClientJmv":"GetCritOnClientJmv");ModLua::Push(state,&unit,frame.scope);
 lua_pushnumber(state,unit.GetUserUnitStats()->GetPerkModifier(power?4:12));
 lua_pushinteger(state,left?slot:-1);lua_pushboolean(state,left);lua_pushstring(state,unit.GetPrototype()->szName);
 lua_pushinteger(state,w?w->GetWeaponType():0);lua_pushinteger(state,slot);Call(7,1);return CombatNumber(state,"Panel critical");
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
