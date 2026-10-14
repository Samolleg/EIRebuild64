#include "system_telemetry.h"

#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace owned {
namespace {

using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;

constexpr auto kSampleInterval = 500ms;
constexpr std::uint64_t kStaleAfterMs = 1500;

std::uint64_t file_time_value(const FILETIME& value) noexcept {
    ULARGE_INTEGER result{};
    result.LowPart = value.dwLowDateTime;
    result.HighPart = value.dwHighDateTime;
    return result.QuadPart;
}

bool equal_ascii_insensitive(wchar_t left, wchar_t right) noexcept {
    return std::towlower(left) == std::towlower(right);
}

bool contains_ascii_insensitive(const std::wstring& text, const wchar_t* needle) noexcept {
    const std::size_t needle_length = std::wcslen(needle);
    if (needle_length > text.size()) return false;
    for (std::size_t begin = 0; begin + needle_length <= text.size(); ++begin) {
        bool match = true;
        for (std::size_t offset = 0; offset < needle_length; ++offset) {
            if (!equal_ascii_insensitive(text[begin + offset], needle[offset])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool parse_unsigned(const std::wstring& text, std::size_t begin, std::size_t end,
                    int base, std::uint64_t& value) noexcept {
    if (begin >= end) return false;
    value = 0;
    for (std::size_t index = begin; index < end; ++index) {
        const wchar_t c = text[index];
        unsigned digit = 0;
        if (c >= L'0' && c <= L'9') digit = static_cast<unsigned>(c - L'0');
        else if (base == 16 && c >= L'a' && c <= L'f') digit = static_cast<unsigned>(c - L'a' + 10);
        else if (base == 16 && c >= L'A' && c <= L'F') digit = static_cast<unsigned>(c - L'A' + 10);
        else return false;
        if (digit >= static_cast<unsigned>(base) ||
            value > (std::numeric_limits<std::uint64_t>::max() - digit) / static_cast<unsigned>(base)) {
            return false;
        }
        value = value * static_cast<unsigned>(base) + digit;
    }
    return true;
}

class GpuPdhQuery {
public:
    ~GpuPdhQuery() { reset(); }

    bool ensure() noexcept {
        if (query_) return true;
        if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) return false;
        const PDH_STATUS status = PdhAddEnglishCounterW(
            query_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter_);
        if (status != ERROR_SUCCESS) {
            reset();
            return false;
        }
        if (PdhCollectQueryData(query_) != ERROR_SUCCESS) {
            reset();
            return false;
        }
        return true;
    }

    bool sample(DWORD process_id, LUID adapter_luid, double& percent) noexcept {
        if (!ensure() || PdhCollectQueryData(query_) != ERROR_SUCCESS) {
            reset();
            return false;
        }

        DWORD bytes = 0;
        DWORD count = 0;
        PDH_STATUS status = PdhGetFormattedCounterArrayW(
            counter_, PDH_FMT_DOUBLE, &bytes, &count, nullptr);
        if (status != PDH_MORE_DATA || bytes == 0) {
            if (status != ERROR_SUCCESS) reset();
            return false;
        }
        std::vector<BYTE> storage(bytes);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
        status = PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE, &bytes, &count, items);
        if (status != ERROR_SUCCESS) {
            reset();
            return false;
        }

        bool valid = false;
        double maximum = 0.0;
        for (DWORD index = 0; index < count; ++index) {
            const auto& item = items[index];
            if (!item.szName ||
                (item.FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
                 item.FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) ||
                !system_telemetry_test::gpu_engine_matches(item.szName, process_id, adapter_luid)) {
                continue;
            }
            maximum = valid ? std::max(maximum, item.FmtValue.doubleValue)
                            : item.FmtValue.doubleValue;
            valid = true;
        }
        if (valid) percent = std::clamp(maximum, 0.0, 100.0);
        else reset();
        return valid;
    }

    void reset() noexcept {
        if (query_) PdhCloseQuery(query_);
        query_ = nullptr;
        counter_ = nullptr;
    }

private:
    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER counter_ = nullptr;
};

} // namespace

namespace system_telemetry_test {

bool parse_gpu_engine_instance(const std::wstring& instance, GpuEngineIdentity& identity) noexcept {
    identity = {};
    const std::size_t pid_key = instance.find(L"pid_");
    if (pid_key == std::wstring::npos) return false;
    const std::size_t pid_begin = pid_key + 4;
    const std::size_t pid_end = instance.find(L'_', pid_begin);
    std::uint64_t pid = 0;
    if (pid_end == std::wstring::npos || !parse_unsigned(instance, pid_begin, pid_end, 10, pid) ||
        pid > std::numeric_limits<DWORD>::max()) {
        return false;
    }

    const std::size_t luid_key = instance.find(L"luid_", pid_end);
    if (luid_key == std::wstring::npos) return false;
    std::size_t high_begin = luid_key + 5;
    if (high_begin + 2 <= instance.size() && instance[high_begin] == L'0' &&
        (instance[high_begin + 1] == L'x' || instance[high_begin + 1] == L'X')) {
        high_begin += 2;
    }
    const std::size_t high_end = instance.find(L'_', high_begin);
    if (high_end == std::wstring::npos) return false;
    std::size_t low_begin = high_end + 1;
    if (low_begin + 2 <= instance.size() && instance[low_begin] == L'0' &&
        (instance[low_begin + 1] == L'x' || instance[low_begin + 1] == L'X')) {
        low_begin += 2;
    }
    const std::size_t low_end = instance.find(L'_', low_begin);
    if (low_end == std::wstring::npos) return false;

    std::uint64_t high = 0;
    std::uint64_t low = 0;
    if (!parse_unsigned(instance, high_begin, high_end, 16, high) ||
        !parse_unsigned(instance, low_begin, low_end, 16, low) ||
        high > std::numeric_limits<DWORD>::max() || low > std::numeric_limits<DWORD>::max()) {
        return false;
    }
    identity.process_id = static_cast<DWORD>(pid);
    identity.adapter_luid.HighPart = static_cast<LONG>(static_cast<DWORD>(high));
    identity.adapter_luid.LowPart = static_cast<DWORD>(low);
    identity.is_3d = contains_ascii_insensitive(instance.substr(low_end + 1), L"engtype_3d");
    return true;
}

bool gpu_engine_matches(const std::wstring& instance, DWORD process_id, LUID adapter_luid) noexcept {
    GpuEngineIdentity identity{};
    return parse_gpu_engine_instance(instance, identity) && identity.is_3d &&
           identity.process_id == process_id &&
           identity.adapter_luid.HighPart == adapter_luid.HighPart &&
           identity.adapter_luid.LowPart == adapter_luid.LowPart;
}

double maximum_gpu_engine_percent(const std::wstring* instances,
                                  const double* values,
                                  std::size_t count,
                                  DWORD process_id,
                                  LUID adapter_luid,
                                  bool& valid) noexcept {
    valid = false;
    double maximum = 0.0;
    if (!instances || !values) return maximum;
    for (std::size_t index = 0; index < count; ++index) {
        if (!gpu_engine_matches(instances[index], process_id, adapter_luid)) continue;
        maximum = valid ? std::max(maximum, values[index]) : values[index];
        valid = true;
    }
    return valid ? std::clamp(maximum, 0.0, 100.0) : 0.0;
}

double cpu_percent_from_deltas(std::uint64_t process_time_delta_100ns,
                               std::uint64_t wall_time_delta_100ns,
                               DWORD logical_processor_count,
                               bool& valid) noexcept {
    valid = wall_time_delta_100ns != 0 && logical_processor_count != 0;
    if (!valid) return 0.0;
    const double capacity = static_cast<double>(wall_time_delta_100ns) * logical_processor_count;
    return std::clamp(static_cast<double>(process_time_delta_100ns) * 100.0 / capacity, 0.0, 100.0);
}

} // namespace system_telemetry_test

struct SystemTelemetrySampler::Impl {
    ~Impl() { stop(); }

    bool start(IDXGIAdapter* initial_adapter) {
        std::lock_guard<std::mutex> state_guard(state_mutex);
        if (worker.joinable()) return true;
        stop_requested = false;
        active = false;
        set_adapter_locked(initial_adapter);
        {
            std::lock_guard<std::mutex> snapshot_guard(snapshot_mutex);
            snapshot = {};
        }
        try {
            worker = std::thread([this] { run(); });
        } catch (...) {
            adapter.Reset();
            return false;
        }
        return true;
    }

    void stop() noexcept {
        {
            std::lock_guard<std::mutex> guard(state_mutex);
            stop_requested = true;
            active = false;
        }
        wake.notify_all();
        if (worker.joinable()) worker.join();
        std::lock_guard<std::mutex> guard(state_mutex);
        adapter.Reset();
        std::lock_guard<std::mutex> snapshot_guard(snapshot_mutex);
        snapshot.active = false;
        snapshot.stale = true;
    }

    void set_active(bool value) noexcept {
        {
            std::lock_guard<std::mutex> guard(state_mutex);
            active = value;
        }
        wake.notify_all();
    }

    void set_adapter(IDXGIAdapter* value) {
        {
            std::lock_guard<std::mutex> guard(state_mutex);
            set_adapter_locked(value);
            ++adapter_generation;
        }
        wake.notify_all();
    }

    void set_adapter_locked(IDXGIAdapter* value) { adapter = value; }

    void run() noexcept {
        const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool uninitialize_com = SUCCEEDED(com_result);
        HANDLE process = GetCurrentProcess();
        const DWORD process_id = GetCurrentProcessId();
        SYSTEM_INFO system_info{};
        GetSystemInfo(&system_info);
        const DWORD active_processor_count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        const DWORD processor_count = active_processor_count
                                          ? active_processor_count
                                          : std::max<DWORD>(1, system_info.dwNumberOfProcessors);
        LARGE_INTEGER qpc_frequency{};
        QueryPerformanceFrequency(&qpc_frequency);

        GpuPdhQuery gpu_query;
        std::uint64_t previous_process_time = 0;
        std::uint64_t previous_wall_time = 0;
        std::uint64_t observed_generation = std::numeric_limits<std::uint64_t>::max();

        std::unique_lock<std::mutex> state_lock(state_mutex);
        while (!stop_requested) {
            wake.wait(state_lock, [this] { return stop_requested || active; });
            if (stop_requested) break;

            const std::uint64_t generation = adapter_generation;
            ComPtr<IDXGIAdapter> sample_adapter = adapter;
            state_lock.unlock();

            if (generation != observed_generation) {
                gpu_query.reset();
                previous_process_time = 0;
                previous_wall_time = 0;
                observed_generation = generation;
            }

            SystemTelemetrySnapshot next{};
            next.active = true;
            next.sampled_tick_ms = GetTickCount64();

            FILETIME creation{}, exit{}, kernel{}, user{};
            LARGE_INTEGER qpc_now{};
            if (GetProcessTimes(process, &creation, &exit, &kernel, &user) &&
                qpc_frequency.QuadPart > 0 && QueryPerformanceCounter(&qpc_now)) {
                const std::uint64_t process_time = file_time_value(kernel) + file_time_value(user);
                const std::uint64_t wall_time = static_cast<std::uint64_t>(qpc_now.QuadPart);
                if (previous_process_time && previous_wall_time &&
                    process_time >= previous_process_time && wall_time >= previous_wall_time) {
                    const auto wall_delta = static_cast<std::uint64_t>(
                        static_cast<long double>(wall_time - previous_wall_time) * 10'000'000.0L /
                        static_cast<long double>(qpc_frequency.QuadPart));
                    next.cpu_percent.value = system_telemetry_test::cpu_percent_from_deltas(
                        process_time - previous_process_time, wall_delta,
                        processor_count, next.cpu_percent.valid);
                }
                previous_process_time = process_time;
                previous_wall_time = wall_time;
            }

            PROCESS_MEMORY_COUNTERS_EX memory{};
            memory.cb = sizeof(memory);
            if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                     static_cast<DWORD>(sizeof(memory)))) {
                next.ram_private_bytes.value = static_cast<double>(memory.PrivateUsage);
                next.ram_private_bytes.valid = true;
            }

            DXGI_ADAPTER_DESC adapter_desc{};
            if (sample_adapter && SUCCEEDED(sample_adapter->GetDesc(&adapter_desc))) {
                double gpu_percent = 0.0;
                if (gpu_query.sample(process_id, adapter_desc.AdapterLuid, gpu_percent)) {
                    next.gpu_3d_percent = {gpu_percent, true};
                }
                ComPtr<IDXGIAdapter3> adapter3;
                if (SUCCEEDED(sample_adapter.As(&adapter3))) {
                    DXGI_QUERY_VIDEO_MEMORY_INFO video_memory{};
                    if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(
                            0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &video_memory))) {
                        next.vram_usage_bytes = {static_cast<double>(video_memory.CurrentUsage), true};
                        next.vram_budget_bytes = {static_cast<double>(video_memory.Budget), true};
                    }
                }
            }

            state_lock.lock();
            if (stop_requested) break;
            if (!active || adapter_generation != generation) {
                if (!active) {
                    std::lock_guard<std::mutex> snapshot_guard(snapshot_mutex);
                    snapshot.active = false;
                    snapshot.stale = true;
                }
                continue;
            }
            {
                std::lock_guard<std::mutex> snapshot_guard(snapshot_mutex);
                next.sequence = snapshot.sequence + 1;
                snapshot = next;
            }
            wake.wait_for(state_lock, kSampleInterval, [this, generation] {
                return stop_requested || !active || adapter_generation != generation;
            });
            if (!active) {
                previous_process_time = 0;
                previous_wall_time = 0;
                std::lock_guard<std::mutex> snapshot_guard(snapshot_mutex);
                snapshot.active = false;
                snapshot.stale = true;
            }
        }
        state_lock.unlock();
        gpu_query.reset();
        if (uninitialize_com) CoUninitialize();
    }

    mutable std::mutex snapshot_mutex;
    SystemTelemetrySnapshot snapshot{};
    std::mutex state_mutex;
    std::condition_variable wake;
    ComPtr<IDXGIAdapter> adapter;
    std::thread worker;
    std::uint64_t adapter_generation = 0;
    bool active = false;
    bool stop_requested = false;
};

SystemTelemetrySampler::SystemTelemetrySampler() : impl_(std::make_unique<Impl>()) {}
SystemTelemetrySampler::~SystemTelemetrySampler() = default;

bool SystemTelemetrySampler::start(IDXGIAdapter* adapter) { return impl_->start(adapter); }
void SystemTelemetrySampler::stop() noexcept { impl_->stop(); }
void SystemTelemetrySampler::set_active(bool active) noexcept { impl_->set_active(active); }
void SystemTelemetrySampler::set_adapter(IDXGIAdapter* adapter) { impl_->set_adapter(adapter); }

bool SystemTelemetrySampler::try_get_snapshot(SystemTelemetrySnapshot& in_out) const noexcept {
    std::unique_lock<std::mutex> guard(impl_->snapshot_mutex, std::try_to_lock);
    if (!guard.owns_lock()) return false;
    in_out = impl_->snapshot;
    const std::uint64_t now = GetTickCount64();
    in_out.age_ms = in_out.sampled_tick_ms && now >= in_out.sampled_tick_ms
                        ? now - in_out.sampled_tick_ms
                        : 0;
    in_out.stale = !in_out.active || in_out.sequence == 0 || in_out.age_ms > kStaleAfterMs;
    return true;
}

} // namespace owned
