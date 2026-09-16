#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "idbFormat.h"
#include "sdbFormat.h"

#include "SaveLoader.h"
#include "SpellSystem.h"

#include <vector>
#include <list>
#include <unordered_map>
/////////////////////////////////////////////////////////////////////////////////////
class CWorld;
class CItem;
/////////////////////////////////////////////////////////////////////////////////////
struct SMaterialAmount
{
    const SIDBMaterial *pMaterial;
    int nMin, nMax;
    int nAmount;
    //
    SMaterialAmount(const SIDBMaterial *pNewMaterial, int nNewMin, int nNewMax, int nNewAmount)
        : pMaterial(pNewMaterial)
        , nMin(nNewMin)
        , nMax(nNewMax)
        , nAmount(nNewAmount)
    {
    }
};
/////////////////////////////////////////////////////////////////////////////////////
enum EAGItemType
{
    AGIT_WEAPON,
    AGIT_ARMOR,
    AGIT_QUICK,
    AGIT_WAND,
    AGIT_SPELL_PROTOTYPE,
    AGIT_RUNE,
    AGIT_SPELL,
};
/////////////////////////////////////////////////////////////////////////////////////
class CAssortmentGenerator
{
private:
    CWorld *pWorld;
    // current shop index
    int nShopID;
    // available shapes
    std::vector<const SIDBItemGeneralShape *> armorShapes;
    std::vector<const SIDBItemGeneralShape *> weaponShapes;
    std::vector<const SIDBItemGeneralShape *> quickitemShapes;
    // available material shapes
    std::vector<SMaterialAmount> materials;
    // available spell prototypes and modifiers
    std::vector<const SSDBSpellPrototype *> spellPrototypes;
    std::vector<const SSDBModifier *> spellModifiers;
    // complex spells generation data
    typedef std::unordered_map<std::string, std::list<const SSDBComplexSpell *>> CComplexSpellsMap;
    typedef std::unordered_map<std::string, const SSDBModifier *> CRunesMap;
    typedef std::list<CSmartPtr<CSpell>> CSpellsList;
    CComplexSpellsMap cspells;
    CRunesMap runes;
    //
    float fMinSkills[RPG_NUM_MAGIC_SUBTYPES];
    float fMaxSkills[RPG_NUM_MAGIC_SUBTYPES];

private:
    void GenerateItems(std::vector<const SIDBItemGeneralShape *> &shapes, int nAmount1,
                       int nAmount2, std::list<CItem *> &items, EAGItemType type);
    bool CanCastSpell(ERPGMagicSubtypes subtype, DWORD dwComplexity) const
    {
        return MINT(fMaxSkills[subtype]) >= static_cast<int>(dwComplexity);
    }
    bool HaveSpellPrototype(const std::string &szPrototype) const;
    void BuildTables(const std::vector<SSDBComplexSpell> &spells);
    void CreateSpells(CSpellsList &readySpells, float *fLocalMaxSkills, const int nRandomVal);

public:
    CAssortmentGenerator(CWorld *pNewWorld)
        : pWorld(pNewWorld)
        , nShopID(0) {};
    ~CAssortmentGenerator() { pWorld = 0; }
    // clear all shapes and materials
    void Clear();
    //
    void SetShopID(int _nShopID) { nShopID = _nShopID - 1; }
    // adding shapes
    void AddArmorShape(const SIDBArmorShape *pShape);
    void AddWeaponShape(const SIDBWeaponShape *pShape);
    void AddQuickItemShape(const SIDBQuickItemShape *pShape);
    void AddSpellPrototype(const SSDBSpellPrototype *pSpellPrototype);
    void AddSpellModifier(const SSDBModifier *pSpellModifier);
    // adding material (with amount range)
    void AddMaterial(const SIDBMaterial *pMaterial, int nMin, int nMax);
    void RemoveMaterial(const SIDBMaterial *pMaterial);
    void RestoreMaterials();
    // setting parameters
    void SetParameters(float *fMinLevels, float *fMaxLevels);
    // special function for spell and item generator sync.
    void ImportSpells(const CAssortmentGenerator &gen);
    // generating
    // instructions
    void GenerateArmorInstructions(int nAmount1, int nAmount2, std::list<CItem *> &items);
    void GenerateWeaponInstructions(int nAmount1, int nAmount2, std::list<CItem *> &items);
    void GenerateQuickItemInstructions(int nAmount1, int nAmount2, std::list<CItem *> &items);
    void GenerateSpellInstructions(int nAmount1, int nAmount2, std::list<CItem *> &items);
    void GenerateModifierInstructions(int nAmount1, int nAmount2, std::list<CItem *> &items);
    // items
    void GenerateArmors(int nAmount1, int nAmount2, std::list<CItem *> &items);
    void GenerateWeapons(int nAmount1, int nAmount2, std::list<CItem *> &items);
    void GenerateQuickItems(int nAmount1, int nAmount2, std::list<CItem *> &items);
    // materials
    void GenerateMaterials(int nAmount1, int nAmount2, std::list<CItem *> &items);
    // spells
    void GenerateSpells(int nAmount1, int nAmount2, std::list<CItem *> &items);
    // save/load assortment parameters
    void Load(CSave &file);
    void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
