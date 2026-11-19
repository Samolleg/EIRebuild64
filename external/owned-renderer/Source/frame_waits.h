#pragma once
#include "runtime_diagnostics.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

// Each thread owns a wall-time partition, including nested renderer calls and
// explicit waits. Main idle iterations and scene-command frames report separately.
namespace owned::frame_waits {
enum Category { logic, record, limiter, swapchain, fence, pacer, scene_sync, present_slot,
                lock_frame, idle, present, reflex, count };
inline LONGLONG now() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}
inline double milliseconds(LONGLONG ticks) noexcept {
    static const LONGLONG frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value.QuadPart;
    }();
    return ticks * 1000.0 / frequency;
}
// Native output is bounded to one in flight. The next logic step can start
// before the prior frame has finished encoding/submitting its command list.
struct PipelineTiming {
    std::atomic<unsigned long long> frame{0};
    std::atomic<LONGLONG> start{0},submitted{0},finish{0},layersDone{0};
    std::atomic<bool> drawingLayers{false};
    unsigned long long frames=0,overlapped=0,encoding=0,drawing=0,layerPackets=0;
    double overlapMs=0,encodingMs=0,drawMs=0;
};
// QPC intervals use a common clock across both threads. World and staging
// are paired before publishing another opaque pass; never count FIFO wait as draw.
struct WorldPipelineTiming {
    std::atomic<LONGLONG> begin{0}, end{0};
    LONGLONG stageBegin=0, stageEnd=0;
    unsigned long long samples=0, overlapped=0, reused=0, sectorRows=0;
    double overlapMs=0;
    void collect() noexcept {
        if (!stageBegin) return;
        const auto first=begin.load(), last=end.load();
        if (first && last) {
            ++samples;
            const auto ticks=(std::min)(last,stageEnd)-(std::max)(first,stageBegin);
            if (ticks>0) { ++overlapped; overlapMs+=milliseconds(ticks); }
        }
        stageBegin=stageEnd=0;
    }
};
inline WorldPipelineTiming worldPipeline;
inline PipelineTiming pipeline;
inline void pipeline_post() noexcept {
    if(!RuntimeDiagnostics::Enabled())return;
    pipeline.layersDone=0; pipeline.drawingLayers=false; pipeline.start=0; pipeline.submitted=0; pipeline.finish=0; ++pipeline.frame;
}
class PreparationInterval {
    const bool enabled=RuntimeDiagnostics::Enabled();
    unsigned long long frame=enabled?pipeline.frame.load():0;
    LONGLONG begin=frame?now():0;
public:
    ~PreparationInterval() {
        if(!frame || pipeline.frame!=frame)return;
        const auto end=now(), start=pipeline.start.load();
        ++pipeline.frames;
        if(!start)return;
        const auto intersection=[&](LONGLONG stop) {
            return (std::max)(0LL,(std::min)(end,stop?stop:end)-(std::max)(start,begin));
        };
        const auto elapsed=intersection(pipeline.finish.load());
        const auto encoding=intersection(pipeline.submitted.load());
        if(elapsed){++pipeline.overlapped;pipeline.overlapMs+=milliseconds(elapsed);}
        if(pipeline.drawingLayers){
            const auto layers=intersection(pipeline.layersDone.load());
            if(layers){++pipeline.drawing;pipeline.drawMs+=milliseconds(layers);}
        }
        if(encoding){++pipeline.encoding;pipeline.encodingMs+=milliseconds(encoding);}
    }
};

struct Frame {
    bool active = false, presented = false;
    Category category = logic;
    LONGLONG checkpoint = 0;
    std::array<LONGLONG, count> ticks{};
};
inline thread_local Frame frame;
inline void checkpoint(LONGLONG time) noexcept {
    frame.ticks[frame.category] += time - frame.checkpoint;
    frame.checkpoint = time;
}
class Scope {
    bool active = frame.active;
    Category previous = frame.category;
public:
    explicit Scope(Category category) noexcept {
        if (active) { checkpoint(now()); frame.category = category; }
    }
    Scope(const Scope&) = delete;
    ~Scope() noexcept { finish(); }
    void finish() noexcept {
        if (!active) return;
        checkpoint(now()); frame.category = previous; active = false;
    }
};
// Fixed storage, no allocation or sorting inside measured scopes. Overflow is
// reported explicitly. Zeros are retained for waits absent from a given frame.
struct Distribution {
    std::array<double, 4096> values{};
    size_t size = 0, dropped = 0;
    void add(double value) noexcept {
        if (size < values.size()) values[size++] = value;
        else ++dropped;
    }
    void append(std::string& text, const char* name) {
        std::sort(values.begin(), values.begin() + size);
        const double median = !size ? -1.0 : size % 2 ? values[size / 2]
            : (values[size / 2 - 1] + values[size / 2]) * 0.5;
        const double p95 = size ? values[(95 * size + 99) / 100 - 1] : -1.0;
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), " %s_ms_median=%.3f %s_ms_p95=%.3f %s_n=%zu %s_dropped=%zu",
            name, median, name, p95, name, size, name, dropped);
        text += buffer;
        size = dropped = 0;
    }
};
inline thread_local std::array<Distribution, count> cpu;
inline thread_local Distribution cpu_frame;
inline std::mutex render_mutex;
inline std::array<Distribution, count> render;
inline bool begin() noexcept {
    if (!RuntimeDiagnostics::Enabled() || frame.active) return false;
    frame = {};
    frame.active = true;
    frame.checkpoint = now();
    return true;
}
inline void end() noexcept {
    checkpoint(now());
    frame.active = false;
    if (!frame.presented) return;
    LONGLONG total = 0;
    for (size_t i = 0; i < count; ++i) {
        cpu[i].add(milliseconds(frame.ticks[i]));
        total += frame.ticks[i];
    }
    cpu_frame.add(milliseconds(total));
}
// Scene commands accumulate from first command through Present. Queue starvation
// is measured as idle and excluded from command recording. Submit once per frame.
inline void end_render() noexcept {
    if (!frame.active) return;
    checkpoint(now());
    frame.active = false;
    std::lock_guard<std::mutex> lock(render_mutex);
    for (size_t i = 0; i < count; ++i) render[i].add(milliseconds(frame.ticks[i]));
}
inline std::string take_cpu_report() {
    static const char* names[count] = {"main_logic_prepare", "main_render_api_record",
        "main_limiter", "main_swapchain_latency", "main_gpu_fence", "main_fg_backpressure",
        "main_scene_sync", "main_present_slot", "main_lock_frame", "main_idle", "main_present", "main_reflex"};
    std::string text = "cpu_scope=present_submitted_idle_iterations cpu_partition=exclusive cpu_thread=main cpu_window=completed_since_report";
    cpu_frame.append(text, "main_frame");
    for (size_t i = 0; i < count; ++i) cpu[i].append(text, names[i]);
    static const char* render_names[count] = {"scene_other", "scene_record", "scene_limiter",
        "scene_swapchain_latency", "scene_gpu_fence", "scene_fg_backpressure", "scene_sync",
        "scene_present_slot", "scene_lock_frame", "scene_input_wait", "scene_present", "scene_reflex"};
    std::lock_guard<std::mutex> lock(render_mutex);
    for (size_t i = 0; i < count; ++i) render[i].append(text, render_names[i]);
    return text;
}
} // namespace owned::frame_waits
