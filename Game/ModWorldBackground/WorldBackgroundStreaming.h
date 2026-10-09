#pragma once
#include "WorldBackgroundData.h"
#include <map>

#pragma pack(push,8)
namespace WorldBackground {
struct SectorKey {
    size_t neighbor=0,sector=0;
    bool operator<(const SectorKey& b)const{return neighbor<b.neighbor||(neighbor==b.neighbor&&sector<b.sector);}
    bool operator==(const SectorKey& b)const{return neighbor==b.neighbor&&sector==b.sector;}
};
constexpr uint32_t BatchVertexLimit=65520; // whole 24-vertex tiles, below API65535
constexpr uint64_t LegacyPayloadBudget=64ull*1024*1024;
constexpr uint64_t MaxPayloadBudget=256ull*1024*1024;
struct PatchCost { uint32_t tag=0,atlas=0,vertices=0; uint64_t textureBytes=0; };
struct SectorCost { uint64_t bytes=0; uint32_t meshes=0,vertices=0; std::vector<PatchCost> patches; };
// Opaque patches share a batch per neighbor/atlas; alpha remains per sector.
class BatchCost {
    std::map<std::pair<size_t,uint32_t>,uint32_t> landVertices_;
public:
    uint64_t bytes=0,vertices=0; uint32_t meshes=0;
    void Add(size_t neighbor,const SectorCost&);
};
// Matches API2's expanded triangle storage and separate texture copy per mesh.
// One patch per sector/layer/material/atlas. No index array is submitted.
SectorCost MeasureSector(const Prepared& neighbor,size_t sector);
struct SectorView {
    Point anticipated;bool anticipate=false; // bounded optional look-ahead, current view wins
    Point camera; // active-local XY from the authoritative camera, not layout XY
    double radius=96, prefetch=32, retention=16;
    bool useFrustum=false;
    std::array<float,16> worldToClip={}; // API2 row-major/column-vector
    double verticalMargin=0; // supply verified water deformation envelope
};
struct SectorSelection {
    uint64_t epoch=0,layoutRevision=0,request=0;
    std::vector<SectorKey> sectors,added,removed;
    uint64_t bytes=0;
    uint32_t meshes=0,budgetSkipped=0,visibleSkipped=0,invalidSkipped=0;
};
class SectorStreaming {
public:
    // Pure CPU scheduling. Same selected keys -> same request, preventing worker
    // starvation during camera motion. Epoch/revision changes always invalidate.
    const SectorSelection& Update(const Snapshot&,uint64_t epoch,uint64_t layoutRevision,
        bool enabled,const SectorView&,uint64_t budget=64u*1024u*1024u,uint32_t meshLimit=256,uint64_t staticInputBytes=0,bool lighting=false,uint64_t futureBudget=MaxPayloadBudget);
    // A worker's completion must match all three values before publication.
    bool Accepts(uint64_t epoch,uint64_t layoutRevision,uint64_t request)const;
    void Clear();
private:
    SectorSelection selection_;
    uint64_t sequence_=0;
    bool enabled_=false;
};
struct SectorApplyResult { uint32_t loaded=0,removed=0,loadFailures=0,removeFailures=0; };
class SectorResidency {
public:
    // Synchronous render-thread calls, no disk/worker waits. publish consumes
    // already prepared data and must roll back its own partial patch allocation
    // on false. remove(false) retains ownership, so further publication stops.
    using Operation=std::function<bool(const SectorKey&)>;
    SectorApplyResult Apply(const SectorSelection&,const Operation& publish,const Operation& remove);
    const std::vector<SectorKey>& Resident()const{return resident_;}
private:
    uint64_t epoch_=0,layoutRevision_=0,request_=0;
    std::vector<SectorKey> resident_;
};
}
#pragma pack(pop)
