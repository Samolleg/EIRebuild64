#pragma once
#include "../UnityRenderBridge.h"
#include "NalaStarterPantsData.h"
#include "NalaStarterShirtData.h"
#include "NalaPantsMaterials.h"
#if defined(NALA_WAIST_WRAPS_PREVIEW)
#include "NalaWaistLayers.h"
#endif
namespace NalaStarterPants {
using namespace UnityBody;
inline bool Matches(unsigned subtype){
#if defined(NALA_PANTS_MATERIAL_LINE)
 return NalaPantsMaterials::ForSubtype(subtype)!=nullptr;
#else
 return subtype==0x0002001cu;
#endif
}
inline bool Compute(const std::vector<V>&body,const std::vector<V>&bodyNormals,std::vector<V>&out,std::vector<V>&normal){
 if(body.size()!=bodyNormals.size())return false;
 out.resize(std::size(NalaStarterPantsData::Anchors));normal.assign(out.size(),{});
 for(unsigned i=0;i<out.size();++i){const auto&a=NalaStarterPantsData::Anchors[i];V center{},n{};
  for(unsigned k=0;k<3;++k){if(a.index[k]>=body.size())return false;center=Add(center,Scale(body[a.index[k]],a.weight[k]));n=Add(n,Scale(bodyNormals[a.index[k]],a.weight[k]));}
  n=Unit(n);V edge=Sub(body[a.index[1]],body[a.index[0]]),t=Unit(Sub(edge,Scale(n,Dot(edge,n)))),b=Cross(n,t);
  out[i]=Add(center,Add(Scale(t,a.offset[0]),Add(Scale(b,a.offset[1]),Scale(n,a.offset[2]))));
  if(!std::isfinite(Dot(out[i],out[i])))return false;
 }
 if constexpr(NalaStarterPantsData::HasRefinement){
  auto support=std::move(out);std::vector<V>sn(support.size());
  for(unsigned i=0;i<std::size(NalaStarterPantsData::SupportTriangles);i+=3){auto a=NalaStarterPantsData::SupportTriangles[i],b=NalaStarterPantsData::SupportTriangles[i+1],c=NalaStarterPantsData::SupportTriangles[i+2];if(a>=support.size()||b>=support.size()||c>=support.size())return false;auto n=Cross(Sub(support[b],support[a]),Sub(support[c],support[a]));sn[a]=Add(sn[a],n);sn[b]=Add(sn[b],n);sn[c]=Add(sn[c],n);}
  if constexpr(NalaStarterPantsData::HasWeld){std::vector<V>sum(sn.size());for(unsigned i=0;i<sn.size();++i){unsigned g=NalaStarterPantsData::SupportWeldGroups[i];if(g>=sn.size())return false;sum[g]=Add(sum[g],sn[i]);}for(unsigned i=0;i<sn.size();++i)sn[i]=sum[NalaStarterPantsData::SupportWeldGroups[i]];}
  for(auto&n:sn)n=Unit(n);out.resize(std::size(NalaStarterPantsData::Cloth));
  for(unsigned i=0;i<out.size();++i){const auto&p=NalaStarterPantsData::Cloth[i];V center{},n{};for(unsigned k=0;k<3;++k){if(p.index[k]>=support.size())return false;center=Add(center,Scale(support[p.index[k]],p.weight[k]));n=Add(n,Scale(sn[p.index[k]],p.weight[k]));}out[i]=Add(center,Scale(Unit(n),p.height));}
 }
 if constexpr(NalaStarterPantsData::HasWeld){std::vector<V>sum(out.size());std::vector<unsigned>count(out.size());
  for(unsigned i=0;i<out.size();++i){unsigned g=NalaStarterPantsData::WeldGroups[i];if(g>=out.size())return false;sum[g]=Add(sum[g],out[i]);++count[g];}
  for(unsigned i=0;i<out.size();++i){unsigned g=NalaStarterPantsData::WeldGroups[i];out[i]=Scale(sum[g],1./count[g]);}
 }
 normal.assign(out.size(),{});
 for(unsigned i=0;i<std::size(NalaStarterPantsData::Indices);i+=3){auto a=NalaStarterPantsData::Indices[i],b=NalaStarterPantsData::Indices[i+1],c=NalaStarterPantsData::Indices[i+2];if(a>=out.size()||b>=out.size()||c>=out.size())return false;
  V n=Cross(Sub(out[b],out[a]),Sub(out[c],out[a]));normal[a]=Add(normal[a],n);normal[b]=Add(normal[b],n);normal[c]=Add(normal[c],n);}
 if constexpr(NalaStarterPantsData::HasWeld){std::vector<V>sum(normal.size());for(unsigned i=0;i<normal.size();++i){unsigned g=NalaStarterPantsData::WeldGroups[i];sum[g]=Add(sum[g],normal[i]);}for(unsigned i=0;i<normal.size();++i)normal[i]=sum[NalaStarterPantsData::WeldGroups[i]];}
 for(auto&n:normal)n=Unit(n);return true;
}
inline bool Packet(const std::vector<V>&positions,const std::vector<V>&normals,const M&root,const M&trigger,RenderPacket&packet
#if defined(NALA_WAIST_WRAPS_PREVIEW)
 ,const std::vector<V>*wrap=nullptr,const std::vector<V>*wrapNormal=nullptr
#endif
 ){
 const unsigned count=unsigned(std::size(NalaStarterPantsData::Cloth));if(positions.size()!=count||normals.size()!=count||count*2>65535)return false;
 std::vector<V>v=positions,n=normals;v.resize(count*2);n.resize(count*2);std::vector<float>uv(count*4);std::vector<std::uint16_t>indices;
#if defined(NALA_WAIST_WRAPS_PREVIEW)
 const float*pantsUV=NalaWaistLayersData::PantsUV;
#else
 const float*pantsUV=NalaStarterPantsData::UV;
#endif
 for(unsigned i=0;i<count;++i){v[count+i]=Sub(v[i],Scale(n[i],.0006));n[count+i]=Scale(n[i],-1);
  for(unsigned k=0;k<2;++k)uv[i*2+k]=uv[(count+i)*2+k]=pantsUV[i*2+k];}
 for(unsigned i=0;i<std::size(NalaStarterPantsData::Indices);i+=3){auto a=NalaStarterPantsData::Indices[i],b=NalaStarterPantsData::Indices[i+1],c=NalaStarterPantsData::Indices[i+2];indices.insert(indices.end(),{a,b,c,std::uint16_t(count+a),std::uint16_t(count+c),std::uint16_t(count+b)});}
 for(const auto&e:NalaStarterPantsData::Rims){auto a=e[0],b=e[1];indices.insert(indices.end(),{std::uint16_t(a),std::uint16_t(b),std::uint16_t(count+b),std::uint16_t(a),std::uint16_t(count+b),std::uint16_t(count+a)});}
#if defined(NALA_WAIST_WRAPS_PREVIEW)
 if((wrap==nullptr)!=(wrapNormal==nullptr))return false;
 if(wrap&&!NalaWaistLayers::Append(*wrap,*wrapNormal,v,n,uv,indices))return false;
#endif
 unsigned faces=unsigned(indices.size()/3);return InitializePacket(unsigned(v.size()),uv.data(),indices.data(),unsigned(indices.size()),&faces,1,packet)&&PreparePacket(v,n,root,trigger,packet);
}
inline void HidePanties(RenderPacket&packet){
 std::vector<std::uint16_t>kept;kept.reserve(packet.indices.size());const unsigned count=unsigned(std::size(NalaStarterShirtData::UnderwearTop));
 for(unsigned i=0;i+2<packet.indices.size();i+=3){bool bottom=false;for(unsigned k=0;k<3;++k)bottom|=NalaStarterShirtData::UnderwearTop[packet.indices[i+k]%count]==0;if(!bottom)kept.insert(kept.end(),packet.indices.begin()+i,packet.indices.begin()+i+3);}
 packet.indices=std::move(kept);packet.materialEnds.assign(1,unsigned(packet.indices.size()));
}
}
