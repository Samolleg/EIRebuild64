#pragma once

#include "retained_static_scene.h"

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace owned {

struct RetainedPreparedNode {
    std::shared_ptr<const environment::Material> environment_material;
    const FigureCpuAsset* figure = nullptr;
    const TextureCpuAsset* texture = nullptr;
    // Item 5 live-sourced models: the part's game surface instead of an
    // archive texture (its pixels live only in the renderer's surface cache).
    std::shared_ptr<const OwnedVisualSurface> surface;
    // A surface part's cut-out class from the archive texture of the same
    // name (its top level's opaque_mips), as the neighbour path classifies
    // the model: 1 cut-out, 0 opaque, -1 no archive texture found.
    std::int8_t archive_cut_out = -1;
    // The game texture's name (surface parts), for that lookup.
    std::string texture_name;
    std::vector<NativePackedGroup> positions;
    std::vector<NativePackedGroup> normals;
    std::vector<NativeOwnedTextureMip> texture_mips;
    NativeOwnedTextureFormat texture_format = NativeOwnedTextureFormat::dxt1;
    // Immutable model-node placement below the authoritative object transform.
    // Runtime visibility can therefore consume a compact current object
    // position/quaternion without rebuilding shared FIG geometry.
    std::array<float, 16> object_local{};
    std::array<float, 16> object_to_world{};
    FigureBounds bounds{};
    std::uintptr_t geometry_identity = 0;
    std::uint64_t texture_identity = 0;
    std::array<std::uint32_t, 5> geometry_revision{};
};

struct RetainedStaticSourceCandidate {
    std::uint32_t nid = 0;
    std::uint32_t family = 0;
    std::int32_t signed_id = 0;
    std::shared_ptr<const OwnedMapSnapshot> snapshot;
    const StaticObjectSource* source = nullptr;
    std::array<float, 3> world_position{};
    // The model's root node in the same space as world_position (valid when
    // the model has exactly one root): the sway anchor of an object-space
    // record, which has no pose node to read it from.
    std::array<float, 3> root_world{};
    bool root_valid = false;
    std::vector<RetainedPreparedNode> nodes;
    // References the same immutable model/FIG/BON/MMP values for both passes.
    std::shared_ptr<const VisualAssemblyAsset> assembly;
    RetainedStaticPrepareDiagnostic preparation{};
    bool prepared = false;
    // False: some drawn part has no archive texture (prebuild_decorations
    // then plans no impostor for it; item 5 live-sourced models).
    bool impostor_inputs_complete = true;
};

struct RetainedPreparedInstance {
    LifecycleIdentity lifecycle{};
    std::shared_ptr<const RetainedStaticSourceCandidate> candidate;
};

const char* retained_static_prepare_result_name(
    RetainedStaticPrepareResult result) noexcept;

// Source-family eligibility is fixed at publication. A candidate that passes
// this ownership boundary remains claimed even if its later immutable geometry
// preparation fails; that failure is isolated to the one visible instance.
bool retained_static_source_eligible(
    const StaticObjectSource& source,
    const OwnedMapSnapshot& snapshot) noexcept;

// Family-independent structural check of one static object's resolved model,
// drawn nodes, FIG8 data and textures: nullptr when it can be prepared,
// otherwise a short reason for diagnostics.
const char* retained_static_structure_omission(
    const StaticObjectSource& source,
    const OwnedMapSnapshot& snapshot) noexcept;

// True when the object draws model node `index`: its MOB part list names it,
// or the list is empty (every node). Unselected nodes still carry their bone
// offset to drawn descendants.
bool retained_static_node_selected(const StaticObjectSource& source,
    const ModelCpuAsset& model, std::size_t index) noexcept;

// Compiles one immutable, capability-validated FIG8/BON/MMP source candidate
// into the exact static endpoint selected by MOB complection. This is called
// only while publishing a source family; lifecycle instances share the result.
bool prepare_retained_static_candidate(
    std::shared_ptr<const OwnedMapSnapshot> snapshot,
    std::shared_ptr<const TerrainCpuAsset> terrain,
    const StaticObjectSource& source,
    std::shared_ptr<RetainedStaticSourceCandidate>& prepared,
    RetainedStaticPrepareDiagnostic* diagnostic = nullptr,
    std::size_t source_index = kRetainedStaticPrepareNoIndex) noexcept;

// Builds lightweight lifecycle views over already-prepared source candidates.
// Retained runtime submission performs direct per-ID lookup and does not call
// this whole-scene inspection helper per frame.
bool prepare_retained_static_scene(
    const RetainedStaticSceneSnapshot& source,
    std::vector<RetainedPreparedInstance>& prepared,
    RetainedStaticPrepareDiagnostic* diagnostic = nullptr) noexcept;

std::array<float, 16> retained_multiply_matrix(
    const std::array<float, 16>& left,
    const std::array<float, 16>& right) noexcept;
std::array<float, 16> retained_transpose_matrix(
    const std::array<float, 16>& source) noexcept;
bool retained_object_matrix(
    const std::array<float, 3>& position,
    const std::array<float, 4>& quaternion,
    std::array<float, 16>& matrix) noexcept;
bool retained_bounds_visible(
    const FigureBounds& bounds,
    const std::array<float, 16>& object_to_clip) noexcept;
// Cold interaction bounds use the same FIG endpoint evaluation as retained
// geometry. Call only when the immutable resource or complection changes.
bool retained_figure_position_bounds(const FigureCpuAsset& figure,
    const std::array<float, 3>& shape, NativeWorldBounds& bounds) noexcept;
const std::array<float,3>& retained_visual_shape(const RetainedVisualSample& sample) noexcept;

bool retained_visual_world_bounds(const RetainedVisualSample& sample,
                                   NativeWorldBounds& bounds) noexcept;
// True only when every corner is inside every clip plane (then every box
// inside these bounds is visible too).
bool retained_world_bounds_inside(const NativeWorldBounds& bounds,
    const std::array<float, 16>& world_to_clip) noexcept;
bool retained_world_bounds_visible(const NativeWorldBounds& bounds,
    const std::array<float, 16>& world_to_clip) noexcept;
NativeWorldLighting shadow_projection_lighting(const NativeWorldLighting& lighting) noexcept;
// Unions static receiver boxes per square world cell (terrain-sector sized),
// appending one box per occupied cell in stable cell order. Coverage clips every
// receiver against the camera and each map, so the count is what it pays for.
bool append_cell_merged_receivers(float cell,const std::vector<NativeWorldBounds>& statics,
    std::vector<NativeWorldBounds>& receivers);
// The cell key of a receiver box (by its centre) in that order.
std::uint64_t receiver_cell_key(float cell,const NativeWorldBounds& bounds) noexcept;
// The same, with some cells' unions already formed: `cells` is (key, union)
// in ascending key order, unique keys; each is joined with the keyed statics.
bool append_cell_merged_receivers(float cell,const std::vector<NativeWorldBounds>& statics,
    const std::vector<std::pair<std::uint64_t,NativeWorldBounds>>& cells,
    std::vector<NativeWorldBounds>& receivers);
// Camera class of a box with a float rounding margin: every box inside an
// `inside` box passes retained_world_bounds_visible, none inside an
// `outside` box does; `partial` decides nothing.
enum class ReceiverCellClass : std::uint8_t { outside, inside, partial };
ReceiverCellClass classify_receiver_cell(const NativeWorldBounds& bounds,
    const std::array<float,16>& world_to_clip) noexcept;

bool build_retained_directional_coverage(
    const std::array<float, 16>& world_to_clip,
    const NativeWorldLighting& lighting,
    const std::vector<NativeWorldBounds>& receiving_bounds,
    RetainedDirectionalCoverage& result,
    const TerrainCpuAsset* base_terrain = nullptr,
    const std::array<float, 16>* terrain_to_world = nullptr,
    std::uint32_t map_side = 2048,
    std::uint8_t close_cascades = 3,
    std::uint64_t base_terrain_generation = 0, // base_terrain's source generation
    float close_range = 80,                    // close band length (m) past its near start
    float camera_anchor_z = NAN) noexcept;     // the game camera's anchor height; NaN unknown
NativeContribution retained_world_contribution(const NativeWorldBounds& bounds,
    bool casts_shadow, const RetainedDirectionalCoverage& coverage,
    NativeWorldBounds* caster_in_light = nullptr,
    float minimum_projected_texels = 0) noexcept;
// Slice mask from an already projected light-space caster box. Valid for the
// coverage's light axes only; a sun-direction change requires reprojection.
std::uint8_t retained_light_mask(const NativeWorldBounds& caster_in_light,
    const RetainedDirectionalCoverage& coverage,float minimum_projected_texels=0) noexcept;
std::uint8_t retained_world_light_contribution(const NativeWorldBounds& bounds,
    bool casts_shadow, const RetainedDirectionalCoverage& coverage,
    NativeWorldBounds& caster_in_light,
    float minimum_projected_texels = 0) noexcept;
// Persistent static casters, summarised for cascade fitting: one revision
// entry per slice drives the redraw decision, and a light-space grid of the
// lowest caster depth extends each cascade's depth range upstream.
struct StaticShadowSummary {
    std::uint64_t revision = 0;
    // Store revision each map last saw a change inside its square (L-081);
    // zero falls back to the whole-store revision.
    std::array<std::uint64_t,5> slice_revision{};
    std::array<std::array<float,3>,3> axes{};
    std::array<float,2> origin{};
    float cell = 0;
    unsigned side = 0;
    std::vector<float> minimum_z;   // side*side cells, +inf where empty
    // Each caster's bounds along the light axes, in caster order (invalid when
    // not finite): selects the casters of a scrolled map's exposed strip.
    std::vector<NativeWorldBounds> light;
    bool valid = false;
};
bool build_static_shadow_summary(const std::vector<RetainedShadowCasterValue>& casters,
    const std::array<std::array<float,3>,3>& axes,std::uint64_t revision,
    StaticShadowSummary& summary);

bool build_retained_shadow_frame(const RetainedDirectionalCoverage& coverage,
    const std::vector<RetainedShadowCasterValue>& casters,
    NativeShadowFrame& frame,
    const struct StaticShadowSummary* statics = nullptr) noexcept;
// Consume the current tuple established by retained_visual_gpu_pose. This
// changes no gameplay/pose state and performs no geometry or texture preparation.
bool retained_visual_depth_revision(const RetainedVisualSample& sample,
    std::size_t part_index, double time, NativeShadowCasterRevision& revision) noexcept;
std::uint64_t next_retained_shadow_revision() noexcept;

// Whether the game currently shows this sample to the player: ready, not
// hidden, and for units the visible-list detection bit (0x00400000) unless the
// game's own setting draws undetected units. The one rule for drawing a unit
// and for every render effect it causes (ground-cover pressing, corpses).
inline bool retained_visual_sample_shown(const RetainedVisualSample& sample,
    bool draw_undetected_units) noexcept {
    if (sample.status != RetainedVisualStatus::ready || sample.values.hidden ||
        (sample.values.object_flags & 0x40u)) return false;
    // The packet's detection bit is distinct from the camera-written OBJ_VISIBLE.
    return !sample.values.unit || draw_undetected_units ||
        (sample.values.object_flags & 0x00400000u) != 0;
}
bool retained_visual_part_visible(const RetainedVisualSample& sample,
    const VisualPartAsset& part, bool draw_undetected_units) noexcept;
float retained_visual_effective_alpha(const RetainedVisualSample& sample,
    const VisualPartAsset& part) noexcept;
bool retained_visual_texture_shift(std::uint32_t type, double time,
    float& u, float& v) noexcept;
// CPU pose/view oracles are available to focused tests only. Production color,
// depth and wound consumers share the GPU binding and current world generation.
#ifdef EID3D11_TESTING
void reset_retained_shadow_receiver_clip_count_for_test() noexcept;
std::uint64_t retained_shadow_receiver_clip_count_for_test() noexcept;
void reset_retained_shadow_projection_count_for_test() noexcept;
std::uint64_t retained_shadow_projection_count_for_test() noexcept;
bool retained_visual_nodes(const RetainedVisualSample& sample,
    std::vector<VisualNodeSample>& nodes);
bool retained_visual_current_nodes(const RetainedVisualSample& sample,
    std::shared_ptr<const std::vector<VisualNodeSample>>& nodes);
#endif
bool retained_visual_gpu_pose(const RetainedVisualSample& sample,
    NativeWorldGeometry::Pose*& pose);
// 0: legacy visual, 1: world point, -1: selected glTF rejected.
int retained_visual_equipment_point(const RetainedVisualSample& sample, const char* slot,
    const char* anchor, std::array<float,3>& point) noexcept;
bool retained_visual_combat_anchor(const RetainedVisualSample& sample,
    std::array<float,3>& anchor);
// Owned party portrait: the box of the listed parts in the neutral pose, in
// the pose root's space (the space the portrait's root_to_world places).
bool retained_visual_neutral_parts_box(const RetainedVisualSample& sample,
    const std::vector<std::size_t>& parts,std::array<float,3>& minimum,std::array<float,3>& maximum);
bool retained_visual_part_bounds(const RetainedVisualSample& sample,
    std::size_t part_index,NativeWorldBounds& bounds);
bool retained_visual_part_submission(const RetainedVisualSample& sample,
    std::size_t part_index,const NativeWorldBounds& bounds,NativeContribution contribution,
    double time,std::uint8_t fog,NativePreparedPartSubmission*& record);
bool retained_visual_preserve_color_order(const RetainedVisualSample& sample,
    const VisualPartAsset& part) noexcept;
// Foliage evidence of one part, from the texels its triangles sample, and the
// positions of its cut-out-free (bark) triangles. Cached per figure + texture
// identity. Bark, cactus and mushroom parts have no foliage.
struct PartSwayEvidence { bool foliage = false; std::vector<std::uint16_t> clear_positions; };
PartSwayEvidence retained_visual_figure_sway(const FigureCpuAsset& figure,
    const TextureAlphaPixels& alpha) noexcept;
PartSwayEvidence retained_visual_part_sway(const VisualPartAsset& part) noexcept;
// Still-trunk shape of a part at one complection, with the same endpoint
// blend the GPU uses: radius = largest horizontal distance from the figure's
// local axis among cut-out-free positions in the lower 40% of the figure;
// top = highest cut-out-free position; height = highest position.
struct TrunkShape { float radius = 0, top = 0, height = 0; };
TrunkShape retained_visual_trunk_shape(const FigureCpuAsset& figure,
    const std::vector<std::uint16_t>& clear_positions, const std::array<float,3>& shape) noexcept;
// Pack world-unit dimensions, rounded outwards. Use the same authored scale
// as the object-to-world transform: maximum XY for radius, Z for top/height.
std::uint32_t retained_visual_sway_shape(const TrunkShape& trunk,
    const std::array<float,3>& scale) noexcept;
// Tree sway inputs from immutable part evidence (assembly.sways, sway_shape,
// sway_root): the object sways when any part samples foliage texels; its one
// root node anchors the field and the root parts' cut-out-free geometry gives
// the still trunk. Main-map and neighbour-map trees (MOB family 53, the tree
// class) take the same rule.
void sway_tree_assembly(VisualAssemblyAsset& assembly) noexcept;
// The same rule for an object-space record of one part: 0x200 and the trunk
// shape, without the root-node distance (its sway anchor is carried in the
// record, NativeWorldGeometry::sway_root). Zero when the part does not sway.
std::uint32_t retained_object_space_sway_flags(const VisualAssemblyAsset& assembly,
    const VisualPartAsset& part) noexcept;
// Instance flags of a part of a swaying object: 0x200, trunk top (bits 10-15,
// 1/4 unit), trunk radius (bits 16-21, 1/16 unit), plant height (bits 22-27,
// 1/4 unit) and the part node's distance from the root node (bits 28-31).
// All parts evaluate one displacement field around the root node, so parts
// that touch never separate. Zero when the object does not sway.
std::uint32_t retained_visual_sway_flags(const RetainedVisualSample& sample,
    const VisualPartAsset& part) noexcept;
// Item 5, main-map statics in object space. The renderer publishes which
// MOB objects may enter (eligible: trees 53 and stones 55 of the active map's
// MOB, no quest object) and which catalog models are ready. A model is the
// live object's own resolved assembly at its complection
// (retained_visual_object_space_model): the catalog is built from the
// assemblies the game already resolved, never from a second load. Revision
// changes with every publication (models ready, released).
struct ActiveStaticsIndex {
    std::uint64_t revision = 0;
    // Changes only when the catalog is released or replaced: object-space
    // registrations made under another catalog are stale.
    std::uint64_t catalog = 0;
    // The terrain source generation the catalog was built for: at a map
    // change the previous map's index stays published for a few frames (#289:
    // zone3obr's statics read as not eligible against zone18's index and drew
    // posed while the reveal held; active_statics_index_current).
    std::uint64_t source_generation = 0;
    std::unordered_set<std::int32_t> eligible;   // MOB object ids
    std::unordered_set<std::uint64_t> models;    // ready catalog models
    std::unordered_set<std::uint64_t> failed;    // models the catalog could not build
};
void active_statics_index_publish(std::shared_ptr<ActiveStaticsIndex> index) noexcept;
// Whether the index belongs to the current terrain source (not the previous map's).
bool active_statics_index_current(const ActiveStaticsIndex* index) noexcept;
std::shared_ptr<const ActiveStaticsIndex> active_statics_index() noexcept;
// The catalog model of a sample, by content: its model resource, complection,
// scale and centre shift, and per part FIG resource, model node, texture name
// (archive texture or game surface alike) and hidden flag. Cached per
// assembly revision on the prepared state.
std::uint64_t retained_visual_object_space_model(const RetainedVisualSample& sample) noexcept;
// Models the bridge's caster store build found on eligible live objects and
// not yet in the catalog: the renderer takes them as one worker batch. Each
// model is requested once per index lifetime (released with a null publish).
struct ActiveStaticsModelRequest {
    std::uint64_t key = 0;
    std::shared_ptr<const VisualAssemblyAsset> assembly;
    std::array<float, 3> shape{};
};
void active_statics_request(const RetainedVisualSample& sample, std::uint64_t key) noexcept;
std::vector<ActiveStaticsModelRequest> active_statics_take_requests() noexcept;
// One catalog model built from a live assembly at its complection, as a
// neighbour decoration candidate in the object's own frame (identity
// placement): per assembly part one node (figure null when the part has no
// geometry), positions selected at the shape, node placement from the neutral
// skeleton. impostor_inputs_complete is false when a drawn part's texture is
// not an archive texture (a game surface): that model gets static LOD only.
bool prepare_live_static_candidate(const ActiveStaticsModelRequest& request,
    StaticObjectSource& source, std::int32_t model_index,
    std::shared_ptr<RetainedStaticSourceCandidate>& candidate) noexcept;
// Registrations the renderer refused (a part it could not match): the
// object stays posed; shadow stores rebuild when this changes.
std::uint64_t active_statics_failures() noexcept;
void active_statics_note_failure() noexcept;
// The step an object-space registration (colour or caster) failed at, per
// refusal: the bridge's part preparation and placement self-check, then the
// renderer's catalog match and queueing. Counts and the first examples per
// step reset with the index; the renderer logs them (F11 too).
enum class ObjectSpaceRefusal : std::uint8_t { pose, part_prepare, part_submission, place_node, place_bounds,
    invalid, model_missing, part_source, node_unmatched, figure_mismatch, queue, adopt, exception, count };
const char* object_space_refusal_name(ObjectSpaceRefusal step) noexcept;
void active_statics_note_refusal(ObjectSpaceRefusal step, const char* detail = nullptr) noexcept;
struct ActiveStaticsRefusals {
    std::array<std::uint32_t, std::size_t(ObjectSpaceRefusal::count)> counts{};
    std::array<std::vector<std::string>, std::size_t(ObjectSpaceRefusal::count)> examples;
    std::uint64_t revision = 0; // bumped per note
};
ActiveStaticsRefusals active_statics_refusals() noexcept;
// Objects the bridge admitted to object space in its last colour pass.
void active_statics_note_admitted(std::uint32_t objects) noexcept;
std::uint32_t active_statics_admitted() noexcept;
// The admitted objects of the whole world (sorted ids), from the sun caster
// store's build: the renderer's object-space caster bake draws exactly
// these, the store none of them. Revision changes with the set.
struct ActiveStaticsCasters { std::uint64_t revision = 0; std::vector<std::int32_t> ids; };
void active_statics_casters_publish(std::vector<std::int32_t> ids) noexcept;
std::shared_ptr<const ActiveStaticsCasters> active_statics_casters() noexcept;
// Per-frame admission from authoritative state: a catalog object, not a
// unit, not hidden, dead or invisible, no clip selected and its animation
// time inactive, unit scale and no centre shift, no time-scrolled or
// order-preserving part (wanted: model is its catalog key), its model ready
// in the catalog and no refused registration for this assembly under this
// index. Anything else draws today's posed records this frame. Colour and
// the sun caster store both decide by this; the store build requests the
// model of a wanted object that is not ready.
// Why an object is (not) in object space, first failing clause in order.
enum class ObjectSpaceReason : std::uint8_t { admitted, not_eligible, unit, hidden, animating, scaled, texture_kind,
    model_failed, model_pending, refused, count };
ObjectSpaceReason retained_visual_object_space_reason(const RetainedVisualSample& sample,
    const ActiveStaticsIndex* index, const std::vector<std::int32_t>& time_active_ids, std::uint64_t& model) noexcept;
// The caster store's last full build: objects per reason, then their drawn
// parts per reason (F11 main_map_object_space_reasons).
void active_statics_reasons_publish(const std::array<std::uint32_t, 2 * std::size_t(ObjectSpaceReason::count)>& counts) noexcept;
std::array<std::uint32_t, 2 * std::size_t(ObjectSpaceReason::count)> active_statics_reasons() noexcept;
bool retained_visual_object_space_wanted(const RetainedVisualSample& sample,
    const ActiveStaticsIndex* index, const std::vector<std::int32_t>& time_active_ids, std::uint64_t& model) noexcept;
bool retained_visual_object_space_admitted(const RetainedVisualSample& sample,
    const ActiveStaticsIndex* index, const std::vector<std::int32_t>& time_active_ids) noexcept;
// The object-space placement of an admitted sample's parts at its neutral
// pose, by the same evaluation as the posed geometry: per part its node's
// world matrix and its object-space sway flags, and the sway anchor. False
// when a part at that placement (its node origin and position radius) leaves
// its posed bounds.
bool retained_visual_object_space_parts(const RetainedVisualSample& sample,
    const std::uint32_t* parts, std::size_t count, const NativeWorldBounds* posed_bounds,
    std::vector<std::array<float, 16>>& node_to_world, std::vector<std::uint32_t>& sway_flags,
    std::array<float, 3>& sway_root);
// Character part merge (#65): (re)build the unit's merged groups for its
// current assembly; parts that cannot merge stay per part.
bool retained_visual_merged_groups(const RetainedVisualSample& sample);
bool retained_visual_group_depth_revision(const RetainedVisualSample& sample,
    std::size_t group_index,double time,NativeShadowCasterRevision& revision) noexcept;
bool retained_visual_group_submission(const RetainedVisualSample& sample,
    std::size_t group_index,const NativeWorldBounds& bounds,NativeContribution contribution,
    double time,std::uint8_t fog,NativePreparedPartSubmission*& record);

void retained_visual_part_fixed_state(const RetainedVisualSample& sample,NativeOwnedFixedState& state) noexcept;

// Shared material/animation values for active and streamed astral parts.
bool retained_visual_part_values(const RetainedVisualSample& sample,
    const VisualPartAsset& part, double time, std::uint8_t fog,
    NativePreparedPartValues& values);

} // namespace owned
