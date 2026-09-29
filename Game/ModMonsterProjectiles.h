#pragma once
#include <cstring>
namespace ModMonsterProjectiles {
struct Kind { bool ranged=false, acid=false; };
inline bool Prefix(const char* name,const char* prefix,size_t length) {
    return name && strlen(name)>=length && !_strnicmp(name,prefix,length);
}
inline Kind Classify(const char* name) {
    if(!name)return {};
    const bool toad=Prefix(name,"toad",4), dragon=Prefix(name,"drag",4);
    const bool green=Prefix(name,"dragongreen",11);
    const bool dragonkin=Prefix(name,"dragonkin",9);
    const bool succ=Prefix(name,"succ",4), bat=Prefix(name,"bat",3);
    const bool eye=Prefix(name,"Evileye",7), wisp=Prefix(name,"Willowisp",9);
    const bool octo=Prefix(name,"octo",4), acidEye=Prefix(name,"EvileyeG",8);
    return {toad||(dragon&&!dragonkin)||green||octo||succ||bat||eye||wisp,toad||acidEye};
}
}
