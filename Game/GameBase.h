#pragma once
namespace SaveSchema { class Writer; class Reader; [[noreturn]] void RequireDynamicSchema(); }

#include <AfxTempl.h>    // MFC template library
#include "mm_malloc.h"   // malloc and free blocks, aligned on 32 bytes
#include "SimplyArray.h" // MFC array analog w/o constructor/destructor call
#include "HashMap.h"     // simply fast hash map w/o constructor/destructor call
#include "sArray.h"      // 2d arrays & array with copy operation
#include "Tools.h"       // some useful functions
#include "ObjectIdentity.h"
#include <lua.hpp>
/////////////////////////////////////////////////////////////////////////////////////
class CBaseManager;
class CWorld;
class CWorldBase;
class CSave;
/////////////////////////////////////////////////////////////////////////////////////
#define MAKE_FLAG(base, add) (1 << static_cast<DWORD>((base) + (add)))
#define MAKE_LAST_BIT(base, num_bits_used)         \
    (static_cast<DWORD>(static_cast<DWORD>(base) + \
                        static_cast<DWORD>(static_cast<int>(num_bits_used) + 1)))

const DWORD GENERIC_LAST_BIT = 0;

const DWORD CGB_INVALID = MAKE_FLAG(GENERIC_LAST_BIT, 0);
const DWORD CGB_UPDATED = MAKE_FLAG(GENERIC_LAST_BIT, 1);
// const DWORD CGB_JUSTCREATED = MAKE_FLAG( GENERIC_LAST_BIT, 2 );

const DWORD CGB_LAST_BIT = MAKE_LAST_BIT(GENERIC_LAST_BIT, 2);
// const DWORD CGB_INVALID     = 0x80000000;
// const DWORD CGB_UPDATED     = 0x40000000;
// const DWORD CGB_JUSTCREATED = 0x20000000;
/////////////////////////////////////////////////////////////////////////////////////
class CGameBase
{
private:
    ObjectIdentity::Instance identity;
    int nRefCount; // reference count. access to it only via IncRefCount/DecRefCount
    DWORD dwFlags; // different flags

    int nID;            // unique ID for each instance
    ETypeIDs tidTypeID; // type id for each instance (set during data creation in manager)
protected:
    CBaseManager *
        lpManager; // data manager, which creates this instant (or this instant was registered with)

    DWORD dwSubtypeID; // subtype id of this instant (loaded from table)
#if defined(_DEBUG) && !defined(__NOMFC__)
    const char *pszGameClassName;
#endif // defined(_DEBUG) && !defined(__NOMFC__)
public:
    CGameBase();
    CGameBase(const CGameBase &base);
    virtual ~CGameBase();
    // operator= : no comments
    virtual void operator=(const CGameBase &base);
    // create/destroy functions
    virtual BOOL Create() = 0;
    virtual void Destroy() = 0;
    // id, type id and subtype id functions
    void SetID(int nNewID) { nID = nNewID; }
    int GetID() const { return nID; }
    ObjectId GetRTID() const { return identity.GetId(); }
    void SetTypeID(ETypeIDs tidNewTypeID) { tidTypeID = tidNewTypeID; }
    ETypeIDs GetTypeID() const { return tidTypeID; }
    void SetSubtypeID(DWORD dwNewSubtypeID) { dwSubtypeID = dwNewSubtypeID; }
    DWORD GetSubtypeID() const { return dwSubtypeID; }
    // manager functions
    CBaseManager *SetManager(CBaseManager *lpManager);
    CBaseManager *GetManager() const { return lpManager; }
    CWorld *GetWorld() const;
    CWorldBase *GetWorldBase() const;
    // reference count functions
    int GetRefCount() const { return nRefCount; }
    BOOL IsReferenced() const { return (nRefCount != 0); }
    BOOL IsUnreferenced() const { return (nRefCount == 0); }
    int AddRef();
    int DecRefCount();
    virtual void Release();
    // функция должна освободить все ссылки на другие объекты, находящиеся в этом объекте
    virtual void FreeReferences() {}
    // flags functions (may be this functions unnecessary)
    void SetFlag(DWORD dwFlag) { dwFlags |= dwFlag; }
    void ResetFlag(DWORD dwFlag) { dwFlags &= ~dwFlag; }
    void ToggleFlag(DWORD dwFlag) { dwFlags ^= dwFlag; }
    DWORD CheckFlag(DWORD dwFlag) const { return (dwFlags & dwFlag); }
    void ResetAllFlags() { dwFlags = 0; }
    DWORD GetFlag() const { return dwFlags; }
    DWORD GetFlags() const { return dwFlags; }
    // validity information, uses 31st bit of dwFlags
    void Invalidate() { SetFlag(CGB_INVALID); }
    BOOL IsValid() const { return CheckFlag(CGB_INVALID) == 0; }
    BOOL IsInvalid() const { return CheckFlag(CGB_INVALID) != 0; }
    BOOL InvalidAndReleased() { return IsValid() ? FALSE : (Release(), TRUE); }
    // update information ( for server )
    void SetUpdated() { SetFlag(CGB_UPDATED); }
    BOOL IsUpdated() const { return CheckFlag(CGB_UPDATED) != 0; }
    BOOL IsNotUpdated() const { return CheckFlag(CGB_UPDATED) == 0; }
    void ResetUpdated() { ResetFlag(CGB_UPDATED); } //| CGB_JUSTCREATED ); }
    // void SetJustCreated() { SetFlag( CGB_JUSTCREATED ); }
    // BOOL IsJustCreated() const { return CheckFlag( CGB_JUSTCREATED ) != 0; }
    // BOOL IsNotJustCreated() const { return CheckFlag( CGB_JUSTCREATED ) == 0; }
    // segment virtual function
    virtual int Segment() { return 0; }
    // instant identification information (optional)
    virtual LPCSTR GetName() const { return "noname"; }
    virtual LPCSTR GetDesc() const { return "nodesc"; }
    // load&store
    virtual void Load(CSave &file) { SaveSchema::RequireDynamicSchema(); }
    virtual void Store(CSave &file) { SaveSchema::RequireDynamicSchema(); }
    // class identification functions
    virtual LPCSTR GetGameClassName() const = 0;
    virtual EClassIDs WhoAmI() const = 0;
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave() {}
};
/////////////////////////////////////////////////////////////////////////////////////
#if !defined(__NOMFC__)
typedef CList<CGameBase *, CGameBase *> CGameBaseList;
#endif // !defined(__NOMFC__)
/////////////////////////////////////////////////////////////////////////////////////
inline CGameBase::CGameBase()
{
    nRefCount = 0;
    nID = -1;
    dwSubtypeID = 0;
    tidTypeID = TID_EMPTY;
    dwFlags = 0;
    lpManager = NULL;
#if defined(_DEBUG) && !defined(__NOMFC__)
    pszGameClassName = "undefined";
#endif // defined(_DEBUG) && !defined(__NOMFC__)
}
inline void CGameBase::operator=(const CGameBase &base)
{
    dwFlags = base.dwFlags;

    tidTypeID = base.tidTypeID;
    dwSubtypeID = base.dwSubtypeID;
}
inline CGameBase::CGameBase(const CGameBase &base) : CGameBase()
{
    (*this) = base;
}
/////////////////////////////////////////////////////////////////////////////////////
#if (!(defined _DEBUG) || (defined __3DFP_DLL__))
inline int CGameBase::AddRef()
{
    return (++nRefCount);
}
inline int CGameBase::DecRefCount()
{
    return (--nRefCount);
}
#endif // ( !(defined _DEBUG) || !(defined __GAME__) )
// --------------------------------------------------------------------------------------------
typedef CGameBase *LPGAMEBASE;
typedef CSimplyArray<LPGAMEBASE, LPGAMEBASE> CGameBasePtrArray;
