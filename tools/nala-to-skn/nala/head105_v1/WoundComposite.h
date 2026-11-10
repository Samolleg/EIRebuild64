#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace NalaWoundComposite {
inline void Pixel(const std::uint8_t* base,std::uint8_t coverage,const std::uint8_t* masks,
                  const std::array<std::uint8_t,6>& levels,std::uint8_t* output){
 unsigned alpha=0;
 for(unsigned i=0;i<6;++i)if(levels[i])alpha=std::max(alpha,unsigned(masks[i]));
 alpha=(alpha*unsigned(coverage)+127)/255;
 constexpr unsigned blood[3]={105,20,17};
 for(unsigned c=0;c<3;++c)output[c]=std::uint8_t((unsigned(base[c])*(255-alpha)+blood[c]*alpha+127)/255);
}
// Masks contain opacity, indexed by hit location and severity (1..3).
// Coverage is 255 for exposed skin and 0 for covered/non-skin surfaces.
inline bool Apply(const std::uint8_t* base,std::uint8_t* output,const std::uint8_t* exposed,
                  std::size_t pixels,const std::array<std::uint8_t,6>& levels,
                  const std::array<std::array<const std::uint8_t*,3>,6>& masks) {
 if(!base||!output||!exposed||!pixels)return false;
 for(unsigned area=0;area<6;++area)if(levels[area]>3||(levels[area]&&!masks[area][levels[area]-1]))return false;
 for(std::size_t i=0;i<pixels;++i){
  unsigned alpha=0;
  for(unsigned area=0;area<6;++area)if(levels[area])alpha=std::max(alpha,unsigned(masks[area][levels[area]-1][i]));
  alpha=(alpha*unsigned(exposed[i])+127)/255;
  constexpr unsigned blood[3]={105,20,17};
  for(unsigned c=0;c<3;++c)output[3*i+c]=std::uint8_t((unsigned(base[3*i+c])*(255-alpha)+blood[c]*alpha+127)/255);
 }
 return true;
}
}
