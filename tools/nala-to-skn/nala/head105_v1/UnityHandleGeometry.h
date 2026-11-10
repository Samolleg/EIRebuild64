#pragma once
#include "NativeWeaponGeometry.h"
#include "UnityHandleData.h"
#include "UnityStaffShaftData.h"
#include "Bone03ShoulderShaft.h"
namespace UnityHandleGeometry {
using namespace UnityBody;
struct Shape {bool known=false,round=true;std::vector<V>polygon;double radius=.012;};
inline std::vector<V> Hull(std::vector<V>p){std::sort(p.begin(),p.end(),[](V a,V b){return a.x<b.x||(a.x==b.x&&a.z<b.z);});p.erase(std::unique(p.begin(),p.end(),[](V a,V b){return std::abs(a.x-b.x)<1e-7&&std::abs(a.z-b.z)<1e-7;}),p.end());if(p.size()<3)return {};
 auto turn=[](V a,V b,V c){return(b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x);};std::vector<V>h;for(V v:p){while(h.size()>1&&turn(h[h.size()-2],h.back(),v)<=1e-9)h.pop_back();h.push_back(v);}unsigned first=unsigned(h.size());for(auto i=p.rbegin()+1;i!=p.rend();++i){while(h.size()>first&&turn(h[h.size()-2],h.back(),*i)<=1e-9)h.pop_back();h.push_back(*i);}if(h.size()>1)h.pop_back();return h;
}
inline bool Describe(const UnityGame::WeaponGeometry&geometry,const std::string&name,V&point,double&radius,Shape&shape,bool usePointPlane=false){const auto*d=UnityHandleData::Find(name);if(!d||!d->known)return false;std::vector<V>p;double plane=usePointPlane?point.y:d->point.y;
 if(d->shield){for(unsigned i=0;i<d->vertexCount;++i){V v;if(!geometry.Point(name,UnityHandleData::Vertices[d->vertices+i],v))return false;p.push_back(v);}if(p.empty())return false;V low=p[0],high=p[0];for(V v:p){low={std::min(low.x,v.x),std::min(low.y,v.y),std::min(low.z,v.z)};high={std::max(high.x,v.x),std::max(high.y,v.y),std::max(high.z,v.z)};}point=Scale(Add(low,high),.5);radius=std::max(high.x-low.x,high.z-low.z)*.5;shape.known=true;shape.round=true;shape.radius=radius;return true;}
 bool staffShaft=name=="rh3.dstaff05",bone0=usePointPlane&&name=="rh3.staff03",bone1=usePointPlane&&name=="rh3.dstaff03",axe0=usePointPlane&&name=="rh3.daxe00";unsigned edgeCount=axe0?unsigned(std::size(Bone03ShoulderShaft::Edges2)):bone0?unsigned(std::size(Bone03ShoulderShaft::Edges0)):bone1?unsigned(std::size(Bone03ShoulderShaft::Edges1)):staffShaft?unsigned(std::size(UnityStaffShaftData::Edges)):d->count;
 for(unsigned i=0;i<edgeCount;++i){const auto&e=axe0?Bone03ShoulderShaft::Edges2[i]:bone0?Bone03ShoulderShaft::Edges0[i]:bone1?Bone03ShoulderShaft::Edges1[i]:staffShaft?UnityStaffShaftData::Edges[i]:UnityHandleData::Edges[d->edges+i];V a,b;if(!geometry.Point(name,e[0],a)||!geometry.Point(name,e[1],b))return false;if(std::abs(b.y-a.y)<1e-10)continue;double t=(plane-a.y)/(b.y-a.y);if(t<0||t>1)continue;V v=Add(a,Scale(Sub(b,a),t));v.y=plane;p.push_back(v);}
 auto polygon=Hull(p);if(polygon.size()<3)return false;V low=polygon[0],high=low;for(V v:polygon){low.x=std::min(low.x,v.x);low.z=std::min(low.z,v.z);high.x=std::max(high.x,v.x);high.z=std::max(high.z,v.z);}point={.5*(low.x+high.x),plane,.5*(low.z+high.z)};
 unsigned corners=0;for(unsigned i=0;i<polygon.size();++i){V a=Unit(Sub(polygon[i],polygon[(i+polygon.size()-1)%polygon.size()])),b=Unit(Sub(polygon[(i+1)%polygon.size()],polygon[i]));if(Dot(a,b)<std::cos(.20))++corners;}
 radius=0;double mean=0,squares=0;for(V v:polygon){double r=std::sqrt(Dot(Sub(v,point),Sub(v,point)));radius=std::max(radius,r);mean+=r;squares+=r*r;}mean/=polygon.size();double deviation=std::sqrt(std::max(0.,squares/polygon.size()-mean*mean));shape.known=true;shape.round=corners>=6&&deviation<std::max(mean*.08,.0002);shape.polygon=std::move(polygon);shape.radius=radius;return std::isfinite(radius)&&radius>.001&&radius<.08;
}
struct Section {bool round=true;V palmDirection{},point{};double radius=.012,aspect=1;};
inline Section Facing(const Shape&s,const M&mount,V centre,V desiredNormal,V axis){Section out;out.point=centre;axis=Unit(axis);desiredNormal=Unit(Sub(desiredNormal,Scale(axis,Dot(desiredNormal,axis))));out.palmDirection=desiredNormal;out.radius=s.radius;if(s.round||s.polygon.size()<3)return out;
 V local=Rotate(Inverse(mount),Scale(desiredNormal,-1)),normal{};double best=-1e9;
 for(unsigned i=0;i<s.polygon.size();++i){V edge=Sub(s.polygon[(i+1)%s.polygon.size()],s.polygon[i]);V n=Unit({edge.z,0,-edge.x});double score=Dot(n,local);if(score>best){best=score;normal=n;}}
 V tangent=Unit(Cross({0,1,0},normal));double loN=1e9,hiN=-1e9,loT=1e9,hiT=-1e9;for(V point:s.polygon){V v=Sub(point,centre);double a=Dot(v,normal),b=Dot(v,tangent);loN=std::min(loN,a);hiN=std::max(hiN,a);loT=std::min(loT,b);hiT=std::max(hiT,b);}
 out.round=false;out.point=Add(centre,Add(Scale(normal,.5*(loN+hiN)),Scale(tangent,.5*(loT+hiT))));out.radius=.5*(hiN-loN);out.aspect=std::clamp((hiT-loT)/(hiN-loN),.7,2.);out.palmDirection=Scale(Unit(Rotate(mount,normal)),-1);return out;
}
}
