#pragma once

enum class EVertexType
{
    Word = 0,
    STL = 1,
    PNT = 2
};

struct STLVertex2
{
    DirectX::XMFLOAT4 pos;
    DWORD color;
    DWORD specular;
    DirectX::XMFLOAT2 tuv;
};

struct STLVertex1
{
public:
    float sx;       // transformed 'screen' x-coord
    float sy;       // transformed 'screen' y-coord
    float sz;       // transformed 'screen = ZBuffer' x-coord
    float rhw;      // reciprocal homogeneous w
    DWORD color;    // diffuse color component (in ARGB format)
    DWORD specular; // specular color component (in FRGB format)
    float tu;       // texture map u-coord
    float tv;       // texture map v-coord
};

struct PNTVertex
{
    DirectX::XMFLOAT4 pos;
    DirectX::XMFLOAT4 normal;
    DWORD color;
    DWORD specular;
    DirectX::XMFLOAT2 tuv;
    DWORD useVertexColor;
};

typedef CSimplyArray<STLVertex1, STLVertex1> CTLVertex1Array;