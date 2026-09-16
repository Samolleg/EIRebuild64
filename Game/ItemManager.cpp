#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "World.h"
#include "ItemManager.h"
#include "Item.h"
#include "Inventory.h"
#include "RPGSystemConsts.h"
#include "SpellManager.h"
#include "ItemConstructor.h"
#include "StringProcessing.h"
#include "BasicChunk.h"
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemManager::Create()
{
    CString szResourcesPath = ((CWorld *)GetWorld())->GetResourcePath();

    if (CResourceManager::Create() == FALSE)
        return FALSE;
    // add database resource: animations, units, items, etc.
    //	AddNewResource( szResourcesPath + "database.res" );

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemManager::Destroy()
{
    CResourceManager::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// create instant...
LPGAMEBASE CItemManager::CreateInstant(ETypeIDs tidTypeID)
{
    CItem *pItem = NULL;

    switch (tidTypeID)
    {
    case ITID_ITEM_WEAPON:
        pItem = dbgnew CItemWeapon();
        break;
    case ITID_ITEM_ARMOR:
        pItem = dbgnew CItemArmor();
        break;
    case ITID_ITEM_QUICK:
        pItem = dbgnew CItemQuick();
        break;
    case ITID_ITEM_QUEST:
        pItem = dbgnew CItemQuest();
        break;
    case ITID_ITEM_LOOT:
        pItem = dbgnew CItemLoot();
        break;
    case ITID_ITEM_SPELL:
        pItem = dbgnew CItemSpell();
        break;
    }

    if (pItem == NULL)
        return NULL;
    pItem->SetTypeID(tidTypeID);

    return pItem;
}
/////////////////////////////////////////////////////////////////////////////////////
// create item from their complete string name...
CItem *CItemManager::CreateItem(LPCSTR pszString)
{
    CItemConstructor *lpConstructor = lpWorld->GetItemConstructor();
    CString szSpell, szString = pszString;
    CStringArray szStrings;
    int i, pos = szString.Find('[');

    if (pos != -1)
    {
        szSpell = szString.Right(szString.GetLength() - pos - 1);
        szSpell.TrimLeft('[');
        szSpell.TrimRight(']');
        szSpell.TrimLeft();
        szSpell.TrimRight();
        szString = szString.Left(pos);
        szString.TrimRight();
    }

    NStrProc::SplitString(szString, szStrings, '.');
    //
    for (i = 0; i < szStrings.GetSize(); ++i)
        szStrings[i].MakeLower();
    //
    if (szStrings[0] == "spell container")
        return CreateItemSpell(szSpell, GetWorld()->GetSpellManager());
    //
    const SIDBItemGeneralShape *lpShape = GetGeneralShape(szStrings[0]);
    if (!lpShape)
    {
        ReportError("Can't find prototype for item \"%s\".\n", szStrings[0]);
        return NULL;
    }

    lpConstructor->SetItemManager(this);
    lpConstructor->BeginConstructItem(lpShape->GetTypeID());

    const SIDBMaterial *lpMaterial = NULL;
    if (szStrings.GetSize() > 1)
        lpMaterial = GetMaterial(szStrings[1]);

    lpConstructor->SetShape(lpShape);
    lpConstructor->SetMaterial(lpMaterial);

    if (!szSpell.IsEmpty() && lpShape->szName != "material")
    {
        CSpell *pSpell = GetWorld()->GetSpellManager()->CreateSpell(szSpell);
        if (pSpell)
            lpConstructor->SetSpell(pSpell);
    }
    else if (lpShape->szName == "material" && !szSpell.IsEmpty())
        lpConstructor->SetAmount(atoi(szSpell));

    CItem *lpItem = lpConstructor->EndConstructItem();

    if (lpItem == 0)
        ReportError("Can't create item \"%s\".\nCheck shape and material names\n", pszString);

    lpConstructor->SetItemManager(NULL);
    //
    if ((lpItem->WhoAmI() == HPC_ITEM_LOOT) &&
        (((CItemLoot *)lpItem)->GetLootType() == IDBLIT_INSTRUCTION) && (szStrings.GetSize() > 1))
    {
        DWORD dwInstructionItemIndex = 0;
        switch (((CItemLoot *)lpItem)->GetInstructionType())
        {
        case IDBINS_WEAPON:
            dwInstructionItemIndex = theItemDatabase.weapons.GetEntryIndex(szStrings[1]);
            break;
        case IDBINS_ARMOR:
            dwInstructionItemIndex = theItemDatabase.armors.GetEntryIndex(szStrings[1]);
            break;
        case IDBINS_QUICK:
            dwInstructionItemIndex = theItemDatabase.quickItems.GetEntryIndex(szStrings[1]);
            break;
        case IDBINS_PROTOTYPE:
            dwInstructionItemIndex = theSpellDatabase.spells.GetEntryIndex(szStrings[1]);
            break;
        case IDBINS_RUNE:
            dwInstructionItemIndex = theSpellDatabase.runes.GetEntryIndex(szStrings[1]);
            break;
        }
        ((CItemLoot *)lpItem)->SetInstructionItemIndex(dwInstructionItemIndex);
    }
    //

    return lpItem;
}
/////////////////////////////////////////////////////////////////////////////////////
// create item by typeId, shape and material ID
CItem *CItemManager::CreateItem(ETypeIDs tidTypeID, DWORD dwShapeID, DWORD dwMaterialID, int nID)
{
    CItemConstructor *lpConstructor = ((CWorld *)lpWorld)->GetItemConstructor();
    const SIDBMaterial *lpMaterial = 0;
    const SIDBItemGeneralShape *lpShape = NULL;
    // CRAP - wrong item :(
    if (short(dwShapeID) == -1)
        dwShapeID = 0;
    // CRAP end

    lpConstructor->SetItemManager(this);
    lpConstructor->BeginConstructItem(tidTypeID, nID);

    switch (tidTypeID)
    {
    case ITID_ITEM_ARMOR:
        lpShape = GetArmorShape(dwShapeID);
        lpMaterial = GetMaterial(dwMaterialID);
        break;
    case ITID_ITEM_WEAPON:
        lpShape = GetWeaponShape(dwShapeID);
        lpMaterial = GetMaterial(dwMaterialID);
        break;
    case ITID_ITEM_QUICK:
    case ITID_ITEM_SPELL:
        lpShape = GetQuickItemShape(dwShapeID);
        break;
    case ITID_ITEM_QUEST:
        lpShape = GetQuestItemShape(dwShapeID);
        break;
    case ITID_ITEM_LOOT:
        lpShape = GetLootItemShape(dwShapeID);
        break;
    }

    lpConstructor->SetShape(lpShape);
    lpConstructor->SetMaterial(lpMaterial);
    CItem *lpItem = lpConstructor->EndConstructItem();

    lpConstructor->SetItemManager(NULL);

    return lpItem;
}
/////////////////////////////////////////////////////////////////////////////////////
CItemSpell *CItemManager::CreateItemSpell(LPCSTR pszSpell, CSpellManager *pSpellManager)
{
    CItemSpell *lpItem = NULL;
    CSpell *lpSpell = pSpellManager->CreateSpell(pszSpell);
    if (lpSpell)
    {
        // CRAP - material & shape for itemspell are unknown
        lpItem = (CItemSpell *)CreateItem(ITID_ITEM_SPELL,
                                          GetShapeIndex(GetQuickItemShape("spell container")), 0,
                                          -1);
        if (lpItem)
        {
            lpItem->AddSpell(lpSpell);
        }
    }
    return lpItem;
}
/////////////////////////////////////////////////////////////////////////////////////
// get names of the all materials
void CItemManager::GetMaterialNames(CStringArray &szNames)
{
    szNames.SetSize(theItemDatabase.materials.GetNumEntries());
    for (int i = 0; i < szNames.GetSize(); i++)
        szNames[i] = theItemDatabase.materials[i]->szName;
}
/////////////////////////////////////////////////////////////////////////////////////
// get names of the all weapon shapes
void CItemManager::GetWeaponShapeNames(CStringArray &szNames)
{
    szNames.SetSize(theItemDatabase.weapons.GetNumEntries());
    for (int i = 0; i < szNames.GetSize(); i++)
        szNames[i] = theItemDatabase.weapons[i]->szName;
}
/////////////////////////////////////////////////////////////////////////////////////
// get names of the all armor shapes
void CItemManager::GetArmorShapeNames(CStringArray &szNames)
{
    szNames.SetSize(theItemDatabase.armors.GetNumEntries());
    for (int i = 0; i < szNames.GetSize(); i++)
        szNames[i] = theItemDatabase.armors[i]->szName;
}
/////////////////////////////////////////////////////////////////////////////////////
// get names of the all quick item shapes
void CItemManager::GetQuickItemShapeNames(CStringArray &szNames)
{
    szNames.SetSize(theItemDatabase.quickItems.GetNumEntries());
    for (int i = 0; i < szNames.GetSize(); i++)
        szNames[i] = theItemDatabase.quickItems[i]->szName;
}
/////////////////////////////////////////////////////////////////////////////////////
// get names of the all quest item shapes
void CItemManager::GetQuestItemShapeNames(CStringArray &szNames)
{
    szNames.SetSize(theItemDatabase.questItems.GetNumEntries());
    for (int i = 0; i < szNames.GetSize(); i++)
        szNames[i] = theItemDatabase.questItems[i]->szName;
}
/////////////////////////////////////////////////////////////////////////////////////
// get shape by name in the next search sequence:
// weapon - armor - qitem - litem
const SIDBItemGeneralShape *CItemManager::GetGeneralShape(LPCSTR pszName)
{
    const SIDBItemGeneralShape *lpShape = NULL;
    // check for weapon shape
    lpShape = GetWeaponShape(pszName);
    if (lpShape)
        return lpShape;
    // check for armor shape
    lpShape = GetArmorShape(pszName);
    if (lpShape)
        return lpShape;
    // check for quick item shape
    lpShape = GetQuickItemShape(pszName);
    if (lpShape)
        return lpShape;
    // check for quest item shape
    lpShape = GetQuestItemShape(pszName);
    if (lpShape)
        return lpShape;
    // check for litem shape
    lpShape = GetLootItemShape(pszName);
    if (lpShape)
        return lpShape;

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
