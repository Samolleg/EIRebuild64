#pragma once
#include "static_lod.h"

#include "lifecycle_probe.h"
#include "owned_asset_loader.h"
#include "owned_visual_evaluation.h"
#include "owned_terrain_front_end.h"
#include "retained_publication.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace owned {
namespace skn {struct RuntimeAsset;}
namespace skm {struct Clip;}
namespace gltf {struct PlaybackState;}

struct RetainedStaticSourceCandidate;
struct FurProfile;

enum class MobSourceRole : std::uint8_t {
    main,
    secondary,
};

// How the local server's streamed (dynamic non-unit) list feeds render-only
// copies, by MOB object type (issue #59). Every listed visible object is in
// the authoritative set, so its window-exit copy survives reconciliation;
// leaving one out retires the copy on the next tick and the object pops out.
// Levers (60) are seeded too: their only visual state is the complection the
// server sends on entry. Torches (58) keep a window-exit mesh but are never
// seeded, since their flame belongs to the live object. Traps (59) never
// stream through the window.
enum class ServerStreamedObject : std::uint8_t { skip, retain, seed };
inline ServerStreamedObject server_streamed_object(std::uint32_t type) noexcept {
    if (type == 59) return ServerStreamedObject::skip;
    return type == 58 ? ServerStreamedObject::retain : ServerStreamedObject::seed;
}

struct RetainedAdmission {
    std::uint32_t nid = 0;
    std::uint32_t family = 0;
    std::int32_t signed_id = 0;
};

struct VisualResourceIncarnation {
    std::int32_t signed_id = 0;
    std::uint64_t serial = 0;
};

enum class RetainedVisualStatus : std::uint8_t {
    constructing, awaiting_values, ready, invalid_asset, invalid_intent,
    unsupported_callback, unsupported_selector, unsupported_appearance, unavailable_surface,
    invalid_route, invalid_height, clock_discontinuity, publication_fault,
    // Current height input is absent; valid pose/effect clocks still commit.
    unavailable_terrain
};
const char* retained_visual_status_name(std::uint32_t status) noexcept;

struct VisualPartAsset {
    std::string name;
    std::shared_ptr<const FigureCpuAsset> geometry;
    std::uint32_t skeleton_node = UINT_MAX;
    std::shared_ptr<const TextureCpuAsset> texture;
    std::string texture_source_name;
    bool assembled_character_texture = false;
    // The game texture's own name (CTexture name) for surface and value
    // parts alike: the key of its material composition (material classes).
    std::string material_name;
    std::shared_ptr<const OwnedVisualSurface> surface;
    bool source_available = true;
    std::shared_ptr<const MaterialCpuAsset> material;
    std::uint32_t diffuse = 0xffffffffu;
    std::uint32_t specular = 0xff000000u;
    std::uint32_t texture_type = 0;
    std::uint32_t material_type = 0;
    std::uint32_t figure_flags = 0;
    std::uint32_t material_flags = 0;
    std::uint32_t texture_flags = 0;
    bool weapon = false;
    bool arrow = false;
    bool explicitly_hidden = false;
    bool casts_shadow = true;
};

// The ordered source layers of one composed character atlas (the game's
// CTextureManager::ComposeTexture or the addon's memory compose), keyed by
// the composite's texture name. Material classes come from these layers.
struct MaterialComposition {
    std::vector<std::string> layers;  // composition order, the selected file names
    // Addon packed attachment atlas: the layer index placed in each 512
    // quadrant (right hand, left hand, helm, shield); -1 none. All -1: every
    // layer covers the whole composite (body atlases, stock secondary atlas).
    std::array<std::int32_t, 4> slots{{-1, -1, -1, -1}};
    std::uint32_t dimension = 0;      // composite side, texels
    // The set's user data: 12-byte item tags {type, item, ...} (0x3004
    // weapon, 0x3005 armor; item = shape << 16 | material row).
    std::vector<std::uint8_t> user_data;
    std::uint64_t serial = 0;         // changes when the layer list changes
};

// Immutable semantic appearance, built from cold named parts. The skeleton
// is shared even when an attachment's FIG comes from a different model.
struct VisualAssemblyAsset {
    bool main_hero_visual=false;
    std::uint64_t revision = 0;
    std::shared_ptr<const ModelCpuAsset> skeleton;
    std::vector<VisualPartAsset> parts;
    std::array<float, 3> shape{};
    std::array<float, 3> center_shift{};
    std::array<float, 3> scale{1, 1, 1};
    float envelope_radius = 0;
    // A tree-class object (trees, bushes, giant mushrooms) with at least one
    // part sampling cut-out (foliage) texels. Selects wind sway of the whole
    // object; it never changes geometry. sway_shape holds the encoded trunk
    // top, trunk radius and plant height flag bits (see
    // retained_visual_sway_flags); sway_root is the root node index.
    bool sways = false;
    std::uint32_t sway_shape = 0;
    std::uint32_t sway_root = 0;
    // Selected model/bone member bytes this assembly was built from. Holding
    // them keeps the resource store's weak backing alive, so republishing an
    // unchanged appearance does not read and allocate the member again.
    std::vector<std::shared_ptr<const std::vector<std::uint8_t>>> source_backings;
};

struct VisualTickValues {
    std::uint32_t object_flags = 0;
    bool unit = false;
    bool timed_highlight_active = false;
    // Renderer highlight policy bits (visual_highlight.h). Only frame samples
    // carry it; published tick values keep zero.
    std::uint8_t highlight = 0;
    // Completed native local bounding-box maxima used only for death targeting.
    std::array<float,2> target_half_extent{};
    VisualRateValues rate{};
    VisualEquipmentValues equipment{};
    double route_start_tick = 0;
    float base_z = 0;
    float fade_alpha = 1;
    std::uint8_t locomotion = 0;
    std::uint8_t start_animation = 0xff;
    float visibility = 1;          // Effective sight percept; exact on the local server.
    float loudness = 1;            // Effective hearing percept; rounded client fallback.
    std::uint8_t pose = 2;          // Replicated unit pose: 0 lay, 1 crawl, 2 stand, 3 run, 4 rest.
    bool moving = false;            // Replicated moving flag (the game stores 0 or -1).
    std::uint32_t blood = 0; // Authoritative race blood type, captured with unit values.
    bool undead_prototype = false; // Explicit Undead prototype code, including Base human-model undead.
    float vision = 0;              // Exact server sight range (m); 0 without the local server.
    bool controlled = false;       // Controlled by the local human player (the party).
    bool dead = false;
    bool hidden = false;
    bool invisible = false;
    bool stunned = false;
};

// Completed common-object values. Selection/phase are sampled after the game's
// semantic writers; only units use the independent route/intent evaluator.
struct VisualObjectPose {
    std::array<float,3> position{};
    std::array<float,4> quaternion{1,0,0,0};
    std::array<float,3> shape{};
    std::uint32_t object_flags = 0;
    VisualClipSelection selection{nullptr,nullptr,0,true};
    double phase = 0;
    std::uint64_t source_generation = 0;
    // The cached ADB can survive an appearance change; current ANM selection
    // must be observed again for that completed appearance revision.
    std::uint64_t appearance_revision = 0;
};

// One lock-owned view of every scene value needed to publish a completed
// non-unit object pose. Native selector/callback values are still read at their
// authoritative boundary; this snapshot binds one capture to one retained
// tuple and makes final publication reject a stale resource, resolver,
// assembly, or prior-pose incarnation atomically.
struct VisualObjectPublicationSnapshot {
    VisualResourceIncarnation resource{};
    std::uint64_t resolver_generation = 0;
    std::uint64_t assembly_revision = 0;
    std::shared_ptr<const AnimationDatabaseAsset> database;
    VisualObjectPose prior_pose{};
    bool has_prior_pose = false;
};

// Backend handles remain with the existing resource incarnation. They confer
// no world membership. A frame borrows this one resource record while queued
// commands independently pin their renderer generations.
struct VisualPreparedResources {
    struct SelectionEnvelope {
        std::weak_ptr<const AnimationDatabaseAsset> database;
        std::weak_ptr<const AnimationClipAsset> clip;
        std::uint32_t ordinal = 0;
        float radius = 0;
        bool neutral = false;
    };
    struct DepthPose {
        std::array<float,16> root{};
        std::array<float,3> shape{},center{};
        std::uint32_t frame = 0;
        float fraction = 0,height = 1,alpha = 1,u = 0,v = 0;
        bool operator==(const DepthPose& other) const noexcept {
            return root==other.root && shape==other.shape && center==other.center && frame==other.frame &&
                fraction==other.fraction && height==other.height && alpha==other.alpha && u==other.u && v==other.v;
        }
    };
    struct AdmissionKey {
        LifecycleIdentity lifecycle{};
        VisualResourceIncarnation resource{};
        std::shared_ptr<const VisualAssemblyAsset> assembly;
        std::uint64_t assembly_revision = 0;
        std::uint32_t object_flags = 0;
        float opacity = 1;
        bool shadows_enabled = false;
        bool draw_undetected = false;
        bool unit = false;
        bool hidden = false;
        bool transparent_override = false;
        bool hide_weapons = false;
        bool hide_arrow = false;
        bool casts_shadow = true;
    };
    struct PartAdmission {
        bool visible = false;
        bool receives = false;
        bool casts = false;
    };
    struct WorldBoundsKey {
        LifecycleIdentity lifecycle{};
        VisualResourceIncarnation resource{};
        std::shared_ptr<const VisualAssemblyAsset> assembly;
        VisualClipSelection selection{};
        std::array<float,3> root{}, position{};
        std::array<float,4> root_rotation{1,0,0,0}, quaternion{1,0,0,0};
        std::uint64_t assembly_revision = 0;
        float envelope_radius = 0;
        bool unit = false;
        bool tilted_facing = false;
        bool root_rotation_valid = false;
    };
    struct PoseKey {
        LifecycleIdentity lifecycle{};
        VisualResourceIncarnation resource{};
        std::shared_ptr<const VisualAssemblyAsset> assembly;
        VisualClipSelection selection{};
        std::array<float,3> root{}, position{}, shape{};
        std::array<float,4> root_rotation{1,0,0,0}, quaternion{1,0,0,0};
        std::uint64_t assembly_revision = 0;
        double phase = 0;
        float facing = 0;
        bool unit = false;
        bool tilted_facing = false;
        bool root_rotation_valid = false;
    };
#ifdef EID3D11_TESTING
    struct NodeKey {
        std::string model, bones, animation, database;
        std::array<float,3> shape{}, center_shift{};
        double phase = 0;
        bool operator==(const NodeKey& other) const noexcept {
            return model==other.model && bones==other.bones && animation==other.animation &&
                database==other.database && shape==other.shape && center_shift==other.center_shift &&
                phase==other.phase;
        }
    };
#endif
    struct Part {
        NativePreparedMeshHandle mesh{};
        NativePreparedMaterialHandle material{};
        NativePreparedMaterialHandle wound_material{};
        std::shared_ptr<const FigureCpuAsset> interaction_geometry;
        std::array<float,3> interaction_shape{};
        NativeWorldBounds interaction_bounds{};
        NativeGeometryView record;
        NativePreparedPartSubmission submission;
        std::array<NativeOwnedTextureMip,32> texture_mips{};
        std::shared_ptr<const VisualAssemblyAsset> bound_assembly;
        std::shared_ptr<const VisualSkeletonBinding> bound_skeleton;
        std::shared_ptr<const VisualAssemblyAsset> envelope_assembly;
        std::array<float,3> envelope_shape{};
        std::array<float,3> envelope_center{};
        float envelope_radius = 0, geometry_radius = 0;
        std::uint64_t record_revision = 0;
        std::shared_ptr<const VisualAssemblyAsset> depth_assembly;
        std::shared_ptr<const VisualSkeletonBinding> depth_binding;
        DepthPose depth_pose;
        std::uint64_t depth_revision = 0;
        NativeWorldBounds derived_bounds{};
        std::uint64_t derived_bounds_pose_generation = 0;
        bool derived_bounds_valid = false;
#ifdef EID3D11_TESTING
        std::uint64_t derived_bounds_builds = 0, derived_bounds_reuses = 0;
#endif
    };
    std::vector<Part> parts;
    // Character part merge (#65): a unit's parts that share texture, material,
    // flags and shape layout are concatenated into one mesh and drawn as one
    // colour record. Each component carries its part's skeleton node and
    // position base, so the GPU animates every part as before. Built once per
    // assembly (NPCs never change appearance on a map); rebuilt on change.
    // Immutable merged vertices, one copy per content identity: identical
    // characters (a squad of guards) share it instead of each holding its own
    // eight-shape copy (#188).
    struct MergedGeometry {
        std::vector<NativePackedGroup> positions, normals;
        std::vector<NativeTextureCoordinate> texture_coordinates;
        std::vector<NativeVertexComponent> components;
        std::vector<std::uint8_t> skin_vertices;
        std::vector<std::uint32_t> component_parts;   // skeleton node | position base << 16
        std::vector<std::uint16_t> indices;
        NativeMeshLods skn_lods;
        ResourceIdentity identity;                    // content identity: shared by identical characters
    };
    bool skn_selected = false;
    std::shared_ptr<const skn::RuntimeAsset> skn_bounds_asset;
    PoseKey skn_bounds_key{};
    NativeWorldBounds skn_bounds{};
    bool skn_bounds_valid = false;
    struct MergedGroup {
        std::shared_ptr<const skn::RuntimeAsset> skn_asset;
        std::uint32_t skn_material = 0;
        bool skn_casts_shadow = true;
        std::vector<std::uint32_t> parts;
        std::uint32_t shape_count = 0, position_groups = 0, normal_groups = 0;
        std::shared_ptr<const MergedGeometry> geometry;
        NativePreparedMeshHandle mesh{};
        NativePreparedMaterialHandle material{};
        NativePreparedMaterialHandle wound_material{};
        NativeGeometryView record;
        NativePreparedPartSubmission submission;
        std::array<NativeOwnedTextureMip,32> texture_mips{};
        std::uint64_t record_revision = 0;
        float geometry_radius = 0;
        // Shadow depth identity of the whole group (members share every pose input).
        DepthPose depth_pose;
        std::shared_ptr<const VisualSkeletonBinding> depth_binding;
        std::uint64_t depth_revision = 0;
        // This frame's caster values (serial-stamped scratch).
        NativeShadowCasterRevision frame_revision;
        std::uint64_t frame_serial = 0;
    };
    std::vector<MergedGroup> merged_groups;           // never resized after build: records borrow its identities
    std::vector<std::int32_t> merged_part_group;      // part index -> group, or -1 (drawn per part)
    std::shared_ptr<const VisualAssemblyAsset> merged_assembly;
    bool merged_fur_hair = false;                    // built with hair parts kept apart (fur_mode 2)
    // Fur prototype (fur.h): the profile of fur_assembly, built once per assembly.
    std::shared_ptr<const FurProfile> fur_profile;
    std::shared_ptr<const VisualAssemblyAsset> fur_assembly;
    AdmissionKey derived_admission_key{};
    std::vector<PartAdmission> derived_admission_parts;
    WorldBoundsKey derived_world_bounds_key{};
    NativeWorldBounds derived_world_bounds{};
    PoseKey derived_pose_key{};
    std::uint64_t derived_pose_generation = 0;
    // Neutral body volumes belong to the immutable appearance. Each effective
    // skeleton root supplies current posture/lift without child limb animation.
    struct CombatAnchorBody {
        std::uint32_t root = 0;
        std::array<float,3> minimum{}, maximum{};
    };
    std::weak_ptr<const VisualAssemblyAsset> combat_anchor_assembly;
    std::vector<CombatAnchorBody> combat_anchor_bodies;
    bool combat_anchor_valid = false;
    // Ground cover object mask: the ground-contact rectangles (centre xy,
    // half size xy) of this static object's visible non-crown parts
    // (ground_cover::contact_footprints). A function of the pose key without
    // its animation phase and of the terrain source only.
    std::vector<std::array<float,4>> ground_contacts;
    PoseKey ground_contacts_key{};
    std::uint64_t ground_contacts_terrain_generation = 0, ground_contacts_terrain_revision = 0;
    bool ground_contacts_valid = false;
    // Critter perches of a tree or cactus (bridge_critter_perches): world
    // points, w = critters::PerchKind; a function of the pose key without its phase.
    std::vector<std::array<float,4>> perches;
    PoseKey perches_key{};
    bool perches_valid = false;
    bool derived_admission_valid = false;
    bool derived_world_bounds_valid = false;
    bool derived_pose_valid = false;
    // Last color selection of a static object whose selected parts were all
    // inside the frustum. Reused verbatim while every input of those records
    // is unchanged and their union stays inside the frustum.
    struct ColorPacket {
        bool valid = false;
        const VisualAssemblyAsset* assembly = nullptr;
        std::uint64_t assembly_revision = 0, pose_generation = 0;
        VisualTickValues values{};
        std::uint8_t fog = 0;
        bool quest_light = false;
        std::vector<std::uint8_t> visible;            // admission visibility per part
        std::vector<std::uint32_t> parts;             // selected parts, in submission order
        std::vector<std::uint64_t> values_revisions;  // per selected part
        std::vector<NativeWorldBounds> part_bounds;   // per selected part
        NativeWorldBounds bounds{};                   // union of part_bounds
        // Registered in the renderer's persistent static set under the key
        // fields above (all visible parts, GPU-culled) for this set generation.
        bool static_registered = false;
        std::uint64_t static_generation = 0;
        // The object journal does not change when neighbour rendering arrives.
        std::uint64_t border_fog_revision = 0;
        bool static_current(std::uint64_t generation, std::uint64_t fog_revision) const noexcept {
            return static_registered && static_generation == generation && border_fog_revision == fog_revision;
        }
    };
    ColorPacket color_packet;
    std::shared_ptr<const VisualSkeletonBinding> skeleton_binding;
    NativeWorldGeometry::Pose gpu_pose;
    std::shared_ptr<gltf::PlaybackState> gltf_playback;
    std::shared_ptr<const skm::Clip> motion_clip;
    std::shared_ptr<const skn::RuntimeAsset> motion_asset;
    std::shared_ptr<const VisualAssemblyAsset> motion_assembly;
    std::shared_ptr<const VisualSkeletonBinding> motion_source_binding,motion_binding;
    std::vector<unsigned> motion_nodes,motion_mounts;
    unsigned motion_context=UINT_MAX,motion_root=UINT_MAX;
    PoseKey motion_pose_key;
    bool motion_pose_valid=false;
    std::vector<VisualNodeEnvelope> node_envelopes;
    std::array<float,3> envelope_shape{}, envelope_center{};
    std::shared_ptr<const VisualSkeletonBinding> envelope_binding;
    VisualResourceIncarnation selection_envelope_resource{};
    std::shared_ptr<const VisualAssemblyAsset> selection_envelope_assembly;
    std::array<float,3> selection_envelope_shape{}, selection_envelope_center{}, selection_envelope_scale{};
    std::vector<SelectionEnvelope> selection_envelopes;
    std::size_t selection_envelope_replace = 0;
#ifdef EID3D11_TESTING
    std::uint64_t derived_admission_builds = 0, derived_admission_reuses = 0;
    std::uint64_t derived_world_bounds_builds = 0, derived_world_bounds_reuses = 0;
    std::uint64_t derived_pose_builds = 0, derived_pose_reuses = 0;
    std::uint64_t selection_envelope_builds = 0;
    NodeKey node_key;
    std::shared_ptr<const std::vector<VisualNodeSample>> nodes;
    std::uint64_t node_generation = 0;
#endif
};

struct RetainedVisualSample {
    // Embedded units use the host's completed pose/materials, not the retail
    // route/intent evaluator. Their rendering still follows unit policy.
    bool completed_pose = false;
    VisualResourceIncarnation resource{};
    LifecycleIdentity lifecycle{};
    RetainedVisualStatus status = RetainedVisualStatus::awaiting_values;
    const char* readiness_reason = nullptr;
    const char* failure_scope = nullptr;
    std::shared_ptr<const VisualAssemblyAsset> assembly;
    std::shared_ptr<VisualPreparedResources> prepared;
    bool mesh_declared = false;
    VisualObjectPose object_pose{};
    VisualClipSelection selection;
    VisualPoseState pose{};
    float liquid_speed = 0; // Current world XY route derivative; zero while stationary/frozen.
    std::array<float,2> liquid_direction{}; // Unit XY heading of that derivative (zero when still).
    bool height_valid = true;
    VisualEffectState effects{};
    VisualClipVisibility visibility{};
    VisualTickValues values{};
    // Derived from this frame's quest_lights, never a second light owner.
    bool quest_light_active = false;
    std::uint64_t route_generation = 0;
    std::uint64_t pose_revision = 0;
    float envelope_radius = 0;
};

struct RetainedQuestLight {
    LifecycleIdentity source{};
};

// Renderer-owned highlight inputs for one frame: F10 options, whether the
// base (briefing) screen is open, and the game's current cursor target.
struct RetainedHighlightState {
    bool show_selected = false;
    bool strip_preselected = false;
    bool hover_characters = false;
    bool hover_objects = false;
    VisualResourceIncarnation hover{};
};

// The world light manager owns these IDs and values. The renderer consumes
// shadow eligibility while the same published light drives illumination.
struct RetainedWorldPointLight {
    std::int32_t source_id = 0;
    std::array<float,3> center{};
    std::array<float,3> color{};
    float radius = 0;
    bool casts_shadow = false;
    float shadow_fade = 1; // retained lamps fade their shadow out after the game drops them
    bool attached = false; // unit attachment: never remembered in place
    bool hero = false; // current game-defined hero attachment, captured with manager membership
};

inline std::array<float,3> retained_point_shadow_center(
    const RetainedWorldPointLight& light) noexcept {
    // Shadows cast from the published light position. The former upward lift
    // moved lamp origins above their own heads and cast detached fixture
    // shadows; enclosing fixtures are now excluded per part instead.
    return light.center;
}

// Copies cheap values/handles only. Bone/morph sampling is an explicit consumer
// operation after color/light contribution selection, never part of this view.
struct RetainedVisualFrame {
    double time = 0;
    std::vector<RetainedVisualSample> entities;
    std::vector<RetainedQuestLight> quest_lights;
    std::vector<RetainedWorldPointLight> world_point_lights;
    OwnedVisualDiagnostic diagnostic{};
    OwnedVisualRejection first_failure{};
};

// Writer-side shadow input changes. A render consumer acknowledges only after
// it has published a replacement; failed preparation leaves the changes live.
// Time-active IDs are a source-owned subset, not a second scene membership.
struct RetainedShadowSourceChanges {
    std::uint64_t revision = 0;
    bool full = false;
    bool terrain_dirty = false;
    bool terrain_time_only = false;
    std::vector<std::int32_t> dirty_visuals;
    std::vector<std::int32_t> time_active_visuals;
    std::vector<std::uint32_t> time_active_terrain_sectors;
    std::vector<std::uint32_t> wave_active_terrain_sectors;
};

// ground_contacts: the ground-cover object contact store (#164); it reads
// value and lifecycle changes, not shadow state.
enum class RetainedShadowConsumer : std::uint8_t { directional, point, persistent_scene, ground_contacts, count };

// The scene lock protects this journal. Consumers acknowledge only completed
// publications. Inactive consumers retain no history and begin with a full read.
class RetainedShadowChangeJournal {
public:
    static constexpr std::size_t capacity = 8192;
    std::uint64_t revision() const noexcept { return revision_; }
    // A publication prepared from revision `through` may still be acknowledged:
    // no reset happened after it.
    bool acknowledgeable(std::uint64_t through) const noexcept {
        return through && through <= revision_ && through >= full_revision_;
    }
    void visual_changed(std::int32_t id) noexcept;
    void terrain_changed(bool time_only) noexcept;
    RetainedShadowSourceChanges changes_since(std::uint64_t since) const;
    RetainedShadowSourceChanges read(RetainedShadowConsumer consumer);
    void acknowledge(RetainedShadowConsumer consumer, std::uint64_t through) noexcept;
private:
    struct Entry { std::uint64_t revision; std::int32_t id; };
    void advance() noexcept;
    void compact() noexcept;
    void reset() noexcept;
    std::uint64_t revision_ = 1, full_revision_ = 1, compacted_through_ = 0;
    std::uint64_t terrain_revision_ = 1, terrain_static_revision_ = 1;
    std::deque<Entry> entries_;
    std::array<std::uint64_t, static_cast<std::size_t>(RetainedShadowConsumer::count)> cursors_{};
    std::array<bool, static_cast<std::size_t>(RetainedShadowConsumer::count)> active_{};
    std::array<bool, static_cast<std::size_t>(RetainedShadowConsumer::count)> needs_full_{};
};

struct RetainedDirectionalSlice {
    std::array<float,16> world_to_clip{};
    NativeWorldBounds receivers_in_light{};
    // The two far maps use this visible-receiver footprint for caster admission.
    // The broader bounds above continue to own stable projection fitting.
    NativeWorldBounds far_receivers_in_light{};
    std::array<std::uint64_t,256> far_receiver_cells{};
    // Receiver-only XY fit; casters extend upstream depth, not texel size.
    std::array<float,2> light_center{};
    float light_radius = 0;
    // The receiver fades toward a coarser map over this view-distance interval.
    float blend_start_view_distance = 0;
    float blend_end_view_distance = 0;
};

struct RetainedShadowCasterValue {
    NativeWorldBounds bounds{};
    NativeShadowCasterRevision revision;
    std::uint8_t mask = 0;
    bool persistent_static = false;
    // Projected once at contribution classification.
    NativeWorldBounds light_bounds{};
};

struct RetainedDirectionalCoverage {
    std::array<float,16> world_to_clip{};
    std::array<std::array<float,3>,3> light_axes{};
    std::array<RetainedDirectionalSlice,5> slices{};
    std::uint32_t map_side = 2048;
    bool valid = false;
    bool directional = false;
    bool spatial_selection = false;
};

struct RetainedShadowActiveVisualPart {
    std::uint32_t index = 0;
    NativeWorldBounds bounds{};
    NativeShadowCasterRevision revision{};
};
struct RetainedShadowActiveVisualValue {
    std::int32_t signed_id = 0;
    NativeWorldBounds receiver{};
    std::vector<RetainedShadowActiveVisualPart> casters;
};
struct RetainedShadowActiveTerrainValue {
    std::uint32_t sector = 0;
    std::uint64_t phase_revision = 0;
    std::uint32_t phase_count = 0;
    bool captured = false;
    bool animated_tiles_enabled = false;
};

// Scene-owned shadow source values. Current-frame GPU commands and borrowed
// material/mesh pointers never enter this durable value.
struct RetainedShadowInputDomain {
    std::uint64_t source_revision = 0;
    std::array<float,16> world_to_clip{};
    NativeWorldLighting lighting{};
    std::uint32_t class_flags = 0;
    bool draw_undetected = false;
    bool terrain_enabled = false;
    std::uint64_t terrain_source_generation = 0, terrain_source_revision = 0;
    std::uint64_t terrain_structural_revision = 0;
    NativeTerrainState terrain_animation{};
    OwnedTerrainFrameInputs terrain_inputs{};
    std::array<float,16> terrain_object_to_world{};
    NativeOwnedFixedState terrain_material{};
    RetainedDirectionalCoverage coverage{};
    std::vector<NativeWorldBounds> receivers;
    std::vector<RetainedShadowCasterValue> casters;
    std::vector<RetainedShadowActiveVisualValue> active_visuals;
    std::vector<RetainedShadowActiveTerrainValue> active_terrain;
    NativeShadowFrame frame{};
};

struct RetainedStaticBinding {
    LifecycleIdentity lifecycle{};
    std::shared_ptr<const RetainedStaticSourceCandidate> candidate;
};

enum class RetainedStaticPrepareResult : std::uint8_t {
    not_attempted,
    success,
    source_collision,
    missing_terrain,
    binding_identity,
    source_values,
    model_lookup,
    model_structure,
    terrain_sample,
    grounded_position,
    object_transform,
    figure_structure,
    component_range,
    index_range,
    bone_translation,
    parent_order,
    node_bounds,
    position_selection,
    texture_selector,
    texture_lookup,
    exception,
};

inline constexpr std::size_t kRetainedStaticPrepareNoIndex =
    static_cast<std::size_t>(-1);

struct RetainedStaticPrepareDiagnostic {
    RetainedStaticPrepareResult result =
        RetainedStaticPrepareResult::not_attempted;
    std::uint64_t binding_revision = 0;
    std::size_t source_bindings = 0;
    std::size_t binding_index = kRetainedStaticPrepareNoIndex;
    std::size_t node_index = kRetainedStaticPrepareNoIndex;
    std::int32_t signed_id = 0;
};

struct RetainedStaticFrameSnapshot {
    std::uint64_t source_revision = 0;
    std::uint64_t binding_revision = 0;
    std::size_t binding_count = 0;
    std::size_t unique_geometry_variants = 0;
    bool collision_free = true;
    RetainedStaticPrepareDiagnostic prepare{};
};

struct RetainedStaticInstrumentation {
    std::uint64_t source_publications = 0;
    std::uint64_t source_object_visits = 0;
    std::uint64_t source_candidate_validations = 0;
    std::uint64_t source_candidate_preparations = 0;
    std::uint64_t source_index_rebuilds = 0;
    std::uint64_t source_index_candidate_visits = 0;
    std::uint64_t lifecycle_lookups = 0;
    std::uint64_t binding_insertions = 0;
    std::uint64_t binding_retirements = 0;
};

struct RetainedStaticSceneSnapshot {
    std::uint64_t source_generation = 0;
    std::uint64_t source_revision = 0;
    std::uint64_t binding_revision = 0;
    std::vector<std::string> source_membership;
    std::shared_ptr<const TerrainCpuAsset> terrain;
    std::vector<RetainedStaticBinding> bindings;
    std::size_t pending_transactions = 0;
    bool collision_free = true;
};

struct RetainedTerrainTextureValue {
    std::shared_ptr<const OwnedVisualSurface> surface;
    bool available = false;
};
using RetainedTerrainTextures = std::map<std::uint32_t, RetainedTerrainTextureValue>;

struct RetainedTerrainSourceSnapshot {
    std::uint64_t source_generation = 0;
    std::uint64_t source_revision = 0;
    std::shared_ptr<const TerrainCpuAsset> terrain;
    std::shared_ptr<const OwnedTerrainGeometryValues> geometry;
    std::shared_ptr<OwnedTerrainPreparedResources> prepared;
    std::shared_ptr<const RetainedTerrainTextures> textures;
    // The same published main source (immutable MOB objects and resource
    // names); read-only input for renderer-owned ambient effects.
    std::shared_ptr<const OwnedMapSnapshot> map;
};

struct RetainedTerrainSourceIdentity {
    std::uint64_t source_generation = 0;
    // Revision at main publication, independent of later source membership.
    std::uint64_t source_revision = 0;
};

enum class RetainedSceneIdentityStatus : std::uint8_t {
    unobserved,
    selected,
    loading,
    published,
};

constexpr std::size_t kRetainedSceneIdentitySourceLimit = 64;

struct RetainedSceneIdentitySnapshot {
    RetainedSceneIdentityStatus status =
        RetainedSceneIdentityStatus::unobserved;
    std::string main_outer_resource;
    std::string main_mob_resource;
    std::vector<std::string> published_source_membership;
    std::uint64_t source_generation = 0;
    std::uint64_t source_revision = 0;
    std::uint64_t binding_revision = 0;
    std::uint64_t positive_world_epoch = 0;
    std::size_t pending_transactions = 0;
    bool published_source_membership_truncated = false;
};

const char* retained_scene_identity_status_name(
    RetainedSceneIdentityStatus status) noexcept;

class RetainedStaticScene;

// A binding generation token, not a lock across the native scene callback.
// Each lookup briefly locks the owner, validates the captured revisions and
// returns a shared_ptr pin that survives publication and sink execution.
class RetainedStaticBindingView {
public:
    RetainedStaticBindingView() noexcept = default;
    ~RetainedStaticBindingView();
    RetainedStaticBindingView(const RetainedStaticBindingView&) = delete;
    RetainedStaticBindingView& operator=(
        const RetainedStaticBindingView&) = delete;
    RetainedStaticBindingView(RetainedStaticBindingView&& other) noexcept;
    RetainedStaticBindingView& operator=(
        RetainedStaticBindingView&& other) noexcept;

    explicit operator bool() const noexcept { return scene_ != nullptr; }
    bool current() const noexcept;
    bool empty() const noexcept;
    RetainedStaticFrameSnapshot frame_snapshot() const noexcept;
    bool binding(std::int32_t signed_id,
                 RetainedStaticBinding& result) const noexcept;

private:
    friend class RetainedStaticScene;
    explicit RetainedStaticBindingView(
        const RetainedStaticScene* scene) noexcept;
    void release() noexcept;

    const RetainedStaticScene* scene_ = nullptr;
    RetainedStaticFrameSnapshot frame_{};
};

// Authoritative source membership and lifecycle join for the first immutable
// retained-scene slice. Each publication owns one prevalidated candidate set;
// the current family owns one signed-ID index, and lifecycle events update only
// full numeric identities. Engine pointers are never accepted.
class RetainedStaticScene {
public:
    std::uint64_t begin_source(MobSourceRole role, const char* name,
                               const char* selected_mob = nullptr,
                               bool* reused_existing = nullptr) noexcept;
    // Confirmation comes from either the common MOB parser or an accepted
    // saved-world document. Both establish the same selected source name.
    bool record_source(std::uint64_t transaction, const char* name) noexcept;
    bool record_parser_source(std::uint64_t transaction, const char* name) noexcept {
        return record_source(transaction, name);
    }
    void record_admission(std::uint64_t transaction, std::uint32_t nid,
                          std::uint32_t family,
                          std::int32_t signed_id) noexcept;
    bool finish_source(std::uint64_t transaction, bool owner_success,
                       bool common_source_confirmed,
                       std::string& authoritative_name,
                       std::string* terrain_resource = nullptr,
                       bool* reused_existing = nullptr) noexcept;
    void abandon_source(std::uint64_t transaction) noexcept;
    bool publish_source(std::uint64_t transaction,
                        std::shared_ptr<const OwnedMapSnapshot> snapshot) noexcept;

    void lifecycle_added(const LifecycleIdentity& identity) noexcept;
    void lifecycle_removed(std::int32_t signed_id) noexcept;
    void positive_lifecycle_reset(std::uint64_t new_epoch) noexcept;
    void reset() noexcept;
    // A client removal that the server issued only because the object left
    // the player's camera window is not a world deletion. Call before the removal: a ready non-unit visual keeps
    // drawing from its last published values while membership retires
    // normally. The copy is replaced once the next incarnation of the ID is
    // ready, and retired by visual_retire_detached on server deletion or any
    // server-side change, or by a reset.
    bool visual_detach(std::int32_t signed_id) noexcept;
    void visual_retire_detached(std::int32_t signed_id) noexcept;
    // The renderer whose device owns detached texture copies. visual_detach
    // calls the retainer under the scene lock for every surface part and
    // rejects detachment when one generation has no resident renderer copy.
    // Clearing it (device teardown) retires every detached copy first.
    using VisualSurfaceTextureRetainer = std::shared_ptr<ID3D11Texture2D> (*)(
        void* context, const OwnedVisualSurface& surface) noexcept;
    void visual_set_surface_texture_retainer(
        VisualSurfaceTextureRetainer retainer, void* context) noexcept;
    void visual_clear_surface_texture_retainer(void* context) noexcept;
    std::shared_ptr<ID3D11Texture2D> visual_detached_surface_texture(
        std::uint64_t surface_id, std::uint64_t generation) const noexcept;
    bool visual_detached_surface_textures(
        std::vector<std::shared_ptr<ID3D11Texture2D>>& output) const noexcept;
    bool visual_has_detached() const noexcept {
        return detached_count_.load(std::memory_order_acquire) != 0;
    }
    // Render-only seeds from the local server's own object list (issue #59).
    // A seed is a detached copy built from server state for an ID the client
    // does not hold: it is inserted only while the ID has neither a live
    // member nor a detached copy, yields to a ready client incarnation exactly
    // like a window-exit copy, and is retired by the same deletion, change and
    // reset paths. It never enters membership, so it cannot feed selection,
    // interaction or simulation. Seed generations carry the high bit and so
    // never alias a lifecycle generation.
    bool visual_seed(std::int32_t signed_id,
        std::shared_ptr<const VisualAssemblyAsset> assembly,
        const VisualObjectPose& pose) noexcept;
    // `eligible` is the sorted set of IDs the local server would stream. When
    // `authoritative`, every detached copy (seed or window exit) outside it is
    // retired: the server deleted, hid or stopped streaming that object.
    // Otherwise (no local single-player server) only seeds are retired and
    // window-exit copies keep their existing behaviour. `missing` receives the
    // eligible IDs that have neither a live member nor a detached copy.
    void visual_reconcile_seeds(const std::vector<std::int32_t>& eligible,
        bool authoritative, std::vector<std::int32_t>& missing) noexcept;

    VisualResourceIncarnation visual_begin_resource(std::int32_t signed_id) noexcept;
    bool visual_finish_resource(VisualResourceIncarnation resource, bool success) noexcept;
    void visual_destroy_resource(std::int32_t signed_id) noexcept;
    VisualResourceIncarnation visual_resource(std::int32_t signed_id) const noexcept;
    bool visual_declare_mesh(VisualResourceIncarnation resource, bool present) noexcept;
    bool visual_claims_mesh(const LifecycleIdentity& identity) const noexcept;
    bool visual_bind_database(VisualResourceIncarnation resource,
        std::uint64_t source_generation,
        std::shared_ptr<const AnimationDatabaseAsset> database) noexcept;
    std::shared_ptr<const AnimationDatabaseAsset> visual_database(
        VisualResourceIncarnation resource, std::uint64_t& source_generation) const noexcept;
    bool visual_publish_assembly(VisualResourceIncarnation resource,
        std::shared_ptr<const VisualAssemblyAsset> assembly, bool weapons_replaced = false) noexcept;
    std::shared_ptr<const VisualAssemblyAsset> visual_assembly(
        VisualResourceIncarnation resource) const noexcept;
    bool visual_replace_surface(std::uint64_t id,
        std::shared_ptr<const OwnedVisualSurface> value) noexcept;
    // Entities whose drawn surface was destroyed under them (a native texture
    // reload recreates the surfaces) and are due a re-read of their live
    // appearance bindings at this world frame. Each is retried when another
    // surface is published, else after a doubling wait from 30 frames.
    bool visual_surface_rebind_candidates(std::uint64_t world_frame,
        std::vector<std::int32_t>& ids) noexcept;
    bool visual_replace_generated_texture(const std::string& source_name,
        std::shared_ptr<const TextureCpuAsset> value,
        TextureValueSource source = TextureValueSource::generated_memory) noexcept;
    bool visual_publish_route(VisualResourceIncarnation resource,
        std::shared_ptr<const CompiledVisualRoute> route) noexcept;
    bool visual_publish_intent(VisualResourceIncarnation resource,
        VisualPoseIntent intent, const std::array<float, 3>& root,
        const char** failure = nullptr) noexcept;
    bool visual_publish_root_rotation(VisualResourceIncarnation resource,
        const std::array<float, 4>& rotation, double time,
        const char** failure = nullptr) noexcept;
    bool visual_append_idle(VisualResourceIncarnation resource,
        std::uint64_t event, double time, VisualClipSelection selected,
        const char** failure = nullptr) noexcept;
    bool visual_publish_tick_values(VisualResourceIncarnation resource,
        const VisualTickValues& values, const char** failure = nullptr) noexcept;
    bool visual_publish_detection(VisualResourceIncarnation resource, bool detected) noexcept;
    // Render visibility at the scene walk: the detection bit the game's own
    // draw-list build reads in that same scene. Publishes only on a change.
    void visual_publish_scene_detection(std::int32_t signed_id, bool detected) noexcept;
    void visual_reject_detection(VisualResourceIncarnation resource, const char* predicate) noexcept;
    void visual_reject_detected_unit_detection(const char* predicate) noexcept;
    bool visual_tick_values(VisualResourceIncarnation resource,
        VisualTickValues& values) const noexcept;
    // Source values for a separate UI view; no world traversal or pose sampling.
    // The clip selection an object preview shows (view 0: character panel,
    // 1: inventory), published by the game process when the preview
    // re-anchors and recorded, so the host's replica draws the same pose.
    // Material compositions (material classes, S1): the latest layer list per
    // composite name. Kept across map changes: composites outlive maps.
    void material_publish_composition(const std::string& name, MaterialComposition value) noexcept;
    bool material_composition(const std::string& name, MaterialComposition& value) const noexcept;
    // Changes whenever any composition's layer list changes.
    std::uint64_t material_composition_revision() const noexcept;
    void visual_publish_preview(VisualResourceIncarnation resource, std::uint32_t view,
        const VisualClipSelection& selection) noexcept;
    bool visual_preview_selection(VisualResourceIncarnation resource, std::uint32_t view,
        VisualClipSelection& selection) const noexcept;
    bool visual_preview_source(VisualResourceIncarnation resource,
        RetainedVisualSample& sample, std::uint64_t& selection_revision) const noexcept;
    bool visual_publish_object_pose(VisualResourceIncarnation resource,
        const VisualObjectPose& values) noexcept;
    bool visual_object_publication_snapshot(std::int32_t signed_id,
        VisualObjectPublicationSnapshot& values) const noexcept;
    bool visual_publish_object_pose(const VisualObjectPublicationSnapshot& snapshot,
        const VisualObjectPose& values) noexcept;
    bool visual_reject_object_pose_if_current(
        const VisualObjectPublicationSnapshot& snapshot, RetainedVisualStatus reason,
        const char* predicate, const char* scope = "local_keep.object_pose") noexcept;
    bool visual_object_pose_current(VisualResourceIncarnation resource,
        const VisualObjectPose& values) const noexcept;
    bool visual_object_pose(VisualResourceIncarnation resource,
        VisualObjectPose& values) const noexcept;
    bool visual_publish_shape(VisualResourceIncarnation resource,
        const std::array<float,3>& shape) noexcept;
    bool visual_publish_forced_weapon_ids(const std::vector<std::int32_t>& ids) noexcept;
    bool visual_publish_bridges(const std::vector<VisualBridgePlane>& planes,
        const char** failure = nullptr) noexcept;
    bool visual_publish_quest_lights(std::vector<RetainedQuestLight> lights,
        const char** failure = nullptr) noexcept;
    void visual_publish_highlight(const RetainedHighlightState& state) noexcept;
    bool visual_publish_world_point_lights(std::vector<RetainedWorldPointLight> lights,
        const char** failure = nullptr) noexcept;
#ifdef EID3D11_TESTING
    std::uint64_t visual_bridge_index_rebuilds_for_test() const noexcept;
#endif
    void visual_reject(VisualResourceIncarnation resource, RetainedVisualStatus reason,
        const char* predicate = nullptr, const char* scope = "local_clear") noexcept;
    void visual_reject_all(RetainedVisualStatus reason, const char* predicate = nullptr,
        const char* scope = "global_clear") noexcept;
    bool visual_commit_tick(double time, const char** failure = nullptr,
        std::uint64_t* committed_entities = nullptr) noexcept;
    RetainedVisualFrame visual_frame(double time, bool diagnose = false) const;
    std::shared_ptr<const RetainedVisualFrame> visual_frame_value(double time, bool diagnose = false) const;
    void release_visual_frame_scratch() noexcept;
    RetainedShadowSourceChanges shadow_source_changes(std::uint64_t since) const;
    RetainedShadowSourceChanges shadow_source_changes(RetainedShadowConsumer consumer);
    // The same read without the time/wave activity lists (value consumers).
    RetainedShadowSourceChanges visual_source_changes(RetainedShadowConsumer consumer);
    void acknowledge_shadow_source_changes(RetainedShadowConsumer consumer,
        std::uint64_t through) noexcept;
    std::uint64_t shadow_source_revision() const noexcept;
    std::shared_ptr<const RetainedShadowInputDomain> shadow_input_domain() const noexcept;
    bool publish_shadow_input_domain(std::shared_ptr<const RetainedShadowInputDomain> value) noexcept;
#ifdef EID3D11_TESTING
    void acknowledge_shadow_source_changes(std::uint64_t through) noexcept;
    std::uint64_t visual_frame_evaluations() const noexcept { return visual_frame_evaluations_; }
    std::uint64_t visual_dependency_rebuilds() const noexcept { return visual_dependency_rebuilds_; }
    std::uint64_t visual_join_rebuilds() const noexcept { return visual_join_rebuilds_; }
    std::uint64_t shadow_terrain_activity_rebuilds_for_test() const noexcept {
        return shadow_terrain_activity_rebuilds_;
    }
#endif

    RetainedStaticSceneSnapshot snapshot() const;
    RetainedTerrainSourceSnapshot terrain_source_snapshot() const noexcept;
    RetainedTerrainSourceIdentity terrain_source_identity() const noexcept;
    bool publish_terrain_geometry(std::uint64_t source_generation,
        std::uint64_t source_revision, const OwnedTerrainGeometryValues* values) noexcept;
    bool publish_terrain_texture(std::uint64_t source_generation,
        std::uint64_t source_revision, std::uint32_t material,
        std::shared_ptr<const OwnedVisualSurface> surface) noexcept;
    RetainedStaticBindingView binding_view() const noexcept;
#ifdef EID3D11_TESTING
    std::shared_ptr<const OwnedMapSnapshot> published_source_for_test(MobSourceRole role) const noexcept;
    bool write_available_for_test() const noexcept {
        if (!TryAcquireSRWLockExclusive(&lock_)) return false;
        ReleaseSRWLockExclusive(&lock_);
        return true;
    }
#endif
    RetainedStaticFrameSnapshot frame_snapshot() const noexcept;
    RetainedStaticInstrumentation instrumentation() const noexcept;
    bool binding(std::int32_t signed_id,
                 RetainedStaticBinding& result) const noexcept;
    RetainedSceneIdentitySnapshot identity_snapshot() const;

    // Publication stream (retained_publication.h). Attach before the first
    // mutation; the recorder outlives the scene's use. Null: standard mode.
    void set_publication_recorder(RetainedPublicationRecorder* recorder) noexcept {
        recorder_.store(recorder, std::memory_order_release);
    }
    // The x64 host's replica: visual_detach keeps no renderer texture copy
    // (the replica draws nothing yet; S3 decides pixel ownership there).
    void set_replica() noexcept { replica_ = true; }

private:
    friend class RetainedStaticBindingView;
    struct VisualEntity {
        VisualResourceIncarnation resource{};
        std::uint64_t excluded_membership_generation = 0;
        std::uint64_t resolver_generation = 0;
        std::shared_ptr<const AnimationDatabaseAsset> database;
        std::shared_ptr<const VisualAssemblyAsset> assembly;
        std::shared_ptr<VisualPreparedResources> prepared;
        std::shared_ptr<const CompiledVisualRoute> route;
        VisualPoseIntent intent;
        VisualPoseState anchor{};
        bool height_valid = true;
        VisualEffectState effects{};
        bool effects_anchored = false;
        VisualTickValues tick{};
        bool detection_unavailable = false;
        VisualObjectPose object_pose{};
        bool has_object_pose = false;
        bool mesh_declared = false;
        std::vector<VisualBridgePlane> bridge_planes;
        std::uint64_t last_idle_event = 0;
        std::uint64_t pose_revision = 0;
        float envelope_radius = 0;
        RetainedVisualStatus status = RetainedVisualStatus::constructing;
        const char* readiness_reason = "construction";
        const char* failure_scope = nullptr;
        bool constructed = false;
        bool source_available = true;
        // A drawn surface was retired with no replacement value: the part
        // waits for visual_surface_rebind_candidates to re-read its binding.
        bool surface_lost = false;
        std::uint64_t surface_rebind_serial = 0, surface_rebind_frame = 0;
        std::uint32_t surface_rebind_attempts = 0;
        // Scratch ordinals name this record only during the current value
        // evaluation. Target lookup still uses the existing resource owner.
        mutable std::uint64_t frame_serial = 0;
        mutable std::size_t frame_ordinal = 0;
        mutable std::size_t frame_unit_ordinal = 0;
        // This frame's root can serve as a target; this frame's pose is evaluated.
        mutable bool frame_root_valid = false;
        mutable bool frame_pose_current = false;
    };
    struct DetachedSurfaceTexture {
        std::uint64_t surface_id = 0;
        std::uint64_t generation = 0;
        std::shared_ptr<ID3D11Texture2D> texture;
    };
    struct DetachedVisual {
        LifecycleIdentity lifecycle{};
        VisualEntity entity;
        // Renderer copies of the client surfaces destroyed with the object.
        std::vector<DetachedSurfaceTexture> textures;
        bool seeded = false;  // built from local-server state (visual_seed)
    };
    struct VisualFrameJoin {
        LifecycleIdentity lifecycle{};
        const VisualEntity* entity = nullptr;
        const DetachedVisual* detached = nullptr;
    };
    VisualEntity* visual_find_unlocked(VisualResourceIncarnation resource) noexcept;
    const VisualEntity* visual_find_unlocked(VisualResourceIncarnation resource) const noexcept;
    void visual_refresh_status(VisualEntity& entity, bool invalidate = true) noexcept;
    bool visual_refresh_envelope(VisualEntity& entity, bool invalidate = true) noexcept;
    bool visual_publish_object_pose_unlocked(const VisualObjectPublicationSnapshot& snapshot,
        const VisualObjectPose& values, bool invalidate, bool& changed) noexcept;
    bool visual_object_publication_current_unlocked(const VisualEntity& entity,
        const VisualObjectPublicationSnapshot& snapshot) const noexcept;
    void visual_reject_unlocked(VisualEntity& entity, RetainedVisualStatus reason,
        const char* predicate, const char* scope, bool invalidate) noexcept;
    void visual_reject_detection_unlocked(VisualEntity& entity, const char* predicate) noexcept;
    void visual_record_failure_unlocked(VisualEntity& entity, const char* predicate,
        const char* scope, bool invalidate = true) noexcept;
    void visual_structure_changed_unlocked() noexcept {
        if(++visual_structure_revision_==0)++visual_structure_revision_;
        visual_frame_join_revision_=0;
        visual_frame_joins_.clear();
    }
    void visual_frame_changed_unlocked() noexcept {
        if (++visual_value_revision_==0) ++visual_value_revision_;
        visual_frame_key_={};
        if(visual_frame_cache_){
            if(visual_frame_cache_.use_count()==1)visual_frame_cache_->entities.clear();
            else visual_frame_cache_.reset();
        }
    }
    void visual_values_changed_unlocked(std::int32_t shadow_id = 0) noexcept {
        visual_frame_changed_unlocked();
        shadow_source_changed_unlocked(shadow_id);
    }
    void shadow_source_changed_unlocked(std::int32_t signed_id) noexcept;
    void shadow_terrain_changed_unlocked(bool time_only = false) noexcept;
    void shadow_refresh_time_activity_unlocked(std::int32_t signed_id) noexcept;
    void shadow_refresh_terrain_activity_unlocked() noexcept;
    std::array<std::uint64_t,5> visual_frame_key_unlocked() const noexcept {
        return {visual_value_revision_,source_generation_,source_revision_,binding_revision_,positive_epoch_};
    }
    void visual_frame_unlocked(double time, bool diagnose,RetainedVisualFrame& result) const;
    std::shared_ptr<const RetainedVisualFrame> visual_frame_value_unlocked(double time,bool diagnose) const;
    struct Transaction {
        std::uint64_t serial = 0;
        MobSourceRole role = MobSourceRole::main;
        std::string outer_name;
        std::string mob_name;
        std::vector<RetainedAdmission> admissions;
        bool engine_finished = false;
        bool engine_success = false;
        bool source_confirmed = false;
        bool mob_preselected = false;
        bool reuse_published_main = false;
        bool discarded = false;
    };

    struct Source {
        MobSourceRole role = MobSourceRole::main;
        std::uint64_t transition_serial = 0;
        // The successful main publication's existing scene revision. Copies
        // and exact-main reuse preserve it; transition_serial follows selection.
        std::uint64_t publication_revision = 0;
        std::string outer_name;
        std::string name;
        std::vector<RetainedAdmission> admissions;
        std::shared_ptr<const OwnedMapSnapshot> snapshot;
        std::shared_ptr<const OwnedTerrainGeometryValues> terrain_geometry;
        std::shared_ptr<OwnedTerrainPreparedResources> terrain_prepared;
        std::shared_ptr<const RetainedTerrainTextures> terrain_textures;
        std::uint64_t terrain_geometry_serial = 0;
        std::vector<std::shared_ptr<const RetainedStaticSourceCandidate>>
            candidates;
    };

    using SourceIndex = std::map<
        std::int32_t, std::shared_ptr<const RetainedStaticSourceCandidate>>;
    // A live signed ID names exactly one lifecycle identity. Keep this as the
    // sole authoritative binding container so the early visibility selector
    // is an average O(1) lookup rather than a parallel claim cache.
    using BindingMap =
        std::unordered_map<std::int32_t, RetainedStaticBinding>;

    bool replace_sources(std::vector<Source>&& sources,
                         bool source_revision_changed,
                         const RetainedStaticPrepareDiagnostic* prepare,
                         std::uint64_t object_visits,
                         std::uint64_t candidate_preparations,
                         bool publication) noexcept;
    static bool build_source_index(
        const std::vector<Source>& sources, SourceIndex& index,
        bool& collision_free, std::uint64_t& candidate_visits) noexcept;
    static bool build_bindings(
        const SourceIndex& index, bool collision_free,
        const std::map<std::int32_t, LifecycleIdentity>& live,
        std::uint64_t positive_epoch, BindingMap& bindings,
        std::map<std::uintptr_t, std::size_t>& geometry_refs) noexcept;
    void lifecycle_removed_unlocked(std::int32_t signed_id) noexcept;
    void detached_changed_unlocked() noexcept;
    void retire_replaced_detached_unlocked(std::int32_t signed_id) noexcept;
    void retire_all_detached_unlocked() noexcept;
    std::map<std::int32_t, DetachedVisual>::iterator retire_detached_unlocked(
        std::map<std::int32_t, DetachedVisual>::iterator copy) noexcept;
    void retire_detached_using_unlocked(const std::function<bool(const VisualPartAsset&)>& uses) noexcept;
    bool retire_binding(const LifecycleIdentity& identity) noexcept;
    bool insert_binding(const LifecycleIdentity& identity) noexcept;
    void advance_binding_revision() noexcept;
    RetainedStaticFrameSnapshot frame_snapshot_unlocked() const noexcept;
    bool binding_unlocked(std::int32_t signed_id,
                          RetainedStaticBinding& result) const noexcept;

    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::uint64_t next_transaction_ = 0;
    std::uint64_t source_generation_ = 0;
    std::uint64_t source_revision_ = 0;
    std::uint64_t binding_revision_ = 0;
    std::uint64_t positive_epoch_ = 0;
    std::uint64_t selected_main_transition_ = 0;
    std::map<std::uint64_t, Transaction> transactions_;
    std::vector<Source> sources_;
    SourceIndex source_index_;
    std::map<std::int32_t, LifecycleIdentity> live_;
    // Resource creation precedes world admission and can survive world removal.
    // Membership remains solely in live_; this map owns resource incarnations.
    std::map<std::int32_t, VisualEntity> visuals_;
    // Surface publications so far, and whether any entity is surface_lost.
    std::uint64_t surface_publish_serial_ = 0;
    bool surface_lost_any_ = false;
    // Whether visual_publish_forced_weapon_ids may have left any entity
    // forced_hidden (its only writer); false lets an empty list skip the walk.
    bool forced_weapon_hidden_any_ = false;
    // Window-exited non-unit visuals, outside membership (see visual_detach).
    std::map<std::int32_t, DetachedVisual> detached_;
    std::atomic<std::size_t> detached_count_{0};
    VisualSurfaceTextureRetainer surface_texture_retainer_ = nullptr;
    void* surface_texture_retainer_context_ = nullptr;
    std::uint64_t next_seed_generation_ = 0;
    std::uint64_t next_visual_incarnation_ = 0;
    double visual_tick_time_ = -1;
    std::uint64_t visual_value_revision_ = 1;
    std::uint64_t visual_structure_revision_ = 1;
    RetainedShadowChangeJournal shadow_changes_;
    std::set<std::int32_t> shadow_time_active_visuals_;
    std::set<std::uint32_t> shadow_time_active_terrain_sectors_;
    std::set<std::uint32_t> shadow_wave_active_terrain_sectors_;
    std::shared_ptr<const RetainedShadowInputDomain> shadow_input_domain_;
    bool shadow_tracking_failed_ = false;
    mutable std::shared_ptr<RetainedVisualFrame> visual_frame_cache_;
    mutable std::shared_ptr<RetainedVisualFrame> visual_frame_spare_;
    mutable std::array<std::uint64_t,5> visual_frame_key_{};
    mutable std::uint64_t visual_frame_serial_ = 0;
    mutable std::uint64_t visual_frame_join_revision_ = 0;
    mutable bool visual_frame_graph_valid_ = false;
    mutable std::vector<VisualFrameJoin> visual_frame_joins_;
    mutable std::vector<VisualRouteSample> visual_frame_roots_;
    mutable std::vector<const VisualEntity*> visual_frame_records_;
    mutable std::vector<std::size_t> visual_frame_unit_ordinals_;
    mutable std::vector<std::size_t> visual_frame_dependencies_,visual_frame_order_,visual_frame_chain_;
    mutable std::vector<std::uint8_t> visual_frame_visited_;
#ifdef EID3D11_TESTING
    mutable std::uint64_t visual_frame_evaluations_ = 0;
    mutable std::uint64_t visual_dependency_rebuilds_ = 0;
    mutable std::uint64_t visual_join_rebuilds_ = 0;
    std::uint64_t shadow_terrain_activity_rebuilds_ = 0;
#endif
    // Same lifetime as the existing process-capped first-failure event.
    OwnedVisualRejection visual_first_failure_{};
    std::map<std::pair<std::int32_t, std::int32_t>,
        std::vector<std::pair<std::int32_t,std::uint32_t>>> bridge_cells_;
    // Exact bytes from the last accepted completed bridge publication. Resource
    // retirement marks the index dirty, so this fast path never survives an
    // owner/incarnation change.
    std::vector<VisualBridgePlane> accepted_bridge_planes_;
    std::vector<RetainedQuestLight> quest_lights_;
    RetainedHighlightState highlight_;
    std::vector<RetainedWorldPointLight> world_point_lights_;
    bool bridge_index_dirty_ = false;
#ifdef EID3D11_TESTING
    std::uint64_t bridge_index_rebuilds_ = 0;
#endif
    BindingMap bindings_;
    std::map<std::uintptr_t, std::size_t> geometry_refs_;
    bool collision_free_ = true;
    RetainedStaticPrepareDiagnostic prepare_diagnostic_{};
    mutable RetainedStaticInstrumentation instrumentation_{};
    std::atomic<RetainedPublicationRecorder*> recorder_{nullptr};
    bool replica_ = false;
    // visual_publish_preview: per (resource id, view) the incarnation and selection.
    std::unordered_map<std::uint64_t, std::pair<VisualResourceIncarnation, VisualClipSelection>> preview_selections_;
    std::unordered_map<std::string, MaterialComposition> material_compositions_;
    std::uint64_t material_composition_serial_ = 0;
    RetainedPublicationRecorder* recorder() const noexcept {
        return recorder_.load(std::memory_order_relaxed);
    }
};

} // namespace owned
