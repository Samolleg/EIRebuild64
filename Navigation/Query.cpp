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
Location Address(const Mesh& m,Point p,dtPolyRef ref,float height,std::uint64_t hint=0) {Location l;l.position=FromNav(p);l.surface=1;l.radius=m.profile.radius;l.height=height;l.polygon=ref;l.generation=m.generation;
    unsigned char area=1;m.data->getPolyArea(ref,&area);l.area=area;
    if(m.sources) {
        const dtMeshTile* tile=nullptr;const dtPoly* poly=nullptr;
        if(dtStatusSucceed(m.data->getTileAndPolyByRef(ref,&tile,&poly))) {
            auto found=m.surfaceTriangles.find({tile->header->x,tile->header->y});float gap=.15f;
            if(found!=m.surfaceTriangles.end())for(size_t i:found->second) {
                const auto& t=m.sources->ground[i];if(m.excludedObjects.count(t.object))continue;const float ax=t.b.x-t.a.x,ay=t.b.y-t.a.y,bx=t.c.x-t.a.x,by=t.c.y-t.a.y,det=ax*by-ay*bx;
                if(std::abs(det)<1.e-8f)continue;const float x=l.position.x-t.a.x,y=l.position.y-t.a.y;
                const float u=(x*by-y*bx)/det,v=(ax*y-ay*x)/det;
                if(u<-.001f||v<-.001f||u+v>1.001f)continue;
                const float z=t.a.z+u*(t.b.z-t.a.z)+v*(t.c.z-t.a.z),distance=std::abs(z-p.y);
                if(distance<gap||(hint&&t.surface==hint&&std::abs(distance-gap)<.00001f)) {
                    gap=distance;l.surface=t.surface;l.position.z=z;
                }
            }
        }
    }
    return l;}
bool Nearest(dtNavMeshQuery& q,Point point,float horizontal,float vertical,const dtQueryFilter& filter,dtPolyRef& ref,Point& result) {
    if(!Finite(point)||!std::isfinite(horizontal)||horizontal<0||!std::isfinite(vertical)||vertical<=0)return false;
    auto p=ToNav(point);float pos[3]={p.x,p.y,p.z},ext[3]={horizontal,vertical,horizontal},near[3]{};ref=0;
    auto status=q.findNearestPoly(pos,ext,&filter,&ref,near);
    if(dtStatusFailed(status)||!ref)return false;
    result={near[0],near[1],near[2]};
    return std::hypot(p.x-result.x,p.z-result.z)<=horizontal+.001f && std::abs(p.y-result.y)<=vertical;
}
bool Start(const Mesh& mesh,dtNavMeshQuery& q,Point p,const dtQueryFilter& filter,dtPolyRef& ref,Point& near,Point& actual,bool& ingress,float,const Location* address=nullptr) {
    ingress=false;
    // Keep the address reached by the previous move. A new nearest-poly
    // search at every edge can choose a different overlapping floor/region.
    if(address&&address->generation==mesh.generation&&q.isValidPolyRef(address->polygon,&filter)) {
        const auto nav=ToNav(p);const float point[]={nav.x,nav.y,nav.z};float closest[3]{};
        if(dtStatusSucceed(q.closestPointOnPoly(address->polygon,point,closest,nullptr))&&
            std::hypot(closest[0]-nav.x,closest[2]-nav.z)<=.001f&&std::abs(closest[1]-nav.y)<=mesh.profile.climb+.15f) {
            ref=address->polygon;near={closest[0],closest[1],closest[2]};actual=FromNav(near);return true;
        }
    }
    if(!Nearest(q,p,.05f,4.f,filter,ref,near))return false;
    actual=FromNav(near);return true;
}
bool Limited(dtStatus s){return dtStatusDetail(s,DT_OUT_OF_NODES)||dtStatusDetail(s,DT_BUFFER_TOO_SMALL);}
}
bool Project(const Mesh& m,Point p,float horizontal,float vertical,Location& result,float height) {
    if(!m.data||!m.tiles)return false;auto* query=Query(m);if(!query)return false;auto& q=*query;
    height=height>0?height:m.profile.height;const auto mask=ClearanceMask(m.profile,height);if(!mask)return false;
    dtQueryFilter filter;filter.setIncludeFlags(mask);dtPolyRef ref=0;Point near;
    if(!Nearest(q,p,horizontal,vertical,filter,ref,near))return false;
    result=Address(m,near,ref,height);return true;
}
bool ResolveLocation(const Mesh& m,Point p,const Location* hint,Location& result,float height) {
    if(!m.data||!m.tiles||!m.query||!Finite(p))return false;
    const auto mask=ClearanceMask(m.profile,height);if(!mask)return false;
    dtQueryFilter filter;filter.setIncludeFlags(mask);
    const auto nav=ToNav(p);const float point[]={nav.x,nav.y,nav.z},extent[]={1.f,4.f,1.f};
    dtPolyRef refs[2048];int count=0;
    const auto status=m.query->queryPolygons(point,extent,&filter,refs,&count,2048);
    if(dtStatusFailed(status)||Limited(status))return false;
    float best=1.e30f;bool found=false,foundExact=false;
    for(int i=0;i<count;++i) {
        float near[3]{};if(dtStatusFailed(m.query->closestPointOnPoly(refs[i],point,near,nullptr)))continue;
        if(std::hypot(near[0]-nav.x,near[2]-nav.z)>1.001f||std::abs(near[1]-nav.y)>4.f)continue;
        auto candidate=Address(m,{near[0],near[1],near[2]},refs[i],height,hint?hint->surface:0);
        const float dx=candidate.position.x-p.x,dy=candidate.position.y-p.y,dz=candidate.position.z-p.z;
        const bool exact=dx*dx+dy*dy<=.000001f;
        if(foundExact&&!exact)continue;
        const float score=dx*dx+dy*dy+dz*dz;
        const bool prefer=found&&hint&&candidate.surface==hint->surface&&result.surface!=hint->surface&&std::abs(score-best)<.00001f;
        if((exact&&!foundExact)||score<best-.00001f||prefer||!found){best=score;result=candidate;found=true;foundExact=exact;}
    }
    return found;
}
bool StartLocation(const Mesh& m,Point p,Location& result,float height) {
    if(!m.data||!m.tiles)return false;auto* query=Query(m);if(!query)return false;
    height=height>0?height:m.profile.height;const auto mask=ClearanceMask(m.profile,height);if(!mask)return false;
    dtQueryFilter filter;filter.setIncludeFlags(mask);dtPolyRef ref=0;Point near,actual;bool ingress=false;
    if(!Start(m,*query,p,filter,ref,near,actual,ingress,height))return false;
    result=Address(m,ingress?ToNav(actual):near,ref,height);return true;
}
Route Find(const Mesh& m,const Request& r) {
    Route result;if(!m.data||!m.tiles){result.status=Status::NoMesh;return result;}
    if(!Finite(r.source)||!Finite(r.goal)||!std::isfinite(r.maxLength)||r.maxLength<0||!std::isfinite(r.goalRadius)||r.goalRadius<0)return result;
    auto* query=Query(m);if(!query){result.status=Status::Limit;return result;}auto& q=*query;
    const float height=r.height>0?r.height:m.profile.height;const auto mask=ClearanceMask(m.profile,height);if(!mask)return result;
    dtQueryFilter filter;filter.setIncludeFlags(mask);for(int i=1;i<16;++i){if(!(r.areaCosts[i]>0&&std::isfinite(r.areaCosts[i])))return result;filter.setAreaCost(i,r.areaCosts[i]);}
    dtPolyRef from=0,to=0;Point source,goal,actual;bool ingress=false;
    if(!Start(m,q,r.source,filter,from,source,actual,ingress,height,r.start)){result.status=Status::NoStart;return result;}
    if(!Nearest(q,r.goal,std::max(.15f,r.goalRadius),4.f,filter,to,goal)){result.status=Status::NoGoal;return result;}
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
    if(ingress)result.points.push_back(Address(m,ToNav(actual),from,height));
    for(int i=0;i<n;++i){Point p{points[i*3],points[i*3+1],points[i*3+2]};auto l=Address(m,p,refs[i]?refs[i]:to,height);
        if(!result.points.empty())result.length+=Distance(result.points.back().position,l.position);result.points.push_back(l);}
    if(result.length>r.maxLength){result.points.clear();result.corridor.clear();result.status=Status::TooLong;return result;}
    result.status=Status::Complete;return result;
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
    auto goal=ToNav(desired);float start[3]={near.x,near.y,near.z},end[3]={goal.x,goal.y,goal.z},moved[3]{};
    dtPolyRef visited[512];int count=0;
    // Surface traversal handles shared edges/vertices without raycast's strict
    // last-polygon test at a floating-point portal endpoint. A clipped move is
    // still a rejection: never slide through a real obstacle or teleport.
    auto status=q.moveAlongSurface(ref,start,end,&filter,moved,visited,&count,512);
    if(dtStatusFailed(status))return reject("surface_move_failed");
    if(Limited(status))return reject("surface_move_limit");
    if(!count)return reject("blocked_edge");
    // A portal can have a few float ulps of separation (tile-local origins),
    // or the target can lie exactly on a diagonal shared edge. In that case
    // Detour's strict inside test finds no containing polygon and reports an
    // unrelated wall. Project only onto polygons actually reached in this
    // local traversal, with at most 1 mm of numerical correction.
    float supported[3]{},best=1.e30f;dtPolyRef support=0;
    for(int i=0;i<count;++i) {
        float point[3]{};if(dtStatusFailed(q.closestPointOnPoly(visited[i],end,point,nullptr)))continue;
        const float error=std::hypot(point[0]-end[0],point[2]-end[2]);
        if(error<best){best=error;support=visited[i];std::copy(point,point+3,supported);}
    }
    if(!support||best>.001f)return reject("blocked_edge");
    result=Address(m,{supported[0],supported[1],supported[2]},support,height);return true;
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
    if(!Nearest(*mesh.query,source,.05f,4.f,filter,first,near))return false;
    const float start[]={near.x,near.y,near.z};
    const float search=radius+std::hypot(source.x-center.x,source.y-center.y);
    for(int attempt=0;attempt<8;++attempt) {
        dtPolyRef ref=0;float point[3]{};
        const auto status=mesh.query->findRandomPointAroundCircle(first,start,search,&filter,random,&ref,point);
        if(dtStatusFailed(status)||Limited(status)||!ref)return false;
        const auto p=FromNav({point[0],point[1],point[2]});
        if(std::hypot(p.x-center.x,p.y-center.y)<=radius) {result=p;return true;}
    }
    return false;
}

}
