#pragma once
#include "gltf_asset.h"
#include "secondary_physics.h"

namespace owned::gltf {
// Rotation around the animated chest attachment. Only a small axial stretch
// is allowed, so a forward stop cannot pull the tissue away from the chest.
inline Affine breast_bend(const std::array<float,3>& base,const std::array<float,3>& tip,
                          const std::array<float,3>& displacement) {
    Affine out{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    std::array<float,3> u{},v{};
    float a=0,b=0;
    for(unsigned i=0;i<3;++i) {
        u[i]=tip[i]-base[i]; v[i]=u[i]+displacement[i];
        a+=u[i]*u[i]; b+=v[i]*v[i];
    }
    a=std::sqrt(a); b=std::sqrt(b);
    if(a<1e-5f||b<1e-5f)return out;
    for(unsigned i=0;i<3;++i){u[i]/=a;v[i]/=b;}
    const float cosine=std::clamp(u[0]*v[0]+u[1]*v[1]+u[2]*v[2],-1.f,1.f);
    if(cosine<-.99f)return out;
    const float axial=std::clamp(b/a,.85f,1.15f)-1.f;
    // Shortest arc R = I + K + K*K/(1+cos), K = v*u' - u*v'.
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j) {
        const float k=v[i]*u[j]-u[i]*v[j];
        const float kk=cosine*(v[i]*u[j]+u[i]*v[j])-v[i]*v[j]-u[i]*u[j];
        out[i*4+j]+=k+kk/(1+cosine)+axial*v[i]*u[j];
    }
    for(unsigned i=0;i<3;++i) {
        out[i*4+3]=base[i];
        for(unsigned j=0;j<3;++j)out[i*4+3]-=out[i*4+j]*base[j];
    }
    return out;
}
struct BreastState {
    std::shared_ptr<const Asset> asset;
    std::shared_ptr<SecondaryPhysics> physics;
    std::array<unsigned,2> targets{}, attachments{};
    struct Affected {unsigned node,side;};
    std::vector<Affected> affected;
    double clock=-1, physics_clock=0;
    bool frozen=false;
    unsigned node_base=UINT32_MAX;
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
            if(part==2)targets[side]=roots[side*3+2];
            if(part==0) {
                const std::string baseName=std::string("PeChoBase.")+(side?"R":"L");
                const auto anchor=std::find_if(body->geometry.joints.begin(),body->geometry.joints.end(),
                    [&](const auto& joint){return joint.name==baseName;});
                if(anchor==body->geometry.joints.end())return false;
                attachments[side]=unsigned(anchor-body->geometry.joints.begin());
            }
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
        if(initialized&&!reset&&base==node_base&&now==clock&&paused==frozen&&root==placement&&pose.clip==playback.clip&&
           pose.previous==playback.previous&&pose.time==playback.time&&
           pose.previous_time==playback.previous_time&&pose.blend==playback.blend)return;
        if(!initialized)physics_clock=now;
        else {
            reset|=now<clock;
            if(!paused&&!frozen)physics_clock+=(std::max)(0.,now-clock);
        }
        clock=now;frozen=paused;node_base=base;placement=root;playback=pose;initialized=true;
        if(!physics&&secondary_factory)physics=secondary_factory();
        if(!physics)return;
        const auto jointMatrix=[&](unsigned node) {
            return multiply(root,socket_pose(*asset->animation,
                asset->attachment_chain(asset->geometry.joints[node].name),pose));
        };
        SecondaryPoints points{}, bases{};
        for(unsigned i=0;i<2;++i) {
            const auto matrix=jointMatrix(targets[i]);
            const auto anchor=jointMatrix(attachments[i]);
            bases[i]={anchor[3],anchor[7],anchor[11]};
            points[i]={matrix[3],matrix[7],matrix[11]};
        }
        const auto offsets=physics->advance(physics_clock,points,reset);
        if(!physics->error().empty()) {
            overrides.reset();
            if(!reported){bridge_append_probe_event("nala_physx_unavailable",physics->error().c_str());reported=true;}
            return;
        }
        std::array<Affine,2> bend;
        for(unsigned side=0;side<2;++side)
            bend[side]=breast_bend(bases[side],points[side],offsets[side]);
        auto result=std::make_shared<std::vector<NativeWorldGeometry::Pose::Override>>();
        result->reserve(affected.size());
        for(const auto entry:affected) {
            const auto matrix=multiply(bend[entry.side],jointMatrix(entry.node));
            NativeWorldGeometry::Pose::Override out{};out.node=base+entry.node;
            for(unsigned r=0;r<3;++r) {
                std::copy_n(matrix.data()+r*4,4,out.value.rows[r].begin());
            }
            result->push_back(out);
        }
        overrides=std::move(result);
        if(!reported){bridge_append_probe_event("nala_physx_ready","backend=PhysX-5.6.1 dynamics=GPU joints=D6 gravity=world_z anchored_swing=1 limit_m=0.085");reported=true;}
    }
};
}
