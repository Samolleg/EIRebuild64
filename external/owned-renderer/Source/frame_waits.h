#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
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
    if (frame.active) return false;
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
