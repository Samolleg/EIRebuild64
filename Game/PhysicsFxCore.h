#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Presentation-only physics. No gameplay objects, RNG, renderer or save pointers.
namespace PhysicsFx {
struct Vec { float x=0,y=0,z=0; };
struct Triangle { Vec a,b,c; };
struct Pose { Vec position; std::array<float,4> rotation{0,0,0,1}; };
struct Stone { Pose pose; float radius=0,alpha=1; unsigned shape=0; std::uint32_t color=0xff8b887f; };
inline constexpr unsigned MaxStones=128;
inline constexpr float TickSeconds=.055f, StepSeconds=TickSeconds/4;
inline constexpr float PatchRadius=12.f;
struct Stats {
    unsigned bodies=0,active=0,patches=0,substeps=0; double stepMs=0; unsigned errors=0;
    bool gpuDynamics=false,gpuBroadPhase=false;
    std::uint64_t gpuHeapBytes=0,gpuBroadPhaseBytes=0,gpuSolverBytes=0;
    unsigned gpuContacts=0;
};
// The same six vertices describe each convex physics shape and render mesh.
std::array<Vec,6> StoneVertices(unsigned shape,float radius);
void InstallCharacterPhysics();
struct StoneTrack {
    std::array<Pose,5> poses;
    float radius, age, life;
    unsigned shape;
    std::uint32_t color;
};
using StoneTracks=std::vector<StoneTrack>;
std::vector<Stone> SampleTracks(const StoneTracks&,float fraction);
class World {
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::string failure;
    bool attempted=false;
public:
    World(); ~World();
    World(const World&)=delete;
    World& operator=(const World&)=delete;
    bool Initialize();
    const std::string& Error() const { return failure; }
    std::string Device() const;
    void Clear();
    bool HasPatch(std::uint64_t key) const;
    bool AddPatch(std::uint64_t key,Vec center,const std::vector<Triangle>& triangles);
    unsigned Burst(std::uint64_t key,Vec point,Vec normal,float power,std::uint32_t seed);
    void Tick(bool paused);
    std::vector<Stone> Sample(float fraction) const;
    StoneTracks Snapshot() const;
    Stats Statistics() const;
};
// Reject initial sync, stale events and duplicates without consuming combat RNG.
class Events {
    struct Entry { int id,start; };
    std::vector<Entry> seen;
    int readyAfter=0;
public:
    void Reset(int tick) { seen.clear();readyAfter=tick+2; }
    bool Admit(int id,int start,int now,bool ready);
};
}
