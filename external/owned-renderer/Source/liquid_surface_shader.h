#pragma once

namespace owned {
// Decode-side liquid art controls. A liquid surface slope (tangent) between
// these values ramps a vertex's cascade foam source from 0 to 1; the source
// fades out over kLiquidCascadeReach world units around it (waterfall pools).
inline constexpr float kLiquidCascadeSlope[2] = {.3f,1.f};
inline constexpr float kLiquidCascadeReach = 6.f;
// A layer vertex more than this below its ground is buried and ignored.
inline constexpr float kLiquidBuriedTolerance = .05f;
// A liquid surface step this high across a 1 m grid edge is a fall, not a
// rapid (procedural_falls.h replaces the authored triangles of falls).
inline constexpr float kLiquidFallEdgeDrop = .9f;
// Level river reaches (owned_terrain_build_liquid_outflow). Authored rivers
// are mostly dead level between their cascades (Dejavu zone11-13: 3-6% of
// their water sloped), so their downstream comes from the liquid's layout:
// the surface slope a level reach runs as (its full current); the real slope
// of moving water (falls and rapids); the band above a water body's lowest
// level that is its bottom level; the distance to land of open water (lakes,
// sea); and the openness (mean free run over radials as a share of 24 m: a
// 10-20 m river ~.4-.6, open water 1) over which a channel fades to still.
inline constexpr float kLiquidOutflowSlope = .07f;
inline constexpr float kLiquidOutflowSource = .05f;
inline constexpr float kLiquidOutflowLevelBand = .15f;
inline constexpr float kLiquidOutflowWide = 12.f;
inline constexpr float kLiquidOutflowOpenness[2] = {.6f,.9f};
// A rise of the surface above this over its current level starts a new
// level (the reach above a step or fall lip).
inline constexpr float kLiquidOutflowStep = .1f;
// Water above a fall lip: how far upstream of the lip it churns (m) and the
// drop beside the lip that ramps a lip from none to a full fall (m).
inline constexpr float kLiquidLipReach = 9.f;
inline constexpr float kLiquidLipDrop[2] = {.6f,2.f};
// Shared verbatim by the production native shader and its small GPU regression.
inline constexpr char kLiquidSurfaceShader[] = "#if 0"
#include "shaders/liquid_surface.hlsl"
"\n#endif\n";
} // namespace owned
