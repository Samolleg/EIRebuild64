#pragma once
// Procedural waterfalls. The map's authored liquid triangles of a fall are
// often a diagonal ramp or a staircase on the 2 m grid; the renderer omits
// those (visual only: the game's liquid data and movement are untouched) and
// draws its own fall sheet: a lip roll-over and a ballistic sheet kept in
// front of the rock, built from the map's own liquid and ground heights.
//
// Detection: a fall edge joins two visible liquid vertices 1 m apart whose
// surface heights differ by kFallEdgeDrop or more (steeper than any rapid).
// Connected fall vertices form one fall; its top and bottom levels, flow
// direction, lip line (perpendicular to the flow) and width come from the
// liquid around it. A step whose ledge water is level separates into its own
// fall, so multi-step falls are a chain of falls. Identity: the fall's lowest
// grid vertex (stable for a map). Shared by the renderer and the shader lab.
#include "engine_bridge.h"
#include "liquid_surface_shader.h"
#include "owned_asset_loader.h"
#include "owned_terrain_front_end.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <atomic>
#include <limits>
#include <memory>
#include <vector>

namespace owned {

inline constexpr float kFallEdgeDrop = kLiquidFallEdgeDrop; // m of surface height across a 1 m grid edge
inline constexpr float kFallMinimumDrop = 2.5f; // smallest fall worth replacing: steps below stay rapids (m)
inline constexpr float kFallMaximumWidth = 30.f; // wider steps are terrain seams, not falls (m)
inline constexpr float kFallMinimumSteepness = 1.4f; // drop per m of run from lip to pool: below, a steep chute (kept authored)
inline constexpr float kFallLipOverlap = .7f; // m the sheet starts upstream of the lip, over the river
inline constexpr float kFallFlowSpeed = 1.6f; // m/s of water leaving the lip (sheet trajectory)
inline constexpr float kFallBelowPool = .3f;  // m the sheet ends below the pool surface (inside the boil)
inline constexpr float kFallRockClearance = .2f; // m the sheet stays in front of the rock
inline constexpr float kFallLevelMargin = .3f; // m off the top and pool levels: liquid between them is part of the drop
// Terrain vertex flags of falls (engine_bridge.h NativeTerrainVertexFlag
// leaves bits 6 and 7 free): an authored liquid vertex on a fall edge, and a
// vertex of a generated fall sheet.
inline constexpr std::uint32_t kTerrainFallVertex = native_terrain_fall_vertex;
inline constexpr std::uint32_t kTerrainGeneratedFall = native_terrain_generated_fall;
static_assert(kTerrainFallVertex == 64u && kTerrainGeneratedFall == 128u, "native_shader.h tests these literals");

// Visible liquid surface and ground heights on the map's 1 m vertex grid
// (authored rest heights).
struct LiquidHeightGrid {
    int verts_x = 0, verts_y = 0;
    std::vector<float> liquid, ground; // liquid: NaN where no visible liquid
    std::vector<float> layer; // liquid surface also where buried under the ground (NaN: no liquid)
    std::vector<std::uint16_t> object;
    bool valid(int x, int y) const { return x >= 0 && y >= 0 && x < verts_x && y < verts_y && !std::isnan(liquid[std::size_t(y) * verts_x + x]); }
    float at(int x, int y) const { return liquid[std::size_t(y) * verts_x + x]; }
    float ground_at(float x, float y) const {
        x = std::clamp(x, 0.f, float(verts_x - 1)); y = std::clamp(y, 0.f, float(verts_y - 1));
        const int ix = std::min(int(x), verts_x - 2), iy = std::min(int(y), verts_y - 2);
        const float fx = x - ix, fy = y - iy;
        const auto g = [&](int a, int b) { return ground[std::size_t(b) * verts_x + a]; };
        return (g(ix, iy) * (1 - fx) + g(ix + 1, iy) * fx) * (1 - fy) + (g(ix, iy + 1) * (1 - fx) + g(ix + 1, iy + 1) * fx) * fy;
    }
};

inline LiquidHeightGrid build_liquid_height_grid(const TerrainCpuAsset& terrain) {
    LiquidHeightGrid grid;
    grid.verts_x = int(terrain.sector_count_x * 32 + 1); grid.verts_y = int(terrain.sector_count_y * 32 + 1);
    const std::size_t n = std::size_t(grid.verts_x) * grid.verts_y;
    grid.liquid.assign(n, std::numeric_limits<float>::quiet_NaN());
    grid.ground.assign(n, 0.f); grid.object.assign(n, UINT16_MAX);
    grid.layer.assign(n, std::numeric_limits<float>::quiet_NaN());
    for (const auto& sector : terrain.sectors) {
        if (sector.vertices.size() < 1089) continue;
        for (unsigned i = 0; i < 1089; ++i) {
            const int x = int(sector.map_x * 32 + i % 33), y = int(sector.map_y * 32 + i / 33);
            if (x >= grid.verts_x || y >= grid.verts_y) continue;
            const std::size_t cell = std::size_t(y) * grid.verts_x + x;
            const float ground = sector.vertices[i].z;
            grid.ground[cell] = std::isfinite(ground) ? ground : 0.f;
            if (!sector.has_layers || sector.vertices.size() < 2178) continue;
            const auto& layer = sector.vertices[i + 1089];
            if (layer.lighting_object >= terrain.objects.size() || !terrain_liquid_object(terrain.objects[layer.lighting_object].type) ||
                terrain.objects[layer.lighting_object].self_illumination > 0) continue;
            const float z = layer.z;
            if (!std::isfinite(z)) continue;
            grid.layer[cell] = z;
            if (z < ground - kLiquidBuriedTolerance) continue;
            grid.liquid[cell] = z; grid.object[cell] = layer.lighting_object;
        }
    }
    return grid;
}

// A vertex on a fall edge (pure function of the global grid, so every copy
// of a shared vertex classifies alike).
inline bool liquid_fall_vertex(const LiquidHeightGrid& grid, int x, int y) {
    return liquid_fall_vertex_at([&](int px, int py, float& z) { if (!grid.valid(px, py)) return false; z = grid.at(px, py); return true; },
                                 x, y, kFallEdgeDrop);
}

struct ProceduralFall {
    std::uint32_t id = 0;           // lowest grid vertex index of the fall
    std::array<float, 2> lip{};      // lip line centre (world xy)
    std::array<float, 2> dir{};      // flow direction (unit, horizontal)
    float width = 0, top = 0, bottom = 0;
    std::uint16_t object = UINT16_MAX;        // liquid object of the top level
    std::uint16_t bottom_object = UINT16_MAX; // liquid object of the pool
    std::uint32_t vertices = 0;      // authored fall vertices it replaces
    float steepness = 0;             // drop over the horizontal run from the lip to the pool
    // The liquid terrain command that carries the sheet: the sector, layer
    // material and subpart of the water tile nearest the sheet's middle.
    std::uint32_t host_sector = UINT32_MAX;
    std::uint16_t host_material = 0;
    std::uint8_t host_part = 0;
};

// cells (optional): the grid vertices of each detected fall, parallel to the result.
inline std::vector<ProceduralFall> detect_procedural_falls(const LiquidHeightGrid& grid, std::vector<std::vector<int>>* cells = nullptr) {
    std::vector<ProceduralFall> falls;
    const int w = grid.verts_x, h = grid.verts_y;
    std::vector<std::uint8_t> fall(std::size_t(w) * h, 0), seen(std::size_t(w) * h, 0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) fall[std::size_t(y) * w + x] = liquid_fall_vertex(grid, x, y) ? 1 : 0;
    std::vector<int> stack, cluster;
    for (int y0 = 0; y0 < h; ++y0) for (int x0 = 0; x0 < w; ++x0) {
        const std::size_t start = std::size_t(y0) * w + x0;
        if (!fall[start] || seen[start]) continue;
        cluster.clear(); stack.assign(1, int(start)); seen[start] = 1;
        while (!stack.empty()) {
            const int c = stack.back(); stack.pop_back(); cluster.push_back(c);
            const int cx = c % w, cy = c / w;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= w || y >= h) continue;
                const std::size_t k = std::size_t(y) * w + x;
                if (fall[k] && !seen[k]) { seen[k] = 1; stack.push_back(int(k)); }
            }
        }
        // Levels: the level water around the fall (not on a fall edge,
        // within 2 m) splits into its top and bottom.
        if (cluster.size() < 4) continue;
        // The map's outer liquid dropping off its border is no fall.
        bool border = false;
        for (const int c : cluster) {
            const int x = c % w, y = c / w;
            border = border || x < 3 || y < 3 || x > w - 4 || y > h - 4;
        }
        if (border) continue;
        std::vector<float> heights;
        for (const int c : cluster) heights.push_back(grid.liquid[std::size_t(c)]);
        std::sort(heights.begin(), heights.end());
        if (heights.back() - heights.front() < kFallMinimumDrop) continue;
        const float mid = (heights.back() + heights.front()) * .5f;
        std::vector<std::uint8_t> ring_mark(cluster.size() * 25, 0);
        std::vector<float> tops, bottoms;
        double tx = 0, ty = 0, bx = 0, by = 0; unsigned tn = 0, bn = 0;
        std::vector<int> ring;
        for (const int c : cluster) {
            const int cx = c % w, cy = c / w;
            for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
                const int x = cx + dx, y = cy + dy;
                if (!grid.valid(x, y)) continue;
                const std::size_t k = std::size_t(y) * w + x;
                if (std::find(ring.begin(), ring.end(), int(k)) != ring.end()) continue;
                ring.push_back(int(k));
                if (fall[k]) continue;
                const float z = grid.at(x, y);
                if (z >= mid) { tops.push_back(z); tx += x; ty += y; ++tn; } else { bottoms.push_back(z); bx += x; by += y; ++bn; }
            }
        }
        if (!tn || !bn) continue;
        std::sort(tops.begin(), tops.end()); std::sort(bottoms.begin(), bottoms.end());
        const float top = tops[tops.size() / 2], bottom = bottoms[bottoms.size() / 2];
        if (top - bottom < kFallMinimumDrop) continue;
        float dir[2] = {float(bx / bn - tx / tn), float(by / bn - ty / tn)};
        const float len = std::hypot(dir[0], dir[1]);
        if (len < 1e-3f) continue;
        dir[0] /= len; dir[1] /= len;
        const float side[2] = {-dir[1], dir[0]};
        // Lip: the downstream-most water still at the top level; width: the
        // fall vertices across the flow.
        float lip_along = -1e30f, across_low = 1e30f, across_high = -1e30f;
        std::uint32_t id = std::uint32_t(cluster.front());
        std::uint16_t object = UINT16_MAX, bottom_object = UINT16_MAX;
        for (const int k : ring) {
            const int x = k % w, y = k / w;
            if (grid.at(x, y) >= top - .4f) { lip_along = std::max(lip_along, x * dir[0] + y * dir[1]); object = grid.object[std::size_t(k)]; }
            else if (!fall[std::size_t(k)] && grid.at(x, y) <= bottom + .4f) bottom_object = grid.object[std::size_t(k)];
        }
        for (const int c : cluster) {
            const int x = c % w, y = c / w;
            id = std::min(id, std::uint32_t(c));
            const float across = x * side[0] + y * side[1];
            across_low = std::min(across_low, across); across_high = std::max(across_high, across);
        }
        if (object == UINT16_MAX || bottom_object == UINT16_MAX) continue;
        // Steepness: the drop over the horizontal run from the lip to the
        // nearest pool water below it. A steep chute (rapids sloping
        // all the way down) stays the map's own mesh.
        float foot_along = 1e30f;
        for (const int k : ring) {
            const int x = k % w, y = k / w;
            const float across = x * side[0] + y * side[1];
            if (!fall[std::size_t(k)] && grid.at(x, y) <= bottom + .4f && across >= across_low - .5f && across <= across_high + .5f)
                foot_along = std::min(foot_along, x * dir[0] + y * dir[1]);
        }
        ProceduralFall f;
        f.steepness = (top - bottom) / std::max(foot_along - lip_along, .5f);
        f.id = id; f.dir = {dir[0], dir[1]};
        const float across_mid = (across_low + across_high) * .5f;
        f.lip = {dir[0] * lip_along + side[0] * across_mid, dir[1] * lip_along + side[1] * across_mid};
        f.width = across_high - across_low + 1.f;
        f.top = top; f.bottom = bottom;
        // The sheet spans only where the river actually reaches the lip:
        // the run of top-level water just upstream of it, across the flow.
        {
            const auto top_water = [&](float across) {
                const float x = f.lip[0] - dir[0] * .8f + side[0] * across, y = f.lip[1] - dir[1] * .8f + side[1] * across;
                const int gx = int(std::lround(x)), gy = int(std::lround(y));
                return grid.valid(gx, gy) && grid.at(gx, gy) >= top - .6f;
            };
            float low = 0, high = 0;
            if (top_water(0)) {
                while (low > -f.width && top_water(low - .25f)) low -= .25f;
                while (high < f.width && top_water(high + .25f)) high += .25f;
                const float shift = (low + high) * .5f;
                f.lip = {f.lip[0] + side[0] * shift, f.lip[1] + side[1] * shift};
                f.width = std::min(f.width, high - low + .5f);
            }
        }
        f.object = object; f.bottom_object = bottom_object; f.vertices = std::uint32_t(cluster.size());
        if (f.width > kFallMaximumWidth || f.steepness < kFallMinimumSteepness) continue;
        if (cells) cells->push_back(cluster);
        falls.push_back(f);
    }
    return falls;
}

// Generated sheet of one fall: decode-target arrays the native world pass
// prepares like terrain (positions/normals in groups of four, components).
struct ProceduralFallMesh {
    std::vector<NativePackedGroup> positions, normals;
    std::vector<NativeTextureCoordinate> uvs;
    std::vector<NativeVertexComponent> components;
    std::vector<NativeTerrainVertex> terrain;
    std::vector<std::uint16_t> indices;
    std::uint32_t vertex_count = 0;
    float inside_rock = 0; // share of the falling vertices left inside the rock
};

inline bool build_procedural_fall_mesh(const ProceduralFall& f, const LiquidHeightGrid& grid, const TerrainCpuAsset& terrain,
                                       ProceduralFallMesh& out) {
    out = {};
    if (f.object >= terrain.objects.size()) return false;
    const auto& object = terrain.objects[f.object];
    const float side[2] = {-f.dir[1], f.dir[0]};
    // Path along the flow from the lip: level over the river, then ballistic
    // (horizontal kFallFlowSpeed, gravity) down to below the pool surface.
    std::vector<std::array<float, 2>> path; // {along, height}
    path.push_back({-kFallLipOverlap, f.top}); path.push_back({-kFallLipOverlap * .5f, f.top}); path.push_back({0, f.top});
    const float end = f.bottom - kFallBelowPool;
    for (float t = .05f;; t += .02f) {
        const float along = kFallFlowSpeed * t, z = f.top - 4.905f * t * t;
        const auto& last = path.back();
        if (std::hypot(along - last[0], z - last[1]) >= .35f || z <= end) path.push_back({along, std::max(z, end)});
        if (z <= end) break;
        if (t > 4) break;
    }
    const int rows = int(path.size()), columns = std::max(3, int(std::ceil(f.width / .5f)) + 1);
    if (rows * columns > 60000) return false;
    out.vertex_count = std::uint32_t(rows * columns);
    std::vector<std::array<float, 3>> p(out.vertex_count);
    std::uint32_t falling = 0, inside = 0;
    for (int c = 0; c < columns; ++c) {
        const float u = float(c) / (columns - 1), across = (u - .5f) * f.width;
        float push = 0; // extra distance out from the rock, never decreasing down the sheet
        for (int r = 0; r < rows; ++r) {
            float along = path[r][0] + push; const float z = path[r][1];
            if (r >= 3)
                for (int k = 0; k < 15; ++k) { // at most 1.5 m out from the rock
                    const float x = f.lip[0] + f.dir[0] * along + side[0] * across, y = f.lip[1] + f.dir[1] * along + side[1] * across;
                    if (grid.ground_at(x, y) < z - kFallRockClearance) break;
                    along += .1f; push += .1f;
                }
            p[std::size_t(r) * columns + c] = {f.lip[0] + f.dir[0] * along + side[0] * across,
                                               f.lip[1] + f.dir[1] * along + side[1] * across, z};
            if (r >= 3 && z > f.bottom + kFallLevelMargin) {
                ++falling;
                const auto& q = p[std::size_t(r) * columns + c];
                inside += grid.ground_at(q[0], q[1]) > z;
            }
        }
    }
    out.inside_rock = falling ? float(inside) / float(falling) : 0.f;
    const std::uint32_t groups = (out.vertex_count + 3) / 4;
    out.positions.assign(groups, {}); out.normals.assign(groups, {});
    out.uvs.assign(out.vertex_count, {}); out.components.assign(out.vertex_count, {}); out.terrain.assign(out.vertex_count, {});
    for (int r = 0; r < rows; ++r) for (int c = 0; c < columns; ++c) {
        const std::size_t k = std::size_t(r) * columns + c;
        const auto& a = p[std::size_t(std::max(r - 1, 0)) * columns + c];
        const auto& b = p[std::size_t(std::min(r + 1, rows - 1)) * columns + c];
        const auto& l = p[std::size_t(r) * columns + std::max(c - 1, 0)];
        const auto& q = p[std::size_t(r) * columns + std::min(c + 1, columns - 1)];
        const float dv[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, du[3] = {q[0] - l[0], q[1] - l[1], q[2] - l[2]};
        float nrm[3] = {du[1] * dv[2] - du[2] * dv[1], du[2] * dv[0] - du[0] * dv[2], du[0] * dv[1] - du[1] * dv[0]};
        const float nl = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
        for (auto& v : nrm) v = nl > 1e-6f ? v / nl : 0.f;
        if (nrm[2] < 0) for (auto& v : nrm) v = -v;
        const std::size_t g = k / 4, lane = k % 4;
        out.positions[g].x[lane] = p[k][0]; out.positions[g].y[lane] = p[k][1]; out.positions[g].z[lane] = p[k][2]; out.positions[g].w[lane] = 1;
        out.normals[g].x[lane] = nrm[0]; out.normals[g].y[lane] = nrm[1]; out.normals[g].z[lane] = nrm[2];
        out.uvs[k] = {float(c) / (columns - 1), float(r) / (rows - 1)};
        out.components[k] = {std::uint16_t(k), std::uint16_t(k), std::uint16_t(k)};
        auto& v = out.terrain[k];
        v.flags = native_terrain_water_vertex | kTerrainFallVertex | kTerrainGeneratedFall;
        for (int a3 = 0; a3 < 3; ++a3) { v.object_color[a3] = object.color[a3]; v.emissive[a3] = 0; }
        v.opacity = object.opacity;
        // Depth drives the fall's edge feather: thin at the sides and where
        // the sheet starts over the river, so both blend in softly.
        const float side_distance = std::min(float(c), float(columns - 1 - c)) / (columns - 1) * f.width;
        const float start_distance = path[r][0] + kFallLipOverlap;
        // ... and below the pool surface, so the sheet's end melts into the boil.
        const float end_distance = (p[k][2] - (f.bottom - kFallBelowPool)) * 1.5f;
        v.depth = std::max(std::min({side_distance * .8f, start_distance * .9f + .05f, end_distance}), 0.f);
        v.liquid_depth = 2.f;
        v.liquid_multiplier = object.wave_multiplier;
        v.liquid_cascade = 1.f;
        v.liquid_current[0] = f.dir[0] * 1.5f; v.liquid_current[1] = f.dir[1] * 1.5f;
        v.liquid_self_illumination = object.self_illumination;
        v.liquid_drop = std::max(f.top - p[k][2], 0.f);
        v.animation_index = ~0u;
        // Live level shifts (terrainLayerShift): the upper half follows the
        // river's liquid object, the lower half the pool's.
        v.shift_objects = 0xffff0000u | (p[k][2] >= (f.top + f.bottom) * .5f ? f.object : f.bottom_object);
    }
    for (int r = 0; r + 1 < rows; ++r) for (int c = 0; c + 1 < columns; ++c) {
        const std::uint16_t a = std::uint16_t(r * columns + c), b = std::uint16_t(a + 1), d = std::uint16_t(a + columns), e = std::uint16_t(d + 1);
        out.indices.insert(out.indices.end(), {a, d, b, b, d, e});
    }
    return true;
}

// The water tile nearest a point (world xy, within 4 m): its sector,
// layer material and subpart carry a generated sheet in that liquid command.
inline bool procedural_fall_host(const TerrainCpuAsset& terrain, float px, float py, ProceduralFall& f) {
    float best = 16.f; // squared m
    const std::uint32_t first_layer = terrain.texture_count + 2u;
    for (std::uint32_t s = 0; s < terrain.sectors.size(); ++s) {
        const auto& sector = terrain.sectors[s];
        const float sx = float(sector.map_x * 32), sy = float(sector.map_y * 32);
        if (px < sx - 4 || py < sy - 4 || px > sx + 36 || py > sy + 36) continue;
        for (std::uint32_t m = first_layer; m < sector.materials.size(); ++m)
            for (std::uint8_t part = 0; part < 4; ++part) {
                const auto& range = sector.materials[m].subparts[part];
                for (std::uint32_t t = range.tile_begin; t < std::uint32_t(range.tile_begin) + range.tile_count && t < sector.tiles.size(); ++t) {
                    const auto& tile = sector.tiles[t];
                    if (!tile.transparent || tile.object_id >= terrain.objects.size() || !terrain_liquid_object(terrain.objects[tile.object_id].type)) continue;
                    float cx = 0, cy = 0;
                    for (const auto index : tile.vertex_indices) { cx += float(index % 1089 % 33); cy += float(index % 1089 / 33); }
                    cx = sx + cx / 9; cy = sy + cy / 9;
                    const float d = (cx - px) * (cx - px) + (cy - py) * (cy - py);
                    if (d < best) { best = d; f.host_sector = s; f.host_material = std::uint16_t(m); f.host_part = part; }
                }
            }
    }
    return f.host_sector != UINT32_MAX;
}

// Waterfall shell of one fall: a curtain of its own geometry that follows the
// map's liquid overlay down the fall face, a little off it, from just above
// the lip over the river into the pool. It carries the fall's foam (the
// overlay stays the clear water body); renderer_ambient_particles.cpp draws it.
//
// Columns run along the flow every kFallShellColumn across the fall. Each
// column samples the overlay's surface height (the authored layer heights,
// buried ones included, so the shape stays continuous) along the flow line,
// keeps it non-increasing, smooths the 1 m staircase into one curved face
// and resamples it at equal arc lengths. Vertices: three float4 each,
// {x, y, z, across}, {fall time, arc from the lip, drop below the lip,
// alpha}, {outward normal xyz, seed}; a triangle list.
// Fall time integrates arc length over the water's speed
// sqrt(v0^2 + 2 g drop), so a pattern constant in fall time stretches as the
// water accelerates, also over treads where the height does not change.
// Alpha fades the sides, the start over the river, the end into the pool and
// wherever the overlay is buried under the ground (hidden in the game too).
inline constexpr float kFallShellColumn = .3f;   // m between columns across the fall
inline constexpr float kFallShellRow = .2f;      // m of arc between rows down the fall
inline constexpr float kFallShellLift = .06f;    // m the shell stands off the overlay
inline constexpr float kFallShellUpstream = .8f; // m the shell starts over the river before the lip
inline constexpr float kFallShellPool = .8f;     // m the shell runs on into the pool after the foot
inline void build_fall_shell(const ProceduralFall& f, const LiquidHeightGrid& grid, std::vector<std::array<float,4>>& out) {
    const float side[2] = {-f.dir[1], f.dir[0]};
    // Bilinear surface height (layer heights) and visible share at a world point.
    const auto surface = [&](float x, float y, float& z, float& visible) {
        if (x < 0 || y < 0 || x >= float(grid.verts_x - 1) || y >= float(grid.verts_y - 1)) return false;
        const int ix = int(x), iy = int(y);
        const float fx = x - ix, fy = y - iy;
        float h[4], v[4];
        for (int k = 0; k < 4; ++k) {
            const std::size_t cell = std::size_t(iy + k / 2) * grid.verts_x + std::size_t(ix + k % 2);
            h[k] = grid.layer[cell];
            if (std::isnan(h[k])) return false;
            v[k] = std::isnan(grid.liquid[cell]) ? 0.f : 1.f;
        }
        const float w[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
        z = h[0] * w[0] + h[1] * w[1] + h[2] * w[2] + h[3] * w[3];
        visible = v[0] * w[0] + v[1] * w[1] + v[2] * w[2] + v[3] * w[3];
        return true;
    };
    struct Row { float along = 0, z = 0, arc = 0, tau = 0, visible = 0, n[2]{}; };
    const float half = f.width * .5f + .5f;
    const int columns = std::max(2, int(std::ceil(2 * half / kFallShellColumn)) + 1);
    std::vector<std::vector<Row>> profile(static_cast<std::size_t>(columns));
    constexpr float step = .1f, v0 = kFallFlowSpeed, g = 9.81f, t_begin = -2.f;
    int rows = 0;
    for (int c = 0; c < columns; ++c) {
        const float across = -half + 2 * half * float(c) / float(columns - 1);
        const float bx = f.lip[0] + side[0] * across, by = f.lip[1] + side[1] * across;
        // Surface along the flow line; the lip is where it starts to drop,
        // the foot where it reaches the pool.
        std::vector<float> zs, vis;
        float lip = NAN, foot = NAN;
        for (int k = 0; k <= 180; ++k) {
            const float t = t_begin + step * float(k);
            float z = NAN, visible = 0;
            if (!surface(bx + f.dir[0] * t, by + f.dir[1] * t, z, visible)) {
                if (std::isnan(lip)) { zs.push_back(NAN); vis.push_back(0); continue; }
                break;
            }
            zs.push_back(z); vis.push_back(visible);
            if (std::isnan(lip) && z < f.top - .15f && t > -1.f) lip = t;
            if (!std::isnan(lip) && z <= f.bottom + .12f) { foot = t; break; }
        }
        if (std::isnan(lip) || std::isnan(foot) || foot - lip > 12.f) continue;
        const float t0 = lip - kFallShellUpstream, t1 = foot + kFallShellPool;
        // Non-increasing heights from t0 on (past the foot: the pool level),
        // then a running maximum over +-0.4 m and two box passes over +-0.2 m
        // round the staircase off. The maximum first keeps the rounded face
        // on or outside the overlay everywhere (above its surface at every
        // point along the flow): plain smoothing cut each step's upper corner
        // into the rock, behind the overlay, where the depth test hid it.
        std::vector<float> along, height, visible;
        float running = f.top + 1.f;
        const int count = int(std::lround((t1 - t0) / step)) + 1;
        for (int i = 0; i < count; ++i) {
            const float t = t0 + step * float(i);
            const int k = int(std::lround((t - t_begin) / step));
            const bool sampled = k >= 0 && k < int(zs.size()) && !std::isnan(zs[std::size_t(k)]);
            float z = sampled ? zs[std::size_t(k)] : running;
            if (t > foot) z = std::max(f.bottom, std::min(z, running));
            running = std::min(running, z);
            along.push_back(t); height.push_back(running);
            visible.push_back(sampled ? vis[std::size_t(k)] : (t > foot ? 1.f : 0.f));
        }
        if (along.size() < 3) continue;
        {
            std::vector<float> dilated = height;
            for (std::size_t i = 0; i < height.size(); ++i)
                for (int d = -4; d <= 4; ++d) {
                    const int j = int(i) + d;
                    if (j >= 0 && j < int(height.size())) dilated[i] = std::max(dilated[i], height[std::size_t(j)]);
                }
            height.swap(dilated);
        }
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<float> smooth = height;
            for (std::size_t i = 0; i < height.size(); ++i) {
                float sum = 0; int n = 0;
                for (int d = -2; d <= 2; ++d) {
                    const int j = int(i) + d;
                    if (j >= 0 && j < int(height.size())) { sum += height[std::size_t(j)]; ++n; }
                }
                smooth[i] = sum / float(n);
            }
            height.swap(smooth);
        }
        std::vector<float> arc(along.size(), 0.f);
        for (std::size_t i = 1; i < along.size(); ++i)
            arc[i] = arc[i - 1] + std::hypot(along[i] - along[i - 1], height[i] - height[i - 1]);
        const float lip_arc = arc[std::min(arc.size() - 1, std::size_t(std::lround(kFallShellUpstream / step)))];
        const float total = arc.back();
        const int n = std::max(2, int(std::ceil(total / kFallShellRow)) + 1);
        rows = std::max(rows, n);
        auto& column = profile[std::size_t(c)];
        column.resize(std::size_t(n));
        std::size_t j = 0;
        for (int r = 0; r < n; ++r) {
            const float s = total * float(r) / float(n - 1);
            while (j + 2 < arc.size() && arc[j + 1] < s) ++j;
            const float span = std::max(arc[j + 1] - arc[j], 1e-5f), u = std::clamp((s - arc[j]) / span, 0.f, 1.f);
            Row& row = column[std::size_t(r)];
            row.along = along[j] + (along[j + 1] - along[j]) * u;
            row.z = height[j] + (height[j + 1] - height[j]) * u;
            row.visible = visible[j] + (visible[j + 1] - visible[j]) * u;
            row.arc = s - lip_arc;
            // Outward normal in the (along, height) plane: the tangent turned
            // towards downstream and up.
            const float ta = along[j + 1] - along[j], tz = height[j + 1] - height[j], tl = std::max(std::hypot(ta, tz), 1e-6f);
            row.n[0] = -tz / tl; row.n[1] = ta / tl;
        }
        // Fall time: zero at the lip, the river's speed upstream of it.
        for (int r = 0; r < n; ++r) {
            Row& row = column[std::size_t(r)];
            if (row.arc <= 0 || r == 0) { row.tau = row.arc / v0; continue; }
            const Row& previous = column[std::size_t(r - 1)];
            const float drop = std::max(f.top - row.z, 0.f);
            row.tau = std::max(previous.tau, 0.f) + (row.arc - std::max(previous.arc, 0.f)) / std::sqrt(v0 * v0 + 2 * g * drop);
        }
    }
    if (rows < 2) return;
    // Every column resampled to the fall's row count (by arc share), so the
    // columns join into one grid; then positions are smoothed across
    // neighbouring columns against the ragged tile outline.
    std::vector<std::vector<Row>> mesh(static_cast<std::size_t>(columns));
    for (int c = 0; c < columns; ++c) {
        const auto& column = profile[std::size_t(c)];
        if (column.size() < 2) continue;
        auto& out_column = mesh[std::size_t(c)];
        out_column.resize(std::size_t(rows));
        for (int r = 0; r < rows; ++r) {
            const float x = float(r) / float(rows - 1) * float(column.size() - 1);
            const std::size_t a = std::min(std::size_t(x), column.size() - 2);
            const float u = x - float(a);
            const Row& p = column[a]; const Row& q = column[a + 1];
            Row& o = out_column[std::size_t(r)];
            o.along = p.along + (q.along - p.along) * u; o.z = p.z + (q.z - p.z) * u;
            o.arc = p.arc + (q.arc - p.arc) * u; o.tau = p.tau + (q.tau - p.tau) * u;
            o.visible = p.visible + (q.visible - p.visible) * u;
            o.n[0] = p.n[0] + (q.n[0] - p.n[0]) * u; o.n[1] = p.n[1] + (q.n[1] - p.n[1]) * u;
        }
    }
    {
        auto smoothed = mesh;
        for (int c = 0; c < columns; ++c) {
            if (mesh[std::size_t(c)].empty()) continue;
            for (int r = 0; r < rows; ++r) {
                const Row& centre = mesh[std::size_t(c)][std::size_t(r)];
                float along = 2 * centre.along, z = 2 * centre.z, w = 2;
                for (const int d : {-1, 1}) {
                    const int k = c + d;
                    if (k < 0 || k >= columns || mesh[std::size_t(k)].empty()) continue;
                    along += mesh[std::size_t(k)][std::size_t(r)].along; z += mesh[std::size_t(k)][std::size_t(r)].z; w += 1;
                }
                // Never pulled back behind its own column's face.
                smoothed[std::size_t(c)][std::size_t(r)].along = std::max(along / w, centre.along);
                smoothed[std::size_t(c)][std::size_t(r)].z = std::max(z / w, centre.z);
            }
        }
        mesh.swap(smoothed);
    }
    // Side fade: distance in columns to the nearest column without a profile.
    std::vector<float> side_alpha(static_cast<std::size_t>(columns), 0.f);
    for (int c = 0; c < columns; ++c) {
        if (mesh[std::size_t(c)].empty()) continue;
        int distance = 1;
        while (distance < 4 && c - distance >= 0 && c + distance < columns &&
               !mesh[std::size_t(c - distance)].empty() && !mesh[std::size_t(c + distance)].empty()) ++distance;
        side_alpha[std::size_t(c)] = std::clamp((float(distance) - .5f) / 3.f, 0.f, 1.f);
    }
    const float seed = float(f.id % 997u) * .731f;
    const auto vertex = [&](int c, int r) {
        const Row& row = mesh[std::size_t(c)][std::size_t(r)];
        const float across = -half + 2 * half * float(c) / float(columns - 1);
        const float nx = f.dir[0] * row.n[0], ny = f.dir[1] * row.n[0], nz = row.n[1];
        // The lip stand-off of the tessellated overlay (liquid_surface.hlsl
        // liquidTessLip, liquidTessLipCurve) moves the shell out with it.
        const float stand = .35f * (1 - std::exp(-std::max(f.top - row.z, 0.f) / .8f));
        const float x = f.lip[0] + side[0] * across + f.dir[0] * (row.along + stand) + nx * kFallShellLift;
        const float y = f.lip[1] + side[1] * across + f.dir[1] * (row.along + stand) + ny * kFallShellLift;
        const float z = row.z + nz * kFallShellLift;
        const float start = std::clamp((row.arc + kFallShellUpstream) / (kFallShellUpstream * .8f), 0.f, 1.f);
        const float end = std::clamp((row.z - f.bottom - .02f) / .5f, 0.f, 1.f);
        const float alpha = side_alpha[std::size_t(c)] * start * start * end * std::clamp(row.visible * 1.5f - .25f, 0.f, 1.f);
        out.push_back({x, y, z, across});
        out.push_back({row.tau, row.arc, std::max(f.top - row.z, 0.f), alpha});
        out.push_back({nx, ny, nz, seed});
        return alpha;
    };
    for (int c = 0; c + 1 < columns; ++c) {
        if (mesh[std::size_t(c)].empty() || mesh[std::size_t(c + 1)].empty()) continue;
        for (int r = 0; r + 1 < rows; ++r) {
            const std::size_t mark = out.size();
            float alpha = 0;
            const int corners[6][2] = {{c, r}, {c, r + 1}, {c + 1, r}, {c + 1, r}, {c, r + 1}, {c + 1, r + 1}};
            for (const auto& corner : corners) alpha = std::max(alpha, vertex(corner[0], corner[1]));
            if (alpha <= .01f) out.resize(mark); // fully faded quads draw nothing
        }
    }
}

// The map's detected falls (at the authored rest liquid levels), their
// generated sheets, and which grid vertices belong to them: the liquid
// triangles to omit are those whose every corner belongs to a fall (within
// one vertex of it) across a step as high as a fall edge. Map-wide, so every
// tile, sector and LOD of the same vertex agrees. A fall without a sheet or
// a host command is dropped whole: its authored triangles stay.
struct ProceduralFallMap {
    int verts_x = 0, verts_y = 0;
    std::vector<ProceduralFall> falls;
    std::vector<ProceduralFallMesh> meshes; // parallel to falls
    std::vector<std::uint8_t> member;
    // Waterfall shells of every fall (build_fall_shell): a triangle list,
    // three float4 per vertex.
    std::vector<std::array<float,4>> shells;
    bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < verts_x && y < verts_y && member[std::size_t(y) * verts_x + x] != 0; }
    // Generated vertices and indices a liquid command of this sector,
    // material and subparts carries after its tiles.
    void counts(std::uint32_t sector, std::uint32_t material, std::uint8_t subparts, std::uint32_t& vertices, std::uint32_t& indices) const {
        vertices = indices = 0;
        for (std::size_t n = 0; n < falls.size(); ++n)
            if (falls[n].host_sector == sector && falls[n].host_material == material && (subparts >> falls[n].host_part & 1u)) {
                vertices += meshes[n].vertex_count; indices += std::uint32_t(meshes[n].indices.size());
            }
    }
};
inline std::shared_ptr<const ProceduralFallMap> build_procedural_fall_map(const TerrainCpuAsset& terrain) {
    auto map = std::make_shared<ProceduralFallMap>();
    const auto grid = build_liquid_height_grid(terrain);
    map->verts_x = grid.verts_x; map->verts_y = grid.verts_y;
    map->member.assign(std::size_t(grid.verts_x) * grid.verts_y, 0);
    std::vector<std::vector<int>> cells;
    const auto falls = detect_procedural_falls(grid, &cells);
    for (std::size_t n = 0; n < falls.size(); ++n) {
        auto f = falls[n];
        ProceduralFallMesh mesh;
        if (!build_procedural_fall_mesh(f, grid, terrain, mesh) ||
            !procedural_fall_host(terrain, f.lip[0] + f.dir[0], f.lip[1] + f.dir[1], f)) continue;
        for (const int c : cells[n]) map->member[std::size_t(c)] = 1;
        // Also the authored liquid between the levels under and beside the
        // sheet (ramp remnants and lone spikes in the drop, buried ones too:
        // their triangles still show where they leave the rock): the
        // omission follows the sheet's footprint, not only the steepest edges.
        {
            const float side[2] = {-f.dir[1], f.dir[0]};
            float length = 0;
            for (std::uint32_t k = 0; k < mesh.vertex_count; ++k)
                length = std::max(length, (mesh.positions[k / 4].x[k % 4] - f.lip[0]) * f.dir[0] + (mesh.positions[k / 4].y[k % 4] - f.lip[1]) * f.dir[1]);
            const float reach = length + f.width * .5f + 5.f;
            for (int y = std::max(0, int(f.lip[1] - reach)); y <= std::min(grid.verts_y - 1, int(f.lip[1] + reach)); ++y)
                for (int x = std::max(0, int(f.lip[0] - reach)); x <= std::min(grid.verts_x - 1, int(f.lip[0] + reach)); ++x) {
                    const float z = grid.layer[std::size_t(y) * grid.verts_x + x];
                    if (std::isnan(z)) continue;
                    const float along = (x - f.lip[0]) * f.dir[0] + (y - f.lip[1]) * f.dir[1];
                    const float across = (x - f.lip[0]) * side[0] + (y - f.lip[1]) * side[1];
                    if (along >= -1.5f && along <= length + 2.f && std::abs(across) <= f.width * .5f + 4.f &&
                        z > f.bottom + kFallLevelMargin && z < f.top - kFallLevelMargin)
                        map->member[std::size_t(y) * grid.verts_x + x] = 1;
                }
        }
        build_fall_shell(f, grid, map->shells);
        map->falls.push_back(f); map->meshes.push_back(std::move(mesh));
    }
    // One vertex around each fall too: the authored sheet's ragged edge rows
    // (buried liquid included). Triangles there are omitted only across a
    // fall-high step (shader), so the level river and pool beside a fall
    // keep their own mesh.
    std::vector<std::uint8_t> core = map->member;
    for (int y = 0; y < grid.verts_y; ++y) for (int x = 0; x < grid.verts_x; ++x) {
        if (!core[std::size_t(y) * grid.verts_x + x]) continue;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
            if (x + dx >= 0 && y + dy >= 0 && x + dx < grid.verts_x && y + dy < grid.verts_y) map->member[std::size_t(y + dy) * grid.verts_x + x + dx] = 1;
    }
    return map;
}
// The asset's fall map, derived on first use (immutable input: concurrent
// first uses compute the same map; one is kept). The loader derives it under
// the loading screen (attach_procedural_falls).
inline std::shared_ptr<const ProceduralFallMap> procedural_fall_map(const TerrainCpuAsset& terrain) {
    auto map = std::atomic_load(&terrain.procedural_falls);
    if (!map) { map = build_procedural_fall_map(terrain); std::atomic_store(&terrain.procedural_falls, map); }
    return map;
}

// A terrain command's counts including the generated sheets it carries
// (every site that sizes a terrain command calls this after its tile counts).
inline void add_procedural_fall_counts(const TerrainCpuAsset& terrain, OwnedTerrainCommand& command) {
    if (command.material < terrain.texture_count + 2u) return;
    std::uint32_t vertices = 0, indices = 0;
    procedural_fall_map(terrain)->counts(command.source_sector, command.material, command.subpart_mask, vertices, indices);
    command.vertex_count += vertices; command.index_count += indices;
}

// Derives the map at load (under the loading screen): its falls place the
// fall VFX and particles. The generated sheets are not drawn: the map's own
// liquid overlay keeps every fall's geometry and the liquid surface shader
// gives its steep faces the curtain look (liquid_surface.hlsl).
inline void attach_procedural_falls(TerrainCpuAsset& terrain) {
    (void)procedural_fall_map(terrain);
}

} // namespace owned
