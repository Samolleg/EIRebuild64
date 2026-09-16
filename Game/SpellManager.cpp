#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "SpellManager.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include "SpellConstructor.h"
#include "StringProcessing.h"
#include "BasicChunk.h"
#include "World.h"
// STL includes and usings
#include <string>
#include <functional>
using std::find_if;
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                struct SSpellSequence constructor/destructor                ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
SSpellSequence::SSpellSequence()
{
    spells.SetHashTableSize(16);
}
/////////////////////////////////////////////////////////////////////////////////////
SSpellSequence::~SSpellSequence()
{
    // delete all derived spells
    CHashMapIterator<DWORD, CSpellContainer *> hmi;
    CSpellContainer *pSpell = hmi.GetFirst(&spells);
    while (pSpell)
    {
        pSpell->Release();
        pSpell = hmi.GetNext();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                     class CSpellManager functions                          ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CSpellManager::CSpellManager()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CSpellManager::~CSpellManager()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CSpellManager::Create()
// create manager's internal data structures
// load and convert spell data and store it in hash maps for further fast access
BOOL CSpellManager::Create()
{
    if (CResourceManager::Create() == FALSE)
        return FALSE;
    // open resource
    //	CString szResourcesPath = ((CWorld*)GetWorld())->GetResourcePath();
    //	AddNewResource( szResourcesPath + "database.res" );

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CSpellManager::Destroy()
// destroy all maps
void CSpellManager::Destroy()
{
    // destroy spells map
    CHashMapIterator<DWORD, SSpellSequence *> hmiSpells;
    SSpellSequence *pSpell = hmiSpells.GetFirst(&spells);
    while (pSpell)
    {
        delete pSpell;
        pSpell = hmiSpells.GetNext();
    }
    spells.RemoveAll();
    //
    CResourceManager::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CSpellManager::CreateInstant( ETypeIDs tidTypeID )
// create spell/spell effect and their containers
LPGAMEBASE CSpellManager::CreateInstant(ETypeIDs tidTypeID)
{
    LPGAMEBASE lpBase = NULL;
    //
    switch (tidTypeID)
    {
    case SSOTID_SPELL:
        lpBase = dbgnew CSpell();
        break;
    case SSOTID_SPELL_CONTAINER:
        lpBase = dbgnew CSpellContainer();
        break;
    }
    //
    return lpBase;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CSpellManager::DestroySpellDatabase()
//
void CSpellManager::DestroySpellDatabase()
{
    CHashMapIterator<DWORD, SSpellSequence *> hmi;
    SSpellSequence *pSequence = hmi.GetFirst(&spells);
    //
    while (!hmi.IsReachEnd())
    {
        if (pSequence)
            delete pSequence;
    }
    spells.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
// CSpellContainer* CSpellManager::CreateSpellContainer()
// create spell container and (if need) fill it with data from hash map
CSpellContainer *CSpellManager::CreateSpellContainer()
{
    return (CSpellContainer *)CreateData(SSOTID_SPELL_CONTAINER);
}
/////////////////////////////////////////////////////////////////////////////////////
// CSpell* CSpellManager::CreateSpell( DWORD dwType )
// create spell and (if need) fill it with data from hash map
CSpell *CSpellManager::CreateSpell(DWORD dwType)
{
    ASSERT(dwType == DWORD(-1));
    CSpell *pSpell = (CSpell *)CreateData(SSOTID_SPELL);

    if (dwType == DWORD(-1))
        return pSpell; // we need only EMPTY spell container
    pSpell->SetSubtypeID(dwType);

    if ((dwType & 0x0000FFFF) == 0) // fill with main spell data
        pSpell->Setup(theSpellDatabase.spells[(dwType & 0xFFFF0000) >> 16]);
    else // fill with child spell data
    {
        SSpellSequence *pSequence = spells[dwType & 0xFFFF0000];
        if (pSequence)
            pSequence->spells[dwType & 0x0000FFFF]->GetSpell().Duplicate(pSpell);
        else
            ReportError("Can't find spell sequence to create child spell.\n");
    }
    return pSpell;
}
/////////////////////////////////////////////////////////////////////////////////////
CSpell *CSpellManager::CreateSpell(LPCSTR pszCodeName)
{
    if(auto*mod=ModRuntime::Current())if(mod->SuppressLegacyRefreshSpell(pszCodeName))return nullptr;
    // first, extract part w/o modifiers and modifiers itself
    CString szCodeName = pszCodeName;
    CStringArray szRunes;
    int pos = szCodeName.Find('{');
    if (pos == -1)
    {
        szCodeName.TrimLeft();
        szCodeName.TrimRight();
    }
    else
    {
        CString szRuneString = szCodeName.Right(szCodeName.GetLength() - pos - 1);
        szRuneString.TrimLeft();
        szRuneString.TrimRight();
        szRuneString.TrimRight('}');
        szRuneString.TrimRight();
        NStrProc::SplitString(szRuneString, szRunes, ';');
        szCodeName = szCodeName.Left(pos);
        szCodeName.TrimLeft();
        szCodeName.TrimRight();
    }
    // construct spell with modifiers
    CSpellConstructor *pConstructor = GetWorld()->GetSpellConstructor();
    if (!pConstructor)
        return reinterpret_cast<CSpell *>(ReportError("No valid spell constructor\n"));
    // begin spell construction
    pConstructor->SetSpellManager(this);
    if (!pConstructor->BeginConstructSpell(GetPrototype(szCodeName)))
        return reinterpret_cast<CSpell *>(
            ReportError("Can't begin spell construction with prototype \"%s\"\n", szCodeName));
    // add all runes
    for (int i = 0; i < szRunes.GetSize(); ++i)
    {
        if (!szRunes[i].IsEmpty())
            pConstructor->AddRune(GetModifier(szRunes[i]));
    }
    // end spell constrcution
    if (!pConstructor->EndConstructSpell())
        return reinterpret_cast<CSpell *>(ReportError("Can't end spell construction\n"));
    // extract created spell and reset spell manager
    CSpell *pSpell = pConstructor->GetSpellContainer()->GetSpell().Duplicate(this);
    pConstructor->SetSpellManager(0);

    return pSpell;
}
/////////////////////////////////////////////////////////////////////////////////////
CSpell *CSpellManager::CreateSpell(const SSDBSpellPrototype *pPrototype,
                                   const std::list<const SSDBModifier *> &runes)
{
    // construct spell with modifiers
    CSpellConstructor *pConstructor = GetWorld()->GetSpellConstructor();
    if (!pConstructor)
        return reinterpret_cast<CSpell *>(ReportError("No valid spell constructor\n"));
    // begin spell construction
    pConstructor->SetSpellManager(this);
    if (!pConstructor->BeginConstructSpell(pPrototype))
        return reinterpret_cast<CSpell *>(
            ReportError("Can't begin spell construction with prototype \"%s\"\n",
                        pPrototype->szCodeName));
    // add all runes
    int nCounter = 0;
    for (auto pos = runes.begin(); pos != runes.end(); ++pos, ++nCounter)
    {
        if (nCounter >= 8)
            break;
        pConstructor->AddRune(*pos);
    }
    // end spell constrcution
    if (!pConstructor->EndConstructSpell())
        return reinterpret_cast<CSpell *>(ReportError("Can't end spell construction\n"));
    // extract created spell and reset spell manager
    CSpell *pSpell = pConstructor->GetSpellContainer()->GetSpell().Duplicate(this);
    pConstructor->SetSpellManager(0);

    return pSpell;
}
/////////////////////////////////////////////////////////////////////////////////////
// register spell in spell database
BOOL CSpellManager::RegisterSpell(const CSpellContainer *pContainer)
{
    //	DWORD dwSubtype = lpSpell->GetSubtypeID();

    //	DWORD dwKey = spells[dwSubtype & 0xFFFF0000]->spells.FindFirstFreeKey();
    //	spells[dwSubtype & 0xFFFF0000]->spells[dwKey] = lpSpell;
    //	lpSpell->SetSubtypeID( dwSubtype | dwKey );
    //	lpSpell->SetSpellSubtypeID( lpSpell->GetSubtypeID() );
    //	lpSpell->AddRef();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
LPCSTR CSpellManager::GetPrototypeCodeName(LPCSTR pszName)
{
    for (int i = 0; i < theSpellDatabase.spells.GetNumEntries(); ++i)
    {
        if (theSpellDatabase.spells[i]->szName.CompareNoCase(pszName) == 0)
            return theSpellDatabase.spells[i]->szCodeName;
    }

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
LPCSTR CSpellManager::GetRuneCodeName(LPCSTR pszName)
{
    for (int i = 0; i < theSpellDatabase.runes.GetNumEntries(); ++i)
    {
        if (theSpellDatabase.runes[i]->szName.CompareNoCase(pszName) == 0)
            return theSpellDatabase.runes[i]->pszCode;
    }

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CSpellManager::GetPrototypeNames(CStringArray &szNames)
{
    szNames.SetSize(theSpellDatabase.spells.GetNumEntries());
    for (int i = 0; i < theSpellDatabase.spells.GetNumEntries(); ++i)
        szNames[i] = theSpellDatabase.spells[i]->szName;
}
/////////////////////////////////////////////////////////////////////////////////////
void CSpellManager::GetRuneNames(CStringArray &szNames)
{
    szNames.SetSize(theSpellDatabase.runes.GetNumEntries());
    for (int i = 0; i < theSpellDatabase.runes.GetNumEntries(); ++i)
        szNames[i] = theSpellDatabase.runes[i]->szName;
}
/////////////////////////////////////////////////////////////////////////////////////
