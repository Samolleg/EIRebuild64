#pragma once
#include "ObjectIdentity.h"
#include <memory>
#include <map>
#include <nlohmann/json.hpp>
struct lua_State;
struct SRPGUnitStats;
struct SRPGCommonStats;
struct SRPGUserUnitStats;
class CUnit;
class CItem;
class CItemWeapon;
class CSpell;
struct SAttackParams;
struct SPDBPerk;
namespace ModLua {
// Owned by a finite CModPackage; Lua registry and perk handles hold weak references only.
struct PerkHostLifetime {};
void BindPerkDatabase(lua_State*,const std::shared_ptr<PerkHostLifetime>&);
struct Scope {
 bool active=true;
 const CUnit *unit=nullptr;
 SRPGUnitStats *stagedStats=nullptr;
 std::map<CItem*,float,ObjectIdLess<CItem>> itemRegeneration;
 std::map<CItem*,float,ObjectIdLess<CItem>> itemMana;
 std::map<CItem*,int,ObjectIdLess<CItem>> itemCounts;
 std::map<std::pair<const CUnit*,int>,std::shared_ptr<CItemWeapon>> weaponViews;
 CItemWeapon* Weapon(const CUnit&,int);
 int LeftHand(const CUnit&);
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
void Push(lua_State*,const CSpell*,const std::shared_ptr<Scope>&);
void Push(lua_State*,SAttackParams*,const std::shared_ptr<Scope>&);
void Push(lua_State*,const SPDBPerk*,const std::shared_ptr<Scope>&);
nlohmann::json CheckNativeBridge(lua_State*);
}
