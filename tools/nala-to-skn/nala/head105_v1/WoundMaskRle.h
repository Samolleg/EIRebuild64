#pragma once
#include <cstddef>
#include <vector>
#include <utility>
namespace NalaWoundMaskRle {
inline bool Decode(const unsigned char* src,std::size_t length,std::size_t expected,std::vector<unsigned char>& result){
 if(!src||!expected)return false;std::vector<unsigned char> data;data.reserve(expected);std::size_t pos=0;
 while(pos<length){if(length-pos<3)return false;unsigned mode=src[pos++];unsigned count=unsigned(src[pos])|(unsigned(src[pos+1])<<8);pos+=2;
  if(mode>1||!count||count>expected-data.size()||(mode==1&&count>length-pos))return false;
  if(mode==0)data.insert(data.end(),count,0);else{data.insert(data.end(),src+pos,src+pos+count);pos+=count;}}
 if(data.size()!=expected)return false;result=std::move(data);return true;
}
}
