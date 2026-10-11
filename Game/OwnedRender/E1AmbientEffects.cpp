#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "E1Scene.h"
#include "E1Terrain.h"
#include "E1World.h"
#include "ground_cover.h"
#include "world_scene_algorithms.h"

namespace {
struct CurrentAmbientFrame {
    std::shared_ptr<const owned::RetainedVisualFrame> visuals;
    owned::RetainedTerrainSourceSnapshot terrain;
    std::uint64_t world = 0;
};

bool current_ambient_frame(CurrentAmbientFrame& output) noexcept try {
    output = {};
    owned::WorldEnvironment environment{};
    output.world = OwnedRender::E1WorldGeneration();
    if (!output.world || !OwnedRender::E1ReadWorldEnvironment(environment) ||
        !OwnedRender::E1Scene().CurrentWorld(output.world)) return false;
    OwnedRender::E1CurrentTerrainSource(output.terrain);
    output.visuals = OwnedRender::E1Scene().Scene().visual_frame_value(environment.world_time);
    return bool(output.visuals);
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
    static AmbientCacheState value;
    return value;
}

const owned::TextureCpuAsset* tree_atlas(const owned::RetainedTerrainSourceSnapshot& terrain) noexcept {
    if (!terrain.map) return nullptr;
    const auto found = terrain.map->textures.find("tree02");
    return found != terrain.map->textures.end() && found->second && found->second->source
        ? found->second.get() : nullptr;
}
}

namespace owned {
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

bool bridge_ground_cover_objects(float x, float y, float radius,
    std::vector<ground_cover::FootprintSplat>& output) noexcept {
    output.clear();
    CurrentAmbientFrame frame;
    if (!current_ambient_frame(frame)) return false;
    auto& cache = ambient_cache();
    cache.synchronize(frame);
    return build_ground_cover_objects(*frame.visuals, frame.terrain,
        cache.contacts, x, y, radius, output);
}
}
