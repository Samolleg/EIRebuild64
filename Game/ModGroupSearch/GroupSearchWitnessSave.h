#pragma once
#include "GroupSearchBodySave.h"

// Separate optional namespace: R8/R9 can still read base/body words, and the
// saved return ticket takes the witness home if that older DLL ignores this.
namespace GroupSearch { namespace WitnessSave {
inline std::string Prefix(const std::string &zone,int side)
{return "jmv.witness.v1."+SaveFormat::Prefix(zone,side);}
template<class State> inline SaveFormat::Words Encode(const State &s,const SaveFormat::Words &base)
{
    const auto &v=s.witness;if(v.observer.id<0)return {};
    SaveFormat::Writer w;w.U(0x31575447);w.U(1);w.U(uint32_t(base.size()));
    for(auto word:base)w.U(word);
    w.Id(v.observer);w.Id(v.body);w.P(v.target);w.U(v.quietTicks);w.U(v.travelTicks);w.U(v.looking?1:0);
    w.U(SaveFormat::Checksum(w.words,w.words.size()));return w.words;
}
inline BodySave::Result Decode(const SaveFormat::Words &words,const SaveFormat::Words &base,ExpeditionSnapshot &s)
{
    using Result=BodySave::Result;
    if(words.size()<14||words.size()>SaveFormat::MaxWords||SaveFormat::Checksum(words,words.size()-1)!=words.back())return Result::Invalid;
    SaveFormat::Reader r{words};if(r.U()!=0x31575447||r.U()!=1)return Result::Invalid;
    const auto n=r.Count(1);bool same=n==base.size();
    for(size_t i=0;i<n;++i){const auto word=r.U();if(i>=base.size()||base[i]!=word)same=false;}
    if(!r.ok)return Result::Invalid;
    if(!same)return Result::Stale;
    auto restored=s;auto &v=restored.witness;
    v.observer=r.Id();v.body=r.Id();v.target=r.P();v.quietTicks=r.I();v.travelTicks=r.I();v.looking=r.B();
    Expedition validator(s.awareness.zone,s.awareness.side);
    if(!r.ok||r.cursor!=words.size()-1||!validator.Restore(restored))return Result::Invalid;
    s=restored;return Result::Applied;
}
} }
