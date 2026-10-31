#pragma once

#include "owned_asset_loader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace owned {

// The decoder places base tiles below texture_count+2, upper/liquid tiles above it.
// Base vertices have no lighting_object, so layer_shifts cannot move these casters.
inline bool owned_terrain_base_shadow_material(const TerrainCpuAsset& source,
    std::uint32_t material) noexcept { return material<source.texture_count+2u; }

// Completed map values. No sector residency, camera admission or decorative
// atlas phase is needed to reconstruct depth geometry from the immutable MPR.
struct OwnedTerrainGeometryValues {
    std::uint64_t revision = 0;
    std::uint64_t structural_revision = 0;
    std::uint64_t noise_revision = 0;
    // Live per-object layer shifts (a game water level moves them every tick
    // while it ramps). They are shader values: geometry, flow, mesh, bounds
    // and registration keys never read them.
    std::vector<float> layer_shifts;
    // Per object, the [min,max] shift seen on this map, grown geometrically
    // (grow_owned_terrain_layer_shift_range). Spatial bounds cover it, so a
    // level step inside the range changes no key at all.
    std::vector<std::array<float, 2>> layer_shift_range;
    std::array<float, 33u * 33u> wind_noise{};
    NativeTerrainState animation{};
};

// Widens range (one [min,max] per object, missing entries [0,0]) to contain
// shifts. A widened side at least doubles the side's span from the other
// bound, so a monotonic ramp of N steps widens O(log N) times. Returns
// whether anything changed; false with range untouched on a size mismatch
// or a non-finite shift.
inline bool grow_owned_terrain_layer_shift_range(std::vector<std::array<float,2>>& range,
    const std::vector<float>& shifts) noexcept {
    if(!range.empty() && range.size()!=shifts.size())return false;
    for(float value:shifts)if(!std::isfinite(value))return false;
    try { if(range.empty())range.assign(shifts.size(),{0.f,0.f}); } catch (...) { return false; }
    bool grew=false;
    for(std::size_t index=0;index<shifts.size();++index) {
        auto& [low,high]=range[index];
        const float value=shifts[index];
        if(value>high){high=(std::max)(value,high+(high-low));grew=true;}
        else if(value<low){low=(std::min)(value,low-(high-low));grew=true;}
    }
    return grew;
}

// A render-only map (no live game owner) at its authored rest heights: zero
// layer shifts and one immutable, therefore nonzero, values revision.
OwnedTerrainGeometryValues frozen_owned_terrain_geometry(const TerrainCpuAsset& source);

bool owned_terrain_base_liquid_depth(const TerrainCpuAsset& source,
    const TerrainSectorCpuAsset& sector, std::uint32_t base,
    const OwnedTerrainGeometryValues& values, float& depth) noexcept;

// Flow of the liquid surface itself at one layer vertex: the layer heights of
// liquid-object vertices (plus their live layer shift, when values are given),
// never the bed, so a level lake is still over any bed and a sloped river runs
// (layer vertices buried under the ground are ignored)
// down its own surface. Slopes are tangents (height per world unit). Art
// controls live in liquid_surface_shader.h.
struct OwnedLiquidFlow {
    std::array<float,2> current{}; // downhill surface gradient, 5x5 tent-smoothed
    float bend = 0;    // rate of change of the surface gradient (per world unit)
    float cascade = 0; // 0..1: steep surface here or within kLiquidCascadeReach
    float impact = 0;  // 0..1: flat receiving water below a nearby visible fall
    float drop = 0;    // how far this vertex lies below the highest liquid surface within the
                       // cascade disc: on a fall, the distance fallen from its lip (world units)
    float fill = 0;    // depth of this vertex inside a local pit of the surface (an impact
                       // funnel): the level it fills to before spilling out (world units)
};
// A liquid vertex on a fall edge: a 4-neighbour kLiquidFallEdgeDrop higher
// or lower. A lone spike (every neighbour far above or below it, as authored
// stray vertices are) is no fall, and neither is the edge to a spike.
// height(x,y,z): the visible liquid surface on the global 1 m grid.
template<class Height> bool liquid_fall_vertex_at(const Height& height,int x,int y,float edge_drop) noexcept {
    const auto spike=[&](int px,int py) {
        float c=0;if(!height(px,py,c))return false;
        int above=0,below=0,count=0;
        const int d[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
        for(const auto& o:d){float z=0;if(!height(px+o[0],py+o[1],z))continue;++count;
            if(z-c>=edge_drop)++above;else if(c-z>=edge_drop)++below;}
        return count>=2&&(above==count||below==count);
    };
    float c=0;
    if(!height(x,y,c)||spike(x,y))return false;
    const int d[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    for(const auto& o:d){float z=0;
        if(height(x+o[0],y+o[1],z)&&std::abs(z-c)>=edge_drop&&!spike(x+o[0],y+o[1]))return true;}
    return false;
}
// Fills terrain.outside_liquid from the map adjoining it, whose vertex (0,0)
// lies at (-offset_x, -offset_y) of this terrain's grid: liquid flow, cascade
// and drop then read the adjoining liquid across the shared edge.
void owned_terrain_set_outside_liquid(TerrainCpuAsset& terrain,int offset_x,int offset_y,
    const TerrainCpuAsset& adjoining);
// The current of level river reaches: per global liquid vertex (row stride
// sector_count_x*32+1) the downstream direction times its strength, in units
// of kLiquidOutflowSlope/127, and how close it runs to a fall lip (0..127,
// raised into the cascade source); empty when no level water of the map runs.
using TerrainLiquidOutflowField = std::vector<std::array<std::int8_t,3>>;
struct TerrainLiquidOutflow {
    std::mutex lock;
    bool built = false;
    std::shared_ptr<const TerrainLiquidOutflowField> field;
};
// Gives terrain.liquid_outflow an empty cache (descriptor validation), or
// empties it (a neighbour's seam was set), so the field is (re)built on first
// use. The field reads the liquid surface first asked for (its layer shifts:
// a shift moves a buried overlay edge into view) and, like the rest of the
// flow, is not rebuilt by a later shift.
void owned_terrain_reset_liquid_outflow(TerrainCpuAsset& terrain) noexcept;
// The level-reach field, built on first use: the current of level river
// reaches. Each liquid body drains to its lowest level (lake or
// sea water, else a map edge at that level, else its widest low water); a
// level vertex runs toward that outlet, along the geodesic distance to it,
// when its way down crosses moving water (it drains over a fall or rapid) or
// it lies downstream of moving water. Open water (lakes, sea) and ponds stay
// still, as do swamp and lava, and a lake fed by moving water passes it on.
// With terrain.outside_liquid set (a neighbour map), an edge where the
// adjoining map's flow leaves this map is an outlet and one where it enters
// is a source, so the flow continues across the seam. CPU only, at decode.
std::shared_ptr<const TerrainLiquidOutflowField> owned_terrain_liquid_outflow(
    const TerrainCpuAsset& terrain,const OwnedTerrainGeometryValues* values) noexcept;
// Blends a flow decoded at global vertex (x,y) of terrain toward the
// adjoining map's flow on their shared edge (outside_liquid.edge_flow): equal
// on the edge, the terrain's own kTerrainEdgeFlowBand vertices in.
constexpr int kTerrainEdgeFlowBand=6;
void owned_terrain_weld_edge_flow(const TerrainCpuAsset& terrain,int x,int y,OwnedLiquidFlow& flow) noexcept;
bool owned_terrain_liquid_flow(const TerrainCpuAsset& source,
    std::uint32_t sector_index,std::uint32_t base,
    const OwnedTerrainGeometryValues* values,OwnedLiquidFlow& flow) noexcept;

// The same flow for every base vertex of one sector, with each surface
// gradient evaluated once instead of ~140 times per vertex. Decoding a whole
// map's liquid per vertex cost ~75 ms (Suslanger, 60k liquid tile vertices),
// and a game water-level change re-decodes it. Results are bit-identical to
// owned_terrain_liquid_flow(); flows are memoized per vertex.
class OwnedLiquidFlowSector {
public:
    static constexpr int kReach = 5;   // gradient window radius the flow terms read
    static constexpr int kSide = 33 + 2 * kReach;
    bool prepare(const TerrainCpuAsset& source, std::uint32_t sector_index,
                 const OwnedTerrainGeometryValues* values) noexcept;
    bool flow(std::uint32_t base, OwnedLiquidFlow& flow) noexcept;
private:
    std::shared_ptr<const TerrainLiquidOutflowField> outflow_;
    int xmax_ = 0, ymax_ = 0;
    int origin_x_ = 0, origin_y_ = 0;
    bool ready_ = false;
    std::array<std::array<float,2>, kSide * kSide> gradients_{};
    std::array<float, kSide * kSide> steepness_{};
    std::array<float, kSide * kSide> heights_{};
    std::array<std::uint8_t, kSide * kSide> valid_{};
    std::array<OwnedLiquidFlow, 1089> flows_{};
    std::array<std::uint8_t, 1089> done_{};
};

// Packed terrain_slope_normal() of one base vertex over the zone grid, so both
// copies of a sector-edge vertex get the same value. Immutable MPR positions.
std::uint32_t owned_terrain_slope_normal(const TerrainCpuAsset& source,
    const TerrainSectorCpuAsset& sector, std::uint32_t base) noexcept;
// terrain_facet_flatness() of one base vertex over the zone grid (the same
// zone-grid positions, so sector-edge copies agree).
std::uint32_t owned_terrain_facet_flatness(const TerrainCpuAsset& source,
    const TerrainSectorCpuAsset& sector, std::uint32_t base) noexcept;
// Depth of a base vertex below all four grid neighbours, up to the lowest of
// them (an authored pit; 0 on slopes, ridges, valleys and wall feet). The
// one-ring slope normal and the stored lighting normal never see it, so sun
// and point casters draw the vertex raised by it (L-085). Zone-grid positions.
// Height of a base vertex over the lowest of its 3x3 grid nodes. Across a
// face one cell wide (a 1 m staircase riser) both its foot and crest nodes
// see the floor, so the interpolated value is the pixel's rise above the
// floor there; the material blend lets the floor climb the wall foot by it.
float owned_terrain_foot_rise(const TerrainCpuAsset& source,
    const TerrainSectorCpuAsset& sector, std::uint32_t base) noexcept;
// Nearness of a wall rising beside a base vertex, 0..1 (the strongest node
// within 3 m: its rise of 0.35-1.05 m, smoothstepped, times (1-(d/3)^2)^2),
// for the terrain's wall-foot contact darkening; even along a 1 m staircase.
float owned_terrain_wall_above(const TerrainCpuAsset& source,
    const TerrainSectorCpuAsset& sector, std::uint32_t base) noexcept;
float owned_terrain_pit_fill(const TerrainCpuAsset& source,
    const TerrainSectorCpuAsset& sector, std::uint32_t base) noexcept;
// Fills every sector's base_slope_normals/base_facet_flatness (several threads),
// so the terrain decode at the first world frame reads them instead.
void owned_terrain_prepare_shading(TerrainCpuAsset& source) noexcept;

// terrain_fill_corners() of one base tile over the zone grid, reordered into
// atlas orientation (bit (v>centre)*2+(u>centre) of the tile's UV corners) and
// shifted into NativeTerrainVertex::flags bits 8-11.
std::uint32_t owned_terrain_fill_flags(const TerrainCpuAsset& source,
    const TerrainSectorCpuAsset& sector, const TerrainTileDescriptor& tile,
    const std::array<NativeTextureCoordinate,9>& uvs) noexcept;

// Fills terrain.swamp_frame_cover from the loaded BC1 pages (map loader,
// before the asset is shared). Returns one log line, empty when the map has
// no swamp next to open water.
std::string owned_terrain_classify_swamp_frames(TerrainCpuAsset& terrain);

// Average adjacent liquid tiles at source-grid positions, including sector seams.
// swamp (optional): the swamp share (terrain_swamp_tile) of the liquid tiles
// within a few cells, smoothly weighted by distance (a soft, grid-free field).
void owned_terrain_liquid_materials(const TerrainCpuAsset& source, std::uint32_t sector,
    std::array<TerrainObjectDescriptor,1089>& materials,
    std::array<float,1089>* swamp = nullptr) noexcept;

// Per tile of a sector (16x16 tiles of 2x2 grid cells, row-major): bit
// (dy+1)*3+(dx+1) is set when the tile at that offset carries a liquid object.
// The shader smooths this 3x3 neighbourhood with a Gaussian to round the
// square tile outline of liquid sheets.
void owned_terrain_liquid_neighbour_masks(const TerrainCpuAsset& source,std::uint32_t sector,
    std::array<std::uint16_t,256>& masks) noexcept;

struct OwnedTerrainPreparedPart {
    NativePreparedMeshHandle mesh;
    NativePreparedMaterialHandle material;
    NativeGeometryView source;
    std::weak_ptr<const void> owner;
    std::uint64_t source_revision = 0;
    struct DepthValues {
        std::array<float,16> object_to_world{};
        std::array<std::uint32_t,16> state{};
        std::array<float,5> fog{},wave{};
        float alpha = 1;
        bool waves = false;
        std::uint64_t structure = 0,surface = 0,surface_generation = 0,phase_revision = 0;
        std::uint32_t phase_count = 0;
        bool operator==(const DepthValues& other) const noexcept {
            return object_to_world==other.object_to_world && state==other.state && fog==other.fog &&
                wave==other.wave && waves==other.waves && alpha==other.alpha && structure==other.structure &&
                surface==other.surface && surface_generation==other.surface_generation &&
                phase_revision==other.phase_revision && phase_count==other.phase_count;
        }
    };
    DepthValues depth_values;
    std::uint64_t depth_revision = 0;
};

enum class OwnedTerrainPacketStatus : std::uint8_t {
    pending_source,
    ready,
    invalid,
};

enum class OwnedTerrainRangeAction : std::uint8_t {
    omit_pending_source,
    omit_invalid,
    omit_stale,
    omit_wrong_order,
    submit_base,
    submit_transparent,
};

struct OwnedTerrainFrameInputs {
    std::uint64_t source_generation = 0;
    std::uint64_t source_revision = 0;
    std::uint64_t world_frame_serial = 0;
    std::array<float, 3> terrain_origin{};
    std::array<float, 3> camera_position{};
    std::array<float, 2> lod_distances{};
    std::int32_t maximum_lod = 0;
    bool force_full_detail = false;
    std::uint32_t runtime_texture_count = 0;
    std::uint32_t terrain_render_state = 0;
    std::uint64_t game_visibility_fingerprint = 0;
    bool game_visibility_domain_present = false;
    std::int32_t game_visibility_min_y = 0;
    std::vector<std::array<std::int32_t, 2>> game_visibility_rows;
    std::uint64_t dynamic_state_fingerprint = 0;
    std::array<float, 16> object_to_clip{};
};

struct OwnedTerrainSectorState {
    std::uint32_t source_sector = 0;
    std::uint8_t lod = 0;
    bool visible = false;
    std::array<bool, 4> subpart_visible{};
    bool lu_higher = false;
    bool rd_higher = false;
};

struct OwnedTerrainCommand {
    std::uint32_t source_sector = 0;
    std::uint16_t material = 0;
    std::uint8_t lod = 0;
    bool lu_higher = false;
    bool rd_higher = false;
    std::uint8_t subpart_mask = 0;
    std::uint32_t tile_count = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    // First selected spatial part. Multi-part color commands consume at most
    // the remaining contiguous four-part material/sector run from this index.
    std::uint32_t spatial_index = UINT32_MAX;
};

// One entry for every occupied source material/subpart in the current outer
// world frame. Bounds are independent of camera admission and LOD/stitching;
// current geometry values and placement are fixed before this table is built.
struct OwnedTerrainSpatialPart {
    std::uint32_t source_sector = 0;
    std::uint16_t material = 0;
    std::uint8_t subpart_mask = 0;
    NativeWorldBounds local_bounds;
    NativeWorldBounds world_bounds;
};

// Source-owned preparation that survives outer render frames. The spatial
// bounds key contains only values consumed by owned_terrain_command_bounds;
// camera, LOD, texture animation and current sunlight remain frame values.
// Renderer resource handles share this owner but remain independently keyed.
struct OwnedTerrainPreparedResources {
    // Keyed {sector, material, subpart mask, LOD variant}; looked up for every
    // terrain caster and color part, so hashed (node-based: borrowed part
    // pointers stay valid across inserts).
    struct PartKeyHash {
        std::size_t operator()(const std::array<std::uint32_t,4>& key) const noexcept {
            std::uint64_t h=0x9e3779b97f4a7c15ull;
            for(const auto value:key){h^=value;h*=0x100000001b3ull;h^=h>>29;}
            return static_cast<std::size_t>(h^(h>>32));
        }
    };
    std::unordered_map<std::array<std::uint32_t,4>,OwnedTerrainPreparedPart,PartKeyHash> parts;
    std::vector<OwnedTerrainSpatialPart> spatial_parts;
    std::vector<std::array<float, 2>> spatial_layer_shift_range;
    std::array<float,16> spatial_object_to_world{};
    float spatial_absolute_wind_force = 0;
    std::uint64_t spatial_source_generation = 0;
    std::uint64_t spatial_source_revision = 0;
    bool spatial_waves_enabled = false;
    bool spatial_local_ready = false;
    bool spatial_world_ready = false;
};

struct OwnedTerrainFramePacket {
    OwnedTerrainPacketStatus status =
        OwnedTerrainPacketStatus::pending_source;
    std::shared_ptr<const TerrainCpuAsset> source;
    OwnedTerrainFrameInputs inputs{};
    std::vector<OwnedTerrainSectorState> sectors;
    std::vector<OwnedTerrainSpatialPart> spatial_parts;
    std::vector<OwnedTerrainCommand> base_commands;
    std::vector<OwnedTerrainCommand> transparent_commands;
    std::array<float,16> spatial_object_to_world{};
    std::uint64_t spatial_geometry_revision = 0;
    std::uint64_t spatial_bounds_computations = 0;
    std::uint64_t spatial_bounds_reuses = 0;
    std::uint64_t spatial_front_end_reuses = 0;
    std::uint64_t spatial_receiver_reuses = 0;
    std::uint64_t spatial_caster_reuses = 0;
    std::uint64_t spatial_record_reuses = 0;
    std::uint64_t preparation_count = 0;
    std::uint64_t base_submission_count = 0;
    std::uint64_t transparent_reuse_count = 0;
    bool base_requested = false;
    bool base_submitted = false;
    bool failed_after_claim = false;
    bool spatial_ready = false;
};

// Prepares the complete render-front-end decision once from the immutable MPR
// snapshot. A missing source is a handled, fail-visible pending state; it is
// never authorization to invoke the original terrain range renderer.
bool prepare_owned_terrain_frame(
    std::shared_ptr<const TerrainCpuAsset> source,
    const OwnedTerrainFrameInputs& inputs,
    OwnedTerrainFramePacket& packet) noexcept;

// The world-to-clip transform that reproduces the game's own terrain
// object-to-clip for the given terrain placement (column vectors:
// object_to_clip = world_to_clip * object_to_world). False for a singular
// placement or non-finite input.
bool owned_terrain_world_to_clip(const std::array<float,16>& object_to_clip,
    const std::array<float,16>& object_to_world,
    std::array<float,16>& world_to_clip) noexcept;

// Called after the exact game clip/publication duties. The game-facing sector
// decisions are preserved; only renderer commands use current render envelopes.
bool prepare_owned_terrain_spatial_parts(OwnedTerrainFramePacket& packet,
    const OwnedTerrainGeometryValues& values,
    const std::array<float,16>& object_to_world,
    OwnedTerrainPreparedResources* prepared = nullptr) noexcept;
// Produces one color command for each visible material/sector pair while the
// spatial table retains one record per occupied part for shadow selection.
bool prepare_owned_terrain_render_commands(OwnedTerrainFramePacket& packet) noexcept;
// Color and close-light consumers use the same source topology/LOD/stitch decision.
// Neighbor camera admission does not change an edge's geometry.
bool owned_terrain_render_command(const OwnedTerrainFramePacket& packet,
    std::uint32_t sector,std::uint32_t material,std::uint8_t subparts,
    OwnedTerrainCommand& command) noexcept;
// Distant directional depth uses the source's uniformly coarse terrain topology.
// Every far-shadow sector uses LOD2, so no neighbor stitching is needed.
bool owned_terrain_far_shadow_command(const OwnedTerrainFramePacket& packet,
    std::uint32_t sector,std::uint32_t material,std::uint8_t subparts,
    OwnedTerrainCommand& command) noexcept;
bool owned_terrain_spatial_bounds(OwnedTerrainFramePacket& packet,
    const OwnedTerrainCommand& command,NativeWorldBounds& local,
    NativeWorldBounds& world) noexcept;

OwnedTerrainRangeAction decide_owned_terrain_range(
    OwnedTerrainFramePacket& packet,
    const OwnedTerrainFrameInputs& current,
    int material_from, int material_to) noexcept;
bool complete_owned_terrain_base(OwnedTerrainFramePacket& packet, bool success) noexcept;

bool owned_terrain_frame_inputs_equal(
    const OwnedTerrainFrameInputs& left,
    const OwnedTerrainFrameInputs& right) noexcept;

std::array<std::uint32_t, 5> owned_terrain_geometry_revision(
    const OwnedTerrainFramePacket& packet,
    const OwnedTerrainCommand& command,
    std::uint64_t structural_generation) noexcept;

bool select_owned_terrain_descriptor_counts(
    const TerrainMaterialDescriptor& descriptor, std::uint8_t lod,
    bool lu_higher, bool rd_higher, std::uint8_t subpart_mask,
    std::uint32_t& tile_count, std::uint32_t& index_count) noexcept;

bool owned_terrain_command_bounds(const TerrainCpuAsset& source,
    const OwnedTerrainCommand& command, const OwnedTerrainGeometryValues& values,
    NativeWorldBounds& bounds) noexcept;

bool owned_terrain_vertex_height(const TerrainVertex& vertex,
    const OwnedTerrainGeometryValues& values, float& height) noexcept;

bool owned_terrain_receiver_bounds(const TerrainCpuAsset& source,
    const OwnedTerrainGeometryValues& values,
    const std::array<float,16>& world_to_clip,
    std::vector<NativeWorldBounds>& bounds) noexcept;

std::uint64_t owned_terrain_command_structure(const TerrainCpuAsset& source,
    const OwnedTerrainCommand& command, const OwnedTerrainGeometryValues& values) noexcept;

} // namespace owned
