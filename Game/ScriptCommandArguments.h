#pragma once
#include "ScriptCompiler.h"
#include <stdexcept>
namespace ModMob {
struct ArgumentError:std::runtime_error{using std::runtime_error::runtime_error;};
inline std::string Expand(const char*format,const SSCCmd&cmd){
 int counts[]={cmd.params.GetSize(),cmd.szParams.GetSize(),cmd.oParams.GetSize(),cmd.gParams.GetSize()};
 std::string result;char previous=0;
 auto total=[&](){return counts[0]+counts[1]+counts[2]+counts[3];};
 auto take=[&](char type){const char*types="fsog";auto*p=strchr(types,type);if(!p||counts[p-types]<=0)throw ArgumentError("MOB argument type/count mismatch");--counts[p-types];result+=type;previous=type;};
 for(const char*p=format;*p;++p){
  if(*p=='?'){if(!total())break;continue;}
  if(*p=='+'){if(!previous||p[1])throw std::runtime_error("Invalid variadic MOB format");while(total())take(previous);}
  else take(*p);
 }
 if(total())throw ArgumentError("Excess MOB arguments");return result;
}
}
