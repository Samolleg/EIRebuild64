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
Location Address(const Mesh& m,Point p,dtPolyRef ref,float height) {Location l;l.position=FromNav(p);l.surface=1;l.radius=m.profile.radius;l.height=height;l.polygon=ref;l.generation=m.generation;
    unsigned char area=1;m.data->getPolyArea(ref,&area);l.area=area;return l;}
bool Nearest(dtNavMeshQuery& q,Point point,float horizontal,float vertical,const dtQueryFilter& filter,dtPolyRef& ref,Point& result) {
    if(!Finite(point)||!std::isfinite(horizontal)||horizontal<0||!std::isfinite(vertical)||vertical<=0)return false;
    auto p=ToNav(point);float pos[3]={p.x,p.y,p.z},ext[3]={horizontal,vertical,horizontal},near[3]{};ref=0;
    auto status=q.findNearestPoly(pos,ext,&filter,&ref,near);
    if(dtStatusFailed(status)||!ref)return false;
    result={near[0],near[1],near[2]};
    return std::hypot(p.x-result.x,p.z-result.z)<=horizontal+.001f && std::abs(p.y-result.y)<=vertical;
}
// Clip a line parameter to the non-negative half-plane v + t*d >= 0.
bool Clip(float v,float d,float& low,float& high) {
    if(std::abs(d)<1.e-8f)return v>=-1.e-6f;
    float t=-v/d;if(d>0)low=std::max(low,t);else high=std::min(high,t);
    return low<=high+1.e-6f;
}
float Cross(Point a,Point b,Point p) {return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);}
float SegmentDistance2(Point p,Point a,Point b) {
    float dx=b.x-a.x,dy=b.y-a.y,n=dx*dx+dy*dy;
    float t=n>0?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/n,0.f,1.f):0;
    dx=p.x-a.x-t*dx;dy=p.y-a.y-t*dy;return dx*dx+dy*dy;
}
// Only an existing source may leave the eroded fringe. Prove continuous MPR
// support and body clearance on the complete short ingress, never snap/teleport.
// The regular corridor, placement and target still use the full eroded mesh.
bool GroundIngress(const Mesh& m,Point from,Point to,Point& supportedFrom,Point& supportedTo,float bodyHeight) {
    if(!m.geometry)return false;
    std::vector<std::pair<float,float>> spans;
    bool haveFrom=false,haveTo=false;float fromZ=0,toZ=0;
    const float tolerance=m.profile.climb+2*m.profile.cellHeight;
    for(const auto& triangle:m.geometry->ground) {
        const auto t=Surface(triangle,m.profile);
        if(!t.area || std::max({t.a.x,t.b.x,t.c.x})<std::min(from.x,to.x) ||
            std::min({t.a.x,t.b.x,t.c.x})>std::max(from.x,to.x) ||
            std::max({t.a.y,t.b.y,t.c.y})<std::min(from.y,to.y) ||
            std::min({t.a.y,t.b.y,t.c.y})>std::max(from.y,to.y))continue;
        float det=Cross(t.a,t.b,t.c);if(det<=1.e-8f)continue;
        auto u=[&](Point p){return Cross(t.a,p,t.c)/det;};
        auto v=[&](Point p){return Cross(t.a,t.b,p)/det;};
        float u0=u(from),v0=v(from),du=u(to)-u0,dv=v(to)-v0,low=0,high=1;
        if(!Clip(u0,du,low,high)||!Clip(v0,dv,low,high)||!Clip(1-u0-v0,-du-dv,low,high))continue;
        auto height=[&](float at){return t.a.z+(u0+du*at)*(t.b.z-t.a.z)+(v0+dv*at)*(t.c.z-t.a.z);};
        const float h0=height(low),h1=height(high);
        const float feet0=from.z+(to.z-from.z)*low,feet1=from.z+(to.z-from.z)*high;
        if(std::abs(h0-feet0)>tolerance||std::abs(h1-feet1)>tolerance)continue;
        const float ux=t.b.x-t.a.x,uy=t.b.y-t.a.y,uz=t.b.z-t.a.z;
        const float vx=t.c.x-t.a.x,vy=t.c.y-t.a.y,vz=t.c.z-t.a.z;
        const float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
        if(nz<=0||nz/std::sqrt(nx*nx+ny*ny+nz*nz)<std::cos(m.profile.slope*3.14159265f/180.f))continue;
        spans.emplace_back(low,high);
        if(low<=1.e-6f&&(!haveFrom||std::abs(height(0)-from.z)<std::abs(fromZ-from.z))){fromZ=height(0);haveFrom=true;}
        if(high>=1-1.e-6f&&(!haveTo||std::abs(height(1)-to.z)<std::abs(toZ-to.z))){toZ=height(1);haveTo=true;}
    }
    if(!haveFrom||!haveTo)return false;
    std::sort(spans.begin(),spans.end());float covered=0;
    for(auto span:spans){if(span.first>covered+1.e-5f)return false;covered=std::max(covered,span.second);}
    if(covered<1-1.e-5f)return false;
    supportedFrom=from;supportedFrom.z=fromZ;supportedTo=to;supportedTo.z=toZ;
    // Use the resolved floor heights, rather than coarse legacy hints, when
    // checking the head against an overhead walkable part (either winding).
    for(const auto& triangle:m.geometry->ground) {
        const auto t=Surface(triangle,m.profile);
        float det=Cross(t.a,t.b,t.c);if(std::abs(det)<=1.e-8f)continue;
        float u0=Cross(t.a,from,t.c)/det,v0=Cross(t.a,t.b,from)/det;
        float du=Cross(t.a,to,t.c)/det-u0,dv=Cross(t.a,t.b,to)/det-v0,low=0,high=1;
        if(!Clip(u0,du,low,high)||!Clip(v0,dv,low,high)||!Clip(1-u0-v0,-du-dv,low,high))continue;
        auto gap=[&](float at){return t.a.z+(u0+du*at)*(t.b.z-t.a.z)+(v0+dv*at)*(t.c.z-t.a.z)-(fromZ+(toZ-fromZ)*at);};
        float a=gap(low),b=gap(high);
        if(std::max(a,b)>.1f&&std::min(a,b)<bodyHeight)return false;
    }
    for(const auto& solid:m.geometry->solids) {
        float bottom,top;auto hull=Footprint(solid,bottom,top);
        if(hull.size()<9 || std::max(fromZ,toZ)+bodyHeight<=bottom || std::min(fromZ,toZ)>=top)continue;
        std::vector<Point> polygon;
        for(size_t i=0;i<hull.size();i+=3)polygon.push_back({hull[i],-hull[i+2],0});
        float area=0;for(size_t i=0;i<polygon.size();++i){auto a=polygon[i],b=polygon[(i+1)%polygon.size()];area+=a.x*b.y-b.x*a.y;}
        float sign=area>0?1.f:-1.f,low=0,high=1;bool intersects=true;
        float startDistance=std::numeric_limits<float>::max(),segmentDistance=startDistance;
        for(size_t i=0;i<polygon.size();++i) {
            auto a=polygon[i],b=polygon[(i+1)%polygon.size()];
            float s=sign*Cross(a,b,from),e=sign*Cross(a,b,to);
            if(!Clip(s,e-s,low,high))intersects=false;
            startDistance=std::min(startDistance,SegmentDistance2(from,a,b));
            segmentDistance=std::min({segmentDistance,SegmentDistance2(from,a,b),SegmentDistance2(to,a,b),SegmentDistance2(a,from,to)});
        }
        // Never cross a raw solid, nor increase an initial radius overlap.
        if(intersects)return false;
        float clearance=std::min(m.profile.radius,std::sqrt(startDistance));
        if(std::sqrt(segmentDistance)+.001f<clearance)return false;
    }
    return true;
}
bool Start(const Mesh& m,dtNavMeshQuery& q,Point p,const dtQueryFilter& filter,dtPolyRef& ref,Point& near,Point& actual,bool& ingress,float height) {
    ingress=false;
    if(Nearest(q,p,.05f,2.f,filter,ref,near)){actual=FromNav(near);return true;}
    if(!Nearest(q,p,m.profile.radius+2*m.profile.cellSize,2.f,filter,ref,near))return false;
    Point end;if(!GroundIngress(m,p,FromNav(near),actual,end,height))return false;
    ingress=true;return true;
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
    if(!Start(m,q,r.source,filter,from,source,actual,ingress,height)){result.status=Status::NoStart;return result;}
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
bool Constrain(const Mesh& m,Point from,Point desired,Location& result,float height) {
    if(!Finite(from)||!Finite(desired)||!m.data||!m.tiles)return false;auto* query=Query(m);if(!query)return false;auto& q=*query;
    height=height>0?height:m.profile.height;const auto mask=ClearanceMask(m.profile,height);if(!mask)return false;
    dtQueryFilter filter;filter.setIncludeFlags(mask);dtPolyRef ref=0;Point near;
    Point actual;bool ingress=false;
    if(!Start(m,q,from,filter,ref,near,actual,ingress,height))return false;
    if(ingress) {
        if(std::hypot(desired.x-from.x,desired.y-from.y)<.00001f){result=Address(m,ToNav(actual),ref,height);return true;} // Turning/standing, no ingress step.
        if(std::hypot(desired.x-from.x,desired.y-from.y)>m.profile.radius+2*m.profile.cellSize)return false;
        dtPolyRef target=0;Point next;
        if(!Nearest(q,desired,m.profile.radius+2*m.profile.cellSize,2.f,filter,target,next))return false;
        float oldDistance=std::hypot(from.x-near.x,from.y+near.z),newDistance=std::hypot(desired.x-next.x,desired.y+next.z);
        if(newDistance>=oldDistance-.0001f)return false;
        Point begin,end;if(!GroundIngress(m,actual,desired,begin,end,height))return false;
        result=Address(m,ToNav(end),target,height);return true;
    }
    auto goal=ToNav(desired);float start[3]={near.x,near.y,near.z},end[3]={goal.x,goal.y,goal.z},normal[3]{},t=0;
    dtPolyRef visited[512];int count=0;
    auto status=q.raycast(ref,start,end,&filter,&t,normal,visited,&count,512);
    if(dtStatusFailed(status)||Limited(status)||t<1.f||!count)return false;
    float support=0;if(dtStatusFailed(q.getPolyHeight(visited[count-1],end,&support)))return false;
    goal.y=support;result=Address(m,goal,visited[count-1],height);return true;
}
}
