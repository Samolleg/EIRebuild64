#pragma once
#include <d3d11.h>
#include <wrl/client.h>

namespace owned {
// Synchronous access only. The host must not retain targets, change the
// renderer lifecycle, or present from this callback. In particular, a target
// borrowed before world composition is not the target after composition.
// Swap-chain ownership is deliberately not part of this interface.
struct EmbeddedDeviceView {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11RenderTargetView* color = nullptr;
    ID3D11DepthStencilView* depth = nullptr;
    UINT width = 0, height = 0;
};
using EmbeddedDeviceVisitor = HRESULT (*)(void*, const EmbeddedDeviceView&);
using EmbeddedOutputVisitor = HRESULT (*)(void*, ID3D11DeviceContext*, ID3D11Texture2D*);

// The composed output, independent of the current world-sized render target.
// Borrowed for this callback only; callers must serialize it with resize and
// release, exactly as the active-device visitor below.
inline HRESULT visit_embedded_output(ID3D11Device* device, ID3D11DeviceContext* context,
    ID3D11RenderTargetView* output, void* user, EmbeddedOutputVisitor visitor) {
    if (!visitor) return E_INVALIDARG;
    if (!device || !context || !output) return DXGI_ERROR_INVALID_CALL;
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> actual;
    context->GetDevice(&actual);
    if (actual.Get() != device || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return E_INVALIDARG;
    actual.Reset(); output->GetDevice(&actual);
    if (actual.Get() != device) return E_INVALIDARG;
    ComPtr<ID3D11Resource> resource;
    output->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = resource.As(&texture);
    if (FAILED(hr)) return hr;
    D3D11_RENDER_TARGET_VIEW_DESC view{}; output->GetDesc(&view);
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    if (view.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || view.Texture2D.MipSlice ||
        !desc.Width || !desc.Height || desc.ArraySize != 1 || desc.SampleDesc.Count != 1)
        return E_INVALIDARG;
    hr = device->GetDeviceRemovedReason();
    return FAILED(hr) ? hr : visitor(user, context, texture.Get());
}

// Also used by the headless host-boundary check with real D3D11 resources.
// Reject partial/foreign targets before any host code can mutate the context.
inline HRESULT visit_embedded_device(ID3D11Device* device,
    ID3D11DeviceContext* context, ID3D11RenderTargetView* color,
    ID3D11DepthStencilView* depth, void* user, EmbeddedDeviceVisitor visitor) {
    if (!visitor) return E_INVALIDARG;
    if (!device || !context || !color || !depth) return DXGI_ERROR_INVALID_CALL;
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> actual;
    context->GetDevice(&actual);
    if (actual.Get() != device || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return E_INVALIDARG;
    actual.Reset(); color->GetDevice(&actual);
    if (actual.Get() != device) return E_INVALIDARG;
    actual.Reset(); depth->GetDevice(&actual);
    if (actual.Get() != device) return E_INVALIDARG;
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Texture2D> colorTexture, depthTexture;
    color->GetResource(&resource);
    HRESULT hr = resource.As(&colorTexture);
    if (FAILED(hr)) return hr;
    resource.Reset(); depth->GetResource(&resource);
    hr = resource.As(&depthTexture);
    if (FAILED(hr)) return hr;
    D3D11_TEXTURE2D_DESC c{}, d{};
    colorTexture->GetDesc(&c); depthTexture->GetDesc(&d);
    // c56's active views address mip zero of single-sample 2D textures.
    D3D11_RENDER_TARGET_VIEW_DESC cv{}; color->GetDesc(&cv);
    D3D11_DEPTH_STENCIL_VIEW_DESC dv{}; depth->GetDesc(&dv);
    if (cv.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || cv.Texture2D.MipSlice ||
        dv.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D || dv.Texture2D.MipSlice ||
        !c.Width || !c.Height || c.Width != d.Width || c.Height != d.Height ||
        c.ArraySize != 1 || d.ArraySize != 1 ||
        c.SampleDesc.Count != 1 || d.SampleDesc.Count != 1)
        return E_INVALIDARG;
    hr = device->GetDeviceRemovedReason();
    if (FAILED(hr)) return hr;
    return visitor(user, {device, context, color, depth, c.Width, c.Height});
}
}
