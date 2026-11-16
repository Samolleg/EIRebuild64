#pragma once
#include "skn_runtime.h"
#include "gltf_equipment.h"
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
    std::string socket;
    std::map<std::string,std::array<float,3>> anchors;
    mutable std::map<std::string,SocketChain> sockets;
    mutable std::mutex socket_mutex;
    Document document;
    skn::Asset geometry;
    std::vector<std::array<skn::Dds, 4>> textures;
    std::vector<skn::Matrix> idle;
    std::shared_ptr<Animation> animation;
    std::shared_ptr<GpuProgram> gpu_animation;
    std::vector<skn::Vertex> rest_vertices;
    std::vector<std::uint32_t> vertex_morphs;
    std::string cache_key;
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
struct EquipmentAsset {
    std::shared_ptr<const Asset> asset;
    std::string socket, slot;
    bool weapon=false, arrow=false;
};
struct EquipmentSelection { std::vector<EquipmentAsset> items; float radius=0; };
std::vector<EquipmentAsset> select_equipment(const std::vector<VisualEquipmentItem>& equipment);
// CPU decoding/acceptance shared by the game and ModelCheck; no device required.
using PublishPixels = std::function<ResourceBlob(const std::string &, const void *, std::uint32_t)>;
std::shared_ptr<Asset> decode(const ResourceBlob &blob, const ResourceBlob &profile,
                              const PublishPixels &publish);
// Absence permits the established resource lookup. A selected invalid entry throws.
std::shared_ptr<const Asset> select(const std::string &visual, bool hero, bool &present);
} // namespace owned::gltf

namespace owned::skn {
std::shared_ptr<const RuntimeAsset> from_gltf(const std::shared_ptr<const gltf::Asset>& loaded);
}
