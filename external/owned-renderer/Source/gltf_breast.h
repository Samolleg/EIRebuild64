#pragma once
#include "gltf_asset.h"
#include "secondary_physics.h"
#include "runtime_diagnostics.h"
#include <cstdio>

namespace owned::gltf {
inline SecondaryPoint mapped_point(const Affine& m,const SecondaryPoint& p){
    SecondaryPoint out{};
    for(unsigned r=0;r<3;++r)out[r]=m[4*r+3]+m[4*r]*p[0]+m[4*r+1]*p[1]+m[4*r+2]*p[2];
    return out;
}
inline SecondaryPoint tissue_axis(const SecondaryPoint& start,const SecondaryPoint& end) {
    auto axis=weapon_subtract(end,start);
    const float length=weapon_length(axis);
    if(length<1e-6f)return {};
    for(auto& v:axis)v/=length;
    return axis;
}
struct BreastState {
    std::shared_ptr<const Asset> asset;
    std::shared_ptr<SecondaryPhysics> physics;
    std::array<unsigned,2> targets{}, attachments{};
    std::array<unsigned,body_parts> roots{}, starts{}, ends{};
    std::array<unsigned,hair_groups*hair_points> hair_nodes{};
    static constexpr unsigned rib_part=body_parts+hair_groups*hair_points;
    unsigned chest_spine=0;
    std::array<unsigned,2> ribs{},hips{},knees{};
    double breath_origin=0;
    bool has_hair=false;
    struct Affected {unsigned node,part;};
    std::vector<Affected> affected;
    std::vector<SocketJoint> joints;
    std::vector<unsigned> parent_slots, slots;
    std::vector<Affine> matrices;
    double clock=-1, physics_clock=0;
    bool frozen=false, resting_pose=false;
    float idle_weight=0;
    unsigned node_base=UINT32_MAX;
    Playback playback;
    Affine placement{}, display_mapping{};
    bool initialized=false, reported=false;
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
        chest_spine=lookup("CC_Base_Spine02");
        if(chest_spine==UINT32_MAX)return false;
        for(unsigned side=0;side<2;++side){
            const std::string suffix=side?".R":".L";
            ribs[side]=lookup("CC_Base_Ribs"+suffix);
            hips[side]=lookup("CC_Base_Thigh"+suffix);
            knees[side]=lookup("CC_Base_Calf"+suffix);
            if(ribs[side]==UINT32_MAX||hips[side]==UINT32_MAX||knees[side]==UINT32_MAX)return false;
        }
        // Chest-only weights shared by the body and its fitted clothes. Keep
        // the shoulders, head, hands and equipped weapon sockets on their clip.
        for(const char* name:{"DEF_Spine02","CC_Base_Ribs.L","CC_Base_Ribs.R",
                "PeChoBase.L","PeChoBase.R","DEF_PechoBase.L","DEF_PechoBase.R"}){
            const auto node=lookup(name);
            if(node!=UINT32_MAX)affected.push_back({node,rib_part});
        }
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
        include_chain(chest_spine);
        for(const auto n:hips)include_chain(n);
        for(const auto n:knees)include_chain(n);
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
        const float targetWeight=resting?1.f:0.f;
        if(!initialized||reset)idle_weight=targetWeight;
        else{
            const float dt=float(physics_clock-previousPhysicsClock);
            idle_weight+=(targetWeight-idle_weight)*(-std::expm1(-dt/.075f));
        }
        if(!initialized||reset)breath_origin=physics_clock;
        resting_pose=resting;
        if(ui)display_mapping=ui->to_display;
        clock=now;frozen=paused;node_base=base;placement=root;playback=pose;initialized=true;
        if(has_hair&&!physics&&secondary_factory)physics=secondary_factory();

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
        const auto mid=[](const SecondaryPoint& a,const SecondaryPoint& b){
            return SecondaryPoint{.5f*(a[0]+b[0]),.5f*(a[1]+b[1]),.5f*(a[2]+b[2])};
        };
        const auto ribMid=mid(origin(ribs[0]),origin(ribs[1]));
        const auto rise=weapon_subtract(ribMid,origin(chest_spine));
        const auto chestMid=mid(origin(attachments[0]),origin(attachments[1]));
        const auto depth=weapon_subtract(mid(origin(targets[0]),origin(targets[1])),chestMid);
        const auto across=tissue_axis(origin(ribs[1]),origin(ribs[0]));
        // One slow inhalation for the rib cage and both attachments.
        const float inhale=idle_weight*float(.5-.5*std::cos(std::fmod(physics_clock-breath_origin,4.4)*6.283185307179586/4.4));
        auto chestPose=physique_identity;
        for(unsigned r=0;r<3;++r){
            chestPose[4*r+3]=inhale*(.13f*rise[r]+.16f*depth[r]);
            for(unsigned c=0;c<3;++c){
                const float expand=inhale*.055f*across[r]*across[c];
                chestPose[4*r+c]+=expand;
                chestPose[4*r+3]-=expand*chestMid[c];
            }
        }
        SecondaryPose offsets{};
        if(physics && has_hair){
            SecondaryTargets input;
            for(unsigned i=0;i<hair_nodes.size();++i)input.hair[i]=origin(hair_nodes[i]);
            offsets=physics->advance(physics_clock,input,reset);
            if(!physics->error().empty()&&!reported){
                bridge_append_probe_event("nala_hair_unavailable",physics->error().c_str());reported=true;
            }
        }
        // Hip extension changes the shape of the loaded side of the seat. Use
        // the signed difference in femur directions; common crouch/translation
        // cancels, and reversing the stride swaps the effect between the sides.
        const auto legDifference=weapon_subtract(tissue_axis(origin(hips[0]),origin(knees[0])),
                                                 tissue_axis(origin(hips[1]),origin(knees[1])));
        const auto back=tissue_axis(mid(origin(starts[3]),origin(starts[7])),
                                    mid(origin(ends[3]),origin(ends[7])));
        float stride=0;
        for(unsigned r=0;r<3;++r)stride+=.5f*legDifference[r]*back[r];
        stride=std::clamp(stride,-1.f,1.f)*(1.f-idle_weight);

        std::array<SecondaryPoint,2> seatShift;
        for(unsigned side=0;side<2;++side){
            const unsigned part=4*side+3;
            seatShift[side]=weapon_subtract(origin(ends[part]),origin(starts[part]));
            const float load=(side?-stride:stride)*.45f;
            for(auto& v:seatShift[side])v*=load;
        }
        auto result=std::make_shared<std::vector<NativeWorldGeometry::Pose::Override>>();
        result->reserve(affected.size());
        for(const auto entry:affected) {
            auto matrix=jointMatrix(entry.node);
            if(entry.part==rib_part || (entry.part<body_parts && entry.part%4!=3))
                matrix=multiply(chestPose,matrix);
            else if(entry.part<body_parts){
                for(unsigned r=0;r<3;++r)matrix[r*4+3]+=seatShift[entry.part/4][r];
            }else for(unsigned r=0;r<3;++r)matrix[r*4+3]+=offsets.hair[entry.part-body_parts][r];
            if(ui)matrix=multiply(ui->to_display,matrix);
            NativeWorldGeometry::Pose::Override out{};out.node=base+entry.node;
            for(unsigned r=0;r<3;++r) {
                std::copy_n(matrix.data()+r*4,4,out.value.rows[r].begin());
            }
            result->push_back(out);
        }
        overrides=std::move(result);
        if(!reported&&physics&&physics->ready()){bridge_append_probe_event("nala_hair_ready","hair_guides=4 fixed_hz=120");reported=true;}
    }
};
}
