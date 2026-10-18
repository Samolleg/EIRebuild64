#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "GameJournal.h"

// Permanent, passive main-thread journal counters. All times are inclusive wall
// time (including waits); nested counters must not be added together.
namespace CorePerf {
enum Id {
    IdleFrame, CountTime, OnIdle, DrawFrame, Draw3D, Draw2D, Messages,
    ServerSegment, ServerPackets, ServerNetwork, WorldSegment, VisibleSet,
    Effects, AIMap, Water, Spells, Script, ScriptTemp, ActiveObjects,
    UnitSegment, UnitAI, Vision, FindPath, L1, MoveCollisions, L0, Lua,
    ClientSegment, ClientObjects, ClientEffects, ClientParticles, ClientPrints,
    Count
};
inline const char* Name(Id id) noexcept {
    static const char* const names[] = {
        "frame.idle", "frame.count_time", "frame.on_idle", "frame.draw",
        "frame.draw_3d", "frame.draw_2d", "window.messages",
        "server.segment", "server.packets_active_zones", "server.network",
        "server.world", "server.visible_set", "world.effects", "world.ai_map",
        "world.water", "world.spells", "world.script", "world.script_temp",
        "world.active_objects", "unit.segment", "unit.ai", "unit.vision",
        "unit.find_path", "unit.l1", "unit.move_collisions", "unit.l0", "lua.call",
        "client.segment", "client.objects", "client.effects", "client.particles", "client.prints"
    };
    return names[id];
}
inline LONGLONG Now() noexcept {
    LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart;
}
struct Counter {
    LONGLONG total = 0, maximum = 0;
    std::uint64_t calls = 0;
    void Add(LONGLONG elapsed) noexcept {
        total += elapsed; if (elapsed > maximum) maximum = elapsed; ++calls;
    }
};
struct TickData {
    TickData* parent = nullptr;
    Counter scopes[Count]{};
    std::uint64_t dynamic = 0, active = 0, objects = 0;
    char zone[384]{};
};
struct State {
    const DWORD thread = GetCurrentThreadId();
    LONGLONG frequency = 0, window = Now();
    Counter scopes[Count]{};
    TickData* tick = nullptr;
    TickData worst{};
    bool inFrame = false;
    std::uint64_t frameTicks = 0, histogram[4]{}, hitches = 0;
    std::uint64_t dynamicTotal = 0, activeTotal = 0, dynamicMax = 0, activeMax = 0;
    State() noexcept { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); frequency = f.QuadPart; }
};
inline State& Get() noexcept { static State value; return value; }
inline void Initialize() noexcept { (void)Get(); }
inline State* Main() noexcept {
    State& s = Get(); return GetCurrentThreadId() == s.thread && s.frequency > 0 ? &s : nullptr;
}
inline void Record(State& s, Id id, LONGLONG elapsed) noexcept {
    s.scopes[id].Add(elapsed);
    for (TickData* tick = s.tick; tick; tick = tick->parent) tick->scopes[id].Add(elapsed);
}
class Scope {
    State* state = Main();
    Id id;
    LONGLONG started;
public:
    explicit Scope(Id value) noexcept : id(value), started(state ? Now() : 0) {}
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    void Finish() noexcept {
        if (state) { Record(*state, id, Now() - started); state = nullptr; }
    }
    ~Scope() noexcept { Finish(); }
};
inline void Objects(std::uint64_t dynamic, std::uint64_t active) noexcept {
    if (State* s = Main()) {
        s->dynamicTotal += dynamic; s->activeTotal += active;
        if (dynamic > s->dynamicMax) s->dynamicMax = dynamic;
        if (active > s->activeMax) s->activeMax = active;
        if (s->tick) { s->tick->dynamic = dynamic; s->tick->active = active; }
    }
}
inline void ObjectVisited() noexcept {
    if (State* s = Main())
        for (TickData* tick = s->tick; tick; tick = tick->parent) ++tick->objects;
}
class ServerTick {
    State* state = Main();
    LONGLONG started;
    TickData tick{};
public:
    explicit ServerTick(const char* zone, std::uint64_t dynamic, std::uint64_t active) noexcept
        : started(state ? Now() : 0) {
        if (!state) return;
        strncpy_s(tick.zone, zone ? zone : "unknown", _TRUNCATE);
        tick.dynamic = dynamic; tick.active = active;
        tick.parent = state->tick; state->tick = &tick;
        if (state->inFrame) ++state->frameTicks;
    }
    ServerTick(const ServerTick&) = delete;
    ~ServerTick() noexcept {
        if (!state) return;
        const LONGLONG elapsed = Now() - started;
        state->scopes[ServerSegment].Add(elapsed);
        tick.scopes[ServerSegment].Add(elapsed);
        state->tick = tick.parent;
        if (elapsed * 1000.0 / state->frequency > 25.0) {
            ++state->hitches;
            if (elapsed > state->worst.scopes[ServerSegment].total) {
                state->worst = tick;
                state->worst.parent = nullptr; // Retain scalars only, never stack/game pointers.
            }
        }
    }
};
inline void Flush() noexcept {
    State* p = Main(); if (!p) return;
    State& s = *p;
    const LONGLONG now = Now();
    if (s.tick || s.inFrame || now - s.window < s.frequency * 5) return;
    const double ms = 1000.0 / static_cast<double>(s.frequency);
    const double window = (now - s.window) * ms;
    for (int i = 0; i < Count; ++i) {
        const Counter& c = s.scopes[i];
        char extra[320]{};
        if (i == IdleFrame)
            sprintf_s(extra, " frames_ticks_0=%llu frames_ticks_1=%llu frames_ticks_2=%llu frames_ticks_3plus=%llu",
                s.histogram[0], s.histogram[1], s.histogram[2], s.histogram[3]);
        if (i == ActiveObjects)
            sprintf_s(extra, " dynamicObjs_mean=%.2f dynamicObjs_max=%llu activeSet_mean=%.2f activeSet_max=%llu",
                c.calls ? double(s.dynamicTotal) / c.calls : 0.0, s.dynamicMax,
                c.calls ? double(s.activeTotal) / c.calls : 0.0, s.activeMax);
        GameJournal::Event("CORE", "core_perf",
            "scope=%s window_ms=%.3f calls=%llu total_ms=%.3f mean_ms=%.4f max_ms=%.3f inclusive=1%s",
            Name(static_cast<Id>(i)), window, c.calls, c.total * ms,
            c.calls ? c.total * ms / c.calls : 0.0, c.maximum * ms, extra);
    }
    if (s.hitches) {
        char parts[4096]{}; size_t used = 0;
        for (int i = ServerPackets; i < Count; ++i) {
            const Counter& c = s.worst.scopes[i];
            if (!c.calls) continue;
            const int n = _snprintf_s(parts + used, sizeof(parts) - used, _TRUNCATE,
                " %s_ms=%.3f %s_calls=%llu", Name(static_cast<Id>(i)), c.total * ms,
                Name(static_cast<Id>(i)), c.calls);
            if (n < 0) break;
            used += n;
        }
        GameJournal::Event("CORE", "core_hitch",
            "scope=server.segment window_ms=%.3f tick_ms=%.3f threshold_ms=25 inclusive=1 selection=worst"
            " skipped=%llu dynamicObjs=%llu activeSet=%llu objects_segmented=%llu units_segmented=%llu"
            " path_calls=%llu lua_calls=%llu zone=%s%s",
            window, s.worst.scopes[ServerSegment].total * ms, s.hitches - 1,
            s.worst.dynamic, s.worst.active, s.worst.objects, s.worst.scopes[UnitSegment].calls,
            s.worst.scopes[FindPath].calls, s.worst.scopes[Lua].calls,
            GameJournal::Quote(s.worst.zone).c_str(), parts);
    }
    for (auto& c : s.scopes) c = Counter{};
    for (auto& n : s.histogram) n = 0;
    s.dynamicTotal = s.activeTotal = s.dynamicMax = s.activeMax = s.hitches = 0;
    s.worst = TickData{}; s.window = Now();
}
class Frame {
    State* state = Main();
    Scope timing{IdleFrame};
public:
    Frame() noexcept { if (state) { state->frameTicks = 0; state->inFrame = true; } }
    Frame(const Frame&) = delete;
    ~Frame() noexcept {
        timing.Finish();
        if (state) {
            ++state->histogram[state->frameTicks < 3 ? state->frameTicks : 3];
            state->inFrame = false;
            Flush(); // Journal formatting/queueing is outside all frame/tick timers.
        }
    }
};

// One completion record per lifecycle operation, including failed/exceptional
// exits. Named phases partition that operation; nested operations are inclusive.
class Operation {
    State* state = Main();
    const char* scope;
    char target[384]{};
    const char* phase = "prepare";
    LONGLONG started, checkpoint;
    char parts[1536]{};
    size_t used = 0;
    bool success = false;
    void EndPhase(LONGLONG now) noexcept {
        const int n = _snprintf_s(parts + used, sizeof(parts) - used, _TRUNCATE,
            " %s_ms=%.3f", phase, (now - checkpoint) * 1000.0 / state->frequency);
        if (n >= 0) used += n;
        checkpoint = now;
    }
public:
    Operation(const char* name, const char* detail) noexcept
        : scope(name), started(state ? Now() : 0), checkpoint(started) {
        Target(detail);
    }
    Operation(const Operation&) = delete;
    void Target(const char* detail) noexcept { strncpy_s(target, detail ? detail : "unknown", _TRUNCATE); }
    void Stage(const char* name) noexcept { if (state) { EndPhase(Now()); phase = name; } }
    void Result(bool value) noexcept { success = value; }
    ~Operation() noexcept {
        if (!state) return;
        const LONGLONG now = Now(); EndPhase(now);
        GameJournal::Event("CORE", "core_operation",
            "scope=%s target=%s total_ms=%.3f success=%d inclusive=1%s",
            scope, GameJournal::Quote(target).c_str(), (now - started) * 1000.0 / state->frequency,
            int(success), parts);
    }
};
}
