#pragma once
// The CPU inputs of a neighbour's ground cover, contact maps and sea surf,
// computed by the neighbour stream after the weld (ground_cover_neighbour.cpp)
// from the decoded terrain and map. The renderer turns them into GPU
// resources at adoption (Renderer::add_ground_cover_neighbour) and never
// needs the decoded terrain itself. Shared by the renderer DLL and the x64
// render host.
#include "ambient_terrain_grids.h"
#include "owned_asset_loader.h"
#include "owned_resource_store.h"
#include "owned_terrain_decode.h"
#include "terrain_tile_blend.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace owned {

// One base tile's atlas cell: material and corner UVs by world corner.
struct GcTileGpu { float uv[8]; std::uint32_t material, padding[3]; };
static_assert(sizeof(GcTileGpu) == 48);

// Tile atlas cells (base tiles only); used: per material, 1 where a base tile uses it.
std::vector<GcTileGpu> ground_cover_tiles(const TerrainCpuAsset& terrain, std::uint32_t tiles_x,
    std::uint32_t tiles_y, std::vector<std::uint8_t>& used);
// Per base tile: the slot signature corner at each world corner, two bits each.
std::vector<std::uint8_t> ground_cover_corner_lanes(const std::vector<GcTileGpu>& tiles);
// Per tile: tree litter influence of the placed MOB trees and cave burial affinity.
std::vector<std::array<std::uint8_t,4>> ground_cover_trees(const OwnedMapSnapshot& map,
    std::uint32_t tiles_x, std::uint32_t tiles_y);

struct GroundCoverNeighbourInputs {
    ambient::TerrainGrids grids;
    std::vector<GcTileGpu> tiles;
    std::vector<std::uint8_t> used, base_lanes;
    std::vector<std::array<std::uint8_t,4>> trees;
    // The map's tile families, loaded by the stream (valid: the resource
    // mounts were known, so the load ran; its result may still be empty).
    bool families_valid = false;
    TerrainTileFamilies families;
    std::vector<std::uint16_t> base_slots;
    std::vector<std::uint32_t> slot_types;
    std::shared_ptr<TerrainTileBlendInput> blend_input; // only for 8-tile atlas sides
    // Contact grid rows (owned_terrain_contact_tiles) and the materials they use.
    std::vector<std::array<std::uint32_t,4>> contact_tiles;
    std::uint64_t contact_used = 0;
    // Terrain values add_ground_cover_neighbour reads.
    float lowest = 0, highest = 0;
    std::uint32_t texture_count = 0, tiles_per_texture_side = 0;
    // Per material in `used`: its atlas page's index in the piece's texture
    // table (NeighbourPiece::textures), UINT32_MAX when the page is missing.
    std::vector<std::uint32_t> atlas_textures;
    bool grids_valid = false;
};

// Pure CPU, from the welded neighbour terrain and its map. The tile families
// load from `mounts` here (empty mounts: no families); `cancel` ends that load
// early when the stream stops.
std::shared_ptr<GroundCoverNeighbourInputs> prepare_ground_cover_neighbour_inputs(
    const OwnedMapSnapshot& map, const ResourceStoreConfig& mounts,
    const std::atomic<bool>* cancel) noexcept;

struct LiquidSurfField;
// Its sea surf field (liquid_surf.h) from those inputs' area grid; null
// without water or inputs.
std::shared_ptr<const LiquidSurfField> prepare_liquid_surf_neighbour(
    const GroundCoverNeighbourInputs* inputs, const std::string& mpr, const std::string& no_sea_maps) noexcept;

} // namespace owned
