#pragma once
// Area classes (low 4 bits) and leafy-tree counts within ~6 m (high 4 bits)
// per 2 m tile (a liquid layer only where its liquid lies over the ground),
// base terrain type per tile, ground/liquid-surface height and base ground
// height per vertex, of one immutable terrain source. The active map's
// ambient particles and ground cover, and each neighbour map's ground cover,
// build their grids here. Temporary CPU values: callers upload and free them.
#include "ambient_particles.h"
#include "emissive_lights.h"
#include "owned_asset_loader.h"
#include "owned_terrain_front_end.h"
#include <array>
#include <cmath>
#include <vector>

namespace owned::ambient {

// The live layer shifts (game water levels) move only liquid-surface heights,
// which tiles count as wet and the wet emissive tiles. These are the static
// inputs that apply_liquid_level needs to recompute exactly those values in
// place, bounded by the liquid vertices and tiles (never a full rebuild).
struct LiquidLevelInputs {
    std::uint32_t tiles_x = 0;
    std::vector<std::uint8_t> dry_area, liquid_area; // per tile: base|trees<<4, liquid class (0xff none)
    std::vector<float> ground;                        // per vertex, 0 where not finite
    struct Vertex { std::uint32_t cell = 0; float ground = 0, rest = 0; std::uint16_t object = 0; };
    std::vector<Vertex> vertices;                     // vertices under a liquid layer vertex
    struct Tile { std::uint32_t tile = 0; bool emissive = false; std::array<float, 3> emission{}; };
    std::vector<Tile> tiles;                          // tiles holding a liquid layer tile, map order
};

struct TerrainGrids {
    std::uint32_t tiles_x = 0, tiles_y = 0;
    std::vector<std::uint8_t> area, types;   // per tile
    std::vector<float> height, ground;       // per vertex: (tiles*2+1)^2
    std::vector<emissive::Tile> emissive;    // wet self-illuminated liquid tiles, map order
    LiquidLevelInputs level;
};

// Liquid-surface heights, wet area classes and wet emissive tiles for these
// layer shifts (null: authored rest heights), in place over height/area that
// build_terrain_grids produced from the same inputs.
inline bool apply_liquid_level(const LiquidLevelInputs& level, const std::vector<float>* shifts,
    std::vector<float>& height, std::vector<std::uint8_t>& area, std::vector<emissive::Tile>& tiles) {
    const std::uint32_t verts_x = level.tiles_x*2u+1u;
    for (const auto& vertex : level.vertices) {
        float liquid = vertex.rest;
        if (shifts) { if (vertex.object >= shifts->size()) return false; liquid += (*shifts)[vertex.object]; }
        const float z = std::max(vertex.ground, liquid);
        height[vertex.cell] = std::isfinite(z) ? z : 0.0f;
    }
    // A liquid layer tile counts only where its liquid lies over the ground at
    // one of its vertices: elsewhere the drawn surface is the base terrain.
    tiles.clear();
    for (const auto& tile : level.tiles) {
        const std::uint32_t x = tile.tile%level.tiles_x, y = tile.tile/level.tiles_x;
        bool wet = false;
        for (std::uint32_t vy = 2*y; vy <= 2*y+2 && !wet; ++vy)
            for (std::uint32_t vx = 2*x; vx <= 2*x+2 && !wet; ++vx)
                wet = height[std::size_t(vy)*verts_x + vx] > level.ground[std::size_t(vy)*verts_x + vx] + .05f;
        const std::uint8_t dry = level.dry_area[tile.tile];
        area[tile.tile] = wet ? static_cast<std::uint8_t>(level.liquid_area[tile.tile] | (dry & 0xf0u)) : dry;
        if (!wet || !tile.emissive) continue;
        emissive::Tile t; t.x = x; t.y = y; t.z = height[std::size_t(2*y+1)*verts_x + 2*x+1];
        t.emission = tile.emission;
        tiles.push_back(t);
    }
    return true;
}

// Immutable liquid/height evidence only: wet bands within 4 m of water or
// swamp, strongest below nearby rising rock. No open-water source => no drip.
// No overhead geometry is available in these grids, so do not invent ceilings.
inline float cave_drip_at(const TerrainGrids& grids, std::uint32_t x, std::uint32_t y) noexcept {
    const auto here = grids.area[std::size_t(y)*grids.tiles_x+x] & 15u;
    if (here == area_lava || here == area_snow) return 0;
    const std::uint32_t stride = grids.tiles_x*2u+1u;
    const auto height = [&](int vx, int vy) {
        vx = std::clamp(vx,0,int(grids.tiles_x*2u));
        vy = std::clamp(vy,0,int(grids.tiles_y*2u));
        return grids.ground[std::size_t(vy)*stride+vx];
    };
    const int cx = int(x)*2+1, cy = int(y)*2+1;
    const float z = height(cx,cy);
    float wet = 0, wall = 0;
    for (int dy=-2;dy<=2;++dy) for (int dx=-2;dx<=2;++dx) {
        const int tx=int(x)+dx, ty=int(y)+dy;
        if (tx<0 || ty<0 || tx>=int(grids.tiles_x) || ty>=int(grids.tiles_y)) continue;
        const float distance = 2.f*std::sqrt(float(dx*dx+dy*dy));
        const auto area = grids.area[std::size_t(ty)*grids.tiles_x+tx] & 15u;
        if (area == area_water || area == area_swamp)
            wet = std::max(wet,std::clamp(1.f-distance/4.f,0.f,1.f));
        if (distance > 0 && distance <= 4)
            wall = std::max(wall,std::clamp((height(tx*2+1,ty*2+1)-z)/distance-.8f,0.f,1.f));
    }
    return wet*(.15f+.85f*wall);
}

// Atlas slot of each 2 m tile's base (non-layer) tile; 0xffff without one.
inline std::vector<std::uint16_t> terrain_base_slots(const TerrainCpuAsset& terrain) {
    const std::uint32_t tiles_x = terrain.sector_count_x*16u, tiles_y = terrain.sector_count_y*16u;
    std::vector<std::uint16_t> slots(std::size_t(tiles_x)*tiles_y, 0xffff);
    constexpr std::uint32_t layer = 33u*33u;
    for (const auto& sector : terrain.sectors) {
        if (sector.map_x >= terrain.sector_count_x || sector.map_y >= terrain.sector_count_y) continue;
        for (const auto& tile : sector.tiles) {
            const std::uint32_t first = tile.vertex_indices[0];
            if (first >= layer) continue;
            const std::uint32_t gx = sector.map_x*16u + (first%33u)/2u, gy = sector.map_y*16u + (first/33u)/2u;
            if (gx >= tiles_x || gy >= tiles_y || tile.tile_index >= terrain.tile_types.size() || tile.tile_index >= 0xffffu) continue;
            slots[std::size_t(gy)*tiles_x + gx] = static_cast<std::uint16_t>(tile.tile_index);
        }
    }
    return slots;
}

// Base terrain type per tile from its slot: ETERRTYPE in the low 4 bits, 0xff
// without a base tile, bit 4 for a slot with a sand corner family. Ground
// cover's gcBaseTypeAt keeps the original type; placement uses the family
// bits only for cover habitat.
constexpr std::uint8_t kSandFamilyType = 16;
constexpr std::uint8_t kBareFamilyType = 32; // fully classified soil/hard atlas slot
constexpr unsigned kBareCornerShift = 8;     // terrain_cover_types: bare world corners
constexpr unsigned kSoftSandCornerShift = 12; // terrain_cover_types: soft sand world corners
inline std::vector<std::uint8_t> terrain_base_types(const std::vector<std::uint16_t>& slots,
    const std::vector<std::uint32_t>& tile_types, const std::vector<bool>& sand_slots) {
    std::vector<std::uint8_t> types(slots.size(), 0xff);
    for (std::size_t g = 0; g < slots.size(); ++g) {
        const std::uint16_t slot = slots[g];
        if (slot >= tile_types.size()) continue;
        const bool sand = slot < sand_slots.size() && sand_slots[slot];
        types[g] = static_cast<std::uint8_t>((tile_types[slot] & 15u) | (sand ? kSandFamilyType : 0u));
    }
    return types;
}

// Ground cover's base type grid (R16) once the map's tile families load:
// terrain_base_types, kBareFamilyType where all four corners are soil/hard,
// bits 8-11 the world corners (cy*2+cx) whose family is soil/hard, and bits
// 12-15 those whose family is soft sand (terrain_tile_soft_sand_corners).
// lanes: per tile, the signature corner at each world corner (2 bits each,
// terrain_tile_world_corners); bare_corners: per slot, bit k for signature
// corner k (terrain_tile_bare_corners).
inline std::vector<std::uint16_t> terrain_cover_types(const std::vector<std::uint16_t>& slots,
    const std::vector<std::uint8_t>& lanes, const std::vector<std::uint32_t>& tile_types,
    const std::vector<bool>& sand_slots, const std::vector<std::uint8_t>& bare_corners,
    const std::vector<std::uint8_t>& soft_sand_corners = {}) {
    const auto base = terrain_base_types(slots, tile_types, sand_slots);
    std::vector<std::uint16_t> types(base.begin(), base.end());
    for (std::size_t g = 0; g < slots.size(); ++g) {
        const std::uint16_t slot = slots[g];
        if (base[g] == 0xff || g >= lanes.size()) continue;
        unsigned world = 0, soft = 0;
        for (unsigned k = 0; k < 4; ++k) {
            const unsigned corner = (lanes[g] >> (2 * k)) & 3u;
            if (slot < bare_corners.size() && ((bare_corners[slot] >> corner) & 1u)) world |= 1u << k;
            if (slot < soft_sand_corners.size() && ((soft_sand_corners[slot] >> corner) & 1u)) soft |= 1u << k;
        }
        types[g] = static_cast<std::uint16_t>(types[g] | (world == 15u ? kBareFamilyType : 0u) | (world << kBareCornerShift) |
            (soft << kSoftSandCornerShift));
    }
    return types;
}

inline bool build_terrain_grids(const TerrainCpuAsset& terrain, const OwnedMapSnapshot* map, TerrainGrids& out,
    const OwnedTerrainGeometryValues* geometry = nullptr) {
    const std::uint32_t tiles_x = terrain.sector_count_x*16u, tiles_y = terrain.sector_count_y*16u;
    const std::uint32_t verts_x = terrain.sector_count_x*32u+1u, verts_y = terrain.sector_count_y*32u+1u;
    if (!tiles_x || !tiles_y) return false;
    auto& level = out.level;
    level = {};level.tiles_x = tiles_x;
    std::vector<std::uint8_t> base(std::size_t(tiles_x)*tiles_y, area_bare), liquid(base.size(), 0xff);
    std::vector<std::uint16_t> emissive_object(base.size(), 0xffff);
    std::vector<float> ground(std::size_t(verts_x)*verts_y, 0.0f);
    constexpr std::uint32_t layer = 33u*33u;
    for (const auto& sector : terrain.sectors) {
        if (sector.map_x >= terrain.sector_count_x || sector.map_y >= terrain.sector_count_y) continue;
        for (const auto& tile : sector.tiles) {
            const std::uint32_t first = tile.vertex_indices[0];
            const bool is_layer = first >= layer;
            const std::uint32_t local = first % layer;
            const std::uint32_t gx = sector.map_x*16u + (local%33u)/2u, gy = sector.map_y*16u + (local/33u)/2u;
            if (gx >= tiles_x || gy >= tiles_y || tile.tile_index >= terrain.tile_types.size()) continue;
            const std::uint32_t type = terrain.tile_types[tile.tile_index];
            const std::size_t g = std::size_t(gy)*tiles_x + gx;
            if (!is_layer) base[g] = area_from_terrain_type(type);
            else if (tile.object_id < terrain.objects.size() && terrain_liquid_object(terrain.objects[tile.object_id].type)) {
                liquid[g] = liquid_area(type, terrain.objects[tile.object_id].self_illumination);
                if (terrain.objects[tile.object_id].self_illumination > 0.0f) emissive_object[g] = tile.object_id;
            }
        }
        for (std::uint32_t i = 0; i < layer && i < sector.vertices.size(); ++i) {
            const std::uint32_t x = sector.map_x*32u + i%33u, y = sector.map_y*32u + i/33u;
            if (x >= verts_x || y >= verts_y) continue;
            const std::size_t cell = std::size_t(y)*verts_x + x;
            if (std::isfinite(sector.vertices[i].z)) ground[cell] = sector.vertices[i].z;
            if (sector.has_layers && i+layer < sector.vertices.size()) {
                const auto& top = sector.vertices[i+layer];
                if (top.lighting_object < terrain.objects.size() && terrain_liquid_object(terrain.objects[top.lighting_object].type))
                    level.vertices.push_back({static_cast<std::uint32_t>(cell), sector.vertices[i].z, top.z, top.lighting_object});
            }
        }
    }
    std::vector<std::uint8_t> trees(base.size(), 0);
    if (map) for (const auto& object : map->objects) {
        if (!leafy_tree_model(object.visual_reference.c_str())) continue;
        const int cx = static_cast<int>(std::floor(object.position[0]*.5f)), cy = static_cast<int>(std::floor(object.position[1]*.5f));
        for (int dy = -3; dy <= 3; ++dy) for (int dx = -3; dx <= 3; ++dx) {
            const int x = cx+dx, y = cy+dy;
            if (x < 0 || y < 0 || x >= int(tiles_x) || y >= int(tiles_y) || dx*dx+dy*dy > 10) continue;
            auto& count = trees[std::size_t(y)*tiles_x + x];
            if (count < 15) ++count;
        }
    }
    level.dry_area.resize(base.size());
    for (std::size_t i = 0; i < base.size(); ++i)
        level.dry_area[i] = static_cast<std::uint8_t>(base[i] | (trees[i] << 4));
    for (std::size_t g = 0; g < liquid.size(); ++g) {
        if (liquid[g] == 0xff) continue;
        LiquidLevelInputs::Tile tile; tile.tile = static_cast<std::uint32_t>(g);
        if (emissive_object[g] != 0xffff) {
            const auto& o = terrain.objects[emissive_object[g]];
            tile.emissive = true;
            for (int a = 0; a < 3; ++a) tile.emission[a] = std::max(o.color[a], 0.0f) * o.self_illumination;
        }
        level.tiles.push_back(tile);
    }
    level.liquid_area = std::move(liquid);
    level.ground = ground;
    out.height = ground;out.area = level.dry_area;out.emissive.clear();
    if (!apply_liquid_level(level, geometry ? &geometry->layer_shifts : nullptr, out.height, out.area, out.emissive))
        return false;
    out.types = terrain_base_types(terrain_base_slots(terrain), terrain.tile_types, {}); out.ground = std::move(ground);
    out.tiles_x = tiles_x; out.tiles_y = tiles_y;
    return true;
}

} // namespace owned::ambient
