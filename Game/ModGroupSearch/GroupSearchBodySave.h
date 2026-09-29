#pragma once
#include "GroupSearchSave.h"

// Optional metadata alongside unchanged v1 group words. Old DLLs can still
// read the base state. Binding to its exact word stream prevents stale body
// flags being merged into another save or a base edited by an older DLL.
namespace GroupSearch { namespace BodySave {
inline std::string Prefix(const std::string &zone,int side)
{return "jmv.body.v1."+SaveFormat::Prefix(zone,side);}
template<class State> inline SaveFormat::Words Encode(const State &s,const SaveFormat::Words &base)
{
    bool any=false;for(const auto &m:s.awareness.memories)any|=m.second.bodyReported;
    for(const auto *list:{&s.awareness.pending,&s.awareness.active})for(const auto &c:*list)any|=c.body;
    if(!any)return {};
    SaveFormat::Writer w;w.U(0x31424447);w.U(1);w.U(uint32_t(base.size()));
    for(auto word:base)w.U(word);
    size_t count=0;for(const auto &m:s.awareness.memories)if(m.second.bodyReported)++count;
    w.U(uint32_t(count));for(const auto &m:s.awareness.memories)if(m.second.bodyReported){w.Id(m.first);w.Wide(m.second.episode);}
    for(const auto *list:{&s.awareness.pending,&s.awareness.active})
    {
        count=0;for(const auto &c:*list)if(c.body)++count;
        w.U(uint32_t(count));for(const auto &c:*list)if(c.body){w.Id(c.unit);w.Wide(c.episode);}
    }
    w.U(SaveFormat::Checksum(w.words,w.words.size()));return w.words;
}
enum class Result { Applied, Stale, Invalid };
inline Result Decode(const SaveFormat::Words &words,const SaveFormat::Words &base,ExpeditionSnapshot &s)
{
    if(words.size()<8||words.size()>SaveFormat::MaxWords||SaveFormat::Checksum(words,words.size()-1)!=words.back())return Result::Invalid;
    SaveFormat::Reader r{words};if(r.U()!=0x31424447||r.U()!=1)return Result::Invalid;
    const auto n=r.Count(1);bool same=n==base.size();
    for(size_t i=0;i<n;++i){auto v=r.U();if(i>=base.size()||v!=base[i])same=false;}
    if(!r.ok)return Result::Invalid;
    if(!same)return Result::Stale; // discard only extension, not legacy world
    auto restored=s;std::set<Identity> seen;
    const auto memories=r.Count(4);
    for(size_t i=0;i<memories;++i)
    {
        auto id=r.Id();auto episode=r.Wide();auto m=restored.awareness.memories.find(id);
        if(m==restored.awareness.memories.end()||m->second.episode!=episode||!m->second.absent||!seen.insert(id).second)return Result::Invalid;
        m->second.bodyReported=true;
    }
    for(auto *list:{&restored.awareness.pending,&restored.awareness.active})
    {
        std::set<std::pair<Identity,uint64_t>> unique;const auto count=r.Count(4);
        for(size_t i=0;i<count;++i)
        {
            auto id=r.Id();auto episode=r.Wide();bool found=false;
            for(auto &c:*list)if(c.unit==id&&c.episode==episode){c.body=true;found=true;}
            if(!found||!unique.emplace(id,episode).second)return Result::Invalid;
        }
    }
    if(!r.ok||r.cursor!=words.size()-1)return Result::Invalid;
    s=restored;return Result::Applied;
}
} }
