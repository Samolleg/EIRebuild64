#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dxgi.h>

#include <cstdint>
#include <memory>
#include <string>

namespace owned {

struct TelemetryValue {
    double value = 0.0;
    bool valid = false;
};

struct SystemTelemetrySnapshot {
    TelemetryValue cpu_percent{};
    TelemetryValue ram_private_bytes{};
    TelemetryValue gpu_3d_percent{};
    TelemetryValue vram_usage_bytes{};
    TelemetryValue vram_budget_bytes{};
    std::uint64_t sequence = 0;
    std::uint64_t sampled_tick_ms = 0;
    std::uint64_t age_ms = 0;
    bool active = false;
    bool stale = true;
};

// Samples low-frequency process statistics on its own worker. Adapter changes and
// shutdown are lifecycle operations; snapshot reads are nonblocking and leave the
// caller's prior snapshot untouched when the sampler is publishing.
class SystemTelemetrySampler {
public:
    SystemTelemetrySampler();
    ~SystemTelemetrySampler();

    SystemTelemetrySampler(const SystemTelemetrySampler&) = delete;
    SystemTelemetrySampler& operator=(const SystemTelemetrySampler&) = delete;

    // Starting an already-running sampler is idempotent. Use set_adapter() for
    // an adapter change after a successful start.
    bool start(IDXGIAdapter* adapter);
    void stop() noexcept;
    void set_active(bool active) noexcept;
    void set_adapter(IDXGIAdapter* adapter);
    bool try_get_snapshot(SystemTelemetrySnapshot& in_out) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

namespace system_telemetry_test {

struct GpuEngineIdentity {
    DWORD process_id = 0;
    LUID adapter_luid{};
    bool is_3d = false;
};

bool parse_gpu_engine_instance(const std::wstring& instance, GpuEngineIdentity& identity) noexcept;
bool gpu_engine_matches(const std::wstring& instance, DWORD process_id, LUID adapter_luid) noexcept;
double maximum_gpu_engine_percent(const std::wstring* instances,
                                  const double* values,
                                  std::size_t count,
                                  DWORD process_id,
                                  LUID adapter_luid,
                                  bool& valid) noexcept;
double cpu_percent_from_deltas(std::uint64_t process_time_delta_100ns,
                               std::uint64_t wall_time_delta_100ns,
                               DWORD logical_processor_count,
                               bool& valid) noexcept;

} // namespace system_telemetry_test
} // namespace owned
