#pragma once
#include <d3d.h>
#include <d3d11.h>
#include <cstdint>
namespace owned {
struct TextureSampler { D3D11_SAMPLER_DESC description{}; std::uint64_t key=0; };
inline TextureSampler world_texture_sampler(DWORD /*min_filter*/,DWORD /*mag_filter*/,DWORD mip_filter,
    DWORD address_u,DWORD address_v,bool world,UINT anisotropy) {
    TextureSampler result;auto& d=result.description;
    // Renderer filtering owns every game color texture, including interface
    // previews and images. Keep the existing world/interface mip limits.
    d.Filter=anisotropy>1?D3D11_FILTER_ANISOTROPIC:D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    d.MaxAnisotropy=anisotropy;
    d.MaxLOD=world||mip_filter!=D3DTFP_NONE?D3D11_FLOAT32_MAX:0.f;
    auto address=[](DWORD value){switch(value){case D3DTADDRESS_MIRROR:return D3D11_TEXTURE_ADDRESS_MIRROR;
        case D3DTADDRESS_CLAMP:return D3D11_TEXTURE_ADDRESS_CLAMP;
        case D3DTADDRESS_BORDER:return D3D11_TEXTURE_ADDRESS_BORDER; // transparent black (a cropped camp icon)
        default:return D3D11_TEXTURE_ADDRESS_WRAP;}};
    d.AddressU=address(address_u);d.AddressV=address(address_v);d.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    result.key=(d.MaxLOD==0?1ull<<63:0)|(std::uint64_t(d.MaxAnisotropy)<<48)|
        (std::uint64_t(d.Filter)<<32)|(std::uint64_t(address_u)<<16)|address_v;
    return result;
}
}
