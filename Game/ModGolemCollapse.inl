#include <algorithm>
#include <cmath>
#include "ModGolemCollapseLayout.inl"
// Pure trajectory shared by native playback and offline geometry validation.
namespace Collapse {
struct Vec {float x,y,z;};
struct Quat {float w,x,y,z;};
struct Pose {Vec position;Quat rotation;float bottom;bool visible;};
static Quat Mul(Quat a,Quat b){return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};}
static Vec Rotate(Quat q,Vec v){Vec t={2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,v.z+q.w*t.z+q.x*t.y-q.y*t.x};}
static float Clamp(float f){return (std::max)(0.f,(std::min)(1.f,f));}
static Quat Axis(int axis,float angle){float c=std::cos(angle*.5f),s=std::sin(angle*.5f);return {c,axis==0?s:0,axis==1?s:0,axis==2?s:0};}
static Pose Sample(const CollapsePiece&p,int index,float frame,bool clearing=false){
 float t=(frame-p.delay)/18.f;if(t<0&&!clearing)return {{7,6,-40},{1,0,0,0},-40,false};
 constexpr float flight=.80f;float fall=Clamp(t/flight),after=(std::max)(0.f,t-flight);
 float settle=after<.55f?std::exp(-after*11)*std::sin(after*27):0;
 float remain=1-fall;
 float rx=(1.65f+.25f*(index%3))*remain+.13f*settle;
 float ry=(index%2?-1.05f:.90f)*remain-.09f*settle;
 float yaw=p.yaw*.01745329252f+(index%2?-1.f:1.f)*(.9f+.1f*index)*remain+.05f*settle;
 Quat q=Mul(Axis(2,yaw),Mul(Axis(1,ry),Axis(0,rx)));
 float z=t<flight?9.f*(1-fall*fall):.17f*std::abs(settle);
 float x=p.x+(index%2?-.20f:.24f)*remain*remain,y=p.y-.22f*remain*remain;
 if(clearing){float u=Clamp(frame/72.f);u=u*u*(3-2*u);q=Mul(Axis(2,p.yaw*.01745329252f),Axis(1,(index%2?1.f:-1.f)*u*.9f));x=p.x+(p.x<62?-5.5f:5.5f)*u;y=p.y;z=-(p.height+12)*u;}
 Vec pivot={0,0,p.height*.5f};float low=1.e20f;
 for(int i=0;i<p.count;i++){Vec v={p.points[i][0]-pivot.x,p.points[i][1]-pivot.y,p.points[i][2]-pivot.z};auto r=Rotate(q,v);low=(std::min)(low,r.z);}
 auto pivotRot=Rotate(q,pivot);
 return {{x-pivotRot.x,y-pivotRot.y,z-low-pivotRot.z},q,z,true};
}
}
