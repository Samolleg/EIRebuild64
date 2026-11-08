#pragma once
#include "Types.h"
#include <filesystem>
#include <cctype>

namespace Navigation {
enum AgentType { Human, Large, Giant, Levitate, Flying, AgentCount };
struct Agent { const char* name; Profile profile; };
inline const Agent Agents[]={
    {"human",          {.2f,.4f,40.f,.35f,.1f}},
    {"large",          {.5f,.4f,40.f,.35f,.2f,.05f,96,6}},
    {"giant",          {1.f,.4f,40.f,.35f,.3f,.05f,96,7}},
    {"levitate",       {.5f,.2f,60.f,.35f,.2f,.05f,96,0,Movement::Levitate}},
    {"flying",         {1.f,.2f,90.f,.35f,.3f,.05f,96,0,Movement::Flying}}
};
// Height selection is shared by unit movement and read-only profile probes.
inline float LegacyClassHeight(unsigned aiClass) {
    constexpr float heights[]={2,.4f,1,1.6f,2.2f,1.7f,3,6};
    return heights[aiClass<8?aiClass:7];
}
inline float SelectedClearance(const Profile& profile,float height) {
    const auto& tiers=ClearanceHeights(profile);float selected=tiers[0];
    for(float tier:tiers)if(tier<=height)selected=tier;
    return selected;
}
inline int ProfileForRadius(float radius,bool levitate) {
    if(levitate)return Levitate;
    const int first=Human,last=Giant;
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
