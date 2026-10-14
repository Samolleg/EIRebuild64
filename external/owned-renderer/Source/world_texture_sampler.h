#pragma once
#include <d3d.h>
#include <d3d11.h>
#include <cstdint>
namespace owned {
struct TextureSampler { D3D11_SAMPLER_DESC description{}; std::uint64_t key=0; };
inline TextureSampler world_texture_sampler(DWORD min_filter,DWORD mag_filter,DWORD mip_filter,
    DWORD address_u,DWORD address_v,bool world,UINT anisotropy) {
    const bool min_linear=min_filter!=D3DTFN_POINT,mag_linear=mag_filter!=D3DTFG_POINT;
    const bool mip_linear=mip_filter==D3DTFP_LINEAR;
    TextureSampler result;auto& d=result.description;
    // D3D11 encodes min, mag and mip linear selection in bits 4, 2 and 0.
    d.Filter=static_cast<D3D11_FILTER>((min_linear?0x10:0)|(mag_linear?0x4:0)|(mip_linear?1:0));
    // Preserve the existing UI translation, including its both-point/mip-linear case.
    if(!world&&!min_linear&&!mag_linear&&mip_linear)d.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    d.MaxLOD=mip_filter==D3DTFP_NONE?0.f:D3D11_FLOAT32_MAX;
    d.MaxAnisotropy=1;
    if(world){d.Filter=anisotropy>1?D3D11_FILTER_ANISOTROPIC:D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        d.MaxAnisotropy=anisotropy;d.MaxLOD=D3D11_FLOAT32_MAX;}
    auto address=[](DWORD value){switch(value){case D3DTADDRESS_MIRROR:return D3D11_TEXTURE_ADDRESS_MIRROR;
        case D3DTADDRESS_CLAMP:return D3D11_TEXTURE_ADDRESS_CLAMP;
        default:return D3D11_TEXTURE_ADDRESS_WRAP;}};
    d.AddressU=address(address_u);d.AddressV=address(address_v);d.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    result.key=(d.MaxLOD==0?1ull<<63:0)|(std::uint64_t(d.MaxAnisotropy)<<48)|
        (std::uint64_t(d.Filter)<<32)|(std::uint64_t(address_u)<<16)|address_v;
    return result;
}
}
