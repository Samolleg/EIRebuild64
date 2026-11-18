#pragma once
#include "gltf_animation.h"
#include <DirectXMath.h>
#include <memory>

namespace owned::gltf {
using PhysiqueInput = std::array<float,5>;
inline constexpr PhysiqueInput neutral_physique{25,25,.3f,.3f,.85f};
inline constexpr Affine physique_identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
inline Affine physique_inverse(const Affine& a) {
    DirectX::XMFLOAT4X4 in,out;
    std::memcpy(&in,a.data(),sizeof(in));
    DirectX::XMVECTOR det;
    auto m=DirectX::XMMatrixInverse(&det,DirectX::XMLoadFloat4x4(&in));
    const float d=DirectX::XMVectorGetX(det);
    animation_require(std::isfinite(d)&&d>1e-9f,"singular physique transform");
    DirectX::XMStoreFloat4x4(&out,m);
    Affine result;std::memcpy(result.data(),&out,sizeof(out));
    return result;
}
struct PhysiqueLocal {
    // Three affine rows followed by the transform mode (0 unchanged,
    // 1 pre-rotate, 2 post-rotate). The offset is added to local translation.
    std::array<float,12> rows{};
    std::uint32_t mode=0;
};
static_assert(sizeof(PhysiqueLocal)==52);
struct PhysiquePose {
    PhysiqueInput input{};
    std::shared_ptr<const GpuProgram> gpu;
    std::vector<float> weights;
    std::vector<PhysiqueLocal> locals;
    std::vector<Affine> inverse_bind_delta;
    Affine apply(unsigned node,Affine local) const {
        const auto& change=locals.at(node);
        if(!change.mode)return local;
        Affine linear=physique_identity;
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)linear[r*4+c]=change.rows[r*4+c];
        auto result=change.mode==1?multiply(linear,local):multiply(local,linear);
        for(unsigned r=0;r<3;++r)result[r*4+3]=local[r*4+3]+change.rows[r*4+3];
        return result;
    }
};
}
