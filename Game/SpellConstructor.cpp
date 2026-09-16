#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "SpellConstructor.h"
#include "SpellManager.h"

#include "WorldClient.h"
/////////////////////////////////////////////////////////////////////////////////////
CSpellConstructor::CSpellConstructor()
{
    Create();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CSpellConstructor::~CSpellConstructor()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CSpellConstructor::Create()
{
    pSpellContainer = 0;
    lpSpellManager = 0;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CSpellConstructor::Destroy()
{
    SetSpellManager(0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CSpellConstructor::ReleaseOldSpell()
{
    pSpellContainer = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CSpellConstructor::SetSpellManager(CSpellManager *lpNewSpellManager)
{
    ReleaseOldSpell();
    //
    if (lpSpellManager)
        lpSpellManager->DecRefCount();
    //
    lpSpellManager = lpNewSpellManager;
    //
    if (lpSpellManager)
        lpSpellManager->AddRef();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CSpellConstructor::BeginConstructSpell(const SSDBSpellPrototype *pPrototype)
{
    if (!lpSpellManager)
        return ReportError(
            "CSpellConstructor::BeginConstructSpell.\n There is no valid spell manager.\n");
    //
    if (!pPrototype)
        return ReportError("No valid spell prototype\n");
    //
    pSpellContainer = lpSpellManager->CreateSpellContainer();
    pSpellContainer->SetPrototype(pPrototype);
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CSpellConstructor::EndConstructSpell()
{
    if (!lpSpellManager || !pSpellContainer)
        return ReportError("No valid spell manager or spell container.\n");
    //
    pSpellContainer->Recalculate();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CSpellConstructor::CancelConstructSpell()
{
    ReleaseOldSpell();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CSpellConstructor::AddRune(const SSDBModifier *pRune)
{
    if (!CanAddRune(pRune))
        return FALSE;
    pSpellContainer->AddRune(pRune);
    pSpellContainer->Recalculate(); // recalculate after each change
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CSpellConstructor::RemoveRune(const SSDBModifier *pRune)
{
    pSpellContainer->RemoveRune(pRune);
    pSpellContainer->Recalculate(); // recalculate after each change
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CSpellConstructor::CanAddRune(const SSDBModifier *pRune)
{
    if (pSpellContainer)
        return pSpellContainer->CanAddRune(pRune);
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
