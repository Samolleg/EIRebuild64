#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "World.h"
#include "ItemConstructor.h"
#include "SpellSystem.h"
#include "SpellManager.h"
/////////////////////////////////////////////////////////////////////////////////////
CItemConstructor::CItemConstructor()
{
    lpMaterial = NULL;
    lpShape = NULL;
    lpItem = NULL;
    pSpell = 0;
    lpItemManager = NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
CItemConstructor::~CItemConstructor()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemConstructor::Create()
{
    SetItemManager(NULL);
    SetItem(NULL);
    lpMaterial = NULL;
    lpShape = NULL;
    pSpell = 0;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemConstructor::Destroy()
{
    SetItemManager(NULL);
    SetItem(NULL);
    lpMaterial = NULL;
    lpShape = NULL;
    pSpell = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CItemConstructor::SetItemManager( CItemManager* lpNewItemManager )
// set item manager for further item construction
void CItemConstructor::SetItemManager(CItemManager *lpNewItemManager)
{
    if (lpItemManager)
        lpItemManager->DecRefCount();
    //
    lpItemManager = lpNewItemManager;
    //
    if (lpItemManager)
        lpItemManager->AddRef();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CItemConstructor::SetItem( CItem* lpNewItem )
// set empty item to perform construction
void CItemConstructor::SetItem(CItem *lpNewItem)
{
    if (lpItem)
        lpItem->Release();
    //
    lpItem = lpNewItem;
    //
    if (lpItem)
        lpItem->AddRef();
    // remove all material/shape
    lpMaterial = NULL;
    lpShape = NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemConstructor::BeginConstructItem(ETypeIDs tidTypeID, int nID)
{
    CItem *lpNewItem = (CItem *)lpItemManager->CreateData(tidTypeID, nID);
    if (!lpNewItem)
        return ReportError("CItemConstructor::BeginCreateItem.\n");
    SetItem(lpNewItem);
    SetMaterial(0);
    SetShape(0);
    SetAmount(1);
    SetFlag(IC_CONSTRUCTION);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemConstructor::SetMaterial(const SIDBMaterial *lpNewMaterial)
{
    lpMaterial = lpNewMaterial;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemConstructor::SetShape(const SIDBItemGeneralShape *lpNewShape)
{
    lpShape = lpNewShape;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemConstructor::SetSpell(CSpell *pNewSpell)
{
    pSpell = pNewSpell;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// CItem* CItemConstructor::EndConstructItem()
// NOTE: this function will return item with ref count == 1
CItem *CItemConstructor::EndConstructItem()
{
    if (RecalcItem() == FALSE)
    {
        lpItem->Release();
        lpItem = 0;
        return 0;
    }

    ResetFlag(IC_CONSTRUCTION);

    CItem *lpRetItem = lpItem;
    lpItem->DecRefCount();
    lpItem = NULL;
    pSpell = 0;
    nItemsAmount = 0;

    return lpRetItem;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemConstructor::CancelConstructItem()
{
    if (lpItem)
        lpItem->Release();
    lpItem = 0;
    pSpell = 0;
    lpMaterial = 0;
    lpShape = 0;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemConstructor::RecalcItem()
{
    SIDBItemGeneralShape *lpRecalcShape = NULL;
    int i;
    //
    if (((lpItem->WhoAmI() == HPC_ITEM_WEAPON) || (lpItem->WhoAmI() == HPC_ITEM_ARMOR)) &&
        !lpMaterial)
        return FALSE; // can't recalc armor or weapon w/o material
    // setup shape
    switch (lpItem->WhoAmI())
    {
    case HPC_ITEM_WEAPON:
        lpRecalcShape = dbgnew SIDBWeaponShape;
        break;
    case HPC_ITEM_ARMOR:
        lpRecalcShape = dbgnew SIDBArmorShape;
        break;
    case HPC_ITEM_QUICK:
    case HPC_ITEM_SPELL:
        lpRecalcShape = dbgnew SIDBQuickItemShape;
        break;
    case HPC_ITEM_QUEST:
        lpRecalcShape = dbgnew SIDBQuestItemShape;
        break;
    case HPC_ITEM_LOOT:
        lpRecalcShape = dbgnew SIDBLootItemShape;
        break;
    }
    // recalc general
    if (lpMaterial) // recal shape with material
    {
        lpRecalcShape->fpPrice = lpShape->fpPrice + lpMaterial->fpPrice * lpShape->dwComponents;
        lpRecalcShape->fpWeight = lpShape->fpWeight * lpMaterial->fpWeight;
        lpRecalcShape->fpSize = lpShape->fpSize;
        lpRecalcShape->fpManaCapacity = lpShape->fpManaCapacity + lpMaterial->fpManaCapacity;
        lpRecalcShape->fpDurability = lpShape->fpDurability * lpMaterial->fpDurability;
        lpRecalcShape->dwSpellSlots = lpShape->dwSpellSlots + lpMaterial->dwSpellSlots;
        lpRecalcShape->dwComponents = lpShape->dwComponents;
    }
    else // take in account shape only
    {
        lpRecalcShape->fpPrice = lpShape->fpPrice;
        lpRecalcShape->fpWeight = lpShape->fpWeight;
        lpRecalcShape->fpSize = lpShape->fpSize;
        lpRecalcShape->fpManaCapacity = lpShape->fpManaCapacity;
        lpRecalcShape->fpDurability = lpShape->fpDurability;
        lpRecalcShape->dwSpellSlots = lpShape->dwSpellSlots;
        lpRecalcShape->dwComponents = lpShape->dwComponents;
    }
    //
    /*
    if ( pSpell )
            lpRecalcShape->dwSpellSlots -= pSpell->GetNumSlots();
    */
    // specific
    DWORD dwShapeIndex;
    switch (lpItem->WhoAmI())
    {
    case HPC_ITEM_WEAPON:
        ((SIDBWeaponShape *)lpRecalcShape)->fpRange = ((const SIDBWeaponShape *)lpShape)->fpRange;
        ((SIDBWeaponShape *)lpRecalcShape)->fpDamageMin = ((const SIDBWeaponShape *)lpShape)
                                                              ->fpDamageMin *
                                                          lpMaterial->fpDamageModifier;
        ((SIDBWeaponShape *)lpRecalcShape)->fpDamageRandom = ((const SIDBWeaponShape *)lpShape)
                                                                 ->fpDamageRandom *
                                                             lpMaterial->fpDamageModifier;
        dwShapeIndex = lpItemManager->GetShapeIndex((const SIDBWeaponShape *)lpShape);
        break;
    case HPC_ITEM_ARMOR:
        ((SIDBArmorShape *)lpRecalcShape)->main.dwCrippleMask = ((const SIDBArmorShape *)lpShape)
                                                                    ->main.dwCrippleMask |
                                                                lpMaterial->dwCrippleMask;
        ((SIDBArmorShape *)lpRecalcShape)->second.dwCrippleMask = ((const SIDBArmorShape *)lpShape)
                                                                      ->second.dwCrippleMask |
                                                                  lpMaterial->dwCrippleMask;
        for (i = 0; i < RPG_NUM_ATTACK_TYPES; ++i)
        {
            ((SIDBArmorShape *)lpRecalcShape)->main.fpDistributions[i] =
                ((const SIDBArmorShape *)lpShape)->main.fpAbsorbtion *
                ((const SIDBArmorShape *)lpShape)->main.fpDistributions[i] *
                lpMaterial->fpAbsorbtionMultipliers[i];
            ((SIDBArmorShape *)lpRecalcShape)->second.fpDistributions[i] =
                ((const SIDBArmorShape *)lpShape)->second.fpAbsorbtion *
                ((const SIDBArmorShape *)lpShape)->second.fpDistributions[i] *
                lpMaterial->fpAbsorbtionMultipliers[i];
        }
        dwShapeIndex = lpItemManager->GetShapeIndex((const SIDBArmorShape *)lpShape);
        break;
    case HPC_ITEM_QUICK:
    case HPC_ITEM_SPELL:
        ((SIDBQuickItemShape *)lpRecalcShape)->dwSkill = ((const SIDBQuickItemShape *)lpShape)
                                                             ->dwSkill;
        dwShapeIndex = lpItemManager->GetShapeIndex((const SIDBQuickItemShape *)lpShape);
        break;
    case HPC_ITEM_QUEST:
        ((SIDBQuestItemShape *)lpRecalcShape)->dwScriptID = ((const SIDBQuestItemShape *)lpShape)
                                                                ->dwScriptID;
        dwShapeIndex = lpItemManager->GetShapeIndex((SIDBQuestItemShape *)lpShape);
        break;
    case HPC_ITEM_LOOT:
        ((SIDBLootItemShape *)lpRecalcShape)->litType = ((const SIDBLootItemShape *)lpShape)
                                                            ->litType;
        ((SIDBLootItemShape *)lpRecalcShape)
            ->itInstructionType = ((const SIDBLootItemShape *)lpShape)->itInstructionType;
        if (((const SIDBLootItemShape *)lpShape)->litType == IDBLIT_MATERIAL && lpMaterial)
            ((SIDBLootItemShape *)lpRecalcShape)->dwMaterialIndex = lpMaterial->dwTypeIndex;
        else
            ((SIDBLootItemShape *)lpRecalcShape)
                ->dwMaterialIndex = ((const SIDBLootItemShape *)lpShape)->dwMaterialIndex;
        dwShapeIndex = lpItemManager->GetShapeIndex((const SIDBLootItemShape *)lpShape);
        break;
    }
    //
    lpItem->Setup(lpRecalcShape);
    //
    if ((lpItem->WhoAmI() == HPC_ITEM_QUICK) &&
        (((const SIDBQuickItemShape *)lpShape)->idbType == IDB_SCROLL))
    {
        // special case for the potions...
        CSpellManager *pSpellManager = lpItemManager->GetWorld()->GetSpellManager();
        pSpell = pSpellManager->CreateSpell(((const SIDBQuickItemShape *)lpShape)->szSpell);
        lpItem->AddFreeSpell(pSpell);
    }
    else if (pSpell)
        lpItem->AddSpell(pSpell);
    //
    if (lpRecalcShape)
        delete lpRecalcShape;
    //
    if (lpMaterial)
        lpItem->SetSubtypeID(((dwShapeIndex << 16) & 0xFFFF0000) |
                             (lpMaterial->dwTypeIndex & 0xFFFF));
    else
        lpItem->SetSubtypeID((dwShapeIndex << 16) & 0xFFFF0000);
    //
    lpItem->SetCount(nItemsAmount);
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CItemConstructor::EstimateComplexityAndMana(float *pfComplexity, float *pfMana)
{
    if (((lpItem->WhoAmI() == HPC_ITEM_WEAPON) || (lpItem->WhoAmI() == HPC_ITEM_ARMOR)) &&
        !lpMaterial)
        return false; // can't recalc armor or weapon w/o material
    // recalc general
    if (lpMaterial) // recal shape with material
    {
        *pfComplexity = lpShape->dwSpellSlots + lpMaterial->dwSpellSlots;
        *pfMana = lpShape->fpManaCapacity + lpMaterial->fpManaCapacity;
    }
    else // take in account shape only
    {
        *pfComplexity = lpShape->dwSpellSlots;
        *pfMana = lpShape->fpManaCapacity;
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
