#pragma once

// The bridge's world frames, shared by its two halves: the game half in
// engine_bridge.cpp (x86 only: hooks, game reads, the seam's captures) and
// the scene half (owned_world_prepare.cpp, owned_world_shadows.cpp,
// owned_world_consumers.cpp), which prepares and submits the world from the
// captured copies only and builds into the x64 host too. The frames are
// per-thread: the game thread's and the scene thread's are separate.

#include "engine_bridge.h"
#include "frame_accounting.h"
#include "ground_cover.h"
#include "owned_terrain_decode.h"
#include "owned_terrain_front_end.h"
#include "owned_resource_runtime.h"
#include "retained_static_scene.h"
#include "retained_static_submission.h"
#include "world_lamp_memory.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace owned {

struct NativeWorldLightingFrame {
    bool sampled = false;
    FigureFallbackReason result = FigureFallbackReason::unavailable;
    NativeWorldLighting lighting{};
    // The zone is a cave (the game's GMS_CAVE sky type), read with the light:
    // no sun light and no sun shadow maps there (cave_lighting.h).
    bool cave = false;
};
extern thread_local NativeWorldLightingFrame g_native_world_lighting_frame;

struct OwnedVisualPartPreparation {
    NativeWorldBounds bounds{};
    NativeWorldBounds light_bounds{};
    NativeContribution contribution{};
    NativeShadowCasterRevision depth_revision;
    // Per-frame values carry the serial of the frame that computed them: a
    // kept entity (OwnedAdmissionCache) never sees another frame's value.
    std::uint64_t ready_serial = 0; // bounds/contribution; a zero contribution is still a result
    std::uint64_t depth_serial = 0; // depth_revision
    bool visible = false;
    bool receives = false;
    bool casts = false;
    bool color_admitted = false; // Successful base admission only, for wounds.
};
struct OwnedVisualFramePartState {
    NativeWorldBounds bounds{},point_membership_bounds{};
    NativeContribution contribution{};
    // Exact successful part admission from this invocation's base submission.
    // Wounds must not widen it back to the entity envelope.
    std::vector<OwnedVisualPartPreparation> parts;
    bool claimed = false;
    bool submitted = false;
    bool static_reused = false;   // unchanged member of the persistent static sets
    std::size_t candidate_begin = 0;
    std::size_t candidate_end = 0;
    std::uint64_t casting_parts = 0;
};
struct OwnedVisualPartCandidate {
    std::size_t entity = 0;
    std::size_t part = 0;
};
struct OwnedVisualWorldFrame {
    OwnedVisualDiagnostic diagnostic{};
    float camera_anchor_z = NAN;  // this frame's game camera anchor height (close shadow band); NaN unknown
    NativeShadowSourceDiagnostics shadow_source_diagnostic{};
    std::shared_ptr<const RetainedVisualFrame> values;
    std::vector<OwnedVisualFramePartState> prepared;
    std::vector<RetainedShadowCasterValue> shadow_casters;
    std::vector<NativeWorldBounds> shadow_receivers;
    std::vector<NativeWorldBounds> static_receivers;   // per-frame scratch before cell merge
    std::vector<std::uint8_t> static_drawn;            // drawn by the persistent static set this frame
    std::vector<OwnedVisualPartCandidate> candidates;
    std::vector<std::pair<double,std::size_t>> color_order;
    std::vector<std::size_t> light_only_order;
    std::vector<std::size_t> submission_order;
    std::vector<const NativePreparedPartSubmission*> selected;
    std::vector<NativeDepthSelection> depth_selected;
    VisualResourceIncarnation observed_hover{};
    std::uint32_t observed_sun_selections=0;
    std::uint8_t observed_sun_mask=0,observed_retained_mask=0,observed_scroll_mask=0;
    std::vector<std::int32_t> shadow_time_active_visuals;
    NativeShadowFrame shadows;
    RetainedDirectionalCoverage coverage{};
    std::uint64_t serial = 0;
    std::uint64_t considered = 0, eligible = 0, submitted_parts = 0;
    std::uint64_t shadow_considered = 0, shadow_eligible = 0, shadow_submitted = 0;
    bool active = false;
    bool draw_undetected_units = false;
    bool shadows_enabled = true;
    bool shadow_attempted = false;
    bool shadow_ready = false;
    bool color_submitted = false;
    bool persistent_static_shadows = false;
    bool admission_incremental = false; // prepared by the persistent admission (OwnedAdmissionCache)
};
extern thread_local OwnedVisualWorldFrame g_owned_visual_world_frame;
// The per-entity states of the last frame, kept out of sight between frames:
// each holds its own parts vector, and destroying the frame freed and
// prepare_owned_visual_values reallocated one per entity on every frame
// (~3% of the main thread in a pan sample). Only prepare_owned_visual_values
// takes it back, and it resets every element it keeps.
extern thread_local std::vector<OwnedVisualFramePartState> g_owned_visual_prepared_spare;

// Persistent entity admission (issue #162). Index-parallel to the frame's
// entity list and to frame.prepared, owned by the thread that prepares the
// world (both are thread-local). An entity whose admission is unchanged keeps
// its frame.prepared state across frames and is not visited:
//  - reused: in the persistent static colour set (the static_skip path);
//  - admitted: a static object with admission and bounds whose contribution
//    was rejected and which is not registered; only its contribution is
//    tested again each frame.
// Validity is decided once per frame, never per pointer: the same admission
// settings, renderer static generation and neighbour fog revision, a
// journal read that covers every change since the cache's last frame
// (dirty_since <= through), an ID-ordered list, and the same lifecycle
// identity (id, generation, epoch, resource serial, mesh claim, readiness)
// at every index. Any journalled entity is admitted again. Anything else
// takes the full walk, which rebuilds the cache; a new receiver cell size
// only re-keys the kept receivers. Per-part values that are per frame (part bounds,
// contributions, depth revisions) are stamped with the frame serial, so a
// kept entity can never reuse an older frame's value.
// Receivers of kept entities are unioned per receiver cell (the same cells
// as append_cell_merged_receivers); a cell wholly inside or outside the
// camera (classify_receiver_cell, with a rounding margin) decides all its
// members at once, a straddling cell tests each member exactly.
struct OwnedAdmissionCache {
    enum class State : std::uint8_t { process, reused, admitted };
    struct Entry {
        LifecycleIdentity lifecycle{};
        std::uint64_t resource_serial=0;
        std::uint64_t cell=0;
        State state=State::process;
        bool claimed=false,in_cell=false,dirty=false;
    };
    struct Member { std::uint32_t index=0; NativeWorldBounds bounds{}; };
    struct Cell { std::vector<Member> members; NativeWorldBounds all{}; bool all_current=false; };
    bool valid=false;
    bool draw_undetected=false,shadows=false;
    std::uint32_t class_flags=0;
    std::uint64_t static_generation=0,border_fog_revision=0,through=0;
    float receiver_cell=0;
    std::vector<Entry> entries;
    std::map<std::uint64_t,Cell> cells;
    std::vector<std::uint32_t> admit,contribute;   // this frame, ascending
    std::vector<std::uint8_t> rejected;            // contribution rejected this frame, per index
    std::vector<std::pair<std::uint64_t,NativeWorldBounds>> cell_unions;
    void clear() noexcept {
        valid=false;entries.clear();cells.clear();admit.clear();contribute.clear();rejected.clear();cell_unions.clear();
    }
    void leave_cell(std::uint32_t index) {
        auto& entry=entries[index];
        if(!entry.in_cell)return;
        entry.in_cell=false;
        const auto found=cells.find(entry.cell);
        if(found==cells.end())return;
        auto& members=found->second.members;
        for(std::size_t m=0;m<members.size();++m)if(members[m].index==index) {
            members[m]=members.back();members.pop_back();break;
        }
        found->second.all_current=false;
        if(members.empty())cells.erase(found);
    }
    void join_cell(std::uint32_t index,const NativeWorldBounds& bounds) {
        auto& entry=entries[index];
        entry.cell=receiver_cell_key(receiver_cell,bounds);
        auto& cell=cells[entry.cell];
        cell.members.push_back({index,bounds});
        if(cell.all_current) {
            for(unsigned axis=0;axis<3;++axis) {
                cell.all.minimum[axis]=(std::min)(cell.all.minimum[axis],bounds.minimum[axis]);
                cell.all.maximum[axis]=(std::max)(cell.all.maximum[axis],bounds.maximum[axis]);
            }
        }
        entry.in_cell=true;
    }
};

struct OwnedTerrainRuntimeFrame {
    OwnedTerrainFramePacket front_end;
    RetainedTerrainSourceSnapshot source_values;
    std::vector<OwnedTerrainDynamicSector> dynamic;
    std::vector<std::uint8_t> publication_seen;
    // These are map-owned engine texture wrappers, copied only for the
    // uninterrupted outer World::Draw call. They are never dereferenced by
    // source state and are cleared before that call returns. Only the slots
    // of materials this world's source contains are captured; every other
    // slot stays null and is never read from the game's array.
    std::vector<void*> texture_handles;
    std::vector<std::uint8_t> shadow_texture_observed;
    NativeOwnedFixedState shadow_material;
    std::array<float,16> world_to_clip{};
    std::array<float,16> object_to_world{};
    bool world_transform_valid = false;
    std::uint64_t world_serial = 0;
    bool base_attempted = false;
    bool base_publication_ran = false;
    bool base_pending = false;
    void* base_renderer = nullptr; // synchronous borrow inside this outer World::Draw
};

extern thread_local OwnedTerrainRuntimeFrame g_owned_terrain_frame;
// The outer world draw's bracket of the terrain and visual world frames
// (owned_world_state.cpp): begin clears the terrain frame and opens a new
// world serial, end seals it; reset clears the visual world frame (its
// per-entity states go to the spare).
void begin_owned_terrain_world_frame() noexcept;
void end_owned_terrain_world_frame() noexcept;
void reset_owned_visual_world_frame() noexcept;

// The game's footprint list as the game thread read it for a queued world
// command (bridge_ground_cover_footprints' result there). The marker records
// (g_cover_markers) are game-thread state; the scene thread only copies this.
struct GroundCoverFootprintSnapshot {
    bool ok = false;
    std::vector<ground_cover::FootprintSplat> splats;
    std::vector<FootprintTrailPoint> trail_points;
};

// The seam's world preparation in two parts. The head reads game memory and
// copies the frame's authoritative inputs (game thread); the rest prepares
// shadows, values and submissions from that copy alone (the scene thread with
// the render_thread option, else straight after the head).
struct OwnedShadowInputs {
    NativeFrameState pass{};
    double time = 0;
    std::uint32_t flags = 0;
    bool draw_undetected = false;
    std::uint64_t shadow_source_revision = 0;
    std::shared_ptr<const RetainedVisualFrame> values;
};

// ---- Scene thread (render_thread option): the seam's world work ------------
// Game thread at the seam: the game reads of prepare_owned_world_shadows'
// head (time, flags, object poses, world light, highlight, the retained
// visual frame), the game's fog function for every entity, the terrain frame
// with the atlases the game binds, and the device state the sinks read. The
// scene thread then prepares shadows, values, terrain and colour submissions
// from that copy only. Simulation visibility, picking and selection never wait
// for or read these results (execute_owned_object_interaction keeps computing
// them from game state and the copied values).
struct OwnedSceneCommand {
    std::array<float,16> world_to_clip{};
    float camera_anchor_z = NAN;  // the game camera's anchor height at the seam; NaN unknown
    OwnedShadowInputs inputs;
    OwnedTerrainRuntimeFrame terrain;
    NativeWorldLightingFrame lighting{};
    std::vector<std::int16_t> fog;
    std::uint64_t fog_revision = 0;   // neighbour_border_fog_revision() read before fog was computed
    std::vector<std::shared_ptr<SurfaceImage>> terrain_textures;
    std::vector<std::shared_ptr<const OwnedVisualSurface>> terrain_shadow_textures;
    GroundCoverFootprintSnapshot footprints;
    std::shared_ptr<void> sinks;
    BridgeSceneHandoff handoff;
};

// ---- Game-half types the scene half uses ----------------------------------
// Temporary startup diagnostics stages (startup_begin/end/failure).
enum class StartupStage : unsigned {
    hooks, source_begin, source_native, source_complete, tick, world,
    terrain_capture, terrain_publish, scene_binding, scene_native, object_native,
    appearance_native, appearance, retained_seam, visual_sample, shadow, resource_native, resource_owned,
    terrain_visibility, terrain_commands, terrain_textures, terrain_renderer, shadow_prepare,
    count
};

struct BridgeFrameSample {
    OwnedVisualDiagnostic owned_visual{};
    struct ApiGap {
        std::int64_t ticks = 0;
        const char* from_kind = "none";
        const char* to_kind = "none";
        std::uint32_t from_rva = 0;
        std::uint32_t to_rva = 0;
    };
    struct SemanticGap {
        std::int64_t ticks = 0;
        const char* from_kind = "none";
        const char* to_kind = "none";
        std::uint32_t from_outer_rva = 0;
        std::uint32_t to_outer_rva = 0;
    };
    struct SimpleCaller {
        std::uint32_t rva = 0;
        std::uint64_t calls = 0;
        std::uint64_t vertices = 0;
        std::uint64_t indices = 0;
    };
    std::uint64_t frame = 0;
    std::int64_t frame_begin = 0;
    std::int64_t begin_interval_ticks = 0;
    std::int64_t simple_ticks = 0;
    std::int64_t figure_placed_ticks = 0;
    std::int64_t figure_lit_ticks = 0;
    std::int64_t figure_lit_morphed_ticks = 0;
    std::int64_t figure_lit_rigid_ticks = 0;
    std::int64_t world_draw_ticks = 0;
    std::int64_t world_named_ticks = 0;
    std::int64_t scene_3d_ticks = 0;
    std::int64_t scene_3d_named_ticks = 0;
    std::int64_t scene_partition_retained_nonqueue_ticks = 0;
    std::array<std::int64_t,12> city_retained_owner_ticks{};
    std::int64_t scene_partition_object_nonqueue_ticks = 0;
    std::int64_t scene_partition_queue_native_flush_ticks = 0;
    std::int64_t scene_partition_queue_ui_flush_ticks = 0;
    std::int64_t scene_partition_queue_packet_or_control_ticks = 0;
    std::int64_t scene_partition_remaining_game_ticks = 0;
    std::int64_t object_draw_ticks = 0;
    std::int64_t object_draw_named_ticks = 0;
    std::int64_t object_plan_bootstrap_ticks = 0;
    std::int64_t object_plan_submit_ticks = 0;
    std::int64_t terrain_ticks = 0;
    std::int64_t terrain_outer_ticks = 0;
    std::int64_t terrain_capture_ticks = 0;
    std::int64_t terrain_submit_ticks = 0;
    std::uint64_t simple_calls = 0;
    std::uint64_t figure_placed_calls = 0;
    std::uint64_t figure_lit_calls = 0;
    std::uint64_t figure_lit_morphed_calls = 0;
    std::uint64_t figure_lit_rigid_calls = 0;
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
    std::uint64_t figure_gpu_owned_calls = 0;
    std::uint64_t figure_cpu_fallback_calls = 0;
    std::uint64_t figure_gpu_morph_vertices = 0;
    std::uint64_t figure_gpu_submit_failures = 0;
    std::uint64_t figure_gpu_culled_calls = 0;
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
    std::uint64_t figure_alpha_zero_calls = 0;
    std::uint64_t figure_alpha_partial_calls = 0;
    std::uint64_t figure_alpha_opaque_calls = 0;
    std::uint64_t figure_morphed_calls = 0;
    std::uint64_t figure_texture0_calls = 0;
    std::uint64_t figure_texture1_calls = 0;
    std::uint64_t figure_multitexture_calls = 0;
    std::uint64_t figure_single_submit_failures = 0;
    std::uint64_t figure_multitexture_submit_failures = 0;
    std::uint64_t simple_vertices = 0;
    std::uint64_t simple_indices = 0;
    std::array<ApiGap, 8> largest_api_gaps{};
    std::array<SemanticGap, 8> largest_semantic_gaps{};
    std::array<SimpleCaller, 16> simple_callers{};
    bool scene_partition_valid = true;
    bool active = false;
};

struct OwnedShadowSettings {
    std::uint32_t map_side = 2048;
    std::uint8_t close_cascades = 3;
    bool far_shadows = true;
    bool point_shadows = true;
    float close_range = 80;  // close shadow band length (m), shadow_close_range
};

// These subdivide retained nonqueue; physical flushes and packet/control work
// remain in their existing exclusive leaves.
enum class CityRetainedOwner : std::size_t {
    receiver_fitting, caster_values, depth_registration, nonunit_pose,
    visual_frame_values, shadow_input_decision, domain_snapshot, color_submission, point_shadow, terrain_base, lighting_publish
};

// Defined in engine_bridge.cpp (the scene partition timing it records into);
// the x64 host answers it in Host/host_bridge.cpp (frame accounting only).
struct CityRetainedOwnerGuard {
    explicit CityRetainedOwnerGuard(CityRetainedOwner owner,std::int64_t now=0) noexcept;
    void finish(std::int64_t now=0) noexcept;
    ~CityRetainedOwnerGuard(){finish();}
    CityRetainedOwnerGuard(const CityRetainedOwnerGuard&)=delete;
    CityRetainedOwnerGuard& operator=(const CityRetainedOwnerGuard&)=delete;
    std::size_t index;
    std::int64_t begin=0,excluded=0;
    bool active=false;
    bool accounted=false;
};

constexpr std::size_t kMergedPartBase = 0x10000;   // candidate part index of merged group 0

// ---- Defined in engine_bridge.cpp, used by the scene half -----------------
// Game-side answers: bridge.jsonl, the bridge profiler, the startup trace,
// the hovered target, the hook state, and the two leaves that still answer
// from the game when no captured input is installed (render_thread 0). The
// x64 host answers these in Host/host_bridge.cpp.
void append_bridge_event(const char* event, const char* detail,
                         bool sites_verified, const char* stage = nullptr,
                         const char* edge = nullptr, unsigned sequence = 0) noexcept;
bool bridge_profile_enabled();
VisualResourceIncarnation hovered_shadow_observation_target() noexcept;
bool retained_world_fog(void* controller,
                        const std::array<float, 3>& position,
                        BYTE& fog) noexcept;
bool startup_begin(StartupStage stage) noexcept;
void startup_end(StartupStage stage, bool traced, const char* detail) noexcept;
bool startup_failure(StartupStage stage, const char* detail) noexcept;
bool visual_texture_surface(void* texture, IDirectDrawSurface7*& surface,
                            std::uint32_t& flags) noexcept;
// The scene command's installed inputs when present, else the game.
FigureFallbackReason publish_native_world_lighting(void* renderer) noexcept;
bool bind_owned_terrain_texture(void* renderer,std::size_t index,void* wrapper);
extern bool g_visual_publication_active;
extern CompletedVisualSurfaceReader g_visual_surface_reader;

// ---- Defined in owned_world_state.cpp, used by both halves ----------------
// Settings, the native sinks, the scene command's installed inputs, failure
// reporting and the base terrain submission. Game-free.
bool apply_owned_terrain_world_transform(NativeGeometryView& view) noexcept;
std::int64_t bridge_now();
std::int64_t bridge_frequency();
void clear_owned_visual_world_frame(OwnedVisualWorldFrame& frame) noexcept;
// The frame's per-frame scratch vectors: a frame reset clears them (their
// elements are destroyed) but keeps their capacity, so each frame does not
// reallocate them. bridge_release_world_caches frees the empty ones.
template<class Visit> void visit_owned_visual_frame_scratch(OwnedVisualWorldFrame& frame,Visit&& visit) {
    visit(frame.shadow_casters);visit(frame.shadow_receivers);visit(frame.static_receivers);visit(frame.static_drawn);
    visit(frame.candidates);visit(frame.color_order);visit(frame.light_only_order);visit(frame.submission_order);
    visit(frame.selected);visit(frame.depth_selected);visit(frame.shadow_time_active_visuals);
}
std::uint64_t current_border_fog_revision() noexcept;
bool hovered_shadow_observation_enabled() noexcept;
bool load_reveal_held() noexcept;
void log_owned_visual_failure(const OwnedVisualRejection& rejection) noexcept;
bool owned_merged_member(const RetainedVisualSample& sample,std::size_t part) noexcept;
std::uint64_t owned_scene_ack_revision(std::uint64_t revision) noexcept;
bool owned_unit_merged(const RetainedVisualSample& sample);
bool reject_owned_shadow_part(const char* stage,const NativePreparedIdentity& identity,
    const NativeGeometryResult* result=nullptr,const NativeOwnedTextureView* texture=nullptr) noexcept;
void reject_owned_visual(const char* stage, const char* reason,
    const RetainedVisualSample* sample = nullptr, bool failure = true, std::uint32_t class_flags = 0) noexcept;
void report_object_plan_failure(const char* stage) noexcept;
void report_world_point_light_omission(const char* reason) noexcept;
bool same_lifecycle_identity(const LifecycleIdentity& left,
                             const LifecycleIdentity& right) noexcept;
bool source_terrain_world_record(const RetainedTerrainSourceSnapshot& source,
    OwnedTerrainFramePacket& packet, const OwnedTerrainCommand& command,
    const OwnedTerrainDynamicSector* color, const NativeContribution& contribution,
    OwnedTerrainSourceDecodeContext& context, NativeGeometryView*& output,
    bool alpha_coverage=false);
bool submit_owned_visual_records(const RetainedVisualSample& sample,
    const NativePreparedPartSubmission* const* parts,std::uint32_t count,
    const std::array<float,16>& clip,std::uint64_t serial,std::uintptr_t wound,
    NativeGeometryResult& result,bool retained_color_diagnostic = false);
bool submit_pending_owned_terrain_base() noexcept;
bool submit_source_owned_terrain_commands(
    void* renderer, const std::vector<OwnedTerrainCommand>& commands,
    const NativeOwnedFixedState* captured_state = nullptr);
bool owned_terrain_static_op(NativeStaticSubmission& request,const char** failure=nullptr);
void report_owned_terrain_set_failure(const char* stage,std::uint32_t sector,std::uint32_t material,const char* detail);
void report_owned_terrain_omission(OwnedTerrainRangeAction action) noexcept;
extern thread_local const char* g_owned_terrain_failure_stage;
extern thread_local HRESULT g_owned_terrain_failure_hresult;
extern thread_local BridgeFrameSample g_bridge_sample;
extern std::atomic<bool> g_load_reveal_enabled;
extern std::atomic<std::uint64_t> g_load_reveal_revealed_epoch;
extern bool g_merge_character_parts;
extern thread_local NativeGeometryBatchBeginFn g_native_sink_begin_batch;
extern thread_local void* g_native_sink_context;
extern thread_local NativeGeometryBatchEndFn g_native_sink_end_batch;
extern thread_local NativeGeometryObjectBeginFn g_native_sink_begin_object;
extern thread_local NativeGeometryObjectEndFn g_native_sink_end_object;
extern thread_local NativeGeometryFrameStateFn g_native_sink_frame_state;
extern thread_local NativeWorldLightingSetFn g_native_sink_world_lighting;
extern thread_local NativePointLightsSetFn g_native_sink_point_lights;
extern thread_local NativePointShadowAllocateFn g_native_sink_point_shadow_allocate;
extern thread_local NativePointShadowBatchFn g_native_sink_point_shadow_batch;
extern thread_local NativePointShadowBeginFn g_native_sink_point_shadow_begin;
extern thread_local NativePointShadowEndFn g_native_sink_point_shadow_end;
extern thread_local NativePointShadowPublishFn g_native_sink_point_shadow_publish;
extern thread_local NativePointShadowReleaseFn g_native_sink_point_shadow_release;
extern thread_local NativeRainOcclusionBeginFn g_native_sink_rain_begin;
extern thread_local NativeRainOcclusionEndFn g_native_sink_rain_end;
extern thread_local NativeShadowBeginFn g_native_sink_shadow_begin;
extern thread_local NativeShadowCullProjectionFn g_native_sink_shadow_cull_projection;
extern thread_local NativeShadowSourceDiagnosticsFn g_native_sink_shadow_diagnostics;
extern thread_local NativeShadowEndFn g_native_sink_shadow_end;
extern thread_local NativeShadowStaticDepthFn g_native_sink_shadow_static_depth;
extern thread_local NativeShadowTerrainBakeFn g_native_sink_shadow_terrain_bake;
extern thread_local NativeGeometrySubmitFn g_native_sink_submit;
extern thread_local std::uint64_t g_object_render_presentation_serial;
extern OwnedShadowSettings g_owned_shadow_settings;
void report_owned_shadow_pass_failure(const char* reason) noexcept;
extern int g_point_shadow_fast_overlays;
extern float g_point_shadow_merge_distance;
extern RainOcclusionRequest g_rain_request;
extern thread_local std::uint64_t g_scene_capture_revision;
extern thread_local std::uint64_t g_scene_fog_revision;
extern thread_local const std::vector<std::int16_t>* g_scene_fog_table;
extern thread_local const GroundCoverFootprintSnapshot* g_scene_footprints;
bool scene_ground_cover_footprints(std::vector<ground_cover::FootprintSplat>& output,
    std::vector<FootprintTrailPoint>* trail_points) noexcept; // owned_world_state.cpp
extern thread_local bool g_scene_frame_values_installed;
extern thread_local void* g_scene_sinks;
extern thread_local void (*g_scene_bind_texture)(void*, const std::shared_ptr<SurfaceImage>&);
extern thread_local const std::vector<std::shared_ptr<const OwnedVisualSurface>>* g_scene_terrain_shadow_textures;
extern thread_local const std::vector<std::shared_ptr<SurfaceImage>>* g_scene_terrain_textures;
extern WorldLampMemory<RetainedWorldPointLight> g_world_lamp_memory;
bool current_base_terrain_quality_reference(const TerrainCpuAsset*& source,
    const std::array<float,16>*& transform,std::uint64_t& generation) noexcept;
void* owned_terrain_texture_wrapper(std::size_t material) noexcept;

// ---- Defined in the scene half, used by engine_bridge.cpp -----------------
bool submit_owned_visual_color(void* fog_controller, bool prepare_light_only, bool static_set);
bool finish_owned_visual_scene(void* fog_controller,const std::array<float,16>& world_to_clip);
void run_owned_visual_scene(OwnedSceneCommand& command) noexcept;
// An object preview (character panel, inventory doll; owned_world_state.cpp).
struct OwnedPreviewPart {
    std::size_t ordinal = 0;
    std::size_t set = 0;              // index into the captured state sets
    NativeOwnedFixedState material{};
    bool uses_default_colors = false;
    std::uint32_t default_diffuse = 0, default_specular = 0;
    NativeLighting lighting{};
    std::uint32_t material_flags = 0;
    std::uintptr_t wound = 0;
    std::shared_ptr<SurfaceImage> wound_texture;
};
// Owned party portrait expression: the texels of the expression face picture
// that differ from the neutral one, laid out in the unit atlas's UV space
// (the face pictures share the layout and scale of the atlas's head block,
// at (0, 28/256) and half the atlas side). Drawn over the head part as a
// second record of the same mesh and pose (owned_world_state.cpp).
struct OwnedPortraitOverlay {
    std::shared_ptr<const TextureCpuAsset> texture;
    std::array<NativeOwnedTextureMip,32> mips{};
    NativePreparedMaterialHandle material{}, wound_material{};
};
std::shared_ptr<OwnedPortraitOverlay> build_owned_portrait_overlay(const TextureCpuAsset& neutral,
    const TextureCpuAsset& expression, const char*& reason) noexcept;
// The expression picture of a neutral face picture name (ChangeFaceTexture
// 0x640670: 1 happy +"a", 2 pain +"c", 3 tired +"b"); empty for neutral.
std::string owned_portrait_expression_name(const std::string& neutral, std::int32_t expression);
// Bare-head portrait (portrait_helmet off): a copy of the unit's assembly in
// which each helmet head/hair part (hd.armorTT, hr.HH.armorTT) is replaced in
// place by its bare part (hd, hr.HH) from the unit's model archive. Ordinals
// stay those of the unit's assembly.
std::shared_ptr<const VisualAssemblyAsset> build_owned_portrait_bare_assembly(
    const VisualAssemblyAsset& unit, const ResourceBlob& model, const char*& reason) noexcept;
struct OwnedPreviewCommand {
    RetainedVisualSample sample;
    double texture_time = 0;
    BYTE fog = 255;
    std::array<float,16> base_world{}, world_to_clip{};
    std::uint64_t presentation_serial = 0;
    std::vector<std::shared_ptr<void>> sets;
    std::vector<OwnedPreviewPart> parts;
    BridgeSceneHandoff handoff;
    std::shared_ptr<OwnedPortraitOverlay> portrait_overlay;   // portraits only
    bool portrait = false;   // a party portrait: its records keep HUD submission order
};
void run_owned_preview_command(OwnedPreviewCommand& command) noexcept;
// One object's wound layer (owned_world_state.cpp): the parts and wound
// surface interface a game-thread request names.
struct OwnedWoundRequest {
    std::size_t sample = 0, part = 0;
    BYTE fog = 255;
    std::uintptr_t wound = 0;
};
void run_owned_wound_layer(const std::vector<OwnedWoundRequest>& requests,
    const std::vector<std::shared_ptr<SurfaceImage>>& wounds,void* sinks,const BridgeSceneHandoff& handoff,
    bool same_world) noexcept;
// Scene thread: install a queued command's inputs (world, preview, HUD figure); clear afterwards.
void install_owned_scene_inputs(void* sinks,const BridgeSceneHandoff& handoff,
    const std::vector<std::shared_ptr<SurfaceImage>>* textures,
    const std::vector<std::shared_ptr<const OwnedVisualSurface>>* shadow_textures,
    const std::vector<std::int16_t>* fog,std::uint64_t revision) noexcept;
void clear_owned_scene_inputs(const BridgeSceneHandoff& handoff) noexcept;
extern thread_local OwnedAdmissionCache g_owned_admission_cache;
bool prepare_owned_world_shadows_from(OwnedShadowInputs in,
    const std::array<float,16>& world_to_clip);
void retire_owned_shadow_maps() noexcept;
bool capture_owned_terrain_shadow_material() noexcept;
std::uint64_t snow_trail_key(const VisualResourceIncarnation& resource) noexcept;
float snow_trail_extent(const std::array<float,2>& half) noexcept;
void release_owned_terrain_shadow_caches() noexcept;
void release_critter_perch_models() noexcept;

// ---- Shared between the scene-half files ----------------------------------
bool owned_point_shadow_influence(const NativeWorldBounds& bounds,
    const RetainedWorldPointLight& light) noexcept;
bool owned_point_shadow_influence(const NativeWorldBounds& bounds,
    const std::vector<RetainedWorldPointLight>& lights) noexcept;
VisualPreparedResources::PoseKey owned_visual_pose_key(const RetainedVisualSample& sample) noexcept;
bool owned_visual_pose_key_matches(const VisualPreparedResources::PoseKey& left,
    const RetainedVisualSample& sample,const double* phase) noexcept;
bool prepare_owned_visual_admission(const RetainedVisualSample& sample,
    bool draw_undetected,bool shadows_enabled);
bool prepare_owned_visual_part(std::size_t entity,std::size_t part,
    const OwnedVisualPartPreparation*& output);
bool prepare_owned_visual_pose(const RetainedVisualSample& sample);
bool prepare_owned_visual_values(std::shared_ptr<const RetainedVisualFrame> values,
    const std::array<float,16>& world_to_clip, const NativeWorldLighting& lighting,
    bool draw_undetected, std::uint32_t class_flags,
    const std::vector<NativeWorldBounds>& terrain_receivers,bool shadows_enabled=true,
    const RetainedDirectionalCoverage* reused_coverage=nullptr,
    const std::vector<std::int32_t>* static_dirty=nullptr,
    std::uint64_t dirty_since=0,std::uint64_t dirty_through=0);
bool prepare_owned_visual_world_bounds(const RetainedVisualSample& sample,
    NativeWorldBounds& bounds);
bool same_visual_resource(const VisualResourceIncarnation& left,
    const VisualResourceIncarnation& right) noexcept;
bool owned_visual_part_casts(const RetainedVisualSample& sample,const VisualPartAsset& part,
    bool draw_undetected) noexcept;

} // namespace owned
