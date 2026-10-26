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
    void ProfileOut(const Profile& p){F(p.radius);F(p.height);F(p.slope);F(p.climb);F(p.cellSize);F(p.cellHeight);U(p.tileCells);}
    Profile ProfileIn(){Profile p;p.radius=F();p.height=F();p.slope=F();p.climb=F();p.cellSize=F();p.cellHeight=F();p.tileCells=U();return p;}
};
void CheckProfile(const Profile& p) {
    Require(std::isfinite(p.radius)&&p.radius>0&&p.radius<=4 && std::isfinite(p.height)&&p.height>.1f&&p.height<=10 &&
        p.cellSize==.1f && p.cellHeight==.05f && p.tileCells==256 && p.slope==40 && p.climb==.35f,"Nav64: unsupported build profile");
}
// v1.6.0 addTile does not take bounds for its internal sections. Validate all
// counts and the exact aligned library layout BEFORE handing it cached bytes.
void CheckTile(const unsigned char* data,size_t size) {
    Require(size>=sizeof(dtMeshHeader)&&size<=16*1024*1024,"Nav64: invalid tile size");
    dtMeshHeader h;std::memcpy(&h,data,sizeof(h));
    Require(h.magic==DT_NAVMESH_MAGIC && h.version==DT_NAVMESH_VERSION && h.layer==0 &&
        h.polyCount>0&&h.polyCount<=32768 && h.vertCount>0&&h.vertCount<65536 && h.maxLinkCount>=0&&h.maxLinkCount<1000000 &&
        h.detailMeshCount==h.polyCount&&h.detailVertCount>=0&&h.detailVertCount<1000000&&h.detailTriCount>=0&&h.detailTriCount<1000000&&
        h.bvNodeCount>=0&&h.bvNodeCount<=h.polyCount*2 && h.offMeshConCount==0&&h.offMeshBase==h.polyCount,"Nav64: invalid tile header");
    size_t needed=dtAlign4(sizeof(dtMeshHeader))+dtAlign4(sizeof(float)*3*h.vertCount)+dtAlign4(sizeof(dtPoly)*h.polyCount)+
        dtAlign4(sizeof(dtLink)*h.maxLinkCount)+dtAlign4(sizeof(dtPolyDetail)*h.detailMeshCount)+dtAlign4(sizeof(float)*3*h.detailVertCount)+
        dtAlign4(sizeof(unsigned char)*4*h.detailTriCount)+dtAlign4(sizeof(dtBVNode)*h.bvNodeCount);
    Require(needed==size,"Nav64: mismatched tile sections");
    Require(std::isfinite(h.walkableHeight)&&h.walkableHeight>0&&std::isfinite(h.walkableRadius)&&h.walkableRadius>0&&
        std::isfinite(h.walkableClimb)&&h.walkableClimb>=0&&std::isfinite(h.bvQuantFactor)&&h.bvQuantFactor>0,"Nav64: invalid tile metrics");
    for(int i=0;i<3;++i)Require(std::isfinite(h.bmin[i])&&std::isfinite(h.bmax[i])&&h.bmin[i]<=h.bmax[i],"Nav64: invalid tile bounds");
    const auto* vertices=reinterpret_cast<const float*>(data+dtAlign4(sizeof(dtMeshHeader)));
    for(int i=0;i<h.vertCount*3;++i)Require(std::isfinite(vertices[i]),"Nav64: invalid tile vertex");
    size_t offset=dtAlign4(sizeof(dtMeshHeader))+dtAlign4(sizeof(float)*3*h.vertCount);
    const auto* polys=reinterpret_cast<const dtPoly*>(data+offset);
    for(int i=0;i<h.polyCount;++i) {
        Require(polys[i].vertCount>=3 && polys[i].vertCount<=DT_VERTS_PER_POLYGON && polys[i].getType()==DT_POLYTYPE_GROUND&&
            polys[i].getArea()>0&&polys[i].getArea()<16&&polys[i].flags==1,"Nav64: invalid polygon");
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
struct Resources {
    rcHeightfield* height=nullptr;rcCompactHeightfield* compact=nullptr;rcContourSet* contours=nullptr;rcPolyMesh* polys=nullptr;rcPolyMeshDetail* detail=nullptr;
    ~Resources(){rcFreeHeightField(height);rcFreeCompactHeightfield(compact);rcFreeContourSet(contours);rcFreePolyMesh(polys);rcFreePolyMeshDetail(detail);}
};
}
Bytes Inputs(const Geometry& g,const Profile& p,const std::string& scene,const Bytes& source) {
    Wire w;w.U(ContainerVersion);w.U(AlgorithmVersion);w.U(LibraryVersion);w.U(sizeof(dtPolyRef));w.ProfileOut(p);w.S(scene);
    w.U(std::uint32_t(source.size()));w.B(source.data(),source.size());
    w.U(std::uint32_t(g.ground.size()));
    for(const auto& t:g.ground){w.P(t.a);w.P(t.b);w.P(t.c);w.U(t.area);}
    w.U(std::uint32_t(g.solids.size()));for(const auto& s:g.solids)for(auto v:s.corners)w.P(v);
    return std::move(w.bytes);
}
std::unique_ptr<Mesh> Build(const Geometry& g,const Profile& p,const std::string& key) {
    CheckProfile(p);
    auto mesh=std::make_unique<Mesh>();mesh->profile=p;mesh->fingerprint=key;mesh->data.reset(dtAllocNavMesh());
    Require(bool(mesh->data),"Nav64: cannot allocate Detour mesh");
    float tileSize=p.cellSize*p.tileCells;
    int nx=int(std::ceil((g.maximum.x-g.minimum.x)/tileSize)), nz=int(std::ceil((g.maximum.z-g.minimum.z)/tileSize));
    Require(nx>0&&nz>0&&std::uint64_t(nx)*nz<=4096,"Nav64: zone exceeds tile limit");
    dtNavMeshParams params{};params.orig[0]=g.minimum.x;params.orig[1]=g.minimum.y-2;params.orig[2]=g.minimum.z;
    params.tileWidth=params.tileHeight=tileSize;params.maxTiles=int(dtNextPow2(nx*nz));params.maxPolys=32768;
    Require(dtStatusSucceed(mesh->data->init(&params)),"Nav64: cannot initialize Detour mesh");
    // Bin once, instead of scanning every triangle/solid again for each tile.
    // The margin is exactly the Recast border used below, including erosion.
    struct TileInput {std::vector<int> triangles,solids;};
    struct SolidInput {std::vector<float> hull;float low,high;};
    std::vector<TileInput> inputs(nx*nz);std::vector<SolidInput> solids;
    std::vector<float> allVertices;std::vector<int> allIndices;
    std::vector<unsigned char> allAreas;
    allVertices.reserve(g.ground.size()*9);allIndices.reserve(g.ground.size()*3);allAreas.reserve(g.ground.size());
    const float border=(std::ceil(p.radius/p.cellSize)+3)*p.cellSize;
    auto bin=[&](float minx,float minz,float maxx,float maxz,int index,bool solid) {
        int x0=std::max(0,int(std::floor((minx-g.minimum.x-border)/tileSize)));
        int z0=std::max(0,int(std::floor((minz-g.minimum.z-border)/tileSize)));
        int x1=std::min(nx-1,int(std::floor((maxx-g.minimum.x+border)/tileSize)));
        int z1=std::min(nz-1,int(std::floor((maxz-g.minimum.z+border)/tileSize)));
        for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)
            (solid?inputs[z*nx+x].solids:inputs[z*nx+x].triangles).push_back(index);
    };
    for(size_t i=0;i<g.ground.size();++i) {
        const auto& t=g.ground[i];Point a=ToNav(t.a),b=ToNav(t.b),c=ToNav(t.c);
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
    std::vector<TileBlob> tiles(nx*nz);std::vector<std::exception_ptr> errors(nx*nz);
    std::atomic<int> next{0};std::atomic<bool> failed{false};
    std::atomic<std::uint64_t> rasterUs{0},polygonUs{0};
    auto micros=[](auto begin){return std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count());};
    auto tile=[&](int index) {
        const int x=index%nx,z=index/nx;
        if(inputs[index].triangles.empty())return;
        rcContext context(false);auto begin=std::chrono::steady_clock::now();
        rcConfig cfg{};cfg.cs=p.cellSize;cfg.ch=p.cellHeight;cfg.walkableSlopeAngle=p.slope;
        cfg.walkableHeight=int(std::ceil(p.height/cfg.ch));cfg.walkableClimb=int(std::floor(p.climb/cfg.ch));cfg.walkableRadius=int(std::ceil(p.radius/cfg.cs));
        cfg.tileSize=p.tileCells;cfg.borderSize=cfg.walkableRadius+3;cfg.width=cfg.height=cfg.tileSize+cfg.borderSize*2;
        cfg.maxEdgeLen=120;cfg.maxSimplificationError=1.3f;cfg.minRegionArea=0;cfg.mergeRegionArea=400;cfg.maxVertsPerPoly=6;
        cfg.detailSampleDist=.6f;cfg.detailSampleMaxError=.05f;
        cfg.bmin[0]=g.minimum.x+x*tileSize-cfg.borderSize*cfg.cs;cfg.bmin[1]=g.minimum.y-2;cfg.bmin[2]=g.minimum.z+z*tileSize-cfg.borderSize*cfg.cs;
        cfg.bmax[0]=g.minimum.x+(x+1)*tileSize+cfg.borderSize*cfg.cs;cfg.bmax[1]=g.maximum.y+p.height+2;cfg.bmax[2]=g.minimum.z+(z+1)*tileSize+cfg.borderSize*cfg.cs;
        Resources r;r.height=rcAllocHeightfield();Require(r.height&&rcCreateHeightfield(&context,*r.height,cfg.width,cfg.height,cfg.bmin,cfg.bmax,cfg.cs,cfg.ch),"Nav64: heightfield allocation failed");
        std::vector<float> vertices;std::vector<int> indices;std::vector<unsigned char> areas;
        vertices.reserve(inputs[index].triangles.size()*9);indices.reserve(inputs[index].triangles.size()*3);areas.reserve(inputs[index].triangles.size());
        for(int triangle:inputs[index].triangles) {
            const auto* v=allVertices.data()+size_t(triangle)*9;
            for(int j=0;j<3;++j){indices.push_back(int(vertices.size()/3));vertices.insert(vertices.end(),v+j*3,v+j*3+3);}
            areas.push_back(allAreas[triangle]);
        }
        Require(rcRasterizeTriangles(&context,vertices.data(),int(vertices.size()/3),indices.data(),areas.data(),int(areas.size()),*r.height,cfg.walkableClimb),"Nav64: ground rasterization failed");
        rcFilterLedgeSpans(&context,cfg.walkableHeight,cfg.walkableClimb,*r.height);
        rcFilterWalkableLowHeightSpans(&context,cfg.walkableHeight,*r.height);
        r.compact=rcAllocCompactHeightfield();Require(r.compact&&rcBuildCompactHeightfield(&context,cfg.walkableHeight,cfg.walkableClimb,*r.height,*r.compact),"Nav64: compact heightfield failed");
        for(int solid:inputs[index].solids) {
            const auto& s=solids[solid];
            rcMarkConvexPolyArea(&context,s.hull.data(),int(s.hull.size()/3),s.low-p.height+.001f,s.high-.001f,RC_NULL_AREA,*r.compact);
        }
        rasterUs.fetch_add(micros(begin));begin=std::chrono::steady_clock::now();
        Require(rcErodeWalkableArea(&context,cfg.walkableRadius,*r.compact),"Nav64: radius erosion failed");
        Require(rcBuildDistanceField(&context,*r.compact)&&rcBuildRegions(&context,*r.compact,cfg.borderSize,cfg.minRegionArea,cfg.mergeRegionArea),"Nav64: region build failed");
        r.contours=rcAllocContourSet();Require(r.contours&&rcBuildContours(&context,*r.compact,cfg.maxSimplificationError,cfg.maxEdgeLen,*r.contours),"Nav64: contour build failed");
        r.polys=rcAllocPolyMesh();Require(r.polys&&rcBuildPolyMesh(&context,*r.contours,cfg.maxVertsPerPoly,*r.polys),"Nav64: polygon build failed");
        if(!r.polys->npolys){polygonUs.fetch_add(micros(begin));return;}
        Require(r.polys->npolys<=params.maxPolys&&r.polys->nverts<65536,"Nav64: tile polygon limit exceeded");
        r.detail=rcAllocPolyMeshDetail();Require(r.detail&&rcBuildPolyMeshDetail(&context,*r.polys,*r.compact,cfg.detailSampleDist,cfg.detailSampleMaxError,*r.detail),"Nav64: detail build failed");
        for(int i=0;i<r.polys->npolys;++i)r.polys->flags[i]=1;
        dtNavMeshCreateParams create{};create.verts=r.polys->verts;create.vertCount=r.polys->nverts;create.polys=r.polys->polys;create.polyAreas=r.polys->areas;create.polyFlags=r.polys->flags;create.polyCount=r.polys->npolys;create.nvp=r.polys->nvp;
        create.detailMeshes=r.detail->meshes;create.detailVerts=r.detail->verts;create.detailVertsCount=r.detail->nverts;create.detailTris=r.detail->tris;create.detailTriCount=r.detail->ntris;
        create.walkableHeight=p.height;create.walkableRadius=p.radius;create.walkableClimb=p.climb;create.tileX=x;create.tileY=z;create.tileLayer=0;
        rcVcopy(create.bmin,r.polys->bmin);rcVcopy(create.bmax,r.polys->bmax);create.cs=cfg.cs;create.ch=cfg.ch;create.buildBvTree=true;
        Require(dtCreateNavMeshData(&create,&tiles[index].data,&tiles[index].size),"Nav64: Detour tile creation failed");
        polygonUs.fetch_add(micros(begin));
    };
    auto worker=[&] {
        while(!failed.load()) {
            int index=next.fetch_add(1);if(index>=nx*nz)break;
            try {tile(index);}catch(...) {errors[index]=std::current_exception();failed.store(true);}
        }
    };
    mesh->workers=std::min(unsigned(nx*nz),std::min(8u,std::max(1u,std::thread::hardware_concurrency()/2)));
    std::vector<std::thread> workers;
    try {for(unsigned i=1;i<mesh->workers;++i)workers.emplace_back(worker);}
    catch(...) {failed.store(true);for(auto& thread:workers)thread.join();throw;}
    worker();for(auto& thread:workers)thread.join();
    for(const auto& error:errors)if(error)std::rethrow_exception(error);
    mesh->rasterMs=rasterUs.load()/1000;mesh->polygonMs=polygonUs.load()/1000;
    for(auto& tile:tiles)if(tile.data) {
        auto status=mesh->data->addTile(tile.data,tile.size,DT_TILE_FREE_DATA,0,nullptr);
        Require(dtStatusSucceed(status),"Nav64: Detour tile insertion failed");
        tile.data=nullptr;
        ++mesh->tiles;
    }
    // No traversable polygons is a valid empty mesh, not a build/data failure.
    return mesh;
}
Bytes Store(const Mesh& mesh) {
    Wire w;w.U(0x3656414e);w.U(0x00003436);w.U(ContainerVersion);w.U(AlgorithmVersion);w.U(LibraryVersion);w.U(0x12345678);w.U(sizeof(dtPolyRef));w.S(mesh.fingerprint);w.ProfileOut(mesh.profile);
    const auto* p=mesh.data->getParams();for(float v:p->orig)w.F(v);w.F(p->tileWidth);w.F(p->tileHeight);w.U(p->maxTiles);w.U(p->maxPolys);
    w.U(3); // Typed sections: tiles, surface metadata, reserved build layers.
    Wire tiles;tiles.U(mesh.tiles);
    const dtNavMesh* data=mesh.data.get();
    for(int i=0;i<data->getMaxTiles();++i){const auto* t=data->getTile(i);if(t&&t->header){tiles.U(t->dataSize);tiles.B(t->data,t->dataSize);}}
    w.U(1);w.U(std::uint32_t(tiles.bytes.size()));w.B(tiles.bytes.data(),tiles.bytes.size());
    w.U(2);w.U(8);w.U(1);w.U(1); // one imported terrain/object surface domain
    w.U(3);w.U(4);w.U(0); // no TileCache layers until X64NAV05
    return std::move(w.bytes);
}
std::unique_ptr<Mesh> Load(const Bytes& bytes,const Profile& expected,const std::string& key) {
    Wire w;w.bytes=bytes;
    Require(w.U()==0x3656414e&&w.U()==0x00003436&&w.U()==ContainerVersion&&w.U()==AlgorithmVersion&&w.U()==LibraryVersion&&w.U()==0x12345678&&w.U()==sizeof(dtPolyRef),"Nav64: incompatible container/ABI");
    auto mesh=std::make_unique<Mesh>();mesh->fingerprint=w.S();Require(mesh->fingerprint==key,"Nav64: stale input fingerprint");mesh->profile=w.ProfileIn();CheckProfile(mesh->profile);
    Wire a,b;a.ProfileOut(expected);b.ProfileOut(mesh->profile);Require(a.bytes==b.bytes,"Nav64: mismatched profile");
    dtNavMeshParams p{};for(auto& v:p.orig)v=w.F();p.tileWidth=w.F();p.tileHeight=w.F();p.maxTiles=w.U();p.maxPolys=w.U();
    Require(p.maxTiles>0&&p.maxTiles<=4096&&dtNextPow2(p.maxTiles)==p.maxTiles&&p.maxPolys==32768&&p.tileWidth==25.6f&&p.tileHeight==25.6f,"Nav64: invalid mesh limits");
    mesh->data.reset(dtAllocNavMesh());Require(mesh->data&&dtStatusSucceed(mesh->data->init(&p)),"Nav64: mesh initialization failed");
    Require(w.U()==3,"Nav64: invalid section count");
    Require(w.U()==1,"Nav64: missing tile section");auto section=w.U();Require(section<=bytes.size()-w.at,"Nav64: truncated tile section");auto end=w.at+section;
    auto count=w.U();Require(count<=std::uint32_t(p.maxTiles),"Nav64: tile count limit");
    for(std::uint32_t i=0;i<count;++i){auto n=w.U();Require(w.at<=end&&n<=end-w.at,"Nav64: truncated tile");CheckTile(bytes.data()+w.at,n);
        auto* data=static_cast<unsigned char*>(dtAlloc(n,DT_ALLOC_PERM));Require(data!=nullptr,"Nav64: tile allocation failed");std::memcpy(data,bytes.data()+w.at,n);w.at+=n;
        auto s=mesh->data->addTile(data,n,DT_TILE_FREE_DATA,0,nullptr);if(dtStatusFailed(s)){dtFree(data);throw std::runtime_error("Nav64: invalid/duplicate tile");}++mesh->tiles;
    }
    Require(w.at==end&&w.U()==2&&w.U()==8&&w.U()==1&&w.U()==1&&w.U()==3&&w.U()==4&&w.U()==0&&w.at==bytes.size(),"Nav64: invalid metadata/layer sections");
    return mesh;
}
}
