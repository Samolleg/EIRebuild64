#pragma once
#include "gltf_asset.h"
#include "secondary_physics.h"

namespace owned::gltf {
struct BreastState {
    std::shared_ptr<const Asset> asset;
    std::shared_ptr<SecondaryPhysics> physics;
    std::array<unsigned,2> targets{};
    struct Affected {unsigned node,side;};
    std::vector<Affected> affected;
    double clock=-1, physics_clock=0;
    bool frozen=false;
    Playback playback;
    Affine placement{};
    bool initialized=false, reported=false;
    std::shared_ptr<const std::vector<NativeWorldGeometry::Pose::Override>> overrides;

    bool bind(const std::shared_ptr<const Asset>& body) {
        asset=body;
        // Exact existing Nala branch names; no changes to skins or vertex weights.
        std::array<unsigned,6> roots{};
        for(unsigned side=0;side<2;++side)for(unsigned part=0;part<3;++part) {
            const std::string name=std::string("PHYSICS_Pecho.")+(side?"R":"L")+
                (part==0?"":part==1?".001":".002");
            const auto found=std::find_if(body->geometry.joints.begin(),body->geometry.joints.end(),
                [&](const auto& joint){return joint.name==name;});
            if(found==body->geometry.joints.end())return false;
            roots[side*3+part]=unsigned(found-body->geometry.joints.begin());
            if(!part)targets[side]=roots[side*3];
        }
        for(unsigned n=0;n<body->animation->nodes.size();++n) {
            for(int p=int(n);p>=0;p=body->animation->nodes[p].parent) {
                const auto root=std::find(roots.begin(),roots.end(),unsigned(p));
                if(root!=roots.end()) {affected.push_back({n,unsigned(root-roots.begin())/3});break;}
            }
        }
        return true;
    }
    void update(double now,const Playback& pose,const Affine& root,bool reset,unsigned base,bool paused=false) {
        if(initialized&&!reset&&now==clock&&paused==frozen&&root==placement&&pose.clip==playback.clip&&
           pose.previous==playback.previous&&pose.time==playback.time&&
           pose.previous_time==playback.previous_time&&pose.blend==playback.blend)return;
        if(!initialized)physics_clock=now;
        else {
            reset|=now<clock;
            if(!paused&&!frozen)physics_clock+=(std::max)(0.,now-clock);
        }
        clock=now;frozen=paused;placement=root;playback=pose;initialized=true;
        if(!physics&&secondary_factory)physics=secondary_factory();
        if(!physics)return;
        SecondaryPoints points{};
        for(unsigned i=0;i<2;++i) {
            const auto matrix=multiply(root,socket_pose(*asset->animation,
                asset->attachment_chain(asset->geometry.joints[targets[i]].name),pose));
            points[i]={matrix[3],matrix[7],matrix[11]};
        }
        const auto offsets=physics->advance(physics_clock,points,reset);
        if(!physics->error().empty()) {
            overrides.reset();
            if(!reported){bridge_append_probe_event("nala_physx_unavailable",physics->error().c_str());reported=true;}
            return;
        }
        auto result=std::make_shared<std::vector<NativeWorldGeometry::Pose::Override>>();
        result->reserve(affected.size());
        for(const auto entry:affected) {
            auto matrix=multiply(root,socket_pose(*asset->animation,
                asset->attachment_chain(asset->geometry.joints[entry.node].name),pose));
            NativeWorldGeometry::Pose::Override out{};out.node=base+entry.node;
            for(unsigned r=0;r<3;++r) {
                matrix[r*4+3]+=offsets[entry.side][r];
                std::copy_n(matrix.data()+r*4,4,out.value.rows[r].begin());
            }
            result->push_back(out);
        }
        overrides=std::move(result);
        if(!reported){bridge_append_probe_event("nala_physx_ready","backend=PhysX-5.6.1 dynamics=GPU joints=D6 limit_m=0.025");reported=true;}
    }
};
}
