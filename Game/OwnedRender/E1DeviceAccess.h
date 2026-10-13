#pragma once
#include "embedded_device.h"

namespace OwnedRender {
// Operations execute during the provider's synchronous device lease. Targets
// are never cached by CDX11, so world/UI composition may replace them freely.
inline HRESULT E1BindTargets(void*, const owned::EmbeddedDeviceView& view) {
    view.context->OMSetRenderTargets(1, &view.color, view.depth);
    const D3D11_VIEWPORT viewport{0, 0, float(view.width), float(view.height), 0, 1};
    view.context->RSSetViewports(1, &viewport);
    return S_OK;
}
inline HRESULT E1ClearColor(void* color, const owned::EmbeddedDeviceView& view) {
    if (!color) return E_INVALIDARG;
    view.context->ClearRenderTargetView(view.color, static_cast<const float*>(color));
    return S_OK;
}
inline HRESULT E1ClearDepth(void* depth, const owned::EmbeddedDeviceView& view) {
    if (!depth) return E_INVALIDARG;
    view.context->ClearDepthStencilView(view.depth, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
        *static_cast<const float*>(depth), 0);
    return S_OK;
}
// Capture the bound world/UI target, falling back to the composed output.
// Both texture references remain local to the provider's output callback.
inline HRESULT E1CaptureSource(ID3D11DeviceContext* context, ID3D11Texture2D* output,
    Microsoft::WRL::ComPtr<ID3D11Texture2D>& source,
    bool& usedBoundTarget, bool& differentFromOutput) {
    source.Reset(); usedBoundTarget = differentFromOutput = false;
    if (!context || !output) return E_INVALIDARG;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> bound;
    context->OMGetRenderTargets(1, &bound, nullptr);
    if (bound) {
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        bound->GetResource(&resource);
        usedBoundTarget = resource && SUCCEEDED(resource.As(&source));
    }
    if (!source) source = output;
    differentFromOutput = source.Get() != output;
    return S_OK;
}
}
