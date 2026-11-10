#pragma once
#include <cstdint>
namespace LameLocomotion {
inline std::uint32_t Normalize(bool femaleModel,std::uint32_t caps){
 const auto action=caps&0x003c0000u,state=caps&0x00038000u,stage=caps&0xc0000000u,modifier=caps&0x3fc00000u;
 if(femaleModel&&action==0x00140000u&&stage==0x80000000u&&
    (state==0x00020000u||state==0x00030000u)&&
    (modifier==0x00400000u||modifier==0x00800000u))return caps&~0x3fc00000u;
 return caps;
}
}
