#include "StdAfx.h"

static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "aiSettings.h"
#include "Notification.h"
#include "WinRegistry.h"
#include "FileRegistry.h"
#include "EngineDiagnostics.h"
/////////////////////////////////////////////////////////////////////////////////////
CAIGeneralSettings theAIGeneralSettings;
/////////////////////////////////////////////////////////////////////////////////////
CAIGeneralSettings::CAIGeneralSettings()
{
    fpGuardStayTime = 10;
    fpSentryStayTime = 10;
    fpFollowMinDist = 2;
    fpFollowMaxDist = 4;
    fpGuardRadius = 10;
    fpCallForHelpRadius = 7;
    fpCallForHelpRadiusDefault = 7;
    fpFollowLimit = 15;
    fpPlayerCallForHelp = 16;
    fpRangedRunDist = 4;
    fpRangedAttackDist = 7;
    fpSwarmRadius = 10;

    fpHumanCrossAnimationSpeed = 1;
    fpBackstabAdd = 3;
    fpBackstabMult = 0.0f;
    fpRotateSpeedMult = 100;
    //
    fDamageLevel1 = 0.5f;
    fDamageLevel1Value = 0.5f;
    fDamageLevel2 = 0;
    fDamageLevel2Value = 0.33f;
    fDamageLevelRunLimit = 0.5f;
    fInt1ExpBonus = 0.2f;
    //
    fHPVal1 = 25;
    fHPVal2 = 30;
    fHPVal3 = 50;
    fHPVal4 = 1.1f;
    fHPVal5 = 1.61f;
    fMPVal1 = 25;
    fMPVal2 = 30;
    fMPVal3 = 50;
    fMPVal4 = 1.1f;
    fMPVal5 = 1.61f;
    //
    fSkillVal1 = 50;
    fSkillVal2 = 5;
    //
    fPerkPowerBase = 2.0f;
    //
    fConsiderModifier = 200;
    fConsiderLogBase = 1.61f;
    //
    fItemDurabilityCritical = 0.3f;
    //
    nQuestsToRespawn = 3;
    //
    fpLMPLostMoney = 0.1f;
    fpLMPLostXP = 0.1f;
    //
    for (int i = 0; i < 3; ++i)
    {
        fAbsorptionLevels[i] = 1;
        fAttackLevels[i] = 1;
        fDefenceLevels[i] = 1;
    }
    //
    bLoaded = false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIGeneralSettings::ReadFromFile(const char *pszFileName)
{
    if (bLoaded)
        return;
// The configured ai.reg is a binary registry, including float arrays.
    CFileRegistry reg;

    if (!reg.Open(pszFileName))
    {
        ReportError("Unable to open %s", pszFileName);
        return;
    }

    fpGuardStayTime = reg.GetFloat("Logic", "GuardStayTime", fpGuardStayTime);
    fpSentryStayTime = reg.GetFloat("Logic", "SentryStayTime", fpSentryStayTime);
    fpFollowMinDist = reg.GetFloat("Logic", "FollowMinDist", fpFollowMinDist);
    fpFollowMaxDist = reg.GetFloat("Logic", "FollowMaxDist", fpFollowMaxDist);
    fpGuardRadius = reg.GetFloat("Logic", "GuardRadius", fpGuardRadius);
    fpCallForHelpRadius = reg.GetFloat("Logic", "CallForHelpRadius", fpCallForHelpRadius);
    fpCallForHelpRadiusDefault = reg.GetFloat("Logic", "CallForHelpRadiusDefault",
                                              fpCallForHelpRadiusDefault);
    fpFollowLimit = reg.GetFloat("Logic", "FollowLimit", fpFollowLimit);
    fpPlayerCallForHelp = reg.GetFloat("Logic", "PlayerCallForHelp", fpPlayerCallForHelp);
    fpRangedRunDist = reg.GetFloat("Logic", "RangedRunDist", fpRangedRunDist);
    fpRangedAttackDist = reg.GetFloat("Logic", "RangedAttackDist", fpRangedAttackDist);
    fpSwarmRadius = reg.GetFloat("Logic", "SwarmRadius", fpSwarmRadius);

    fpHumanCrossAnimationSpeed = reg.GetFloat("RPG", "HumanCrossAnimationSpeed",
                                              fpHumanCrossAnimationSpeed);
    fpBackstabAdd = reg.GetFloat("RPG", "BackstabAdd", fpBackstabAdd);
    fpBackstabMult = reg.GetFloat("RPG", "BackstabMult", fpBackstabMult);
    fpRotateSpeedMult = reg.GetFloat("RPG", "RotateSpeedMult", fpRotateSpeedMult);
    //
    fDamageLevel1 = reg.GetFloat("RPG", "DamageLevel1", fDamageLevel1);
    fDamageLevel1Value = reg.GetFloat("RPG", "DamageLevel1Value", fDamageLevel1Value);
    fDamageLevel2 = reg.GetFloat("RPG", "DamageLevel2", fDamageLevel2);
    fDamageLevel2Value = reg.GetFloat("RPG", "DamageLevel2Value", fDamageLevel2Value);
    fDamageLevelRunLimit = reg.GetFloat("RPG", "DamageLevelRunLimit", fDamageLevelRunLimit);
    fInt1ExpBonus = reg.GetFloat("RPG", "Int1ExpBonus", fInt1ExpBonus);

    fHPVal1 = reg.GetFloat("RPG", "HP Val 1", fHPVal1);
    fHPVal2 = reg.GetFloat("RPG", "HP Val 2", fHPVal2);
    fHPVal3 = reg.GetFloat("RPG", "HP Val 3", fHPVal3);
    fHPVal4 = reg.GetFloat("RPG", "HP Val 4", fHPVal4);
    fHPVal5 = reg.GetFloat("RPG", "HP Val 5", fHPVal5);

    fMPVal1 = reg.GetFloat("RPG", "MP Val 1", fMPVal1);
    fMPVal2 = reg.GetFloat("RPG", "MP Val 2", fMPVal2);
    fMPVal3 = reg.GetFloat("RPG", "MP Val 3", fMPVal3);
    fMPVal4 = reg.GetFloat("RPG", "MP Val 4", fMPVal4);
    fMPVal5 = reg.GetFloat("RPG", "MP Val 5", fMPVal5);

    fSkillVal1 = reg.GetFloat("RPG", "Skill Val 1", fSkillVal1);
    fSkillVal2 = reg.GetFloat("RPG", "Skill Val 2", fSkillVal2);

    fPerkPowerBase = reg.GetFloat("RPG", "Perk Power Base", fPerkPowerBase);
    //
    fConsiderModifier = reg.GetFloat("RPG", "Consider Modifier", fConsiderModifier);
    fConsiderLogBase = reg.GetFloat("RPG", "Consider Log Base", fConsiderLogBase);
    //
    fItemDurabilityCritical = reg.GetFloat("RPG", "Item Durability Critical",
                                           fItemDurabilityCritical);
    //
    nQuestsToRespawn = reg.GetInt("Scenario", "QuestsToRespawn", nQuestsToRespawn);
    //
    fpLMPLostMoney = reg.GetFloat("LMP", "Lost Money", fpLMPLostMoney);
    fpLMPLostXP = reg.GetFloat("LMP", "Lost XP", fpLMPLostXP);
    // difficulty levels - multiplayers for respective params
    CFloatArray floats;
    for (int i = 0; i < 3; ++i)
    {
        fAbsorptionLevels[i] = 1;
        fAttackLevels[i] = 1;
        fDefenceLevels[i] = 1;
    }

    // absorption
    reg.GetFloatArray("DifficultyLevels", "Absorption", floats);
    for (int i = 0; i < Min(floats.GetSize(), 3); ++i)
        fAbsorptionLevels[i] = floats[i];
    // attacks
    floats.RemoveAll();
    reg.GetFloatArray("DifficultyLevels", "Attack", floats);
    for (int i = 0; i < Min(floats.GetSize(), 3); ++i)
        fAttackLevels[i] = floats[i];
    // defenses
    floats.RemoveAll();
    reg.GetFloatArray("DifficultyLevels", "Defence", floats);
    for (int i = 0; i < Min(floats.GetSize(), 3); ++i)
        fDefenceLevels[i] = floats[i];
    bLoaded = true;
    EngineDiagnostics::Event("AI_SETTINGS source=%s ranged_run=%.3f ranged_attack=%.3f follow=%.3f/%.3f backstab=%.3f/%.3f",
        pszFileName, fpRangedRunDist, fpRangedAttackDist, fpFollowMinDist, fpFollowMaxDist, fpBackstabAdd, fpBackstabMult);
}
/////////////////////////////////////////////////////////////////////////////////////
