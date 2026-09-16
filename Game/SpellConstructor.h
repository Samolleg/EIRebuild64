#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "GameBase.h"
#include "SmartPtr.h"
#include "SpellSystem.h"
#include "SpellManager.h"
/////////////////////////////////////////////////////////////////////////////////////
// class CSpellConstructor : public CGameBase
// spell constructor common part
// all spell construction process will proceed on CLIENT
// after construction, we'll sent created spell data to SERVER and
// after validation and conformation, finish spell construction process.
// NOTE: on client we can play some kind of movie during SERVER validation
class CSpellConstructor : public CGameBase
{
private:
    CSpellManager *lpSpellManager;              // spell manager
    CSmartPtr<CSpellContainer> pSpellContainer; // currently constructed spell
private:
    void ReleaseOldSpell();

public:
    CSpellConstructor();
    virtual ~CSpellConstructor();
    // create/destroy functions
    virtual BOOL Create();
    virtual void Destroy();
    //
    void SetSpellManager(CSpellManager *lpNewSpellManager);
    // spell construction begin/end/cancel
    BOOL BeginConstructSpell(const SSDBSpellPrototype *pPrototype);
    BOOL EndConstructSpell();
    BOOL CancelConstructSpell();
    CSpellContainer *GetSpellContainer() const { return pSpellContainer; }
    // spell construction process
    BOOL AddRune(const SSDBModifier *pRune);
    BOOL RemoveRune(const SSDBModifier *pRune);
    BOOL CanAddRune(const SSDBModifier *pRune);
    // identification
    virtual LPCSTR GetGameClassName() const { return "CSpellConstructor"; }
    virtual EClassIDs WhoAmI() const { return HPC_SPELLCONSTRUCTOR; }
};
/////////////////////////////////////////////////////////////////////////////////////
