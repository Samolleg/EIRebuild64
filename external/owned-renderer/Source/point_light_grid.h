#pragma once

// World-space light grid for the forward point-light loop (issue #31).
//
// Every world pixel shader used to visit every point light of the frame. The
// game's lamps live on a map seen from above, so the frame's lights are binned
// into a 2D grid over world X/Y and each pixel visits only the lights listed
// for the cell that contains its world position. The grid is view-independent
// (reflections, portraits, fog froxels and the main camera share it) and is
// rebuilt only when a light's centre or radius changes, not when colours fade
// or flicker.
//
// Exactness: a light contributes to a pixel only when the pixel lies inside
// the light's sphere, so inside its X/Y disc. A cell lists every light whose
// disc, slightly widened for float rounding, touches the cell, in ascending
// light order: the per-pixel sum is the same sum in the same order as before.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace owned {

struct PointLightGrid {
    float origin[2] = {0, 0};
    float inverse_cell = 0;
    std::uint32_t width = 0, height = 0;
    // (width*height+1) cell starts, then the light indices; a cell's lights
    // are data[data[c]] .. data[data[c+1]-1]. Empty when there are no lights.
    std::vector<std::uint32_t> data;
    std::uint32_t max_per_cell = 0;
};

// The cell the shaders select for a world position (HLSL mirrors this float
// math in nativePointLightRange). Returns false outside the grid.
inline bool point_light_grid_cell(const PointLightGrid& grid, float x, float y, std::uint32_t& cell) noexcept {
    if (grid.width == 0) return false;
    const float cx = std::floor((x - grid.origin[0]) * grid.inverse_cell);
    const float cy = std::floor((y - grid.origin[1]) * grid.inverse_cell);
    if (!(cx >= 0 && cy >= 0 && cx < float(grid.width) && cy < float(grid.height))) return false;
    cell = std::uint32_t(cy) * grid.width + std::uint32_t(cx);
    return true;
}

// lights[i] = {centre x, y, z, 1/radius^2} exactly as uploaded to the shaders.
// Returns false only on invalid input (the caller already validated it).
inline bool build_point_light_grid(const std::vector<std::array<float, 4>>& lights, PointLightGrid& grid) {
    grid = PointLightGrid{};
    if (lights.empty()) return true;
    if (lights.size() >= std::numeric_limits<std::uint32_t>::max() / 2) return false;
    std::vector<double> radius(lights.size());
    double lo[2] = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    double hi[2] = {-lo[0], -lo[1]};
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const auto& l = lights[i];
        if (!(l[3] > 0) || !std::isfinite(l[0]) || !std::isfinite(l[1]) || !std::isfinite(l[3])) return false;
        radius[i] = 1.0 / std::sqrt(double(l[3]));
        for (int a = 0; a < 2; ++a) {
            lo[a] = std::min(lo[a], double(l[a]) - radius[i]);
            hi[a] = std::max(hi[a], double(l[a]) + radius[i]);
        }
    }
    // Cell edge: half the median light radius, so a lamp spans about 4x4 cells
    // and a pixel tests few lights beyond its own. The grid side stays within
    // 256 cells (256 KB table) by coarsening cells, never by dropping lights.
    std::vector<double> sorted = radius;
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    const double extent = std::max(hi[0] - lo[0], hi[1] - lo[1]);
    double cell = std::max({sorted[sorted.size() / 2] * .5, extent / 256.0, 1e-3});
    // Widening that covers float rounding of the shader's distance test and
    // cell selection (a few ulps of the coordinates); it only adds candidates.
    const double margin_scale = 1.001;
    const double margin = cell * 1e-3 + 4e-6 * std::max({std::abs(lo[0]), std::abs(lo[1]), std::abs(hi[0]), std::abs(hi[1]), 1.0});
    const double origin[2] = {lo[0] - 2 * margin - cell * .5, lo[1] - 2 * margin - cell * .5};
    const std::uint32_t width = std::uint32_t(std::ceil((hi[0] - origin[0] + 2 * margin) / cell)) + 1;
    const std::uint32_t height = std::uint32_t(std::ceil((hi[1] - origin[1] + 2 * margin) / cell)) + 1;
    grid.origin[0] = float(origin[0]);
    grid.origin[1] = float(origin[1]);
    grid.inverse_cell = float(1.0 / cell);
    grid.width = width;
    grid.height = height;
    // Recompute the cell geometry from the float values the shader will use.
    const double inv = double(grid.inverse_cell), ox = double(grid.origin[0]), oy = double(grid.origin[1]);
    const std::size_t cells = std::size_t(width) * height;
    std::vector<std::uint32_t> counts(cells + 1, 0);
    auto visit = [&](std::size_t i, auto&& emit) {
        const double r = radius[i] * margin_scale + margin;
        const double x = lights[i][0], y = lights[i][1];
        const auto first = [&](double v, double o, std::uint32_t n) {
            return std::uint32_t(std::clamp(std::floor((v - o) * inv), 0.0, double(n - 1)));
        };
        const std::uint32_t x0 = first(x - r, ox, width), x1 = first(x + r, ox, width);
        const std::uint32_t y0 = first(y - r, oy, height), y1 = first(y + r, oy, height);
        for (std::uint32_t cy = y0; cy <= y1; ++cy)
            for (std::uint32_t cx = x0; cx <= x1; ++cx) {
                // Distance from the light centre to the cell rectangle.
                const double ax = ox + cx / inv, bx = ox + (cx + 1) / inv;
                const double ay = oy + cy / inv, by = oy + (cy + 1) / inv;
                const double dx = std::max({ax - x, 0.0, x - bx}), dy = std::max({ay - y, 0.0, y - by});
                if (dx * dx + dy * dy <= r * r) emit(std::size_t(cy) * width + cx);
            }
    };
    for (std::size_t i = 0; i < lights.size(); ++i) visit(i, [&](std::size_t c) { ++counts[c]; });
    std::uint64_t total = 0;
    for (std::size_t c = 0; c < cells; ++c) {
        grid.max_per_cell = std::max(grid.max_per_cell, counts[c]);
        total += counts[c];
    }
    if (cells + 1 + total >= std::numeric_limits<std::uint32_t>::max()) return false;
    grid.data.assign(cells + 1 + std::size_t(total), 0);
    std::uint32_t cursor = std::uint32_t(cells + 1);
    for (std::size_t c = 0; c < cells; ++c) {
        grid.data[c] = cursor;
        cursor += counts[c];
        counts[c] = grid.data[c];
    }
    grid.data[cells] = cursor;
    // Ascending light order within every cell keeps the shaders' summation order.
    for (std::size_t i = 0; i < lights.size(); ++i)
        visit(i, [&](std::size_t c) { grid.data[counts[c]++] = std::uint32_t(i); });
    return true;
}

}  // namespace owned
