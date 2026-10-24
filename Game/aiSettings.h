#pragma once
/////////////////////////////////////////////////////////////////////////////////////
class CAIGeneralSettings
{
    bool bLoaded;

public:
    float fpGuardStayTime;
    float fpSentryStayTime;
    float fpFollowMinDist;
    float fpFollowMaxDist;
    float fpGuardRadius;
    float fpCallForHelpRadius;
    float fpCallForHelpRadiusDefault;
    float fpFollowLimit;
    float fpPlayerCallForHelp;
    float fpRangedRunDist;
    float fpRangedAttackDist;
    float fpSwarmRadius;

    float fpHumanCrossAnimationSpeed;
    float fpBackstabAdd;
    float fpBackstabMult;
    float fpRotateSpeedMult;
    //
    float fDamageLevel1;
    float fDamageLevel1Value;
    float fDamageLevel2;
    float fDamageLevel2Value;
    float fDamageLevelRunLimit;
    float fInt1ExpBonus;
    float fHPVal1, fHPVal2, fHPVal3, fHPVal4, fHPVal5;
    float fMPVal1, fMPVal2, fMPVal3, fMPVal4, fMPVal5;
    float fSkillVal1, fSkillVal2;
    //
    float fPerkPowerBase;
    // consider function params
    float fConsiderModifier;
    float fConsiderLogBase;
    //
    //
    int nQuestsToRespawn;
    //
    float fItemDurabilityCritical;
    // difficulty levels - multiplayers for respective params
    float fAbsorptionLevels[3];
    float fAttackLevels[3];
    float fDefenceLevels[3];
    //
    CAIGeneralSettings();
    void ReadFromFile(const char *pszFileName);
};
extern CAIGeneralSettings theAIGeneralSettings;
/////////////////////////////////////////////////////////////////////////////////////
