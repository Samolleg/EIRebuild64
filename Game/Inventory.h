#pragma once

/*==========================================================================================================

        Inventory class header

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/
#include "GameBase.h"
#include "PacketIDs.h"
#include "Item.h"

// --------------------------------------------------------------------------------------------
// Inventory
// --------------------------------------------------------------------------------------------

class CInventory : public CItem
{
public:
    CItemPtrHash items; // items in inventory

public:
    CInventory();
    virtual ~CInventory();

    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();

    // Items
    void AddItem(CItem *pItem);
    void RemoveItem(CItem *pItem);
    CItem *FindItem(ITEM_ID);

    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CInventory"; }
    virtual EClassIDs WhoAmI() const { return HPC_INVENTORY; }

private:
    void Clear();
};
