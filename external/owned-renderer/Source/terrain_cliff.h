#pragma once

namespace owned {

// Terrain cliffs (terrain_cliffs) and cliff tessellation (terrain_tessellation).
// Render-only: gameplay, collision and placement keep the game's heightfield.
//
// Walkability: the game's AI map marks a cell unwalkable for ground units when
// its height differs from a neighbour 0.5 m away by more than a 40 degree rise
// (FP_AI_MAX_ANGLE_TO_MOVE = 40, FP_AI_MAX_ANGLE_TO_FLY = 60; both read from the
// supported game.exe). The renderer applies the same 40 degree limit to its
// per-vertex slope normal (terrain_slope_normal), which is position-continuous,
// instead of reading the server's AI map.
//
// Cliffs keep the map's own tile art: past the walk limit each pixel's tile
// (and, near a tile edge, its neighbour's) is re-read through world-space side
// projections, so the art stops stretching down the face. Colour, contrast
// and the authored transitions stay; only the detail's coordinates change.

// terrain_tessellation: 0 off, 1 on (up to 8x). A 16x level was removed: an
// edge needs span * bend over 2.4 m before 8 pieces are not enough, and then
// the 8-piece curve is already within ~5 mm of the Phong surface.
inline constexpr float terrain_tessellation_max_factor(int level) {
    return level >= 1 ? 8.f : 0.f;
}
// Largest offset of unwalkable slopes (m). Fixed: the Phong offset is about
// 0.13 * edge length * bend, under 0.3 m on most EI faces, so a 0.3-1.0 m
// slider changed only tall bent edges (user: barely visible end to end).
inline constexpr float terrain_tessellation_height = .7f;

// Shared verbatim by the production native shaders and the WARP regression.
inline constexpr char kTerrainCliffShader[] = "#if 0"
#include "shaders/terrain_cliff.hlsl"
"\n#endif\n";

} // namespace owned
