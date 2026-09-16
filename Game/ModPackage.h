#pragma once
#include "ModNpcConsumables.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
struct lua_State;
struct ModNpcRefreshQueue;
namespace ModInitialization {struct Ledger;}
class CMappedResource;
class CUnit;
struct SRPGUnitStats;
struct SRPGCommonStats;
struct SPDBPerk;
struct SRPGUserUnitStats;
struct SShopSet;
// Engine-owned package data; no original EXE addresses or addon DLL loading.
class CModPackage {
public:
    CModPackage();
    ~CModPackage();
    CModPackage(const CModPackage&)=delete;
    CModPackage& operator=(const CModPackage&)=delete;
    bool RegisteredLootEnabled() const { return registeredLoot; }
    const std::string& Directory() const { return root; }
    bool GetNpcCombatConsumableConfig(const char*zone,int id,const char*prototype,int kind,int variant,ModConsumableConfig&);
    void Load(const std::string &directory);
    std::string ResolveConfig(const std::string &name,const std::string &fallback) const;
    std::string ResolveMap(const std::string &relative,const std::string &fallback) const;
    nlohmann::json Lookup(const std::string &table,const nlohmann::json &key,int column,const nlohmann::json &fallback);
    std::vector<unsigned char> ReadResource(const std::string &category,const std::string &entry,const std::string &base,std::string &owner);
    bool ResourcePlan(const std::string&,std::vector<std::string>&,bool&) const;
    void InitializeScript();
    // Initial stats + complete spell/custom stage; full armor/final recalc remains separate.
    bool RecalcUnitStats(SRPGUnitStats&,SRPGCommonStats&,CUnit&);
    bool Regenerate(SRPGCommonStats&,SRPGUnitStats&,CUnit*,bool hpOnly);
    void ResetRuntimeState();
    void TickStatRefresh();
    bool RefreshUnitStatsFromMob(CUnit*);
    void SuccessfulWorldLoad();
    bool SuppressLegacyRefreshSpell(const char*) const;
    void PrepareUnitStats(SRPGUnitStats&,SRPGCommonStats&,const CUnit&,const std::string& prototype);
    void RecalcInternalHeroStats(SRPGUnitStats&, float itemsWeight);
    float GetModifiedHealth(SRPGUnitStats&, const CUnit&, float health);
    float CalcBaseHealth(const SRPGUnitStats&);
    float CalcBaseMana(const SRPGUnitStats&);
    int GetSkillPriceDiff(int from, int to);
    int GetPerkPrice(const SRPGUnitStats&, const SPDBPerk&);
    void CalcRunningManaCost(SRPGUnitStats&, CUnit&, SRPGCommonStats&);
    float ExperienceMultiplier(const SRPGUnitStats&);
    float ExperienceMultiplier(const SRPGUserUnitStats&);
    float ManaBonus(const SRPGUnitStats&);
    float ManaBonus(const SRPGUserUnitStats&);
    float DistributeExperience(float experience,int players);
    void RecalcShops(SShopSet&,bool lmp);
    nlohmann::json CheckEconomy();
    nlohmann::json Describe();
private:
    void Call(int args,int results);
    float StatsNumber(const char*,const SRPGUnitStats&);
    void BeginCallback(const char*);
    void Require(const char *name);
    bool registeredLoot=false;
    std::string root;
    std::map<std::string,std::vector<std::string>> archives;
    std::map<std::string,bool> ignoreBase;
    std::map<std::string,std::string> configs,maps;
    std::map<std::string,std::unique_ptr<CMappedResource>> opened;
    lua_State *state=nullptr;
    int lookupRef=-1,indexRef=-1;
    std::string mainScript;
    bool scriptInitialized=false;
    std::unique_ptr<ModNpcRefreshQueue> refreshQueue;
    bool npcStatRefresh=true;
    void InitializeLoadedHeroes();
    bool npcArmorCalculation=true,npcTrickster=true,npcMaterialEffects=true;
    struct ArmorModifiers {unsigned subtype=0;float speed=0,sight=0,hearing=0;};
    std::map<int,ArmorModifiers> npcArmorCache;
    const void* cacheWorld=nullptr;std::string cacheZone;int cacheSegment=-1;
    void SyncRuntimeState();
    std::unique_ptr<ModInitialization::Ledger> initialization;
    bool CollectArmor(CUnit&,ArmorModifiers&);
    CMappedResource &Open(const std::string &path);
};
// Returns false for normal launches. Diagnostic mode never creates a window.
bool InspectModPackageCommandLine(int &result);

