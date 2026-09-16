from pathlib import Path
import shutil,hashlib,json
r=Path('work/eirebuild-local'); ev=r/'evidence/addon-integration/pre-r14';ev.mkdir(parents=True,exist_ok=True)
files=['Game/ModLuaBridge.h','Game/ModLuaBridge.cpp','Game/ModPackage.h','Game/ModPackage.cpp','Game/ModRuntime.cpp','Game/Item.h']
manifest={}
for f in files:
 p=r/f; dest=ev/f;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest);manifest[f]=hashlib.sha256(p.read_bytes()).hexdigest()
(ev/'source.json').write_text(json.dumps(manifest,indent=2))
def edit(f,old,new):
 p=r/f;b=p.read_bytes();a=old.replace('\n','\r\n').encode() if b'\r\n' in b else old.encode();z=new.replace('\n','\r\n').encode() if b'\r\n' in b else new.encode();assert b.count(a)==1,(f,old[:60],b.count(a));p.write_bytes(b.replace(a,z))
edit('Game/Item.h','    float GetMP() const { return fpMP; }','    float GetMP() const { return fpMP; }\n    float GetManaRegenRate() const { return fpRegenMP; }\n    void SetManaRegenRate(float value) { fpRegenMP = value; }')
edit('Game/ModLuaBridge.h','#include <memory>','#include <memory>\n#include <map>')
edit('Game/ModLuaBridge.h','struct Scope {bool active=true;~Scope(){active=false;}};','''struct Scope {
 bool active=true;
 const CUnit *unit=nullptr;
 SRPGUnitStats *stagedStats=nullptr;
 std::map<CItem*,float> itemRegeneration;
 void CommitItems();
 ~Scope(){active=false;}
};''')
edit('Game/ModLuaBridge.h','void Register(lua_State *state);','void PushEffects(lua_State*,float*,int,const std::shared_ptr<Scope>&);\nvoid Register(lua_State *state);')
edit('Game/ModLuaBridge.cpp','Floats,Perk,HitLocation,ArmorLayer','Floats,Perk,HitLocation,ArmorLayer,ReadOnlyFloats')
edit('Game/ModLuaBridge.cpp','else if(r.kind==Floats)lua_pushnumber','else if(r.kind==Floats||r.kind==ReadOnlyFloats)lua_pushnumber')
edit('Game/ModLuaBridge.cpp','int UnitEffect(lua_State*s)', '''int EffectArray(lua_State*s){int arg=lua_istable(s,1)?2:1;auto&r=Get(s,arg);
 if(r.kind!=ReadOnlyFloats || luaL_checkint(s,arg+2)!=r.count || !lua_toboolean(s,arg+3))return luaL_error(s,"Expected exact read-only native effect array");
 lua_getglobal(s,"Memory");lua_getfield(s,-1,"Type");lua_getfield(s,-1,"Float");bool type=lua_rawequal(s,arg+1,-1)!=0;lua_pop(s,3);
 if(!type)return luaL_error(s,"Native effects require Float element type");lua_pushvalue(s,arg);return 1;
}
int AddPerkRecord(lua_State*s){auto&r=Get(s);int index=luaL_checkint(s,2),level=luaL_checkint(s,3);
 if(r.kind!=Unit || r.object!=r.scope->unit || !r.scope->stagedStats)return luaL_error(s,"Perk mutation requires the active staged unit");
 if(index<0||index>=RPG_NUM_PERKS||level<0||level>=3||index*3+level>=thePerkDatabase.GetNumPerks())return luaL_error(s,"Invalid native perk record");
 try{r.scope->stagedStats->AddPerk(&thePerkDatabase,thePerkDatabase.GetPerk(index*3+level));}
 catch(const std::exception&e){lua_pushstring(s,e.what());return lua_error(s);}return 0;
}
int UnitWeapon(lua_State*s){auto&r=Get(s);int i=luaL_checkint(s,2);if(r.kind!=Unit||i<0||i>=QI_MAX_WEAPONS)return luaL_error(s,"Invalid weapon slot");auto*p=(CUnit*)r.object;if(!p->GetUnitSpecific())return luaL_error(s,"Weapons require native unit-specific component");PushRef(s,p->GetQWeapon(i),Item,r.scope);return 1;}
int UnitEffect(lua_State*s)''')
edit('Game/ModLuaBridge.cpp','else{if(index>=theItemDatabase.quickItems', 'else if(kind==3){if(index>=theItemDatabase.armors.GetNumEntries())return luaL_error(s,"Armor database index out of range");name=theItemDatabase.armors.GetEntry(index)->szName;}\n else{if(index>=theItemDatabase.quickItems')
edit('Game/ModLuaBridge.cpp','if((r.kind==Bytes||r.kind==Floats)&&!write)', 'if((r.kind==Bytes||r.kind==Floats||r.kind==ReadOnlyFloats)&&!write)')
edit('Game/ModLuaBridge.cpp','if(strcmp(k,"quickItem")==0)', 'if(strcmp(k,"weapon")==0){lua_pushcfunction(s,UnitWeapon);return 1;}\n if(strcmp(k,"quickItem")==0)')
edit('Game/ModLuaBridge.cpp','U("id",p->GetID());','U("itemsWeight",p->GetItemsWeight());U("id",p->GetID());')
edit('Game/ModLuaBridge.cpp','if(r.kind==Item){auto*p=(CItem*)r.object;', '''if(r.kind==Item){auto*p=(CItem*)r.object;
 if(strcmp(k,"regenRate")==0){auto it=r.scope->itemRegeneration.find(p);float value=it==r.scope->itemRegeneration.end()?p->GetManaRegenRate():it->second;int result=Field(s,value,write);if(write)r.scope->itemRegeneration[p]=value;return result;}
 if(!write&&strcmp(k,"prototype")==0){const auto*shape=p->GetShapeEntry();if(!shape)return luaL_error(s,"Item prototype missing");lua_newtable(s);lua_pushstring(s,shape->szName);lua_setfield(s,-2,"name");lua_pushinteger(s,shape->dwTypeID);lua_setfield(s,-2,"typeId");return 1;}''')
edit('Game/ModLuaBridge.cpp','void InstallFactories(lua_State*s){','''void InstallFactories(lua_State*s){
 lua_pushcfunction(s,AddPerkRecord);lua_setglobal(s,"AddPerkRecord");
 lua_newtable(s);lua_pushcfunction(s,EffectArray);lua_setfield(s,-2,"new");lua_newtable(s);lua_pushcfunction(s,EffectArray);lua_setfield(s,-2,"__call");lua_setmetatable(s,-2);lua_setglobal(s,"Array");''')
edit('Game/ModLuaBridge.cpp','const char*names[]={"MaterialsTable","WeaponsTable","QuicksTable"};for(int i=0;i<3;i++)','const char*names[]={"MaterialsTable","WeaponsTable","QuicksTable","ArmorsTable"};for(int i=0;i<4;i++)')
edit('Game/ModLuaBridge.cpp','void Push(lua_State*s,const SPDBPerk*p', '''void Scope::CommitItems(){for(const auto&item:itemRegeneration)item.first->SetManaRegenRate(item.second);itemRegeneration.clear();}
void PushEffects(lua_State*s,float*p,int count,const std::shared_ptr<Scope>&scope){PushRef(s,p,ReadOnlyFloats,scope,count);}
void Push(lua_State*s,const SPDBPerk*p''')
edit('Game/ModPackage.h','    void RecalcInternalHeroStats', '''    // Initial stats + complete spell/custom stage; full armor/final recalc remains separate.
    void PrepareUnitStats(SRPGUnitStats&,SRPGCommonStats&,const CUnit&,const std::string& prototype);
    void RecalcInternalHeroStats''')
edit('Game/ModPackage.cpp','void CModPackage::RecalcInternalHeroStats', '''void CModPackage::PrepareUnitStats(SRPGUnitStats&stats,SRPGCommonStats&common,const CUnit&unit,const std::string&prototype){
 CallbackFrame frame(state);SRPGUnitStats staged=stats;SRPGCommonStats stagedCommon=common;
 frame.scope->unit=&unit;frame.scope->stagedStats=&staged;
 float effects[53]={};for(int i=0;i<unit.GetSpellEffects().GetSize();i++){const auto&e=unit.GetSpellEffects()[i];int id=e.GetEffectType();float power=e.GetPower();if(id>=0&&id<53){if(!std::isfinite(power))throw std::runtime_error("Non-finite spell effect");effects[id]=(std::max)(effects[id],power);}}
 BeginCallback("RecalcInitialJmvStatsExt");ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&stagedCommon,frame.scope);ModLua::Push(state,&unit,frame.scope);lua_pushstring(state,prototype.c_str());Call(4,0);
 BeginCallback("ApplySpellEffects");ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&stagedCommon,frame.scope);ModLua::PushEffects(state,effects,53,frame.scope);
 if(unit.GetID()>=1000000000u&&unit.GetID()<2000000000u)lua_pushnil(state);
 else{BeginCallback("GetUnitPerks");ModLua::Push(state,&unit,frame.scope);lua_pushstring(state,prototype.c_str());lua_getglobal(state,"UnitPerkType");lua_getfield(state,-1,"Trickster");lua_remove(state,-2);Call(3,1);}
 Call(4,0);
 BeginCallback("ApplyCustomEffects");ModLua::Push(state,&unit,frame.scope);ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&stagedCommon,frame.scope);ModLua::PushEffects(state,effects,53,frame.scope);ModLua::Push(state,&unit,frame.scope);Call(5,0);
 frame.scope->CommitItems();stats=staged;common=stagedCommon;
}
void CModPackage::RecalcInternalHeroStats''')
# Existing engine header provides spell effects; use its complete CUnit declaration.
if '#include "Unit.h"' not in (r/'Game/ModPackage.cpp').read_text():edit('Game/ModPackage.cpp','#include "ModPackage.h"','#include "ModPackage.h"\n#include "Unit.h"')
