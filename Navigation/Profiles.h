#pragma once
#include "Types.h"
#include <filesystem>
#include <cctype>

namespace Navigation {
enum AgentType { Tiny, Small, Human, Large, Giant, LevitateTiny, LevitateSmall, Levitate, AgentCount };
struct Agent { const char* name; Profile profile; };
inline const Agent Agents[]={
    {"tiny",           {0.f,.2f,40.f,.35f,.1f,.05f,32,1}},
    {"small",          {.1f,.2f,40.f,.35f,.1f,.05f,32,2}},
    {"human",          {.2f,.4f,40.f,.35f,.1f}},
    {"large",          {.5f,.4f,40.f,.35f,.1f,.05f,32,6}},
    {"giant",          {1.f,.4f,40.f,.35f,.1f,.05f,32,7}},
    {"levitate_tiny",  {0.f,.2f,60.f,.35f,.1f,.05f,32,0}},
    {"levitate_small", {.2f,.2f,60.f,.35f,.1f,.05f,32,0}},
    {"levitate",       {.5f,.2f,60.f,.35f,.1f,.05f,32,0}}
};
inline int ProfileForRadius(float radius,bool levitate) {
    const int first=levitate?LevitateTiny:Tiny,last=levitate?Levitate:Giant;
    int type=first;for(int i=first+1;i<=last;++i)if(Agents[i].profile.radius<=radius)type=i;
    return type;
}
inline std::string ResourceName(const char* name) {
    auto result=std::filesystem::path(name?name:"").filename().generic_string();
    for(auto& c:result)if(static_cast<unsigned char>(c)<128)c=char(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
inline std::string MeshFilename(const std::string& mpr,const std::string& mob,int type) {
    return std::filesystem::path(mpr).stem().string()+"--"+
        std::filesystem::path(mob).stem().string()+"."+Agents[type].name+".nav64";
}
}
