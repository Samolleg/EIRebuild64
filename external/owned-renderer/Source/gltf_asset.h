#pragma once
#include "skn_runtime.h"
#include "gltf_playback.h"
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
};
// CPU decoding/acceptance shared by the game and ModelCheck; no device required.
using PublishPixels = std::function<ResourceBlob(const std::string &, const void *, std::uint32_t)>;
std::shared_ptr<Asset> decode(const ResourceBlob &blob, const ResourceBlob &profile,
                              const PublishPixels &publish);
// Absence permits the established resource lookup. A selected invalid entry throws.
std::shared_ptr<const Asset> select(const std::string &visual, bool hero, bool &present);
} // namespace owned::gltf
