#pragma once
#include "skn_runtime.h"
#include "gltf_equipment.h"
#include "gltf_physique_surface.h"
#include "gltf_playback.h"
#include "gltf_socket.h"
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
struct Asset
{
    bool rigid = false;
    bool wearable = false;
    std::string socket;
    std::map<std::string,std::array<float,3>> anchors;
    mutable std::map<std::string,SocketChain> sockets;
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
};
struct EquipmentSelection { std::vector<EquipmentAsset> items; float radius=0; };
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
