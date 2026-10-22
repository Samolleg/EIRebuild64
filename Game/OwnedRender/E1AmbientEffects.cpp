#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "E1Scene.h"
#include "E1AmbientEffects.h"
#include "E1Terrain.h"
#include "E1World.h"
#include "E1VisualFrame.h"
#include "E2LoadReveal.h"
#include "ground_cover.h"
#include "retained_static_submission.h"
#include "world_scene_algorithms.h"
#include <set>

namespace {
struct CurrentAmbientFrame {
    std::shared_ptr<const owned::RetainedVisualFrame> visuals;
    owned::RetainedTerrainSourceSnapshot terrain;
    std::uint64_t world = 0;
};

struct AmbientVisualFrame {
    std::uint64_t world = 0, serial = 0;
    std::shared_ptr<const owned::RetainedVisualFrame> visuals;
};
AmbientVisualFrame& ambient_visual_frame() {
    // Released by the existing world/footprint retirement boundary.
    static AmbientVisualFrame* value = new AmbientVisualFrame;
    return *value;
}

bool current_ambient_frame(CurrentAmbientFrame& output) noexcept try {
    output = {};
    owned::WorldEnvironment environment{};
    output.world = OwnedRender::E1WorldGeneration();
    if (!output.world || !OwnedRender::E1ReadWorldEnvironment(environment) ||
        !OwnedRender::E1Scene().CurrentWorld(output.world)) return false;
    OwnedRender::E1CurrentTerrainSource(output.terrain);
    auto& cached = ambient_visual_frame();
    const auto serial = owned::bridge_scene_world_to_clip_snapshot().scene_frame_serial;
    if (cached.world != output.world || cached.serial != serial || !cached.visuals) {
        cached = {};
        const auto source = OwnedRender::E1Scene().Scene().visual_frame_value(environment.world_time);
        if (!source) return false;
        auto completed = std::make_shared<owned::RetainedVisualFrame>(*source);
        if (!OwnedRender::E1CompleteAmbientFrame(*completed)) return false;
        cached.world = output.world; cached.serial = serial;
        cached.visuals = std::move(completed);
    }
    output.visuals = cached.visuals;
    return true;
} catch (...) {
    output = {};
    return false;
}

struct AmbientCacheState {
    std::uint64_t world = 0;
    std::uint64_t terrain_generation = 0;
    std::uint64_t terrain_revision = 0;
    owned::WorldPerchCache perches;
    owned::WorldContactCache contacts;

    void synchronize(const CurrentAmbientFrame& frame) {
        if (world == frame.world &&
            terrain_generation == frame.terrain.source_generation &&
            terrain_revision == frame.terrain.source_revision) return;
        world = frame.world;
        terrain_generation = frame.terrain.source_generation;
        terrain_revision = frame.terrain.source_revision;
        perches = {};
        contacts = {};
    }
};

AmbientCacheState& ambient_cache() {
    // The renderer releases contacts while their source owners still live.
    // Do not release retained prepared resources from a late CRT destructor.
    static AmbientCacheState* value = new AmbientCacheState;
    return *value;
}

struct CreatedGroundFootprint {
    owned::VisualResourceIncarnation creator{};
    std::uint64_t world = 0;
    std::int32_t tick = 0;
    float center_x = 0, center_y = 0, half_extent = 0;
    bool walks = false, captured = false;
    owned::ground_cover::FootprintSplat stamp{};
};
std::map<const void*, CreatedGroundFootprint> created_ground_footprints;
std::vector<owned::ground_cover::FootprintSplat> published_ground_footprints;
std::vector<owned::FootprintTrailPoint> published_ground_trails;
std::uint64_t published_ground_world = 0;
bool published_ground_valid = false;

const owned::TextureCpuAsset* tree_atlas(const owned::RetainedTerrainSourceSnapshot& terrain) noexcept {
    if (!terrain.map) return nullptr;
    const auto found = terrain.map->textures.find("tree02");
    return found != terrain.map->textures.end() && found->second && found->second->source
        ? found->second.get() : nullptr;
}

}

namespace OwnedRender {
void E1ResetGroundCoverFootprints() noexcept {
    ambient_visual_frame() = {};
    created_ground_footprints.clear();
    published_ground_footprints.clear();
    published_ground_trails.clear();
    published_ground_world = 0;
    published_ground_valid = false;
}

void E1RegisterGroundCoverFootprint(const void* host_marker, const void* owner,
    std::uint64_t world, std::int32_t tick, float center_x, float center_y) noexcept try {
    if (!host_marker || !owner || !world || !E1Scene().CurrentWorld(world) ||
        !std::isfinite(center_x) || !std::isfinite(center_y)) return;
    const auto creator = E1Scene().Resource(owner);
    if (!creator.serial) return;
    created_ground_footprints[host_marker] =
        {creator, world, tick, center_x, center_y, 0.f, false, false, {}};
} catch (...) {
    published_ground_footprints.clear();
    published_ground_trails.clear();
    published_ground_valid = false;
}

bool E1PublishGroundCoverFootprints(const std::vector<E1GroundCoverFootprintFact>& facts,
    bool enabled) noexcept try {
    published_ground_footprints.clear();
    published_ground_trails.clear();
    published_ground_valid = false;
    CurrentAmbientFrame frame;
    if (!current_ambient_frame(frame)) return false;
    published_ground_world = frame.world;
    std::set<const void*> current_markers;
    for (const auto& fact : facts) if (fact.host_marker) current_markers.insert(fact.host_marker);
    for (auto item = created_ground_footprints.begin(); item != created_ground_footprints.end();) {
        if (item->second.world != frame.world || !current_markers.count(item->first))
            item = created_ground_footprints.erase(item);
        else ++item;
    }
    if (enabled) for (const auto& fact : facts) {
        if (!fact.host_marker || !fact.visible || fact.remaining_segments <= 0 || fact.fade_segments <= 0) continue;
        const auto found = created_ground_footprints.find(fact.host_marker);
        if (found == created_ground_footprints.end()) continue;
        auto& created = found->second;
        if (created.world != frame.world) continue;
        if (!created.captured) {
            for (const auto& unit : frame.visuals->entities) {
                if (unit.resource.signed_id != created.creator.signed_id ||
                    unit.resource.serial != created.creator.serial) continue;
                if (unit.height_valid && retained_visual_sample_shown(unit, false) &&
                    !unit.values.dead && !unit.values.invisible) {
                    created.stamp = owned::ground_cover::live_unit_splat(
                        created.center_x, created.center_y, unit.envelope_radius);
                    created.half_extent = owned::snow_trail_extent(unit.values.target_half_extent);
                    created.walks = owned::ground_cover::unit_presses_cover(unit.values.locomotion);
                    created.captured = true;
                }
                break;
            }
        }
        if (!created.captured) continue;
        const float visibility = owned::ground_cover::footprint_visibility(
            double(fact.remaining_segments), fact.fade_segments);
        if (created.stamp.effect[2] > 0)
            published_ground_footprints.push_back(
                owned::ground_cover::footprint_splat(true, created.stamp, visibility));
        if (created.walks && visibility > 0)
            published_ground_trails.push_back({owned::snow_trail_key(created.creator), created.tick,
                created.center_x, created.center_y, created.half_extent});
    }
    published_ground_valid = true;
    return true;
} catch (...) {
    published_ground_footprints.clear();
    published_ground_trails.clear();
    published_ground_valid = false;
    return false;
}
}

namespace owned {
bool bridge_ground_cover_footprints(std::vector<ground_cover::FootprintSplat>& output,
    std::vector<FootprintTrailPoint>* trail_points) noexcept {
    output.clear();
    if (trail_points) trail_points->clear();
    if (!published_ground_valid || published_ground_world != OwnedRender::E1WorldGeneration()) return false;
    try {
        output = published_ground_footprints;
        if (trail_points) *trail_points = published_ground_trails;
        return true;
    } catch (...) {
        output.clear();
        if (trail_points) trail_points->clear();
        return false;
    }
}

bool bridge_ground_cover_units(float center_x, float center_y, float radius,
    std::vector<std::array<float,4>>& output, std::vector<GroundUnitDetail>* details,
    std::vector<GroundUnitTrail>* trails) noexcept {
    output.clear();
    if (details) details->clear();
    if (trails) trails->clear();
    CurrentAmbientFrame frame;
    if (!current_ambient_frame(frame) || !frame.terrain.terrain) return false;
    // The host's retained scene contains only published visual facts. Hidden
    // units stay excluded, matching the normal game-facing cover contract.
    build_ground_cover_units(*frame.visuals, *frame.terrain.terrain,
        center_x, center_y, radius, false, output, details, trails);
    return true;
}

void bridge_ground_cover_corpses(float center_x, float center_y, float radius,
    std::vector<ground_cover::FootprintSplat>& output) noexcept {
    CurrentAmbientFrame frame;
    if (!current_ambient_frame(frame) || !frame.terrain.terrain) return;
    build_ground_cover_corpses(*frame.visuals, *frame.terrain.terrain,
        center_x, center_y, radius, false, output);
}

bool bridge_liquid_ripples(NativeLiquidRipples& output) noexcept {
    output = {};
    CurrentAmbientFrame frame;
    if (!current_ambient_frame(frame) || !frame.terrain.terrain || !frame.terrain.geometry) return false;
    build_liquid_ripples(*frame.visuals, *frame.terrain.terrain, *frame.terrain.geometry,
        false, output);
    output.reach[0] = frame.terrain.geometry->animation.maximum_wind_multiplier;
    return true;
}

bool bridge_critter_obstacles(float x, float y, float radius,
    std::vector<std::array<float,6>>& output) noexcept {
    output.clear();
    CurrentAmbientFrame frame;
    return current_ambient_frame(frame) &&
        build_critter_obstacles(*frame.visuals, x, y, radius, output);
}

bool bridge_critter_perches(float x, float y, float radius,
    std::vector<CritterPerchTree>& output) noexcept {
    output.clear();
    CurrentAmbientFrame frame;
    if (!current_ambient_frame(frame)) return false;
    auto& cache = ambient_cache();
    cache.synchronize(frame);
    return build_critter_perches(*frame.visuals, tree_atlas(frame.terrain),
        cache.perches, x, y, radius, output);
}

void bridge_release_ground_contacts() noexcept {
    ambient_cache() = {};
    OwnedRender::E1ResetGroundCoverFootprints();
    decltype(published_ground_footprints)().swap(published_ground_footprints);
    decltype(published_ground_trails)().swap(published_ground_trails);
}

bool bridge_ground_cover_objects(float x, float y, float radius,
    std::vector<ground_cover::FootprintSplat>& output) noexcept {
    output.clear();
    CurrentAmbientFrame frame;
    if (!current_ambient_frame(frame)) return false;
    auto& cache = ambient_cache();
    cache.synchronize(frame);
    return build_ground_cover_objects(*frame.visuals, frame.terrain,
        cache.contacts, x, y, radius, output, nullptr, &OwnedRender::E1Scene().Scene(),
        OwnedRender::E2LoadRevealHeld(bridge_current_world_epoch()));
}
}
