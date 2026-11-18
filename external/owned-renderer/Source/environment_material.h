#pragma once
#include "owned_asset_loader.h"
struct cgltf_material;
struct cgltf_data;
namespace owned::environment {
struct Texture {
    std::shared_ptr<const TextureCpuAsset> asset;
    std::vector<NativeOwnedTextureMip> mips;
    NativeOwnedTextureView view() const;
};
struct Material {
    std::array<std::shared_ptr<const Texture>,4> textures; // base, normal, ORM, emission
    std::array<float,4> base{1,1,1,1}, emission{};
    float roughness=1, metallic=1, cutoff=.5f, alpha_factor=1;
    unsigned alpha=0;
    bool two_sided=false, unlit=false;
    unsigned uv_set=0,address_u=1,address_v=1;
    std::array<float,6> uv{1,0,0,0,1,0};
    NativeSknMaterial native() const;
};
// Own immutable texture bytes; no host texture surface or character dependency.
std::shared_ptr<const Material> material(const cgltf_material* input,const ResourceIdentity& identity,const cgltf_data& document);
}
