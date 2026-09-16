#include "StdAfx.h"
#include "ScenarioTracker.h"
#include "ModPackage.h"
#include "ModAudioConfig.h"
#include "ModRegisteredLoot.inl"
#include "ModNpcConsumables.inl"
#include "Unit.h"
#include "ModStatRules.h"
#include "ModStatInitialization.h"
#include "ModNpcRefresh.h"
#include "MapObjectServer.h"
#include "SpellManager.h"
#include "WorldServer.h"
#include "WorldClient.h"
#include "Player.h"
#include "PlayerServer.h"
#include "UnitServer.h"
#include "Item.h"
#include "ModLuaBridge.h"
#include "ModRuntime.h"
#include "RpgSystem.h"
#include "PerkDatabase.h"
#include <limits>
#include <cmath>
#include "FileRegistry.h"
#include "CMappedResource.h"
#include "luajit-x86/lua.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <set>
#include <shellapi.h>
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
 CFileRegistry config;if(!config.Open(Join(root,"config.reg").c_str()))throw std::runtime_error("Cannot open mod config.reg");
 CStringArray entries;config.GetEntries("RES",entries);
 for(int i=0;i<entries.GetSize();i++){
   std::string key=Lower((LPCSTR)entries[i]);CStringArray paths;config.GetStringArray("RES",entries[i],paths);
   for(int j=0;j<paths.GetSize();j++){std::string path=(LPCSTR)paths[j];if(path.empty())throw std::runtime_error("Empty RES value");if(path[0]=='-')ignoreBase[key]=true;else archives[key].push_back(Join(root,path));}
 }
 entries.RemoveAll();config.GetEntries("CONFIG",entries);
 for(int i=0;i<entries.GetSize();i++){CString path=config.GetString("CONFIG",entries[i],"");if(!path.IsEmpty())configs[Lower((LPCSTR)entries[i])]=Join(root,(LPCSTR)path);}
 for(const auto &kv:configs)if(!fs::is_regular_file(kv.second))throw std::runtime_error("Missing config: "+kv.second);
 mainScript="main.lua";auto ai=configs.find("ai.reg");if(ai!=configs.end()){CFileRegistry r;if(!r.Open(ai->second.c_str()))throw std::runtime_error("Cannot read addon AI registry");mainScript=(LPCSTR)r.GetString("Addon","LuaMainScriptPath","main.lua");}
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
 {bool enabled=false;auto ai=configs.find("ai.reg");if(ai!=configs.end()){CFileRegistry settings;if(!settings.Open(ai->second.c_str()))throw std::runtime_error("Cannot read addon settings");enabled=(settings.GetInt("Addon","JMVSetItemsComplex",0)!=0);ConfigureModAudio(settings,root);registeredLoot=settings.GetInt("Addon","JMVRegisterLoot",0)!=0;npcStatRefresh=settings.GetInt("Addon","JMVNpcStatRefresh",1)!=0;npcArmorCalculation=settings.GetInt("Addon","JMVNpcArmorCalculation",1)!=0;npcTrickster=settings.GetInt("Addon","JMVNpcTrickster",1)!=0;npcMaterialEffects=settings.GetInt("Addon","JMVNpcArmorMaterialEffects",1)!=0;}lua_pushboolean(state,enabled);lua_setglobal(state,"JMVSetItemsComplexEnabled");}
 if(luaL_loadfile(state,Join(root,mainScript).c_str()))throw std::runtime_error(lua_tostring(state,-1));
 Call(0,0);ModLua::InstallFactories(state);
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
void CModPackage::ResetRuntimeState(){JMV_NpcConsumablesReset();JMV_RegisteredLootReset("package-reset");refreshQueue->Reset();initialization->Reset();npcArmorCache.clear();cacheWorld=nullptr;cacheZone.clear();cacheSegment=-1;}
void CModPackage::SyncRuntimeState(){
 const void*world=theWorldServer.GetSpecificManager();std::string zone;
 zone=(LPCSTR)theWorldServer.szMobFileName;
 int segment=theWorldServer.GetSegmentTimeCounter();
 if(world!=cacheWorld||zone!=cacheZone||(cacheSegment>=0&&segment<cacheSegment)){JMV_RegisteredLootReset("world-epoch");npcArmorCache.clear();initialization->Reset();refreshQueue->Reset();}
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
 SyncRuntimeState();if(theWorldServer.IsMapLoaded())JMV_RegisteredLootTick();InitializeLoadedHeroes();if(!npcStatRefresh||!theWorldServer.IsMapLoaded())return;
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
void CModPackage::SuccessfulWorldLoad(){ResetRuntimeState();SyncRuntimeState();JMV_RegisteredLootOnSuccessfulSaveLoad();JMV_NpcConsumablesLoaded();InitializeLoadedHeroes();}
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
 CallbackFrame frame(state);StunEntry stun(state,unit.GetID());SyncRuntimeState();
 initialization->Invalidate(unit.GetID());const unsigned epoch=initialization->epoch;
 SRPGUnitStats staged=stats;SRPGCommonStats stagedCommon=common;frame.scope->unit=&unit;frame.scope->stagedStats=&staged;
 // SetupStats supplies prototype common HP before SetRPGStats performs its two passes.
 // A fresh skeleton has no previous distribution to preserve proportionally.
 bool used=false,fresh=true;for(const auto&h:staged.hls)if(h.state>RPGALS_NOTUSED){used=true;fresh=fresh&&h.fpHP==0&&h.fpMaxHP==0;}
 if(used&&fresh){
  if(!std::isfinite(stagedCommon.ssHP.fpMax)||stagedCommon.ssHP.fpMax<=0||!std::isfinite(staged.hls[RPGHL_BODY].fpHPWeight)||staged.hls[RPGHL_BODY].fpHPWeight<=0)throw std::runtime_error("Fresh unit requires prototype HP and body weight");
  staged.DistributeHP(stagedCommon.ssHP.fpMax);
 }
 ModStats::Armor(stagedCommon,unit,npcArmorCalculation);
 float effects[53]={};for(int i=0;i<unit.GetSpellEffects().GetSize();i++){const auto&e=unit.GetSpellEffects()[i];int id=e.GetEffectType();float power=e.GetPower();if(id>=0&&id<53){if(!std::isfinite(power))throw std::runtime_error("Non-finite spell effect");effects[id]=(std::max)(effects[id],power);}}
 BeginCallback("RecalcUnitStatsJmv");ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&stagedCommon,frame.scope);ModLua::Push(state,&unit,frame.scope);ModLua::PushEffects(state,effects,53,frame.scope);lua_pushboolean(state,true);lua_pushboolean(state,true);lua_pushstring(state,unit.GetPrototype()->szName);lua_pushboolean(state,true);Call(8,0);
 bool npc=unit.GetOwner()==nullptr;ArmorModifiers mods;bool hasMods=false;
 if(npc&&npcTrickster){int level=(std::min)(3,int(staged.cPerks[11]));if(level>0){const float day[]={0,1,3,6},night[]={0,.15f,.30f,.65f},speed[]={1,1.05f,1.1f,1.2f};staged.senses[RPGS_SIGHT].fpEffectiveValue=(std::max)(staged.senses[RPGS_SIGHT].fpEffectiveValue,staged.senses[RPGS_SIGHT].fpBaseValue+day[level]);staged.senses[RPGS_INFRA].fpEffectiveValue=(std::max)(staged.senses[RPGS_INFRA].fpEffectiveValue,staged.senses[RPGS_INFRA].fpBaseValue*(1+night[level]));staged.move.fpEffectiveValue*=speed[level];}}
 if(npc&&npcMaterialEffects){CUnit*candidates[]={&unit,theWorldServer.GetMapObjectManager()?theWorldServer.GetUnit(unit.GetID()):nullptr,theWorld.GetMapObjectManager()?theWorld.GetUnit(unit.GetID()):nullptr};
 for(auto*c:candidates)if(c&&c->GetID()==unit.GetID()&&c->GetSubtypeID()==unit.GetSubtypeID()){ArmorModifiers found;if(CollectArmor(*c,found)){mods=found;hasMods=true;break;}}
 if(!hasMods){auto it=npcArmorCache.find(unit.GetID());if(it!=npcArmorCache.end()&&it->second.subtype==unit.GetSubtypeID()){mods=it->second;hasMods=true;}}
 if(hasMods){staged.move.fpEffectiveValue*=(std::max)(0.f,1+mods.speed*.01f);staged.percepts[RPGS_SIGHT].fpEffectiveValue=(std::max)(0.f,staged.percepts[RPGS_SIGHT].fpEffectiveValue+mods.sight);staged.percepts[RPGS_HEARING].fpEffectiveValue=(std::max)(0.f,staged.percepts[RPGS_HEARING].fpEffectiveValue+mods.hearing);}}
 if(!std::isfinite(staged.move.fpEffectiveValue)||!std::isfinite(staged.percepts[RPGS_SIGHT].fpEffectiveValue)||!std::isfinite(staged.percepts[RPGS_HEARING].fpEffectiveValue)||!std::isfinite(stagedCommon.ssHP.fpMax)||!std::isfinite(stagedCommon.ssMP.fpMax))throw std::runtime_error("Invalid final unit stats");
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
Json CheckStartup(const std::string&directory){
 CModPackage expected;expected.Load(directory);CMappedResource native;CWin32MappedResource mapped;int entries=0;std::string base=(fs::current_path()/"res/database.res").string();
 {
  ModRuntime::Host host(directory);
  if(!ModRuntime::Current()||!ModRuntime::Resources())throw std::runtime_error("Host did not bind runtime and resources");
  if(!native.OpenResource(base.c_str())||!mapped.OpenResource(base.c_str()))throw std::runtime_error("Native database resources failed to open");
  for(const char*name:{"units.udb","items.idb","perks.pdb"}){
   std::string owner;auto bytes=expected.ReadResource("database.res",name,base,owner);DWORD n=0,m=0;auto*a=native.GetFile(name,&n);auto*b=mapped.GetFile(name,&m);
   if(!a||!b||n!=bytes.size()||m!=n||memcmp(a,bytes.data(),n)||memcmp(b,bytes.data(),n))throw std::runtime_error("Native RES override bytes differ");
   CResourceBase*poly=&native;CResourceBase*polyMapped=&mapped;if(!poly->CheckFileExist(name)||!polyMapped->CheckFileExist(name))throw std::runtime_error("Native RES existence differs");
   if(!native.CloseFile(a)||!mapped.CloseFile(b))throw std::runtime_error("Resource owner failed to close entry");entries++;
  }
  CStringArray names;names.Add("__stale__");mapped.GetFileList(names,"*.udb");if(names.GetSize()==0)throw std::runtime_error("Overlay enumeration empty");
  for(int i=0;i<names.GetSize();i++)if(names[i]=="__stale__")throw std::runtime_error("Enumeration retained stale output");
  CFileRegistry config;if(!config.Open("config\\ai.reg"))throw std::runtime_error("Config consumer did not resolve mod registry");
  std::string actual=ModRuntime::ConfigPath("config\\ai.reg"),wanted=expected.ResolveConfig("ai.reg","config\\ai.reg");if(actual!=wanted)throw std::runtime_error("Config consumer mismatch");
  auto mapRoot=fs::path(directory)/"Maps";bool mapTest=false;for(const auto&f:fs::recursive_directory_iterator(mapRoot))if(f.is_regular_file()&&f.path().extension()==".mpr"){
   auto relative=fs::relative(f.path(),mapRoot).string();std::string candidate="maps\\"+relative,resolved=ModRuntime::MapPath(candidate.c_str());if(fs::path(resolved)!=f.path())throw std::runtime_error("Map path mismatch");CFileSave file;if(!file.OpenRead(candidate.c_str()))throw std::runtime_error("Native map read failed");file.CloseFile();mapTest=true;break;}
  if(!mapTest)throw std::runtime_error("No installed map fixture");
 }
 if(ModRuntime::Current()||ModRuntime::Resources())throw std::runtime_error("Host leaked its binding");
 DWORD size=0;auto*p=mapped.GetFile("units.udb",&size);if(!p||!mapped.CloseFile(p))throw std::runtime_error("Resource handle lifetime depended on destroyed Host");
 return {{"success",true},{"hostLifecycle",true},{"nativeReaderEntries",entries},{"mappedReaderEntries",entries},{"resourceOwnership",true},{"enumeration",true},{"configConsumer",true},{"mapReadConsumer",true},{"gameplayActivated",false}};
}
}
float CModPackage::ExperienceMultiplier(const SRPGUnitStats&stats){return StatsNumber("GetExpMultiplierOnServer",stats);}
float CModPackage::ExperienceMultiplier(const SRPGUserUnitStats&stats){CallbackFrame frame(state);BeginCallback("GetExpMultiplierOnClient");ModLua::Push(state,&stats,frame.scope);Call(1,1);double n=Number(state,"Client experience multiplier");if(fabs(n)>(std::numeric_limits<float>::max)())throw std::runtime_error("Experience multiplier overflow");return float(n);}
float CModPackage::ManaBonus(const SRPGUnitStats&stats){return StatsNumber("GetManaBonusOnServer",stats);}
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
 bool economy=argc>=2&&wcscmp(argv[1],L"--inspect-mod-economy")==0;
 bool startup=argc>=2&&wcscmp(argv[1],L"--inspect-mod-startup")==0;
 if(argc<2||(!economy&&!startup&&wcscmp(argv[1],L"--inspect-addon")!=0)){LocalFree(argv);return false;}
 result=2;Json report;std::wstring output;
 if(argc==4){output=argv[3];try{if(economy){CModPackage package;package.Load(Ansi(argv[2]));report=package.CheckEconomy();}else if(startup)report=CheckStartup(Ansi(argv[2]));else{CModPackage package;package.Load(Ansi(argv[2]));report=package.Describe();}result=0;}catch(const std::exception&e){report={{"success",false},{"error",e.what()}};}}
 else report={{"success",false},{"error","Expected --inspect-addon MOD_DIRECTORY OUTPUT_JSON"}};
 LocalFree(argv);if(!output.empty()){std::ofstream file{fs::path(output)};file<<report.dump(2,' ',false,Json::error_handler_t::replace);if(!file)result=3;}return true;
}

bool CModPackage::GetNpcCombatConsumableConfig(const char*zone,int id,const char*prototype,int kind,int variant,ModConsumableConfig&config){
 config={};if(!state||!zone||!zone[0]||id<0||kind<1||kind>5||variant<0||variant>2)return false;
 Stack stack(state);lua_getglobal(state,"GetNpcCombatConsumableConfig");if(!lua_isfunction(state,-1))return false;lua_pushstring(state,zone);lua_pushinteger(state,id);lua_pushstring(state,prototype?prototype:"");lua_pushinteger(state,kind);lua_pushinteger(state,variant);Call(5,4);
 if(!lua_isstring(state,-4)||!lua_isnumber(state,-3))return false;const char*formula=lua_tostring(state,-4);int charges=int(lua_tointeger(state,-3));if(!formula||!formula[0]||charges<1||charges>7)return false;
 config.spellFormula=formula;config.charges=charges;config.automatic=lua_toboolean(state,-2)!=0;config.prototypeFallback=lua_toboolean(state,-1)!=0;return true;
}
