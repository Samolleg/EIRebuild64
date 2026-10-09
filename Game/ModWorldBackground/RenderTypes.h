#pragma once
#include "WorldBackgroundSdk/world_background_api_v2.h"
#include "WorldBackgroundSdk/world_background_lighting.h"
#include <array>
#pragma pack(push,8)
// Value-only geometry contract retained from the source-bound producer. No
// DDraw provider, registry, retail offset or callback is used by native DX11.
struct NeighborVisual {
    ET_Mesh2 mesh;
    uint64_t geometryRevision,textureRevision;
    float scrollU,scrollV;
    std::array<uint32_t,4> key;
    uint64_t lightMapKey=0;
    const ET_LightRef1* lightRefs=nullptr;
};
struct NeighborSnapshot {
    uint32_t mapEpoch,count;
    uint64_t topologyRevision;
    float activeWorldX,activeWorldY;
    const NeighborVisual* meshes;
    uint64_t contentRevision=0;
    const ET_LightSnapshot1* lighting=nullptr;
};
#pragma pack(pop)
