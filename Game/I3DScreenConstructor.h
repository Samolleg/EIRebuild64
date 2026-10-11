#pragma once

#include "I3DInventory.h"
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
const int NUM_VISIBLE_ITEMS = 8;
typedef CSimplyArray<CItem *, CItem *> CItemsArray;
/////////////////////////////////////////////////////////////////////////////////////
// it can be wagon or assortment in shop or something
class CI3DObjectInventory : public CI3DItemObject
{
    friend struct E4CampAccess;
protected:
    int nSortMode; // sort mode ( 0 - 5 )

    int nLock;       // 0 - none, 1 - scroll left, 2 - scroll right
    float fLockTime; // how long is button pushed

    // for smooth scrolling
    float fCurPos[6];
    float fToScroll;
    float fSpeed;

    int nSelected; // selected item

    int nTopHeight; // level of placing on screen
    float fDepth;   // screen depth

    int nSortedItems;
    bool bIsSorted;

    HSOUND hSnd;

protected:
    virtual void FormSortedList() = 0;

public:
    CI3DObjectInventory(CInterface3D *pI, CI3DItemScreen *pScr)
        : CI3DItemObject(pI, pScr)
    {
    }

    void Init(bool bUp = false);

    void SetSortMode(int nMode);

    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectInventoryItems : public CI3DObjectInventory
{
    friend struct E4CampAccess;
private:
    int nSortType; // 0 - camp sorting, 1 - spellconstr sorting, 2 - itemconstr

    CArray<CItem *, CItem *> pSortedItems; // sorted items
    CSimplyArray<CItem *, CItem *> *pAllItems;

    EItemScreenKoeffs ePriceType;

private:
    int GetItemSortMode(CItem *pItem);
    // returns number of mode corr. to this item
    int SortFunction0(CItem *pItem); // for camp
    int SortFunction1(CItem *pItem); // for spell constr
    int SortFunction2(CItem *pItem); // for item constr

protected:
    virtual void FormSortedList();

public:
    CI3DObjectInventoryItems(CInterface3D *pI, CI3DItemScreen *pScr)
        : CI3DObjectInventory(pI, pScr)
    {
    }

    void Init(CSimplyArray<CItem *, CItem *> *pItems, bool bUp = false);

    void SetPriceType(EItemScreenKoeffs eNewType);
    void SetSortType(int nType);
    void SetToItem(CItem *pItem);
    void SwitchToMode(int nMode);
    void SwitchToAlternativeMode(int nMode, int nCount);

    virtual CItem *GetMouseItem(CPoint point, int &nSlotType);
    virtual BOOL Draw(CRenderer *pRend);
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectQuickSlots : public CI3DItemObject
{
private:
    bool bDrawArmors;
    int nCurrentQItem;

public:
    CI3DObjectQuickSlots(CInterface3D *pI, CI3DItemScreen *pScr)
        : CI3DItemObject(pI, pScr)
    {
    }

    void Init(bool bArmors = true);
    void SetCurrentQItem(CItem *pQuick);
    void SetCurrentWeapon(CItem *pWeapon);

    virtual CItem *GetMouseItem(CPoint point, int &nSlotType);
    virtual BOOL Draw(CRenderer *pRend);

    virtual void OnRButtonDown(UINT nFlags, CPoint point);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectSpellSlots : public CI3DItemObject
{
    friend struct E4CampAccess;
private:
    int nTopHeight; // level of placing on screen
    int nCurrentSpell;
    int nFirstSpell;
    float fSpellPosition;
    float fSpellToScroll;
    float fSpellScrollSpeed;

public:
    CI3DObjectSpellSlots(CInterface3D *pI, CI3DItemScreen *pScr)
        : CI3DItemObject(pI, pScr)
    {
    }

    void Init(bool bUp = true);
    void SetCurrentSpell(CItem *pSpell);

    virtual CItem *GetMouseItem(CPoint point, int &nSlotType);
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    virtual BOOL Draw(CRenderer *pRend);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectNameStats : public CI3DItemObject
{
private:
    int nCurPers;
    CString szPersName;

    EItemScreenModes eCurMode;

    CStringArray szStrings;
    enum
    {
        ATTACK,
        DEFENSE,
        DAMAGE,
        CURWEAPON,
        OVERLOAD,
        POINTSLEFT,
        TALL,
        MAGICSKILLS
    };
    int nTall;

    int nLock;
    float fTime;

private:
    void OutputSkill(CRect &rc, LPCTSTR pszSkillName, int nVal, int nSecondVal = -1);
    void OutputSkill(CRect &rc, LPCTSTR pszSkillName, float nVal);
    void OutputHLAbsorb(CRect &rc, LPCTSTR pszHLName, ERPGHitLocations eHL);

public:
    CI3DObjectNameStats(CInterface3D *pI, CI3DItemScreen *pScr)
        : CI3DItemObject(pI, pScr)
    {
    }

    void Init();

    void SetScreenMode(EItemScreenModes eCampMode);

    void SetPersonage(int nNewCurPers, LPCTSTR pszName);
    void StopPrimaryRepeat();
    void UpdateStats();
    void GetMouseSkill(CPoint point, SCampSkillMouseInfo *pSkillInfo);

    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
};
/////////////////////////////////////////////////////////////////////////////////////
struct SPerkLineDesc
{
    CString szName;
    CString szCode;
    CString szNextCode;
    int nPrice;
    int nValue;
    bool bValid;
};
/////////////////////////////////////////////////////////////////////////////////////
struct SPerksBackup
{
    BYTE cManualSkillsCopy[RPG_NUM_ACTUAL_MANUAL_SKILLS];
    BYTE cAutoSkillsCopy[RPG_NUM_ACTUAL_AUTO_SKILLS];
    BYTE cPerksCopy[RPG_NUM_PERKS];
    float fExperienceCopy;
    float fSpentExpCopy;
    float fExpPenaltyCopy;
    bool bModified;
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectPerks : public CI3DItemObject
{
    friend struct E4CampAccess;
private:
    int nCurPers;
    int nPosAv;
    int nPosReal;
    std::vector<SPerkLineDesc> perksAv;
    std::vector<SPerkLineDesc> perksReal;
    std::vector<SPerkLineDesc> skillsAuto;
    std::vector<SPerkLineDesc> skillsManual;
    int nCharPoints;
    std::vector<SPerksBackup> backup;

    int nLock;
    float fTime;

    enum
    {
        SKILLS,
        PERKS,
        PERKSAV,
        TOTAL
    };
    CStringArray szStrings;

private:
    void CheckRanges();
    void UpdatePerks();
    void RedrawPerks();
    void FormPerkPrice(int nPrice, CString &szOut);

    void CopySkillsAndPerks();
    void RestoreSkillsAndPerks();

public:
    CI3DObjectPerks(CInterface3D *pI, CI3DItemScreen *pScr)
        : CI3DItemObject(pI, pScr)
    {
    }

    void UpdateBackup();
    bool IsModified();
    int GetExpTotal();
    int GetExpPrice();
    void Confirm(bool bConfirm, bool bForAll = false);

    void Init();
    void SetPersonage(int nNewCurPers);
    void GetMouseSkill(CPoint point, SCampSkillMouseInfo *pSkillInfo);

    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DTradeTable : public CI3DItemObject
{
private:
    CItemsArray *pItemsBuy;
    CItemsArray *pItemsSell;

    EItemScreenKoeffs eBuyType;
    EItemScreenKoeffs eSellType;
    bool bExchange;

public:
    CI3DTradeTable(CInterface3D *pI, CI3DItemScreen *pScr)
        : CI3DItemObject(pI, pScr)
    {
    }
    void Init(CItemsArray *pItemsBuy, CItemsArray *pItemsSell, bool bSpell, bool bEx = false);

    virtual CItem *GetMouseItem(CPoint point, int &nSlotType);

    virtual BOOL Draw(CRenderer *pRend);
};
/////////////////////////////////////////////////////////////////////////////////////
class CParty;
class CI3DConstrSpell : public CI3DItemObject
{
private:
    CParty *pTableParty;
    float fTime;
    enum
    {
        MAXCOMPL,
        MAXMANA,
    };
    CStringArray szStrings;

public:
    CI3DConstrSpell(CInterface3D *pI, CI3DItemScreen *pScr)
        : CI3DItemObject(pI, pScr)
    {
    }
    void Init(CParty *pItems);

    virtual CItem *GetMouseItem(CPoint point, int &nSlotType);

    virtual BOOL Draw(CRenderer *pRend);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DConstrItem : public CI3DItemObject
{
private:
    CParty *pTableParty;
    float fTime;

public:
    CI3DConstrItem(CInterface3D *pI, CI3DItemScreen *pScr)
        : CI3DItemObject(pI, pScr)
    {
    }
    void Init(CParty *pItems);

    virtual CItem *GetMouseItem(CPoint point, int &nSlotType);

    virtual BOOL Draw(CRenderer *pRend);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DRepairTable : public CI3DItemObject
{
private:
    CItemsArray *pItemsRepair;

public:
    CI3DRepairTable(CInterface3D *pI, CI3DItemScreen *pScr)
        : CI3DItemObject(pI, pScr)
    {
    }
    void Init(CItemsArray *pItemsRepair);

    virtual CItem *GetMouseItem(CPoint point, int &nSlotType);

    virtual BOOL Draw(CRenderer *pRend);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectInventoryFaces : public CI3DObjectInventory
{
private:
    std::vector<CString> *pPeople;

protected:
    virtual void FormSortedList();

public:
    CI3DObjectInventoryFaces(CInterface3D *pI, CI3DItemScreen *pScr)
        : CI3DObjectInventory(pI, pScr)
    {
    }

    void Init(std::vector<CString> *pFaces, bool bUp = false, bool bNoTips = false);
    void SetSelected(int nSel) { nSelected = nSel; }

    virtual BOOL Draw(CRenderer *pRend);
    virtual CItem *GetMouseItem(CPoint point, int &nSlotType) { return 0; }
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
};
