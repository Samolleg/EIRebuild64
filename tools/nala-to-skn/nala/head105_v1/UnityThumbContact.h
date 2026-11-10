#pragma once
#include "UnityThumbContactData.h"
namespace UnityHands {
inline double HandleDistance(V point,const UnityHandleGeometry::Shape&shape,V centre){
 if(shape.round||shape.polygon.size()<3){V delta=Sub(point,centre);return std::sqrt(delta.x*delta.x+delta.z*delta.z)-shape.radius;}
 double distance=1e9;bool inside=true;for(unsigned i=0;i<shape.polygon.size();++i){V a=shape.polygon[i],b=shape.polygon[(i+1)%shape.polygon.size()],e=Sub(b,a),delta=Sub(point,a);e.y=delta.y=0;if(e.x*delta.z-e.z*delta.x< -1e-9)inside=false;double t=std::clamp(Dot(delta,e)/std::max(Dot(e,e),1e-15),0.,1.);V radial=Sub(delta,Scale(e,t));distance=std::min(distance,std::sqrt(Dot(radial,radial)));}return inside?-distance:distance;
}
inline void ThumbContact(const Data&d,const UnityBodyAppearance::Profile&p,const Input&input,std::vector<M>&world){
 for(unsigned side=0;side<2;++side){if(input.grip[side]<.999)continue;unsigned holder=side;if(!input.solved[holder]&&input.primary>=0&&input.support==int(side))holder=unsigned(input.primary);if(!input.solved[holder]||!input.shapes[holder].known)continue;const auto&shape=input.shapes[holder];const auto&binding=input.bindings[holder];M inverse=Inverse(input.mounts[holder]);unsigned bone=UnityHandsData::Sides[side].fingers[0][0];Q original=Quaternion(world[bone]);const unsigned*ids=side==0?UnityThumbContactData::Side0:UnityThumbContactData::Side1;unsigned count=side==0?unsigned(std::size(UnityThumbContactData::Side0)):unsigned(std::size(UnityThumbContactData::Side1));
  auto cost=[&](const std::vector<M>&raw,double rotation){std::vector<M>scaled=raw;UnityBodyAppearance::ApplyScales(p,scaled);double worst=0,total=0;for(unsigned i=0;i<count;++i){V local=Point(inverse,Scale(UnityHandsV6::Vertex(d,scaled,ids[i]),p.scale));double distance=HandleDistance(local,shape,binding.point)*input.objectScale[holder];double penetration=std::max(.0005-distance,0.);worst=std::max(worst,penetration);total+=penetration*penetration;}return worst*worst+total/count+rotation*1e-6;};
  double best=cost(world,0);if(best<1e-7)continue;Q chosen=original;
  // Small CMC adjustments move the whole nearly-straight thumb. Distal joints
  // retain their fitted angles; source topology and bone lengths are unchanged.
  for(double abduction:{-.14,-.07,0.,.07,.14})for(double opposition:{-.07,0.,.07}){if(abduction==0&&opposition==0)continue;Q turn=Mul(UnityHandsV6::AxisAngle(UnityContactData::Sides[side].abduction,abduction),UnityHandsV6::AxisAngle(UnityContactData::Sides[side].opposition,opposition));Q q=Mul(original,turn);auto trial=world;Replace(d,trial,bone,q);double value=cost(trial,abduction*abduction+opposition*opposition);if(value<best){best=value;chosen=q;}}
  Replace(d,world,bone,chosen);
 }
}
}
