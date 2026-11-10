// Ported from bahonec morphs_head105_v1; owner-authorized X64SKINMORPH.
#pragma once
#include <algorithm>
#include <cmath>
#include "BodyShapeData.h"
namespace BodyShape { namespace NalaMorphs {
using Values=std::array<double,EditorSchema::Count>;
struct Input {double strength=25,dexterity=25;std::array<double,3>db{.3,.3,.85};};
struct Status {unsigned saturated=0;Input effective;};
inline bool Resolve(const Input&raw,Values&out,Status&status){
 status={};status.effective=raw;double*fields[]={&status.effective.strength,&status.effective.dexterity,&status.effective.db[0],&status.effective.db[1],&status.effective.db[2]};
 for(unsigned i=0;i<5;++i){double x=*fields[i];if(!std::isfinite(x))return false;double low=i<2?15:NalaMorphData::Knots[i-2][0],high=i<2?35:NalaMorphData::Knots[i-2][2];*fields[i]=std::clamp(x,low,high);if(*fields[i]!=x)status.saturated|=1u<<i;}
 const auto&q=status.effective;unsigned si=q.strength<25?0:1,di=q.dexterity<25?0:1;double x=(q.strength-(15+si*10))/10,y=(q.dexterity-(15+di*10))/10;
 for(unsigned k=0;k<out.size();++k){const auto&v=NalaMorphData::Stats;out[k]=((1-x)*v[si*3+di][k]+x*v[(si+1)*3+di][k])*(1-y)+((1-x)*v[si*3+di+1][k]+x*v[(si+1)*3+di+1][k])*y;
  for(unsigned axis=0;axis<3;++axis){double value=q.db[axis],center=NalaMorphData::Knots[axis][1];unsigned side=value<center?0:1;double endpoint=NalaMorphData::Knots[axis][side?2:0],t=(value-center)/(endpoint-center),base=v[4][k],sample=NalaMorphData::Database[axis*2+side][k];
   if(k==EditorSchema::height||k==EditorSchema::stature)out[k]*=1+t*(sample/base-1);else out[k]+=t*(sample-base);
  }
  if(!std::isfinite(out[k])||out[k]<NalaMorphData::Limits[k][0]-1e-9||out[k]>NalaMorphData::Limits[k][1]+1e-9)return false;
 }
 // Accepted Nala default: absolute editor head-size setting 105%, for every body profile.
 out[EditorSchema::headSize]=1.05;return true;
}
}

} // namespace BodyShape
