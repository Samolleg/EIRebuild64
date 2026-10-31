#include "Build.h"
#include "Flight.h"
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
    std::uint32_t U(){Require(at<=bytes.size() && bytes.size()-at>=4,"Nav64: truncated field");std::uint32_t n;std::memcpy(&n,bytes.data()+at,4);at+=4;return n;}
    float F(){auto n=U();float v;std::memcpy(&v,&n,4);Require(std::isfinite(v),"Nav64: invalid float");return v;}
    std::string S(){auto n=U();Require(n<=4096 && n<=bytes.size()-at,"Nav64: invalid text");std::string s(reinterpret_cast<const char*>(bytes.data()+at),n);at+=n;return s;}
    void ProfileOut(const Profile& p){F(p.radius);F(p.height);F(p.slope);F(p.climb);F(p.cellSize);F(p.cellHeight);U(p.tileCells);U(p.materialClass);U(unsigned(p.movement));}
    Profile ProfileIn(){Profile p;p.radius=F();p.height=F();p.slope=F();p.climb=F();p.cellSize=F();p.cellHeight=F();p.tileCells=U();p.materialClass=U();p.movement=Movement(U());Require(unsigned(p.movement)<=2&&p.materialClass<8,"Nav64: invalid material class");return p;}
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
// Byte planes preserve every float/index bit while making repeated source
// metadata and grid-coordinate exponents adjacent for the lossless codec.
void SourceArray(Bytes& bytes,size_t offset,size_t count,size_t width) {
    Require(offset<=bytes.size()&&count<=(bytes.size()-offset)/width,"Nav64: truncated source array");
    Bytes result(count*width);const auto* input=bytes.data()+offset;
    for(size_t first=0;first<count;first+=256) {
        const auto end=std::min(count,first+256);
        for(size_t c=0;c<width;++c)for(size_t i=first;i<end;++i)result[c*count+i]=input[i*width+c];
    }
    std::copy(result.begin(),result.end(),bytes.begin()+offset);
}
// Read the existing byte planes directly into Geometry. Reassembling packed
// records first would allocate/write/copy the full 47-byte triangle array.
// Bounds are checked once per array; index and finite-float checks stay below.
template<size_t Width> struct SourceColumns {
    std::array<const std::uint8_t*,Width> lanes;
    SourceColumns(Wire& w,size_t count) {
        Require(w.at<=w.bytes.size()&&count<=(w.bytes.size()-w.at)/Width,"Nav64: truncated source array");
        for(size_t c=0;c<Width;++c)lanes[c]=w.bytes.data()+w.at+c*count;
        w.at+=count*Width;
    }
    std::uint8_t B(size_t row,size_t column) const {return lanes[column][row];}
    std::uint32_t U(size_t row,size_t column) const {
        return std::uint32_t(B(row,column))|(std::uint32_t(B(row,column+1))<<8)|
            (std::uint32_t(B(row,column+2))<<16)|(std::uint32_t(B(row,column+3))<<24);
    }
    float F(size_t row,size_t column) const {
        const auto n=U(row,column);float v;std::memcpy(&v,&n,4);
        Require(std::isfinite(v),"Nav64: invalid float");return v;
    }
    Point P(size_t row,size_t column) const {return {F(row,column),F(row,column+4),F(row,column+8)};}
};
// Source decoding has no cross-row dependencies. Fill disjoint ranges in the
// final vectors; small maps avoid thread startup. Future destruction joins all
// workers even if an index/float check throws, before the vectors can expire.
template<class Work> void SourceRows(size_t count,Work work) {
    if(count<65536){work(0,count);return;}
    constexpr size_t workers=4;std::array<std::future<void>,workers> pending;
    for(size_t i=0;i<workers;++i)pending[i]=std::async(std::launch::async,[&,i] {
        work(count*i/workers,count*(i+1)/workers);
    });
    for(auto& job:pending)job.get();
}
void CheckProfile(const Profile& p) {
    Require(std::isfinite(p.radius)&&p.radius>=0&&p.radius<=4 && std::isfinite(p.height)&&p.height>.1f&&p.height<=10 &&
        p.cellSize==.1f && p.cellHeight==.05f && (p.tileCells==32||p.tileCells==64||p.tileCells==96||p.tileCells==128) && ((p.movement==Movement::Ground&&p.slope==40)||(Levitates(p)&&p.slope==60)||(Flies(p)&&p.slope==90)) && p.climb==.35f,"Nav64: unsupported build profile");
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
void ShuffleTile(const unsigned char* data,size_t size,bool encode,unsigned char* result) {
    const auto h=CheckTileHeader(data,size);
    size_t offset=dtAlign4(sizeof(dtMeshHeader));std::memcpy(result,data,offset);
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
}
void CheckTile(const unsigned char* data,size_t size) {
    const auto h=CheckTileHeader(data,size);
    const auto* vertices=reinterpret_cast<const float*>(data+dtAlign4(sizeof(dtMeshHeader)));
    for(int i=0;i<h.vertCount*3;++i)Require(std::isfinite(vertices[i]),"Nav64: invalid tile vertex");
    size_t offset=dtAlign4(sizeof(dtMeshHeader))+dtAlign4(sizeof(float)*3*h.vertCount);
    const auto* polys=reinterpret_cast<const dtPoly*>(data+offset);
    for(int i=0;i<h.polyCount;++i) {
        Require(polys[i].vertCount>=3 && polys[i].vertCount<=DT_VERTS_PER_POLYGON && polys[i].getType()==DT_POLYTYPE_GROUND&&
            polys[i].getArea()>0&&polys[i].getArea()<16&&(polys[i].flags&15)>0&&polys[i].flags<=4095&&
            ((polys[i].flags&15)&((polys[i].flags&15)+1))==0,"Nav64: invalid polygon");
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
    unsigned material; float radius; int x,z; bool printing; bool multipleOutlines=false; std::string failure;
    BakeContext(const Profile& p,int tx,int tz,bool logging):rcContext(true),material(p.materialClass),radius(p.radius),x(tx),z(tz),printing(logging) {}
    void doLog(const rcLogCategory category,const char* message,const int length) override {
        if(category==RC_LOG_ERROR && (std::string(message,length).find("rcBuildContours: Multiple outlines for region ")==0||
            std::string(message,length).find("rcBuildContours: Bad outline for region ")==0)) {
            multipleOutlines=true;return;
        }
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
static std::unique_ptr<Mesh> BuildProfile(const Geometry& g,const Profile& p,const std::string& key,RasterBatch* batch,unsigned workerLimit,int onlyX=-1,int onlyZ=-1,const std::vector<Point>& columns={},bool flightReady=false) {
    if(Flies(p)&&!flightReady) {
        auto sky=FlightGeometry(g,p,onlyX,onlyZ);
        return BuildProfile(sky,p,key,nullptr,workerLimit,onlyX,onlyZ,columns,true);
    }
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
    struct TileInput {std::vector<int> triangles,solids,ceilings;};
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
    // Material permissions only restrict geometry; they never override slope.
    for(size_t i=0;i<allAreas.size();++i)if(slopes[i]==RC_NULL_AREA)allAreas[i]=RC_NULL_AREA;
    for(const auto& s:g.solids) {
        SolidInput input;input.hull=Footprint(s,input.low,input.high);
        if(input.hull.size()<9)continue;
        float minx=input.hull[0],maxx=minx,minz=input.hull[2],maxz=minz;
        for(size_t i=0;i<input.hull.size();i+=3){minx=std::min(minx,input.hull[i]);maxx=std::max(maxx,input.hull[i]);minz=std::min(minz,input.hull[i+2]);maxz=std::max(maxz,input.hull[i+2]);}
        bin(minx,minz,maxx,maxz,int(solids.size()),true);solids.push_back(std::move(input));
    }
    for(size_t i=0;i<g.ceilings.size();++i) {
        const auto& c=g.ceilings[i];
        const int x0=std::max(0,int(std::floor((c.x*.3f-g.minimum.x-border)/tileSize))),
            x1=std::min(nx-1,int(std::floor(((c.x+int(c.width))*.3f-g.minimum.x+border)/tileSize))),
            z0=std::max(0,int(std::floor((-(c.y+int(c.height))*.3f-g.minimum.z-border)/tileSize))),
            z1=std::min(nz-1,int(std::floor((-c.y*.3f-g.minimum.z+border)/tileSize)));
        for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)inputs[z*nx+x].ceilings.push_back(int(i));
    }
    // Workers own all Recast state and output blobs. Only the simulation thread
    // mutates Detour, in fixed z/x order, so cache bytes/references stay stable.
    struct TileBlob {unsigned char* data=nullptr;int size=0;std::vector<std::vector<std::uint64_t>> surfaces;~TileBlob(){dtFree(data);}};
    std::vector<std::unique_ptr<TileBlob>> tiles(nx*nz);std::vector<std::exception_ptr> errors(nx*nz);
    std::atomic<int> next{0};std::atomic<bool> failed{false};
    std::atomic<std::uint64_t> rasterUs{0},polygonUs{0};
    auto micros=[](auto begin){return std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count());};
    auto tile=[&](int index) {
        const int x=index%nx,z=index/nx;
        if((onlyX>=0&&(x!=onlyX||z!=onlyZ))||inputs[index].triangles.empty())return;
        BakeContext context(p,x,z,onlyX<0);auto begin=std::chrono::steady_clock::now();
        rcConfig cfg{};cfg.cs=p.cellSize;cfg.ch=p.cellHeight;cfg.walkableSlopeAngle=p.slope;
        cfg.walkableHeight=int(std::ceil(p.height/cfg.ch));cfg.walkableClimb=Flies(p)?int(std::ceil((g.maximum.y-g.minimum.y+4)/cfg.ch)):int(std::floor(p.climb/cfg.ch));cfg.walkableRadius=Flies(p)?0:int(std::ceil(p.radius/cfg.cs));
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
        Require(rcRasterizeTriangles(&context,vertices.data(),int(vertices.size()/3),indices.data(),areas.data(),int(areas.size()),*field,0),"Nav64: ground rasterization failed");
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
        // Flight clearance is already in the upper envelope. Ground ledge and
        // footprint erosion would remove valid sky beside the map boundary.
        if(!Flies(p))rcFilterLedgeSpans(&context,cfg.walkableHeight,cfg.walkableClimb,*r.height);
        rcFilterWalkableLowHeightSpans(&context,cfg.walkableHeight,*r.height);
        r.compact=rcAllocCompactHeightfield();Require(r.compact&&rcBuildCompactHeightfield(&context,cfg.walkableHeight,cfg.walkableClimb,*r.height,*r.compact),"Nav64: compact heightfield failed");
        rasterUs.fetch_add(micros(begin));begin=std::chrono::steady_clock::now();
        for(const auto& door:g.doors) {
            const auto a=ToNav(door.minimum),b=ToNav(door.maximum);
            const float low[]={std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};
            const float high[]={std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};
            rcMarkBoxArea(&context,low,high,15,*r.compact);
        }
        std::array<std::pair<unsigned char,unsigned short>,64> permissions{};
        unsigned permissionCount=0;
        std::vector<unsigned short> forbidden(r.compact->spanCount,0);
        auto& sourceField=*r.compact;
        if(!Levitates(p))for(int ti:inputs[index].triangles) {
            const auto& t=g.ground[ti];if(!t.terrain)continue;
            unsigned short flags=0;
            for(unsigned c=1;c<8;++c)if(!t.classAreas[c])flags|=ExcludeClass(c);
            if(!flags)continue;
            const auto a=ToNav(t.a),b=ToNav(t.b),c=ToNav(t.c);
            const float dx=b.x-a.x,dz=b.z-a.z,ex=c.x-a.x,ez=c.z-a.z,det=dx*ez-dz*ex;
            if(std::abs(det)<1.e-8f)continue;
            const int x0=std::max(0,int(std::floor((std::min({a.x,b.x,c.x})-cfg.bmin[0])/cfg.cs))),
                x1=std::min(sourceField.width-1,int(std::floor((std::max({a.x,b.x,c.x})-cfg.bmin[0])/cfg.cs))),
                z0=std::max(0,int(std::floor((std::min({a.z,b.z,c.z})-cfg.bmin[2])/cfg.cs))),
                z1=std::min(sourceField.height-1,int(std::floor((std::max({a.z,b.z,c.z})-cfg.bmin[2])/cfg.cs)));
            for(int iz=z0;iz<=z1;++iz)for(int ix=x0;ix<=x1;++ix) {
                std::vector<Point> clipped={a,b,c};
                auto clip=[&](bool xaxis,float edge,bool greater) {
                    std::vector<Point> out;if(clipped.empty())return;
                    auto previous=clipped.back();float pv=(xaxis?previous.x:previous.z)-edge;
                    for(auto current:clipped) {
                        const float cv=(xaxis?current.x:current.z)-edge;
                        if((pv>=0)!=(cv>=0)) {const float f=pv/(pv-cv);out.push_back({previous.x+(current.x-previous.x)*f,previous.y+(current.y-previous.y)*f,previous.z+(current.z-previous.z)*f});}
                        if(greater?cv>=0:cv<=0)out.push_back(current);
                        previous=current;pv=cv;
                    }
                    clipped=std::move(out);
                };
                clip(true,cfg.bmin[0]+ix*cfg.cs,true);clip(true,cfg.bmin[0]+(ix+1)*cfg.cs,false);
                clip(false,cfg.bmin[2]+iz*cfg.cs,true);clip(false,cfg.bmin[2]+(iz+1)*cfg.cs,false);
                if(clipped.size()<3)continue;float top=-FLT_MAX;for(auto point:clipped)top=std::max(top,point.y);
                const auto& cell=sourceField.cells[ix+iz*sourceField.width];
                for(unsigned j=cell.index;j<cell.index+cell.count;++j)
                    if(std::abs(cfg.bmin[1]+sourceField.spans[j].y*cfg.ch-top)<=cfg.ch*1.01f)forbidden[j]|=flags;
            }
        }
        // Recover raster provenance before contours discard the source identity.
        // Match the clipped triangle's raster top, never a Detour polygon Z.
        auto& field=*r.compact;
        std::vector<std::uint64_t> support(field.spanCount,1);
        bool layered=false;
        for(int ti:inputs[index].triangles)if(allAreas[ti]&&g.ground[ti].surface>1){layered=true;break;}
        if(layered) {
            std::fill(support.begin(),support.end(),0);
            std::vector<float> gaps(field.spanCount,1.e30f);
            for(int ti:inputs[index].triangles)if(allAreas[ti]&&g.ground[ti].surface) {
                const auto t=Surface(g.ground[ti],p);const Point a=ToNav(t.a),b=ToNav(t.b),c=ToNav(t.c);
                const int x0=std::max(0,int(std::floor((std::min({a.x,b.x,c.x})-cfg.bmin[0])/cfg.cs))),
                    x1=std::min(field.width-1,int(std::floor((std::max({a.x,b.x,c.x})-cfg.bmin[0])/cfg.cs))),
                    z0=std::max(0,int(std::floor((std::min({a.z,b.z,c.z})-cfg.bmin[2])/cfg.cs))),
                    z1=std::min(field.height-1,int(std::floor((std::max({a.z,b.z,c.z})-cfg.bmin[2])/cfg.cs)));
                for(int iz=z0;iz<=z1;++iz)for(int ix=x0;ix<=x1;++ix) {
                    std::vector<Point> clipped={a,b,c};
                    auto clip=[&](bool xaxis,float edge,bool greater) {
                        std::vector<Point> out;if(clipped.empty())return;
                        auto previous=clipped.back();float pv=(xaxis?previous.x:previous.z)-edge;
                        for(auto current:clipped) {
                            const float cv=(xaxis?current.x:current.z)-edge;
                            if((pv>=0)!=(cv>=0)) {const float f=pv/(pv-cv);out.push_back({previous.x+(current.x-previous.x)*f,previous.y+(current.y-previous.y)*f,previous.z+(current.z-previous.z)*f});}
                            if(greater?cv>=0:cv<=0)out.push_back(current);
                            previous=current;pv=cv;
                        }
                        clipped=std::move(out);
                    };
                    clip(true,cfg.bmin[0]+ix*cfg.cs,true);clip(true,cfg.bmin[0]+(ix+1)*cfg.cs,false);
                    clip(false,cfg.bmin[2]+iz*cfg.cs,true);clip(false,cfg.bmin[2]+(iz+1)*cfg.cs,false);
                    if(clipped.size()<3)continue;float top=-1.e30f;for(auto v:clipped)top=std::max(top,v.y);
                    const auto& cell=field.cells[ix+iz*field.width];
                    for(unsigned j=cell.index;j<cell.index+cell.count;++j) {
                        const float gap=std::abs(cfg.bmin[1]+field.spans[j].y*cfg.ch-top);
                        if(gap<=cfg.ch*1.01f&&gap<gaps[j]){gaps[j]=gap;support[j]=t.surface;}
                    }
                }
            }
            for(int j=0;j<field.spanCount;++j)if(!support[j])field.areas[j]=RC_NULL_AREA;
        }
        if(layered) {
            // A thin raised slab can merge with sloping ground in one raster
            // column. Its voxel top then extends past the real slab edge.
            // Remove only those mixed edge columns before radius erosion;
            // separate terrain/floor layers are retained unchanged.
            std::map<std::uint64_t,std::vector<int>> planes;
            for(int ti:inputs[index].triangles)if((allAreas[ti]&&g.ground[ti].surface)||g.ground[ti].terrain)planes[g.ground[ti].surface].push_back(ti);
            auto heightAt=[&](std::uint64_t surface,float x,float y) {
                float result=-FLT_MAX;
                for(int ti:planes.at(surface)) {
                    const auto& t=g.ground[ti];
                    const float ax=t.b.x-t.a.x,ay=t.b.y-t.a.y,bx=t.c.x-t.a.x,by=t.c.y-t.a.y,det=ax*by-ay*bx;
                    if(det<=1.e-8f)continue;
                    const float u=((x-t.a.x)*by-(y-t.a.y)*bx)/det,v=(ax*(y-t.a.y)-ay*(x-t.a.x))/det;
                    if(u>=-.00001f&&v>=-.00001f&&u+v<=1.00001f)result=std::max(result,t.a.z+u*(t.b.z-t.a.z)+v*(t.c.z-t.a.z));
                }
                return result;
            };
            if(planes.count(1))for(int iz=0;iz<field.height;++iz)for(int ix=0;ix<field.width;++ix) {
                const auto& cell=field.cells[ix+iz*field.width];
                for(unsigned j=cell.index;j<cell.index+cell.count;++j)if(field.areas[j]&&support[j]>1) {
                    auto* raw=r.height->spans[ix+iz*field.width];
                    while(raw&&raw->smax!=field.spans[j].y)raw=raw->next;
                    if(!raw)continue;
                    const float bottom=cfg.bmin[1]+raw->smin*cfg.ch,top=cfg.bmin[1]+raw->smax*cfg.ch;
                    bool cliff=false;
                    for(float dz:{.05f,.5f,.95f})for(float dx:{.05f,.5f,.95f}) {
                        const float x=cfg.bmin[0]+(ix+dx)*cfg.cs,y=-(cfg.bmin[2]+(iz+dz)*cfg.cs);
                        if(heightAt(support[j],x,y)>-FLT_MAX)continue;
                        const float ground=heightAt(1,x,y);
                        if(ground>=bottom-cfg.ch&&ground<top-p.climb-.001f)cliff=true;
                    }
                    if(cliff)field.areas[j]=RC_NULL_AREA;
                }
            }
        }
        // Canopies only cap existing floor spans. They cannot create extra
        // floors, ledges or regions at their upper side. Above the tallest
        // pose plus one climb they do not affect this profile at all.
        for(int ci:inputs[index].ceilings) {
            const auto& c=g.ceilings[ci];auto& chf=*r.compact;
            for(unsigned gy=0;gy<c.height;++gy)for(unsigned gx=0;gx<c.width;++gx) {
                const auto h=c.heights[size_t(gy)*c.width+gx];if(h==0xffff)continue;
                const float lowX=(c.x+int(gx))*.3f,highX=(c.x+int(gx)+1)*.3f,
                    lowZ=-(c.y+int(gy)+1)*.3f,highZ=-(c.y+int(gy))*.3f;
                const int x0=std::max(0,int(std::floor((lowX-cfg.bmin[0])/cfg.cs))),x1=std::min(chf.width-1,int(std::floor((highX-cfg.bmin[0])/cfg.cs))),
                    z0=std::max(0,int(std::floor((lowZ-cfg.bmin[2])/cfg.cs))),z1=std::min(chf.height-1,int(std::floor((highZ-cfg.bmin[2])/cfg.cs)));
                const int ceiling=int(std::floor((c.base+h*.05f-cfg.bmin[1])/cfg.ch));
                const int maximum=int(std::ceil(ClearanceHeights(p).back()/cfg.ch))+cfg.walkableClimb;
                for(int iz=z0;iz<=z1;++iz)for(int ix=x0;ix<=x1;++ix) {
                    // A shared boundary has no covered area. Inclusive source
                    // bounds must not lower the next row/column of free cells.
                    const float cellX=cfg.bmin[0]+ix*cfg.cs,cellZ=cfg.bmin[2]+iz*cfg.cs;
                    if(std::min(highX,cellX+cfg.cs)-std::max(lowX,cellX)<=cfg.cs*.001f||
                        std::min(highZ,cellZ+cfg.cs)-std::max(lowZ,cellZ)<=cfg.cs*.001f)continue;
                    const auto& cell=chf.cells[ix+iz*chf.width];
                    for(unsigned j=cell.index;j<cell.index+cell.count;++j) {
                        auto& span=chf.spans[j];const int free=ceiling-int(span.y);
                        if(free<0||free>=maximum)continue;
                        span.h=static_cast<unsigned char>(std::min(int(span.h),free));
                        if(free<cfg.walkableHeight)chf.areas[j]=RC_NULL_AREA;
                    }
                }
            }
        }
        if(PoseClearance(p)) {
            auto& chf=*r.compact;
            const std::vector<unsigned char> material(chf.areas,chf.areas+chf.spanCount);
            // A terrain policy never applies to a separate BASE above it.
            for(int j=0;j<chf.spanCount;++j)if(support[j]!=1)forbidden[j]=0;
            const auto originalForbidden=forbidden;
            // Erosion distributes over intersection: physical pose clearance
            // and class-permitted terrain are eroded independently, then both
            // flags must pass. This preserves all eight shared-radius classes
            // without multiplying meshes or Recast's 63 material/pose areas.
            if(!Levitates(p))for(unsigned c=1;c<8;++c) {
                const auto flag=ExcludeClass(c);
                if(std::none_of(originalForbidden.begin(),originalForbidden.end(),[&](unsigned short f){return (f&flag)!=0;}))continue;
                for(int j=0;j<chf.spanCount;++j)chf.areas[j]=(originalForbidden[j]&flag)?RC_NULL_AREA:material[j];
                Require(rcErodeWalkableArea(&context,cfg.walkableRadius,chf),"Nav64: class radius erosion failed");
                for(int j=0;j<chf.spanCount;++j)if(material[j]&&!chf.areas[j])forbidden[j]|=flag;
            }
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
            for(int j=0;j<chf.spanCount;++j) {
                if(!tiers[j]){chf.areas[j]=RC_NULL_AREA;continue;}
                const auto value=std::make_pair(material[j],static_cast<unsigned short>((1u<<tiers[j])-1));
                unsigned id=1;for(;id<=permissionCount&&permissions[id]!=value;++id){}
                Require(id<permissions.size(),"Nav64: material/clearance area limit");
                if(id>permissionCount){permissionCount=id;permissions[id]=value;}
                chf.areas[j]=static_cast<unsigned char>(id);
            }
        } else Require(rcErodeWalkableArea(&context,cfg.walkableRadius,*r.compact),"Nav64: radius erosion failed");
        for(auto point:columns) {
            const auto nav=ToNav(point);
            if(int(std::floor((nav.x-g.minimum.x)/tileSize))!=x||int(std::floor((nav.z-g.minimum.z)/tileSize))!=z)continue;
            const int ix=int(std::floor((nav.x-cfg.bmin[0])/cfg.cs)),iz=int(std::floor((nav.z-cfg.bmin[2])/cfg.cs));
            for(auto* span=r.height->spans[ix+iz*cfg.width];span;span=span->next)
                fprintf(stderr,"BAKE_COLUMN_RAW class=%u radius=%.2f tile=%d,%d xy=%.6f,%.6f cell=%d,%d min=%.6f max=%.6f area=%u\n",p.materialClass,p.radius,x,z,point.x,point.y,ix,iz,cfg.bmin[1]+span->smin*cfg.ch,cfg.bmin[1]+span->smax*cfg.ch,unsigned(span->area));
            const auto& cell=field.cells[ix+iz*field.width];
            for(unsigned j=cell.index;j<cell.index+cell.count;++j) {
                const auto area=field.areas[j];const unsigned flags=area?(permissions[area].second|forbidden[j]):0;
                fprintf(stderr,"BAKE_COLUMN_COMPACT class=%u radius=%.2f tile=%d,%d xy=%.6f,%.6f floor=%.6f clearance=%.6f flags=%u surface=%llu\n",p.materialClass,p.radius,x,z,point.x,point.y,cfg.bmin[1]+field.spans[j].y*cfg.ch,field.spans[j].h*cfg.ch,flags,static_cast<unsigned long long>(support[j]));
            }
        }
        std::vector<std::uint64_t> regionSupport;
        std::vector<unsigned short> regionForbidden;
        std::vector<unsigned char> regionArea;
        auto splitSupports=[&] {
            std::vector<unsigned short> old(field.spanCount);for(int j=0;j<field.spanCount;++j)old[j]=field.spans[j].reg;
            std::vector<bool> seen(field.spanCount,false);std::vector<std::tuple<int,int,unsigned>> stack;
            unsigned short next=0;regionSupport.assign(1,0);regionForbidden.assign(1,0);regionArea.assign(1,0);
            for(int iz=0;iz<field.height;++iz)for(int ix=0;ix<field.width;++ix) {
                const auto& cell=field.cells[ix+iz*field.width];
                for(unsigned j=cell.index;j<cell.index+cell.count;++j)if(!seen[j]&&old[j]&&!(old[j]&RC_BORDER_REG)) {
                    Require(next+1<RC_BORDER_REG,"Nav64: surface region limit");++next;regionSupport.push_back(support[j]);regionForbidden.push_back(forbidden[j]);regionArea.push_back(field.areas[j]);
                    seen[j]=true;stack.emplace_back(ix,iz,j);
                    while(!stack.empty()) {
                        const auto [cx,cz,k]=stack.back();stack.pop_back();auto& span=field.spans[k];span.reg=next;
                        for(int d=0;d<4;++d)if(rcGetCon(span,d)!=RC_NOT_CONNECTED) {
                            const int nx=cx+rcGetDirOffsetX(d),nz=cz+rcGetDirOffsetY(d);
                            const unsigned n=field.cells[nx+nz*field.width].index+rcGetCon(span,d);
                            if(!seen[n]&&old[n]==old[j]&&support[n]==support[j]&&forbidden[n]==forbidden[j]&&field.areas[n]==field.areas[j]){seen[n]=true;stack.emplace_back(nx,nz,n);}
                        }
                    }
                }
            }
            field.maxRegions=next;
        };
        Require(rcBuildRegionsMonotone(&context,field,cfg.borderSize,cfg.minRegionArea,cfg.mergeRegionArea),"Nav64: region build failed");
        splitSupports();
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
        const bool classBoundaries=std::any_of(forbidden.begin(),forbidden.end(),[](unsigned short flags){return flags!=0;});
        cfg.maxSimplificationError=(layered||classBoundaries)?0.f:holes?.25f:1.f;
        auto buildContours=[&](float error) {
            // Same-area region borders are simplified without an error test
            // even at maxError=0. Temporary adjacent-region colors preserve
            // class/floor boundaries; stored materials are restored afterwards.
            std::map<unsigned short,std::set<unsigned short>> adjacent;
            for(int iz=0;iz<field.height;++iz)for(int ix=0;ix<field.width;++ix) {
                const auto& cell=field.cells[ix+iz*field.width];
                for(unsigned j=cell.index;j<cell.index+cell.count;++j) {
                    const auto& span=field.spans[j];if(!span.reg||!field.areas[j])continue;
                    adjacent[span.reg];
                    for(int d=0;d<4;++d)if(rcGetCon(span,d)!=RC_NOT_CONNECTED) {
                        const int nx=ix+rcGetDirOffsetX(d),nz=iz+rcGetDirOffsetY(d);
                        const auto n=field.cells[nx+nz*field.width].index+rcGetCon(span,d);
                        const auto other=field.spans[n].reg;
                        if(other&&other!=span.reg&&field.areas[n]){adjacent[span.reg].insert(other);adjacent[other].insert(span.reg);}
                    }
                }
            }
            std::vector<unsigned short> order;for(const auto& entry:adjacent)order.push_back(entry.first);
            std::sort(order.begin(),order.end(),[&](auto a,auto b){return adjacent[a].size()!=adjacent[b].size()?adjacent[a].size()>adjacent[b].size():a<b;});
            std::map<unsigned short,unsigned char> color;
            for(auto region:order) {
                bool used[64]{};for(auto neighbour:adjacent[region]){auto c=color.find(neighbour);if(c!=color.end())used[c->second]=true;}
                unsigned id=1;while(id<64&&used[id])++id;
                Require(id<64,"Nav64: contour topology color limit");color[region]=static_cast<unsigned char>(id);
            }
            const std::vector<unsigned char> areas(field.areas,field.areas+field.spanCount);
            for(int j=0;j<field.spanCount;++j)if(field.spans[j].reg&&field.areas[j])field.areas[j]=color.at(field.spans[j].reg);
            const bool built=rcBuildContours(&context,field,error,cfg.maxEdgeLen,*r.contours);
            std::copy(areas.begin(),areas.end(),field.areas);
            if(built)for(int i=0;i<r.contours->nconts;++i) {
                auto& contour=r.contours->conts[i];
                Require(contour.reg<regionArea.size(),"Nav64: contour has no material policy");contour.area=regionArea[contour.reg];
            }
            return built;
        };
        r.contours=rcAllocContourSet();
        Require(r.contours&&buildContours(cfg.maxSimplificationError),"Nav64: contour build failed");
        auto exactContours=[&] {
            // Monotone's merge pass can join sweeps around a narrow hole, and
            // even a quarter-cell error can collapse its contour. Select an
            // exact monotone contour for this tile only; do not accept the
            // ambiguous contour set returned by the first pass.
            context.multipleOutlines=false;
            rcFreeContourSet(r.contours);r.contours=nullptr;
            Require(rcBuildRegionsMonotone(&context,*r.compact,cfg.borderSize,cfg.minRegionArea,0),"Nav64: exact monotone region build failed");
            splitSupports();
            // Recast's final merge is also applied when mergeRegionArea is
            // zero (interior regions still merge). Partition this exceptional
            // tile into exact connected rectangles. Equal adjacent row runs
            // coalesce without changing cells, floor identity or permissions.
            auto& field=*r.compact;
            std::vector<unsigned short> regions(field.spanCount);
            for(int i=0;i<field.spanCount;++i)regions[i]=field.spans[i].reg;
            unsigned short nextRegion=0;regionSupport.assign(1,0);regionForbidden.assign(1,0);regionArea.assign(1,0);
            struct Run {int x0,x1;unsigned short original;unsigned char area;std::vector<unsigned> spans;};
            using RunKey=std::tuple<int,int,unsigned short,unsigned char>;
            std::map<RunKey,unsigned short> previous;
            std::vector<unsigned> runIndex(field.spanCount,0);
            for(int iz=0;iz<field.height;++iz) {
                std::vector<Run> runs;
                for(int ix=0;ix<field.width;++ix) {
                    const auto& cell=field.cells[ix+iz*field.width];
                    for(unsigned j=cell.index;j<cell.index+cell.count;++j) {
                        const auto& span=field.spans[j];const auto original=regions[j];
                        if(!original||(original&RC_BORDER_REG))continue;
                        unsigned left=0;
                        if(ix>0&&rcGetCon(span,0)!=RC_NOT_CONNECTED) {
                            const auto k=field.cells[ix-1+iz*field.width].index+rcGetCon(span,0);
                            if(regions[k]==original&&field.areas[k]==field.areas[j])left=runIndex[k];
                        }
                        if(!left){runs.push_back({ix,ix+1,original,field.areas[j],{}});left=unsigned(runs.size());}
                        runIndex[j]=left;auto& run=runs[left-1];run.x1=ix+1;run.spans.push_back(j);
                    }
                }
                std::map<RunKey,unsigned short> current;
                for(const auto& run:runs) {
                    const RunKey key{run.x0,run.x1,run.original,run.area};
                    const auto found=previous.find(key);unsigned short region=found==previous.end()?0:found->second;
                    if(region)for(unsigned j:run.spans) {
                        const auto& span=field.spans[j];
                        if(rcGetCon(span,3)==RC_NOT_CONNECTED){region=0;break;}
                    }
                    // Equal row intervals may share one rectangle only when
                    // every cell connects to that same rectangle above it.
                    if(region)for(size_t k=0;k<run.spans.size();++k) {
                        const auto& span=field.spans[run.spans[k]];
                        const auto above=field.cells[run.x0+int(k)+(iz-1)*field.width].index+rcGetCon(span,3);
                        if(field.spans[above].reg!=region){region=0;break;}
                    }
                    if(!region) {
                        Require(nextRegion+1<RC_BORDER_REG,"Nav64: exact rectangle region limit");
                        region=++nextRegion;regionSupport.push_back(support[run.spans.front()]);regionForbidden.push_back(forbidden[run.spans.front()]);regionArea.push_back(field.areas[run.spans.front()]);
                    }
                    for(unsigned j:run.spans)field.spans[j].reg=region;
                    current.emplace(key,region);
                }
                previous=std::move(current);
            }
            field.maxRegions=nextRegion;
            r.contours=rcAllocContourSet();
            Require(r.contours&&buildContours(0.f),"Nav64: exact monotone contour build failed");
            if(context.printing)fprintf(stderr,"MONOTONE_TILE class=%u radius=%.2f tile=%d,%d exact=1\n",p.materialClass,p.radius,x,z);
        };
        if(context.multipleOutlines&&context.failure.empty())exactContours();
        if(context.multipleOutlines) {
            const auto reason="Nav64: multiple outlines remain after monotone strips tile="+std::to_string(x)+","+std::to_string(z);
            if(context.printing)fprintf(stderr,"RECAST class=%u radius=%.2f tile=%d,%d reason=%s\n",p.materialClass,p.radius,x,z,reason.c_str());
            throw std::runtime_error(reason);
        }
        if(!context.failure.empty())throw std::runtime_error("Nav64: Recast tile="+std::to_string(x)+","+std::to_string(z)+": "+context.failure);
        // Keep border vertices: removing one can merge different source regions
        // into RC_MULTIPLE_REGS and lose the exact floor identity.
        for(int i=0;i<r.contours->nconts;++i)for(int j=0;j<r.contours->conts[i].nverts;++j)
            r.contours->conts[i].verts[j*4+3]&=~RC_BORDER_VERTEX;
        r.polys=rcAllocPolyMesh();
        Require(r.polys&&rcBuildPolyMesh(&context,*r.contours,cfg.maxVertsPerPoly,*r.polys),"Nav64: polygon build failed");
        if(!context.failure.empty())throw std::runtime_error("Nav64: Recast tile="+std::to_string(x)+","+std::to_string(z)+": "+context.failure);
        auto validPolygons=[](const rcPolyMesh& mesh) {
            for(int i=0;i<mesh.npolys;++i) {
                const auto* poly=mesh.polys+i*mesh.nvp*2;int count=0;
                while(count<mesh.nvp&&poly[count]!=RC_MESH_NULL_IDX)++count;
                if(count<3)return false;
                std::int64_t area=0;
                for(int j=0;j<count;++j) {
                    const auto* a=mesh.verts+poly[j]*3;const auto* b=mesh.verts+poly[(j+1)%count]*3;const auto* c=mesh.verts+poly[(j+2)%count]*3;
                    area+=std::int64_t(a[0])*b[2]-std::int64_t(b[0])*a[2];
                    if((std::int64_t(b[0])-a[0])*(std::int64_t(c[2])-b[2])-(std::int64_t(b[2])-a[2])*(std::int64_t(c[0])-b[0])>0)return false;
                }
                if(area>=0)return false; // Detour requires clockwise convex XZ.
            }
            return true;
        };
        if(!validPolygons(*r.polys)) {
            // A simplified narrow contour can triangulate to a reversed face.
            // Its links remain connected but the funnel walks outside the
            // polygon. Repartition the actual compact cells as exact row runs;
            // never reverse overlapping faces or open a hole in the mesh.
            exactContours();
            for(int i=0;i<r.contours->nconts;++i)for(int j=0;j<r.contours->conts[i].nverts;++j)r.contours->conts[i].verts[j*4+3]&=~RC_BORDER_VERTEX;
            rcFreePolyMesh(r.polys);r.polys=rcAllocPolyMesh();
            Require(r.polys&&rcBuildPolyMesh(&context,*r.contours,cfg.maxVertsPerPoly,*r.polys),"Nav64: exact polygon build failed");
            Require(context.failure.empty()&&!context.multipleOutlines&&validPolygons(*r.polys),"Nav64: nonconvex/reversed polygon after exact row partition");
            if(context.printing)fprintf(stderr,"POLYGON_STRIPS class=%u radius=%.2f tile=%d,%d exact=1\n",p.materialClass,p.radius,x,z);
        }
        auto& poly=*r.polys;if(!poly.npolys)return;
        for(int i=0;i<poly.npolys;++i) {
            const unsigned encoded=poly.areas[i];
            Require(encoded>0&&encoded<=permissionCount,"Nav64: invalid clearance/class area");
            Require(poly.regs[i]<regionForbidden.size()&&poly.regs[i]>0,"Nav64: polygon has no class policy");
            poly.flags[i]=permissions[encoded].second|regionForbidden[poly.regs[i]];
            poly.areas[i]=permissions[encoded].first;
        }
        dtNavMeshCreateParams create{};
        create.verts=poly.verts;create.vertCount=poly.nverts;create.polys=poly.polys;
        create.polyAreas=poly.areas;create.polyFlags=poly.flags;create.polyCount=poly.npolys;create.nvp=poly.nvp;
        if(Flies(p)) {
            r.detail=rcAllocPolyMeshDetail();
            Require(r.detail&&rcBuildPolyMeshDetail(&context,poly,*r.compact,.5f,.05f,*r.detail),"Nav64: flight detail mesh failed");
            create.detailMeshes=r.detail->meshes;create.detailVerts=r.detail->verts;create.detailVertsCount=r.detail->nverts;
            create.detailTris=r.detail->tris;create.detailTriCount=r.detail->ntris;
        }
        create.walkableHeight=p.height;create.walkableRadius=p.radius;create.walkableClimb=p.climb;
        create.tileX=x;create.tileY=z;create.tileLayer=0;
        rcVcopy(create.bmin,poly.bmin);rcVcopy(create.bmax,poly.bmax);
        create.cs=cfg.cs;create.ch=cfg.ch;create.buildBvTree=true;
        // Detour emits polygon fans without a detail mesh. Game support supplies Z.
        auto blob=std::make_unique<TileBlob>();
        // Voxel boundaries can straddle two exact source surfaces. Retain only
        // supports joined by a real compact-field edge in this region; never
        // admit arbitrary overlapping floors or a solid object's top.
        std::vector<std::set<std::uint64_t>> neighbours(regionSupport.size());
        for(size_t i=1;i<regionSupport.size();++i)neighbours[i].insert(regionSupport[i]);
        for(int iz=0;iz<field.height;++iz)for(int ix=0;ix<field.width;++ix) {
            const auto& cell=field.cells[ix+iz*field.width];
            for(unsigned j=cell.index;j<cell.index+cell.count;++j) {
                const auto& span=field.spans[j];if(!span.reg||(span.reg&RC_BORDER_REG))continue;
                for(int d=0;d<4;++d)if(rcGetCon(span,d)!=RC_NOT_CONNECTED) {
                    const int nx=ix+rcGetDirOffsetX(d),nz=iz+rcGetDirOffsetY(d);
                    const auto n=field.cells[nx+nz*field.width].index+rcGetCon(span,d);
                    if(field.areas[n]&&support[n])neighbours[span.reg].insert(support[n]);
                }
            }
        }
        for(int i=0;i<poly.npolys;++i) {
            Require(poly.regs[i]<regionSupport.size()&&regionSupport[poly.regs[i]],"Nav64: polygon has no original support");
            const auto primary=regionSupport[poly.regs[i]];const auto& group=neighbours[poly.regs[i]];
            blob->surfaces.push_back({primary});for(auto id:group)if(id!=primary)blob->surfaces.back().push_back(id);
        }
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
    for(size_t index=0;index<tiles.size();++index)if(auto& blob=tiles[index]) {
        mesh->polygonSurfaces[{int(index)%nx,int(index)/nx}]=std::move(blob->surfaces);
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
            try {return BuildProfile(g,request.profile,"",&batch,workers,-1,-1,request.columns);}
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
        Bytes shuffled(raw.size());ShuffleTile(raw.data(),raw.size(),true,shuffled.data());
        tiles.U(std::uint32_t(shuffled.size()));tiles.B(shuffled.data(),shuffled.size());
        const auto& support=mesh.polygonSurfaces.at({t->header->x,t->header->y});
        Require(support.size()==size_t(t->header->polyCount),"Nav64: polygon support count");
        tiles.U(unsigned(support.size()));for(const auto& group:support){tiles.U(unsigned(group.size()));for(auto id:group){tiles.U(unsigned(id));tiles.U(unsigned(id>>32));}}
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
        const auto header=CheckTileHeader(tiles.bytes.data()+tiles.at,n);
        auto freeTile=[](unsigned char* p){dtFree(p);};
        std::unique_ptr<unsigned char,decltype(freeTile)> data(static_cast<unsigned char*>(dtAlloc(n,DT_ALLOC_PERM)),freeTile);
        Require(data!=nullptr,"Nav64: tile allocation failed");
        ShuffleTile(tiles.bytes.data()+tiles.at,n,false,data.get());CheckTile(data.get(),n);tiles.at+=n;
        const auto supports=tiles.U();
        Require(supports==unsigned(header.polyCount),"Nav64: polygon support count");
        auto& identities=mesh->polygonSurfaces[{header.x,header.y}];identities.reserve(supports);
        for(unsigned j=0;j<supports;++j) {
            const auto count=tiles.U();Require(count>0&&count<=1024,"Nav64: polygon support group limit");
            std::vector<std::uint64_t> group;group.reserve(count);
            for(unsigned k=0;k<count;++k){const auto low=tiles.U();const auto id=low|(std::uint64_t(tiles.U())<<32);Require(id>0,"Nav64: invalid polygon support");group.push_back(id);}
            identities.push_back(std::move(group));
        }
        auto s=mesh->data->addTile(data.get(),n,DT_TILE_FREE_DATA,0,nullptr);Require(dtStatusSucceed(s),"Nav64: invalid/duplicate tile");data.release();++mesh->tiles;
    }
    Require(tiles.at==tiles.bytes.size()&&w.at==end&&w.U()==2&&w.U()==8&&w.U()==1&&w.U()==2&&w.at==w.bytes.size(),"Nav64: invalid surface section");
    mesh->query.reset(dtAllocNavMeshQuery());Require(mesh->query&&dtStatusSucceed(mesh->query->init(mesh->data.get(),65535)),"Nav64: query allocation failed");
    return mesh;
}

Bytes StoreSources(const Geometry& g) {
    // Preserve the imported planes exactly. Centimetre rounding changes the
    // slope and even collapses thin BASE faces; it cannot define support Z.
    using Vertex=std::array<float,3>;
    auto vertex=[](Point p){return Vertex{p.x,p.y,p.z};};
    std::map<Vertex,unsigned> indices;std::vector<Vertex> vertices;
    for(const auto& t:g.ground)for(auto p:{t.a,t.b,t.c}) {
        const auto v=vertex(p);if(indices.emplace(v,unsigned(vertices.size())).second)vertices.push_back(v);
    }
    Wire w;w.U(0x4352534e);w.U(7);w.P(g.minimum);w.P(g.maximum);w.U(unsigned(vertices.size()));
    const auto vertexStart=w.bytes.size();
    for(const auto& v:vertices)for(auto c:v)w.F(c);
    SourceArray(w.bytes,vertexStart,vertices.size(),12);
    w.U(unsigned(g.ground.size()));const auto triangleStart=w.bytes.size();
    for(const auto& t:g.ground) {
        for(auto p:{t.a,t.b,t.c})w.U(indices.at(vertex(p)));
        w.P(t.waterDepth);
        w.bytes.push_back(t.area);w.bytes.push_back(t.levitateArea);w.bytes.push_back(t.terrain?1:0);
        for(auto area:t.classAreas)w.bytes.push_back(area);
        w.U(t.object);w.U(unsigned(t.surface));w.U(unsigned(t.surface>>32));
    }
    SourceArray(w.bytes,triangleStart,g.ground.size(),47);
    w.U(unsigned(g.ceilings.size()));
    for(const auto& c:g.ceilings) {
        w.U(c.object);w.U(c.x);w.U(c.y);w.U(c.width);w.U(c.height);w.F(c.base);
        Require(c.width&&c.height&&c.heights.size()==size_t(c.width)*c.height,"Nav64: invalid canopy grid");
        for(auto h:c.heights){w.bytes.push_back(std::uint8_t(h));w.bytes.push_back(std::uint8_t(h>>8));}
    }
    w.U(unsigned(g.flightBoxes.size()));
    for(const auto& box:g.flightBoxes){w.U(box.object);w.P(box.minimum);w.P(box.maximum);}
    w.U(unsigned(g.doors.size()));for(const auto& d:g.doors){w.U(d.object);w.U(d.openState);w.P(d.minimum);w.P(d.maximum);}
    w.U(unsigned(g.connections.size()));for(const auto& c:g.connections) {
        w.P(c.from);w.P(c.to);w.F(c.radius);w.U(unsigned(c.fromSurface));w.U(unsigned(c.fromSurface>>32));
        w.U(unsigned(c.toSurface));w.U(unsigned(c.toSurface>>32));w.U(c.flags);
    }
    w.F(g.tileWidth);
    auto catalogue=[&](const auto& index) {
        w.U(unsigned(index.size()));
        for(const auto& entry:index) {
            w.U(entry.first.first);w.U(entry.first.second);w.U(unsigned(entry.second.size()));
            for(auto triangle:entry.second)w.U(unsigned(triangle));
        }
    };
    catalogue(g.tileTriangles);catalogue(g.surfaceTriangles);catalogue(g.tileCeilings);
    w.U(unsigned(g.objectTiles.size()));
    for(const auto& entry:g.objectTiles) {
        w.U(entry.first);w.U(unsigned(entry.second.size()));for(auto tile:entry.second){w.U(tile.first);w.U(tile.second);}
    }
    // Four independent lossless blocks retain LZMS's small release data while
    // allowing parallel decode on entry. No geometry is imported or rebuilt.
    constexpr unsigned chunks=4;std::array<std::future<Bytes>,chunks> pending;
    for(unsigned i=0;i<chunks;++i)pending[i]=std::async(std::launch::async,[&,i] {
        const size_t a=w.bytes.size()*i/chunks,b=w.bytes.size()*(i+1)/chunks;
        return Pack(Bytes(w.bytes.begin()+a,w.bytes.begin()+b));
    });
    Wire out;out.U(0x4352534e);out.U(8);out.U(unsigned(w.bytes.size()));out.U(chunks);
    for(unsigned i=0;i<chunks;++i) {
        auto packed=pending[i].get();out.U(unsigned(w.bytes.size()*(i+1)/chunks-w.bytes.size()*i/chunks));
        out.U(unsigned(packed.size()));out.B(packed.data(),packed.size());
    }
    return std::move(out.bytes);
}
std::shared_ptr<Geometry> LoadSources(Bytes bytes,std::array<double,3>* stages) {
    const auto begin=std::chrono::steady_clock::now();double arrayMs=0;
    auto ms=[](auto from){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-from).count();};
    Wire in;in.bytes=std::move(bytes);Require(in.U()==0x4352534e&&in.U()==8,"Nav64: incompatible source container");
    const auto size=in.U(),count=in.U();Require(size>=4&&size<=512ull*1024*1024&&count==4,"Nav64: source block limits");
    Wire w;w.bytes.resize(size);std::array<std::future<void>,4> pending;size_t offset=0;
    for(unsigned i=0;i<count;++i) {
        const auto expanded=in.U(),packed=in.U();
        Require(expanded>=4&&expanded<=size-offset&&packed<=in.bytes.size()-in.at,"Nav64: invalid source block");
        const auto input=in.at,output=offset;in.at+=packed;offset+=expanded;
        pending[i]=std::async(std::launch::async,[&,input,output,expanded,packed] {
            Codec codec(false);SIZE_T actual=0;
            Require(Decompress(codec.handle,in.bytes.data()+input,packed,w.bytes.data()+output,expanded,&actual)!=FALSE&&actual==expanded,"Nav64: corrupt source block");
        });
    }
    Require(offset==size&&in.at==in.bytes.size(),"Nav64: source block size mismatch");
    for(auto& job:pending)job.get();const auto unpacked=std::chrono::steady_clock::now();
    const double unpackMs=ms(begin);
    Require(w.U()==0x4352534e&&w.U()==7,"Nav64: incompatible source section");
    auto g=std::make_shared<Geometry>();auto point=[&](){return Point{w.F(),w.F(),w.F()};};
    g->minimum=point();g->maximum=point();auto n=w.U();Require(n<12000000&&n<= (w.bytes.size()-w.at)/12,"Nav64: source vertex limit");
    const auto arrayStart=std::chrono::steady_clock::now();const SourceColumns<12> vertexColumns(w,n);
    std::vector<Point> vertices(n);
    SourceRows(n,[&](size_t first,size_t last){for(size_t i=first;i<last;++i)vertices[i]=vertexColumns.P(i,0);});
    n=w.U();Require(n<4000000,"Nav64: source triangle limit");g->ground.resize(n);
    const SourceColumns<47> triangleColumns(w,n);
    SourceRows(n,[&](size_t first,size_t last){for(size_t i=first;i<last;++i) {
        auto vertex=[&](size_t column){auto j=triangleColumns.U(i,column);Require(j<vertices.size(),"Nav64: invalid source index");return vertices[j];};
        auto& t=g->ground[i];t.a=vertex(0);t.b=vertex(4);t.c=vertex(8);t.waterDepth=triangleColumns.P(i,12);
        t.area=triangleColumns.B(i,24);t.levitateArea=triangleColumns.B(i,25);t.terrain=triangleColumns.B(i,26)!=0;
        for(size_t c=0;c<t.classAreas.size();++c)t.classAreas[c]=triangleColumns.B(i,27+c);
        t.object=int(triangleColumns.U(i,35));
        t.surface=triangleColumns.U(i,39)|(std::uint64_t(triangleColumns.U(i,43))<<32);
    }});
    arrayMs=ms(arrayStart);
    n=w.U();Require(n<=100000,"Nav64: canopy grid limit");g->ceilings.reserve(n);
    for(unsigned i=0;i<n;++i) {
        CeilingGrid c;c.object=int(w.U());c.x=int(w.U());c.y=int(w.U());c.width=w.U();c.height=w.U();c.base=w.F();
        const auto count=std::uint64_t(c.width)*c.height;
        Require(c.object>0&&c.width&&c.height&&count<=4000000&&count<=(w.bytes.size()-w.at)/2,"Nav64: invalid canopy grid");
        // Windows x64 uses the same little-endian uint16 samples as NSRC.
        // Keep both the quantized heights and the empty 0xffff sentinel exact.
        c.heights.resize(size_t(count));const auto sampleBytes=size_t(count)*sizeof(std::uint16_t);
        std::memcpy(c.heights.data(),w.bytes.data()+w.at,sampleBytes);w.at+=sampleBytes;
        g->ceilings.push_back(std::move(c));
    }
    n=w.U();Require(n<=1000000,"Nav64: flight box limit");
    for(unsigned i=0;i<n;++i) {
        FlightBox box;box.object=int(w.U());box.minimum=point();box.maximum=point();
        Require(box.object>0&&Finite(box.minimum)&&Finite(box.maximum)&&box.minimum.x<=box.maximum.x&&box.minimum.y<=box.maximum.y&&box.minimum.z<=box.maximum.z,"Nav64: invalid flight box");
        g->flightBoxes.push_back(box);
    }
    n=w.U();Require(n<=65536,"Nav64: door count limit");for(unsigned i=0;i<n;++i){Door d;d.object=int(w.U());d.openState=int(w.U());d.minimum=point();d.maximum=point();g->doors.push_back(d);}
    n=w.U();Require(n<=65536,"Nav64: connection count limit");for(unsigned i=0;i<n;++i) {
        OffMesh c;c.from=point();c.to=point();c.radius=w.F();auto low=w.U();c.fromSurface=low|(std::uint64_t(w.U())<<32);
        low=w.U();c.toSurface=low|(std::uint64_t(w.U())<<32);c.flags=w.U();g->connections.push_back(c);
    }
    g->tileWidth=w.F();Require(g->tileWidth>0&&g->tileWidth<=12.8f,"Nav64: invalid source tile width");
    auto catalogue=[&](auto& index,size_t limit) {
        auto count=w.U();Require(count<=131072,"Nav64: source catalogue limit");
        for(unsigned i=0;i<count;++i) {
            const int x=int(w.U()),z=int(w.U());auto n=w.U();Require(n<4000000&&n<=(w.bytes.size()-w.at)/4,"Nav64: source index limit");
            auto inserted=index.emplace(std::make_pair(x,z),std::vector<std::uint32_t>{});Require(inserted.second,"Nav64: duplicate source tile");
            auto& list=inserted.first->second;list.reserve(n);
            for(unsigned j=0;j<n;++j){auto triangle=w.U();Require(triangle<limit,"Nav64: invalid source index");list.push_back(triangle);}
        }
    };
    catalogue(g->tileTriangles,g->ground.size());catalogue(g->surfaceTriangles,g->ground.size());catalogue(g->tileCeilings,g->ceilings.size());
    n=w.U();Require(n<=100000,"Nav64: source object limit");
    for(unsigned i=0;i<n;++i) {
        const int id=int(w.U());auto count=w.U();Require(count<=131072,"Nav64: object tile limit");
        auto& tiles=g->objectTiles[id];for(unsigned j=0;j<count;++j){const int x=int(w.U()),z=int(w.U());tiles.emplace(x,z);}
    }
    Require(w.at==w.bytes.size(),"Nav64: trailing source bytes");
    if(stages)*stages={unpackMs,arrayMs,ms(unpacked)-arrayMs};return g;
}
void IndexSources(Geometry& g,float width) {
    Require(g.ground.size()<4000000&&g.ceilings.size()<=100000,"Nav64: source catalogue index limit");
    g.tileWidth=width;g.tileTriangles.clear();g.surfaceTriangles.clear();g.tileCeilings.clear();g.objectTiles.clear();
    const int nx=int(std::ceil((g.maximum.x-g.minimum.x)/width)),nz=int(std::ceil((g.maximum.z-g.minimum.z)/width));
    for(size_t i=0;i<g.ground.size();++i) {
        const auto& t=g.ground[i];
        const auto a=ToNav(t.a),b=ToNav(t.b),c=ToNav(t.c);
        auto bin=[&](float border,auto& index,bool objects) {
            const int x0=std::max(0,int(std::floor((std::min({a.x,b.x,c.x})-border-g.minimum.x)/width))),x1=std::min(nx-1,int(std::floor((std::max({a.x,b.x,c.x})+border-g.minimum.x)/width)));
            const int z0=std::max(0,int(std::floor((std::min({a.z,b.z,c.z})-border-g.minimum.z)/width))),z1=std::min(nz-1,int(std::floor((std::max({a.z,b.z,c.z})+border-g.minimum.z)/width)));
            for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x){index[{x,z}].push_back(std::uint32_t(i));if(objects&&t.object)g.objectTiles[t.object].emplace(x,z);}
        };
        bin(1.3f,g.tileTriangles,true);
        if(t.terrain||(t.surface>1&&t.area))bin(.025f,g.surfaceTriangles,false);
    }
    for(size_t i=0;i<g.ceilings.size();++i) {
        const auto& c=g.ceilings[i];constexpr float border=1.3f;
        const int x0=std::max(0,int(std::floor((c.x*.3f-border-g.minimum.x)/width))),
            x1=std::min(nx-1,int(std::floor(((c.x+int(c.width))*.3f+border-g.minimum.x)/width))),
            z0=std::max(0,int(std::floor((-(c.y+int(c.height))*.3f-border-g.minimum.z)/width))),
            z1=std::min(nz-1,int(std::floor((-c.y*.3f+border-g.minimum.z)/width)));
        for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x){g.tileCeilings[{x,z}].push_back(std::uint32_t(i));g.objectTiles[c.object].emplace(x,z);}
    }
}
void IndexSurfaces(Mesh& mesh) {
    if(!mesh.sources)return;
    Require(mesh.sources->tileWidth==mesh.data->getParams()->tileWidth,"Nav64: mismatched source tile width");
    // Catalogue storage is shared by all profiles; queries read it in place.
}
std::unique_ptr<Mesh> RebuildTile(const Geometry& g,const Profile& p,int x,int z) {
    const auto start=std::chrono::steady_clock::now();
    auto result=BuildProfile(g,p,"",nullptr,1,x,z);
    result->buildMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
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
    // Stage the complete replacement in the reserved spare capacity first.
    // Tile hashes use XY; the temporary layer keeps the old address available
    // until addTile has validated and linked the new data. No query runs here.
    const auto* previous=mesh.data->getTileAt(x,z,0);
    const auto previousRef=previous?mesh.data->getTileRef(previous):0;
    const auto key=std::make_pair(x,z);
    auto surfaces=replacement.polygonSurfaces.extract(key);
    dtTileRef installed=0;
    if(replacement.replacementData) {
        Require(!surfaces.empty(),"Nav64: replacement has no polygon provenance");
        auto* header=reinterpret_cast<dtMeshHeader*>(replacement.replacementData);
        Require(header->x==x&&header->y==z&&header->layer==0&&!mesh.data->getTileAt(x,z,1),"Nav64: invalid replacement tile address");
        header->layer=previous?1:0;
        const auto status=mesh.data->addTile(replacement.replacementData,replacement.replacementSize,DT_TILE_FREE_DATA,0,&installed);
        if(dtStatusFailed(status)){header->layer=0;throw std::runtime_error("Nav64: cannot stage replacement tile; old tile retained");}
        replacement.replacementData=nullptr;
    }
    if(previousRef&&dtStatusFailed(mesh.data->removeTile(previousRef,nullptr,nullptr))) {
        if(installed)mesh.data->removeTile(installed,nullptr,nullptr);
        throw std::runtime_error("Nav64: cannot remove source tile; replacement rolled back");
    }
    if(installed)mesh.data->getTileAt(x,z,previousRef?1:0)->header->layer=0;
    mesh.polygonSurfaces.erase(key);
    if(installed)mesh.polygonSurfaces.insert(std::move(surfaces));
    mesh.tiles+=unsigned(installed!=0);mesh.tiles-=unsigned(previousRef!=0);
    for(auto i=mesh.doorFlags.begin();i!=mesh.doorFlags.end();) {
        if(std::get<0>(i->first)==x&&std::get<1>(i->first)==z)i=mesh.doorFlags.erase(i);else ++i;
    }
    ++mesh.generation;
}
void DoorState(Mesh& mesh,int object,bool open) {
    if(Flies(mesh.profile))return;
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
