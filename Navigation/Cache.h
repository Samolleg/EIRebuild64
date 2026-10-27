#pragma once
#include "Types.h"
#include "../external/recastnavigation/DetourTileCache/Include/DetourTileCache.h"
#include "../external/recastnavigation/DetourTileCache/Include/DetourTileCacheBuilder.h"
#include <memory>

namespace Navigation {
struct Mesh;
// A small lossless packet codec; independent of host pointers and paths.
struct LayerCodec final : dtTileCacheCompressor {
    int maxCompressedSize(const int size) override { return size+size/128+2; }
    dtStatus compress(const unsigned char*,int,unsigned char*,int,int*) override;
    dtStatus decompress(const unsigned char*,int,unsigned char*,int,int*) override;
};
struct LayerProcess final : dtTileCacheMeshProcess {
    Mesh* owner=nullptr;
    void process(dtNavMeshCreateParams*,unsigned char*,unsigned short*) override;
};
struct Cache {
    dtTileCacheAlloc allocator;
    LayerCodec codec;
    LayerProcess process;
    std::unique_ptr<dtTileCache> data;
    explicit Cache(Mesh& mesh);
    bool Update(Mesh& mesh);
};
}
