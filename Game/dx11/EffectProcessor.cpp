#include "stdafx.h"
#include "EffectProcessor.h"
#include "RendererDX11.h"
#include "PostProcess.h"
#include "RetailModalFade.h"

void CEffectProcessor::SaveState()
{
    pDX11->GetDeviceContext()->OMGetRenderTargets(1, curView.GetAddressOf(),
                                                  curStencilView.GetAddressOf());
}

void CEffectProcessor::RestoreState()
{
    pDX11->GetDeviceContext()->OMSetRenderTargets(1, curView.GetAddressOf(), curStencilView.Get());
    curStencilView.Reset();
    curView.Reset();
}

CEffectProcessor::CEffectProcessor(CDX11 *pDXRenderer)
    : pDX11(pDXRenderer)
{
    // maybe use own device / dc to capture effects instead o save/restore state
    //    D3D_FEATURE_LEVEL featureLevel;
    //    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
    //        D3D_FEATURE_LEVEL_11_0,
    //        D3D_FEATURE_LEVEL_10_0,
    //    };
    //    UINT createDeviceFlags = 0;
    //
    // #if defined(_DEBUG)
    //    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    // #endif
    //
    //    IfFailedThrowHR(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
    //    createDeviceFlags,
    //                    featureLevelArray, 2, D3D11_SDK_VERSION, device.GetAddressOf(),
    //                    &featureLevel, nullptr));
}

void CEffectProcessor::CreateCompatibleResource(ID3D11ShaderResourceView *texView,
                                               ID3D11Resource *result)
{
    //
}

void CEffectProcessor::GetTextureDesc(ID3D11ShaderResourceView *texView, D3D11_TEXTURE2D_DESC *tex2d)
{
    Microsoft::WRL::ComPtr<ID3D11Resource> resource;
    texView->GetResource(resource.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    resource->QueryInterface(IID_PPV_ARGS(&texture));
    texture->GetDesc(tex2d);
}

void CEffectProcessor::ApplyEffect(ID3D11ShaderResourceView *texView,
                                  DirectX::BasicPostProcess::Effect effect, ID3D11Texture2D *result,
                                  ID3D11PixelShader *customShader)
{
    // postProcess->SetSourceTexture needs ID3D11ShaderResourceView...maybe can pass texture and
    // create one, but eh
    auto device = pDX11->GetDevice().Get();
    auto dc = pDX11->GetDeviceContext().Get();

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtView;
    device->CreateRenderTargetView(result, nullptr, rtView.GetAddressOf());

    SaveState();
    {
        dc->OMSetRenderTargets(1, rtView.GetAddressOf(), nullptr);
        std::unique_ptr<DirectX::BasicPostProcess> postProcess;
        postProcess = std::make_unique<DirectX::BasicPostProcess>(device);
        postProcess->SetSourceTexture(texView);
        postProcess->SetEffect(effect);
        postProcess->Process(dc, [dc, customShader]() {
            if (customShader) dc->PSSetShader(customShader, nullptr, 0);
        });
    }
    RestoreState();
}

void CEffectProcessor::Fade(ID3D11ShaderResourceView *texView)
{
    // create renderable texture
    D3D11_TEXTURE2D_DESC tex2d;
    GetTextureDesc(texView, &tex2d);
    tex2d.BindFlags |= D3D11_BIND_RENDER_TARGET;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> target;
    pDX11->CreateTexture(&tex2d, target);

    if (!modalFadeShader)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
        IfFailedThrowHR(RetailModalFade::Compile(bytecode.GetAddressOf()));
        IfFailedThrowHR(pDX11->GetDevice()->CreatePixelShader(bytecode->GetBufferPointer(),
            bytecode->GetBufferSize(), nullptr, modalFadeShader.GetAddressOf()));
    }
    ApplyEffect(texView, DirectX::BasicPostProcess::Effect::Copy, target.Get(),
                modalFadeShader.Get());

    Microsoft::WRL::ComPtr<ID3D11Resource> resource;
    texView->GetResource(resource.GetAddressOf());
    auto dc = pDX11->GetDeviceContext().Get();
    dc->CopyResource(resource.Get(), target.Get());
}
void CEffectProcessor::StretchTo(ID3D11ShaderResourceView *texView,
                                ID3D11ShaderResourceView *resultView, const RECT& rect)
{
    // create renderable texture
    D3D11_TEXTURE2D_DESC tex2d;
    GetTextureDesc(resultView, &tex2d);
    tex2d.BindFlags |= D3D11_BIND_RENDER_TARGET;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> target;
    pDX11->CreateTexture(&tex2d, target);

    D3D11_VIEWPORT oldviewport;
    UINT numViewports = 1;

    auto device = pDX11->GetDevice().Get();
    auto dc = pDX11->GetDeviceContext().Get();

    dc->RSGetViewports(&numViewports, &oldviewport);

    D3D11_VIEWPORT viewport;
    ZeroMemory(&viewport, sizeof(D3D11_VIEWPORT));
    viewport.TopLeftX = rect.left;
    viewport.TopLeftY = rect.top;
    viewport.Width = rect.right;
    viewport.Height = rect.bottom;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    // Set the Viewport
    dc->RSSetViewports(1, &viewport);
    ApplyEffect(texView, DirectX::BasicPostProcess::Effect::Copy, target.Get());
    dc->RSSetViewports(numViewports, &oldviewport);

    Microsoft::WRL::ComPtr<ID3D11Resource> result;
    resultView->GetResource(result.GetAddressOf());
    dc->CopyResource(result.Get(), target.Get());
};