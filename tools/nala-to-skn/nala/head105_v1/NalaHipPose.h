#pragma once
// Shared post-skinning, pre-size pose corrective. Not a physics simulation.
namespace NalaHipPose {
using namespace UnityBody;
struct Ramp {double value=0,derivative=0;};
inline Ramp Smooth(double x){if(x<=0)return {};if(x>=1)return {1,0};return {x*x*(3-2*x),6*x*(1-x)};}
inline Ramp Band(double x,double a,double b,double low,double high){if(x<a){auto r=Smooth((x-low)/(a-low));r.derivative/=a-low;return r;}auto r=Smooth((high-x)/(high-b));r.derivative/=-(high-b);return r;}
inline bool Under(const Data&d,unsigned node,unsigned root){for(int i=int(node);i>=0;i=d.parent[unsigned(i)])if(unsigned(i)==root)return true;return false;}
inline double Activation(const Data&d,const std::vector<M>&w,unsigned thigh,unsigned calf){
 M delta=Product(Proper(w[156]),Inverse(Rotation(d.restQ[156])));V a=Unit(Rotate(delta,Sub(d.rest[calf].t,d.rest[thigh].t))),b=Unit(Sub(w[calf].t,w[thigh].t));
 constexpr double degree=3.14159265358979323846/180.;return Smooth((std::acos(std::clamp(Dot(a,b),-1.,1.))-45*degree)/(35*degree)).value;
}
inline bool Apply(const Data&d,const UnityBodyAppearance::Profile&profile,const std::vector<M>&world,std::vector<V>&positions,std::vector<V>&normals,double sitting){
 if(sitting<=0)return true;if(world.size()!=d.rest.size()||positions.size()!=d.vertices.size()||normals.size()!=positions.size())return false;
 double activation=std::clamp(sitting,0.,1.)*.5*(Activation(d,world,173,176)+Activation(d,world,157,160));if(activation==0)return true;
 std::vector<M>neutral=d.rest;Control(d,neutral);UnityBodyAppearance::ApplyScales(profile,neutral);
 std::vector<M>skin(d.bindInverse.size());std::vector<bool>glute(skin.size()),pelvic(skin.size());
 for(unsigned i=0;i<skin.size();++i){unsigned node=d.bindNode.empty()?i:d.bindNode[i];skin[i]=Product(neutral[node],d.bindInverse[i]);glute[i]=Under(d,node,344)||Under(d,node,350);pelvic[i]=Under(d,node,156)||node==0||node==1||node==2||node==3||node==72||node==155;}
 M frame=Product(Proper(world[156]),Inverse(Rotation(d.restQ[156])));frame.t=world[156].t;M inverse=Inverse(frame);
 double back=-1e9,posedBack=-1e9,halfWidth=0,low=1e9,high=-1e9;
 for(unsigned i=0;i<positions.size();++i){double weight=0;for(unsigned k=0;k<4;++k)if(glute[d.skin[i].bone[k]])weight+=d.skin[i].weight[k];if(weight<=.15)continue;V p{};
  for(unsigned k=0;k<4;++k)p=Add(p,Scale(Point(skin[d.skin[i].bone[k]],d.vertices[i]),d.skin[i].weight[k]));p=Sub(p,d.rest[156].t);
  back=std::max(back,p.y);halfWidth=std::max(halfWidth,std::abs(p.x));low=std::min(low,p.z);high=std::max(high,p.z);posedBack=std::max(posedBack,Point(inverse,positions[i]).y);
 }
 if(!(back>1e-4&&halfWidth>1e-4&&high>low))return true;
 const double size=halfWidth/.20,cutoff=back*.20,allowance=.010*size;
 const double amount=activation*std::clamp((posedBack-back-allowance)/std::max(.001,posedBack-cutoff),0.,.40);if(amount<=0)return true;
 const double blend=.045*size,outerWidth=halfWidth+.045*size,innerWidth=halfWidth*.68;
 for(unsigned i=0;i<positions.size();++i){bool eligible=false;for(unsigned k=0;k<4;++k)eligible|=d.skin[i].weight[k]>.01&&pelvic[d.skin[i].bone[k]];if(!eligible)continue;
  V p=Point(inverse,positions[i]);auto x=Band(p.x,-innerWidth,innerWidth,-outerWidth,outerWidth),z=Band(p.z,low-.010*size,high,low-.080*size,high+.065*size);double y=p.y-cutoff;auto ramp=Smooth(y/blend);if(x.value*z.value*ramp.value==0)continue;
  double soft=y*ramp.value,dx=-amount*x.derivative*z.value*soft,dz=-amount*x.value*z.derivative*soft,dy=1-amount*x.value*z.value*(ramp.value+y*ramp.derivative/blend);if(!(dy>.30))return false;
  V n=Rotate(inverse,normals[i]);n={n.x-dx*n.y/dy,n.y/dy,n.z-dz*n.y/dy};p.y-=amount*x.value*z.value*soft;positions[i]=Point(frame,p);normals[i]=Unit(Rotate(frame,n));
  if(!std::isfinite(Dot(positions[i],positions[i]))||!std::isfinite(Dot(normals[i],normals[i])))return false;
 }return true;
}
}
