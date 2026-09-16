#pragma once

#include "TypeIDs.h"
#include "RPGSystemConsts.h"

#include <vector>

class CStructureSaver;

// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
struct SIDBMaterial
{
public:
    // names
    CString szName;     // name of this material
    CString szType;     // type: cloth, leather, bone, stone, metal, crystal
    CString szCodeName; // code name (2 letters) to identify material to the texture
    DWORD dwTypeIndex;
    // general modifiers
    float fpPrice;        // price
    float fpWeight;       // weight
    float fpManaCapacity; // mana capacity
    DWORD dwSpellSlots;   // spell slots
    float fpDurability;   // durability
    // skill modifier
    DWORD dwSkill;
    // damage  modifier
    float fpDamageModifier; // this material damage modifier
    // absorbtion/cripple mask
    float fpAbsorbtionMultipliers[RPG_NUM_ATTACK_TYPES]; // this material absorbtion multipliers
    DWORD dwCrippleMask;
    //
    DWORD dwAvailableInShop; // set bit for each shop, this material available in
public:
    void Serialize(CStructureSaver &file);

    LPCSTR GetCode() const { return szName; }

    bool IsAvailableInShop(int nNumber) const { return (dwAvailableInShop & (1 << nNumber)) != 0; }
};

// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
struct SIDBItemGeneralShape
{
public:
    // names
    CString szName; // name of this shape
    CString
        szType; // proficiency class for weapon: sword, axe, dagger, spear, hammer, bow, crossbow
                // armor shape type for armor: helm, plate, leggings; shirt, boots, gloves
    DWORD dwTypeID;
    CString szMaterialType; // material type (see SIDBMaterial structure)
    DWORD dwMaterialTypeIndex;
    // graphics data
    DWORD dwTextureTypeIndex;  // index, which corresponds texture 1st number
    DWORD dwTextureTypeIndex2; // index, which corresponds texture 2nd number
    CStringArray szFigures;    // figure names, this item consist of
    // general modifiers
    float fpPrice;           // base price
    float fpWeight;          // weight
    float fpSize;            // size
    float fpManaCapacity;    // mana capacity
    DWORD dwSpellSlots;      // spell slots
    float fpDurability;      // durability
    DWORD dwComponents;      // number of material components to create this item
    CStringArray szZones;    // zones, this item will be used in. all zones, if array is empty
    DWORD dwAvailableInShop; // set bit for each shop, this shape available in
    //
    bool bDeconstructable; // можно ли его раззобрать в конструкторе
public:
    SIDBItemGeneralShape() { bDeconstructable = true; }
    virtual ~SIDBItemGeneralShape() = default;
    SIDBItemGeneralShape(const SIDBItemGeneralShape &shape);

    SIDBItemGeneralShape &operator=(const SIDBItemGeneralShape &shape);

    void Serialize(CStructureSaver &file);

    LPCSTR GetCode() const { return szName; }

    bool IsAvailableInShop(int nNumber) const { return (dwAvailableInShop & (1 << nNumber)) != 0; }
    bool IsDeconstructable() const { return bDeconstructable; }

    virtual ETypeIDs GetTypeID() const = 0;
};
// --------------------------------------------------------------------------------------------
inline SIDBItemGeneralShape &SIDBItemGeneralShape::operator=(const SIDBItemGeneralShape &shape)
{
    szName = shape.szName;
    szType = shape.szType;
    dwTypeID = shape.dwTypeID;
    szMaterialType = shape.szMaterialType;
    dwMaterialTypeIndex = shape.dwMaterialTypeIndex;
    dwTextureTypeIndex = shape.dwTextureTypeIndex;
    dwTextureTypeIndex2 = shape.dwTextureTypeIndex2;
    fpPrice = shape.fpPrice;
    fpWeight = shape.fpWeight;
    fpSize = shape.fpSize;
    fpManaCapacity = shape.fpManaCapacity;
    dwSpellSlots = shape.dwSpellSlots;
    fpDurability = shape.fpDurability;
    dwComponents = shape.dwComponents;
    bDeconstructable = shape.bDeconstructable;

    szFigures.Copy(shape.szFigures);

    return *this;
}
inline SIDBItemGeneralShape::SIDBItemGeneralShape(const SIDBItemGeneralShape &shape)
{
    *this = shape;
}

// --------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------
struct SIDBWeaponShape : public SIDBItemGeneralShape
{
public:
    // specific data
    DWORD dwActionPoints;     // number of action points to attack with this weapon
    DWORD dwStatModifiers[2]; // skill/stat modifiers. 0 => end
    BYTE cManualSkillModifier[RPG_NUM_MANUAL_SKILLS];
    BYTE cAutoSkillModifier[RPG_NUM_AUTO_SKILLS];
    float fAttackBonus;
    float fDefenceBonus;
    float fpRange;        // weapon attack range
    float fpDamageMin;    // damage MIN value (will be affected by material's damage modifier)
    float fpDamageRandom; // damage random value (will be affected by material's damage modifier).
                          // damage = damageMin + Random(damageRandom)
    float fpDamageDistributions[RPG_NUM_ATTACK_TYPES]; // percents of damage distribution by attack
                                                       // types
public:
    SIDBWeaponShape()
    {
        memset(cManualSkillModifier, 0, sizeof(cManualSkillModifier));
        memset(cAutoSkillModifier, 0, sizeof(cAutoSkillModifier));
    }
    SIDBWeaponShape(const SIDBWeaponShape &shape);
    SIDBWeaponShape &operator=(const SIDBWeaponShape &shape);

    float GetSkillModifier(ERPGManualSkills skill) const { return cManualSkillModifier[skill]; }
    float GetSkillModifier(ERPGAutoSkills skill) const { return cAutoSkillModifier[skill]; }
    float GetAttackBonus() const { return fAttackBonus; }
    float GetDefenceBonus() const { return fDefenceBonus; }

    void Serialize(CStructureSaver &file);

    virtual ETypeIDs GetTypeID() const { return ITID_ITEM_WEAPON; };
};
// --------------------------------------------------------------------------------------------
inline SIDBWeaponShape &SIDBWeaponShape::operator=(const SIDBWeaponShape &shape)
{
    this->SIDBItemGeneralShape::operator=(shape);

    dwActionPoints = shape.dwActionPoints;
    dwStatModifiers[0] = shape.dwStatModifiers[0];
    dwStatModifiers[1] = shape.dwStatModifiers[1];
    fpRange = shape.fpRange;
    fpDamageMin = shape.fpDamageMin;
    fpDamageRandom = shape.fpDamageRandom;
    memcpy(fpDamageDistributions, shape.fpDamageDistributions, sizeof(fpDamageDistributions));

    memcpy(cManualSkillModifier, shape.cManualSkillModifier, sizeof(cManualSkillModifier));
    memcpy(cAutoSkillModifier, shape.cAutoSkillModifier, sizeof(cAutoSkillModifier));

    return *this;
}
inline SIDBWeaponShape::SIDBWeaponShape(const SIDBWeaponShape &shape)
{
    *this = shape;
}

// --------------------------------------------------------------------------------------------
struct SIDBArmorAbsorbtion
{
public:
    float fpAbsorbtion;
    float fpDistributions[RPG_NUM_ATTACK_TYPES];
    DWORD dwCrippleMask;
};
// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
struct SIDBArmorShape : public SIDBItemGeneralShape
{
public:
    // specific data
    SIDBArmorAbsorbtion main;
    SIDBArmorAbsorbtion second;
    bool bApplyWounds;
    DWORD dwWearOrder;
    BYTE cManualSkillModifier[RPG_NUM_MANUAL_SKILLS];
    BYTE cAutoSkillModifier[RPG_NUM_AUTO_SKILLS];

public:
    SIDBArmorShape() {}
    SIDBArmorShape(const SIDBArmorShape &shape);
    SIDBArmorShape &operator=(const SIDBArmorShape &shape);

    float GetSkillModifier(ERPGManualSkills skill) const { return cManualSkillModifier[skill]; }
    float GetSkillModifier(ERPGAutoSkills skill) const { return cAutoSkillModifier[skill]; }

    bool IsUpper() const
    {
        return ((dwTypeID == RPGAST_HELM) || (dwTypeID == RPGAST_PLATE) ||
                (dwTypeID == RPGAST_LEGGINGS));
    }

    void Serialize(CStructureSaver &file);

    virtual ETypeIDs GetTypeID() const { return ITID_ITEM_ARMOR; };
};
// --------------------------------------------------------------------------------------------
inline SIDBArmorShape &SIDBArmorShape::operator=(const SIDBArmorShape &shape)
{
    this->SIDBItemGeneralShape::operator=(shape);

    memcpy(&main, &shape.main, sizeof(main));
    memcpy(&second, &shape.second, sizeof(second));

    return *this;
}
inline SIDBArmorShape::SIDBArmorShape(const SIDBArmorShape &shape)
{
    *this = shape;
}

// --------------------------------------------------------------------------------------------
enum EIDBQuickItemType
{
    IDB_DANGER_SENSOR,
    IDB_PERSUADER,
    IDB_TRAP,
    IDB_PICKLOCK,
    IDB_CROWBAR,
    IDB_WAND,
    IDB_KEY,
    IDB_PLUG,
    IDB_SCROLL,

    IDB_QUICK_ITEM_FORCE_DWORD = 0x7FFFFFFF
};
// --------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------
struct SIDBQuickItemShape : public SIDBItemGeneralShape
{
public:
    EIDBQuickItemType idbType;
    DWORD dwSkill;
    float fDamage;
    ERPGAttackTypes rpgDamageType;
    CString szSpell;

    BYTE cManualSkillModifier[RPG_NUM_MANUAL_SKILLS];
    BYTE cAutoSkillModifier[RPG_NUM_AUTO_SKILLS];

public:
    SIDBQuickItemShape()
    {
        memset(cManualSkillModifier, 0, sizeof(cManualSkillModifier));
        memset(cAutoSkillModifier, 0, sizeof(cAutoSkillModifier));
    }
    SIDBQuickItemShape(const SIDBQuickItemShape &shape);
    SIDBQuickItemShape &operator=(const SIDBQuickItemShape &shape);

    float GetSkillModifier(ERPGManualSkills skill) const { return cManualSkillModifier[skill]; }
    float GetSkillModifier(ERPGAutoSkills skill) const { return cAutoSkillModifier[skill]; }

    void Serialize(CStructureSaver &file);

    virtual ETypeIDs GetTypeID() const { return ITID_ITEM_QUICK; };
};
// --------------------------------------------------------------------------------------------
inline SIDBQuickItemShape &SIDBQuickItemShape::operator=(const SIDBQuickItemShape &shape)
{
    this->SIDBItemGeneralShape::operator=(shape);
    dwSkill = shape.dwSkill;
    memcpy(cManualSkillModifier, shape.cManualSkillModifier, sizeof(cManualSkillModifier));
    memcpy(cAutoSkillModifier, shape.cAutoSkillModifier, sizeof(cAutoSkillModifier));

    return *this;
}
inline SIDBQuickItemShape::SIDBQuickItemShape(const SIDBQuickItemShape &shape)
{
    *this = shape;
}
/////////////////////////////////////////////////////////////////////////////////////
struct SIDBQuestItemShape : public SIDBItemGeneralShape
{
public:
    DWORD dwScriptID;             // material index in material array
    std::vector<CString> szZones; // зоны, на которых показывать этот quest item
public:
    SIDBQuestItemShape() {}
    SIDBQuestItemShape(const SIDBQuestItemShape &shape);
    SIDBQuestItemShape &operator=(const SIDBQuestItemShape &shape);

    void Serialize(CStructureSaver &file);

    virtual ETypeIDs GetTypeID() const { return ITID_ITEM_QUEST; }
};
/////////////////////////////////////////////////////////////////////////////////////
inline SIDBQuestItemShape &SIDBQuestItemShape::operator=(const SIDBQuestItemShape &shape)
{
    this->SIDBItemGeneralShape::operator=(shape);
    dwScriptID = shape.dwScriptID;

    return *this;
}
inline SIDBQuestItemShape::SIDBQuestItemShape(const SIDBQuestItemShape &shape)
{
    *this = shape;
}
/////////////////////////////////////////////////////////////////////////////////////
enum EIDBLootItemType
{
    IDBLIT_TREASURE,
    IDBLIT_MATERIAL,
    IDBLIT_INSTRUCTION,
    IDBLIT_FORCE_DWORD = 0x7FFFFFFF
};
enum EIDBInstructionType
{
    IDBINS_WEAPON,
    IDBINS_ARMOR,
    IDBINS_QUICK,
    IDBINS_PROTOTYPE,
    IDBINS_RUNE,
    IDBINS_FORCE_DWORD = 0x7FFFFFFF
};
/////////////////////////////////////////////////////////////////////////////////////
struct SIDBLootItemShape : public SIDBItemGeneralShape
{
public:
    EIDBLootItemType litType;              // type as EIDBLootItemType constant
    EIDBInstructionType itInstructionType; //
    DWORD dwMaterialIndex; // material index in material array (of index of the prototype for
                           // instruction)
public:
    SIDBLootItemShape() {}
    SIDBLootItemShape(const SIDBLootItemShape &shape);
    SIDBLootItemShape &operator=(const SIDBLootItemShape &shape);

    void Serialize(CStructureSaver &file);

    bool IsInstruction() const { return litType == IDBLIT_INSTRUCTION; }
    bool IsTreasure() const { return litType == IDBLIT_TREASURE; }
    bool IsMaterial() const { return litType == IDBLIT_MATERIAL; }

    EIDBInstructionType GetInstructionType() const { return itInstructionType; }
    DWORD GetMaterialIndex() const { return dwMaterialIndex; }

    virtual ETypeIDs GetTypeID() const { return ITID_ITEM_LOOT; }
};
/////////////////////////////////////////////////////////////////////////////////////
inline SIDBLootItemShape &SIDBLootItemShape::operator=(const SIDBLootItemShape &shape)
{
    this->SIDBItemGeneralShape::operator=(shape);
    litType = shape.litType;
    itInstructionType = shape.itInstructionType;
    dwMaterialIndex = shape.dwMaterialIndex;

    return *this;
}
inline SIDBLootItemShape::SIDBLootItemShape(const SIDBLootItemShape &shape)
{
    *this = shape;
}
