#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Hoover.h"
#include "ItemManager.h"
#include "GSVars.h"
#include "randomGen.h"
/////////////////////////////////////////////////////////////////////////////////////
CHoover::CHoover()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CHoover::~CHoover()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CHoover::Create()
{
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CHoover::Destroy()
{
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CHoover::Hoover( CUnit* lpUnit )
// спылесосить с unit'а все вещи, которые можно: Loot items, Common loot, Rare loot, always drop
// items, qitems с наших убитых наёмников можно спылесосить ВСЕ вещи (в т.ч. и armors & weapons)
BOOL CHoover::Hoover(CWorld *pWorld, CUnit *lpUnit, CList<CItem *, CItem *> &allItems,
                     CList<CItem *, CItem *> &newItems, CList<CSpell *, CSpell *> &newSpells,
                     int &nHooveredMoney)
{
    bool bHero = IsIDofPersonage(lpUnit->GetID());
    //
    ASSERT(allItems.IsEmpty() && newItems.IsEmpty() && newSpells.IsEmpty());
    if (lpUnit->CheckFlag(UNIT_ALREADY_HOOVER))
        return FALSE;
    //
    CItemManager *lpItemManager = pWorld->GetItemManager();
    CItem *lpItem = NULL;
    int i;
    // hoover
    const SUDBUnitPrototype *lpPrototype = lpUnit->GetPrototype();
    //
    nHooveredMoney = 0;
    // process loot items (with dynamic creation)
    if (lpPrototype)
    {
        if (lpPrototype->szDrops.size())
        {
            // process [always drops]
            for (i = 0; i < lpPrototype->szDrops.size(); i++)
            {
                lpItem = lpItemManager->CreateItem(lpPrototype->szDrops[i]);
                if (lpItem)
                {
                    lpItem->AddRef();
                    allItems.AddTail(lpItem);
                    newItems.AddTail(lpItem);
                }
            }
        }
        // initialize random seed from the unit's ID in order to stabilaze hoovered money
        //    srand( lpUnit->GetID() );
        // process common loot
        int nLootedMoney = MINT(lpPrototype->ulCommon.fpPriceMin);
        if (lpPrototype->ulCommon.fpPriceMax > lpPrototype->ulCommon.fpPriceMin)
            nLootedMoney = random.GetFloat(lpPrototype->ulCommon.fpPriceMin,
                                           lpPrototype->ulCommon.fpPriceMax);
        if (float(Dice(100)) <= lpPrototype->ulCommon.fpProbability)
            nHooveredMoney += nLootedMoney;
        // process rare loot
        nLootedMoney = MINT(lpPrototype->ulRare.fpPriceMin);
        if (lpPrototype->ulRare.fpPriceMax > lpPrototype->ulRare.fpPriceMin)
            nLootedMoney = random.GetFloat(lpPrototype->ulRare.fpPriceMin,
                                           lpPrototype->ulRare.fpPriceMax);
        if (float(Dice(100)) <= lpPrototype->ulRare.fpProbability)
            nHooveredMoney += nLootedMoney;
        nHooveredMoney = Max(0, nHooveredMoney);
    }
    // process quick items
    for (i = 0; i < QI_MAX_ITEMS; i++)
    {
        lpItem = lpUnit->GetQItem(i);
        if (lpItem)
        {
            lpItem->AddRef();
            allItems.AddTail(lpItem);
        }
        lpUnit->RemoveQItem(i);
    }
    // process quest items
    for (i = 0; i < lpUnit->GetQuestItemsNum(); i++)
    {
        lpItem = lpUnit->GetQuestItem(i);
        if (lpItem)
        {
            lpItem->AddRef();
            allItems.AddTail(lpItem);
        }
    }
    while (lpUnit->GetQuestItemsNum())
        lpUnit->RemoveQuestItem(0);
    // process armors and weapons for hero-group personages
    if (bHero)
    {
        // process armors
        for (i = 0; i < RPG_NUM_ARMOR_TYPES; ++i)
        {
            CItemArmor *pArmor = lpUnit->GetQArmor(i);
            if (pArmor)
            {
                pArmor->AddRef();
                allItems.AddTail(pArmor);
                lpUnit->Remove(pArmor->GetArmorType());
            }
        }
        // process weapons
        for (i = 0; i < QI_MAX_WEAPONS; ++i)
        {
            CItemWeapon *pWeapon = lpUnit->GetQWeapon(i);
            if (pWeapon)
            {
                pWeapon->AddRef();
                allItems.AddTail(pWeapon);
                lpUnit->RemoveWeapon(i);
            }
        }
    }
    // hoover is done
    lpUnit->SetFlag(UNIT_ALREADY_HOOVER);
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
