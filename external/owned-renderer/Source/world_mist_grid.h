#pragma once
// c56 mist geometry shared by native and embedded frontends. No game reads.
#include "retained_static_scene.h"
#include "world_fog.h"
#include <cfloat>

namespace owned {
class WorldMistGridCache {
    std::shared_ptr<const WorldMistGrid> cached;
    std::weak_ptr<const TerrainCpuAsset> cached_terrain;
    RetainedTerrainSourceIdentity cached_source{};
    std::array<float,16> cached_transform{};
    std::uint64_t generation = 0;
public:
    void retire() noexcept { cached.reset(); cached_terrain.reset(); cached_source = {}; }
    std::shared_ptr<const WorldMistGrid> get(const RetainedTerrainSourceSnapshot& source,
        const std::array<float,16>& transform) noexcept {
        if (!source.terrain || !source.source_generation || !source.source_revision) {
            retire(); return {};
        }
        for (float value : transform) if (!std::isfinite(value)) { retire(); return {}; }
        if (cached && cached_terrain.lock() == source.terrain &&
            cached_source.source_generation == source.source_generation &&
            cached_source.source_revision == source.source_revision && cached_transform == transform)
            return cached;
    try {
        const auto& m = transform;
        auto world = [&](float x, float y, float z) {
            return std::array<float, 3>{m[0] * x + m[1] * y + m[2] * z + m[3],
                                        m[4] * x + m[5] * y + m[6] * z + m[7],
                                        m[8] * x + m[9] * y + m[10] * z + m[11]};
        };
        auto grid = std::make_shared<WorldMistGrid>();
        float lo[2]{FLT_MAX, FLT_MAX}, hi[2]{-FLT_MAX, -FLT_MAX};
        std::vector<float> heights;
        for (const auto& sector : source.terrain->sectors) {
            for (int corner = 0; corner < 4; ++corner) {
                const auto p = world(corner & 1 ? sector.bounds.maximum[0] : sector.bounds.minimum[0],
                                     corner & 2 ? sector.bounds.maximum[1] : sector.bounds.minimum[1],
                                     sector.average_z);
                lo[0] = (std::min)(lo[0], p[0]); lo[1] = (std::min)(lo[1], p[1]);
                hi[0] = (std::max)(hi[0], p[0]); hi[1] = (std::max)(hi[1], p[1]);
            }
            const float cx = .5f * (sector.bounds.minimum[0] + sector.bounds.maximum[0]);
            const float cy = .5f * (sector.bounds.minimum[1] + sector.bounds.maximum[1]);
            heights.push_back(world(cx, cy, sector.average_z)[2]);
        }
        if (heights.empty() || !(hi[0] > lo[0]) || !(hi[1] > lo[1])) return {};
        std::nth_element(heights.begin(), heights.begin() + heights.size() / 10, heights.end());
        grid->terrain_low_z = heights[heights.size() / 10];
        grid->origin[0] = lo[0]; grid->origin[1] = lo[1];
        // Sized by the map at the mist resolution; the only bound is the D3D11
        // texture dimension limit (16384 cells = 65 km at 4 m).
        const double cells_x = std::ceil((hi[0] - lo[0]) / grid->cell) + 1.0, cells_y = std::ceil((hi[1] - lo[1]) / grid->cell) + 1.0;
        if (cells_x > 16384.0 || cells_y > 16384.0) return {};
        grid->width = static_cast<std::uint32_t>(cells_x);
        grid->height = static_cast<std::uint32_t>(cells_y);
        std::vector<float> count(std::size_t(grid->width) * grid->height), zsum(count.size()), swamps(count.size());
        // Liquid tiles carry mist; self-illuminated liquids (lava) do not.
        // Swamp or open water by the tile's own type (terrain_swamp_tile),
        // the same class the liquid shader uses.
        std::vector<std::uint8_t> liquid(source.terrain->objects.size());
        for (std::size_t i = 0; i < liquid.size(); ++i) {
            const auto& o = source.terrain->objects[i];
            liquid[i] = terrain_liquid_object(o.type) && o.self_illumination <= .5f;
        }
        for (const auto& sector : source.terrain->sectors)
            for (const auto& tile : sector.tiles) {
                if (tile.object_id >= liquid.size() || !liquid[tile.object_id]) continue;
                const bool is_swamp = terrain_swamp_tile(*source.terrain, tile);
                for (const auto index : tile.vertex_indices) {
                    if (index >= sector.vertices.size()) continue;
                    const auto& v = sector.vertices[index];
                    const auto p = world(v.x, v.y, v.z);
                    const auto x = static_cast<std::uint32_t>((p[0] - lo[0]) / grid->cell);
                    const auto y = static_cast<std::uint32_t>((p[1] - lo[1]) / grid->cell);
                    if (x >= grid->width || y >= grid->height) continue;
                    count[std::size_t(y) * grid->width + x] += 1.f;
                    zsum[std::size_t(y) * grid->width + x] += p[2];
                    if (is_swamp) swamps[std::size_t(y) * grid->width + x] += 1.f;
                }
            }
        grid->texels.resize(count.size() * 4);
        for (std::size_t i = 0; i < count.size(); ++i) {
            // Swamp and open-water coverage, coverage-weighted surface height
            // and coverage (bilinear safe).
            if (count[i] <= 0.f) continue;
            const float swamp_share = swamps[i] / count[i];
            grid->texels[4 * i] = swamp_share;
            grid->texels[4 * i + 1] = 1.f - swamp_share;
            grid->texels[4 * i + 2] = zsum[i] / count[i];
            grid->texels[4 * i + 3] = 1.f;
        }
        // Soft swamp edges: a 5x5 binomial blur of all four (coverage-weighted)
        // channels, about 8 m at 4 m cells.
        {
            const std::size_t w = grid->width, h = grid->height;
            std::vector<float> pass(grid->texels.size());
            static constexpr float k[5] = {1.f / 16, 4.f / 16, 6.f / 16, 4.f / 16, 1.f / 16};
            for (int axis = 0; axis < 2; ++axis) {
                const auto& in = axis ? pass : grid->texels;
                auto& out = axis ? grid->texels : pass;
                std::vector<float> result(in.size());
                for (std::size_t y = 0; y < h; ++y)
                    for (std::size_t x = 0; x < w; ++x)
                        for (int t = -2; t <= 2; ++t) {
                            const std::ptrdiff_t sx = axis ? std::ptrdiff_t(x) : std::ptrdiff_t(x) + t;
                            const std::ptrdiff_t sy = axis ? std::ptrdiff_t(y) + t : std::ptrdiff_t(y);
                            if (sx < 0 || sy < 0 || sx >= std::ptrdiff_t(w) || sy >= std::ptrdiff_t(h)) continue;
                            for (int c = 0; c < 4; ++c)
                                result[(y * w + x) * 4 + c] += k[t + 2] * in[(std::size_t(sy) * w + std::size_t(sx)) * 4 + c];
                        }
                out = std::move(result);
            }
        }
        if (++generation == 0) ++generation;
        grid->generation = generation;
        cached = std::move(grid);
        cached_source = {source.source_generation, source.source_revision};
        cached_terrain = source.terrain;
        cached_transform = transform;
        return cached;
    } catch (...) {
        return {};
    }
    }
};
} // namespace owned
