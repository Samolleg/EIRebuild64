#pragma once
#include "gltf_physique.h"
#include <numeric>

namespace owned::gltf {
inline Affine physique_transpose(const Affine& m){
    Affine t;
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)t[r*4+c]=m[c*4+r];
    return t;
}
struct GarmentAnchor {
    std::array<unsigned,4> vertex{};
    std::array<float,4> weights{};
};
// One read-only search tree per body, built once. The resulting garment map
// depends only on the two rest meshes and survives stat changes.
struct PhysiqueTree {
    std::vector<unsigned> order;
    void build(const std::vector<skn::Vertex>& vertices,size_t lo,size_t hi,unsigned axis) {
        if(lo>=hi)return;
        const size_t mid=lo+(hi-lo)/2;
        std::nth_element(order.begin()+lo,order.begin()+mid,order.begin()+hi,[&](unsigned a,unsigned b){
            return vertices[a].position[axis]<vertices[b].position[axis];});
        build(vertices,lo,mid,(axis+1)%3);build(vertices,mid+1,hi,(axis+1)%3);
    }
    void initialize(const std::vector<skn::Vertex>& vertices){
        order.resize(vertices.size());std::iota(order.begin(),order.end(),0u);build(vertices,0,order.size(),0);
    }
    void nearest(const std::vector<skn::Vertex>& vertices,const float* p,size_t lo,size_t hi,unsigned axis,
                 std::array<float,4>& ds,std::array<unsigned,4>& ids) const {
        if(lo>=hi)return;
        const size_t mid=lo+(hi-lo)/2;const auto id=order[mid];
        const auto& v=vertices[id];const float plane=p[axis]-v.position[axis];
        float d=0;for(unsigned c=0;c<3;++c){const float x=p[c]-v.position[c];d+=x*x;}
        if(d<ds.back()){
            unsigned i=3;while(i && d<ds[i-1]){ds[i]=ds[i-1];ids[i]=ids[i-1];--i;}
            ds[i]=d;ids[i]=id;
        }
        const auto next=(axis+1)%3;
        if(plane<0){
            nearest(vertices,p,lo,mid,next,ds,ids);
            if(plane*plane<=ds.back())nearest(vertices,p,mid+1,hi,next,ds,ids);
        }else{
            nearest(vertices,p,mid+1,hi,next,ds,ids);
            if(plane*plane<=ds.back())nearest(vertices,p,lo,mid,next,ds,ids);
        }
    }
    std::vector<GarmentAnchor> map(const std::vector<skn::Vertex>& body,const std::vector<skn::Vertex>& item) const {
        std::vector<GarmentAnchor> result(item.size());
        for(size_t i=0;i<item.size();++i){
            std::array<float,4> ds;ds.fill((std::numeric_limits<float>::max)());
            auto& a=result[i];nearest(body,item[i].position,0,order.size(),0,ds,a.vertex);
            float sum=0;for(unsigned j=0;j<4;++j){a.weights[j]=1.f/(ds[j]+1e-8f);sum+=a.weights[j];}
            for(float& w:a.weights)w/=sum;
        }
        return result;
    }
};
struct PhysiqueSurface {
    std::vector<skn::Vertex> body;
    std::vector<std::array<float,3>> garment_delta;
    std::vector<skn::Matrix> inverse_skin;
    void apply_garment(const GarmentAnchor& anchor,skn::Vertex& v) const {
        for(unsigned k=0;k<4;++k)for(unsigned c=0;c<3;++c)
            v.position[c]+=anchor.weights[k]*garment_delta[anchor.vertex[k]][c];
    }
    void build(const Physique& shape,const PhysiquePose& pose,const std::vector<skn::Vertex>& rest,
               const std::vector<skn::Joint>& joints){
        body=rest;garment_delta.resize(rest.size());inverse_skin.resize(joints.size());
        std::vector<Affine> forward(joints.size());
        for(size_t i=0;i<joints.size();++i){
            const auto inv=physique_transpose(joints[i].inverse);
            const auto changed=multiply(pose.inverse_bind_delta[i],inv);
            inverse_skin[i]=physique_transpose(changed);
            forward[i]=multiply(physique_inverse(changed),inv);
        }
        for(size_t i=0;i<body.size();++i){
            auto& v=body[i];shape.apply(unsigned(i),pose.weights,v);
            Affine blend{};
            for(unsigned k=0;k<4;++k)if(v.weights[k]){
                const auto& m=forward[v.joints[k]];const float w=v.weights[k]/65535.f;
                for(unsigned c=0;c<12;++c)blend[c]+=w*m[c];
            }
            blend[15]=1.f;
            const auto inv=physique_inverse(blend);
            for(unsigned r=0;r<3;++r)garment_delta[i][r]=inv[r*4]*v.position[0]+inv[r*4+1]*v.position[1]+
                inv[r*4+2]*v.position[2]+inv[r*4+3]-rest[i].position[r];
        }
    }
};
}
