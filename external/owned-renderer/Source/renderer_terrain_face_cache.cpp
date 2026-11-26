// Terrain 2.0 face cache (option terrain_face_cache; terrain2_cache.h
// kTerrainFaceCacheShader, terrain_face_atlas.h): the steep faces of the
// active map and the neighbours in the terrain 2.0 table as charts in a
// virtual atlas per map and level, pages baked on demand by rasterising
// their triangles into one shared pool, read by the terrain pixel stage
// instead of running terrain2Color.
#include "graphics_settings.h"
#include "terrain_face_atlas.h"
#include "neighbour_piece.h"
#include "worker_priority.h"
#include "renderer.h"
#include "renderer_internal.h"
#include "engine_bridge.h"
#include "owned_terrain_front_end.h"
#include "point_shadow_types.h"
#include "contact_blend.h"
#include "owned_resource_runtime.h"
#include "retained_static_scene.h"
#include "owned_asset_loader.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <intrin.h>

namespace owned {
#ifndef EID3D11_SHADER_EMBED
namespace {

// Steep class (owned_terrain_decode.cpp kTerrainSteepClassZ): only a
// triangle with a corner below it can show side projections.
constexpr float kSteepZ = .76604444f + .002f;
// Chart cone: every triangle of a chart within ~37 degrees of its seed.
constexpr float kChartCos = .8f;
constexpr unsigned kReferenceLevel = 2; // packed by coverage; finer levels are its scaled copies
constexpr unsigned kNoChart = 0xffffu;
constexpr std::uint32_t kTerrainFacePatch = 32; // triangles per request unit (TerrainFaceAtlas::Patch)

struct V3 { double x, y, z; };
V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double length(V3 a) { return std::sqrt(dot(a, a)); }
V3 scale(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }

float bits_float(std::uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; }

}  // namespace

bool build_terrain_face_grid(const TerrainCpuAsset& terrain, TerrainFaceGrid& out, std::string& failure) try {
    const std::uint32_t gx = terrain.sector_count_x * 32u + 1u, gy = terrain.sector_count_y * 32u + 1u;
    if (terrain.sector_count_x == 0 || terrain.sector_count_y == 0) { failure = "no sectors"; return false; }
    out.grid_x = gx; out.grid_y = gy;
    auto& grid = out.grid;
    grid.assign(std::size_t(gx) * gy, {});
    auto& flags = out.flags;
    flags.assign(std::size_t(gx) * gy, 0);
    for (const auto& sector : terrain.sectors) {
        for (std::uint32_t i = 0; i < 33u * 33u && i < sector.vertices.size(); ++i) {
            const std::uint32_t x = sector.map_x * 32u + i % 33u, y = sector.map_y * 32u + i / 33u;
            if (x >= gx || y >= gy) continue;
            const std::size_t at = std::size_t(y) * gx + x;
            const auto& v = sector.vertices[i];
            const std::uint32_t slope = owned_terrain_slope_normal(terrain, sector, i);
            const std::uint32_t flat = owned_terrain_facet_flatness(terrain, sector, i);
            // The liquid bed: its object and signed rest depth (owned_terrain_decode.cpp; the live shift is added per frame).
            const std::uint16_t bed = terrain_base_liquid_object(terrain, sector, i);
            const float depth = bed != UINT16_MAX && i + 33u * 33u < sector.vertices.size() ? sector.vertices[i + 33u * 33u].z - v.z : 0.f;
            grid[at] = {v.x, v.y, v.z, owned_terrain_foot_rise(terrain, sector, i), bits_float(slope), bits_float(flat), depth, bits_float(bed)};
            const float sx = float(std::int16_t(slope & 0xffffu)) / 32767.f, sy = float(std::int16_t(slope >> 16)) / 32767.f;
            const float sz = std::sqrt((std::max)(0.f, 1.f - sx * sx - sy * sy));
            flags[at] |= 4u | (sz < kSteepZ ? 1u : 0u) | (bed != UINT16_MAX ? 2u : 0u);
        }
    }
    return true;
} catch (...) {
    failure = "out of memory";
    return false;
}

bool build_terrain_face_atlas(const TerrainCpuAsset& terrain, TerrainFaceAtlas& atlas, std::string& failure) try {
    auto grid = std::make_shared<TerrainFaceGrid>();
    if (!build_terrain_face_grid(terrain, *grid, failure)) return false;
    return build_terrain_face_atlas(std::move(grid), atlas, failure);
} catch (...) {
    failure = "out of memory";
    return false;
}

bool build_terrain_face_atlas(std::shared_ptr<const TerrainFaceGrid> source, TerrainFaceAtlas& atlas, std::string& failure,
                              const std::atomic<bool>* cancel) try {
    const auto start = std::chrono::steady_clock::now();
    const auto cancelled = [&] { if (cancel && cancel->load(std::memory_order_relaxed)) { failure = "cancelled"; return true; } return false; };
    if (!source || source->grid_x < 2 || source->grid_y < 2) { failure = "no grid"; return false; }
    atlas.source = source;
    const std::uint32_t gx = source->grid_x, gy = source->grid_y;
    atlas.grid_x = gx; atlas.grid_y = gy;
    const auto& flags = source->flags;
    const std::uint32_t cx = gx - 1, cy = gy - 1;
    const auto corner = [&](std::uint32_t tri, unsigned k) -> std::uint32_t {
        const std::uint32_t cell = tri >> 1, i = (cell / cx) * gx + cell % cx;
        const std::uint32_t v[2][3]{{i, i + 1, i + gx + 1}, {i, i + gx + 1, i + gx}};
        return v[tri & 1u][k];
    };
    const auto position = [&](std::uint32_t vertex) { const auto& g = source->grid[vertex]; return V3{g[0], g[1], g[2]}; };
    // Triangles: steep (a steep corner), charted unless a corner has a liquid bed.
    const std::uint32_t triangle_count = cx * cy * 2u;
    std::vector<std::uint8_t> state(triangle_count, 0); // 1 candidate
    std::vector<V3> normal(triangle_count);
    std::vector<double> area(triangle_count, 0);
    std::vector<std::uint32_t> order;
    for (std::uint32_t tri = 0; tri < triangle_count; ++tri) {
        std::uint8_t f[3]; for (unsigned k = 0; k < 3; ++k) f[k] = flags[corner(tri, k)];
        if (!((f[0] | f[1] | f[2]) & 1u) || !(f[0] & f[1] & f[2] & 4u)) continue;
        ++atlas.steep_triangles;
        if ((f[0] | f[1] | f[2]) & 2u) ++atlas.bed_triangles; // baked with the live bed depth (t107)
        const V3 a = position(corner(tri, 0)), n = cross(sub(position(corner(tri, 1)), a), sub(position(corner(tri, 2)), a));
        const double l = length(n);
        if (!(l > 1e-9)) continue;
        normal[tri] = scale(n, 1 / l); area[tri] = l * .5; state[tri] = 1; order.push_back(tri);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) { return area[a] > area[b]; });
    // Charts: region growing over shared edges from the largest free triangle.
    std::vector<std::uint32_t> chart_of(triangle_count, kNoChart), front;
    const auto neighbours = [&](std::uint32_t tri, std::uint32_t out[3]) -> unsigned {
        const std::uint32_t cell = tri >> 1, x = cell % cx, y = cell / cx;
        unsigned n = 0;
        if ((tri & 1u) == 0) { // (a, b, d): its (a, d, c), the cell below's top, the right cell's left
            out[n++] = cell * 2 + 1;
            if (y > 0) out[n++] = (cell - cx) * 2 + 1;
            if (x + 1 < cx) out[n++] = (cell + 1) * 2 + 1;
        } else {
            out[n++] = cell * 2;
            if (y + 1 < cy) out[n++] = (cell + cx) * 2;
            if (x > 0) out[n++] = (cell - 1) * 2;
        }
        return n;
    };
    for (const std::uint32_t seed : order) {
        if (chart_of[seed] != kNoChart) continue;
        if ((atlas.charts.size() & 63u) == 0 && cancelled()) return false;
        if (atlas.charts.size() >= kNoChart) break; // the rest stays per pixel
        const std::uint32_t id = std::uint32_t(atlas.charts.size());
        TerrainFaceAtlas::Chart chart;
        chart.first = std::uint32_t(atlas.triangles.size());
        const V3 N = normal[seed];
        chart_of[seed] = id; front.assign(1, seed); atlas.triangles.push_back(seed);
        while (!front.empty()) {
            const std::uint32_t t = front.back(); front.pop_back();
            std::uint32_t next[3]; const unsigned n = neighbours(t, next);
            for (unsigned k = 0; k < n; ++k) {
                const std::uint32_t q = next[k];
                if (!state[q] || chart_of[q] != kNoChart || dot(normal[q], N) < kChartCos) continue;
                chart_of[q] = id; front.push_back(q); atlas.triangles.push_back(q);
            }
        }
        chart.count = std::uint32_t(atlas.triangles.size()) - chart.first;
        // Frame: the seed's plane, turned to the chart's smallest box.
        V3 t = cross(N, V3{0, 0, 1});
        if (length(t) < 1e-6) t = {1, 0, 0};
        t = scale(t, 1 / length(t));
        V3 b = cross(N, t);
        double best = 1e300, best_c = 1, best_s = 0;
        for (unsigned step = 0; step < 24; ++step) {
            const double angle = step * (3.14159265358979 / 48), c = std::cos(angle), s = std::sin(angle);
            const V3 tt{t.x * c + b.x * s, t.y * c + b.y * s, t.z * c + b.z * s}, bb{-t.x * s + b.x * c, -t.y * s + b.y * c, -t.z * s + b.z * c};
            double lo[2]{1e300, 1e300}, hi[2]{-1e300, -1e300};
            for (std::uint32_t k = chart.first; k < chart.first + chart.count; ++k)
                for (unsigned c3 = 0; c3 < 3; ++c3) {
                    const V3 p = position(corner(atlas.triangles[k], c3));
                    const double u = dot(p, tt), v = dot(p, bb);
                    lo[0] = (std::min)(lo[0], u); hi[0] = (std::max)(hi[0], u); lo[1] = (std::min)(lo[1], v); hi[1] = (std::max)(hi[1], v);
                }
            const double box = (hi[0] - lo[0] + .1) * (hi[1] - lo[1] + .1);
            if (box < best) { best = box; best_c = c; best_s = s; }
        }
        const V3 T{t.x * best_c + b.x * best_s, t.y * best_c + b.y * best_s, t.z * best_c + b.z * best_s};
        const V3 B{-t.x * best_s + b.x * best_c, -t.y * best_s + b.y * best_c, -t.z * best_s + b.z * best_c};
        // Scale per axis: surface length of a unit chart step, the largest over its triangles.
        double su = 1, sv = 1, lo[2]{1e300, 1e300}, hi[2]{-1e300, -1e300}, box_lo[3]{1e300, 1e300, 1e300}, box_hi[3]{-1e300, -1e300, -1e300};
        for (std::uint32_t k = chart.first; k < chart.first + chart.count; ++k) {
            const std::uint32_t tri = atlas.triangles[k];
            V3 p[3]; double u[3], v[3];
            for (unsigned c3 = 0; c3 < 3; ++c3) {
                p[c3] = position(corner(tri, c3)); u[c3] = dot(p[c3], T); v[c3] = dot(p[c3], B);
                lo[0] = (std::min)(lo[0], u[c3]); hi[0] = (std::max)(hi[0], u[c3]); lo[1] = (std::min)(lo[1], v[c3]); hi[1] = (std::max)(hi[1], v[c3]);
                const double xyz[3]{p[c3].x, p[c3].y, p[c3].z};
                for (unsigned a = 0; a < 3; ++a) { box_lo[a] = (std::min)(box_lo[a], xyz[a]); box_hi[a] = (std::max)(box_hi[a], xyz[a]); }
            }
            const V3 e1 = sub(p[1], p[0]), e2 = sub(p[2], p[0]);
            const double f1u = u[1] - u[0], f1v = v[1] - v[0], f2u = u[2] - u[0], f2v = v[2] - v[0], det = f1u * f2v - f1v * f2u;
            if (std::abs(det) < 1e-12) continue;
            su = (std::max)(su, length(scale(sub(scale(e1, f2v), scale(e2, f1v)), 1 / det)));
            sv = (std::max)(sv, length(scale(sub(scale(e2, f1u), scale(e1, f2u)), 1 / det)));
        }
        su = (std::min)(su, 8.0); sv = (std::min)(sv, 8.0);
        chart.t[0] = float(T.x); chart.t[1] = float(T.y); chart.t[2] = float(T.z); chart.u0 = float(lo[0]);
        chart.b[0] = float(B.x); chart.b[1] = float(B.y); chart.b[2] = float(B.z); chart.v0 = float(lo[1]);
        chart.su = float(su); chart.sv = float(sv);
        chart.size_u = float((hi[0] - lo[0]) * su); chart.size_v = float((hi[1] - lo[1]) * sv);
        for (unsigned a = 0; a < 3; ++a) chart.centre[a] = float((box_lo[a] + box_hi[a]) * .5);
        chart.radius = float(.5 * std::sqrt((box_hi[0] - box_lo[0]) * (box_hi[0] - box_lo[0]) + (box_hi[1] - box_lo[1]) * (box_hi[1] - box_lo[1]) +
                                           (box_hi[2] - box_lo[2]) * (box_hi[2] - box_lo[2])));
        for (std::uint32_t k = chart.first; k < chart.first + chart.count; ++k) atlas.face_area += area[atlas.triangles[k]];
        for (std::uint32_t k = chart.first; k < chart.first + chart.count; k += kTerrainFacePatch) {
            TerrainFaceAtlas::Patch patch; patch.chart = id; patch.first = k; patch.count = (std::min)(kTerrainFacePatch, chart.first + chart.count - k);
            double plo[3]{1e300, 1e300, 1e300}, phi[3]{-1e300, -1e300, -1e300};
            for (std::uint32_t q = patch.first; q < patch.first + patch.count; ++q)
                for (unsigned c3 = 0; c3 < 3; ++c3) {
                    const V3 p = position(corner(atlas.triangles[q], c3)); const double xyz[3]{p.x, p.y, p.z};
                    for (unsigned a = 0; a < 3; ++a) { plo[a] = (std::min)(plo[a], xyz[a]); phi[a] = (std::max)(phi[a], xyz[a]); }
                }
            double r2 = 0;
            for (unsigned a = 0; a < 3; ++a) { patch.centre[a] = float((plo[a] + phi[a]) * .5); r2 += (phi[a] - plo[a]) * (phi[a] - plo[a]); }
            patch.radius = float(.5 * std::sqrt(r2));
            atlas.patches.push_back(patch);
        }
        atlas.charts.push_back(chart);
    }
    atlas.cells.assign(std::size_t(cx) * cy, 0xffffffffu);
    for (std::uint32_t tri = 0; tri < triangle_count; ++tri) {
        if (chart_of[tri] == kNoChart) continue;
        auto& cell = atlas.cells[tri >> 1];
        cell = (tri & 1u) ? (cell & 0xffffu) | (chart_of[tri] << 16) : (cell & 0xffff0000u) | chart_of[tri];
    }
    if (atlas.charts.empty()) { failure = "no steep faces"; return false; }

    // Chart metres of a map-local point, and its atlas texel at level l.
    const auto metres = [&](const TerrainFaceAtlas::Chart& c, V3 p) {
        return std::array<double, 2>{(p.x * c.t[0] + p.y * c.t[1] + p.z * c.t[2] - c.u0) * c.su,
                                     (p.x * c.b[0] + p.y * c.b[1] + p.z * c.b[2] - c.v0) * c.sv};
    };
    const double gutter = kTerrainFaceCacheGutter;
    // Coverage packing ("tetris" skyline over each chart's covered blocks:
    // texels within the gutter plus half a texel of one of its triangles).
    const auto pack = [&](unsigned l, int block, std::vector<std::array<float, 2>>& origin, double& width_texels, double& height_texels) {
        const double D = double(128u >> l);
        struct Mask { int w = 0, h = 0; std::vector<int> bottom, top; std::uint32_t chart = 0; };
        std::vector<Mask> masks(atlas.charts.size());
        double covered = 0;
        std::vector<std::uint8_t> bits;
        for (std::uint32_t id = 0; id < atlas.charts.size(); ++id) {
            const auto& c = atlas.charts[id];
            Mask& m = masks[id]; m.chart = id;
            m.w = int(std::ceil((c.size_u * D + 2 * gutter) / block)) + 1; m.h = int(std::ceil((c.size_v * D + 2 * gutter) / block)) + 1;
            bits.assign(std::size_t(m.w) * m.h, 0);
            const double reach = gutter + .5 + block * .7072;
            for (std::uint32_t k = c.first; k < c.first + c.count; ++k) {
                double px[3], py[3];
                for (unsigned c3 = 0; c3 < 3; ++c3) {
                    const auto mm = metres(c, position(corner(atlas.triangles[k], c3)));
                    px[c3] = mm[0] * D + gutter; py[c3] = mm[1] * D + gutter;
                }
                double ex[3][3]; // edge lines: inward normal and offset
                const double orient = (px[1] - px[0]) * (py[2] - py[0]) - (py[1] - py[0]) * (px[2] - px[0]) < 0 ? -1 : 1;
                for (unsigned e = 0; e < 3; ++e) {
                    const unsigned f = (e + 1) % 3;
                    double nx = -(py[f] - py[e]) * orient, ny = (px[f] - px[e]) * orient;
                    const double len = std::sqrt(nx * nx + ny * ny);
                    if (len < 1e-12) { nx = ny = 0; } else { nx /= len; ny /= len; }
                    ex[e][0] = nx; ex[e][1] = ny; ex[e][2] = -(nx * px[e] + ny * py[e]);
                }
                const int x0 = (std::max)(0, int(std::floor(((std::min)({px[0], px[1], px[2]}) - reach) / block)));
                const int x1 = (std::min)(m.w - 1, int(std::floor(((std::max)({px[0], px[1], px[2]}) + reach) / block)));
                const int y0 = (std::max)(0, int(std::floor(((std::min)({py[0], py[1], py[2]}) - reach) / block)));
                const int y1 = (std::min)(m.h - 1, int(std::floor(((std::max)({py[0], py[1], py[2]}) + reach) / block)));
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x) {
                        const double qx = (x + .5) * block, qy = (y + .5) * block;
                        bool inside = true;
                        for (unsigned e = 0; e < 3 && inside; ++e) inside = ex[e][0] * qx + ex[e][1] * qy + ex[e][2] >= -reach;
                        if (inside) bits[std::size_t(y) * m.w + x] = 1;
                    }
            }
            m.bottom.assign(m.w, -1); m.top.assign(m.w, -1);
            for (int x = 0; x < m.w; ++x)
                for (int y = 0; y < m.h; ++y)
                    if (bits[std::size_t(y) * m.w + x]) { if (m.bottom[x] < 0) m.bottom[x] = y; m.top[x] = y; covered += 1; }
        }
        int width = int(std::ceil(std::sqrt(covered * 1.3)));
        for (const auto& m : masks) width = (std::max)(width, m.w);
        width = (width + 63) / 64 * 64;
        std::vector<std::uint32_t> sorted(masks.size());
        std::iota(sorted.begin(), sorted.end(), 0u);
        std::stable_sort(sorted.begin(), sorted.end(), [&](std::uint32_t a, std::uint32_t b) {
            return masks[a].h != masks[b].h ? masks[a].h > masks[b].h : masks[a].w > masks[b].w; });
        std::vector<int> sky(width, 0);
        origin.assign(atlas.charts.size(), {0.f, 0.f});
        int height = 0;
        for (const std::uint32_t id : sorted) {
            const Mask& m = masks[id];
            int best_x = 0, best_y = INT32_MAX;
            for (int x = 0; x + m.w <= width; ++x) {
                int y = 0;
                for (int i = 0; i < m.w && y < best_y; ++i) if (m.bottom[i] >= 0) y = (std::max)(y, sky[x + i] - m.bottom[i]);
                if (y < best_y) { best_y = y; best_x = x; if (y == 0) break; }
            }
            for (int i = 0; i < m.w; ++i) if (m.top[i] >= 0) sky[best_x + i] = best_y + m.top[i] + 1;
            height = (std::max)(height, best_y + m.h);
            origin[id] = {float(best_x * block), float(best_y * block)};
        }
        width_texels = double(width) * block; height_texels = double(height) * block;
    };

    char text[160];
    atlas.summary.clear();
    double packed_w[kTerrainFaceCacheLevels]{}, packed_h[kTerrainFaceCacheLevels]{};
    for (unsigned l = kReferenceLevel; l < kTerrainFaceCacheLevels; ++l) {
        if (cancelled()) return false;
        pack(l, l == kReferenceLevel ? 4 : 2, atlas.levels[l].origin, packed_w[l], packed_h[l]);
    }
    for (unsigned l = 0; l < kReferenceLevel; ++l) {
        // Finer levels: the reference packing scaled by k, the charts at the
        // same scaled place (k (origin + gutter + m D_ref) = origin_l + gutter + m D_l).
        const double k = double(1u << (kReferenceLevel - l));
        auto& origin = atlas.levels[l].origin;
        origin.resize(atlas.charts.size());
        for (std::size_t id = 0; id < origin.size(); ++id)
            for (unsigned a = 0; a < 2; ++a) origin[id][a] = float(k * atlas.levels[kReferenceLevel].origin[id][a] + (k - 1) * gutter);
        packed_w[l] = packed_w[kReferenceLevel] * k; packed_h[l] = packed_h[kReferenceLevel] * k;
    }
    // Page lists per level.
    std::uint32_t table = 0;
    for (unsigned l = 0; l < kTerrainFaceCacheLevels; ++l) {
        if (cancelled()) return false;
        auto& level = atlas.levels[l];
        const double D = double(128u >> l);
        level.pages_x = std::uint32_t(std::ceil(packed_w[l] / kTerrainFaceCachePage)) + 1;
        level.pages_y = std::uint32_t(std::ceil(packed_h[l] / kTerrainFaceCachePage)) + 1;
        level.offset = table; table += level.pages_x * level.pages_y;
        std::vector<std::uint64_t> draw;   // page << 32 | triangle
        std::vector<std::uint32_t> pages, all; // a patch's pages its reads reach; every level page some read reaches (the summary)
        for (std::uint32_t pid = 0; pid < atlas.patches.size(); ++pid) {
            const auto& patch = atlas.patches[pid];
            const std::uint32_t id = patch.chart;
            const auto& c = atlas.charts[id];
            pages.clear();
            for (std::uint32_t k = patch.first; k < patch.first + patch.count; ++k) {
                double lo[2]{1e300, 1e300}, hi[2]{-1e300, -1e300};
                for (unsigned c3 = 0; c3 < 3; ++c3) {
                    const auto m = metres(c, position(corner(atlas.triangles[k], c3)));
                    for (unsigned a = 0; a < 2; ++a) {
                        const double t = m[a] * D + gutter + level.origin[id][a];
                        lo[a] = (std::min)(lo[a], t); hi[a] = (std::max)(hi[a], t);
                    }
                }
                // Reads reach 2 texels past a triangle; a page's bake draws what
                // reaches its slot (2-texel page border) grown by 2.5.
                const auto range = [&](double grow, std::uint32_t r[4]) {
                    r[0] = std::uint32_t((std::max)(0.0, std::floor((lo[0] - grow) / kTerrainFaceCachePage)));
                    r[1] = std::uint32_t((std::max)(0.0, std::floor((lo[1] - grow) / kTerrainFaceCachePage)));
                    r[2] = (std::min)(level.pages_x - 1, std::uint32_t(std::floor((hi[0] + grow) / kTerrainFaceCachePage)));
                    r[3] = (std::min)(level.pages_y - 1, std::uint32_t(std::floor((hi[1] + grow) / kTerrainFaceCachePage)));
                };
                std::uint32_t need[4], bake[4];
                range(gutter + .5, need); range(gutter + kTerrainFaceCacheBorder + 1, bake);
                for (std::uint32_t y = need[1]; y <= need[3]; ++y) for (std::uint32_t x = need[0]; x <= need[2]; ++x) pages.push_back(y * level.pages_x + x);
                for (std::uint32_t y = bake[1]; y <= bake[3]; ++y) for (std::uint32_t x = bake[0]; x <= bake[2]; ++x)
                    draw.push_back(std::uint64_t(y * level.pages_x + x) << 32 | atlas.triangles[k]);
            }
            std::sort(pages.begin(), pages.end()); pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
            all.insert(all.end(), pages.begin(), pages.end());
        }
        std::sort(draw.begin(), draw.end()); draw.erase(std::unique(draw.begin(), draw.end()), draw.end());
        level.page_triangles.reserve(draw.size());
        for (const std::uint64_t e : draw) {
            const std::uint32_t page = std::uint32_t(e >> 32);
            if (level.page_ids.empty() || level.page_ids.back() != page) { level.page_ids.push_back(page); level.page_first.push_back(std::uint32_t(level.page_triangles.size())); }
            level.page_triangles.push_back(std::uint32_t(e));
        }
        level.page_first.push_back(std::uint32_t(level.page_triangles.size()));
        // Allocated (every page some read reaches, with its border) over ideal (the face area at D).
        std::sort(all.begin(), all.end()); all.erase(std::unique(all.begin(), all.end()), all.end());
        const double ratio = double(all.size()) * kTerrainFaceCacheSlot * kTerrainFaceCacheSlot / (atlas.face_area * D * D);
        std::snprintf(text, sizeof(text), " l%u=%zu/%.2f", l, all.size(), ratio);
        atlas.summary += text;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::snprintf(text, sizeof(text), "steep=%u bed=%u charts=%zu patches=%zu area=%.0f ms=%.0f pages/alloc_over_ideal:",
                  atlas.steep_triangles, atlas.bed_triangles, atlas.charts.size(), atlas.patches.size(), atlas.face_area, ms);
    atlas.summary = text + atlas.summary;
    return true;
} catch (...) {
    failure = "out of memory";
    return false;
}

std::shared_ptr<TerrainFaceAtlasJob> start_terrain_face_atlas_job(std::shared_ptr<const TerrainFaceGrid> grid,
    std::shared_ptr<const TerrainCpuAsset> terrain, std::string name) noexcept try {
    auto job = std::make_shared<TerrainFaceAtlasJob>();
    // The worker reads the job's cancel flag only; the job waits for it on destruction.
    const std::atomic<bool>* cancel = &job->cancel;
    job->result = std::async(std::launch::async, [cancel, grid = std::move(grid), terrain = std::move(terrain), name = std::move(name)]() mutable
                                                     -> std::shared_ptr<const TerrainFaceAtlas> {
        const BackgroundWorkerPriority priority;
        try {
            std::string failure;
            if (!grid && terrain) {
                auto built = std::make_shared<TerrainFaceGrid>();
                if (build_terrain_face_grid(*terrain, *built, failure)) grid = std::move(built);
                terrain.reset();
            }
            auto atlas = std::make_shared<TerrainFaceAtlas>();
            if (grid && build_terrain_face_atlas(grid, *atlas, failure, cancel)) {
                bridge_append_probe_event("terrain_face_atlas", (name + atlas->summary).c_str());
                return atlas;
            }
            if (failure != "cancelled")
                bridge_append_probe_event("terrain_face_cache_omitted", (name + "atlas not built: " + failure + "; faces stay per pixel").c_str());
        } catch (...) {}
        return nullptr;
    });
    return job;
} catch (...) {
    return nullptr;
}

#endif

void append_terrain_face_cache_shader_programs(std::vector<ShaderProgram>& out) {
    const std::string& source = owned_native_shader_source();
    static const D3D_SHADER_MACRO terrain[] = {{"NATIVE_TERRAIN_PS", "1"}, {"NATIVE_TERRAIN2_POOLS", "1"}, {nullptr, nullptr}};
    out.push_back({source, nullptr, terrain, "nativeFaceCacheVS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, nullptr, terrain, "nativeFaceCachePS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    static const D3D_SHADER_MACRO feedback[] = {{"NATIVE_TERRAIN_PS", "1"}, {"NATIVE_TERRAIN2_POOLS", "1"}, {"NATIVE_FACE_FEEDBACK", "1"}, {nullptr, nullptr}};
    out.push_back({source, nullptr, feedback, "nativeFaceFeedbackVS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, nullptr, feedback, "nativeFaceFeedbackPS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
}
// D3D12 (DXIL table only): the bake by compute; the feedback programs above are in the table already.
void append_terrain_face_cache_d3d12_programs(std::vector<ShaderProgram>& out) {
    const std::string& source = owned_native_shader_source();
    static const D3D_SHADER_MACRO terrain[] = {{"NATIVE_TERRAIN_PS", "1"}, {"NATIVE_TERRAIN2_POOLS", "1"}, {nullptr, nullptr}};
    out.push_back({source, nullptr, terrain, "nativeFaceCacheCS", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

#ifndef EID3D11_SHADER_EMBED
HRESULT Renderer::ensure_terrain_face_programs() noexcept {
    if (d3d12_) {
        // D3D12: the programs (bake CS, feedback VS and PS) by key; the executor builds their pipelines.
        if (face_failed_) return E_FAIL;
        if (face_programs12_.empty()) try {
            append_terrain_face_cache_d3d12_programs(face_programs12_);
            std::vector<ShaderProgram> programs; append_terrain_face_cache_shader_programs(programs);
            face_programs12_.push_back(programs[2]); face_programs12_.push_back(programs[3]);
        } catch (...) { face_programs12_.clear(); return E_OUTOFMEMORY; }
        return S_OK;
    }
    if (face_vs_ && face_ps_ && face_bake_constants_ && face_fb_constants_) return S_OK;
    if (face_failed_ || !device_ || feature_level_ < D3D_FEATURE_LEVEL_11_0) return E_FAIL;
    HRESULT hr = S_OK;
    try {
        std::vector<ShaderProgram> programs; append_terrain_face_cache_shader_programs(programs);
        ComPtr<ID3DBlob> code[4];
        for (unsigned k = 0; k < 4 && SUCCEEDED(hr); ++k) {
            ComPtr<ID3DBlob> error; hr = compile_shader(programs[k], &code[k], &error);
            if (FAILED(hr) && error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
        }
        if (SUCCEEDED(hr)) hr = create_shader(device_.Get(), code[0]->GetBufferPointer(), code[0]->GetBufferSize(), nullptr, &face_vs_);
        if (SUCCEEDED(hr)) hr = create_shader(device_.Get(), code[1]->GetBufferPointer(), code[1]->GetBufferSize(), nullptr, &face_ps_);
        if (SUCCEEDED(hr)) hr = create_shader(device_.Get(), code[2]->GetBufferPointer(), code[2]->GetBufferSize(), nullptr, &face_fb_vs_);
        if (SUCCEEDED(hr)) hr = create_shader(device_.Get(), code[3]->GetBufferPointer(), code[3]->GetBufferSize(), nullptr, &face_fb_ps_);
    } catch (...) { hr = E_OUTOFMEMORY; }
    if (SUCCEEDED(hr)) {
        D3D11_BUFFER_DESC d{}; d.ByteWidth = 160; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = device_->CreateBuffer(&d, nullptr, &face_fb_constants_);
    }
    if (SUCCEEDED(hr)) {
        D3D11_DEPTH_STENCIL_DESC ds{}; ds.DepthEnable = TRUE; ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; ds.DepthFunc = D3D11_COMPARISON_LESS;
        hr = device_->CreateDepthStencilState(&ds, &face_fb_depth_write_);
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        if (SUCCEEDED(hr)) hr = device_->CreateDepthStencilState(&ds, &face_fb_depth_test_);
    }
    if (SUCCEEDED(hr)) {
        D3D11_SAMPLER_DESC sampler{}; sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sampler.MaxLOD = 0;
        hr = device_->CreateSamplerState(&sampler, &face_sampler_);
    }
    if (SUCCEEDED(hr)) {
        D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
        hr = device_->CreateRasterizerState(&raster, &face_raster_);
    }
    if (SUCCEEDED(hr)) {
        D3D11_BUFFER_DESC d{}; d.ByteWidth = 32; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = device_->CreateBuffer(&d, nullptr, &face_bake_constants_);
    }
    if (FAILED(hr)) {
        face_vs_.Reset(); face_ps_.Reset(); face_sampler_.Reset(); face_raster_.Reset(); face_bake_constants_.Reset();
        face_fb_vs_.Reset(); face_fb_ps_.Reset(); face_fb_constants_.Reset(); face_fb_depth_write_.Reset(); face_fb_depth_test_.Reset();
        face_failed_ = true;
        char text[160]; std::snprintf(text, sizeof(text), "programs could not be created (0x%08lx); faces stay per pixel", static_cast<unsigned long>(hr));
        bridge_append_probe_event("terrain_face_cache_omitted", text);
    }
    return hr;
}

void Renderer::release_terrain_face_cache() noexcept {
    face_atlas_.reset();
    std::vector<TerrainFaceMap>().swap(face_maps_); std::vector<std::uint32_t>().swap(face_page_slot_);
    face_tris_.Reset(); face_tris_srv_.Reset(); face_fb_patches_.Reset(); face_fb_patches_srv_.Reset(); face_fb_bits_.Reset(); face_fb_bits_uav_.Reset();
    for (auto& s : face_fb_staging_) s.Reset();
    face_fb_issued_ = {}; face_fb_words_ = 0; face_fb_next_ = 0; face_fb_frames_ = 0; face_fb_log_ = 0;
    std::vector<std::uint32_t>().swap(face_fb_cpu_);
    std::vector<TerrainFaceSlot>().swap(face_slots_); std::vector<std::uint32_t>().swap(face_free_); std::vector<std::uint32_t>().swap(face_jobs_cpu_);
    face_color_.Reset(); face_height_.Reset(); face_color_srv_.Reset(); face_height_srv_.Reset(); face_color_rtvs_.clear(); face_height_rtvs_.clear();
    face_grid_.Reset(); face_cells_.Reset(); face_data_.Reset(); face_pages_.Reset(); face_jobs_.Reset(); face_depth_.Reset(); face_depth_srv_.Reset(); face_depth_hash_ = 0;
    face_grid_srv_.Reset(); face_cells_srv_.Reset(); face_data_srv_.Reset(); face_pages_srv_.Reset(); face_jobs_srv_.Reset();
    face_jobs_capacity_ = 0; face_header_ = {}; face_on_ = false; face_inputs_ = 0; face_fill_pages_ = 0; face_slices_ = 0;
    face_color12_.reset(); face_height12_.reset(); face_grid12_.reset(); face_cells12_.reset(); face_data12_.reset(); face_pages12_.reset();
    face_jobs12_.reset(); face_depth12_.reset(); face_tris12_.reset(); face_fb_patches12_.reset(); face_fb_bits12_.reset(); face_fb_depth12_.reset();
    face_fb_ticket12_ = {}; face_slim_bits12_.reset();
}

namespace {
HRESULT structured(ID3D11Device* device, UINT stride, UINT count, const void* data, D3D11_USAGE usage,
                   ComPtr<ID3D11Buffer>& buffer, ComPtr<ID3D11ShaderResourceView>& srv) {
    D3D11_BUFFER_DESC d{}; d.ByteWidth = stride * (std::max)(count, 1u); d.Usage = usage; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride = stride;
    const D3D11_SUBRESOURCE_DATA initial{data, 0, 0};
    HRESULT hr = device->CreateBuffer(&d, data ? &initial : nullptr, &buffer);
    if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(buffer.Get(), nullptr, &srv);
    return hr;
}
}  // namespace

// The GPU tables of face_maps_ (grids, cells, data, charted triangles and
// empty page tables, each map's part after the previous map's; the offsets
// in TerrainFaceMap and the map headers), the feedback bits and their copies.
// Every page is empty afterwards; the pool keeps its slices.
HRESULT Renderer::create_terrain_face_map() noexcept try {
    std::uint32_t vertices = 0, cells = 0, charts = 0, triangles = 0, patches = 0, pages = 0;
    for (auto& m : face_maps_) {
        const auto& a = *m.atlas;
        m.vertices = vertices; m.cells = cells; m.charts = charts; m.triangles = triangles;
        vertices += std::uint32_t(a.grid().size()); cells += std::uint32_t(a.cells.size());
        charts += std::uint32_t(a.charts.size()); triangles += std::uint32_t(a.triangles.size()); patches += std::uint32_t(a.patches.size());
        for (unsigned l = 0; l < kTerrainFaceCacheLevels; ++l) { m.pages[l] = pages; pages += a.levels[l].pages_x * a.levels[l].pages_y; }
    }
    std::vector<std::array<float, 4>> data(kTerrainFaceCacheHead + 7u * charts, std::array<float, 4>{});
    data[1] = {float(charts), 0.f, 0.f, 0.f};
    std::vector<std::uint32_t> tris; tris.reserve(triangles);
    for (const auto& m : face_maps_) {
        const auto& a = *m.atlas;
        const std::size_t h = 2u + kTerrainFaceCacheMapHead * m.entry;
        data[h] = {float(m.vertices), float(a.grid_x), float(a.grid_y), float(m.cells)};
        data[h + 1] = {m.origin[0], m.origin[1], m.origin[2], float(m.charts)};
        for (unsigned l = 0; l < kTerrainFaceCacheLevels; ++l)
            data[h + 2 + l] = {float(m.pages[l]), float(a.levels[l].pages_x), float(a.levels[l].pages_y), 0.f};
        for (std::uint32_t c = 0; c < a.charts.size(); ++c) {
            const auto& ch = a.charts[c];
            const std::size_t at = kTerrainFaceCacheHead + 3u * (m.charts + c);
            data[at] = {ch.t[0], ch.t[1], ch.t[2], ch.u0};
            data[at + 1] = {ch.b[0], ch.b[1], ch.b[2], ch.v0};
            data[at + 2] = {ch.su, ch.sv, 0.f, 0.f};
            for (unsigned l = 0; l < kTerrainFaceCacheLevels; ++l) {
                auto& o = data[kTerrainFaceCacheHead + 3u * charts + 4u * (m.charts + c) + l / 2];
                o[(l & 1) * 2] = a.levels[l].origin[c][0]; o[(l & 1) * 2 + 1] = a.levels[l].origin[c][1];
            }
        }
        for (const std::uint32_t t : a.triangles) tris.push_back(std::uint32_t(m.entry) << kTerrainFaceCacheMapShift | t);
    }
    std::vector<std::uint32_t> zero(pages, 0u);
    face_page_slot_.assign(pages, 0u);
    // Every slot free (the tables are new).
    face_free_.clear();
    for (std::uint32_t s = std::uint32_t(face_slots_.size()); s-- > 0;) { face_slots_[s] = TerrainFaceSlot{}; face_free_.push_back(s); }
    face_fb_words_ = (pages + 31u) / 32u;
    face_fb_issued_ = {}; face_fb_ticket12_ = {}; face_fb_next_ = 0; face_fb_cpu_.assign(face_fb_words_, 0u);
    face_header_ = {}; face_depth_hash_ = 0;
    if (d3d12_) {
        // The same tables as D3D12 buffers (structured, the same strides); the bits are a u-slot target.
        const auto make = [&](UINT64 bytes, UINT stride, UINT flags, const void* initial, D3D12BufferRef& out) -> HRESULT {
            out.reset();
            HRESULT r = d3d12_->create_buffer((std::max<UINT64>)(bytes, stride), stride, flags, out);
            if (SUCCEEDED(r) && initial && bytes) r = d3d12_->write_buffer_bulk(*out, 0, initial, bytes);
            return r;
        };
        HRESULT hr = make(std::uint64_t(vertices) * 32, 16, 0, nullptr, face_grid12_);
        if (SUCCEEDED(hr)) hr = make(std::uint64_t(cells) * 4, 4, 0, nullptr, face_cells12_);
        for (const auto& m : face_maps_) {
            const auto& a = *m.atlas;
            if (SUCCEEDED(hr)) hr = d3d12_->write_buffer_bulk(*face_grid12_, std::uint64_t(m.vertices) * 32, a.grid().data(), a.grid().size() * 32);
            if (SUCCEEDED(hr)) hr = d3d12_->write_buffer_bulk(*face_cells12_, std::uint64_t(m.cells) * 4, a.cells.data(), a.cells.size() * 4);
        }
        if (SUCCEEDED(hr)) hr = make(data.size() * 16, 16, 0, data.data(), face_data12_);
        if (SUCCEEDED(hr)) hr = make(std::uint64_t(pages) * 4, 4, 0, zero.data(), face_pages12_);
        if (SUCCEEDED(hr)) hr = make(tris.size() * 4, 4, 0, tris.data(), face_tris12_);
        if (SUCCEEDED(hr)) hr = make(std::uint64_t(patches) * 8, 8, 0, nullptr, face_fb_patches12_);
        if (SUCCEEDED(hr)) hr = make(std::uint64_t(face_fb_words_) * 4, 4, kD3D12BufferUnordered, nullptr, face_fb_bits12_);
        if (SUCCEEDED(hr)) hr = make(std::uint64_t(face_fb_words_) * 4, 4, kD3D12BufferUnordered, nullptr, face_slim_bits12_);
        if (SUCCEEDED(hr)) d3d12_->clear_buffer_uint(*face_slim_bits12_, 0);
        face_depth12_.reset();
        return hr;
    }
    face_grid_.Reset(); face_grid_srv_.Reset(); face_cells_.Reset(); face_cells_srv_.Reset(); face_data_.Reset(); face_data_srv_.Reset();
    face_pages_.Reset(); face_pages_srv_.Reset(); face_tris_.Reset(); face_tris_srv_.Reset(); face_fb_patches_.Reset(); face_fb_patches_srv_.Reset();
    face_fb_bits_.Reset(); face_fb_bits_uav_.Reset(); for (auto& s : face_fb_staging_) s.Reset();
    face_depth_.Reset(); face_depth_srv_.Reset();
    HRESULT hr = structured(device_.Get(), 16, vertices * 2, nullptr, D3D11_USAGE_DEFAULT, face_grid_, face_grid_srv_);
    if (SUCCEEDED(hr)) hr = structured(device_.Get(), 4, cells, nullptr, D3D11_USAGE_DEFAULT, face_cells_, face_cells_srv_);
    if (SUCCEEDED(hr))
        for (const auto& m : face_maps_) {
            const auto& a = *m.atlas;
            const D3D11_BOX grid{m.vertices * 32u, 0, 0, m.vertices * 32u + UINT(a.grid().size() * 32), 1, 1};
            context_->UpdateSubresource(face_grid_.Get(), 0, &grid, a.grid().data(), 0, 0);
            const D3D11_BOX cell{m.cells * 4u, 0, 0, m.cells * 4u + UINT(a.cells.size() * 4), 1, 1};
            context_->UpdateSubresource(face_cells_.Get(), 0, &cell, a.cells.data(), 0, 0);
        }
    if (SUCCEEDED(hr)) hr = structured(device_.Get(), 16, UINT(data.size()), data.data(), D3D11_USAGE_DEFAULT, face_data_, face_data_srv_);
    if (SUCCEEDED(hr)) hr = structured(device_.Get(), 4, pages, zero.data(), D3D11_USAGE_DEFAULT, face_pages_, face_pages_srv_);
    // Feedback: every charted triangle (by map and chart, so a patch is a
    // run), the patches in view, a bit per page table entry and its staging copies.
    if (SUCCEEDED(hr)) hr = structured(device_.Get(), 4, UINT(tris.size()), tris.data(), D3D11_USAGE_IMMUTABLE, face_tris_, face_tris_srv_);
    if (SUCCEEDED(hr)) hr = structured(device_.Get(), 8, patches, nullptr, D3D11_USAGE_DEFAULT, face_fb_patches_, face_fb_patches_srv_);
    if (SUCCEEDED(hr)) {
        D3D11_BUFFER_DESC d{}; d.ByteWidth = (std::max)(face_fb_words_, 1u) * 4; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride = 4;
        hr = device_->CreateBuffer(&d, nullptr, &face_fb_bits_);
        if (SUCCEEDED(hr)) hr = device_->CreateUnorderedAccessView(face_fb_bits_.Get(), nullptr, &face_fb_bits_uav_);
        D3D11_BUFFER_DESC s{}; s.ByteWidth = d.ByteWidth; s.Usage = D3D11_USAGE_STAGING; s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (auto& staging : face_fb_staging_) if (SUCCEEDED(hr)) hr = device_->CreateBuffer(&s, nullptr, &staging);
    }
    return hr;
} catch (...) {
    return E_OUTOFMEMORY;
}

// The pool grows to `slices` (2040 x 2040 RGBA8 colour + R8 height each),
// copying the resident slots.
bool Renderer::grow_terrain_face_pool(std::uint32_t slices) noexcept try {
    const std::uint32_t old = face_slices_;
    // Growing keeps every slice; shrinking (the caller emptied the slots of
    // the slices that go) keeps the first ones.
    if (slices == old || !slices) return slices == old;
    if (slices > D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION) return false;
    const auto add_slots = [&] {
        face_slices_ = slices;
        const std::uint32_t first = std::uint32_t((std::min)(face_slots_.size(), std::size_t(slices) * kTerrainFaceCacheSlotsPerSlice));
        face_slots_.resize(std::size_t(slices) * kTerrainFaceCacheSlotsPerSlice);
        face_free_.erase(std::remove_if(face_free_.begin(), face_free_.end(), [&](std::uint32_t s) { return s >= face_slots_.size(); }), face_free_.end());
        for (std::uint32_t s = std::uint32_t(face_slots_.size()); s-- > first;) face_free_.push_back(s);
    };
    if (d3d12_) {
        // Storage arrays the compute bake writes; the resident slices copied over.
        D3D12TextureRef color, height;
        if (FAILED(d3d12_->create_storage_texture(UINT(kTerrainFaceCacheSlice), UINT(kTerrainFaceCacheSlice), 0, DXGI_FORMAT_R8G8B8A8_UNORM, color, 1, slices, true)) ||
            FAILED(d3d12_->create_storage_texture(UINT(kTerrainFaceCacheSlice), UINT(kTerrainFaceCacheSlice), 0, DXGI_FORMAT_R8_UNORM, height, 1, slices, true)))
            return false;
        for (std::uint32_t s = 0; s < (std::min)(old, slices); ++s)
            if (FAILED(d3d12_->copy_texture(*color, s, *face_color12_, s)) || FAILED(d3d12_->copy_texture(*height, s, *face_height12_, s))) return false;
        face_color12_ = color; face_height12_ = height;
        add_slots();
        return true;
    }
    ComPtr<ID3D11Texture2D> color, height; ComPtr<ID3D11ShaderResourceView> color_srv, height_srv;
    std::vector<ComPtr<ID3D11RenderTargetView>> color_rtvs, height_rtvs;
    const auto make = [&](DXGI_FORMAT format, ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11ShaderResourceView>& srv,
                          std::vector<ComPtr<ID3D11RenderTargetView>>& rtvs) -> HRESULT {
        D3D11_TEXTURE2D_DESC d{}; d.Width = d.Height = UINT(kTerrainFaceCacheSlice); d.MipLevels = 1; d.ArraySize = slices; d.Format = format;
        d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = device_->CreateTexture2D(&d, nullptr, &texture);
        if (SUCCEEDED(hr)) hr = device_->CreateShaderResourceView(texture.Get(), nullptr, &srv);
        for (std::uint32_t s = 0; s < slices && SUCCEEDED(hr); ++s) {
            D3D11_RENDER_TARGET_VIEW_DESC v{}; v.Format = format; v.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            v.Texture2DArray.FirstArraySlice = s; v.Texture2DArray.ArraySize = 1;
            ComPtr<ID3D11RenderTargetView> rtv; hr = device_->CreateRenderTargetView(texture.Get(), &v, &rtv); rtvs.push_back(rtv);
        }
        return hr;
    };
    HRESULT hr = make(DXGI_FORMAT_R8G8B8A8_UNORM, color, color_srv, color_rtvs);
    if (SUCCEEDED(hr)) hr = make(DXGI_FORMAT_R8_UNORM, height, height_srv, height_rtvs);
    if (FAILED(hr)) return false;
    for (std::uint32_t s = 0; s < (std::min)(old, slices); ++s) {
        context_->CopySubresourceRegion(color.Get(), s, 0, 0, 0, face_color_.Get(), s, nullptr);
        context_->CopySubresourceRegion(height.Get(), s, 0, 0, 0, face_height_.Get(), s, nullptr);
    }
    face_color_ = color; face_height_ = height; face_color_srv_ = color_srv; face_height_srv_ = height_srv;
    face_color_rtvs_ = std::move(color_rtvs); face_height_rtvs_ = std::move(height_rtvs);
    add_slots();
    return true;
} catch (...) {
    return false;
}

void Renderer::bind_terrain_face_cache() noexcept {
    const bool on = face_on_;
    if (d3d12_) return; // bind_terrain_face_cache_d3d12, from bind_terrain2_d3d12
    ID3D11ShaderResourceView* views[6]{on ? face_color_srv_.Get() : nullptr, on ? face_height_srv_.Get() : nullptr, on ? face_grid_srv_.Get() : nullptr,
                                       on ? face_cells_srv_.Get() : nullptr, on ? face_data_srv_.Get() : nullptr, on ? face_pages_srv_.Get() : nullptr};
    context_->PSSetShaderResources(100, 6, views);
    // s15: the material cache's sampler when it exists (the same bilinear filter).
    if (on && !terrain2_cache_sampler_) { ID3D11SamplerState* s = face_sampler_.Get(); context_->PSSetSamplers(15, 1, &s); }
}

// D3D12: the pixel stage's views (t100-t105; s15 is the material cache's
// sampler, which bind_terrain2_d3d12 always sets), with the world flush's slots.
void Renderer::bind_terrain_face_cache_d3d12() noexcept {
    const bool on = face_on_ && d3d12_;
    if (!d3d12_) return;
    d3d12_->set_resource(100, on ? face_color12_.get() : nullptr); d3d12_->set_resource(101, on ? face_height12_.get() : nullptr);
    d3d12_->set_resource(102, on ? face_grid12_.get() : nullptr); d3d12_->set_resource(103, on ? face_cells12_.get() : nullptr);
    d3d12_->set_resource(104, on ? face_data12_.get() : nullptr); d3d12_->set_resource(105, on ? face_pages12_.get() : nullptr);
}

// The baked/live check (option terrain_face_cache_check): the charts in view
// drawn with the frame's camera into an offscreen target (own depth), each
// pixel comparing the cache read with terrain2Color at its own footprint;
// GPU counters read back a few frames later and logged once per check.
// 2 also writes the |baked - live| x 8 view (misses blue) beside the F11 captures.
void Renderer::run_terrain_face_check(const std::vector<std::uint32_t>& charts, int mode, const float eye_hiz[4]) noexcept try {
    if (!face_check_vs_ || !face_check_ps_) {
        std::vector<ShaderProgram> programs;
        const std::string& source = owned_native_shader_source();
        static const D3D_SHADER_MACRO check[] = {{"NATIVE_TERRAIN_PS", "1"}, {"NATIVE_TERRAIN2_POOLS", "1"}, {"NATIVE_FACE_CHECK", "1"}, {nullptr, nullptr}};
        programs.push_back({source, nullptr, check, "nativeFaceCheckVS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
        programs.push_back({source, nullptr, check, "nativeFaceCheckPS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
        ComPtr<ID3DBlob> code[2];
        for (unsigned k = 0; k < 2; ++k) {
            ComPtr<ID3DBlob> error;
            if (FAILED(compile_shader(programs[k], &code[k], &error))) {
                if (error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
                bridge_append_probe_event("terrain_face_cache_check", error ? static_cast<const char*>(error->GetBufferPointer()) : "check programs could not be compiled"); return;
            }
        }
        if (FAILED(create_shader(device_.Get(), code[0]->GetBufferPointer(), code[0]->GetBufferSize(), nullptr, &face_check_vs_)) ||
            FAILED(create_shader(device_.Get(), code[1]->GetBufferPointer(), code[1]->GetBufferSize(), nullptr, &face_check_ps_))) {
            bridge_append_probe_event("terrain_face_cache_check", "check shaders could not be created"); return; }
    }
    const UINT w = UINT(width_), h = UINT(height_);
    const auto fail = [&](const char* what) { char text[96]; std::snprintf(text, sizeof(text), "%s (%ux%u)", what, w, h); bridge_append_probe_event("terrain_face_cache_check", text); };
    D3D11_TEXTURE2D_DESC have{};
    if (face_check_view_) face_check_view_->GetDesc(&have);
    if (!face_check_view_ || have.Width != w || have.Height != h) {
        face_check_view_.Reset(); face_check_rtv_.Reset(); face_check_depth_.Reset(); face_check_dsv_.Reset(); face_check_staging_view_.Reset();
        D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(device_->CreateTexture2D(&d, nullptr, &face_check_view_)) || FAILED(device_->CreateRenderTargetView(face_check_view_.Get(), nullptr, &face_check_rtv_))) { fail("view target failed"); return; }
        d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device_->CreateTexture2D(&d, nullptr, &face_check_staging_view_))) { fail("view staging failed"); return; }
        d.Format = DXGI_FORMAT_D32_FLOAT; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_DEPTH_STENCIL; d.CPUAccessFlags = 0;
        if (FAILED(device_->CreateTexture2D(&d, nullptr, &face_check_depth_)) || FAILED(device_->CreateDepthStencilView(face_check_depth_.Get(), nullptr, &face_check_dsv_))) { fail("depth failed"); return; }
    }
    if (!face_check_counters_) {
        D3D11_BUFFER_DESC d{}; d.ByteWidth = 32; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride = 4;
        if (FAILED(device_->CreateBuffer(&d, nullptr, &face_check_counters_)) ||
            FAILED(device_->CreateUnorderedAccessView(face_check_counters_.Get(), nullptr, &face_check_uav_))) { fail("counters failed"); return; }
        D3D11_BUFFER_DESC s{}; s.ByteWidth = 32; s.Usage = D3D11_USAGE_STAGING; s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device_->CreateBuffer(&s, nullptr, &face_check_staging_))) return;
        D3D11_BUFFER_DESC c{}; c.ByteWidth = 160; c.Usage = D3D11_USAGE_DYNAMIC; c.BindFlags = D3D11_BIND_CONSTANT_BUFFER; c.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateBuffer(&c, nullptr, &face_check_constants_))) return;
        D3D11_DEPTH_STENCIL_DESC ds{}; ds.DepthEnable = TRUE; ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; ds.DepthFunc = D3D11_COMPARISON_LESS;
        if (FAILED(device_->CreateDepthStencilState(&ds, &face_check_depth_state_))) return;
    }
    // The active map's charts' triangles (ids with its table entry) after this frame's bake jobs.
    const TerrainFaceMap* active = nullptr;
    for (const auto& m : face_maps_) if (m.contact_map == 0) active = &m;
    if (!active) { fail("no active map atlas"); return; }
    const TerrainFaceAtlas& atlas = *active->atlas;
    const std::uint32_t tag = std::uint32_t(active->entry) << kTerrainFaceCacheMapShift;
    std::vector<std::uint32_t> list;
    for (const std::uint32_t id : charts) {
        const auto& c = atlas.charts[id];
        for (std::uint32_t t = c.first; t < c.first + c.count; ++t) list.push_back(tag | atlas.triangles[t]);
    }
    if (list.empty()) { fail("no charts in view"); return; }
    if (list.size() > face_jobs_capacity_) {
        face_jobs_capacity_ = std::uint32_t(list.size() * 3 / 2 + 1024); face_jobs_.Reset(); face_jobs_srv_.Reset();
        if (FAILED(structured(device_.Get(), 4, face_jobs_capacity_, nullptr, D3D11_USAGE_DEFAULT, face_jobs_, face_jobs_srv_))) { face_jobs_capacity_ = 0; return; }
    }
    const D3D11_BOX box{0, 0, 0, UINT(list.size() * 4), 1, 1};
    context_->UpdateSubresource(face_jobs_.Get(), 0, &box, list.data(), 0, 0);
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(face_check_constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
        float clip[40]{}; for (unsigned k = 0; k < 16; ++k) { clip[k] = float(native_world_clip_[k]); clip[20 + k] = float(native_hiz_clip_[k]); }
        clip[16] = float(mode); clip[17] = eye_hiz[3]; for (unsigned k = 0; k < 3; ++k) clip[36 + k] = eye_hiz[k];
        std::memcpy(mapped.pData, clip, sizeof(clip)); context_->Unmap(face_check_constants_.Get(), 0);
    }
    const float black[4]{0, 0, 0, 1}; const UINT zero[4]{};
    context_->ClearRenderTargetView(face_check_rtv_.Get(), black);
    context_->ClearDepthStencilView(face_check_dsv_.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
    context_->ClearUnorderedAccessViewUint(face_check_uav_.Get(), zero);
    ID3D11ShaderResourceView* tables[6]{face_grid_srv_.Get(), face_cells_srv_.Get(), face_data_srv_.Get(), face_pages_srv_.Get(), face_jobs_srv_.Get(), face_depth_srv_.Get()};
    context_->VSSetShaderResources(102, 6, tables); context_->PSSetShaderResources(102, 6, tables);
    ID3D11ShaderResourceView* pool[2]{face_color_srv_.Get(), face_height_srv_.Get()}; context_->PSSetShaderResources(100, 2, pool);
    { ID3D11ShaderResourceView* pyramid = eye_hiz[3] > 0 ? native_hiz_mip_srv_[0].Get() : nullptr; context_->PSSetShaderResources(108, 1, &pyramid); }
    { ID3D11SamplerState* s = face_sampler_.Get(); context_->PSSetSamplers(15, 1, &s); }
    ComPtr<ID3D11Buffer> ps_b13; context_->PSGetConstantBuffers(13, 1, &ps_b13);
    { ID3D11Buffer* b = face_check_constants_.Get(); context_->VSSetConstantBuffers(13, 1, &b); context_->PSSetConstantBuffers(13, 1, &b); } // PS: the view mode
    context_->VSSetShader(face_check_vs_.Get(), nullptr, 0); context_->PSSetShader(face_check_ps_.Get(), nullptr, 0);
    context_->OMSetDepthStencilState(face_check_depth_state_.Get(), 0);
    ID3D11RenderTargetView* rtv = face_check_rtv_.Get(); ID3D11UnorderedAccessView* uav = face_check_uav_.Get();
    context_->OMSetRenderTargetsAndUnorderedAccessViews(1, &rtv, face_check_dsv_.Get(), 1, 1, &uav, nullptr);
    const D3D11_VIEWPORT viewport{0, 0, float(w), float(h), 0, 1};
    context_->RSSetViewports(1, &viewport);
    context_->Draw(UINT(list.size() * 3), 0);
    ID3D11ShaderResourceView* none[8]{};
    context_->VSSetShaderResources(100, 8, none); context_->PSSetShaderResources(100, 8, none); context_->PSSetShaderResources(108, 1, none);
    { ID3D11Buffer* b = nullptr; context_->VSSetConstantBuffers(13, 1, &b); b = ps_b13.Get(); context_->PSSetConstantBuffers(13, 1, &b); }
    context_->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 0, nullptr, nullptr);
    context_->CopyResource(face_check_staging_.Get(), face_check_counters_.Get());
    face_check_image_ = mode >= 2;
    if (face_check_image_) context_->CopyResource(face_check_staging_view_.Get(), face_check_view_.Get());
    face_check_pending_ = native_frame_index_;
} catch (...) {
    bridge_append_probe_event("terrain_face_cache_check", "exception");
}

// The counters of the last check, once the GPU is done with them.
void Renderer::read_terrain_face_check() noexcept try {
    if (!face_check_pending_ || !face_check_staging_) return;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (context_->Map(face_check_staging_.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped) != S_OK) return;
    std::uint32_t c[8]{}; std::memcpy(c, mapped.pData, sizeof(c)); context_->Unmap(face_check_staging_.Get(), 0);
    std::string image;
    if (face_check_image_ && SUCCEEDED(context_->Map(face_check_staging_view_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        D3D11_TEXTURE2D_DESC d{}; face_check_staging_view_->GetDesc(&d);
        char folder[MAX_PATH]{}; DWORD n = GetEnvironmentVariableA("EI_RENDERER_DEBUG_DIR", folder, MAX_PATH);
        std::string root = n && n < MAX_PATH ? std::string(folder) : std::string();
        if (root.empty()) { n = GetEnvironmentVariableA("LOCALAPPDATA", folder, MAX_PATH); if (n && n < MAX_PATH) root = std::string(folder) + "\\EID3D11"; }
        if (!root.empty()) {
            CreateDirectoryA((root + "\\captures").c_str(), nullptr);
            image = root + "\\captures\\face-check-" + std::to_string(face_check_pending_) + ".bmp";
            if (FILE* f = std::fopen(image.c_str(), "wb")) {
                const std::uint32_t row = d.Width * 3u + 3u & ~3u, size = 54u + row * d.Height;
                unsigned char header[54]{'B', 'M'};
                const auto put = [&](unsigned at, std::uint32_t v) { std::memcpy(header + at, &v, 4); };
                put(2, size); put(10, 54); put(14, 40); put(18, d.Width); put(22, d.Height); header[26] = 1; header[28] = 24; put(34, row * d.Height);
                std::fwrite(header, 1, 54, f);
                std::vector<unsigned char> line(row, 0);
                for (UINT y = d.Height; y-- > 0;) {
                    const auto* src = static_cast<const unsigned char*>(mapped.pData) + std::size_t(y) * mapped.RowPitch;
                    for (UINT x = 0; x < d.Width; ++x) { line[x * 3] = src[x * 4 + 2]; line[x * 3 + 1] = src[x * 4 + 1]; line[x * 3 + 2] = src[x * 4]; }
                    std::fwrite(line.data(), 1, row, f);
                }
                std::fclose(f);
            } else image.clear();
        }
        context_->Unmap(face_check_staging_view_.Get(), 0);
    }
    char text[512];
    std::snprintf(text, sizeof(text), "frame=%llu compared=%u miss=%u over2=%u (%.3f%%) over8=%u (%.3f%%) top_plane=%u black=%u hidden=%u image=%s",
                  static_cast<unsigned long long>(face_check_pending_), c[0], c[1], c[2], c[0] ? 100.0 * c[2] / c[0] : 0.0, c[3],
                  c[0] ? 100.0 * c[3] / c[0] : 0.0, c[4], c[5], c[6], image.empty() ? "-" : image.c_str());
    bridge_append_probe_event("terrain_face_cache_check", text);
    face_check_pending_ = 0;
} catch (...) {
    face_check_pending_ = 0;
}

// The feedback copies the GPU has finished, oldest first, or-ed into face_fb_cpu_.
bool Renderer::read_terrain_face_feedback() noexcept {
    bool any = false;
    face_fb_read_cover_ = 0;
    const auto covered = [&](unsigned k) { if (face_fb_epoch_[k] == face_still_epoch_) face_fb_read_cover_ |= 1u << face_fb_jitter_[k]; };
    if (d3d12_) {
        // D3D12: the readback tickets, oldest first (face_fb_issued_ the frames).
        for (;;) {
            unsigned oldest = 3;
            for (unsigned k = 0; k < 3; ++k)
                if (face_fb_issued_[k] && (oldest == 3 || face_fb_issued_[k] < face_fb_issued_[oldest])) oldest = k;
            if (oldest == 3) return any;
            std::vector<std::uint8_t> data;
            const HRESULT hr = d3d12_->poll_readback(face_fb_ticket12_[oldest], data);
            if (hr == S_FALSE) return any;
            face_fb_issued_[oldest] = 0;
            if (hr != S_OK || data.size() < std::size_t(face_fb_words_) * 4) continue; // dropped: the next one
            const auto* bits = reinterpret_cast<const std::uint32_t*>(data.data());
            if (!any) std::memcpy(face_fb_cpu_.data(), bits, std::size_t(face_fb_words_) * 4);
            else for (std::uint32_t w = 0; w < face_fb_words_; ++w) face_fb_cpu_[w] |= bits[w];
            covered(oldest); ++face_fb_frames_; any = true;
        }
    }
    for (;;) {
        unsigned oldest = 3;
        for (unsigned k = 0; k < 3; ++k)
            if (face_fb_issued_[k] && (oldest == 3 || face_fb_issued_[k] < face_fb_issued_[oldest])) oldest = k;
        if (oldest == 3) return any;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (context_->Map(face_fb_staging_[oldest].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped) != S_OK) return any;
        const auto* bits = static_cast<const std::uint32_t*>(mapped.pData);
        if (!any) std::memcpy(face_fb_cpu_.data(), bits, std::size_t(face_fb_words_) * 4);
        else for (std::uint32_t w = 0; w < face_fb_words_; ++w) face_fb_cpu_[w] |= bits[w];
        context_->Unmap(face_fb_staging_[oldest].Get(), 0);
        covered(oldest); face_fb_issued_[oldest] = 0; ++face_fb_frames_; any = true;
    }
}

// Once a world frame, before the terrain draws (after the material cache):
// the atlas of a new map, the maps the tables hold (the active map and the
// neighbours with atlases in the terrain 2.0 table), invalidation, page
// requests from the view, the bake within its budget, the page tables, the
// pixel stage's views.
void Renderer::update_terrain_face_cache() noexcept try {
    if (scene_composited_) return;
    const auto& config = debug_overlay_.config();
    const int mode = std::clamp(configured_integer(config, "terrain_face_cache", 1), 0, 2);
    const bool on = mode > 0 && terrain2_table_count_ > 0 && native_world_clip_valid_ && !face_failed_ && !vram_face_cache_dropped() &&
        (d3d12_ || feature_level_ >= D3D_FEATURE_LEVEL_11_0);
    // Off (or unavailable) frees the atlases (the neighbours' too: their
    // grids stay for the next time it turns on), the tables and the pool.
    if (!on) {
        if (face_atlas_ || face_atlas_job_ || !face_maps_.empty() || face_color_ || face_color12_ || face_on_) release_terrain_face_cache();
        face_atlas_job_.reset();
        if (mode == 0) for (auto& n : gc_neighbours_) { n.face_job.reset(); n.face_atlas.reset(); }
        face_map_ = UINT64_MAX; bind_terrain_face_cache(); return;
    }
    if (face_frame_ == native_frame_index_ || hit_test_active_) { bind_terrain_face_cache(); return; }
    face_frame_ = native_frame_index_;
    // CPU per step (profiler CPU scopes face_*): each mark closes the step since the last.
    std::int64_t cpu_since = profiler_.enabled() ? perf_now() : 0;
    const auto cpu_mark = [&](const char* name) {
        if (!cpu_since) return;
        const std::int64_t now = perf_now(); profiler_.add_cpu_time(name, now - cpu_since); cpu_since = now;
    };
    if (FAILED(ensure_terrain_face_programs())) { release_terrain_face_cache(); bind_terrain_face_cache(); return; }
    if (!d3d12_) read_terrain_face_check();
    // A new map: drop every atlas and page, build the active map's atlas on a worker.
    if (ambient_map_generation_ != face_map_) {
        release_terrain_face_cache(); face_map_ = ambient_map_generation_;
        face_atlas_job_.reset();
        if (auto terrain = ambient_terrain_.lock()) face_atlas_job_ = start_terrain_face_atlas_job(nullptr, std::move(terrain), "");
    }
    if (!face_atlas_ && face_atlas_job_ && face_atlas_job_->ready()) face_atlas_ = face_atlas_job_->result.get();
    // The maps with an atlas, by table entry; a different set makes the tables again.
    {
        static_assert(kTerrainFaceCacheMaps == kTerrain2TableMaps, "the face cache's map headers are the terrain 2.0 table's entries");
        std::vector<TerrainFaceMap> maps;
        for (unsigned k = 0; k < terrain2_table_count_; ++k) {
            TerrainFaceMap m; m.entry = k; m.contact_map = terrain2_table_[k];
            if (m.contact_map == 0) m.atlas = face_atlas_;
            else for (auto& n : gc_neighbours_) if (n.contact_map == m.contact_map) {
                // Its atlas from its grid on a worker, taken once done (until
                // then its faces shade per pixel); the job stays as "started".
                if (!n.face_job && n.face_grid) n.face_job = start_terrain_face_atlas_job(n.face_grid, nullptr, "map=" + n.name + " ");
                if (!n.face_atlas && n.face_job && n.face_job->ready()) n.face_atlas = n.face_job->result.get();
                m.atlas = n.face_atlas; m.origin = n.origin; break;
            }
            if (m.atlas) maps.push_back(std::move(m));
        }
        const bool same = maps.size() == face_maps_.size() && std::equal(maps.begin(), maps.end(), face_maps_.begin(), [](const TerrainFaceMap& a, const TerrainFaceMap& b) {
            return a.atlas == b.atlas && a.entry == b.entry && a.contact_map == b.contact_map && a.origin == b.origin; });
        if (!same) {
            face_maps_ = std::move(maps); ++face_page_generation_;
            if (face_maps_.empty()) { std::vector<std::uint32_t>().swap(face_page_slot_); face_on_ = false; bind_terrain_face_cache(); return; }
            if (FAILED(create_terrain_face_map())) {
                release_terrain_face_cache(); face_failed_ = true;
                bridge_append_probe_event("terrain_face_cache_omitted", "map tables could not be created; faces stay per pixel");
                bind_terrain_face_cache(); return;
            }
            // CPU memory: the neighbours' kept grids (with or without the cache) and the atlases' own tables.
            double grids = 0, atlases = 0;
            for (const auto& n : gc_neighbours_) if (n.face_grid) grids += double(n.face_grid->grid.size()) * 33;
            for (const auto& m : face_maps_) {
                const auto& a = *m.atlas;
                atlases += a.cells.size() * 4.0 + a.charts.size() * sizeof(TerrainFaceAtlas::Chart) + a.triangles.size() * 4.0 + a.patches.size() * sizeof(TerrainFaceAtlas::Patch);
                for (const auto& l : a.levels)
                    atlases += l.origin.size() * 8.0 + (l.page_ids.size() + l.page_first.size() + l.page_triangles.size()) * 4.0;
            }
            char text[200]; int n = std::snprintf(text, sizeof(text), "neighbour_grids_mb=%.1f atlas_tables_mb=%.1f maps=%zu entries:", grids / 1048576.0, atlases / 1048576.0, face_maps_.size());
            for (const auto& m : face_maps_) if (n > 0 && n < int(sizeof(text))) n += std::snprintf(text + n, sizeof(text) - n, " %u%s", m.entry, m.contact_map ? "" : "(active)");
            bridge_append_probe_event("terrain_face_cache_maps", text);
        }
    }
    if (face_maps_.empty()) { face_on_ = false; bind_terrain_face_cache(); return; }

    // Inputs: those of the material cache (terrain2Color's) and the live liquid
    // shifts (the active map's bed depth); any change empties every page.
    std::uint64_t inputs = terrain2_inputs_hash();
    {
        const std::vector<float>* shifts = ambient_geometry_ ? &ambient_geometry_->layer_shifts : nullptr;
        std::uint64_t shift_hash = 1469598103934665603ull;
        if (shifts) for (const float s : *shifts) { std::uint32_t b; std::memcpy(&b, &s, 4); shift_hash = (shift_hash ^ b) * 1099511628211ull; }
        shift_hash |= 1u; // never 0 (0: the depth buffer is to be made)
        if (shift_hash != face_depth_hash_ || !(d3d12_ ? bool(face_depth12_) : bool(face_depth_))) {
            // Per vertex: the signed bed depth (rest depth, on the active map
            // plus its object's live shift; neighbours keep their rest layers; 0 without a bed).
            std::size_t count = 0;
            for (const auto& m : face_maps_) count += m.atlas->grid().size();
            std::vector<float> depth(count, 0.f);
            for (const auto& m : face_maps_) {
                const auto& grid = m.atlas->grid();
                for (std::size_t v = 0; v < grid.size(); ++v) {
                    std::uint32_t object; std::memcpy(&object, &grid[v][7], 4);
                    if (object == UINT16_MAX) continue;
                    const float s = m.contact_map == 0 && shifts && object < shifts->size() && std::isfinite((*shifts)[object]) ? (*shifts)[object] : 0.f;
                    depth[m.vertices + v] = grid[v][6] + s;
                }
            }
            const bool made = d3d12_ ? (face_depth12_ || SUCCEEDED(d3d12_->create_buffer(depth.size() * 4, 4, 0, face_depth12_)))
                                     : (face_depth_ || SUCCEEDED(structured(device_.Get(), 4, UINT(depth.size()), nullptr, D3D11_USAGE_DEFAULT, face_depth_, face_depth_srv_)));
            if (!made) {
                face_failed_ = true; release_terrain_face_cache();
                bridge_append_probe_event("terrain_face_cache_omitted", "bed depth buffer could not be created; faces stay per pixel");
                bind_terrain_face_cache(); return;
            }
            if (d3d12_) (void)d3d12_->write_buffer(*face_depth12_, 0, depth.data(), depth.size() * 4);
            else context_->UpdateSubresource(face_depth_.Get(), 0, nullptr, depth.data(), 0, 0);
            face_depth_hash_ = shift_hash;
        }
        inputs ^= shift_hash * 31u;
        for (const auto& m : face_maps_) { const auto* p = m.atlas.get(); inputs = (inputs ^ reinterpret_cast<std::uintptr_t>(p)) * 1099511628211ull; }
    }
    if (inputs != face_inputs_) {
        face_inputs_ = inputs;
        for (std::uint32_t s = 0; s < face_slots_.size(); ++s)
            if (face_slots_[s].map != UINT32_MAX) { face_slots_[s] = TerrainFaceSlot{}; face_free_.push_back(s); }
        std::fill(face_page_slot_.begin(), face_page_slot_.end(), 0u); ++face_page_generation_;
        try {
            std::vector<std::uint32_t> zero(face_page_slot_.size(), 0u);
            if (d3d12_) (void)d3d12_->write_buffer(*face_pages12_, 0, zero.data(), std::uint64_t(zero.size()) * 4);
            else context_->UpdateSubresource(face_pages_.Get(), 0, nullptr, zero.data(), 0, 0);
        } catch (...) {}
    }

    // The view: eye, frustum planes (rows of the world-to-clip matrix).
    const auto& m = native_world_clip_;
    const double row[4][4]{{m[0], m[1], m[2], m[3]}, {m[4], m[5], m[6], m[7]}, {m[8], m[9], m[10], m[11]}, {m[12], m[13], m[14], m[15]}};
    double eye[3]{};
    {   // x = y = w = 0 in clip space.
        const double A[3][3]{{row[0][0], row[0][1], row[0][2]}, {row[1][0], row[1][1], row[1][2]}, {row[3][0], row[3][1], row[3][2]}};
        const double r[3]{-row[0][3], -row[1][3], -row[3][3]};
        const double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
        if (std::abs(det) < 1e-18) { bind_terrain_face_cache(); return; }
        for (unsigned c = 0; c < 3; ++c) {
            double M[3][3]; std::memcpy(M, A, sizeof(M));
            for (unsigned k = 0; k < 3; ++k) M[k][c] = r[k];
            eye[c] = (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0])) / det;
        }
    }
    double planes[5][4];
    for (unsigned k = 0; k < 4; ++k) planes[0][k] = row[3][k] + row[0][k];
    for (unsigned k = 0; k < 4; ++k) planes[1][k] = row[3][k] - row[0][k];
    for (unsigned k = 0; k < 4; ++k) planes[2][k] = row[3][k] + row[1][k];
    for (unsigned k = 0; k < 4; ++k) planes[3][k] = row[3][k] - row[1][k];
    for (unsigned k = 0; k < 4; ++k) planes[4][k] = row[3][k] - row[2][k];
    for (auto& p : planes) { const double l = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]); if (l > 0) for (double& v : p) v /= l; }

    // A still view: what decides the page demand (camera clip, level bias,
    // render size) is unchanged and no resident page was emptied, and the
    // feedback of every one of the 16 jitter positions, drawn in that state,
    // missed no page. Its pixels read nothing new, so the view test and the
    // feedback stop until that state changes. On leaving it every resident
    // page counts as read now, so the still frames do not make them all
    // evictable at once. Not with the check.
    const int check = std::clamp(configured_integer(config, "terrain_face_cache_check", 0), 0, 4);
    const float bias = configured_floating_point(config, "terrain_face_cache_bias", 0.f);
    if (native_world_clip_ != face_still_clip_ || face_page_generation_ != face_still_generation_ || bias != face_still_bias_ ||
        width_ != face_still_width_ || height_ != face_still_height_) {
        face_still_clip_ = native_world_clip_; face_still_generation_ = face_page_generation_; face_still_bias_ = bias;
        face_still_width_ = width_; face_still_height_ = height_; ++face_still_epoch_; face_still_cover_ = 0;
    }
    const bool still = face_still_cover_ == 0xffffu && check == 0;
    if (face_still_ && !still) for (auto& s : face_slots_) if (s.map != UINT32_MAX) s.used = native_frame_index_;
    face_still_ = still;

    // The patches in the frustum (every map's, at its origin): the feedback
    // pass draws their triangles; the check the active map's charts.
    std::vector<std::array<std::uint32_t, 2>> view_patches;
    std::vector<std::uint32_t> in_view;
    const TerrainFaceMap* active = nullptr;
    for (const auto& fm : face_maps_) {
        if (still) break;
        const auto& atlas = *fm.atlas;
        std::vector<std::uint8_t> chart_seen(fm.contact_map == 0 ? atlas.charts.size() : 0, 0);
        if (fm.contact_map == 0) active = &fm;
        for (const auto& c : atlas.patches) {
            const double r = c.radius + 2.0, x = c.centre[0] + fm.origin[0], y = c.centre[1] + fm.origin[1], z = c.centre[2] + fm.origin[2];
            bool visible = true;
            for (const auto& p : planes) if (p[0] * x + p[1] * y + p[2] * z + p[3] < -r) { visible = false; break; }
            if (!visible) continue;
            view_patches.push_back({fm.triangles + c.first, c.count});
            if (!chart_seen.empty() && !chart_seen[c.chart]) { chart_seen[c.chart] = 1; in_view.push_back(c.chart); }
        }
    }

    // Wants: the pages the latest finished feedback says pixels read. Resident
    // ones are marked used; the others are baked coarse levels first (a coarse
    // page covers the most pixels) within the per-frame budget.
    cpu_mark("face_setup");
    struct Want { std::uint32_t map, level, page; };
    std::vector<Want> wanted;
    std::uint32_t read_pages[kTerrainFaceCacheLevels]{};
    const bool fresh = read_terrain_face_feedback();
    if (fresh) {
        // Page table entry ranges (map, level), ascending.
        struct Range { std::uint32_t first, map, level; };
        std::vector<Range> ranges;
        for (std::uint32_t i = 0; i < face_maps_.size(); ++i)
            for (unsigned l = 0; l < kTerrainFaceCacheLevels; ++l) ranges.push_back({face_maps_[i].pages[l], i, l});
        // The set bits ascend, so the range holding each follows by a forward walk.
        std::size_t next = 0; // the first range starting past the current entry
        for (std::uint32_t w = 0; w < face_fb_words_; ++w) {
            for (std::uint32_t bits = face_fb_cpu_[w]; bits; bits &= bits - 1u) {
                unsigned long b = 0; _BitScanForward(&b, bits);
                const std::uint32_t e = w * 32u + std::uint32_t(b);
                while (next < ranges.size() && ranges[next].first <= e) ++next;
                if (next == 0) continue;
                const auto it = ranges.begin() + std::ptrdiff_t(next - 1);
                const auto& level = face_maps_[it->map].atlas->levels[it->level];
                const std::uint32_t page = e - it->first;
                if (page >= level.pages_x * level.pages_y) continue;
                ++read_pages[it->level];
                const std::uint32_t slot = face_page_slot_[e];
                if (slot) { face_slots_[slot - 1].used = native_frame_index_; continue; }
                if (std::binary_search(level.page_ids.begin(), level.page_ids.end(), page)) wanted.push_back({it->map, it->level, page});
            }
        }
    }
    const std::size_t wanted_all = wanted.size();
    if (fresh) face_still_cover_ = wanted_all == 0 ? face_still_cover_ | face_fb_read_cover_ : 0;
    std::stable_sort(wanted.begin(), wanted.end(), [](const Want& a, const Want& b) { return a.level > b.level; });
    const std::uint32_t budget = std::uint32_t(std::clamp(configured_integer(config, "terrain_face_cache_bake", 64), 1, 4096));
    if (wanted.size() > budget) wanted.resize(budget);
    // By map: one binding of its batch constants per map.
    std::stable_sort(wanted.begin(), wanted.end(), [](const Want& a, const Want& b) { return a.map < b.map; });

    // Slots: free ones first, then the least recently read that no feedback
    // of the last kRetain frames read (the jittered feedback sees a page's
    // pixels only on some frames). When every slot was read that recently the
    // pool grows, to twice its slices, and never shrinks in play; it goes at
    // the map change, and a map set's first pool is its previous visit's
    // peak (face_map_peaks_, by the active map; at least kMinSlices) or
    // kFirstSlices for one not seen yet, so a map entry makes it once
    // instead of climbing a slice at a time (#289 sizing audit: starting at 1 slice and growing
    // by a quarter, a 1 -> 8 climb created ~690 MiB and copied ~550, and a
    // shrink every 300 frames followed by regrowth made 7 reallocations in
    // 32 s of panning). Under budget pressure (level 1 and up) it does not
    // grow: the least recently read slot is reused even inside kRetain
    // (never one taken this frame), and a page that finds no slot this
    // frame is drawn per pixel.
    constexpr std::uint64_t kRetain = 24;
    constexpr std::uint32_t kFirstSlices = 6, kMinSlices = 4;
    const auto map_set=[]{const auto source=retained_static_terrain_source_snapshot();return source.map?source.map->map_resource:std::string{};};
    struct Job { std::uint32_t map, level, page, slot; };
    std::vector<Job> jobs;
    std::vector<std::array<std::uint32_t, 2>> table_writes; // table index, value
    const auto entry_of = [&](std::uint32_t map, std::uint32_t level, std::uint32_t page) { return face_maps_[map].pages[level] + page; };
    std::vector<std::uint32_t> evictable;
    std::size_t evict_next = 0;
    bool evictable_ready = false;
    for (const Want& w : wanted) {
        if (face_free_.empty()) {
            const bool full = vram_budget_.level >= 1 && face_slices_ > 0;
            if (!evictable_ready) {
                evictable_ready = true;
                for (std::uint32_t s = 0; s < face_slots_.size(); ++s)
                    if (face_slots_[s].map != UINT32_MAX && (full ? face_slots_[s].used < native_frame_index_ :
                        face_slots_[s].used + kRetain < native_frame_index_)) evictable.push_back(s);
                std::sort(evictable.begin(), evictable.end(), [&](std::uint32_t a, std::uint32_t b) { return face_slots_[a].used < face_slots_[b].used; });
            }
            if (full && evict_next >= evictable.size()) break; // every slot was taken this frame: the rest stay per pixel
            if (evict_next < evictable.size()) {
                const std::uint32_t s = evictable[evict_next++];
                auto& slot = face_slots_[s];
                const std::uint32_t e = entry_of(slot.map, slot.level, slot.page);
                face_page_slot_[e] = 0; table_writes.push_back({e, 0u}); ++face_page_generation_;
                slot = TerrainFaceSlot{}; face_free_.push_back(s);
            } else {
                const std::uint32_t slices = face_slices_;
                std::uint32_t target = slices * 2u;
                if (!slices) {
                    const auto peak = face_map_peaks_.find(map_set());
                    target = peak != face_map_peaks_.end() ? (std::max)(kMinSlices, peak->second) : kFirstSlices;
                }
                if (!grow_terrain_face_pool(target)) {
                    face_failed_ = true; release_terrain_face_cache();
                    char text[128]; std::snprintf(text, sizeof(text), "page pool could not grow past %u slices; faces stay per pixel", slices);
                    bridge_append_probe_event("terrain_face_cache_omitted", text); bind_terrain_face_cache(); return;
                }
                try { auto& peak = face_map_peaks_[map_set()]; peak = (std::max)(peak, face_slices_); } catch (...) {}
                {   // One line a pool size (at most a few a map now): the growth is visible in every log.
                    char text[96]; std::snprintf(text, sizeof(text), "slices=%u mb=%.0f maps=%zu", face_slices_,
                        double(face_slices_) * kTerrainFaceCacheSlice * kTerrainFaceCacheSlice * 5 / 1048576.0, face_maps_.size());
                    bridge_append_probe_event("terrain_face_cache_pool", text);
                }
            }
        }
        const std::uint32_t s = face_free_.back(); face_free_.pop_back();
        face_slots_[s] = {w.map, w.level, w.page, native_frame_index_};
        jobs.push_back({w.map, w.level, w.page, s});
    }
    cpu_mark("face_decode");
    // Demand, now and then: the pages the last feedback read per level, the
    // ones still missing, the resident pages and the pool.
    if (fresh && native_frame_index_ >= face_fb_log_) {
        face_fb_log_ = native_frame_index_ + 600;
        std::uint32_t resident = 0, resident_neighbours = 0;
        for (const auto& s : face_slots_) if (s.map != UINT32_MAX) { ++resident; resident_neighbours += face_maps_[s.map].contact_map != 0; }
        char text[352]; int n = std::snprintf(text, sizeof(text), "frame=%llu read", static_cast<unsigned long long>(native_frame_index_));
        for (unsigned l = 0; l < kTerrainFaceCacheLevels && n > 0 && n < int(sizeof(text)); ++l) n += std::snprintf(text + n, sizeof(text) - n, " l%u=%u", l, read_pages[l]);
        if (n > 0 && n < int(sizeof(text)))
            std::snprintf(text + n, sizeof(text) - n, " missing=%zu resident=%u neighbour_resident=%u maps=%zu slots=%zu slices=%zu mb=%.0f baked=%llu", wanted_all, resident,
                          resident_neighbours, face_maps_.size(), face_slots_.size(), size_t(face_slices_),
                          double(face_slices_) * kTerrainFaceCacheSlice * kTerrainFaceCacheSlice * 5 / 1048576.0, static_cast<unsigned long long>(face_fill_pages_));
        bridge_append_probe_event("terrain_face_cache_demand", text);
    }

    // Header: mode, bias.
    {
        const std::array<float, 4> header{float(mode), bias, 0.f, 0.f};
        if (header != face_header_) {
            face_header_ = header;
            if (d3d12_) (void)d3d12_->write_buffer(*face_data12_, 0, header.data(), 16);
            else { const D3D11_BOX box{0, 0, 0, 16, 1, 1}; context_->UpdateSubresource(face_data_.Get(), 0, &box, header.data(), 0, 0); }
        }
    }
    // The batch constants and contact views a map's bake binds, as for its
    // terrain batches (bake_terrain2_cache_rect): false when its neighbour is gone.
    struct MapBinding { Constants constants{}; ID3D11ShaderResourceView* views[4]{}; const D3D12Texture* views12[4]{}; };
    const auto map_binding = [&](const TerrainFaceMap& fm, MapBinding& b) -> bool {
        identity(b.constants.world); identity(b.constants.view); identity(b.constants.projection);
        b.constants.textured = 1; b.constants.contact_origin[3] = 1;
        std::uint64_t ready = 0;
        if (fm.contact_map == 0) {
            const bool art = (d3d12_ ? contact_maps_.tiles12 && contact_maps_.atlas12 : contact_maps_.tiles && contact_maps_.atlas) &&
                             contact_map_generation_ == ambient_map_generation_ && contact_constants_generation_ == contact_map_generation_;
            const bool ground = gc_map_generation_ == ambient_map_generation_ &&
                                (d3d12_ ? gc12_.map.ground && gc12_.map.generation == ambient_map_generation_ : bool(ambient_ground_srv_));
            if (ground) { b.views[0] = ambient_ground_srv_.Get(); b.views[1] = gc_color_srv_.Get(); b.views12[0] = gc12_.map.ground.get(); b.views12[1] = gc12_.map.color.get(); }
            if (art) { b.views[2] = contact_maps_.tiles.Get(); b.views[3] = contact_maps_.atlas.Get(); b.views12[2] = contact_maps_.tiles12.get(); b.views12[3] = contact_maps_.atlas12.get(); ready = contact_maps_.ready; }
            b.constants.layout_origin[0] = terrain_layout_origin_[0]; b.constants.layout_origin[1] = terrain_layout_origin_[1];
        } else {
            const GcNeighbour* found = nullptr;
            for (const auto& n : gc_neighbours_) if (n.contact_map == fm.contact_map) { found = &n; break; }
            if (!found) return false;
            const auto& n = *found;
            if (n.prepared) { b.views[0] = n.ground.Get(); b.views[1] = n.color.Get(); }
            if (n.cover12.color_ready) { b.views12[0] = n.cover12.ground.get(); b.views12[1] = n.cover12.color.get(); }
            b.views[2] = n.contact.tiles.Get(); b.views[3] = n.contact.atlas.Get(); b.views12[2] = n.contact.tiles12.get(); b.views12[3] = n.contact.atlas12.get();
            ready = n.contact.ready;
            for (unsigned a = 0; a < 3; ++a) b.constants.contact_origin[a] = n.origin[a];
            b.constants.contact_origin[3] = 0;
            b.constants.layout_origin[0] = n.layout_origin[0]; b.constants.layout_origin[1] = n.layout_origin[1];
        }
        b.constants.layout_origin[3] = float(fm.entry + 1);
        b.constants.contact_materials[0] = static_cast<UINT>(ready); b.constants.contact_materials[1] = static_cast<UINT>(ready >> 32);
        return true;
    };
    // Bake: per page one draw of its triangles into its slot; then the check (terrain_face_cache_check).
    const bool run_check = check > 0 && !face_check_pending_ && native_frame_index_ >= face_check_next_ && !d3d12_ && face_slices_ > 0 && active;
    const bool feedback = !view_patches.empty() && !face_fb_issued_[face_fb_next_];
    // The last occlusion pyramid when one was made (after its frame's opaque
    // terrain; native_hiz_clip_ its camera; native_hiz_frame_ is cleared at
    // each world composite, so it cannot say which frame): feedback and check.
    // A stale pyramid (a cut) hides faces for a frame; they then compute per pixel.
    const bool hiz = native_occlusion_enabled_ && native_hiz_ && !native_hiz_mip_srv_.empty() &&
                     std::any_of(native_hiz_clip_.begin(), native_hiz_clip_.end(), [](float v) { return v != 0.f; });
    const float eye_hiz[4]{float(eye[0]), float(eye[1]), float(eye[2]), hiz ? 1.f : 0.f};
    // The bake jobs' triangles (t106, ids with the map's table entry), each page's run.
    face_jobs_cpu_.clear();
    struct Draw { std::uint32_t first, count; const Job* job; };
    std::vector<Draw> draws;
    for (const Job& j : jobs) {
        const auto& fm = face_maps_[j.map];
        const auto& level = fm.atlas->levels[j.level];
        const auto it = std::lower_bound(level.page_ids.begin(), level.page_ids.end(), j.page);
        if (it == level.page_ids.end() || *it != j.page) continue;
        const std::size_t at = std::size_t(it - level.page_ids.begin());
        const std::uint32_t first = std::uint32_t(face_jobs_cpu_.size()), tag = std::uint32_t(fm.entry) << kTerrainFaceCacheMapShift;
        for (std::uint32_t t = level.page_first[at]; t < level.page_first[at + 1]; ++t) face_jobs_cpu_.push_back(tag | level.page_triangles[t]);
        draws.push_back({first, std::uint32_t(face_jobs_cpu_.size()) - first, &j});
    }
    // The feedback's constants (b13): clip, pyramid clip, scale, pyramid on, jitter, eye.
    const UINT fw = (std::max)(1u, (UINT(width_) + 3u) / 4u), fh = (std::max)(1u, (UINT(height_) + 3u) / 4u);
    unsigned fb_jitter = 0; // the jitter position of the feedback recorded this frame
    const auto feedback_values = [&](float values[40], bool pyramid) {
        // Jitter: the 4 x 4 positions inside a feedback pixel over 16 frames.
        static constexpr unsigned kOrder[16]{0, 10, 2, 8, 5, 15, 7, 13, 1, 11, 3, 9, 4, 14, 6, 12};
        const unsigned j = kOrder[face_fb_frames_ % 16];
        fb_jitter = j;
        for (unsigned k = 0; k < 40; ++k) values[k] = 0.f;
        for (unsigned k = 0; k < 16; ++k) { values[k] = float(native_world_clip_[k]); values[16 + k] = float(native_hiz_clip_[k]); }
        values[32] = float(fw) / float((std::max)(1u, UINT(width_))); values[33] = pyramid ? 1.f : 0.f;
        values[34] = ((float(j % 4) + .5f) / 4.f - .5f) * 2.f / float(fw); values[35] = -((float(j / 4) + .5f) / 4.f - .5f) * 2.f / float(fh);
        values[36] = float(eye[0]); values[37] = float(eye[1]); values[38] = float(eye[2]);
    };
    if (d3d12_) {
        // D3D12: the bake by compute (nativeFaceCacheCS, a dispatch a page), the
        // feedback as on D3D11 (draws into its own depth target, the bits
        // read back by ticket); no check. S_FALSE (a pipeline still compiling):
        // that work waits, its pixels compute per pixel.
        const bool pyramid12 = native_occlusion_enabled_ && native_hiz12_ &&
                               std::any_of(native_hiz_clip_.begin(), native_hiz_clip_.end(), [](float v) { return v != 0.f; });
        if (!draws.empty() || feedback) {
            transition_profile_pass("terrain_face_cache");
            profiler_.sub_scope("bake");
            bool ready = true, compiling = false; // compiling: a pipeline is still being built (its first use)
            if (face_jobs_cpu_.size() > face_jobs_capacity_) {
                face_jobs_capacity_ = std::uint32_t(face_jobs_cpu_.size() * 3 / 2 + 1024);
                face_jobs12_.reset();
                if (FAILED(d3d12_->create_buffer(std::uint64_t(face_jobs_capacity_) * 4, 4, 0, face_jobs12_))) {
                    face_jobs_capacity_ = 0; face_failed_ = true; release_terrain_face_cache(); end_profile_pass();
                    bridge_append_probe_event("terrain_face_cache_omitted", "bake job buffer could not be created (D3D12); faces stay per pixel");
                    return;
                }
            }
            if (!face_jobs_cpu_.empty()) (void)d3d12_->write_buffer(*face_jobs12_, 0, face_jobs_cpu_.data(), face_jobs_cpu_.size() * 4);
            D3D11_SAMPLER_DESC contact{}; contact.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            contact.AddressU = contact.AddressV = contact.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; contact.MaxLOD = D3D11_FLOAT32_MAX;
            D3D11_SAMPLER_DESC noise{}; noise.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            noise.AddressU = noise.AddressV = noise.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; noise.MaxLOD = 0;
            MapBinding binding; std::uint32_t bound = UINT32_MAX; bool bound_ok = false;
            for (const Draw& d : draws) {
                const Job& j = *d.job;
                if (j.map != bound) { bound = j.map; binding = MapBinding{}; bound_ok = map_binding(face_maps_[j.map], binding); }
                if (!bound_ok) continue; // its neighbour went this frame: the slot returns below
                const std::uint32_t slice = j.slot / kTerrainFaceCacheSlotsPerSlice, r = j.slot % kTerrainFaceCacheSlotsPerSlice;
                const auto& level = face_maps_[j.map].atlas->levels[j.level];
                struct { int offset[4]; float texel[4]; int target[4]; } values{
                    {int(d.first), int(j.level), int(j.page % level.pages_x), int(j.page / level.pages_x)}, {},
                    {int((r % kTerrainFaceCacheRow) * kTerrainFaceCacheSlot), int((r / kTerrainFaceCacheRow) * kTerrainFaceCacheSlot), int(slice), int(d.count)}};
                d3d12_->clear_slots();
                d3d12_->set_constants(0, &binding.constants, sizeof(binding.constants)); d3d12_->set_constants(12, &values, sizeof(values));
                d3d12_->set_resource(kContactGroundSlotD3D12, binding.views12[0]); d3d12_->set_resource(kContactColorSlotD3D12, binding.views12[1]);
                d3d12_->set_resource(15, binding.views12[2]); d3d12_->set_resource(16, binding.views12[3]);
                d3d12_->set_resource(102, face_grid12_.get()); d3d12_->set_resource(103, face_cells12_.get()); d3d12_->set_resource(104, face_data12_.get());
                d3d12_->set_resource(106, face_jobs12_.get()); d3d12_->set_resource(107, face_depth12_.get());
                d3d12_->set_resource(127, native12_.terrain_frame.get());d3d12_->set_resource(72,fire_burn_mask12());if(fire_burn_bound_)d3d12_->set_resource(74,fire_burn_materials12());
                d3d12_->set_resource(PointShadowTextureLayout::terrain_noise_srv, native12_.terrain_noise.get());
                d3d12_->set_sampler(8, contact); d3d12_->set_sampler(9, contact); d3d12_->set_sampler(12, noise);
                d3d12_->set_unordered(0, face_color12_.get()); d3d12_->set_unordered(1, face_height12_.get());
                const HRESULT hr = d3d12_->dispatch(face_programs12_[0], (UINT(kTerrainFaceCacheSlot) + 7u) / 8u, (UINT(kTerrainFaceCacheSlot) + 7u) / 8u, 1);
                if (hr != S_OK) {
                    compiling |= hr == S_FALSE;
                    ready = false; break;
                }
                const std::uint32_t e = entry_of(j.map, j.level, j.page);
                face_page_slot_[e] = j.slot + 1;
                table_writes.push_back({e, j.slot + 1}); ++face_fill_pages_;
            }
            d3d12_->clear_slots();
            cpu_mark("face_bake");
            if (feedback && ready) {
                profiler_.sub_scope("feedback");
                UINT have_w = 0, have_h = 0, have_mips = 0; DXGI_FORMAT have_format{};
                if (!face_fb_depth12_ || !d3d12_->texture_info(*face_fb_depth12_, have_w, have_h, have_mips, have_format) || have_w != fw || have_h != fh) {
                    face_fb_depth12_.reset();
                    if (FAILED(d3d12_->create_target(fw, fh, face_fb_depth12_, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_D32_FLOAT))) face_fb_depth12_.reset();
                }
                if (face_fb_depth12_) {
                    float values[40]; feedback_values(values, pyramid12);
                    Constants constants{}; identity(constants.world); identity(constants.view); identity(constants.projection);
                    (void)d3d12_->write_buffer(*face_fb_patches12_, 0, view_patches.data(), view_patches.size() * 8);
                    // The marks start as the slim pass's page reads since the
                    // last feedback (faceSlimBits), which then start again: one
                    // readback carries both, no CPU merge. A still view draws no
                    // feedback, so they wait (their pages were read and baked
                    // before it was still).
                    if (FAILED(d3d12_->copy_buffer(*face_fb_bits12_, *face_slim_bits12_, std::uint64_t(face_fb_words_) * 4)))
                        d3d12_->clear_buffer_uint(*face_fb_bits12_, 0);
                    d3d12_->clear_buffer_uint(*face_slim_bits12_, 0);
                    cpu_mark("face_fb_upload");
                    d3d12_->set_target(face_fb_depth12_.get());
                    d3d12_->clear_target(nullptr, true);
                    { const RECT whole{0, 0, LONG(fw), LONG(fh)}; d3d12_->set_viewport(&whole); } // the target's size, set explicitly
                    d3d12_->set_constants(0, &constants, sizeof(constants)); d3d12_->set_constants(13, values, sizeof(values));
                    d3d12_->set_resource(102, face_grid12_.get()); d3d12_->set_resource(103, face_cells12_.get()); d3d12_->set_resource(104, face_data12_.get());
                    d3d12_->set_resource(108, pyramid12 ? native_hiz12_.get() : nullptr);
                    d3d12_->set_resource(109, face_fb_patches12_.get()); d3d12_->set_resource(110, face_tris12_.get());
                    D3D12NativeState state{};
                    state.raster.FillMode = D3D11_FILL_SOLID; state.raster.CullMode = D3D11_CULL_NONE; state.raster.DepthClipEnable = TRUE;
                    state.depth.DepthEnable = TRUE; state.depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; state.depth.DepthFunc = D3D11_COMPARISON_LESS;
                    const UINT vertices = UINT(view_patches.size() * 96);
                    // Depth first, then the marks of the nearest face per pixel.
                    HRESULT hr = d3d12_->draw(face_programs12_[1], ShaderProgram{}, state, nullptr, vertices);
                    if (hr == S_OK) {
                        state.depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; state.depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
                        d3d12_->set_unordered(1, face_fb_bits12_.get());
                        hr = d3d12_->draw(face_programs12_[1], face_programs12_[2], state, nullptr, vertices);
                    }
                    if (hr == S_OK) {
                        cpu_mark("face_fb_draw");
                        std::uint64_t ticket = 0;
                        if (SUCCEEDED(d3d12_->request_readback(*face_fb_bits12_, std::uint64_t(face_fb_words_) * 4, ticket, "terrain face cache feedback"))) {
                            face_fb_ticket12_[face_fb_next_] = ticket; face_fb_issued_[face_fb_next_] = native_frame_index_;
                            face_fb_jitter_[face_fb_next_] = std::uint8_t(fb_jitter); face_fb_epoch_[face_fb_next_] = face_still_epoch_;
                            face_fb_next_ = (face_fb_next_ + 1) % 3;
                        }
                    } else compiling |= hr == S_FALSE;
                    d3d12_->clear_slots();
                    if (world_d3d12_bound_ && world_d3d12_) bind_world_targets_d3d12(); else d3d12_->set_target(nullptr);
                    d3d12_->set_viewport(nullptr);
                }
            }
            end_profile_pass();
            // A pipeline built on first use (the pipeline cache warms it on the
            // next start): nothing is missing meanwhile, its faces shade per
            // pixel; one line when it starts waiting and one when it is ready.
            if (compiling && !face_wait12_++)
                bridge_append_probe_event("terrain_face_cache", "D3D12 bake/feedback pipeline compiling on first use; faces shade per pixel until it is ready");
            else if (!compiling && face_wait12_) {
                char text[96]; std::snprintf(text, sizeof(text), "D3D12 bake/feedback pipelines ready after %u frames", face_wait12_);
                bridge_append_probe_event("terrain_face_cache", text); face_wait12_ = 0;
            }
        }
        for (const Job& j : jobs) if (face_page_slot_[entry_of(j.map, j.level, j.page)] != j.slot + 1) { face_slots_[j.slot] = TerrainFaceSlot{}; face_free_.push_back(j.slot); }
        for (const auto& w : table_writes) (void)d3d12_->write_buffer(*face_pages12_, std::uint64_t(w[0]) * 4, &w[1], 4);
        cpu_mark("face_finish");
        face_on_ = face_slices_ > 0;
        return;
    }
    if (!jobs.empty() || run_check || feedback) {
        transition_profile_pass("terrain_face_cache");
        profiler_.sub_scope("bake");
        if (face_jobs_cpu_.size() > face_jobs_capacity_) {
            face_jobs_capacity_ = std::uint32_t(face_jobs_cpu_.size() * 3 / 2 + 1024);
            face_jobs_.Reset(); face_jobs_srv_.Reset();
            if (FAILED(structured(device_.Get(), 4, face_jobs_capacity_, nullptr, D3D11_USAGE_DEFAULT, face_jobs_, face_jobs_srv_))) {
                face_jobs_capacity_ = 0; face_failed_ = true; release_terrain_face_cache(); end_profile_pass();
                bridge_append_probe_event("terrain_face_cache_omitted", "bake job buffer could not be created; faces stay per pixel");
                bind_terrain_face_cache(); return;
            }
        }
        if (!face_jobs_cpu_.empty()) {
            const D3D11_BOX box{0, 0, 0, UINT(face_jobs_cpu_.size() * 4), 1, 1};
            context_->UpdateSubresource(face_jobs_.Get(), 0, &box, face_jobs_cpu_.data(), 0, 0);
        }
        BakeStateD3D11 state; save_bake_state(state);
        ComPtr<ID3D11Buffer> vs_b12; context_->VSGetConstantBuffers(12, 1, &vs_b12);
        ID3D11ShaderResourceView* none[8]{};
        context_->PSSetShaderResources(100, 8, none); // the targets
        context_->IASetInputLayout(nullptr); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(face_vs_.Get(), nullptr, 0); context_->PSSetShader(face_ps_.Get(), nullptr, 0);
        context_->HSSetShader(nullptr, nullptr, 0); context_->DSSetShader(nullptr, nullptr, 0); context_->GSSetShader(nullptr, nullptr, 0);
        context_->RSSetState(face_raster_.Get()); context_->OMSetBlendState(nullptr, nullptr, 0xffffffffu); context_->OMSetDepthStencilState(nullptr, 0);
        { ID3D11Buffer* b = face_bake_constants_.Get(); context_->PSSetConstantBuffers(12, 1, &b); context_->VSSetConstantBuffers(12, 1, &b); }
        ID3D11ShaderResourceView* tables[6]{face_grid_srv_.Get(), face_cells_srv_.Get(), face_data_srv_.Get(), nullptr, face_jobs_srv_.Get(), face_depth_srv_.Get()};
        context_->VSSetShaderResources(102, 6, tables); context_->PSSetShaderResources(102, 6, tables);
        context_->PSSetShaderResources(127, 1, native_terrain_frame_srv_.GetAddressOf());
        { ID3D11ShaderResourceView* noise = terrain_noise_srv_.Get(); ID3D11SamplerState* sampler = terrain_noise_sampler_.Get();
          context_->PSSetShaderResources(PointShadowTextureLayout::terrain_noise_srv, 1, &noise); context_->PSSetSamplers(12, 1, &sampler); }
        // A map's batch constants and contact views (bake_terrain2_cache_rect).
        const auto bind_map = [&](const TerrainFaceMap& fm) -> bool {
            MapBinding b;
            if (!map_binding(fm, b)) return false;
            context_->PSSetShaderResources(13, 4, b.views);
            (void)upload_draw_constants(&b.constants, sizeof(b.constants));
            return true;
        };
        std::uint32_t bound_slice = UINT32_MAX, bound_map = UINT32_MAX; bool bound_ok = false;
        for (const Draw& d : draws) {
            const Job& j = *d.job;
            if (j.map != bound_map) { bound_map = j.map; bound_ok = bind_map(face_maps_[j.map]); }
            if (!bound_ok) continue; // its neighbour went this frame: the slot returns below
            const std::uint32_t slice = j.slot / kTerrainFaceCacheSlotsPerSlice, r = j.slot % kTerrainFaceCacheSlotsPerSlice;
            if (slice != bound_slice) {
                bound_slice = slice;
                ID3D11RenderTargetView* targets[2]{face_color_rtvs_[slice].Get(), face_height_rtvs_[slice].Get()};
                context_->OMSetRenderTargets(2, targets, nullptr);
            }
            const auto& level = face_maps_[j.map].atlas->levels[j.level];
            struct { int offset[4]; float texel[4]; } values{{int(d.first), int(j.level), int(j.page % level.pages_x), int(j.page / level.pages_x)}, {}};
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context_->Map(face_bake_constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) break;
            std::memcpy(mapped.pData, &values, sizeof(values)); context_->Unmap(face_bake_constants_.Get(), 0);
            const D3D11_VIEWPORT viewport{float((r % kTerrainFaceCacheRow) * kTerrainFaceCacheSlot), float((r / kTerrainFaceCacheRow) * kTerrainFaceCacheSlot),
                                          float(kTerrainFaceCacheSlot), float(kTerrainFaceCacheSlot), 0, 1};
            context_->RSSetViewports(1, &viewport);
            context_->DrawInstanced(d.count * 3, 2, 0, 0);
            const std::uint32_t e = entry_of(j.map, j.level, j.page);
            face_page_slot_[e] = j.slot + 1;
            table_writes.push_back({e, j.slot + 1}); ++face_fill_pages_;
        }
        if (feedback) {
            profiler_.sub_scope("feedback");
            D3D11_TEXTURE2D_DESC have{};
            if (face_fb_depth_) face_fb_depth_->GetDesc(&have);
            if (!face_fb_depth_ || have.Width != fw || have.Height != fh) {
                face_fb_depth_.Reset(); face_fb_dsv_.Reset();
                D3D11_TEXTURE2D_DESC d{}; d.Width = fw; d.Height = fh; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_D32_FLOAT;
                d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
                if (FAILED(device_->CreateTexture2D(&d, nullptr, &face_fb_depth_)) || FAILED(device_->CreateDepthStencilView(face_fb_depth_.Get(), nullptr, &face_fb_dsv_)))
                    face_fb_depth_.Reset();
            }
            if (face_fb_dsv_) {
                const D3D11_BOX box{0, 0, 0, UINT(view_patches.size() * 8), 1, 1};
                context_->UpdateSubresource(face_fb_patches_.Get(), 0, &box, view_patches.data(), 0, 0);
                float values[40]; feedback_values(values, hiz);
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (SUCCEEDED(context_->Map(face_fb_constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                    std::memcpy(mapped.pData, values, sizeof(values)); context_->Unmap(face_fb_constants_.Get(), 0);
                    const UINT zero[4]{};
                    context_->ClearUnorderedAccessViewUint(face_fb_bits_uav_.Get(), zero);
                    context_->ClearDepthStencilView(face_fb_dsv_.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
                    ID3D11ShaderResourceView* lists[2]{face_fb_patches_srv_.Get(), face_tris_srv_.Get()};
                    context_->VSSetShaderResources(109, 2, lists);
                    ID3D11ShaderResourceView* pyramid = hiz ? native_hiz_mip_srv_[0].Get() : nullptr;
                    context_->PSSetShaderResources(108, 1, &pyramid);
                    { ID3D11Buffer* b = face_fb_constants_.Get(); context_->VSSetConstantBuffers(13, 1, &b); context_->PSSetConstantBuffers(13, 1, &b); }
                    const D3D11_VIEWPORT viewport{0, 0, float(fw), float(fh), 0, 1};
                    context_->RSSetViewports(1, &viewport);
                    const UINT vertices = UINT(view_patches.size() * 96);
                    // Depth first, then the marks of the nearest face per pixel.
                    context_->VSSetShader(face_fb_vs_.Get(), nullptr, 0); context_->PSSetShader(nullptr, nullptr, 0);
                    context_->OMSetRenderTargets(0, nullptr, face_fb_dsv_.Get()); context_->OMSetDepthStencilState(face_fb_depth_write_.Get(), 0);
                    context_->Draw(vertices, 0);
                    ID3D11UnorderedAccessView* uav = face_fb_bits_uav_.Get();
                    context_->PSSetShader(face_fb_ps_.Get(), nullptr, 0);
                    context_->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, face_fb_dsv_.Get(), 1, 1, &uav, nullptr);
                    context_->OMSetDepthStencilState(face_fb_depth_test_.Get(), 0);
                    context_->Draw(vertices, 0);
                    uav = nullptr;
                    context_->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 1, 1, &uav, nullptr);
                    ID3D11ShaderResourceView* clear3[3]{};
                    context_->VSSetShaderResources(108, 3, clear3); context_->PSSetShaderResources(108, 3, clear3);
                    { ID3D11Buffer* b = nullptr; context_->VSSetConstantBuffers(13, 1, &b); context_->PSSetConstantBuffers(13, 1, &b); }
                    context_->CopyResource(face_fb_staging_[face_fb_next_].Get(), face_fb_bits_.Get());
                    face_fb_issued_[face_fb_next_] = native_frame_index_;
                    face_fb_jitter_[face_fb_next_] = std::uint8_t(fb_jitter); face_fb_epoch_[face_fb_next_] = face_still_epoch_;
                    face_fb_next_ = (face_fb_next_ + 1) % 3;
                }
            }
        }
        if (run_check && bind_map(*active)) { run_terrain_face_check(in_view, check, eye_hiz); face_check_next_ = native_frame_index_ + 60; }
        context_->VSSetShaderResources(102, 6, none); context_->PSSetShaderResources(102, 6, none);
        { ID3D11Buffer* b = vs_b12.Get(); context_->VSSetConstantBuffers(12, 1, &b); }
        restore_bake_state(state);
        end_profile_pass();
    }
    // Slots of jobs that drew nothing (a page without triangles, none
    // expected, or a map gone this frame) return to the free list.
    for (const Job& j : jobs) if (face_page_slot_[entry_of(j.map, j.level, j.page)] != j.slot + 1) { face_slots_[j.slot] = TerrainFaceSlot{}; face_free_.push_back(j.slot); }
    for (const auto& w : table_writes) {
        const D3D11_BOX box{w[0] * 4, 0, 0, w[0] * 4 + 4, 1, 1};
        context_->UpdateSubresource(face_pages_.Get(), 0, &box, &w[1], 0, 0);
    }
    face_on_ = face_slices_ > 0;
    bind_terrain_face_cache();
} catch (...) {
    release_terrain_face_cache(); bind_terrain_face_cache();
}

#endif
}  // namespace owned
