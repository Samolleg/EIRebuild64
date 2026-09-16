/*==========================================================================================================

        Inventory class implementation

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/
#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Inventory.h"
#include "Item.h"

// ----------------------------- Create/destroy
// ------------------------------------------------------------
CInventory::CInventory()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif
    Clear();
}

CInventory::~CInventory()
{
    Clear();
}

BOOL CInventory::Create()
{
    if (!CItem::Create())
        return FALSE;
    return TRUE;
}

void CInventory::Destroy()
{
    // Release items
    CItemPtrHashIterator itemIter;
    CItem *pItem = itemIter.GetFirst(&items);
    while (pItem)
    {
        pItem->Release();
        pItem = itemIter.GetNext();
    }
    CItem::Destroy();
}

void CInventory::Clear()
{
}

// -------------------------------- Add/remove/find
// --------------------------------------------------------
inline void CInventory::AddItem(CItem *pItem)
{
    pItem->AddRef();
    assert(!items.Seek(pItem->GetID()));
    items.SetAt(pItem->GetID(), pItem);
}

inline void CInventory::RemoveItem(CItem *pItem)
{
    assert(items.Seek(pItem->GetID()));
    items.RemoveAt(pItem->GetID());
    pItem->Release();
}

inline CItem *CInventory::FindItem(ITEM_ID nID)
{
    CItem *pItem = NULL;
    items.Lookup(nID, pItem);
    return pItem;
}
