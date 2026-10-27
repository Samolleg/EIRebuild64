#include "Cache.h"
#include "Build.h"
#include "../external/recastnavigation/Detour/Include/DetourNavMeshBuilder.h"
#include <cstring>
#include <stdexcept>

namespace Navigation {
dtStatus LayerCodec::compress(const unsigned char* src,int n,unsigned char* dst,int cap,int* size) {
    int in=0,out=0;
    while(in<n) {
        int run=1;while(in+run<n&&run<128&&src[in+run]==src[in])++run;
        if(run>=3) {if(out+2>cap)return DT_FAILURE|DT_BUFFER_TOO_SMALL;dst[out++]=static_cast<unsigned char>(127+run);dst[out++]=src[in];in+=run;}
        else {
            const int first=in;in+=run;
            while(in<n&&in-first<128) {
                run=1;while(in+run<n&&run<3&&src[in+run]==src[in])++run;
                if(run>=3)break;
                ++in;
            }
            int count=in-first;if(out+count+1>cap)return DT_FAILURE|DT_BUFFER_TOO_SMALL;
            dst[out++]=static_cast<unsigned char>(count-1);std::memcpy(dst+out,src+first,count);out+=count;
        }
    }
    *size=out;return DT_SUCCESS;
}
dtStatus LayerCodec::decompress(const unsigned char* src,int n,unsigned char* dst,int cap,int* size) {
    int in=0,out=0;
    while(in<n) {
        const unsigned packet=src[in++];const int count=(packet&127)+1;
        if(count>cap-out)return DT_FAILURE|DT_BUFFER_TOO_SMALL;
        if(packet&128) {if(in==n)return DT_FAILURE;std::memset(dst+out,src[in++],count);}
        else {if(count>n-in)return DT_FAILURE;std::memcpy(dst+out,src+in,count);in+=count;}
        out+=count;
    }
    if(out!=cap)return DT_FAILURE;
    *size=out;return DT_SUCCESS;
}
void LayerProcess::process(dtNavMeshCreateParams* params,unsigned char* areas,unsigned short* flags) {
    for(int i=0;i<params->polyCount;++i) {
        const unsigned encoded=areas[i];
        if(!encoded||encoded>60){areas[i]=0;flags[i]=0;continue;}
        flags[i]=static_cast<unsigned short>((1u<<((encoded-1)/15+1))-1);
        areas[i]=static_cast<unsigned char>((encoded-1)%15+1);
    }
}
Cache::Cache(Mesh& mesh):data(std::make_unique<dtTileCache>()) {
    dtTileCacheParams p{};const auto& profile=mesh.profile;
    std::memcpy(p.orig,mesh.data->getParams()->orig,sizeof(p.orig));
    p.cs=profile.cellSize;p.ch=profile.cellHeight;p.width=p.height=profile.tileCells;
    p.walkableHeight=profile.height;p.walkableRadius=profile.radius;p.walkableClimb=profile.climb;
    p.maxSimplificationError=1;p.maxTiles=mesh.data->getParams()->maxTiles;p.maxObstacles=2048;
    if(dtStatusFailed(data->init(&p,&allocator,&codec,&process)))throw std::runtime_error("Nav64: cannot initialize TileCache");
    for(const auto& layer:mesh.layers) {
        if(layer.size()<sizeof(dtTileCacheLayerHeader))throw std::runtime_error("Nav64: truncated cache layer");
        dtTileCacheLayerHeader h;std::memcpy(&h,layer.data(),sizeof(h));
        if(h.magic!=DT_TILECACHE_MAGIC||h.version!=DT_TILECACHE_VERSION||!h.width||!h.height||h.width>profile.tileCells||h.height>profile.tileCells||h.tlayer<0||h.tlayer>255||h.hmin>h.hmax)
            throw std::runtime_error("Nav64: invalid cache layer header");
        auto* bytes=static_cast<unsigned char*>(dtAlloc(layer.size(),DT_ALLOC_PERM));
        if(!bytes)throw std::runtime_error("Nav64: cannot allocate cache layer");
        std::memcpy(bytes,layer.data(),layer.size());
        if(dtStatusFailed(data->addTile(bytes,int(layer.size()),DT_COMPRESSEDTILE_FREE_DATA,nullptr))) {dtFree(bytes);throw std::runtime_error("Nav64: invalid/duplicate cache layer");}
    }
}
bool Cache::Update(Mesh& mesh) {
    bool ready=false;
    if(dtStatusFailed(data->update(0,mesh.data.get(),&ready)))throw std::runtime_error("Nav64: TileCache update failed");
    ++mesh.generation;
    return ready;
}
}
