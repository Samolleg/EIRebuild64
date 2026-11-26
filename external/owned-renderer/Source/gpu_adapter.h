// The GPU every renderer device is created on (the game's D3D11 device, the
// x64 host's D3D11 device and its D3D12 device): the high-performance GPU by
// Windows' GPU preference (IDXGIFactory6::EnumAdapterByGpuPreference,
// Windows 10 1803 and newer), skipping software adapters, as GD3D11 does.
//
// Hybrid laptops list the integrated GPU first; adapter 0 put a player's
// RTX 4070 Laptop system on its Radeon 780M, which removed the device
// (887a0020) under the renderer's load (issue "Hybrid laptops render on the
// integrated GPU"). Without IDXGIFactory6 the result is null: the caller
// keeps the default adapter (D3D11CreateDevice(nullptr), EnumAdapters1(0)),
// the behaviour before this choice existed.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>

namespace gpu_adapter {

struct Choice {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter; // null: use the default adapter
    char why[320]{};                               // one log line: which GPU and why
};

inline Choice choose() noexcept {
    Choice choice;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        std::snprintf(choice.why, sizeof(choice.why), "default adapter (CreateDXGIFactory1 failed)");
        return choice;
    }
    // The default adapter, named in the log so a hybrid system shows both.
    char first[160] = "none";
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter0;
    DXGI_ADAPTER_DESC1 desc0{};
    if (SUCCEEDED(factory->EnumAdapters1(0, &adapter0)) && SUCCEEDED(adapter0->GetDesc1(&desc0)))
        std::snprintf(first, sizeof(first), "%ls (vendor %04x)", desc0.Description, desc0.VendorId);
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory6;
    if (FAILED(factory.As(&factory6))) {
        std::snprintf(choice.why, sizeof(choice.why),
                      "default adapter %s: no IDXGIFactory6 (Windows older than 10 1803), no GPU preference", first);
        return choice;
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0; factory6->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                               IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())) !=
                         DXGI_ERROR_NOT_FOUND;
         ++index) {
        DXGI_ADAPTER_DESC1 desc{};
        if (!adapter || FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        const bool same = desc.AdapterLuid.LowPart == desc0.AdapterLuid.LowPart &&
                          desc.AdapterLuid.HighPart == desc0.AdapterLuid.HighPart;
        std::snprintf(choice.why, sizeof(choice.why), "%ls (vendor %04x, %llu MiB dedicated): high-performance GPU by preference%s%s",
                      desc.Description, desc.VendorId, static_cast<unsigned long long>(desc.DedicatedVideoMemory >> 20),
                      same ? ", also the default adapter" : "; default adapter was ", same ? "" : first);
        choice.adapter = adapter;
        return choice;
    }
    std::snprintf(choice.why, sizeof(choice.why), "default adapter %s: GPU preference listed no hardware adapter", first);
    return choice;
}

} // namespace gpu_adapter
