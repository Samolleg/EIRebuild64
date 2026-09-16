#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "udbFormat.h"

#include "BasicChunk.h"
/////////////////////////////////////////////////////////////////////////////////////
void SUDBUnitLoot::Serialize(CStructureSaver &file)
{
    file.Add(0, fpProbability);
    file.Add(1, dwMask);
    file.Add(2, fpPriceMin);
    file.Add(3, fpPriceMax);
}
/////////////////////////////////////////////////////////////////////////////////////
void SUDBUnitHitLocation::Serialize(CStructureSaver &file)
{
    file.Add(0, szHLType);
    file.Add(1, dwHLTypeIndex);
    file.Add(2, fpWeight);
    file.Add(3, fpVitality);
}
/////////////////////////////////////////////////////////////////////////////////////
void SUDBUnitPrototype::Serialize(CStructureSaver &file)
{
    file.Add(0, szName);
    file.Add(1, szRace);
    file.Add(2, dwRaceIndex);
    // graphics data
    file.Add(3, dwSkinIndex);
    file.Add(4, dwHair);
    file.Add(5, fStrength);
    file.Add(6, fGrace);
    file.Add(7, fTall);
    // sounds data. NOTE: all fields with numbers will be filled during initializing process
    file.Add(8, szSFXPath);
    //
    file.Add(9, fpMaxHP);
    file.Add(10, fpMaxMP);
    //
    file.Add(11, fpAbsorbtionMultiplier);
    file.Add(12, fpActionPoints);
    file.Add(13, fpMovePoints);
    // attack
    file.Add(14, fpAttackRange);
    file.Add(15, fpAttackToHit);
    file.Add(16, fpAttackParry);
    file.Add(17, fpAttackWeight);
    file.AddRaw(18, &rpgWeaponType, sizeof(rpgWeaponType));
    // damage
    file.Add(19, fpDamageMin);
    file.Add(20, fpDamageRandom);
    // skills
    file.Add(21, fpDefaultSkill);
    file.Add(22, fpSkillSteal);
    file.Add(23, fpSkillTame);
    file.Add(24, fpPeripheral);
    // senses & percepts
    file.AddRaw(25, senses, sizeof(senses));
    file.AddRaw(26, percepts, sizeof(percepts));
    // loot
    file.Add(27, ulCommon);
    file.Add(28, ulRare);
    // always drop
    file.AddT(29, szDrops);
    // skills in magic schools
    file.AddRaw(30, magics, sizeof(magics));
    // spells
    file.AddT(31, szSpells);
    // wears and weapons
    file.AddT(32, szWears);
    file.Add(33, szWeapon);
    //
    file.Add(34, fInfoWindowScale);
    file.Add(35, fAltitude);
    //
    file.Add(36, fpAttackToHitRandom);
    // graphics dialog-related data
    file.Add(37, fDialogCameraDistance);
    file.Add(38, fDialogCameraHeight);
    //
    file.AddRaw(39, &rpgAttackWeaponType, sizeof(rpgAttackWeaponType));
    //
    file.Add(40, fDetonation);
    //
    file.Add(41, nLevel);
    //
    file.Add(42, szWeapon2);
    //
    file.Add(43, fExp);
}
/////////////////////////////////////////////////////////////////////////////////////
void SUDBNPCPrototype::Serialize(CStructureSaver &file)
{
    file.Add(0, szName);
    //
    file.Add(1, dwCharacterPoints);
    file.Add(2, fExpirience);
    // primary stats
    file.Add(3, fStrength);
    file.Add(4, fDexterity);
    file.Add(5, fIntelligence);
    // skills & perks
    file.AddRaw(6, cManualSkills, sizeof(cManualSkills));
    file.AddRaw(7, cAutoSkills, sizeof(cAutoSkills));
    file.AddT(8, szPerks);
    //
    file.AddT(9, szWeapons);
    file.AddT(10, szQItems);
    file.AddT(11, szSpells);
    file.Add(12, fExpToDistribute);
    file.Add(13, dwMoney);
    file.Add(14, bCanUseAsVoicePrototype);
}
/////////////////////////////////////////////////////////////////////////////////////
void SUDBRaceModel::Serialize(CStructureSaver &file)
{
    file.Add(0, szRace);
    file.AddRaw(1, &tidTypeID, sizeof(tidTypeID));
    //
    file.Add(2, fpRegenerateHP);
    file.Add(3, fpRegenerateMP);
    //
    file.Add(4, dwLanguage);
    file.Add(5, dwLocomotion);
    file.Add(6, fpVisionArc);
    // speed in different movement type
    file.AddRaw(7, fpSpeeds, sizeof(fpSpeeds));
    //
    file.Add(8, fpAttackDistance);
    file.Add(9, dwAIClassStay);
    file.Add(10, dwAIClassLie);
    //
    file.AddRaw(11, attacks, sizeof(attacks));
    //
    for (int i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i)
        file.Add(12 + i, hls[i]);
    //
    file.Add(30, szMaskName);
    file.AddT(31, szPrimaryTextures);
    file.AddT(32, szSecondaryTextures);
    file.Add(33, fpCenterShift);
    // sounds data. NOTE: all fields with numbers will be filled during initializing process
    // SFXes
    file.Add(34, szSFXPath);
    // steps
    file.AddT(35, szSteps);
    //
    file.AddRaw(36, fAnimationCoeffs, sizeof(fAnimationCoeffs));
    //
    file.Add(37, dwIdleProbability);
    file.Add(38, dwAttackProbability);
    //
    file.AddRaw(39, defences, sizeof(defences));
    //
    file.Add(40, dwBloodType);
    file.Add(41, dwCastingType);
    file.Add(42, nFootprintType);
    file.Add(43, dwNumLegSegments);
    //
    file.Add(44, szNativeSkinType);
    file.Add(45, bFirstStepRight);
    file.Add(46, fHeadHeight);
    //
    file.Add(47, nMouthPointX);
    file.Add(48, nMouthPointY);
    file.Add(49, nMouthRectSizeX);
    file.Add(50, nMouthRectSizeY);
}
/////////////////////////////////////////////////////////////////////////////////////
void SUDBHitLocationType::Serialize(CStructureSaver &file)
{
    file.Add(0, szName);
    file.AddRaw(1, fpResistances, sizeof(fpResistances));
    file.AddRaw(2, fpAbsorbtions, sizeof(fpAbsorbtions));
    file.Add(3, dwCrippleMask);
    file.AddRaw(4, &hlt, sizeof(hlt));
}
/////////////////////////////////////////////////////////////////////////////////////
