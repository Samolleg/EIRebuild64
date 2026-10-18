#pragma once
// Shared c56 algorithms: typed renderer assets only, no game image or hooks.
#include "engine_bridge.h"
#include "retained_static_scene.h"
#include "critters.h"

namespace owned {
// Snow trail identity: the visual incarnation (never an address) the
// footprint markers record as their creator, so live rows and prints join.
inline std::uint64_t snow_trail_key(const VisualResourceIncarnation& resource) noexcept {
    return (std::uint64_t(std::uint32_t(resource.signed_id)) << 32) ^ std::uint64_t(resource.serial);
}
inline float snow_trail_extent(const std::array<float,2>& half) noexcept {
    const float e=std::max(std::abs(half[0]),std::abs(half[1]));
    return std::isfinite(e)?e:0.f;
}
struct PerchRef { std::string part; std::uint32_t triangle=0; int sample=-1; critters::PerchKind kind=critters::perch_fill; };
VisualPreparedResources::PoseKey owned_visual_pose_key(const RetainedVisualSample& sample) noexcept;
bool owned_visual_pose_key_matches(const VisualPreparedResources::PoseKey& left,
    const RetainedVisualSample& sample,const double* phase=nullptr) noexcept;
bool same_visual_pose_key(const VisualPreparedResources::PoseKey& left,
    const VisualPreparedResources::PoseKey& right) noexcept;
bool prepare_world_visual_pose(const RetainedVisualSample& sample,
    std::uint64_t* preparation_count = nullptr);
bool build_ground_contacts(const RetainedVisualSample& sample, const TerrainCpuAsset& terrain,
    const std::vector<float>& sector_top, std::vector<std::array<float,4>>& output, std::uint64_t* preparation_count = nullptr);
std::vector<PerchRef> choose_model_perches(const RetainedVisualSample& sample,
    bool cactus, const TextureCpuAsset* atlas);
bool place_perches(const RetainedVisualSample& sample, const std::vector<PerchRef>& refs,
    std::vector<std::array<float,4>>& output, std::uint64_t* preparation_count = nullptr);
// Queries consume one immutable, completed visual frame. Cache lifetime is
// explicit so independent worlds do not share a hidden model census.
struct WorldPerchCache { std::unordered_map<std::string,std::vector<PerchRef>> models; };
struct WorldContactStore;
struct WorldContactCache {
    std::shared_ptr<WorldContactStore> store;
    std::vector<float> sector_top;
    std::shared_ptr<const TerrainCpuAsset> terrain;
    std::uint64_t generation = 0, revision = 0;
};
bool prepare_world_visual_bounds(const RetainedVisualSample& sample, NativeWorldBounds& bounds);
bool build_critter_obstacles(const RetainedVisualFrame& frame,float x,float y,float radius,
    std::vector<std::array<float,6>>& output) noexcept;
bool build_critter_perches(const RetainedVisualFrame& frame,const TextureCpuAsset* atlas,
    WorldPerchCache& cache,float x,float y,float radius,std::vector<CritterPerchTree>& output,
    std::uint64_t* preparation_count = nullptr) noexcept;
bool build_ground_cover_objects(const RetainedVisualFrame& frame,const RetainedTerrainSourceSnapshot& source,
    WorldContactCache& cache,float x,float y,float radius,std::vector<ground_cover::FootprintSplat>& output,
    std::uint64_t* preparation_count = nullptr,RetainedStaticScene* owner = nullptr,
    bool load_reveal_held = false) noexcept;
} // namespace owned
