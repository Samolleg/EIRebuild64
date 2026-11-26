#pragma once
#include "retained_static_scene.h"
#include "environment_material.h"

namespace owned::environment {
// Immutable rigid scene geometry. FigureCpuAsset is the renderer's packed mesh
// storage, not a FIG file: no legacy model, bone or animation resource is read.
struct Part {
    std::string name, logical_name;
    std::shared_ptr<const FigureCpuAsset> geometry;
    std::shared_ptr<const MaterialCpuAsset> material;
    std::shared_ptr<const TextureCpuAsset> texture;
    std::shared_ptr<const Material> pbr;
    std::vector<FigureBounds> collision; // one or eight endpoints, separate from render bounds
    enum class Role { none, solid, crown, walkable } role=Role::solid;
    unsigned passability=0, opacity=0;
    bool foliage=false, render=true;
    std::array<float,3> minimum{}, maximum{}; // explicit model-local, game Z-up collider
    bool solid = false;
};
struct ModelView {
    std::string primary,secondary,clip;
    float seconds=0;
    std::array<float,3> shape{.5f,.5f,.5f}; // strength, grace and stature
};
struct ModelMotion;
struct Asset {
    std::shared_ptr<const ModelMotion> motion;
    std::map<std::string,float> clip_lengths;
    std::map<std::string,std::string> clip_bindings;
    bool native_model=false;
    ModelView view;
    ResourceIdentity identity;
    std::vector<Part> parts;
    std::shared_ptr<const ModelCpuAsset> transforms; // rigid scene nodes, no joints/skin
    std::array<float,3> minimum{}, maximum{};
    std::uint64_t geometry_bytes = 0;
    std::uint64_t texture_bytes = 0;
    bool plant=false;
    std::array<float,3> wind_root{};
    // radius, protected trunk height, plant height, per shape endpoint.
    std::vector<std::array<float,3>> wind;
};
std::shared_ptr<const Asset> decode(const ResourceBlob& blob, const std::string& visual, const ModelView* view=nullptr);
std::shared_ptr<const Asset> sample_model(const std::shared_ptr<const Asset>& source,const std::string& clip,float seconds);
// Missing entry uses the existing format. Invalid selected GLB never falls back.
std::shared_ptr<const Asset> select(const std::string& visual,const ModelView& view={});
std::shared_ptr<const Asset> select(ResourceStore& store,const std::string& visual,const ModelView& view={});
bool assigned(ResourceStore& store,const std::string& visual);
FigureBounds collision(const Part& part,const std::array<float,3>& shape);
bool selected(const Part& part,const std::vector<std::string>& names);
void reshape(VisualAssemblyAsset& assembly,const std::array<float,3>& shape);
std::shared_ptr<const VisualAssemblyAsset> assembly(const std::shared_ptr<const Asset>& asset,
    const std::vector<std::string>& selected, std::uint64_t revision,const std::array<float,3>& shape={});
}
