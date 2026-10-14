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
// Shared verbatim by the production native shader and its small GPU regression.
inline constexpr char kLiquidSurfaceShader[] = "#if 0"
#include "shaders/liquid_surface.hlsl"
"\n#endif\n";
} // namespace owned
