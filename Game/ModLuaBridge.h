#pragma once
#include <memory>
#include <map>
#include <nlohmann/json.hpp>
struct lua_State;
struct SRPGUnitStats;
struct SRPGCommonStats;
struct SRPGUserUnitStats;
class CUnit;
class CItem;
struct SPDBPerk;
namespace ModLua {
struct Scope {
 bool active=true;
 const CUnit *unit=nullptr;
 SRPGUnitStats *stagedStats=nullptr;
 std::map<CItem*,float> itemRegeneration;
 std::map<CItem*,float> itemMana;
 void CommitItems();
 ~Scope(){active=false;}
};
void PushEffects(lua_State*,float*,int,const std::shared_ptr<Scope>&);
void PushFloats(lua_State*,float*,int,const std::shared_ptr<Scope>&);
void Push(lua_State*,const SRPGUserUnitStats*,const std::shared_ptr<Scope>&);
void Register(lua_State *state);
void InstallFactories(lua_State *state);
void Push(lua_State*,SRPGUnitStats*,const std::shared_ptr<Scope>&);
void Push(lua_State*,SRPGCommonStats*,const std::shared_ptr<Scope>&);
void Push(lua_State*,const CUnit*,const std::shared_ptr<Scope>&);
void Push(lua_State*,CItem*,const std::shared_ptr<Scope>&);
void Push(lua_State*,const SPDBPerk*,const std::shared_ptr<Scope>&);
nlohmann::json CheckNativeBridge(lua_State*);
}
