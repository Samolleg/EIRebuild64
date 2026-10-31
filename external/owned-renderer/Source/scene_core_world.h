#pragma once
// Core mode (S3): the bridge's queued world work as typed commands. The game
// process captures each at the seam exactly as for its scene thread
// (engine_bridge.cpp) and sends it; the host runs the scene half
// (owned_world_*.cpp) from the copy on its own Renderer (Host/core_world.cpp).
//
// What does not cross: the retained visual frame values (the host evaluates
// its replica at the command's time; the replica barrier makes it the scene
// the game had), the terrain source snapshot (the host takes its own replica's
// snapshot and checks generation and revision), and every game pointer. The
// terrain texture wrappers and the base-pass renderer cross only as opaque
// 64-bit tokens: the scene half compares and null-checks them, never
// dereferences them (bind_owned_terrain_texture binds by index from the
// command's surface snapshots).
#include "owned_world_frame.h"
#include "scene_core_types.h"

SCENE_WIRE_POD_NAMED(owned::OwnedTerrainSectorState, terrain_sector_state)
SCENE_WIRE_POD_NAMED(owned::OwnedTerrainSpatialPart, terrain_spatial_part)
SCENE_WIRE_POD_NAMED(owned::OwnedTerrainCommand, terrain_command)
SCENE_WIRE_POD_NAMED(owned::ground_cover::FootprintSplat, footprint_splat)
SCENE_WIRE_POD_NAMED(owned::FootprintTrailPoint, footprint_trail_point)
SCENE_WIRE_POD_NAMED(owned::LifecycleIdentity, lifecycle_identity)

namespace owned {

// The device state a queued world command's sinks read (SceneSinks).
struct SceneSinkState {
    FixedState state{};
    NativeFrameState frame{};
    bool frame_valid = false;
    std::shared_ptr<SurfaceImage> bound;
    // ---- core mode: the camp placement at the capture (claude/s3-store) ----
    camp::SubmissionState camp_state{};
};
template <class A> void fields(A& a, SceneSinkState& v) {
    scene_wire::fields_of(a, v.state, v.frame, v.frame_valid, v.bound);
    camp_placement_fields(a, v.camp_state);
}

template <class A> void fields(A& a, NativeWorldLightingFrame& v) { scene_wire::fields_of(a, v.sampled, v.result, v.lighting, v.cave); }

template <class A> void fields(A& a, OwnedTerrainFrameInputs& v) {
    scene_wire::fields_of(a, v.source_generation, v.source_revision, v.world_frame_serial, v.terrain_origin,
                          v.camera_position, v.lod_distances, v.maximum_lod, v.force_full_detail,
                          v.runtime_texture_count, v.terrain_render_state, v.game_visibility_fingerprint,
                          v.game_visibility_domain_present, v.game_visibility_min_y, v.game_visibility_rows,
                          v.dynamic_state_fingerprint, v.object_to_clip);
}
template <class A> void fields(A& a, OwnedTerrainDynamicSector& v) {
    scene_wire::fields_of(a, v.captured, v.source_revision, v.structural_generation, v.structural_copy_count, v.layer_z,
                          v.wind_noise, v.animation, v.animation_phases, v.phase_values.revision);
}

template <class A> void fields(A& a, OwnedWoundRequest& v) {
    std::uint64_t sample = v.sample, part = v.part, wound = v.wound;
    scene_wire::fields_of(a, sample, part, v.fog, wound);
    if constexpr (A::decoding) {
        v.sample = static_cast<std::size_t>(sample); v.part = static_cast<std::size_t>(part);
        v.wound = static_cast<std::uintptr_t>(wound);
    }
}
template <class A> void fields(A& a, OwnedPreviewPart& v) {
    std::uint64_t ordinal = v.ordinal, set = v.set, wound = v.wound;
    scene_wire::fields_of(a, ordinal, set, v.material, v.uses_default_colors, v.default_diffuse, v.default_specular,
                          v.lighting, v.material_flags, wound, v.wound_texture);
    if constexpr (A::decoding) {
        v.ordinal = static_cast<std::size_t>(ordinal); v.set = static_cast<std::size_t>(set);
        v.wound = static_cast<std::uintptr_t>(wound);
    }
}
template <class A> void fields(A& a, GroundCoverFootprintSnapshot& v) { scene_wire::fields_of(a, v.ok, v.splats, v.trail_points); }

namespace scene_core {

// The host's replica terrain source with the game's generation and revision,
// else empty (that frame's terrain is omitted, logged once per mismatch).
RetainedTerrainSourceSnapshot core_terrain_source(std::uint64_t generation, std::uint64_t revision, bool present);

template <class A> void opaque_pointer(A& a, void*& value) {
    std::uint64_t wide = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value));
    scene_wire::field(a, wide);
    if constexpr (A::decoding) value = reinterpret_cast<void*>(static_cast<std::uintptr_t>(wide));
}

template <class A> void terrain_frame_fields(A& a, OwnedTerrainRuntimeFrame& v) {
    auto& f = v.front_end;
    std::uint8_t source_is_values = f.source && f.source == v.source_values.terrain ? 1 : 0;
    std::uint8_t source_present = v.source_values.terrain ? 1 : 0;
    std::uint64_t generation = v.source_values.source_generation, revision = v.source_values.source_revision;
    scene_wire::fields_of(a, f.status, source_is_values, f.inputs, f.sectors, f.spatial_parts, f.base_commands,
                          f.transparent_commands, f.spatial_object_to_world, f.spatial_geometry_revision,
                          f.spatial_bounds_computations, f.spatial_bounds_reuses, f.spatial_front_end_reuses,
                          f.spatial_receiver_reuses, f.spatial_caster_reuses, f.spatial_record_reuses,
                          f.preparation_count, f.base_submission_count, f.transparent_reuse_count, f.base_requested,
                          f.base_submitted, f.failed_after_claim, f.spatial_ready, source_present, generation, revision,
                          v.dynamic, v.publication_seen);
    std::uint32_t handles = static_cast<std::uint32_t>(v.texture_handles.size());
    scene_wire::field(a, handles);
    if constexpr (A::decoding) {
        if (!a.plausible(handles, 8)) return;
        v.texture_handles.assign(handles, nullptr);
    }
    for (auto& handle : v.texture_handles) opaque_pointer(a, handle);
    scene_wire::fields_of(a, v.shadow_texture_observed, v.shadow_material, v.world_to_clip, v.object_to_world,
                          v.world_transform_valid, v.world_serial, v.base_attempted, v.base_publication_ran,
                          v.base_pending);
    opaque_pointer(a, v.base_renderer);
    if constexpr (A::decoding) {
        v.source_values = core_terrain_source(generation, revision, source_present != 0);
        f.source = source_is_values ? v.source_values.terrain : nullptr;
    }
}

// post_scene_world_begin / post_scene_world_end: the outer world draw's place
// in the stream.
struct WorldBegin {
    static constexpr bool frame_values = true;
    std::uint64_t profile_frame = 0;
    void run(Renderer& renderer);
};
template <class A> void fields(A& a, WorldBegin& v) { scene_wire::field(a, v.profile_frame); }
struct WorldEnd {
    static constexpr bool frame_values = true;
    void run(Renderer& renderer);
};
template <class A> void fields(A&, WorldEnd&) {}

// queue_owned_visual_scene: the seam's whole world frame.
struct WorldScene {
    static constexpr bool frame_values = true;
    std::uint64_t world_sequence = 0;  // the game thread's count of world frames sent (wound layers name it)
    std::array<float, 16> world_to_clip{};
    float camera_anchor_z = 0;  // the game camera's anchor height (NaN unknown)
    NativeFrameState pass{};
    double time = 0;
    std::uint32_t flags = 0;
    bool draw_undetected = false;
    std::uint64_t shadow_source_revision = 0;
    OwnedTerrainRuntimeFrame terrain;
    NativeWorldLightingFrame lighting{};
    std::vector<std::int16_t> fog;
    // The entity each fog value belongs to (the game process's frame order):
    // the host's frame is evaluated from its replica, so the values are
    // matched by entity, never by position.
    std::vector<std::int32_t> fog_ids;
    std::vector<std::uint64_t> fog_serials;
    std::uint64_t fog_revision = 0;
    std::vector<std::shared_ptr<SurfaceImage>> terrain_textures;
    std::vector<std::shared_ptr<const OwnedVisualSurface>> terrain_shadow_textures;
    GroundCoverFootprintSnapshot footprints;
    SceneSinkState sinks;
    void run(Renderer& renderer);
};
template <class A> void fields(A& a, WorldScene& v) {
    scene_wire::fields_of(a, v.world_sequence, v.world_to_clip, v.camera_anchor_z, v.pass, v.time, v.flags, v.draw_undetected,
                          v.shadow_source_revision);
    terrain_frame_fields(a, v.terrain);
    scene_wire::fields_of(a, v.lighting, v.fog, v.fog_ids, v.fog_serials, v.fog_revision, v.terrain_textures, v.terrain_shadow_textures,
                          v.footprints, v.sinks);
}

// The transparent terrain range (draw_source_owned_terrain_range's
// submit_transparent action).
struct TransparentTerrain {
    static constexpr bool frame_values = true;
    OwnedTerrainRuntimeFrame terrain;
    NativeWorldLightingFrame lighting{};
    std::vector<std::shared_ptr<SurfaceImage>> textures;
    void* renderer = nullptr;  // opaque token (non-null check only)
    SceneSinkState sinks;
    void run(Renderer& renderer);
};
template <class A> void fields(A& a, TransparentTerrain& v) {
    terrain_frame_fields(a, v.terrain);
    scene_wire::fields_of(a, v.lighting, v.textures);
    opaque_pointer(a, v.renderer);
    scene_wire::field(a, v.sinks);
}

// queue_owned_wound_layer: one object's wound parts behind the world frame
// world_sequence names. The wound surface interface crosses as an opaque
// token: SceneSinks matches it against set_wound's, never dereferences it.
struct WoundLayer {
    static constexpr bool frame_values = true;
    std::uint64_t world_sequence = 0;
    std::vector<OwnedWoundRequest> requests;
    // The entity each request's sample index names in the game process's frame.
    std::vector<std::int32_t> sample_ids;
    std::vector<std::uint64_t> sample_serials;
    std::vector<std::shared_ptr<SurfaceImage>> wounds;
    SceneSinkState sinks;
    void run(Renderer& renderer);
};
template <class A> void fields(A& a, WoundLayer& v) { scene_wire::fields_of(a, v.world_sequence, v.requests, v.sample_ids, v.sample_serials, v.wounds, v.sinks); }

// An object preview (execute_owned_preview_render_plan): the sample comes
// from the host's replica (visual_preview_source plus the selection the game
// published for this resource and view), the pose phase from the game.
struct Preview {
    static constexpr bool frame_values = true;
    VisualResourceIncarnation resource{};
    LifecycleIdentity lifecycle{};
    std::uint32_t view = 0;
    double phase = 0, texture_time = 0;
    BYTE fog = 255;
    std::array<float, 16> base_world{}, world_to_clip{};
    std::uint64_t presentation_serial = 0;
    std::vector<OwnedPreviewPart> parts;
    std::vector<SceneSinkState> sets;
    void run(Renderer& renderer);
};
template <class A> void fields(A& a, Preview& v) {
    scene_wire::fields_of(a, v.resource.signed_id, v.resource.serial, v.lifecycle, v.view, v.phase, v.texture_time, v.fog,
                          v.base_world, v.world_to_clip, v.presentation_serial, v.parts, v.sets);
}

}  // namespace scene_core
}  // namespace owned

