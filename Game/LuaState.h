#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Internal.h"
//#include "addon/EIClasses/item.h"

//namespace EIClasses {
//class CUnit;
//struct SRPGCommonStats;
//struct SRPGUnitStats;
//struct SRPGUserUnitStats;
//class SPDBPerk;
//class CItemArmor;
//class CItemWeapon;
//struct SAttackParams;
//struct SRPGPrimaryStat;
//
//} // namespace EIClasses
class CUnit;
struct SRPGCommonStats;
struct SRPGUnitStats;
struct SRPGUserUnitStats;
struct SPDBPerk;
class CItemArmor;
class CItemWeapon;
struct SAttackParams;
struct SRPGPrimaryStat;

namespace Lua {

class CLuaState
{
    __EI_ONLY_CTOR_DTOR(CLuaState);

public:
    CLuaState();
    ~CLuaState();

    void Initialize(const std::string &path, const std::string &fileName);

    float CalcBaseHealth(SRPGUnitStats *stats);
    float CalcBaseHealthJmv(SRPGUnitStats *stats, CUnit* unit, const char* unitName);
    float CalcBaseMana(SRPGUnitStats *stats);
    float CalcBaseManaJmv(SRPGUnitStats *stats, CUnit* unit, const char* unitName);
	const char* GetPerkRequiredJmv(const char* perkName, int num);
    void CalcRunningManaCost(SRPGUnitStats *unitStats,
                             CUnit *unit,
                             SRPGCommonStats *commonStats);
    int GetSkillPriceDiff(int from, int to);
    int GetPerkPrice(SRPGUnitStats *stats, SPDBPerk *perk);
    int GetPerkVal(SRPGUnitStats *stats, int index);

    float GetModifiedHealth(SRPGUnitStats *stats, CUnit *unit, float health);
    float GetModifiedDamageOnServer(SRPGUnitStats *stats,
                                    CUnit *unit,
                                    float damage, float *spellEffects);
    float GetModifiedDamageOnServerJmv(SRPGUnitStats *stats,
                                    CUnit *unit,
                                    float damage, 
                                    float rand, 
									int leftHandNum, 
									bool leftHand,
									const char* unitName, uint32_t weapid, int weapnum
		);
    float GetCritOnServerJmv(SRPGUnitStats *stats,
                                    CUnit *unit,
									int leftHandNum, 
									bool leftHand,
									const char* unitName, int TypeID, int weapNum);
    float GetCritOnClientJmv(CUnit *unit,
									float critPerkMod,
									int leftHandNum,
									bool leftHand,
									const char* unitName, int weapNum, int TypeID);
    float GetCritDamageOnClientJmv(CUnit *unit,
									float critPerkMod,
									int leftHandNum,
									bool leftHand,
									const char* unitName, int weapNum, int TypeID);
    float GetModifiedDamageOnClient(SRPGUserUnitStats *userStats,
                                    CUnit *unit,
                                    float damage, float *spellEffects);
    float GetModifiedDamageOnClientJmv(SRPGUserUnitStats *userStats,
                                    CUnit *unit,
                                    float damage, 
                                    float rand, 
									const char* unitName, uint32_t weapid, int weapnum, float* spellEffects);
    bool RegenerateHpAndMp(SRPGCommonStats *commonStats,
                           SRPGUnitStats *unitStats,
                           CUnit *unit,
                           bool hpOnly);
    bool RegenerateHpAndMpJmv(SRPGCommonStats *commonStats,
                           SRPGUnitStats *unitStats,
                           CUnit *unit,
                           bool hpOnly,
						   bool inDanger);
    void RecalcUnitStats(SRPGUnitStats *unitStats,
                         SRPGCommonStats *commonStats,
                         CUnit *unit,
                         float *spellEffects);
    void RecalcUnitStatsJmv(SRPGUnitStats *unitStats,
                         SRPGCommonStats *commonStats,
                         CUnit *unit,
                         float *spellEffects,
						 int HumanStatsRecalc,
						 int ArmorRecalc,
						 const char * unitName, 
						 bool inDanger);
	void RecalcInitialJmvStatsExt(SRPGUnitStats* unitStats,
						 SRPGCommonStats* commonStats,
						 CUnit* unit,
						 const char* unitName);
    float RecalcHeadDamageModifierLS(SRPGUnitStats *unitStats);
    float GetAnatomyModifierLS(SRPGUnitStats *unitStats);
    float GetWeaponsMasterLS(SRPGUnitStats *unitStats);
    float GetWiseManDamageModiferLS(CUnit* unit, SRPGUnitStats* unitStats, int intelligence);

    void RecalcInternalHeroStats(SRPGUnitStats *stats, float itemsWeigh);
    float CheckShieldBlock(SRPGUserUnitStats*unitStats,
                                  CUnit *unit);
    float GetShieldBlockChance(SRPGUserUnitStats*unitStats,
                                  CUnit *unit);
	int GetDualAnims(const char* animName, int prop);
	float CheckWeaponParry(SRPGUserUnitStats *unitStats,
                                  CUnit *unit, int num);
	float GetWeaponParryChance(SRPGUserUnitStats *unitStats,
                                  CUnit *unit, int num);
	float GetDamageModAttr(int weapon_type, int strength, int dexterity, int intelligence, float mod);
	float CheckShieldParry(SRPGUserUnitStats *unitStats,
                                  CUnit *unit);
	float GetShieldParryChance(SRPGUserUnitStats *unitStats,
                                  CUnit *unit);
	float GetAttackModiferFromHL(int hit_location);
	float GetArmorPropsInside(const char* itemName, int prop);
	float GetWeaponPropsInside(const char* itemName, int prop);
	float GetMaterialPropsInside(const char* itemName, int prop);
	float GetJewelryPropsInside(const char* itemName, int prop);
	int GetRegenBonusFromPerkMagicBlood(int level);
	float GetComplexSummStats(CUnit *unit, int stat);
	float GetUnitPerksInside(const char* unitName, int prop);
	float GetShopConstructLimit(int shop, int prop);
	float GetSpellAdds(const char* name, int prop);
	bool IsForbiddenItem(const char* itemName, int prop, int shop);
	float GetUnitStatsInside(const char* statName, int prop);

	void CalcAttackParamsOnClient(SRPGUserUnitStats *unitStats,
                                  CUnit *unit,
                                  SAttackParams *attackParams,
                                  float *spellEffects, int leftHandNum);
    void CalcAttackParamsOnServer(SRPGUnitStats *unitStats,
                                  CUnit *unit,
                                  SAttackParams *attackParams,
                                  float *spellEffects);
    void ApplySpellEffectsOnServer(SRPGUnitStats *unitStats,
								  SRPGCommonStats *commonStats,
                                  float *spellEffects);


	float GetHitLocDecr( SRPGUnitStats* unitStats,
						CUnit* unit,
						bool server);
	float GetHitLocDecrDamage( SRPGUnitStats* unitStats,
						CUnit* unit,
						bool server);
	float GetOverloadDefenceOnServer(float currEncumbrance, float maxEncumbrance);
	float GetOverloadAttackOnServer(float currEncumbrance, float maxEncumbrance);
	float OverloadManacostModsOnServer(float currEncumbrance, float maxEncumbrance);
	float GetOverloadDamageOnServer(float currEncumbrance, float maxEncumbrance);
	float GetOverloadBlockOnServer(float currEncumbrance, float maxEncumbrance);
	float GetOverloadParryOnServer(float currEncumbrance, float maxEncumbrance);
	float GetOverloadPercentOnServer(SRPGUnitStats*unitStats);
	float GetOverloadPercentCurOnServer(SRPGUnitStats*unitStats);
 

	float GetManaBonusMultiplier(void *stats, bool isServer);
    float GetExpMultiplier(void *stats, bool isServer);
    float GetExpMultiplier2(float intelligence, bool isServer);
    void RecalcShopsPriceMultipliers(std::vector<float> *mults, bool isLmp);
    float DistributeLmpExp(float exp, int playersNum);

private:
    void ReportOnError(int status);
    int DoCall(int args, int results);
    int DoFile(const std::string &path);

    void *state; // lua_State
};

extern CLuaState State;

} // namespace Lua
