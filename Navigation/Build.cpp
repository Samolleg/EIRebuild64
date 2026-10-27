#include "Build.h"
#include "../external/recastnavigation/Recast/Include/Recast.h"
#include "../external/recastnavigation/Detour/Include/DetourNavMeshBuilder.h"
#include "../external/recastnavigation/Detour/Include/DetourCommon.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <limits>
#include <atomic>
#include <thread>
#include <chrono>
#include <exception>
#include <future>
#include <map>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <tuple>
#include <cstdio>
#define NOMINMAX
#include <windows.h>
#include <compressapi.h>
#pragma comment(lib,"cabinet.lib")

namespace Navigation {
namespace {
void Require(bool ok,const char* what) { if(!ok)throw std::runtime_error(what); }
struct Wire {
    Bytes bytes; size_t at=0;
    void U(std::uint32_t n){for(int i=0;i<4;++i)bytes.push_back(std::uint8_t(n>>(8*i)));}
    void F(float v){std::uint32_t n;std::memcpy(&n,&v,4);U(n);}
    void P(Point v){F(v.x);F(v.y);F(v.z);}
    void B(const void* p,size_t n){auto b=static_cast<const std::uint8_t*>(p);if(n)bytes.insert(bytes.end(),b,b+n);}
    void S(const std::string& s){U(std::uint32_t(s.size()));B(s.data(),s.size());}
    std::uint32_t U(){Require(at<=bytes.size() && bytes.size()-at>=4,"Nav64: truncated field");std::uint32_t n=0;for(int i=0;i<4;++i)n|=std::uint32_t(bytes[at++])<<(8*i);return n;}
    float F(){auto n=U();float v;std::memcpy(&v,&n,4);Require(std::isfinite(v),"Nav64: invalid float");return v;}
    std::string S(){auto n=U();Require(n<=4096 && n<=bytes.size()-at,"Nav64: invalid text");std::string s(reinterpret_cast<const char*>(bytes.data()+at),n);at+=n;return s;}
    void ProfileOut(const Profile& p){F(p.radius);F(p.height);F(p.slope);F(p.climb);F(p.cellSize);F(p.cellHeight);U(p.tileCells);U(p.materialClass);}
    Profile ProfileIn(){Profile p;p.radius=F();p.height=F();p.slope=F();p.climb=F();p.cellSize=F();p.cellHeight=F();p.tileCells=U();p.materialClass=U();Require(p.materialClass<8,"Nav64: invalid material class");return p;}
};
// Windows' built-in lossless LZMS codec needs no shipped DLL or
// third-party source. The entire tile section shares one compression stream.
struct Codec {
    COMPRESSOR_HANDLE handle=nullptr;
    bool encoding;
    explicit Codec(bool encode):encoding(encode) {
        Require(encode?CreateCompressor(COMPRESS_ALGORITHM_LZMS,nullptr,&handle)!=FALSE:
            CreateDecompressor(COMPRESS_ALGORITHM_LZMS,nullptr,&handle)!=FALSE,"Nav64: compression unavailable");
    }
    ~Codec(){if(encoding)CloseCompressor(handle);else CloseDecompressor(handle);}
};
Bytes Pack(const Bytes& input) {
    Codec codec(true);SIZE_T size=0;
    Require(!Compress(codec.handle,input.data(),input.size(),nullptr,0,&size)&&GetLastError()==ERROR_INSUFFICIENT_BUFFER&&
        size>0&&size<=512ull*1024*1024,"Nav64: compressed size unavailable");
    Bytes output(size);
    Require(Compress(codec.handle,input.data(),input.size(),output.data(),output.size(),&size)!=FALSE,"Nav64: compression failed");
    output.resize(size);return output;
}
Bytes Unpack(const void* input,size_t size,size_t expected) {
    Require(expected>=4&&expected<=512ull*1024*1024,"Nav64: expanded section limit");
    Codec codec(false);SIZE_T actual=0;Bytes output(expected);
    Require(Decompress(codec.handle,input,size,output.data(),output.size(),&actual)!=FALSE&&actual==expected,"Nav64: corrupt compressed tiles");
    return output;
}
void CheckProfile(const Profile& p) {
    Require(std::isfinite(p.radius)&&p.radius>=0&&p.radius<=4 && std::isfinite(p.height)&&p.height>.1f&&p.height<=10 &&
        p.cellSize==.1f && p.cellHeight==.05f && (p.tileCells==32||p.tileCells==64||p.tileCells==96||p.tileCells==128) && (p.slope==40||p.slope==60) && p.climb==.35f,"Nav64: unsupported build profile");
}
// v1.6.0 addTile does not take bounds for its internal sections. Validate all
// counts and the exact aligned library layout BEFORE handing it cached bytes.
dtMeshHeader CheckTileHeader(const unsigned char* data,size_t size) {
    Require(size>=sizeof(dtMeshHeader)&&size<=16*1024*1024,"Nav64: invalid tile size");
    dtMeshHeader h;std::memcpy(&h,data,sizeof(h));
    Require(h.magic==DT_NAVMESH_MAGIC && h.version==DT_NAVMESH_VERSION && h.layer>=0&&h.layer<256 &&
        h.polyCount>0&&h.polyCount<=32768 && h.vertCount>0&&h.vertCount<65536 && h.maxLinkCount>0&&h.maxLinkCount<1000000 &&
        h.detailMeshCount==h.polyCount&&h.detailVertCount>=0&&h.detailVertCount<1000000&&h.detailTriCount>=0&&h.detailTriCount<1000000&&
        h.bvNodeCount>=0&&h.bvNodeCount<=h.polyCount*2 && h.offMeshConCount==0&&h.offMeshBase==h.polyCount,"Nav64: invalid tile header");
    size_t needed=dtAlign4(sizeof(dtMeshHeader))+dtAlign4(sizeof(float)*3*h.vertCount)+dtAlign4(sizeof(dtPoly)*h.polyCount)+
        dtAlign4(sizeof(dtLink)*h.maxLinkCount)+dtAlign4(sizeof(dtPolyDetail)*h.detailMeshCount)+dtAlign4(sizeof(float)*3*h.detailVertCount)+
        dtAlign4(sizeof(unsigned char)*4*h.detailTriCount)+dtAlign4(sizeof(dtBVNode)*h.bvNodeCount);
    Require(needed==size,"Nav64: mismatched tile sections");
    Require(std::isfinite(h.walkableHeight)&&h.walkableHeight>0&&std::isfinite(h.walkableRadius)&&h.walkableRadius>=0&&
        std::isfinite(h.walkableClimb)&&h.walkableClimb>=0&&std::isfinite(h.bvQuantFactor)&&h.bvQuantFactor>0,"Nav64: invalid tile metrics");
    for(int i=0;i<3;++i)Require(std::isfinite(h.bmin[i])&&std::isfinite(h.bmax[i])&&h.bmin[i]<=h.bmax[i],"Nav64: invalid tile bounds");
    return h;
}
// Byte lanes of each fixed-size array record are contiguous on disk. This is
// reversible (including float bits); no vertex, height or polygon is quantized.
// The header is unchanged so the decoder can bound every array before unshuffle.
Bytes ShuffleTile(const unsigned char* data,size_t size,bool encode) {
    const auto h=CheckTileHeader(data,size);Bytes result(size);
    size_t offset=dtAlign4(sizeof(dtMeshHeader));std::memcpy(result.data(),data,offset);
    auto array=[&](size_t count,size_t width) {
        for(size_t lane=0;lane<width;++lane)for(size_t i=0;i<count;++i) {
            const size_t plain=i*width+lane,packed=lane*count+i;
            result[offset+(encode?packed:plain)]=data[offset+(encode?plain:packed)];
        }
        offset+=dtAlign4(int(count*width));
    };
    array(h.vertCount,sizeof(float)*3);array(h.polyCount,sizeof(dtPoly));
    array(h.maxLinkCount,sizeof(dtLink));array(h.detailMeshCount,sizeof(dtPolyDetail));
    array(h.detailVertCount,sizeof(float)*3);array(h.detailTriCount,4);
    array(h.bvNodeCount,sizeof(dtBVNode));
    Require(offset==size,"Nav64: shuffled tile layout mismatch");
    return result;
}
void CheckTile(const unsigned char* data,size_t size) {
    const auto h=CheckTileHeader(data,size);
    const auto* vertices=reinterpret_cast<const float*>(data+dtAlign4(sizeof(dtMeshHeader)));
    for(int i=0;i<h.vertCount*3;++i)Require(std::isfinite(vertices[i]),"Nav64: invalid tile vertex");
    size_t offset=dtAlign4(sizeof(dtMeshHeader))+dtAlign4(sizeof(float)*3*h.vertCount);
    const auto* polys=reinterpret_cast<const dtPoly*>(data+offset);
    for(int i=0;i<h.polyCount;++i) {
        Require(polys[i].vertCount>=3 && polys[i].vertCount<=DT_VERTS_PER_POLYGON && polys[i].getType()==DT_POLYTYPE_GROUND&&
            polys[i].getArea()>0&&polys[i].getArea()<16&&polys[i].flags>0&&polys[i].flags<=15&&
            (polys[i].flags&(polys[i].flags+1))==0,"Nav64: invalid polygon");
        for(int j=0;j<polys[i].vertCount;++j) {
            Require(polys[i].verts[j]<h.vertCount,"Nav64: invalid vertex index");
            auto nei=polys[i].neis[j];Require((nei&DT_EXT_LINK)?((nei&0xff)<8):(nei<=h.polyCount),"Nav64: invalid neighbour index");
        }
    }
    offset+=dtAlign4(sizeof(dtPoly)*h.polyCount)+dtAlign4(sizeof(dtLink)*h.maxLinkCount);
    auto* details=reinterpret_cast<const dtPolyDetail*>(data+offset);
    for(int i=0;i<h.detailMeshCount;++i)Require(std::uint64_t(details[i].vertBase)+details[i].vertCount<=std::uint64_t(h.detailVertCount)&&
        std::uint64_t(details[i].triBase)+details[i].triCount<=std::uint64_t(h.detailTriCount),"Nav64: invalid detail index");
    offset+=dtAlign4(sizeof(dtPolyDetail)*h.detailMeshCount);
    const auto* detailVertices=reinterpret_cast<const float*>(data+offset);
    for(int i=0;i<h.detailVertCount*3;++i)Require(std::isfinite(detailVertices[i]),"Nav64: invalid detail vertex");
    offset+=dtAlign4(sizeof(float)*3*h.detailVertCount);
    const auto* triangles=data+offset;
    for(int i=0;i<h.polyCount;++i)for(unsigned j=0;j<details[i].triCount;++j)for(int k=0;k<3;++k)
        Require(triangles[(details[i].triBase+j)*4+k]<polys[i].vertCount+details[i].vertCount,"Nav64: invalid detail triangle");
    offset+=dtAlign4(sizeof(unsigned char)*4*h.detailTriCount);
    const auto* nodes=reinterpret_cast<const dtBVNode*>(data+offset);
    for(int i=0;i<h.bvNodeCount;++i) {
        for(int j=0;j<3;++j)Require(nodes[i].bmin[j]<=nodes[i].bmax[j],"Nav64: invalid BV node bounds");
        Require(nodes[i].i>=0?nodes[i].i<h.polyCount:(std::int64_t(-std::int64_t(nodes[i].i))<=h.bvNodeCount-i),"Nav64: invalid BV node index");
    }
}
struct BakeContext final : rcContext {
    unsigned material; float radius; int x,z; bool printing; std::string failure;
    BakeContext(const Profile& p,int tx,int tz,bool logging):rcContext(true),material(p.materialClass),radius(p.radius),x(tx),z(tz),printing(logging) {}
    void doLog(const rcLogCategory category,const char* message,const int length) override {
        if(category==RC_LOG_ERROR&&failure.empty())failure.assign(message,length);
        if(category==RC_LOG_ERROR&&printing)fprintf(stderr,"RECAST class=%u radius=%.2f tile=%d,%d reason=%.*s\n",material,radius,x,z,length,message);
    }
};
struct Resources {
    rcHeightfield* height=nullptr;rcCompactHeightfield* compact=nullptr;rcContourSet* contours=nullptr;rcPolyMesh* polys=nullptr;rcPolyMeshDetail* detail=nullptr;
    ~Resources(){rcFreeHeightField(height);rcFreeCompactHeightfield(compact);rcFreeContourSet(contours);rcFreePolyMesh(polys);rcFreePolyMeshDetail(detail);}
};
 // Shared raw tiles are bounded: a faster profile cannot retain a whole zone.
using RawField = std::shared_ptr<rcHeightfield>;
struct RasterBatch {
    struct Entry {
        std::promise<RawField> promise;
        std::shared_future<RawField> value=promise.get_future().share();
        unsigned readers=0;
    };
    using DomainKey=std::tuple<float,float,unsigned>; // cell size, ground/levitation slope
    using TileKey=std::tuple<float,float,unsigned,int>;
    std::map<DomainKey,Profile> domains;
    std::map<DomainKey,unsigned> consumers,retained;
    std::map<TileKey,std::shared_ptr<Entry>> fields;
    std::mutex mutex;
    std::condition_variable changed;
    bool failed=false;
    explicit RasterBatch(const std::vector<BuildRequest>& requests) {
        for(const auto& request:requests) {
            const auto& p=request.profile;auto key=DomainKey{p.cellSize,p.slope,p.materialClass};
            auto i=domains.emplace(key,p).first;
            i->second.radius=std::max(i->second.radius,p.radius);
            i->second.height=std::max(i->second.height,p.height);
            ++consumers[key];
        }
    }
    const Profile& Domain(const Profile& p) const {return domains.at({p.cellSize,p.slope,p.materialClass});}
    void Cancel() {std::lock_guard<std::mutex> lock(mutex);failed=true;changed.notify_all();}
    RawField Get(const Profile& p,int tile,const std::function<RawField()>& rasterize) {
        const DomainKey domain{p.cellSize,p.slope,p.materialClass};const TileKey key{p.cellSize,p.slope,p.materialClass,tile};
        std::shared_ptr<Entry> entry;bool owner=false;
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock,[&]{return failed||fields.count(key)||retained[domain]<32;});
            Require(!failed,"Nav64: profile build cancelled");
            auto i=fields.find(key);
            if(i==fields.end()){entry=std::make_shared<Entry>();fields.emplace(key,entry);++retained[domain];owner=true;}
            else entry=i->second;
        }
        if(owner) {
            try {entry->promise.set_value(rasterize());}
            catch(...) {entry->promise.set_exception(std::current_exception());Cancel();throw;}
        }
        auto field=entry->value.get();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(++entry->readers==consumers.at(domain)){fields.erase(key);--retained[domain];changed.notify_all();}
        }
        return field;
    }
};
}
static std::unique_ptr<Mesh> BuildProfile(const Geometry& g,const Profile& p,const std::string& key,RasterBatch* batch,unsigned workerLimit,int onlyX=-1,int onlyZ=-1) {
    const auto buildStart=std::chrono::steady_clock::now();
    CheckProfile(p);
    const auto& domain=batch?batch->Domain(p):p;
    auto mesh=std::make_unique<Mesh>();mesh->profile=p;mesh->data.reset(dtAllocNavMesh());
    Require(bool(mesh->data),"Nav64: cannot allocate Detour mesh");
    float tileSize=p.cellSize*p.tileCells;
    int nx=int(std::ceil((g.maximum.x-g.minimum.x)/tileSize)), nz=int(std::ceil((g.maximum.z-g.minimum.z)/tileSize));
    Require(nx>0&&nz>0&&std::uint64_t(nx)*nz<=16384,"Nav64: zone exceeds tile limit");
    dtNavMeshParams params{};params.orig[0]=g.minimum.x;params.orig[1]=g.minimum.y-2;params.orig[2]=g.minimum.z;
    params.tileWidth=params.tileHeight=tileSize;params.maxTiles=int(dtNextPow2(nx*nz*4));params.maxPolys=32768;
    // Bin once, instead of scanning every triangle/solid again for each tile.
    // The margin is exactly the Recast border used below, including erosion.
    struct TileInput {std::vector<int> triangles,solids;};
    struct SolidInput {std::vector<float> hull;float low,high;};
    std::vector<TileInput> inputs(nx*nz);std::vector<SolidInput> solids;
    std::vector<float> allVertices;std::vector<int> allIndices;
    std::vector<unsigned char> allAreas;
    allVertices.reserve(g.ground.size()*9);allIndices.reserve(g.ground.size()*3);allAreas.reserve(g.ground.size());
    const float border=(std::ceil(domain.radius/p.cellSize)+3)*p.cellSize;
    auto bin=[&](float minx,float minz,float maxx,float maxz,int index,bool solid) {
        int x0=std::max(0,int(std::floor((minx-g.minimum.x-border)/tileSize)));
        int z0=std::max(0,int(std::floor((minz-g.minimum.z-border)/tileSize)));
        int x1=std::min(nx-1,int(std::floor((maxx-g.minimum.x+border)/tileSize)));
        int z1=std::min(nz-1,int(std::floor((maxz-g.minimum.z+border)/tileSize)));
        for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)
            (solid?inputs[z*nx+x].solids:inputs[z*nx+x].triangles).push_back(index);
    };
    for(size_t i=0;i<g.ground.size();++i) {
        const auto t=Surface(g.ground[i],p);Point a=ToNav(t.a),b=ToNav(t.b),c=ToNav(t.c);
        for(auto v:{a,b,c}){allIndices.push_back(int(allVertices.size()/3));allVertices.insert(allVertices.end(),{v.x,v.y,v.z});}
        allAreas.push_back(t.area);
        bin(std::min({a.x,b.x,c.x}),std::min({a.z,b.z,c.z}),std::max({a.x,b.x,c.x}),std::max({a.z,b.z,c.z}),int(i),false);
    }
    rcContext classifier(false);std::vector<unsigned char> slopes(allAreas.size(),RC_NULL_AREA);
    rcMarkWalkableTriangles(&classifier,p.slope,allVertices.data(),int(allVertices.size()/3),allIndices.data(),int(allAreas.size()),slopes.data());
    for(size_t i=0;i<allAreas.size();++i)if(slopes[i]==RC_NULL_AREA)allAreas[i]=RC_NULL_AREA;
    for(const auto& s:g.solids) {
        SolidInput input;input.hull=Footprint(s,input.low,input.high);
        if(input.hull.size()<9)continue;
        float minx=input.hull[0],maxx=minx,minz=input.hull[2],maxz=minz;
        for(size_t i=0;i<input.hull.size();i+=3){minx=std::min(minx,input.hull[i]);maxx=std::max(maxx,input.hull[i]);minz=std::min(minz,input.hull[i+2]);maxz=std::max(maxz,input.hull[i+2]);}
        bin(minx,minz,maxx,maxz,int(solids.size()),true);solids.push_back(std::move(input));
    }
    // Workers own all Recast state and output blobs. Only the simulation thread
    // mutates Detour, in fixed z/x order, so cache bytes/references stay stable.
    struct TileBlob {unsigned char* data=nullptr;int size=0;~TileBlob(){dtFree(data);}};
    std::vector<std::unique_ptr<TileBlob>> tiles(nx*nz);std::vector<std::exception_ptr> errors(nx*nz);
    std::atomic<int> next{0};std::atomic<bool> failed{false};
    std::atomic<std::uint64_t> rasterUs{0},polygonUs{0};
    auto micros=[](auto begin){return std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count());};
    auto tile=[&](int index) {
        const int x=index%nx,z=index/nx;
        if((onlyX>=0&&(x!=onlyX||z!=onlyZ))||inputs[index].triangles.empty())return;
        BakeContext context(p,x,z,onlyX<0);auto begin=std::chrono::steady_clock::now();
        rcConfig cfg{};cfg.cs=p.cellSize;cfg.ch=p.cellHeight;cfg.walkableSlopeAngle=p.slope;
        cfg.walkableHeight=int(std::ceil(p.height/cfg.ch));cfg.walkableClimb=int(std::floor(p.climb/cfg.ch));cfg.walkableRadius=int(std::ceil(p.radius/cfg.cs));
        cfg.tileSize=p.tileCells;cfg.borderSize=int(std::ceil(domain.radius/p.cellSize))+3;cfg.width=cfg.height=cfg.tileSize+cfg.borderSize*2;
        // A zero error retained every voxel stair in every clearance contour.
        // One cell removes those stairs without removing region/pose borders.
        cfg.maxEdgeLen=120;cfg.maxSimplificationError=1.f;cfg.minRegionArea=0;cfg.mergeRegionArea=400;cfg.maxVertsPerPoly=6;
        // Keep Detour's floor/BASE heights, with a coarser sample grid and at
        // most 10 cm sample error. This does not change walkable contours.
        cfg.detailSampleDist=1.2f;cfg.detailSampleMaxError=.1f;
        cfg.bmin[0]=g.minimum.x+x*tileSize-cfg.borderSize*cfg.cs;cfg.bmin[1]=g.minimum.y-2;cfg.bmin[2]=g.minimum.z+z*tileSize-cfg.borderSize*cfg.cs;
        cfg.bmax[0]=g.minimum.x+(x+1)*tileSize+cfg.borderSize*cfg.cs;cfg.bmax[1]=g.maximum.y+domain.height+2;cfg.bmax[2]=g.minimum.z+(z+1)*tileSize+cfg.borderSize*cfg.cs;
        auto rasterize=[&]() -> RawField {
        RawField field(rcAllocHeightfield(),rcFreeHeightField);Require(field&&rcCreateHeightfield(&context,*field,cfg.width,cfg.height,cfg.bmin,cfg.bmax,cfg.cs,cfg.ch),"Nav64: heightfield allocation failed");
        std::vector<float> vertices;std::vector<int> indices;std::vector<unsigned char> areas;
        vertices.reserve(inputs[index].triangles.size()*9);indices.reserve(inputs[index].triangles.size()*3);areas.reserve(inputs[index].triangles.size());
        for(int triangle:inputs[index].triangles) {
            const auto* v=allVertices.data()+size_t(triangle)*9;
            for(int j=0;j<3;++j){indices.push_back(int(vertices.size()/3));vertices.insert(vertices.end(),v+j*3,v+j*3+3);}
            areas.push_back(allAreas[triangle]);
        }
        Require(rcRasterizeTriangles(&context,vertices.data(),int(vertices.size()/3),indices.data(),areas.data(),int(areas.size()),*field,cfg.walkableClimb),"Nav64: ground rasterization failed");
        // Insert each real solid as a non-walkable prism. Unlike subtracting
        // at one agent height, this retains the underside as a ceiling for all
        // poses, and never creates a walkable BV top. Quantize outwards.
        for(int solid:inputs[index].solids) {
            const auto& s=solids[solid];
            float minx=s.hull[0],maxx=minx,minz=s.hull[2],maxz=minz;
            for(size_t j=0;j<s.hull.size();j+=3){minx=std::min(minx,s.hull[j]);maxx=std::max(maxx,s.hull[j]);minz=std::min(minz,s.hull[j+2]);maxz=std::max(maxz,s.hull[j+2]);}
            int x0=std::max(0,int(std::floor((minx-cfg.bmin[0])/cfg.cs))),x1=std::min(cfg.width-1,int(std::floor((maxx-cfg.bmin[0])/cfg.cs)));
            int z0=std::max(0,int(std::floor((minz-cfg.bmin[2])/cfg.cs))),z1=std::min(cfg.height-1,int(std::floor((maxz-cfg.bmin[2])/cfg.cs)));
            int bottom=std::clamp(int(std::floor((s.low-cfg.bmin[1])/cfg.ch)),0,RC_SPAN_MAX_HEIGHT-1);
            int top=std::clamp(int(std::ceil((s.high-cfg.bmin[1])/cfg.ch)),bottom+1,RC_SPAN_MAX_HEIGHT);
            for(int iz=z0;iz<=z1;++iz)for(int ix=x0;ix<=x1;++ix) {
                const float lx=cfg.bmin[0]+ix*cfg.cs,lz=cfg.bmin[2]+iz*cfg.cs;
                bool inside=true;
                // Footprint() returns a counter-clockwise convex hull in XZ.
                // A cell belongs if its rectangle intersects every half-plane.
                for(size_t j=0;j<s.hull.size();j+=3) {
                    const auto k=(j+3)%s.hull.size();float ax=s.hull[j],az=s.hull[j+2],dx=s.hull[k]-ax,dz=s.hull[k+2]-az;
                    const float px=dz>0?lx:lx+cfg.cs,pz=dx>0?lz+cfg.cs:lz;
                    if(dx*(pz-az)-dz*(px-ax)<-1.e-6f){inside=false;break;}
                }
                if(inside)Require(rcAddSpan(&context,*field,ix,iz,static_cast<unsigned short>(bottom),static_cast<unsigned short>(top),RC_NULL_AREA,0),"Nav64: solid rasterization failed");
            }
        }

            return field;
        };
        auto raw=batch?batch->Get(p,index,rasterize):rasterize();
        Resources r;r.height=rcAllocHeightfield();
        Require(r.height&&rcCreateHeightfield(&context,*r.height,cfg.width,cfg.height,cfg.bmin,cfg.bmax,cfg.cs,cfg.ch),"Nav64: profile heightfield allocation failed");
        for(int iz=0;iz<raw->height;++iz)for(int ix=0;ix<raw->width;++ix)
            for(auto* span=raw->spans[ix+iz*raw->width];span;span=span->next)
                Require(rcAddSpan(&context,*r.height,ix,iz,span->smin,span->smax,static_cast<unsigned char>(span->area),0),"Nav64: profile heightfield copy failed");
        raw.reset();
        rcFilterLedgeSpans(&context,cfg.walkableHeight,cfg.walkableClimb,*r.height);
        rcFilterWalkableLowHeightSpans(&context,cfg.walkableHeight,*r.height);
        r.compact=rcAllocCompactHeightfield();Require(r.compact&&rcBuildCompactHeightfield(&context,cfg.walkableHeight,cfg.walkableClimb,*r.height,*r.compact),"Nav64: compact heightfield failed");
        rasterUs.fetch_add(micros(begin));begin=std::chrono::steady_clock::now();
        for(const auto& door:g.doors) {
            const auto a=ToNav(door.minimum),b=ToNav(door.maximum);
            const float low[]={std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};
            const float high[]={std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};
            rcMarkBoxArea(&context,low,high,15,*r.compact);
        }
        if(PoseClearance(p)) {
            auto& chf=*r.compact;
            const std::vector<unsigned char> material(chf.areas,chf.areas+chf.spanCount);
            std::vector<unsigned char> tiers(chf.spanCount,0),clearance(chf.spanCount);
            // Include overlap across a step, so two individually tall spans
            // cannot connect through a shorter ceiling at their common edge.
            for(int iz=0;iz<chf.height;++iz)for(int ix=0;ix<chf.width;++ix) {
                const auto& cell=chf.cells[ix+iz*chf.width];
                for(unsigned j=cell.index;j<cell.index+cell.count;++j) {
                    const auto& span=chf.spans[j];int free=span.h;
                    for(int dir=0;dir<4;++dir)if(rcGetCon(span,dir)!=RC_NOT_CONNECTED) {
                        const int nx=ix+rcGetDirOffsetX(dir),nz=iz+rcGetDirOffsetY(dir);
                        const auto& other=chf.spans[chf.cells[nx+nz*chf.width].index+rcGetCon(span,dir)];
                        free=std::min(free,int(std::min(span.y+span.h,other.y+other.h))-int(std::max(span.y,other.y)));
                    }
                    clearance[j]=static_cast<unsigned char>(std::clamp(free,0,255));
                }
            }
            // Rasterization and compact data are shared by every pose. Erode
            // each permission at the full agent radius, then split contours at
            // the resulting clearance boundary. 4 * 15 material IDs fit Recast's
            // 63 area IDs; Detour receives the original material and pose flags.
            for(unsigned tier=0;tier<4;++tier) {
                const int cells=int(std::ceil(ClearanceHeights(p)[tier]/cfg.ch));
                for(int j=0;j<chf.spanCount;++j)chf.areas[j]=clearance[j]>=cells?material[j]:RC_NULL_AREA;
                Require(rcErodeWalkableArea(&context,cfg.walkableRadius,chf),"Nav64: pose radius erosion failed");
                for(int j=0;j<chf.spanCount;++j)if(chf.areas[j])tiers[j]=static_cast<unsigned char>(tier+1);
            }
            for(int j=0;j<chf.spanCount;++j)chf.areas[j]=tiers[j]?static_cast<unsigned char>((tiers[j]-1)*15+material[j]):RC_NULL_AREA;
        } else Require(rcErodeWalkableArea(&context,cfg.walkableRadius,*r.compact),"Nav64: radius erosion failed");
        Require(rcBuildRegionsMonotone(&context,*r.compact,cfg.borderSize,cfg.minRegionArea,cfg.mergeRegionArea),"Nav64: region build failed");
        // Classify holes from the region's actual grid topology, not an
        // axis-aligned gap width: a slanted/branched hole evades that heuristic.
        // Euler characteristic for 4-connected pixels, in quarter units:
        // chi*4 = single corners - triple corners + 2*diagonal pairs.
        const auto& compact=*r.compact;
        std::vector<int> euler(compact.maxRegions+1,0);
        std::vector<bool> present(compact.maxRegions+1,false);
        std::vector<std::pair<unsigned short,unsigned>> masks;masks.reserve(16);
        for(int iz=0;iz<=compact.height;++iz)for(int ix=0;ix<=compact.width;++ix) {
            masks.clear();
            for(int corner=0;corner<4;++corner) {
                const int cx=ix-1+(corner&1),cz=iz-1+(corner>>1);
                if(cx<0||cz<0||cx>=compact.width||cz>=compact.height)continue;
                const auto& cell=compact.cells[cx+cz*compact.width];
                for(unsigned j=cell.index;j<cell.index+cell.count;++j) {
                    const auto reg=compact.spans[j].reg;if(!reg||(reg&RC_BORDER_REG))continue;
                    auto i=std::find_if(masks.begin(),masks.end(),[&](const auto& pair){return pair.first==reg;});
                    if(i==masks.end())masks.emplace_back(reg,1u<<corner);else i->second|=1u<<corner;
                }
            }
            for(const auto& entry:masks) {
                const auto mask=entry.second;const int count=(mask&1)+((mask>>1)&1)+((mask>>2)&1)+((mask>>3)&1);
                present[entry.first]=true;
                euler[entry.first]+=count==1?1:count==3?-1:(mask==6||mask==9)?2:0;
            }
        }
        bool holes=false;for(size_t i=1;i<euler.size();++i)if(present[i]&&euler[i]<=0){holes=true;break;}
        // Keep hole boundaries within a quarter cell; one full cell can
        // collapse them to zero signed area and turn holes into outlines.
        cfg.maxSimplificationError=holes?.25f:1.f;
        r.contours=rcAllocContourSet();
        Require(r.contours&&rcBuildContours(&context,*r.compact,cfg.maxSimplificationError,cfg.maxEdgeLen,*r.contours),"Nav64: contour build failed");
        r.polys=rcAllocPolyMesh();
        Require(r.polys&&rcBuildPolyMesh(&context,*r.contours,cfg.maxVertsPerPoly,*r.polys),"Nav64: polygon build failed");
        if(!context.failure.empty())throw std::runtime_error("Nav64: Recast tile="+std::to_string(x)+","+std::to_string(z)+": "+context.failure);
        auto& poly=*r.polys;if(!poly.npolys)return;
        for(int i=0;i<poly.npolys;++i) {
            const unsigned encoded=poly.areas[i];
            Require(encoded>0&&encoded<=60,"Nav64: invalid clearance area");
            poly.flags[i]=static_cast<unsigned short>((1u<<((encoded-1)/15+1))-1);
            poly.areas[i]=static_cast<unsigned char>((encoded-1)%15+1);
        }
        dtNavMeshCreateParams create{};
        create.verts=poly.verts;create.vertCount=poly.nverts;create.polys=poly.polys;
        create.polyAreas=poly.areas;create.polyFlags=poly.flags;create.polyCount=poly.npolys;create.nvp=poly.nvp;
        create.walkableHeight=p.height;create.walkableRadius=p.radius;create.walkableClimb=p.climb;
        create.tileX=x;create.tileY=z;create.tileLayer=0;
        rcVcopy(create.bmin,poly.bmin);rcVcopy(create.bmax,poly.bmax);
        create.cs=cfg.cs;create.ch=cfg.ch;create.buildBvTree=true;
        // Detour emits polygon fans without a detail mesh. Game support supplies Z.
        auto blob=std::make_unique<TileBlob>();
        Require(dtCreateNavMeshData(&create,&blob->data,&blob->size),"Nav64: ready tile creation failed");
        tiles[index]=std::move(blob);
        polygonUs.fetch_add(micros(begin));
    };
    auto worker=[&] {
        while(!failed.load()) {
            int index=next.fetch_add(1);if(index>=nx*nz)break;
            try {tile(index);}catch(...) {errors[index]=std::current_exception();failed.store(true);if(batch)batch->Cancel();}
        }
    };
    mesh->workers=std::min(unsigned(nx*nz),workerLimit);
    std::vector<std::thread> workers;
    try {for(unsigned i=1;i<mesh->workers;++i)workers.emplace_back(worker);}
    catch(...) {failed.store(true);for(auto& thread:workers)thread.join();throw;}
    worker();for(auto& thread:workers)thread.join();
    for(const auto& error:errors)if(error)std::rethrow_exception(error);
    mesh->rasterMs=rasterUs.load()/1000;mesh->polygonMs=polygonUs.load()/1000;
    Require(params.maxTiles<=131072&&dtStatusSucceed(mesh->data->init(&params)),"Nav64: cannot initialize Detour mesh");
    for(auto& blob:tiles)if(blob) {
        Require(dtStatusSucceed(mesh->data->addTile(blob->data,blob->size,DT_TILE_FREE_DATA,0,nullptr)),"Nav64: cannot add ready tile");
        blob->data=nullptr;
    }
    const dtNavMesh& finalMesh=*mesh->data;
    for(int i=0;i<finalMesh.getMaxTiles();++i)if(finalMesh.getTile(i)->header)++mesh->tiles;
    mesh->query.reset(dtAllocNavMeshQuery());Require(mesh->query&&dtStatusSucceed(mesh->query->init(mesh->data.get(),65535)),"Nav64: query allocation failed");
    // No traversable polygons is a valid empty mesh, not a build/data failure.

    mesh->buildMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-buildStart).count();
    return mesh;
}
std::unique_ptr<Mesh> Build(const Geometry& g,const Profile& p,const std::string& key) {
    return BuildProfile(g,p,key,nullptr,std::min(8u,std::max(1u,std::thread::hardware_concurrency()/2)));
}
std::vector<std::unique_ptr<Mesh>> BuildProfiles(const Geometry& g,const std::vector<BuildRequest>& requests) {
    RasterBatch batch(requests);std::vector<std::future<std::unique_ptr<Mesh>>> pending;
    const unsigned workers=std::max(1u,std::min(8u,std::max(1u,std::thread::hardware_concurrency()/2))/unsigned(std::max(size_t(1),requests.size())));
    try {
        for(const auto& request:requests)pending.push_back(std::async(std::launch::async,[&,request]{
            try {return BuildProfile(g,request.profile,"",&batch,workers);}
            catch(...) {batch.Cancel();throw;}
        }));
    }catch(...) {batch.Cancel();throw;}
    std::vector<std::unique_ptr<Mesh>> result;std::exception_ptr failure;
    for(auto& future:pending)try {result.push_back(future.get());}catch(...) {if(!failure)failure=std::current_exception();}
    if(failure)std::rethrow_exception(failure);
    return result;
}
Bytes Store(const Mesh& mesh) {
    Require(!mesh.mapName.empty()&&!mesh.mobName.empty()&&!mesh.profileId.empty(),"Nav64: missing map identity");
    Wire w;w.U(0x3656414e);w.U(0x00003436);w.U(ContainerVersion);w.U(AlgorithmVersion);w.U(LibraryVersion);w.U(0x12345678);w.U(sizeof(dtPolyRef));
    w.S(mesh.mapName);w.S(mesh.mobName);w.S(mesh.profileId);w.ProfileOut(mesh.profile);
    const auto* p=mesh.data->getParams();for(float v:p->orig)w.F(v);w.F(p->tileWidth);w.F(p->tileHeight);w.U(p->maxTiles);w.U(p->maxPolys);
    w.U(2); // Ready tiles and surface metadata. Rebuild sources are shared per map.
    Wire tiles;tiles.U(mesh.tiles);
    const dtNavMesh* data=mesh.data.get();
    for(int i=0;i<data->getMaxTiles();++i) {
        const auto* t=data->getTile(i);if(!t||!t->header)continue;
        Bytes raw(t->data,t->data+t->dataSize);
        const size_t polyOffset=dtAlign4(sizeof(dtMeshHeader))+dtAlign4(sizeof(float)*3*t->header->vertCount);
        auto* polys=reinterpret_cast<dtPoly*>(raw.data()+polyOffset);
        for(int j=0;j<t->header->polyCount;++j)polys[j].firstLink=DT_NULL_LINK;
        // addTile/connectIntLinks rebuild all links, including neighbour links.
        // Never persist the process-local references of an already added tile.
        std::memset(raw.data()+polyOffset+dtAlign4(sizeof(dtPoly)*t->header->polyCount),0,
            dtAlign4(sizeof(dtLink)*t->header->maxLinkCount));
        const auto shuffled=ShuffleTile(raw.data(),raw.size(),true);
        tiles.U(std::uint32_t(shuffled.size()));tiles.B(shuffled.data(),shuffled.size());
    }
    const auto packed=Pack(tiles.bytes);
    w.U(1);w.U(std::uint32_t(packed.size()+12));w.U(COMPRESS_ALGORITHM_LZMS);
    w.U(std::uint32_t(tiles.bytes.size()));w.U(std::uint32_t(packed.size()));w.B(packed.data(),packed.size());
    w.U(2);w.U(8);w.U(1);w.U(2); // terrain=1, BASE=(NID<<32)|(part+2), source/off-mesh section in navgeom
    return std::move(w.bytes);
}
std::unique_ptr<Mesh> Load(Bytes bytes,const Profile& expected,
    const std::string& mapName,const std::string& mobName,const std::string& profileId) {
    Wire w;w.bytes=std::move(bytes);
    Require(w.U()==0x3656414e&&w.U()==0x00003436&&w.U()==ContainerVersion&&w.U()==AlgorithmVersion&&w.U()==LibraryVersion&&w.U()==0x12345678&&w.U()==sizeof(dtPolyRef),"Nav64: incompatible container/ABI");
    auto mesh=std::make_unique<Mesh>();
    mesh->mapName=w.S();mesh->mobName=w.S();mesh->profileId=w.S();
    Require(mesh->mapName==mapName&&mesh->mobName==mobName&&mesh->profileId==profileId,"Nav64: mismatched map identity");
    mesh->profile=w.ProfileIn();CheckProfile(mesh->profile);
    Wire a,b;auto profile=expected;profile.climb=mesh->profile.climb;a.ProfileOut(profile);b.ProfileOut(mesh->profile);Require(a.bytes==b.bytes,"Nav64: mismatched profile");
    dtNavMeshParams p{};for(auto& v:p.orig)v=w.F();p.tileWidth=w.F();p.tileHeight=w.F();p.maxTiles=w.U();p.maxPolys=w.U();
    Require(p.maxTiles>0&&p.maxTiles<=131072&&dtNextPow2(p.maxTiles)==p.maxTiles&&p.maxPolys==32768&&p.tileWidth==expected.cellSize*expected.tileCells&&p.tileHeight==expected.cellSize*expected.tileCells,"Nav64: invalid mesh limits");
    mesh->data.reset(dtAllocNavMesh());Require(mesh->data&&dtStatusSucceed(mesh->data->init(&p)),"Nav64: mesh initialization failed");
    Require(w.U()==2,"Nav64: invalid section count");
    Require(w.U()==1,"Nav64: missing tile section");auto section=w.U();Require(section<=w.bytes.size()-w.at,"Nav64: truncated tile section");auto end=w.at+section;
    Require(section>=12&&w.U()==COMPRESS_ALGORITHM_LZMS,"Nav64: incompatible compression");
    auto expanded=w.U(),packed=w.U();Require(w.at<=end&&packed==end-w.at,"Nav64: truncated compressed tiles");
    Wire tiles;tiles.bytes=Unpack(w.bytes.data()+w.at,packed,expanded);w.at=end;
    auto count=tiles.U();Require(count<=std::uint32_t(p.maxTiles),"Nav64: tile count limit");
    for(std::uint32_t i=0;i<count;++i){auto n=tiles.U();Require(n<=tiles.bytes.size()-tiles.at,"Nav64: truncated tile");
        const auto raw=ShuffleTile(tiles.bytes.data()+tiles.at,n,false);CheckTile(raw.data(),raw.size());
        auto* data=static_cast<unsigned char*>(dtAlloc(n,DT_ALLOC_PERM));Require(data!=nullptr,"Nav64: tile allocation failed");std::memcpy(data,raw.data(),n);tiles.at+=n;
        auto s=mesh->data->addTile(data,n,DT_TILE_FREE_DATA,0,nullptr);if(dtStatusFailed(s)){dtFree(data);throw std::runtime_error("Nav64: invalid/duplicate tile");}++mesh->tiles;
    }
    Require(tiles.at==tiles.bytes.size()&&w.at==end&&w.U()==2&&w.U()==8&&w.U()==1&&w.U()==2&&w.at==w.bytes.size(),"Nav64: invalid surface section");
    mesh->query.reset(dtAllocNavMeshQuery());Require(mesh->query&&dtStatusSucceed(mesh->query->init(mesh->data.get(),65535)),"Nav64: query allocation failed");
    return mesh;
}

Bytes StoreSources(const Geometry& g) {
    // Shared indexed centimetre vertices, one geometry stream for all profiles.
    using Vertex=std::array<std::int32_t,3>;
    auto quantize=[](Point p){return Vertex{std::int32_t(std::lround(p.x*100)),std::int32_t(std::lround(p.y*100)),std::int32_t(std::lround(p.z*100))};};
    std::map<Vertex,unsigned> indices;std::vector<Vertex> vertices;
    for(const auto& t:g.ground)for(auto p:{t.a,t.b,t.c}) {
        const auto v=quantize(p);if(indices.emplace(v,unsigned(vertices.size())).second)vertices.push_back(v);
    }
    Wire w;w.U(0x4352534e);w.U(2);w.P(g.minimum);w.P(g.maximum);w.U(unsigned(vertices.size()));
    for(const auto& v:vertices)for(auto c:v)w.U(std::uint32_t(c));
    w.U(unsigned(g.ground.size()));
    for(const auto& t:g.ground) {
        for(auto p:{t.a,t.b,t.c})w.U(indices.at(quantize(p)));
        for(float d:{t.waterDepth.x,t.waterDepth.y,t.waterDepth.z})w.U(std::uint32_t(std::int32_t(std::lround(d*100))));
        w.bytes.push_back(t.area);w.bytes.push_back(t.levitateArea);w.bytes.push_back(t.terrain?1:0);
        for(auto area:t.classAreas)w.bytes.push_back(area);
        w.U(t.object);w.U(unsigned(t.surface));w.U(unsigned(t.surface>>32));
    }
    w.U(unsigned(g.doors.size()));for(const auto& d:g.doors){w.U(d.object);w.U(d.openState);w.P(d.minimum);w.P(d.maximum);}
    w.U(unsigned(g.connections.size()));for(const auto& c:g.connections) {
        w.P(c.from);w.P(c.to);w.F(c.radius);w.U(unsigned(c.fromSurface));w.U(unsigned(c.fromSurface>>32));
        w.U(unsigned(c.toSurface));w.U(unsigned(c.toSurface>>32));w.U(c.flags);
    }
    const auto packed=Pack(w.bytes);Wire out;out.U(unsigned(w.bytes.size()));out.B(packed.data(),packed.size());return std::move(out.bytes);
}
std::shared_ptr<Geometry> LoadSources(Bytes bytes) {
    Wire in;in.bytes=std::move(bytes);auto size=in.U();Wire w;w.bytes=Unpack(in.bytes.data()+in.at,in.bytes.size()-in.at,size);
    Require(w.U()==0x4352534e&&w.U()==2,"Nav64: incompatible source section");
    auto g=std::make_shared<Geometry>();auto point=[&](){return Point{w.F(),w.F(),w.F()};};
    g->minimum=point();g->maximum=point();auto n=w.U();Require(n<12000000&&n<= (w.bytes.size()-w.at)/12,"Nav64: source vertex limit");
    std::vector<Point> vertices;vertices.reserve(n);
    auto coordinate=[&](){return std::int32_t(w.U())*.01f;};
    for(unsigned i=0;i<n;++i)vertices.push_back({coordinate(),coordinate(),coordinate()});
    n=w.U();Require(n<4000000,"Nav64: source triangle limit");g->ground.reserve(n);
    for(unsigned i=0;i<n;++i) {
        auto vertex=[&](){auto j=w.U();Require(j<vertices.size(),"Nav64: invalid source index");return vertices[j];};
        Triangle t;t.a=vertex();t.b=vertex();t.c=vertex();t.waterDepth={coordinate(),coordinate(),coordinate()};
        Require(w.bytes.size()-w.at>=11,"Nav64: truncated source metadata");
        t.area=w.bytes[w.at++];t.levitateArea=w.bytes[w.at++];t.terrain=w.bytes[w.at++]!=0;
        for(auto& area:t.classAreas)area=w.bytes[w.at++];t.object=int(w.U());
        auto low=w.U();t.surface=low|(std::uint64_t(w.U())<<32);g->ground.push_back(t);
    }
    n=w.U();Require(n<=65536,"Nav64: door count limit");for(unsigned i=0;i<n;++i){Door d;d.object=int(w.U());d.openState=int(w.U());d.minimum=point();d.maximum=point();g->doors.push_back(d);}
    n=w.U();Require(n<=65536,"Nav64: connection count limit");for(unsigned i=0;i<n;++i) {
        OffMesh c;c.from=point();c.to=point();c.radius=w.F();auto low=w.U();c.fromSurface=low|(std::uint64_t(w.U())<<32);
        low=w.U();c.toSurface=low|(std::uint64_t(w.U())<<32);c.flags=w.U();g->connections.push_back(c);
    }
    Require(w.at==w.bytes.size(),"Nav64: trailing source bytes");return g;
}
void IndexSurfaces(Mesh& mesh) {
    mesh.surfaceTriangles.clear();if(!mesh.sources)return;
    const auto* params=mesh.data->getParams();const float width=params->tileWidth;
    for(size_t i=0;i<mesh.sources->ground.size();++i) {
        const auto& t=mesh.sources->ground[i];if(t.surface<=1||!t.area)continue;
        auto a=ToNav(t.a),b=ToNav(t.b),c=ToNav(t.c);
        const int x0=int(std::floor((std::min({a.x,b.x,c.x})-params->orig[0])/width)),x1=int(std::floor((std::max({a.x,b.x,c.x})-params->orig[0])/width));
        const int z0=int(std::floor((std::min({a.z,b.z,c.z})-params->orig[2])/width)),z1=int(std::floor((std::max({a.z,b.z,c.z})-params->orig[2])/width));
        for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)mesh.surfaceTriangles[{x,z}].push_back(i);
    }
}
std::unique_ptr<Mesh> RebuildTile(const Geometry& g,const Profile& p,int x,int z) {
    auto result=BuildProfile(g,p,"",nullptr,1,x,z);
    const auto* tile=result->data->getTileAt(x,z,0);
    if(tile) {
        result->replacementSize=tile->dataSize;
        result->replacementData=static_cast<unsigned char*>(dtAlloc(tile->dataSize,DT_ALLOC_PERM));
        Require(result->replacementData!=nullptr,"Nav64: replacement allocation failed");
        std::memcpy(result->replacementData,tile->data,tile->dataSize);
    }
    // Destroy the temporary zone-capacity mesh/query on the worker too.
    result->query.reset();result->data.reset();
    return result;
}
void ReplaceTile(Mesh& mesh,Mesh& replacement,int x,int z) {
    // No query observes a half-replaced tile: called on the simulation thread.
    const auto* previous=mesh.data->getTileAt(x,z,0);
    if(previous)Require(dtStatusSucceed(mesh.data->removeTile(mesh.data->getTileRef(previous),nullptr,nullptr)),"Nav64: cannot remove source tile");
    if(replacement.replacementData) {
        Require(dtStatusSucceed(mesh.data->addTile(replacement.replacementData,replacement.replacementSize,DT_TILE_FREE_DATA,0,nullptr)),"Nav64: cannot install replacement tile");
        replacement.replacementData=nullptr;
    }
    for(auto i=mesh.doorFlags.begin();i!=mesh.doorFlags.end();) {
        if(std::get<0>(i->first)==x&&std::get<1>(i->first)==z)i=mesh.doorFlags.erase(i);else ++i;
    }
    ++mesh.generation;
}
void DoorState(Mesh& mesh,int object,bool open) {
    mesh.doorOpen[object]=open;if(!mesh.sources)return;
    const auto d=std::find_if(mesh.sources->doors.begin(),mesh.sources->doors.end(),[&](const Door& d){return d.object==object;});
    if(d==mesh.sources->doors.end())return;
    const dtNavMesh& data=*mesh.data;const auto* params=data.getParams();const float width=params->tileWidth;
    const int x0=int(std::floor((d->minimum.x-params->orig[0])/width)),x1=int(std::floor((d->maximum.x-params->orig[0])/width));
    const int z0=int(std::floor((-d->maximum.y-params->orig[2])/width)),z1=int(std::floor((-d->minimum.y-params->orig[2])/width));
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x) {
        const auto* t=data.getTileAt(x,z,0);if(!t)continue;
        for(int j=0;j<t->header->polyCount;++j) {
            const auto& p=t->polys[j];if(p.getArea()!=15)continue;Point center{};
            for(int k=0;k<p.vertCount;++k){const auto* v=t->verts+p.verts[k]*3;center.x+=v[0]/p.vertCount;center.y-=v[2]/p.vertCount;center.z+=v[1]/p.vertCount;}
            if(center.x<d->minimum.x-.01f||center.x>d->maximum.x+.01f||center.y<d->minimum.y-.01f||center.y>d->maximum.y+.01f||center.z<d->minimum.z-.01f||center.z>d->maximum.z+.01f)continue;
            const auto ref=data.getPolyRefBase(t)|dtPolyRef(j);const auto key=std::make_tuple(t->header->x,t->header->y,t->header->layer,j);
            auto saved=mesh.doorFlags.emplace(key,p.flags).first;
            mesh.data->setPolyFlags(ref,open?saved->second:0);
        }
    }
    ++mesh.generation;
}
}
