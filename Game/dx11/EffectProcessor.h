#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include "PostProcess.h"

class CDX11;

class CEffectProcessor
{
    CDX11 *pDX11;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> curView;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> curStencilView;
    void SaveState();
    void RestoreState();
    void GetTextureDesc(ID3D11ShaderResourceView *texView, D3D11_TEXTURE2D_DESC *tex2d);
    void CreateCompatibleResource(ID3D11ShaderResourceView *texView, ID3D11Resource *result);
    void ApplyEffect(ID3D11ShaderResourceView *texView, DirectX::BasicPostProcess::Effect effect,
                     ID3D11Texture2D *result);

public:
    CEffectProcessor(CDX11 *pDXRenderer);
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> GetDeviceContext() { return deviceContext; };
    Microsoft::WRL::ComPtr<ID3D11Device> GetDevice() { return device; };
    void Fade(ID3D11ShaderResourceView *texView);
    void StretchTo(ID3D11ShaderResourceView *texView, ID3D11ShaderResourceView *resultView,
                   const RECT &rect);
};