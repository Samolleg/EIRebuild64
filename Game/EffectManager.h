#pragma once

/*==========================================================================================================

        Effects manager class header

==========================================================================================================*/

#include "BaseManager.h"

class CEffect;

// --------------------------------------------------------------------------------------------
// CEffectManager
// --------------------------------------------------------------------------------------------
class CEffectManager : public CBaseManager
{
private:
    virtual LPGAMEBASE CreateInstant(ETypeIDs tidTypeID);

public:
    CEffectManager();
    virtual ~CEffectManager() {}
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    // searching existed effect
    CEffect *FindByID(int nID) { return (CEffect *)Find(nID); }
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CEffectManager"; }
    virtual EClassIDs WhoAmI() const { return HPC_EFFECTMANAGER; }
};

inline CEffectManager::CEffectManager()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}

class CEffectIterator : public CManagerIterator
{
public:
    CEffect *GetFirst(CEffectManager *lpManager)
    {
        return (CEffect *)CManagerIterator::GetFirst(lpManager);
    }
    CEffect *GetNext() { return (CEffect *)CManagerIterator::GetNext(); }
};
