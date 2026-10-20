#pragma once
#include "FileRegistry.h"
#include "regFormat.h"
#include <cstring>
#include <stdexcept>

namespace ModConfiguration {
// Only settings already mapped by CModPackage. Value defaults match the AI
// reads in the installed-bound native Config.cpp; no new configuration key.
struct BoolRule {const char* key;bool fallback;};
inline const BoolRule* CombatRules(size_t& count){
    static const BoolRule rules[]={
        {"AntiCrash",false},{"useAdvancedDescriptions",false},{"useLS_UpdateStats",false},
        {"useLS_FormTextInfo",false},{"useLS_SetSkill",false},
        {"useLS_DrawTotalHP",false},{"useLS_SetUnitS",false},
        {"useLS_ObjItemInfoDraw",false},{"useLS_GetMaxManaAndComplexity",false},
        {"useLS_DrawStats",false},{"useLS_ShowEnemyConsider",false},
        {"useLS_ObjectItemsOnIdle",false},{"useLS_ObjectWeaponsOnIdle",false},
        {"useLS_SetAssortmentParameters",false},
        {"useLS_SetUnitW",false},{"JMVExtendedSpellSlots",true},
        {"useLS_DrawSpellEffects",false},{"useLS_DrawHLState",false},
        {"useLS_NewFont",false},{"JMVGraphics",false},{"writeComposeInMemory",false},{"showJmvDebug",false},{"JMVCharacterAtlasSharing",true},{"JMVGraphics2",false},
        {"AllowMagicNegative",false},{"JMVParticles",false},{"NewRunes",false},
        {"JMVSelfCast",true},{"JMVAreaTargetPreview",true},
        {"ArchersDefence",false},{"DeagrPlayerOnLoose",false},{"DisableRunaway",false},
        {"DistanceModifier",false},{"WeightBonus",false},{"DisableCrippledHL",false},
        {"UsePrototypeActionsForMobAttack",false},{"LoadSpellAnimationFromSlotsField",false},
        {"SendSpellEffectValueToPlayers",false},
        {"JMVPowerHit",true},{"JMVDoubleShot",true},{"JMVPreciseShot",true},{"JMVExplosiveShot",true},
        {"JMVShit",false},{"JMVAttacks",false},{"JMVNoWeaponSwitchShit",false},{"JMVDaggerCritical",true},{"JMV136",false},{"JMVPerks",false},{"JMVAlchemy",false},
        {"JMVWeaponBlocking",true},{"JMVAnatomyLimbDamage",true},
        {"JMVFixCriticalHitSoundTrigger",false},{"JMVRangedBackstab",true},
        {"JMVBackstabDefenderEquipment",true},{"JMVHealAfterHit",false},{"usePotionAcks",false},
        {"AttackAnimSpeed",false},{"UsePrototypeActionsForMobCast",false},
        {"RemoveSpellConstructReqs",false},{"improveInfoWindows",false},
        {"JMVItemInfoLineHeight",true},{"JMVRegenerationTooltip",true},{"JMVArmorMaterialStats",true},
        {"JMVShowCharacterModelInStats",true},{"JMVShopUpdate",false},{"JMVLoadVideos",true},
        {"JMVFixConstructorShutdownRelease",true},{"JMVGenocidePlayerKillTracking",false},
        {"ShowAllQuests",false},{"JMVNoDrawall",false},{"FastTeleport",false},{"UnlockExperimentalSpells",false},
        {"JMVSteals",false},{"useLS_DrawMonsterMana",false},
        {"JMVGoCampRebindScriptReferences",false},
        {"JMVOneAmuletLimit",false},{"JMVRingsLimit",false},
        {"P100MCalcSpellManaCostTooltip",false},{"P100MCalcSpellEffectTooltip",false},
        {"P100MCalcSpellDurationTooltip",false}
    };
    count=sizeof(rules)/sizeof(*rules);return rules;
}
inline bool Bool(CFileRegistry& reg,const char* section,const char* key,bool fallback){
    // Native GetBool is GetDword(...) != 0, including FLOAT-to-DWORD conversion.
    return reg.GetDWord(section,key,fallback?1u:0u)!=0;
}
inline float Float(CFileRegistry& reg,const char* section,const char* key,float fallback){
    if(const auto p=reg.FindData(section,key,ERT_FLOAT,NULL,FALSE)){float value;std::memcpy(&value,p,sizeof(value));return value;}
    // The engine's general GetFloat casts DWORD through signed long. Native
    // addon GetFloat casts uint32_t; keep this adapter local to addon settings.
    if(const auto p=reg.FindData(section,key,ERT_DWORD)){DWORD value;std::memcpy(&value,p,sizeof(value));return float(value);}
    return fallback;
}
}
