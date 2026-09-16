#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "RPGSystemConsts.h"
#include "TypeIDs.h"

#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
const DWORD UDB_NUM_MAGIC_SCHOOLS = 3;
/////////////////////////////////////////////////////////////////////////////////////
class CStructureSaver;
/////////////////////////////////////////////////////////////////////////////////////
// struct SUDBUnitLoot
// барахло, выпадающее из убитого
struct SUDBUnitLoot
{
public:
    float fpProbability; // possibility of the appearance
    DWORD dwMask;        // items mask
    float fpPriceMin;    // minimal price | price range
    float fpPriceMax;    // maximal price |
public:
    void Serialize(CStructureSaver &file);
};
/////////////////////////////////////////////////////////////////////////////////////
// struct SUDBUnitHitLocation
// hit location descritpion: reference to type and weight/vitality
struct SUDBUnitHitLocation
{
public:
    CString szHLType;    // hit location type name
    DWORD dwHLTypeIndex; // hit location type index to perform fast search
    float fpWeight;      // weight - how many percents of MaxHP will be strored in this HL
    float fpVitality;    // vitality - how manypercents of HP add/remove from soul then this HL HP
                         // change on '1'
public:
    void Serialize(CStructureSaver &file);
};
/////////////////////////////////////////////////////////////////////////////////////
// struct SUDBUnitPrototype
// unit prototype
// reference to race + all necessary defaults
struct SUDBUnitPrototype
{
public:
    CString szName;
    CString szRace;
    DWORD dwRaceIndex; // race index to perform fast search
    // graphics data
    DWORD dwSkinIndex;           // skin texture index
    DWORD dwHair;                // hair model number [1...n]
    float fStrength;             // default complection strength
    float fGrace;                // default complection grace
    float fTall;                 // default complection tall
    float fInfoWindowScale;      // parameter to scale in info window
    float fAltitude;             // height over surface
    float fDialogCameraDistance; //
    float fDialogCameraHeight;   //
    float fDetonation;           // как потрясти камеру от шагов этого юнита (амплитуда в метрах)
    // sounds data. NOTE: all fields with numbers will be filled during initializing process
    CString szSFXPath;    // general path to this prototype SFXes
    DWORD dwNumSFXAttack; // number of SFXes to play with animation 'attack'
    DWORD dwNumSFXIdle;   // number of SFXes to play with animation 'idle'
    DWORD dwNumSFXHit;    // number of SFXes to play with animation 'suffer'
    DWORD dwNumSFXDeath;  // number of SFXes to play with animation 'death'
    //
    float fpMaxHP; // soul max health
    float fpMaxMP; // max mana
    //
    int nLevel; // this monster integrate level
    float fExp; // killing reward (experience)
    //
    float fpAbsorbtionMultiplier; // absorbtion multiplier
    float fpActionPoints;         // action points
    float fpMovePoints;           // move points
    // attack
    float fpAttackRange;                 // attack range with native weapon
    float fpAttackToHit;                 // to hit with native weapon
    float fpAttackToHitRandom;           // to hit random with native weapon
    float fpAttackParry;                 // parry with native weapon
    float fpAttackWeight;                // weight of native weapon
    ERPGWeaponTypes rpgWeaponType;       // native weapon type
    ERPGWeaponTypes rpgAttackWeaponType; // для правильного проигрывания звука
    // damage
    float fpDamageMin;    // minimal damage
    float fpDamageRandom; // damage random: damage = fpDamageMin + Random(fpDamageRandom);
    // skills
    float fpDefaultSkill; // all skills will be as it by default
    float fpSkillSteal;   // skill in stealing
    float fpSkillTame;    // skill in taming
    float fpPeripheral;   // skill in peripherial sight
    // senses & percepts
    float senses[RPG_NUM_SENSES];   // default senses
    float percepts[RPG_NUM_SENSES]; // default percepts
    // loot
    SUDBUnitLoot ulCommon; // common loot
    SUDBUnitLoot ulRare;   // rare loot
    // always drop
    std::vector<CString> szDrops; // always drop items
    // skills in magic schools
    float magics[UDB_NUM_MAGIC_SCHOOLS]; // default skills in magic schools
    // spells
    std::vector<CString> szSpells; // spells, this unit have
    // wears and weapons
    std::vector<CString> szWears;
    CString szWeapon, szWeapon2;

public:
    void Serialize(CStructureSaver &file);

    LPCSTR GetCode() const { return szName; }
};
/////////////////////////////////////////////////////////////////////////////////////
struct SUDBNPCPrototype
{
    CString szName;
    //
    DWORD dwCharacterPoints;
    float fExpirience;
    // primary stats
    float fStrength;
    float fDexterity;
    float fIntelligence;
    // skills & perks
    BYTE cManualSkills[RPG_NUM_MANUAL_SKILLS];
    BYTE cAutoSkills[RPG_NUM_AUTO_SKILLS];
    std::vector<CString> szPerks;
    // LMP specific
    std::vector<CString> szWeapons;
    std::vector<CString> szQItems;
    std::vector<CString> szSpells;
    float fExpToDistribute;
    DWORD dwMoney;
    bool bCanUseAsVoicePrototype;

public:
    void Serialize(CStructureSaver &file);

    LPCSTR GetCode() const { return szName; }
};
/////////////////////////////////////////////////////////////////////////////////////
// struct SUDBRaceModel
// race description
struct SUDBRaceModel
{
public:
    CString szRace;     // race name
    ETypeIDs tidTypeID; // type: human, monster, animal
    //
    float fpRegenerateHP; // health regeneration rate (in percents from max health)
    float fpRegenerateMP; // mana regeneration rate (in percents from max mana)
    //
    DWORD dwLanguage;   // language identificator
    DWORD dwLocomotion; // locomotion type
    float fpVisionArc;  // vision arc
    // speed in different movement type
    float fpSpeeds[4];         // speed in run/walk/sneak/crawl movement
    float fAnimationCoeffs[6]; // animation coeffs for: attack, cast, hit, death, cross, special
    //
    float fpAttackDistance; // minimal attack distance
    DWORD dwAIClassStay;    // AI class сто
    DWORD dwAIClassLie;     // AI class лёжа
    //
    float attacks[RPG_NUM_ATTACK_TYPES];  // default attack distribution (in percents by each type)
    float defences[RPG_NUM_ATTACK_TYPES]; // absorption distribution
    //
    SUDBUnitHitLocation hls[RPG_NUM_HIT_LOCATIONS]; // hit locations info
    // graphics data
    CString szMaskName;                       // model mask name
    std::vector<CString> szPrimaryTextures;   // available primary textures
    std::vector<CString> szSecondaryTextures; // available secondary textures
    float fpCenterShift;                      // center shift to rotate around '0'
    DWORD dwBloodType;
    long nFootprintType;      // '-1' if no prints require for this race
    DWORD dwNumLegSegments;   // number of visible leg segments
    DWORD dwCastingType;      // require or not particles with 'start casting'
    CString szNativeSkinType; // тип кожи для проигрывания звука удара по ней
    bool bFirstStepRight;     // начинает идти с правой ноги
    float fHeadHeight;        // отношение высоты подбородка к высоте юнита
    // sounds data. NOTE: all fields with numbers will be filled during initializing process
    // SFXes
    CString szSFXPath;    // path to SFXes of thie race model (use it if no path in prototype)
    DWORD dwNumSFXAttack; // number of SFXes to play with animation 'attack'
    DWORD dwNumSFXIdle;   // number of SFXes to play with animation 'idle'
    DWORD dwNumSFXHit;    // number of SFXes to play with animation 'suffer'
    DWORD dwNumSFXDeath;  // number of SFXes to play with animation 'death'
    // steps
    std::vector<CString> szSteps;  // path to steps for all terrain types
    std::vector<DWORD> dwNumSteps; // number of steps for all terrain types
    // action sounds playing probability
    DWORD dwIdleProbability;
    DWORD dwAttackProbability;
    // facial animation mouth params
    int nMouthPointX;
    int nMouthPointY;
    int nMouthRectSizeX;
    int nMouthRectSizeY;

public:
    void Serialize(CStructureSaver &file);

    LPCSTR GetCode() const { return szRace; }
};
/////////////////////////////////////////////////////////////////////////////////////
// struct SUDBHitLocationType
// descritpion of hit location type (NOTE: don't misplace with hit location)
struct SUDBHitLocationType
{
public:
    CString szName; // hit location type name
    ERPGHitLocationTypes hlt;
    float fpResistances[RPG_NUM_ATTACK_TYPES]; // resistances by all attack types
    float fpAbsorbtions[RPG_NUM_ATTACK_TYPES]; // absorbtions by all attack types
    DWORD dwCrippleMask;                       // cripple mask
public:
    void Serialize(CStructureSaver &file);

    LPCSTR GetCode() const { return szName; }
};
/////////////////////////////////////////////////////////////////////////////////////
