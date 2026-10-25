#pragma once
namespace SaveSchema { class Writer; class Reader; }
/////////////////////////////////////////////////////////////////////////////////////
#include "HashMap.h"
#include "GameBase.h"
#include "SmartPtr.h"
#include "SaveLoader.h"
#include <map>
/////////////////////////////////////////////////////////////////////////////////////
class CWorld;

/////////////////////////////////////////////////////////////////////////////////////
class CBaseManager : public CGameBase
{
    friend class CManagerIterator;
    const std::map<int, int>* omittedSaveObjects = nullptr;
    bool restoringObject = false;

protected:
    // main data
    CHashMap<int, LPGAMEBASE> hash;     // hash table to store data pointers in
    CList<LPGAMEBASE, LPGAMEBASE> list; // all the data above, placed in list for sequential search
    CHashMap<int, POSITION> respmap;    // map of the respects for fast find data in list by ID
    // trash data
    CHashMap<ETypeIDs, CList<LPGAMEBASE, LPGAMEBASE> *> trash; // trash hash table
    CHashMap<ETypeIDs, ETypeIDs> trashstatistics;              // data type id statistics
    // key range (default from '0' to 'MAXINT')
    int nKeyRangeFrom; // start of key range
    int nKeyRangeTo;   // end of key range
    //
    int nSwapCount;     // swap count
    int nSwapCriterion; // swap count criterion
    int nLastKey;       // last key, which was the last data registered under
    //
    CWorld *lpWorld; // pointer to the world (sa CGameBase derived object)
protected:
    // create
    virtual BOOL Create();
    // data create/release
    virtual LPGAMEBASE CreateInstant(ETypeIDs tidTypeID) = 0;
    // working with trash
    BOOL MoveToTrash(LPGAMEBASE lpData);
    LPGAMEBASE GetFromTrash(ETypeIDs tidTypeID);
    void SwapReset();

public:
    CBaseManager();
    virtual ~CBaseManager();
    // create/destroy functions
    virtual BOOL Create(CWorld *lpWorld);
    virtual void Destroy();
    virtual void RemoveAll();
    // registration
    int RegisterInstant(LPGAMEBASE lpData);                  // register data under 'first free' key
    LPGAMEBASE RegisterInstantAs(int id, LPGAMEBASE lpData); // register data under desired key
    LPGAMEBASE RemoveInstantAt(int id); // remove instant from specific location
    LPGAMEBASE ChangeInstantID(int nOldID, int nNewID);
    // work with resources
    // single resource file
    virtual BOOL AddNewResource(LPCSTR pszResourceFileName) { return FALSE; }
    virtual BOOL CloseResource(LPCSTR pszResourceFileName) { return FALSE; }
    // multiple resource operations
    virtual BOOL CloseResources() { return FALSE; }
    // file open from resource
    virtual LPBYTE GetFile(LPCSTR pszFileName, LPDWORD pnDataSize = NULL) { return NULL; }
    virtual bool CloseFile(LPBYTE pFileImage) { return false; }
    virtual BOOL CheckFileExist(LPCSTR pszFileName) { return FALSE; }
    virtual void CloseAllFiles() {}
    //
    void SetupKeyRange(int from, int to);
    //
    int GetNumElements() const { return hash.GetCount(); }
    int GetNumTrashElements() const { return trash.GetCount(); }
    // world function
    void SetWorld(CWorld *lpNewWorld);
    CWorld *GetWorld() const { return (CWorld *)lpWorld; }
    // swap criterion functions
    void SetSwapCriterion(int nNewSwapCriterion) { nSwapCriterion = nNewSwapCriterion; }
    int GetSwapCriterion() const { return nSwapCriterion; }
    void ClearTrash();
    // data creating/releasing
    virtual LPGAMEBASE CreateData(ETypeIDs tidTypeID, int id = -1);
    // Only in a RestoreNamespace and only on a staged manager. Bypasses trash
    // so the construction ticket cannot bind an identity to a reused object.
    CSmartPtr<CGameBase> CreateSavedData(ETypeIDs type, int id, ObjectId savedId);
    virtual void Release(LPGAMEBASE lpData);
    // Only while applying serialized state: omitted save IDs resolve to null.
    // The guard restores normal map lookups on success and exception alike.
    class SavedReferences {
        CBaseManager& manager;
        const std::map<int, int>* previous;
    public:
        SavedReferences(CBaseManager& m, const std::map<int, int>& omitted)
            : manager(m), previous(m.omittedSaveObjects) { m.omittedSaveObjects = &omitted; }
        ~SavedReferences() { End(); }
        void End() { manager.omittedSaveObjects = previous; }
        SavedReferences(const SavedReferences&) = delete;
        SavedReferences& operator=(const SavedReferences&) = delete;
    };
    bool IsOmittedSaveObject(int id) const {
        return omittedSaveObjects && omittedSaveObjects->find(id) != omittedSaveObjects->end();
    }
    int SavedObjectID(int id) const { return IsOmittedSaveObject(id) ? -1 : id; }
    // searching functions
    LPGAMEBASE Find(int nID);
    LPGAMEBASE FindType(ETypeIDs tidTypeID);
    // load&store
    // load/store for manager has 2 phases in first it stores types of objects in second
    // it stores data for each object in the manager, loading is inverse process
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
    void LoadData(CSave &file) = delete;
    void StoreData(CSave &file) = delete;
    void CheckReferences();

    void ForEach(std::function<void(CGameBase *)> fn);
public:
    void SavedKeyRange(int &first, int &last, int &previous) const { first = nKeyRangeFrom; last = nKeyRangeTo; previous = nLastKey; }
    void RestoreSavedKeyRange(int first, int last, int previous);
};
/////////////////////////////////////////////////////////////////////////////////////
class CManagerIterator
{
private:
    CBaseManager *lpManager; // pointer to data manager to iterate
    POSITION pos;

public:
    CManagerIterator();
    ~CManagerIterator();

    LPGAMEBASE GetFirst(CBaseManager *lpNewManager);
    LPGAMEBASE GetNext();
    BOOL IsReachEnd() const { return (pos == NULL); }
};
/////////////////////////////////////////////////////////////////////////////////////
inline CManagerIterator::CManagerIterator()
{
    lpManager = NULL;
}
inline CManagerIterator::~CManagerIterator()
{
    if (lpManager)
    {
        lpManager->DecRefCount();
        lpManager = NULL;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// load/store  arrays of pointers
template <class TYPE, class ARG_TYPE>
void StoreGBPtrArray(CSimplyArray<TYPE, ARG_TYPE> &arr, CSave &file) = delete;
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
void LoadGBPtrArray(CSimplyArray<TYPE, ARG_TYPE> &arr, CSave &file, CBaseManager *pManager) = delete;
/////////////////////////////////////////////////////////////////////////////////////
// load/store  list of pointers
template <class TYPE, class ARG_TYPE>
void StoreGBPtrList(CList<TYPE, ARG_TYPE> &arr, CSave &file) = delete;
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
void LoadGBPtrList(CList<TYPE, ARG_TYPE> &arr, CSave &file, CBaseManager *pManager) = delete;
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
void LoadGBPtrSmartList(CList<TYPE, ARG_TYPE> &arr, CSave &file, CBaseManager *pManager) = delete;
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
TYPE *LoadGBPtr(CSave &file, CBaseManager *pManager) = delete;
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
TYPE *LoadGBSmartPtr(CSave &file, CBaseManager *pManager) = delete;
/////////////////////////////////////////////////////////////////////////////////////
inline void StoreGBPtr(CSave &file, CGameBase *pObj) = delete;
/////////////////////////////////////////////////////////////////////////////////////
