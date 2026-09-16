#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "ResourceManager.h"
#include "SpellSystem.h"
#include "SpellDatabase.h"

#include <vector>
#include <list>
/////////////////////////////////////////////////////////////////////////////////////
struct SSpellSequence
{
public:
    CHashMap<DWORD, CSpellContainer *> spells; // all spells, which was created from spell above
public:
    SSpellSequence();
    ~SSpellSequence();
};
/////////////////////////////////////////////////////////////////////////////////////
class CSpellManager : public CResourceManager
{
private:
    CHashMap<DWORD, SSpellSequence *> spells; // all spell containers
protected:
    // create
    virtual BOOL Create();
    // data create/release
    virtual LPGAMEBASE CreateInstant(ETypeIDs tidTypeID);

public:
    CSpellManager();
    virtual ~CSpellManager();
    // destroy
    virtual void Destroy();
    // data creating/releasing
    CSpell *CreateSpell(DWORD dwType = -1);
    CSpell *CreateSpell(LPCSTR pszName);
    CSpell *CreateSpell(const SSDBSpellPrototype *pPrototype,
                        const std::list<const SSDBModifier *> &runes);
    CSpellContainer *CreateSpellContainer();
    BOOL RegisterSpell(const CSpellContainer *pContainer);
    //
    void GetPrototypeNames(CStringArray &szNames);
    void GetRuneNames(CStringArray &szNames);
    LPCSTR GetPrototypeCodeName(LPCSTR pszName);
    LPCSTR GetRuneCodeName(LPCSTR pszName);
    const SSDBSpellPrototype *GetPrototype(LPCSTR pszCodeName) const
    {
        return theSpellDatabase.spells[pszCodeName];
    }
    const SSDBSpellPrototype *GetPrototype(const int nIndex) const
    {
        return theSpellDatabase.spells[nIndex];
    }
    int GetPrototypeIndex(LPCSTR pszCodeName) const
    {
        return theSpellDatabase.spells.GetEntryIndex(pszCodeName);
    }
    const SSDBModifier *GetModifier(LPCSTR pszCodeName) const
    {
        return theSpellDatabase.runes[pszCodeName];
    }
    const SSDBModifier *GetModifier(const int nIndex) const
    {
        return theSpellDatabase.runes[nIndex];
    }
    int GetModifierIndex(LPCSTR pszCodeName) const
    {
        return theSpellDatabase.runes.GetEntryIndex(pszCodeName);
    }
    //
    void DestroySpellDatabase();
    // resource description string
    virtual LPCSTR GetResourceDescription() const { return "spells"; }
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CSpellManager"; };
    virtual EClassIDs WhoAmI() const { return HPC_SPELLMANAGER; }
};
/////////////////////////////////////////////////////////////////////////////////////
