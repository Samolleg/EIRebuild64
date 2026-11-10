#pragma once
#include "../UnityBodyAppearance.h"
#include "UnityHandsData.h"
#include "UnitySittingAnchors.h"
#include "UnityWeaponGripData.h"
namespace UnityHandsV6 {
using namespace UnityBody;
struct Input {double sitting=0;std::array<double,2>grip{};bool weapon=false,twoHand=false;M oldRelative{};};
inline V GripCentre(unsigned side){const auto&s=UnityHandsData::Sides[side];return Add(s.palm,Add(Scale(s.forward,.020),Scale(s.normal,.012)));}
inline M WeaponRelative(const Data&d,const UnityBodyAppearance::Profile&p,const Input&input,bool adjust){
 const auto&s=UnityHandsData::Sides[1];M target=Proper(d.rest[s.hand]);M old=Proper(Product(Inverse(d.gameBind[0]),d.gameBind[8]));
 M local=Product(Product(Inverse(target),old),input.oldRelative);if(adjust){V centre=Scale(GripCentre(1),p.scale),point=Point(local,UnityHandsData::ShaftPoint),axis=Unit(Rotate(local,UnityHandsData::ShaftAxis));
  local.t=Add(local.t,Sub(centre,Add(point,Scale(axis,Dot(Sub(centre,point),axis)))));}return local;
}
inline M WeaponPose(const Data&d,const UnityBodyAppearance::Profile&p,const Input&input,const std::vector<M>&world,bool adjust){
 M hand=Proper(world[UnityHandsData::Sides[1].hand]);hand.t=Scale(world[UnityHandsData::Sides[1].hand].t,p.scale);return Product(hand,WeaponRelative(d,p,input,adjust));
}
inline Q AxisAngle(V axis,double angle){axis=Unit(axis);double s=std::sin(angle/2);return {std::cos(angle/2),axis.x*s,axis.y*s,axis.z*s};}
inline Q Between(V a,V b){a=Unit(a);b=Unit(b);double dot=std::clamp(Dot(a,b),-1.,1.);if(dot>1-1e-13)return {1,0,0,0};
 if(dot< -1+1e-10){V seed=std::abs(a.x)<.7?V{1,0,0}:V{0,1,0};return AxisAngle(Cross(a,seed),3.14159265358979323846);}V cross=Cross(a,b);return Normalize({1+dot,cross.x,cross.y,cross.z});}
inline M Basis(V forward,V normal){normal=Unit(normal);forward=Unit(Sub(forward,Scale(normal,Dot(forward,normal))));V across=Unit(Cross(normal,forward));M r;
 V columns[]={forward,across,normal};for(unsigned i=0;i<3;++i){r.r[0][i]=columns[i].x;r.r[1][i]=columns[i].y;r.r[2][i]=columns[i].z;}return r;}
// Two-bone solve preserves source segment lengths and the animation's elbow plane.
inline bool Reach(const Data&d,std::vector<M>&w,const UnityHandsData::Side&s,V target,Q orientation,double weight){
 weight=std::clamp(weight,0.,1.);if(weight<=0)return true;
 V shoulder=w[s.upper].t,elbow=w[s.forearm].t,wrist=w[s.hand].t;
 target=Add(wrist,Scale(Sub(target,wrist),weight));orientation=Nlerp(Quaternion(w[s.hand]),orientation,weight);
 double a=std::sqrt(Dot(Sub(elbow,shoulder),Sub(elbow,shoulder))),b=std::sqrt(Dot(Sub(wrist,elbow),Sub(wrist,elbow)));
 V ray=Sub(target,shoulder);double distance=std::sqrt(Dot(ray,ray));if(a<.05||b<.05||distance<1e-8)return false;
 V direction=Scale(ray,1/distance);distance=std::clamp(distance,std::abs(a-b)+.001,a+b-.001);target=Add(shoulder,Scale(direction,distance));
 V pole=Sub(Sub(elbow,shoulder),Scale(direction,Dot(Sub(elbow,shoulder),direction)));
 if(Dot(pole,pole)<1e-10){pole=Cross(direction,Rotate(w[s.upper],{0,0,1}));if(Dot(pole,pole)<1e-10)pole=Cross(direction,{1,0,0});}
 pole=Unit(pole);double x=(a*a+distance*distance-b*b)/(2*distance),height=std::sqrt(std::max(0.,a*a-x*x));
 V desiredElbow=Add(shoulder,Add(Scale(direction,x),Scale(pole,height)));
 Replace(d,w,s.upper,Mul(Between(Sub(elbow,shoulder),Sub(desiredElbow,shoulder)),Quaternion(w[s.upper])));
 Replace(d,w,s.forearm,Mul(Between(Sub(w[s.hand].t,w[s.forearm].t),Sub(target,w[s.forearm].t)),Quaternion(w[s.forearm])));
 Replace(d,w,s.hand,orientation);return std::sqrt(Dot(Sub(w[s.hand].t,target),Sub(w[s.hand].t,target)))<1e-6;
}
inline void Fingers(const Data&d,std::vector<M>&w,unsigned side,double grip,double relaxed){
 const auto&s=UnityHandsData::Sides[side];grip=std::clamp(grip,0.,1.);relaxed=std::clamp(relaxed,0.,1.);if(grip==0&&relaxed==0)return;
 constexpr double pi=3.14159265358979323846;
 for(unsigned finger=0;finger<5;++finger)for(unsigned joint=0;joint<3;++joint){
  const double closed[3]={finger?62.:28.,finger?88.:38.,finger?48.:24.};const double loose[3]={8,12,6};
  double angle=(closed[joint]*grip+loose[joint]*relaxed*(1-grip))*pi/180;
  unsigned bone=s.fingers[finger][joint];Replace(d,w,bone,Mul(Quaternion(w[bone]),AxisAngle(s.axes[finger][joint],angle)));
 }
}
inline bool SkinWorld(const Data&d,const std::vector<M>&world,std::vector<V>&out,std::vector<V>&normals){
 std::vector<M>skin(d.bindInverse.size()),normalMatrix(skin.size());for(unsigned i=0;i<skin.size();++i){unsigned node=d.bindNode.empty()?i:d.bindNode[i];skin[i]=Product(world[node],d.bindInverse[i]);M inv=Inverse(skin[i]);for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)normalMatrix[i].r[r][c]=inv.r[c][r];}
 out.assign(d.vertices.size(),{});normals.assign(out.size(),{});for(unsigned i=0;i<out.size();++i){V n{};for(unsigned k=0;k<4;++k){unsigned bone=d.skin[i].bone[k];double weight=d.skin[i].weight[k];out[i]=Add(out[i],Scale(Point(skin[bone],d.vertices[i]),weight));n=Add(n,Scale(Rotate(normalMatrix[bone],d.normals[i]),weight));}
  if(!std::isfinite(Dot(out[i],out[i]))||!std::isfinite(Dot(n,n)))return false;normals[i]=Unit(n);}return true;
}
inline V Vertex(const Data&d,const std::vector<M>&world,unsigned index){V out{};for(unsigned k=0;k<4;++k){unsigned bone=d.skin[index].bone[k];unsigned node=d.bindNode.empty()?bone:d.bindNode[bone];out=Add(out,Scale(Point(Product(world[node],d.bindInverse[bone]),d.vertices[index]),d.skin[index].weight[k]));}return out;}
inline bool Compute(const Data&d,const UnityBodyAppearance::Profile&p,const std::array<M,15>&game,const Input&input,
 std::vector<V>&v,std::vector<V>&n,std::vector<M>&world){
 if(!std::isfinite(input.sitting)||!std::isfinite(input.grip[0])||!std::isfinite(input.grip[1]))return false;
 if(input.sitting<=0&&input.grip[0]<=0&&input.grip[1]<=0)return UnityBodyAppearance::Compute(d,p,game,v,n,world);
 world=Retarget(d,game);LimitRetarget(d,world);
 if(input.weapon&&input.twoHand){const auto&s=UnityHandsData::Sides[0];M weapon=WeaponPose(d,p,input,world,true);V axis=Unit(Rotate(weapon,UnityHandsData::ShaftAxis)),point=Point(weapon,UnityHandsData::ShaftPoint);
  V own=Add(Scale(world[s.hand].t,p.scale),Rotate(world[s.hand],Scale(GripCentre(0),p.scale)));
  V contact=Add(point,Scale(axis,Dot(Sub(own,point),axis)));V target=Scale(Sub(contact,Rotate(world[s.hand],Scale(GripCentre(0),p.scale))),1/p.scale);
  if(!Reach(d,world,s,target,Quaternion(world[s.hand]),1))return false;
 }
 std::vector<M>preWorld;if(input.sitting>0){preWorld=world;Control(d,preWorld);UnityBodyAppearance::ApplyScales(p,preWorld);}
 if(input.sitting>0)for(unsigned side=0;side<2;++side){const auto&s=UnityHandsData::Sides[side];const auto&t=UnityHandsData::SittingTriangles[side];
  V a=Vertex(d,preWorld,t[0]),b=Vertex(d,preWorld,t[1]),c=Vertex(d,preWorld,t[2]),surfaceNormal=Unit(Cross(Sub(b,a),Sub(c,a)));
  V point=Add(Scale(Add(Add(a,b),c),1./3),Scale(surfaceNormal,.007));
  M desired=Product(Basis(Sub(preWorld[s.calf].t,preWorld[s.thigh].t),Scale(surfaceNormal,-1)),Inverse(Basis(s.forward,s.normal)));
  V wrist=Sub(point,Rotate(desired,s.palm));if(!Reach(d,world,s,wrist,Quaternion(desired),input.sitting*(1-std::clamp(input.grip[side],0.,1.))))return false;
 }
 Control(d,world);for(unsigned side=0;side<2;++side)Fingers(d,world,side,input.grip[side],input.sitting);
 UnityBodyAppearance::ApplyScales(p,world);return SkinWorld(d,world,v,n);
}
}
