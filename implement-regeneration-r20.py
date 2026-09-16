from pathlib import Path
import shutil
r=Path(__file__).resolve().parent
def edit(name,old,new):
 p=r/name;b=p.read_bytes();old=old.replace('\n','\r\n').encode();new=new.replace('\n','\r\n').encode()
 if old not in b:old=old.replace(b'\r\n',b'\n');new=new.replace(b'\r\n',b'\n')
 assert b.count(old)==1,(name,old[:70],b.count(old))
 backup=r/'evidence/addon-integration/pre-r20'/name
 if not backup.exists():backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,backup)
 p.write_bytes(b.replace(old,new))
edit('Game/ModPackage.h','    void ResetRuntimeState();','    bool Regenerate(SRPGCommonStats&,SRPGUnitStats&,CUnit*,bool hpOnly);\n    void ResetRuntimeState();')
edit('Game/ModPackage.cpp','#include "Player.h"','#include "Player.h"\n#include "PlayerServer.h"\n#include "UnitServer.h"')
edit('Game/RPGSystem.cpp','bool SRPGCommonStats::Regenerate(SRPGUnitStats &rpgUnitStats, CUnit *pUnit, bool bHPOnly)\n{','bool SRPGCommonStats::Regenerate(SRPGUnitStats &rpgUnitStats, CUnit *pUnit, bool bHPOnly)\n{\n    if(auto*mod=ModRuntime::Current())return mod->Regenerate(*this,rpgUnitStats,pUnit,bHPOnly);')
edit('Game/ModPackage.cpp','void CModPackage::ResetRuntimeState()', '''bool CModPackage::Regenerate(SRPGCommonStats&common,SRPGUnitStats&stats,CUnit*unit,bool hpOnly){
 if(!unit)throw std::runtime_error("Mod regeneration requires a native unit");
 if(common.ssHP.fpCurrent<=0)return false;
 // The same serialized counter and unsigned increment as RegenerationSchedule.h.
 unsigned next=static_cast<unsigned>(common.nRegenerateCounter)+1u;
 if(next>0&&next<15){common.nRegenerateCounter=static_cast<int>(next);return false;}
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
 // Retain the native full-recalc caller until initialization proof/observers are ported.
 return changed;
}
void CModPackage::ResetRuntimeState()''')
edit('Game/ModLuaBridge.h',' std::map<CItem*,float> itemRegeneration;',' std::map<CItem*,float> itemRegeneration;\n std::map<CItem*,float> itemMana;')
edit('Game/ModLuaBridge.cpp','#include "Item.h"','#include "Item.h"\n#include "SpellSystem.h"')
edit('Game/ModLuaBridge.cpp','void Scope::CommitItems(){for(const auto&item:itemRegeneration)item.first->SetManaRegenRate(item.second);itemRegeneration.clear();}','void Scope::CommitItems(){for(const auto&item:itemRegeneration)item.first->SetManaRegenRate(item.second);for(const auto&item:itemMana)item.first->SetMP(item.second);itemRegeneration.clear();itemMana.clear();}')
edit('Game/ModLuaBridge.cpp','int Access(lua_State*s,bool write)', '''int Heal(lua_State*s){
 auto&r=Get(s);auto&c=Get(s,2);double value=luaL_checknumber(s,3);
 if(r.kind!=Stats||c.kind!=Common||r.scope!=c.scope||!std::isfinite(value)||fabs(value)>(std::numeric_limits<float>::max)())return luaL_error(s,"Invalid native heal arguments");
 lua_pushboolean(s,((SRPGUnitStats*)r.object)->Heal((SRPGCommonStats*)c.object,float(value)));return 1;
}
int RegenMp(lua_State*s){auto&r=Get(s);double value=luaL_checknumber(s,2);if(r.kind!=Common||!std::isfinite(value)||fabs(value)>(std::numeric_limits<float>::max)())return luaL_error(s,"Invalid native MP regeneration");lua_pushboolean(s,((SRPGCommonStats*)r.object)->RegenerateMP(float(value)));return 1;}
int Access(lua_State*s,bool write)''')
edit('Game/ModLuaBridge.cpp',' if(r.kind==Common){auto*p=', ' if(!write&&r.kind==Stats&&strcmp(k,"heal")==0){lua_pushcfunction(s,Heal);return 1;}\n if(!write&&r.kind==Common&&strcmp(k,"regenMp")==0){lua_pushcfunction(s,RegenMp);return 1;}\n if(r.kind==Common){auto*p=')
edit('Game/ModLuaBridge.cpp',' if(r.kind==Item){auto*p=(CItem*)r.object;', ''' if(r.kind==Item){auto*p=(CItem*)r.object;
 if(strcmp(k,"mp")==0){auto it=r.scope->itemMana.find(p);float value=it==r.scope->itemMana.end()?p->GetMP():it->second;int result=Field(s,value,write);if(write)r.scope->itemMana[p]=value;return result;}
 if(!write&&strcmp(k,"spell")==0){auto*spell=p->GetSpell(0);if(!spell)lua_pushnil(s);else{lua_newtable(s);lua_pushnumber(s,spell->GetManaCost());lua_setfield(s,-2,"manaCost");}return 1;}''')
edit('Game/ModLuaBridge.cpp','if(strcmp(k,"mp")==0){float v=p->GetMP();Field(s,v,true);p->SetMP(v);return 0;}','')
edit('Game/ModLuaBridge.cpp','I("mp",p->GetMP());','')
print('R20 regeneration core and transactional item bridge written')
