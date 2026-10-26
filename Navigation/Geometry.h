#pragma once
#include "Types.h"
#include <array>

namespace Navigation {
struct Triangle { Point a,b,c; std::uint8_t area = 1; };
struct Solid { std::array<Point,8> corners; };
struct Geometry {
    std::vector<Triangle> ground;
    std::vector<Solid> solids;
    Point minimum, maximum;
    void Bounds();
};
// Convex XZ footprint of an actual transformed solid BV, not its world AABB.
std::vector<float> Footprint(const Solid& solid, float& low, float& high);
}
