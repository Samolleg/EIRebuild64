#pragma once
#include "mmpFormat.h"
#include "ModPrimaryAtlas.h"
#include <cstring>
#include <cstdint>
#include <list>

namespace ModTextureMemory {
struct Payload {
    // Complete MMP with native DWORD alignment, including all mip levels.
    std::vector<DWORD> words;
    std::size_t bytes=0;
    SMMPFileHeader* Header(){return words.empty()?nullptr:reinterpret_cast<SMMPFileHeader*>(words.data());}
};
class Cache {
    using Entry=std::pair<std::string,Payload>;
    std::list<Entry> entries;
public:
    static constexpr std::size_t Capacity=30;
    Payload* Get(const std::string& name){
        for(auto i=entries.begin();i!=entries.end();++i)if(i->first==name){
            entries.splice(entries.end(),entries,i);return &i->second;
        }
        return nullptr;
    }
    void Remove(const std::string& name){
        for(auto i=entries.begin();i!=entries.end();++i)if(i->first==name){entries.erase(i);return;}
    }
    void Put(const std::string& name,Payload value){
        if(auto* old=Get(name)){*old=std::move(value);return;}
        entries.emplace_back(name,std::move(value));
        if(entries.size()>Capacity)entries.pop_front();
    }
    std::size_t Size() const{return entries.size();}
    std::uint64_t Bytes() const noexcept {
        std::uint64_t bytes=0;for(const auto& entry:entries)bytes+=entry.second.bytes;return bytes;
    }
    void Clear(){entries.clear();}
};
struct MemoryState {Cache cache;bool forceCanonicalCpu=false;};
inline MemoryState*& StatePointer(){static MemoryState* value=nullptr;return value;}
inline const MemoryState* PeekState() noexcept {return StatePointer();}
inline MemoryState& State(){
    // Bounded process-lifetime storage, reset at existing world/layer cleanup.
    static auto* state=[](){auto* value=new MemoryState;StatePointer()=value;return value;}();return *state;
}
inline bool Exact(Payload* p,DWORD size,DWORD mips,DWORD format,const std::string& user){
    if(!p || p->bytes<sizeof(SMMPFileHeader))return false;
    auto* h=p->Header();
    return h && h->dwSignature==MMP_SIGNATURE && h->dwDimentionX==size && h->dwDimentionY==size &&
        h->dwMipMapLevel==mips && h->dwFormat==format && h->dwUserDataShift==user.size() &&
        p->bytes>=sizeof(*h)+user.size() && (user.empty() || std::memcmp(h->GetUsedData(),user.data(),user.size())==0);
}
inline void Mip8888(DWORD* dst,const DWORD* src,DWORD dimension){
    const DWORD width=dimension*2;
    for(DWORD y=0;y<dimension;++y)for(DWORD x=0;x<dimension;++x){
        const auto i=y*2*width+x*2;DWORD pixel=0;
        for(unsigned shift=0;shift<32;shift+=8){
            const DWORD sum=((src[i]>>shift)&255)+((src[i+1]>>shift)&255)+
                ((src[i+width]>>shift)&255)+((src[i+width+1]>>shift)&255);
            pixel|=(sum>>2)<<shift;
        }
        dst[y*dimension+x]=pixel;
    }
}
inline std::size_t OutputBytes(const SMMPFileHeader& header,std::size_t userBytes){
    if(!header.dwDimentionX || header.dwDimentionX!=header.dwDimentionY || !header.dwMipMapLevel ||
       header.dwUserDataShift!=userBytes)throw std::runtime_error("Invalid memory MMP input");
    const std::size_t texel=header.dwFormat==MMP_FORMAT_8888?sizeof(DWORD):sizeof(WORD);
    std::size_t bytes=sizeof(header)+userBytes;DWORD dimension=header.dwDimentionX;
    for(DWORD level=0;level<header.dwMipMapLevel;++level){
        if(!dimension)throw std::runtime_error("Invalid memory mip count");
        bytes+=std::size_t(dimension)*dimension*texel;dimension>>=1;
    }
    return bytes;
}
template<class Allocate>
bool ReserveOutput(Payload& result,const SMMPFileHeader& header,std::size_t userBytes,
                   int& drawVisibleOnly,Allocate allocate){
    const auto bytes=OutputBytes(header,userBytes);
    // Only failure of the complete output MMP allocation maps to native NULL.
    // Temporary composition/encoding/cache allocations are outside this scope.
    try {
        if(allocate(result.words,(bytes+3)/4)){result.bytes=bytes;return true;}
    }catch(const std::bad_alloc&){}
    drawVisibleOnly=0;
    result.words.clear();result.bytes=0;return false;
}
template<class Convert16,class Mip16>
Payload EncodeInto(Payload result,const SMMPFileHeader& header,const std::string& user,std::vector<DWORD> pixels,
                   Convert16 convert16,Mip16 mip16){
    if(result.bytes!=OutputBytes(header,user.size()) || result.words.size()<(result.bytes+3)/4 ||
       pixels.size()!=std::size_t(header.dwDimentionX)*header.dwDimentionY)
        throw std::runtime_error("Invalid reserved memory MMP input");
    const std::size_t texel=header.dwFormat==MMP_FORMAT_8888?sizeof(DWORD):sizeof(WORD);

    auto* output=reinterpret_cast<BYTE*>(result.words.data());
    std::memcpy(output,&header,sizeof(header));output+=sizeof(header);
    if(!user.empty()){std::memcpy(output,user.data(),user.size());output+=user.size();}
    DWORD dimension=header.dwDimentionX;
    std::vector<WORD> converted(texel==sizeof(WORD)?pixels.size():0);
    std::vector<DWORD> next(pixels.size());
    for(DWORD level=0;level<header.dwMipMapLevel;++level){
        const auto count=std::size_t(dimension)*dimension;
        if(texel==sizeof(DWORD))std::memcpy(output,pixels.data(),count*texel);
        else {convert16(converted.data(),pixels.data(),int(count));std::memcpy(output,converted.data(),count*texel);}
        output+=count*texel;
        if(level+1<header.dwMipMapLevel){
            dimension>>=1;
            if(texel==sizeof(DWORD))Mip8888(next.data(),pixels.data(),dimension);
            else mip16(next.data(),pixels.data(),int(dimension));
            pixels.swap(next);
        }
    }
    return result;
}
template<class Convert16,class Mip16>
Payload Encode(const SMMPFileHeader& header,const std::string& user,std::vector<DWORD> pixels,
               Convert16 convert16,Mip16 mip16){
    // Compatibility entry for existing pure encoder callers; production reserves
    // its complete output before composing pixels, then uses EncodeInto.
    Payload result;result.bytes=OutputBytes(header,user.size());result.words.resize((result.bytes+3)/4);
    return EncodeInto(std::move(result),header,user,std::move(pixels),convert16,mip16);
}
inline Payload* Source(Cache& cache,const ModPrimaryAtlas::Aliases& aliases,const char* name,bool privateAlias){
    if(!name)return nullptr;
    if(auto* direct=cache.Get(name))if(direct->Header())return direct;
    std::string source;
    if(privateAlias && aliases.Source(name,source))return cache.Get(source);
    return nullptr;
}
template<class Resource,class Load,class Replay>
bool LoadSource(Cache& cache,const ModPrimaryAtlas::Aliases& aliases,const char* name,bool privateAlias,
                Resource resource,Load load,Replay replay){
    // RES requested name precedes both direct CPU data and canonical alias.
    if(auto* header=resource())return load(header,true);
    auto* payload=Source(cache,aliases,name,privateAlias);
    if((!payload || !payload->Header()) && !privateAlias &&
       (ModPrimaryAtlas::Canonical(name) || ModSecondaryAtlas::Canonical(name))) {
        if(!replay(name))return false;
        payload=Source(cache,aliases,name,false);
    }
    if(!payload || !payload->Header())return !privateAlias;
    // Native CPU loader reports TRUE after upload; production owner commit also
    // checks the actual surface, independently of this permissive load result.
    return load(payload->Header(),false);
}
template<class Resource,class Load>
bool LoadSource(Cache& cache,const ModPrimaryAtlas::Aliases& aliases,const char* name,bool privateAlias,
                Resource resource,Load load){
    // Existing callers without a replay owner retain their missing-source result.
    return LoadSource(cache,aliases,name,privateAlias,resource,load,[](const char*){return true;});
}
template<class Rebuild>
bool EnsureCpu(MemoryState& state,const std::string& canonical,Rebuild rebuild){
    auto* cached=state.cache.Get(canonical);if(cached && cached->Header())return true;
    const bool previous=state.forceCanonicalCpu;state.forceCanonicalCpu=true;
    struct Restore {bool& flag;bool value;~Restore(){flag=value;}} restore{state.forceCanonicalCpu,previous};
    std::string name;
    try {if(!rebuild(name) || _stricmp(name.c_str(),canonical.c_str())!=0)return false;}
    catch(...){return false;}
    cached=state.cache.Get(canonical);return cached && cached->Header();
}
}
