#include "E1Terrain.h"
#include "E1World.h"
#include "E1Surface.h"
#include "world_mist_grid.h"
#include <cmath>

namespace {
const void* sourceOwner = nullptr;
std::uint64_t worldIdentity = 0, sourceRevision = 0, nextGeneration = 0, terrainFrame = 0, geometrySerial = 0;
owned::RetainedTerrainSourceSnapshot source;
std::vector<std::uint64_t> atlasIds;
std::array<float,16> transform{};
owned::WorldMistGridCache mist;
owned::OwnedTerrainFramePacket view;
owned::OwnedTerrainPreparedResources prepared;
bool viewReady = false, terrainLod = true;
std::vector<std::shared_ptr<const owned::OwnedTerrainDynamicSector>> sectors;
std::uint64_t sectorFrame = 0, phaseSerial = 0;
void clear() noexcept {
    sourceOwner = nullptr; worldIdentity = sourceRevision = terrainFrame = 0;
    source = {}; transform = {}; mist.retire(); atlasIds.clear(); view = {}; prepared = {}; viewReady = false;
    sectors.clear(); sectorFrame = 0;
}
bool current() noexcept {
    const auto frame = owned::bridge_scene_world_to_clip_snapshot();
    return sourceOwner && terrainFrame && frame.valid && frame.scene_frame_serial == terrainFrame &&
        OwnedRender::E1WorldGeneration() == worldIdentity;
}
}
namespace OwnedRender {
bool E1PublishTerrainSource(const void* owner, std::uint64_t world, std::uint64_t revision,
    const char* project, const char* header, E1TerrainRead read, void* context,
    std::string& failure) noexcept try {
    failure.clear();
    if (!owner || !world || !revision || !project || !header || !read) {
        clear(); failure = "terrain source identity missing"; return false;
    }
    if (owner == sourceOwner && world == worldIdentity && revision == sourceRevision) return true;
    clear(); // A failed replacement must never expose the previous world's bytes.
    if (++nextGeneration == 0) ++nextGeneration;
    owned::ResourceIdentity identity;
    identity.canonical_name = project; identity.container_incarnation = nextGeneration;
    auto terrain = std::make_shared<owned::TerrainCpuAsset>();
    owned::ResourceFailure error;
    const auto resolve = [&](const std::string& name, owned::ResourceBlob& blob, owned::ResourceFailure& why) {
        blob = {};
        std::uint32_t bytes = 0;
        HRESULT hr = read(context, name.c_str(), nullptr, 0, &bytes);
        if (FAILED(hr) || !bytes) {
            why = {owned::ResourceFailureCode::file_read, "terrain member size unavailable: " + name}; return false;
        }
        auto copy = std::make_shared<std::vector<std::uint8_t>>(bytes);
        std::uint32_t actual = 0;
        hr = read(context, name.c_str(), copy->data(), bytes, &actual);
        if (FAILED(hr) || actual != bytes) {
            why = {owned::ResourceFailureCode::file_read, "terrain member changed during copy: " + name}; return false;
        }
        blob.identity = identity; blob.identity.canonical_name = name; blob.identity.bytes = bytes;
        blob.bytes = std::move(copy); return true;
    };
    if (!owned::parse_terrain_members(identity, project, header, resolve, *terrain, error) ||
        !owned::validate_terrain_asset_descriptor_once(*terrain)) {
        failure = error.message.empty() ? "terrain descriptor invalid" : error.message; return false;
    }
    source.source_generation = nextGeneration; source.source_revision = revision;
    source.prepared = std::make_shared<owned::OwnedTerrainPreparedResources>();
    source.terrain = std::move(terrain);
    sourceOwner = owner; worldIdentity = world; sourceRevision = revision;
    return true;
} catch (...) { clear(); try { failure = "terrain source allocation/read failed"; } catch (...) {} return false; }
void E1PublishTerrainAtlases(const void* owner, const std::vector<std::uint64_t>& surfaces) noexcept try {
    if (owner != sourceOwner) return;
    atlasIds.clear();
    if (!source.terrain || surfaces.size() != std::size_t(source.terrain->texture_count)+2u) return;
    atlasIds = surfaces;
} catch (...) { atlasIds.clear(); }
void E1RetireTerrain(const void* owner) noexcept { if (owner == sourceOwner) clear(); }
bool E1PublishTerrainSectors(const void* owner,
    const std::vector<E1TerrainSectorValues>& input) noexcept try {
    sectorFrame = 0;
    if (!owner || owner != sourceOwner || !current() || !source.geometry) return false;
    std::vector<std::shared_ptr<const owned::OwnedTerrainDynamicSector>> next(source.terrain->sectors.size());
    for (const auto& row : input) {
        if (row.sector >= next.size() || next[row.sector]) return false;
        const auto& authored = source.terrain->sectors[row.sector];
        if (row.phases.size() != authored.animation_count) return false;
        for (const auto& tile : authored.tiles) {
            if (tile.animation_ordinal == UINT_MAX) continue;
            if (tile.animation_ordinal >= row.phases.size() ||
                row.phases[tile.animation_ordinal] >= tile.animation_phases) return false;
        }
        auto value = std::make_shared<owned::OwnedTerrainDynamicSector>();
        value->captured = true;
        value->source_revision = source.source_revision;
        value->animation = source.geometry->animation;
        value->animation.waves_enabled = value->animation.waves_enabled && authored.has_layers;
        value->animation.animation_phase_count = static_cast<std::uint32_t>(row.phases.size());
        value->animation_phases = row.phases;
        // Revisions describe actual phase bytes, not a host address or frame.
        // Reuse the renderer's upload cache only when the completed bytes agree.
        if (row.sector < sectors.size() && sectors[row.sector] &&
            sectors[row.sector]->animation_phases == row.phases)
            value->phase_values = sectors[row.sector]->phase_values;
        else {
            if (++phaseSerial == 0) ++phaseSerial;
            value->phase_values.revision = phaseSerial;
        }
        next[row.sector] = std::move(value);
    }
    sectors.swap(next);
    sectorFrame = terrainFrame;
    return true;
} catch (...) { sectorFrame = 0; return false; }
std::shared_ptr<const owned::OwnedTerrainDynamicSector> E1TerrainSector(std::uint32_t sector) noexcept {
    return current() && sectorFrame == terrainFrame && sector < sectors.size() ? sectors[sector] : nullptr;
}
bool E1PublishTerrainView(const void* owner, const owned::OwnedTerrainFrameInputs& input) noexcept try {
    viewReady = false;
    if (!owner || owner != sourceOwner || !current() || !source.geometry) return false;
    auto values = input;
    values.source_generation = source.source_generation;
    values.source_revision = source.source_revision;
    values.world_frame_serial = terrainFrame;
    values.dynamic_state_fingerprint = source.geometry->revision;
    values.force_full_detail = !terrainLod;
    if (!owned::prepare_owned_terrain_frame(source.terrain, values, view) ||
        view.status != owned::OwnedTerrainPacketStatus::ready ||
        !owned::prepare_owned_terrain_spatial_parts(view, *source.geometry, transform, &prepared) ||
        !owned::prepare_owned_terrain_render_commands(view)) return false;
    viewReady = true;
    return true;
} catch (...) { viewReady = false; return false; }
const owned::OwnedTerrainFramePacket* E1TerrainView() noexcept {
    return viewReady && current() ? &view : nullptr;
}
bool E1PrepareTerrainCommand(bool transparent, std::size_t index, E1TerrainCommandSource& output) noexcept try {
    output.view = {}; output.context = {};
    output.terrain.reset(); output.geometry.reset(); output.dynamic.reset(); output.atlas.reset();
    output.prepared.reset();
    if (!E1TerrainView() || !source.geometry) return false;
    const auto& commands = transparent ? view.transparent_commands : view.base_commands;
    if (index >= commands.size()) return false;
    const auto& command = commands[index];
    auto dynamic = E1TerrainSector(command.source_sector);
    std::shared_ptr<const owned::OwnedVisualSurface> atlas;
    if (!owned::bridge_terrain_atlas(command.material % (source.terrain->texture_count+2u), atlas))
        return false;
    owned::NativeGeometryView* record = nullptr;
    if (!owned::prepare_owned_terrain_world_record(source, view, command, dynamic.get(),
        {true, true, 0}, output.context, record) || !record) {
        // A rejected color record may already have borrowed the cache slot.
        // Release that borrow without exposing a partially prepared command.
        if (record) {
            record->decode_context = nullptr; record->terrain_animation_phases = nullptr;
            record->terrain_phase_values = nullptr; record->owned_texture = {};
        }
        output.context.borrowed_view = nullptr;
        output.context = {};
        return false;
    }
    output.view = *record;
    record->decode_context = nullptr; record->terrain_animation_phases = nullptr;
    record->terrain_phase_values = nullptr; record->owned_texture = {};
    output.context.borrowed_view = nullptr;
    output.terrain = source.terrain; output.geometry = source.geometry;
    output.prepared = source.prepared;
    output.dynamic = std::move(dynamic); output.atlas = std::move(atlas);
    output.view.world.object_to_world = output.view.lighting.object_to_world = transform;
    output.view.world.world_to_clip = owned::bridge_scene_world_to_clip_snapshot().world_to_clip;
    output.view.owned_texture.identity = output.atlas->id;
    output.view.owned_texture.generation = output.atlas->generation;
    output.view.owned_texture.surface = output.atlas.get();
    return true;
} catch (...) {
    output.context.borrowed_view = nullptr;
    output.view = {}; output.context = {};
    output.terrain.reset(); output.geometry.reset(); output.dynamic.reset(); output.atlas.reset();
    output.prepared.reset();
    return false;
}
bool E1PublishTerrainFrame(const void* owner, const owned::OwnedTerrainGeometryValues& input,
    const std::array<float,16>& placement) noexcept try {
    terrainFrame = 0; sectorFrame = 0; viewReady = false;
    const auto frame = owned::bridge_scene_world_to_clip_snapshot();
    if (!owner || owner != sourceOwner || !source.terrain || !frame.valid || !frame.scene_frame_serial ||
        E1WorldGeneration() != worldIdentity ||
        !input.revision || input.layer_shifts.size() != source.terrain->objects.size()) return false;
    for (float v : placement) if (!std::isfinite(v)) return false;
    for (float v : input.layer_shifts) if (!std::isfinite(v)) return false;
    for (float v : input.wind_noise) if (!std::isfinite(v)) return false;
    const auto& a = input.animation;
    if (!std::isfinite(a.wind_direction[0]) || !std::isfinite(a.wind_direction[1]) ||
        !std::isfinite(a.wind_force) || !std::isfinite(a.wind_force_normalized) ||
        !std::isfinite(a.wind_phase) || !std::isfinite(a.precise_time)) return false;
    auto completed = std::make_shared<owned::OwnedTerrainGeometryValues>(input);
    if (++geometrySerial == 0) ++geometrySerial;
    completed->revision = geometrySerial;
    if (source.geometry) completed->layer_shift_range = source.geometry->layer_shift_range;
    owned::grow_owned_terrain_layer_shift_range(completed->layer_shift_range, completed->layer_shifts);
    completed->structural_revision = source.geometry ? source.geometry->structural_revision : geometrySerial;
    completed->noise_revision = source.geometry ? source.geometry->noise_revision : geometrySerial;
    if (source.geometry && source.geometry->layer_shift_range != completed->layer_shift_range)
        completed->structural_revision = geometrySerial;
    if (source.geometry && source.geometry->wind_noise != completed->wind_noise)
        completed->noise_revision = geometrySerial;
    source.geometry = std::move(completed);
    transform = placement; terrainFrame = frame.scene_frame_serial; return true;
} catch (...) { terrainFrame = 0; return false; }
}
namespace owned {
void bridge_set_terrain_lod_enabled(bool enabled) noexcept {
    if (terrainLod != enabled) { terrainLod = enabled; viewReady = false; }
}
bool bridge_terrain_atlas(std::uint32_t material, std::shared_ptr<const OwnedVisualSurface>& output,
    const char** failure) noexcept {
    output.reset();
    if (failure) *failure = nullptr;
    const char* why = "terrain frame unavailable";
    if (current()) {
        why = "terrain atlas not loaded";
        if (material < atlasIds.size()) output = OwnedRender::E1SurfaceValue(atlasIds[material]);
        if (output) return true;
    }
    if (failure) *failure = why;
    return false;
}
bool bridge_ambient_world_source(AmbientWorldSource& output) noexcept {
    output = {};
    if (!current() || !source.terrain || !source.geometry) return false;
    output.terrain = source.terrain; output.geometry = source.geometry;
    // No fabricated MOB snapshot: map-dependent effects remain unavailable
    // until the main retained scene publisher supplies its real map source.
    const auto& a = source.geometry->animation;
    output.wind_direction[0] = a.wind_direction[0]; output.wind_direction[1] = a.wind_direction[1];
    output.wind_force_normalized = a.wind_force_normalized;
    return true;
}
std::shared_ptr<const WorldMistGrid> bridge_world_mist_grid() noexcept {
    if (!current()) return {};
    return mist.get(source, transform);
}
}
