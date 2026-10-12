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

};
