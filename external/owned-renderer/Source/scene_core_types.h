#pragma once
// Core mode (S3): the wire forms of the typed scene commands and of the
// values they carry (scene_wire.h codec, scene_core.h ids). Included by
// renderer.h's core post path and by the host dispatcher, so both ends see
// one definition of every wire form.
//
// Not yet on the wire (listed gaps, each one omitted visibly on the host
// until its milestone sends it):
// - BridgeFrameValues::terrain_atlases (M2: ground cover and contact maps);
// - ScenePresentInputs::profile and outer_timing (F10 and the hitch
//   recorder, with the overlay at S6).
// renderer.h includes scene_core_post.h at its end, which needs this file
// complete: entered from here first, that include is deferred to our end.
#ifndef SCENE_CORE_TYPES_FIRST
#define SCENE_CORE_TYPES_FIRST
#define SCENE_CORE_TYPES_DEFERRED_POST
#endif
#include "owned_resource_store.h"
#include "renderer.h"
#include "scene_commands_ui.h"
#include "scene_core.h"
#include "scene_wire.h"
#include "test_harness_command.h"

// ---- raw wire PODs (layout-checked at connect) ------------------------------
SCENE_WIRE_POD_NAMED(owned::WorldDrawCompletionSnapshot, world_draw_completion)
SCENE_WIRE_POD_NAMED(owned::SceneWorldToClipSnapshot, scene_world_to_clip)
SCENE_WIRE_POD_NAMED(owned::StockDistanceFog, stock_distance_fog)
SCENE_WIRE_POD_NAMED(owned::WorldEnvironment, world_environment)
SCENE_WIRE_POD_NAMED(owned::HudPanelBounds, hud_panel_bounds)
SCENE_WIRE_POD_NAMED(owned::HudAssetFrame, hud_asset_frame)
SCENE_WIRE_POD_NAMED(owned::widescreen::HudComposition, hud_composition)
SCENE_WIRE_POD_NAMED(frame_acct::Record, frame_acct_record)
SCENE_WIRE_POD_NAMED(D3DRECT, d3d_rect)
SCENE_WIRE_POD_NAMED(RECT, rect)
SCENE_WIRE_POD_NAMED(DDPIXELFORMAT, dd_pixel_format)
SCENE_WIRE_POD_NAMED(DDCOLORKEY, dd_color_key)
SCENE_WIRE_POD_NAMED(D3DVIEWPORT7, d3d_viewport7)
SCENE_WIRE_POD_NAMED(owned::FixedState, fixed_state)
SCENE_WIRE_POD_NAMED(owned::camp::Composition, camp_composition)
SCENE_WIRE_POD_NAMED(owned::widescreen::PresentationFit, presentation_fit)
SCENE_WIRE_POD_NAMED(owned::ScreenRectangleRequest, screen_rectangle_request)
SCENE_WIRE_POD_NAMED(owned::scene_ui::SurfaceHandle, surface_handle)
SCENE_WIRE_POD_NAMED(owned::CampDecoration, camp_decoration)
SCENE_WIRE_POD_NAMED(owned::scene_ui::GlyphAssetKey, glyph_asset_key)
SCENE_WIRE_POD_NAMED(POINT, point)
SCENE_WIRE_POD_NAMED(owned::widescreen::Layout, widescreen_layout)
SCENE_WIRE_POD_NAMED(owned::GameParticle, game_particle)
SCENE_WIRE_POD_NAMED(owned::GameParticleGpu, game_particle_gpu)
SCENE_WIRE_POD_NAMED(owned::GameRibbonPoint, game_ribbon_point)
SCENE_WIRE_POD_NAMED(owned::GameRibbonStrip, game_ribbon_strip)
SCENE_WIRE_POD_NAMED(owned::GameSelectionView, game_selection_view)
SCENE_WIRE_POD_NAMED(owned::NativeWorldLighting, native_world_lighting)
SCENE_WIRE_POD_NAMED(owned::NativeContribution, native_contribution)
SCENE_WIRE_POD_NAMED(owned::NativePreparedIdentity, native_prepared_identity)
SCENE_WIRE_POD_NAMED(owned::NativeLighting, native_lighting)
SCENE_WIRE_POD_NAMED(owned::NativeOwnedFixedState, native_owned_fixed_state)
SCENE_WIRE_POD_NAMED(owned::NativeTerrainState, native_terrain_state)
SCENE_WIRE_POD_NAMED(owned::NativePackedGroup, native_packed_group)
SCENE_WIRE_POD_NAMED(owned::NativeTextureCoordinate, native_texture_coordinate)
SCENE_WIRE_POD_NAMED(owned::NativeVertexComponent, native_vertex_component)
SCENE_WIRE_POD_NAMED(owned::NativeMorphOffset, native_morph_offset)
SCENE_WIRE_POD_NAMED(owned::NativeFrameState, native_frame_state)

namespace owned {

template <class A> void fields(A& a, WorldFogSnapshot& v) {
    scene_wire::fields_of(a, v.scene_frame_serial, v.valid, v.enabled);
    scene_wire::static_text(a, v.failure);
    scene_wire::fields_of(a, v.fog, v.environment);
}

// A completed visual surface's facts (no pixels: the host resolves its GPU
// home by id and generation through its surface table).
template <class A> void fields(A& a, std::shared_ptr<const OwnedVisualSurface>& v) {
    std::uint8_t present = v ? 1 : 0;
    scene_wire::field(a, present);
    if (!present) { if constexpr (A::decoding) v.reset(); return; }
    OwnedVisualSurface value = A::decoding ? OwnedVisualSurface{} : *v;
    std::uint8_t has_mask = value.alpha_cut_mask ? 1 : 0;
    std::vector<std::uint8_t> mask = has_mask && !A::decoding ? *value.alpha_cut_mask : std::vector<std::uint8_t>{};
    scene_wire::fields_of(a, value.id, value.generation, value.width, value.height, value.pitch, value.format,
                          value.has_color_key, value.color_key, has_mask, mask, value.alpha_fully_opaque,
                          value.static_mip_hint);
    if constexpr (A::decoding) {
        if (has_mask) value.alpha_cut_mask = std::make_shared<const std::vector<std::uint8_t>>(std::move(mask));
        v = std::make_shared<const OwnedVisualSurface>(std::move(value));
    }
}

// The terrain atlas snapshot of the game thread (bridge_terrain_atlas):
// sent as its own command when the game thread's snapshot changes, never in
// the per-post envelope (its alpha masks are large and it changes rarely).
template <class A> void fields(A& a, std::shared_ptr<const TerrainAtlasSnapshot>& v) {
    std::uint8_t present = v ? 1 : 0;
    scene_wire::field(a, present);
    if (!present) { if constexpr (A::decoding) v.reset(); return; }
    TerrainAtlasSnapshot value = A::decoding ? TerrainAtlasSnapshot{} : *v;
    scene_wire::field(a, value.surfaces);
    std::uint32_t failures = static_cast<std::uint32_t>(value.failures.size());
    scene_wire::field(a, failures);
    if constexpr (A::decoding) {
        if (!a.plausible(failures, 1)) return;
        value.failures.assign(failures, nullptr);
    }
    for (auto& failure : value.failures) scene_wire::static_text(a, failure);
    if constexpr (A::decoding) v = std::make_shared<const TerrainAtlasSnapshot>(std::move(value));
}

// terrain_atlases travel as scene_core::TerrainAtlases (above), not here.
template <class A> void fields(A& a, BridgeFrameValues& v) {
    scene_wire::fields_of(a, v.frame_serial, v.completion, v.completion_counter, v.world_to_clip, v.fog,
                          v.character_pane_ui_epoch, v.chat_text_ui_epoch, v.item_card_text_ui_epoch, v.environment, v.world_time,
                          v.world_time_valid, v.local_single_player, v.owned_sky_requested, v.world_draw_active,
                          v.world_epoch);
}

template <class A> void fields(A& a, HarnessCommand& v) {
    scene_wire::fields_of(a, v.verb, v.number, v.frames, v.name, v.value, v.error);
}

// profile and outer timing are not sent yet (F10 and the hitch recorder).
template <class A> void fields(A& a, ScenePresentInputs& v) {
    scene_wire::fields_of(a, v.harness_command_valid, v.harness_command, v.harness_entry_id, v.harness_camera_steps,
                          v.harness_on_travel_map, v.bridge, v.recorder_present_entry, v.game_frame, v.hud_frames);
}

namespace scene_cmd {
template <class A> void fields(A& a, BeginEngineFrame& v) { scene_wire::field(a, v.world_epoch); }
template <class A> void fields(A&, MarkLoadPicture&) {}
template <class A> void fields(A& a, HitTestFrame& v) { scene_wire::field(a, v.begin); }
template <class A> void fields(A&, BeginScene&) {}
template <class A> void fields(A&, EndScene&) {}
template <class A> void fields(A& a, FlushNative& v) { scene_wire::field(a, v.layout_slot); }
template <class A> void fields(A&, CompositeWorld&) {}
template <class A> void fields(A& a, Clear& v) { scene_wire::fields_of(a, v.rects, v.flags, v.color, v.z, v.stencil); }
}  // namespace scene_cmd

template <class A> void fields(A& a, ProgressMovie& v) {
    scene_wire::fields_of(a, v.width, v.height, v.frames, v.current, v.path);
}

// The texture pointer never crosses: DrawFrame::textures names each packet's
// surface. size_t offsets travel as 64-bit values.
template <class A> void fields(A& a, LegacyDrawPacket& v) {
    std::uint64_t vertex_offset = v.vertex_offset, index_offset = v.index_offset;
    scene_wire::fields_of(a, v.type, v.fvf, v.vertex_count, vertex_offset, v.index_count, index_offset, v.state,
                          v.semantic_kind, v.caller_rva, v.legacy_draw_count, v.hud_composition, v.camp_placement,
                          v.camp_placement_active);
    if constexpr (A::decoding) {
        v.vertex_offset = static_cast<std::size_t>(vertex_offset);
        v.index_offset = static_cast<std::size_t>(index_offset);
        v.texture = nullptr;
    }
}

namespace scene_ui {
template <class A> void fields(A& a, Quad& v) {
    scene_wire::fields_of(a, v.source_id, v.cache_key, v.generation, v.base_generation, v.revision, v.ui_epoch,
                          v.frame_serial, v.native_canvas, v.output_space, v.filtered_text, v.keyed, v.gpu, v.cached,
                          v.partial, v.force_opaque_alpha, v.batchable_text, v.source_is_surface, v.source_destroyed,
                          v.quarter_turn, v.backdrop, v.presentation_fill, v.composition, v.source, v.destination,
                          v.update, v.color_key, v.width, v.height, v.pitch, v.diffuse, v.u0, v.v0, v.u1, v.v1,
                          v.pixels, v.home);
}
template <class A> void fields(A& a, Blits& v) { scene_wire::field(a, v.quads); }
template <class A> void fields(A& a, PresentImage& v) {
    scene_wire::fields_of(a, v.quads, v.fit, v.filled, v.fitted, v.movie, v.movie_valid);
}
template <class A> void fields(A& a, DrawFrame& v) {
    scene_wire::fields_of(a, v.packets, v.textures, v.vertex_bytes, v.indices);
}
template <class A> void fields(A& a, Cursor& v) {
    scene_wire::fields_of(a, v.cache_key, v.pixels, v.width, v.height, v.destination);
}
template <class A> void fields(A& a, RetireSurface& v) { scene_wire::field(a, v.surface_id); }
template <class A> void fields(A&, FlushUi&) {}
template <class A> void fields(A& a, ScreenRectangle& v) { scene_wire::fields_of(a, v.request, v.viewport); }
template <class A> void fields(A& a, HudFrameLayer& v) { scene_wire::field(a, v.frames); }
template <class A> void fields(A& a, CampDecorationFrame& v) { scene_wire::field(a, v.frame); }
template <class A> void fields(A& a, CharacterPaneText& v) {
    std::uint32_t quads = static_cast<std::uint32_t>(v.frame.quads.size());
    scene_wire::fields_of(a, v.frame.width, v.frame.height, quads);
    if constexpr (A::decoding) {
        if (!a.plausible(quads, 16)) return;
        v.frame.quads.resize(quads);
    }
    for (auto& quad : v.frame.quads) {  // asset: by key in assets
        std::uint64_t page = quad.atlas_page_index;
        scene_wire::fields_of(a, page, quad.source, quad.destination, quad.color, quad.raster_scale, quad.anchor, quad.shift,
                              quad.offset, quad.clip);
        if constexpr (A::decoding) { quad.atlas_page_index = static_cast<std::size_t>(page); quad.asset = nullptr; }
    }
    scene_wire::fields_of(a, v.assets, v.ui_epoch, v.frame_serial, v.destination, v.has_source, v.source, v.interface_space, v.observe_scale, v.has_fit,
                          v.fit.scale, v.fit.center_x, v.fit.top, v.fit.bottom);
}
template <class A> void fields(A& a, GlyphAtlas& v) {
    scene_wire::fields_of(a, v.asset.identity, v.asset.generation, v.asset.revision, v.asset.width, v.asset.height,
                          v.asset.coverage);
}
template <class A> void fields(A& a, BeginCampPopup& v) { scene_wire::field(a, v.clear); }
template <class A> void fields(A& a, EndCampPopup& v) { scene_wire::field(a, v.complete); }
template <class A> void fields(A&, ResetCampPopup&) {}
template <class A> void fields(A& a, Capture& v) {
    scene_wire::fields_of(a, v.target, v.box_left, v.box_top, v.box_right, v.box_bottom, v.x, v.y, v.width, v.height);
}
template <class A> void fields(A& a, SurfaceWrite& v) {
    scene_wire::fields_of(a, v.id, v.generation, v.width, v.height, v.pitch, v.format, v.has_color_key, v.color_key,
                          v.static_mip_hint, v.world_mip_use, v.mip_source_used, v.whole, v.dirty, v.first_row, v.rows,
                          v.bytes);
}
}  // namespace scene_ui

// A command's surface snapshot (scene_cmd::SurfaceHandle): its id and
// generation cross; the host resolves them through its surface table while
// it decodes (scene_core::g_decode_renderer, set by the host only).
namespace scene_core { inline thread_local Renderer* g_decode_renderer = nullptr; }
template <class A> void fields(A& a, std::shared_ptr<SurfaceImage>& v) {
    std::uint8_t present = v ? 1 : 0;
    std::uint64_t id = v ? v->id : 0, generation = v ? v->generation : 0;
    scene_wire::fields_of(a, present, id, generation);
    if constexpr (A::decoding) {
        v = present && scene_core::g_decode_renderer ? scene_core::g_decode_renderer->core_surface_snapshot(id, generation)
                                                     : nullptr;
    }
}

template <class A> void fields(A& a, GameParticleView& v) {  // particles/count travel in the command
    scene_wire::fields_of(a, v.object_to_clip, v.clip_half_width, v.clip_half_height, v.additive, v.interface_pass);
    if constexpr (A::decoding) { v.particles = nullptr; v.count = 0; }
}
template <class A> void fields(A& a, GameRibbonView& v) {  // points/count travel in the command
    scene_wire::fields_of(a, v.object_to_clip, v.clip_half_width, v.clip_half_height, v.half_width_pixels_w, v.strip,
                          v.interface_pass);
    if constexpr (A::decoding) { v.points = nullptr; v.count = 0; }
}

namespace scene_cmd {
template <class A> void fields(A& a, GameParticles& v) { scene_wire::fields_of(a, v.view, v.particles, v.state, v.texture); }
template <class A> void fields(A& a, GameRibbon& v) { scene_wire::fields_of(a, v.view, v.points, v.state, v.texture); }
template <class A> void fields(A& a, GameSelection& v) { scene_wire::fields_of(a, v.view, v.state, v.texture); }
template <class A> void fields(A& a, GameGlyphs& v) { scene_wire::fields_of(a, v.glyphs, v.state, v.texture); }
template <class A> void fields(A& a, GamePrintRun& v) {
    scene_wire::fields_of(a, v.vertices, v.object_to_clip, v.clip_half_width, v.clip_half_height, v.state, v.texture);
}
template <class A> void fields(A& a, WorldLighting& v) { scene_wire::fields_of(a, v.lighting, v.environment); }
template <class A> void fields(A&, DiscardNative&) {}
template <class A> void fields(A& a, TakeNativeCheckpoint& v) { scene_wire::field(a, v.checkpoint_id); }
template <class A> void fields(A& a, ReleaseNativeCheckpoint& v) { scene_wire::field(a, v.checkpoint_id); }
template <class A> void fields(A& a, RollbackNative& v) { scene_wire::field(a, v.checkpoint_id); }
template <class A> void fields(A&, AbortDirectionalShadows&) {}
template <class A> void fields(A&, AbortPointShadowFaces&) {}
}  // namespace scene_cmd

// A generic scene packet's view (proxy queueScenePacket; scenePacketEligible
// guarantees the pointer-valued members are null or the arrays the Packet
// owns). The opaque identity tokens and the borrowed-address cache selector
// cross as 64-bit values (never dereferenced); the host's decoded view starts
// value-initialised, so every pointer member is null until Packet::bind.
template <class A> void fields(A& a, NativeOwnedTextureView& v) {
    scene_wire::fields_of(a, v.identity, v.generation, v.format, v.mip_count, v.value_source, v.opaque_mips,
                          v.camp_surface_alias);
}
template <class A> void opaque_token(A& a, std::uintptr_t& value) {
    std::uint64_t wide = value;
    scene_wire::field(a, wide);
    if constexpr (A::decoding) value = static_cast<std::uintptr_t>(wide);
}
template <class A> void packet_view_fields(A& a, NativeGeometryView& v) {
    scene_wire::fields_of(a, v.contact_world_height, v.contact_map, v.contribution);
    opaque_token(a, v.object_token); opaque_token(a, v.figure_token); opaque_token(a, v.geometry_token);
    opaque_token(a, v.wound_surface_interface);
    scene_wire::fields_of(a, v.wound_only, v.prepare_only);
    opaque_token(a, v.cache_identity);
    for (auto& pointer : v.cache_pointers) opaque_token(a, pointer);
    scene_wire::fields_of(a, v.cache_pointers_valid, v.prepared_identity, v.geometry_revision, v.geometry_revision_valid,
                          v.color_revision, v.color_revision_valid, v.bounds_flags, v.bounds_center, v.bounds_radius,
                          v.bounds_minimum, v.bounds_maximum, v.bounds_valid, v.position_groups, v.normal_groups,
                          v.texture_coordinate_count, v.component_count, v.index_count, v.object_to_clip, v.morph_count,
                          v.lighting, v.owned_fixed_state, v.owned_texture, v.terrain_page_count, v.terrain,
                          v.semantic_kind, v.ordering, v.u_shift, v.v_shift, v.default_diffuse, v.default_specular,
                          v.fog, v.flags, v.decoded_channels);
}
template <class A> void fields(A& a, NativeOwnedTextureMip& v) {  // data travels in Packet::mip_bytes
    scene_wire::fields_of(a, v.width, v.height, v.bytes);
    if constexpr (A::decoding) v.data = nullptr;
}

// ---- core mode: camp composition and prepared figures (claude/s3-store) ----
// The game thread's camp placement (camp_layout.h): only while composing.
template <class A> void camp_placement_fields(A& a, camp::SubmissionState& v) {
    scene_wire::field(a, v.composing);
    if (v.composing) scene_wire::fields_of(a, v.visual, v.output, v.glow, v.glow_rgb);
}
// A camp icon surface's facts (the host resolves its GPU home by id and
// generation; the alpha mask stays in the game process).
template <class A> void camp_surface_fields(A& a, bool& present, OwnedVisualSurface& v) {
    scene_wire::field(a, present);
    if (present)
        scene_wire::fields_of(a, v.id, v.generation, v.width, v.height, v.pitch, v.format, v.alpha_fully_opaque);
}

namespace scene_ui {
template <class A> void fields(A& a, Packet& v) {
    packet_view_fields(a, v.view);
    scene_wire::fields_of(a, v.positions, v.normals, v.diffuse, v.specular, v.coordinates, v.components, v.indices,
                          v.morphs, v.mips, v.mip_bytes, v.state, v.frame, v.frame_valid, v.bound, v.wound);
    camp_placement_fields(a, v.camp_state);
    camp_surface_fields(a, v.camp_surface_present, v.camp_surface);
    scene_wire::field(a, v.prepared_handles);
}
template <class A> void fields(A& a, RetireCampTexture& v) { scene_wire::fields_of(a, v.surface_id, v.generation); }
}  // namespace scene_ui

namespace scene_core {

// World commands (scene_core_world.h; the host includes it to run them).
struct WorldBegin; struct WorldEnd; struct WorldScene; struct TransparentTerrain; struct WoundLayer; struct Preview;

// The per-post frame envelope (scene_post_frame's captured values).
struct FrameEnvelope {
    BridgeFrameValues values;
    widescreen::HudComposition hud{};
};
template <class A> void fields(A& a, FrameEnvelope& v) { scene_wire::fields_of(a, v.values, v.hud); }

// The game thread's present (Renderer::queue_present in core mode). serial
// is the client's present count: the host publishes it as presents_done.
struct Present {
    static constexpr bool frame_values = false;  // the values travel in inputs
    std::uint64_t serial = 0;
    bool game_foreground = true;  // the game window is in front (background frame limit, statistics)
    ScenePresentInputs inputs;
    void run(Renderer& renderer);  // host (core_host.cpp)
};
template <class A> void fields(A& a, Present& v) { scene_wire::fields_of(a, v.serial, v.game_foreground, v.inputs); }

// The loaded options of the game process's ini, sent whenever its revision
// moves; the host Renderer sets each registered key it knows.
struct ConfigOption {
    std::string key;
    std::uint8_t type = 0;  // RendererOptionType
    bool boolean_value = false;
    std::int32_t integer_value = 0;
    float floating_value = 0.0f;
    std::string text_value;
};
template <class A> void fields(A& a, ConfigOption& v) {
    scene_wire::fields_of(a, v.key, v.type, v.boolean_value, v.integer_value, v.floating_value, v.text_value);
}
// The game thread's terrain atlas snapshot, sent when it changed; the host's
// bridge_terrain_atlas answers from it (host_bridge.cpp).
struct TerrainAtlases {
    static constexpr bool frame_values = false;
    std::shared_ptr<const TerrainAtlasSnapshot> snapshot;
    void run(Renderer& renderer);  // host (core_host.cpp)
};
template <class A> void fields(A& a, TerrainAtlases& v) { scene_wire::field(a, v.snapshot); }

// The game process's neighbour stream inputs (neighbour_remote.h encoding):
// the host runs the stream in its own process for its Renderer.
struct NeighbourBegin {
    static constexpr bool frame_values = false;
    std::uint64_t epoch = 0;
    std::vector<std::uint8_t> inputs;  // encode_neighbour_begin
    void run(Renderer& renderer);      // host (core_world.cpp)
};
template <class A> void fields(A& a, NeighbourBegin& v) { scene_wire::fields_of(a, v.epoch, v.inputs); }
struct NeighbourStop {
    static constexpr bool frame_values = false;
    std::uint64_t epoch = 0;           // 0: any
    void run(Renderer& renderer);      // host (core_world.cpp)
};
template <class A> void fields(A& a, NeighbourStop& v) { scene_wire::field(a, v.epoch); }

// The game process's hook status the renderer reports or depends on.
struct GameStatus {
    static constexpr bool frame_values = false;
    bool owned_sky_installed = false;
    const char* owned_weather_problem = nullptr;  // static text
    bool semantic_slots_installed = false;
    bool game_sites_all_verified = false;
    void run(Renderer& renderer);  // host (core_host.cpp)
    bool operator==(const GameStatus& other) const noexcept {
        return owned_sky_installed == other.owned_sky_installed && owned_weather_problem == other.owned_weather_problem &&
               semantic_slots_installed == other.semantic_slots_installed &&
               game_sites_all_verified == other.game_sites_all_verified;
    }
};
template <class A> void fields(A& a, GameStatus& v) {
    scene_wire::field(a, v.owned_sky_installed);
    scene_wire::static_text(a, v.owned_weather_problem);
    scene_wire::fields_of(a, v.semantic_slots_installed, v.game_sites_all_verified);
}

// The game window's client size changed (display Apply, mode change): the
// host resizes its child window and swap chain. The game process answers the
// game's call at once (a host failure loses the device, named).
struct Resize {
    static constexpr bool frame_values = false;
    std::uint32_t width = 0, height = 0;
    void run(Renderer& renderer);  // host (core_host.cpp)
};
template <class A> void fields(A& a, Resize& v) { scene_wire::fields_of(a, v.width, v.height); }

// A synchronous readback (core mode): the host packs the source as tight
// BGRA rows and copies bytes [first_byte, first_byte + bytes) into the
// shared readback area, then replies (reply serial). first_byte 0 packs the
// source anew; later chunks reuse that copy. Source: the back buffer box
// (after composing the world and queued UI, as capture does) or a game
// surface's GPU home in the host table (id, generation).
struct Readback {
    static constexpr bool frame_values = true;
    std::uint64_t reply = 0;
    std::uint8_t back_buffer = 1;
    std::uint64_t id = 0, generation = 0;
    std::uint32_t left = 0, top = 0, right = 0, bottom = 0;  // back buffer box
    std::uint32_t first_byte = 0, bytes = 0;
    void run(Renderer& renderer);  // host (core_host.cpp)
};
template <class A> void fields(A& a, Readback& v) {
    scene_wire::fields_of(a, v.reply, v.back_buffer, v.id, v.generation, v.left, v.top, v.right, v.bottom, v.first_byte,
                          v.bytes);
}

// F11 in core mode (the overlay that handles it is off in the game process):
// the host Renderer's next present takes the existing F11 capture.
struct CaptureRequest {
    static constexpr bool frame_values = false;
    void run(Renderer& renderer);  // host (core_host.cpp)
};
template <class A> void fields(A&, CaptureRequest&) {}

// The progress movie frames a host decode request asked for, decoded by the
// game process with the game's binkw32 (renderer_load_progress.cpp).
struct ProgressFrames {
    static constexpr bool frame_values = false;
    std::uint64_t serial = 0;
    std::uint32_t width = 0, height = 0;
    std::vector<std::vector<std::uint8_t>> frames;
    const char* failure = nullptr;  // static text
    void run(Renderer& renderer);   // host (core_host.cpp)
};
template <class A> void fields(A& a, ProgressFrames& v) {
    scene_wire::fields_of(a, v.serial, v.width, v.height, v.frames);
    scene_wire::static_text(a, v.failure);
}

// The game process's owned interface layout (widescreen.cpp): whether the
// interface is owned (HUD composition, fades and placement apply) and its
// canvas layout. Sent when it changes; the host's widescreen answers and
// widescreen::interface_owned follow it.
struct WidescreenState {
    static constexpr bool frame_values = false;
    bool interface_owned = false;
    widescreen::Layout layout{};
    void run(Renderer& renderer);  // host (core_host.cpp)
};
template <class A> void fields(A& a, WidescreenState& v) { scene_wire::fields_of(a, v.interface_owned, v.layout); }

struct Config {
    static constexpr bool frame_values = false;
    std::vector<ConfigOption> options;
    DebugOverlayMode overlay_mode = DebugOverlayMode::hidden;
    DebugResourceView debug_resource_view = DebugResourceView::disabled;
    std::wstring config_path;  // the game's ini: the host's F10 overlay loads and saves it
    // The game process changed options it saves (HUD layout edit end, a
    // display Apply): the host, the only ini writer, saves them.
    bool save = false;
    void run(Renderer& renderer);  // host (core_host.cpp)
};
template <class A> void fields(A& a, Config& v) { scene_wire::fields_of(a, v.options, v.overlay_mode, v.debug_resource_view, v.config_path, v.save); }

// The game's resource mounts (owned_resource_runtime): the host has no
// registry/configuration of its own, so the passes that open game resources
// themselves (terrain tile families for the cover types and beach sand,
// critters, precipitation, owned text) use the game process's. Sent when
// they change.
struct ResourceMounts {
    static constexpr bool frame_values = false;
    ResourceStoreConfig mounts, text_mounts;
    void run(Renderer& renderer);  // host (core_host.cpp)
};
template <class A> void fields(A& a, ResourceMounts& v) {
    scene_wire::fields_of(a, v.mounts.loose_roots, v.mounts.archives, v.mounts.archive_kinds,
                          v.text_mounts.loose_roots, v.text_mounts.archives, v.text_mounts.archive_kinds);
}

// The combat UI's game-side state (combat_ui_encode_game): units, effects,
// catalog, font, pane effect lists, portraits, hover. Sent ahead of the
// world scene and UI commands that read it, when it changed.
struct CombatState {
    static constexpr bool frame_values = false;
    std::vector<std::uint8_t> bytes;
    void run(Renderer& renderer);  // host (core_host.cpp)
};
template <class A> void fields(A& a, CombatState& v) { scene_wire::field(a, v.bytes); }
// The camp popup's cached composition is stale (Renderer::invalidate_camp_popup).
struct InvalidateCampPopup {
    static constexpr bool frame_values = false;
    std::uint8_t unused = 0;
    void run(Renderer& renderer);  // host (core_host.cpp)
};
template <class A> void fields(A& a, InvalidateCampPopup& v) { scene_wire::field(a, v.unused); }

// ---- core mode: camp icons (claude/s3-store) ----
// The camp item icons are decoded on the host (Host/core_camp.cpp) from the
// archives the game's texture manager lists; the game process keeps only
// each icon's identity and canvas size (no pixels, no GPU texture).
// The game's icon mounts and the high-resolution (addon x2/x4) lookup, sent
// before the first request and whenever they change.
struct CampIconMounts {
    static constexpr bool frame_values = false;
    ResourceStoreConfig mounts;
    bool high_resolution = false;
    void run(Renderer& renderer);  // host (core_camp.cpp)
};
template <class A> void fields(A& a, CampIconMounts& v) {
    scene_wire::fields_of(a, v.mounts.loose_roots, v.mounts.archives, v.mounts.archive_kinds, v.high_resolution);
}
// One icon the game process wants (serial: its loader entry). The host's
// worker decodes it and answers through Control::camp_icon_answers; a
// repeated serial is answered again.
struct CampIconRequest {
    static constexpr bool frame_values = false;
    std::uint64_t serial = 0, world_epoch = 0;
    std::string name;
    void run(Renderer& renderer);  // host (core_camp.cpp)
};
template <class A> void fields(A& a, CampIconRequest& v) { scene_wire::fields_of(a, v.serial, v.world_epoch, v.name); }
// The pending icons the camp shows now: the host's worker decodes these first.
struct CampIconPriority {
    static constexpr bool frame_values = false;
    std::vector<std::uint64_t> serials;
    void run(Renderer& renderer);  // host (core_camp.cpp)
};
template <class A> void fields(A& a, CampIconPriority& v) { scene_wire::field(a, v.serials); }
// A game surface (id, generation) became the native binding of icon serial:
// the icon's host texture is that surface's GPU home.
struct CampIconBind {
    static constexpr bool frame_values = false;
    std::uint64_t serial = 0, id = 0, generation = 0;
    void run(Renderer& renderer);  // host (core_camp.cpp)
};
template <class A> void fields(A& a, CampIconBind& v) { scene_wire::fields_of(a, v.serial, v.id, v.generation); }

// The game process's result of a display Apply the host overlay asked for
// (Control::display_apply_serial): the overlay shows the message.
struct DisplayApplyResult {
    static constexpr bool frame_values = false;
    std::int32_t mode = 0, width = 0, height = 0, result = 0;
    std::string message;
    void run(Renderer& renderer);  // host (core_host.cpp)
};
template <class A> void fields(A& a, DisplayApplyResult& v) { scene_wire::fields_of(a, v.mode, v.width, v.height, v.result, v.message); }

#define SCENE_CORE_WIRE(name, type) \
    template <> struct wire_command<type> { static constexpr Command id = Command::name; };
SCENE_CORE_COMMANDS(SCENE_CORE_WIRE)
#undef SCENE_CORE_WIRE

}  // namespace scene_core
}  // namespace owned

#ifdef SCENE_CORE_TYPES_DEFERRED_POST
#undef SCENE_CORE_TYPES_DEFERRED_POST
#include "scene_core_post.h"
#endif
