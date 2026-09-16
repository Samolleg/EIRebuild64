#pragma once
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                                                                            ** //
// **                      Perk/skill database description                       ** //
// **                                                                            ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                general representation of the ability                       ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
#include "RPGSystemConsts.h"

#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
class CStructureSaver;
/////////////////////////////////////////////////////////////////////////////////////
struct SPDBGeneralAbility
{
public:
    CString szName;           // name of this skill/perk
    CString szCode;           // codename
    DWORD dwTextureTypeIndex; // texture type index (texture name will be skill####, where #### ==
                              // dwTextureTypeIndex)
public:
    void Serialize(CStructureSaver &file);
};
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                     representation of the skill                            ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
struct SPDBSkill : public SPDBGeneralAbility
{
public:
    std::vector<CString> szBaseAttributes; // attributes, this skill based on
public:
    void Serialize(CStructureSaver &file);
};
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                       representation of the perk                           ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
struct SPDBPerk : public SPDBGeneralAbility
{
public:
    CString szPerkRequired;          // perk, which required to get this one
    int nPerkExclusive;              // mutually exclusive perk group ID
    CString szSkillRequired;         // skill, which required to get this perk
    ERPGAutoSkills rpgAutoSkill;     // the same, as a string above, but in non-string form
    ERPGManualSkills rpgManualSkill; //
    DWORD dwSkillLevel;              // required skill level
    union
    {
        struct
        {
            float fStrength;
            float fDexterity;
            float fIntelligence;
        };
        float fpAttributes[3]; // requirements on base atributes (str/dex/int)
    };
    DWORD dwCost; // cost of this perk in char points
    DWORD dwModifier;
    DWORD dwMult;
    DWORD dwAdd;
    bool bActive;

public:
    void Serialize(CStructureSaver &file);
};
