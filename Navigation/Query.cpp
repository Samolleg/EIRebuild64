#include "Query.h"
#include "../external/recastnavigation/Detour/Include/DetourNavMeshQuery.h"
#include <algorithm>
#include <limits>

namespace Navigation {
const char* StatusName(Status s) {
    switch(s){case Status::Complete:return "complete";case Status::NoMesh:return "no_mesh";case Status::Invalid:return "invalid";case Status::NoStart:return "no_start";case Status::NoGoal:return "no_goal";case Status::NoPath:return "no_path";case Status::Limit:return "search_limit";case Status::TooLong:return "too_long";}return "invalid";
}
namespace {
dtNavMeshQuery* Query(const Mesh& m) {
    if(!m.query) {
        std::unique_ptr<dtNavMeshQuery,QueryDeleter> q(dtAllocNavMeshQuery());
        if(!q||dtStatusFailed(q->init(m.data.get(),65535)))return nullptr;
        m.query=std::move(q);
    }
    return m.query.get();
}
Location Address(const Mesh& m,Point p,dtPolyRef ref) {Location l;l.position=FromNav(p);l.surface=1;l.radius=m.profile.radius;l.height=m.profile.height;l.polygon=ref;l.generation=m.generation;
    unsigned char area=1;m.data->getPolyArea(ref,&area);l.area=area;return l;}
bool Nearest(dtNavMeshQuery& q,Point point,float horizontal,float vertical,const dtQueryFilter& filter,dtPolyRef& ref,Point& result) {
    if(!Finite(point)||!std::isfinite(horizontal)||horizontal<0||!std::isfinite(vertical)||vertical<=0)return false;
    auto p=ToNav(point);float pos[3]={p.x,p.y,p.z},ext[3]={horizontal,vertical,horizontal},near[3]{};ref=0;
    auto status=q.findNearestPoly(pos,ext,&filter,&ref,near);
    if(dtStatusFailed(status)||!ref)return false;
    result={near[0],near[1],near[2]};
    return std::hypot(p.x-result.x,p.z-result.z)<=horizontal+.001f && std::abs(p.y-result.y)<=vertical;
}
bool Limited(dtStatus s){return dtStatusDetail(s,DT_OUT_OF_NODES)||dtStatusDetail(s,DT_BUFFER_TOO_SMALL);}
}
bool Project(const Mesh& m,Point p,float horizontal,float vertical,Location& result) {
    if(!m.data)return false;auto* query=Query(m);if(!query)return false;auto& q=*query;
    dtQueryFilter filter;dtPolyRef ref=0;Point near;
    if(!Nearest(q,p,horizontal,vertical,filter,ref,near))return false;
    result=Address(m,near,ref);return true;
}
Route Find(const Mesh& m,const Request& r) {
    Route result;if(!m.data){result.status=Status::NoMesh;return result;}
    if(!Finite(r.source)||!Finite(r.goal)||!std::isfinite(r.maxLength)||r.maxLength<0||!std::isfinite(r.goalRadius)||r.goalRadius<0)return result;
    auto* query=Query(m);if(!query){result.status=Status::Limit;return result;}auto& q=*query;
    dtQueryFilter filter;for(int i=1;i<16;++i){if(!(r.areaCosts[i]>0&&std::isfinite(r.areaCosts[i])))return result;filter.setAreaCost(i,r.areaCosts[i]);}
    dtPolyRef from=0,to=0;Point source,goal;
    if(!Nearest(q,r.source,.05f,2.f,filter,from,source)){result.status=Status::NoStart;return result;}
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
    for(int i=0;i<n;++i){Point p{points[i*3],points[i*3+1],points[i*3+2]};auto l=Address(m,p,refs[i]?refs[i]:to);
        if(!result.points.empty())result.length+=Distance(result.points.back().position,l.position);result.points.push_back(l);}
    if(result.length>r.maxLength){result.points.clear();result.corridor.clear();result.status=Status::TooLong;return result;}
    result.status=Status::Complete;return result;
}
bool Constrain(const Mesh& m,Point from,Point desired,Location& result) {
    if(!Finite(from)||!Finite(desired)||!m.data)return false;auto* query=Query(m);if(!query)return false;auto& q=*query;
    dtQueryFilter filter;dtPolyRef ref=0;Point near;
    if(!Nearest(q,from,.15f,2.f,filter,ref,near))return false;
    auto goal=ToNav(desired);float start[3]={near.x,near.y,near.z},end[3]={goal.x,goal.y,goal.z},normal[3]{},t=0;
    dtPolyRef visited[512];int count=0;
    auto status=q.raycast(ref,start,end,&filter,&t,normal,visited,&count,512);
    if(dtStatusFailed(status)||Limited(status)||t<1.f||!count)return false;
    float height=0;if(dtStatusFailed(q.getPolyHeight(visited[count-1],end,&height)))return false;
    goal.y=height;result=Address(m,goal,visited[count-1]);return true;
}
}
