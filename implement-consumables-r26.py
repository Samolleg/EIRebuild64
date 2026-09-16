from pathlib import Path
r=Path(__file__).resolve().parent;ref=r.parent/'src_addon_world_background_data_20260916/src/addon'
def edit(name,fn):
 p=r/name;b=p.read_bytes();dest=r/'evidence/addon-integration/pre-r26'/name;dest.parent.mkdir(parents=True,exist_ok=True)
 if not dest.exists():dest.write_bytes(b)
 p.write_bytes(fn(b.decode('cp1251')).encode('cp1251'))
(r/'Game/ModNpcConsumables.h').write_text('''#pragma once
#include <string>
class CUnit;struct SRPGCommonStats;
struct ModConsumableConfig {std::string spellFormula;int charges=0;bool automatic=false;bool prototypeFallback=false;};
bool JMV_RegisterNpcCombatConsumable(CUnit*,const char*,const char*,int);
void JMV_NpcConsumablesUpdate(SRPGCommonStats*,CUnit*);
void JMV_NpcConsumablesLoaded();
void JMV_NpcConsumablesRestart();
void JMV_NpcConsumablesReset();
bool JMV_CheckNpcConsumablePacking();
''',encoding='utf-8')
s=(ref/'Patches/LuaScripts.cpp').read_text(encoding='utf-8-sig')
start=s.index('namespace {',s.index('enum ENpcCombatConsumableKind')-30);end=s.index('\n#else\n\nbool JMV_RegisterNpcCombatConsumable',start);s=s[start:end]
s=s.replace('static void(__stdcall *OrigNpcCombatConsumableRestartScript)(CScript *);','')
a='static void __stdcall NewNpcCombatConsumableRestartScript(CScript *script)';i=s.index(a);j=s.index('} // namespace',i);s=s[:i]+s[j:]
s=s.replace('Lua::SNpcCombatConsumableConfig','ModConsumableConfig').replace('Lua::State.GetNpcCombatConsumableConfig','ModRuntime::Current()->GetNpcCombatConsumableConfig').replace('WorldServer->GetMobFileName()','&theWorldServer.szMobFileName').replace('mobName->GetString()','(LPCSTR)*mobName').replace('prototype->name.GetString()','(LPCSTR)prototype->szName').replace('GetClassID()','WhoAmI()').replace('InformFailureServer','InformFailure').replace('server->logic.state','server->logic.GetScriptState()').replace('stats->hp.current','stats->ssHP.fpCurrent').replace('stats->hp.max','stats->ssHP.fpMax').replace('g_addonCfg.enableLuaScripts','true').replace('g_addonCfg.JMVNpcCombatConsumables','true')
a='\t\tg_Log.Error(';i=s.index(a);j=s.index('\n\t\treturn false;',i);s=s[:i]+s[j:]
s='''#include "ModNpcConsumables.h"
#include "WorldServer.h"
#include "WorldClient.h"
#include "UnitServer.h"
#include "SpellManager.h"
#include "Player.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include <unordered_map>
#include <cctype>
#define WorldServer (&theWorldServer)
#define WorldClient (&theWorld)
#define JMV_NPC_COMBAT_CONSUMABLES_DIAGNOSTICS 0
'''+s+'''
void JMV_NpcConsumablesUpdate(SRPGCommonStats*s,CUnit*u){if(ModRuntime::Current()&&theWorldServer.GetPlayerManager())NPC_Update(s,u);}
void JMV_NpcConsumablesLoaded(){if(ModRuntime::Current())NPC_ResetRuntimeAfterLoad();}
void JMV_NpcConsumablesRestart(){if(ModRuntime::Current())NPC_OnScriptRestart();}
void JMV_NpcConsumablesReset(){G_npcCombatConsumableStates.clear();G_npcCombatConsumableZone.clear();G_npcCombatConsumableVars=nullptr;G_npcCombatConsumableVisitToken=0;G_npcCombatConsumableFreshVisit=false;G_npcCombatConsumableNextZoneCheckTick=0;G_npcCombatConsumableLastObservedTick=0;G_npcCombatConsumableLoadResumePending=false;G_npcCombatConsumableLoadResumeZone.clear();G_npcCombatConsumableLoadResumeArmedAt=0;}
bool JMV_CheckNpcConsumablePacking(){CGSVariableSet vars;SNpcCombatConsumableState state;state.visitToken=511;state.nextHealingTick=109;for(int i=0;i<5;i++){state.slots[i].remainingCharges=7;state.slots[i].selection=3;state.slots[i].phase=2;}NPC_PersistState(vars,123,state);auto bits=NPC_ReadPacked(vars,NPC_UnitKey("state",G_npcCombatConsumableZone,123));auto meta=NPC_ReadPacked(vars,NPC_UnitKey("meta",G_npcCombatConsumableZone,123));if((bits>>15)!=511)return false;for(int i=0;i<5;i++)if(((bits>>(i*3))&7)!=7||((meta>>(i*2))&3)!=3||((meta>>(10+i*2))&3)!=2)return false;return NPC_ParseKind("HEALING")==1&&NPC_NormalizeMobName("Maps/ZONE_1.mob")=="zone_1";}
#undef WorldServer
#undef WorldClient
#undef JMV_NPC_COMBAT_CONSUMABLES_DIAGNOSTICS
'''
(r/'Game/ModNpcConsumables.inl').write_text(s,encoding='utf-8')
edit('Game/ModPackage.h',lambda s:s.replace('#pragma once','#pragma once\r\n#include "ModNpcConsumables.h"').replace('    void Load(const std::string &directory);','    bool GetNpcCombatConsumableConfig(const char*zone,int id,const char*prototype,int kind,int variant,ModConsumableConfig&);\r\n    void Load(const std::string &directory);'))
def package(s):
 s=s.replace('#include "ModRegisteredLoot.inl"','#include "ModRegisteredLoot.inl"\r\n#include "ModNpcConsumables.inl"')
 s=s.replace('void CModPackage::ResetRuntimeState(){','void CModPackage::ResetRuntimeState(){JMV_NpcConsumablesReset();')
 s=s.replace('JMV_RegisteredLootOnSuccessfulSaveLoad();InitializeLoadedHeroes();','JMV_RegisteredLootOnSuccessfulSaveLoad();JMV_NpcConsumablesLoaded();InitializeLoadedHeroes();')
 s=s.replace('common.nRegenerateCounter=static_cast<int>(next);return false;','common.nRegenerateCounter=static_cast<int>(next);JMV_NpcConsumablesUpdate(&common,unit);return false;')
 s=s.replace('frame.scope->CommitItems();stats=staged;common=stagedCommon;','frame.scope->CommitItems();stats=staged;common=stagedCommon;\r\n JMV_NpcConsumablesUpdate(&common,unit);')
 code='''
bool CModPackage::GetNpcCombatConsumableConfig(const char*zone,int id,const char*prototype,int kind,int variant,ModConsumableConfig&config){
 config={};if(!state||!zone||!zone[0]||id<0||kind<1||kind>5||variant<0||variant>2)return false;
 Stack stack(state);lua_getglobal(state,"GetNpcCombatConsumableConfig");if(!lua_isfunction(state,-1))return false;lua_pushstring(state,zone);lua_pushinteger(state,id);lua_pushstring(state,prototype?prototype:"");lua_pushinteger(state,kind);lua_pushinteger(state,variant);Call(5,4);
 if(!lua_isstring(state,-4)||!lua_isnumber(state,-3))return false;const char*formula=lua_tostring(state,-4);int charges=int(lua_tointeger(state,-3));if(!formula||!formula[0]||charges<1||charges>7)return false;
 config.spellFormula=formula;config.charges=charges;config.automatic=lua_toboolean(state,-2)!=0;config.prototypeFallback=lua_toboolean(state,-1)!=0;return true;
}
'''
 return s+code.replace('\n','\r\n')
edit('Game/ModPackage.cpp',package)
def script(s):
 s=s.replace('#include "ModMobCommands.h"','#include "ModMobCommands.h"\r\n#include "ModNpcConsumables.h"')
 a='    for (POSITION pos = worldActions.GetHeadPosition(); pos;)\r\n        RunThreadDelayed(&worldActions.GetNext(pos));\r\n}';assert s.count(a)==1;s=s.replace(a,a[:-1]+'    JMV_NpcConsumablesRestart();\r\n}');return s
edit('Game/Script.cpp',script)
def mob(s):
 s=s.replace('"NpcCombatConsumable","ssof",false','"NpcCombatConsumable","ssof",true')
 a=' case 382:';assert a in s;s=s.replace(a,' case 384:if(server&&!JMV_RegisterNpcCombatConsumable(unit,cmd.szParams[0],cmd.szParams[1],CheckedInt(a)))throw std::runtime_error("NPC consumable profile rejected");break;\r\n'+a);return s
edit('Game/ModMobCommands.h',mob)
edit('Game/ModRuntime.cpp',lambda s:s.replace('pending.command=384','pending.command=387').replace('            require(JMV_CheckRegisteredLootCodec()','            require(JMV_CheckNpcConsumablePacking(),"NPC consumable persistence packing mismatch");\r\n            require(JMV_CheckRegisteredLootCodec()').replace('return {{"messageQueueLifecycle"','return {{"npcConsumablePacking",true},{"messageQueueLifecycle"'))
