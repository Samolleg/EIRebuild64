#pragma once
// Emissive surfaces as soft point lights (there is no GI). Pure CPU code: no
// D3D, no engine state.
//
// What is emissive (shipped data): an MPR liquid object (type 2/3) with a
// positive self-illumination scalar; its emission is colour * self_illumination,
// the value the terrain vertices already carry (engine_bridge terrain emissive,
// OpenEI TerrainImporter). Every shipped/Dejavu map that has one uses it for
// lava (0.796,0,0 x 1.0). No base terrain tile is lava-typed. Figure materials
// ("magic") are emissive too but are small parts already handled by bloom.
//
// The renderer collects the wet emissive layer tiles of the active map (the
// ambient terrain-grid rule: the liquid lies over the ground at one of the
// tile's vertices), groups them into square cells of `cell_tiles` x `cell_tiles`
// 2 m tiles and publishes one light per occupied cell, appended to the game's
// point lights so the existing light grid, culling and shaders handle it.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace owned::emissive {

struct Tile {
    std::uint32_t x = 0, y = 0;          // 2 m tile coordinates
    float z = 0;                         // liquid surface height at the tile centre
    std::array<float, 3> emission{};     // colour * self_illumination
};

struct Cluster {
    std::uint32_t cell = 0;              // cy * cells_x + cx
    std::uint32_t tiles = 0;
    std::array<float, 3> centre{};       // world units (tile centre = 2*t+1)
    float coverage = 0;                  // tiles / cell_tiles^2
    std::array<float, 3> emission{};     // mean emission of the member tiles
};

// One cluster per occupied cell, in ascending cell order; every tile belongs to
// exactly the cell that contains it. Deterministic for a given tile set
// regardless of tile order.
inline std::vector<Cluster> cluster_tiles(const std::vector<Tile>& tiles, std::uint32_t tiles_x,
                                          std::uint32_t cell_tiles) {
    std::vector<Cluster> out;
    if (tiles.empty() || !tiles_x || !cell_tiles) return out;
    const std::uint32_t cells_x = (tiles_x + cell_tiles - 1) / cell_tiles;
    std::vector<std::pair<std::uint32_t, std::size_t>> order;
    order.reserve(tiles.size());
    for (std::size_t i = 0; i < tiles.size(); ++i)
        order.emplace_back((tiles[i].y / cell_tiles) * cells_x + tiles[i].x / cell_tiles, i);
    // Sorting by (cell, tile index in the map) fixes the summation order too.
    std::sort(order.begin(), order.end(), [&](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        const Tile& ta = tiles[a.second]; const Tile& tb = tiles[b.second];
        return ta.y != tb.y ? ta.y < tb.y : ta.x < tb.x;
    });
    const float area = float(cell_tiles) * float(cell_tiles);
    for (std::size_t i = 0; i < order.size();) {
        Cluster c{}; c.cell = order[i].first;
        double sum[3] = {0, 0, 0}, emission[3] = {0, 0, 0};
        for (; i < order.size() && order[i].first == c.cell; ++i) {
            const Tile& t = tiles[order[i].second];
            sum[0] += 2.0 * t.x + 1.0; sum[1] += 2.0 * t.y + 1.0; sum[2] += t.z;
            for (int a = 0; a < 3; ++a) emission[a] += t.emission[a];
            ++c.tiles;
        }
        for (int a = 0; a < 3; ++a) {
            c.centre[a] = float(sum[a] / c.tiles);
            c.emission[a] = float(emission[a] / c.tiles);
        }
        c.coverage = std::min(float(c.tiles) / area, 1.0f);
        out.push_back(c);
    }
    return out;
}

// Light shape of a cluster. Radius and lift do not depend on the cell size, so
// the finer level only localises the same glow; intensity scales with the
// emissive area a light stands for (coverage * cell area), so the summed glow
// of a lava field is about the same at either level and grows smoothly with
// the field instead of peaking at single lights.
struct LightShape {
    std::array<float, 3> centre{};
    float radius = 0;
    std::array<float, 3> color{};        // before strength and flicker
};
constexpr float kLift = 1.5f;            // above the surface
constexpr float kRadius = 9.0f, kRadiusCoverage = 4.0f;
constexpr float kReferenceCellArea = 64.0f; // 8 m x 8 m

// CPU mirror of the liquid shader's liquidMoltenColor (liquid_surface_shader.h)
// without its 4% lit-crust term: the colour a lava pixel shows at a heat.
inline std::array<float, 3> molten_color(const std::array<float, 3>& emission, float heat) noexcept {
    const float peak = std::max({emission[0], emission[1], emission[2], 1e-3f});
    const auto lerp3 = [](const std::array<float, 3>& a, const std::array<float, 3>& b, float t) {
        return std::array<float, 3>{a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
    };
    std::array<float, 3> hue{emission[0] / peak, emission[1] / peak, emission[2] / peak};
    const auto crust = lerp3({.07f, .01f, .004f}, {hue[0] * .07f, hue[1] * .07f, hue[2] * .07f}, .2f);
    const auto red = lerp3({.62f, .06f, .02f}, {hue[0] * .62f, hue[1] * .62f, hue[2] * .62f}, .2f);
    const std::array<float, 3> orange{.95f, .30f, .04f}, yellow{1.f, .82f, .30f};
    if (heat < .30f) return lerp3(crust, red, heat / .30f);
    if (heat < .78f) return lerp3(red, orange, (heat - .30f) / .48f);
    return lerp3(orange, yellow, std::clamp((heat - .78f) / .17f, 0.f, 1.f));
}

// Mean rendered lava colour for an emission: the molten ramp averaged over
// heat/brightness samples taken from the liquid shader's own detail map
// (renderer_liquid.cpp; {heat, brightness} per sample). Without samples, the
// ramp's typical heat.
inline std::array<float, 3> rendered_color(const std::array<float, 3>& emission,
                                           const std::vector<std::array<float, 2>>& samples) noexcept {
    if (samples.empty()) { auto c = molten_color(emission, .45f); for (float& v : c) v *= .97f; return c; }
    double sum[3] = {0, 0, 0};
    for (const auto& s : samples) {
        const auto c = molten_color(emission, s[0]);
        for (int a = 0; a < 3; ++a) sum[a] += double(c[a]) * s[1];
    }
    return {float(sum[0] / samples.size()), float(sum[1] / samples.size()), float(sum[2] / samples.size())};
}

// `color`: the cluster's rendered lava colour (rendered_color).
inline LightShape light_shape(const Cluster& c, std::uint32_t cell_tiles, const std::array<float, 3>& color) noexcept {
    LightShape s;
    s.centre = {c.centre[0], c.centre[1], c.centre[2] + kLift};
    s.radius = kRadius + kRadiusCoverage * std::sqrt(c.coverage);
    const float cell_area = 4.0f * float(cell_tiles) * float(cell_tiles);
    const float weight = std::pow(c.coverage, .75f) * (cell_area / kReferenceCellArea);
    for (int a = 0; a < 3; ++a) s.color[a] = color[a] * weight;
    return s;
}

// Gentle slow pulse on game seconds (the liquid shader's clock), with a phase
// per cell so neighbouring lights never pulse together.
inline float flicker(std::uint32_t cell, float seconds) noexcept {
    std::uint32_t h = cell * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    const float p0 = float(h & 1023u) * (6.2831853f / 1024.0f), p1 = float((h >> 10) & 1023u) * (6.2831853f / 1024.0f);
    return 1.0f + .08f * std::sin(seconds * .6f + p0) + .04f * std::sin(seconds * 1.7f + p1);
}

} // namespace owned::emissive
