#pragma once
#include <cmath>
#include <cstdint>
namespace ModMobFlagArguments {
inline bool Mask(float value,std::uint32_t& mask){
 mask=0;
 if(!std::isfinite(value)||std::floor(value)!=value||double(value)<-2147483648.0||double(value)>=4294967296.0)return false;
 mask=value<0?static_cast<std::uint32_t>(static_cast<std::int32_t>(value)):static_cast<std::uint32_t>(value);
 return true;
}
inline bool Bit(float value,std::uint32_t& mask){
 mask=0;
 if(!std::isfinite(value)||std::floor(value)!=value||value<0||value>=32)return false;
 mask=std::uint32_t(1)<<static_cast<unsigned>(value);
 return true;
}
}
