#pragma once
#include "Types.h"
#include <array>
#include <map>

namespace Navigation {
struct Triangle {
    Point a,b,c;
    std::uint8_t area = 1;
    // MPR water layer, per vertex, above the original ground. Objects use zero.
    Point waterDepth;
    std::uint8_t levitateArea = 1;
    std::array<std::uint8_t,8> classAreas = {1,1,1,1,1,1,1,1};
    bool terrain = false;
};
inline Triangle Surface(Triangle t,const Profile& p) {
    if(t.terrain)t.area=t.classAreas[p.materialClass];
    if(Levitates(p)) {t.a.z+=t.waterDepth.x;t.b.z+=t.waterDepth.y;t.c.z+=t.waterDepth.z;t.area=t.levitateArea;}
    return t;
}
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
