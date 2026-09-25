#pragma once

#include "ResourceManager.h"
#include "MapObject.h"
#include "udbFormat.h"
#include "adFormat.h"
#include "UnitDatabase.h"

#include <vector>

class CResourceBase;

class CMapObjectManager : public CResourceManager
{
private:
    virtual LPGAMEBASE CreateInstant(ETypeIDs tidTypeID);
    BOOL ReadUnitsDatabase();

public:
    CMapObjectManager();
    virtual ~CMapObjectManager();
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    // data creation/initialization
    LPGAMEBASE CreateData(ETypeIDs tidTypeID, int nNewID = -1);
    BOOL SetupDefaultStats(LPGAMEBASE lpMapObject);
    BOOL SetupDefaultStats(SRPGCommonStats *pCommonStats, SRPGUnitStats *pUnitStats);
    BOOL SetupStats(LPGAMEBASE lpMapObject, LPCSTR pszPrototype);
    BOOL SetupStats(SRPGCommonStats *pCommonStats, SRPGUnitStats *pUnitStats, LPCSTR pszPrototype);
    BOOL CreateNewHero(SRPGUnitStats *lpStats, LPCSTR pszPrototype);
    BOOL WearAll(LPGAMEBASE lpMapObject, LPCSTR pszPrototype);
    //
    void GetHitLocationTypeNames(CStringArray &szNames) const;
    void GetRaceNames(CStringArray &szNames) const;
    void GetPrototypeNames(CStringArray &szNames) const;
    void GetNPCPrototypeNames(CStringArray &szNames) const;
    const SUDBHitLocationType *GetHitLocationType(LPCSTR pszName) const
    {
        return theUnitDatabase.hlts[pszName];
    }
    const SUDBHitLocationType *GetHitLocationType(int nIndex) const
    {
        return theUnitDatabase.hlts[nIndex];
    }
    const SUDBRaceModel *GetRace(LPCSTR pszName) const { return theUnitDatabase.races[pszName]; }
    const SUDBRaceModel *GetRace(int nIndex) const { return theUnitDatabase.races[nIndex]; }
    const SUDBUnitPrototype *GetPrototype(LPCSTR pszName) const
    {
        return theUnitDatabase.prototypes[pszName];
    }
    const SUDBUnitPrototype *GetPrototype(int nIndex) const
    {
        return theUnitDatabase.prototypes[nIndex];
    }
    int GetPrototypeIndex(LPCSTR pszPrototypeName) const
    {
        return theUnitDatabase.prototypes.GetEntryIndex(pszPrototypeName);
    };
    const SUDBNPCPrototype *GetNPCPrototype(LPCSTR pszName) const
    {
        return theUnitDatabase.npcs[pszName];
    }
    const SUDBNPCPrototype *GetNPCPrototype(int nIndex) const
    {
        return theUnitDatabase.npcs[nIndex];
    }
    int GetNPCPrototypeIndex(LPCSTR pszPrototypeName) const
    {
        return theUnitDatabase.npcs.GetEntryIndex(pszPrototypeName);
    }
    //
    ETypeIDs GetTypeIDFromPrototype(LPCSTR pszPrototypeName);
    //
    BOOL GetItemsByPrototype(LPCSTR pszPrototype, CStringArray &szWears, CString &szWeapon,
                             CString &szWeapon2, CStringArray &szSpells);
    //
    void GetAllAnimationEntries(CString &szMaskName, CADEntryPtrArray &szArray);
    SADEntry *GetAnimationEntry(const CString &szMaskName, LPCSTR pszAnimationName);
    SADEntry *GetAnimationEntry(CString &szMaskName, DWORD dwCaps);
    SADEntry *GetAnimationEntryByIndex(CString &szMaskName, DWORD cIndex);
    SADEntry *GetSpecialAnimationEntry(CString &szMaskName, DWORD dwSpecialIndex);
    SADEntry *GetRandomAnimationEntry(CString &szMaskName, DWORD dwCaps);
    // resource description string
    virtual LPCSTR GetResourceDescription() const { return "map objects"; };
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CMapObjectManager"; };
    virtual EClassIDs WhoAmI() const { return HPC_MAPOBJECTMANAGER; };
};
// --------------------------------------------------------------------------------------------
class CMapObject;
class CMapObjectIterator : public CManagerIterator
{
public:
    CMapObject *GetFirst(CMapObjectManager *lpManager)
    {
        return (CMapObject *)CManagerIterator::GetFirst(lpManager);
    }
    CMapObject *GetNext() { return (CMapObject *)CManagerIterator::GetNext(); }
};
