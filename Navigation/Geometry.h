#pragma once
#include "Types.h"
#include <array>
#include <map>

namespace Navigation {
struct Triangle { Point a,b,c; std::uint8_t area = 1; };
struct Solid { std::array<Point,8> corners; std::string error; };
struct Geometry {
    std::vector<Triangle> ground;
    std::map<std::size_t,std::string> groundSources; // First triangle of each source sector, diagnostics only.
    std::vector<Solid> solids;
    Point minimum, maximum;
    void Bounds();
};
// Convex XZ footprint of an actual transformed solid BV, not its world AABB.
std::vector<float> Footprint(const Solid& solid, float& low, float& high);
}
