#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
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
    InterfaceSetup, InterfaceScreen, InterfaceEffects, ScreenMission, ScreenMenu,
    ScreenBase, ScreenCamp, ScreenChoosePers, ScreenDebug, ScreenEndGame, ScreenExchange, ScreenGlobalMap, ScreenOptions, ScreenSaveLoad, ScreenTutorial,
    WorldDraw, WorldSetup, WorldTerrainInputs, WorldBegin, WorldTerrainBase,
    WorldPrepare, WorldSceneInputs, WorldVisualInputs, WorldRender, WorldOverlays,
    WorldWater, WorldEffects, WorldFinish, Visibility, Placement, PoseWalk,
    VisualResources, Skeleton, VisualPose, PosePublish, Picking, FootprintInputs, VisualFrameRun,
    SceneObjectWalk, SceneSubmit, RenderTerrain, EffectsFog, EffectsParticles, EffectsLightning, EffectsWeather,
    LockFrameWait, SceneSyncWait, PresentSlotWait, LimiterWait, IdleWait,
    NavigationTotal, NavigationPath, NavigationPlace, NavigationStep, NavigationRestore, NavigationFrame,
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
        "client.segment", "client.objects", "client.effects", "client.particles", "client.prints",
        "draw3d.interface_setup", "draw3d.screen", "draw3d.interface_effects", "screen.mission", "screen.menu",
        "screen.base", "screen.camp", "screen.choosepers", "screen.debug", "screen.endgame", "screen.exchange", "screen.globalmap", "screen.options", "screen.saveload", "screen.tutorial",
        "draw3d.world", "world_draw.setup", "world_draw.terrain_inputs", "world_draw.begin",
        "world_draw.terrain_base", "world_draw.prepare", "world_draw.scene_inputs",
        "world_draw.visual_inputs", "world_draw.render", "world_draw.overlays", "world_draw.water",
        "world_draw.effects", "world_draw.finish", "prepare.visibility", "visibility.placement",
        "prepare.pose_walk", "visual.resources", "visual.skeleton", "visual.pose", "visual.pose_publish",
        "visual.picking", "prepare.footprints", "render.visual_frame",
        "scene_inputs.objects", "scene_inputs.submit", "render.terrain", "effects.fog", "effects.particles", "effects.lightning", "effects.weather",
        "wait.lock_frame", "wait.scene_sync", "wait.present_slot", "wait.limiter", "wait.idle",
        "nav.total", "nav.path", "nav.place", "nav.step", "nav.restore", "nav.frame"
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
struct VisualSample {
    VisualSample* parent = nullptr;
    std::uint64_t values[3]{}; // visible objects, visible figures, published poses
};
struct AsyncCounter {
    std::mutex mutex;
    Counter value;
    void Add(LONGLONG elapsed) noexcept {
        std::lock_guard<std::mutex> lock(mutex); value.Add(elapsed);
    }
    Counter Take() noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        const Counter result = value; value = Counter{}; return result;
    }
};
// One passive main-thread frame timeline shared by core, host and embedded
// renderer scopes. Exclusive CPU wall time includes waits, never GPU duration.
struct FrameDetail { const char* name = nullptr; LONGLONG ticks = 0; };
struct FrameDetailStack { unsigned slot = 0; LONGLONG checkpoint = 0; };
struct State {
    const DWORD thread = GetCurrentThreadId();
    LONGLONG frequency = 0, window = Now();
    Counter scopes[Count]{};
    AsyncCounter async[Count]{};
    VisualSample* visual = nullptr;
    std::uint64_t visualFrames = 0, visualTotals[3]{}, visualMax[3]{};
    std::uint64_t emptyFrames = 0, skips[5]{};
    bool drew3D = false;
    TickData* tick = nullptr;
    TickData worst{};
    FrameDetail frameDetails[192]{}, worstDetails[3]{};
    FrameDetailStack detailStack[64]{};
    unsigned detailCount = 0, detailDepth = 0;
    bool detailOverflow = false, worstOverflow = false;
    LONGLONG frameStarted = 0, frameDraw = 0, hitchWindow = Now(), worstFrame = 0, worstDraw = 0;
    std::uint64_t frameNumber = 0, worstFrameNumber = 0, slowFrames = 0;
    LONGLONG navigationFrame = 0;
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
// Tokens are stack depths, valid only until their matching synchronous end.
// Worker threads are ignored; fixed bounds avoid allocating in render paths.
inline unsigned BeginFrameDetail(const char* name, LONGLONG now) noexcept {
    auto* s = Main();
    if (!s || !s->inFrame) return 0;
    if (s->detailDepth == 64) { s->detailOverflow = true; return 0; }
    unsigned slot = 0;
    while (slot < s->detailCount && std::strcmp(s->frameDetails[slot].name, name)) ++slot;
    if (slot == s->detailCount) {
        if (slot == 192) { s->detailOverflow = true; return 0; }
        s->frameDetails[slot].name = name; ++s->detailCount;
    }
    if (s->detailDepth) {
        auto& parent = s->detailStack[s->detailDepth - 1];
        s->frameDetails[parent.slot].ticks += now - parent.checkpoint;
    }
    s->detailStack[s->detailDepth++] = {slot, now};
    return s->detailDepth;
}
inline void EndFrameDetail(unsigned token, LONGLONG now) noexcept {
    if (!token) return;
    auto* s = Main();
    if (!s || !s->inFrame) return;
    if (token != s->detailDepth) { s->detailOverflow = true; return; }
    const auto& current = s->detailStack[--s->detailDepth];
    s->frameDetails[current.slot].ticks += now - current.checkpoint;
    if (s->detailDepth) s->detailStack[s->detailDepth - 1].checkpoint = now;
}
inline void CompleteFrameDetails(State& s, LONGLONG now) noexcept {
    const auto elapsed = now - s.frameStarted;
    if (elapsed * 1000.0 / s.frequency > 33.0) {
        ++s.slowFrames;
        if (elapsed > s.worstFrame) {
            s.worstFrame = elapsed; s.worstDraw = s.frameDraw;
            s.worstFrameNumber = s.frameNumber; s.worstOverflow = s.detailOverflow || s.detailDepth != 0;
            for (auto& detail : s.worstDetails) detail = {};
            for (unsigned i = 0; i < s.detailCount; ++i) {
                auto candidate = s.frameDetails[i];
                for (auto& best : s.worstDetails)
                    if (candidate.ticks > best.ticks) { const auto old = best; best = candidate; candidate = old; }
            }
        }
    }
    // Keep the worst completed frame per second. Formatting is outside timers;
    // the event states the exact frame, not unrelated sliding-window maxima.
    if (now - s.hitchWindow < s.frequency) return;
    if (s.slowFrames) {
        const double ms = 1000.0 / s.frequency;
        const auto& a = s.worstDetails[0]; const auto& b = s.worstDetails[1]; const auto& c = s.worstDetails[2];
        GameJournal::Event("CORE", "frame_spike",
            "frame=%llu frame_ms=%.3f draw_ms=%.3f threshold_ms=33 selection=worst_per_second slow_frames=%llu"
            " top1=%s top1_ms=%.3f top2=%s top2_ms=%.3f top3=%s top3_ms=%.3f self=1 thread=main overflow=%d",
            s.worstFrameNumber, s.worstFrame * ms, s.worstDraw * ms, s.slowFrames,
            a.name ? a.name : "none", a.ticks * ms, b.name ? b.name : "none", b.ticks * ms,
            c.name ? c.name : "none", c.ticks * ms, int(s.worstOverflow));
    }
    s.slowFrames = 0; s.worstFrame = 0; s.hitchWindow = now;
}
inline void Record(State& s, Id id, LONGLONG elapsed) noexcept {
    if (id == DrawFrame && s.inFrame) s.frameDraw += elapsed;
    s.scopes[id].Add(elapsed);
    if(id==NavigationTotal&&s.inFrame)s.navigationFrame+=elapsed;
    for (TickData* tick = s.tick; tick; tick = tick->parent) tick->scopes[id].Add(elapsed);
}
class Scope {
    State* state = Main();
    Id id;
    LONGLONG started;
    unsigned detail;
public:
    explicit Scope(Id value) noexcept : id(value), started(state ? Now() : 0),
        detail(BeginFrameDetail(Name(value), started)) {}
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    void Finish() noexcept {
        if (state) { const auto now = Now(); EndFrameDetail(detail, now); Record(*state, id, now - started); state = nullptr; }
    }
    ~Scope() noexcept { Finish(); }
};
// Sequential phases partition their parent, without moving host operations.
class Partition {
    State* state = Main();
    Id id;
    LONGLONG checkpoint;
    unsigned detail;
public:
    explicit Partition(Id value) noexcept : id(value), checkpoint(state ? Now() : 0),
        detail(BeginFrameDetail(Name(value), checkpoint)) {}
    void Stage(Id value) noexcept {
        if (state) { const auto now = Now(); EndFrameDetail(detail, now); Record(*state, id, now - checkpoint); checkpoint = now; detail = BeginFrameDetail(Name(value), now); }
        id = value;
    }
    ~Partition() noexcept { if (state) { const auto now = Now(); EndFrameDetail(detail, now); Record(*state, id, now - checkpoint); } }
};
// Renderer waits may also run on its scene thread. Keep that timeline separate
// from the main thread's inclusive scopes; never add it to frame.draw_3d.
class AnyThreadScope {
    State& state = Get();
    State* main = Main();
    Id id;
    LONGLONG started = Now();
    unsigned detail;
public:
    explicit AnyThreadScope(Id value) noexcept : id(value), detail(BeginFrameDetail(Name(value), started)) {}
    ~AnyThreadScope() noexcept {
        const auto now = Now();
        EndFrameDetail(detail, now);
        const auto elapsed = now - started;
        if (main) Record(state, id, elapsed); else state.async[id].Add(elapsed);
    }
};
class VisualFrame {
    State* state = Main();
    VisualSample sample{};
public:
    VisualFrame() noexcept {
        if (state) { sample.parent = state->visual; state->visual = &sample; state->drew3D = true; }
    }
    ~VisualFrame() noexcept {
        if (!state) return;
        state->visual = sample.parent;
        ++state->visualFrames;
        for (int i = 0; i < 3; ++i) {
            state->visualTotals[i] += sample.values[i];
            if (sample.values[i] > state->visualMax[i]) state->visualMax[i] = sample.values[i];
        }
    }
};
inline void VisualCount(unsigned index, std::uint64_t count = 1) noexcept {
    if (State* s = Main()) if (s->visual) s->visual->values[index] += count;
}
// background pause, disabled render, no render requested, no screen, inactive renderer
inline void DrawSkipped(unsigned reason) noexcept { if (State* s = Main()) ++s->skips[reason]; }
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
        char extra[640]{};
        if (i == IdleFrame)
            sprintf_s(extra, " frames_ticks_0=%llu frames_ticks_1=%llu frames_ticks_2=%llu frames_ticks_3plus=%llu empty_iterations=%llu skip_background=%llu skip_disabled=%llu skip_not_needed=%llu skip_no_screen=%llu skip_inactive=%llu",
                s.histogram[0], s.histogram[1], s.histogram[2], s.histogram[3], s.emptyFrames,
                s.skips[0], s.skips[1], s.skips[2], s.skips[3], s.skips[4]);
        if (i == Draw3D)
            sprintf_s(extra, " samples=%llu visible_objects_mean=%.2f visible_objects_max=%llu visible_figures_mean=%.2f visible_figures_max=%llu published_poses_mean=%.2f published_poses_max=%llu",
                s.visualFrames, s.visualFrames ? double(s.visualTotals[0]) / s.visualFrames : 0.0, s.visualMax[0],
                s.visualFrames ? double(s.visualTotals[1]) / s.visualFrames : 0.0, s.visualMax[1],
                s.visualFrames ? double(s.visualTotals[2]) / s.visualFrames : 0.0, s.visualMax[2]);
        if (i == ActiveObjects)
            sprintf_s(extra, " dynamicObjs_mean=%.2f dynamicObjs_max=%llu activeSet_mean=%.2f activeSet_max=%llu",
                c.calls ? double(s.dynamicTotal) / c.calls : 0.0, s.dynamicMax,
                c.calls ? double(s.activeTotal) / c.calls : 0.0, s.activeMax);
        GameJournal::Event("CORE", "core_perf",
            "scope=%s window_ms=%.3f calls=%llu total_ms=%.3f mean_ms=%.4f max_ms=%.3f inclusive=1 thread=main%s",
            Name(static_cast<Id>(i)), window, c.calls, c.total * ms,
            c.calls ? c.total * ms / c.calls : 0.0, c.maximum * ms, extra);
    }
    for (int i = 0; i < Count; ++i) {
        const auto c = s.async[i].Take();
        const auto calls = c.calls;
        const auto total = c.total;
        const auto maximum = c.maximum;
        if (calls || total)
            GameJournal::Event("CORE", "core_perf",
                "scope=%s window_ms=%.3f calls=%llu total_ms=%.3f mean_ms=%.4f max_ms=%.3f inclusive=1 thread=scene",
                Name(static_cast<Id>(i)), window, calls, total * ms,
                calls ? total * ms / calls : 0.0, maximum * ms);
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
    s.visualFrames = s.emptyFrames = 0;
    for (auto& v : s.visualTotals) v = 0;
    for (auto& v : s.visualMax) v = 0;
    for (auto& v : s.skips) v = 0;
    s.worst = TickData{}; s.window = Now();
}
class Frame {
    State* state = Main();
    Scope timing{IdleFrame};
public:
    Frame() noexcept { if (state) {
        state->frameStarted = Now(); state->frameDraw = 0; ++state->frameNumber;
        state->detailCount = state->detailDepth = 0; state->detailOverflow = false;
        for (auto& detail : state->frameDetails) detail = {};
        state->frameTicks = 0; state->navigationFrame = 0; state->drew3D = false; state->inFrame = true; } }
    Frame(const Frame&) = delete;
    ~Frame() noexcept {
        timing.Finish();
        if (state) {
            state->scopes[NavigationFrame].Add(state->navigationFrame);
            ++state->histogram[state->frameTicks < 3 ? state->frameTicks : 3];
            if (!state->drew3D) ++state->emptyFrames;
            state->inFrame = false;
            CompleteFrameDetails(*state, Now());
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
