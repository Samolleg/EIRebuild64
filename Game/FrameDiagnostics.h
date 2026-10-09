#pragma once
#include <windows.h>
#include <cstdio>
#include <cstring>

// Camera trace accepted in R146. Keep the recorder compiled out of regular builds.
#define EI_REBUILD_FRAME_DIAGNOSTICS 0

namespace FrameDiagnostics {
enum Stage { Simulation, Camera, Idle, Draw3D, Draw2D, Present, GetDC, ReleaseDC,
             DiagnosticEvent, StageCount };
#if EI_REBUILD_FRAME_DIAGNOSTICS
struct Sample {
    unsigned long long frame;
    double elapsed, gap, loop, stage[StageCount], shadow, rawDelta, cameraAdvance;
    double x, y;
    unsigned messages, cameraCalls, cameraInput, cameraFlags, presented;
    long presentResult;
};
inline constexpr unsigned Capacity = 8192;
inline Sample samples[Capacity] = {}, current = {};
inline LARGE_INTEGER frequency = {};
inline LONGLONG origin = 0, began = 0, previousBegin = 0, shadowBegin = 0;
inline unsigned next = 0, count = 0, pendingMessages = 0;
inline unsigned long long total = 0;
inline double maxGap = 0, maxLoop = 0;
inline unsigned long long over33 = 0, over50 = 0, over100 = 0;
inline wchar_t path[MAX_PATH] = {};
inline bool active = false;
inline volatile LONG flushed = 0;
inline LONGLONG Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(LONGLONG ticks) { return frequency.QuadPart ? ticks * 1000.0 / frequency.QuadPart : 0; }
inline void Configure(const wchar_t *runtimePath) {
    QueryPerformanceFrequency(&frequency); origin = Now();
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s.frames.csv", runtimePath);
}
inline void Message() { ++pendingMessages; }
inline bool Begin(LONGLONG shadowTicks) {
    if (!frequency.QuadPart || active) return false;
    began = Now(); shadowBegin = shadowTicks;
    current = {}; current.frame = ++total;
    current.elapsed = Ms(began-origin);
    current.gap = previousBegin ? Ms(began-previousBegin) : 0;
    previousBegin = began;
    current.messages = pendingMessages; pendingMessages = 0;
    active = true;
    return true;
}
class Scope {
    Stage stage; LONGLONG began;
public:
    explicit Scope(Stage value) : stage(value), began(active ? Now() : 0) {}
    ~Scope() { if (began && active) current.stage[stage] += Ms(Now()-began); }
};
inline void RawDelta(DWORD ms) { if (active) current.rawDelta += ms; }
inline void CameraStep(double ms, bool input, unsigned flags) {
    if (!active) return;
    current.cameraAdvance += ms; ++current.cameraCalls;
    current.cameraInput |= input ? 1u : 0u; current.cameraFlags = flags;
}
inline void Presented(HRESULT result) { if (active) { ++current.presented; current.presentResult = result; } }
inline void End(LONGLONG shadowTicks, double x, double y) {
    if (!active) return;
    current.loop = Ms(Now()-began); current.shadow = Ms(shadowTicks-shadowBegin);
    current.x = x; current.y = y;
    if (current.gap > maxGap) maxGap = current.gap;
    if (current.loop > maxLoop) maxLoop = current.loop;
    if (current.gap > 33.333) ++over33;
    if (current.gap > 50) ++over50;
    if (current.gap > 100) ++over100;
    samples[next] = current; next = (next+1)%Capacity;
    if (count < Capacity) ++count;
    active = false;
}
inline void Flush() {
    if (!path[0] || InterlockedExchange(&flushed, 1)) return;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    auto write = [file](const char *text, int n) {
        if (n <= 0) return; DWORD written; WriteFile(file, text, static_cast<DWORD>(n), &written, nullptr);
    };
    char line[1024];
    int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
        "# EIRebuild R145 diagnostic; CPU wall-clock scopes may overlap; gap includes input dispatch; 8192-frame tail; file written only at exit/crash\r\n"
        "# frames=%llu,max_gap_ms=%.3f,max_loop_ms=%.3f,gaps_over33=%llu,gaps_over50=%llu,gaps_over100=%llu\r\n",
        total,maxGap,maxLoop,over33,over50,over100); write(line,n);
    const char *header = "frame,elapsed_ms,gap_ms,loop_ms,simulation_ms,camera_cpu_ms,idle_ms,draw3d_ms,draw2d_ms,present_ms,getdc_ms,releasedc_ms,diagnostic_event_ms,shadow_cpu_ms,raw_delta_ms,camera_requested_ms,camera_calls,camera_input,camera_flags,anchor_x,anchor_y,messages,presents,present_hresult\r\n";
    write(header,static_cast<int>(strlen(header)));
    for (unsigned i=0; i<count; ++i) {
        const Sample &s = samples[(next+Capacity-count+i)%Capacity];
        n = _snprintf_s(line,sizeof(line),_TRUNCATE,
            "%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%u,%.6f,%.6f,%u,%u,%ld\r\n",
            s.frame,s.elapsed,s.gap,s.loop,s.stage[Simulation],s.stage[Camera],s.stage[Idle],
            s.stage[Draw3D],s.stage[Draw2D],s.stage[Present],s.stage[GetDC],s.stage[ReleaseDC],
            s.stage[DiagnosticEvent],s.shadow,s.rawDelta,s.cameraAdvance,s.cameraCalls,
            s.cameraInput,s.cameraFlags,s.x,s.y,s.messages,s.presented,s.presentResult);
        write(line,n);
    }
    CloseHandle(file);
}
#else
inline void Configure(const wchar_t *) {}
inline void Message() {}
inline bool Begin(LONGLONG) { return false; }
class Scope { public: explicit Scope(Stage) {} };
inline void RawDelta(DWORD) {}
inline void CameraStep(double, bool, unsigned) {}
inline void Presented(HRESULT) {}
inline void End(LONGLONG, double, double) {}
inline void Flush() {}
#endif
}
