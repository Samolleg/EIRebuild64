#pragma once
#include "WorldBackgroundPixels.h"
#include "WorldBackgroundStreaming.h"
#include "WorldBackgroundSdk/world_background_api_v2.h"
#include "RenderTypes.h"
#include <memory>
#include "WorldBackgroundLightingPacket.h"
#include "WorldBackgroundLightingTransport.h"

#pragma pack(push, 8)
namespace WorldBackground {
struct TileUV { std::array<float,9> u,v; };
TileUV NativeTileUV(uint16_t packedTile,uint32_t logicalTextureSize);
struct VisualPatch {
    uint32_t atlas = 0, materialTag = 0;
    ET_Material2 material = {};
    std::vector<ET_Vertex> vertices;
    std::vector<ET_LightRef1> lightRefs;
    std::shared_ptr<const Pixels> pixels;
    uint64_t Bytes() const;
};
struct SectorVisual { SectorKey key; std::vector<VisualPatch> patches; uint64_t bytes = 0; };
// Producer supplies authoritative active visual state. No invented light/fog/time.
// Position input is LOCAL; translated active-local position is supplied separately.
// The provider may deform local XYZ for native visual motion, but must leave UV
// and material alpha coherent with its material policy. Throws fail the whole sector.
using VisualVertex = std::function<ET_Vertex(const Vertex&,Point,const Material*,uint8_t,float,float)>;
using PixelCache = std::map<uint32_t,std::shared_ptr<const Pixels>>;
SectorVisual BuildSectorVisual(const Snapshot&,SectorKey,const VisualVertex&,
    uint64_t budgetBytes = 64u*1024u*1024u,PixelCache* cache = nullptr);
struct MeshBatch { size_t neighbor=0; uint32_t part=0; VisualPatch patch; };
struct RenderSnapshot {
    NeighborSnapshot view = {};
    std::vector<SectorVisual> sectors;
    std::vector<MeshBatch> batches;
    std::vector<VisualPatch> decorationPatches;
    uint64_t bytes=0;
    const Snapshot* source=nullptr;uint64_t layoutRevision=0;
    std::vector<NeighborVisual> meshes;
    std::vector<std::string> diagnostics;
    std::vector<std::shared_ptr<const LightingPacket>> lightingPackets;
    std::shared_ptr<const LightingTransport> lightingTransport;
    RenderSnapshot() = default;
    RenderSnapshot(const RenderSnapshot&) = delete;
    RenderSnapshot& operator=(const RenderSnapshot&) = delete;
};
std::shared_ptr<RenderSnapshot> PrepareRenderSnapshot(const Snapshot&,const SectorSelection&,
    Point activeSavedOrigin,const VisualVertex&,const std::function<bool()>& cancelled = {},
    uint64_t budgetBytes = LegacyPayloadBudget,const RenderSnapshot* previous = nullptr,uint64_t visualRevision = 0);
}
#pragma pack(pop)
