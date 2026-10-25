// The CPU inputs of a neighbour's ground cover, contact maps and sea surf
// (ground_cover_neighbour.h). Pure CPU over the decoded terrain and map; runs
// on the neighbour stream's worker in the renderer DLL or the x64 render host.
#include "ground_cover_neighbour.h"

#include "ground_cover.h"
#include "ground_cover_cave.h"
#include "liquid_surf.h"
#include "neighbour_maps.h"

#include <algorithm>
#include <cmath>

namespace owned {
using namespace ground_cover;

// Tile atlas cells (base tiles only): material and corner UVs by world corner.
// used: per material, 1 where a base tile uses it.
std::vector<GcTileGpu> ground_cover_tiles(const TerrainCpuAsset& terrain, std::uint32_t tiles_x, std::uint32_t tiles_y,
                                          std::vector<std::uint8_t>& used) {
    std::vector<GcTileGpu> tiles(std::size_t(tiles_x) * tiles_y);
    for (auto& tile : tiles) tile.material = 0xffffffffu;
    constexpr std::uint32_t layer = 33u * 33u;
    for (const auto& sector : terrain.sectors) {
        if (sector.map_x >= terrain.sector_count_x || sector.map_y >= terrain.sector_count_y) continue;
        for (const auto& tile : sector.tiles) {
            if (tile.transparent || tile.vertex_indices[0] >= layer) continue;
            std::uint32_t lx = 33, ly = 33;
            for (const unsigned k : {0u, 2u, 6u, 8u}) {
                lx = std::min(lx, std::uint32_t(tile.vertex_indices[k] % 33u)); ly = std::min(ly, std::uint32_t(tile.vertex_indices[k] / 33u));
            }
            const std::uint32_t gx = sector.map_x * 16u + lx / 2u, gy = sector.map_y * 16u + ly / 2u;
            if (gx >= tiles_x || gy >= tiles_y || lx > 30 || ly > 30) continue;
            auto& out = tiles[std::size_t(gy) * tiles_x + gx];
            bool complete = true;
            const auto tile_uvs = owned::terrain_tile_uvs(terrain, tile);
            for (const unsigned k : {0u, 2u, 6u, 8u}) {
                const std::uint32_t cx = (tile.vertex_indices[k] % 33u - lx) / 2u, cy = (tile.vertex_indices[k] / 33u - ly) / 2u;
                if (cx > 1 || cy > 1) { complete = false; break; }
                out.uv[(cy * 2 + cx) * 2] = tile_uvs[k].u;
                out.uv[(cy * 2 + cx) * 2 + 1] = tile_uvs[k].v;
            }
            if (!complete) continue;
            out.material = tile.material_index;
            if (tile.material_index >= used.size()) used.resize(std::size_t(tile.material_index) + 1, 0);
            used[tile.material_index] = 1;
        }
    }
    return tiles;
}

// Per base tile: the slot signature corner (NW, NE, SE, SW) at each world
// corner cy*2+cx, two bits each (terrain_cover_types lanes). Same base tile
// as terrain_base_slots: the MPR decoder stores positional vertex indices
// (index 0 is the tile's min corner; rotation lives only in the UVs) and
// marks exactly the layer tiles transparent, whose indices start past the
// base layer, so both skip the same tiles and land on the same cell.
std::vector<std::uint8_t> ground_cover_corner_lanes(const std::vector<GcTileGpu>& tiles) {
    std::vector<std::uint8_t> lanes(tiles.size(), 0);
    for (std::size_t t = 0; t < tiles.size(); ++t) {
        if (tiles[t].material == 0xffffffffu) continue;
        const float* uv = tiles[t].uv;
        const auto corner = terrain_tile_world_corners({0, 1, 2, 3}, {{{uv[0], uv[1]}, {uv[2], uv[3]}, {uv[4], uv[5]}, {uv[6], uv[7]}}});
        for (unsigned k = 0; k < 4; ++k) lanes[t] = std::uint8_t(lanes[t] | (corner[k] << (2 * k)));
    }
    return lanes;
}

// Per tile: deciduous, conifer and non-conifer woody influence of the placed MOB trees,
// and the undead affinity of cave burial sites (alpha).
std::vector<std::array<std::uint8_t,4>> ground_cover_trees(const OwnedMapSnapshot& map, std::uint32_t tiles_x, std::uint32_t tiles_y) {
    std::vector<std::array<std::uint8_t,4>> trees(std::size_t(tiles_x)*tiles_y);
    for (const auto& object : map.objects) {
        const auto kind = ambient::tree_kind(object.visual_reference.c_str());
        if (kind == ambient::tree_none) continue;
        const int cx = int(std::floor(object.position[0]*.5f)), cy = int(std::floor(object.position[1]*.5f));
        for (int dy=-3;dy<=3;++dy) for (int dx=-3;dx<=3;++dx) {
            const int x=cx+dx,y=cy+dy;
            if (x<0||y<0||x>=int(tiles_x)||y>=int(tiles_y)) continue;
            auto& value=trees[std::size_t(y)*tiles_x+x];
            const float influence = tree_litter_falloff(x*2.f+1.f-object.position[0], y*2.f+1.f-object.position[1]);
            add_tree_litter_influence(value, kind, influence);
        }
    }
    if(map.mob && map.mob->bytes) {
        const auto& bytes=*map.mob->bytes;
        const auto sites=[&](const auto& self,const std::vector<MobChunkRecord>& chunks)->void {
            for(const auto& chunk:chunks) {
                if(chunk.id==0xbbbb0000u) {
                    const MobChunkRecord* model=nullptr;const MobChunkRecord* position=nullptr;bool duplicate=false;
                    for(const auto& field:chunk.children) {
                        if(field.id==0xb006u){duplicate|=model!=nullptr;model=&field;}
                        if(field.id==0xb009u){duplicate|=position!=nullptr;position=&field;}
                    }
                    std::array<float,2> site{};
                    // Chunk extents were validated once by parse_mob_document.
                    if(!duplicate && model && position && cave_undead_site(bytes.data()+model->payload_offset,model->payload_bytes,
                        bytes.data()+position->payload_offset,position->payload_bytes,site)) {
                        const int cx=int(std::floor(site[0]*.5f)),cy=int(std::floor(site[1]*.5f));
                        for(int dy=-10;dy<=10;++dy)for(int dx=-10;dx<=10;++dx) {
                            const int x=cx+dx,y=cy+dy;if(x<0||y<0||x>=int(tiles_x)||y>=int(tiles_y))continue;
                            auto& value=trees[std::size_t(y)*tiles_x+x][3];
                            const auto affinity=std::uint8_t(std::max(0.f,1.f-std::sqrt(float(dx*dx+dy*dy))/10.f)*255);
                            value=std::max(value,affinity);
                        }
                    }
                }
                self(self,chunk.children);
            }
        };
        sites(sites,map.mob->roots);
    }
    return trees;
}

std::shared_ptr<GroundCoverNeighbourInputs> prepare_ground_cover_neighbour_inputs(
    const OwnedMapSnapshot& map, const ResourceStoreConfig& mounts,
    const std::atomic<bool>* cancel) noexcept try {
    if (!map.terrain) return nullptr;
    const auto& terrain = *map.terrain;
    auto inputs = std::make_shared<GroundCoverNeighbourInputs>();
    inputs->grids_valid = !terrain.sectors.empty() && ambient::build_terrain_grids(terrain, &map, inputs->grids);
    if (!inputs->grids_valid) return inputs;
    const std::uint32_t tx = inputs->grids.tiles_x, ty = inputs->grids.tiles_y;
    inputs->lowest = terrain.sectors.front().bounds.minimum[2]; inputs->highest = terrain.sectors.front().bounds.maximum[2];
    for (const auto& sector : terrain.sectors) {
        inputs->lowest = std::min(inputs->lowest, sector.bounds.minimum[2]);
        inputs->highest = std::max(inputs->highest, sector.bounds.maximum[2]);
    }
    inputs->texture_count = terrain.texture_count;
    inputs->tiles_per_texture_side = terrain.tiles_per_texture_side;
    // The tile families: loaded here, as the renderer's own job would have,
    // whenever the resource mounts are known.
    inputs->families_valid = !mounts.archives.empty();
    if (inputs->families_valid) {
        inputs->families = load_terrain_tile_families(mounts, terrain_tile_map_input(terrain), cancel);
        inputs->base_slots = ambient::terrain_base_slots(terrain); inputs->slot_types = terrain.tile_types;
    }
    inputs->tiles = ground_cover_tiles(terrain, tx, ty, inputs->used);
    if (inputs->families_valid) inputs->base_lanes = ground_cover_corner_lanes(inputs->tiles);
    inputs->trees = ground_cover_trees(map, tx, ty);
    inputs->bake_cells = std::make_shared<const TerrainBakeCells>(terrain_bake_cells(inputs->grids.height, tx, ty));
    if (inputs->families_valid && terrain.tiles_per_texture_side == 8)
        inputs->blend_input = std::make_shared<TerrainTileBlendInput>(owned_terrain_tile_blend_input(terrain, tx, ty));
    inputs->contact_tiles = owned_terrain_contact_tiles(terrain, tx, ty, inputs->contact_used);
    return inputs;
} catch (...) { return nullptr; }

std::shared_ptr<const LiquidSurfField> prepare_liquid_surf_neighbour(
    const GroundCoverNeighbourInputs* inputs, const std::string& mpr, const std::string& no_sea_maps) noexcept try {
    if (!inputs || !inputs->grids_valid) return nullptr;
    auto field = std::make_shared<LiquidSurfField>(
        build_liquid_surf_field(inputs->grids.area, inputs->grids.level.liquid_area, inputs->grids.height,
                                inputs->grids.ground, inputs->grids.tiles_x, inputs->grids.tiles_y,
                                liquid_surf_map_takes_sea(mpr, no_sea_maps)));
    if (field->texels.empty()) return nullptr;
    return field;
} catch (...) { return nullptr; }

} // namespace owned
