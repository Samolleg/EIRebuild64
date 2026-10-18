#pragma once
// Sea surf field of one map: how exposed each stretch of coast is to open
// water, and the direction its waves come in from. Built once per map from
// the 2 m tile area classes (ambient_terrain_grids.h, water only: swamp, lava
// and dry tiles are land here) and sampled by the liquid surface shader
// (liquid_surface.hlsl liquidSurfAt), so breaking surf shows on sea coasts
// and not on rivers, ponds or sheltered lakes.
//
// Exposure is the effective fetch of coastal engineering (US Army Corps of
// Engineers, Shore Protection Manual): radials fanned around the direction
// facing open water, each marched over water until land, and averaged with a
// cos weight so the radials facing the sea count most. A cosine-weighted mean
// rather than a low percentile: a percentile drops a bay mouth's few open
// radials entirely, while the weighted mean keeps a bay's inner shore
// moderately exposed and a river bank (land within a few metres on most
// radials) near zero. A radial that leaves the map over water is credited by
// the width of the water run along that map edge (liquidSurfEdgeCredit).
// Temporary CPU values: the renderer composites and uploads them.
#include "ambient_particles.h"
#include <algorithm>
#include <string>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace owned {

struct LiquidSurfField {
    std::uint32_t tiles_x = 0, tiles_y = 0;
    // Per tile: r surf 0..1 (the coastal band and, beyond it, open sea water),
    // gb the incoming wave direction (unit, from open water toward the shore;
    // zero out at sea past the band) as .5+.5*xy, a the signed horizontal distance to
    // the waterline (tile borders between water and land): water .25+.75*d/
    // liquidSurfBand (1 beyond the band), land .25-.25*d/liquidSurfLandReach
    // (0 beyond it); liquidSurfDecode in the shader returns metres.
    std::vector<std::array<std::uint8_t,4>> texels;
    std::uint32_t surf_cells = 0;  // tiles with surf above .05
    float max_exposure = 0;        // largest effective fetch (m)
    // Per tile: the sea surface height (water: its liquid vertices; shore land:
    // the sea beside it), liquidSurfNoLevel elsewhere. The beach run-up
    // measures the ground against it.
    std::vector<float> level;
    // Per tile: 0..1 how steep the shore is (ground rising steeply from the
    // waterline, or deep water right at it): waves burst against it instead
    // of breaking on a beach. Spread over the coastal band; 0 elsewhere.
    std::vector<float> impact;
    // Per tile, for the swell's crest geometry and shoaling (smooth, so crests
    // do not bend over every bump of the bed or wobble of the tile coast):
    // water a heavily blurred distance to land (m, up to liquidSurfSwellReach),
    // land minus its distance to water; and on water a blurred water depth (m).
    std::vector<float> swell_distance, swell_depth;
    // Up to three strongest steep-shore spots: map metres x, y, score
    // (surf x impact, 0: none), incoming wave direction (degrees, atan2).
    std::array<std::array<float,4>,3> impact_spots{};
};

// Distances in metres (one tile is 2 m).
constexpr float liquidSurfBand = 25;        // water this close to land takes surf
constexpr float liquidSurfLandReach = 8;    // shore land tiles take the run-up and wet sand
constexpr float liquidSurfCap = 250;        // longest radial (open water)
constexpr float liquidSurfSheltered = 40;   // @param effective fetch below which no surf
constexpr float liquidSurfExposed = 150;    // @param effective fetch of full surf
constexpr int liquidSurfRadials = 13;       // fan of +-60 degrees, 10 degree steps
constexpr float liquidSurfFan = 1.0471976f; // half angle of the fan (rad)
// A radial leaving the map over water: this many times the edge's water run
// width. 3: a 30 m river mouth credits 90 m, under the 150 m of full surf
// even if every radial left there; a sea edge run of 84 m or more reaches the
// 250 m cap.
constexpr float liquidSurfEdgeCredit = 3;
constexpr float liquidSurfNoLevel = -1e4f;  // no sea surface here
constexpr float liquidSurfSpill = 28.f;     // m the sea's surf fades over into other water (no seam)
constexpr float liquidSurfSeaFloor = .35f;  // least surf of any sea water (the sea colour's full weight)
constexpr float liquidSurfSwellReach = 60;  // m; the smooth swell distance is capped here

// The sea: water around an open-water core. Core tiles are water at least
// liquidSeaCore metres from any land (the map edge is not land); a core
// component counts if it covers at least liquidSeaCoreArea and touches the
// map edge along at least liquidSeaEdgeRun. The sea is the water within
// liquidSeaCore metres of such a core, through water. Rivers, canals and
// lakes with islands have no such core (shipped-map scan: zone1 west sea,
// zone18 bay, zone7/8/19 open sea; not the zone1 delta, the zone18 canals,
// zone13/15/4/9/3obr). Also the ground cover's sea habitat
// (renderer_ground_cover.cpp make_sea_srv).
constexpr float liquidSeaCore = 40.f;          // m of open water from land
// Maps excluded from the sea by art direction: the water_surf_no_sea_maps
// setting (comma-separated map names, eid3d11.ini; default zone7, the Orc
// Delta, whose open body passes the core rule). Any map path or name;
// compared as the trimmed lower-case basename.
inline std::string liquid_surf_map_key(std::string name) {
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(0, 1);
    const auto slash = name.find_last_of("\\/");
    if (slash != std::string::npos) name.erase(0, slash + 1);
    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos) name.erase(dot);
    for (char& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return name;
}
inline bool liquid_surf_map_takes_sea(const std::string& name, const std::string& excluded) {
    const std::string key = liquid_surf_map_key(name);
    std::size_t start = 0;
    while (start <= excluded.size()) {
        const std::size_t end = std::min(excluded.find(',', start), excluded.size());
        if (!key.empty() && liquid_surf_map_key(excluded.substr(start, end - start)) == key) return false;
        start = end + 1;
    }
    return true;
}
constexpr float liquidSeaCoreArea = 1500.f;    // m2 of core (zone1 west sea 2144 in the area grid; largest inland lake core 600)
constexpr float liquidSeaEdgeRun = 100.f;      // m of core along the map edge
inline std::vector<std::uint8_t> ground_cover_sea_mask(const std::vector<std::uint8_t>& area,
    std::uint32_t width, std::uint32_t height) {
    const std::size_t count = std::size_t(width) * height;
    std::vector<std::uint8_t> mask(count, 0);
    if (area.size() != count || !count) return mask;
    constexpr float tile = 2.f;
    const auto water = [&](std::size_t i) { return (area[i] & 15u) == ambient::area_water; };
    const int w = int(width), h = int(height);
    // 4-neighbour (city-block) distance (m) from land, as in the shipped-map
    // scan that set the thresholds; the map edge is not land.
    std::vector<float> land(count, 1e9f);
    for (std::size_t i = 0; i < count; ++i) if (!water(i)) land[i] = 0;
    const float d1 = tile, d2 = 1e9f; // no diagonal steps
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        float& d = land[std::size_t(y)*w + x];
        if (x) d = std::min(d, land[std::size_t(y)*w + x-1] + d1);
        if (y) { d = std::min(d, land[std::size_t(y-1)*w + x] + d1);
                 if (x) d = std::min(d, land[std::size_t(y-1)*w + x-1] + d2);
                 if (x+1 < w) d = std::min(d, land[std::size_t(y-1)*w + x+1] + d2); }
    }
    for (int y = h-1; y >= 0; --y) for (int x = w-1; x >= 0; --x) {
        float& d = land[std::size_t(y)*w + x];
        if (x+1 < w) d = std::min(d, land[std::size_t(y)*w + x+1] + d1);
        if (y+1 < h) { d = std::min(d, land[std::size_t(y+1)*w + x] + d1);
                       if (x+1 < w) d = std::min(d, land[std::size_t(y+1)*w + x+1] + d2);
                       if (x) d = std::min(d, land[std::size_t(y+1)*w + x-1] + d2); }
    }
    // Core components that are large and run along the map edge.
    std::vector<std::int32_t> label(count, -1), queue;
    std::vector<float> reach(count, 1e9f); // m through water from a qualifying core
    std::vector<std::int32_t> seeds;
    for (std::size_t start = 0; start < count; ++start) {
        if (land[start] < liquidSeaCore || label[start] >= 0) continue;
        queue.clear(); queue.push_back(std::int32_t(start)); label[start] = std::int32_t(start);
        std::size_t edge = 0;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const int i = queue[head], x = i % w, y = i / w;
            if (x == 0 || y == 0 || x + 1 == w || y + 1 == h) ++edge;
            const int next[4]{x ? i-1 : -1, x+1 < w ? i+1 : -1, y ? i-w : -1, y+1 < h ? i+w : -1};
            for (const int n : next)
                if (n >= 0 && land[std::size_t(n)] >= liquidSeaCore && label[std::size_t(n)] < 0) { label[std::size_t(n)] = std::int32_t(start); queue.push_back(n); }
        }
        if (float(queue.size())*tile*tile >= liquidSeaCoreArea && float(edge)*tile >= liquidSeaEdgeRun)
            for (const auto i : queue) { reach[std::size_t(i)] = 0; seeds.push_back(i); }
    }
    // The sea: water within liquidSeaCore of such a core, through water (two-pass
    // chamfer restricted to water tiles, repeated so it flows around bends).
    if (seeds.empty()) return mask;
    const float e2 = tile*1.41421356f;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y)*w + x; if (!water(i)) continue;
            float& d = reach[i];
            if (x) d = std::min(d, reach[i-1] + d1);
            if (y) { d = std::min(d, reach[i-w] + d1); if (x) d = std::min(d, reach[i-w-1] + e2); if (x+1 < w) d = std::min(d, reach[i-w+1] + e2); }
        }
        for (int y = h-1; y >= 0; --y) for (int x = w-1; x >= 0; --x) {
            const std::size_t i = std::size_t(y)*w + x; if (!water(i)) continue;
            float& d = reach[i];
            if (x+1 < w) d = std::min(d, reach[i+1] + d1);
            if (y+1 < h) { d = std::min(d, reach[i+w] + d1); if (x+1 < w) d = std::min(d, reach[i+w+1] + e2); if (x) d = std::min(d, reach[i+w-1] + e2); }
        }
    }
    for (std::size_t i = 0; i < count; ++i) if (water(i) && reach[i] <= liquidSeaCore) mask[i] = 255;
    return mask;
}

// area: the tile area classes; liquid_area: per tile the liquid layer class
// (0xff none; ambient_terrain_grids.h LiquidLevelInputs). Water counts only
// where a liquid layer lies over the ground: a dry tile whose base terrain
// type is water (riverbed or path art) is land here. height/ground: per
// vertex ((tiles*2+1)^2) liquid-surface-or-ground and ground heights, for
// the sea level the beach run-up measures from.
inline LiquidSurfField build_liquid_surf_field(const std::vector<std::uint8_t>& tile_area,
    const std::vector<std::uint8_t>& liquid_area, const std::vector<float>& height, const std::vector<float>& ground,
    std::uint32_t tiles_x, std::uint32_t tiles_y, bool takes_sea = true) {
    LiquidSurfField out;
    const std::size_t count = std::size_t(tiles_x)*tiles_y;
    const std::size_t verts = std::size_t(tiles_x*2u+1u)*(tiles_y*2u+1u);
    if (!tiles_x || !tiles_y || tile_area.size() != count || liquid_area.size() != count ||
        height.size() != verts || ground.size() != verts) return out;
    std::vector<std::uint8_t> area = tile_area;
    for (std::size_t i = 0; i < count; ++i)
        if ((area[i] & 15u) == ambient::area_water && liquid_area[i] == 0xff)
            area[i] = static_cast<std::uint8_t>((area[i] & 0xf0u) | ambient::area_bare);
    out.tiles_x = tiles_x; out.tiles_y = tiles_y;
    out.texels.assign(count, {0, 128, 128, 255});
    out.level.assign(count, liquidSurfNoLevel);
    out.impact.assign(count, 0.f);
    out.swell_distance.assign(count, 0.f); out.swell_depth.assign(count, 0.f);
    std::vector<std::uint8_t> water(count);
    std::size_t water_count = 0;
    for (std::size_t i = 0; i < count; ++i) water_count += water[i] = (area[i] & 15u) == ambient::area_water;
    for (std::size_t i = 0; i < count; ++i) if (!water[i]) out.texels[i][3] = 0; // land beyond the band
    if (!water_count || water_count == count) return out;
    // Surf is for the sea only: sea-mask water seeds and keeps it; dry land
    // beside the sea takes it for the waterline. Lakes, ponds and other water
    // never hold surf, nor do swamp and lava tiles (land for the fetch, but
    // never given surf). The area classes do not tell running water from
    // still: a river joined to the sea is sea-mask water, kept low by its fetch.
    const std::vector<std::uint8_t> sea = takes_sea ? ground_cover_sea_mask(area, tiles_x, tiles_y) : std::vector<std::uint8_t>(count, 0);
    const auto may_take_surf = [&](std::size_t i) {
        if (water[i]) return sea[i] != 0;
        const auto type = area[i] & 15u;
        return type != ambient::area_swamp && type != ambient::area_lava;
    };
    const int w = int(tiles_x), h = int(tiles_y);
    constexpr float tile = 2;
    // Distance to the other class (water tiles: to land; land tiles: to
    // water), two-pass chamfer transform in tiles; the map edge is neither.
    std::vector<float> distance(count, 1e9f);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y)*w + x;
        for (int dy = -1; dy <= 1 && distance[i] > 0; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const int nx = x+dx, ny = y+dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            if (water[std::size_t(ny)*w + nx] != water[i]) { distance[i] = .5f; break; }
        }
    }
    const float diagonal = 1.41421356f;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        float& d = distance[std::size_t(y)*w + x];
        if (x) d = std::min(d, distance[std::size_t(y)*w + x-1] + 1);
        if (y) {
            d = std::min(d, distance[std::size_t(y-1)*w + x] + 1);
            if (x) d = std::min(d, distance[std::size_t(y-1)*w + x-1] + diagonal);
            if (x+1 < w) d = std::min(d, distance[std::size_t(y-1)*w + x+1] + diagonal);
        }
    }
    for (int y = h-1; y >= 0; --y) for (int x = w-1; x >= 0; --x) {
        float& d = distance[std::size_t(y)*w + x];
        if (x+1 < w) d = std::min(d, distance[std::size_t(y)*w + x+1] + 1);
        if (y+1 < h) {
            d = std::min(d, distance[std::size_t(y+1)*w + x] + 1);
            if (x+1 < w) d = std::min(d, distance[std::size_t(y+1)*w + x+1] + diagonal);
            if (x) d = std::min(d, distance[std::size_t(y+1)*w + x-1] + diagonal);
        }
    }
    const auto in_band = [&](std::size_t i) {
        return water[i] ? distance[i]*tile <= liquidSurfBand + 2*tile : distance[i]*tile <= liquidSurfLandReach;
    };
    // Facing: from land toward water, the inverse-square weighted sum of the
    // offsets to every opposite-class tile in reach (smooth along the coast,
    // defined everywhere the band reaches; it cancels across a narrow river,
    // whose fetch is short anyway).
    std::vector<std::array<float,2>> facing(count, {0, 0});
    std::vector<float> exposure(count, 0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y)*w + x;
        if (!in_band(i)) continue;
        const int reach = int(std::ceil(distance[i])) + 3;
        float fx = 0, fy = 0;
        for (int dy = -reach; dy <= reach; ++dy) for (int dx = -reach; dx <= reach; ++dx) {
            const int nx = x+dx, ny = y+dy;
            if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            if (water[std::size_t(ny)*w + nx] == water[i]) continue;
            const float d2 = float(dx*dx + dy*dy);
            // Water tile: away from land; land tile: toward water.
            const float s = water[i] ? -1.f : 1.f;
            fx += s*dx/d2; fy += s*dy/d2;
        }
        facing[i] = {fx, fy};
    }
    // Water past the map edge is unknown: a radial leaving the map over water
    // is credited with liquidSurfEdgeCredit times the width of the contiguous
    // water run along that edge where it leaves (at least the distance it
    // already crossed), capped at liquidSurfCap. An open sea edge is a long
    // run and reaches the cap; a river crossing the edge (10-30 m) stays
    // short, so its banks do not read as an open coast.
    std::vector<int> edge_run[4]; // left, right (per row), top, bottom (per column); tiles
    const auto runs = [&](int n, auto is_water) {
        std::vector<int> run(std::size_t(n), 0);
        for (int a = 0; a < n;) {
            if (!is_water(a)) { ++a; continue; }
            int b = a; while (b < n && is_water(b)) ++b;
            for (int k = a; k < b; ++k) run[std::size_t(k)] = b - a;
            a = b;
        }
        return run;
    };
    edge_run[0] = runs(h, [&](int y) { return water[std::size_t(y)*w] != 0; });
    edge_run[1] = runs(h, [&](int y) { return water[std::size_t(y)*w + w-1] != 0; });
    edge_run[2] = runs(w, [&](int x) { return water[std::size_t(x)] != 0; });
    edge_run[3] = runs(w, [&](int x) { return water[std::size_t(h-1)*w + x] != 0; });
    // Effective fetch of each band water tile.
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y)*w + x;
        if (!water[i] || !sea[i] || !in_band(i)) continue; // sea water only seeds surf
        const float length = std::sqrt(facing[i][0]*facing[i][0] + facing[i][1]*facing[i][1]);
        if (length < 1e-4f) continue;
        const float base = std::atan2(facing[i][1], facing[i][0]);
        float sum = 0, weights = 0;
        for (int k = 0; k < liquidSurfRadials; ++k) {
            const float offset = liquidSurfFan*(2.f*k/(liquidSurfRadials-1) - 1.f);
            const float c = std::cos(offset), dx = std::cos(base+offset), dy = std::sin(base+offset);
            float reach = liquidSurfCap;
            int px = x, py = y; // last tile inside the map (water)
            // One-tile steps from the tile centre.
            for (float t = 1; t*tile < liquidSurfCap; t += 1) {
                const int nx = int(std::floor(x + .5f + dx*t)), ny = int(std::floor(y + .5f + dy*t));
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                    const int run = nx < 0 ? edge_run[0][std::size_t(py)] : nx >= w ? edge_run[1][std::size_t(py)] :
                                    ny < 0 ? edge_run[2][std::size_t(px)] : edge_run[3][std::size_t(px)];
                    reach = std::min(liquidSurfCap, std::max(t*tile, liquidSurfEdgeCredit*run*tile));
                    break;
                }
                if (!water[std::size_t(ny)*w + nx]) { reach = t*tile; break; }
                px = nx; py = ny;
            }
            sum += reach*c; weights += c;
        }
        exposure[i] = sum/weights;
        out.max_exposure = std::max(out.max_exposure, exposure[i]);
    }
    // Sea water beyond the band (open sea, no shore to face): the plain mean
    // fetch over 12 directions (2-tile steps, the same edge credit), so the
    // swell and chop carry on out to sea where it is open and stay low in a
    // large enclosed harbour, joining the band's values through the blur
    // below instead of stopping at the band's edge.
    const auto open_sea = [&](std::size_t i) { return water[i] && sea[i] && !in_band(i); };
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y)*w + x;
        if (!open_sea(i)) continue;
        float sum = 0;
        for (int k = 0; k < 12; ++k) {
            const float dx = std::cos(k*.5235988f), dy = std::sin(k*.5235988f);
            float reach = liquidSurfCap;
            int px = x, py = y; // last tile inside the map (water)
            for (float t = 2; t*tile < liquidSurfCap; t += 2) {
                const int nx = int(std::floor(x + .5f + dx*t)), ny = int(std::floor(y + .5f + dy*t));
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                    const int run = nx < 0 ? edge_run[0][std::size_t(py)] : nx >= w ? edge_run[1][std::size_t(py)] :
                                    ny < 0 ? edge_run[2][std::size_t(px)] : edge_run[3][std::size_t(px)];
                    reach = std::min(liquidSurfCap, std::max(t*tile, liquidSurfEdgeCredit*run*tile));
                    break;
                }
                if (!water[std::size_t(ny)*w + nx]) { reach = t*tile; break; }
                px = nx; py = ny;
            }
            sum += reach;
        }
        exposure[i] = sum/12;
    }
    const auto in_region = [&](std::size_t i) { return in_band(i) || open_sea(i); };
    // Shore land tiles take the exposure of the water beside them.
    std::vector<float> surf(count, 0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y)*w + x;
        if (!in_region(i)) continue;
        float e = exposure[i];
        if (!may_take_surf(i)) e = 0;
        else if (!water[i]) {
            float sum = 0, weights = 0;
            for (int dy = -5; dy <= 5; ++dy) for (int dx = -5; dx <= 5; ++dx) {
                const int nx = x+dx, ny = y+dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const std::size_t n = std::size_t(ny)*w + nx;
                if (!water[n] || !sea[n] || exposure[n] <= 0) continue;
                const float weight = 1.f/float(1 + dx*dx + dy*dy);
                sum += exposure[n]*weight; weights += weight;
            }
            e = weights > 0 ? sum/weights : 0;
        }
        const float t = std::clamp((e - liquidSurfSheltered)/(liquidSurfExposed - liquidSurfSheltered), 0.f, 1.f);
        surf[i] = t*t*(3 - 2*t);
    }
    // Sea level: a sea tile's liquid vertices (those above the ground); shore
    // land in the band takes the sea level beside it.
    const std::uint32_t verts_x = tiles_x*2u + 1u;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y)*w + x;
        if (!water[i] || !sea[i] || !in_band(i)) continue;
        float sum = 0, n = 0, top = -1e30f;
        for (int vy = 2*y; vy <= 2*y + 2; ++vy) for (int vx = 2*x; vx <= 2*x + 2; ++vx) {
            const std::size_t v = std::size_t(vy)*verts_x + vx;
            top = std::max(top, height[v]);
            if (height[v] > ground[v] + .05f) { sum += height[v]; n += 1; }
        }
        out.level[i] = n > 0 ? sum/n : top;
    }
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y)*w + x;
        if (water[i] || !in_band(i) || !may_take_surf(i)) continue;
        float sum = 0, weights = 0;
        for (int dy = -5; dy <= 5; ++dy) for (int dx = -5; dx <= 5; ++dx) {
            const int nx = x+dx, ny = y+dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const std::size_t n = std::size_t(ny)*w + nx;
            if (out.level[n] == liquidSurfNoLevel || !water[n]) continue;
            const float weight = 1.f/float(1 + dx*dx + dy*dy);
            sum += out.level[n]*weight; weights += weight;
        }
        if (weights > 0) out.level[i] = sum/weights;
    }
    // Shore steepness (impact): at each sea tile on the shore (within 1.5
    // tiles of land), the largest of: the ground's rise above the sea on the
    // land right beside it (within one tile, ~2-3 m: a 1-2 m bank counts), its
    // rise over the land within 2 tiles (per 4 m), and the water's depth at
    // the shore; then spread over the band from the shore tiles within 5 tiles.
    std::vector<float> shore_impact(count, -1.f);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y)*w + x;
        if (!water[i] || !sea[i] || distance[i] > 1.5f || out.level[i] == liquidSurfNoLevel) continue;
        float bed = 0, n = 0, rise = 0, beside = 0;
        for (int vy = 2*y; vy <= 2*y + 2; ++vy) for (int vx = 2*x; vx <= 2*x + 2; ++vx) {
            bed += ground[std::size_t(vy)*verts_x + vx]; n += 1;
        }
        const float depth = out.level[i] - bed/n;
        for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
            const int nx = x+dx, ny = y+dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || water[std::size_t(ny)*w + nx]) continue;
            for (int vy = 2*ny; vy <= 2*ny + 2; ++vy) for (int vx = 2*nx; vx <= 2*nx + 2; ++vx)
            {
                const float r = ground[std::size_t(vy)*verts_x + vx] - out.level[i];
                rise = std::max(rise, r);
                if (std::abs(dx) <= 1 && std::abs(dy) <= 1) beside = std::max(beside, r);
            }
        }
        const auto ramp = [](float v, float a, float b) { const float t = std::clamp((v - a)/(b - a), 0.f, 1.f); return t*t*(3 - 2*t); };
        shore_impact[i] = std::max(std::max(ramp(beside, .6f, 1.3f), ramp(rise/4.f, .3f, .7f)), ramp(depth, .6f, 1.5f));
    }
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y)*w + x;
        if (!in_band(i) || !may_take_surf(i)) continue;
        float sum = 0, weights = 0;
        for (int dy = -5; dy <= 5; ++dy) for (int dx = -5; dx <= 5; ++dx) {
            const int nx = x+dx, ny = y+dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const float s = shore_impact[std::size_t(ny)*w + nx];
            if (s < 0) continue;
            const float weight = 1.f/float(1 + dx*dx + dy*dy);
            sum += s*weight; weights += weight;
        }
        if (weights > 0) out.impact[i] = sum/weights;
    }
    // Two 5x5 box passes over the band and the open sea, so surf and direction fade along the
    // coast instead of switching from tile to tile.
    std::vector<float> next_surf(count);
    std::vector<std::array<float,2>> next_facing(count);
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y)*w + x;
            next_surf[i] = surf[i]; next_facing[i] = facing[i];
            if (!in_region(i)) continue;
            float s = 0, fx = 0, fy = 0, n = 0;
            for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
                const int nx = x+dx, ny = y+dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const std::size_t j = std::size_t(ny)*w + nx;
                if (!in_region(j)) continue;
                const float l = std::sqrt(facing[j][0]*facing[j][0] + facing[j][1]*facing[j][1]);
                s += surf[j]; n += 1;
                if (l > 1e-6f) { fx += facing[j][0]/l; fy += facing[j][1]/l; }
            }
            next_surf[i] = s/n; next_facing[i] = {fx/n, fy/n}; // mean of unit facings: its length is how well they agree
        }
        surf.swap(next_surf); facing.swap(next_facing);
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!in_region(i)) continue;
        const float l = std::sqrt(facing[i][0]*facing[i][0] + facing[i][1]*facing[i][1]);
        // Incoming: from open water toward the shore (against the facing). Its
        // length is how well the shore directions around agree: where two
        // coasts meet at an angle the shore-distance crests would fold into a
        // sharp crease (seen as a diagonal seam), so the swell fades there
        // (liquid_surface.hlsl liquidSurfDecode reads the length as its share).
        const float agree = l > 1e-6f ? std::clamp((l - .72f)/.24f, 0.f, 1.f) : 0.f;
        const float ix = l > 1e-6f ? -facing[i][0]/l*agree : 0, iy = l > 1e-6f ? -facing[i][1]/l*agree : 0;
        // Tile-centre distances: a boundary water tile is 1 m from the border, a boundary land tile -1 m.
        const float shore = water[i] ? .25f + .75f*std::clamp(distance[i]*tile/liquidSurfBand, 0.f, 1.f)
                                     : .25f - .25f*std::clamp(distance[i]*tile/liquidSurfLandReach, 0.f, 1.f);
        const auto byte = [](float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.f, 1.f)*255.f)); };
        // Again after the blur: none leaks into lakes, canals off the sea
        // mask, swamp or lava.
        // Every sea-mask water tile is at least liquidSurfSeaFloor (sheltered sea still
        // takes the sea colour and a little swell; exposure raises it).
        const float s = may_take_surf(i) ? (water[i] ? std::max(surf[i], liquidSurfSeaFloor) : surf[i]) : 0.f;
        out.texels[i] = {byte(s), byte(.5f + .5f*ix), byte(.5f + .5f*iy), byte(shore)};
        if (s > .05f) ++out.surf_cells;
    }
    // No visible seam where the sea meets other water (a river mouth, a
    // lagoon, a canal off the sea mask): the sea's surf spills into that
    // water and dies out over liquidSurfSpill metres (chamfer decay through
    // water tiles only), so swell, chop, tint and foam fade instead of
    // stopping at a tile line. Swamp, lava and land take none.
    {
        std::vector<float> spill(count, 0.f);
        for (std::size_t i = 0; i < count; ++i) if (water[i] && sea[i]) spill[i] = out.texels[i][0]/255.f;
        const float step = tile/liquidSurfSpill, diagonal = step*1.41421356f;
        const auto relax = [&](std::size_t i, std::size_t j, float d) {
            if (water[i] && !sea[i]) spill[i] = std::max(spill[i], spill[j] - d);
        };
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y)*w + x;
            if (x) relax(i, i-1, step);
            if (y) { relax(i, i-w, step); if (x) relax(i, i-w-1, diagonal); if (x+1 < w) relax(i, i-w+1, diagonal); }
        }
        for (int y = h-1; y >= 0; --y) for (int x = w-1; x >= 0; --x) {
            const std::size_t i = std::size_t(y)*w + x;
            if (x+1 < w) relax(i, i+1, step);
            if (y+1 < h) { relax(i, i+w, step); if (x+1 < w) relax(i, i+w+1, diagonal); if (x) relax(i, i+w-1, diagonal); }
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (!water[i] || sea[i] || spill[i] <= 0) continue;
            const float t = spill[i];
            out.texels[i][0] = static_cast<std::uint8_t>(std::lround(std::clamp(t*t*(3 - 2*t), 0.f, 1.f)*255.f));
        }
    }
    // Smooth swell distance and depth: masked separable box blurs over water
    // tiles only (land keeps its plain negative distance; depth is 0 there).
    // Distance: three 7-tile passes (about a 7 m Gaussian), so crest lines
    // follow the coast's large shape, not its tile steps or small bumps;
    // depth (mean liquid height over the ground at a tile's vertices): two
    // 5-tile passes, so shoaling and breaking do not ripple over the bed.
    const auto masked_blur = [&](std::vector<float>& value, int radius, int passes) {
        std::vector<float> sum(count), weight(count), next(count);
        for (int pass = 0; pass < passes; ++pass) {
            for (int axis = 0; axis < 2; ++axis) {
                for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                    const std::size_t i = std::size_t(y)*w + x;
                    float s = 0, n = 0;
                    for (int k = -radius; k <= radius; ++k) {
                        const int nx = axis ? x : x+k, ny = axis ? y+k : y;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        const std::size_t j = std::size_t(ny)*w + nx;
                        const float m = axis ? weight[j] : float(water[j]);
                        s += (axis ? sum[j] : value[j]*m); n += m;
                    }
                    if (axis) next[i] = water[i] && n > 0 ? s/n : value[i];
                    else { sum[i] = s; weight[i] = n; }
                }
            }
            value.swap(next);
        }
    };
    for (std::size_t i = 0; i < count; ++i)
        out.swell_distance[i] = water[i] ? std::min(distance[i]*tile, liquidSurfSwellReach) : -distance[i]*tile;
    masked_blur(out.swell_distance, 6, 4); // wide: crests come in toward each shore and only gently curve in bays (no focusing rings)
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y)*w + x;
        if (!water[i]) continue;
        float sum = 0;
        for (int vy = 2*y; vy <= 2*y + 2; ++vy) for (int vx = 2*x; vx <= 2*x + 2; ++vx) {
            const std::size_t v = std::size_t(vy)*verts_x + vx;
            sum += std::max(height[v] - ground[v], 0.f);
        }
        out.swell_depth[i] = sum/9;
    }
    masked_blur(out.swell_depth, 2, 2);
    // The three strongest steep-shore spots (surf x impact on sea water at the
    // shore, at least 40 m apart), for the load log: where bursts show best.
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y)*w + x;
        if (!water[i] || !sea[i] || distance[i] > 2.f) continue;
        const float score = (out.texels[i][0]/255.f)*out.impact[i];
        if (score <= .05f) continue;
        const float px = (x + .5f)*tile, py = (y + .5f)*tile;
        bool merged = false;
        for (auto& spot : out.impact_spots)
            if (spot[2] > 0 && (spot[0]-px)*(spot[0]-px) + (spot[1]-py)*(spot[1]-py) < 1600.f) {
                if (score > spot[2]) spot = {px, py, score, std::atan2(out.texels[i][2]/127.5f - 1.f, out.texels[i][1]/127.5f - 1.f)*57.29578f};
                merged = true; break;
            }
        if (merged) continue;
        auto weakest = std::min_element(out.impact_spots.begin(), out.impact_spots.end(),
                                        [](const auto& a, const auto& b) { return a[2] < b[2]; });
        if (score > (*weakest)[2])
            *weakest = {px, py, score, std::atan2(out.texels[i][2]/127.5f - 1.f, out.texels[i][1]/127.5f - 1.f)*57.29578f};
    }
    return out;
}

} // namespace owned
