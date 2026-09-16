#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "idbFormat.h"

#include "BasicChunk.h"
/////////////////////////////////////////////////////////////////////////////////////
void SIDBMaterial::Serialize(CStructureSaver &file)
{
    // names
    file.Add(0, szName);
    file.Add(1, szType);
    file.Add(2, szCodeName);
    file.Add(3, dwTypeIndex);
    // general modifiers
    file.Add(4, fpPrice);
    file.Add(5, fpWeight);
    file.Add(6, fpManaCapacity);
    file.Add(7, dwSpellSlots);
    file.Add(8, fpDurability);
    // skill modifier
    file.Add(9, dwSkill);
    // damage  modifier
    file.Add(10, fpDamageModifier);
    // absorbtion/cripple mask
    file.AddRaw(11, fpAbsorbtionMultipliers, sizeof(fpAbsorbtionMultipliers));
    file.Add(12, dwCrippleMask);
    //
    file.Add(13, dwAvailableInShop);
}
/////////////////////////////////////////////////////////////////////////////////////
void SIDBItemGeneralShape::Serialize(CStructureSaver &file)
{
    file.Add(0, szName);
    file.Add(1, szType);
    // armor shape type for armor: helm, plate, leggings; shirt, boots, gloves
    file.Add(2, dwTypeID);
    file.Add(3, szMaterialType);
    file.Add(4, dwMaterialTypeIndex);
    // graphics data
    file.Add(5, dwTextureTypeIndex);
    file.Add(6, dwTextureTypeIndex2);
    // general modifiers
    file.Add(7, fpPrice);
    file.Add(8, fpWeight);
    file.Add(9, fpSize);
    file.Add(10, fpManaCapacity);
    file.Add(11, dwSpellSlots);
    file.Add(12, fpDurability);
    file.Add(13, dwComponents);
    //
    file.Add(14, dwAvailableInShop);
    //
    file.Add(15, bDeconstructable);
}
/////////////////////////////////////////////////////////////////////////////////////
void SIDBWeaponShape::Serialize(CStructureSaver &file)
{
    SIDBItemGeneralShape::Serialize(file);

    file.Add(21, dwActionPoints);
    file.AddRaw(22, dwStatModifiers, sizeof(dwStatModifiers));
    file.Add(23, fpRange);
    file.Add(24, fpDamageMin);
    file.Add(25, fpDamageRandom);
    file.AddRaw(26, fpDamageDistributions, sizeof(fpDamageDistributions));
    file.AddRaw(27, cManualSkillModifier, sizeof(cManualSkillModifier));
    file.AddRaw(28, cAutoSkillModifier, sizeof(cAutoSkillModifier));
    //
    file.Add(29, fAttackBonus);
    file.Add(30, fDefenceBonus);
}
/////////////////////////////////////////////////////////////////////////////////////
void SIDBArmorShape::Serialize(CStructureSaver &file)
{
    SIDBItemGeneralShape::Serialize(file);

    file.AddRaw(21, &main, sizeof(main));
    file.AddRaw(22, &second, sizeof(second));

    file.Add(23, bApplyWounds);

    file.Add(24, dwWearOrder);
    file.AddRaw(25, cManualSkillModifier, sizeof(cManualSkillModifier));
    file.AddRaw(26, cAutoSkillModifier, sizeof(cAutoSkillModifier));
}
/////////////////////////////////////////////////////////////////////////////////////
void SIDBQuickItemShape::Serialize(CStructureSaver &file)
{
    SIDBItemGeneralShape::Serialize(file);

    file.AddRaw(21, &idbType, sizeof(idbType));
    file.Add(22, dwSkill);
    file.Add(23, fDamage);
    file.AddRaw(24, &rpgDamageType, sizeof(rpgDamageType));
    file.Add(25, szSpell);

    file.AddRaw(26, cManualSkillModifier, sizeof(cManualSkillModifier));
    file.AddRaw(27, cAutoSkillModifier, sizeof(cAutoSkillModifier));
}
/////////////////////////////////////////////////////////////////////////////////////
void SIDBQuestItemShape::Serialize(CStructureSaver &file)
{
    SIDBItemGeneralShape::Serialize(file);
    file.Add(21, dwScriptID);
    file.AddT(22, szZones);
}
/////////////////////////////////////////////////////////////////////////////////////
void SIDBLootItemShape::Serialize(CStructureSaver &file)
{
    SIDBItemGeneralShape::Serialize(file);

    file.AddRaw(21, &litType, sizeof(litType));
    file.AddRaw(22, &itInstructionType, sizeof(itInstructionType));
    file.Add(23, dwMaterialIndex);
}
/////////////////////////////////////////////////////////////////////////////////////
