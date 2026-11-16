#pragma once
// Terrain 2.0 face cache atlas (terrain2_cache.h kTerrainFaceCacheShader,
// renderer_terrain_face_cache.cpp): the active map's steep LOD0 base
// triangles grouped into near-planar charts, and per cache level the
// charts packed into a virtual atlas of 64 x 64 texel pages. Built once per
// map off the render thread; immutable afterwards.
#include "terrain2_cache.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace owned {

struct TerrainCpuAsset;

// The per-vertex values a map's atlas is built from (the only terrain data
// it needs): small enough to keep with a neighbour while its atlas is not
// wanted, so the atlas can be built whenever the face cache turns on.
struct TerrainFaceGrid {
    std::uint32_t grid_x = 0, grid_y = 0;          // base vertices per axis (cells: one less)
    // Per base vertex (t102): x, y, z (map-local rest position), rise (the
    // wall-foot band's owned_terrain_foot_rise), the packed slope normal and
    // facet flatness as raw bits, the signed rest bed depth, the bed's liquid
    // object as raw bits (UINT16_MAX: none).
    std::vector<std::array<float, 8>> grid;
    std::vector<std::uint8_t> flags;               // per vertex: 1 steep, 2 liquid bed, 4 present
};

struct TerrainFaceAtlas {
    std::uint32_t grid_x = 0, grid_y = 0;          // base vertices per axis (cells: one less)
    std::shared_ptr<const TerrainFaceGrid> source; // its vertex grid (shared with the neighbour that keeps it)
    const std::vector<std::array<float, 8>>& grid() const { return source->grid; }
    // Per base cell (t103): chart of triangle (a, b, d) | chart of (a, d, c) << 16; 0xffff none.
    std::vector<std::uint32_t> cells;
    struct Chart {
        float t[3]{}, u0 = 0, b[3]{}, v0 = 0;      // chart metres = ((x.t - u0) su, (x.b - v0) sv)
        float su = 1, sv = 1, size_u = 0, size_v = 0;
        float centre[3]{}, radius = 0;             // bounding sphere (map-local)
        std::uint32_t first = 0, count = 0;        // its triangles in `triangles`
    };
    std::vector<Chart> charts;
    std::vector<std::uint32_t> triangles;          // cell * 2 + (0: (a, b, d), 1: (a, d, c)), by chart
    // Request units: runs of at most kTerrainFacePatch neighbouring triangles of
    // one chart (its growth order), each with its own bounding sphere, so a
    // large chart wants a fine level only near the camera.
    struct Patch { float centre[3]{}, radius = 0; std::uint32_t chart = 0, first = 0, count = 0; };
    std::vector<Patch> patches;
    struct Level {
        std::uint32_t pages_x = 0, pages_y = 0, offset = 0; // page table: its first entry in t105
        std::vector<std::array<float, 2>> origin;  // per chart: atlas texel of chart metres (0, 0) less the gutter
        std::vector<std::uint32_t> page_ids, page_first, page_triangles; // sorted pages: the triangles their bake draws
    };
    std::array<Level, kTerrainFaceCacheLevels> levels;
    double face_area = 0;                          // surface of the charted triangles (m^2)
    std::uint32_t steep_triangles = 0, bed_triangles = 0;
    std::string summary;                           // one log line: charts, pages per level, allocated / ideal
};

// The vertex grid of `terrain`. False with a reason when it has no sectors.
bool build_terrain_face_grid(const TerrainCpuAsset& terrain, TerrainFaceGrid& grid, std::string& failure);
// Builds the atlas of a map from its grid. False with a reason when it has
// no steep faces, or "cancelled" once *cancel is set.
bool build_terrain_face_atlas(std::shared_ptr<const TerrainFaceGrid> grid, TerrainFaceAtlas& atlas, std::string& failure,
                              const std::atomic<bool>* cancel = nullptr);
// Both, for the active map.
bool build_terrain_face_atlas(const TerrainCpuAsset& terrain, TerrainFaceAtlas& atlas, std::string& failure);

// An atlas building on a worker (std::async): its owner takes the result
// when ready. Destroying the job cancels the build and waits for the
// worker, so no build outlives the renderer state that started it.
struct TerrainFaceAtlasJob {
    std::atomic<bool> cancel{false};
    std::future<std::shared_ptr<const TerrainFaceAtlas>> result;
    TerrainFaceAtlasJob() = default;
    TerrainFaceAtlasJob(const TerrainFaceAtlasJob&) = delete;
    TerrainFaceAtlasJob& operator=(const TerrainFaceAtlasJob&) = delete;
    ~TerrainFaceAtlasJob() { cancel.store(true, std::memory_order_relaxed); if (result.valid()) result.wait(); }
    bool ready() const { return result.valid() && result.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
};
// Starts the atlas of `grid`, or of `terrain` when grid is null (`name` for
// the log); null when the worker could not start.
std::shared_ptr<TerrainFaceAtlasJob> start_terrain_face_atlas_job(std::shared_ptr<const TerrainFaceGrid> grid,
    std::shared_ptr<const TerrainCpuAsset> terrain, std::string name) noexcept;

} // namespace owned
