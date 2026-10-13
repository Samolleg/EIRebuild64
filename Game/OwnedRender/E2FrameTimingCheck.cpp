#include "E2FrameTiming.h"
#include <cstdio>
#include <limits>

int main() {
    using namespace OwnedRender;
    using Microsoft::WRL::ComPtr;
    unsigned checks = 0;
    auto check = [&](bool ok) { if (!ok) throw ++checks; ++checks; };
    try {
        FrameSamples samples;
        check(samples.Read().count == 0);
        check(!samples.Add(-1) && !samples.Add(std::numeric_limits<double>::infinity()) &&
              !samples.Add(std::numeric_limits<double>::quiet_NaN()));
        check(samples.Add(10)); check(samples.Read().median == 10 && samples.Read().p95 == 10);
        check(samples.Add(30)); check(samples.Read().median == 20 && samples.Read().p95 == 30);
        check(samples.Add(20)); check(samples.Read().median == 20);
        samples = {};
        for (unsigned i = 1; i <= 300; ++i) samples.Add(i);
        // Bounded tail contains 61..300, median 180.5, nearest-rank p95 288.
        check(samples.Read().count == 240 && samples.Read().median == 180.5 && samples.Read().p95 == 288);
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{1000000, FALSE};
        double milliseconds = -1;
        check(E2FrameTiming::DecodeGpu(disjoint, 100, 2100, milliseconds) && milliseconds == 2);
        disjoint.Disjoint = TRUE;
        check(!E2FrameTiming::DecodeGpu(disjoint, 100, 2100, milliseconds) && milliseconds == 2);
        disjoint = {0, FALSE}; check(!E2FrameTiming::DecodeGpu(disjoint, 100, 2100, milliseconds));
        disjoint.Frequency = 1000000; check(!E2FrameTiming::DecodeGpu(disjoint, 2100, 100, milliseconds));

        E2FrameTiming timing;
        check(timing.Attach(nullptr, nullptr) == E_INVALIDARG);
        timing.Begin(); timing.End(true);
        check(timing.Read().completed == 1 && timing.Read().cpu.count == 1 && timing.Read().gpu.count == 0);
        timing.End(true); check(timing.Read().completed == 1); // unmatched present
        timing.Begin(); timing.Begin(); timing.End(false);
        check(timing.Read().aborted == 2 && timing.Read().cpu.count == 1);

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level{};
        check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, &level, &context)));
        ComPtr<ID3D11DeviceContext> deferred;
        check(SUCCEEDED(device->CreateDeferredContext(0, &deferred)));
        check(timing.Attach(device.Get(), deferred.Get()) == E_INVALIDARG);
        ComPtr<ID3D11Device> other;
        ComPtr<ID3D11DeviceContext> otherContext;
        check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &other, nullptr, &otherContext)));
        check(timing.Attach(device.Get(), otherContext.Get()) == E_INVALIDARG);
        check(timing.Attach(device.Get(), context.Get()) == S_OK);
        check(timing.Read().cpu.count == 0 && timing.Read().completed == 0);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = 64; desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> target;
        check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)));
        check(SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &target)));
        const float color[]{.25f,.5f,.75f,1.f};
        for (unsigned i = 0; i < 4; ++i) {
            timing.Begin(); context->ClearRenderTargetView(target.Get(), color); timing.End(true);
        }
        // The fixture has no swap chain. Explicitly submit its work once;
        // production Poll uses DONOTFLUSH and never performs this Flush/wait.
        context->Flush();
        const auto deadline = GetTickCount64() + 10000;
        while (timing.Read().gpu.count < 4 && GetTickCount64() < deadline) { timing.Poll(); Sleep(1); }
        auto snapshot = timing.Read();
        check(snapshot.completed == 4 && snapshot.cpu.count == 4);
        check(snapshot.gpu.count == 4 && snapshot.gpuStatus == S_OK && snapshot.invalid == 0 && snapshot.dropped == 0);
        check(snapshot.gpu.median >= 0 && snapshot.gpu.p95 >= snapshot.gpu.median);
        timing.Begin(); context->ClearRenderTargetView(target.Get(), color); timing.End(false);
        timing.Begin(); timing.Begin(); timing.End(false);
        context->Flush(); timing.Poll();
        check(timing.Read().gpu.count == 4 && timing.Read().cpu.count == 4 && timing.Read().aborted == 3);
        timing.Begin(); timing.Reset();
        check(timing.Read().gpu.count == 0 && timing.Read().cpu.count == 0 && timing.Read().completed == 0);
        check(timing.Attach(device.Get(), context.Get()) == S_OK);
        timing.Begin(); timing.End(true); context->Flush();
        const auto nextDeadline = GetTickCount64() + 10000;
        while (!timing.Read().gpu.count && GetTickCount64() < nextDeadline) { timing.Poll(); Sleep(1); }
        check(timing.Read().gpu.count == 1 && timing.Read().completed == 1);
        std::printf("E2_FRAME_TIMING_PASS checks=%u WARP_feature_level=%x GPU_samples=%u median_ms=%.6f p95_ms=%.6f\n",
            checks, unsigned(level), snapshot.gpu.count, snapshot.gpu.median, snapshot.gpu.p95);
        return 0;
    } catch (unsigned failure) { std::printf("E2_FRAME_TIMING_FAIL check=%u\n", failure); return 1; }
}
