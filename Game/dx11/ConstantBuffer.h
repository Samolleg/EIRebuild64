#pragma once
#include <d3d11.h>
#include <wrl/client.h>

template <class T>
class ConstantBuffer
{
private:
    ConstantBuffer(const ConstantBuffer<T> &rhs);

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    ID3D11DeviceContext *deviceContext = nullptr;

public:
    ConstantBuffer() {}

    T data;

    ID3D11Buffer *Get() const { return buffer.Get(); }

    ID3D11Buffer *const *GetAddressOf() const { return buffer.GetAddressOf(); }

    HRESULT Initialize(ID3D11Device *device, ID3D11DeviceContext *deviceContext);
    bool ApplyChanges();
    bool TestConstantBuffer(LPCVOID *pShader, int slotNo);
};

template <class T>
HRESULT ConstantBuffer<T>::Initialize(ID3D11Device *device, ID3D11DeviceContext *deviceContext)
{
    this->deviceContext = deviceContext;

    D3D11_BUFFER_DESC desc;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = 0;
    desc.ByteWidth = static_cast<UINT>(sizeof(T) + (16 - (sizeof(T) % 16)));
    desc.StructureByteStride = 0;

    HRESULT hr = device->CreateBuffer(&desc, 0, buffer.ReleaseAndGetAddressOf());
    return hr;
}

template <class T>
bool ConstantBuffer<T>::ApplyChanges()
{
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = this->deviceContext->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                                          &mappedResource);
    if (FAILED(hr))
    {
        ReportError(hr, "Failed to map constant buffer.");
        return false;
    }
    CopyMemory(mappedResource.pData, &data, sizeof(T));
    this->deviceContext->Unmap(buffer.Get(), 0);
    return true;
}

template <class T>
bool ConstantBuffer<T>::TestConstantBuffer(LPCVOID *pShader, int slotNo)
{
    /*D3DReflect(pPixelShaderBuffer->GetBufferPointer(), pPixelShaderBuffer->GetBufferSize(),
               IID_ID3D11ShaderReflection, (void **)&pReflector);*/
    return false;
}

// D3DVALUE = float = 4
// D3DCOLORVALUE = vec4 = 16
// CPlot3D = D3DVECTOR = vec3 ...

struct CLight11
{
    D3DLIGHT7 light;                  // structure defining the light
    float fpAddHeight;                // additional height
    float range2;                     // range*range
    float rcpr_range2;                // 1.0f/range2
    CPlot3D rd;                       // respect direction
    D3DCOLORVALUE dcMaterialDiffuse;  // light color * material diffuse reflection
    D3DCOLORVALUE dcMaterialSpecular; // light color * material specular reflection
};
// sizeof(D3DLIGHT7);

class CLight;
struct CLight11Packed
{
    CPlot3D rd;
    float rcpr_range2;
    D3DCOLORVALUE dcMaterialDiffuse;
    CPlot3D respectCP;
    float junk;
    CPlot3D position;
    float junk2;
    CPlot3D direction;
    float junk3;

    CLight11Packed() = default;   
    CLight11Packed(CLight &src);
};

constexpr int lightSize = sizeof(CLight11Packed);

struct CB_VS_vertexshader
{
    DirectX::XMMATRIX camera;
    DirectX::XMMATRIX projection;
    DirectX::XMMATRIX clip;
    DirectX::XMMATRIX translation;
    DirectX::XMMATRIX scale;
    DirectX::XMMATRIX translation2;
    DirectX::XMMATRIX camera_proj_clip;
    DirectX::XMMATRIX ei_projspace;
    DirectX::XMMATRIX ei_scrnspace;
    DirectX::XMMATRIX model_to_world;
    D3DCOLORVALUE dcvAmbient;
    DWORD nlights;
    D3DVECTOR dvEmissive;
    DirectX::XMFLOAT3 cameraPosition;
};

struct CB_VS_vertexlights
{
    CLight11Packed sunlight;
    CLight11Packed lights[16];
};

enum class EPS_AlphaMode
{
    am_texture = 0,
    am_multiply = 1,
    am_vertex = 2
};

struct CB_PS_pixelshader
{
    DirectX::XMINT4 isTexture[4];
    DirectX::XMFLOAT4 fogColor;
    float alphaRef;
    uint32_t alphaMode;
    uint32_t useSpecular;
};

DirectX::XMMATRIX sh_to_xm(SHMatrix mat);
DirectX::XMMATRIX sh_to_xm(SHMatrix &&mat);

class CConstantBuffers
{
public:
    ConstantBuffer<CB_VS_vertexshader> cb_vs_vertexshader;
    ConstantBuffer<CB_VS_vertexlights> cb_vs_vertexlights;
    ConstantBuffer<CB_PS_pixelshader> cb_ps_pixelshader;

    bool Initialize(Microsoft::WRL::ComPtr<ID3D11Device> &device,
                    Microsoft::WRL::ComPtr<ID3D11DeviceContext> &deviceContext);
};