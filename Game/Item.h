#pragma once
namespace SaveSchema { class Writer; class Reader; }

#include "GameBase.h"
#include "PacketIDs.h"
#include "SpellSystem.h"
#include "RPGSystemConsts.h"
#include "idbFormat.h"
#include "SaveLoader.h"
#include "Basic.h"
#include "RpgSystem.h"

// ******************************************************************************** //
// **	Common item class header
// **
// **	Written by Ivanov Evgeny
// **	(C) Nival Interactive, 1998
// ** Modified by Yuri V. Blazhevich
// ** (C) Nival Interactive, 1999
// ** 10.06.99 16:50
// ******************************************************************************** //

// --------------------------------------------------------------------------------------------
// class CItem : public CGameBase
// NOTE: item's type (SHAPE + MATERIAL) are stored in 'dwSubtypeID'
// --------------------------------------------------------------------------------------------
class CItem : public CGameBase
{
private:
    CPtr<CObjectBase> pUserData;

protected:
    // add to save/load functions new fields!
    float fpHP, fpMaxHP;      // item's quality (health :)
    float fpMP, fpMaxMP;      // item's mana and mana capacity
    float fpRegenMP;          // mana regeneration rate
    float fpWeight;           // item's weight
    float fpPrice;            // item's price
    float fpSize;             // just size
    DWORD dwSpellSlots;       // total spell slots in this item
    CSmartPtr<CSpell> pSpell; // spell in this item
    int nCount;               // number of stocked items (most frquently - 1)
public:
    CItem();
    virtual ~CItem();
    // create/destroy functions
    virtual BOOL Create();
    virtual void Destroy();
    // item parameters setuping
    virtual void Setup(const SIDBItemGeneralShape *lpShape);
    //
    static DWORD GetShape(DWORD dwSubtype) { return ((dwSubtype >> 16) & 0xFFFF); }
    static DWORD GetMaterial(DWORD dwSubtype) { return (dwSubtype & 0xFFFF); }
    //
    DWORD GetShape() const { return GetShape(dwSubtypeID); }
    DWORD GetMaterial() const { return GetMaterial(dwSubtypeID); }
    //
    virtual BOOL Compare(CItem *lpItem);
    // item duplication
    CItem *Duplicate(CSpellManager *pSpellManager) const;
    // copy item to lpItem
    virtual BOOL Duplicate(CItem *lpItem, CSpellManager *pSpellManager) const;
    //
    const SIDBItemGeneralShape *GetShapeEntry() const;
    //
    int GetCount() const { return nCount; }
    void SetCount(int nNewCount) { nCount = nNewCount; }
    float GetWeight() const { return fpWeight; }
    float GetQuality() const { return fpHP / fpMaxHP; }
    float GetHP() const { return fpHP; }
    float GetMaxHP() const { return fpMaxHP; }
    float GetMP() const { return fpMP; }
    float GetManaRegenRate() const { return fpRegenMP; }
    void SetManaRegenRate(float value) { fpRegenMP = value; }
    float GetMaxMP() const { return fpMaxMP; }
    float GetSize() const { return fpSize; }
    void SetMP(float _fpMP) { fpMP = _fpMP; }
    virtual float GetPrice() const { return fpPrice; } // to remap in loot item
    virtual CString FormTextureNameMask() const { return ""; }
    virtual CString GetFigureName() const { return ""; }
    virtual CString GetI3DTextureName() const { return ""; }
    virtual CString GetI3DFigureName() const { return ""; }
    int GetTTIndex() const;
    int GetTTIndex2() const;
    // information, extracted from item database
    DWORD GetSpellSlots() const { return dwSpellSlots; }
    DWORD GetMaterialAmount() const { return GetShapeEntry()->dwComponents; }
    //
    LPCSTR GetMaterialCode() const;
    //
    void RegenerateMP() { fpMP = Min(fpMaxMP, fpMP + fpRegenMP); }
    void ChangeCurrentMP(float fpDMP) { fpMP += fpDMP; }
    void ApplyDamage(float fDamage)
    {
        if (nCount <= 0)
            fpHP -= fDamage;
        else
            fpHP -= fDamage / float(nCount);
    }
    void RestoreHP() { fpHP = fpMaxHP; }
    //
    bool IsBroken() const { return MINT(fpHP) <= 0; }
    // usability of item with science skill
    ERPGToolTypes GetToolType() { return (ERPGToolTypes)GetTool().dwTools; }
    BOOL IsTool() { return GetToolType() != RPGTOOL_NONE; }
    virtual SRPGScienceStats GetTool()
    {
        SRPGScienceStats tool;
        tool.dwTools = RPGTOOL_NONE;
        return tool;
    }
    //
    void AddSpell(CSpell *pSpell);
    void AddFreeSpell(CSpell *pSpell);
    int GetSpellsCount() { return pSpell != 0 ? 1 : 0; }
    CSpell *GetSpell(int nIdx) { return pSpell; }
    //
    CObjectBase *GetUserData() { return pUserData; }
    void AttachUserData(CObjectBase *pData) { pUserData = pData; }
    void FreeUserData() { pUserData = 0; }
    //
    virtual int Segment();
    //
    virtual void Load(CSave &file) = delete;
    virtual void Store(CSave &file) = delete;
    // In-process item packets use manager-local IDs, never slot ObjectId.
    virtual void StoreLocal(CSave &file) const;
    virtual void LoadLocal(CSave &file, CSpellManager &spells);
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
// --------------------------------------------------------------------------------------------
inline int CItem::GetTTIndex() const
{
    const SIDBItemGeneralShape *lpShape = GetShapeEntry();
    if (lpShape)
        return lpShape->dwTextureTypeIndex;
    else
        return -1;
}
inline int CItem::GetTTIndex2() const
{
    const SIDBItemGeneralShape *lpShape = GetShapeEntry();
    if (lpShape)
        return lpShape->dwTextureTypeIndex2;
    else
        return -1;
}

// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
class CItemWeapon : public CItem
{
protected:
    DWORD dwProficiencyClass; // proficiency class - skill to use on this weapon
    float fpDamageMin,
        fpDamageRnd; // damage random range: min value and random value (damage = min + Random(rnd))
    float fpRange;   // weapon attack range
public:
    CItemWeapon();
    virtual ~CItemWeapon() {}
    // item parameters setuping
    virtual void Setup(const SIDBItemGeneralShape *lpShape);
    //
    DWORD GetProficiencyClass() const { return dwProficiencyClass; }
    float GetRandomDamage() const;
    float GetDamageRandomRange() const { return fpDamageRnd; }
    float GetDamageMin() const { return fpDamageMin; }
    float GetDamageMax() const { return fpDamageMin + fpDamageRnd; }
    float GetRange() const { return fpRange; }
    void GetDamageDistribution(float *fpAttacks);
    virtual CString FormTextureNameMask() const;
    static void FormTextureNameMask(CString &szName, const SIDBItemGeneralShape *pShape,
                                    const SIDBMaterial *pMaterial);
    static void FormTextureNameMask(CString &szName, DWORD dwShape, DWORD dwMaterial);
    virtual CString GetFigureName() const;
    virtual CString GetI3DTextureName() const;
    virtual CString GetI3DFigureName() const;
    void GetWeaponModelNames(CStringArray &szNames);
    static void GetWeaponModelNames(DWORD dwShape, CStringArray &szNames);
    static void GetWeaponModelNames(DWORD dwShape, CStringArray &szNames, bool bRightHand);

    static void GetWeaponModelNames(DWORD dwWeapon, std::vector<std::string> &szNames);
    bool GetCastPointName(CString &szName);
    static bool GetCastPointName(DWORD dwShape, CString &szName);
    ERPGWeaponTypes GetWeaponType() const;
    static bool IsRanged(DWORD dwWeapon);
    // data from shape+material
    static float GetDamageMin(DWORD dwWeapon);
    static float GetDamageMax(DWORD dwWeapon);
    static ERPGWeaponTypes GetWeaponType(DWORD dwWeapon);
    static float GetWeight(DWORD dwWeapon);
    static ERPGAttackTypes GetAttackType(DWORD dwWeapon);
    static float GetSkillModifier(DWORD dwWeapon, ERPGAutoSkills skill);
    static float GetAtackBonus(DWORD dwWeapon);
    static float GetDefenceBonus(DWORD dwWeapon);
    // skill modifiers
    DWORD GetSkillModifier(ERPGAutoSkills skill) const
    {
        return static_cast<const SIDBWeaponShape *>(GetShapeEntry())->GetSkillModifier(skill);
    }
    DWORD GetSkillModifier(ERPGManualSkills skill) const
    {
        return static_cast<const SIDBWeaponShape *>(GetShapeEntry())->GetSkillModifier(skill);
    }
    float GetAtackBonus() const { return CItemWeapon::GetAtackBonus(GetSubtypeID()); }
    float GetDefenceBonus() const { return CItemWeapon::GetDefenceBonus(GetSubtypeID()); }
    // action points
    DWORD GetActionPoints() const
    {
        return static_cast<const SIDBWeaponShape *>(GetShapeEntry())->dwActionPoints;
    }
    //
    virtual BOOL Compare(CItem *lpItem);
    // item duplication
    CItemWeapon *Duplicate(CSpellManager *pSpellManager) const
    {
        return (CItemWeapon *)CItem::Duplicate(pSpellManager);
    }
    virtual BOOL Duplicate(CItem *lpItem, CSpellManager *pSpellManager) const;
    //
    virtual void Load(CSave &file) = delete;
    virtual void Store(CSave &file) = delete;
    // In-process item packets use manager-local IDs, never slot ObjectId.
    virtual void StoreLocal(CSave &file) const;
    virtual void LoadLocal(CSave &file, CSpellManager &spells);
    // identification
    virtual LPCSTR GetGameClassName() const { return "CItemWeapon"; }
    virtual EClassIDs WhoAmI() const { return HPC_ITEM_WEAPON; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
// --------------------------------------------------------------------------------------------
// Armor.
// can cover up to 3 hit locations (HL)
// each covered HL can be protected with unique absorbtions and resistances on each armor layer
// fpAbsorbtions[i] belongs to cHLs[i] hit location (cHLs one can get from item's shape database)
// --------------------------------------------------------------------------------------------
class CItemArmor : public CItem
{
protected:
    float fpAbsorbtions[2][RPG_NUM_ATTACK_TYPES]; // absorbtion for each attack type and for all
                                                  // covered HL
    DWORD dwCrippleMasks[2];                      // cripple masks
public:
    CItemArmor();
    virtual ~CItemArmor() {}
    // item parameters setuping
    virtual void Setup(const SIDBItemGeneralShape *lpShape);
    void GetAbsorbtions(float *absorbtions, DWORD dwIndex) const
    {
        memcpy(absorbtions, fpAbsorbtions[dwIndex], sizeof(float) * RPG_NUM_ATTACK_TYPES);
    }
    DWORD GetCrippleMask(DWORD dwIndex) const { return dwCrippleMasks[dwIndex]; }
    //
    int GetHLIndex() const { return 0; }
    int GetALIndex() const;
    static int GetALIndex(ERPGArmorTypes type);
    virtual CString FormTextureNameMask() const;
    static void FormTextureNameMask(CString &szName, const SIDBItemGeneralShape *pShape,
                                    const SIDBMaterial *pMaterial);
    static void FormTextureNameMask(CString &szName, DWORD dwShape, DWORD dwMaterial);
    virtual CString GetFigureName() const;
    virtual CString GetI3DTextureName() const;
    virtual CString GetI3DFigureName() const;
    ERPGArmorTypes GetArmorType() const;
    bool IsUpper() const { return CItemArmor::IsUpper(GetSubtypeID()); }
    static bool IsUpper(DWORD dwArmor);
    // wear order
    DWORD GetWearOrder() const
    {
        return static_cast<const SIDBArmorShape *>(GetShapeEntry())->dwWearOrder;
    }
    static DWORD GetWearOrder(DWORD dwShape);
    // skill modifiers
    DWORD GetSkillModifier(ERPGAutoSkills skill) const
    {
        return static_cast<const SIDBArmorShape *>(GetShapeEntry())->GetSkillModifier(skill);
    }
    DWORD GetSkillModifier(ERPGManualSkills skill) const
    {
        return static_cast<const SIDBArmorShape *>(GetShapeEntry())->GetSkillModifier(skill);
    }
    //
    int GetSlotIndex() const { return GetArmorType(); } // get slot-to-wear index
    bool IsApplyWounds() const
    {
        return static_cast<const SIDBArmorShape *>(GetShapeEntry())->bApplyWounds;
    }
    //
    virtual BOOL Compare(CItem *lpItem);
    // item duplication
    CItemArmor *Duplicate(CSpellManager *pSpellManager) const
    {
        return (CItemArmor *)CItem::Duplicate(pSpellManager);
    }
    virtual BOOL Duplicate(CItem *lpItem, CSpellManager *pSpellManager) const;
    //
    virtual void Load(CSave &file) = delete;
    virtual void Store(CSave &file) = delete;
    // In-process item packets use manager-local IDs, never slot ObjectId.
    virtual void StoreLocal(CSave &file) const;
    virtual void LoadLocal(CSave &file, CSpellManager &spells);
    // identification
    virtual LPCSTR GetGameClassName() const { return "CItemArmor"; }
    virtual EClassIDs WhoAmI() const { return HPC_ITEM_ARMOR; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
class CItemQuick : public CItem
{
protected:
    DWORD dwSkill;

public:
    CItemQuick();
    virtual ~CItemQuick();
    //
    virtual void Setup(const SIDBItemGeneralShape *lpShape);
    //
    virtual BOOL Compare(CItem *lpItem);
    // item duplication
    CItemQuick *Duplicate(CSpellManager *pSpellManager) const
    {
        return (CItemQuick *)CItem::Duplicate(pSpellManager);
    }
    virtual BOOL Duplicate(CItem *lpItem, CSpellManager *pSpellManager) const;
    // skill modifiers
    DWORD GetSkillModifier(ERPGAutoSkills skill) const
    {
        return static_cast<const SIDBQuickItemShape *>(GetShapeEntry())->GetSkillModifier(skill);
    }
    DWORD GetSkillModifier(ERPGManualSkills skill) const
    {
        return static_cast<const SIDBQuickItemShape *>(GetShapeEntry())->GetSkillModifier(skill);
    }
    //
    virtual CString GetI3DTextureName() const;
    virtual CString GetI3DFigureName() const;
    //
    EIDBQuickItemType GetQuickItemType() const;
    //
    virtual void Load(CSave &file) = delete;
    virtual void Store(CSave &file) = delete;
    // In-process item packets use manager-local IDs, never slot ObjectId.
    virtual void StoreLocal(CSave &file) const;
    virtual void LoadLocal(CSave &file, CSpellManager &spells);
    // identification
    virtual LPCSTR GetGameClassName() const { return "CItemQuick"; }
    virtual EClassIDs WhoAmI() const { return HPC_ITEM_QUICK; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
class CItemQuest : public CItem
{
protected:
    DWORD dwScriptID;

public:
    CItemQuest();
    virtual ~CItemQuest();
    //
    virtual void Setup(const SIDBItemGeneralShape *lpShape);
    //
    virtual BOOL Compare(CItem *lpItem);
    // item duplication
    CItemQuest *Duplicate(CSpellManager *pSpellManager) const
    {
        return (CItemQuest *)CItem::Duplicate(pSpellManager);
    }
    virtual BOOL Duplicate(CItem *lpItem, CSpellManager *pSpellManager) const;

    virtual CString GetI3DTextureName() const;
    virtual CString GetI3DFigureName() const;
    // usability of item with science skill
    virtual SRPGScienceStats GetTool()
    {
        SRPGScienceStats tool;
        tool.dwTools = RPGTOOL_KEY;
        tool.dwKeyNumber = dwScriptID;
        return tool;
    }
    //
    virtual void Load(CSave &file) = delete;
    virtual void Store(CSave &file) = delete;
    // In-process item packets use manager-local IDs, never slot ObjectId.
    virtual void StoreLocal(CSave &file) const;
    virtual void LoadLocal(CSave &file, CSpellManager &spells);
    //
    DWORD GetScriptID() const { return dwScriptID; }
    // identification
    virtual LPCSTR GetGameClassName() const { return "CItemQuest"; }
    virtual EClassIDs WhoAmI() const { return HPC_ITEM_QUEST; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
class CItemLoot : public CItem
{
private:
    EIDBLootItemType litType;              // loot item type
    EIDBInstructionType itInstructionType; // instruction type (if loot item type == instruction)
    DWORD dwMaterialIndex;                 // index of the material
public:
    CItemLoot();
    virtual ~CItemLoot();
    //
    virtual void Setup(const SIDBItemGeneralShape *lpShape);
    //
    virtual BOOL Compare(CItem *lpItem);
    //
    virtual float GetPrice() const;
    // item duplication
    CItemLoot *Duplicate(CSpellManager *pSpellManager) const
    {
        return (CItemLoot *)CItem::Duplicate(pSpellManager);
    }
    virtual BOOL Duplicate(CItem *lpItem, CSpellManager *pSpellManager) const;

    virtual CString GetI3DTextureName() const;
    virtual CString GetI3DFigureName() const;
    //
    EIDBLootItemType GetLootType() const { return litType; }
    static const SIDBItemGeneralShape *ResolveInstructionShape(EIDBInstructionType type, DWORD index);
    const SIDBItemGeneralShape *GetInstructionShape() const { return ResolveInstructionShape(itInstructionType, dwMaterialIndex); }
    static void ValidateInstructionReference(EIDBInstructionType type, DWORD index);
    void ValidateInstruction() const { if (litType == IDBLIT_INSTRUCTION) ValidateInstructionReference(itInstructionType, dwMaterialIndex); }
    EIDBInstructionType GetInstructionType() const { return itInstructionType; }
    DWORD GetMaterialIndex() const { return dwMaterialIndex; }
    DWORD GetInstructionItemIndex() const { return dwMaterialIndex; }
    void SetInstructionItemIndex(DWORD dwIndex) { if (litType == IDBLIT_INSTRUCTION) ValidateInstructionReference(itInstructionType, dwIndex); dwMaterialIndex = dwIndex; }
    //
    virtual void Load(CSave &file) = delete;
    virtual void Store(CSave &file) = delete;
    // In-process item packets use manager-local IDs, never slot ObjectId.
    virtual void StoreLocal(CSave &file) const;
    virtual void LoadLocal(CSave &file, CSpellManager &spells);
    // identification
    virtual LPCSTR GetGameClassName() const { return "CItemLoot"; }
    virtual EClassIDs WhoAmI() const { return HPC_ITEM_LOOT; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
class CItemSpell : public CItemQuick
{
public:
    CItemSpell();
    virtual ~CItemSpell();
    // item duplication
    CItemSpell *Duplicate(CSpellManager *pSpellManager) const
    {
        return (CItemSpell *)CItemQuick::Duplicate(pSpellManager);
    }
    virtual BOOL Duplicate(CItem *lpItem, CSpellManager *pSpellManager) const;
    //
    CSpell *GetSpell()
    {
        ASSERT(pSpell != 0);
        return pSpell;
    }

    virtual CString GetI3DTextureName() const;
    virtual CString GetI3DFigureName() const;
    // identification
    virtual LPCSTR GetGameClassName() const { return "CItemSpell"; }
    virtual EClassIDs WhoAmI() const { return HPC_ITEM_SPELL; }
};
// --------------------------------------------------------------------------------------------
extern void AddItemToLists(CGameBaseList &items, CGameBaseList &spells, CItem *pItem);
