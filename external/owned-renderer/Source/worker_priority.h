#pragma once
// Background workers (decode, shader object creation, asset and bake jobs)
// run below normal priority for their whole body. The game thread, the
// render thread and every other process's input and composition threads then
// always win a contended core: DirectInput's system-mouse hook thread in this
// process (the game acquires the mouse non-exclusive in the background, so
// every mouse event on the desktop waits for that thread), DWM and the
// foreground application. With idle cores the workers run exactly as fast.
// The previous priority comes back when the scope ends: std::async runs on
// the CRT's reused pool threads.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace owned {

class BackgroundWorkerPriority {
public:
    BackgroundWorkerPriority() noexcept : previous_(GetThreadPriority(GetCurrentThread())) {
        if (previous_ != THREAD_PRIORITY_ERROR_RETURN && previous_ > THREAD_PRIORITY_BELOW_NORMAL)
            changed_ = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL) != FALSE;
    }
    ~BackgroundWorkerPriority() {
        if (changed_) SetThreadPriority(GetCurrentThread(), previous_);
    }
    BackgroundWorkerPriority(const BackgroundWorkerPriority&) = delete;
    BackgroundWorkerPriority& operator=(const BackgroundWorkerPriority&) = delete;

private:
    int previous_;
    bool changed_ = false;
};

} // namespace owned
