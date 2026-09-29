#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <SpriteBatch.h>
#include <CommonStates.h>
#pragma comment(lib, "d3dcompiler.lib")

// Retail UI blits replace RGB except the exact color key. GDI's unused alpha
// channel must not turn the text into an additive premultiplied sprite.
class UIColorKeyBlit {
    Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> keyBuffer;

    HRESULT Initialize(ID3D11Device* device) {
        static const char code[] = R"(
Texture2D image : register(t0);
SamplerState imageSampler : register(s0);
cbuffer Key : register(b0) { float4 colorKey; };
float4 main(float4 color : COLOR0, float2 uv : TEXCOORD0) : SV_Target {
    float3 rgb = image.Sample(imageSampler, uv).rgb;
    if (all(abs(rgb - colorKey.rgb) < (0.5 / 255.0))) discard;
    return float4(rgb, 1.0) * color;
}
)";
        Microsoft::WRL::ComPtr<ID3DBlob> bytecode, errors;
        HRESULT result = D3DCompile(code, std::strlen(code), "UIColorKeyBlit",
            nullptr, nullptr, "main", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0, bytecode.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(result)) return result;
        result = device->CreatePixelShader(bytecode->GetBufferPointer(),
            bytecode->GetBufferSize(), nullptr, shader.ReleaseAndGetAddressOf());
        if (FAILED(result)) return result;
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = 16;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return device->CreateBuffer(&desc, nullptr, keyBuffer.GetAddressOf());
    }
public:
    // The caller has an active UI SpriteBatch. Keep subsequent UI draws in it.
    HRESULT Draw(ID3D11DeviceContext* context, DirectX::SpriteBatch* batch,
                 DirectX::CommonStates* states, ID3D11ShaderResourceView* image,
                 const RECT& destination, const RECT* source, COLORREF key) {
        if (!keyBuffer) {
            Microsoft::WRL::ComPtr<ID3D11Device> device;
            context->GetDevice(device.GetAddressOf());
            HRESULT result = Initialize(device.Get());
            if (FAILED(result)) return result;
        }
        batch->End();
        const float color[4] = { GetRValue(key) / 255.f, GetGValue(key) / 255.f,
                                GetBValue(key) / 255.f, 0.f };
        context->UpdateSubresource(keyBuffer.Get(), 0, nullptr, color, 0, 0);
        Microsoft::WRL::ComPtr<ID3D11Buffer> previous;
        context->PSGetConstantBuffers(0, 1, previous.GetAddressOf());
        batch->Begin(DirectX::SpriteSortMode_Immediate, states->Opaque(),
            states->PointClamp(), nullptr, nullptr, [this, context]() {
                ID3D11Buffer* buffer = keyBuffer.Get();
                context->PSSetShader(shader.Get(), nullptr, 0);
                context->PSSetConstantBuffers(0, 1, &buffer);
            });
        batch->Draw(image, destination, source);
        batch->End();
        ID3D11Buffer* restore = previous.Get();
        context->PSSetConstantBuffers(0, 1, &restore);
        batch->Begin();
        return S_OK;
    }
};
