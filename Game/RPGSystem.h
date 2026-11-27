#pragma once
#include "SaveSchema.h"
namespace ModCombat {struct Defense;struct Feedback;}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **	RPG System structures                                                      ** //
// **                                                                            ** //
// **	Written by Ivanov Evgeny                                                   ** //
// **	(C) Nival Interactive, 1998                                                ** //
// **                                                                            ** //
// **	Modified by Yuri V. Blazhevich                                             ** //
// **	(C) Nival Interactive, 1999                                                ** //
// ** 10.06.99 14:43  :)                                                         ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
#include "RPGSystemConsts.h"
#include "GameBase.h"
#include "SaveLoader.h"
#include "pdbFormat.h"
/////////////////////////////////////////////////////////////////////////////////////
class CUnit;
class CPerkDatabase;
class CItem;

struct SRPGUnitStats;
struct SIDBWeaponShape;
/////////////////////////////////////////////////////////////////////////////////////
struct SRPGScienceStats
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    // mask of tools that can be applied to this object, set of ERPGToolTypes
    DWORD dwTools;
    // key number that is compatible with this object (key number 0 means no special key
    // is required)
    DWORD dwKeyNumber;
    // science skill required to open smth
    DWORD dwSkill;

    SRPGScienceStats()
    {
        dwTools = 1;
        dwKeyNumber = 0;
        dwSkill = 0;
    }
    void Clear()
    {
        dwTools = 1;
        dwKeyNumber = 0;
        dwSkill = 0;
    }
    void Load(CSave &file);
    void Store(CSave &file);
    BOOL CanOpen(const SRPGScienceStats &door) const;
    BOOL CanDisarmTrap() const;
};
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                           RPG system stats                                 ** //
// **                         client light stats set                             ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
struct SRPGUserHitLocation
{
public:
    ERPGHitLocationTypes hlt;                // hit location type
    BYTE cCrippleMask;                       // Cripple mask for hit location
    WORD wAbsorbtions[RPG_NUM_ATTACK_TYPES]; // absorbtion for each attack type
    ERPGArmorLayerStates state;              // current state of hit location (same states as AL)
    WORD wHP, wMaxHP;                        // Hit points of hit location
public:
    // clearing all values
    void Clear();
};
/////////////////////////////////////////////////////////////////////////////////////
struct SRPGUserCommonStats
{
public:
    BYTE cKnowLevel;                         // knowledge level
    short nHP, nMaxHP;                       // health (hit points)
    WORD wMP, wMaxMP;                        // mana (mana points)
    WORD wAbsorbtions[RPG_NUM_ATTACK_TYPES]; // absorbtion for each attack type
public:
    // clearing all values
    void Clear();
};
/////////////////////////////////////////////////////////////////////////////////////
struct SRPGUserUnitStats
{
    BYTE cKnowLevel; // knowledge level
    // exp/char pts
    DWORD dwExperience; // gained experience
    long nFreeExp;      // exp to distribute
    // primary attributes
    BYTE cStrength;     // physical strength (affects physical damage, encumbrance, HP)
    BYTE cDexterity;    // dexterity (affects thief-like skills, action points, etc.)
    BYTE cIntelligence; // intelligence (affects spell power)
    //
    BYTE cLocomotion; // locomotion type
    // secondary attributes
    BYTE cSpeeds[4];   // speed in 4 different movement type
    BYTE cMove;        // move points (coeff for all movement actions)
    BYTE cAction;      // action points (coeff for all non-movement actions /ZB: attack/)
    WORD wEncumbrance; // encumbrance...
    //
    BYTE cNAWRange;
    BYTE cNAWToHit;
    BYTE cNAWToHitRandom;
    BYTE cNAWParry;
    BYTE cNAWWeight;
    BYTE cNAWDamageMin;
    BYTE cNAWDamageRnd;
    BYTE cNAWWeaponType;
    WORD wAttackerPrototype;
    //
    BYTE cSenses[RPG_NUM_SENSES];   // чувствительность по каждому органу чувств
    BYTE cPercepts[RPG_NUM_SENSES]; // степень заметности для каждого органа чувств
    // hit locations
    SRPGUserHitLocation hls[RPG_NUM_HIT_LOCATIONS]; // all hit locations
    // skills
    BYTE cManualSkills[RPG_NUM_MANUAL_SKILLS]; // manual skills
    BYTE cAutoSkills[RPG_NUM_AUTO_SKILLS];     // automatic skills
    // perks
    BYTE cPerks[RPG_NUM_PERKS]; //
    BYTE bFlags;                // different flags about unit stats
public:
    // clearing all values
    void Clear();
    //
    const SPDBPerk *GetPerk(int nPerk) const;
    float GetPerkModifier(int nPerk) const;
    float GetStrengthModifier() const;
    float GetDexterityModifier() const;
    float GetIntelligenceModifier() const;
    //
    float GetStrengthBonus() const { return static_cast<float>(cStrength - 25.0f); }
    float GetIntelligenceBonus() const { return static_cast<float>(cIntelligence - 25.0f); }
    float GetDexterityBonus() const { return static_cast<float>(cDexterity - 25.0f); }
    //
    void GetAbsorbtions(const SRPGUserCommonStats *pCommonStats, float *pfAbsorbtions,
                        bool bHero) const;
    float GetManaBonusMultiplier() const;
    float GetExpMultiplier() const;
    float GetDamageModifier(const CUnit *pUnit) const;
    float GetModifiedDamage(const CUnit *pUnit, float fDamage) const;
    //
    float GetWeaponSkill(ERPGWeaponTypes rpgWeaponType) const;
    float GetWeaponSkillLevel(ERPGWeaponTypes rpgWeaponType, CUnit *pUnit) const;
    float GetWeaponSkillLevel(ERPGWeaponTypes rpgWeaponType, const SIDBWeaponShape *pShape) const;
    float GetAutoSkill(ERPGAutoSkills rpgAutoSkill) const;
    float GetAutoSkillLevel(ERPGAutoSkills rpgAutoSkill, CUnit *pUnit) const;
    float GetAutoSkillLevel(ERPGAutoSkills rpgAutoSkill, const SIDBWeaponShape *pShape) const;
    //
    float GetSightRange(float fNightPower) const;
};
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                           RPG system stats                                 ** //
// **                       server complete stats set                            ** //
// ******************************************************************************** //
#pragma pack(4)
/////////////////////////////////////////////////////////////////////////////////////
// struct SRPGAttackPortion
// attack portion - emmited by attacker and will be processed by target (victim)
struct SRPGAttackPortion
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
public:
    float fpAttacks[RPG_NUM_ATTACK_TYPES]; // damage distribution by attack types
    int nSkill;                            // skill in this attack type
    float fpToHit;                         // to-hit parameter
    float fpDamage;                        // damage (overall power)
    float fpDamagePercentage;
    ERPGHitLocations hl; // hit location to attack
    BOOL bAreaEffect;    // is this attack portion direct or area effect
    float fBackStabMultiplier;
    bool bCalledShot;

public:
    void MakeDeadlyAttack();
    void Clear()
    {
        memset(this, 0, sizeof(*this));
        fpDamagePercentage = 1;
    }
    void Load(CSave &) = delete;
    void Store(CSave &) = delete;
    bool IsMagicAttack() const;
};
/////////////////////////////////////////////////////////////////////////////////////
// структура, содержащая распределение по разным видам атаки, используется для
// представления абсорбций по разным видам атаки или распределение повреждения
// атаки по этим видам
struct SRPGGeneralAttack
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    WORD wToHit;
    WORD wAttacks[RPG_NUM_ATTACK_TYPES];

    WORD GetMaxDamage() const;
    WORD CalcDamage(const SRPGGeneralAttack &def) const;
    void Load(CSave &) = delete;
    void Store(CSave &) = delete;
};
/////////////////////////////////////////////////////////////////////////////////////
static const DWORD RPG_NUM_ARMORS_IN_HL = 2;
// single armor layer on the hit location
struct SRPGArmorLayer
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
public:
    DWORD dwCrippleMask;                       // cripple mask for this layer
    float fpAbsorbtions[RPG_NUM_ATTACK_TYPES]; // absorbtion for each attack type
    float fpResistances[RPG_NUM_ATTACK_TYPES]; // resistance for each attack type
    ERPGArmorLayerStates state;                // current state of armor layer
    LPGAMEBASE lpItems[RPG_NUM_ARMORS_IN_HL];  // armors, which forms this AL in the HL
public:
    // clearing all values
    void Clear();
};
/////////////////////////////////////////////////////////////////////////////////////
// struct SRPGHitLocation
// hit location: contain HL type, cripple mask, armor layers and HP/MaxHP
struct SRPGHitLocation
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
public:
    ERPGHitLocationTypes hlt;                    // hit location type
    DWORD dwCrippleMask;                         // Cripple mask for hit location
    SRPGArmorLayer layers[RPG_NUM_ARMOR_LAYERS]; // all armor layers in this HL
    ERPGArmorLayerStates state; // current state of hit location (same states as AL)
    float fpHP, fpMaxHP;        // Hit points of hit location
    float fpHPWeight;           // how many HP (%) from soul's maxHP will be distributed here
    float fpVitality; // how many HP (%) will be removed from soul, then this HL will be dead
public:
    // data export
    void Export(SRPGUserHitLocation *lpStats) const;
    // clearing all values
    void Clear();
};
/////////////////////////////////////////////////////////////////////////////////////
// struct SRPGPrimaryStat
// represents primary stat, which have base value and effective value (base with all modifiers)
struct SRPGPrimaryStat
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
public:
    float fpBaseValue;      // base value of this stat
    float fpEffectiveValue; // effective value => base value with all modifiers
};
// struct SRPGSecondaryStat
// represents secondary stat, which have current value, max value and regeneration rate
struct SRPGSecondaryStat
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
public:
    float fpCurrent;    // current value
    float fpMax;        // max value
    float fpRegenerate; // regenerate rate
};
/////////////////////////////////////////////////////////////////////////////////////
// struct SRPGCommonStats
// common RPG stats: complete server version
// HP/MP and native resistances and protections
struct SRPGCommonStats
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
public:
    DWORD dwFlags; // just a flags
    //
    int nRegenerateCounter; // regeneration counter - can regenerate only once per second
    // secondary common attributes (HP & MP)
    SRPGSecondaryStat ssHP; // hit points secondary stat
    SRPGSecondaryStat ssMP; // mana points secondary stat
    // absorbtions and resistances
    SRPGPrimaryStat psAbsorbtions[RPG_NUM_ATTACK_TYPES];

public:
    // data export
    void Export(SRPGUserCommonStats *lpStats) const;
    // clearing all values
    void Clear();
    //
    float GetHP() const { return ssHP.fpCurrent; }
    float GetMP() const { return ssMP.fpCurrent; }
    //
    bool RegenerateMP(float fRegenerateRate);
    bool Regenerate(SRPGUnitStats &rpgUnitStats, CUnit *pUnit, bool bHPOnly);
    //
    BOOL ProcessAttackPortion(SRPGAttackPortion *lpAttackPortion);
    //
    BOOL IsDead() const { return ((short)MINT(ssHP.fpCurrent) <= 0); }
    //
    void Load(CSave &) = delete;
    void Store(CSave &) = delete;
};
/////////////////////////////////////////////////////////////////////////////////////
inline bool SRPGCommonStats::RegenerateMP(float fRegenerateRate)
{
    if (ssMP.fpCurrent == ssMP.fpMax)
        return false;
    ssMP.fpCurrent = Min(ssMP.fpCurrent + ssMP.fpMax * fRegenerateRate, ssMP.fpMax);
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
// struct SRPGUnitStats
// unit RPG stats: complete server version (handles PRIMARY attributes and their recalc to
// secondary)
struct SRPGUnitStats
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
public:
    DWORD dwFlags; // just a flags
    // exp/char pts
    float fpExperience; // gained experience
    float fSpentExp;
    // primary attributes
    SRPGPrimaryStat psStrength;     // physical strength (affects physical damage, encumbrance, HP)
    SRPGPrimaryStat psDexterity;    // dexterity (affects thief-like skills, action points, etc.)
    SRPGPrimaryStat psIntelligence; // intelligence (affects spell power)
    // secondary attributes
    SRPGPrimaryStat move;         // move points (coeff for all movement actions)
    SRPGPrimaryStat action;       // action points (coeff for all non-movement actions /ZB: attack/)
    float fpSpeeds[4];            // speed in 4 different movement type
    float fpVisionArc;            // vision arc - cosine( arc )
    float fpPeripheral;           // peripharal sight distance
    float fpPeripheralArc;        // peripheral sight arc. by default = 180 degrees
    float fpCurrEncumbrance;      // current encimbrance
    float fpMaxEncumbrance;       // maximum encumbrance, hero can run
    float fpMoveDamageModifier;   // damage modifier for movement [0..1]
    float fpActionDamageModifier; // damage modifier for action [0..1]
    DWORD dwLocomotion;           // locomotion type: walk, levitate, fly, swim, ...
    //
    float fpAttackDistance;                // minimal attack distance
    WORD wAIClassStay, wAlignProblemFixer; // ai classes for different poses
    WORD wAIClassLie, wAIClassCrawl;       //
    // native attack weapon (NAW) params
    float fpNAWRange;
    float fpNAWToHit;
    float fpNAWToHitRandom;
    float fpNAWParry;
    float fpNAWWeight;
    float fpNAWDamageMin;
    float fpNAWDamageRnd;
    float fpNAWActionPoints;
    float fpNAWAttacks[RPG_NUM_ATTACK_TYPES];
    ERPGWeaponTypes rpgNAWWeaponType;
    //
    SRPGPrimaryStat senses[RPG_NUM_SENSES];   // чувствительность по каждому органу чувств
    SRPGPrimaryStat percepts[RPG_NUM_SENSES]; // степень заметности для каждого органа чувств
    // hit locations
    SRPGHitLocation hls[RPG_NUM_HIT_LOCATIONS]; // all hit locations
    // skills
    BYTE cManualSkills[RPG_NUM_MANUAL_SKILLS]; // manual skills
    BYTE cAutoSkills[RPG_NUM_AUTO_SKILLS];     // automatic skills
    // perks
    BYTE cPerks[RPG_NUM_PERKS]; //
    DWORD bFlags;               // different flags about unit stats
    // stored RPG values for temporal modifications
    float fBaseHP;
    //
    float fExpPenalty; // пенальти по экспе. пока оно не заполнится, экспа расти не будет
private:
    BOOL ProcessModAttackSingle(CUnit*,SRPGCommonStats*,const SRPGAttackPortion*,CItem*,CUnit*,const ModCombat::Defense&,ModCombat::Feedback&);
    BOOL ProcessAttackPortionSingle(CUnit *pUnit, SRPGCommonStats *pCommonStats,
                                    const SRPGAttackPortion *pAttackPortion, CItem *pWeapon);
    void ApplyDamageToArmors(CUnit *pUnit, ERPGHitLocations hlType, int nArmorLayerIndex,
                             float fDamage);
    static float GetSkillPrice(int nSkillLevel);
    //
    float GetStrengthBonus() const { return psStrength.fpEffectiveValue - 25.0f; }
    float GetIntelligenceBonus() const { return psIntelligence.fpEffectiveValue - 25.0f; }
    float GetDexterityBonus() const { return psDexterity.fpEffectiveValue - 25.0f; }
    //
    void RedistributeHPMP(SRPGCommonStats *pCommonStats, CUnit *pUnit);

public:
    // data export
    void Export(const CUnit *pUnit, SRPGUserUnitStats *lpStats) const;
    void ExportAbsorbs(CUnit *pUnit, SRPGCommonStats *lpCommonStats, SRPGGeneralAttack &res) const;
    // clearing all values
    void Clear();
    //
    void RecalcComplectionFromPrimaryStats(float &fStrength, float &fGrace, float &fTall);
    // get effective values
    float GetSpeed() const { return move.fpEffectiveValue; }
    float GetRotationSpeed() const { return move.fpEffectiveValue * 0.5f; }
    float GetSense(int nIndex) const { return senses[nIndex].fpEffectiveValue; }
    float GetPercept(int nIndex) const { return percepts[nIndex].fpEffectiveValue; }
    //
    float GetRealExp() const { return fpExperience + fExpPenalty; }
    double GainExperience(float fpGainedExperience, bool useIntelligence = true);
    void GainExperienceInCombat(CUnit *pUnit, float fpGainedExperience);
    void AddExperience(float fAddExp);
    void AddExpPenalty(float fPenalty);
    void RecalcStats(SRPGCommonStats *lpCommonStats, CUnit *pUnit);
    void RecalcInternalHeroStats(float fHeroItemsWeight);
    void RecalcHitLocations(CUnit *pUnit);
    float GetExpMultiplier() const;
    float RecalcHP() const;
    float RecalcMP() const;
    float CollectMaxHPWithoutDeadHLs(float fMaxHP) const;
    float GetHealthMultiplier() const;
    float GetStaminaMultiplier() const;
    float GetMaxEncumbrance() const;
    float GetActionPoints() const;
    float GetManaBonusMultiplier() const;
    float GetSpellAddonManaBonusMultiplier() const;
    //
    // some effects applying
    void StrengthChanged(SRPGCommonStats *lpCommonStats, const CUnit *pUnit);
    float GetHealthModifier(const CUnit *pUnit) const;
    float GetModifiedHealth(const CUnit *pUnit, float fHealth);
    float GetDamageModifier(const CUnit *pUnit) const;
    float GetModifiedDamage(const CUnit *pUnit, float fDamage) const;
    //
    float GetMoveDamageModifier() const { return fpMoveDamageModifier; }
    float RecalcMoveDamageModifier(bool bHero) const;
    float RecalcActionDamageModifier(bool bHero) const;
    ECanRun CanRun(bool bHero) const;
    //
    BOOL ProcessModAttackPortion(CUnit*,SRPGCommonStats*,const SRPGAttackPortion*,CItem*,CUnit*,const ModCombat::Defense&,ModCombat::Feedback* = nullptr);
    BOOL ProcessAttackPortion(CUnit *pUnit, SRPGCommonStats *pCommonStats,
                              const SRPGAttackPortion *pAttackPortion, CItem *pWeapon = 0);
    BOOL Heal(SRPGCommonStats *lpCommonStats, float fpHeal);
    bool Heal(float fMaxHP, float fHeal);
    BOOL RegenerateHP(SRPGCommonStats *lpCommonStats);
    void HealAll();
    void Resurrect(ERPGHitLocations hlType);
    void ResurrectAll();
    //
    float CollectHP(float fpSoulMaxHP) const;
    void FixHls(CUnit* unit, float maxHp);
    void DistributeHP(float fpSoulMaxHP);
    void DistributeHPProportional(float fpSoulMaxHP);
    //
    void Wear(LPGAMEBASE lpItemArmor);
    void Remove(LPGAMEBASE lpItemArmor);
    void RemoveAllArmors();
    //
    BOOL CanAddPerk(const CPerkDatabase *pPDB, const SPDBPerk *pPerk) const;
    BOOL CanAddPerkLater(const CPerkDatabase *pPDB, const SPDBPerk *pPerk,
                         bool bCheckExist = true) const;
    void AddPerk(const CPerkDatabase *pPDB, const SPDBPerk *pPerk);
    const SPDBPerk *GetPerk(int nPerk) const;
    float GetPerkModifier(int nPerk) const;
    //
    ERPGHitLocations GetNearestHitLocation(ERPGHitLocations hl) const;
    //
    float GetNightVisionModifier() const;
    float GetActionModifier() const;
    // primary stats modifiers
    float GetStrengthModifier() const;
    float GetDexterityModifier() const;
    float GetIntelligenceModifier() const;
    //
    float GetBaseWeaponSkill(ERPGWeaponTypes rpgWeaponType) const;
    float GetWeaponSkill(ERPGWeaponTypes rpgWeaponType) const;
    float GetWeaponSkillLevel(ERPGWeaponTypes rpgWeaponType, CUnit *pUnit) const;
    float GetBaseAutoSkill(ERPGAutoSkills rpgAutoSkill) const;
    float GetAutoSkill(ERPGAutoSkills rpgAutoSkill) const;
    float GetAutoSkillLevel(ERPGAutoSkills skill, CUnit *pUnit) const;
    float GetBaseManualSkill(ERPGManualSkills skill) const;
    float GetManualSkill(ERPGManualSkills skill) const;
    float GetManualSkillLevel(ERPGManualSkills skill, CUnit *pUnit) const;
    // more precise weapon skill level distribution:
    float GetWSLDexterityBonus() const { return GetDexterityBonus(); }
    float GetWSLBaseSkill(ERPGWeaponTypes rpgWeaponType) const;
    float GetWSLPerkValue(ERPGWeaponTypes rpgWeaponType) const;
    float GetWSLWeaponModifier(DWORD dwWeaponSubtypeID) const;
    //
    float GetBaseBackstabDamageMultiplier() const;
    float GetBackstabDamageMultiplier(CUnit *pUnit) const;
    //
    float GetBaseMagicSkill(ERPGMagicSubtypes subtype) const;
    float GetMagicSkill(ERPGMagicSubtypes subtype) const;
    int GetAvailableComplexity(ERPGMagicSubtypes subtype) const
    {
        return MINT(GetMagicSkill(subtype));
    }
    bool CanCastSpell(ERPGMagicSubtypes subtype, DWORD dwComplexity) const
    {
        return GetAvailableComplexity(subtype) >= static_cast<int>(dwComplexity);
    }
    //
    static int GetSkillPrice(int nFrom, int nTo);
    int GetPerkPrice(const SPDBPerk *pPerk);
    //
    bool PresentSkill(ERPGAutoSkills skill, BYTE cAddLevel, CUnit *pUnit);
    bool PresentSkill(ERPGManualSkills skill, BYTE cAddLevel, CUnit *pUnit);
    bool PresentStrength(int nValue, CUnit *pUnit);
    bool PresentDexterity(int nValue, CUnit *pUnit);
    bool PresentIntelligence(int nValue, CUnit *pUnit);
    //
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};
#pragma pack()
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                   Client <=> Server synchronization                        ** //
// ******************************************************************************** //
class CUpdateHistory;
void UpdateTo(SRPGUserCommonStats &stats, CUpdateHistory &pkt);
void UpdateFrom(SRPGUserCommonStats &stats, CUpdateHistory &pkt);
void UpdateTo(SRPGUserUnitStats &stats, CUpdateHistory &pkt);
void UpdateFrom(SRPGUserUnitStats &stats, CUpdateHistory &pkt);
/////////////////////////////////////////////////////////////////////////////////////
