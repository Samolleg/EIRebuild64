#include "E1Terrain.h"
#include "E1PerfDiagnostics.h"
#include "E1World.h"
#include "E1Scene.h"
#include "E1Surface.h"
#include "world_mist_grid.h"
#include <cmath>
#include "frame_waits.h"

namespace {
const void* sourceOwner = nullptr;
std::uint64_t worldIdentity = 0, sourceRevision = 0, nextGeneration = 0, terrainFrame = 0, geometrySerial = 0;
owned::RetainedTerrainSourceSnapshot source;
std::vector<std::uint64_t> atlasIds;
std::array<float,16> transform{};
owned::WorldMistGridCache mist;
using TerrainView = owned::OwnedTerrainFramePacket;
using TerrainSectors = std::vector<std::shared_ptr<const owned::OwnedTerrainDynamicSector>>;
std::shared_ptr<TerrainView> view, nextView;
struct BoundsKey {
    std::uint64_t structuralRevision = 0;
    float wind = 0;
    bool waves = false;
    bool matches(const owned::OwnedTerrainGeometryValues& geometry) const noexcept {
        return structuralRevision == geometry.structural_revision &&
            waves == bool(geometry.animation.waves_enabled) &&
            (!waves || wind == std::fabs(geometry.animation.wind_force));
    }
    void read(const owned::OwnedTerrainGeometryValues& geometry) noexcept {
        structuralRevision = geometry.structural_revision;
        waves = geometry.animation.waves_enabled != 0;
        wind = std::fabs(geometry.animation.wind_force);
    }
};
BoundsKey viewBounds, nextBounds;
thread_local const OwnedRender::E1TerrainDrawInputs* drawing = nullptr;
owned::OwnedTerrainPreparedResources prepared;
bool viewReady = false, terrainLod = true;
float terrainHiddenDistance = 0.f;
std::array<owned::NativeOwnedFixedState,2> materials{};
std::array<std::uint64_t,2> materialGeometry{};
std::shared_ptr<const TerrainSectors> sectors;
std::uint64_t sectorFrame = 0, phaseSerial = 0;
void clear() noexcept {
    sourceOwner = nullptr; worldIdentity = sourceRevision = terrainFrame = 0;
    source = {}; transform = {}; mist.retire(); atlasIds.clear(); view.reset(); nextView.reset(); prepared = {}; viewReady = false;
    sectors.reset(); sectorFrame = 0; materials = {}; materialGeometry = {};
}
bool current() noexcept {
    const auto frame = owned::bridge_scene_world_to_clip_snapshot();
    return sourceOwner && terrainFrame && frame.valid && frame.scene_frame_serial == terrainFrame &&
        OwnedRender::E1WorldGeneration() == worldIdentity;
}
}
namespace OwnedRender {
std::shared_ptr<const owned::TerrainAtlasSnapshot> E1CaptureTerrainAtlases() {
    auto snapshot = std::make_shared<owned::TerrainAtlasSnapshot>();
    if (current()) {
        snapshot->surfaces.reserve(atlasIds.size());
        snapshot->failures.reserve(atlasIds.size());
        for (const auto id : atlasIds) {
            auto surface = E1SurfaceValue(id);
            snapshot->failures.push_back(surface ? nullptr : "terrain atlas not loaded");
            snapshot->surfaces.push_back(std::move(surface));
        }
    }
    return snapshot; // Survives surface replacement and world retirement.
}
bool E1PhysicsTerrainSource(owned::RetainedTerrainSourceSnapshot& output) noexcept {
    output = {};
    if (!sourceOwner || !source.terrain || OwnedRender::E1WorldGeneration() != worldIdentity) return false;
    output = source;
    return true;
}
bool E1CurrentTerrainSource(owned::RetainedTerrainSourceSnapshot& output) noexcept {
    output = {};
    if (drawing) { output = drawing->source; return bool(output.terrain); }
    if (!current()) return false;
    output = source;
    return bool(output.terrain);
}
bool E1ShadowTerrain(owned::EmbeddedVisualFrameInputs& inputs,
    owned::RetainedTerrainSourceSnapshot& pinned) noexcept {
    const auto host = drawing ? E1TerrainDrawInputs{} : E1CaptureTerrainDraw(false);
    const auto& pass = drawing ? *drawing : host;
    if (!pass.view || pass.transparent) return false;
    pinned = pass.source;
    inputs.terrain_source = &pinned;
    inputs.terrain_frame = pass.view.get();
    inputs.terrain_dynamic = pass.sectors.get();
    inputs.terrain_to_world = pass.transform;
    inputs.terrain_material = pass.material;
    return true;
}
bool E1PublishTerrainSource(const void* owner, std::uint64_t world, std::uint64_t revision,
    const char* project, const char* header, E1TerrainRead read, void* context,
    std::string& failure, E1TerrainRead readTexture, void* textureContext) noexcept try {
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
    const auto copy = [&](E1TerrainRead reader, void* readerContext, const std::string& name,
        owned::ResourceBlob& blob, owned::ResourceFailure& why) {
        blob = {};
        std::uint32_t bytes = 0;
        HRESULT hr = reader(readerContext, name.c_str(), nullptr, 0, &bytes);
        if (FAILED(hr) || !bytes) {
            why = {owned::ResourceFailureCode::file_read, "terrain member size unavailable: " + name}; return false;
        }
        auto bytesCopy = std::make_shared<std::vector<std::uint8_t>>(bytes);
        std::uint32_t actual = 0;
        hr = reader(readerContext, name.c_str(), bytesCopy->data(), bytes, &actual);
        if (FAILED(hr) || actual != bytes) {
            why = {owned::ResourceFailureCode::file_read, "terrain member changed during copy: " + name}; return false;
        }
        blob.identity = identity; blob.identity.canonical_name = name; blob.identity.bytes = bytes;
        blob.bytes = std::move(bytesCopy); return true;
    };
    const auto resolve = [&](const std::string& name, owned::ResourceBlob& blob, owned::ResourceFailure& why) {
        return copy(read, context, name, blob, why);
    };
    if (!owned::parse_terrain_members(identity, project, header, resolve, *terrain, error) ||
        !owned::validate_terrain_asset_descriptor_once(*terrain)) {
        failure = error.message.empty() ? "terrain descriptor invalid" : error.message; return false;
    }
    // The retail map loader classifies painted water/swamp transitions from
    // selected CPU atlas bytes. Host GPU surfaces alone cannot supply them.
    // Keep the host's existing surface/mip upload path; these pages supply
    // only the shared classifier and its generated swamp frame.
    if (readTexture) {
        terrain->pages.resize(terrain->texture_resources.size());
        for (std::size_t page = 0; page < terrain->pages.size(); ++page) {
            owned::ResourceBlob blob;
            auto texture = std::make_shared<owned::TextureCpuAsset>();
            if (copy(readTexture, textureContext, terrain->texture_resources[page] + ".mmp", blob, error) &&
                owned::parse_texture_asset(blob, *texture, error))
                terrain->pages[page].texture = std::move(texture);
        }
        owned::owned_terrain_classify_swamp_frames(*terrain);
        // Classification owns its resulting cover/texels. Host draws use
        // E1Surface GPU atlases, not these temporary MMP source copies.
        terrain->pages.clear();
    }
    // Active-map geometry is published from the engine's map members, not
    // from OwnedResourceLoader's separate terrain asset. Resolve the atlas
    // profile through the same selected texture resources as these atlases.
    const owned::TerrainMemberResolver profile = [&](const std::string& name,
        owned::ResourceBlob& blob, owned::ResourceFailure& why) {
        return readTexture && copy(readTexture, textureContext, name, blob, why);
    };
    owned::owned_terrain_prepare_astral(profile, *terrain);
    const auto astralSlots = std::count_if(terrain->astral_slots.begin(), terrain->astral_slots.end(),
        [](auto mask) { return mask != 0; });
    if (astralSlots) {
        std::size_t tiles = 0;
        for (const auto& sector : terrain->sectors) for (const auto& tile : sector.tiles)
            if (!tile.transparent && tile.animation_ordinal == UINT_MAX &&
                tile.tile_index < terrain->astral_slots.size() && terrain->astral_slots[tile.tile_index]) ++tiles;
        owned::bridge_append_probe_event("astral_terrain_active", (terrain->map_header_resource +
            ": slots=" + std::to_string(astralSlots) + " placed_tiles=" + std::to_string(tiles)).c_str());
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
    if (!source.terrain) return;
    const auto required = std::size_t(source.terrain->texture_count);
    const auto capacity = required + 2u; // The two long-tile atlases are optional in the map.
    if (surfaces.size() < required || surfaces.size() > capacity) return;
    atlasIds = surfaces;
    atlasIds.resize(capacity, 0); // Preserve absent trailing slots without shifting real atlas indices.
} catch (...) { atlasIds.clear(); }
void E1RetireTerrain(const void* owner) noexcept { if (owner == sourceOwner) clear(); }
bool E1PublishTerrainSectors(const void* owner,
    const std::vector<E1TerrainSectorValues>& input) noexcept try {
    static OwnedRender::E1PerfCounter counter{"terrain_sectors"};
    OwnedRender::E1PerfScope timing(counter);
    sectorFrame = 0;
    if (!owner || owner != sourceOwner || !current() || !source.geometry) return false;
    auto next = std::make_shared<TerrainSectors>(source.terrain->sectors.size());
    for (const auto& row : input) {
        if (row.sector >= next->size() || (*next)[row.sector]) return false;
        const auto& authored = source.terrain->sectors[row.sector];
        if (row.phases.size() != authored.animation_count) return false;
        const auto previous = sectors && row.sector < sectors->size() ? (*sectors)[row.sector] : nullptr;
        const bool samePhases = previous && previous->source_revision == source.source_revision &&
            previous->animation_phases == row.phases;
        if (!samePhases && authored.animation_count != 0) {
            for (const auto& tile : authored.tiles) {
                if (tile.animation_ordinal == UINT_MAX) continue;
                if (tile.animation_ordinal >= row.phases.size() ||
                    row.phases[tile.animation_ordinal] >= tile.animation_phases) return false;
            }
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
        if (samePhases)
            value->phase_values = previous->phase_values;
        else {
            if (++phaseSerial == 0) ++phaseSerial;
            value->phase_values.revision = phaseSerial;
        }
        (*next)[row.sector] = std::move(value);
    }
    sectors = std::move(next);
    sectorFrame = terrainFrame;
    return true;
} catch (...) { sectorFrame = 0; return false; }
std::shared_ptr<const owned::OwnedTerrainDynamicSector> E1TerrainSector(std::uint32_t sector) noexcept {
    const auto& rows = drawing ? drawing->sectors : sectors;
    if (!drawing && (!current() || sectorFrame != terrainFrame)) return {};
    return rows && sector < rows->size() ? (*rows)[sector] : nullptr;
}
E1TerrainDrawInputs E1CaptureTerrainDraw(bool transparent) noexcept {
    E1TerrainDrawInputs pass;
    const unsigned slot = transparent ? 1 : 0;
    if (!viewReady || !current() || !source.geometry || !view ||
        materialGeometry[slot] != source.geometry->revision || !materials[slot].enabled) return pass;
    pass.source = source;
    pass.view = view;
    if (sectorFrame == terrainFrame) pass.sectors = sectors;
    pass.transform = transform;
    pass.material = materials[slot];
    pass.transparent = transparent;
    return pass;
}
E1TerrainDrawScope::E1TerrainDrawScope(const E1TerrainDrawInputs& inputs) noexcept
    : previous(drawing) { drawing = &inputs; }
E1TerrainDrawScope::~E1TerrainDrawScope() { drawing = previous; }

// Stage the next frame's visibility/LOD packet during this frame's world pass.
// Dynamic state and spatial bounds are always resolved from the actual next
// geometry. A camera, resident-domain or LOD change rejects this staging slot.
void E1StageNextTerrainView() noexcept try {
    if (!viewReady || !view || !current()) return;
    const bool measure = RuntimeDiagnostics::Enabled();
    auto& timing = owned::frame_waits::worldPipeline;
    if (measure) timing.stageBegin = owned::frame_waits::now();
    struct FinishStage {
        bool enabled;
        ~FinishStage() { if (enabled) owned::frame_waits::worldPipeline.stageEnd = owned::frame_waits::now(); }
    } finish{measure};
    if (!nextView || !nextView.unique()) nextView = std::make_shared<TerrainView>();
    auto& staged = *nextView;
    const bool sameSource = staged.source == source.terrain &&
        staged.status == owned::OwnedTerrainPacketStatus::ready;
    // Serial and wind phase vary every frame. Culling uses the camera/domain,
    // placement, structural layer range and absolute wave amplitude instead.
    staged.inputs.world_frame_serial = view->inputs.world_frame_serial;
    staged.inputs.dynamic_state_fingerprint = view->inputs.dynamic_state_fingerprint;
    if (sameSource && staged.spatial_ready &&
        owned::owned_terrain_frame_inputs_equal(staged.inputs, view->inputs) &&
        nextBounds.matches(*source.geometry) && staged.spatial_object_to_world == transform) return;
    // The current packet has exactly these visibility and bounds decisions.
    // A separate writable slot permits independent timestamps on adjacent frames.
    staged = *view;
    nextBounds = viewBounds;
} catch (...) { nextView.reset(); }
bool E1PublishTerrainView(const void* owner, const owned::OwnedTerrainFrameInputs& input) noexcept try {
    static OwnedRender::E1PerfCounter counter{"terrain_view"};
    OwnedRender::E1PerfScope timing(counter);
    viewReady = false;
    if (!owner || owner != sourceOwner || !current() || !source.geometry) return false;
    auto values = input;
    values.source_generation = source.source_generation;
    values.source_revision = source.source_revision;
    values.world_frame_serial = terrainFrame;
    values.dynamic_state_fingerprint = source.geometry->revision;
    values.force_full_detail = !terrainLod;
    values.lod_hidden_distance = values.force_full_detail ? 0.f : terrainHiddenDistance;
    view.swap(nextView);
    std::swap(viewBounds, nextBounds);
    if (!view || !view.unique()) view = std::make_shared<TerrainView>();
    auto& packet = *view;
    packet.inputs.world_frame_serial = values.world_frame_serial;
    packet.inputs.dynamic_state_fingerprint = values.dynamic_state_fingerprint;
    const bool staged = packet.status == owned::OwnedTerrainPacketStatus::ready &&
        packet.source == source.terrain && owned::owned_terrain_frame_inputs_equal(packet.inputs, values);
    const bool reuseSpatial = staged && packet.spatial_ready && viewBounds.matches(*source.geometry) &&
        packet.spatial_object_to_world == transform;
    if (reuseSpatial) {
        packet.spatial_geometry_revision = source.geometry->revision;
        if (RuntimeDiagnostics::Enabled()) ++owned::frame_waits::worldPipeline.reused;
    } else if ((!staged && !owned::prepare_owned_terrain_frame(source.terrain, values, packet)) ||
        packet.status != owned::OwnedTerrainPacketStatus::ready ||
        !owned::prepare_owned_terrain_spatial_parts(packet, *source.geometry, transform, &prepared) ||
        !owned::prepare_owned_terrain_render_commands(packet)) return false;
    viewBounds.read(*source.geometry);
    viewReady = true;
    return true;
} catch (...) { viewReady = false; return false; }
const owned::OwnedTerrainFramePacket* E1TerrainView() noexcept {
    return drawing ? drawing->view.get() : (viewReady && current() ? view.get() : nullptr);
}
bool E1PublishTerrainMaterial(const void* owner, bool transparent,
    const owned::NativeOwnedFixedState& state) noexcept {
    const unsigned pass = transparent ? 1u : 0u;
    materialGeometry[pass] = 0; materials[pass] = {};
    if (!owner || owner != sourceOwner || !current() || !source.geometry || !state.enabled ||
        !state.replay_frame_state || !state.captured_viewport[2] || !state.captured_viewport[3] ||
        !std::isfinite(state.captured_depth_range[0]) || !std::isfinite(state.captured_depth_range[1]) ||
        state.captured_depth_range[0] < 0.f || state.captured_depth_range[1] > 1.f ||
        state.captured_depth_range[0] > state.captured_depth_range[1]) return false;
    materials[pass] = transparent ? state : owned::owned_terrain_base_material(state);
    materialGeometry[pass] = source.geometry->revision;
    return true;
}
bool E1PrepareTerrainPass(bool transparent, E1TerrainPassSource& output) noexcept {
    output = {};
    const auto* packet = E1TerrainView();
    const auto& data = drawing ? drawing->source : source;
    const unsigned pass = transparent ? 1u : 0u;
    if (!packet || !data.geometry || (drawing ? drawing->transparent != transparent :
        materialGeometry[pass] != data.geometry->revision || !materials[pass].enabled) ||
        !E1ReadWorldLighting(output.lighting)) return false;
    output.geometry = data.geometry;
    output.commands = transparent ? packet->transparent_commands.size() : packet->base_commands.size();
    return true;
}
bool E1PrepareTerrainCommand(bool transparent, std::size_t index, E1TerrainCommandSource& output) noexcept try {
    output.view = {}; output.context = {};
    output.lighting = {};
    output.terrain.reset(); output.geometry.reset(); output.dynamic.reset(); output.atlas.reset();
    output.prepared.reset();
    const unsigned pass = transparent ? 1u : 0u;
    const auto* packet = E1TerrainView();
    const auto& data = drawing ? drawing->source : source;
    if (!packet || !data.geometry || (drawing ? drawing->transparent != transparent :
        materialGeometry[pass] != data.geometry->revision || !materials[pass].enabled) ||
        !E1ReadWorldLighting(output.lighting)) return false;
    const auto& commands = transparent ? packet->transparent_commands : packet->base_commands;
    if (index >= commands.size()) return false;
    const auto& command = commands[index];
    auto dynamic = E1TerrainSector(command.source_sector);
    std::shared_ptr<const owned::OwnedVisualSurface> atlas;
    if (!owned::bridge_terrain_atlas(command.material % (data.terrain->texture_count+2u), atlas))
        return false;
    owned::NativeGeometryView* record = nullptr;
    if (!owned::prepare_owned_terrain_world_record(data, *packet, command, dynamic.get(),
        {true, true, 0}, output.context, record, false, nullptr) || !record) {
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
    output.view.owned_fixed_state = drawing ? drawing->material : materials[pass];
    record->decode_context = nullptr; record->terrain_animation_phases = nullptr;
    record->terrain_phase_values = nullptr; record->owned_texture = {};
    output.context.borrowed_view = nullptr;
    output.terrain = data.terrain; output.geometry = data.geometry;
    output.prepared = data.prepared;
    output.dynamic = std::move(dynamic); output.atlas = std::move(atlas);
    output.view.world.object_to_world = output.view.lighting.object_to_world = drawing ? drawing->transform : transform;
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
    static OwnedRender::E1PerfCounter counter{"terrain_frame"};
    OwnedRender::E1PerfScope timing(counter);
    terrainFrame = 0; sectorFrame = 0; viewReady = false; materials = {}; materialGeometry = {};
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
void bridge_set_terrain_lod_hidden_distance(float metres) noexcept {
    // Match the author: never simplify visible ground before the fog fade ends.
    terrainHiddenDistance = std::isfinite(metres) && metres > 0.f ? std::ceil(metres / 8.f) * 8.f : 0.f;
}
void bridge_set_terrain_lod_enabled(bool enabled) noexcept {
    if (terrainLod != enabled) { terrainLod = enabled; viewReady = false; }
}
bool bridge_terrain_atlas(std::uint32_t material, std::shared_ptr<const OwnedVisualSurface>& output,
    const char** failure) noexcept {
    output.reset();
    if (failure) *failure = nullptr;
    if (const auto* frame = bridge_embedded_installed_frame()) {
        const auto& atlases = frame->terrain_atlases;
        if (atlases && material < atlases->surfaces.size()) {
            output = atlases->surfaces[material];
            if (failure) *failure = atlases->failures[material];
        } else if (failure) *failure = "terrain frame unavailable";
        return bool(output);
    }
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
    // The scene owner retains the exact main MOB/MPR snapshot published by the
    // server loader. Pair it with the current terrain by its internal header.
    auto& scene = OwnedRender::E1Scene();
    if (scene.CurrentWorld(worldIdentity)) {
        const auto mapSource = scene.Scene().terrain_source_snapshot();
        // The host project name is synthesized from the internal .mp member;
        // it is not the external MPR filename published by the MOB loader.
        if (mapSource.map && mapSource.map->terrain) {
            const auto& loaded = *mapSource.map->terrain;
            const auto& live = *source.terrain;
            const auto sameMember = [](const std::string& a, const std::string& b) {
                if (a.size() != b.size()) return false;
                for (std::size_t i = 0; i < a.size(); ++i) {
                    const auto fold = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; };
                    if (fold(a[i]) != fold(b[i])) return false;
                }
                return !a.empty();
            };
            if (sameMember(loaded.map_header_resource, live.map_header_resource) &&
                loaded.sector_count_x == live.sector_count_x &&
                loaded.sector_count_y == live.sector_count_y)
                output.map = mapSource.map;
        }
    }
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
