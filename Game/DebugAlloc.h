#pragma once
typedef unsigned int uint;
//#define FORCE_ALLOC_CHECK
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
enum EMemoryAllocTrackSort
{
    AMTS_NONE,
    AMTS_BYSIZE,
    AMTS_BYFILENAME
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma pack(4)
struct SExternalDebugAllocStat
{
    // identification
    const char *pszFileName;
    uint uLineNumber;
    // allocation params
    uint uNumAllocs;       // number of alloc calls in this alloc place
    uint uNumFrees;        // number of dealloc calls for the memory, allocated in this alloc place
    uint uAllocatedMemory; // total allocated memory from this alloc place
    uint uFreedMemory;     // total deallocated memory, allocated in this alloc place
    //
    SExternalDebugAllocStat()
        : pszFileName(0)
        , uLineNumber(0)
        , uNumAllocs(0)
        , uNumFrees(0)
        , uAllocatedMemory(0)
        , uFreedMemory(0)
    {
    }
};
#pragma pack()
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if defined(FORCE_ALLOC_CHECK)
#pragma warning(disable : 4291)
//
void DumpUnfreedMemory(EMemoryAllocTrackSort sorttype = AMTS_BYSIZE, bool bDestroyStatInfo = true);
bool GetAllocStats(SExternalDebugAllocStat *&stats, int &nNumStats);
void SetDebugAllocBreak(int nAllocIndex);
//
void *__cdecl operator new(unsigned int uSize, const char *pszFileName, int nLineNumber);
void __cdecl operator delete(void *ptr);
#define ALLOC_TRACK_NEW new (LOCAL_FILE, __LINE__)
#define dbgnew ALLOC_TRACK_NEW
//
#else
//
inline void DumpUnfreedMemory(EMemoryAllocTrackSort sorttype = AMTS_BYSIZE,
                              bool bDestroyStatInfo = true)
{
}
inline void SetDebugAllocBreak(int nAllocIndex)
{
}
inline bool GetAllocStats(SExternalDebugAllocStat *&stats, int &nNumStats)
{
    stats = 0;
    nNumStats = 0;
    return false;
}
#define dbgnew new
//
#endif // defined( FORCE_ALLOC_CHECK )
