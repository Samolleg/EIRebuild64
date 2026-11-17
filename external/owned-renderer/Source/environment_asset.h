#pragma once
#include "retained_static_scene.h"

namespace owned::environment {
// Immutable rigid scene geometry. FigureCpuAsset is the renderer's packed mesh
// storage, not a FIG file: no legacy model, bone or animation resource is read.
struct Part {
    std::string name;
    std::shared_ptr<const FigureCpuAsset> geometry;
    std::shared_ptr<const MaterialCpuAsset> material;
    std::shared_ptr<const TextureCpuAsset> texture;
    std::array<float,3> minimum{}, maximum{}; // explicit model-local, game Z-up collider
    bool solid = false;
};
struct Asset {
    ResourceIdentity identity;
    std::vector<Part> parts;
    std::shared_ptr<const ModelCpuAsset> transforms; // rigid scene nodes, no joints/skin
    std::array<float,3> minimum{}, maximum{};
    std::uint64_t geometry_bytes = 0;
};
std::shared_ptr<const Asset> decode(const ResourceBlob& blob, const std::string& visual);
// Missing entry uses the existing format. Invalid selected GLB never falls back.
std::shared_ptr<const Asset> select(const std::string& visual);
std::shared_ptr<const VisualAssemblyAsset> assembly(const std::shared_ptr<const Asset>& asset,
    const std::vector<std::string>& selected, std::uint64_t revision);
}
