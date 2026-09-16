#pragma once

/*==========================================================================================================

        Item manager class header

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

#include "ResourceManager.h"
#include "ItemDatabase.h"

class CItem;
class CItemSpell;

// --------------------------------------------------------------------------------------------
// CItemManager
// --------------------------------------------------------------------------------------------
class CItemManager : public CResourceManager
{
private:
    virtual LPGAMEBASE CreateInstant(ETypeIDs tidTypeID);

public:
    CItemManager() {}
    virtual ~CItemManager() {}
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    //
    CItem *CreateItem(LPCSTR pszString);
    CItem *CreateItem(ETypeIDs tidTypeID, DWORD dwShapeID, DWORD dwMaterialID, int nID);
    CItemSpell *CreateItemSpell(LPCSTR pszSpell, CSpellManager *pSpellManager);
    //
    void GetMaterialNames(CStringArray &szNames);
    void GetWeaponShapeNames(CStringArray &szNames);
    void GetArmorShapeNames(CStringArray &szNames);
    void GetQuickItemShapeNames(CStringArray &szNames);
    void GetQuestItemShapeNames(CStringArray &szNames);
    const SIDBMaterial *GetMaterial(LPCSTR pszName) const
    {
        return theItemDatabase.materials[pszName];
    }
    const SIDBMaterial *GetMaterial(int nIndex) const { return theItemDatabase.materials[nIndex]; }
    const SIDBWeaponShape *GetWeaponShape(LPCSTR pszName) const
    {
        return theItemDatabase.weapons[pszName];
    }
    const SIDBWeaponShape *GetWeaponShape(int nIndex) const
    {
        return theItemDatabase.weapons[nIndex];
    }
    const SIDBArmorShape *GetArmorShape(LPCSTR pszName) const
    {
        return theItemDatabase.armors[pszName];
    }
    const SIDBArmorShape *GetArmorShape(int nIndex) const { return theItemDatabase.armors[nIndex]; }
    const SIDBQuickItemShape *GetQuickItemShape(LPCSTR pszName) const
    {
        return theItemDatabase.quickItems[pszName];
    }
    const SIDBQuickItemShape *GetQuickItemShape(int nIndex) const
    {
        return theItemDatabase.quickItems[nIndex];
    }
    const SIDBQuestItemShape *GetQuestItemShape(LPCSTR pszName) const
    {
        return theItemDatabase.questItems[pszName];
    }
    const SIDBQuestItemShape *GetQuestItemShape(int nIndex) const
    {
        return theItemDatabase.questItems[nIndex];
    }
    const SIDBLootItemShape *GetLootItemShape(LPCSTR pszName) const
    {
        return theItemDatabase.lootItems[pszName];
    }
    const SIDBLootItemShape *GetLootItemShape(int nIndex) const
    {
        return theItemDatabase.lootItems[nIndex];
    }
    const SIDBItemGeneralShape *GetGeneralShape(LPCSTR pszName);
    int GetShapeIndex(const SIDBQuickItemShape *lpShape) const
    {
        return theItemDatabase.quickItems.GetEntryIndex(lpShape);
    }
    int GetShapeIndex(const SIDBQuestItemShape *lpShape) const
    {
        return theItemDatabase.questItems.GetEntryIndex(lpShape);
    }
    int GetShapeIndex(const SIDBArmorShape *lpShape) const
    {
        return theItemDatabase.armors.GetEntryIndex(lpShape);
    }
    int GetShapeIndex(const SIDBWeaponShape *lpShape) const
    {
        return theItemDatabase.weapons.GetEntryIndex(lpShape);
    }
    int GetShapeIndex(const SIDBLootItemShape *lpShape) const
    {
        return theItemDatabase.lootItems.GetEntryIndex(lpShape);
    }
    // searching existed item
    CItem *FindByID(int nID) { return (CItem *)Find(nID); }
    // resource description string
    virtual LPCSTR GetResourceDescription() const { return "items"; };
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CItemManager"; }
    virtual EClassIDs WhoAmI() const { return HPC_ITEMMANAGER; }
};

// --------------------------------------------------------------------------------------------
class CItemIterator : public CManagerIterator
{
public:
    CItem *GetFirst(CItemManager *lpManager)
    {
        return (CItem *)CManagerIterator::GetFirst(lpManager);
    }
    CItem *GetNext() { return (CItem *)CManagerIterator::GetNext(); }
};
