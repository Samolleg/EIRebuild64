#pragma once
#include <d3dcompiler.h>
#include <cstring>
#pragma comment(lib, "d3dcompiler.lib")

// Retail RGB32 modal Fade dims as well as desaturating. Keep the integer
// expression shared by the scalar inspector and the production pixel shader.
#define EI_MODAL_FADE_GRAY(r, g, b) ((2u * (r) + 4u * (g) + (b)) >> 4u)
#define EI_MODAL_FADE_STRING_INNER(x) #x
#define EI_MODAL_FADE_STRING(x) EI_MODAL_FADE_STRING_INNER(x)
namespace RetailModalFade {
inline unsigned Gray(unsigned r, unsigned g, unsigned b) {
    return EI_MODAL_FADE_GRAY(r, g, b);
}
inline HRESULT Compile(ID3DBlob** bytecode) {
    static const char code[] =
        "Texture2D image : register(t0);\n"
        "float4 main(float4 position : SV_Position) : SV_Target {\n"
        " float4 pixel = image.Load(int3(int2(position.xy), 0));\n"
        " uint3 rgb = (uint3)round(saturate(pixel.rgb) * 255.0);\n"
        " uint gray = " EI_MODAL_FADE_STRING(EI_MODAL_FADE_GRAY(rgb.r, rgb.g, rgb.b)) ";\n"
        " return float4(float3(gray, gray, gray) / 255.0, pixel.a);\n"
        "}\n";
    return D3DCompile(code, std::strlen(code), "RetailModalFade", nullptr,
        nullptr, "main", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        bytecode, nullptr);
}
}
#undef EI_MODAL_FADE_STRING
#undef EI_MODAL_FADE_STRING_INNER
#undef EI_MODAL_FADE_GRAY
