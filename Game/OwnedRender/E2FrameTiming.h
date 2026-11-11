#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "render_profiler.h"

namespace OwnedRender {
// A bounded tail of completed measurements, in milliseconds. Missing GPU
// results never enter the distribution as zero. No per-frame allocation.
class FrameSamples {
    std::array<double, 240> values{};
    unsigned count = 0, next = 0;
public:
    struct Summary { unsigned count = 0; double median = 0, p95 = 0; };
    bool Add(double value) noexcept {
        if (!std::isfinite(value) || value < 0) return false;
        values[next] = value; next = (next + 1) % values.size();
        count = (std::min)(count + 1, unsigned(values.size()));
        return true;
    }
    Summary Read() const noexcept {
        if (!count) return {};
        auto sorted = values;
        std::sort(sorted.begin(), sorted.begin() + count);
        const double median = count % 2 ? sorted[count / 2] :
            (sorted[count / 2 - 1] + sorted[count / 2]) * .5;
        return {count, median, sorted[(95 * count + 99) / 100 - 1]};
    }
};

// Render-owner thread only. Eight asynchronous query sets bound memory even
// when the GPU never completes. GetData never flushes or waits for the GPU.
class E2FrameTiming {
    using Query = Microsoft::WRL::ComPtr<ID3D11Query>;
    struct Slot { Query disjoint, begin, end; bool pending = false;
        std::uint64_t nativeBegin = 0, nativeEnd = 0; unsigned polls = 0; };
    std::array<Slot, 8> slots;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    FrameSamples cpu, gpu;
    owned::ProfileTimestamps* native = nullptr; // executor lifetime, reattached at renderer initialization
    LONGLONG frequency = 0, cpuBegin = 0;
    int active = -1;
    bool open = false, gpuEnabled = false;
    HRESULT queryStatus = S_FALSE;
    std::uint64_t completed = 0, dropped = 0, invalid = 0, aborted = 0;
    static LONGLONG Now() noexcept {
        LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart;
    }
    void CloseQuery() noexcept {
        if (active >= 0 && context) {
            context->End(slots[active].end.Get());
            context->End(slots[active].disjoint.Get());
        }
    }
public:
    struct Snapshot {
        FrameSamples::Summary cpu, gpu;
        std::uint64_t completed = 0, dropped = 0, invalid = 0, aborted = 0;
        HRESULT gpuStatus = S_FALSE;
    };
    E2FrameTiming() = default;
    E2FrameTiming(const E2FrameTiming&) = delete;
    E2FrameTiming& operator=(const E2FrameTiming&) = delete;
    ~E2FrameTiming() { Reset(); }
    static bool DecodeGpu(const D3D11_QUERY_DATA_TIMESTAMP_DISJOINT& disjoint,
                          UINT64 begin, UINT64 end, double& milliseconds) noexcept {
        if (disjoint.Disjoint || !disjoint.Frequency || end < begin) return false;
        milliseconds = double(end - begin) * 1000.0 / double(disjoint.Frequency);
        return true;
    }
    void Reset() noexcept {
        CloseQuery(); active = -1; open = false;
        for (auto& slot : slots) slot = {};
        context.Reset(); cpu = {}; gpu = {};
        completed = dropped = invalid = aborted = 0;
        cpuBegin = frequency = 0; gpuEnabled = false; queryStatus = S_FALSE; native = nullptr;
    }
    HRESULT AttachNative(owned::ProfileTimestamps* timestamps) noexcept {
        Reset();
        LARGE_INTEGER value{};
        if (QueryPerformanceFrequency(&value)) frequency = value.QuadPart;
        native = timestamps;
        gpuEnabled = native && native->frequency() != 0;
        return queryStatus = gpuEnabled ? S_OK : S_FALSE;
    }
    HRESULT Attach(ID3D11Device* device, ID3D11DeviceContext* immediate) noexcept {
        Reset();
        LARGE_INTEGER value{};
        if (QueryPerformanceFrequency(&value)) frequency = value.QuadPart;
        if (!device || !immediate || immediate->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
            return queryStatus = E_INVALIDARG;
        Microsoft::WRL::ComPtr<ID3D11Device> actual;
        immediate->GetDevice(&actual);
        if (actual.Get() != device) return queryStatus = E_INVALIDARG;
        context = immediate;
        for (auto& slot : slots) {
            D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
            HRESULT hr = device->CreateQuery(&desc, &slot.disjoint);
            if (SUCCEEDED(hr)) {
                desc.Query = D3D11_QUERY_TIMESTAMP;
                hr = device->CreateQuery(&desc, &slot.begin);
                if (SUCCEEDED(hr)) hr = device->CreateQuery(&desc, &slot.end);
            }
            if (FAILED(hr)) {
                for (auto& item : slots) item = {};
                return queryStatus = hr; // CPU timing remains available.
            }
        }
        gpuEnabled = true; return queryStatus = S_OK;
    }
    void Poll() noexcept {
        if (native && gpuEnabled) {
            const auto ticksPerSecond = native->frequency();
            for (auto& slot : slots) {
                if (!slot.pending) continue;
                std::uint64_t begin = 0, end = 0;
                if (!native->value(slot.nativeEnd, end) || !native->value(slot.nativeBegin, begin)) {
                    // A lost timestamp (e.g. a growing query heap) must not
                    // occupy one of the bounded slots forever.
                    if (++slot.polls > 120) { slot.pending = false; ++dropped; }
                    continue;
                }
                slot.pending = false;
                if (!ticksPerSecond || end < begin) { ++invalid; continue; }
                gpu.Add(double(end - begin) * 1000.0 / double(ticksPerSecond));
            }
            return;
        }
        if (!gpuEnabled || !context) return;
        for (auto& slot : slots) {
            if (!slot.pending) continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            UINT64 begin = 0, end = 0;
            HRESULT hr = context->GetData(slot.disjoint.Get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (hr == S_FALSE) continue;
            if (hr == S_OK) hr = context->GetData(slot.begin.Get(), &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (hr == S_FALSE) continue;
            if (hr == S_OK) hr = context->GetData(slot.end.Get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (hr == S_FALSE) continue;
            slot.pending = false;
            if (FAILED(hr)) { ++invalid; queryStatus = hr; gpuEnabled = false; return; }
            double milliseconds = 0;
            if (!DecodeGpu(disjoint, begin, end, milliseconds)) { ++invalid; continue; }
            gpu.Add(milliseconds);
        }
    }
    void Begin() noexcept {
        // A failed/abandoned frame is not a successful frame sample.
        if (open) { CloseQuery(); if (active >= 0) slots[active].pending = false; ++aborted; }
        active = -1; open = true; cpuBegin = Now();
        Poll();
        if (!gpuEnabled) return;
        for (unsigned i = 0; i < slots.size(); ++i) if (!slots[i].pending) {
            if (native) {
                slots[i].nativeBegin = native->stamp(); slots[i].nativeEnd = 0; slots[i].polls = 0;
                if (!slots[i].nativeBegin) { ++dropped; return; }
                active = int(i); return;
            }
            active = int(i);
            context->Begin(slots[i].disjoint.Get()); context->End(slots[i].begin.Get());
            return;
        }
        ++dropped;
    }
    // Called after the backend's Present: its final composition/UI commands
    // belong to this range too. This is GPU timeline elapsed time, including
    // idle/presentation gaps, not GPU busy time or CPU submission cost.
    void End(bool presented) noexcept {
        if (!open) return;
        CloseQuery();
        if (active >= 0) {
            if (native) {
                slots[active].nativeEnd = native->stamp();
                slots[active].pending = presented && slots[active].nativeEnd != 0;
                if (presented && !slots[active].nativeEnd) ++dropped;
            } else slots[active].pending = presented;
        }
        active = -1;
        if (presented) {
            if (frequency > 0) cpu.Add(double(Now() - cpuBegin) * 1000.0 / double(frequency));
            ++completed;
        } else ++aborted;
        open = false;
    }
    Snapshot Read() const noexcept { return {cpu.Read(), gpu.Read(), completed, dropped, invalid, aborted, queryStatus}; }
};
}
