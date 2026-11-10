#pragma once
#include "UnityBodySizeData.h"
namespace UnityBodySize {
inline bool Apply(const std::array<UnityBody::M,15>&game,std::vector<UnityBody::V>&positions,
                  std::vector<UnityBody::M>&world,UnityBody::M&root,double bodyScale=Scale){
 using namespace UnityBody;
 if(!std::isfinite(bodyScale)||bodyScale<.5||bodyScale>1.5||world.size()<=std::max(Feet[0],Feet[1]))return false;
 for(V&v:positions)v=UnityBody::Scale(v,bodyScale);
 for(M&m:world)m.t=UnityBody::Scale(m.t,bodyScale);
 root=game[0];V oldCenter{},newCenter{};
 for(unsigned side=0;side<2;++side){oldCenter=Add(oldCenter,Point(game[13+side],GameSoles[side]));
  newCenter=Add(newCenter,Point(root,Point(world[Feet[side]],UnityBody::Scale(SourceSoles[side],bodyScale))));}
 V shift=UnityBody::Scale(Sub(oldCenter,newCenter),.5);
 if(!std::isfinite(Dot(shift,shift))||Dot(shift,shift)>.25)return false;
 root.t=Add(root.t,shift);return true;
}
}
