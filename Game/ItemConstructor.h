#pragma once

#include "GameBase.h"
#include "Item.h"
#include "ItemManager.h"

// --------------------------------------------------------------------------------------------
const DWORD IC_CONSTRUCTION = MAKE_FLAG(CGB_LAST_BIT, 0);
const DWORD IC_LAST_BIT = MAKE_LAST_BIT(CGB_LAST_BIT, 0);
// const DWORD IC_CONSTRUCTION = 0x00000001;
// --------------------------------------------------------------------------------------------

class CSpell;
// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
class CItemConstructor : public CGameBase
{
private:
    CItemManager *lpItemManager;

    const SIDBMaterial *lpMaterial; // material, assigned to this item
    const SIDBItemGeneralShape
        *lpShape;  // shape, assigned to this item. can be armor shape, weapon shape, qitem shape
    CItem *lpItem; // currently constructed item
    CSpell *pSpell;
    int nItemsAmount; // amount of items to create - this value will be set to 'nCount' of the
                      // created item
private:
    void SetItem(CItem *lpNewItem);
    BOOL RecalcItem();

public:
    CItemConstructor();
    virtual ~CItemConstructor();
    // create/destroy functions
    virtual BOOL Create();
    virtual void Destroy();
    //
    void SetItemManager(CItemManager *lpNewItemManager);
    // creation process
    BOOL BeginConstructItem(ETypeIDs tidTypeID, int nID = -1);
    BOOL SetMaterial(const SIDBMaterial *lpNewMaterial);
    BOOL SetShape(const SIDBItemGeneralShape *lpNewShape);
    BOOL SetSpell(CSpell *pSpell);
    void SetAmount(int nAmount) { nItemsAmount = nAmount; }
    CItem *EndConstructItem();
    BOOL CancelConstructItem();
    bool EstimateComplexityAndMana(float *fComplexity, float *fMana);
    //
    float EstimatePrice(const SIDBItemGeneralShape *lpShape, const SIDBMaterial *lpMaterial = NULL);
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CItemConstructor"; }
    virtual EClassIDs WhoAmI() const { return HPC_ITEMCONSTRUCTOR; }
};

// --------------------------------------------------------------------------------------------
// estimate price for item, which defined by shape and material
// --------------------------------------------------------------------------------------------
inline float CItemConstructor::EstimatePrice(const SIDBItemGeneralShape *lpShape,
                                             const SIDBMaterial *lpMaterial)
{
    if (!lpShape) // no shape - no item => price = 0
        return 0;
    else if (!lpMaterial) // no material => return price of the shape
        return lpShape->fpPrice;
    else // return price for item with shape and material
        return lpShape->fpPrice + lpMaterial->fpPrice;
}
