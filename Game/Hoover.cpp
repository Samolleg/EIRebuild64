#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Hoover.h"
#include "ItemManager.h"
#include "GSVars.h"
#include "randomGen.h"
#include "WorldServer.h"
#include "Player.h"
#include "UnitDatabase.h"
#include <cmath>
#include <limits>
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


// Addon-source loot split used by JMVSteals: ordinary loot and carried/quest loot.
int CHoover::HooverCarriedLoot(CWorldServer *world, CUnit *carrier,
                               CList<CItem *, CItem *> &allItems,
                               CList<CItem *, CItem *> &newItems,
                               int &money)
{
    const int maxCarriedCorpses = 128;
    if (!world || !carrier)
        return 0;
    CPlayer *player = world->GetPlayer(0);
    if (!player)
        return 0;
    const char *zone = player->party.szCurrentZone.GetString();
    if (!zone || !*zone)
        zone = "unknown";
    char base[224];
    _snprintf_s(base, sizeof(base), _TRUNCATE,
        "jmv.murderawareness.carry.%s.%d.%u.", zone,
        carrier->GetID(), carrier->GetSubtypeID());
    auto makeMarker = [&](const char *suffix, char *name, size_t size) {
        _snprintf_s(name, size, _TRUNCATE, "%s%s", base, suffix);
    };
    auto readInteger = [&](const char *name) {
        const float value = player->party.vars.GetVar(name);
        if (!std::isfinite(value)) return 0;
        return value >= 0.0f ? static_cast<int>(value + 0.5f)
                             : static_cast<int>(value - 0.5f);
    };

    char countName[256];
    makeMarker("count", countName, sizeof(countName));
    int count = readInteger(countName);
    if (count <= 0) return 0;
    if (count > maxCarriedCorpses) count = maxCarriedCorpses;
    CItemManager *itemManager = world->GetItemManager();
    if (!itemManager) return 0;

    int created = 0;
    long long carriedMoney = 0;
    for (int i = 0; i < count; ++i)
    {
        char suffix[32], name[256];
        _snprintf_s(suffix, sizeof(suffix), _TRUNCATE, "entry.%d", i);
        makeMarker(suffix, name, sizeof(name));
        const int encodedSubtype = readInteger(name);
        _snprintf_s(suffix, sizeof(suffix), _TRUNCATE, "moneylo.%d", i);
        makeMarker(suffix, name, sizeof(name));
        const int moneyLow = readInteger(name);
        _snprintf_s(suffix, sizeof(suffix), _TRUNCATE, "moneyhi.%d", i);
        makeMarker(suffix, name, sizeof(name));
        const int moneyHigh = readInteger(name);
        if (encodedSubtype <= 0 ||
            encodedSubtype - 1 >= theUnitDatabase.prototypes.GetNumEntries() ||
            moneyLow < 0 || moneyLow >= 32768 ||
            moneyHigh < 0 || moneyHigh >= 65536)
            continue;
        const unsigned long long value =
            static_cast<unsigned long long>(moneyHigh) * 32768ull +
            static_cast<unsigned long long>(moneyLow);
        if (value > static_cast<unsigned long long>((std::numeric_limits<int>::max)()))
            continue;
        carriedMoney += static_cast<int>(value);
        const SUDBUnitPrototype *prototype =
            theUnitDatabase.prototypes[static_cast<DWORD>(encodedSubtype - 1)];
        if (!prototype) continue;
        for (size_t drop = 0; drop < prototype->szDrops.size(); ++drop)
        {
            CItem *item = itemManager->CreateItem(prototype->szDrops[drop].GetString());
            if (!item) continue;
            item->AddRef();
            allItems.AddTail(item);
            newItems.AddTail(item);
            ++created;
        }
    }
    long long combinedMoney = static_cast<long long>(money) + carriedMoney;
    if (combinedMoney < 0) combinedMoney = 0;
    if (combinedMoney > (std::numeric_limits<int>::max)())
        combinedMoney = (std::numeric_limits<int>::max)();
    money = static_cast<int>(combinedMoney);

    for (int i = 0; i < count; ++i)
    {
        const char *formats[] = { "entry.%d", "moneylo.%d", "moneyhi.%d" };
        for (int kind = 0; kind < 3; ++kind)
        {
            char suffix[32], name[256];
            _snprintf_s(suffix, sizeof(suffix), _TRUNCATE, formats[kind], i);
            makeMarker(suffix, name, sizeof(name));
            player->party.vars.DelVar(name);
        }
    }
    player->party.vars.DelVar(countName);
    return created;
}BOOL CHoover::HooverAddon(CWorld *pWorld, CUnit *lpUnit,
                          CList<CItem *, CItem *> &allItems,
                          CList<CItem *, CItem *> &newItems,
                          CList<CSpell *, CSpell *> &newSpells,
                          int &nHooveredMoney, int scienceSkill, float dexterity, bool questOnly)
{
    if (!pWorld || !lpUnit) return FALSE;
    nHooveredMoney = 0; CItemManager *itemManager=pWorld->GetItemManager();
    const SUDBUnitPrototype *prototype=lpUnit->GetPrototype();
    if (!questOnly)
    {
        if (prototype) for (int i=0;i<prototype->szDrops.size();++i)
            if (CItem *item=itemManager->CreateItem(prototype->szDrops[i]))
            { item->AddRef(); allItems.AddTail(item); newItems.AddTail(item); }
        for (int i=0;i<QI_MAX_ITEMS;++i)
        { if(CItem *item=lpUnit->GetQItem(i)){item->AddRef();allItems.AddTail(item);} lpUnit->RemoveQItem(i); }
        if(IsIDofPersonage(lpUnit->GetID()))
        {
            for(int i=0;i<RPG_NUM_ARMOR_TYPES;++i) if(CItemArmor *a=lpUnit->GetQArmor(i)){a->AddRef();allItems.AddTail(a);lpUnit->Remove(a->GetArmorType());}
            for(int i=0;i<QI_MAX_WEAPONS;++i) if(CItemWeapon *w=lpUnit->GetQWeapon(i)){w->AddRef();allItems.AddTail(w);lpUnit->RemoveWeapon(i);}
        }
        return TRUE;
    }
    for(int i=0;i<lpUnit->GetQuestItemsNum();++i) if(CItemQuest *item=lpUnit->GetQuestItem(i)){item->AddRef();allItems.AddTail(item);}
    while(lpUnit->GetQuestItemsNum()) lpUnit->RemoveQuestItem(0);
    if(!prototype) return TRUE;
    int money=MINT(prototype->ulCommon.fpPriceMin);
    if(prototype->ulCommon.fpPriceMax>prototype->ulCommon.fpPriceMin) money=random.GetFloat(prototype->ulCommon.fpPriceMin,prototype->ulCommon.fpPriceMax);
    if(float(Dice(100))<=prototype->ulCommon.fpProbability) nHooveredMoney+=money;
    const int skill=scienceSkill;
    const int stat=static_cast<int>(Max(25.0f,dexterity))-25;
    money=MINT(prototype->ulRare.fpPriceMin);
    if(prototype->ulRare.fpPriceMax>prototype->ulRare.fpPriceMin)
    { const int currentSkill=stat+skill; const float price=prototype->ulRare.fpPriceMin+(prototype->ulRare.fpPriceMax-prototype->ulRare.fpPriceMin)*(currentSkill*0.01f); money=static_cast<int>(price); }
    if(float(Dice(100))<=prototype->ulRare.fpProbability||skill>0||stat>0) nHooveredMoney+=money;
    nHooveredMoney=Max(0,nHooveredMoney); return TRUE;
}
