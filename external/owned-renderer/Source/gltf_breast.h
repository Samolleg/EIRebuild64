#pragma once
#include "gltf_asset.h"
#include "secondary_physics.h"
#include "runtime_diagnostics.h"
#include <cstdio>

namespace owned::gltf {
// Retarget the tapered breast displacement to Nala's three parallel weighted
// branches. Keep consecutive endpoints joined in skin space. Applying the
// individual translations directly can tear the surface during a running stop.
inline SecondaryPoint mapped_point(const Affine& m,const SecondaryPoint& p){
    SecondaryPoint out{};
    for(unsigned r=0;r<3;++r)out[r]=m[4*r+3]+m[4*r]*p[0]+m[4*r+1]*p[1]+m[4*r+2]*p[2];
    return out;
}
inline Affine breast_segment_skin(const SecondarySegment& segment,const SecondaryPoint& start,
                                  const SecondaryPoint& solved,float maxAngle){
    SecondaryPoint u{},v{};float rest2=0,goal2=0,dot=0;
    for(unsigned k=0;k<3;++k){
        u[k]=segment.end[k]-segment.pivot[k];v[k]=solved[k]-start[k];
        rest2+=u[k]*u[k];goal2+=v[k]*v[k];dot+=u[k]*v[k];
    }
    const float rest=std::sqrt(rest2),goal=std::sqrt(goal2);
    Node node;
    float stretch=1;
    if(rest>1e-6f&&goal>1e-6f){
        const float cosine=std::clamp(dot/(rest*goal),-1.f,1.f);
        const float half=.5f*(std::min)(maxAngle,std::acos(cosine));
        SecondaryPoint axis{u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
        float norm=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
        if(norm>1e-8f){
            for(unsigned k=0;k<3;++k)node.rotation[k]=axis[k]*std::sin(half)/norm;
            node.rotation[3]=std::cos(half);
        }
        stretch=std::clamp(goal/rest,.90f,1.10f);
    }
    auto m=trs(node);
    if(rest2>1e-12f){
        auto direction=mapped_point(m,u);
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
            m[r*4+c]+=(stretch-1)*direction[r]*u[c]/rest2;
    }
    const auto pivot=mapped_point(m,segment.pivot);
    for(unsigned r=0;r<3;++r)m[r*4+3]=start[r]-pivot[r];
    return m;
}
struct BreastState {
    std::shared_ptr<const Asset> asset;
    std::shared_ptr<SecondaryPhysics> physics;
    std::array<unsigned,2> targets{}, attachments{};
    std::array<unsigned,body_parts> roots{}, starts{}, ends{};
    std::array<unsigned,hair_groups*hair_points> hair_nodes{};
    bool has_hair=false;
    struct Affected {unsigned node,part;};
    std::vector<Affected> affected;
    std::vector<SocketJoint> joints;
    std::vector<unsigned> parent_slots, slots;
    std::vector<Affine> matrices;
    double clock=-1, physics_clock=0;
    bool frozen=false, resting_pose=false;
    static constexpr float idle_gain=.60f, active_gain=3.375f;
    float tissue_gain=active_gain;
    unsigned node_base=UINT32_MAX;
    Playback playback;
    Affine placement{}, display_mapping{};
    bool initialized=false, reported=false;
    double motion_window=-1;
    unsigned motion_clip=UINT32_MAX, motion_samples=0;
    float min_tip_z=0,max_tip_z=0;
    std::shared_ptr<const std::vector<NativeWorldGeometry::Pose::Override>> overrides;

    bool bind(const std::shared_ptr<const Asset>& body) {
        asset=body;
        const auto lookup=[&](const std::string& name){
            const auto& joints=body->geometry.joints;
            auto it=std::find_if(joints.begin(),joints.end(),[&](const auto& j){return j.name==name;});
            return it==joints.end()?UINT32_MAX:unsigned(it-joints.begin());
        };
        for(unsigned side=0;side<2;++side){
            const std::string suffix=side?".R":".L";
            const unsigned first=side*4;
            attachments[side]=lookup("PeChoBase"+suffix);
            for(unsigned k=0;k<3;++k){
                roots[first+k]=ends[first+k]=lookup("PHYSICS_Pecho"+suffix+(k==0?"":k==1?".001":".002"));
                starts[first+k]=k?ends[first+k-1]:attachments[side];
            }
            targets[side]=ends[first+2];
            roots[first+3]=starts[first+3]=lookup("DEF_Glute"+suffix);
            ends[first+3]=lookup("CENTER_OF_MASS_Glute"+suffix);
        }
        for(unsigned i=0;i<body_parts;++i)if(starts[i]==UINT32_MAX||ends[i]==UINT32_MAX)return false;
        const char* groups[]={"LooseL","LooseR","Braid","Armor"};
        has_hair=true;
        for(unsigned i=0;i<hair_nodes.size();++i){
            hair_nodes[i]=lookup(std::string("NalaHair_")+groups[i/hair_points]+"_"+std::to_string(i%hair_points));
            has_hair &= hair_nodes[i]!=UINT32_MAX;
        }
        for(unsigned n=0;n<body->animation->nodes.size();++n) {
            for(int p=int(n);p>=0;p=body->animation->nodes[p].parent) {
                const auto root=std::find(roots.begin(),roots.end(),unsigned(p));
                if(root!=roots.end()) {affected.push_back({n,unsigned(root-roots.begin())});break;}
            }
        }
        const auto& animation=*body->animation;
        slots.assign(animation.nodes.size(),UINT32_MAX);
        const auto include_chain=[&](unsigned node) {
            std::vector<unsigned> path;
            int n=int(node);
            while(n>=0 && slots[n]==UINT32_MAX) {
                path.push_back(unsigned(n));n=animation.nodes[n].parent;
                animation_require(path.size()<=slots.size(),"secondary hierarchy cycle");
            }
            for(auto it=path.rbegin();it!=path.rend();++it) {
                const int parent=animation.nodes[*it].parent;
                parent_slots.push_back(parent<0?UINT32_MAX:slots[parent]);
                slots[*it]=unsigned(joints.size());
                SocketJoint joint; joint.node=*it;
                joint.tracks.resize(animation.clips.size(),{UINT32_MAX,UINT32_MAX,UINT32_MAX});
                joints.push_back(std::move(joint));
            }
        };
        if(has_hair)for(unsigned i=0;i<hair_nodes.size();++i)affected.push_back({hair_nodes[i],body_parts+i});
        for(const auto entry:affected)include_chain(entry.node);
        for(const auto n:ends)include_chain(n);
        for(const auto anchor:attachments)include_chain(anchor);
        for(unsigned c=0;c<animation.clips.size();++c) {
            const auto& tracks=animation.clips[c].tracks;
            for(unsigned t=0;t<tracks.size();++t) {
                const auto& track=tracks[t];
                if(track.path==Path::weights || slots.at(track.node)==UINT32_MAX)continue;
                joints[slots[track.node]].tracks[c][unsigned(track.path)]=t;
            }
        }
        matrices.resize(joints.size());
        return true;
    }
    void update(double now,const Playback& pose,const Affine& root,bool reset,unsigned base,bool paused=false,
                const RetainedVisualSample::PreviewPhysicsSpace* ui=nullptr,const PhysiquePose* shape=nullptr,bool resting=false) {
        if(initialized&&!reset&&resting==resting_pose&&(!ui||display_mapping==ui->to_display)&&base==node_base&&now==clock&&paused==frozen&&root==placement&&pose.clip==playback.clip&&
           pose.previous==playback.previous&&pose.time==playback.time&&
           pose.previous_time==playback.previous_time&&pose.blend==playback.blend)return;
        const double previousPhysicsClock=physics_clock;
        if(!initialized)physics_clock=now;
        else {
            reset|=now<clock;
            if(!paused&&!frozen)physics_clock+=(std::max)(0.,now-clock);
        }
        // Standing idle clips also contain keyframed weight shifts and abrupt
        // changes of facing. The authored pose carries that movement. Give
        // secondary inertia a small residual there, easing on simulation time
        // when a footfall or stop crosses the locomotion/idle boundary.
        const float targetGain=resting?idle_gain:active_gain;
        if(!initialized||reset)tissue_gain=targetGain;
        else{
            const float dt=float(physics_clock-previousPhysicsClock);
            tissue_gain+=(targetGain-tissue_gain)*(-std::expm1(-dt/.075f));
        }
        resting_pose=resting;
        if(ui)display_mapping=ui->to_display;
        clock=now;frozen=paused;node_base=base;placement=root;playback=pose;initialized=true;
        if(!physics&&secondary_factory)physics=secondary_factory();
        if(!physics)return;
        const Affine basis={1,0,0,0,0,0,-1,0,0,1,0,0,0,0,0,1};
        for(unsigned i=0;i<joints.size();++i) {
            const auto parent=parent_slots[i];
            matrices[i]=multiply(parent==UINT32_MAX?basis:matrices[parent],
                socket_local(*asset->animation,joints[i],pose,shape));
        }
        const auto jointMatrix=[&](unsigned node){return multiply(root,matrices[slots[node]]);};
        const auto origin=[&](unsigned node){
            const auto m=jointMatrix(node);return SecondaryPoint{m[3],m[7],m[11]};
        };
        SecondaryTargets input;
        for(unsigned i=0;i<body_parts;++i){
            auto& segment=input.body[i];segment.pivot=origin(starts[i]);segment.end=origin(ends[i]);
            const unsigned k=i%4;
            const auto left=origin(starts[k]),right=origin(starts[4+k]);
            for(unsigned axis=0;axis<3;++axis)segment.lateral[axis]=(left[axis]-right[axis])*(k==3?-1.f:1.f);
        }
        input.has_hair=has_hair;
        if(has_hair)for(unsigned i=0;i<hair_nodes.size();++i)input.hair[i]=origin(hair_nodes[i]);
        const auto offsets=physics->advance(physics_clock,input,reset);
        if(!physics->error().empty()) {
            overrides.reset();
            if(!reported){bridge_append_probe_event("nala_physx_unavailable",physics->error().c_str());reported=true;}
            return;
        }
        std::array<Affine,body_parts> bend;
        for(unsigned i=0;i<body_parts;++i){
            Node node;
            if(i%4==3)node.rotation=slerp({0,0,0,1},offsets.body[i].rotation,tissue_gain);
            auto& m=bend[i];m=trs(node);
            const auto& p=input.body[i].pivot;
            for(unsigned r=0;r<3;++r)
                m[r*4+3]=tissue_gain*offsets.body[i].shift[r]+p[r]-(m[r*4]*p[0]+m[r*4+1]*p[1]+m[r*4+2]*p[2]);
        }
        for(unsigned side=0;side<2;++side){
            const unsigned first=4*side;
            auto start=input.body[first].pivot;
            for(unsigned k=0;k<3;++k){
                unsigned i=first+k;
                auto goal=mapped_point(bend[i],input.body[i].end);
                // Preserve the connected surface across the three weighted branches.
                constexpr float degreesToRadians=.01745329252f;
                bend[i]=breast_segment_skin(input.body[i],start,goal,(k?43.f:25.f)*degreesToRadians);
                start=mapped_point(bend[i],input.body[i].end);
            }
        }
        if(RuntimeDiagnostics::Enabled() && physics->ready() && !paused) {
            // Final displaced tip minus its authored pose, in world metres.
            const auto& p=input.body[2].end;const auto& m=bend[2];
            const float dz=m[8]*p[0]+m[9]*p[1]+m[10]*p[2]+m[11]-p[2];
            if(!motion_samples || reset || physics_clock<motion_window || motion_clip!=pose.clip){
                motion_window=physics_clock;motion_clip=pose.clip;motion_samples=0;
                min_tip_z=max_tip_z=dz;
            }
            min_tip_z=(std::min)(min_tip_z,dz);max_tip_z=(std::max)(max_tip_z,dz);
            ++motion_samples;
            if(physics_clock-motion_window>=1.) {
                char detail[200]{};
                std::snprintf(detail,sizeof(detail),"view=%s clip=%s samples=%u left_tip_dz_min_m=%.5f left_tip_dz_max_m=%.5f",
                    ui?ui->view:"world",asset->animation->clips.at(pose.clip).name.c_str(),motion_samples,min_tip_z,max_tip_z);
                bridge_append_probe_event("nala_physx_motion",detail);
                motion_samples=0;
            }
        }
        auto result=std::make_shared<std::vector<NativeWorldGeometry::Pose::Override>>();
        result->reserve(affected.size());
        for(const auto entry:affected) {
            auto matrix=jointMatrix(entry.node);
            if(entry.part<body_parts)matrix=multiply(bend[entry.part],matrix);
            else for(unsigned r=0;r<3;++r)matrix[r*4+3]+=offsets.hair[entry.part-body_parts][r];
            if(ui)matrix=multiply(ui->to_display,matrix);
            NativeWorldGeometry::Pose::Override out{};out.node=base+entry.node;
            for(unsigned r=0;r<3;++r) {
                std::copy_n(matrix.data()+r*4,4,out.value.rows[r].begin());
            }
            result->push_back(out);
        }
        overrides=std::move(result);
        if(!reported&&physics->ready()){bridge_append_probe_event("nala_physx_ready",has_hair?"backend=PhysX GPU D6=4 fixed_hz=120 hair_guides=4":"backend=PhysX GPU D6=4 fixed_hz=120 hair_guides=0");reported=true;}
    }
};
}
