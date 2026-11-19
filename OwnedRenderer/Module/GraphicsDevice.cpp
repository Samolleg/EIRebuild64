#include "GraphicsApi.h"
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

HRESULT EiCreateDevice11V1(D3D_DRIVER_TYPE driver, ID3D11Device** device,
                           D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context) noexcept {
    if (!device || !level || !context) return E_POINTER;
    constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                            D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3};
    // Avoid the driver lock inversion during background shader preparation.
    constexpr UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
    return D3D11CreateDevice(nullptr, driver, nullptr, flags, levels, ARRAYSIZE(levels),
                             D3D11_SDK_VERSION, device, level, context);
}

HRESULT EiCreateSwapChain11V1(ID3D11Device* device, HWND window, UINT width, UINT height,
                              EiRenderStage stage, void* context, IDXGISwapChain** output,
                              UINT* flags, BOOL* tearing) noexcept {
    if (!output || !flags || !tearing || !stage || !device) return E_POINTER;
    *output = nullptr;
    *flags = 0;
    *tearing = FALSE;
    stage(context, "swap-chain-entry");
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgi));
    if (SUCCEEDED(hr)) hr = dxgi->GetAdapter(&adapter);
    if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;
    ComPtr<IDXGIFactory5> factory5;
    BOOL supported = FALSE;
    if (SUCCEEDED(factory.As(&factory5)) &&
        SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &supported, sizeof(supported))))
        *tearing = supported;
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 3;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = *tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    ComPtr<IDXGISwapChain1> chain;
    stage(context, "create-swap-chain-for-hwnd");
    hr = factory->CreateSwapChainForHwnd(device, window, &desc, nullptr, nullptr, &chain);
    if (FAILED(hr)) {
        OutputDebugStringA("EID3D11: flip-model swap chain unavailable; using the blit presentation model.\n");
        *tearing = FALSE;
        desc.Flags = 0;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        stage(context, "create-fallback-swap-chain-for-hwnd");
        hr = factory->CreateSwapChainForHwnd(device, window, &desc, nullptr, nullptr, &chain);
        if (FAILED(hr)) return hr;
    }
    stage(context, "swap-chain-created");
    *flags = desc.Flags;
    factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    stage(context, "swap-chain-complete");
    return chain.CopyTo(output);
}
