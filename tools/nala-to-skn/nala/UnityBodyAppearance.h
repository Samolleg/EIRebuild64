#pragma once
#include "UnityAppearanceData.h"
namespace UnityBodyAppearance {
using namespace UnityBody;
struct Input {std::array<float,3>current{},base{};std::array<unsigned char,3>stats{};};
struct Profile {double fat=0,legs=1,arms=1,waist=1,depth=1,breasts=1,scale=1;};
// Source MathfExtension.LerpPolarizado, scalar overload with invert=false.
inline double SourceScale(double low,double high,double parameter){
 parameter=std::clamp(parameter,-1.,1.);
 return 1.+(parameter<0?1.-low:high-1.)*parameter;
}
inline bool Resolve(const Input&input,Profile&p){
 for(float v:input.current)if(!std::isfinite(v)||std::abs(v)>16)return false;
 for(float v:input.base)if(!std::isfinite(v)||std::abs(v)>16)return false;
 if(!input.stats[0]||!input.stats[1])return false;
 // New, explicitly calibrated mapping. Native current complection already
 // carries its saved NPC base/engine modifiers; do not add the base twice.
 // Raw primary stats are a separate response centred at 25, not the untraced
 // addon RecalcComplection formula. Intellect never enters any shape output.
 double strength=std::clamp(double(input.current[0]),0.,1.);
 double grace=std::clamp(double(input.current[1]),0.,1.);
 double s=std::clamp((double(input.stats[0])-25.)/20.,-1.,1.);
 double d=std::clamp((double(input.stats[1])-25.)/20.,-1.,1.);
 double muscle=std::clamp(.3+.5*strength+.6*s,-.4,.9);
 p.fat=std::clamp(grace-.05+.1*s-.3*d,-.3,.7);
 p.legs=SourceScale(.75,1.2,muscle);p.arms=SourceScale(.7,1.175,muscle);
 double waist=std::clamp((grace-.5)*.8-.4*d,-.4,.4);
 p.waist=SourceScale(.7,1.2,waist);p.depth=SourceScale(.99,1.01,waist);
 p.breasts=SourceScale(.55,2.,std::clamp(.03+.1*strength+.04*s,-.15,.18));
 double tall=std::clamp(double(input.current[2]),0.,1.);
 p.scale=((1-tall)*UnityAppearanceData::Heights[0]+tall*UnityAppearanceData::Heights[1])/UnityAppearanceData::SourceHeight;
 return std::isfinite(p.scale)&&p.scale>=.5&&p.scale<=1.5;
}
inline void Morph(const Data&source,double fat,Data&out){
 out=source;const auto*delta=fat>=0?UnityAppearanceData::FatPositive:UnityAppearanceData::FatNegative;
 const auto*dn=fat>=0?UnityAppearanceData::NormalPositive:UnityAppearanceData::NormalNegative;
 for(unsigned i=0;i<std::size(UnityAppearanceData::FatPositive);++i){out.vertices[i]=Add(source.vertices[i],Scale(delta[i],std::abs(fat)));out.normals[i]=Unit(Add(source.normals[i],Scale(dn[i],std::abs(fat))));}
}
inline void ApplyScales(const Profile&p,std::vector<M>&world){
 std::array<M,std::size(UnityAppearanceData::Pivots)>affine;
 for(unsigned i=0;i<affine.size();++i){unsigned pivot=UnityAppearanceData::Pivots[i];M r=Proper(world[pivot]),s;
  if(i==0){s.r[0][0]=p.waist;s.r[2][2]=p.depth;}
  else if(i<5){s.r[0][0]=s.r[2][2]=p.arms;}
  else if(i<9){s.r[0][0]=s.r[2][2]=p.legs;}
  else{s.r[0][0]=s.r[1][1]=s.r[2][2]=p.breasts;}
  affine[i]=Product(Product(r,s),Inverse(r));affine[i].t=Sub(world[pivot].t,Rotate(affine[i],world[pivot].t));
 }
 for(unsigned i=0;i<world.size();++i){int group=UnityAppearanceData::Groups[i];if(group>=0)world[i]=Product(affine[unsigned(group)],world[i]);}
}
// Kinematic adaptation using source ranges and original bone weights. This is
// not the full source local-scale/ignore-parent-scale/physics control stack.
inline bool Compute(const Data&shaped,const Profile&p,const std::array<M,15>&game,
 std::vector<V>&out,std::vector<V>&normals,std::vector<M>&world){
 world=Retarget(shaped,game);LimitRetarget(shaped,world);Control(shaped,world);ApplyScales(p,world);
 std::vector<M>skin(shaped.bindInverse.size()),normalMatrix(skin.size());
 for(unsigned i=0;i<skin.size();++i){unsigned node=shaped.bindNode.empty()?i:shaped.bindNode[i];skin[i]=Product(world[node],shaped.bindInverse[i]);M inv=Inverse(skin[i]);
  for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)normalMatrix[i].r[row][col]=inv.r[col][row];}
 out.assign(shaped.vertices.size(),{});normals.assign(out.size(),{});
 for(unsigned i=0;i<out.size();++i){V n{};for(unsigned k=0;k<4;++k){unsigned bone=shaped.skin[i].bone[k];double weight=shaped.skin[i].weight[k];out[i]=Add(out[i],Scale(Point(skin[bone],shaped.vertices[i]),weight));n=Add(n,Scale(Rotate(normalMatrix[bone],shaped.normals[i]),weight));}
  if(!std::isfinite(Dot(out[i],out[i]))||!std::isfinite(Dot(n,n)))return false;normals[i]=Unit(n);}
 return true;
}
}
