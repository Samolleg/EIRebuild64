#pragma once
#include "ModSecondaryAtlas.h"

namespace ModPrimaryAtlas {
inline bool Canonical(const char* name) {
    return name && std::string(name).compare(0,6,"jmvatp")==0;
}
inline bool Private(const char* name) {
    return name && std::string(name).compare(0,6,"jmvatm")==0;
}
inline std::string PrivateName(std::uint32_t unit,const std::string& canonical) {
    char prefix[32];std::snprintf(prefix,sizeof(prefix),"jmvatm%08X",unsigned(unit));
    return prefix+canonical.substr(6);
}
struct Aliases {
    static constexpr std::size_t Limit=64;
    std::unordered_map<std::string,std::string> entries;
    bool Add(const std::string& name,const std::string& canonical) {
        auto found=entries.find(name);
        if(found!=entries.end())return found->second==canonical;
        if(entries.size()>=Limit)return false;
        entries.emplace(name,canonical);return true;
    }
    void Remove(const char* name){if(Private(name))entries.erase(name);}
    bool Source(const char* name,std::string& source) const {
        if(!name)return false;
        if(!Private(name)){source=name;return true;}
        auto found=entries.find(name);if(found==entries.end())return false;
        source=found->second;return true;
    }
    void Clear(){entries.clear();}
};
inline Aliases& State() {
    // Same bounded lifetime/reset policy as the recipe registry; no texture refs.
    static auto* aliases=new Aliases;return *aliases;
}
template<class Ready,class Bind>
bool EnsurePrivate(Aliases& aliases,std::uint32_t unit,const char* currentName,
                   Ready ready,Bind bind) {
    if(!ready())return false;
    if(Private(currentName)||!Canonical(currentName))return true;
    const std::string canonical(currentName),name=PrivateName(unit,canonical);
    const bool existed=aliases.entries.find(name)!=aliases.entries.end();
    if(!aliases.Add(name,canonical))return false;
    try {
        if(bind(name.c_str()))return true;
    }catch(...) {
        if(!existed)aliases.Remove(name.c_str());
        throw;
    }
    // Roll back only the newly introduced alias, preserving any prior state.
    if(!existed)aliases.Remove(name.c_str());
    return false;
}
}
