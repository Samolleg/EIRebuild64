#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "BaseManager.h"
#include "SaveLoader.h"
// #include "World.h"

#include <list>
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
inline void RadixSortStageOne(const int shift, const TYPE *pSrc, TYPE *pDst, const int nCount)
{
    DWORD counts[256];
    TYPE *index[256];
    TYPE *addr;
    int i;

    MemSetDWord(reinterpret_cast<DWORD *>(counts), 0, 256);

    for (i = 0; i < nCount; i++)
        counts[(pSrc[i] >> shift) & 0xFF]++;
    for (i = 0, addr = pDst; i < 256; i++)
    {
        index[i] = addr;
        addr += counts[i];
    }
    for (i = 0; i < nCount; i++)
        *(index[(pSrc[i] >> shift) & 0xFF]++) = pSrc[i];
}

void RadixSort(BYTE *lpValues, const int nCount)
{
    static CBYTEArray tempSort;
    tempSort.SetSize(nCount); // ARRAY BOUNDS CHECK

    RadixSortStageOne(0, lpValues, tempSort.GetData(), nCount);
    memcpy(lpValues, tempSort.GetData(), nCount);
}
void RadixSort(WORD *lpValues, const int nCount)
{
    static CWORDArray tempSort;
    tempSort.SetSize(nCount); // ARRAY BOUNDS CHECK

    RadixSortStageOne(0, lpValues, tempSort.GetData(), nCount);
    RadixSortStageOne(8, tempSort.GetData(), lpValues, nCount);
}
void RadixSort(DWORD *lpValues, const int nCount)
{
    static CDWORDArray tempSort;
    tempSort.SetSize(nCount); // ARRAY BOUNDS CHECK

    RadixSortStageOne(0, lpValues, tempSort.GetData(), nCount);
    RadixSortStageOne(8, tempSort.GetData(), lpValues, nCount);
    RadixSortStageOne(16, lpValues, tempSort.GetData(), nCount);
    RadixSortStageOne(24, tempSort.GetData(), lpValues, nCount);
}
/////////////////////////////////////////////////////////////////////////////////////
CBaseManager::CBaseManager()
{
    nKeyRangeFrom = 0;        // start of key range
    nKeyRangeTo = 2000000000; // end of key range

    nSwapCount = -1;
    nSwapCriterion = -1;
    nLastKey = -1;

    lpWorld = NULL; // pointer to the world
}
/////////////////////////////////////////////////////////////////////////////////////
CBaseManager::~CBaseManager()
{
    // destroy all data structures
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CBaseManager::Create()
// create all internal data structures
BOOL CBaseManager::Create()
{
    // default key range
    nKeyRangeFrom = 0;        // start of key range
    nKeyRangeTo = 2000000000; // end of key range
    // main data
    hash.RemoveAll();
    hash.SetHashTableSize(1024);
    respmap.RemoveAll();
    respmap.SetHashTableSize(1024);
    // trash data
    trash.RemoveAll();
    trash.SetHashTableSize(256);
    trashstatistics.RemoveAll();
    trashstatistics.SetHashTableSize(256);

    nSwapCount = 0;
    nSwapCriterion = 5 * 60; // 5 minutes

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CBaseManager::Create( LPGAMEBASE lpWorld )
// create and register world
BOOL CBaseManager::Create(CWorld *lpWorld)
{
    SetWorld(lpWorld);

    if (Create() == FALSE)
    {
        SetWorld(NULL);
        return FALSE;
    }
    else
        return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CBaseManager::Destroy()
// destroy all data, this manager contain in and all internal data structures
void CBaseManager::Destroy()
{
    CHashMapIterator<int, LPGAMEBASE> hmi;
    LPGAMEBASE lpData = NULL;
    //
    // main data deleting
    //
    // set used mark for every object
    lpData = hmi.GetFirst(&hash);
    while (lpData)
    {
        lpData->AddRef();
        lpData = hmi.GetNext();
    }
    // destroying data
    std::vector<int> ids;
    lpData = hmi.GetFirst(&hash);
    while (lpData)
    {
        ids.push_back(lpData->GetID());
        lpData->Destroy();
        lpData = hmi.GetNext();
    }
    // release objects
    for (auto pos = ids.begin(); pos != ids.end(); ++pos)
    {
        LPGAMEBASE *ppGB = const_cast<LPGAMEBASE *>(hash(*pos));
        if (ppGB != 0)
            (*ppGB)->Release();
    }
    ids.clear();
    // clear list
    list.RemoveAll();
    // deleting data
    lpData = hmi.GetFirst(&hash);
    while (lpData)
    {
        delete lpData;
        lpData = hmi.GetNext();
    }
    // clear table and remove all data structures
    hash.RemoveAll();
    respmap.RemoveAll();
    //
    // trash data deleting
    ClearTrash();
    // clear table and remove all data structures
    trash.RemoveAll();
    trashstatistics.RemoveAll();
    // unregister world
    SetWorld(NULL);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CBaseManager::RemoveAll()
// remove all data from main hash map
void CBaseManager::RemoveAll()
{
    CHashMapIterator<int, LPGAMEBASE> hmi;
    LPGAMEBASE lpData;
    // set used mark for every object
    lpData = hmi.GetFirst(&hash);
    while (lpData)
    {
        lpData->AddRef();
        lpData = hmi.GetNext();
    }
    // destroying data
    lpData = hmi.GetFirst(&hash);
    while (lpData)
    {
        lpData->Destroy();
        lpData = hmi.GetNext();
    }
    // release objects
    lpData = hmi.GetFirst(&hash);
    while (lpData)
    {
        lpData->Release();
        lpData = hmi.GetNext();
    }
    //
    hash.ClearHashTable();
    list.RemoveAll();
    respmap.ClearHashTable();
}
/////////////////////////////////////////////////////////////////////////////////////
// set world this manager belong to
void CBaseManager::SetWorld(CWorld *lpNewWorld)
{
    lpWorld = lpNewWorld;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CBaseManager::CreateData( ETypeIDs tidTypeID, int id )
// create data instant (or get it from trash) and register under desired ID
LPGAMEBASE CBaseManager::CreateData(ETypeIDs tidTypeID, int id)
{
    // first, trying to find such data in trash
    LPGAMEBASE lpData = GetFromTrash(tidTypeID);
    if (lpData == NULL) // if no such data in trash, create new one
        lpData = CreateInstant(tidTypeID);
    if (lpData == NULL) // if we can't create such data, return with NULL
        return NULL;
    lpData->ResetAllFlags();
    lpData->SetTypeID(tidTypeID);
    // register data
    if (id == -1)
        RegisterInstant(lpData);
    else
        RegisterInstantAs(id, lpData);
    //
    lpData->Create();

    return lpData;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CBaseManager::RemoveInstantAt( int id )
// remove instant from specific location
LPGAMEBASE CBaseManager::RemoveInstantAt(int id)
{
    LPGAMEBASE lpData = NULL;

    if (hash.Lookup(id, lpData))
    {
        hash.RemoveAt(id);
        POSITION pos1 = respmap.RemoveAt(id);
        if (pos1)
            list.RemoveAt(pos1);
    }

    return lpData;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CBaseManager::Release( LPGAMEBASE lpData )
// if nRefCount == 0, move this data to trash
void CBaseManager::Release(LPGAMEBASE lpData)
{
    if (lpData && lpData->IsUnreferenced())
    {
        POSITION pos1 = respmap.RemoveAt(lpData->GetID());
        if (pos1)
            list.RemoveAt(pos1);
        lpData->Destroy();
        lpData->SetManager(NULL);
        MoveToTrash(lpData);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// int CBaseManager::RegisterInstant( LPGAMEBASE lpData )
// find first free key and register data under it in the hash map
// returns key, this data was registerd under
int CBaseManager::RegisterInstant(LPGAMEBASE lpData)
{
    int key = hash.FindFirstFreeKeyInRange(nKeyRangeFrom, nKeyRangeTo);
    // work with invalid-size hash table (no empty keys)
    if (key == -1)
        ReportError("CBaseManager::RegisterInstant.\n Can't find first free key.\n");
        // register data under first free found key
#ifdef _DEBUG
    LPGAMEBASE pValue = RegisterInstantAs(key, lpData);
    if (pValue != NULL)
        ReportError("CBaseManager::RegisterInstant.\n Missing data \"%s\" name \"%s\"",
                    pValue->GetGameClassName(), pValue->GetName());
#else
    RegisterInstantAs(key, lpData);
#endif // _DEBUG

    return key;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CBaseManager::RegisterInstantAs( int id, LPGAMEBASE lpData )
// register data in the hash map under desired key
// return old data from this entry
LPGAMEBASE CBaseManager::RegisterInstantAs(int id, LPGAMEBASE lpData)
{
    LPGAMEBASE pValue = NULL;
    //
    if (hash.Lookup(id, pValue) == FALSE)
    {
        pValue = NULL;
        list.AddTail(lpData);
        respmap[id] = list.GetTailPosition();
    }
    // set new one in this place
    hash.SetAt(id, lpData);
    lpData->SetID(id);
    nLastKey = id;
    // set data manager, which handles this (lpData) instant
    lpData->SetManager(this);
    // return old data (or NULL, if no one)
    return pValue;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CBaseManager::ChangeInstantID( int nOldID, int nNewID )
// change id for instant on the [nOldID] to [nNewID]
LPGAMEBASE CBaseManager::ChangeInstantID(int nOldID, int nNewID)
{
    LPGAMEBASE lpOldData = NULL;
    LPGAMEBASE lpData = NULL;
    // get data from desired (nNewID) position
    hash.Lookup(nNewID, lpOldData);
    // change id for data from nOldID to nNewID
    lpData = hash.RemoveAt(nOldID);
    hash.SetAt(nNewID, lpData);
    lpData->SetID(nNewID);
    // change id for POSITION
    POSITION pos1 = respmap.RemoveAt(nOldID);
    respmap.SetAt(nNewID, pos1);

    return lpOldData;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CBaseManager::Find( int nID )
// find data by it's id
LPGAMEBASE CBaseManager::Find(int nID)
{
    LPGAMEBASE pValue = NULL;
    hash.Lookup(nID, pValue);
    return pValue;
}
/////////////////////////////////////////////////////////////////////////////////////
LPGAMEBASE CBaseManager::FindType(ETypeIDs tidTypeID)
{
    // then, iterate hash table in order to find desired data
    CHashMapIterator<int, LPGAMEBASE> hmi;
    LPGAMEBASE lpData = hmi.GetFirst(&hash);

    while (lpData)
    {
        if (lpData->GetTypeID() == tidTypeID)
            return lpData;

        lpData = hmi.GetNext();
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
void *__cdecl operator new(unsigned int uSize, const char *pszFileName, int nLineNumber);

// BOOL CBaseManager::MoveToTrash( LPGAMEBASE lpData )
// move data from main hash to trash one
BOOL CBaseManager::MoveToTrash(LPGAMEBASE lpData)
{
    CList<LPGAMEBASE, LPGAMEBASE> *pList = NULL;
    ETypeIDs tid = lpData->GetTypeID();
    int key = lpData->GetID();
    LPGAMEBASE pValue = NULL;

    // remove this data from hash table only if data on this entry are equal to lpData
    if (hash.Lookup(key, pValue) && (pValue == lpData))
        RemoveInstantAt(key);
    // trying to place it in the trash hash table
    if (trash.Lookup(tid, pList))
    {
        pList->AddTail(lpData);
        lpData->SetID(-1);
        int value = int(trashstatistics[tid]) + 1;
        trashstatistics[tid] = ETypeIDs(value);
    }
    else
    {
        pList = new CList<LPGAMEBASE, LPGAMEBASE>;
        trash[tid] = pList;

        pList->AddTail(lpData);
        lpData->SetID(-1);
        int value = int(trashstatistics[tid]) + 1;
        trashstatistics[tid] = ETypeIDs(value);
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CBaseManager::GetFromTrash( ETypeIDs tidTypeID )
// trying to find data with desired type id in the trash. if it is, remove it from trash
// and return to end user. else, return NULL
LPGAMEBASE CBaseManager::GetFromTrash(ETypeIDs tidTypeID)
{
    // first, check for such data presence
    if (trashstatistics[tidTypeID] <= 0)
        return NULL;
    // and, then, try to get such data from trash list
    CList<LPGAMEBASE, LPGAMEBASE> *pList = NULL;

    if (trash.Lookup(tidTypeID, pList) && !pList->IsEmpty())
        return pList->RemoveHead();
    else
        return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CBaseManager::SwapReset()
// increase swap count. if it equal or greater, than swap criterion, clear trash and
// perform main hash table iteration in order to find new data for trash
void CBaseManager::SwapReset()
{
    nSwapCount++;
    if (nSwapCount >= nSwapCriterion)
    {
        // remove old trash data
        nSwapCount = 0;
        ClearTrash();
        // check for new trash data in the main hash table
        CHashMapIterator<int, LPGAMEBASE> hmi;
        LPGAMEBASE lpData = hmi.GetFirst(&hash);

        while (lpData)
        {
            if (lpData->IsUnreferenced())
                MoveToTrash(lpData);

            lpData = hmi.GetNext();
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CBaseManager::ClearTrash()
// delete all elements from trash hash table
void CBaseManager::ClearTrash()
{
    CHashMapIterator<ETypeIDs, CList<LPGAMEBASE, LPGAMEBASE> *> hmi;
    CList<LPGAMEBASE, LPGAMEBASE> *lpList = hmi.GetFirst(&trash);
    LPGAMEBASE lpData = NULL;
    POSITION pos = NULL;
    // iterate all lists
    while (lpList)
    {
        // for each list delete all elements
        pos = lpList->GetHeadPosition();
        while (pos)
        {
            lpData = lpList->GetNext(pos);
            delete lpData;
        }
        //
        lpList->RemoveAll();
        // delete list
        delete lpList;
        //
        lpList = hmi.GetNext();
    }
    // clear hash table data and statistics data
    trash.ClearHashTable();
    trashstatistics.ClearHashTable();
}
/////////////////////////////////////////////////////////////////////////////////////
void CBaseManager::SetupKeyRange(int from, int to)
{
    // for hash keys in range
    DWORD dwLargestKey = 0;
    for (auto pos = hash.begin(); pos != hash.end(); ++pos)
    {
        const DWORD &key = pos->first;
        if ((key >= from) && (key <= to))
            dwLargestKey = Max(dwLargestKey, key);
    }
    //
    hash.SetLastLargestKey(dwLargestKey);
    //
    nKeyRangeFrom = from;
    nKeyRangeTo = to;
}
/////////////////////////////////////////////////////////////////////////////////////
void CBaseManager::Load(CSave &file)
{
    int nCount, i;
    ASSERT(list.GetCount() == 0);
    ClearTrash();
    file.Read(nCount);
    for (i = 0; i < nCount; i++)
    {
        int nID;
        int typeID;
        DWORD dwSubtype;
        file.Read(nID);
        file.Read(typeID);
        file.Read(dwSubtype);
        LPGAMEBASE lpData = CreateData(ETypeIDs(typeID), nID);
        lpData->SetSubtypeID(dwSubtype);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CBaseManager::Store(CSave &file)
{
    file.Write(list.GetCount());
    POSITION pos = list.GetHeadPosition();
    while (pos)
    {
        CGameBase *pObj = list.GetNext(pos);
        file.Write(pObj->GetID());
        file.Write((int)pObj->GetTypeID());
        file.Write(pObj->GetSubtypeID());
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CBaseManager::LoadData(CSave &file)
{
    int nID;
    char ch;
    for (;;)
    {
        file.Read(nID);
        if (nID == -1313)
        {
            file.Read(ch);
            if (ch == 0)
                break;
        }
        CGameBase *pObj = Find(nID);
        ASSERT(pObj);
        if (!pObj)
            return;
        pObj->Load(file);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CBaseManager::StoreData(CSave &file)
{
    POSITION pos = list.GetHeadPosition();
    while (pos)
    {
        CGameBase *pObj = list.GetNext(pos);
        file.Write(pObj->GetID());
        if (pObj->GetID() == -1313)
            file.Write((char)1);
        pObj->Store(file);
    }
    file.Write((int)-1313);
    file.Write((char)0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CBaseManager::CheckReferences()
{
    POSITION pos = list.GetHeadPosition();
    bool bHasUnreferred = false;
    while (pos)
    {
        CGameBase *pObj = list.GetNext(pos);
        if (!pObj->IsReferenced())
        {
            bHasUnreferred = true;
            pObj->AddRef();
            pObj->Release();
        }
    }
    ASSERT(!bHasUnreferred);
}
/////////////////////////////////////////////////////////////////////////////////////
void CBaseManager::ForEach(std::function<void(CGameBase *)> fn)
{
    for (auto pos = list.GetHeadPosition(); pos;)
        fn(list.GetNext(pos));
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
//                               CManagerIterator
//                               //
// ******************************************************************************** //
//////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CManagerIterator::GetFirst( CBaseManager* lpManager )
// gets first element from data manager
LPGAMEBASE CManagerIterator::GetFirst(CBaseManager *lpNewManager)
{
    // release old manager
    if (lpManager)
        lpManager->DecRefCount();
    // set new one
    lpManager = lpNewManager;
    if (lpManager)
        lpManager->AddRef();
    // begin iteration
    pos = lpManager->list.GetHeadPosition();
    if (pos)
        return lpManager->list.GetNext(pos);
    else
        return NULL;

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CManagerIterator::GetNext()
// get next element from data manager during iteration
LPGAMEBASE CManagerIterator::GetNext()
{
    if (pos)
        return lpManager->list.GetNext(pos);
    else
        return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
