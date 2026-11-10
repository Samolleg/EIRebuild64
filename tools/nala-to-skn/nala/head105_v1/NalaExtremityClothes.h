#pragma once
#include "../UnityRenderBridge.h"
#include "NalaExtremityData.h"
#if defined(NALA_EXTREMITY_MATERIAL_LINE)
#include "NalaExtremityMaterials.h"
#endif
namespace NalaExtremityClothes {
using namespace UnityBody;
struct Data {const NalaExtremityData::Anchor*anchors;const std::uint16_t*indices;const float*uv;const unsigned*groups;const unsigned(*rims)[2];unsigned count,indexCount,rimCount,slot,subtype,resource;};
inline bool Get(unsigned kind,Data&d){
 if(kind==0){using namespace NalaExtremityData::cuffs;d={Anchors,Indices,NalaExtremityData::cuffs::UV,NormalGroups,Rims,unsigned(std::size(Anchors)),unsigned(std::size(Indices)),unsigned(std::size(Rims)),6,0x4001c,222};return true;}
 if(kind==1){using namespace NalaExtremityData::socks;d={Anchors,Indices,NalaExtremityData::socks::UV,NormalGroups,Rims,unsigned(std::size(Anchors)),unsigned(std::size(Indices)),unsigned(std::size(Rims)),5,0x3001c,223};return true;}return false;
}
inline unsigned Match(unsigned slot,unsigned subtype){
#if defined(NALA_EXTREMITY_MATERIAL_LINE)
 return NalaExtremityMaterials::Match(slot,subtype);
#else
 for(unsigned i=0;i<2;++i){Data d{};if(Get(i,d)&&d.slot==slot&&d.subtype==subtype)return d.resource;}return 0;
#endif
}
inline bool Compute(unsigned kind,const std::vector<V>&body,const std::vector<V>&bn,std::vector<V>&out,std::vector<V>&normal){
 Data d{};if(!Get(kind,d)||body.size()!=20579||bn.size()!=body.size())return false;out.resize(d.count);normal.assign(d.count,{});
 for(unsigned i=0;i<d.count;++i){const auto&a=d.anchors[i];V c{},n{};for(unsigned k=0;k<3;++k){if(a.index[k]>=body.size())return false;c=Add(c,Scale(body[a.index[k]],a.weight[k]));n=Add(n,Scale(bn[a.index[k]],a.weight[k]));}n=Unit(n);V e=Sub(body[a.index[1]],body[a.index[0]]),t=Unit(Sub(e,Scale(n,Dot(e,n)))),b=Cross(n,t);out[i]=Add(c,Add(Scale(t,a.offset[0]),Add(Scale(b,a.offset[1]),Scale(n,a.offset[2]))));if(!std::isfinite(Dot(out[i],out[i])))return false;}
 for(unsigned i=0;i<d.indexCount;i+=3){auto a=d.indices[i],b=d.indices[i+1],c=d.indices[i+2];V n=Cross(Sub(out[b],out[a]),Sub(out[c],out[a]));normal[a]=Add(normal[a],n);normal[b]=Add(normal[b],n);normal[c]=Add(normal[c],n);}
 std::vector<V>sum(d.count);for(unsigned i=0;i<d.count;++i){if(d.groups[i]>=d.count)return false;sum[d.groups[i]]=Add(sum[d.groups[i]],normal[i]);}for(unsigned i=0;i<d.count;++i)normal[i]=Unit(sum[d.groups[i]]);return true;
}
inline bool Packet(unsigned kind,const std::vector<V>&positions,const std::vector<V>&normals,const M&root,const M&trigger,RenderPacket&packet){
 Data d{};if(!Get(kind,d)||positions.size()!=d.count||normals.size()!=d.count||d.count*2>65535)return false;
 std::vector<V>v=positions,n=normals;v.resize(d.count*2);n.resize(d.count*2);std::vector<float>uv(d.count*4);std::vector<std::uint16_t>indices;indices.reserve(d.indexCount*2+d.rimCount*6);
 for(unsigned i=0;i<d.count;++i){v[d.count+i]=Sub(v[i],Scale(n[i],.00045));n[d.count+i]=Scale(n[i],-1);for(unsigned k=0;k<2;++k)uv[i*2+k]=uv[(d.count+i)*2+k]=d.uv[i*2+k];}
 for(unsigned i=0;i<d.indexCount;i+=3){auto a=d.indices[i],b=d.indices[i+1],c=d.indices[i+2];indices.insert(indices.end(),{a,b,c,std::uint16_t(d.count+a),std::uint16_t(d.count+c),std::uint16_t(d.count+b)});}
 for(unsigned i=0;i<d.rimCount;++i){auto a=d.rims[i][0],b=d.rims[i][1];indices.insert(indices.end(),{std::uint16_t(a),std::uint16_t(b),std::uint16_t(d.count+b),std::uint16_t(a),std::uint16_t(d.count+b),std::uint16_t(d.count+a)});}
 unsigned faces=unsigned(indices.size()/3);return InitializePacket(unsigned(v.size()),uv.data(),indices.data(),unsigned(indices.size()),&faces,1,packet)&&PreparePacket(v,n,root,trigger,packet);
}
}
