#include "Query.h"
#include "../external/recastnavigation/Detour/Include/DetourNavMeshQuery.h"
#include <algorithm>


namespace Navigation {
const char* StatusName(Status s) {
    switch(s){case Status::Complete:return "complete";case Status::NoMesh:return "no_mesh";case Status::Invalid:return "invalid";case Status::NoStart:return "no_start";case Status::NoGoal:return "no_goal";case Status::NoPath:return "no_path";case Status::Limit:return "search_limit";case Status::TooLong:return "too_long";}return "invalid";
}
namespace {
dtNavMeshQuery* Query(const Mesh& m) {
    return m.query.get();
}
Location Address(const Mesh& m,Point p,dtPolyRef ref,float height,float referenceZ) {
    Location l;l.position=FromNav(p);l.radius=m.profile.radius;l.height=height;l.polygon=ref;l.generation=m.generation;
    unsigned char area=1;m.data->getPolyArea(ref,&area);l.area=area;
    const dtMeshTile* tile=nullptr;const dtPoly* poly=nullptr;
    if(!m.sources||dtStatusFailed(m.data->getTileAndPolyByRef(ref,&tile,&poly)))return l;
    const auto key=std::make_pair(tile->header->x,tile->header->y);
    const auto identities=m.polygonSurfaces.find(key);const auto triangles=m.sources->surfaceTriangles.find(key);
    const size_t index=size_t(poly-tile->polys);
    if(identities==m.polygonSurfaces.end()||index>=identities->second.size()||triangles==m.sources->surfaceTriangles.end())return l;
    const auto& surfaces=identities->second[index];
    float best=1.e30f,terrainZ=-1.e30f;bool primary=false;
    std::vector<std::pair<float,std::uint64_t>> heights;
    for(size_t i:triangles->second) {
        const auto& original=m.sources->ground[i];
        const bool admitted=std::find(surfaces.begin(),surfaces.end(),original.surface)!=surfaces.end();
        if((!admitted&&!original.terrain)||m.excludedObjects.count(original.object))continue;
        const auto surface=original.surface;
        // Recast can join a sub-cell seam between imported BASE faces. Only
        // declared connected BASE planes may cover it, never solid BV tops.
        // The spatial catalogue includes the same quarter-cell margin.
        const float tolerance=original.terrain?.0011f:m.profile.cellSize*.25f;
        if(l.position.x<std::min({original.a.x,original.b.x,original.c.x})-tolerance||
            l.position.x>std::max({original.a.x,original.b.x,original.c.x})+tolerance||
            l.position.y<std::min({original.a.y,original.b.y,original.c.y})-tolerance||
            l.position.y>std::max({original.a.y,original.b.y,original.c.y})+tolerance)continue;
        const auto t=Surface(original,m.profile);if(!t.area)continue;
        const float ax=t.b.x-t.a.x,ay=t.b.y-t.a.y,bx=t.c.x-t.a.x,by=t.c.y-t.a.y,det=ax*by-ay*bx;
        // Walkability is owned by the baked mesh. This function evaluates
        // original support Z, including the sub-cell edge of a terrain triangle.
        if(det<=1.e-8f)continue;
        if(!t.terrain) {
            const float nx=ay*(t.c.z-t.a.z)-(t.b.z-t.a.z)*by,ny=(t.b.z-t.a.z)*bx-ax*(t.c.z-t.a.z);
            if(det/std::sqrt(nx*nx+ny*ny+det*det)<=std::cos(m.profile.slope*3.14159265f/180.f))continue;
        }
        const float x=l.position.x-t.a.x,y=l.position.y-t.a.y;
        const float u=(x*by-y*bx)/det,v=(ax*y-ay*x)/det;
        // Footprint margins are measured in metres, not barycentric coordinates.
        const float e0=std::max(0.f,-u)*std::abs(det)/std::max(1.e-8f,std::hypot(bx,by));
        const float e1=std::max(0.f,-v)*std::abs(det)/std::max(1.e-8f,std::hypot(ax,ay));
        const float e2=std::max(0.f,u+v-1)*std::abs(det)/std::max(1.e-8f,std::hypot(bx-ax,by-ay));
        if(std::max({e0,e1,e2})>tolerance)continue;
        const float z=t.a.z+u*(t.b.z-t.a.z)+v*(t.c.z-t.a.z);
        if(t.terrain) {
            terrainZ=std::max(terrainZ,z);
            if(admitted&&surfaces.size()==1){l.surface=surface;l.position.z=z;return l;}
        }
        if(admitted)heights.emplace_back(z,surface);
    }
    for(const auto& at:heights) {
        if(at.second!=1&&at.first<terrainZ-.015f)continue;
        bool covered=false;
        for(const auto& upper:heights)if(upper.first>at.first+.000001f&&upper.first-at.first<height) {covered=true;break;}
        if(covered)continue;
        const float distance=std::abs(at.first-referenceZ);
        const bool same=at.second==surfaces.front();
        // Select between floors by original world height, only after rejecting
        // a lower skin with insufficient body clearance below another support.
        if(distance<best-.000001f||(std::abs(distance-best)<=.000001f&&same&&!primary)) {
            best=distance;l.surface=at.second;l.position.z=at.first;primary=same;
        }
    }
    return l;
}
bool Nearest(const Mesh& m,dtNavMeshQuery& q,Point point,float horizontal,float vertical,const dtQueryFilter& filter,dtPolyRef& ref,Point& result,std::uint64_t hint=0,float height=0) {
    if(!Finite(point)||!m.sources||!std::isfinite(horizontal)||horizontal<0||!std::isfinite(vertical)||vertical<=0)return false;
    const auto p=ToNav(point);const auto& g=*m.sources;
    // Enumerate overlapping XY polygons through the whole source height range.
    // Detour's fan Z is neither a floor selector nor an admission threshold.
    const float pos[]={p.x,(g.minimum.y+g.maximum.y)*.5f,p.z};
    const float ext[]={horizontal,(g.maximum.y-g.minimum.y)*.5f+2,horizontal};
    dtPolyRef refs[2048];int count=0;ref=0;
    const auto status=q.queryPolygons(pos,ext,&filter,refs,&count,2048);
    if(dtStatusFailed(status)||dtStatusDetail(status,DT_BUFFER_TOO_SMALL))return false;
    height=height>0?height:m.profile.height;
    std::vector<std::pair<dtPolyRef,Location>> candidates;
    for(int i=0;i<count;++i) {
        const float input[]={p.x,p.y,p.z};float near[3]{};
        if(dtStatusFailed(q.closestPointOnPoly(refs[i],input,near,nullptr)))continue;
        auto at=Address(m,{near[0],near[1],near[2]},refs[i],height,point.z);
        if(!at.surface)continue;
        const float xy=std::hypot(at.position.x-point.x,at.position.y-point.y),dz=std::abs(at.position.z-point.z);
        if(xy>horizontal+.001f||dz>vertical)continue;
        candidates.emplace_back(refs[i],at);
    }
    float best=1.e30f;bool exact=false;std::uint64_t selectedSurface=0;
    for(const auto& candidate:candidates) {
        const auto& at=candidate.second;bool covered=false;
        for(const auto& upper:candidates)if(std::hypot(at.position.x-upper.second.position.x,at.position.y-upper.second.position.y)<=.001f&&upper.second.position.z>at.position.z+.000001f&&upper.second.position.z-at.position.z<height) {covered=true;break;}
        if(covered)continue;
        const float xy=std::hypot(at.position.x-point.x,at.position.y-point.y),dz=std::abs(at.position.z-point.z);
        const bool same=xy<=.001f;if(exact&&!same)continue;
        const float score=xy*xy+dz*dz;
        if((same&&!exact)||score<best-.000001f||
            (same==exact&&std::abs(score-best)<=.000001f&&at.surface==hint&&selectedSurface!=hint)) {
            best=score;ref=candidate.first;result=ToNav(at.position);exact=same;selectedSurface=at.surface;
        }
    }
    return ref!=0;
}
bool Start(const Mesh& mesh,dtNavMeshQuery& q,Point p,const dtQueryFilter& filter,dtPolyRef& ref,Point& near,Point& actual,bool& ingress,float height,const Location* address=nullptr) {
    ingress=false;
    // Keep the address reached by the previous move. A new nearest-poly
    // search at every edge can choose a different overlapping floor/region.
    if(address&&address->generation==mesh.generation&&q.isValidPolyRef(address->polygon,&filter)) {
        const auto nav=ToNav(p);const float point[]={nav.x,nav.y,nav.z};float closest[3]{};
        if(dtStatusSucceed(q.closestPointOnPoly(address->polygon,point,closest,nullptr))&&
            std::hypot(closest[0]-nav.x,closest[2]-nav.z)<=.001f) {
            const auto at=Address(mesh,{closest[0],closest[1],closest[2]},address->polygon,address->height,p.z);
            if(at.surface&&std::abs(at.position.z-p.z)<=mesh.profile.climb+.001f) {
                ref=address->polygon;near=ToNav(at.position);actual=at.position;return true;
            }
        }
    }
    if(!Nearest(mesh,q,p,.05f,4.f,filter,ref,near,0,height))return false;
    actual=FromNav(near);return true;
}
bool Limited(dtStatus s){return dtStatusDetail(s,DT_OUT_OF_NODES)||dtStatusDetail(s,DT_BUFFER_TOO_SMALL);}
Route FindAround(const Mesh&,const Request&);
}
bool Project(const Mesh& m,Point p,float horizontal,float vertical,Location& result,float height) {
    if(!m.data||!m.tiles)return false;auto* query=Query(m);if(!query)return false;auto& q=*query;
    height=height>0?height:m.profile.height;const auto mask=ClearanceMask(m.profile,height);if(!mask)return false;
    dtQueryFilter filter;filter.setIncludeFlags(mask);dtPolyRef ref=0;Point near;
    if(!Nearest(m,q,p,horizontal,vertical,filter,ref,near,0,height))return false;
    result=Address(m,near,ref,height,p.z);return true;
}
bool ResolveLocation(const Mesh& m,Point p,const Location* hint,Location& result,float height) {
    if(!m.query||!m.data||!m.tiles)return false;
    const auto mask=ClearanceMask(m.profile,height);if(!mask)return false;
    dtQueryFilter filter;filter.setIncludeFlags(mask);dtPolyRef ref=0;Point near;
    if(!Nearest(m,*m.query,p,1.f,4.f,filter,ref,near,hint?hint->surface:0,height))return false;
    result=Address(m,near,ref,height,p.z);return result.surface!=0;
}
bool StartLocation(const Mesh& m,Point p,Location& result,float height) {
    if(!m.data||!m.tiles)return false;auto* query=Query(m);if(!query)return false;
    height=height>0?height:m.profile.height;const auto mask=ClearanceMask(m.profile,height);if(!mask)return false;
    dtQueryFilter filter;filter.setIncludeFlags(mask);dtPolyRef ref=0;Point near,actual;bool ingress=false;
    if(!Start(m,*query,p,filter,ref,near,actual,ingress,height))return false;
    result=Address(m,ingress?ToNav(actual):near,ref,height,p.z);return true;
}
Route Find(const Mesh& m,const Request& r) {
    Route result;if(!m.data||!m.tiles){result.status=Status::NoMesh;return result;}
    if(!Finite(r.source)||!Finite(r.goal)||!std::isfinite(r.maxLength)||r.maxLength<0||!std::isfinite(r.goalRadius)||r.goalRadius<0)return result;
    auto* query=Query(m);if(!query){result.status=Status::Limit;return result;}auto& q=*query;
    const float height=r.height>0?r.height:m.profile.height;const auto mask=ClearanceMask(m.profile,height);if(!mask)return result;
    dtQueryFilter filter;filter.setIncludeFlags(mask);for(int i=1;i<16;++i){if(!(r.areaCosts[i]>0&&std::isfinite(r.areaCosts[i])))return result;filter.setAreaCost(i,r.areaCosts[i]);}
    if(r.obstacles&&!r.obstacles->empty())return FindAround(m,r);
    dtPolyRef from=0,to=0;Point source,goal,actual;bool ingress=false;
    if(!Start(m,q,r.source,filter,from,source,actual,ingress,height,r.start)){result.status=Status::NoStart;return result;}
    if(!Nearest(m,q,r.goal,std::max(.15f,r.goalRadius),4.f,filter,to,goal,0,height)){result.status=Status::NoGoal;return result;}
    float start[3]={source.x,source.y,source.z},end[3]={goal.x,goal.y,goal.z};
    // Fixed work bound within Detour, no retry with a different pathfinder.
    auto status=q.initSlicedFindPath(from,to,start,end,&filter);
    int iterations=0;
    while(dtStatusInProgress(status)&&iterations<65536){int done=0;status=q.updateSlicedFindPath(2048,&done);iterations+=done;if(!done)break;}
    if(dtStatusInProgress(status)||Limited(status)){result.status=Status::Limit;return result;}
    if(dtStatusFailed(status)){result.status=Status::NoPath;return result;}
    std::vector<dtPolyRef> corridor(8192);int count=0;status=q.finalizeSlicedFindPath(corridor.data(),&count,int(corridor.size()));
    if(Limited(status)){result.status=Status::Limit;return result;}
    if(dtStatusFailed(status)||dtStatusDetail(status,DT_PARTIAL_RESULT)||!count||corridor[count-1]!=to){result.status=Status::NoPath;return result;}
    std::vector<float> points(8192*3);std::vector<dtPolyRef> refs(8192);std::vector<unsigned char> flags(8192);int n=0;
    status=q.findStraightPath(start,end,corridor.data(),count,points.data(),flags.data(),refs.data(),&n,8192,DT_STRAIGHTPATH_ALL_CROSSINGS);
    if(Limited(status)){result.status=Status::Limit;return result;}
    if(dtStatusFailed(status)||n<1||!(flags[n-1]&DT_STRAIGHTPATH_END)){result.status=Status::NoPath;return result;}
    result.corridor.assign(corridor.begin(),corridor.begin()+count);
    if(ingress)result.points.push_back(Address(m,ToNav(actual),from,height,r.source.z));
    for(int i=0;i<n;++i){Point p{points[i*3],points[i*3+1],points[i*3+2]};auto l=Address(m,p,refs[i]?refs[i]:to,height,result.points.empty()?r.source.z:result.points.back().position.z);
        if(!l.surface){result.points.clear();result.corridor.clear();result.status=Status::NoPath;return result;}
        if(!result.points.empty())result.length+=Distance(result.points.back().position,l.position);result.points.push_back(l);}
    if(result.length>r.maxLength){result.points.clear();result.corridor.clear();result.status=Status::TooLong;return result;}
    result.status=Status::Complete;return result;
}
namespace {
Route FindAround(const Mesh& mesh,const Request& request) {
    Request plain=request;plain.obstacles=nullptr;
    auto direct=Find(mesh,plain);
    if(direct.status!=Status::Complete)return direct;
    auto intersects=[](Point a,Point b,const Obstacle& obstacle) {
        const float dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
        const float t=length>0?std::clamp(((obstacle.position.x-a.x)*dx+(obstacle.position.y-a.y)*dy)/length,0.f,1.f):0;
        // Same floor policy as the live circle-collision query. Never block an
        // independent bridge/floor with a unit on the terrain below it.
        if(std::abs(a.z+(b.z-a.z)*t-obstacle.position.z)>2.f)return false;
        return std::hypot(a.x+dx*t-obstacle.position.x,a.y+dy*t-obstacle.position.y)+.0001f<obstacle.radius;
    };
    auto clearSegment=[&](Point a,Point b) {
        for(const auto& obstacle:*request.obstacles)if(intersects(a,b,obstacle))return false;
        return true;
    };
    auto clearRoute=[&](const Route& route) {
        for(size_t i=1;i<route.points.size();++i)if(!clearSegment(route.points[i-1].position,route.points[i].position))return false;
        return true;
    };
    if(clearRoute(direct))return direct;
    // Query-local visibility vertices: circumscribed dodecagons preserve the
    // physical radii even on their chords. Every graph edge is a real Find
    // corridor on the original mesh and is checked against ALL standing units.
    // No mesh flags or tiles are changed, including the source polygon.
    std::vector<const Obstacle*> rings;
    for(const auto& obstacle:*request.obstacles) {
        bool blocks=false;
        for(size_t i=1;i<direct.points.size()&&!blocks;++i)blocks=intersects(direct.points[i-1].position,direct.points[i].position,obstacle);
        if(blocks)rings.push_back(&obstacle);
    }
    std::sort(rings.begin(),rings.end(),[&](const Obstacle* a,const Obstacle* b){return Distance(a->position,request.source)<Distance(b->position,request.source);});
    // Explicit bounded work in dense crowds; a queued unit will wait for an
    // obstacle change instead of expanding an unbounded secondary search.
    if(rings.size()>4){Route failed;failed.status=Status::Limit;return failed;}
    std::vector<Location> nodes{direct.points.front(),direct.points.back()};
    const float height=request.height>0?request.height:mesh.profile.height;
    for(const auto* obstacle:rings)for(int i=0;i<12;++i) {
        const float angle=float(i)*6.283185307f/12,radius=(obstacle->radius+.025f)/.965925826f;
        Point p{obstacle->position.x+radius*std::cos(angle),obstacle->position.y+radius*std::sin(angle),obstacle->position.z};
        Location at;
        if(Project(mesh,p,.001f,4.f,at,height)&&clearSegment(at.position,at.position))nodes.push_back(at);
    }
    std::vector<float> cost(nodes.size(),1.e30f),length(nodes.size(),0);
    std::vector<int> previous(nodes.size(),-1);std::vector<bool> closed(nodes.size(),false);
    std::vector<Route> incoming(nodes.size());cost[0]=0;
    for(size_t iteration=0;iteration<nodes.size();++iteration) {
        size_t from=nodes.size();float best=1.e30f;
        for(size_t i=0;i<nodes.size();++i)if(!closed[i]&&cost[i]<best){best=cost[i];from=i;}
        if(from==nodes.size()||from==1)break;
        closed[from]=true;
        for(size_t to=1;to<nodes.size();++to) {
            if(closed[to]||to==from||!clearSegment(nodes[from].position,nodes[to].position))continue;
            Request edge=plain;edge.source=nodes[from].position;edge.start=&nodes[from];
            edge.goal=nodes[to].position;edge.goalRadius=.001f;edge.maxLength=request.maxLength-length[from];
            auto route=Find(mesh,edge);
            if(route.status!=Status::Complete||!clearRoute(route)||Distance(route.points.back().position,nodes[to].position)>.003f)continue;
            float weight=0;
            for(size_t i=1;i<route.points.size();++i)weight+=Distance(route.points[i-1].position,route.points[i].position)*request.areaCosts[route.points[i-1].area];
            if(cost[from]+weight>=cost[to])continue;
            cost[to]=cost[from]+weight;length[to]=length[from]+route.length;previous[to]=int(from);incoming[to]=std::move(route);
        }
    }
    Route result;result.status=Status::NoPath;
    if(previous[1]<0)return result;
    std::vector<int> chain;for(int i=1;i!=0;i=previous[i])chain.push_back(i);
    std::reverse(chain.begin(),chain.end());
    for(int i:chain) {
        const auto& edge=incoming[i];
        for(const auto& p:edge.points)if(result.points.empty()||Distance(result.points.back().position,p.position)>.00001f)result.points.push_back(p);
        for(auto ref:edge.corridor)if(result.corridor.empty()||result.corridor.back()!=ref)result.corridor.push_back(ref);
        result.length+=edge.length;
    }
    result.status=Status::Complete;return result;
}
}
bool Constrain(const Mesh& m,Point from,Point desired,Location& result,float height,const char** reason,const Location* address) {
    auto reject=[&](const char* text){if(reason)*reason=text;return false;};
    if(reason)*reason="none";
    if(!Finite(from)||!Finite(desired))return reject("non_finite");
    if(!m.data||!m.tiles)return reject("empty_mesh");auto* query=Query(m);if(!query)return reject("no_query");auto& q=*query;
    height=height>0?height:m.profile.height;const auto mask=ClearanceMask(m.profile,height);if(!mask)return false;
    dtQueryFilter filter;filter.setIncludeFlags(mask);dtPolyRef ref=0;Point near;
    Point actual;bool ingress=false;
    if(!Start(m,q,from,filter,ref,near,actual,ingress,height,address))return reject("no_start");
    auto goal=ToNav(desired);float end[3]={goal.x,goal.y,goal.z};
    // Clip the actual step against convex polygons in double precision. Both
    // Detour local walkers use strict float edge tests; at a funnel corner
    // they can pick an unrelated wall or stop before a valid shared portal.
    // Traverse only linked portals intersected by this segment, in order.
    // The 1 mm margin is solely the existing numerical endpoint allowance.
    constexpr double epsilon=.001;
    const double x=near.x,z=near.z,dx=goal.x-x,dz=goal.z-z;
    auto bound=[](double value,double delta,double minimum,double& lo,double& hi) {
        if(std::abs(delta)<1.e-15)return value>=minimum;
        const double t=(minimum-value)/delta;
        if(delta>0)lo=std::max(lo,t);else hi=std::min(hi,t);
        return lo<=hi+1.e-12;
    };
    auto interval=[&](const dtMeshTile* tile,const dtPoly* poly,double& lo,double& hi) {
        double winding=0;
        for(int i=0;i<poly->vertCount;++i) {
            const float* a=tile->verts+poly->verts[i]*3;const float* b=tile->verts+poly->verts[(i+1)%poly->vertCount]*3;
            winding+=double(a[0])*b[2]-double(b[0])*a[2];
        }
        const double sign=winding>=0?1:-1;
        for(int i=0;i<poly->vertCount;++i) {
            const float* a=tile->verts+poly->verts[i]*3;const float* b=tile->verts+poly->verts[(i+1)%poly->vertCount]*3;
            const double ex=double(b[0])-a[0],ez=double(b[2])-a[2],length=std::hypot(ex,ez);
            if(length<1.e-12)continue;
            if(!bound(sign*(ex*(z-a[2])-ez*(x-a[0])),sign*(ex*dz-ez*dx),-epsilon*length,lo,hi))return false;
        }
        return true;
    };
    struct Reached {dtPolyRef ref;double arrival;};
    std::vector<Reached> reached{{ref,0}};dtPolyRef visited[512];int count=0;
    for(size_t at=0;at<reached.size();++at) {
        if(reached.size()>512)return reject("surface_move_limit");
        const auto node=reached[at];const dtMeshTile* tile=nullptr;const dtPoly* poly=nullptr;
        if(dtStatusFailed(m.data->getTileAndPolyByRef(node.ref,&tile,&poly)))return reject("invalid_polygon");
        double low=node.arrival,high=1;
        if(!interval(tile,poly,low,high))continue;
        if(high>=1.-1.e-12)visited[count++]=node.ref;
        for(auto linkIndex=poly->firstLink;linkIndex!=DT_NULL_LINK;linkIndex=tile->links[linkIndex].next) {
            const auto& link=tile->links[linkIndex];if(!link.ref)continue;
            const dtMeshTile* otherTile=nullptr;const dtPoly* other=nullptr;
            if(dtStatusFailed(m.data->getTileAndPolyByRef(link.ref,&otherTile,&other))||!filter.passFilter(link.ref,otherTile,other))continue;
            const auto* va=tile->verts+poly->verts[link.edge]*3;const auto* vb=tile->verts+poly->verts[(link.edge+1)%poly->vertCount]*3;
            double ax=va[0],az=va[2],bx=vb[0],bz=vb[2];
            if(link.side!=0xff) {
                // bmin/bmax quantize an entire tile edge to 255 intervals.
                // Recover the exact shared slab from the linked polygon edges;
                // a centimetre-sized quantization loss must not close a portal.
                const int axis=(link.side==0||link.side==4)?2:0,across=axis==2?0:2;
                double otherLow=1.e30,otherHigh=-1.e30;
                for(int i=0;i<other->vertCount;++i) {
                    const auto* a=otherTile->verts+other->verts[i]*3;const auto* b=otherTile->verts+other->verts[(i+1)%other->vertCount]*3;
                    if(std::abs(double(a[across])-va[across])>epsilon||std::abs(double(b[across])-va[across])>epsilon)continue;
                    otherLow=std::min({otherLow,double(a[axis]),double(b[axis])});otherHigh=std::max({otherHigh,double(a[axis]),double(b[axis])});
                }
                const double first=std::max(std::min(double(va[axis]),double(vb[axis])),otherLow),last=std::min(std::max(double(va[axis]),double(vb[axis])),otherHigh);
                if(last<first-epsilon||std::abs(double(vb[axis])-va[axis])<1.e-12)continue;
                double f=(first-va[axis])/(double(vb[axis])-va[axis]),g=(last-va[axis])/(double(vb[axis])-va[axis]);
                ax=va[0]+(double(vb[0])-va[0])*f;az=va[2]+(double(vb[2])-va[2])*f;
                bx=va[0]+(double(vb[0])-va[0])*g;bz=va[2]+(double(vb[2])-va[2])*g;
            }
            const double ex=bx-ax,ez=bz-az,length=std::hypot(ex,ez);if(length<1.e-12)continue;
            double enter=low,exit=high;
            const double side=ex*(z-az)-ez*(x-ax),sideDelta=ex*dz-ez*dx;
            const double along=ex*(x-ax)+ez*(z-az),alongDelta=ex*dx+ez*dz;
            if(!bound(side,sideDelta,-epsilon*length,enter,exit)||!bound(-side,-sideDelta,-epsilon*length,enter,exit)||
               !bound(along,alongDelta,-epsilon*length,enter,exit)||!bound(-along,-alongDelta,-length*length-epsilon*length,enter,exit)||
               !interval(otherTile,other,enter,exit))continue;
            auto found=std::find_if(reached.begin(),reached.end(),[&](const Reached& p){return p.ref==link.ref;});
            if(found==reached.end())reached.push_back({link.ref,enter});
            else if(enter<found->arrival-1.e-12){found->arrival=enter;if(size_t(found-reached.begin())<at)reached.push_back({link.ref,enter});}
        }
    }
    if(!count)return reject("blocked_edge");
    // A portal can have a few float ulps of separation (tile-local origins),
    // or the target can lie exactly on a diagonal shared edge. In that case
    // Detour's strict inside test finds no containing polygon and reports an
    // unrelated wall. Project only onto polygons actually reached in this
    // local traversal, with at most 1 mm of numerical correction.
    float best=1.e30f,bestXY=1.e30f;Location selected;const char* failure="blocked_edge";
    const float limit=m.profile.climb+std::tan(m.profile.slope*3.14159265f/180.f)*std::hypot(desired.x-from.x,desired.y-from.y)+.001f;
    for(int i=0;i<count;++i) {
        float point[3]{};if(dtStatusFailed(q.closestPointOnPoly(visited[i],end,point,nullptr)))continue;
        const float error=std::hypot(point[0]-end[0],point[2]-end[2]);
        if(error>.001f)continue;
        const auto candidate=Address(m,{point[0],point[1],point[2]},visited[i],height,from.z);
        if(!candidate.surface){failure="missing_original_support";continue;}
        if(std::abs(candidate.position.z-from.z)>limit){failure="height_jump";continue;}
        const float score=std::abs(candidate.position.z-from.z);
        const bool upper=selected.surface&&candidate.position.z>selected.position.z+.000001f&&candidate.position.z-selected.position.z<height;
        const bool below=selected.surface&&selected.position.z>candidate.position.z+.000001f&&selected.position.z-candidate.position.z<height;
        if(!below&&(upper||error<bestXY-.00001f||(std::abs(error-bestXY)<=.00001f&&score<best))){best=score;bestXY=error;selected=candidate;}
    }
    if(!selected.surface)return reject(failure);
    result=selected;return true;
}
bool Follow(const Mesh& mesh,Point from,Point desired,const std::vector<Point>& path,Location& result,float height,const char** reason,const Location* address) {
    auto progress=[&](Point point,float& along) {
        float best=1.e30f,total=0;along=0;
        for(size_t i=1;i<path.size();++i) {
            const auto a=path[i-1],b=path[i];const float dx=b.x-a.x,dy=b.y-a.y,len=std::hypot(dx,dy);
            const float f=len>0?std::clamp(((point.x-a.x)*dx+(point.y-a.y)*dy)/(len*len),0.f,1.f):0;
            const float distance=std::hypot(point.x-a.x-dx*f,point.y-a.y-dy*f);
            if(distance<best){best=distance;along=total+len*f;}
            total+=len;
        }
        return best<=.002f;
    };
    float begin=0,end=0;
    if(!progress(from,begin)||!progress(desired,end)||end+.002f<begin) {
        if(reason)*reason="off_corridor";return false;
    }
    Location at;if(address)at=*address;Point current=from;float total=0;
    for(size_t i=1;i<path.size();++i) {
        total+=std::hypot(path[i].x-path[i-1].x,path[i].y-path[i-1].y);
        if(total<=begin+.0001f||total>=end-.0001f)continue;
        Location next;if(!Constrain(mesh,current,path[i],next,height,reason,at.polygon?&at:nullptr))return false;
        current=next.position;at=next;
    }
    return Constrain(mesh,current,desired,result,height,reason,at.polygon?&at:nullptr);
}
bool RandomReachable(const Mesh& mesh,Point source,Point center,float radius,float height,float (*random)(),Point& result) {
    if(!mesh.query||!Finite(source)||!Finite(center)||!std::isfinite(radius)||radius<=0||!random)return false;
    dtQueryFilter filter;filter.setIncludeFlags(ClearanceMask(mesh.profile,height));
    dtPolyRef first=0;Point near;
    if(!Nearest(mesh,*mesh.query,source,.05f,4.f,filter,first,near,0,height))return false;
    const float start[]={near.x,near.y,near.z};
    const float search=radius+std::hypot(source.x-center.x,source.y-center.y);
    for(int attempt=0;attempt<8;++attempt) {
        dtPolyRef ref=0;float point[3]{};
        const auto status=mesh.query->findRandomPointAroundCircle(first,start,search,&filter,random,&ref,point);
        if(dtStatusFailed(status)||Limited(status)||!ref)return false;
        const auto at=Address(mesh,{point[0],point[1],point[2]},ref,height,source.z);if(!at.surface)continue;
        const auto p=at.position;
        if(std::hypot(p.x-center.x,p.y-center.y)<=radius) {result=p;return true;}
    }
    return false;
}

}
