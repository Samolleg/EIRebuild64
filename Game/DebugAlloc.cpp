#include "StdAfx.h"

#include "DebugAlloc.h"
#include <assert.h>

#include "Notification.h"

#include <unordered_map>
#include <list>
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if defined(FORCE_ALLOC_CHECK)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint32_t ALLOC_CHECK_GUARD_BAND_WIDTH = 64;
const uint32_t ALLOC_CHECK_DELETED = 0xDD;
const uint32_t ALLOC_CHECK_ALLOCATED = 0xCD;
const uint32_t ALLOC_CHECK_GUARD = 0xFF;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma pack(4)
struct SDebugAllocPlace
{
    const char *pszFileName;
    uint32_t uLineNumber;
    //
    SDebugAllocPlace() { pszFileName = 0; }
    SDebugAllocPlace(const char *pszNewFileName, uint32_t uNewLineNumber)
        : pszFileName(pszNewFileName)
        , uLineNumber(uNewLineNumber)
    {
    }
};
struct SDebugAllocInfo : public SDebugAllocPlace
{
    uint32_t uSize;
    uint32_t uAllocIndex;

    SDebugAllocInfo() {}
    SDebugAllocInfo(uint32_t uNewSize, const char *pszFileName, uint32_t uLineNumber, uint32_t uNewAllocIndex)
        : SDebugAllocPlace(pszFileName, uLineNumber)
        , uSize(uNewSize)
        , uAllocIndex(uNewAllocIndex)
    {
    }
};
struct SDebugAllocInfoFull : public SDebugAllocInfo
{
    DWORD dwPtr;
};
struct SDebugAllocStat
{
    uint32_t uNumAllocs;       // number of alloc calls in this alloc place
    uint32_t uNumFrees;        // number of dealloc calls for the memory, allocated in this alloc place
    uint32_t uAllocatedMemory; // total allocated memory from this alloc place
    uint32_t uFreedMemory;     // total deallocated memory, allocated in this alloc place
    //
    SDebugAllocStat()
        : uNumAllocs(0)
        , uNumFrees(0)
        , uAllocatedMemory(0)
        , uFreedMemory(0)
    {
    }
};
#pragma pack()
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
size_t StringHashFunction(const char *pszString)
{
    unsigned long __h = 0;
    for (; *pszString; ++pszString)
        __h = 5 * __h + *pszString;

    return size_t(__h);
}
struct SDebugAllocPlaceHashFunc
{
    size_t operator()(const SDebugAllocPlace &stat) const
    {
        return StringHashFunction(stat.pszFileName) + stat.uLineNumber;
    }
};
struct SDebugAllocPlaceEqualKey
{
    bool operator()(const SDebugAllocPlace &stat1, const SDebugAllocPlace &stat2) const
    {
        return (strcmp(stat1.pszFileName, stat2.pszFileName) == 0) &&
               (stat1.uLineNumber == stat2.uLineNumber);
    }
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef std::unordered_map<DWORD, SDebugAllocInfo> CAllocInfoTrackMap;
typedef std::unordered_map<SDebugAllocPlace, SDebugAllocStat, SDebugAllocPlaceHashFunc,
                           SDebugAllocPlaceEqualKey>
    CDebugAllocStatMap;
static CDebugAllocStatMap *pAllocInfoStats = 0;
static CAllocInfoTrackMap *pAllocInfoTracks = 0;
static bool bTrackDebugAllocs = false;
static uint32_t uAllocTrackIndex = 0;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CheckMemory(void *ptr, const SDebugAllocInfo &dai, uint32_t uGuardBand, BYTE cGuardFill);
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
std::list<int> debugallocbreaks;
void SetDebugAllocBreak(int nAllocIndex)
{
    debugallocbreaks.push_back(nAllocIndex);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool GetAllocStats(SExternalDebugAllocStat *&stats, int &nNumStats)
{
    if (pAllocInfoStats == 0)
        return false;
    nNumStats = pAllocInfoStats->size();
    if (nNumStats == 0)
        return false;
    if (stats != 0)
        OutputDebugString("\n\n\nstats pointer, passed to GetAllocStats() function are not equal "
                          "to zero!!!\n\n\n");
    stats = new SExternalDebugAllocStat[nNumStats];
    nNumStats = 0;
    for (auto pos = pAllocInfoStats->begin(); pos != pAllocInfoStats->end(); ++pos)
    {
        stats[nNumStats].pszFileName = pos->first.pszFileName;
        stats[nNumStats].uLineNumber = pos->first.uLineNumber;
        stats[nNumStats].uAllocatedMemory = pos->second.uAllocatedMemory;
        stats[nNumStats].uFreedMemory = pos->second.uFreedMemory;
        stats[nNumStats].uNumAllocs = pos->second.uNumAllocs;
        stats[nNumStats].uNumFrees = pos->second.uNumFrees;
        ++nNumStats;
    }

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// static uint32_t uLargestAlloc = 0;
void AddMemAllocStat(const char *pszFileName, uint32_t uLineNumber, uint32_t uSize)
{
    if (!bTrackDebugAllocs)
        return;
    if (bTrackDebugAllocs && (pAllocInfoStats == 0))
        pAllocInfoStats = new CDebugAllocStatMap;
    SDebugAllocPlace key(pszFileName, uLineNumber);
    SDebugAllocStat &stat = (*pAllocInfoStats)[key];
    stat.uAllocatedMemory += uSize;
    stat.uNumAllocs++;
    /*
    //
    uint32_t uAlloc = stat.uAllocatedMemory - stat.uFreedMemory;
    if ( (strcmp("R:\\Soft\\Users\\Yuri\\Work\\Game\\BaseManager.cpp", pszFileName) == 0) &&
    (uLargestAlloc < uAlloc) ) uLargestAlloc = uAlloc;
            */
}
void AddMemFreeStat(const char *pszFileName, uint32_t uLineNumber, uint32_t uSize)
{
    if (!bTrackDebugAllocs)
        return;
    if (bTrackDebugAllocs && (pAllocInfoStats == 0))
        pAllocInfoStats = new CDebugAllocStatMap;

    SDebugAllocPlace key(pszFileName, uLineNumber);
    SDebugAllocStat &stat = (*pAllocInfoStats)[key];
    stat.uFreedMemory += uSize;
    stat.uNumFrees++;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void AddDebugTrack(void *ptr, uint32_t uSize, const char *pszFileName, uint32_t uLineNumber,
                   uint32_t uGuardBand, BYTE cMemFill, BYTE cGuardFill)
{
    if (!pAllocInfoTracks)
    {
        pAllocInfoTracks = new CAllocInfoTrackMap;
        bTrackDebugAllocs = true;
    }
    if (!bTrackDebugAllocs)
        return;
    // fill guard band by ALLOC_CHECK_GUARD and other memory by ALLOC_CHECK_ALLOCATED
    if (uGuardBand != 0)
    {
        memset(ptr, cGuardFill, uGuardBand);
        memset((void *)(uint32_t(ptr) + uGuardBand + uSize), cGuardFill, uGuardBand);
    }
    memset((void *)(uint32_t(ptr) + uGuardBand), cMemFill, uSize);

    DWORD dwKey = DWORD(ptr);
    assert(pAllocInfoTracks->find(dwKey) == pAllocInfoTracks->end());
    pAllocInfoTracks->insert(
        CAllocInfoTrackMap::value_type(dwKey, SDebugAllocInfo(uSize, pszFileName, uLineNumber,
                                                              uAllocTrackIndex)));
    if (std::find(debugallocbreaks.begin(), debugallocbreaks.end(), uAllocTrackIndex) !=
        debugallocbreaks.end())
    {
        _asm int 3
    }
    uAllocTrackIndex++;
    AddMemAllocStat(pszFileName, uLineNumber, uSize);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void *RemoveDebugTrack(void *ptr, uint32_t uGuardBand, BYTE cMemFill, BYTE cGuardFill)
{
    if (!pAllocInfoTracks || !bTrackDebugAllocs)
        return ptr;

    void *ptr1 = (void *)(uint32_t(ptr) - uGuardBand);
    DWORD dwKey = DWORD(ptr1);
    auto pos = pAllocInfoTracks->find(dwKey);
    if (pos == pAllocInfoTracks->end())
        return ptr; // this is not an our memory block - just return pointer...
    // check guard band (pre- and post-)
    if (uGuardBand != 0)
        CheckMemory(ptr, pos->second, uGuardBand, cGuardFill);
    // fill memory as deleted
    memset(ptr1, cMemFill, pos->second.uSize + 2 * uGuardBand);
    //
    AddMemFreeStat(pos->second.pszFileName, pos->second.uLineNumber, pos->second.uSize);
    pAllocInfoTracks->erase(dwKey);
    //
    return ptr1;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct SCompareAllocInfoBySizeFunctional
{
    bool operator()(const SDebugAllocInfoFull &info1, const SDebugAllocInfoFull &info2) const
    {
        if (info1.uSize == info2.uSize)
            return strcmp(info1.pszFileName, info2.pszFileName) == -1;
        else
            return info1.uSize < info2.uSize;
    }
};
struct SCompareAllocInfoByFileNameFunctional
{
    bool operator()(const SDebugAllocInfoFull &info1, const SDebugAllocInfoFull &info2) const
    {
        int nCmp = strcmp(info1.pszFileName, info2.pszFileName);
        if (nCmp == 0)
        {
            if (info1.uSize == info2.uSize)
            {
                if (info1.uLineNumber == info2.uLineNumber)
                    return info1.uAllocIndex < info2.uAllocIndex;
                else
                    return info1.uLineNumber < info2.uLineNumber;
            }
            else
                return info1.uSize < info2.uSize;
        }
        else
            return nCmp == -1;
    }
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CheckMemory(void *ptr, const SDebugAllocInfo &dai, uint32_t uGuardBand, BYTE cGuardFill)
{
    if (uGuardBand == 0)
        return;

    void *ptr1 = (void *)(DWORD(ptr) - uGuardBand);

    BYTE *pPreGuard = (BYTE *)ptr1;
    for (uint32_t i = 0; i < uGuardBand; ++i)
    {
        if (pPreGuard[i] != cGuardFill)
        {
            ReportError("Memory Block Pre-Guard Damaged (0x%.2X != 0xFF)!\n%s(%d) : {%d} normal "
                        "block at 0x%.8X, %d bytes long.\n",
                        pPreGuard[i], dai.pszFileName, dai.uLineNumber, dai.uAllocIndex, ptr,
                        dai.uSize);
            break;
        }
    }
    BYTE *pPostGuard = ((BYTE *)ptr1) + dai.uSize + uGuardBand;
    for (uint32_t i = 0; i < uGuardBand; ++i)
    {
        if (pPostGuard[i] != cGuardFill)
        {
            ReportError("Memory Block Post-Guard Damaged (0x%.2X != 0xFF)!\n%s(%d) : {%d} normal "
                        "block at 0x%.8X, %d bytes long.\n",
                        pPostGuard[i], dai.pszFileName, dai.uLineNumber, dai.uAllocIndex, ptr,
                        dai.uSize);
            break;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReportMemoryBlock(DWORD dwPtr, const SDebugAllocInfo &dai)
{
    char buff[128], buff2[256];
    //
    BYTE *ptr = (BYTE *)dwPtr;
    strcpy(buff, " Data: <");
    memcpy(buff + 8, ptr, 16);
    buff[8 + 16] = 0;
    for (int i = 8; i < 8 + 16; ++i)
    {
        if (buff[i] == 0)
            buff[i] = 32;
    }
    // info
    sprintf(buff2, "%s(%d) : {%d} normal block at 0x%.8X, %d bytes long.\n", dai.pszFileName,
            dai.uLineNumber, dai.uAllocIndex, dwPtr, dai.uSize);
    OutputDebugString(buff2);
    //
    sprintf(buff2,
            "%s> %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X\n",
            buff, ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7], ptr[8], ptr[9],
            ptr[10], ptr[11], ptr[12], ptr[13], ptr[14], ptr[15]);
    OutputDebugString(buff2);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void DumpUnfreedMemory(EMemoryAllocTrackSort sorttype, bool bDestroyStatInfo)
{
    uint32_t uGuardBand = ALLOC_CHECK_GUARD_BAND_WIDTH;
    BYTE cGuardFill = ALLOC_CHECK_GUARD;
    //
    if (!pAllocInfoTracks)
        return;

    if (!pAllocInfoTracks->empty())
    {
        uint32_t uTotalSize = 0;
        char buff[1024];

        sprintf(buff, "-----------------------------------------------------------\n");
        OutputDebugString(buff);
        sprintf(buff, "Detected memory leaks!\nDumping objects ->\n");
        OutputDebugString(buff);

        if (sorttype == AMTS_NONE)
        {
            for (auto pos = pAllocInfoTracks->begin(); pos != pAllocInfoTracks->end(); ++pos)
            {
                CheckMemory((void *)(pos->first + uGuardBand), pos->second, uGuardBand, cGuardFill);
                ReportMemoryBlock(pos->first + uGuardBand, pos->second);
                uTotalSize += pos->second.uSize;
            }
        }
        else
        {
            // prepare list
            std::list<SDebugAllocInfoFull> allocs;
            for (auto pos1 = pAllocInfoTracks->begin(); pos1 != pAllocInfoTracks->end(); ++pos1)
            {
                CheckMemory((void *)(pos1->first + uGuardBand), pos1->second, uGuardBand,
                            cGuardFill);
                //
                SDebugAllocInfoFull info;
                info.uAllocIndex = pos1->second.uAllocIndex;
                info.uSize = pos1->second.uSize;
                info.pszFileName = pos1->second.pszFileName;
                info.uLineNumber = pos1->second.uLineNumber;
                info.dwPtr = pos1->first;
                allocs.push_back(info);
            }
            // sort it
            if (sorttype == AMTS_BYSIZE)
                allocs.sort(SCompareAllocInfoBySizeFunctional());
            else if (sorttype == AMTS_BYFILENAME)
                allocs.sort(SCompareAllocInfoByFileNameFunctional());
            // report
            for (auto pos = allocs.begin(); pos != allocs.end(); ++pos)
            {
                ReportMemoryBlock(pos->dwPtr + uGuardBand, *pos);
                uTotalSize += pos->uSize;
            }
        }
        sprintf(buff, "Object dump complete.\n");
        OutputDebugString(buff);
        sprintf(buff, "-----------------------------------------------------------\n");
        OutputDebugString(buff);
        sprintf(buff, "Total Unfreed: %d bytes\n\n", uTotalSize);
        OutputDebugString(buff);

        if (bDestroyStatInfo)
            pAllocInfoTracks->clear();
    }

    if (bDestroyStatInfo)
    {
        bTrackDebugAllocs = false;
        delete pAllocInfoTracks;
        delete pAllocInfoStats;
        pAllocInfoTracks = 0;
        pAllocInfoStats = 0;
    }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void *__cdecl operator new(unsigned int uSize, const char *pszFileName, int nLineNumber)
{
    void *ptr = malloc(uSize + 2 * ALLOC_CHECK_GUARD_BAND_WIDTH);
    AddDebugTrack(ptr, uSize, pszFileName, nLineNumber, ALLOC_CHECK_GUARD_BAND_WIDTH,
                  ALLOC_CHECK_ALLOCATED, ALLOC_CHECK_GUARD);
    return (void *)(DWORD(ptr) + ALLOC_CHECK_GUARD_BAND_WIDTH);
};
void __cdecl operator delete(void *ptr)
{
    ptr = RemoveDebugTrack(ptr, ALLOC_CHECK_GUARD_BAND_WIDTH, ALLOC_CHECK_DELETED,
                           ALLOC_CHECK_GUARD);
    free(ptr);
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif // defined( FORCE_ALLOC_CHECK )
