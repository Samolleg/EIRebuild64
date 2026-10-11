#pragma once
#include "ModNpcConsumables.h"
#include "ModStaticLookupCache.h"
#include "SocialStealthRules.h"
#include "ModWeaponSkills.h"
#include "ModCampDescriptions.h"
#include "ModCustomLower.h"
#include "ModSettingsPolicy.h"
#include "ModSpellAddonConfig.h"
class CSpell;
class CFileRegistry;
bool InspectModConfigurationCommandLine(int& result);
namespace ModSpellTooltip { struct Values; }
class CParty;
class CPerkDatabase;
class CGSVariableSet;
class CItem;
class CMapObject;
struct ModDelayedShot;
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
struct lua_State;
struct ModNpcRefreshQueue;
namespace ModInitialization {struct Ledger;}
namespace ModInfoStats {struct State;}
namespace ModLua {struct PerkHostLifetime;}
class CMappedResource;
class CUnit;
class CItemWeapon;
struct SRPGUnitStats;
struct ModCampSpellLimitInput {const SRPGUnitStats* partyStats;int displayedIntelligence;};
struct SRPGCommonStats;
struct SPDBPerk;
struct SRPGUserUnitStats;
struct SShopSet;
struct SAttackParams;
struct SRPGAttackPortion;
namespace ModCombat { struct Defense; }
struct ModGenocideConfig {
    int population=0;
    int stageOnePercent=0;
    int stageTwoPercent=0;
    std::string resultVar;
    std::string eligibleGroup;
};
// Engine-owned package data; no original EXE addresses or addon DLL loading.
class CModPackage {
public:
    CModPackage();
    ~CModPackage();
    CModPackage(const CModPackage&)=delete;
    CModPackage& operator=(const CModPackage&)=delete;
    bool RegisteredLootEnabled() const { return registeredLoot; }
    bool SinglePlayerAllowed() const { return singlePlayerAllowed; }
    bool MultiplayerAllowed() const { return multiplayerAllowed; }
    bool CustomMpZones() const { return customMpZones; }
    bool MatchesMpZone(int allod, const char *terrain) const;
    bool ExtendActiveObjectsArea() const { return extendActiveObjectsArea; }
    bool BriefingWeaponsEnabled() const { return briefingWeapons; }
    bool WorldBackgroundEnabled() const { return worldBackground; }
    bool PartyVisualOptionsEnabled() const { return partyVisualOptions; }
    bool HidePartyHelmets() const { return partyVisualOptions && hidePartyHelmets; }
    bool HidePartyUpperArmor() const { return partyVisualOptions && hidePartyUpperArmor; }
    void SetPartyVisualOptions(bool helmets, bool upperArmor);
    bool IsLocalPartyUnit(const CUnit&) const;
    void FilterPartyVisualArmor(const CUnit&, unsigned long* armors) const;
    void ApplyPartyVisualOptions();
    const ModSpellAddon::Profile& SpellAddon() const { return spellAddon; }
    const std::string& Directory() const { return root; }
    bool GetNpcCombatConsumableConfig(const char*zone,int id,const char*prototype,int kind,int variant,ModConsumableConfig&);
    void Load(const std::string &directory);
    std::string ResolveConfig(const std::string &name,const std::string &fallback) const;
    std::string ResolveCamera(const std::string &name,const std::string &fallback) const;
    std::string ResolveMovie(const std::string &name,const std::string &fallback) const;
    std::string ResolveSaveRoot(const std::string &fallback) const;
    std::string ResolveMap(const std::string &relative,const std::string &fallback) const;
    float MurderAwarenessValue(int property,float fallback);
    nlohmann::json Lookup(const std::string &table,const nlohmann::json &key,int column,const nlohmann::json &fallback);
    std::vector<unsigned char> ReadResource(const std::string &category,const std::string &entry,const std::string &base,std::string &owner);
    bool ResourcePlan(const std::string&,std::vector<std::string>&,bool&) const;
    void InitializeScript();
    // Initial stats + complete spell/custom stage; full armor/final recalc remains separate.
    bool RecalcUnitStats(SRPGUnitStats&,SRPGCommonStats&,CUnit&);
    bool Regenerate(SRPGCommonStats&,SRPGUnitStats&,CUnit*,bool hpOnly);
    void ResetRuntimeState();
    void TickStatRefresh();
    void MigrateNpcEquipment();
    bool RefreshUnitStatsFromMob(CUnit*);
    void SuccessfulWorldLoad(WorldLoadOrigin origin);
    bool SuppressLegacyRefreshSpell(const char*) const;
    void PrepareUnitStats(SRPGUnitStats&,SRPGCommonStats&,const CUnit&,const std::string& prototype);
    void RecalcInternalHeroStats(SRPGUnitStats&, float itemsWeight);
    float GetModifiedHealth(SRPGUnitStats&, const CUnit&, float health);
    float CalcBaseHealth(const SRPGUnitStats&);
    float CalcBaseMana(const SRPGUnitStats&);
    void RedistributeResources(SRPGUnitStats&,SRPGCommonStats&,CUnit&);
    std::string GetQuickAdds(const char* prototype,int property);
    int GetSkillPriceDiff(int from, int to);
    int GetPerkPrice(const SRPGUnitStats&, const SPDBPerk&);
    void CalcRunningManaCost(SRPGUnitStats&, CUnit&, SRPGCommonStats&);
    float ExperienceMultiplier(const SRPGUnitStats&);
    float ExperienceMultiplier(const SRPGUserUnitStats&);
    float ExperienceLimit() const { return experienceLimit; }
    float MagicRandomLimit() const { return magicRandomLimit; }
    float ManaBonus(const SRPGUnitStats&);
    // IEEE multiplier only for SpellAddon f18/f29; never populates the strict cache.
    float SpellAddonManaBonus(const SRPGUnitStats&);
    float ManaBonus(const SRPGUserUnitStats&);
    float DistributeExperience(float experience,int players);
    float ChargeExperience(float exp,int count) const { return settingsProfile.Experience(exp,count); }
    int AutomaticSkillLimit(int index,bool nativeRedraw=false) const { return settingsProfile.SkillLimit(index,nativeRedraw); }
    bool ForceTutorial() const { return forceTutorial; }
    void LogChat(unsigned color,const char* nickname,const char* text) const;
    void SpellTooltipOverrides(CUnit* server,ModSpellTooltip::Values&,float& durationTicks);
    void RecalcShops(SShopSet&,bool lmp);
    int ShopConstructLimit(int shop);
    void CampSpellLimits(int shop,int subtype,const std::vector<ModCampSpellLimitInput>& party,int& complexity,int& mana);
    std::string LoadVideo(const char* zoneName);
    bool GenocideMapConfig(const char* mobName, ModGenocideConfig& config);
    ModCombat::Defense IncomingAttack(const SRPGUnitStats&,CUnit&,CUnit*,const SRPGAttackPortion&,int parryRoll);
    ModCombat::Defense IncomingCustomAttack0(const SRPGUnitStats&,CUnit&,CUnit*,const SRPGAttackPortion&);
    void RememberCustomAttack0(const SRPGUnitStats&,const CUnit&,const SRPGAttackPortion&,float roll);
    bool CustomCriticalHit(const SRPGAttackPortion&);
    void ClearCustomCriticals(){customCriticals.Clear();}
    int CustomLeft(const CUnit&) const;
    float ScaleCustomMagic0(CUnit&,const SRPGAttackPortion&,float);
    float ScaleCustomSingle0(CUnit&,ERPGHitLocations,float);
    void CompensateHitHistory(const SRPGUnitStats& defender,CUnit& victim,CUnit* attacker,SRPGAttackPortion&,int defense,bool directCustom);
    void RememberAttack(const SRPGUnitStats&,const CUnit&,const SRPGAttackPortion&,float roll,bool left=false);
    bool ConsumeCritical(const SRPGAttackPortion&);
    void MoveAttack(const SRPGAttackPortion&,const SRPGAttackPortion&);
    void ForgetAttack(const SRPGAttackPortion&);
    std::shared_ptr<CItemWeapon> AttackWeapon(const SRPGAttackPortion&) const;
    int AttackSlot(const CUnit&,bool left);
    bool AttackIsLeft(const SRPGAttackPortion&) const;
    std::string DualAnimation(const CUnit&);
    std::vector<std::pair<int,bool>> DualStrikes(const char*animation,int length);
    bool SkillConfig(const char*,ModSkills::Config&);
    bool PartyDisguiseMatches(const char*);
    bool SkillConfig(CSpell*,ModSkills::Config&);
    bool SkillReady(CUnit&,CSpell*,const ModSkills::Config&,bool server);
    float InfoProperty(const char* callback,const char* name,int property);
    bool InfoForbiddenSpell(const char* name, int shop);
    float InfoPerkBonus(const char* callback, int level);
    int InfoSetPosition(const char* name, int position, int property);
    float InfoComplexProperty(int set, int property, int position = -1);
    std::vector<std::pair<int,int>> InfoSetBonuses(const char* name, int count, int set, bool complex);
    float InfoMagicBlood(int level);
    ModSpellTooltip::Values SpellTooltip(const SRPGUserUnitStats&, const CSpell&, float manaIntelligence, int difficulty);
    void BeginInfoStats();
    void EndInfoStats();
    void InvalidateInfoStatsCache();
    struct InfoStatsScope {
        CModPackage* owner;
        explicit InfoStatsScope(CModPackage* package):owner(package){if(owner)owner->BeginInfoStats();}
        ~InfoStatsScope(){if(owner)owner->EndInfoStats();}
        InfoStatsScope(const InfoStatsScope&)=delete;
        InfoStatsScope& operator=(const InfoStatsScope&)=delete;
    };
    float PanelDamage(CUnit&,float damage,int slot,bool left);
    float PanelCritical(CUnit&,int slot,bool left,bool power);
    void PanelDefense(CUnit&,int weight,int capacity,float&block,float&parry);
    void IdleShieldValues(CUnit&,float&power,float&block,float&parry);
    ModCampDescriptions::Result EvaluateCampDescriptions(const ModCampDescriptions::Input&);
    ModCampDescriptions::Result CampDescriptions(class CPersonage&,CUnit&,bool skillCapacity=false);
    std::string CampMissingRequirement(const SRPGUnitStats&,const char* code);
    std::string ProgressionRequirementCode(const char* code,int field);
    float InfoComplex(CUnit&,int property);
    void SaveSkillCooldowns(CGSVariableSet&);
    void LoadSkillCooldowns();
    int SkillCooldown(CUnit&,CSpell*);
    void StartSkillCooldown(CUnit&,CSpell*,const ModSkills::Config&);
    bool TargetSkill(CSpell*) const;
    bool PointSkill(CSpell*) const;
    bool SkillItemBlocked(CItem*,int operation);
    bool SkillContainerBlocked(CParty*,int operation);
    void GrantSkillPrototypes(CParty&);
    bool CompanionSkillAI(CUnit&) const;
    int CrippledSpellId() const;
    bool CanCripple(CUnit&);
    bool ApplyCripple(CUnit&);
    float CrippledSpeed(const CUnit&) const;
    void MarkSkillImpact(const SRPGAttackPortion&,int);
    int TakeSkillImpact(const SRPGAttackPortion&);
    float HitLocationModifier(int);
    void CopyAttack(const SRPGAttackPortion&,const SRPGAttackPortion&);
    void ScheduleDoubleShot(CUnit&,CMapObject*,const SRPGAttackPortion&);
    void TickDoubleShots();
    int CombatSetting(const char*) const;
    bool AdvancedUI(const char*) const;
    std::string InstructionComponent(bool prototype,bool figure,const char* code,const char* fallback);
    float GoCampEnemyRadius();
    bool GoCampZoneExcluded(const char* zone);
    bool DeathFearSettings(float &enterHp, float &exitHp, float &fleeDistance, float &speedPercent);
    bool DeathFearExcludedPrototype(const char *prototype);
    float DeathFearSpeedFactor(const CUnit &) const;
    float VampSpellValue(const char* prototype,int column);
    int AttackCycle(const SRPGUnitStats&,const CUnit&,bool attack);
    void CampInfo(class CPersonage&, CUnit*, int &capacity, float &attackSeconds, float *displayActions=nullptr, float *displayMana=nullptr, bool baseStats=false);
    float CampDisplayMana(const SRPGUnitStats&,int carried,int capacity,bool baseStats);
    int CampSkillCapacity(class CPersonage&,CUnit*);
    float ModifiedDamage(const SRPGUnitStats&, const CUnit&, float damage, float roll, int leftSlot=-1, bool left=false);
    float ModifiedDamage(const SRPGUserUnitStats&, const CUnit&, float damage);
    void AttackParams(const SRPGUnitStats&, CUnit&, SAttackParams&, float roll);
    float ConditionalRetailDamage(SRPGUnitStats&, CUnit&, float damage);
    void ConditionalAttackParams(SRPGUnitStats&, CUnit&, SAttackParams&, bool inherited);
    void AttackParams(const SRPGUserUnitStats&, CUnit&, SAttackParams&);
    nlohmann::json CheckEconomy();
    nlohmann::json Describe();
    const std::string& GameDebugFile() const { return gameDebugFile; }
    struct FalloutSettings { float yaw=45.0f, pitch=35.264f, telephoto=2.0f; };
    const FalloutSettings& FalloutCameraSettings() const { return falloutSettings; }
private:
    friend bool InspectModConfigurationCommandLine(int&);
    bool ConfigureAddonSettings(CFileRegistry&);
    void Call(int args,int results);
    float StatsNumber(const char*,const SRPGUnitStats&);
    float ScalarManaBonus(float intelligence);
    void BeginCallback(const char*);
    bool IsNpcManaCaster(CUnit*);
    void Require(const char *name);
    std::unordered_map<std::string,SocialStealth::Profile> disguiseProfiles;
    bool registeredLoot=false;
    float experienceLimit=-1.0f;
    ModSettingsPolicy::Profile settingsProfile;
    ModSpellAddon::Profile spellAddon;
    bool forceTutorial=false;
    std::string chatHtmlFile;
    std::string gameDebugFile;
    FalloutSettings falloutSettings;
    float magicRandomLimit=-1.0f;
    bool saveInMod=true;
    bool singlePlayerAllowed=true,multiplayerAllowed=true;
    bool customMpZones=false;
    std::vector<std::string> mpZones[3];
    bool extendActiveObjectsArea=false;
    bool briefingWeapons=true;
    bool worldBackground=true;
    bool partyVisualOptions=false, hidePartyHelmets=false, hidePartyUpperArmor=false;
    std::string root;
    std::map<std::string,std::vector<std::string>> archives;
    std::map<std::string,bool> ignoreBase;
    std::map<std::string,std::string> configs,maps,cameras;
    std::map<std::string,std::unique_ptr<CMappedResource>> opened;
    lua_State *state=nullptr;
    std::shared_ptr<ModLua::PerkHostLifetime> perkHostLifetime;
    int lookupRef=-1,indexRef=-1;
    std::string mainScript;
    bool scriptInitialized=false;
    ModStaticLookup::State staticLookups;
    bool scalarManaBonus=false;
    std::map<std::string,float> manaBonusCache;
    bool scheduledRegeneration=false;
    std::unique_ptr<ModNpcRefreshQueue> refreshQueue;
    bool npcStatRefresh=true;
    bool npcDatabaseMigration=false;
    bool npcEquipmentMigrationPending=false;
    void InitializeLoadedHeroes();
    bool npcArmorCalculation=true,npcTrickster=true,npcMaterialEffects=true;
    struct ArmorModifiers {unsigned subtype=0;float speed=0,sight=0,hearing=0;};
    std::map<int,ArmorModifiers> npcArmorCache;
    const void* cacheWorld=nullptr;std::string cacheZone;int cacheSegment=-1;
    void SyncRuntimeState();
    std::unique_ptr<ModInitialization::Ledger> initialization;
    std::unique_ptr<ModInfoStats::State> infoStats;
    bool CollectArmor(CUnit&,ArmorModifiers&);
    std::map<std::string,int> combatSettings;
    std::map<std::string,std::pair<bool,ModSkills::Config>> skillConfigs;
    ModSkills::Cooldowns skillCooldowns;
    std::map<int,std::shared_ptr<ModDelayedShot>> delayedShots;
    struct CriticalRecord {bool critical;std::shared_ptr<CItemWeapon> weapon;bool left;int skillImpact=0;};
    std::map<const SRPGAttackPortion*,CriticalRecord> criticalAttacks;
    ModCustomLower::CriticalCache customCriticals;
    CMappedResource &Open(const std::string &path);
};
// Returns false for normal launches. Diagnostic mode never creates a window.
bool InspectModPackageCommandLine(int &result);

