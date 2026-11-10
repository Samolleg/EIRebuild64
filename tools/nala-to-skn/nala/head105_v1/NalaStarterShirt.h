#pragma once
#include "../UnityRenderBridge.h"
#include "NalaStarterShirtData.h"
#include "NalaShirtFinish.h"
namespace NalaStarterShirt {
using namespace UnityBody;
inline bool Compute(const std::vector<V>&body,const std::vector<V>&bodyNormals,std::vector<V>&out,std::vector<V>&normal){
 if(body.size()!=bodyNormals.size())return false;const unsigned count=unsigned(std::size(NalaStarterShirtData::Anchors));
 std::vector<V>support(count);
 for(unsigned i=0;i<count;++i){const auto&a=NalaStarterShirtData::Anchors[i];V center{},n{};
  for(unsigned k=0;k<3;++k){if(a.index[k]>=body.size())return false;center=Add(center,Scale(body[a.index[k]],a.weight[k]));n=Add(n,Scale(bodyNormals[a.index[k]],a.weight[k]));}
  n=Unit(n);V edge=Sub(body[a.index[1]],body[a.index[0]]),t=Unit(Sub(edge,Scale(n,Dot(edge,n)))),b=Cross(n,t);
  support[i]=Add(center,Add(Scale(t,a.offset[0]),Add(Scale(b,a.offset[1]),Scale(n,a.offset[2]))));
  if(!std::isfinite(Dot(support[i],support[i])))return false;
 }
 if constexpr(NalaStarterShirtData::HasFolds){
  std::vector<V>sn(count);
  for(unsigned i=0;i<std::size(NalaStarterShirtData::SupportTriangles);i+=3){auto a=NalaStarterShirtData::SupportTriangles[i],b=NalaStarterShirtData::SupportTriangles[i+1],c=NalaStarterShirtData::SupportTriangles[i+2];if(a>=count||b>=count||c>=count)return false;auto n=Cross(Sub(support[b],support[a]),Sub(support[c],support[a]));sn[a]=Add(sn[a],n);sn[b]=Add(sn[b],n);sn[c]=Add(sn[c],n);}
  for(auto&n:sn)n=Unit(n);
  out.resize(std::size(NalaStarterShirtData::Cloth));
  for(unsigned i=0;i<out.size();++i){const auto&p=NalaStarterShirtData::Cloth[i];V center{},n{};for(unsigned k=0;k<3;++k){if(p.index[k]>=count)return false;center=Add(center,Scale(support[p.index[k]],p.weight[k]));n=Add(n,Scale(sn[p.index[k]],p.weight[k]));}out[i]=Add(center,Scale(Unit(n),p.height));}
 }else out=std::move(support);
 normal.assign(out.size(),{});
 for(unsigned i=0;i<std::size(NalaStarterShirtData::Indices);i+=3){unsigned a=NalaStarterShirtData::Source[NalaStarterShirtData::Indices[i]],b=NalaStarterShirtData::Source[NalaStarterShirtData::Indices[i+1]],c=NalaStarterShirtData::Source[NalaStarterShirtData::Indices[i+2]];V n=Cross(Sub(out[b],out[a]),Sub(out[c],out[a]));normal[a]=Add(normal[a],n);normal[b]=Add(normal[b],n);normal[c]=Add(normal[c],n);}
 for(auto&n:normal)n=Unit(n);return true;
}
inline bool Packet(const std::vector<V>&positions,const std::vector<V>&normals,const M&root,const M&trigger,RenderPacket&packet,unsigned materialOrdinal=28){
 const unsigned count=unsigned(std::size(NalaStarterShirtData::Source));if(count*2>65535)return false;
 std::vector<V>v(count*2),n(count*2);std::vector<float>uv(count*4);std::vector<std::uint16_t>indices;
 for(unsigned i=0;i<count;++i){auto j=NalaStarterShirtData::Source[i];if(j>=positions.size()||j>=normals.size())return false;
  v[i]=positions[j];n[i]=normals[j];v[count+i]=Sub(v[i],Scale(n[i],.0006));n[count+i]=Scale(n[i],-1);
  for(unsigned k=0;k<2;++k)uv[i*2+k]=uv[(count+i)*2+k]=NalaStarterShirtData::UV[i*2+k];}
 for(unsigned i=0;i<std::size(NalaStarterShirtData::Indices);i+=3){auto a=NalaStarterShirtData::Indices[i],b=NalaStarterShirtData::Indices[i+1],c=NalaStarterShirtData::Indices[i+2];indices.insert(indices.end(),{a,b,c,std::uint16_t(count+a),std::uint16_t(count+c),std::uint16_t(count+b)});}
 for(const auto&e:NalaStarterShirtData::Rims){auto a=e[0],b=e[1];indices.insert(indices.end(),{std::uint16_t(a),std::uint16_t(b),std::uint16_t(count+b),std::uint16_t(a),std::uint16_t(count+b),std::uint16_t(count+a)});}
 if(!NalaShirtFinish::Append(positions,normals,materialOrdinal,v,n,uv,indices))return false;
 unsigned faces=unsigned(indices.size()/3);return InitializePacket(unsigned(v.size()),uv.data(),indices.data(),unsigned(indices.size()),&faces,1,packet)&&PreparePacket(v,n,root,trigger,packet);
}
inline void HideBandeau(RenderPacket&packet){
 std::vector<std::uint16_t>kept;kept.reserve(packet.indices.size());const unsigned count=unsigned(std::size(NalaStarterShirtData::UnderwearTop));
 for(unsigned i=0;i+2<packet.indices.size();i+=3){bool top=false;for(unsigned k=0;k<3;++k)top|=NalaStarterShirtData::UnderwearTop[packet.indices[i+k]%count]!=0;if(!top)kept.insert(kept.end(),packet.indices.begin()+i,packet.indices.begin()+i+3);}
 packet.indices=std::move(kept);packet.materialEnds.assign(1,unsigned(packet.indices.size()));
}
}
