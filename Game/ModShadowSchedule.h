#pragma once
#include <windows.h>
#include <cstdio>
namespace ModShadowSchedule {
// e2c4867 shadow_fixes.cpp: forced refresh tiers and per-frame budget.
// Real engine invalidations are handled before this policy, never deferred.
inline unsigned budget=0,frames=0,rebuilt=0,reused=0,culled=0;
inline LONGLONG generationTicks=0;
inline bool ForceRefresh(DWORD now,DWORD last,bool visible,bool large) {
    DWORD interval=visible?(large?132u:400u):1000u;
    if(DWORD(now-last)<interval || budget>=32)return false;
    ++budget;return true;
}
inline void BeginFrame(){budget=0;}
inline void EndFrame(){
    ++frames;
    if(frames==300 || frames==1200){
        LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
        FILE *f=fopen("eirebuild-shadow-perf.log","a");
        if(f){fprintf(f,"R6 frames=%u rebuilt=%u reused=%u culled=%u generation_ms_per_frame=%.4f\n",frames,rebuilt,reused,culled,1000.0*generationTicks/frequency.QuadPart/frames);fclose(f);}
    }
}
}
