#pragma once
#include "skn_runtime.h"
#include "gltf_equipment.h"
#include "gltf_physique_surface.h"
#include "gltf_playback.h"
#include "gltf_socket.h"
#include "gltf_weapon_constraints.h"
#include <functional>

struct cgltf_data;
namespace owned::gltf {
// The parsed document owns nodes, skins, morph accessors and animation channels.
// Its GLB byte owner outlives every cgltf buffer/accessor pointer.
struct Document
{
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    std::shared_ptr<cgltf_data> parsed;
};
struct WeaponArmPlan;
struct Asset
{
    bool rigid = false;
    bool wearable = false;
    // Local tracks for the few moving mesh nodes of an articulated item.
    std::vector<SocketJoint> mechanism;
    std::shared_ptr<const Animation> mechanism_animation;
    std::unique_ptr<WeaponBowConstraint> bow_constraint;
    std::unique_ptr<WeaponProjectileAim> projectile_aim;
    std::unique_ptr<WeaponTwoHandSupport> two_hand_support;
    std::string socket;
    std::map<std::string,std::array<float,3>> anchors;
    mutable std::map<std::string,SocketChain> sockets;
    mutable std::map<std::string,std::shared_ptr<const WeaponArmPlan>> support_arm_plans;
    mutable std::mutex socket_mutex;
    Document document;
    skn::Asset geometry;
    std::vector<std::array<skn::Dds, 4>> textures;
    // Original linear baseColorFactor used when an item supplies another PNG.
    std::vector<std::array<float,4>> base_color_factors;
    std::vector<skn::Matrix> idle;
    std::shared_ptr<Animation> animation;
    std::shared_ptr<GpuProgram> gpu_animation;
    Physique physique;
    PhysiqueTree physique_tree;
    std::array<unsigned,2> sole_nodes{};
    std::array<std::array<float,3>,2> sole_points{};
    std::vector<skn::Vertex> rest_vertices;
    std::vector<bool> portrait_head_submeshes;
    // Neutral bounds in the game's Z-up model space, measured from drawn triangles.
    std::array<float,3> portrait_min{},portrait_max{},face_min{},face_max{};
    unsigned face_indices=0;
    std::vector<std::uint32_t> vertex_morphs;
    std::string cache_key, resource_key;
    float radius = 0;
    float animation_radius = 0;
    std::vector<NodeEnvelope> node_envelopes;
    // Bone attachments share the body's existing palette. Build only CPU
    // query chains actually requested, once per asset (not per actor/item).
    const SocketChain& attachment_chain(const std::string& name) const {
        std::lock_guard<std::mutex> guard(socket_mutex);
        const auto found=sockets.find(name);
        if(found!=sockets.end())return found->second;
        const auto node=std::find_if(geometry.joints.begin(),geometry.joints.end(),
            [&](const skn::Joint& joint){return joint.name==name;});
        animation_require(animation && node!=geometry.joints.end(),"attachment node missing");
        return sockets.emplace(name,socket_chain(*animation,unsigned(node-geometry.joints.begin()))).first->second;
    }
};
// Resolve only weighted joints; mesh/container nodes do not enter the pose palette.
// Runs once on an immutable equipment change, never during frame submission.
inline std::vector<unsigned> wearable_nodes(const Asset& item,const Asset& body) {
    animation_require(item.wearable && !body.wearable && !body.rigid,"wearable/body kinds");
    std::vector<unsigned> nodes(item.geometry.joints.size(),0);
    std::set<unsigned> used;
    for(const auto& v:item.rest_vertices)for(unsigned k=0;k<4;++k)
        if(v.weights[k])used.insert(v.joints[k]);
    for(unsigned i:used) {
        const auto& joint=item.geometry.joints.at(i);
        auto found=std::find_if(body.geometry.joints.begin(),body.geometry.joints.end(),
            [&](const skn::Joint& j){return j.name==joint.name;});
        animation_require(found!=body.geometry.joints.end() && !joint.name.empty() && joint.name[0]!='#',
            "wearable joint missing in body");
        for(unsigned c=0;c<16;++c)animation_require(std::abs(joint.inverse[c]-found->inverse[c])<1e-4f,
            "wearable inverse bind differs from body");
        nodes[i]=unsigned(found-body.geometry.joints.begin());
    }
    return nodes;
}
inline float wearable_radius(const Asset& item,const Asset& body,const std::vector<unsigned>& nodes) {
    double result=0;
    for(const auto& v:item.rest_vertices) {
        double radius=0;
        for(unsigned k=0;k<4;++k)if(v.weights[k]) {
            const auto point=skn::point(item.geometry.joints.at(v.joints[k]).inverse,v.position);
            const auto& limit=body.node_envelopes.at(nodes.at(v.joints[k]));
            const double length=std::sqrt(double(point[0])*point[0]+double(point[1])*point[1]+double(point[2])*point[2]);
            radius+=(limit.translation+limit.scale*length)*v.weights[k]/65535.;
        }
        result=(std::max)(result,radius);
    }
    animation_require(std::isfinite(result) && result<1e30,"wearable bounds overflow");
    return static_cast<float>(result*(1+1e-5));
}
struct EquipmentAsset {
    std::shared_ptr<const Asset> asset;
    std::string socket, slot;
    bool weapon=false, arrow=false;
    std::vector<unsigned> body_nodes;
    std::shared_ptr<const std::vector<GarmentAnchor>> physique_anchors;
    std::map<unsigned,std::shared_ptr<const skn::Dds>> base_color_overrides;
    std::map<unsigned,std::shared_ptr<const skn::Dds>> metallic_roughness_overrides;
    // Shared by the same GLB material and ORM resource, including across actors.
    std::map<unsigned,std::shared_ptr<const std::uint8_t>> material_identities;
    unsigned mechanism_offset=0;
    std::array<float,3> offset{};
};
struct WeaponArmNodePlan {
    SocketJoint joint;
    int parent=-1;
    bool forearm=false,hand=false,primary=false,output=false;
    int mirror=-1;
    Affine mirror_bind{};
};
struct WeaponArmPlan {
    std::vector<WeaponArmNodePlan> nodes;
    unsigned upper=0,forearm=0,hand=0;
    std::array<unsigned,4> markers{};
    unsigned primary_begin=256,primary_upper=0,primary_forearm=0,primary_hand=0;
    std::array<unsigned,4> primary_markers{};
    std::array<unsigned,2> width_bones{};
};
struct EquipmentSelection {
    std::vector<EquipmentAsset> items;
    unsigned mechanism_nodes=0;
    float radius=0;
    std::shared_ptr<const WeaponArmPlan> support_arm;
};
inline std::shared_ptr<const WeaponArmPlan> weapon_arm_plan(const Asset& body,const WeaponTwoHandSupport& support) {
    std::string key=support.upper+"|"+support.forearm+"|"+support.hand;
    for(const auto& name:support.markers)key+="|"+name;
    if(support.mirror) {
        const auto& m=*support.mirror;
        key+="|mirror|"+m.primary_upper+"|"+m.primary_forearm+"|"+m.primary_hand;
        for(const auto& name:m.primary_markers)key+="|"+name;
        for(const auto& name:m.width_bones)key+="|"+name;
        for(const auto& pair:m.pairs)key+="|"+pair[0]+"|"+pair[1];
        key.append(reinterpret_cast<const char*>(m.bind_normal.data()),sizeof(m.bind_normal));
    }
    std::lock_guard<std::mutex> guard(body.socket_mutex);
    const auto cached=body.support_arm_plans.find(key);
    if(cached!=body.support_arm_plans.end())return cached->second;
    const auto& animation=*body.animation;
    animation_require(animation.nodes.size()==body.geometry.joints.size(),"two-hand body node count");
    const auto index=[&](const std::string& name) {
        const auto found=std::find_if(body.geometry.joints.begin(),body.geometry.joints.end(),
            [&](const skn::Joint& joint){return joint.name==name;});
        animation_require(found!=body.geometry.joints.end(),"two-hand body joint missing");
        return unsigned(found-body.geometry.joints.begin());
    };
    const auto upper=index(support.upper),forearm=index(support.forearm),hand=index(support.hand);
    std::array<unsigned,4> markers{};
    for(unsigned i=0;i<4;++i)markers[i]=index(support.markers[i]);
    std::vector<std::vector<unsigned>> children(animation.nodes.size());
    for(unsigned i=0;i<animation.nodes.size();++i)if(animation.nodes[i].parent>=0)
        children.at(unsigned(animation.nodes[i].parent)).push_back(i);
    std::set<unsigned> used{upper,forearm,hand};
    used.insert(markers.begin(),markers.end());
    for(const auto& v:body.rest_vertices)for(unsigned k=0;k<4;++k)if(v.weights[k])used.insert(v.joints[k]);
    auto plan=std::make_shared<WeaponArmPlan>();
    std::vector<int> lookup(animation.nodes.size(),-1);
    std::function<void(unsigned,int,bool,bool,bool,unsigned,unsigned)> visit=
      [&](unsigned node,int parent,bool lower,bool is_hand,bool primary,unsigned lower_node,unsigned hand_node) {
        animation_require(lookup[node]<0 && plan->nodes.size()<animation.nodes.size() && plan->nodes.size()<256,
                          "two-hand arm hierarchy");
        const unsigned position=unsigned(plan->nodes.size());lookup[node]=int(position);
        WeaponArmNodePlan part;part.joint.node=node;part.parent=parent;part.forearm=lower||node==lower_node;
        part.hand=is_hand||node==hand_node;part.primary=primary;
        const auto& name=body.geometry.joints[node].name;
        part.output=used.count(node)!=0 || (!name.empty() && name[0]!='#');
        part.joint.tracks.resize(animation.clips.size(),{UINT32_MAX,UINT32_MAX,UINT32_MAX});
        plan->nodes.push_back(std::move(part));
        for(unsigned child:children[node])visit(child,int(position),lower||node==lower_node,
                                                is_hand||node==hand_node,primary,lower_node,hand_node);
    };
    visit(upper,-1,false,false,false,forearm,hand);
    animation_require(lookup[forearm]>=0 && lookup[hand]>=0 && plan->nodes[lookup[hand]].forearm,
                      "two-hand arm ancestry");
    plan->upper=unsigned(lookup[upper]);plan->forearm=unsigned(lookup[forearm]);plan->hand=unsigned(lookup[hand]);
    for(unsigned i=0;i<4;++i) {
        animation_require(lookup[markers[i]]>=0 && plan->nodes[lookup[markers[i]]].forearm,"two-hand marker ancestry");
        plan->markers[i]=unsigned(lookup[markers[i]]);
    }
    if(support.mirror) {
        const auto& mirror=*support.mirror;
        const auto pu=index(mirror.primary_upper),pf=index(mirror.primary_forearm),ph=index(mirror.primary_hand);
        plan->primary_begin=unsigned(plan->nodes.size());
        visit(pu,-1,false,false,true,pf,ph);
        animation_require(lookup[pf]>=0 && lookup[ph]>=0 && plan->nodes[lookup[ph]].forearm &&
                          plan->nodes[lookup[ph]].primary,"primary arm ancestry");
        plan->primary_upper=unsigned(lookup[pu]);plan->primary_forearm=unsigned(lookup[pf]);
        plan->primary_hand=unsigned(lookup[ph]);
        for(unsigned i=0;i<4;++i) {
            const auto node=index(mirror.primary_markers[i]);
            animation_require(lookup[node]>=0 && plan->nodes[lookup[node]].hand &&
                              plan->nodes[lookup[node]].primary,"primary marker ancestry");
            plan->primary_markers[i]=unsigned(lookup[node]);
        }
        for(unsigned i=0;i<2;++i) {
            const auto node=index(mirror.width_bones[i]);
            animation_require(lookup[node]>=0 && plan->nodes[lookup[node]].hand &&
                              plan->nodes[lookup[node]].primary,"primary width ancestry");
            plan->width_bones[i]=unsigned(lookup[node]);
        }
        std::vector<Affine> bind(animation.nodes.size());
        for(unsigned i=0;i<bind.size();++i) {
            bind[i]=trs(animation.nodes[i]);
            if(animation.nodes[i].parent>=0)bind[i]=multiply(bind[unsigned(animation.nodes[i].parent)],bind[i]);
        }
        Affine reflection{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
            reflection[r*4+c]-=2*mirror.bind_normal[r]*mirror.bind_normal[c];
        for(const auto& pair:mirror.pairs) {
            const auto l=index(pair[0]),r=index(pair[1]);
            animation_require(lookup[l]>=0 && lookup[r]>=0 && plan->nodes[lookup[l]].hand &&
                              !plan->nodes[lookup[l]].primary && plan->nodes[lookup[r]].hand &&
                              plan->nodes[lookup[r]].primary,"grip mirror pair ancestry");
            Affine inverse{};
            for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)
                inverse[i*4+j]=body.geometry.joints[r].inverse[j*4+i];
            auto& part=plan->nodes[lookup[l]];
            part.mirror=lookup[r];
            part.mirror_bind=multiply(multiply(inverse,reflection),bind[l]);
        }
    }
    for(unsigned clip=0;clip<animation.clips.size();++clip) {
        const auto& tracks=animation.clips[clip].tracks;
        for(unsigned t=0;t<tracks.size();++t) {
            const auto& track=tracks[t];
            if(track.node>=lookup.size() || lookup[track.node]<0 || track.path==Path::weights)continue;
            const unsigned path=track.path==Path::translation?0:track.path==Path::rotation?1:2;
            plan->nodes[lookup[track.node]].joint.tracks[clip][path]=t;
        }
    }
    body.support_arm_plans.emplace(key,plan);
    return plan;
}
std::vector<EquipmentAsset> select_inventory_item(const VisualEquipmentItem& item);
std::vector<EquipmentAsset> select_equipment(const std::vector<VisualEquipmentItem>& equipment, const std::string& body_resource);
// CPU decoding/acceptance shared by the game and ModelCheck; no device required.
using PublishPixels = std::function<ResourceBlob(const std::string &, const void *, std::uint32_t)>;
// Decode one selected external PNG using the same color/mipmap path as GLB images.
skn::Dds decode_base_color(const ResourceBlob &blob, const PublishPixels &publish,
                           const std::array<float,4>& factor);
skn::Dds decode_metallic_roughness(const ResourceBlob &blob, const PublishPixels &publish);
std::shared_ptr<Asset> decode(const ResourceBlob &blob, const ResourceBlob &profile,
                              const PublishPixels &publish);
// Absence permits the established resource lookup. A selected invalid entry throws.
std::shared_ptr<const Asset> select(const std::string &visual, bool hero, bool &present);
} // namespace owned::gltf

namespace owned::skn {
std::shared_ptr<const RuntimeAsset> from_gltf(const std::shared_ptr<const gltf::Asset>& loaded);
}
