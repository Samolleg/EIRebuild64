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
    // Detour compresses heights/areas/cons (3 grids), but provides capacity
    // for an additional scratch regions grid during decompression.
    if(cap%4||out!=cap/4*3)return DT_FAILURE;
    *size=out;return DT_SUCCESS;
}
void LayerProcess::process(dtNavMeshCreateParams* params,unsigned char* areas,unsigned short* flags) {
    for(int i=0;i<params->polyCount;++i) {
        const unsigned encoded=areas[i];
        if(!encoded||encoded>60){areas[i]=0;flags[i]=0;continue;}
        flags[i]=static_cast<unsigned short>((1u<<((encoded-1)/15+1))-1);
        areas[i]=static_cast<unsigned char>((encoded-1)%15+1);
        if(owner)owner->doorFlags[{params->tileX,params->tileY,params->tileLayer,i}]=flags[i];
        if(areas[i]==15&&owner&&owner->sources) {
            Point center{};int count=0;
            for(int k=0;k<params->nvp;++k) {
                const auto index=params->polys[i*params->nvp*2+k];if(index==0xffff)break;
                const auto* v=params->verts+index*3;
                center.x+=params->bmin[0]+v[0]*params->cs;center.y-=params->bmin[2]+v[2]*params->cs;center.z+=params->bmin[1]+v[1]*params->ch;++count;
            }
            if(count)for(const auto& door:owner->sources->doors) {
                auto state=owner->doorOpen.find(door.object);if(state==owner->doorOpen.end()||state->second)continue;
                const float x=center.x/count,y=center.y/count;
                if(x>=door.minimum.x-.01f&&x<=door.maximum.x+.01f&&y>=door.minimum.y-.01f&&y<=door.maximum.y+.01f&&center.z/count>=door.minimum.z-.01f&&center.z/count<=door.maximum.z+.01f)flags[i]=0;
            }
        }
    }
}
Cache::Cache(Mesh& mesh):data(std::make_unique<dtTileCache>()) {
    process.owner=&mesh;
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
