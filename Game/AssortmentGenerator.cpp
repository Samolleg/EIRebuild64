#include "StdAfx.h"

#include <random>
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "AssortmentGenerator.h"

#include "World.h"

#include "SpellSystem.h"
#include "SpellManager.h"
#include "SpellConstructor.h"

#include "Item.h"
#include "ItemManager.h"
#include "ItemConstructor.h"

#include "RandomGen.h"

#include <algorithm>
/////////////////////////////////////////////////////////////////////////////////////
namespace NAssortmentGenerator {
std::vector<const SIDBItemGeneralShape *> selectedShapes;
std::vector<const SSDBSpellPrototype *> selectedSpellPrototypes;
std::vector<const SSDBModifier *> selectedSpellModifiers;
std::vector<SMaterialAmount *> selectedMaterials;
std::vector<SMaterialAmount> selectedMaterials2;
std::random_device rd;
std::mt19937 rng(rd());
//
class CMaterialMatchFunctional
{
private:
    const SIDBMaterial *pMaterial;

public:
    explicit CMaterialMatchFunctional(const SIDBMaterial *pNewMaterial)
        : pMaterial(pNewMaterial)
    {
    }
    bool operator()(const SMaterialAmount &ma) { return ma.pMaterial == pMaterial; }
};
}; // namespace NAssortmentGenerator
using namespace NAssortmentGenerator;
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::Clear()
{
    nShopID = 0;
    armorShapes.clear();
    weaponShapes.clear();
    quickitemShapes.clear();
    materials.clear();
    spellPrototypes.clear();
    spellModifiers.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::AddArmorShape(const SIDBArmorShape *pShape)
{
    if (pShape && std::find(armorShapes.begin(), armorShapes.end(), pShape) == armorShapes.end())
        armorShapes.push_back(pShape);
}
void CAssortmentGenerator::AddWeaponShape(const SIDBWeaponShape *pShape)
{
    if (pShape && std::find(weaponShapes.begin(), weaponShapes.end(), pShape) == weaponShapes.end())
        weaponShapes.push_back(pShape);
}
void CAssortmentGenerator::AddQuickItemShape(const SIDBQuickItemShape *pShape)
{
    if (pShape &&
        std::find(quickitemShapes.begin(), quickitemShapes.end(), pShape) == quickitemShapes.end())
        quickitemShapes.push_back(pShape);
}
void CAssortmentGenerator::AddSpellPrototype(const SSDBSpellPrototype *pSpellPrototype)
{
    if (pSpellPrototype && std::find(spellPrototypes.begin(), spellPrototypes.end(),
                                     pSpellPrototype) == spellPrototypes.end())
        spellPrototypes.push_back(pSpellPrototype);
}
void CAssortmentGenerator::AddSpellModifier(const SSDBModifier *pSpellModifier)
{
    if (pSpellModifier && std::find(spellModifiers.begin(), spellModifiers.end(), pSpellModifier) ==
                              spellModifiers.end())
        spellModifiers.push_back(pSpellModifier);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::AddMaterial(const SIDBMaterial *pMaterial, int nMin, int nMax)
{
    if (!pMaterial)
        return;
    auto iter = std::find_if(materials.begin(), materials.end(),
                             CMaterialMatchFunctional(pMaterial));
    if (iter == materials.end())
        materials.emplace_back(SMaterialAmount(pMaterial, nMin, nMax, nMax));
    else
    {
        iter->nMin = nMin;
        iter->nMax = nMax;
        iter->nAmount = nMax;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::RemoveMaterial(const SIDBMaterial *pMaterial)
{
    auto iter = std::remove_if(materials.begin(), materials.end(),
                               CMaterialMatchFunctional(pMaterial));
    if (iter != materials.end())
        materials.erase(iter, materials.end());
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::RestoreMaterials()
{
    for (auto iter = materials.begin(); iter != materials.end(); ++iter)
    {
        iter->nAmount = iter->nMax;
        if (iter->nMax > iter->nMin)
            iter->nAmount = iter->nMin + Random(iter->nMax - iter->nMin + 1);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::SetParameters(float *fMinLevels, float *fMaxLevels)
{
    memcpy(fMinSkills, fMinLevels, sizeof(fMinSkills));
    memcpy(fMaxSkills, fMaxLevels, sizeof(fMaxSkills));
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::ImportSpells(const CAssortmentGenerator &gen)
{
    spellPrototypes = gen.spellPrototypes;
    spellModifiers = gen.spellModifiers;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::GenerateArmorInstructions(int nAmount1, int nAmount2,
                                                     std::list<CItem *> &items)
{
    CItemConstructor *pItemConstructor = pWorld->GetItemConstructor();
    CItemManager *pItemManager = pWorld->GetItemManager();
    /*
            int nAmount = nAmount2;
            if ( nAmount1 < nAmount2 )
                    nAmount = nAmount1 + Random( nAmount2 - nAmount1 + 1 );
    */
    //
    selectedShapes.clear();
    selectedShapes = armorShapes;
    // generate instructions from this shapes
    const SIDBLootItemShape *pInstructionShape = pItemManager->GetLootItemShape(
        "instruction armor");
    for (auto iter2 = selectedShapes.begin(); iter2 != selectedShapes.end(); ++iter2)
    {
        pItemConstructor->SetItemManager(pItemManager);
        pItemConstructor->BeginConstructItem(ITID_ITEM_LOOT);
        pItemConstructor->SetShape(pInstructionShape);
        pItemConstructor->SetMaterial(0);
        CItemLoot *pItem = static_cast<CItemLoot *>(pItemConstructor->EndConstructItem());
        pItemConstructor->SetItemManager(0);

        pItem->SetInstructionItemIndex(
            pItemManager->GetShapeIndex(static_cast<const SIDBArmorShape *>(*iter2)));

        int nAmount = nAmount2;
        if (nAmount1 < nAmount2)
            nAmount = nAmount1 + Random(nAmount2 - nAmount1 + 1);
        pItem->SetCount(nAmount);

        pItem->AddRef();
        items.push_back(pItem);
    }
    //
    selectedShapes.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::GenerateWeaponInstructions(int nAmount1, int nAmount2,
                                                      std::list<CItem *> &items)
{
    CItemConstructor *pItemConstructor = pWorld->GetItemConstructor();
    CItemManager *pItemManager = pWorld->GetItemManager();
    /*
            int nAmount = nAmount2;
            if ( nAmount1 < nAmount2 )
                    nAmount = nAmount1 + Random( nAmount2 - nAmount1 + 1 );
    */
    //
    selectedShapes.clear();
    selectedShapes = weaponShapes;
    // generate instructions from this shapes
    const SIDBLootItemShape *pInstructionShape = pItemManager->GetLootItemShape(
        "instruction weapon");
    for (auto iter2 = selectedShapes.begin(); iter2 != selectedShapes.end(); ++iter2)
    {
        pItemConstructor->SetItemManager(pItemManager);
        pItemConstructor->BeginConstructItem(ITID_ITEM_LOOT);
        pItemConstructor->SetShape(pInstructionShape);
        pItemConstructor->SetMaterial(0);
        CItemLoot *pItem = static_cast<CItemLoot *>(pItemConstructor->EndConstructItem());
        pItemConstructor->SetItemManager(0);

        pItem->SetInstructionItemIndex(
            pItemManager->GetShapeIndex(static_cast<const SIDBWeaponShape *>(*iter2)));

        int nAmount = nAmount2;
        if (nAmount1 < nAmount2)
            nAmount = nAmount1 + Random(nAmount2 - nAmount1 + 1);
        pItem->SetCount(nAmount);

        pItem->AddRef();
        items.push_back(pItem);
    }
    //
    selectedShapes.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::GenerateQuickItemInstructions(int nAmount1, int nAmount2,
                                                         std::list<CItem *> &items)
{
    CItemConstructor *pItemConstructor = pWorld->GetItemConstructor();
    CItemManager *pItemManager = pWorld->GetItemManager();
    /*
            int nAmount = nAmount2;
            if ( nAmount1 < nAmount2 )
                    nAmount = nAmount1 + Random( nAmount2 - nAmount1 + 1 );
    */
    //
    selectedShapes.clear();
    selectedShapes = quickitemShapes;
    // generate instructions from this shapes
    const SIDBLootItemShape *pInstructionShape = pItemManager->GetLootItemShape(
        "instruction quick item");
    for (auto iter2 = selectedShapes.begin(); iter2 != selectedShapes.end(); ++iter2)
    {
        if ((*iter2)->szType == "scroll")
            continue;
        pItemConstructor->SetItemManager(pItemManager);
        pItemConstructor->BeginConstructItem(ITID_ITEM_LOOT);
        pItemConstructor->SetShape(pInstructionShape);
        pItemConstructor->SetMaterial(0);
        CItemLoot *pItem = static_cast<CItemLoot *>(pItemConstructor->EndConstructItem());
        pItemConstructor->SetItemManager(0);

        pItem->SetInstructionItemIndex(
            pItemManager->GetShapeIndex(static_cast<const SIDBQuickItemShape *>(*iter2)));

        int nAmount = nAmount2;
        if (nAmount1 < nAmount2)
            nAmount = nAmount1 + Random(nAmount2 - nAmount1 + 1);
        pItem->SetCount(nAmount);

        pItem->AddRef();
        items.push_back(pItem);
    }
    //
    selectedShapes.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::GenerateSpellInstructions(int nAmount1, int nAmount2,
                                                     std::list<CItem *> &items)
{
    CItemConstructor *pItemConstructor = pWorld->GetItemConstructor();
    CItemManager *pItemManager = pWorld->GetItemManager();
    CSpellManager *pSpellManager = pWorld->GetSpellManager();
    /*
            int nAmount = nAmount2;
            if ( nAmount1 < nAmount2 )
                    nAmount = nAmount1 + Random( nAmount2 - nAmount1 + 1 );
    */
    //
    selectedSpellPrototypes.clear();
    selectedSpellPrototypes = spellPrototypes;
    // generate instructions from this shapes
    const SIDBLootItemShape *pInstructionShape = pItemManager->GetLootItemShape("prototype");
    for (auto iter2 = selectedSpellPrototypes.begin(); iter2 != selectedSpellPrototypes.end();
         ++iter2)
    {
        pItemConstructor->SetItemManager(pItemManager);
        pItemConstructor->BeginConstructItem(ITID_ITEM_LOOT);
        pItemConstructor->SetShape(pInstructionShape);
        pItemConstructor->SetMaterial(0);
        CItemLoot *pItem = static_cast<CItemLoot *>(pItemConstructor->EndConstructItem());
        pItemConstructor->SetItemManager(0);

        pItem->SetInstructionItemIndex(theSpellDatabase.spells.GetEntryIndex((*iter2)->szCodeName));

        int nAmount = nAmount2;
        if (nAmount1 < nAmount2)
            nAmount = nAmount1 + Random(nAmount2 - nAmount1 + 1);
        pItem->SetCount(nAmount);

        pItem->AddRef();
        items.push_back(pItem);
    }
    //
    selectedSpellPrototypes.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::GenerateModifierInstructions(int nAmount1, int nAmount2,
                                                        std::list<CItem *> &items)
{
    CItemConstructor *pItemConstructor = pWorld->GetItemConstructor();
    CItemManager *pItemManager = pWorld->GetItemManager();
    CSpellManager *pSpellManager = pWorld->GetSpellManager();
    /*
            int nAmount = nAmount2;
            if ( nAmount1 < nAmount2 )
                    nAmount = nAmount1 + Random( nAmount2 - nAmount1 + 1 );
    */
    //
    selectedSpellModifiers.clear();
    selectedSpellModifiers = spellModifiers;
    // generate instructions from this shapes
    const SIDBLootItemShape *pInstructionShape = pItemManager->GetLootItemShape("rune");
    for (auto iter2 = selectedSpellModifiers.begin(); iter2 != selectedSpellModifiers.end();
         ++iter2)
    {
        pItemConstructor->SetItemManager(pItemManager);
        pItemConstructor->BeginConstructItem(ITID_ITEM_LOOT);
        pItemConstructor->SetShape(pInstructionShape);
        pItemConstructor->SetMaterial(0);
        CItemLoot *pItem = static_cast<CItemLoot *>(pItemConstructor->EndConstructItem());
        pItemConstructor->SetItemManager(0);

        pItem->SetInstructionItemIndex(pSpellManager->GetModifierIndex((*iter2)->pszCode));

        int nAmount = nAmount2;
        if (nAmount1 < nAmount2)
            nAmount = nAmount1 + Random(nAmount2 - nAmount1 + 1);
        pItem->SetCount(nAmount);

        pItem->AddRef();
        items.push_back(pItem);
    }
    //
    selectedSpellModifiers.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::GenerateMaterials(int nAmount1, int nAmount2, std::list<CItem *> &items)
{
    CItemConstructor *pItemConstructor = pWorld->GetItemConstructor();
    CItemManager *pItemManager = pWorld->GetItemManager();

    int nAmount = nAmount2;
    if (nAmount1 < nAmount2)
        nAmount = nAmount1 + Random(nAmount2 - nAmount1 + 1);
    //
    selectedMaterials2.clear();
    // select 'nAmount' shapes from the all shapes
    if (materials.size() > nAmount)
    {
        std::shuffle(materials.begin(), materials.end(), rng);
        selectedMaterials2.insert(selectedMaterials2.begin(), materials.begin(),
                                  materials.begin() + nAmount);
    }
    else
        selectedMaterials2 = materials;
    // generate instructions from this shapes
    const SIDBLootItemShape *pInstructionShape = pItemManager->GetLootItemShape("material");
    for (auto iter2 = selectedMaterials2.begin(); iter2 != selectedMaterials2.end(); ++iter2)
    {
        pItemConstructor->SetItemManager(pItemManager);
        pItemConstructor->BeginConstructItem(ITID_ITEM_LOOT);
        pItemConstructor->SetShape(pInstructionShape);
        pItemConstructor->SetMaterial(iter2->pMaterial);
        pItemConstructor->SetAmount(iter2->nMin + Random(iter2->nMax - iter2->nMin + 1));
        // pItemConstructor->SetMaterial( iter2->pMaterial );
        CItemLoot *pItem = static_cast<CItemLoot *>(pItemConstructor->EndConstructItem());
        pItemConstructor->SetItemManager(0);

        //		pItem->SetInstructionItemIndex( iter2->pMaterial->dwTypeIndex );
        //		pItem->SetCount( iter2->nMin + Random( iter2->nMax - iter2->nMin + 1 ) );

        pItem->AddRef();
        items.push_back(pItem);
    }
    //
    selectedMaterials2.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAssortmentGenerator::HaveSpellPrototype(const std::string &szPrototype) const
{
    const SSDBSpellPrototype *pPrototype = theSpellDatabase.spells[szPrototype];
    if (pPrototype == 0)
        return false;
    for (auto pos = spellPrototypes.begin(); pos != spellPrototypes.end(); ++pos)
    {
        if (*pos == pPrototype)
            return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::BuildTables(const std::vector<SSDBComplexSpell> &spells)
{
    cspells.clear();
    runes.clear();
    // создать hash_multimap доступных комплексных спелов
    for (auto spell = spells.begin(); spell != spells.end(); ++spell)
    {
        if (spell->IsAvailableInShop(nShopID) && HaveSpellPrototype(spell->szPrototype))
            cspells[spell->szPrototype].push_back(&(*spell));
    }
    // для рун создать список лучших (в смысле по номерам...)
    // r, t, a, e, d, m [1,2]
    // ff, fe, fh, fg, fo, fl
    // it, ic
    for (auto pos = spellModifiers.begin(); pos != spellModifiers.end(); ++pos)
    {
        std::string szString = "";
        szString += (*pos)->pszCode[0];
        //= (*pos)->szName.Left( 1 );
        switch ((*pos)->pszCode[0])
        {
        case 'r':
        case 't':
        case 'a':
        case 'e':
        case 'd':
        case 'm': {
            const SSDBModifier *pRune = runes[szString];
            if (((pRune != 0) && (pRune->fpValue < (*pos)->fpValue)) || (pRune == 0))
                runes[szString] = (*pos);
        }
        break;
        case 'f':
        case 'i':
            szString += (*pos)->pszCode[1];
            runes[szString] = (*pos);
            break;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::CreateSpells(CSpellsList &readySpells, float *fLocalMaxSkills,
                                        const int nRandomVal)
{
    CSpellManager *pSpellManager = pWorld->GetSpellManager();
    std::list<const SSDBModifier *> addrunes;

    for (auto pos = cspells.begin(); pos != cspells.end(); ++pos)
    {
        const std::list<const SSDBComplexSpell *> &spells = pos->second;
        for (auto spell = spells.begin(); spell != spells.end(); ++spell)
        {
            // random for spell
            if (random.Get(100) < nRandomVal)
                continue;
            //
            addrunes.clear();
            int nNumRunes = 0;
            // for basic spell
            const SSDBSpellPrototype *pPrototype = pSpellManager->GetPrototype(
                (*spell)->szPrototype.c_str());
            int nComplexity = pPrototype->dwComplexity;
            int nMaxComplexity = MINT(fLocalMaxSkills[pPrototype->mstSubtype]);
            float fManaUsage = pPrototype->fpMana;
            int i;
            for (i = 0; i < (*spell)->required.size(); ++i)
            {
                const SSDBModifier *pRune = runes[(*spell)->required[i]];
                if (pRune)
                {
                    addrunes.push_back(pRune);
                    ++nNumRunes;
                    nComplexity += int(pRune->dwComplexity);
                    fManaUsage += pRune->fpMana;
                }
            }
            // check base spell
            if ((nMaxComplexity < nComplexity) || (nNumRunes > 8))
                continue;
            // optional runes
            for (i = 0; (i < (*spell)->optional.size()) && (nNumRunes < 8); ++i)
            {
                if (random.Get(100) < 50)
                    continue;
                const SSDBModifier *pRune = runes[(*spell)->optional[i]];
                if (pRune && (nMaxComplexity >= nComplexity + int(pRune->dwComplexity)))
                {
                    addrunes.push_back(pRune);
                    nComplexity += int(pRune->dwComplexity);
                    fManaUsage += pRune->fpMana;
                    ++nNumRunes;
                }
            }
            // power rune(s)
            {
                const SSDBModifier *pRune = runes[(*spell)->szPower];
                if (pRune)
                {
                    while ((nMaxComplexity >= nComplexity + int(pRune->dwComplexity)) &&
                           (nNumRunes < 8))
                    {
                        addrunes.push_back(pRune);
                        ++nNumRunes;
                        nComplexity += int(pRune->dwComplexity);
                        if (pRune->pszCode[0] == 'm')
                            fManaUsage += pPrototype->fpMana * pRune->fpValue;
                        fManaUsage += pRune->fpMana;
                        if ((pRune->pszCode[0] == 'm') && (fManaUsage <= pPrototype->fpMana))
                            break;
                    }
                }
            }
            // create spell
            CSmartPtr<CSpell> pSpell = pSpellManager->CreateSpell(pPrototype, addrunes);
            readySpells.push_back(pSpell);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::GenerateSpells(int nAmount1, int nAmount2, std::list<CItem *> &items)
{
    CItemConstructor *pItemConstructor = pWorld->GetItemConstructor();
    CItemManager *pItemManager = pWorld->GetItemManager();
    CSpellManager *pSpellManager = pWorld->GetSpellManager();
    //
    BuildTables(theSpellDatabase.cspells);
    // процесс генерации заключается в следующем:
    // берём спелл
    // добавляем обязательные руны. если не можем кастовать - выбросить
    // добавляем опциональные руны (по рандому). если не можем кастовать - выбросить
    // добиваем усиливающими рунами по вкусу :)
    CSpellsList readySpells;
    for (int i = 0; i < 10; ++i)
    {
        readySpells.clear();
        CreateSpells(readySpells, fMaxSkills, 50);
        if (readySpells.size() >= spellPrototypes.size())
            break;
    }
    // generate item-spells
    const SIDBQuickItemShape *pInstructionShape = pItemManager->GetQuickItemShape(
        "spell container");
    for (auto pos = readySpells.begin(); pos != readySpells.end(); ++pos)
    {
        pItemConstructor->SetItemManager(pItemManager);
        pItemConstructor->BeginConstructItem(ITID_ITEM_SPELL);
        pItemConstructor->SetShape(pInstructionShape);
        pItemConstructor->SetMaterial(0);

        CSpell *pSpell = (*pos);
        pItemConstructor->SetSpell(pSpell);

        CItem *pItem = pItemConstructor->EndConstructItem();
        pItemConstructor->SetItemManager(0);

        pItem->AddRef();
        items.push_back(pItem);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::GenerateItems(std::vector<const SIDBItemGeneralShape *> &shapes,
                                         int nAmount1, int nAmount2, std::list<CItem *> &items,
                                         EAGItemType type)
{
    CItemConstructor *pItemConstructor = pWorld->GetItemConstructor();
    CItemManager *pItemManager = pWorld->GetItemManager();
    //
    switch (type)
    {
    case AGIT_ARMOR:
        BuildTables(theSpellDatabase.carmors);
        break;
    case AGIT_WEAPON:
    case AGIT_WAND:
        BuildTables(theSpellDatabase.cweapons);
        break;
    }
    //
    int nAmount = nAmount2;
    if (nAmount1 < nAmount2)
        nAmount = nAmount1 + Random(nAmount2 - nAmount1 + 1);
    // select 'nAmount' shapes from the all shapes
    selectedShapes.clear();
    if (shapes.size() > nAmount)
    {
        std::shuffle(shapes.begin(), shapes.end(), rng);
        selectedShapes.insert(selectedShapes.begin(), shapes.begin(), shapes.begin() + nAmount);
    }
    else
        selectedShapes = shapes;
    // generate items from the selected shapes
    for (auto iter = selectedShapes.begin(); iter != selectedShapes.end(); ++iter)
    {
        if ((*iter)->szType == "wand")
            continue; // can't create wands - only can create their instructions
        pItemConstructor->SetItemManager(pItemManager);
        pItemConstructor->BeginConstructItem((*iter)->GetTypeID());
        pItemConstructor->SetShape(*iter);
        // select all possible materials
        selectedMaterials.clear();
        if ((*iter)->szMaterialType.CompareNoCase("none") != 0)
        {
            for (auto iter1 = materials.begin(); iter1 != materials.end(); ++iter1)
            {
                if (((*iter)->szMaterialType == iter1->pMaterial->szType) &&
                    (int((*iter)->dwComponents) <= iter1->nAmount))
                    selectedMaterials.push_back(&*iter1);
            }
        }
        //
        float fMaterialPrice1 = (*iter)->fpPrice * (1.0f / 7.0f);
        float fMaterialPrice2 = (*iter)->fpPrice * (1.0f / 5.0f);
        SMaterialAmount *pMaterialAmount = 0;
        for (auto iter3 = selectedMaterials.begin(); iter3 != selectedMaterials.end(); ++iter3)
        {
            int nHighProbability = 10;
            if (IsValInRange<BOOL>((*iter3)->pMaterial->fpPrice,
                                   2.0f * fMaterialPrice1 - fMaterialPrice2, fMaterialPrice2))
                nHighProbability = 110.0f - fabs(((*iter3)->pMaterial->fpPrice - fMaterialPrice1) /
                                                 (fMaterialPrice1 - fMaterialPrice2) * 100.0f);
            if (Dice(100) < nHighProbability)
            {
                pMaterialAmount = *iter3;
                break;
            }
        }
        // can't continue to construct, if no material
        if (pMaterialAmount == 0 && (*iter)->szMaterialType.CompareNoCase("none") != 0)
        {
            if (selectedMaterials.size() == 0) // no more suitable materails. cancel construction
            {
                pItemConstructor->CancelConstructItem();
                pItemConstructor->SetItemManager(0);
                continue;
            }
            else // select random material
            {
                std::shuffle(selectedMaterials.begin(), selectedMaterials.end(), rng);
                pMaterialAmount = selectedMaterials[0];
            }
        }
        //
        if (pMaterialAmount)
        {
            pMaterialAmount->nAmount -= (*iter)->dwComponents;
            pItemConstructor->SetMaterial(pMaterialAmount->pMaterial);
        }
        else
            pItemConstructor->SetMaterial(0);
        //
        CSpellsList readySpells;
        if (((type == AGIT_ARMOR) || (type == AGIT_WAND) || (type == AGIT_WEAPON)) &&
            (random.Get(100) < 50))
        {
            float fComplexity = 0, fMana = 0;
            if (pItemConstructor->EstimateComplexityAndMana(&fComplexity, &fMana))
            {
                float fLocalMaxSkills[RPG_NUM_MAGIC_SUBTYPES];
                for (int i = 0; i < RPG_NUM_MAGIC_SUBTYPES; ++i)
                    fLocalMaxSkills[i] = fComplexity;
                CreateSpells(readySpells, fLocalMaxSkills, 0);
                int nNumSpells = readySpells.size();
                if (nNumSpells)
                {
                    int nSpellIndex = nNumSpells > 1 ? random.Get(nNumSpells - 1) : 0;
                    auto pos = readySpells.begin();
                    std::advance(pos, nSpellIndex);
                    if ((*pos)->GetManaCost() <= fMana)
                        pItemConstructor->SetSpell((*pos));
                }
            }
        }
        //
        CItem *pItem = pItemConstructor->EndConstructItem();
        pItemConstructor->SetItemManager(0);

        pItem->AddRef();
        // CRAP
        if ((pItem->GetTypeID() == ITID_ITEM_QUICK) &&
            (((CItemQuick *)pItem)->GetQuickItemType() == IDB_SCROLL))
            pItem->SetCount(20 + Random(30));
        // CRAP ends
        items.push_back(pItem);
        //
        readySpells.clear();
    }
    //
    selectedMaterials.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::GenerateArmors(int nAmount1, int nAmount2, std::list<CItem *> &items)
{
    GenerateItems(armorShapes, nAmount1, nAmount2, items, AGIT_ARMOR);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::GenerateWeapons(int nAmount1, int nAmount2, std::list<CItem *> &items)
{
    GenerateItems(weaponShapes, nAmount1, nAmount2, items, AGIT_WEAPON);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::GenerateQuickItems(int nAmount1, int nAmount2, std::list<CItem *> &items)
{
    GenerateItems(quickitemShapes, nAmount1, nAmount2, items, AGIT_QUICK);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::Load(CSave &file)
{
    CSpellManager *pSpellManager = pWorld->GetSpellManager();
    CItemManager *pItemManager = pWorld->GetItemManager();
    DWORD dwNumElements;
    CString szName;
    int i, nAmount, nMin, nMax;
    // read and restore pointers for armor shapes
    file.Read(dwNumElements);
    for (i = 0; i < dwNumElements; ++i)
    {
        file.Read(szName);
        szName.MakeLower();
        AddArmorShape(pItemManager->GetArmorShape(szName));
    }
    // read and restore pointers for weapon shapes
    file.Read(dwNumElements);
    for (i = 0; i < dwNumElements; ++i)
    {
        file.Read(szName);
        szName.MakeLower();
        AddWeaponShape(pItemManager->GetWeaponShape(szName));
    }
    // read and restore pointers for quick item shapes
    file.Read(dwNumElements);
    for (i = 0; i < dwNumElements; ++i)
    {
        file.Read(szName);
        szName.MakeLower();
        AddQuickItemShape(pItemManager->GetQuickItemShape(szName));
    }
    // read and restore pointers for material shapes
    file.Read(dwNumElements);
    for (i = 0; i < dwNumElements; ++i)
    {
        file.Read(szName);
        szName.MakeLower();
        file.Read(nMin);
        file.Read(nMax);
        file.Read(nAmount);
        materials.emplace_back(
            SMaterialAmount(pItemManager->GetMaterial(szName), nMin, nMax, nAmount));
    }
    // read and restore pointers for spell prototypes and modifiers
    file.Read(dwNumElements);
    for (i = 0; i < dwNumElements; ++i)
    {
        file.Read(szName);
        AddSpellPrototype(theSpellDatabase.spells[szName]);
    }
    file.Read(dwNumElements);
    for (i = 0; i < dwNumElements; ++i)
    {
        file.Read(szName);
        AddSpellModifier(pSpellManager->GetModifier(szName));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAssortmentGenerator::Store(CSave &file)
{
    // store item shape names
    std::vector<const SIDBItemGeneralShape *>::const_iterator iter1;
    DWORD dwNumElements = armorShapes.size();
    file.Write(dwNumElements);
    for (iter1 = armorShapes.begin(); iter1 != armorShapes.end(); ++iter1)
        file.Write((*iter1)->szName);
    dwNumElements = weaponShapes.size();
    file.Write(dwNumElements);
    for (iter1 = weaponShapes.begin(); iter1 != weaponShapes.end(); ++iter1)
        file.Write((*iter1)->szName);
    dwNumElements = quickitemShapes.size();
    file.Write(dwNumElements);
    for (iter1 = quickitemShapes.begin(); iter1 != quickitemShapes.end(); ++iter1)
        file.Write((*iter1)->szName);
    // store material shape names
    dwNumElements = materials.size();
    file.Write(dwNumElements);
    for (auto iter2 = materials.begin(); iter2 != materials.end(); ++iter2)
    {
        file.Write(iter2->pMaterial->szName);
        file.Write(iter2->nMin);
        file.Write(iter2->nMax);
        file.Write(iter2->nAmount);
    }
    // store spell prototype and modifier names
    dwNumElements = spellPrototypes.size();
    file.Write(dwNumElements);
    for (auto iter3 = spellPrototypes.begin(); iter3 != spellPrototypes.end(); ++iter3)
        file.Write((*iter3)->szCodeName);
    dwNumElements = spellModifiers.size();
    file.Write(dwNumElements);
    for (auto iter4 = spellModifiers.begin(); iter4 != spellModifiers.end(); ++iter4)
    {
        char buffer[5];
        *((DWORD *)(&(buffer[0]))) = (*iter4)->dwCode;
        buffer[4] = 0;
        CString szName(buffer);
        file.Write(szName);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
