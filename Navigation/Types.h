#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Navigation {
constexpr std::uint32_t ContainerVersion = 1;
constexpr std::uint32_t AlgorithmVersion = 1;
constexpr std::uint32_t LibraryVersion = 0x010600;
struct Point { float x = 0, y = 0, z = 0; };
inline bool Finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
inline float Distance(Point a, Point b) {
    return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));
}
// One shared, orientation-preserving conversion: game Z up -> Recast Y up.
inline Point ToNav(Point p) { return {p.x,p.z,-p.y}; }
inline Point FromNav(Point p) { return {p.x,-p.z,p.y}; }
struct Profile {
    float radius = .4f, height = 1.6f, slope = 40.f, climb = .35f;
    float cellSize = .1f, cellHeight = .05f;
    std::uint32_t tileCells = 256;
};
struct Location {
    Point position;
    std::uint64_t surface = 0; // 1 = imported MPR ground. BV tops are never floors.
    float radius = 0, height = 0;
    std::uint32_t area = 1; // derived material policy, re-resolved on load
    // Transient. Never serialized: rebuilding a tile invalidates Detour references.
    std::uint64_t polygon = 0, generation = 0;
};
enum class Status { Complete, NoMesh, Invalid, NoStart, NoGoal, NoPath, Limit, TooLong };
const char* StatusName(Status status);
struct Request {
    Point source, goal;
    float goalRadius = 0, maxLength = 1.e6f;
    float areaCosts[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
};
struct Route {
    Status status = Status::Invalid;
    std::vector<Location> points;
    std::vector<std::uint64_t> corridor;
    float length = 0;
};
}
