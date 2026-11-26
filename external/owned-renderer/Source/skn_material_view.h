#pragma once
#include <d3d.h>
#include "skn_runtime.h"

namespace owned::skn {
inline void apply_material_view(NativeGeometryView& view, const Material& material,
    const std::array<Dds,4>& maps, const Dds& texture, const Dds* ormOverride,
    const void* materialIdentity) {
    view.owned_texture.identity=1;view.owned_texture.generation=1;view.owned_texture.value_source=1;
    if(!texture.mips.empty()){
     view.owned_texture.resource=&texture.blob.identity;view.owned_texture.format=texture.format;
     view.owned_texture.mips=texture.mips.data();view.owned_texture.mip_count=static_cast<UINT>(texture.mips.size());
    }else{
     static const std::uint32_t white=0xffffffffu;
     static const NativeOwnedTextureMip mip{reinterpret_cast<const std::uint8_t*>(&white),1,1,4};
     view.owned_texture.identity=0x534b4e;view.owned_texture.value_source=2;
     view.owned_texture.format=NativeOwnedTextureFormat::argb8888;view.owned_texture.mips=&mip;view.owned_texture.mip_count=1;
    }
    auto& skn_material=view.skn_material;
    // The immutable asset and material index are stable across poses.
    skn_material.identity=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
        materialIdentity ? materialIdentity : static_cast<const void*>(&material)));
    skn_material.factors[0]=material.roughness;skn_material.factors[1]=material.metallic;
    skn_material.factors[2]=material.ao;skn_material.factors[3]=material.two_sided?2.f:1.f;
    std::copy_n(material.emission,3,skn_material.emission);
    skn_material.emission[3]=material.unlit?1.f:0.f;
    std::copy_n(material.base,4,skn_material.base);
    skn_material.base[3]=static_cast<float>(material.alpha);
    if(material.alpha==0)view.owned_texture.opaque_mips=UINT_MAX;
    for(unsigned k=0;k<3;++k){
     const auto& src=k==1 && ormOverride ?
         *ormOverride : maps[k+1];
     auto& dst=skn_material.maps[k];if(src.mips.empty())continue;
     dst.identity=1;dst.generation=1;dst.value_source=1;
     dst.resource=&src.blob.identity;dst.format=src.format;
     dst.mips=src.mips.data();dst.mip_count=static_cast<UINT>(src.mips.size());
    }
    view.owned_fixed_state.source_blend=D3DBLEND_SRCALPHA;
    view.owned_fixed_state.destination_blend=D3DBLEND_INVSRCALPHA;
    view.owned_fixed_state.z_write_enable=material.alpha!=2;
    view.owned_fixed_state.alpha_func=D3DCMP_GREATEREQUAL;
    view.owned_fixed_state.cull_mode=material.two_sided?D3DCULL_NONE:D3DCULL_CW;
    view.owned_fixed_state.alpha_test_enable=material.alpha==1;
    view.owned_fixed_state.alpha_ref=static_cast<UINT>(material.cutoff*255);
    view.owned_fixed_state.alpha_blend_enable=material.alpha==2;
}
} // namespace owned::skn
