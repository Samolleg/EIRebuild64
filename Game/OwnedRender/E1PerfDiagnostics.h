#pragma once
#include <windows.h>
#include <cstdint>
#include "../GameJournal.h"

namespace OwnedRender {
// Temporary PERF04 measurement, render thread only. Inclusive scope times;
// nested scopes must not be added together. No per-call file output.
struct E1PerfCounter {
    const char* name;
    LONGLONG window = 0, total = 0, maximum = 0;
    std::uint64_t calls = 0, bytes = 0;
};
class E1PerfScope {
    E1PerfCounter& counter;
    LARGE_INTEGER start{};
    std::uint64_t bytes;
public:
    explicit E1PerfScope(E1PerfCounter& value, std::uint64_t copied = 0) noexcept
        : counter(value), bytes(copied) {
        QueryPerformanceCounter(&start);
        if (!counter.window) counter.window = start.QuadPart;
    }
    ~E1PerfScope() noexcept {
        LARGE_INTEGER end{};
        QueryPerformanceCounter(&end);
        static const LONGLONG frequency = []() {
            LARGE_INTEGER value{}; QueryPerformanceFrequency(&value); return value.QuadPart;
        }();
        if (frequency <= 0) return;
        const LONGLONG elapsed = end.QuadPart - start.QuadPart;
        counter.total += elapsed;
        if (elapsed > counter.maximum) counter.maximum = elapsed;
        ++counter.calls; counter.bytes += bytes;
        if (end.QuadPart - counter.window < frequency * 2) return;
        const double ms = 1000.0 / static_cast<double>(frequency);
        GameJournal::Event("RENDER", "host_perf",
            "scope=%s window_ms=%.3f calls=%llu total_ms=%.3f mean_ms=%.4f max_ms=%.3f bytes=%llu inclusive=1",
            counter.name, (end.QuadPart-counter.window)*ms,
            static_cast<unsigned long long>(counter.calls), counter.total*ms,
            counter.total*ms/static_cast<double>(counter.calls), counter.maximum*ms,
            static_cast<unsigned long long>(counter.bytes));
        counter.window=end.QuadPart; counter.total=counter.maximum=0;
        counter.calls=counter.bytes=0;
    }
};
}
