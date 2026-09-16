#pragma once

#include "I3DScreenConstructor.h"
/////////////////////////////////////////////////////////////////////////////////////
class CUnit;
class CParty;
class CShop;
/////////////////////////////////////////////////////////////////////////////////////
class CI3DScreenCamp : public CI3DItemScreen
{
private:
    EItemScreenModes eScreenMode;

    bool mbLocal;

    bool bIsSpellConstr;
    bool bIsItemConstr;
    CShop *pShop;

    CArray<CUnit *, CUnit *> pUnits;
    int nCurPers;

    CParty *pItemsAssort;
    CParty *pItemsBuy;
    CParty *pItemsSell;
    CParty *pItemsConstr;
    CParty *pItemsRepair;

    CParty *pSpellsAssort;
    CParty *pSpellsBuy;
    CParty *pSpellsSell;
    CParty *pSpellsConstr;

    // left
    CI3DObjectNameStats *pNameStats;
    CI3DObjectItemInfo *pConstrInfo;

    // down
    CI3DObjectInventoryItems *pWagon;

    // up
    CI3DObjectQuickSlots *pSlots;  // armors, weapons and qitems
    CI3DObjectSpellSlots *pSpells; // spells
    CI3DObjectInventoryItems *pAssortSpells;
    CI3DObjectInventoryItems *pAssortItems;

    // center
    CI3DObjectPerks *pPerks; // perks
    CI3DTradeTable *pTradeSpells;
    CI3DConstrSpell *pConstrSpells;
    CI3DTradeTable *pTradeItems;
    CI3DConstrItem *pConstrItems;
    CI3DRepairTable *pRepairItems;

    int nPrice;
    bool bValidOK;
    bool bValidCancel;
    bool bDeconstruct;

    CItem *pConstrItem;
    CItem *pConstrSpell;

    float fPriceKoeffs[ISK_TOTAL];

    CStringArray szModes;

    bool bCrapNoConstr;

    float fPersAngle;
    int nLock;

private:
    void SetScreenMode(EItemScreenModes eCampMode);

    void InventoryToPers(CItem *pItem);
    CItem *PersToInventory(CItem *pItem);
    void DropFromSpellConstr(CItem *pItem);
    void DropFromItemConstr(CItem *pItem);

    void ClickOnWagonItem(CItem *pItem);

    void TransactionOK();
    void TransactionCancel();
    void CheckValidTransaction();

    bool ConstructSpell();
    bool DeconstructSpell();
    bool ConstructItem();
    bool DeconstructItem();

    void UpdatePersonage();

public:
    CI3DScreenCamp(CInterface3D *pI, bool bLocal = false)
        : CI3DItemScreen(pI)
        , mbLocal(bLocal)
    {
    }
    virtual void OnActivate();
    virtual void OnDeactivate();
    virtual void OnExitFromModal(DWORD dwRetValue);
    virtual LPCTSTR GetTutorialScreenName();

    void GetMaxManaAndComplexity(ERPGMagicSubtypes subtype, int &nComplexity, int &nMana);
    void UpdateWeapons();
    void UpdateAttributes();
    void NextPersonage();
    void PrevPersonage();
    CUnit *GetUnit(int num) { return pUnits[num]; }
    CPersonage *GetCurrentPers();

    virtual void GenerateCheatAssortment();

    int GetItemPrice(CItem *pItem, EItemScreenKoeffs eKoeffType);

    virtual void ClickOnItem(CItem *pItem, EItemScreenParts eFrom);
    virtual bool CanClickOnItem(CItem *pItem, EItemScreenParts eFrom);
    virtual void ClickOnSlot(int nSlotType, EItemScreenParts eFrom);

    virtual BOOL Draw(CRenderer *pRend);

    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
};
