#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace owned {

// Player frame-rate limit (graphics option "frame_limit"; off by default and
// forced off in developer mode). Called just before Present: the next present
// is due one period after the previous one. A high-resolution waitable timer
// sleeps until ~1 ms before that and a short spin lands on it, so pacing adds
// no millisecond-scale jitter. A late frame presents at once and re-anchors.
class FrameLimiter {
public:
    FrameLimiter() = default;
    FrameLimiter(const FrameLimiter&) = delete;
    FrameLimiter& operator=(const FrameLimiter&) = delete;
    ~FrameLimiter() {
        if (timer_) CloseHandle(timer_);
    }

    void wait(int fps) noexcept {
        if (fps <= 0) {
            last_ = 0;
            return;
        }
        if (!frequency_) {
            LARGE_INTEGER frequency{};
            QueryPerformanceFrequency(&frequency);
            frequency_ = frequency.QuadPart;
        }
        const std::int64_t now = counter();
        const std::int64_t deadline = last_ + frequency_ / fps;
        if (!last_ || now >= deadline) {
            last_ = now;
            return;
        }
        sleep_until(deadline);
        last_ = deadline;
    }

private:
    static std::int64_t counter() noexcept {
        LARGE_INTEGER value{};
        QueryPerformanceCounter(&value);
        return value.QuadPart;
    }

    void sleep_until(std::int64_t deadline) noexcept {
        if (!timer_ && !timer_tried_) {
            timer_tried_ = true;
            timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                            TIMER_ALL_ACCESS);
        }
        // Without a high-resolution timer (before Windows 10 1803) the wait
        // falls back to yielding spins; standard timers overshoot by ~15 ms.
        const std::int64_t spin = frequency_ / 1000;
        for (;;) {
            const std::int64_t remaining = deadline - counter();
            if (remaining <= 0) return;
            if (timer_ && remaining > spin) {
                LARGE_INTEGER due{};
                due.QuadPart = -((remaining - spin) * 10000000 / frequency_);
                if (due.QuadPart < 0 && SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
                    WaitForSingleObject(timer_, INFINITE);
                    continue;
                }
            }
            if (remaining > spin) SwitchToThread();
            else YieldProcessor();
        }
    }

    HANDLE timer_ = nullptr;
    bool timer_tried_ = false;
    std::int64_t frequency_ = 0;
    std::int64_t last_ = 0;
};

} // namespace owned
