#pragma once
#include "skn_runtime.h"
#include "gltf_physique_pose.h"
#include <DirectXPackedVector.h>

// Persistent appearance controls. The keyed targets stay in CPU asset storage;
// the merged vertex buffer is rebuilt only when the effective shape changes.
namespace owned::gltf {
struct PhysiqueDelta {
    std::uint16_t target;
    std::array<std::uint16_t,9> half;
};
struct Physique {
    struct Joint {unsigned node,carrier;int parent;};
    std::vector<std::string> targets;
    std::array<std::vector<float>,5> axes;
    std::vector<Joint> joints;
    unsigned rig_rank=0,root=0;
    std::vector<float> mesh_coefficients,rig_coefficients,rig_basis;
    std::vector<unsigned> offsets;
    std::vector<PhysiqueDelta> deltas;
    std::shared_ptr<const PhysiquePose> resolve(PhysiqueInput input,unsigned nodes,const std::string& assetKey) const {
        if(targets.empty())return {};
        for(unsigned i=0;i<5;++i){
            animation_require(std::isfinite(input[i]),"nonfinite physique input");
            input[i]=(std::clamp)(input[i],axes[i].front(),axes[i].back());
        }
        if(input==neutral_physique)return {};
        std::array<unsigned,32> ids{};
        std::array<float,32> factors{};factors[0]=1;
        unsigned count=1;
        for(unsigned i=0;i<5;++i){
            const auto& axis=axes[i];
            const unsigned hi=(std::clamp)(unsigned(std::lower_bound(axis.begin(),axis.end(),input[i])-axis.begin()),1u,unsigned(axis.size()-1));
            const float t=(input[i]-axis[hi-1])/(axis[hi]-axis[hi-1]);
            for(unsigned k=0;k<count;++k){
                ids[k]=ids[k]*unsigned(axis.size())+hi-1;ids[count+k]=ids[k]+1;
                factors[count+k]=factors[k]*t;factors[k]*=1-t;
            }
            count*=2;
        }
        auto pose=std::make_shared<PhysiquePose>();
        pose->input=input;pose->weights.resize(targets.size());
        std::vector<float> coefficients(rig_rank);
        for(unsigned k=0;k<count;++k)if(factors[k]){
            for(unsigned t=0;t<targets.size();++t)pose->weights[t]+=factors[k]*mesh_coefficients[ids[k]*targets.size()+t];
            for(unsigned t=0;t<rig_rank;++t)coefficients[t]+=factors[k]*rig_coefficients[ids[k]*rig_rank+t];
        }
        const auto n=joints.size(),stride=24*n+1;
        std::vector<float> values(stride);
        for(unsigned t=0;t<rig_rank;++t)if(coefficients[t])
            for(size_t k=0;k<stride;++k)values[k]+=coefficients[t]*rig_basis[t*stride+k];
        pose->locals.resize(nodes);
        pose->inverse_bind_delta.assign(nodes,physique_identity);
        std::vector<Affine> turns(n,physique_identity);
        for(size_t i=0;i<n;++i)for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
            turns[i][r*4+c]+=values[i*9+r*3+c];
        const auto set=[&](unsigned node,unsigned mode,const Affine& linear,const float* offset){
            auto& row=pose->locals.at(node);
            row.mode=mode;
            std::copy_n(linear.begin(),12,row.rows.begin());
            for(unsigned j=0;j<3;++j)row.rows[j*4+3]=offset?offset[j]:0;
        };
        for(size_t i=0;i<n;++i){
            const auto& j=joints[i];
            const auto pre=j.parent<0?physique_identity:physique_inverse(turns[j.parent]);
            set(j.carrier,1,pre,values.data()+9*n+3*i);
            set(j.node,2,turns[i],nullptr);
            Affine forward=physique_identity;
            for(unsigned k=0;k<12;++k)forward[k]+=values[12*n+12*i+k];
            pose->inverse_bind_delta[j.node]=physique_inverse(forward);
        }
        Affine scale=physique_identity;
        scale[0]=scale[5]=scale[10]=1+values.back();
        set(root,2,scale,nullptr);
        auto gpu=std::make_shared<GpuProgram>();
        gpu->key="physique|"+assetKey+"|";
        gpu->key.append(reinterpret_cast<const char*>(input.data()),sizeof(input));
        gpu->nodes=nodes;
        gpu->bytes.resize(16+nodes*sizeof(PhysiqueLocal));
        std::memcpy(gpu->bytes.data()+16,pose->locals.data(),nodes*sizeof(PhysiqueLocal));
        pose->gpu=std::move(gpu);
        return pose;
    }
    void apply(unsigned vertex,const std::vector<float>& weights,skn::Vertex& v) const {
        if(weights.empty())return;
        std::array<float,9> value{};
        for(unsigned i=offsets.at(vertex);i<offsets.at(vertex+1);++i) {
            const auto& delta=deltas[i];
            const float w=weights[delta.target];
            if(w)for(unsigned c=0;c<9;++c)value[c]+=w*DirectX::PackedVector::XMConvertHalfToFloat(delta.half[c]);
        }
        std::array<float,3> normal{},tangent{};
        float ns=0,tn=0;
        for(unsigned c=0;c<3;++c) {
            v.position[c]+=value[c];
            normal[c]=v.normal[c]/32767.f+value[c+3];
            tangent[c]=v.tangent[c]/32767.f+value[c+6];
            ns+=normal[c]*normal[c];tn+=normal[c]*tangent[c];
        }
        float ts=0;
        if(!(ns>1e-12f))throw std::runtime_error("glTF invalid physique normal");
        for(unsigned c=0;c<3;++c){tangent[c]-=tn/ns*normal[c];ts+=tangent[c]*tangent[c];}
        if(!(ts>1e-12f))throw std::runtime_error("glTF invalid physique tangent");
        const float normalScale=32767.f/std::sqrt(ns),tangentScale=32767.f/std::sqrt(ts);
        for(unsigned c=0;c<3;++c){
            v.normal[c]=static_cast<short>(std::lround(normal[c]*normalScale));
            v.tangent[c]=static_cast<short>(std::lround(tangent[c]*tangentScale));
        }
    }
};
}
