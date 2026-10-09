#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdlib>

// The native /MT addon's CRT is separate from the engine CRT. Its bound UCRT
// owns rand state in FLS, initially 1, without a game/world seed or reset.
namespace ModNativeCrtRandom {
namespace Detail {
inline void NTAPI Release(void* state) { HeapFree(GetProcessHeap(),0,state); }
inline DWORD Slot()
{
    struct Index {
        DWORD value;
        Index() : value(FlsAlloc(Release)) { if(value==FLS_OUT_OF_INDEXES)std::abort(); }
        // Process lifetime: static world teardown must still be able to draw.
        // FLS releases each fiber/thread state; the OS reclaims the index.
    };
    static Index owner;
    return owner.value;
}
inline std::uint32_t& State()
{
    const DWORD savedError=GetLastError();
    const DWORD slot=Slot();
    auto* state=static_cast<std::uint32_t*>(FlsGetValue(slot));
    if(!state){
        state=static_cast<std::uint32_t*>(HeapAlloc(GetProcessHeap(),0,sizeof(*state)));
        if(!state)std::abort();
        *state=1u;
        if(!FlsSetValue(slot,state)){Release(state);std::abort();}
    }
    SetLastError(savedError);
    return *state;
}
}
inline int Random(int maximum)
{
    auto& state=Detail::State();
    state=state*214013u+2531011u;
    const int raw=int((state>>16)&32767u);
    return (raw*maximum/32767)%maximum;
}
inline int Dice(int maximum) { return 1+Random(maximum); }
}
