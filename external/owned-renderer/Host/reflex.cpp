// NVIDIA Reflex and the game-frame latency measurement; see reflex.h.
#include "reflex.h"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <nvapi.h>

#include <algorithm>
#include <cstdio>
#include <atomic>
#include <mutex>
#include <vector>

namespace remote { void host_log(const char* format, ...); }

namespace reflex {
namespace {

std::mutex g_mutex;                       // NVAPI state and the samples
ID3D12Device* g_device = nullptr;         // the device NVAPI low latency is set on
bool g_tried = false, g_ready = false;    // NvAPI_Initialize tried / succeeded
int g_applied = -1;                       // the mode SetSleepMode last applied
std::atomic<int> g_mode{0};
std::atomic<std::uint64_t> g_frame{0};
std::atomic<std::int64_t> g_frame_start{0}, g_frame_taken{0};
// Latency samples (ms) and the 5 s window.
std::vector<double> g_samples;
std::int64_t g_window_start = 0, g_frequency = 0;
double g_delay_sum = 0, g_ring_sum = 0, g_queue_sum = 0, g_interval = 0, g_unpaced = 0;
unsigned g_pacing_count = 0;
bool g_pacing_on = false;
std::atomic<std::uint64_t> g_render_end_frame{0};  // the frame whose RENDERSUBMIT_END was sent

NV_LATENCY_MARKER_TYPE nv_type(Marker type) noexcept {
    switch (type) {
    case Marker::simulation_start: return SIMULATION_START;
    case Marker::simulation_end: return SIMULATION_END;
    case Marker::render_start: return RENDERSUBMIT_START;
    case Marker::render_end: return RENDERSUBMIT_END;
    case Marker::present_start: return PRESENT_START;
    case Marker::present_end: return PRESENT_END;
    }
    return SIMULATION_START;
}

// NVAPI's report: mean simulation start -> present end and -> GPU render end
// over the valid frames of its last 64 (us timestamps). Caller holds g_mutex.
void nvapi_report(char* text, std::size_t bytes) noexcept {
    text[0] = 0;
    if (!g_ready || !g_device || g_mode.load() == 0) return;
    static NV_LATENCY_RESULT_PARAMS params;  // 64 frame reports: too large for the stack line
    params = {};
    params.version = NV_LATENCY_RESULT_PARAMS_VER;
    if (NvAPI_D3D_GetLatency(g_device, &params) != NVAPI_OK) { std::snprintf(text, bytes, "; NVAPI latency report failed"); return; }
    double present_sum = 0, gpu_sum = 0;
    unsigned count = 0;
    for (const auto& r : params.frameReport) {
        if (!r.frameID || !r.simStartTime || r.presentEndTime < r.simStartTime || r.gpuRenderEndTime < r.simStartTime) continue;
        present_sum += double(r.presentEndTime - r.simStartTime) / 1000.0;
        gpu_sum += double(r.gpuRenderEndTime - r.simStartTime) / 1000.0;
        ++count;
    }
    if (count)
        std::snprintf(text, bytes, "; NVAPI (host-side markers, %u frames): simulation start -> present end %.2f ms, -> GPU render end %.2f ms",
                      count, present_sum / count, gpu_sum / count);
    else
        std::snprintf(text, bytes, "; NVAPI latency report: no valid frames yet");
}

} // namespace

void configure(ID3D12Device* device, int mode) noexcept {
    mode = mode < 0 || mode > 2 ? 0 : mode;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (mode && !g_tried) {
        g_tried = true;
        const NvAPI_Status status = NvAPI_Initialize();
        g_ready = status == NVAPI_OK;
        remote::host_log(g_ready ? "reflex: NVAPI initialised" : "reflex: NVAPI not available (%d); Reflex stays off", int(status));
    }
    if (!g_ready || !device) { g_mode.store(0); return; }
    if (device != g_device) { g_device = device; g_applied = -1; }
    if (mode != g_applied) {
        NV_SET_SLEEP_MODE_PARAMS params{};
        params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
        params.bLowLatencyMode = mode != 0;
        params.bLowLatencyBoost = false;
        params.minimumIntervalUs = 0;  // never a frame cap
        params.bUseMarkersToOptimize = mode != 0;
        const NvAPI_Status status = NvAPI_D3D_SetSleepMode(device, &params);
        g_applied = mode;
        if (status != NVAPI_OK) {
            remote::host_log("reflex: NvAPI_D3D_SetSleepMode failed (%d); Reflex stays off", int(status));
            g_ready = false;
            g_mode.store(0);
            return;
        }
        NV_GET_SLEEP_STATUS_PARAMS sleep_status{};
        sleep_status.version = NV_GET_SLEEP_STATUS_PARAMS_VER;
        const bool known = NvAPI_D3D_GetSleepStatus(device, &sleep_status) == NVAPI_OK;
        remote::host_log("reflex: mode %d (%s); driver low latency %s", mode,
                         mode == 0 ? "off" : mode == 1 ? "low latency mode and markers" : "low latency mode, markers and Sleep before presents_done",
                         known ? (sleep_status.bLowLatencyMode ? "on" : "off") : "unknown");
    }
    g_mode.store(mode);
}

int mode() noexcept { return g_mode.load(); }

void set_frame(std::uint64_t id, std::int64_t game_start_qpc) noexcept {
    g_frame.store(id);
    g_frame_start.store(game_start_qpc);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    g_frame_taken.store(now.QuadPart);
}
std::uint64_t frame() noexcept { return g_frame.load(); }
std::int64_t frame_start() noexcept { return g_frame_start.load(); }
std::int64_t frame_taken() noexcept { return g_frame_taken.load(); }

void marker(Marker type, std::uint64_t frame) noexcept {
    if (!g_mode.load() || !frame) return;
    // RENDERSUBMIT_END once per frame: before its PRESENT markers when the
    // frame presents, else when the host's frame ends.
    if (type == Marker::render_end && g_render_end_frame.exchange(frame) == frame) return;
    ID3D12Device* device = g_device;  // set before the mode turns on
    NV_LATENCY_MARKER_PARAMS params{};
    params.version = NV_LATENCY_MARKER_PARAMS_VER;
    params.frameID = frame;
    params.markerType = nv_type(type);
    NvAPI_D3D_SetLatencyMarker(device, &params);
}

void sleep() noexcept {
    if (g_mode.load() != 2) return;
    NvAPI_D3D_Sleep(g_device);
}

void real_presented(std::int64_t game_start_qpc) noexcept {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_frequency) {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        g_frequency = frequency.QuadPart;
    }
    if (game_start_qpc > 0 && now.QuadPart > game_start_qpc) {
        const double ms = 1000.0 * double(now.QuadPart - game_start_qpc) / double(g_frequency);
        if (ms < 1000.0) {
            try { g_samples.push_back(ms); } catch (...) {}
        }
    }
    if (!g_window_start) g_window_start = now.QuadPart;
    if (double(now.QuadPart - g_window_start) < 5.0 * double(g_frequency)) return;
    const double seconds = double(now.QuadPart - g_window_start) / double(g_frequency);
    g_window_start = now.QuadPart;
    if (g_samples.empty()) return;
    std::sort(g_samples.begin(), g_samples.end());
    const double median = g_samples[g_samples.size() / 2];
    const double p95 = g_samples[std::min(g_samples.size() - 1, g_samples.size() * 95 / 100)];
    char nvapi[200], pacing[200] = {};
    nvapi_report(nvapi, sizeof(nvapi));
    if (g_pacing_count)
        std::snprintf(pacing, sizeof(pacing), "; latency_pacing %d: release delay %.2f ms, ring wait %.2f ms, queue %.2f ms, host frame %.2f ms (unpaced %.2f)",
                      g_pacing_on ? 1 : 0, g_delay_sum / g_pacing_count, g_ring_sum / g_pacing_count, g_queue_sum / g_pacing_count, g_interval,
                      g_unpaced);
    remote::host_log("latency over %.1f s: game frame start -> real image Present returned median %.2f ms p95 %.2f ms (%zu frames); reflex %d%s%s",
                     seconds, median, p95, g_samples.size(), g_mode.load(), pacing, nvapi);
    g_samples.clear();
    g_delay_sum = g_ring_sum = g_queue_sum = 0;
    g_pacing_count = 0;
}

void pacing_sample(bool on, double delay_ms, double ring_ms, double queue_ms, double interval_ms, double unpaced_ms) noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pacing_on = on;
    g_delay_sum += delay_ms;
    g_ring_sum += ring_ms;
    g_queue_sum += queue_ms;
    g_interval = interval_ms;
    g_unpaced = unpaced_ms;
    ++g_pacing_count;
}

} // namespace reflex
