from pathlib import Path
import shutil
r=Path(__file__).resolve().parent;e=r/'evidence/addon-integration/pre-r19';e.mkdir(exist_ok=True)
def edit(name,old,new):
 p=r/name;b=p.read_bytes();o=old.replace('\n','\r\n').encode();n=new.replace('\n','\r\n').encode()
 if o not in b:o=old.encode();n=new.encode()
 assert b.count(o)==1,(name,old[:80],b.count(o))
 backup=e/name
 if not backup.exists():backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,backup)
 p.write_bytes(b.replace(o,n))
edit('Game/ModPackage.h','struct SPDBPerk;','struct SPDBPerk;\nstruct SRPGUserUnitStats;\nstruct SShopSet;')
edit('Game/ModPackage.h','    nlohmann::json Describe();','''    float ExperienceMultiplier(const SRPGUnitStats&);
    float ExperienceMultiplier(const SRPGUserUnitStats&);
    float ManaBonus(const SRPGUnitStats&);
    float ManaBonus(const SRPGUserUnitStats&);
    float DistributeExperience(float experience,int players);
    void RecalcShops(SShopSet&,bool lmp);
    nlohmann::json CheckEconomy();
    nlohmann::json Describe();''')
edit('Game/ModLuaBridge.h','struct SRPGCommonStats;','struct SRPGCommonStats;\nstruct SRPGUserUnitStats;')
edit('Game/ModLuaBridge.h','void Register(lua_State *state);','void PushFloats(lua_State*,float*,int,const std::shared_ptr<Scope>&);\nvoid Push(lua_State*,const SRPGUserUnitStats*,const std::shared_ptr<Scope>&);\nvoid Register(lua_State *state);')
edit('Game/ModLuaBridge.cpp','ArmorLayer,ReadOnlyFloats};','ArmorLayer,ReadOnlyFloats,ClientStats};')
edit('Game/ModLuaBridge.cpp','if(r.kind!=ReadOnlyFloats || luaL_checkint(s,arg+2)!=r.count || !lua_toboolean(s,arg+3))return luaL_error(s,"Expected exact read-only native effect array");','if((r.kind!=ReadOnlyFloats&&r.kind!=Floats) || luaL_checkint(s,arg+2)!=r.count || (lua_toboolean(s,arg+3)!=0)!=(r.kind==ReadOnlyFloats))return luaL_error(s,"Expected exact native array size and access mode");')
edit('Game/ModLuaBridge.cpp',' if(r.kind==Unit&&!write){auto*p=(CUnit*)r.object;',' if(r.kind==ClientStats&&!write&&strcmp(k,"intelligence")==0){lua_pushinteger(s,((const SRPGUserUnitStats*)r.object)->cIntelligence);return 1;}\n if(r.kind==Unit&&!write){auto*p=(CUnit*)r.object;')
edit('Game/ModLuaBridge.cpp','{"UnitServerStats",Stats},','{"UnitClientStats",ClientStats},{"UnitServerStats",Stats},')
edit('Game/ModLuaBridge.cpp','void Register(lua_State*s){','void PushFloats(lua_State*s,float*p,int count,const std::shared_ptr<Scope>&scope){PushRef(s,p,Floats,scope,count);}\nvoid Push(lua_State*s,const SRPGUserUnitStats*p,const std::shared_ptr<Scope>&scope){PushRef(s,const_cast<SRPGUserUnitStats*>(p),ClientStats,scope);}\nvoid Register(lua_State*s){')
edit('Game/RPGSystem.h','    float GetExpMultiplier() const { return (float)pow(1.04, GetIntelligenceBonus()); }','    float GetExpMultiplier() const;')
edit('Game/RPGSystem.h','    float GetExpMultiplier() const { return static_cast<float>(pow(1.04, GetIntelligenceBonus())); }','    float GetExpMultiplier() const;')
edit('Game/RPGSystem.cpp','float SRPGUnitStats::GetManaBonusMultiplier() const\n{','float SRPGUnitStats::GetExpMultiplier() const { if(auto*mod=ModRuntime::Current())return mod->ExperienceMultiplier(*this);return float(pow(1.04,GetIntelligenceBonus())); }\nfloat SRPGUserUnitStats::GetExpMultiplier() const { if(auto*mod=ModRuntime::Current())return mod->ExperienceMultiplier(*this);return float(pow(1.04,GetIntelligenceBonus())); }\nfloat SRPGUnitStats::GetManaBonusMultiplier() const\n{\n    if(auto*mod=ModRuntime::Current())return mod->ManaBonus(*this);')
edit('Game/RPGSystem.cpp','float SRPGUserUnitStats::GetManaBonusMultiplier() const\n{','float SRPGUserUnitStats::GetManaBonusMultiplier() const\n{\n    if(auto*mod=ModRuntime::Current())return mod->ManaBonus(*this);')
for name in ['Game/ScenarioTracker.cpp','Game/WorldServer.cpp']:
 edit(name,'#include "StdAfx.h"','#include "StdAfx.h"\n#include "ModRuntime.h"\n#include "ModPackage.h"')
edit('Game/ScenarioTracker.cpp','// SHatUser','// SHatUser') # capture original before targeted function insertion
p=r/'Game/ScenarioTracker.cpp';b=p.read_bytes();start=b.index(b'void SShopSet::NewGame(bool bLMP)');end=b.index(b'// SHatUser',start);pos=b.rfind(b'\r\n}',start,end);assert pos>start
b=b[:pos]+b'\r\n    if(auto*mod=ModRuntime::Current())mod->RecalcShops(*this,bLMP);'+b[pos:];p.write_bytes(b)
edit('Game/WorldServer.cpp','            ue.fpExp = fpExp * sqrt(ue.pUS->GetRPGStats()->GetRealExp()) / fpTotalExp;','            ue.fpExp = ModRuntime::Current() ? ModRuntime::Current()->DistributeExperience(fpExp,nDiv)\n                : fpExp * sqrt(ue.pUS->GetRPGStats()->GetRealExp()) / fpTotalExp;')
edit('Game/ModPackage.cpp','#include "StdAfx.h"','#include "StdAfx.h"\n#include "ScenarioTracker.h"')
edit('Game/ModPackage.cpp','bool InspectModPackageCommandLine(int &result){','''float CModPackage::ExperienceMultiplier(const SRPGUnitStats&stats){return StatsNumber("GetExpMultiplierOnServer",stats);}
float CModPackage::ExperienceMultiplier(const SRPGUserUnitStats&stats){CallbackFrame frame(state);BeginCallback("GetExpMultiplierOnClient");ModLua::Push(state,&stats,frame.scope);Call(1,1);double n=Number(state,"Client experience multiplier");if(fabs(n)>std::numeric_limits<float>::max())throw std::runtime_error("Experience multiplier overflow");return float(n);}
float CModPackage::ManaBonus(const SRPGUnitStats&stats){return StatsNumber("GetManaBonusOnServer",stats);}
float CModPackage::ManaBonus(const SRPGUserUnitStats&stats){CallbackFrame frame(state);BeginCallback("GetManaBonusOnClient");ModLua::Push(state,&stats,frame.scope);Call(1,1);double n=Number(state,"Client mana multiplier");if(fabs(n)>std::numeric_limits<float>::max())throw std::runtime_error("Mana multiplier overflow");return float(n);}
float CModPackage::DistributeExperience(float experience,int players){
 if(!std::isfinite(experience)||players<=0)throw std::runtime_error("Invalid LMP experience input");
 CallbackFrame frame(state);BeginCallback("DistributeLmpExp");lua_pushnumber(state,experience);lua_pushinteger(state,players);Call(2,1);double n=Number(state,"LMP experience");if(fabs(n)>std::numeric_limits<float>::max())throw std::runtime_error("LMP experience overflow");return float(n);
}
void CModPackage::RecalcShops(SShopSet&shops,bool lmp){
 const size_t expected=lmp?4:5;if(shops.shops.size()!=expected)throw std::runtime_error("Lua shop layout expects four LMP or five campaign shops");
 std::vector<float> values;values.reserve(expected*9);for(auto&shop:shops.shops)for(int j=0;j<9;j++)values.push_back(shop->fKoeffs[j]);
 CallbackFrame frame(state);BeginCallback("RecalcShopsPriceMultipliers");ModLua::PushFloats(state,values.data(),int(values.size()),frame.scope);lua_pushboolean(state,lmp);Call(2,0);
 for(float value:values)if(!std::isfinite(value))throw std::runtime_error("Invalid shop multiplier");
 size_t index=0;for(auto&shop:shops.shops)for(int j=0;j<9;j++)shop->fKoeffs[j]=values[index++];
}
bool InspectModPackageCommandLine(int &result){''')
edit('Game/ModPackage.cpp',' bool startup=argc>=2&&wcscmp(argv[1],L"--inspect-mod-startup")==0;',' bool economy=argc>=2&&wcscmp(argv[1],L"--inspect-mod-economy")==0;\n bool startup=argc>=2&&wcscmp(argv[1],L"--inspect-mod-startup")==0;')
edit('Game/ModPackage.cpp','(!startup&&wcscmp(argv[1],L"--inspect-addon")!=0)','(!economy&&!startup&&wcscmp(argv[1],L"--inspect-addon")!=0)')
edit('Game/ModPackage.cpp','try{if(startup)report=CheckStartup(Ansi(argv[2]));else','try{if(economy){CModPackage package;package.Load(Ansi(argv[2]));report=package.CheckEconomy();}else if(startup)report=CheckStartup(Ansi(argv[2]));else')
