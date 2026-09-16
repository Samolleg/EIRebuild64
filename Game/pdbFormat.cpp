#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "pdbFormat.h"

#include "BasicChunk.h"
/////////////////////////////////////////////////////////////////////////////////////
void SPDBGeneralAbility::Serialize(CStructureSaver &file)
{
    file.Add(0, szName);
    file.Add(1, szCode);
    file.Add(2, dwTextureTypeIndex);
}
/////////////////////////////////////////////////////////////////////////////////////
void SPDBSkill::Serialize(CStructureSaver &file)
{
    SPDBGeneralAbility::Serialize(file);

    file.AddT(10, szBaseAttributes);
}
/////////////////////////////////////////////////////////////////////////////////////
void SPDBPerk::Serialize(CStructureSaver &file)
{
    SPDBGeneralAbility::Serialize(file);

    file.Add(10, szPerkRequired);
    file.Add(11, szSkillRequired);
    file.AddRaw(12, &rpgAutoSkill, sizeof(rpgAutoSkill));
    file.AddRaw(13, &rpgManualSkill, sizeof(rpgManualSkill));
    file.Add(14, dwSkillLevel);
    file.Add(15, fStrength);
    file.Add(16, fDexterity);
    file.Add(17, fIntelligence);
    file.Add(18, dwCost);
    file.Add(19, dwModifier);
    file.Add(20, dwMult);
    file.Add(21, dwAdd);
    file.Add(22, bActive);
    file.Add(23, nPerkExclusive);
}
/////////////////////////////////////////////////////////////////////////////////////
