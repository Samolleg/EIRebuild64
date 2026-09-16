from pathlib import Path
import re,shutil,json,hashlib
r=Path('work/eirebuild-local');ev=r/'evidence/addon-integration/pre-r15';ev.mkdir(parents=True,exist_ok=True)
files=['Game/ModPackage.h','Game/ModPackage.cpp','Game/ModLuaBridge.cpp','Game/ModRuntime.h','Game/ModRuntime.cpp','Game/RPGSystem.cpp']
m={}
for f in files:
 p=r/f;d=ev/f;d.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,d);m[f]=hashlib.sha256(p.read_bytes()).hexdigest()
(ev/'source.json').write_text(json.dumps(m,indent=2))
src=Path('work/src_addon_world_background_data_20260916/src/addon/Patches/LS_ShieldsAndWeaponGroups.cpp').read_text(encoding='utf-8-sig')
s=src[src.index('bool IsTwohandedWeapon(int typeID, CUnit* unit)'):src.index('// This override exists only during one PowerHit native suffer call.')]
s=re.sub(r'//[^\n]*','',s);s=re.sub(r'\n\s*\n','\n',s)
s=s.replace('bool IsTwohandedWeapon(int typeID, CUnit* unit)','inline bool IsTwohandedWeapon(int typeID, const CUnit* unit)').replace('unit->userUnitStats.perks[JMVPRK_StrongMan]','unit->GetUserUnitStats()->cPerks[29]')
s=s.replace('int CUnit::GetLeftHand()','inline int LeftHand(const CUnit& unit)').replace('currentWeapon','unit.GetCurrentWeaponSlotIndex()').replace('GetQWeapon(','unit.GetQWeapon(').replace('const SIDBWeaponShape*','const SIDBItemGeneralShape*').replace('->typeID','->dwTypeID').replace(', this)',', &unit)')
header='''#pragma once
#include "Unit.h"
#include "Item.h"
#include "ItemDatabase.h"
#include "UnitDatabase.h"
#include <stdexcept>
// Source: installed-addon6538a636 LS_ShieldsAndWeaponGroups IsTwohandedWeapon/GetLeftHand.
// Branch structure retained; engine names and client-stat access adapted only.
namespace ModStats {
'''+s+'''
inline void Armor(SRPGCommonStats& common,CUnit&unit,bool correctedNpc){
 if(unit.GetSubtypeID()>=unsigned(theUnitDatabase.prototypes.GetNumEntries()))throw std::runtime_error("Unit prototype index out of range");
 const auto*prototype=unit.GetPrototype();if(prototype->dwRaceIndex>=unsigned(theUnitDatabase.races.GetNumEntries()))throw std::runtime_error("Unit race index out of range");
 float armor[RPG_NUM_HIT_LOCATION_TYPES][RPG_NUM_ATTACK_TYPES]={};
 auto add=[&](int location,CItemArmor*item,int layer){float values[RPG_NUM_ATTACK_TYPES];item->GetAbsorbtions(values,layer);for(int i=0;i<RPG_NUM_ATTACK_TYPES;i++)armor[location][i]+=values[i];};
 for(int slot=0;slot<RPG_NUM_ARMOR_TYPES;slot++){auto*item=unit.GetQArmor(slot);if(!item)continue;switch(slot){
 case RPGAST_HELM:add(RPGHLT_HEAD,item,0);break;
 case RPGAST_PLATE:case RPGAST_SHIRT:add(RPGHLT_BODY,item,0);add(RPGHLT_HAND,item,1);break;
 case RPGAST_LEGGINGS:case RPGAST_BOOTS:case RPGAST_PANTS:add(RPGHLT_LEG,item,0);break;
 case RPGAST_GLOVES:add(RPGHLT_HAND,item,0);break;
 }}
 const float weight[]={.1f,.3f,.3f,.3f};auto*race=theUnitDatabase.races.GetEntry(prototype->dwRaceIndex);
 bool corrected=correctedNpc && !IsIDofPersonage(unit.GetID());
 for(int type=0;type<RPG_NUM_ATTACK_TYPES;type++){float base=race->defences[type]*prototype->fpAbsorbtionMultiplier,result=base;
 for(int location=0;location<RPG_NUM_HIT_LOCATION_TYPES;location++)if(corrected)result+=armor[location][type]*weight[location];else result=base+armor[location][type]*weight[location];
 common.psAbsorbtions[type]={result,result};}
}
}
'''
(r/'Game/ModStatRules.h').write_text(header,encoding='utf-8')
def edit(f,a,b):
 p=r/f;v=p.read_bytes();nl=b'\r\n' if b'\r\n' in v else b'\n';x=a.encode().replace(b'\n',nl);y=b.encode().replace(b'\n',nl);assert v.count(x)==1,(f,a[:45],v.count(x));p.write_bytes(v.replace(x,y))
edit('Game/ModLuaBridge.cpp','#include "ModLuaBridge.h"','#include "ModLuaBridge.h"\n#include "ModStatRules.h"')
edit('Game/ModLuaBridge.cpp','int UnitWeapon(lua_State*s)', '''int LeftHand(lua_State*s){auto&r=Get(s);if(r.kind!=Unit)return luaL_error(s,"Expected typed unit for left hand");auto*p=(CUnit*)r.object;if(!p->GetUnitSpecific())return luaL_error(s,"Left hand requires native unit-specific component");lua_pushinteger(s,ModStats::LeftHand(*p));return 1;}
int UnitWeapon(lua_State*s)''')
edit('Game/ModLuaBridge.cpp','lua_pushcfunction(s,AddPerkRecord);lua_setglobal(s,"AddPerkRecord");','lua_pushcfunction(s,AddPerkRecord);lua_setglobal(s,"AddPerkRecord");lua_pushcfunction(s,LeftHand);lua_setglobal(s,"GetLeftHand");')
edit('Game/ModPackage.h','    void PrepareUnitStats','    void RecalcUnitStats(SRPGUnitStats&,SRPGCommonStats&,CUnit&);\n    void ResetRuntimeState();\n    void PrepareUnitStats')
edit('Game/ModPackage.h','    bool scriptInitialized=false;','''    bool scriptInitialized=false;
    bool npcArmorCalculation=true,npcTrickster=true,npcMaterialEffects=true;
    struct ArmorModifiers {unsigned subtype=0;float speed=0,sight=0,hearing=0;};
    std::map<int,ArmorModifiers> npcArmorCache;
    const void* cacheWorld=nullptr;std::string cacheZone;int cacheSegment=-1;
    void SyncRuntimeState();
    bool CollectArmor(CUnit&,ArmorModifiers&);''')
edit('Game/ModPackage.cpp','#include "Unit.h"','#include "Unit.h"\n#include "ModStatRules.h"\n#include "WorldServer.h"\n#include "WorldClient.h"\n#include "Player.h"\n#include "Item.h"')
edit('Game/ModPackage.cpp','enabled=(settings.GetInt("Addon","JMVSetItemsComplex",0)!=0);','enabled=(settings.GetInt("Addon","JMVSetItemsComplex",0)!=0);npcArmorCalculation=settings.GetInt("Addon","JMVNpcArmorCalculation",1)!=0;npcTrickster=settings.GetInt("Addon","JMVNpcTrickster",1)!=0;npcMaterialEffects=settings.GetInt("Addon","JMVNpcArmorMaterialEffects",1)!=0;')
# The full callback shares its local stunSpell table with attack callbacks. Keep the exact table and prior entry.
edit('Game/ModPackage.cpp','struct CallbackFrame {','''struct StunEntry {
 lua_State*s;int table=LUA_NOREF,previous=LUA_NOREF,id;bool committed=false;
 StunEntry(lua_State*state,int unit):s(state),id(unit){
  int top=lua_gettop(s);lua_getglobal(s,"RecalcUnitStatsJmv");
  for(int i=1;;i++){const char*name=lua_getupvalue(s,-1,i);if(!name)break;if(strcmp(name,"stunSpell")==0){if(!lua_istable(s,-1)){lua_settop(s,top);throw std::runtime_error("Invalid stunSpell state");}table=luaL_ref(s,LUA_REGISTRYINDEX);break;}lua_pop(s,1);}
  lua_settop(s,top);if(table==LUA_NOREF)throw std::runtime_error("Full recalc lacks the required stunSpell upvalue");
  lua_rawgeti(s,LUA_REGISTRYINDEX,table);lua_rawgeti(s,-1,id);previous=luaL_ref(s,LUA_REGISTRYINDEX);lua_pop(s,1);
 }
 ~StunEntry(){if(!committed){lua_rawgeti(s,LUA_REGISTRYINDEX,table);if(previous==LUA_REFNIL)lua_pushnil(s);else lua_rawgeti(s,LUA_REGISTRYINDEX,previous);lua_rawseti(s,-2,id);lua_pop(s,1);}luaL_unref(s,LUA_REGISTRYINDEX,table);luaL_unref(s,LUA_REGISTRYINDEX,previous);}
};
struct CallbackFrame {''')
edit('Game/ModPackage.cpp','void CModPackage::PrepareUnitStats','''void CModPackage::ResetRuntimeState(){npcArmorCache.clear();cacheWorld=nullptr;cacheZone.clear();cacheSegment=-1;}
void CModPackage::SyncRuntimeState(){
 const void*world=theWorldServer.GetSpecificManager();std::string zone;
 if(theWorldServer.GetPlayerManager()){auto*p=theWorldServer.GetPlayer(0);if(p)zone=(LPCSTR)p->party.szCurrentZone;}
 int segment=theWorldServer.GetSegmentTimeCounter();
 if(world!=cacheWorld||zone!=cacheZone||(cacheSegment>=0&&segment<cacheSegment))npcArmorCache.clear();
 cacheWorld=world;cacheZone=zone;cacheSegment=segment;
}
bool CModPackage::CollectArmor(CUnit&unit,ArmorModifiers&mods){
 bool has=false;mods.subtype=unit.GetSubtypeID();
 auto value=[&](const char*table,const char*name,int column){auto v=Lookup(table,std::string(name),column,0);if(!v.is_number())throw std::runtime_error("Non-numeric NPC armor property");double n=v.get<double>();if(!std::isfinite(n)||fabs(n)>(std::numeric_limits<float>::max)())throw std::runtime_error("Invalid NPC armor property");return float(n);};
 for(int i=0;i<RPG_NUM_ARMOR_TYPES;i++){auto*a=unit.GetQArmor(i);if(!a)continue;has=true;
 if(a->GetMaterial()>=unsigned(theItemDatabase.materials.GetNumEntries())||a->GetShape()>=unsigned(theItemDatabase.armors.GetNumEntries()))throw std::runtime_error("NPC armor database index out of range");
 const char*material=theItemDatabase.materials.GetEntry(a->GetMaterial())->szName;const char*shape=theItemDatabase.armors.GetEntry(a->GetShape())->szName;
 mods.speed+=value("MaterialProp",material,9);mods.sight+=value("MaterialProp",material,13)+value("ArmorProp",shape,5);mods.hearing+=value("MaterialProp",material,14)+value("ArmorProp",shape,6);
 }return has;
}
void CModPackage::RecalcUnitStats(SRPGUnitStats&stats,SRPGCommonStats&common,CUnit&unit){
 CallbackFrame frame(state);StunEntry stun(state,unit.GetID());SyncRuntimeState();
 SRPGUnitStats staged=stats;SRPGCommonStats stagedCommon=common;frame.scope->unit=&unit;frame.scope->stagedStats=&staged;
 ModStats::Armor(stagedCommon,unit,npcArmorCalculation);
 float effects[53]={};for(int i=0;i<unit.GetSpellEffects().GetSize();i++){const auto&e=unit.GetSpellEffects()[i];int id=e.GetEffectType();float power=e.GetPower();if(id>=0&&id<53){if(!std::isfinite(power))throw std::runtime_error("Non-finite spell effect");effects[id]=(std::max)(effects[id],power);}}
 BeginCallback("RecalcUnitStatsJmv");ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&stagedCommon,frame.scope);ModLua::Push(state,&unit,frame.scope);ModLua::PushEffects(state,effects,53,frame.scope);lua_pushboolean(state,true);lua_pushboolean(state,true);lua_pushstring(state,unit.GetPrototype()->szName);lua_pushboolean(state,true);Call(8,0);
 bool npc=unit.GetOwner()==nullptr;ArmorModifiers mods;bool hasMods=false;
 if(npc&&npcTrickster){int level=(std::min)(3,int(staged.cPerks[11]));if(level>0){const float day[]={0,1,3,6},night[]={0,.15f,.30f,.65f},speed[]={1,1.05f,1.1f,1.2f};staged.senses[RPGS_SIGHT].fpEffectiveValue=(std::max)(staged.senses[RPGS_SIGHT].fpEffectiveValue,staged.senses[RPGS_SIGHT].fpBaseValue+day[level]);staged.senses[RPGS_INFRA].fpEffectiveValue=(std::max)(staged.senses[RPGS_INFRA].fpEffectiveValue,staged.senses[RPGS_INFRA].fpBaseValue*(1+night[level]));staged.move.fpEffectiveValue*=speed[level];}}
 if(npc&&npcMaterialEffects){CUnit*candidates[]={&unit,theWorldServer.GetMapObjectManager()?theWorldServer.GetUnit(unit.GetID()):nullptr,theWorldClient.GetMapObjectManager()?theWorldClient.GetUnit(unit.GetID()):nullptr};
 for(auto*c:candidates)if(c&&c->GetID()==unit.GetID()&&c->GetSubtypeID()==unit.GetSubtypeID()){ArmorModifiers found;if(CollectArmor(*c,found)){mods=found;hasMods=true;break;}}
 if(!hasMods){auto it=npcArmorCache.find(unit.GetID());if(it!=npcArmorCache.end()&&it->second.subtype==unit.GetSubtypeID()){mods=it->second;hasMods=true;}}
 if(hasMods){staged.move.fpEffectiveValue*=(std::max)(0.f,1+mods.speed*.01f);staged.percepts[RPGS_SIGHT].fpEffectiveValue=(std::max)(0.f,staged.percepts[RPGS_SIGHT].fpEffectiveValue+mods.sight);staged.percepts[RPGS_HEARING].fpEffectiveValue=(std::max)(0.f,staged.percepts[RPGS_HEARING].fpEffectiveValue+mods.hearing);}}
 if(!std::isfinite(staged.move.fpEffectiveValue)||!std::isfinite(stagedCommon.ssHP.fpMax)||!std::isfinite(stagedCommon.ssMP.fpMax))throw std::runtime_error("Invalid final unit stats");
 // Map allocation happens before publication. The remaining assignments/setters do not throw.
 if(hasMods)npcArmorCache[unit.GetID()]=mods;else if(npc)npcArmorCache.erase(unit.GetID());
 frame.scope->CommitItems();stats=staged;common=stagedCommon;stun.committed=true;
}
void CModPackage::PrepareUnitStats''')
edit('Game/ModRuntime.cpp','    current = &package;','    package.ResetRuntimeState();\n    current = &package;')
edit('Game/ModRuntime.cpp','Session::~Session() { current = nullptr; }','Session::~Session() { if(current)current->ResetRuntimeState();current = nullptr; }')
edit('Game/RPGSystem.cpp','void SRPGUnitStats::RecalcStats(SRPGCommonStats *lpCommonStats, CUnit *pUnit)\n{','''void SRPGUnitStats::RecalcStats(SRPGCommonStats *lpCommonStats, CUnit *pUnit)
{
    if (auto *mod=ModRuntime::Current()) {
        if(!lpCommonStats || !pUnit || !pUnit->GetUnitSpecific() || !pUnit->GetUnitSpecific()->GetManager())throw std::runtime_error("Full recalc requires an owned native unit");
        mod->RecalcUnitStats(*this,*lpCommonStats,*pUnit);
        if(!pUnit->GetOwner() && cPerks[11]>0) pUnit->RecalcAIParams();
        pUnit->SetUpdateMask(CMapObject::VLD_MASK_STATS);pUnit->SetUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);
        return;
    }''')

