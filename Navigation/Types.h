#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace Navigation {
constexpr std::uint32_t ContainerVersion = 10;
constexpr std::uint32_t AlgorithmVersion = 30;
constexpr std::uint32_t LibraryVersion = 0x010600;
struct Point { float x = 0, y = 0, z = 0; };
inline bool Finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
inline float Distance(Point a, Point b) {
    return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));
}
// One shared, orientation-preserving conversion: game Z up -> Recast Y up.
inline Point ToNav(Point p) { return {p.x,p.z,-p.y}; }
inline Point FromNav(Point p) { return {p.x,-p.z,p.y}; }
enum class Movement : unsigned { Ground, Levitate, Flying };
struct Profile {
    float radius = .4f, height = 1.6f, slope = 40.f, climb = .35f;
    float cellSize = .1f, cellHeight = .05f;
    std::uint32_t tileCells = 96;
    unsigned materialClass = 3;
    Movement movement = Movement::Ground;
};
// Four nested clearance permissions, independent of material areas. A human
// uses one mesh in every pose; the query chooses one of these bits.
inline const std::array<float,4>& ClearanceHeights(const Profile& p) {
    static constexpr std::array<float,4> tiny={.2f,.4f,1.f,1.6f}, human={.4f,1.f,1.6f,2.2f},
        large={.4f,1.6f,2.2f,3.f}, giant={.4f,1.6f,3.f,6.f}, levitate={.2f,.4f,1.f,2.f};
    if(p.movement!=Movement::Ground)return levitate;
    return p.radius<.2f?tiny:p.radius<.5f?human:p.radius<1.f?large:giant;
}
inline bool PoseClearance(const Profile&) { return true; }
inline unsigned short ClearanceMask(const Profile& p,float height) {
    if(!std::isfinite(height)||height<=0)return 0;
    if(!PoseClearance(p))return height<=p.height?1:0;
    const auto& tiers=ClearanceHeights(p);
    for(unsigned i=0;i<4;++i)if(height<=tiers[i])return static_cast<unsigned short>(1u<<i);
    return 0;
}
inline bool Levitates(const Profile& p) { return p.movement==Movement::Levitate; }
inline bool Flies(const Profile& p) { return p.movement==Movement::Flying; }
struct Location {
    Point position;
    std::uint64_t surface = 0; // 1 = terrain/flight envelope; object supports retain their source ID.
    float radius = 0, height = 0;
    std::uint32_t area = 1; // derived material policy, re-resolved on load
    // Transient. Never serialized: rebuilding a tile invalidates Detour references.
    std::uint64_t polygon = 0, generation = 0;
};
enum class Status { Complete, NoMesh, Invalid, NoStart, NoGoal, NoPath, Limit, TooLong, Updating };
const char* StatusName(Status status);
// Physical standing-unit footprint, used only by one synchronous path query.
struct Obstacle { Point position; float radius=0,height=0; int id=0; };
// Synchronous straight queries never remove actors from the spatial index.
struct BodyPolicy {
    std::array<int,16> ignored{};unsigned count=0;
    bool Ignores(int id) const {for(unsigned i=0;i<count&&i<ignored.size();++i)if(ignored[i]==id)return true;return false;}
};
struct StraightRequest {
    Point source,direction;float distance=0,bodyRadius=0,boundaryInset=0;
    float left=28,right=100,bottom=26,top=62;BodyPolicy bodies;
};
struct StraightResult {Status status=Status::Invalid;Location endpoint;std::vector<Location> points;};
// Ground movement orders (player and scripts) can approach an unavailable
// point. Exact internal targets (attacks, placement, visibility-graph edges)
// keep their original contract.
enum class GoalPolicy { Exact, ClosestReachable };
enum class RandomShape { Circle, Square };
constexpr unsigned short ExcludeClass(unsigned aiClass) {
    return aiClass<8?static_cast<unsigned short>(1u<<(aiClass+4)):0x0ff0;
}
struct Route;
struct SlicedSearch;
struct Request {
    SlicedSearch* sliced = nullptr;
    double* frameBudgetMs = nullptr;
    // One small sliced batch per unit/frame may exceed the shared soft budget.
    bool ensureProgress = false;
    Point source, goal;
    GoalPolicy goalPolicy = GoalPolicy::Exact;
    const Location* start = nullptr; // transient source polygon; never serialized
    const std::vector<Obstacle>* obstacles = nullptr;
    const Route* reuseRoute = nullptr;
    const Location* goalSupport = nullptr;
    unsigned short excludedFlags = 0;
    float goalRadius = 0, maxLength = 1.e6f, height = 0;
    float areaCosts[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
};
struct Route {
    Status status = Status::Invalid;
    Status goalStatus = Status::Complete; // Original refusal when the end is adjusted.
    std::vector<Location> points;
    std::vector<std::uint64_t> corridor;
    float length = 0;
    float cost = 0, plannedCost = 0, straightCost = -1; // Transient query/journal metrics.
    bool straightened = false;
    std::size_t sampledTail=~std::size_t(0); // Transient: retained, already sampled game-route nodes.
};
}
