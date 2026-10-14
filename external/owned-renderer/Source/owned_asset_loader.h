#pragma once
#include <functional>

#include "engine_bridge.h"
#include "owned_resource_store.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace owned {

struct MobChunkRecord {
    std::uint32_t id = 0;
    std::uint32_t total_bytes = 0;
    std::uint32_t payload_offset = 0;
    std::uint32_t payload_bytes = 0;
    std::vector<MobChunkRecord> children;
};

struct MobDocument {
    ResourceIdentity identity;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    std::vector<MobChunkRecord> roots;
};

enum class StaticObjectRejection : std::uint8_t {
    none,
    malformed,
    duplicate_nid,
    unknown_field,
    script_mutable,
    hierarchical,
    multipart,
    missing_visual,
    missing_texture,
    outside_asset_slice,
    animated_asset,
    asset_failure,
};

const char* static_object_rejection_name(StaticObjectRejection reason) noexcept;

struct StaticObjectSource {
    std::uint32_t nid = 0;
    std::uint32_t family = 0;
    std::string name;
    std::string prototype_reference;
    std::uint32_t prototype_nid = 0;
    // ASCII-folded asset lookup keys, canonicalized at MOB parsing. Original
    // spelling remains in MobDocument::bytes; display/gameplay names are separate.
    std::string visual_reference;
    std::string primary_texture;
    std::string secondary_texture;
    std::array<float, 3> position{};
    std::array<float, 4> quaternion{};
    // Semantic strength,grace,tall shape/complection selector. Raw MOB B00C
    // stores grace,strength,tall and is converted once by the loader. This is
    // deliberately not a world-scale transform: it selects eight FIG/BON
    // endpoints.
    std::array<float, 3> shape{};
    std::uint8_t owner = 0;
    std::uint8_t quest = 0;
    std::uint8_t shadow = 0;
    // MOB B00D body-part list, ASCII-folded. Non-empty names the model nodes
    // the object draws; empty draws every node (the engine's own rule).
    std::vector<std::string> part_names;
    std::vector<std::uint32_t> unknown_field_ids;
    StaticObjectRejection rejection = StaticObjectRejection::none;
};

// Exact raw signature stored at the start of an eight-shape FIG resource.
constexpr std::uint32_t kFigureSignature8 = 0x38474946u;

// Maps the established FIG texture type IDs onto the corresponding MOB
// texture field. Empty fields and unsupported IDs fail closed.
const std::string* figure_texture_for_type(
    const StaticObjectSource& source, std::uint32_t type_id) noexcept;

struct FigureMorphComponent {
    std::uint16_t position = 0;
    std::uint16_t normal = 0;
};

struct FigureBounds {
    std::array<float, 3> center{};
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
    float radius = 0.0f;
};

struct FigureCpuAsset {
    ResourceIdentity identity;
    std::uint32_t signature = 0;
    std::uint32_t shape_count = 0;
    std::uint32_t position_groups = 0;
    std::uint32_t normal_groups = 0;
    std::uint32_t material_group = 0;
    std::uint32_t texture_number = 0;
    std::vector<FigureBounds> bounds;
    // Computed once from decoded XYZ, independent of potentially narrower
    // authored selection boxes. Absolute shape weights also cover extrapolation.
    std::array<float, 8> shape_position_radii{};
    // Positions are shape-major renderer-native groups. FIG8 normals are one
    // immutable group stream shared by every shape endpoint. Each group has
    // four lanes and a synthesized homogeneous W=1; source XYZ values remain
    // untransformed LH.
    std::vector<NativePackedGroup> positions;
    std::vector<NativePackedGroup> normals;
    std::vector<NativeTextureCoordinate> texture_coordinates;
    std::vector<std::uint16_t> indices;
    std::vector<NativeVertexComponent> components;
    std::vector<FigureMorphComponent> morph_components;
    std::vector<std::uint8_t> user_data;
};

struct ModelLink {
    std::string child;
    std::string parent;
    // Effective shipped-loader link: UINT_MAX for a declared root or when the
    // named parent occurs later and therefore was not linked at insertion.
    std::uint32_t parent_index = UINT_MAX;
};

// Whether a completed game node is linked as the shipped loader links it:
// 50CC60 attaches a node only to a parent added before it (a later parent
// leaves it unlinked), and 50D2E0 makes the first node with no parent name
// the head. node/head/game_parent are the game's node, its head and the
// parent the game linked (null when unlinked); recipe_parent is the game node
// at recipe.parent_index. Dejavu stbuho4/stst44 and HDPack nafltr75 list
// children before their parent.
bool shipped_node_link_matches(const ModelLink& recipe, std::size_t index,
    const void* node, const void* head, const void* game_parent,
    const void* recipe_parent) noexcept;

struct ModelNodeCpuAsset {
    std::string name;
    // Skeleton-only loads leave geometry empty. A semantic part selects its
    // own FIG resource; a joint need not have any drawable FIG at all.
    FigureCpuAsset figure;
    // Three floats per shape endpoint, in source LH coordinates.
    std::vector<float> bone_positions;
    // Missing BON endpoints, or a linked ancestor without them, leave this
    // node's transform unavailable. Keep its LNK index; omit attached visuals.
    bool transform_available = true;
};

struct ModelCpuAsset {
    std::string visual_reference;
    ResourceIdentity model_container;
    ResourceIdentity bone_container;
    std::vector<ModelLink> links;
    std::vector<ModelNodeCpuAsset> nodes;
    bool static_shape_parameterized = false;
    std::uint64_t source_bytes = 0;
};

// Immutable animation resources. Selection and phase belong to the scene
// entity; these values can be shared by color and light-space consumers.
struct AnimationEntryAsset {
    std::string name;
    std::uint32_t ordinal = 0;
    std::uint32_t flags = 0;
    std::uint32_t special_flags = 0;
    std::uint32_t length = 0;
    float speed = 0.0f;
    std::array<std::uint32_t, 2> hide_frames{};
};

// Opaque immutable ANM owner. A decoded ADB remembers only a weak alias to
// the exact library selected for that database incarnation; clip selections
// remain the strong owners, so this backlink cannot extend resource lifetime.
struct OwnedAnimationLibraryOwner;

struct AnimationLibraryBinding {
    ResourceIdentity database_identity;
    std::weak_ptr<OwnedAnimationLibraryOwner> owner;
    std::uint64_t source_generation = 0;
};

struct AnimationDatabaseAsset {
    ResourceIdentity identity;
    std::array<float, 3> heights{};
    std::vector<AnimationEntryAsset> entries;
    mutable AnimationLibraryBinding library_binding;
};

struct AnimationTrackAsset {
    ResourceIdentity identity;
    // Quaternions retain the resource's w,x,y,z order.
    std::vector<std::array<float, 4>> rotations;
    std::vector<std::array<float, 3>> translations;
    std::uint32_t morph_frames = 0;
    std::uint32_t morph_points = 0;
    std::vector<std::array<float, 3>> morphs;
    float translation_radius = 0.0f;
    float morph_radius = 0.0f;
    // Includes the maximum interpolated quaternion norm squared. Rotating by
    // q*p*conjugate(q) scales a vector by this value when q is not exactly unit.
    float rotation_scale_bound = 1.0f;
};

// Resource selection folds ASCII names; LNK display names retain their spelling.
// The lookup uses that same rule without allocating a lowercase string per node.
struct AnimationNodeNameLess {
    bool operator()(const std::string& a, const std::string& b) const noexcept {
        const auto count = a.size() < b.size() ? a.size() : b.size();
        for (std::size_t i = 0; i < count; ++i) {
            auto x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[i]);
            if (x >= 'A' && x <= 'Z') x += 'a'-'A';
            if (y >= 'A' && y <= 'Z') y += 'a'-'A';
            if (x != y) return x < y;
        }
        return a.size() < b.size();
    }
};

struct AnimationClipAsset {
    ResourceIdentity identity;
    // Reservation for decoded frames and retained index/key metadata. Empty
    // storage charges the existing CPU resource budget without another copy.
    std::shared_ptr<const std::vector<std::uint8_t>> byte_reservation;
    std::map<std::string, AnimationTrackAsset, AnimationNodeNameLess> tracks;
};

enum class MmpFormat : std::uint8_t {
    dxt1,
    dxt3,
    rgb565,
    argb1555,
    argb4444,
    argb8888,
};

// The two immutable texture producers share this alpha-only inspection. Packed
// formats use the uploader's alpha mask/key; compressed formats use encoded alpha.
// cut_bits: one bit per texel, row-major, LSB first; a set bit is a cut texel.
enum class TextureAlphaFormat : std::uint8_t { packed, bc1, bc2, bc3, cut_bits };
struct TextureAlphaPixels {
    const std::uint8_t* data = nullptr;
    std::size_t bytes = 0;
    std::uint32_t width = 0, height = 0, pitch = 0;
    TextureAlphaFormat format = TextureAlphaFormat::packed;
    std::uint32_t pixel_bytes = 0, alpha_mask = 0;
    bool has_color_key = false;
    std::uint32_t key_low = 0, key_high = 0;
};
bool texture_alpha_fully_opaque(const TextureAlphaPixels& source) noexcept;
// One texel's alpha under the same rule: true when it is not fully opaque.
bool texture_alpha_texel_cut(const TextureAlphaPixels& source,
                             std::uint32_t x, std::uint32_t y) noexcept;
// Alpha description of a completed native surface's bytes at publication.
bool visual_surface_raw_alpha(const OwnedVisualSurface& surface,
                              const std::uint8_t* data, std::size_t bytes,
                              TextureAlphaPixels& alpha) noexcept;
// Compact per-texel cut mask (TextureAlphaFormat::cut_bits) of a raw source.
std::shared_ptr<const std::vector<std::uint8_t>> texture_alpha_cut_mask(
    const TextureAlphaPixels& source);
// Alpha description of a published surface: its cut mask, or no cut texels.
bool visual_surface_alpha_pixels(const OwnedVisualSurface& surface,
                                 TextureAlphaPixels& alpha) noexcept;
#ifdef EID3D11_TESTING
std::uint64_t texture_alpha_inspections() noexcept;
#endif

struct MmpMipLevel {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t byte_offset = 0;
    std::uint32_t bytes = 0;
};

enum class TextureValueSource : std::uint8_t { archive, generated_memory, generated_disk };

struct TextureCpuAsset {
    TextureValueSource value_source = TextureValueSource::archive;
    // Generated MMP values have their own immutable value serial and exact
    // source name. They are neither archive members nor proxy surfaces.
    std::uint64_t value_serial = 0;
    std::string source_name;
    ResourceIdentity identity;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t bit_count = 0;
    std::uint32_t format_signature = 0;
    MmpFormat format = MmpFormat::dxt1;
    std::array<std::uint32_t, 4> masks{};
    std::array<std::uint32_t, 4> shifts{};
    std::array<std::uint32_t, 4> channel_bits{};
    std::vector<MmpMipLevel> mips;
    // Bit i proves alpha1 throughout mip i, including every sampled texel.
    std::uint32_t opaque_mips = 0;
    // Immutable upload bytes retain exact source row/block order for native
    // formats. PNT3 is expanded once into fresh-zero BGRA pixels.
    std::shared_ptr<const std::vector<std::uint8_t>> source;
    // Lifetime-matched accounting for the retained mip descriptors and copied
    // generated-user-data vectors. It never duplicates the source byte charge.
    std::shared_ptr<const std::vector<std::uint8_t>> byte_reservation;
    std::uint32_t texel_offset = 76;
    std::vector<std::uint8_t> user_data;
};
// Alpha description of one immutable texture mip.
bool texture_alpha_pixels(const TextureCpuAsset& texture, std::size_t mip,
                          TextureAlphaPixels& alpha) noexcept;

enum class MaterialValueSource : std::uint8_t { archive, completed_native };

struct MaterialCpuAsset {
    // A completed native material is a value in its assembly revision. It
    // has no fabricated archive identity and retains no engine address.
    MaterialValueSource source = MaterialValueSource::archive;
    ResourceIdentity identity;
    std::array<float, 4> diffuse{};
    std::array<float, 4> ambient{};
    std::array<float, 4> specular{};
    std::array<float, 4> emissive{};
    // The native highlight stack retains the emissive value before its latest
    // full-white override. Selected and quest-lit unit color consumes it.
    std::array<float, 4> previous_highlight_emissive{};
    bool has_previous_highlight_emissive = false;
    // First saved value in the completed native deque; needed when an attached
    // quest light and selection have independently pushed white overrides.
    std::array<float, 4> oldest_highlight_emissive{};
    bool has_oldest_highlight_emissive = false;
    float power = 0;
};

struct TerrainVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float normal_x = 0.0f;
    float normal_y = 0.0f;
    float normal_z = 1.0f;
    // Layer vertices retain both proved source associations. Geometry XY uses
    // the lowest-wind adjacent object, while lighting/animation metadata uses
    // the first present adjacent object. Base vertices use UINT16_MAX.
    std::uint16_t geometry_object = UINT16_MAX;
    std::uint16_t lighting_object = UINT16_MAX;
    float depth = 0.0f;
};

struct TerrainBounds {
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
};

struct TerrainObjectDescriptor {
    std::uint32_t type = 0;
    std::array<float, 3> color{};
    float opacity = 1.0f;
    float self_illumination = 0.0f;
    float wave_multiplier = 0.0f;
    float warp_speed = 0.0f;
};

struct TerrainTileDescriptor {
    std::uint16_t tile_index = 0;
    std::uint16_t material_index = 0;
    std::uint16_t object_id = UINT16_MAX;
    std::uint8_t rotation = 0;
    std::uint8_t subpart = 0;
    std::uint8_t edge_mask = 0;
    bool transparent = false;
    std::uint32_t animation_ordinal = UINT_MAX;
    std::uint32_t animation_phases = 0;
    std::array<std::uint16_t, 9> vertex_indices{};
    // Lane UVs are not stored: terrain_tile_uvs() derives them exactly from
    // tile_index, rotation and the map tile grid.
};

struct TerrainTileRangeDescriptor {
    std::uint16_t tile_begin = 0;
    std::uint16_t tile_count = 0;
    std::uint32_t required_animation_phases = 0;
    // [lod][stitch flags: bit 0 = LU higher, bit 1 = RD higher].
    std::array<std::array<std::uint16_t, 4>, 3> index_counts{};
    // Cold render envelopes, separate from the game's sector clip boxes.
    // Layer Z follows the first-present (lighting) object selector. Grouping
    // that selector lets a height change update bounds without a vertex scan.
    struct Envelope {
        TerrainBounds bounds{};
        std::uint16_t layer_object = UINT16_MAX;
        float maximum_wave_multiplier = 0;
    };
    std::vector<Envelope> envelopes;
    // Base colour depends on these shifts; base depth geometry does not.
    std::vector<std::uint16_t> liquid_depth_objects;
};

struct TerrainMaterialDescriptor {
    std::array<TerrainTileRangeDescriptor, 4> subparts{};
};

struct TerrainSectorCpuAsset {
    ResourceIdentity identity;
    std::uint32_t map_x = 0;
    std::uint32_t map_y = 0;
    bool has_layers = false;
    bool has_layer_tiles = false;
    float average_z = 0.0f;
    TerrainBounds bounds{};
    std::array<TerrainBounds, 4> subpart_bounds{};
    std::vector<TerrainVertex> vertices;
    // Immutable draw records in material-major, subpart-major order. Within a
    // material/subpart range they retain exact source order (base row-major,
    // then present layer row-major).
    std::vector<TerrainTileDescriptor> tiles;
    // Base tile id by row-major tile position (UINT16_MAX = none), so zone-grid
    // neighbour queries need no scan of the material-ordered records.
    std::array<std::uint16_t, 256> base_tile_ids = [] {
        std::array<std::uint16_t, 256> none{};
        none.fill(UINT16_MAX);
        return none;
    }();
    // Dense material lookup. Empty materials/subparts have valid empty ranges.
    std::vector<TerrainMaterialDescriptor> materials;
    // Immutable decode result. Runtime dynamic capture uses this O(1) value
    // instead of rescanning tile descriptors every frame.
    std::uint32_t animation_count = 0;
    // Per base vertex (1089): owned_terrain_slope_normal and _facet_flatness,
    // computed by the loader worker for a terrain nothing edits after load
    // (the active map). Empty: computed where they are read.
    std::vector<std::uint32_t> base_slope_normals, base_facet_flatness;
};

// Selected terrain page. Only its upload payload is mutable after publication;
// the render thread releases it after upload and rereads the same selection if
// the array slice needs recreation. ResourceLocation holds no strong byte copy.
struct TerrainTexturePage {
    std::shared_ptr<ResourceStore> store;
    ResourceLocation location;
    mutable std::uint32_t mip_count = 0;
    mutable std::shared_ptr<const TextureCpuAsset> texture;
    mutable std::vector<NativeOwnedTextureMip> mips;

    bool ensure_upload_bytes(ResourceFailure& failure, bool require_bc1_chain = true) const;
    void release_upload_bytes() const noexcept;
};

// Immutable base-terrain geometry decoded from the active outer MPR. MOB Z is
// an above-terrain offset; this source-owned surface supplies its authoritative
// ground height without consulting live object pointers or legacy Draw.
struct ProceduralFallMap; // procedural_falls.h

// Visible liquid surface heights of an adjoining map just beyond a terrain's
// edges (rest heights; NaN where it has none), in this terrain's vertex grid:
// (verts_x + 2 reach) x (verts_y + 2 reach), cell (x+reach, y+reach). Only
// cells outside the terrain are read, so liquid flow continues across a
// shared map edge (owned_terrain_set_outside_liquid).
// The adjoining map's liquid flow at the vertices both maps share, per side
// of this terrain (0 y=0, 1 y=max, 2 x=0, 3 x=max; one entry a vertex along
// the side): this terrain's flow blends to it over kTerrainEdgeFlowBand
// vertices, so both maps carry identical flow on their shared edge.
struct TerrainEdgeFlow {
    float current[2]{}; float bend = 0, cascade = 0, impact = 0, drop = 0, fill = 0;
    bool valid = false;
};
struct TerrainOutsideLiquid {
    int reach = 0;
    std::vector<float> heights;
    std::array<std::vector<TerrainEdgeFlow>, 4> edge_flow;
};
struct TerrainCpuAsset {
    ResourceIdentity identity;
    std::string project_resource;
    std::string map_header_resource;
    float maximum_z = 0.0f;
    std::uint32_t sector_count_x = 0;
    std::uint32_t sector_count_y = 0;
    std::uint32_t texture_count = 0;
    std::uint32_t texture_size = 0;
    std::uint32_t tile_count = 0;
    std::uint32_t tile_size = 0;
    std::uint32_t tiles_per_texture_side = 0;
    std::uint32_t tiles_per_texture = 0;
    std::vector<std::string> texture_resources;
    std::vector<TerrainObjectDescriptor> objects;
    std::vector<std::uint32_t> tile_types;
    std::vector<std::uint32_t> tile_animation_phases;
    std::vector<TerrainSectorCpuAsset> sectors;
    // Authored BC1 base pages by material, with their shipped mip chains,
    // filled by the map loader (off the render thread) through the active
    // [RES] priority. Empty entries mean the page was not available.
    std::vector<TerrainTexturePage> pages;
    // Per square-atlas tile slot: the algae share of a swamp frame's art (1
    // for a pure swamp frame; less where the frame has open water painted
    // in, the shipped swamp/water transition frames), filled once by the map
    // loader from the pages (owned_terrain_classify_swamp_frames). Empty: no
    // classification, every swamp frame counts as pure.
    std::vector<float> swamp_frame_cover;
    // The map swamp frame: the most placed pure swamp frame's level-0 art
    // (RGBA 0..1, swamp_frame_side squared, power of two), for liquid tiles
    // with no same-page pure swamp frame to borrow. Empty: none.
    std::vector<std::array<float,4>> swamp_frame_texels;
    std::uint32_t swamp_frame_side = 0;
    // Set exactly once after the complete immutable descriptor has passed its
    // decode-boundary validation. Frame preparation checks these fields in
    // O(1); it never memoizes validation by address or rescans source arrays.
    bool immutable_descriptor_validated = false;
    // Procedural falls of the authored rest liquid (procedural_falls.h),
    // derived once from this immutable asset on first use.
    mutable std::shared_ptr<const ProceduralFallMap> procedural_falls;
    TerrainOutsideLiquid outside_liquid;
    std::uint32_t immutable_descriptor_validation_count = 0;
#ifdef EID3D11_TESTING
    // Decode-boundary proof only; absent from production builds.
    std::uint64_t descriptor_classification_visits = 0;
#endif
};

inline bool terrain_liquid_object(std::uint32_t type) noexcept {
    return type == 2u || type == 3u;
}

// A liquid layer tile is swamp when its own tile type (the .mp tile-type
// table, ETERRTYPE 14) says so, whatever the colour of its liquid object:
// Dejavu paints swamp tiles onto the blue water object (zone9 object 0).
constexpr std::uint32_t kTerrainTypeSwamp = 14u;
inline bool terrain_swamp_tile(const TerrainCpuAsset& source,
    const TerrainTileDescriptor& tile) noexcept {
    return tile.tile_index < source.tile_types.size() &&
        source.tile_types[tile.tile_index] == kTerrainTypeSwamp;
}
// The algae share of a swamp tile's frame art (swamp_frame_cover): 1 for a
// pure swamp frame or an unclassified map.
inline float terrain_swamp_frame_cover(const TerrainCpuAsset& source,
    const TerrainTileDescriptor& tile) noexcept {
    return tile.tile_index < source.swamp_frame_cover.size() ?
        source.swamp_frame_cover[tile.tile_index] : 1.0f;
}

// Reuse the paired layer's established height owner, including mixed boundaries.
inline std::uint16_t terrain_base_liquid_object(const TerrainCpuAsset& source,
    const TerrainSectorCpuAsset& sector, std::uint32_t base) noexcept {
    constexpr std::uint32_t layer = 33u * 33u;
    if (base >= layer || !sector.has_layers || base + layer >= sector.vertices.size())
        return UINT16_MAX;
    const auto object = sector.vertices[base + layer].lighting_object;
    return object < source.objects.size() && terrain_liquid_object(source.objects[object].type)
        ? object : UINT16_MAX;
}

// The layer object (lighting object) of layer vertex `local` (0..1088), the
// same for both copies of a sector-edge vertex: a sector's right/bottom edge
// vertex is the next sector's local 0, whose copy owns it (the map's last row
// and column own themselves). Authored border vertices can name different
// liquid objects in the two sectors (zone3obr x 160, y 233-238: wind
// multipliers 0.2 and 0.8), and the vertex stage's wind wave then moves the
// two copies apart. The owner is used only when both copies are liquid.
inline std::uint16_t owned_terrain_layer_object(const TerrainCpuAsset& source,
    const TerrainSectorCpuAsset& sector, std::uint32_t local) noexcept {
    constexpr std::uint32_t layer = 33u * 33u;
    if (local >= layer || local + layer >= sector.vertices.size()) return UINT16_MAX;
    const std::uint16_t own = sector.vertices[local + layer].lighting_object;
    std::uint32_t x = local % 33u, y = local / 33u, sx = sector.map_x, sy = sector.map_y;
    if (x == 32u && sx + 1u < source.sector_count_x) { x = 0; ++sx; }
    if (y == 32u && sy + 1u < source.sector_count_y) { y = 0; ++sy; }
    if (sx == sector.map_x && sy == sector.map_y) return own;
    const std::size_t index = std::size_t(sy) * source.sector_count_x + sx;
    if (index >= source.sectors.size()) return own;
    const auto& owner = source.sectors[index];
    if (owner.map_x != sx || owner.map_y != sy || owner.vertices.size() < 2u * layer) return own;
    const std::uint16_t shared = owner.vertices[y * 33u + x + layer].lighting_object;
    const auto liquid = [&](std::uint16_t o) {
        return o < source.objects.size() && terrain_liquid_object(source.objects[o].type);
    };
    return liquid(own) && liquid(shared) ? shared : own;
}

// Exact 3x3 lane UVs of a decoded tile, computed from its tile id, rotation
// and the map's tile grid (the same rule the MPR decoder applies). Zeros when
// the grid is not established.
std::array<NativeTextureCoordinate, 9> terrain_tile_uvs(const TerrainCpuAsset& terrain,
    const TerrainTileDescriptor& tile) noexcept;

struct OwnedLoaderMetrics {
    double cold_wall_ms = 0.0;
    double worker_cpu_ms = 0.0;
    double publication_cpu_ms = 0.0;
    std::uint64_t jobs_queued = 0;
    std::uint64_t jobs_completed = 0;
    std::uint64_t jobs_cancelled = 0;
    std::uint64_t deduplicated_requests = 0;
    std::uint64_t decoded_source_bytes = 0;
    std::uint64_t peak_owned_bytes = 0;
};

struct OwnedMapSnapshot {
    std::uint64_t generation = 0;
    std::string map_resource;
    std::string terrain_resource;
    std::uint64_t request_serial = 0;
    std::uint64_t deterministic_fingerprint = 0;
    bool cutover = false;
    std::shared_ptr<const MobDocument> mob;
    std::shared_ptr<const TerrainCpuAsset> terrain;
    std::vector<StaticObjectSource> objects;
    std::map<std::string, std::shared_ptr<const ModelCpuAsset>> models;
    std::map<std::string, std::shared_ptr<const TextureCpuAsset>> textures;
    std::vector<ResourceContainerInfo> containers;
    std::vector<std::string> failures;
    ResourceStoreStats store_stats{};
    OwnedLoaderMetrics metrics{};
};

struct OwnedMapLoadRequest {
    std::uint64_t generation = 0;
    std::string map_resource;
    // The outer project MPR is distinct from the nested/common-parser MOB.
    // Empty is allowed for loader-only callers that do not prepare world-space
    // retained instances.
    std::string terrain_resource;
    // Explicit saved-layout selections. Empty keeps the normal first-wins
    // resolver. These files are read once by the worker through the owned store.
    std::wstring selected_map_file;
    std::wstring selected_terrain_file;
    bool terrain_only = false; // saved entries may omit decorative MOB data
    ResourceStoreConfig mounts;
    // Runtime appearance uses the completed native resolver. Map/terrain loads
    // retain source provenance without eagerly creating a second mesh source.
    bool load_object_assets = true;
    // Optional narrow debug/corpus slice. Empty means every otherwise accepted
    // static source; non-empty entries are ASCII case-insensitive visual names.
    std::vector<std::string> visual_allowlist;
    // Optional loader/corpus slice; never a live scene admission predicate.
    std::vector<std::uint32_t> family_allowlist;
    // Render-only backdrop (neighbour maps): no scripts run there, so quest
    // flags and part lists stay drawable in their authored initial state, and
    // lever objects are decoded as meshes (parse_mob_document include_levers).
    bool backdrop_objects = false;
    // Textures decoded for renderer-owned effects whatever load_object_assets
    // says (ground-cover bark and rock art); names without extension.
    std::vector<std::string> extra_textures;
    // Precompute the per-vertex terrain shading inputs on the worker (only for
    // a terrain that is never edited after load: not neighbours, which weld).
    bool prepare_terrain_shading = false;
};

struct OwnedResourceRequestToken {
    std::uint64_t generation = 0;
    std::uint64_t request_serial = 0;

    explicit operator bool() const noexcept {
        return generation != 0 && request_serial != 0;
    }
};

enum class OwnedResourceWaitResult : std::uint8_t {
    ready,
    retired,
    shutdown,
};

class OwnedResourceLoader {
public:
    OwnedResourceLoader();
    ~OwnedResourceLoader();
    OwnedResourceLoader(const OwnedResourceLoader&) = delete;
    OwnedResourceLoader& operator=(const OwnedResourceLoader&) = delete;

    // Must be called from normal runtime initialization, never DllMain.
    bool start(std::uint32_t worker_count, ResourceFailure& failure);
    bool request(const OwnedMapLoadRequest& request, ResourceFailure& failure,
                 OwnedResourceRequestToken* token = nullptr);
    bool try_publish(std::shared_ptr<const OwnedMapSnapshot>& snapshot);
    bool try_publish(const OwnedResourceRequestToken& token,
                     std::shared_ptr<const OwnedMapSnapshot>& snapshot);
    OwnedResourceWaitResult wait_publish(
        const OwnedResourceRequestToken& token,
        std::shared_ptr<const OwnedMapSnapshot>& snapshot) noexcept;
#ifdef EID3D11_TESTING
    bool wait_ready_for_test(const OwnedResourceRequestToken& token) noexcept;
    void hold_completion_for_test() noexcept;
    bool wait_completion_held_for_test() noexcept;
    bool wait_waiter_entered_for_test() noexcept;
    void release_completion_for_test() noexcept;
#endif
    void retire_generation(std::uint64_t generation) noexcept;
    void shutdown() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// include_levers: also decode lever chunks (doors, gates, chests, panels;
// MOB type 60), whose object fields sit directly in the lever chunk instead of
// a nested object chunk. Only render-only backdrops ask for them.
bool parse_mob_document(const ResourceBlob& blob, MobDocument& document,
                        std::vector<StaticObjectSource>& objects,
                        ResourceFailure& failure, bool include_levers = false);
bool parse_figure_asset(const ResourceBlob& blob, FigureCpuAsset& figure,
                        ResourceFailure& failure);
bool parse_texture_asset(const ResourceBlob& blob, TextureCpuAsset& texture,
                         ResourceFailure& failure);
bool parse_generated_texture_asset(
    const std::shared_ptr<const std::vector<std::uint8_t>>& bytes,
    TextureCpuAsset& texture, ResourceFailure& failure);
bool parse_generated_disk_texture_asset(const ResourceBlob& blob,
    TextureCpuAsset& texture, ResourceFailure& failure);
bool parse_material_asset(const ResourceBlob& blob, MaterialCpuAsset& material,
                          ResourceFailure& failure);
// The host publishes the selected header/sectors, including live saved edits.
// The same parser serves an immutable archive and these completed member bytes.
using TerrainMemberResolver = std::function<bool(const std::string&, ResourceBlob&, ResourceFailure&)>;
bool parse_terrain_members(const ResourceIdentity& identity, const std::string& project_resource,
    const std::string& header_name, const TerrainMemberResolver& resolve,
    TerrainCpuAsset& terrain, ResourceFailure& failure);
bool parse_terrain_asset(const ResourceBlob& blob,
                         const std::string& project_resource,
                         TerrainCpuAsset& terrain,
                         ResourceFailure& failure);
// Performs the complete immutable sector/tile/material/index/UV contract
// validation once. A failed attempt is terminal for this decoded asset.
bool validate_terrain_asset_descriptor_once(
    TerrainCpuAsset& terrain) noexcept;
// Reconstructs the supported runtime's five terrain clip boxes and the exact
// SetLOD center-Z input from a decoded sector. X/Y bounds are nominal cell
// boundaries; all four subparts deliberately share the sector-wide Z range.
bool finalize_terrain_sector_spatial_descriptor(
    TerrainSectorCpuAsset& sector) noexcept;
bool sample_terrain_height(const TerrainCpuAsset& terrain, float x, float y,
                           float& height) noexcept;
// Intersect the immutable base grid in terrain-local coordinates. This is a
// stable shadow-quality reference, not the current LOD/layer/wave surface.
bool intersect_base_terrain_ray(const TerrainCpuAsset& terrain,
    const std::array<double, 3>& origin, const std::array<double, 3>& direction,
    std::array<double, 3>& hit) noexcept;
enum class TerrainHeightLayer : std::uint8_t { ground, surface };
bool sample_terrain_height(const TerrainCpuAsset& terrain, float x, float y,
                           TerrainHeightLayer layer, float& height,
                           const std::vector<float>* layer_shifts = nullptr) noexcept;
bool parse_animation_database(const ResourceBlob& blob,
                              AnimationDatabaseAsset& database,
                              ResourceFailure& failure);
bool parse_animation_track(const ResourceBlob& blob,
                           AnimationTrackAsset& track,
                           ResourceFailure& failure);
bool decode_animation_clip(const ResourceBlob& selected_clip,
                           AnimationClipAsset& clip,
                           ResourceFailure& failure, const ResourceStore* budget = nullptr);
// The caller supplies the exact selected outer resources. This overload never
// chooses mounts, invokes a native loader, or changes a cached ADB selection.
bool decode_model_resources(const ResourceBlob& model_container,
                            const ResourceBlob& bone_container,
                            const std::string& visual_reference,
                            ModelCpuAsset& model, ResourceFailure& failure);
// Reads only LNK/BON hierarchy. Drawable parts select their FIG independently;
// no vertex channel is decoded just to establish skeleton membership.
// Decode the exact LNK and per-node BON selections made by the host loader.
// The callback does not perform a second resource search. Missing nodes return
// not_found; malformed selected bytes remain errors, never missing transforms.
using SelectedBoneResolver = std::function<bool(const std::string&, ResourceBlob&, ResourceFailure&)>;
bool decode_selected_skeleton(const ResourceBlob& links,
    const std::string& visual_reference, const SelectedBoneResolver& select_bone,
    ModelCpuAsset& skeleton, ResourceFailure& failure);
bool decode_model_skeleton_resources(const ResourceBlob& model_container,
                                    const ResourceBlob& bone_container,
                                    const std::string& visual_reference,
                                    ModelCpuAsset& skeleton, ResourceFailure& failure);
bool decode_model_asset(const ResourceStore& store,
                        const std::string& visual_reference,
                        ModelCpuAsset& model, ResourceFailure& failure);

// Writes only beneath EI_RENDERER_DEBUG_DIR and always records cutover=false
// for this no-rendering snapshot phase.
bool write_owned_loader_receipt(const OwnedMapSnapshot& snapshot,
                                ResourceFailure& failure);

} // namespace owned
