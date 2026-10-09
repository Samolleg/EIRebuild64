#pragma once
#include <array>
#include <cmath>
#include <cstring>
#include <ostream>
#include <string>

// Independent of engine ABI packing; never stored in a save or engine object.
#pragma pack(push, 8)
namespace ModInfoStats {
constexpr bool diagnostic = false;
enum Kind { Damage, CritChance, CritDamage, KindCount };
struct Effects {
    bool weaponNil = true, paramsWritten = false;
    double weapon = 0;
    std::array<double, 3> params{};
    bool operator==(const Effects& b) const {
        return weaponNil == b.weaponNil && (weaponNil || weapon == b.weapon) &&
            paramsWritten == b.paramsWritten && (!paramsWritten || params == b.params);
    }
};
struct Entry {
    std::string key;
    float value = 0;
    bool valid = false, checked = false;
    Effects effects;
};
struct Counters {
    unsigned long long requests = 0, hits = 0, misses = 0, bypasses = 0;
    unsigned long long checks = 0, mismatches = 0, errors = 0, evictions = 0;
};
struct Cache {
    std::array<Entry, 128> entries;
    std::array<Counters, KindCount> counts;
    size_t cursor = 0;
    bool failed = false;
    unsigned epoch = 0;
    unsigned long long resets = 0;
    void clear() {
        for (auto& e : entries) { e.valid = false; e.key.clear(); }
        cursor = 0; ++epoch; if (diagnostic) ++resets;
    }
    Entry* find(const std::string& key) {
        for (auto& e : entries) if (e.valid && e.key == key) return &e;
        return nullptr;
    }
    void store(const std::string& key, float value, Kind kind, const Effects& effects) {
        if (!std::isfinite(value) || failed) return;
        auto& e = entries[cursor];
        if (diagnostic && e.valid) ++counts[kind].evictions;
        e = {key, value, true, false, effects}; cursor = (cursor + 1) % entries.size();
    }
    template<class F> float get(Kind kind, const std::string& key, bool eligible, Effects& effects, F calculate) {
        auto& c = counts[kind]; if (diagnostic) ++c.requests;
        if (!eligible || failed) { if (diagnostic) ++c.bypasses; bool ok = false; return calculate(ok); }
        auto* saved = find(key);
        if (saved) {
            if (diagnostic) ++c.hits;
            const float value = saved->value;
            effects = saved->effects;
            const Effects expectedEffects = effects;
            if (!diagnostic || (saved->checked && c.hits % 1024 != 0)) return value;
            saved->checked = true; if (diagnostic) ++c.checks;
            const unsigned before = epoch;
            bool ok = false; const float reference = calculate(ok);
            if (!ok || !std::isfinite(reference)) { if (diagnostic) ++c.errors; clear(); return reference; }
            if (memcmp(&value, &reference, sizeof(value)) || !(effects == expectedEffects)) {
                if (diagnostic) ++c.mismatches; failed = true; clear(); return reference;
            }
            // A nested callback may have invalidated the world/cache.
            if (before != epoch) return reference;
            return value;
        }
        if (diagnostic) ++c.misses;
        const unsigned before = epoch;
        bool ok = false; const float result = calculate(ok);
        if (!ok || !std::isfinite(result)) { if (diagnostic) ++c.errors; clear(); }
        else if (before == epoch) store(key, result, kind, effects);
        return result;
    }
    void append(std::ostream& s) const {
        static const char* names[] = {"damage", "crit_chance", "crit_damage"};
        s << "info_stats resets=" << resets << " failed=" << failed << " diagnostic=" << diagnostic << '\n';
        for (int i = 0; i < KindCount; ++i) {
            const auto& c = counts[i];
            s << "info_stats_" << names[i] << " requests=" << c.requests << " hits=" << c.hits
              << " avoided=" << (c.hits - c.checks) << " misses=" << c.misses << " bypasses=" << c.bypasses << " checks=" << c.checks
              << " mismatches=" << c.mismatches << " errors=" << c.errors << " evictions=" << c.evictions << '\n';
        }
    }
};
}
#pragma pack(pop)


// Typed adapter: no engine-layout copy and no retained item/Scope ownership.
#include <vector>
#include <utility>
#include "ModLuaBridge.h"
#include "luajit-x86/lua.hpp"
#include "Unit.h"
#include "Item.h"
#include "ItemDatabase.h"
#include "PerkDatabase.h"
#include "GeneralSettings.h"
#include "WorldServer.h"
#pragma pack(push, 8)
namespace ModInfoStats {
template<class T> inline void KeyValue(std::string& key,const T& value) {
 key.append(reinterpret_cast<const char*>(&value),sizeof(value));
}
inline void KeyString(std::string& key,const char* value) {
 const std::string text=value?value:"";KeyValue(key,text.size());key+=text;
}
inline void DamageInputs(std::string& key,const SRPGUserUnitStats& stats) {
 KeyValue(key,stats.cStrength);KeyValue(key,stats.cDexterity);KeyValue(key,stats.cIntelligence);
 KeyValue(key,stats.wEncumbrance);KeyValue(key,stats.cPerks);KeyValue(key,stats.cAutoSkills);
}
inline void ItemKey(std::string& key,const CItem* item,bool transient=false) {
 // Ownerless client weapon views are temporary: key their subtype, not heap address.
 const bool present=item!=nullptr;KeyValue(key,present);
 if(item){if(!transient){KeyValue(key,item);KeyValue(key,item->GetID());}KeyValue(key,item->GetSubtypeID());}
}
struct Context {
 const CUnit* unit=nullptr;std::array<std::string,6> parts;std::string key;bool valid=false;
};
struct State {
 Cache cache;std::vector<Context> contexts;unsigned depth=0,worldEpoch=0;
 const void* client=nullptr;const void* server=nullptr;std::string zone;int lastTick=-1;
 bool contract=false,statefulDamage=false;
 std::array<const void*,KindCount> functions{};
 void Invalidate(){cache.clear();contexts.clear();}
 void Begin(unsigned epoch,const void* nextClient,const void* nextServer,const char* nextZone,int tick){
  if(depth++!=0)return;contexts.clear();
  const char* name=nextZone?nextZone:"";
  // Observe the native epoch inputs before UI use without resetting gameplay owners.
  if(worldEpoch!=epoch||client!=nextClient||server!=nextServer||zone!=name||(lastTick>=0&&tick<lastTick)){
   cache.clear();worldEpoch=epoch;client=nextClient;server=nextServer;zone=name;
  }
  lastTick=tick;
 }
 void End(){if(depth>0)--depth;}
 void Initialize(lua_State* s){
  *this=State();
  lua_getglobal(s,"JmvInfoStatsCacheContract");contract=lua_isnumber(s,-1)&&lua_tointeger(s,-1)==1;lua_pop(s,1);
  lua_getglobal(s,"JmvInfoStatsCacheStatefulDamage");statefulDamage=lua_toboolean(s,-1)!=0;lua_pop(s,1);
  const char* names[]={"GetModifiedDamageOnClientJmv","GetCritOnClientJmv","GetCritDamageOnClientJmv"};
  for(int i=0;i<KindCount;++i){lua_getglobal(s,names[i]);functions[i]=lua_isfunction(s,-1)?lua_topointer(s,-1):nullptr;lua_pop(s,1);}
 }
 const Context& Capture(CUnit& unit,ModLua::Scope& scope){
  for(const auto& c:contexts)if(c.unit==&unit)return c;
  Context c;c.unit=&unit;auto* specific=unit.GetUnitSpecific();
  if(specific&&unit.GetPrototype()&&theWorldServer.IsMapLoaded()){
   auto& id=c.parts[0];const CUnit* pointer=&unit;
   KeyValue(id,pointer);KeyValue(id,specific);KeyValue(id,unit.GetID());
   KeyValue(id,unit.GetSubtypeID());KeyValue(id,unit.GetTypeID());KeyString(id,unit.GetPrototype()->szName);
   auto* items=&theItemDatabase;auto* perks=&thePerkDatabase;KeyValue(id,items);KeyValue(id,perks);
   const auto& stats=*unit.GetUserUnitStats();DamageInputs(c.parts[1],stats);
   for(const auto& h:stats.hls){KeyValue(c.parts[1],h.hlt);KeyValue(c.parts[1],h.wHP);KeyValue(c.parts[1],h.wMaxHP);}
   auto& gear=c.parts[2];KeyValue(gear,unit.GetCurrentWeaponSlotIndex());
   const bool transient=specific->WhoAmI()==HPC_UNITCLIENTSPECIFIC&&!unit.GetOwner();
   for(int i=0;i<QI_MAX_WEAPONS;++i)ItemKey(gear,scope.Weapon(unit,i),transient);
   for(int i=0;i<RPG_NUM_ARMOR_TYPES;++i)ItemKey(gear,unit.GetQArmor(i));
   for(int i=0;i<QI_MAX_ITEMS;++i)ItemKey(gear,transient?nullptr:unit.GetQItem(i));
   const float strengthen=unit.GetEffectValue(29),weaken=unit.GetEffectValue(30);
   KeyValue(c.parts[3],strengthen);KeyValue(c.parts[3],weaken);
   float weight=unit.GetItemsWeight();
   if(!transient)for(DWORD i=0;i<unit.GetSpellSlotCount();++i)if(unit.GetQSpellItem(i))weight+=1;
   KeyValue(c.parts[4],weight);KeyValue(c.parts[5],theSettings.nDifficultyLevel);
   for(const auto& p:c.parts){KeyValue(c.key,p.size());c.key+=p;}
   c.valid=std::isfinite(weight)&&std::isfinite(strengthen)&&std::isfinite(weaken);
  }
  contexts.push_back(std::move(c));return contexts.back();
 }
};
inline bool WeaponGlobal(lua_State* s,Effects& effects){
 lua_getglobal(s,"weapon_type");effects.weaponNil=lua_isnil(s,-1)!=0;
 const bool valid=effects.weaponNil||lua_type(s,-1)==LUA_TNUMBER;
 effects.weapon=effects.weaponNil?0:lua_tonumber(s,-1);lua_pop(s,1);return valid&&std::isfinite(effects.weapon);
}
inline const void* ParamsGlobal(lua_State* s){lua_getglobal(s,"params_mod");const void* p=lua_topointer(s,-1);lua_pop(s,1);return p;}
inline bool CaptureEffects(lua_State* s,const void* before,Effects& effects){
 if(!WeaponGlobal(s,effects))return false;
 lua_getglobal(s,"params_mod");effects.paramsWritten=lua_topointer(s,-1)!=before;bool valid=true;
 if(effects.paramsWritten){valid=lua_type(s,-1)==LUA_TTABLE&&lua_objlen(s,-1)==3;
  if(valid)for(int i=0;i<3;++i){lua_rawgeti(s,-1,i+1);effects.params[i]=lua_tonumber(s,-1);
   valid=valid&&lua_type(s,-1)==LUA_TNUMBER&&std::isfinite(effects.params[i]);lua_pop(s,1);}}
 lua_pop(s,1);return valid;
}
inline void ReplayEffects(lua_State* s,const Effects& effects){
 if(effects.weaponNil)lua_pushnil(s);else lua_pushnumber(s,effects.weapon);lua_setglobal(s,"weapon_type");
 if(effects.paramsWritten){lua_createtable(s,3,0);for(int i=0;i<3;++i){lua_pushnumber(s,effects.params[i]);lua_rawseti(s,-2,i+1);}lua_setglobal(s,"params_mod");}
}
template<class F> float Call(State& owner,lua_State* s,Kind kind,CUnit& unit,
 ModLua::Scope& scope,const std::string& args,bool eligible,F calculate){
 if(owner.depth!=1)return calculate();
 const char* names[]={"GetModifiedDamageOnClientJmv","GetCritOnClientJmv","GetCritDamageOnClientJmv"};
 lua_getglobal(s,names[kind]);const bool known=lua_isfunction(s,-1)&&lua_topointer(s,-1)==owner.functions[kind];lua_pop(s,1);
 eligible=eligible&&owner.contract&&known&&!(kind==Damage&&owner.statefulDamage);
 std::string key;Effects effects;if(eligible&&!WeaponGlobal(s,effects))eligible=false;
 if(eligible){const auto& c=owner.Capture(unit,scope);eligible=c.valid;
  KeyValue(key,kind);KeyValue(key,owner.worldEpoch);KeyValue(key,owner.cache.epoch);
  if(kind==Damage){const auto* stats=unit.GetUserUnitStats();KeyValue(key,stats);DamageInputs(key,*stats);key+=c.key;}
  else {KeyValue(key,c.parts[0].size());key+=c.parts[0];KeyValue(key,c.parts[2].size());key+=c.parts[2];}
  key+=args;KeyValue(key,effects.weaponNil);KeyValue(key,effects.weapon);}
 bool called=false;
 try{
  const float result=owner.cache.get(kind,key,eligible,effects,[&](bool& ok){
   called=true;const void* before=eligible?ParamsGlobal(s):nullptr;
   const float value=calculate();ok=true;if(eligible)ok=CaptureEffects(s,before,effects);return value;
  });
  if(!called)ReplayEffects(s,effects);return result;
 }catch(...){owner.Invalidate();throw;}
}
}
#pragma pack(pop)
