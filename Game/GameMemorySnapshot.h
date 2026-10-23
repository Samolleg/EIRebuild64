#pragma once
#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <limits>
#include <malloc.h>
#pragma comment(lib, "psapi.lib")

// Fixed-size values only: safe to collect even after an allocation failure.
namespace GameJournal {
struct TextureMemory {
    std::uint64_t count=0, loaded=0, bytes=0, lruCount=0, lruBytes=0, lruAllowed=0;
    std::uint64_t cacheCount=0, cacheBytes=0;
    bool available=false;
};
struct GpuMemory {
    std::uint64_t localUsage=0, localBudget=0, nonlocalUsage=0, nonlocalBudget=0;
    bool local=false, nonlocal=false;
};
struct ResourceMemory {
    std::uint64_t hostPayload=0, mappedArchives=0, mappedSaved=0, ownedImmutable=0;
    std::uint64_t geometry=0, textures=0, animation=0, world=0, surfaceCpu=0, surfaceGpu=0, neighbours=0;
    bool rendererAvailable=false;
};
struct ProcessMemory {
    std::uint64_t privateBytes=0, workingSet=0, peakWorkingSet=0, pagefile=0;
    std::uint64_t vaTotal=0, vaFree=0, vaLargest=0;
    std::uint64_t privateCommit=0, mappedCommit=0, imageCommit=0, reserved=0;
    std::uint64_t heapAllocated=0, heapCommitted=0, heapReserved=0, crtAllocated=0, crtCommitted=0;
    unsigned heapsSummarized=0;
    bool heapsComplete=false, crtValid=false;
    unsigned heapCount=0, regions=0;
    bool processValid=false, vaComplete=false;
    double vaMilliseconds=0;
};
inline ProcessMemory CollectProcessMemory() noexcept {
    ProcessMemory result;
    PROCESS_MEMORY_COUNTERS_EX counters={};counters.cb=sizeof(counters);
    result.processValid=GetProcessMemoryInfo(GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters))!=FALSE;
    if(result.processValid){
        result.privateBytes=counters.PrivateUsage;result.workingSet=counters.WorkingSetSize;
        result.peakWorkingSet=counters.PeakWorkingSetSize;result.pagefile=counters.PagefileUsage;
    }
    // Optional OS summaries, fixed stack storage; never HeapWalk/HeapLock.
    HANDLE heaps[64]={};
    result.heapCount=GetProcessHeaps(64,heaps);
    using SummaryFn=BOOL(WINAPI*)(HANDLE,DWORD,LPHEAP_SUMMARY);
    const auto summary=reinterpret_cast<SummaryFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"HeapSummary"));
    if(summary && result.heapCount && result.heapCount<=64) {
        for(unsigned i=0;i<result.heapCount;++i) {
            HEAP_SUMMARY values={};values.cb=sizeof(values);
            if(!summary(heaps[i],0,&values))continue;
            ++result.heapsSummarized;
            result.heapAllocated+=values.cbAllocated;result.heapCommitted+=values.cbCommitted;result.heapReserved+=values.cbReserved;
            if(heaps[i]==reinterpret_cast<HANDLE>(_get_heap_handle())) {
                result.crtValid=true;result.crtAllocated=values.cbAllocated;result.crtCommitted=values.cbCommitted;
            }
        }
        result.heapsComplete=result.heapsSummarized==result.heapCount;
    }
    LARGE_INTEGER begin={},end={},frequency={};
    QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&begin);
    SYSTEM_INFO info={};GetSystemInfo(&info);
    const std::uint64_t limit=static_cast<std::uint64_t>(reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress))+1;
    std::uint64_t address=0;
    while(address<limit){
        MEMORY_BASIC_INFORMATION region={};
        if(!VirtualQuery(reinterpret_cast<const void*>(static_cast<uintptr_t>(address)),&region,sizeof(region)))break;
        const std::uint64_t base=reinterpret_cast<uintptr_t>(region.BaseAddress);
        const std::uint64_t next=base+static_cast<std::uint64_t>(region.RegionSize);
        if(base>address || next<=address)break;
        const std::uint64_t stop=next<limit?next:limit,bytes=stop-address;
        result.vaTotal+=bytes;++result.regions;
        if(region.State==MEM_FREE){result.vaFree+=bytes;if(bytes>result.vaLargest)result.vaLargest=bytes;}
        else if(region.State==MEM_RESERVE)result.reserved+=bytes;
        else if(region.State==MEM_COMMIT) {
            if(region.Type==MEM_PRIVATE)result.privateCommit+=bytes;
            else if(region.Type==MEM_MAPPED)result.mappedCommit+=bytes;
            else if(region.Type==MEM_IMAGE)result.imageCommit+=bytes;
        }
        address=stop;
    }
    result.vaComplete=address==limit;
    QueryPerformanceCounter(&end);
    if(frequency.QuadPart>0)result.vaMilliseconds=1000.0*(end.QuadPart-begin.QuadPart)/frequency.QuadPart;
    return result;
}
inline bool MemoryTimerDue(unsigned long now,unsigned long& previous) noexcept {
    if(static_cast<unsigned long>(now-previous)<300000UL)return false;
    previous=now;return true; // Unsigned DWORD wrap, wall clock independent of game time.
}
}
