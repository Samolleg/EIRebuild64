#pragma once

// Terrain decode shared by the renderer and Prototypes/shader-lab (see
// owned_terrain_decode.cpp).

#include "terrain_tile_map_families.h"
#include "owned_terrain_front_end.h"
#include "owned_resource_store.h"
#include "terrain_tile_blend.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace owned {

void owned_terrain_prepare_astral(const TerrainMemberResolver& resolve, TerrainCpuAsset& terrain);
void owned_terrain_prepare_astral(ResourceStore& store, TerrainCpuAsset& terrain);

struct OwnedTerrainDynamicSector {
    bool captured = false;
    std::uint64_t source_revision = 0;
    std::uint32_t structural_generation = 0;
    std::uint64_t structural_copy_count = 0;
    std::vector<float> layer_z;
    std::vector<float> wind_noise;
    NativeTerrainState animation{};
    std::vector<std::uint32_t> animation_phases;
    NativeTerrainPhaseValues phase_values;
};

struct OwnedTerrainSourceDecodeContext {
    NativeGeometryView* borrowed_view=nullptr;
    ~OwnedTerrainSourceDecodeContext() {
        if(!borrowed_view)return;
        borrowed_view->decode_context=nullptr;
        borrowed_view->terrain_animation_phases=nullptr;
        borrowed_view->terrain_phase_values=nullptr;
        borrowed_view->owned_texture={};
    }
    const TerrainCpuAsset* source = nullptr;
    const TerrainSectorCpuAsset* sector = nullptr;
    const TerrainMaterialDescriptor* material = nullptr;
    const OwnedTerrainDynamicSector* dynamic = nullptr;
    const OwnedTerrainGeometryValues* geometry = nullptr;
    OwnedTerrainCommand command{};
};

struct TerrainIndexTemplate {
    const std::uint8_t* indices = nullptr;
    std::uint32_t count = 0;
};
// Shared c56 command-to-native descriptor. Borrows packet/dynamic/context for
// synchronous submission; builds no host-side geometry and reads no game image.
bool prepare_owned_terrain_source_view(const OwnedTerrainFramePacket& packet,
    const OwnedTerrainCommand& command, const OwnedTerrainDynamicSector& dynamic,
    OwnedTerrainSourceDecodeContext& context, NativeGeometryView& view) noexcept;
struct RetainedTerrainSourceSnapshot;
// Shared c56 world record, including prepared handles, bounds and contribution.
// Color may be absent only when selected subparts require no animation phases.
// The returned cached view borrows context until synchronous submission ends.
bool prepare_owned_terrain_world_record(const RetainedTerrainSourceSnapshot& source,
    OwnedTerrainFramePacket& packet, const OwnedTerrainCommand& command,
    const OwnedTerrainDynamicSector* color, const NativeContribution& contribution,
    OwnedTerrainSourceDecodeContext& context, NativeGeometryView*& output,
    bool alpha_coverage = false);
TerrainIndexTemplate terrain_index_template(std::uint32_t lod, std::uint8_t mask,
                                             bool lu_higher, bool rd_higher);
std::uint32_t terrain_vertices_per_tile(std::uint32_t lod);
bool set_owned_terrain_material(const TerrainObjectDescriptor& object,
                                NativeTerrainVertex& vertex) noexcept;
// The context names a command whose target streams are sized for it.
bool owned_terrain_decode_valid(const OwnedTerrainSourceDecodeContext* context,
                                const NativeGeometryDecodeTarget& target) noexcept;
// Positions, normals, UVs, components, indices and NativeTerrainVertex records
// of the command's tiles. Requires owned_terrain_decode_valid().
bool decode_owned_terrain_tiles(const OwnedTerrainSourceDecodeContext& context,
                                NativeGeometryDecodeTarget& target);

// The terrain's own art per 2 m base tile of a tiles_x x tiles_y grid (the
// native shader's contactTiles rows [0,h)): atlas corners 00, 10, 01 as unorm16
// pairs and the material slice (w; 0xffffffff none). used: bits of the
// materials the grid names.
std::vector<std::array<std::uint32_t,4>> owned_terrain_contact_tiles(const TerrainCpuAsset& terrain,
    std::uint32_t tiles_x,std::uint32_t tiles_y,std::uint64_t& used);
// Terrain material blend inputs that do not depend on the tile families,
// read while the decoded terrain is alive (a neighbour map keeps them until
// its families load): per tile of the contact grid its atlas slot (0xffff:
// keeps its art), the signature corner at each world corner k (bits 2k),
// world corners under a liquid at rest, steepness, mean height and whether
// a corner is under a liquid; per slot its use and whether the blend may
// read it as a plain cell.
struct TerrainTileBlendInput {
 std::uint32_t tiles_x=0,tiles_y=0,slots=0;
 std::vector<std::uint16_t> slot;
 std::vector<std::uint8_t> lanes,wet;
 std::vector<bool> steep,underwater;
 std::vector<float> heights;
 std::vector<std::uint32_t> usage;
 std::vector<bool> usable;
};
TerrainTileBlendInput owned_terrain_tile_blend_input(const TerrainCpuAsset& terrain,std::uint32_t tiles_x,std::uint32_t tiles_y);
// The map's own family at every grid vertex (terrain_tile_vertex_families
// of its tiles' world-corner families).
std::vector<std::uint32_t> owned_terrain_tile_vertex_families(const TerrainTileBlendInput& input,const TerrainTileFamilies& families);
// Names of the families the map's blend can draw (those with a material cell).
std::set<std::string> owned_terrain_tile_blend_drawable(const TerrainTileBlendInput& input,const TerrainTileFamilies& families);
// Terrain material blend rows (terrain_tile_blend_rows): every tile's
// world-corner families from its slot and corner lanes and the families'
// plain cells among the slots the map draws (families.signatures covers the
// input's slots). edge_families: per grid vertex the family a shared map
// edge gives it (0: its own; empty: none), used where this map has a plain
// cell for it; the others go to `unmatched`. bed_families: liquid-bed
// families on this map (terrain_tile_bed_name). fold_table: the folded
// cells' table (terrain_tile_fold_cells), the grid's extra row; borrowed:
// families that still borrow another family's art. slot_variants (terrain
// 2.0): each vertex draws the plain slot its own plain tiles place.
// art, when supplied, receives family policy bits in its existing w upper
// bits, from the effective shared-vertex families that generate these rows.
std::vector<std::array<std::uint32_t,4>> owned_terrain_tile_blend_rows(const TerrainTileBlendInput& input,
    const TerrainTileFamilies& families,const std::vector<std::uint32_t>& edge_families,
    std::size_t& blended,std::size_t& plain_materials,std::size_t& bed_families,std::set<std::uint32_t>& unmatched,
    std::vector<std::uint32_t>& fold_table,std::set<std::uint32_t>& borrowed,
    std::map<std::uint32_t,std::uint32_t>* family_cells=nullptr,bool slot_variants=false,
    std::vector<std::array<std::uint32_t,4>>* art=nullptr);
// What the tile family load needs from the map, copied on the calling thread:
// its atlas namespace/layout, its own base tile placements and file name,
// ETERRTYPE per slot and the declared animation phases. type_count excludes
// the loader's extra long-tile defaults; skip is the inference's slot mask.
struct TerrainTileMapInput {
 std::string space,own_file;
 unsigned slots=0,pages=0,tiles_per_side=0,texture_size=0,tile_size=0,type_count=0;
 std::vector<TerrainTilePlacement> own;
 std::vector<std::uint32_t> types;
 std::vector<std::uint32_t> animation_phases;
 std::vector<bool> skip;
};
TerrainTileMapInput terrain_tile_map_input(const TerrainCpuAsset& terrain);
// A map's tile families: the editor profile shipped with its resources, else
// the editor's built-in preset for its atlas when the atlas's maps fit it, else
// families from the placements of every map of that atlas. A set `cancel`
// ends the load early with an empty result (source "tile family load cancelled").
TerrainTileFamilies load_terrain_tile_families(ResourceStoreConfig mounts,TerrainTileMapInput input,
    const std::atomic<bool>* cancel=nullptr) noexcept;
std::string terrain_tile_family_space(const TerrainCpuAsset& terrain);

} // namespace owned
