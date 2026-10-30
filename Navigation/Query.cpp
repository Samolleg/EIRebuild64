#include "Query.h"
#include "../external/recastnavigation/Detour/Include/DetourNavMeshQuery.h"
#include "../external/recastnavigation/Detour/Include/DetourNode.h"
#include <algorithm>
#include <iterator>
#include <unordered_set>


namespace Navigation {
const char* StatusName(Status s) {
    switch(s){case Status::Complete:return "complete";case Status::NoMesh:return "no_mesh";case Status::Invalid:return "invalid";case Status::NoStart:return "no_start";case Status::NoGoal:return "no_goal";case Status::NoPath:return "no_path";case Status::Limit:return "search_limit";case Status::TooLong:return "too_long";}return "invalid";
}
namespace {
dtNavMeshQuery* Query(const Mesh& m) {
    return m.query.get();
}
unsigned short ClassFlags(const Mesh& mesh,unsigned short flags) {
    return flags?flags:ExcludeClass(mesh.profile.materialClass);
}
Location Address(const Mesh& m,Point p,dtPolyRef ref,float height,float referenceZ) {
    Location l;l.position=FromNav(p);l.radius=m.profile.radius;l.height=height;l.polygon=ref;l.generation=m.generation;
    unsigned char area=1;m.data->getPolyArea(ref,&area);l.area=area;
    if(Flies(m.profile)) {
        float point[]={p.x,p.y,p.z},closest[3]{};
        if(m.query&&dtStatusSucceed(m.query->closestPointOnPoly(ref,point,closest,nullptr))&&std::hypot(closest[0]-p.x,closest[2]-p.z)<=.001f){l.position=FromNav({closest[0],closest[1],closest[2]});l.surface=1;}
        return l;
    }
    const dtMeshTile* tile=nullptr;const dtPoly* poly=nullptr;
    if(!m.sources||dtStatusFailed(m.data->getTileAndPolyByRef(ref,&tile,&poly)))return l;
    const auto key=std::make_pair(tile->header->x,tile->header->y);
    const auto identities=m.polygonSurfaces.find(key);const auto triangles=m.sources->surfaceTriangles.find(key);
    const size_t index=size_t(poly-tile->polys);
    if(identities==m.polygonSurfaces.end()||index>=identities->second.size()||triangles==m.sources->surfaceTriangles.end())return l;
    const auto& surfaces=identities->second[index];
    float best=1.e30f,terrainZ=-1.e30f;
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
        for(const auto& upper:heights)if(upper.first>at.first+.001f&&upper.first-at.first<height) {covered=true;break;}
        if(covered)continue;
        const float distance=std::abs(at.first-referenceZ);
        // Coplanar BASE overlaps share one stable address, independent of
        // the queried polygon or traversal direction. Sub-millimetre placement
        // noise is not a second floor/ceiling. The source Z is never shifted.
        if(distance<best-.001f||(std::abs(distance-best)<=.001f&&at.second<l.surface)) {
            best=distance;l.surface=at.second;l.position.z=at.first;
        }
    }
    return l;
}
bool Nearest(const Mesh& m,dtNavMeshQuery& q,Point point,float horizontal,float vertical,const dtQueryFilter& filter,dtPolyRef& ref,Point& result,std::uint64_t hint=0,float height=0) {
    if(!Finite(point)||!m.sources||!std::isfinite(horizontal)||horizontal<0||!std::isfinite(vertical)||vertical<=0)return false;
    const auto p=ToNav(point);const auto& g=*m.sources;
    // Enumerate overlapping XY polygons through the whole source height range.
    // Detour's fan Z is neither a floor selector nor an admission threshold.
    const float pos[]={p.x,Flies(m.profile)?0.f:(g.minimum.y+g.maximum.y)*.5f,p.z};
    const float ext[]={horizontal,Flies(m.profile)?1.e6f:(g.maximum.y-g.minimum.y)*.5f+2,horizontal};
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
        // The requested horizontal tolerance is the full admission bound.
        // Adding another millimetre made strict 1 mm support queries accept
        // targets that the physical step correctly rejects as outside its mesh.
        if(xy>horizontal||(!Flies(m.profile)&&dz>vertical))continue;
        candidates.emplace_back(refs[i],at);
    }
    float best=1.e30f;bool exact=false;std::uint64_t selectedSurface=0;
    for(const auto& candidate:candidates) {
        const auto& at=candidate.second;bool covered=false;
        for(const auto& upper:candidates)if(std::hypot(at.position.x-upper.second.position.x,at.position.y-upper.second.position.y)<=.001f&&upper.second.position.z>at.position.z+.001f&&upper.second.position.z-at.position.z<height) {covered=true;break;}
        if(covered)continue;
        const float xy=std::hypot(at.position.x-point.x,at.position.y-point.y),dz=std::abs(at.position.z-point.z);
        const bool same=xy<=.001f;if(exact&&!same)continue;
        const float score=xy*xy+dz*dz;
        if((same&&!exact)||score<best-.000001f||
            (same==exact&&std::abs(score-best)<=.000001f&&((at.surface==hint&&selectedSurface!=hint)||(!hint&&at.surface<selectedSurface)))) {
            best=score;ref=candidate.first;result=ToNav(at.position);exact=same;selectedSurface=at.surface;
        }
    }
    return ref!=0;
}
// Geometric distance is measured from the click in map XY, with the
// original support height breaking ties between overlapping floors.
struct ClosestGoalQuery final : dtPolyQuery {
    const Mesh& mesh;dtNavMeshQuery& query;Point target;float height;
    Location best;float bestXY=1.e30f,bestZ=1.e30f;
    std::unordered_set<dtPolyRef> seen;bool limited=false;
    ClosestGoalQuery(const Mesh& m,dtNavMeshQuery& q,Point p,float h)
        :mesh(m),query(q),target(p),height(h){seen.reserve(256);}
    void Consider(dtPolyRef ref) {
        const auto nav=ToNav(target);const float input[]={nav.x,nav.y,nav.z};float near[3]{};
        if(dtStatusFailed(query.closestPointOnPoly(ref,input,near,nullptr)))return;
        const float dx=near[0]-nav.x,dy=near[2]-nav.z,xy=dx*dx+dy*dy;
        if(xy>bestXY+.000001f)return;
        const auto at=Address(mesh,{near[0],near[1],near[2]},ref,height,target.z);
        if(!at.surface)return;
        const float dz=std::abs(at.position.z-target.z);
        if(!best.polygon||xy<bestXY-.000001f||
           (std::abs(xy-bestXY)<=.000001f&&(dz<bestZ-.001f||
            (std::abs(dz-bestZ)<=.001f&&ref<best.polygon)))) {
            best=at;bestXY=xy;bestZ=dz;
        }
    }
    void process(const dtMeshTile*,dtPoly**,dtPolyRef* refs,int count) override {
        for(int i=0;i<count;++i) {
            if(limited)return;
            if(!seen.insert(refs[i]).second)continue;
            if(seen.size()>65535){limited=true;return;}
            Consider(refs[i]);
        }
    }
};
Status ClosestGoal(const Mesh& mesh,dtNavMeshQuery& query,Point point,
                   const dtQueryFilter& filter,float height,Location& result) {
    if(!mesh.sources)return Status::NoGoal;
    const auto nav=ToNav(point);const auto& g=*mesh.sources;
    const float center[]={nav.x,Flies(mesh.profile)?0.f:(g.minimum.y+g.maximum.y)*.5f,nav.z};
    const float maximum=std::max({.15f,std::abs(nav.x-g.minimum.x),std::abs(nav.x-g.maximum.x),
                                std::abs(nav.z-g.minimum.z),std::abs(nav.z-g.maximum.z)})+mesh.profile.cellSize;
    ClosestGoalQuery closest(mesh,query,point,height);float radius=.15f;
    for(;;) {
        const float extent[]={radius,Flies(mesh.profile)?1.e6f:(g.maximum.y-g.minimum.y)*.5f+2,radius};
        const auto status=query.queryPolygons(center,extent,&filter,&closest);
        if(dtStatusFailed(status)||closest.limited)return Status::Limit;
        // An XY circle inside the queried square proves that no unqueried
        // polygon can be closer. Each polygon is evaluated at most once.
        if(closest.best.polygon&&closest.bestXY<=radius*radius+.000001f) {
            result=closest.best;return Status::Complete;
        }
        if(radius>=maximum)return Status::NoGoal;
        radius=std::min(maximum,radius*2);
    }
}
bool Start(const Mesh& mesh,dtNavMeshQuery& q,Point p,const dtQueryFilter& filter,dtPolyRef& ref,Point& near,Point& actual,bool& ingress,float height,const Location* address=nullptr) {
    ingress=false;
    // Keep the address reached by the previous move. A new nearest-poly
    // search at every edge can choose a different overlapping floor/region.
    if(address&&address->generation==mesh.generation&&q.isValidPolyRef(address->polygon,&filter)) {
        const auto nav=ToNav(p);const float point[]={nav.x,nav.y,nav.z};float closest[3]{};
        if(dtStatusSucceed(q.closestPointOnPoly(address->polygon,point,closest,nullptr))&&
            std::hypot(closest[0]-nav.x,closest[2]-nav.z)<=.001f) {
            const auto at=Address(mesh,{closest[0],closest[1],closest[2]},address->polygon,height,p.z);
            if(at.surface&&std::abs(at.position.z-p.z)<=mesh.profile.climb+.001f) {
                ref=address->polygon;near=ToNav(at.position);actual=at.position;return true;
            }
        }
    }
    if(!Nearest(mesh,q,p,.05f,4.f,filter,ref,near,0,height))return false;
    actual=FromNav(near);
    ingress=std::hypot(actual.x-p.x,actual.y-p.y)>.001f;
    if(ingress) {
        // The existing 5 cm admission margin is an explicit entry segment,
        // never an unreported replacement of the route's physical origin.
        const auto at=Address(mesh,ToNav(p),ref,height,p.z);
        if(!at.surface||std::abs(at.position.z-p.z)>mesh.profile.climb+.001f)return false;
        actual=at.position;
    }
    return true;
}
bool Limited(dtStatus s){return dtStatusDetail(s,DT_OUT_OF_NODES)||dtStatusDetail(s,DT_BUFFER_TOO_SMALL);}
Route FindAround(const Mesh&,const Request&);
}
bool Project(const Mesh& m,Point p,float horizontal,float vertical,Location& result,float height,unsigned short excludedFlags) {
    if(!m.data||!m.tiles)return false;auto* query=Query(m);if(!query)return false;auto& q=*query;
    height=height>0?height:m.profile.height;const auto mask=ClearanceMask(m.profile,height);if(!mask)return false;
    dtQueryFilter filter;filter.setIncludeFlags(mask);filter.setExcludeFlags(ClassFlags(m,excludedFlags));dtPolyRef ref=0;Point near;
    if(!Nearest(m,q,p,horizontal,vertical,filter,ref,near,0,height))return false;
    result=Address(m,near,ref,height,p.z);return true;
}
bool ResolveLocation(const Mesh& m,Point p,const Location* hint,Location& result,float height,unsigned short excludedFlags) {
    if(!m.query||!m.data||!m.tiles)return false;
    const auto mask=ClearanceMask(m.profile,height);if(!mask)return false;
    dtQueryFilter filter;filter.setIncludeFlags(mask);filter.setExcludeFlags(ClassFlags(m,excludedFlags));dtPolyRef ref=0;Point near;
    if(!Nearest(m,*m.query,p,1.f,4.f,filter,ref,near,hint?hint->surface:0,height))return false;
    result=Address(m,near,ref,height,p.z);return result.surface!=0;
}
bool StartLocation(const Mesh& m,Point p,Location& result,float height,unsigned short excludedFlags) {
    if(!m.data||!m.tiles)return false;auto* query=Query(m);if(!query)return false;
    height=height>0?height:m.profile.height;const auto mask=ClearanceMask(m.profile,height);if(!mask)return false;
    dtQueryFilter filter;filter.setIncludeFlags(mask);filter.setExcludeFlags(ClassFlags(m,excludedFlags));dtPolyRef ref=0;Point near,actual;bool ingress=false;
    if(!Start(m,*query,p,filter,ref,near,actual,ingress,height))return false;
    result=Address(m,ingress?ToNav(actual):near,ref,height,p.z);return true;
}
Route Find(const Mesh& m,const Request& r) {
    Route result;if(!m.data||!m.tiles){result.status=Status::NoMesh;return result;}
    if(!Finite(r.source)||!Finite(r.goal)||!std::isfinite(r.maxLength)||r.maxLength<0||!std::isfinite(r.goalRadius)||r.goalRadius<0)return result;
    auto* query=Query(m);if(!query){result.status=Status::Limit;return result;}auto& q=*query;
    const float height=r.height>0?r.height:m.profile.height;const auto mask=ClearanceMask(m.profile,height);if(!mask)return result;
    dtQueryFilter filter;filter.setIncludeFlags(mask);filter.setExcludeFlags(ClassFlags(m,r.excludedFlags));for(int i=1;i<16;++i){if(!(r.areaCosts[i]>0&&std::isfinite(r.areaCosts[i])))return result;filter.setAreaCost(i,r.areaCosts[i]);}
    if(r.obstacles&&!r.obstacles->empty())return FindAround(m,r);
    dtPolyRef from=0,to=0;Point source,goal,actual;bool ingress=false;
    if(!Start(m,q,r.source,filter,from,source,actual,ingress,height,r.start)){result.status=Status::NoStart;return result;}
    if(!Nearest(m,q,r.goal,std::max(.15f,r.goalRadius),4.f,filter,to,goal,0,height)) {
        if(r.goalPolicy==GoalPolicy::Exact){result.status=Status::NoGoal;return result;}
        Location closest;const auto selected=ClosestGoal(m,q,r.goal,filter,height,closest);
        if(selected!=Status::Complete){result.status=selected;return result;}
        to=closest.polygon;goal=ToNav(closest.position);result.goalStatus=Status::NoGoal;
    }
    float start[3]={source.x,source.y,source.z},end[3]={goal.x,goal.y,goal.z};
    // Fixed work bound within Detour, no retry with a different pathfinder.
    auto status=q.initSlicedFindPath(from,to,start,end,&filter);
    int iterations=0;
    while(dtStatusInProgress(status)&&iterations<65536){int done=0;status=q.updateSlicedFindPath(2048,&done);iterations+=done;if(!done)break;}
    if(dtStatusInProgress(status)||Limited(status)){result.status=Status::Limit;return result;}
    if(dtStatusFailed(status)){result.status=Status::NoPath;return result;}
    const bool reached=from==to||q.isInClosedList(to);
    bool selectedPartial=false;
    if(!reached&&r.goalPolicy==GoalPolicy::ClosestReachable) {
        // An exhausted A* contains the entire reachable component. Choose the
        // closest actual polygon boundary to the click, not the heuristic's
        // closest portal midpoint and not the closest point to the unit.
        ClosestGoalQuery closest(m,q,r.goal,height);
        const auto* pool=q.getNodePool();
        for(int i=1;i<=pool->getNodeCount();++i) {
            const auto* node=pool->getNodeAtIdx(unsigned(i));
            if(node->flags&DT_NODE_CLOSED)closest.Consider(node->id);
        }
        if(!closest.best.polygon){result.status=Status::NoPath;return result;}
        to=closest.best.polygon;goal=ToNav(closest.best.position);
        end[0]=goal.x;end[1]=goal.y;end[2]=goal.z;
        result.goalStatus=Status::NoPath;selectedPartial=true;
    }
    std::vector<dtPolyRef> corridor(8192);int count=0;
    status=selectedPartial?q.finalizeSlicedFindPathPartial(&to,1,corridor.data(),&count,int(corridor.size())):
                           q.finalizeSlicedFindPath(corridor.data(),&count,int(corridor.size()));
    if(Limited(status)){result.status=Status::Limit;return result;}
    if(dtStatusFailed(status)||(!selectedPartial&&dtStatusDetail(status,DT_PARTIAL_RESULT))||!count||corridor[count-1]!=to){result.status=Status::NoPath;return result;}
    corridor.resize(count);
    auto straighten=[&](const std::vector<dtPolyRef>& path,Route& route) {
        std::vector<float> points(8192*3);std::vector<dtPolyRef> refs(8192);
        std::vector<unsigned char> flags(8192);int n=0;
        const auto state=q.findStraightPath(start,end,path.data(),int(path.size()),points.data(),flags.data(),refs.data(),&n,8192,DT_STRAIGHTPATH_ALL_CROSSINGS);
        if(Limited(state))return Status::Limit;
        if(dtStatusFailed(state)||n<1||!(flags[n-1]&DT_STRAIGHTPATH_END))return Status::NoPath;
        route.corridor.assign(path.begin(),path.end());
        if(ingress)route.points.push_back(Address(m,ToNav(actual),from,height,r.source.z));
        for(int i=0;i<n;++i) {
            Point p{points[i*3],points[i*3+1],points[i*3+2]};
            auto at=Address(m,p,refs[i]?refs[i]:to,height,route.points.empty()?r.source.z:route.points.back().position.z);
            if(!at.surface)return Status::NoPath;
            if(!route.points.empty()) {
                const auto& previous=route.points.back();const float length=Distance(previous.position,at.position);
                route.length+=length;route.cost+=length*filter.getAreaCost(previous.area);
            }
            route.points.push_back(at);
        }
        return Status::Complete;
    };
    result.status=straighten(corridor,result);
    if(result.status!=Status::Complete){result.points.clear();result.corridor.clear();return result;}
    result.plannedCost=result.cost;
    if(count>1) {
        // A polygon-centre A* corridor may contain needless turns even on a
        // clear line. Mixed material costs are not a reason to skip visibility:
        // price both actual portal-crossing paths under the unchanged filter.
        std::vector<dtPolyRef> visible(512);int visited=0;float t=0,normal[3]{};
        const auto ray=q.raycast(from,start,end,&filter,&t,normal,visible.data(),&visited,int(visible.size()));
        if(dtStatusSucceed(ray)&&!Limited(ray)&&t>=1&&visited>0&&visible[visited-1]==to) {
            visible.resize(visited);Route candidate;
            if(straighten(visible,candidate)==Status::Complete) {
                result.straightCost=candidate.cost;
                const float tolerance=1.e-5f*std::max(1.f,result.cost);
                if(candidate.cost<=result.cost+tolerance) {
                    candidate.goalStatus=result.goalStatus;candidate.plannedCost=result.cost;
                    candidate.straightCost=candidate.cost;candidate.straightened=true;
                    result=std::move(candidate);
                }
            }
        }
    }
    if(Flies(m.profile)&&result.points.size()>1) {
        std::vector<Location> sampled;sampled.push_back(result.points.front());result.length=0;
        for(size_t i=1;i<result.points.size();++i) {
            auto a=result.points[i-1].position,b=result.points[i].position;
            const int count=std::max(1,int(std::ceil(std::hypot(b.x-a.x,b.y-a.y)/.25f)));
            if(sampled.size()+count>60000){result.status=Status::Limit;result.points.clear();return result;}
            for(int k=1;k<=count;++k) {
                const float f=float(k)/count;Location at;
                if(!Project(m,{a.x+(b.x-a.x)*f,a.y+(b.y-a.y)*f,a.z+(b.z-a.z)*f},.001f,1.e6f,at,height,r.excludedFlags)){result.status=Status::NoPath;result.points.clear();return result;}
                result.length+=Distance(sampled.back().position,at.position);sampled.push_back(at);
            }
        }
        result.points=std::move(sampled);result.cost=result.length;
    }
    if(result.length>r.maxLength){result.points.clear();result.corridor.clear();result.status=Status::TooLong;return result;}
    result.status=Status::Complete;return result;
}
namespace {
Route FindAround(const Mesh& mesh,const Request& input) {
    auto obstacles=*input.obstacles;
    for(auto& obstacle:obstacles) {
        // Movement stops before a physical collision, but may already be
        // inside the optional 2 cm planning margin. Never classify that valid
        // start as occupied; keep the full body radius and add only available
        // clearance. This does not change collision dimensions or speeds.
        const float distance=std::hypot(input.source.x-obstacle.position.x,input.source.y-obstacle.position.y);
        obstacle.radius+=std::clamp(distance-obstacle.radius-.0002f,0.f,.02f);
    }
    Request request=input;request.obstacles=&obstacles;
    Request plain=request;plain.obstacles=nullptr;
    auto direct=Find(mesh,plain);
    if(direct.status!=Status::Complete)return direct;
    auto intersects=[&](Point a,Point b,const Obstacle& obstacle) {
        const float dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
        const float t=length>0?std::clamp(((obstacle.position.x-a.x)*dx+(obstacle.position.y-a.y)*dy)/length,0.f,1.f):0;
        // Same floor policy as the live circle-collision query. Never block an
        // independent bridge/floor with a unit on the terrain below it.
        if(std::abs(a.z+(b.z-a.z)*t-obstacle.position.z)>2.f)return false;
        // A moving neighbour can overlap the source before this query. Match
        // CheckCircleCollision: allow only an outward first edge, retaining
        // the full body radius and rejecting every later re-entry.
        const float ax=a.x-obstacle.position.x,ay=a.y-obstacle.position.y;
        const float before=ax*ax+ay*ay,after=(ax+dx)*(ax+dx)+(ay+dy)*(ay+dy);
        if(std::hypot(a.x-request.source.x,a.y-request.source.y)<=.001f&&
           before<obstacle.radius*obstacle.radius&&ax*dx+ay*dy>=0&&after>before+.00001f)return false;
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
        if(Project(mesh,p,.001f,4.f,at,height,request.excludedFlags)&&clearSegment(at.position,at.position))nodes.push_back(at);
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
            edge.goalPolicy=GoalPolicy::Exact;
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
        result.cost+=edge.cost;result.plannedCost+=edge.plannedCost;
    }
    result.goalStatus=direct.goalStatus;result.status=Status::Complete;return result;
}
}
bool Constrain(const Mesh& m,Point from,Point desired,Location& result,float height,const char** reason,const Location* address,unsigned short excludedFlags) {
    auto reject=[&](const char* text){if(reason)*reason=text;return false;};
    if(reason)*reason="none";
    if(!Finite(from)||!Finite(desired))return reject("non_finite");
    if(!m.data||!m.tiles)return reject("empty_mesh");auto* query=Query(m);if(!query)return reject("no_query");auto& q=*query;
    height=height>0?height:m.profile.height;const auto mask=ClearanceMask(m.profile,height);if(!mask)return false;
    dtQueryFilter filter;filter.setIncludeFlags(mask);filter.setExcludeFlags(ClassFlags(m,excludedFlags));dtPolyRef ref=0;Point near;
    Point actual;bool ingress=false;
    if(!Start(m,q,from,filter,ref,near,actual,ingress,height,address))return reject("no_start");
    if(ingress) {
        const auto entry=FromNav(near);const float dx=entry.x-actual.x,dy=entry.y-actual.y;
        const float length=std::hypot(dx,dy),along=((desired.x-actual.x)*dx+(desired.y-actual.y)*dy)/(length*length);
        if(along<1.f) {
            const float lateral=std::hypot(desired.x-actual.x-dx*along,desired.y-actual.y-dy*along);
            if(along<-.001f/length||lateral>.001f)return reject("ingress_direction");
            auto at=Address(m,ToNav(desired),ref,height,from.z);
            if(!at.surface)return reject("missing_original_support");
            if(std::abs(at.position.z-from.z)>m.profile.climb+.001f)return reject("height_jump");
            result=at;return true;
        }
    }
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
    const float limit=Flies(m.profile)?1.e30f:m.profile.climb+std::tan(m.profile.slope*3.14159265f/180.f)*std::hypot(desired.x-from.x,desired.y-from.y)+.001f;
    for(int i=0;i<count;++i) {
        float point[3]{};if(dtStatusFailed(q.closestPointOnPoly(visited[i],end,point,nullptr)))continue;
        const float error=std::hypot(point[0]-end[0],point[2]-end[2]);
        if(error>.001f)continue;
        const auto candidate=Address(m,{point[0],point[1],point[2]},visited[i],height,from.z);
        if(!candidate.surface){failure="missing_original_support";continue;}
        if(std::abs(candidate.position.z-from.z)>limit){failure="height_jump";continue;}
        const float score=std::abs(candidate.position.z-from.z);
        const bool upper=selected.surface&&candidate.position.z>selected.position.z+.001f&&candidate.position.z-selected.position.z<height;
        const bool below=selected.surface&&selected.position.z>candidate.position.z+.001f&&selected.position.z-candidate.position.z<height;
        if(!below&&(upper||error<bestXY-.00001f||(std::abs(error-bestXY)<=.00001f&&(score<best-.001f||(std::abs(score-best)<=.001f&&candidate.surface<selected.surface))))){best=score;bestXY=error;selected=candidate;}
    }
    if(!selected.surface)return reject(failure);
    result=selected;return true;
}
bool Follow(const Mesh& mesh,Point from,Point desired,const std::vector<Point>& path,Location& result,float height,const char** reason,const Location* address,unsigned short excludedFlags) {
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
        Location next;if(!Constrain(mesh,current,path[i],next,height,reason,at.polygon?&at:nullptr,excludedFlags))return false;
        current=next.position;at=next;
    }
    return Constrain(mesh,current,desired,result,height,reason,at.polygon?&at:nullptr,excludedFlags);
}
bool RandomReachable(const Mesh& mesh,Point source,Point center,float radius,float height,float (*random)(),Point& result,unsigned short excludedFlags,const Location* address,RandomShape shape) {
    if(!mesh.query||!mesh.data||!Finite(source)||!Finite(center)||!std::isfinite(radius)||radius<0||!random)return false;
    const auto mask=ClearanceMask(mesh.profile,height);if(!mask)return false;
    dtQueryFilter filter;filter.setIncludeFlags(mask);filter.setExcludeFlags(ClassFlags(mesh,excludedFlags));
    auto accept=[&](dtPolyRef ref,Point point) {
        const auto at=Address(mesh,point,ref,height,source.z);if(!at.surface)return false;
        const auto p=at.position;const float dx=std::abs(p.x-center.x),dy=std::abs(p.y-center.y);
        const float limit=radius==0?.001f:radius;
        if(shape==RandomShape::Square?(dx>limit||dy>limit):(std::hypot(dx,dy)>limit))return false;
        // Reachable graph polygons alone do not prove a complete route: the
        // same query also enforces source support and bounded Detour results.
        Request request;request.source=source;request.start=address;request.goal=p;request.height=height;request.excludedFlags=excludedFlags;
        if(Find(mesh,request).status!=Status::Complete)return false;
        result=p;return true;
    };
    if(radius==0) {
        dtPolyRef ref=0;Point point;
        return Nearest(mesh,*mesh.query,center,.001f,4.f,filter,ref,point,0,height)&&accept(ref,point);
    }
    // Seed the requested region from a proven source route. Sampling a circle
    // around the source with radius distance+radius almost never hits a small
    // guard/retreat region when its center is far from the unit.
    Request approach;approach.source=source;approach.start=address;approach.goal=center;
    approach.goal.z=source.z;approach.height=height;approach.excludedFlags=excludedFlags;
    approach.goalPolicy=GoalPolicy::ClosestReachable;
    const auto route=Find(mesh,approach);
    if(route.status!=Status::Complete||route.points.empty())return false;
    const auto seed=route.points.back();
    const float search=radius*(shape==RandomShape::Square?1.41421356237f:1.f);
    if(std::hypot(seed.position.x-center.x,seed.position.y-center.y)>search+.001f)return false;
    const auto nav=ToNav({center.x,center.y,seed.position.z});
    const float start[]={nav.x,nav.y,nav.z};
    std::vector<dtPolyRef> refs(4096);int count=0;
    const auto status=mesh.query->findPolysAroundCircle(seed.polygon,start,search,&filter,refs.data(),nullptr,nullptr,&count,int(refs.size()));
    if(dtStatusFailed(status)||Limited(status))return false;
    struct Sample {dtPolyRef ref;Point a,b,c;double end;};
    std::vector<Sample> samples;double total=0;
    for(int i=0;i<count;++i) {
        const dtMeshTile* tile=nullptr;const dtPoly* poly=nullptr;
        if(dtStatusFailed(mesh.data->getTileAndPolyByRef(refs[i],&tile,&poly))||poly->getType()!=DT_POLYTYPE_GROUND)continue;
        std::vector<Point> polygon;for(unsigned j=0;j<poly->vertCount;++j){const auto* v=tile->verts+poly->verts[j]*3;polygon.push_back({v[0],v[1],v[2]});}
        auto clip=[&](bool xaxis,float edge,bool greater) {
            std::vector<Point> out;if(polygon.empty())return;
            auto previous=polygon.back();float pv=(xaxis?previous.x:previous.z)-edge;
            for(auto current:polygon) {
                const float cv=(xaxis?current.x:current.z)-edge;
                if((pv>=0)!=(cv>=0)){const float f=pv/(pv-cv);out.push_back({previous.x+(current.x-previous.x)*f,previous.y+(current.y-previous.y)*f,previous.z+(current.z-previous.z)*f});}
                if(greater?cv>=0:cv<=0)out.push_back(current);
                previous=current;pv=cv;
            }
            polygon=std::move(out);
        };
        clip(true,nav.x-radius,true);clip(true,nav.x+radius,false);
        clip(false,nav.z-radius,true);clip(false,nav.z+radius,false);
        for(size_t j=2;j<polygon.size();++j) {
            const auto a=polygon[0],b=polygon[j-1],c=polygon[j];
            const double area=std::abs((double(b.x)-a.x)*(double(c.z)-a.z)-(double(b.z)-a.z)*(double(c.x)-a.x));
            if(area<=1.e-12)continue;
            total+=area;samples.push_back({refs[i],a,b,c,total});
        }
    }
    if(samples.empty())return false;
    auto fraction=[&](){return std::clamp(random(),0.f,1.f);};
    for(int attempt=0;attempt<8;++attempt) {
        // Sample the polygon clipped to the requested box, not the whole
        // Detour polygon (which can be much larger than a small guard area).
        const double chosen=fraction()*total;
        auto selected=std::lower_bound(samples.begin(),samples.end(),chosen,[](const Sample& s,double value){return s.end<value;});
        if(selected==samples.end())selected=std::prev(samples.end());
        const float u=std::sqrt(fraction()),v=fraction();const auto& t=*selected;
        const Point p{(1-u)*t.a.x+u*((1-v)*t.b.x+v*t.c.x),(1-u)*t.a.y+u*((1-v)*t.b.y+v*t.c.y),(1-u)*t.a.z+u*((1-v)*t.b.z+v*t.c.z)};
        if(accept(t.ref,p))return true;
    }
    return false;
}

}
