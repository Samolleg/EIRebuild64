#pragma once
#include "Types.h"
#include <array>
#include <map>
#include <set>

namespace Navigation {
struct Triangle {
    Point a,b,c;
    std::uint8_t area = 1;
    // MPR water layer, per vertex, above the original ground. Objects use zero.
    Point waterDepth;
    std::uint8_t levitateArea = 1;
    std::array<std::uint8_t,8> classAreas = {1,1,1,1,1,1,1,1};
    bool terrain = false;
    int object = 0;
    std::uint64_t surface = 1;
};
inline Triangle Surface(Triangle t,const Profile& p) {
    if(t.terrain)t.area=t.classAreas[p.materialClass];
    if(t.terrain&&Levitates(p)) {t.a.z+=t.waterDepth.x;t.b.z+=t.waterDepth.y;t.c.z+=t.waterDepth.z;t.area=t.levitateArea;}
    return t;
}
struct Solid { std::array<Point,8> corners; std::string error; };
struct Door { int object=0,openState=1; Point minimum,maximum; };
struct OffMesh { Point from,to; float radius=0; std::uint64_t fromSurface=1,toSurface=1; unsigned flags=0; };
// Bottom canopy envelope. Samples cover 0.3 m XY columns; 0xffff is empty.
// Heights use the same outward 5 cm quantization as the offline rasterizer.
struct CeilingGrid {
    int object=0,x=0,y=0;
    unsigned width=0,height=0;
    float base=0;
    std::vector<std::uint16_t> heights;
};
struct Geometry {
    std::vector<Triangle> ground;
    std::vector<CeilingGrid> ceilings;
    std::vector<Door> doors;
    std::vector<OffMesh> connections;
    std::map<std::size_t,std::string> groundSources; // First triangle of each source sector, diagnostics only.
    std::vector<Solid> solids;
    Point minimum, maximum;
    float tileWidth=0;
    // Offline spatial catalogues shared by all profiles; no runtime binning.
    std::map<std::pair<int,int>,std::vector<size_t>> tileTriangles,surfaceTriangles,tileCeilings;
    std::map<int,std::set<std::pair<int,int>>> objectTiles;
    void Bounds();
};
// Convex XZ footprint of an actual transformed solid BV, not its world AABB.
std::vector<float> Footprint(const Solid& solid, float& low, float& high);
}
