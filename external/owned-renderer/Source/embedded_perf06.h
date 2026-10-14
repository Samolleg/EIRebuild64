#pragma once
#ifdef EID3D11_EMBEDDED
#include <windows.h>
#include <cstdio>
namespace owned {
// Temporary PERF06 bridge, supplied by the embedded host, never retail hooks.
void embedded_perf06_report(const char* detail) noexcept;
struct Perf06Counter { const char* name; LONGLONG start=0,total=0,peak=0; unsigned long long calls=0; };
struct Perf06Scope {
    Perf06Counter& c; LARGE_INTEGER begin{};
    explicit Perf06Scope(Perf06Counter& value):c(value){ QueryPerformanceCounter(&begin);if(!c.start)c.start=begin.QuadPart; }
    ~Perf06Scope(){
        LARGE_INTEGER end{};QueryPerformanceCounter(&end);
        static const LONGLONG hz=[](){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return f.QuadPart;}();
        if(hz<=0)return;
        const auto dt=end.QuadPart-begin.QuadPart;c.total+=dt;if(dt>c.peak)c.peak=dt;++c.calls;
        if(end.QuadPart-c.start<hz*2)return;
        const double ms=1000.0/double(hz);char out[384]{};
        std::snprintf(out,sizeof(out),"scope=%s window_ms=%.3f calls=%llu total_ms=%.3f mean_ms=%.4f max_ms=%.3f inclusive=1",c.name,(end.QuadPart-c.start)*ms,c.calls,c.total*ms,c.total*ms/c.calls,c.peak*ms);
        c.start=end.QuadPart;c.total=c.peak=0;c.calls=0;embedded_perf06_report(out);
    }
};
}
#define PERF06_SCOPE(label) static owned::Perf06Counter perf06counter{label}; owned::Perf06Scope perf06scope(perf06counter)
#else
#define PERF06_SCOPE(label) ((void)0)
#endif
