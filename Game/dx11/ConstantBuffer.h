#pragma once
#include <d3d11.h>

template <class T>
class ConstantBuffer
{
public:
    T data{};
};

// D3DVALUE = float = 4
// D3DCOLORVALUE = vec4 = 16
// CPlot3D = D3DVECTOR = vec3 ...

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


struct CB_VS_vertexshader
{
    DirectX::XMMATRIX ei_projspace;
    D3DCOLORVALUE dcvAmbient;
    DWORD nlights;
    D3DVECTOR dvEmissive;
};

struct CB_VS_vertexlights
{
    CLight11Packed sunlight;
    CLight11Packed lights[16];
};

struct CB_PS_pixelshader
{
    DirectX::XMFLOAT4 fogColor;
    uint32_t alphaMode;
};

DirectX::XMMATRIX sh_to_xm(SHMatrix mat);
DirectX::XMMATRIX sh_to_xm(SHMatrix &&mat);

class CConstantBuffers
{
public:
    ConstantBuffer<CB_VS_vertexshader> cb_vs_vertexshader;
    ConstantBuffer<CB_VS_vertexlights> cb_vs_vertexlights;
    ConstantBuffer<CB_PS_pixelshader> cb_ps_pixelshader;

};
