#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "GameBase.h"
#include "BaseManager.h"
#include "world.h"

/////////////////////////////////////////////////////////////////////////////////////
CGameBase::~CGameBase()
{
    SetManager(NULL);
#ifdef _DEBUG
#ifndef __MATERIAL_CONVERTOR__
    if (IsReferenced())
    {
        ReportError("Deallocating object \"%s\", id = %d, typeID = %d with nRefCount = %d.\n",
                    pszGameClassName, GetID(), int(GetTypeID()), nRefCount);
    }
#endif // __MATERIAL_CONVERTOR__
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
// get world via manager
CWorld *CGameBase::GetWorld() const
{
    return lpManager->GetWorld();
}
/////////////////////////////////////////////////////////////////////////////////////
CWorldBase *CGameBase::GetWorldBase() const
{
    CWorld *pWorld = GetWorld();
    if (pWorld)
        return pWorld->GetWorldBase();
    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// CBaseManager* CGameBase::SetManager( CBaseManager* lpManager )
// set data manager, this instant created in
CBaseManager *CGameBase::SetManager(CBaseManager *lpNewManager)
{
    CBaseManager *lpOldManager = lpManager;
    lpManager = lpNewManager;

    return lpOldManager;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CGameBase::Release()
// release this data via appropriate manager
void CGameBase::Release()
{
    DecRefCount();
    if (nRefCount == 0)
        lpManager->Release(this);
}
/////////////////////////////////////////////////////////////////////////////////////
#if ((defined _DEBUG) && !(defined __3DFP_DLL__))
int CGameBase::AddRef()
{
    if (nRefCount < 0)
        ReportError("CGameBase::IncRefCount.\n Can't increase ref count from below zero for "
                    "object\n \"%s\", id = %d.",
                    pszGameClassName, GetID());
    return (++nRefCount);
}

int CGameBase::DecRefCount()
{
    if (nRefCount == 0)
        ReportError("CGameBase::DecRefCount.\n Can't decrease ref count below zero for object\n "
                    "\"%s\", id = %d.",
                    pszGameClassName, GetID());
    return (--nRefCount);
}
#endif // ( (defined _DEBUG) && !(defined __3DFP_DLL__) )
/////////////////////////////////////////////////////////////////////////////////////
