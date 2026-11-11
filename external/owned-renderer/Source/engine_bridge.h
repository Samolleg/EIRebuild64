#include "point_shadow_types.h"
#include "world_fog.h"
#include "game_particles.h"
#pragma once
#include "shadow_reuse_diagnostics.h"
#include "screen_rectangle.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ddraw.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "lifecycle_probe.h"
struct IDirectDrawSurface7;
struct ID3D11Texture2D;
namespace owned {
struct HudAtlasMutationWindow;
struct HudPublishedVisual;
class Renderer;
class DebugOverlay;
class RetainedStaticScene;
struct RetainedShadowInputDomain;
struct ResourceIdentity;
struct VisualSkeletonBinding;
struct RetainedVisualFrame;
struct RetainedWorldPointLight;
struct TerrainCpuAsset;
struct FigureCpuAsset;
struct TerrainTexturePage;
struct OwnedTerrainGeometryValues;
namespace ground_cover { struct FootprintSplat; }
// The requested 16-unit visual budget. No persistent unit ownership/history.
struct NativeLiquidRipples {
    std::array<float,4> meta{};
    std::array<std::array<float,4>,16> position_speed{},phase{}; // phase: x phase, y body half extent, zw heading
    std::array<float,4> wave{}; // renderer-owned ripple window: origin xy, 1/extent, valid
    // Per unit: the river at its feet, xy the downhill liquid surface gradient
    // (OwnedLiquidFlow::current); the renderer turns it into the current that
    // carries the wading trail downstream (liquid_waves.h).
    std::array<std::array<float,4>,16> flow{};
    // Renderer-owned sea surf texture (t62, liquid_surf.h): origin xy (world
    // m), 1/extent xy; mode x valid, y water_surf_debug + 4 * quality, zw
    // half a texel. Then the live surf look (F10; liquid_surface.hlsl
    // liquidSurfWaveParams..liquidSurfLookParams, then liquidSurfSwellDir).
    std::array<float,4> surf_window{},surf_mode{};
    std::array<float,4> surf_wave{},surf_shape{},surf_foam{},surf_look{},surf_swell{};
    // Per unit: its identity (lifecycle id and generation), so the renderer
    // can ease each unit's wake in and out (renderer_liquid.cpp). Past the
    // ripple shader's cbuffer, never read by it.
    std::array<std::uint32_t,16> id{};
    // x: the map's largest |wave multiplier| over its objects (the geometry
    // values' maximum_wind_multiplier), for the per-batch unit cull's wind
    // sway margin (Renderer::bind_liquid_ripple_units). CPU only.
    std::array<float,4> reach{};
};
void build_liquid_ripples(const RetainedVisualFrame& frame,const TerrainCpuAsset& terrain,
    const OwnedTerrainGeometryValues& geometry,bool draw_undetected_units,NativeLiquidRipples& output) noexcept;
bool bridge_liquid_ripples(NativeLiquidRipples& output) noexcept;
struct OwnedMapSnapshot;
// Retained map source, completed layer geometry and terrain wind for the renderer-owned ambient
// particles. Hour, cave, region, weather and world time come from the one
// environment reader shared with the fog (WorldFogSnapshot::environment).
struct AmbientWorldSource {
    float wind_direction[2]{};    // terrain wind (the one grass and water use)
    float wind_force_normalized = 0.0f;
    std::shared_ptr<const TerrainCpuAsset> terrain;
    std::shared_ptr<const OwnedMapSnapshot> map;
    std::shared_ptr<const OwnedTerrainGeometryValues> geometry;
};
bool bridge_ambient_world_source(AmbientWorldSource& output) noexcept;
// Ground-contact boxes from current authoritative non-unit visual parts.
// Tree crown parts are excluded; trunk/root boxes retain cover around trees.
bool bridge_ground_cover_objects(float x, float y, float radius,
    std::vector<ground_cover::FootprintSplat>& output) noexcept;
// Drops the contact store with the cover's world (it is refilled on use).
void bridge_release_ground_contacts() noexcept;
// Critters: world envelopes (min xyz, max xyz) of the visible non-unit
// objects (buildings, walls, fences, trees, props) within radius of a point.
// Cached per-object envelopes only; no part or mesh work.
bool bridge_critter_obstacles(float x, float y, float radius,
    std::vector<std::array<float, 6>>& output) noexcept;
// Critters: perches of the visible trees (ambient tree_kind) and Suslanger
// cacti (critters::cactus_model) within radius of a point. Each model's
// perches are chosen once from its own mesh and textures
// (critters::select_perches) and placed per object at its pose and shape.
struct CritterPerchTree {
    float x = 0, y = 0, top = 0;   // object position and its envelope top (world)
    bool cactus = false;
    std::uint8_t count = 0;
    float points[6][3]{};          // world; branch picks first, then edge and fill
};
bool bridge_critter_perches(float x, float y, float radius, std::vector<CritterPerchTree>& output) noexcept;
// Chooses the perches of every tree and cactus model of the current visual
// world that has none yet, within budget_us of this call. True when none is
// left (the load reveal holds until then: one model takes 20-50 ms).
bool bridge_prepare_critter_perch_models(unsigned budget_us) noexcept;
// Ground cover: units standing on the ground within radius of a point, as
// x, y, z (ground contact) and envelope radius. Units the game does not show
// the player (retained_visual_sample_shown: hidden or undetected), invisible,
// dead and airborne units are left out, and without details also levitating
// races (ground_cover::unit_presses_cover). No persistent unit state.
// Critters ask for details too: one row per output row, and corpses (dead
// units still in the scene, not fading out) are then added, flagged dead.
// id hashes the unit's lifecycle identity (never an address).
struct GroundUnitDetail {
    std::uint32_t blood = 0; // units.udb race blood type; 0/unknown cannot attract carrion
    std::uint64_t id = 0;
    std::uint8_t stance = 0;   // critters::Stance of the current clip
    bool dead = false;
    bool undead = false;       // undead model family
    bool crawler = false;      // crawler model; renderer-owned larva habitat
    // Exact local-server percepts, or rounded client values when unavailable, and the
    // replicated pose/moving state that select the game's sense modifiers.
    float visibility = 1;
    float loudness = 1;
    std::uint8_t pose = 2;     // critters::Pose, stand
    bool moving = false;
    float vision = 0;          // party members: the game's sight range (m); critters show only within it
    bool controlled = false;   // controlled by the local human player
};
struct GroundUnitTrail { std::uint64_t id = 0; float half_extent = 0; };
void build_ground_cover_units(const RetainedVisualFrame& frame,const TerrainCpuAsset& terrain,
    float center_x,float center_y,float radius,bool draw_undetected_units,std::vector<std::array<float,4>>& output,
    std::vector<GroundUnitDetail>* details = nullptr,std::vector<GroundUnitTrail>* trails = nullptr) noexcept;
// trails: one row per output row: the unit's visual incarnation key (snow
// trails join its successive positions by it; never an address) and the
// larger half extent of its native local bounding box.
bool bridge_ground_cover_units(float center_x,float center_y,float radius,
    std::vector<std::array<float,4>>& output,std::vector<GroundUnitDetail>* details = nullptr,
    std::vector<GroundUnitTrail>* trails = nullptr) noexcept;
// Read-only E-153 snapshot. Reuses caller storage, growing only to list demand.
// False rejects the entire snapshot; the caller must omit affected cover.
// trail_points (optional): every visible print of a walking unit, whether or
// not the game currently shows its maker (the game shows its prints): the
// maker's incarnation key, creation tick, body centre at creation and box
// half extent. Snow trails join a maker's prints in tick order.
struct FootprintTrailPoint { std::uint64_t id = 0; std::int32_t tick = 0; float x = 0, y = 0, half_extent = 0; };
bool bridge_ground_cover_footprints(std::vector<ground_cover::FootprintSplat>& output,
    std::vector<FootprintTrailPoint>* trail_points = nullptr) noexcept;
// Appends one lying-body footprint per shown corpse (dead, fade alpha > 0.5,
// shown to the player, not invisible/airborne) within radius, from the retained unit values.
void build_ground_cover_corpses(const RetainedVisualFrame& frame,const TerrainCpuAsset& terrain,
    float center_x,float center_y,float radius,bool draw_undetected_units,
    std::vector<ground_cover::FootprintSplat>& output) noexcept;
void bridge_ground_cover_corpses(float center_x,float center_y,float radius,
    std::vector<ground_cover::FootprintSplat>& output) noexcept;
struct OwnedVisualSurface;
// The game's terrain atlas of one material (base textures, then long tiles),
// read from its completed proxy surface; null while not yet loaded.
// failure (optional) names the first missing step when no atlas is returned.
bool bridge_terrain_atlas(std::uint32_t material,std::shared_ptr<const OwnedVisualSurface>& output,
    const char** failure=nullptr) noexcept;
// Reads the same game environment as the fog seam, now (no frame-serial
// window): for passes that run after the bridge frame advanced.
struct WorldEnvironment;
bool bridge_read_world_environment(WorldEnvironment& output) noexcept;
void bridge_append_probe_event(const char* event,const char* detail) noexcept;
// Temporary, opt-in normal F11 request; observed before the next world releases its frame.
void bridge_request_hovered_shadow_capture() noexcept; // temporary probes
struct RetainedShadowCasterValue;

// A completed proxy source has its own identity domain. It is never an
// archive ResourceIdentity and carries no borrowed surface or engine pointer.
struct OwnedVisualSurface {
    std::uint64_t id = 0;
    std::uint64_t generation = 0;
    std::uint32_t width = 0, height = 0;
    std::int32_t pitch = 0;
    DDPIXELFORMAT format{};
    bool has_color_key = false;
    DDCOLORKEY color_key{};
    // No pixels: the proxy surface's GPU texture is the only pixel home and
    // the renderer resolves it by id/generation. Alpha facts are established
    // once from this completed generation's bytes at publication: a 1-bit
    // cut mask (row-major, LSB first) exists only when some texel is cut.
    std::shared_ptr<const std::vector<std::uint8_t>> alpha_cut_mask;
    bool alpha_fully_opaque = false;
    bool static_mip_hint = false;
};
using CompletedVisualSurfaceReader = bool (*)(IDirectDrawSurface7*,
    std::shared_ptr<const OwnedVisualSurface>&) noexcept;
void bridge_set_completed_visual_surface_reader(CompletedVisualSurfaceReader reader) noexcept;
bool proxy_completed_visual_surface(IDirectDrawSurface7* surface,
    std::shared_ptr<const OwnedVisualSurface>& value) noexcept;
void proxy_retire_camp_texture(std::uint64_t id, std::uint64_t generation) noexcept;
// The actual initialized overlay/window owner registers after subclass success
// and clears this before shutdown. No renderer scene lifetime is used for input.
void bridge_set_camp_input_overlay(DebugOverlay* overlay) noexcept;
// Accept stock texture-manager creation or the exact bridge-owned HUD hook.
bool bridge_camp_texture_create_matches() noexcept;
// Accept stock pane text or the exact installed bridge-owned text hook.
bool bridge_camp_draw_text_matches() noexcept;
// Accept stock Clear or the exact installed item-card text pass-through.
bool bridge_camp_surface_clear_matches() noexcept;
// Native editor admission; reset is interface-thread-only and requires the
// completed owned HUD to be the native top screen with its own embedded input.
bool bridge_lia_hud_editor_contract_matches() noexcept;
bool bridge_reset_lia_hud_editor_input(void* interface_object) noexcept;
// Fingerprinted native camera/projection contract; unrelated harness sites stay off.
bool bridge_lia_back_clip_contract_matches() noexcept;
std::uint64_t bridge_camp_texture_generation() noexcept;
bool bridge_camp_texture_manager(void* manager) noexcept;
void* bridge_camp_texture_owner() noexcept;
struct ResourceStoreConfig;
// Main-thread immutable snapshot of the texture manager's actual ordered RES list.
bool bridge_camp_texture_mounts(ResourceStoreConfig& mounts) noexcept;
struct PreparedCampIcon {
    OwnedVisualSurface description;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    // Core mode (claude/s3-store): no texture here; the host decoded this
    // icon (CampIconRequest serial) and binds it to the native surface.
    std::uint64_t core_serial = 0;
};
bool proxy_publish_camp_texture(IDirectDrawSurface7* surface,
    const PreparedCampIcon& icon,
    std::shared_ptr<const OwnedVisualSurface>& value) noexcept;

enum class SemanticKind : std::uint8_t {
    unknown,
    frame,
    simple_geometry,
    terrain,
    figure,
    particles,
    ui,
};

struct SemanticContext {
    SemanticKind kind = SemanticKind::unknown;
    std::uint32_t caller_rva = 0;
};

enum class FigureFallbackReason : std::uint8_t {
    none,
    unavailable,
    hidden,
    renderer_state,
    transform,
    material,
    light_array,
    morph_data,
    geometry,
    metadata,
    state_write,
    direct_submit,
    exception,
    count,
};

constexpr std::size_t kFigureFallbackReasonCount =
    static_cast<std::size_t>(FigureFallbackReason::count);

const char* figure_fallback_reason_name(FigureFallbackReason reason) noexcept;

// Temporary shared-scene diagnosis. Strings are static program literals only;
// identity is copied value identity, never a borrowed engine/resource pointer.
struct OwnedVisualRejection {
    const char* stage = nullptr;
    const char* reason = nullptr;
    const char* reason_scope = nullptr; // Literal origin; serialized inside the existing reason.
    std::int32_t signed_id = 0;
    std::uint64_t resource_serial = 0, membership_generation = 0;
    std::uint32_t status = UINT32_MAX, object_flags = 0, class_flags = 0;
    bool unit = false, hidden = false, draw_undetected = false;
};

void format_owned_visual_failure_detail(const OwnedVisualRejection& rejection,
    char* detail, std::size_t size) noexcept;

struct OwnedVisualDiagnostic {
    bool sampled = false, completed = false;
    bool active = false, shadow_ready = false, color_submitted = false;
    std::uint64_t profile_frame = 0, world_serial = 0;
    std::uint64_t live_members = 0, missing_resource = 0, unfinished_resource = 0;
    // Window-exited statics drawn from their retained copy (visual_detach).
    std::uint64_t detached_members = 0;
    std::uint64_t excluded_generation = 0, samples = 0;
    std::uint64_t considered = 0, unavailable_values = 0, class_rejected = 0;
    std::uint64_t no_visible_part = 0, invalid_envelope = 0, bounds_ready = 0;
    std::uint64_t contribution_rejected = 0, eligible = 0, submitted_parts = 0;
    // Existing shadow counters include terrain work, not just entities.
    std::uint64_t shadow_considered = 0, shadow_eligible = 0, shadow_submitted = 0;
    OwnedVisualRejection first_rejection{};
};

struct BridgeProfileSnapshot {
    OwnedVisualDiagnostic owned_visual{};
    LifecycleProbeSnapshot lifecycle{};
    std::uint64_t frame = 0;
    double begin_interval_ms = 0.0;
    double frame_cpu_ms = 0.0;
    double outside_scene_interval_ms = 0.0;
    double outside_entry_cpu_ms = 0.0;
    double world_draw_cpu_ms = 0.0;
    double world_unattributed_cpu_ms = 0.0;
    double scene_3d_cpu_ms = 0.0;
    double scene_3d_unattributed_cpu_ms = 0.0;
    double scene_partition_retained_nonqueue_cpu_ms = 0.0;
    // Receiver fitting, caster values, depth registration, nonunit pose,
    // visual frame values, shadow decision, domain snapshot, color, remainder.
    std::array<double,12> city_retained_owner_cpu_ms{};
    double scene_partition_object_nonqueue_cpu_ms = 0.0;
    double scene_partition_queue_native_flush_cpu_ms = 0.0;
    double scene_partition_queue_ui_flush_cpu_ms = 0.0;
    double scene_partition_queue_packet_or_control_cpu_ms = 0.0;
    double scene_partition_remaining_game_cpu_ms = 0.0;
    double outside_scene_3d_cpu_ms = 0.0;
    double object_draw_cpu_ms = 0.0;
    double object_draw_unattributed_cpu_ms = 0.0;
    double simple_cpu_ms = 0.0;
    double figure_placed_cpu_ms = 0.0;
    double figure_lit_cpu_ms = 0.0;
    double figure_lit_morphed_cpu_ms = 0.0;
    double figure_lit_rigid_cpu_ms = 0.0;
    std::uint64_t world_draw_calls = 0;
    std::uint64_t scene_3d_calls = 0;
    std::uint64_t scene_partition_calls = 0;
    std::uint64_t scene_partition_retained_calls = 0;
    std::uint64_t scene_partition_object_outer_calls = 0;
    std::uint64_t scene_partition_queue_calls = 0;
    std::uint64_t object_draw_calls = 0;
    std::uint64_t object_plan_original_calls = 0;
    std::uint64_t object_plan_owned_calls = 0;
    std::uint64_t object_plan_hidden_calls = 0;
    std::uint64_t object_plan_diagnostic_calls = 0;
    std::uint64_t object_plan_bootstrap_calls = 0;
    std::uint64_t object_plan_submitted_figures = 0;
    std::uint64_t object_plan_early_suffix_removed_calls = 0;
    std::uint64_t object_plan_early_owned_calls = 0;
    std::uint64_t object_plan_early_omitted_calls = 0;
    std::uint64_t retained_static_claimed_instances = 0;
    std::uint64_t retained_static_visible_instances = 0;
    std::uint64_t retained_static_submitted_instances = 0;
    std::uint64_t retained_static_submitted_nodes = 0;
    std::uint64_t retained_static_post_cull_nodes = 0;
    std::uint64_t retained_static_local_failures = 0;
    std::uint64_t retained_static_unique_geometry_variants = 0;
    std::uint64_t retained_static_suppressed_draws = 0;
    std::uint64_t retained_static_legacy_prepare_removed = 0;
    std::uint64_t retained_static_legacy_prepare_remaining = 0;
    std::uint64_t retained_static_owned_visibility_candidates = 0;
    std::uint64_t retained_static_owned_visibility_culled = 0;
    std::uint64_t retained_static_hidden_instances = 0;
    std::uint64_t retained_static_shadow_publications = 0;
    std::uint64_t shadow_active_visuals = 0, shadow_active_terrain = 0;
    std::uint64_t shadow_changed_visuals = 0, shadow_changed_terrain = 0;
    std::uint64_t shadow_static_casters_reused = 0, shadow_bounds_rebuilt = 0;
    std::uint64_t shadow_receiver_refits = 0, shadow_caster_refits = 0;
    std::uint64_t shadow_map_reuses = 0;
    std::uint8_t shadow_dirty_mask = 0;
    std::uint32_t retained_static_prepare_result = 0;
    std::uint64_t retained_static_prepare_binding_revision = 0;
    std::uint64_t retained_static_prepare_source_bindings = 0;
    std::uint64_t retained_static_prepare_binding_index = UINT64_MAX;
    std::uint64_t retained_static_prepare_node_index = UINT64_MAX;
    std::int32_t retained_static_prepare_signed_id = 0;
    double object_plan_bootstrap_cpu_ms = 0.0;
    double object_plan_submit_cpu_ms = 0.0;
    std::uint64_t simple_calls = 0;
    std::uint64_t figure_placed_calls = 0;
    std::uint64_t figure_lit_calls = 0;
    std::uint64_t figure_lit_morphed_calls = 0;
    std::uint64_t figure_lit_rigid_calls = 0;
    std::uint64_t figure_gpu_owned_calls = 0;
    std::uint64_t figure_cpu_fallback_calls = 0;
    std::uint64_t figure_gpu_submit_failures = 0;
    std::uint64_t figure_gpu_culled_calls = 0;
    double terrain_cpu_ms = 0.0;
    double terrain_outer_cpu_ms = 0.0;
    double terrain_capture_cpu_ms = 0.0;
    double terrain_submit_cpu_ms = 0.0;
    std::uint64_t terrain_calls = 0;
    std::uint64_t terrain_outer_calls = 0;
    std::uint64_t terrain_gpu_owned_calls = 0;
    std::uint64_t terrain_cpu_fallback_calls = 0;
    std::uint64_t terrain_gpu_submit_failures = 0;
    std::uint64_t terrain_candidate_sectors = 0;
    std::uint64_t terrain_visible_sectors = 0;
    std::uint64_t terrain_frustum_rejected_sectors = 0;
    std::uint64_t terrain_animation_owned_updates = 0;
    std::uint64_t terrain_animation_fallback_updates = 0;
    std::uint64_t terrain_wave_sectors = 0;
    std::uint64_t terrain_animated_tile_updates = 0;
    std::uint64_t terrain_owned_frame_preparations = 0;
    std::uint64_t terrain_owned_transparent_reuses = 0;
    std::uint64_t terrain_spatial_bounds_prepared = 0;
    std::uint64_t terrain_spatial_bounds_reused = 0;
    std::uint64_t terrain_game_publication_sectors = 0;
    std::uint64_t terrain_dynamic_color_vertices_copied = 0;
    std::uint64_t terrain_structural_vertices_copied = 0;
    std::uint64_t terrain_source_topology_decodes = 0;
    std::uint64_t terrain_source_topology_vertices = 0;
    std::uint64_t terrain_source_color_decodes = 0;
    std::uint64_t terrain_source_color_vertices = 0;
    std::uint64_t terrain_original_render_front_end_calls = 0;
    std::uint64_t terrain_owned_omitted_ranges = 0;
    std::array<std::uint64_t, kFigureFallbackReasonCount> figure_fallback_reasons{};
    std::uint64_t simple_vertices = 0;
    std::uint64_t simple_indices = 0;
    bool scene_partition_valid = false;
};

struct SceneWorldToClipSnapshot {
    std::uint64_t scene_frame_serial = 0;
    std::array<float, 16> world_to_clip{};
    bool valid = false;
};

// Independently verified, packed object-space geometry view used by the
// supported x86 executable before its legacy CPU transform/clip expansion.
// The view is borrowed for the duration of the callback only; the receiver
// must copy or upload anything it needs before returning.
struct NativePackedGroup {
    float x[4];
    float y[4];
    float z[4];
    float w[4];
};

struct NativeTextureCoordinate {
    float u;
    float v;
};

struct NativeMorphOffset {
    float x;
    float y;
    float z;
};

// Immutable per-vertex inputs for terrain animation. Wave deformation and
// animated-tile UV selection consume these in the native shader instead of a
// CPU-mutated/reuploaded vertex stream.
struct NativeTerrainVertex {
    float noise = 0.0f;
    float wind_multiplier = 0.0f;
    float object_color[3]{};
    float depth = 0.0f;
    std::uint32_t flags = 0;
    std::uint32_t animation_index = ~std::uint32_t{0};
    std::uint32_t animation_base_tile = 0;
    std::uint32_t grid_index = 0; // layer vertices: grid index; base vertices: bits 16-31 terrain_facet_flatness()
    float opacity = 1.0f;
    float emissive[3]{};
    float liquid_depth = 0.0f; // Base colour only; never a deformation offset.
    float liquid_multiplier = 0.0f; // Tile material flow; keep shared-vertex deformation separate.
    float liquid_uv_bounds[4]{}; // Inset min/max for this flipbook frame, before phase shift.
    float liquid_bend = 0.0f;    // OwnedLiquidFlow: rate of change of the surface gradient
    float liquid_cascade = 0.0f; // OwnedLiquidFlow: cascade foam source 0..1
    float liquid_current[2]{};   // OwnedLiquidFlow: smoothed downhill surface gradient
    float liquid_self_illumination = 0.0f; // Authored scalar; emission RGB cannot classify red lava.
    float liquid_bed_emission = 1.0f; // Dry vertices suppress wetness when interpolated with emissive beds.
    std::uint32_t slope_normal = 0; // terrain_slope_normal(): projection weights only, never lighting.
    // Terrain objects whose live layer shift the shader applies (0xffff none):
    // low 16 bits lift this layer vertex (its z and depth decode at rest),
    // high 16 bits are the liquid over this base vertex (liquid_depth is the
    // signed rest depth; the shader adds the shift and clamps at zero).
    std::uint32_t shift_objects = 0xffffffffu;
    float liquid_drop = 0.0f; // OwnedLiquidFlow: distance below the fall lip (world units); dry material base vertices: owned_terrain_foot_rise
    float liquid_fill = 0.0f; // Liquid vertices: OwnedLiquidFlow pit depth (the shader lifts the surface by it); dry base vertices: owned_terrain_pit_fill (shadow casters only)
};

enum NativeTerrainVertexFlag : std::uint32_t {
    native_terrain_wave_vertex = 1u << 0,
    native_terrain_grass_vertex = 1u << 1,
    native_terrain_animated_uv = 1u << 2,
    native_terrain_water_vertex = 1u << 3,
    // Opaque, unanimated base tile in a square atlas cell: the owned terrain
    // material may re-read its tile art (terrain_material.h).
    native_terrain_material_vertex = 1u << 4,
    // Opaque, unanimated base tile drawn from a long texture (a 1 x 8 strip
    // per tile, the authored cliff art): no blend, material or cliff stage;
    // only the far de-tiling (terrainLongFar) and the material cache.
    native_terrain_long_tile = 1u << 12,
    // Liquid tiles also carry their 3x3 liquid-neighbour mask (bits 16-24) and
    // the vertex's place in its tile in half-tile steps (x bits 25-26, y 27-28).
    native_terrain_liquid_edge = 1u << 15,
    // Liquid vertices: bits 29-31 hold the smoothed swamp share of the nearby liquid
    // tiles (owned_terrain_liquid_materials) in sevenths. The shader's liquid
    // class reads this, never the object colour.
    native_terrain_swamp_shift = 29u,
    // Open-water liquid tiles and swamp transition tiles (frames with open
    // water painted in, swamp_frame_cover): their liquid_uv_bounds are a
    // nearby pure swamp tile's frame (the algae art), drawn without the
    // tile's own flipbook shift; or no such tile of the same atlas page is
    // near, and the shader reads the map swamp frame's own texture instead
    // (TerrainCpuAsset::swamp_frame_texels; no swamp coverage without it).
    native_terrain_swamp_art_borrowed = 1u << 5,
    native_terrain_swamp_art_map = 1u << 13,
    // Procedural falls (procedural_falls.h): an authored liquid vertex on a
    // fall edge, and a vertex of a renderer-generated fall sheet.
    native_terrain_fall_vertex = 1u << 6,
    native_terrain_generated_fall = 1u << 7,
};

struct NativeTerrainState {
    std::uint32_t waves_enabled = 0;
    std::uint32_t animated_tiles_enabled = 0;
    std::uint32_t tiles_per_texture_side = 0;
    std::uint32_t animation_phase_count = 0;
    float wind_direction[2]{};
    float wind_force = 0.0f;
    float wind_force_normalized = 0.0f;
    float wind_phase = 0.0f;
    float precise_time = 0.0f;
    float maximum_wind_multiplier = 0.0f;
    float liquid_detail_time = 0.0f; // Fractional colour clock; large waves keep precise_time.
    float fog_min[2]{};
    float fog_max[2]{};
    float fog_distance = 0.0f;
    std::uint32_t fog_enabled = 0;
};

// Ordering is a high-level scene contract, not a heuristic inferred from a
// possibly stale D3D7 state slot.  Exact owners can declare that equal-state
// records were concatenated by the engine's batcher and are safe to reorder.
enum class NativeOrdering : std::uint8_t {
    derive_from_state = 0,
    reorderable = 1,
    preserve = 2,
};

inline constexpr NativeOrdering kNativePlacedOrdering = NativeOrdering::preserve;

// Completed environment values shared by the modern world invocation. Material
// response and object transforms remain per instance; legacy object light arrays
// are not inputs. The GPU constant layout remains exactly 48 bytes.
struct NativeWorldLighting {
    std::array<float, 4> ambient{0.2f, 0.2f, 0.2f, 1.0f};
    std::array<float, 4> sunlight_direction{0.0f, 0.0f, -1.0f, 0.0f};
    std::array<float, 4> sunlight_diffuse{0.6f, 0.6f, 0.6f, 1.0f};
};

struct NativeLighting {
    std::uint32_t enabled = 0;
    float material_alpha = 1.0f;
    std::array<float, 3> material_diffuse{1.0f, 1.0f, 1.0f};
    float padding = 0.0f;
    std::array<float, 4> emissive{};
    std::array<float, 4> specular{};
    std::array<float, 16> object_to_world{};
};

enum class NativeOwnedTextureFormat : std::uint32_t {
    dxt1,
    dxt3,
    rgb565,
    argb1555,
    argb4444,
    argb8888,
    dxt5,
    bc5, bc7, bc7_srgb,
};

struct NativeOwnedTextureMip {
    const std::uint8_t* data = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t bytes = 0;
};

struct NativeOwnedTextureView {
    std::uint64_t identity = 0;
    std::uint64_t generation = 0;
    NativeOwnedTextureFormat format = NativeOwnedTextureFormat::dxt1;
    const NativeOwnedTextureMip* mips = nullptr;
    std::uint32_t mip_count = 0;
    // Full immutable archive identity, generated-value serial, and completed
    // proxy surface IDs occupy separate namespaces in the existing GPU pool.
    const ResourceIdentity* resource = nullptr;
    const OwnedVisualSurface* surface = nullptr;
    std::uint32_t value_source = 0; // 0 legacy, 1 archive, 2 memory MMP, 3 disk MMP
    std::uint32_t opaque_mips = 0;
    // Only a camp-owned completed source may share its current GPU home.
    // Other surface consumers keep their detached snapshot semantics.
    bool camp_surface_alias = false;
    // The game texture name of a retained figure part (VisualPartAsset::
    // material_name): the key of its material composition. Null: none.
    const std::string* material_name = nullptr;
};

struct NativeSknMaterial {
    std::uint64_t identity = 0;
    std::array<NativeOwnedTextureView,3> maps{}; // normal, ORM, emission
    float factors[4]{1,0,1,1}; // roughness, metallic, AO, mode (0 none, 1 SKN, 2 two-sided)
    float emission[4]{};
    float base[4]{1,1,1,1};
};

struct NativeOwnedFixedState {
    bool enabled = false;
    std::uint32_t cull_mode = 0;
    std::uint32_t fill_mode = 0;
    std::uint32_t z_enable = 0;
    std::uint32_t z_write_enable = 0;
    std::uint32_t z_func = 0;
    std::uint32_t alpha_blend_enable = 0;
    std::uint32_t source_blend = 0;
    std::uint32_t destination_blend = 0;
    std::uint32_t alpha_test_enable = 0;
    std::uint32_t alpha_func = 0;
    std::uint32_t alpha_ref = 0;
    std::uint32_t texture_factor = 0;
    std::uint32_t specular_enable = 0;
    std::uint32_t color_op = 0;
    std::uint32_t alpha_op = 0;
    std::uint32_t color_arg1 = 0;
    std::uint32_t color_arg2 = 0;
    std::uint32_t alpha_arg1 = 0;
    std::uint32_t alpha_arg2 = 0;
    std::uint32_t address_u = 0;
    std::uint32_t address_v = 0;
    std::uint32_t min_filter = 0;
    std::uint32_t mag_filter = 0;
    // Existing figure contracts inherit their mip filter. A completed terrain
    // material supplies it explicitly for both color coverage and depth.
    std::uint32_t mip_filter = UINT32_MAX;
    // Completed proxy frame values. Only deferred terrain opts into replay;
    // ordinary owned submissions continue inheriting the current frame state.
    bool replay_frame_state = false;
    std::array<std::uint32_t,4> captured_viewport{};
    std::array<float,2> captured_depth_range{0,1};
    std::uint32_t captured_fog_enable = 0;
    std::uint32_t captured_fog_color = 0;
};

// The owned opaque base terrain pass establishes its own blending. The game
// applies its default state block right before CTerrain::DrawBase, but the
// owned pass replaces that draw and was otherwise inheriting whatever device
// state was current (on the no-addon path SRCALPHA/INVSRCALPHA blending, per
// the terrain cull dump, which made the terrain invisible). Depth, alpha test, cull
// and texture stages stay as the frame captured them.
inline NativeOwnedFixedState owned_terrain_base_material(NativeOwnedFixedState captured) noexcept {
    captured.alpha_blend_enable = 0;
    captured.source_blend = 2;       // D3DBLEND_ONE
    captured.destination_blend = 1;  // D3DBLEND_ZERO
    return captured;
}

struct NativeFrameState {
    std::array<std::uint32_t,4> viewport{};
    std::array<float, 16> view{};
    std::array<float, 16> projection{};
    NativeOwnedFixedState material;
    // Latched by the owned renderer at BeginScene. Every consumer in one world
    // invocation therefore makes the same shadow admission decision.
    bool shadows_enabled = true;
};

#pragma pack(push, 1)
struct NativeVertexComponent {
    std::uint16_t position;
    std::uint16_t normal;
    std::uint16_t texture;
};

struct NativeLightComponent {
    std::uint16_t position;
    std::uint16_t normal;
};
#pragma pack(pop)

// Renderer-owned prepared-resource handles.  The bridge retains only these
// opaque generations inside a lifecycle-keyed object plan; engine pointers
// remain bounded inputs to the synchronous preparation call.
struct NativePreparedMeshHandle {
    std::uint32_t slot = UINT_MAX;
    std::uint32_t generation = 0;
};

struct NativePreparedMaterialHandle {
    std::uint32_t slot = UINT_MAX;
    std::uint32_t generation = 0;
};

struct NativePreparedIdentity {
    std::int32_t signed_id = 0;
    std::uint64_t lifecycle_generation = 0;
    std::uint64_t world_epoch = 0;
    std::uint64_t plan_generation = 0;
    std::uint32_t part_index = UINT_MAX;
    std::uint32_t set_index = UINT_MAX;
    std::uint64_t presentation_serial = 0;
    bool valid = false;
    // Renderer resource namespace; terrain coordinates are not game object IDs.
    bool terrain_source = false;
};

struct NativeWorldBounds {
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
    bool valid = false;
};

struct NativeContribution {
    bool known = false;
    bool color = true;
    std::uint8_t light_mask = 0;
};

struct NativeShadowCasterRevision {
    std::int32_t signed_id = 0;
    std::uint32_t part = 0;
    std::uint64_t membership = 0, epoch = 0, resource = 0, revision = 0;
    bool terrain = false;
    bool operator==(const NativeShadowCasterRevision& other) const noexcept {
        return signed_id==other.signed_id && part==other.part && membership==other.membership &&
            epoch==other.epoch && resource==other.resource && revision==other.revision && terrain==other.terrain;
    }
    bool operator!=(const NativeShadowCasterRevision& other) const noexcept { return !(*this==other); }
};

// The existing slice owner retains compact source revisions. Entry, removal
// and revision changes invalidate both the previous and current affected slices.
struct NativeShadowSlice {
    std::array<float,16> world_to_light{};
    std::array<float,16> camera_slice{};
    std::array<float,4> bias{}; // texel world size, inverse depth span, reserved
    std::array<float,3> receiver_minimum{},receiver_maximum{};
    std::vector<NativeShadowCasterRevision> validity;
    // Static layer (persistent static set + terrain): cached in its own depth
    // map and redrawn only when this list or the projection changes; the
    // per-frame casters in `validity` are drawn over a copy of it.
    std::vector<NativeShadowCasterRevision> static_validity;
    bool enabled = false;
};
struct NativeShadowFrame {
    std::array<NativeShadowSlice,5> slices{};
    // Authoritative lifecycle epoch; far-map cadence must not reuse a prior scene.
    std::uint64_t world_epoch = 0;
    // Unique only for a fully rebuilt immutable value frame. Reusing this
    // value may skip repeated validity-vector scans after resource validation.
    std::uint64_t publication_serial = 0;
};

struct NativeWorldGeometry {
    bool enabled = false;
    std::uint64_t record_revision = 0;
    const std::weak_ptr<const void>* owner = nullptr;
    const ResourceIdentity* identity = nullptr;
    // Immutable subdivision within that exact resource, never a raster batch key.
    std::uint32_t part = 0;
    // A rigid source drawn in object space: the vertex stage evaluates the
    // shared mesh with this record's transform, and the record owns no world
    // vertices (static_lod.h native_world_object_space).
    bool object_space = false;
    // A non-animated retained figure may use reduced indices while keeping
    // its existing prepared world vertices, pose and shadow-caster identity.
    bool static_lod = false;
    // Immutable vertex opacity in the packed normal's otherwise unused W.
    // This source carries no animated terrain metadata or plant sway.
    bool vertex_opacity = false;
    // Object space: the plant's root point in world space, the sway anchor a
    // posed record reads from its root pose node (instance flags 0x200 and
    // the trunk shape carry the rest). Unused without sway flags.
    std::array<float, 3> sway_root{};
    // Its crown impostor's atlas slice + 1 (0: none): a cut-out object-space
    // record draws the impostor far away (static_lod.h native_world_impostor).
    std::uint32_t impostor = 0;
    std::uint32_t shape_count = 1;
    std::array<float, 3> shape{};
    std::array<float, 16> object_to_world{};
    std::array<float, 16> world_to_clip{};
    // A current owned pose is evaluated once on the GPU, independently of the
    // camera and pass. The binding owns immutable assets, never engine objects.
    struct Pose {
        std::shared_ptr<const VisualSkeletonBinding> binding;
        std::weak_ptr<const void> owner;
        std::array<float,16> root_to_world{};
        std::array<float,3> shape{}, center_shift{};
        std::uint32_t frame = 0;
        float fraction = 0, height_scale = 1;
        float contact_world_height = 0;
        std::uint32_t slot = UINT_MAX, generation = 0;
    };
    Pose* pose = nullptr;
    std::uint32_t pose_node = UINT_MAX;
    float geometry_radius = 0;
};

// The completed sector phase owner supplies a revision only. The renderer
// finds its GPU slot by the view's prepared identity: this value is copied
// into every scene-thread command, so a handle written back into it would be
// lost with the copy. Values are borrowed synchronously; GPU lifetime uses
// the map source owner.
struct NativeTerrainPhaseValues {
    std::uint64_t revision = 0;
};

// Persistent static draw set: a static object's color records stay registered
// in the renderer and are drawn (GPU-culled) every frame without re-queueing.
struct NativeStaticSubmission {
    // shadow_static/shadow_dynamic select the layer the next shadow draws go to
    // (generation returns the static-layer dirty mask).
    // terrain_frame publishes the frame's terrain globals (terrain_state,
    // full_sun). terrain=true targets the base-terrain set: set takes a
    // terrain view (bound texture/state as for a per-frame terrain draw).
    enum class Operation : std::uint8_t { set, remove, flush, generation, shadow_static, shadow_dynamic, terrain_frame };
    Operation operation = Operation::set;
    std::uint64_t key = 0;
    bool terrain = false;
    const struct NativePreparedSubmission* records = nullptr;   // set only
    const struct NativeGeometryView* terrain_view = nullptr;     // terrain set only
    const NativeTerrainState* terrain_state = nullptr;           // terrain_frame only
    const std::vector<float>* layer_shifts = nullptr;            // terrain_frame: live per-object shifts
    bool full_sun = false;                                      // terrain_frame only
    const std::array<float,16>* world_to_clip = nullptr;         // flush only
    mutable bool registered = false;                            // set: false keeps the object per-frame
    mutable std::uint64_t generation = 0;                       // renderer static-set generation
};

// A mesh decoded off the render thread into a dedicated geometry page (the
// neighbour stream). Offsets are bytes from the start of that page; `page` is
// set when the render thread adopts the page. On a mesh-cache miss the
// renderer registers it instead of decoding and uploading on the render thread.
struct NativePrebuiltMesh {
    std::uint32_t page_slot = 0;
    mutable std::uint32_t page = UINT32_MAX;
    mutable bool used = false;
    std::uint32_t offset = 0, bytes = 0;
    std::uint32_t position_offset = 0, normal_offset = 0, component_offset = 0,
        texture_coordinate_offset = 0, diffuse_storage_offset = 0, specular_storage_offset = 0,
        terrain_vertex_offset = UINT32_MAX, index_offset = 0;
    std::uint32_t position_groups = 0, normal_groups = 0, component_count = 0,
        texture_coordinate_count = 0, index_count = 0, decoded_channels = 0;
    std::uint32_t shape_count = 1; // shapes its positions hold (1 or 8)
    // Reduced levels after the full index list (NativeMesh::lod_levels).
    std::uint32_t lod_levels = 0, lod_extra_triangles = 0;
    bool sphere_block = false; // NativeMesh::sphere_block
    float bounds_min[3]{}, bounds_max[3]{};
    bool has_liquid = false, has_ground = true;
    // Liquid vertices by type (NativeMesh::liquid_types): 1 water, 2 lava (self-illuminated), 4 swamp share.
    std::uint8_t liquid_types = 0;
    std::uint32_t steep_first_triangle = UINT32_MAX; // NativeMesh::steep_first_triangle
};

struct FurProfile;
struct NativeLodIdentity;
struct NativeGeometryView {
    // Borrowed at synchronous registration only; copied into the renderer record.
    const NativeLodIdentity* lod_observer = nullptr;
    float contact_world_height = 0;
    // Fur prototype: an owned preview's part record carries its fur profile
    // here (the submission's own shared pointer, valid for the submit call;
    // the renderer keeps a copy in the record) and the part's node.
    const std::shared_ptr<const FurProfile>* fur = nullptr;
    std::uint32_t fur_node_base = 0;
    const NativePrebuiltMesh* prebuilt_mesh = nullptr;
    // Reduced index levels (static_lod.h) stored after the full index list.
    const struct NativeMeshLods* lods = nullptr;
    // Borrowed immutable source for a cold ordinary mesh-cache build only.
    // The cache owns the uploaded LOD table; warm records never simplify.
    const FigureCpuAsset* lod_figure = nullptr;
    std::uint32_t contact_map = 0; // active map 0; neighbour upload identity otherwise
    // A compact owned-scene invocation uses the existing synchronous sink.
    // Its immutable source descriptions are read only on initial registration
    // or source replacement, never retained by pointer in the renderer.
    const struct NativePreparedSubmission* prepared_submission = nullptr;
    const struct NativeDepthSubmission* depth_submission = nullptr;
    const struct NativeStaticSubmission* static_submission = nullptr;
    // Merged character meshes (#65): per component, the part's skeleton node
    // (low 16 bits) and its position base (high 16 bits); null elsewhere.
    const std::uint32_t* component_parts = nullptr;
    // Immutable SKN upload records, 240 bytes/component: u32 node[4], f32
    // weight[4], column-major inverse bind's first 3 rows as 4xfloat3[4].
    const std::uint8_t* skin_vertices = nullptr;
    NativeSknMaterial skn_material{};
    // Established from conservative source/pose envelopes before resolving any
    // mesh, texture or deformation. A light-only record never becomes a color
    // draw merely because its source was admitted for shadow preparation.
    NativeContribution contribution{};
    NativeWorldGeometry world{};
    // Stable engine object identities are opaque renderer keys only. They let
    // the owned backend retain scene records without making pointer layout a
    // public API or re-running geometry discovery every frame.
    std::uintptr_t object_token = 0;
    std::uintptr_t figure_token = 0;
    std::uintptr_t geometry_token = 0;
    // Borrowed only for the synchronous native submission. A non-zero value
    // is the renderer-owned IDirectDrawSurface7 backing the live wound layer.
    std::uintptr_t wound_surface_interface = 0;
    bool wound_only = false;
    // An owned preview's part (panel doll, outfit, portrait): interface, never
    // drawn in a world flush (Renderer::flush_native).
    bool interface_preview = false;
    // Create (or find) the world mesh only: nothing is queued or drawn
    // (terrain far-shadow mesh prewarm, engine_bridge.cpp).
    bool prepare_only = false;
    // A non-zero cache identity names decoded geometry independently of any
    // temporary CPU packet.  It lets the renderer retain the GPU copy and ask
    // the bridge to decode object-space source data only on a cache miss or an
    // explicit revision change.
    std::uintptr_t cache_identity = 0;
    // Scene thread mode: a packet copied off the game's memory keeps the
    // borrowed pointers' addresses here as its mesh cache selector (opaque
    // values, never dereferenced), so the copy hits the same cached mesh.
    std::array<std::uintptr_t, 5> cache_pointers{};
    bool cache_pointers_valid = false;
    NativePreparedIdentity prepared_identity{};
    NativePreparedMeshHandle* prepared_mesh = nullptr;
    NativePreparedMaterialHandle* prepared_material = nullptr;
    NativePreparedMaterialHandle* prepared_wound_material = nullptr;
    // The supported engine can update a geometry's shape in place.  Preserve
    // its authoritative three-component revision state so retained GPU data
    // can be refreshed without rescanning or hashing the mesh every frame.
    // Five independent words keep the immutable structural identity, dynamic
    // mutation generation, and source revision separate.  Combining these
    // domains into one hash can alias a retained GPU mesh after a lifecycle
    // event.
    std::array<std::uint32_t, 5> geometry_revision{};
    bool geometry_revision_valid = false;
    // Mutable color ownership is independent of immutable topology. A
    // retained mesh may therefore refresh only its diffuse/specular ranges
    // after an authoritative lighting event without decoding or uploading
    // positions, components, UVs, terrain metadata, or indices again.
    std::array<std::uint32_t, 3> color_revision{};
    bool color_revision_valid = false;
    // Figure bounds have already passed the engine's authoritative visibility
    // test before the lower geometry call.  They are sufficient for the
    // legacy selection rectangle and avoid an O(vertices + triangles) scan.
    std::uint32_t bounds_flags = 0;
    std::array<float, 3> bounds_center{};
    float bounds_radius = 0.0f;
    std::array<float, 3> bounds_minimum{};
    std::array<float, 3> bounds_maximum{};
    bool bounds_valid = false;
    const NativePackedGroup* positions = nullptr;
    std::uint32_t position_groups = 0;
    const NativePackedGroup* normals = nullptr;
    std::uint32_t normal_groups = 0;
    const std::uint32_t* diffuse = nullptr;
    const std::uint32_t* specular = nullptr;
    const NativeTextureCoordinate* texture_coordinates = nullptr;
    std::uint32_t texture_coordinate_count = 0;
    const NativeVertexComponent* components = nullptr;
    std::uint32_t component_count = 0;
    const std::uint16_t* indices = nullptr;
    std::uint32_t index_count = 0;
    std::array<float, 16> object_to_clip{};
    const NativeMorphOffset* morphs = nullptr;
    std::uint32_t morph_count = 0;
    NativeLighting lighting{};
    NativeOwnedFixedState owned_fixed_state{};
    NativeOwnedTextureView owned_texture{};
    // Terrain base page: the shipped mip chain of the bound game surface
    // (level 0 must equal the surface bytes), or null.
    const TerrainTexturePage* terrain_page = nullptr;
    // Reorderable terrain materials of this map (texture_count + 2): the
    // demand that sizes its terrain texture-array page (0 when unknown).
    std::uint32_t terrain_page_count = 0;
    NativeTerrainState terrain{};
    const NativeTerrainVertex* terrain_vertices = nullptr;
    std::uint32_t terrain_vertex_count = 0;
    const std::uint32_t* terrain_animation_phases = nullptr;
    const NativeTerrainPhaseValues* terrain_phase_values = nullptr;
    SemanticKind semantic_kind = SemanticKind::unknown;
    NativeOrdering ordering = NativeOrdering::derive_from_state;
    float u_shift = 0.0f;
    float v_shift = 0.0f;
    std::uint32_t default_diffuse = 0xffffffffu;
    std::uint32_t default_specular = 0xff000000u;
    std::uint8_t fog = 0xff;
    std::uint32_t flags = 0;
    // The owned visible-object call has already submitted this visual. The
    // engine still walks its exact draw path to publish gameplay/selection
    // metadata; this flag asks the proxy to calculate that metadata without
    // queueing another visual draw.
    std::uint32_t decoded_channels = 0;
    void* decode_context = nullptr;
    bool (*decode)(void* context, struct NativeGeometryDecodeTarget& target) = nullptr;
    bool (*decode_colors)(
        void* context, struct NativeGeometryColorDecodeTarget& target) = nullptr;
};

struct NativePreparedPartValues {
    std::uint32_t flags = 0, default_diffuse = 0xffffffffu, default_specular = 0xff000000u;
    std::uint32_t lighting_enabled = 0;
    float alpha = 1;
    std::array<float,3> diffuse{1,1,1};
    std::array<float,4> emissive{},specular{};
    float u_shift = 0,v_shift = 0;
    std::uint32_t specular_enable = 0,ordering = 0;
};
static_assert(sizeof(NativePreparedPartValues)==80,"Compact owned material values");

struct NativePreparedPartSubmission {
    const NativeGeometryView* source = nullptr;
    // Fur prototype (fur.h): the unit's fur profile while F10 fur_mode is on,
    // and the skeleton node of this record's components with partNode 0
    // (the part's node; 0 for a merged group, whose components name theirs).
    std::shared_ptr<const FurProfile> fur;
    std::uint32_t fur_node_base = 0;
    NativePreparedPartValues values;
    std::uint64_t values_revision = 0;
    NativeContribution contribution;
    NativeWorldBounds bounds;
};

struct NativePreparedSubmission {
    const NativePreparedPartSubmission* const* parts = nullptr;
    std::uint32_t count = 0;
    NativeWorldGeometry::Pose* pose = nullptr;
    NativePreparedIdentity identity;
    const std::array<float,16>* world_to_clip = nullptr;
    // Borrowed only through the existing conditional proxy Surface adapter.
    std::uintptr_t wound_surface_interface = 0;
    // Diagnostic callsite marker only; not part of retained identity or state.
    bool retained_color_diagnostic = false;
};

// Borrowed only for synchronous depth selection. Persistent commands remain in
// the renderer; a miss asks the current owned producer to register that part.
struct NativeDepthSelection {
    const NativePreparedMeshHandle* mesh = nullptr;
    const NativePreparedMaterialHandle* material = nullptr;
    NativePreparedIdentity identity;
    NativeShadowCasterRevision revision;
    NativeWorldBounds bounds;
    std::uint64_t source_revision = 0;
    std::uint32_t producer_index = 0, producer_part = 0;
    std::uint8_t mask = 0;
    bool persistent_static = false;
};
struct NativeDepthSubmission {
    const NativeDepthSelection* parts = nullptr;
    std::uint32_t count = 0;
    const std::array<float,16>* world_to_clip = nullptr;
    void* context = nullptr;
    bool (*register_part)(void*,const NativeDepthSelection&,struct NativeGeometryResult&) = nullptr;
};

enum NativeGeometryDecodedChannel : std::uint32_t {
    native_decoded_diffuse = 1u << 0,
    native_decoded_specular = 1u << 1,
    native_decoded_terrain = 1u << 2,
};

// Destination storage is owned by the renderer and exists only while a cache
// miss is decoded.  The bridge never retains this memory and the renderer
// releases the temporary CPU vectors immediately after the immutable GPU
// upload, avoiding persistent RAM/VRAM duplication.
struct NativeGeometryDecodeTarget {
    NativePackedGroup* positions = nullptr;
    std::uint32_t position_groups = 0;
    NativePackedGroup* normals = nullptr;
    std::uint32_t normal_groups = 0;
    std::uint32_t* diffuse = nullptr;
    std::uint32_t* specular = nullptr;
    NativeTextureCoordinate* texture_coordinates = nullptr;
    std::uint32_t texture_coordinate_count = 0;
    NativeVertexComponent* components = nullptr;
    std::uint32_t component_count = 0;
    std::uint16_t* indices = nullptr;
    std::uint32_t index_count = 0;
    NativeTerrainVertex* terrain_vertices = nullptr;
    std::uint32_t terrain_vertex_count = 0;
    // Out (terrain decode): indices from here on belong to steep tiles (the
    // steep tile class: some base vertex past the walk limit), all earlier
    // ones to flat tiles (index_count: none steep); UINT32_MAX when the
    // decode does not classify (not a terrain decode).
    std::uint32_t steep_index_begin = UINT32_MAX;
};

// Destination storage exists only for one authoritative color refresh. The
// callback must emit the exact normal/component ordering of this mesh variant;
// it cannot trigger or depend on a topology decode.
struct NativeGeometryColorDecodeTarget {
    std::uint32_t* diffuse = nullptr;
    std::uint32_t* specular = nullptr;
    std::uint32_t color_count = 0;
};

struct NativeGeometryResult {
    // Acceptance and visibility are distinct. A packet may be accepted into
    // the retained GPU scene while producing no visible clipped geometry.
    bool visible = true;
    // A fully clipped extent request is valid and carries an empty RECT.
    bool screen_extents_valid = false;
    RECT screen_extents{};
    // Failure-only detail for the existing one-shot shadow diagnostic.
    HRESULT failure_hresult = S_OK;
    const char* failure_operation = nullptr;
    bool failure_texture = false;
};

using NativeGeometrySubmitFn = bool (*)(void* context, const NativeGeometryView& view,
                                        NativeGeometryResult& result);
using NativeGeometryBatchBeginFn = bool (*)(void* context, SemanticKind kind);
using NativeGeometryBatchEndFn = void (*)(void* context, bool commit);
using NativeGeometryObjectBeginFn = bool (*)(void* context);
using NativeGeometryObjectEndFn = void (*)(void* context, bool commit);
using NativeGeometryFrameStateFn = bool (*)(void* context,
                                             NativeFrameState& state);
using NativeWorldLightingSetFn = bool (*)(void* context,
                                          const NativeWorldLighting& lighting);
using NativeShadowBeginFn = bool (*)(void*, const NativeShadowFrame&, std::uint8_t&);
// After a successful begin: the light projection each sun map is drawn with
// this frame, and the far slices that keep a retained (older) projection.
struct NativeShadowCullProjection {
    std::array<std::array<float,16>,5> world_to_light{};
    std::uint8_t retained_mask = 0;
    // Maps whose cached static layer scrolls this frame: only casters reaching
    // the exposed strip (the old square, in new light NDC, is scroll_copied)
    // are needed for their static layer.
    std::uint8_t scroll_mask = 0;
    std::array<std::array<double,4>,5> scroll_copied{};
    unsigned side = 0;
    // Terrain keys (sector << 32 | part, ascending) that entered each scrolled
    // map's list; drawn whole. Owned by the renderer for this pass.
    const std::array<std::vector<std::uint64_t>,5>* scroll_entered = nullptr;
};
using NativeShadowCullProjectionFn = void (*)(void*, NativeShadowCullProjection&);
using NativeShadowEndFn = bool (*)(void*, bool);
// The persistent static caster store (casters: whole store at the revision,
// nullptr: unchanged). On a static-layer redraw the renderer draws its baked
// store layout and returns store casters it holds no current record for.
using NativeShadowStaticDepthFn = bool (*)(void*,const RetainedShadowCasterValue*,
    std::uint32_t,std::uint64_t,std::vector<RetainedShadowCasterValue>&);
// A terrain sun caster of a static-layer redraw and its final map mask.
struct NativeTerrainShadowEntry {
    NativeShadowCasterRevision revision{};
    std::uint8_t mask = 0;
};
// The renderer draws the terrain casters it holds baked records for and
// returns the entry indices the bridge must submit through the depth path.
using NativeShadowTerrainBakeFn = bool (*)(void*,const NativeTerrainShadowEntry*,
    std::uint32_t,std::vector<std::uint32_t>&);
ShadowInputDiagnostics bridge_shadow_reuse_diagnostics() noexcept;
struct NativeShadowSourceDiagnostics {
    std::uint64_t active_visuals = 0, active_terrain = 0;
    std::uint64_t changed_visuals = 0, changed_terrain = 0;
    std::uint64_t static_casters_reused = 0, part_bounds_rebuilt = 0;
    std::uint64_t receiver_refits = 0, caster_refits = 0, map_reuses = 0;
    std::uint8_t dirty_mask = 0;
};
using NativeShadowSourceDiagnosticsFn = void (*)(void*,std::uint64_t,
    const NativeShadowSourceDiagnostics&);
using NativePointLightsSetFn = bool (*)(void*,const RetainedVisualFrame&);
struct NativePointShadowFrame { std::array<NativeShadowSlice,6> slices{}; };
struct NativePointShadowPublication { int static_slot=-1,overlay_slot=-1;float strength=0,overlay_strength=0,bake_radius=0;
    bool merged=false;std::array<float,3> shadow_center{}; };
using NativePointShadowBeginFn = bool (*)(void*,std::uint32_t,std::uint32_t,const NativePointShadowFrame&);
using NativePointShadowEndFn = bool (*)(void*,bool);
// Shelter map bake for owned precipitation (renderer_precipitation.cpp): one
// straight-down slice of the persistent static casters into a side x side map.
using NativeRainOcclusionBeginFn = bool (*)(void*,const NativeShadowSlice&,std::uint32_t side);
using NativeRainOcclusionEndFn = bool (*)(void*,bool);
void bridge_set_rain_occlusion_sink(NativeRainOcclusionBeginFn begin,NativeRainOcclusionEndFn end) noexcept;
// What the renderer wants baked (published by the precipitation pass; the
// bridge bakes when the serial or the static caster store changed).
struct RainOcclusionRequest {
    bool active = false;
    float center[2]{}, half = 0, top = 0, span = 0;
    std::uint32_t side = 0;
    std::uint64_t serial = 0;
    // The renderer's neighbour-map caster key (its own casters join the
    // bake): a change re-bakes like a moved square.
    std::uint64_t casters = 0;
};
void bridge_set_rain_occlusion_request(const RainOcclusionRequest& request) noexcept;
using NativePointShadowBatchFn = bool (*)(void*,std::uint64_t&,bool&);
using NativePointShadowAllocateFn = bool (*)(void*,bool,int&);
using NativePointShadowReleaseFn = void (*)(void*,std::uint64_t,int);
using NativePointShadowPublishFn = void (*)(void*,const std::vector<NativePointShadowPublication>&,
    const NativePointShadowStats&);
// Completed game particles from the verified DrawParticles entry (slot 17).
// object_to_clip is the game's projection-space matrix (clip = M * p); the
// clip half extents are the TL pipeline's own clip rect.
struct GameParticleView {
    const GameParticle* particles = nullptr;
    std::uint32_t count = 0;
    std::array<float, 16> object_to_clip{};
    float clip_half_width = 0.0f, clip_half_height = 0.0f;
    bool additive = false;
    bool interface_pass = false;
};
using GameParticleSubmitFn = bool (*)(void* context, const GameParticleView& view);
// Registered by the device for the duration of a D3D7 scene. Without it the
// hook omits the particles and logs the reason; it never draws them itself.
void bridge_set_game_particle_sink(void* context, GameParticleSubmitFn submit) noexcept;
// One game lightning ribbon from the verified Draw3DLine entry (slot 16): the
// game's points (projection space like particles), the stock half width
// 15 * width (pixels times W) and the texture strip the caller chose. The
// renderer expands the screen-space ribbon on the GPU (game_particles.h).
struct GameRibbonView {
    const GameRibbonPoint* points = nullptr;
    std::uint32_t count = 0;
    std::array<float, 16> object_to_clip{};
    float clip_half_width = 0.0f, clip_half_height = 0.0f;
    float half_width_pixels_w = 0.0f;
    GameRibbonStrip strip{};
    bool interface_pass = false;
};
using GameRibbonSubmitFn = bool (*)(void* context, const GameRibbonView& view);
// Registered with the particle sink for the duration of a D3D7 scene.
void bridge_set_game_ribbon_sink(void* context, GameRibbonSubmitFn submit) noexcept;
// The game's selection marker from the verified DrawSelection call
// (0x0056BF12): the game's point, size and unit axes (game_particles.h).
struct GameSelectionView {
    GameSelectionMarker marker{};
    std::array<float, 16> object_to_clip{};
    float clip_half_width = 0.0f, clip_half_height = 0.0f;
};
using GameSelectionSubmitFn = bool (*)(void* context, const GameSelectionView& view);
void bridge_set_game_selection_sink(void* context, GameSelectionSubmitFn submit) noexcept;
// One game terrain print's cached mesh from the verified Render call inside
// CTerrainPrints::Draw, expanded to a world-space triangle list
// (game_particles.h). The device batches prints with their texture and state;
// flush draws the batch when the list's last print has drawn.
struct GamePrintView {
    const GameParticleGpu* vertices = nullptr;
    std::uint32_t count = 0;
    std::array<float, 16> object_to_clip{};
    float clip_half_width = 0.0f, clip_half_height = 0.0f;
};
using GamePrintSubmitFn = bool (*)(void* context, const GamePrintView& view);
using GamePrintFlushFn = bool (*)(void* context);
void bridge_set_game_print_sink(void* context, GamePrintSubmitFn submit, GamePrintFlushFn flush) noexcept;
// The game's floating combat text as screen-space glyph quads laid out by
// the renderer from the damage list (combat_text.h), drawn in one call.
struct GameGlyphView {
    const GameParticleGpu* glyphs = nullptr;
    std::uint32_t count = 0;
};
using GameGlyphSubmitFn = bool (*)(void* context, const GameGlyphView& view);
void bridge_set_game_glyph_sink(void* context, GameGlyphSubmitFn submit) noexcept;

// The active device owns this sink across scenes. The verified engine frame
// entry calls it before the native clear and before advancing any UI epoch.
using EngineFrameBeginFn = bool (*)(void* context) noexcept;
void bridge_set_engine_frame_begin_sink(void* context, EngineFrameBeginFn begin) noexcept;
void bridge_clear_engine_frame_begin_sink(void* context) noexcept;

// The global map's zone hit-test frame (#200): the verified call of
// CI3DScreenGlobalMap::GenerateHitTestDDS calls this with begin true before
// and false after (its frame and its mask capture run in between). The
// active device owns it like the frame begin sink.
using HitTestFrameFn = void (*)(void* context, bool begin) noexcept;
void bridge_set_hit_test_frame_sink(void* context, HitTestFrameFn frame) noexcept;
void bridge_clear_hit_test_frame_sink(void* context) noexcept;

// The sink is render-thread-local. A device registers it only while a D3D7
// scene is active so an unsupported or out-of-lifecycle call falls back to the
// original engine path.
void bridge_set_native_geometry_sink(void* context, NativeGeometrySubmitFn submit,
                                     NativeGeometryBatchBeginFn begin_batch = nullptr,
                                     NativeGeometryBatchEndFn end_batch = nullptr,
                                     NativeGeometryObjectBeginFn begin_object = nullptr,
                                     NativeGeometryObjectEndFn end_object = nullptr,
                                     NativeGeometryFrameStateFn frame_state = nullptr,
                                     NativeWorldLightingSetFn world_lighting = nullptr,
                                     NativeShadowBeginFn shadow_begin = nullptr,
                                     NativeShadowEndFn shadow_end = nullptr,
                                     NativeShadowSourceDiagnosticsFn shadow_diagnostics = nullptr,
                                     NativePointLightsSetFn point_lights = nullptr,
                                     NativePointShadowBeginFn point_shadow_begin = nullptr,
                                     NativePointShadowEndFn point_shadow_end = nullptr,
                                     NativeShadowStaticDepthFn static_depth = nullptr,
                                     NativePointShadowBatchFn point_shadow_batch = nullptr,
                                     NativePointShadowPublishFn point_shadow_publish = nullptr,
                                     NativePointShadowAllocateFn point_shadow_allocate = nullptr,
                                     NativePointShadowReleaseFn point_shadow_release = nullptr,
                                     NativeShadowCullProjectionFn shadow_cull_projection = nullptr,
                                     NativeShadowTerrainBakeFn terrain_bake = nullptr);

#ifdef EID3D11_TESTING
bool bridge_test_build_owned_point_shadow_faces(const RetainedWorldPointLight& light,
                                                unsigned first_face,NativeShadowFrame& faces) noexcept;
#endif

enum class ObjectRenderRoute : std::uint8_t {
    original,
    hidden_owned,
    direct_owned,
    diagnostic_missing,
};

struct ObjectRenderRouteInput {
    bool cutover_installed = false;
    bool world_scope = false;
    bool lifecycle_ready = false;
    bool object_hidden = false;
    bool plan_ready = false;
    bool complete_pose_ready = false;
    bool complete_parts_ready = false;
};

constexpr ObjectRenderRoute object_render_route(
    const ObjectRenderRouteInput& input) noexcept {
    if (!input.cutover_installed || !input.world_scope)
        return ObjectRenderRoute::original;
    if (!input.lifecycle_ready)
        return ObjectRenderRoute::diagnostic_missing;
    if (input.object_hidden) return ObjectRenderRoute::hidden_owned;
    return input.plan_ready && input.complete_pose_ready &&
                   input.complete_parts_ready
        ? ObjectRenderRoute::direct_owned
        : ObjectRenderRoute::diagnostic_missing;
}

// The shipped loop uses this large-position sentinel to skip inactive figure
// attachments without placement, visibility publication, or a draw.
constexpr bool object_render_figure_position_active(
    float x, float y, float z) noexcept {
    return !(x * x + y * y + z * z > 1.0e9f);
}

constexpr std::uintptr_t object_render_wound_surface_for_set(
    std::uint8_t set, std::uintptr_t active_surface) noexcept {
    return set == 0 ? active_surface : 0;
}

constexpr std::uint32_t object_render_accumulate_lighted(
    std::uint32_t aggregate, std::uint32_t figure_flags,
    bool active) noexcept {
    return active ? aggregate & figure_flags : aggregate;
}

constexpr std::uint32_t object_render_active_figure_flags(
    std::uint32_t owner_flags, std::uint32_t figure_flags,
    bool active) noexcept {
    return active && (owner_flags & 0x400u) == 0
        ? figure_flags & ~0x10u
        : figure_flags;
}

class EngineBridge {
public:
    static EngineBridge& instance();

    // Installation is deliberately fail-closed.  No table is modified unless
    // every bridge site in the game.exe file has its expected bytes
    // (game_sites.h) and every selected slot has an independently verified
    // value/signature. sites_verified() is that bridge-site result.
    bool install();
    bool uninstall();
    bool installed() const { return installed_; }
    bool sites_verified() const { return sites_verified_; }
    const wchar_t* resource_registry_key() const noexcept;

    SemanticContext current() const;
    std::uint32_t image_rva(const void* address) const;

private:
    EngineBridge() = default;
    EngineBridge(const EngineBridge&) = delete;
    EngineBridge& operator=(const EngineBridge&) = delete;

    bool patch_slots();
    void report(const char* event, const char* detail) const;

    HMODULE image_ = nullptr;
    bool sites_verified_ = false;
    bool installed_ = false;
};

SemanticContext current_semantic_context();
void bridge_report_event(const char* event, const char* detail) noexcept;
bool bridge_consume_hud_atlas_window(
    IDirectDrawSurface7* surface, const void* lock_caller,
    HudAtlasMutationWindow& window) noexcept;
bool bridge_begin_character_pane_getdc(
    IDirectDrawSurface7* surface, const void* caller,
    std::uint64_t surface_id, std::uint32_t width,
    std::uint32_t height, float raster_scale = 1.0f) noexcept;
bool bridge_finish_character_pane_releasedc(
    IDirectDrawSurface7* surface, const void* caller,
    std::uint64_t surface_id, std::uint64_t surface_generation = 0) noexcept;
bool bridge_consume_character_pane_blt(
    IDirectDrawSurface7* source_surface, std::uint64_t source_id, const void* caller,
    const RECT& destination, const RECT* source_rect,
    Renderer& renderer, HRESULT& result,
    std::uint64_t source_generation = 0) noexcept;
// camp_inventory's DescribeItem (005EE760) hook: runs describe(panel) with
// the panel's description surface adopted for owned item card text.
void bridge_item_card_text_describe(void* panel,
                                    void (__thiscall* describe)(void*));
void bridge_retire_owned_text_surface(std::uint64_t surface_id) noexcept;
bool bridge_character_pane_text_epoch_current(
    std::uint64_t ui_epoch, std::uint64_t frame_serial) noexcept;
bool bridge_character_pane_list_current(std::uint64_t ui_epoch, std::uint64_t frame_serial) noexcept;
void bridge_profile_begin_frame();
void bridge_profile_end_frame();
void bridge_profile_api_entry(const char* kind, const void* caller);
void bridge_set_profile_enabled(bool enabled);
void bridge_set_shadow_close_range(float metres) noexcept;
void bridge_set_shadow_settings(std::uint32_t map_side,
    std::uint8_t close_cascades,bool far_shadows,bool point_shadows) noexcept;
void bridge_set_point_shadow_merge_distance(float distance) noexcept;
void bridge_set_point_shadow_fast_overlays(int count) noexcept;
// Renderer-owned game cursor: the game keeps choosing cursor id, animation,
// position and hotspot; the renderer draws the published frame after all UI.
struct OwnedCursorFrame {
    bool visible = false;
    void* surface = nullptr;      // the game's cursor strip (IDirectDrawSurface7)
    RECT source{};                // current frame inside the strip
    LONG x = 0, y = 0;            // frame top-left (cursor position minus hotspot)
    LONG hot_x = 0, hot_y = 0;    // hotspot inside the frame
};
// False while the owned cursor hooks are not installed.
bool bridge_owned_cursor_frame(OwnedCursorFrame& frame) noexcept;
// Temporary EI_RENDERER_INPUT_FOCUS_TRACE=1 snapshots in bridge.jsonl.
void bridge_trace_input_focus(HWND window, const char* phase, UINT message,
                              WPARAM wparam, int overlay_mode) noexcept;
// Owned DrawFrameSelection rectangles. A device registers the sink only while
// a D3D7 scene is active; a request without it is omitted and logged once.
using ScreenRectangleSubmitFn = bool (*)(void* context,
                                         const ScreenRectangleRequest& request);
void bridge_set_screen_rectangle_sink(void* context,
                                      ScreenRectangleSubmitFn submit) noexcept;
void bridge_set_skip_scenery_draw_walk(bool enabled) noexcept;
void bridge_set_merge_character_parts(bool enabled) noexcept;
bool bridge_skip_scenery_draw_walk_enabled() noexcept;
bool bridge_change_driven_poses_enabled() noexcept;
void bridge_set_fur_mode(int mode) noexcept;
// Owned party portraits (engine_bridge.cpp submit_owned_portrait).
void bridge_set_portrait_options(bool owned, bool helmet) noexcept;
bool bridge_set_world_map_previews(Renderer* renderer, bool enabled) noexcept;
void bridge_clear_world_map_previews(Renderer* renderer) noexcept;
void bridge_set_highlight_options(bool briefing, bool selected, bool hover_characters,
                                  bool hover_objects) noexcept;
void bridge_set_change_driven_poses(bool enabled) noexcept;
void bridge_set_terrain_far_shadow_prewarm(bool enabled) noexcept;
void bridge_set_terrain_lod_enabled(bool enabled) noexcept;
// Owned sky (sky_clouds.h): false keeps the game's own sky pass.
void bridge_set_owned_sky(bool enabled) noexcept;
bool bridge_owned_sky_installed() noexcept;
// Owned precipitation: why the stock DrawRain/DrawSnow calls still draw
// (both kept, doubled weather), or nullptr while both are suppressed.
const char* bridge_owned_weather_problem() noexcept;
// Render-only weather test (0 game, 1 rain, 2 snow) and the visual strength
// setting (0 varies, 1-3 light/medium/heavy) the environment reader applies.
void bridge_set_weather_visual(int test, int strength_setting) noexcept;
// The verified game image's semantic renderer slots are patched; false means
// the DLL runs only as a D3D wrapper of the game's own draw stream.
bool bridge_semantic_slots_installed() noexcept;
// Every game.exe site group verified (game_sites.h); false names a feature off.
bool bridge_game_sites_all_verified() noexcept;
// For a while after a loaded save replaced a map object under the same ID
// (save_id_clash in bridge.jsonl): the on-screen warning, else null.
const char* bridge_save_id_clash_notice() noexcept;
// Player-visible features a failed install left off, noted where each
// install decides. The owned visual publication carries the world point
// lights, ground cover, critters and the character figures drawn in panels.
enum BridgeFeatureOff : std::uint32_t {
    bridge_feature_off_owned_world = 1u << 0,
    bridge_feature_off_camp_grid = 1u << 1,
    bridge_feature_off_hud_layout = 1u << 2,
    bridge_feature_off_hud_icons = 1u << 3,
};
void bridge_note_feature_off(std::uint32_t features) noexcept;
// One line naming an unrecognised or partly recognised addon.dll and the
// features that are off; null when the addon is fine or nothing is off.
const char* bridge_addon_notice() noexcept;
// ---- Renderer ownership verification (issue #70) ----
// Every code/slot patch the bridge, widescreen and camp hooks write into
// game.exe is recorded right after the write (`original` = the bytes it
// replaced; the first 5 bytes are kept). Verification classifies each live
// record: ours (any bytes we wrote there), released (the original bytes
// again), or taken over (anything else).
void bridge_note_owned_patch(const void* address, const void* original, std::size_t bytes) noexcept;
// Verifies game.exe's ddraw.dll import slots and every recorded patch; logs
// one renderer_ownership summary and one hook_taken_over per foreign entry.
// `when` names the check point (startup, first_world_frame).
void bridge_verify_renderer_ownership(const char* when) noexcept;
// Called every present: verifies once after the first completed world draw
// (or after 600 presents without one, in case the world draw was taken).
void bridge_verify_renderer_ownership_after_world() noexcept;
// Logs where game.exe's ddraw.dll imports point (ddraw_import_owner).
void bridge_log_ddraw_imports(const char* when) noexcept;
// Process exit without any DirectDrawCreate(Ex) call into this DLL: one
// ddraw_never_called line naming the import owners. No heap use.
void bridge_log_never_called_at_exit() noexcept;
// One line naming what another module took over, null when nothing was.
const char* bridge_takeover_notice() noexcept;
// The game asked for its sky (WC_RENDER_SKY) in the current bridge frame.
bool bridge_owned_sky_requested() noexcept;
#ifdef EID3D11_TESTING
std::array<std::uint32_t,4> bridge_test_shadow_settings() noexcept;
bool bridge_test_terrain_lod_enabled() noexcept;
#endif
const BridgeProfileSnapshot* bridge_latest_profile() noexcept;
SceneWorldToClipSnapshot bridge_scene_world_to_clip_snapshot() noexcept;
// Stock distance fog read from the TL pipeline at the retained scene seam of
// the current bridge frame; invalid outside it (world_fog.h).
struct WorldFogSnapshot {
    std::uint64_t scene_frame_serial = 0;
    bool valid = false;
    bool enabled = false; // the game's own distance-fog gate
    const char* failure = "seam_not_reached_this_frame"; // first failing check when !valid
    StockDistanceFog fog{};
    WorldEnvironment environment{}; // time of day and weather (density inputs)
};
WorldFogSnapshot bridge_world_fog_snapshot() noexcept;
// Swamp mist source of the active map, rebuilt when the terrain source or
// its world transform changes; null outside a world frame.
std::shared_ptr<const WorldMistGrid> bridge_world_mist_grid() noexcept;
std::uint64_t bridge_current_world_epoch() noexcept;
// Experimental displayed XY pan; game camera state/timing are never changed.
// OFF invalidates the sampled history before another Step/view can use it.
void bridge_set_camera_pan_smoothing(bool enabled) noexcept;
const char* bridge_camera_pan_smoothing_unavailable_reason() noexcept;
// Game thread, at the renderer's world change: the bridge's world caches
// (server seed assemblies, frame spare, terrain caster caches).
void bridge_release_world_caches() noexcept;
// Load reveal state for the load pause at the world Draw entry: the hold is
// wanted while enabled and the current world epoch is not yet revealed.
void bridge_set_load_reveal(bool enabled, std::uint64_t revealed_epoch) noexcept;
// The game's open progress movie (005DA400): its size, frame count and BINK
// current frame (1-based, one past the frame last decoded and shown, so the
// shown frame's 0-based index is current - 2), and the file the game opens
// (work path + "Movies\" + name). False when none is open or the bridge sites
// did not verify.
// A typed host decoder. It owns its file/codec lifetime and returns one
// zero-based frame as opaque BGRA. No host player pointers or retail RVAs.
struct ProgressMovieFrames {
    virtual ~ProgressMovieFrames() = default;
    virtual bool read(std::uint32_t frame, std::vector<std::uint8_t>& bgra) = 0;
};
struct ProgressMovie {
    std::shared_ptr<ProgressMovieFrames> source;
    std::uint32_t width = 0, height = 0, frames = 0, current = 0;
    std::string path;
};
bool bridge_progress_movie(ProgressMovie& movie) noexcept;
// The local single-player server runs a loaded mission (not the menu, the
// travel map or a remote server); game thread.
bool bridge_local_single_player_mission() noexcept;
// Reuse the bridge's content-verified mod addon discovery for harness path patches.
void* bridge_supported_addon_image() noexcept;
// Check a selected loaded module with the bridge's existing PE/file contract.
bool bridge_exact_loaded_module(void* module, std::uint32_t image_size,
                               const std::array<std::uint8_t, 32>& digest) noexcept;
struct WorldDrawCompletionSnapshot {
    std::uint64_t bridge_frame = 0, completion_serial = 0;
    bool valid = false;
};
WorldDrawCompletionSnapshot bridge_world_draw_completion_snapshot() noexcept;
std::uint64_t bridge_world_draw_completion_counter() noexcept;
bool bridge_current_world_time(double& time) noexcept;
#ifdef EID3D11_TESTING
void bridge_test_world_draw_completion(bool success) noexcept;
#endif

// Scene thread (render_thread option). Registered by the device for its D3D7
// scene (game thread) while the renderer runs a scene thread. With it the
// bridge splits its world work at the retained scene seam and at the
// transparent terrain range: the game thread reads game memory and copies the
// frame's inputs, the scene thread prepares and submits from that copy.
struct SurfaceImage;
struct BridgeSceneHandoff {
    void* context = nullptr;
    // Game thread: true while the renderer's scene thread owns it.
    bool (*active)(void* context) = nullptr;
    // Game thread: queue work on the scene thread, after everything queued
    // so far, with this point's bridge frame values installed there.
    bool (*post)(void* context, std::function<void()> work) = nullptr;
    // Game thread: the device state the native sinks read (D3D7 fixed state,
    // frame state, world environment), copied; flushes the device's pending
    // legacy draws and UI first, as a native sequence begins.
    std::shared_ptr<void> (*capture_sinks)(void* context) = nullptr;
    // Scene thread: native sinks over that copy (null clears them).
    void (*install_sinks)(void* sinks) = nullptr;
    // Game thread: snapshot of the texture bound to stage 0 now.
    std::shared_ptr<SurfaceImage> (*bound_texture)(void* context) = nullptr;
    // Scene thread: the stage-0 texture the next sink submission samples.
    void (*bind_texture)(void* sinks, const std::shared_ptr<SurfaceImage>& texture) = nullptr;
    // Game thread: snapshot of a proxy surface interface (wound layers).
    std::shared_ptr<SurfaceImage> (*surface_snapshot)(void* context, std::uintptr_t surface_interface) = nullptr;
    // Scene thread: the wound surface a submission names resolves to this snapshot.
    void (*set_wound)(void* sinks, std::uintptr_t surface_interface, const std::shared_ptr<SurfaceImage>& texture) = nullptr;
    // Game thread: wait until the scene thread drained (a path that touches
    // state the scene thread also uses).
    void (*sync)(void* context, const char* reason) = nullptr;
    // Scene thread: the next submissions through `sinks` use the device state,
    // frame state and stage-0 texture captured in `from` (one transaction
    // across several captured render-state sets).
    void (*adopt_sinks)(void* sinks, const void* from) = nullptr;
    // Game thread, core mode (EI_RENDERER_X64_CORE): the device-free Renderer
    // that sends typed world commands to the host (null outside core mode;
    // the bridge then posts closures as above).
    Renderer* (*core_renderer)(void* context) = nullptr;
};
void bridge_set_scene_handoff(const BridgeSceneHandoff& handoff) noexcept;
void bridge_clear_scene_handoff(void* context) noexcept;

// Scene thread (render_thread option; renderer scene_post). The bridge keeps
// its per-frame values thread_local to the game thread's hooks. A queued
// renderer command carries a copy taken when it was queued, and the scene
// thread installs it into its own thread_local slots before the command
// runs, so renderer pulls (world_to_clip, fog, completion, text epochs,
// frame serial) read the values of the command's place in the frame.
// The terrain atlases (bridge_terrain_atlas), resolved by the game thread as
// it queues a world command; one entry per material wrapper.
struct TerrainAtlasSnapshot {
    std::vector<std::shared_ptr<const OwnedVisualSurface>> surfaces;
    std::vector<const char*> failures; // static strings; null with a surface
};
struct BridgeFrameValues {
    std::uint64_t frame_serial = 0;
    WorldDrawCompletionSnapshot completion{};
    std::uint64_t completion_counter = 0;
    SceneWorldToClipSnapshot world_to_clip{};
    WorldFogSnapshot fog{};
    std::uint64_t character_pane_ui_epoch = 0;
    std::uint64_t chat_text_ui_epoch = 0;
    std::uint64_t item_card_text_ui_epoch = 0;
    // Game reads the renderer makes: on the scene thread the bridge answers
    // these from the installed copy, never from game memory.
    WorldEnvironment environment{};
    double world_time = 0;
    bool world_time_valid = false;
    bool local_single_player = false;
    bool owned_sky_requested = false;
    bool world_draw_active = false;  // queued inside the outer World::Draw
    std::uint64_t world_epoch = 0;   // lifecycle positive world epoch when queued
    std::shared_ptr<const TerrainAtlasSnapshot> terrain_atlases;
};
// False means the host has not published a complete queue snapshot.
bool bridge_capture_frame_values(BridgeFrameValues& values) noexcept;
#ifdef EID3D11_EMBEDDED
// Host publishes synchronously; the scene thread uses its installed owning copy.
bool bridge_capture_embedded_frame_values(BridgeFrameValues& values) noexcept;
const BridgeFrameValues* bridge_embedded_installed_frame() noexcept;
#endif
void bridge_install_frame_values(const BridgeFrameValues& values) noexcept;
// The bridge frame profile (large; once per presented frame).
struct BridgeProfileValues {
    bool has_profile = false;
    BridgeProfileSnapshot profile{};
};
void bridge_capture_profile_values(BridgeProfileValues& output) noexcept;
void bridge_install_profile_values(const BridgeProfileValues& values) noexcept;

enum class SceneRendererTimingKind : std::uint8_t {
    native_flush,
    ui_flush,
};

struct SceneRendererTimingToken {
    std::int64_t begin = 0;
    std::uint8_t context = 0;
    SceneRendererTimingKind kind = SceneRendererTimingKind::native_flush;
    bool active = false;
};

struct SceneQueueTimingToken {
    std::int64_t begin = 0;
    std::int64_t native_ticks_at_begin = 0;
    std::int64_t ui_ticks_at_begin = 0;
    std::uint8_t context = 0;
    bool active = false;
};

SceneRendererTimingToken bridge_scene_renderer_timing_begin(
    SceneRendererTimingKind kind) noexcept;
void bridge_scene_renderer_timing_end(
    SceneRendererTimingToken& token) noexcept;
SceneQueueTimingToken bridge_scene_queue_timing_begin() noexcept;
void bridge_scene_queue_timing_end(SceneQueueTimingToken& token) noexcept;

inline constexpr std::size_t kOuterFrameTimingReasonCapacity = 96;
inline constexpr std::size_t kOuterFrameTimingStatusCapacity = 32;

// Fixed direct calls in the fingerprinted CountTime caller. These names
// describe call position, not a claimed gameplay or server owner.
enum class OuterFrameDirectCall : std::size_t {
    pre_tick,
    fixed_step,
    post_tick,
    count,
};

struct OuterFrameDirectCallSample {
    std::int64_t original_ticks = 0;
    std::int64_t unit_original_ticks = 0;
    std::int64_t unit_finish_ticks = 0;
    std::uint64_t calls = 0;
};

enum class NativeTickOwnedCallback : std::size_t {
    packet_open,
    database_binding,
    selected_clip,
    received_packet_completion,
    unit_segment_post,
    appearance_outside_finish,
    count,
};

struct NativeTickOwnedCallbackSample {
    std::int64_t exclusive_ticks = 0;
    std::uint64_t calls = 0;
};

// A fixed-size trace in the existing five-second outer-frame recorder. No
// borrowed addresses, asset names, or per-call heap storage leave the hook.
constexpr std::size_t kOuterFrameVisualEventCapacity = 64;
enum class OuterFrameVisualEventKind : std::uint8_t {
    resource_begin, resource_finish, resource_destroy, appearance
};
enum class OuterFrameAppearanceCallback : std::uint8_t {
    unknown, virtual_create, load_template, load_figures, addon_load_figures,
    change_weapon, set_materials, load_figure, add_unloaded, final_data,
    load_textures, change_texture, highlight, plan_add_figure,
    plan_remove_figure, complection, database_binding, packet_finish
};
struct OuterFrameVisualEvent {
    std::uint64_t sequence = 0;
    std::uint64_t serial = 0;
    std::int64_t exclusive_ticks = 0; // appearance only; excludes nested owned callbacks
    std::int32_t signed_id = 0;
    std::uint32_t source_mask = 0; // OR of nested callback sources
    std::uint16_t change_mask = 0; // changed/material/texture/completed/emissive/specular/weapons/rebound
    OuterFrameVisualEventKind kind = OuterFrameVisualEventKind::appearance;
    OuterFrameAppearanceCallback callback = OuterFrameAppearanceCallback::unknown;
    std::uint8_t phase = 0; // 0=outside unit update, 1=native unit original, 2=unit finish
    bool assembly_before = false;
    bool assembly_after = false;
    bool succeeded = false;
};

struct OuterFrameAppearanceDiagnostic {
    std::int64_t model_native_ticks = 0;
    std::int64_t model_owned_ticks = 0;
    std::int64_t model_provenance_ticks = 0;
    std::int64_t model_resolve_ticks = 0;
    std::int64_t model_file_read_ticks = 0;
    std::int64_t model_comparison_ticks = 0;
    std::int64_t model_archive_ticks = 0;
    std::int64_t asset_lookup_ticks = 0;
    // A subset of asset_lookup_ticks, not another exclusive leaf.
    std::int64_t asset_decode_ticks = 0;
    std::int64_t material_ticks = 0;
    std::int64_t texture_ticks = 0;
    std::int64_t publication_ticks = 0;
    std::uint64_t model_acquisitions = 0;
    std::uint64_t model_selected_bytes = 0;
    std::uint64_t model_unique_identities = 0;
    std::uint64_t model_repeated_identities = 0;
    std::uint64_t model_repeated_selected_bytes = 0;
    std::uint64_t model_file_read_bytes = 0;
    std::uint64_t model_file_read_calls = 0;
    std::uint64_t model_repeated_file_read_bytes = 0;
    std::uint64_t model_backing_cache_hits = 0;
    std::uint64_t model_comparison_span_bytes = 0;
    std::uint64_t model_repeated_comparison_span_bytes = 0;
    std::uint64_t skeleton_cache_hits = 0;
    std::uint64_t skeleton_cache_misses = 0;
    std::uint64_t figure_cache_hits = 0;
    std::uint64_t figure_cache_misses = 0;
    std::uint64_t decoded_figure_bytes = 0;
    bool available = false;
    bool identity_complete = false;
    bool mixed_resolver_generation = false;
    // At most the two exact selected models needed by this diagnostic question.
    // Exact keys have a fixed appearance-event cap; recorder frames hold counts only.
    std::array<std::array<char, 256>, 2> selected_identity_keys{};
    std::array<std::uint64_t, 2> selected_identity_generations{};
    std::uint8_t selected_identity_count = 0;
    bool selected_identity_overflow = false;
};

struct OuterFrameVisualFinishSample {
    std::int64_t finish_ticks = 0;
    std::int64_t appearance_ticks = 0;
    std::int64_t selected_clip_ticks = 0;
    std::int64_t outer_unit_values_ticks = 0;
    std::int32_t signed_id = 0;
    std::uint64_t resource_serial = 0;
    std::uint64_t appearance_calls = 0;
    std::uint64_t selected_clip_calls = 0;
    std::uint64_t outer_unit_values_calls = 0;
    std::uint32_t failure_status = 0;
    bool has_scope = false;
    bool completion_succeeded = false;
    bool appearance_attempted = false;
    bool appearance_succeeded = false;
    bool appearance_changed = false;
    bool appearance_missing_assembly = false;
    OuterFrameAppearanceDiagnostic appearance_diagnostic{};
    std::array<char, kOuterFrameTimingReasonCapacity> failure_reason{};
    std::array<char, kOuterFrameTimingReasonCapacity> appearance_reason{};
    std::array<char, kOuterFrameTimingStatusCapacity> failure_status_name{};
};

struct OuterFrameTimingSnapshot {
    std::uint64_t bridge_frame = 0;
    std::int64_t count_time_ticks = 0;
    std::int64_t interface_on_idle_ticks = 0;
    std::int64_t message_handling_ticks = 0;
    std::int64_t post_end_frame_to_present_entry_ticks = 0;
    std::int64_t visual_unit_update_original_ticks = 0;
    std::int64_t visual_unit_update_finish_ticks = 0;
    std::int64_t visual_unit_update_finish_appearance_ticks = 0;
    std::int64_t visual_unit_update_finish_selected_clip_ticks = 0;
    std::int64_t visual_unit_update_finish_outer_unit_values_ticks = 0;
    // Inclusive native original in the existing lifecycle-tick wrapper.
    // Unit-update child ticks are subsets, never additive CountTime leaves.
    std::int64_t native_lifecycle_tick_original_ticks = 0;
    std::int64_t native_tick_unit_original_ticks = 0;
    std::int64_t native_tick_unit_finish_ticks = 0;
    std::array<NativeTickOwnedCallbackSample,
        static_cast<std::size_t>(NativeTickOwnedCallback::count)> native_tick_owned_callbacks{};
    std::array<OuterFrameVisualEvent, kOuterFrameVisualEventCapacity> visual_events{};
    std::uint32_t visual_event_count = 0;
    std::uint32_t visual_event_overflow = 0;
    std::array<OuterFrameDirectCallSample,
        static_cast<std::size_t>(OuterFrameDirectCall::count)> direct_calls{};
    std::int64_t visual_world_tick_ticks = 0;
    std::int64_t visual_world_tick_objects_ticks = 0;
    std::int64_t visual_world_tick_terrain_ticks = 0;
    std::int64_t visual_world_tick_bridge_forced_ticks = 0;
    std::int64_t visual_world_tick_commit_ticks = 0;
    std::int64_t post_engine_surface_proxy_ticks = 0;
    std::int64_t post_engine_surface_flush_ticks = 0;
    std::int64_t post_engine_surface_cpu_rect_ticks = 0;
    std::int64_t post_engine_completed_surface_ticks = 0;
    std::uint64_t count_time_calls = 0;
    std::uint64_t interface_on_idle_calls = 0;
    std::uint64_t message_handler_calls = 0;
    std::uint64_t visual_unit_update_original_calls = 0;
    std::uint64_t visual_unit_update_finish_calls = 0;
    std::uint64_t visual_unit_update_finish_appearance_calls = 0;
    std::uint64_t visual_unit_update_finish_selected_clip_calls = 0;
    std::uint64_t visual_unit_update_finish_outer_unit_values_calls = 0;
    std::uint64_t native_lifecycle_tick_original_calls = 0;
    std::uint64_t visual_world_tick_calls = 0;
    std::uint64_t visual_world_tick_manager_objects = 0;
    std::uint64_t visual_world_tick_terrain_layers = 0;
    std::uint64_t visual_world_tick_terrain_noise_values = 0;
    std::uint64_t visual_world_tick_bridge_planes = 0;
    std::uint64_t visual_world_tick_forced_ids = 0;
    std::uint64_t visual_world_tick_committed_entities = 0;
    std::uint64_t post_engine_surface_proxy_calls = 0;
    std::uint64_t post_engine_surface_flush_calls = 0;
    std::uint64_t post_engine_surface_cpu_rect_calls = 0;
    std::uint64_t post_engine_surface_cpu_rect_pixels = 0;
    std::uint64_t post_engine_completed_surface_calls = 0;
    std::uint64_t post_engine_completed_surface_bytes = 0;
    std::uint64_t post_engine_surface_material_group_full_scans = 0;
    OuterFrameVisualFinishSample slowest_visual_unit_update_finish{};
    bool fresh = false;
};

enum class PostEngineSurfaceTimingKind : std::uint8_t {
    proxy,
    flush,
    cpu_rect,
    completed_surface,
};

struct PostEngineSurfaceTimingToken {
    std::int64_t begin = 0;
    std::int64_t excluded_at_begin = 0;
    PostEngineSurfaceTimingKind kind = PostEngineSurfaceTimingKind::proxy;
    bool tracked = false;
    bool owner = false;
    bool inside_flush = false;
};

bool bridge_outer_frame_timing_supported() noexcept;
void bridge_set_outer_frame_timing_active(bool active) noexcept;
bool bridge_take_outer_frame_timing(
    std::int64_t present_entry,
    OuterFrameTimingSnapshot& snapshot) noexcept;
bool bridge_outer_frame_timing_matches(
    const OuterFrameTimingSnapshot& snapshot,
    const BridgeProfileSnapshot* bridge) noexcept;
PostEngineSurfaceTimingToken bridge_post_engine_surface_timing_begin(
    PostEngineSurfaceTimingKind kind) noexcept;
void bridge_post_engine_surface_timing_end(
    PostEngineSurfaceTimingToken token, std::uint64_t amount = 0) noexcept;
void bridge_post_engine_surface_material_group_scan() noexcept;

#ifdef EID3D11_TESTING
struct RetainedVisualFrame;
struct RetainedTerrainSourceSnapshot;
struct OwnedTerrainFrameInputs;
struct OwnedTerrainCommand;
struct OwnedTerrainGeometryValues;
struct TerrainCpuAsset;
bool bridge_test_capture_terrain_geometry(const void* terrain,const std::uint8_t* image,
    const TerrainCpuAsset& source,double time,OwnedTerrainGeometryValues& values) noexcept;
bool bridge_test_submit_terrain_geometry(const RetainedTerrainSourceSnapshot& source,
    const OwnedTerrainFrameInputs& inputs,const OwnedTerrainCommand& command,
    const NativeContribution& contribution,const std::vector<std::uint32_t>* phases,
    NativePreparedMeshHandle& handle, NativeTerrainPhaseValues* phase_values=nullptr) noexcept;
struct OwnedSceneSubmissionTestSnapshot {
    std::uint64_t considered = 0, eligible = 0, submitted_parts = 0;
    std::uint64_t shadow_considered = 0, shadow_eligible = 0, shadow_submitted = 0;
    OwnedVisualDiagnostic diagnostic{};
    std::uint64_t pose_preparations = 0, color_sort_entries = 0;
    std::uint64_t part_bounds_computations = 0, part_bounds_reuses = 0;
    std::vector<std::pair<NativeWorldBounds,NativeContribution>> part_values;
    NativeShadowFrame shadows;
    std::uint64_t terrain_spatial_computations = 0;
    std::uint64_t terrain_spatial_reuses = 0;
    std::uint64_t terrain_front_end_reuses = 0;
    std::uint64_t terrain_receiver_reuses = 0;
    std::uint64_t terrain_caster_reuses = 0;
    std::uint64_t terrain_record_reuses = 0;
    std::uint64_t terrain_spatial_geometry_revision = 0;
    std::size_t terrain_base_commands = 0,terrain_transparent_commands = 0;
    std::vector<std::pair<NativeWorldBounds,NativeWorldBounds>> terrain_spatial_values;
};
bool bridge_test_owned_visual_diagnostic_lifetime() noexcept;
bool bridge_test_owned_shadow_failure_record() noexcept;
bool bridge_test_submit_owned_visual_frame(const RetainedVisualFrame& frame,
    const std::array<float,16>& world_to_clip, const NativeWorldLighting& light,
    bool draw_undetected, std::uint32_t class_flags,
    const std::vector<NativeWorldBounds>& receivers,
    OwnedSceneSubmissionTestSnapshot& snapshot) noexcept;
bool bridge_test_submit_owned_shadow_frame(const RetainedVisualFrame& frame,
    const std::array<float,16>& world_to_clip,const NativeWorldLighting& light,
    const std::vector<NativeWorldBounds>& receivers,bool color,
    OwnedSceneSubmissionTestSnapshot& snapshot,bool reuse_parts=true,
    RetainedShadowInputDomain* static_domain=nullptr,
    const std::vector<std::int32_t>* changed_static_ids=nullptr) noexcept;
struct OwnedShadowDeltaTestSnapshot {
    // Exact bridge route: 0 full, 1 unchanged map reuse, 2 selective source.
    std::uint32_t route = 0;
    bool cold_full = false;
    std::uint32_t reuse_begins = 0, reuse_commits = 0;
    bool coverage_same = false;
    bool equivalent = false;
    bool active_sample_changed = false;
    NativeWorldBounds old_receiver{}, new_receiver{};
    std::size_t old_full_receiver_count = 0, new_full_receiver_count = 0;
    std::uint64_t static_casters_reused = 0;
    std::uint64_t selected_candidate_visits = 0;
    std::uint64_t full_candidate_visits = 0;
    std::uint64_t old_changed_casters = 0, new_changed_casters = 0;
};
bool bridge_test_owned_visual_shadow_delta(const RetainedVisualFrame& before,
    const RetainedVisualFrame& after,const std::array<float,16>& before_clip,
    const std::array<float,16>& after_clip,const NativeWorldLighting& light,
    const std::vector<NativeWorldBounds>& receivers,RetainedStaticScene& scene,
    std::uint64_t cached_revision,std::int32_t changed_id,
    OwnedShadowDeltaTestSnapshot& result) noexcept;
struct OwnedStaticShadowReuseTestSnapshot {
    bool pan_equivalent = false, removal_equivalent = false;
    bool removed_caster_absent = false;
    std::uint64_t pan_reused = 0;
};
bool bridge_test_owned_static_shadow_reprojection(
    const RetainedVisualFrame& before,const RetainedVisualFrame& panned,
    const RetainedVisualFrame& removed,
    const std::array<float,16>& before_clip,
    const std::array<float,16>& after_clip,
    const NativeWorldLighting& light,
    const std::vector<NativeWorldBounds>& receivers,
    RetainedStaticScene& scene,std::uint64_t before_revision,
    std::int32_t removed_id,OwnedStaticShadowReuseTestSnapshot& result) noexcept;
struct OwnedTerrainSelectiveTestSnapshot {
    bool unchanged = false, phase_changed = false;
    bool alpha_unavailable = false, wave_changed = false;
    bool current_commands = false, point_noncaster_ignored = false;
    std::uint64_t initial_casters = 0;
};
bool bridge_test_owned_terrain_shadow_selection(const RetainedTerrainSourceSnapshot& source,
    const OwnedTerrainFrameInputs& inputs,RetainedStaticScene& scene,
    OwnedTerrainSelectiveTestSnapshot& result) noexcept;
struct OwnedTerrainShadowTestInputs {
    RetainedStaticScene* scene = nullptr;
    const std::uint8_t* game_image = nullptr;
    const std::vector<std::vector<std::uint32_t>>* phases = nullptr;
    const std::vector<std::uint64_t>* phase_revisions = nullptr;
    const RetainedVisualFrame* entities = nullptr;
    bool color = false;
    bool base_color = false;
    bool terrain_receivers = false;
    void (*after_base_capture)(void*) = nullptr;
    void* after_base_context = nullptr;
};
bool bridge_test_submit_owned_terrain_shadows(const RetainedTerrainSourceSnapshot& source,
    const OwnedTerrainFrameInputs& inputs,void* terrain,void* renderer,
    const NativeWorldLighting& light,const std::vector<NativeWorldBounds>& receivers,
    OwnedSceneSubmissionTestSnapshot& snapshot,
    const OwnedTerrainShadowTestInputs* material_inputs = nullptr) noexcept;
bool test_completed_visual_surface_contract() noexcept;
bool bridge_test_startup_logging() noexcept;
bool bridge_test_owned_visual_unit_classifier() noexcept;
bool bridge_test_owned_visual_root_rotation() noexcept;
bool bridge_test_owned_visual_producers(const std::vector<std::uint8_t>& resources,
    const std::vector<std::uint8_t>& generated_source,
    IDirectDrawSurface7* completed_surface, bool clip_reuse_only = false,
    bool neutral_only = false, bool quest_light_only = false) noexcept;
bool bridge_test_decode_tile_mask_record(const std::uint8_t* record,
                                         const std::uint8_t*& data,
                                         std::uint32_t& count) noexcept;
bool bridge_test_hud_atlas_hook_roundtrip() noexcept;
bool bridge_test_hud_visual_wrapper_semantics() noexcept;
bool bridge_test_character_pane_text_wrapper_semantics() noexcept;
bool bridge_test_outer_frame_timing_preflight_and_transaction() noexcept;
bool bridge_test_outer_frame_timing_frame_association() noexcept;
bool bridge_test_outer_frame_timing_profile_serial_domain() noexcept;
bool bridge_test_visual_event_recorder() noexcept;
bool bridge_test_scene_partition_arithmetic() noexcept;
bool bridge_test_scene_partition_active_chain() noexcept;
bool bridge_test_arm_character_pane_proxy_transaction(
    IDirectDrawSurface7* surface) noexcept;
bool bridge_test_draw_character_pane_proxy_text(HFONT font) noexcept;
bool bridge_test_publish_character_pane_proxy_transaction() noexcept;
bool bridge_test_character_pane_published_first_quad(
    RECT& destination) noexcept;
void bridge_test_disarm_character_pane_proxy_transaction() noexcept;
bool bridge_test_submit_hud_visual(
    const HudPublishedVisual& visual) noexcept;
bool bridge_test_submit_hud_visual_with_shift(
    const HudPublishedVisual& visual, float u_shift,
    float v_shift) noexcept;
bool bridge_test_hud_visual_remap(
    const HudPublishedVisual& visual, float u_shift, float v_shift,
    std::vector<NativeTextureCoordinate>& coordinates,
    std::array<std::uint32_t, 5>& revision) noexcept;
bool bridge_test_hud_interface_figure_selection(
    const HudPublishedVisual& visual, NativePackedGroup& first_position,
    std::array<float, 10>& first_bounds) noexcept;
bool bridge_test_arm_hud_atlas_addon_window(
    void* destination_texture, LONG atlas_width, const RECT& rect) noexcept;
void bridge_test_disarm_hud_atlas_window() noexcept;
std::uintptr_t bridge_test_active_wound_surface(
    const void* render_state_manager_slot, void* figure) noexcept;
const char* bridge_test_figure_extent_contract_violation(void* figure) noexcept;
const char* bridge_test_finalize_figure_post_render(
    void* figure, void* caller_rect, const RECT* extents, bool visible,
    std::uint32_t& figure_flags, int& render_result) noexcept;
std::uint32_t bridge_test_figure_extent_contract_exception_code() noexcept;
bool bridge_test_object_plan_mutation_retirement() noexcept;
bool bridge_test_object_plan_extent_publication(
    void* specific, const RECT* values, std::size_t count) noexcept;
bool bridge_test_object_visual_loop_hook_roundtrip() noexcept;
bool bridge_test_retained_source_hook_roundtrip() noexcept;
bool bridge_test_retained_scene_seam_roundtrip() noexcept;
bool bridge_test_retained_owned_visibility_contract() noexcept;
bool bridge_test_owned_shadow_covers_id(std::int32_t id) noexcept;
bool bridge_test_retained_frame_clip_composition() noexcept;
bool bridge_test_shared_world_lighting_policy() noexcept;
bool bridge_test_object_plan_semantic_scope() noexcept;
bool bridge_test_object_plan_diagnostic_tail_route() noexcept;
bool bridge_test_object_suffix_cut_contract() noexcept;
bool bridge_test_retained_empty_claim_fast_exit() noexcept;
bool bridge_test_owned_terrain_frame_lifetime() noexcept;
bool bridge_test_owned_sky_lifetime() noexcept;
bool bridge_test_owned_terrain_shadow_pass() noexcept;
bool bridge_test_owned_terrain_publication_continues_after_render_failure()
    noexcept;
bool bridge_test_owned_terrain_published_source_texture_capture() noexcept;
bool bridge_test_owned_terrain_light_completion() noexcept;
bool bridge_test_terrain_relight_hook_transaction() noexcept;
bool bridge_test_owned_terrain_gpu_variant_mapping() noexcept;
#endif
void bridge_set_native_terrain_animation_owned(bool enabled) noexcept;
void bridge_update_combat_portraits() noexcept;

#ifdef EID3D11_EMBEDDED
// An embedded host (EIRebuild) owns the game and drives the owned-visual
// world frame itself: it supplies the values the game.exe bridge reads from
// executable memory. Native sinks are installed with
// bridge_set_native_geometry_sink before the call.
struct RetainedTerrainSourceSnapshot;
struct OwnedTerrainFramePacket;
struct OwnedTerrainDynamicSector;
struct EmbeddedVisualFrameInputs {
    bool (*complete_visuals)(RetainedVisualFrame& frame) = nullptr;
    // Synchronous completed host terrain, owned by the caller for this draw.
    const RetainedTerrainSourceSnapshot* terrain_source = nullptr;
    const OwnedTerrainFramePacket* terrain_frame = nullptr;
    const std::vector<std::shared_ptr<const OwnedTerrainDynamicSector>>* terrain_dynamic = nullptr;
    std::array<float,16> terrain_to_world{};
    NativeOwnedFixedState terrain_material{};
    bool (*draw_terrain)(void* context) = nullptr;
    void* terrain_context = nullptr;
    double time = 0;                   // segment ticks + fraction
    std::uint32_t scene_flags = 0;     // client world render flags
    bool draw_undetected = true;
    bool briefing_screen = false;
    std::int32_t hover_id = 0;
    std::uint64_t hover_serial = 0;
    std::array<float, 16> world_to_clip{};
    float camera_anchor_z = NAN;
    NativeWorldLighting lighting{};
    bool lighting_valid = false;
    // Terrain fog alpha at a world position (0 fogged, 255 clear).
    BYTE (*fog)(void* context, const float* position) = nullptr;
    void* fog_context = nullptr;
};
struct EmbeddedVisualOmission {
    std::int32_t id = 0;
    const char* reason = nullptr; // Static reason label, valid for the process lifetime.
};
// Shadows (sun and point), point lights and the colour pass of every visual
// the retained scene claims, for the current world draw.
// drawn receives the IDs of the visuals this frame drew (colour submitted).
const EmbeddedVisualFrameInputs* bridge_embedded_visual_inputs() noexcept;
bool bridge_embedded_owned_visual_frame(const EmbeddedVisualFrameInputs& inputs,
                                        std::vector<std::int32_t>* drawn = nullptr,
                                        std::vector<EmbeddedVisualOmission>* omitted = nullptr) noexcept;
#endif

} // namespace owned
