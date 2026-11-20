#pragma once
#include "CampItemFit.h"
#include "skn_asset.h"

// Put sleeves into the neutral hanging position of the original inventory art.
// Only the inventory copy of a skinned mesh is posed, around its bind shoulders.
inline std::vector<owned::skn::Vertex> camp_sleeves(const owned::gltf::Asset& asset,float degrees) {
    using namespace CampItemFit;
    if(!asset.wearable || degrees==0) return {};
    const auto& joints=asset.geometry.joints;
    std::vector<owned::skn::Matrix> world(joints.size());
    std::vector<int> arm(joints.size(),-1);
    std::vector<float> influence(joints.size(),1.f);
    Point shoulder[2]{};
    bool present[2]{};
    for(unsigned i=0;i<joints.size();++i) {
        const auto& j=joints[i];
        world[i]=owned::skn::trs(j);
        if(j.parent>=0) {
            world[i]=owned::skn::multiply(world[j.parent],world[i]);
            arm[i]=arm[j.parent];
            influence[i]=influence[j.parent];
        }
        for(int side=0;side<2;++side) {
            const char* upper=side?"CC_Base_Upperarm.L":"CC_Base_Upperarm.R";
            const char* clavicle=side?"DEF_Clavicle.L":"DEF_Clavicle.R";
            if(j.name==upper) {
                arm[i]=side;influence[i]=1;
                shoulder[side]={world[i][12],world[i][13],world[i][14]};
                present[side]=true;
            } else if(j.name==clavicle) {arm[i]=side;influence[i]=.3f;}
        }
    }
    if(!present[0] && !present[1]) return {};
    auto vertices=asset.rest_vertices;
    const float radians=degrees*.01745329252f;
    const Basis rotations[2]={roll(radians),roll(-radians)};
    for(auto& v:vertices) {
        float weights[2]{};
        for(unsigned k=0;k<4;++k) if(v.weights[k]) {
            const unsigned joint=v.joints[k];
            if(arm[joint]>=0 && present[arm[joint]])
                weights[arm[joint]]+=influence[joint]*(float(v.weights[k])/65535.f);
        }
        if(weights[0]+weights[1]==0) continue;
        const Point p{v.position[0],v.position[1],v.position[2]},n{float(v.normal[0]),float(v.normal[1]),float(v.normal[2])};
        Point normal=n;
        for(unsigned side=0;side<2;++side) if(weights[side]>0) {
            const auto local=delta(p,shoulder[side]);
            const auto offset=delta(rotate(rotations[side],local),local);
            const auto turn=delta(rotate(rotations[side],n),n);
            for(unsigned k=0;k<3;++k) {
                v.position[k]+=weights[side]*offset[k];
                normal[k]+=weights[side]*turn[k];
            }
        }
        const float length=std::sqrt(dot(normal,normal));
        if(length>0) for(unsigned k=0;k<3;++k)
            v.normal[k]=static_cast<std::int16_t>(std::lround(32767.f*normal[k]/length));
    }
    return vertices;
}
