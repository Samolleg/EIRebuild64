#pragma once
// GPU stall watchdog (proxy.cpp stall_watchdog): the main thread (and, with
// the render_thread option, the render thread) marks the stretches where it
// is inside blocking D3D11/DXGI work. A background thread that sees one
// bracket last more than 5 s records it once per session (bridge.jsonl
// gpu_stall plus a minidump). It never recovers or terminates.
// Cost per outermost bracket: one QPC read and a few relaxed/release stores
// (a nested bracket only swaps the name).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>

namespace owned::stall_watch {

// One slot per bracketing thread: 0 the game thread, 1 the render thread.
// Each slot is written by its own thread only.
struct Slot {
    // QPC tick when the outermost bracket began (0 = outside), and the
    // innermost bracket's name.
    std::atomic<std::int64_t> busy_since{0};
    std::atomic<const char*> busy_name{nullptr};
    std::atomic<DWORD> busy_thread{0};
};
inline constexpr int kSlots = 2;
inline Slot slots[kSlots];
// This thread's slot (the render thread selects 1 when it starts).
inline thread_local int slot_index = 0;
// Bracket nesting on this thread: only threads inside a bracket touch the state.
inline thread_local int depth = 0;

inline Slot& own_slot() noexcept { return slots[slot_index]; }

// The latest memory_status residency summary (renderer_vram_budget.cpp, every
// 30 s), appended to the stall record. Two buffers: the writer fills the one
// not published, then publishes its index; 30 s apart, a reader never sees a
// buffer being rewritten.
inline char residency[2][320]{};
inline std::atomic<int> residency_index{-1};

inline std::int64_t now() noexcept {
    LARGE_INTEGER value;
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

// Render work that should never take seconds.
class Bracket {
public:
    explicit Bracket(const char* name) noexcept
        : slot_(own_slot()), previous_(slot_.busy_name.load(std::memory_order_relaxed)) {
        slot_.busy_name.store(name, std::memory_order_relaxed);
        if (depth++ == 0) {
            slot_.busy_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
            slot_.busy_since.store(now(), std::memory_order_release);
        }
    }
    ~Bracket() {
        if (--depth == 0) slot_.busy_since.store(0, std::memory_order_release);
        slot_.busy_name.store(previous_, std::memory_order_relaxed);
    }
    Bracket(const Bracket&) = delete;
    Bracket& operator=(const Bracket&) = delete;

private:
    Slot& slot_;
    const char* previous_;
};

// Legitimately long work inside a bracket (an FXC compile): the clock stops
// here and restarts when it ends. No effect on threads outside a bracket.
class Exempt {
public:
    Exempt() noexcept : active_(depth > 0) {
        if (active_) own_slot().busy_since.store(0, std::memory_order_release);
    }
    ~Exempt() {
        if (active_) own_slot().busy_since.store(now(), std::memory_order_release);
    }
    Exempt(const Exempt&) = delete;
    Exempt& operator=(const Exempt&) = delete;

private:
    bool active_;
};

}  // namespace owned::stall_watch
