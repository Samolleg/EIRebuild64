#pragma once
#include "gltf_animation.h"
#include "gltf_physique_pose.h"

namespace owned::gltf {
struct SocketJoint {
    unsigned node=0;
    // translation, rotation, scale track indices per clip. No morph evaluation.
    std::vector<std::array<unsigned,3>> tracks;
};
using SocketChain=std::vector<SocketJoint>;
inline SocketChain socket_chain(const Animation& animation,unsigned node) {
    SocketChain chain;
    for(int current=int(node);current>=0;current=animation.nodes.at(current).parent) {
        animation_require(chain.size()<animation.nodes.size(),"socket hierarchy cycle");
        SocketJoint joint; joint.node=unsigned(current);
        joint.tracks.resize(animation.clips.size(),{UINT32_MAX,UINT32_MAX,UINT32_MAX});
        for(unsigned c=0;c<animation.clips.size();++c) {
            const auto& tracks=animation.clips[c].tracks;
            for(unsigned t=0;t<tracks.size();++t) if(tracks[t].node==joint.node && tracks[t].path!=Path::weights) {
                const unsigned p=tracks[t].path==Path::translation?0:tracks[t].path==Path::rotation?1:2;
                joint.tracks[c][p]=t;
            }
        }
        chain.push_back(std::move(joint));
    }
    std::reverse(chain.begin(),chain.end());
    return chain;
}
inline Affine socket_local(const Animation& animation,const SocketJoint& joint,const Playback& playback,const PhysiquePose* shape=nullptr) {
    const auto local=[&](unsigned clip,float time) {
        const auto& bind=animation.nodes[joint.node];
        Node node;node.translation=bind.translation;node.rotation=bind.rotation;node.scale=bind.scale;
        if(clip!=UINT32_MAX) {
            const auto& indices=joint.tracks.at(clip);
            if(indices[0]!=UINT32_MAX)node.translation=sample(animation.clips[clip].tracks[indices[0]],time);
            if(indices[1]!=UINT32_MAX)node.rotation=sample(animation.clips[clip].tracks[indices[1]],time);
            if(indices[2]!=UINT32_MAX)node.scale=sample(animation.clips[clip].tracks[indices[2]],time);
        }
        return node;
    };
    auto node=local(playback.clip,playback.time);
    if(playback.blend<1) {
        const auto prior=local(playback.previous,playback.previous_time);
        node.rotation=slerp(prior.rotation,node.rotation,playback.blend);
        for(unsigned c=0;c<3;++c) {
            node.translation[c]=prior.translation[c]+playback.blend*(node.translation[c]-prior.translation[c]);
            node.scale[c]=prior.scale[c]+playback.blend*(node.scale[c]-prior.scale[c]);
        }
    }
    auto matrix=trs(node);
    return shape?shape->apply(joint.node,matrix):matrix;
}
inline Affine socket_pose(const Animation& animation,const SocketChain& chain,const Playback& playback,
    const Affine& root={1,0,0,0,0,0,-1,0,0,1,0,0,0,0,0,1},const PhysiquePose* shape=nullptr) {
    auto result=root;
    for(const auto& joint:chain) {
        result=multiply(result,socket_local(animation,joint,playback,shape));
    }
    return result;
}
} // namespace owned::gltf
