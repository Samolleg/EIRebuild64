#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace ModSecondaryAtlas {
inline void Append32(std::string& out, std::uint32_t value) {
    for (int i=0;i<4;++i) out.push_back(char((value>>(i*8))&255));
}
inline std::string Recipe(std::uint32_t size, std::uint32_t mips,
                          std::uint32_t background, std::uint32_t format,
                          const std::string& data, const std::vector<std::string>& layers,
                          const std::array<int,4>& slots) {
    std::string out;
    Append32(out,1);Append32(out,size);Append32(out,mips);
    Append32(out,background);Append32(out,format);Append32(out,std::uint32_t(data.size()));
    out+=data;Append32(out,std::uint32_t(layers.size()));
    for(const auto& layer:layers){Append32(out,std::uint32_t(layer.size()));out+=layer;}
    for(int slot:slots)Append32(out,std::uint32_t(slot));
    return out;
}
inline std::uint64_t Hash(const std::string& recipe,std::uint64_t seed) {
    for(unsigned char byte:recipe){seed^=byte;seed*=1099511628211ULL;}return seed;
}
inline std::string Name(const std::string& recipe,bool primary=false) {
    const auto a=Hash(recipe,1469598103934665603ULL),b=Hash(recipe,1099511628211ULL);
    char name[64];std::snprintf(name,sizeof(name),"%s%08X%08X%08X%08X",primary?"jmvatp":"jmvas",
        unsigned(a>>32),unsigned(a),unsigned(b>>32),unsigned(b));return name;
}
inline bool Canonical(const char* name) {
    return name && std::string(name).compare(0,5,"jmvas")==0;
}
// Decode only an already registered recipe; no new names or persistent pixels.
struct DecodedRecipe {
    std::uint32_t size, mips, background, format;
    std::string data;
    std::vector<std::string> layers;
    std::array<int,4> slots;
};
inline bool Decode(const std::string& bytes, DecodedRecipe& result) {
    std::size_t at=0;
    auto word=[&](std::uint32_t& value) {
        if(bytes.size()-at<4)return false;
        value=0;
        for(unsigned i=0;i<4;++i)value|=std::uint32_t(static_cast<unsigned char>(bytes[at+i]))<<(i*8);
        at+=4;return true;
    };
    auto text=[&](std::string& value) {
        std::uint32_t count;
        if(!word(count) || count>bytes.size()-at)return false;
        value.assign(bytes.data()+at,count);at+=count;return true;
    };
    DecodedRecipe decoded;std::uint32_t version,count;
    if(!word(version) || version!=1 || !word(decoded.size) || !word(decoded.mips) ||
       !word(decoded.background) || !word(decoded.format) || !text(decoded.data) || !word(count))return false;
    if(bytes.size()-at<16 || count>(bytes.size()-at-16)/4)return false;
    for(std::uint32_t i=0;i<count;++i) {
        std::string layer;
        if(!text(layer) || layer.find(char(0))!=std::string::npos)return false;
        decoded.layers.push_back(std::move(layer));
    }
    for(auto& slot:decoded.slots) {
        std::uint32_t value;
        if(!word(value))return false;
        if(value==0xFFFFFFFFu)slot=-1;
        else if(value<=0x7FFFFFFFu)slot=int(value);
        else return false;
    }
    if(at!=bytes.size())return false;
    result=std::move(decoded);return true;
}
struct Registry {
    static constexpr std::size_t Limit=1024;
    std::unordered_map<std::string,std::string> recipes;
    bool Resolve(const std::string& recipe,std::string& result,bool primary=false) {
        const auto name=Name(recipe,primary);auto found=recipes.find(name);
        if(found==recipes.end()){
            if(recipes.size()>=Limit)return false;
            recipes.emplace(name,recipe);
        }else if(found->second!=recipe)return false;
        result=name;return true;
    }
    const std::string* Find(const std::string& name) const {
        const auto found=recipes.find(name);return found==recipes.end()?nullptr:&found->second;
    }
    void Clear(){recipes.clear();}
};

template<class Provider,class Find,class Ready,class Acquire,class Release>
bool ReuseLoaded(Find find,Ready ready,Acquire acquire,Release release) {
    Provider* provider=find();if(!provider)return false;
    acquire(provider);std::unique_ptr<Provider,Release> retained(provider,release);
    return ready(provider);
}

// The same transaction is used by production callers and the small component.
// Candidate loading precedes any owner mutation; held refs cover ChangeTexture's
// release-before-set update. A failed commit must visibly restore the old owner.
template<class Provider,class Find,class Ready,class Valid,class Commit,class Current,
         class Acquire,class Release>
bool Rebind(Provider* old,Find find,Ready ready,Valid valid,Commit commit,Current current,
            Acquire acquire,Release release) {
    if(!old)return false;
    acquire(old);std::unique_ptr<Provider,Release> keepOld(old,release);
    Provider* candidate=find();if(!candidate)return false;
    acquire(candidate);std::unique_ptr<Provider,Release> keepCandidate(candidate,release);
    if(!ready(candidate)||!valid(candidate))return false;
    if(candidate!=old)commit(candidate);
    if(current()!=candidate){
        commit(old);
        if(current()!=old)throw std::runtime_error("Secondary atlas owner rollback failed");
        return false;
    }
    return true;
}
}
