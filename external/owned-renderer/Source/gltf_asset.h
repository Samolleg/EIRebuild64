#pragma once
#include "skn_runtime.h"
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
    std::string cache_key;
    float radius = 0;
};
// Absence permits the established resource lookup. A selected invalid entry throws.
std::shared_ptr<const Asset> select(const std::string &visual, bool hero, bool &present);
} // namespace owned::gltf
