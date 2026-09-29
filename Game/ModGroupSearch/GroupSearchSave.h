#pragma once
#include "GroupSearchExpedition.h"
#include <cstring>
#include <limits>

#pragma pack(push, 8)
namespace GroupSearch
{
// Fixed-width field codec. Never serialize native structs, pointers, padding,
// or uint64/float values through a GS float (which loses low bits).
namespace SaveFormat
{
using Words=std::vector<uint32_t>;
constexpr size_t MaxWords=4*1024*1024; // defensive decode limit, not memory TTL
inline uint32_t FloatBits(float f) {uint32_t w;std::memcpy(&w,&f,4);return w;}
inline float BitsFloat(uint32_t w) {float f;std::memcpy(&f,&w,4);return f;}
inline uint32_t Checksum(const Words &w,size_t count)
{
    uint32_t h=2166136261u;
    for(size_t i=0;i<count;++i)for(int b=0;b<4;++b){h^=(w[i]>>(8*b))&255;h*=16777619u;}
    return h;
}
struct Writer
{
    Words words;
    void U(uint32_t v){words.push_back(v);}
    void Id(Identity id){U(uint32_t(id.id));U(id.subtype);}
    void P(Point p){U(FloatBits(p.x));U(FloatBits(p.y));U(FloatBits(p.z));}
    void Wide(uint64_t n){U(uint32_t(n));U(uint32_t(n>>32));}
    void Text(const std::string &s){U(uint32_t(s.size()));for(unsigned char c:s)U(c);}
    void Homes(const std::map<Identity,Home> &list)
    {U(uint32_t(list.size()));for(const auto &e:list){Id(e.first);P(e.second.position);U(e.second.alarm);U(e.second.model);}}
    void Cases(const std::vector<Case> &list)
    {U(uint32_t(list.size()));for(const auto &c:list){Id(c.unit);Wide(c.episode);P(c.lastSeen);}}
};
struct Reader
{
    const Words &words;size_t cursor=0;bool ok=true;
    uint32_t U(){if(cursor>=words.size()){ok=false;return 0;}return words[cursor++];}
    int I(){auto n=U();if(n>uint32_t((std::numeric_limits<int>::max)()))ok=false;return int(n);}
    bool B(){auto n=U();if(n>1)ok=false;return n!=0;}
    Identity Id(bool none=false){auto id=U(),sub=U();if(id>0x7FFFFFFFu && !(none&&id==0xFFFFFFFFu))ok=false;return {int(id),sub};}
    Point P(){float x=BitsFloat(U()),y=BitsFloat(U()),z=BitsFloat(U());Point p{x,y,z};if(!Finite(p))ok=false;return p;}
    uint64_t Wide(){uint64_t lo=U(),hi=U();return lo|(hi<<32);}
    size_t Count(size_t stride){auto n=U();if(!ok||n>(words.size()-cursor)/stride){ok=false;return 0;}return n;}
    std::string Text(){std::string s;const auto n=Count(1);if(n>256){ok=false;return s;}for(size_t i=0;i<n;++i){auto c=U();if(c==0||c>255)ok=false;s.push_back(char(c));}return s;}
    std::map<Identity,Home> Homes()
    {
        std::map<Identity,Home> out;const auto n=Count(7);
        for(size_t i=0;i<n;++i){auto id=Id();Home h;h.position=P();h.alarm=I();h.model=I();if(!out.emplace(id,h).second)ok=false;}return out;
    }
    std::vector<Case> Cases()
    {
        std::vector<Case> out;const auto n=Count(7);
        for(size_t i=0;i<n;++i){Case c;c.unit=Id();c.episode=Wide();c.lastSeen=P();out.push_back(c);}return out;
    }
};
template<class State> inline Words Encode(const State &s)
{
    if(!Expedition::Valid(s))return {};
    Writer w;w.U(0x31534747);w.U(1);const auto &a=s.awareness;
    w.Text(a.zone);w.U(a.side);w.U(a.floor);w.U(a.lookCountdown);w.U(a.lookRemaining);
    w.U(uint32_t(a.phase));w.Id(a.leader);w.P(a.destination);
    w.U(uint32_t(a.memories.size()));
    for(const auto &e:a.memories){w.Id(e.first);w.P(e.second.lastSeen);w.U(e.second.unseenTicks);w.U(e.second.absent?1:0);w.Wide(e.second.episode);}
    w.Cases(a.pending);w.Cases(a.active);w.U(uint32_t(a.party.size()));for(auto id:a.party)w.Id(id);
    w.Homes(s.roster.homes);w.Homes(s.roster.deferredReturns);
    w.P(s.gather);w.P(s.point5);w.P(s.point10);w.P(s.trail);
    w.U(s.phaseTicks);w.U(s.inspectTicks);w.U(s.serial);w.U(s.random);
    w.U(s.waiting?1:0);w.U(s.interrupted?1:0);
    w.U(s.startingFloor);w.U(s.retryTicks);
    w.U(Checksum(w.words,w.words.size()));
    if(w.words.size()>MaxWords)return {};
    return w.words;
}
inline bool Decode(const Words &words,const std::string &zone,int side,ExpeditionSnapshot &out)
{
    if(words.size()<32||words.size()>MaxWords||Checksum(words,words.size()-1)!=words.back())return false;
    Reader r{words};if(r.U()!=0x31534747||r.U()!=1)return false;
    ExpeditionSnapshot s;auto &a=s.awareness;
    a.zone=r.Text();a.side=r.I();a.floor=r.I();a.lookCountdown=r.I();a.lookRemaining=r.I();
    a.phase=Phase(r.U());a.leader=r.Id(true);a.destination=r.P();
    const auto memories=r.Count(9);
    for(size_t i=0;i<memories;++i){auto id=r.Id();Memory m;m.lastSeen=r.P();m.unseenTicks=r.I();m.absent=r.B();m.episode=r.Wide();if(!a.memories.emplace(id,m).second)r.ok=false;}
    a.pending=r.Cases();a.active=r.Cases();const auto party=r.Count(2);for(size_t i=0;i<party;++i)a.party.push_back(r.Id());
    s.roster.homes=r.Homes();s.roster.deferredReturns=r.Homes();
    s.gather=r.P();s.point5=r.P();s.point10=r.P();s.trail=r.P();
    s.phaseTicks=r.I();s.inspectTicks=r.I();s.serial=r.U();s.random=r.U();s.waiting=r.B();s.interrupted=r.B();
    s.startingFloor=r.I();s.retryTicks=r.I();
    if(!r.ok||r.cursor!=words.size()-1||a.zone!=zone||a.side!=side)return false;
    Expedition validator(zone,side);if(!validator.Restore(s))return false;
    out=s;return true;
}
inline std::string Hex(uint32_t value)
{
    const char *digits="0123456789abcdef";std::string s(8,'0');
    for(int i=7;i>=0;--i){s[size_t(i)]=digits[value&15];value>>=4;}return s;
}
inline bool Unhex(const std::string &s,size_t at,uint32_t &out)
{
    if(at>s.size()||s.size()-at<8)return false;out=0;
    for(size_t i=at;i<at+8;++i){char c=s[i];int v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;if(v<0)return false;out=(out<<4)|uint32_t(v);}return true;
}
inline std::string Prefix(const std::string &zone,int side)
{
    // Hex zone prevents ambiguous delimiters or another map's prefix match.
    std::string p="jmv.group.v1.";const char *h="0123456789abcdef";
    for(unsigned char c:zone){p+=h[c>>4];p+=h[c&15];}
    return p+"."+Hex(uint32_t(side))+".";
}
inline std::vector<std::string> Keys(const Words &words,const std::string &prefix)
{
    std::vector<std::string> keys;
    for(size_t i=0;i<words.size();i+=16)
    {
        std::string key=prefix+Hex(uint32_t(i/16))+".";
        for(size_t j=i;j<(std::min)(i+16,words.size());++j)key+=Hex(words[j]);
        keys.push_back(key);
    }
    return keys;
}
// Same keys/format as Keys(), but format only changed 16-word chunks.
// Cache is private to the current zone/load; exact words, never hash equality.
template<class Write> inline size_t SyncKeys(const Words &words,const std::string &prefix,
    Words &previous,std::vector<std::string> &keys,Write write)
{
    size_t formatted=0;const size_t count=(words.size()+15)/16;
    const size_t oldCount=keys.size();keys.resize((std::max)(count,oldCount));
    for(size_t i=0;i<count;++i)
    {
        const size_t begin=i*16,end=(std::min)(begin+16,words.size());
        const size_t oldEnd=(std::min)(begin+16,previous.size());
        if(i<oldCount && end==oldEnd && begin<previous.size() &&
            std::equal(words.begin()+begin,words.begin()+end,previous.begin()+begin))continue;
        std::string key=prefix+Hex(uint32_t(i))+".";
        for(size_t j=begin;j<end;++j)key+=Hex(words[j]);
        ++formatted;
        if(key!=keys[i]){write(key,keys[i]);keys[i]=std::move(key);}
    }
    for(size_t i=count;i<oldCount;++i)write(std::string(),keys[i]);
    keys.resize(count);previous=words;return formatted;
}
inline bool ParseKeys(const std::vector<std::string> &keys,const std::string &prefix,Words &out)
{
    if(keys.empty()||keys.size()>(MaxWords+15)/16)return false;
    std::map<uint32_t,Words> parts;
    for(const auto &key:keys)
    {
        uint32_t index=0;
        if(key.compare(0,prefix.size(),prefix)!=0||key.size()<prefix.size()+17||
            !Unhex(key,prefix.size(),index)||key[prefix.size()+8]!='.'||index>=(MaxWords+15)/16)return false;
        size_t at=prefix.size()+9,length=key.size()-at;
        if(length%8||length>128)return false;
        Words part;for(;at<key.size();at+=8){uint32_t w;if(!Unhex(key,at,w))return false;part.push_back(w);}
        if(!parts.emplace(index,part).second)return false;
    }
    Words joined;uint32_t expected=0;
    for(const auto &part:parts)
    {
        if(part.first!=expected++ || (expected<parts.size()&&part.second.size()!=16))return false;
        joined.insert(joined.end(),part.second.begin(),part.second.end());
    }
    out.swap(joined);return true;
}
} // SaveFormat
}
#pragma pack(pop)
