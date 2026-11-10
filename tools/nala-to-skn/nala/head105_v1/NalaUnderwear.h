#pragma once
#include "../UnityRenderBridge.h"
#include "NalaUnderwearData.h"
namespace NalaUnderwear {
using namespace UnityBody;
inline bool Compute(const std::vector<V>&body,std::vector<V>&out,std::vector<V>&normal){
 unsigned count=unsigned(std::size(NalaUnderwearData::Anchors));out.resize(count);normal.assign(count,{});
 for(unsigned i=0;i<count;++i){const auto&a=NalaUnderwearData::Anchors[i];for(auto j:a.index)if(j>=body.size())return false;V p=body[a.index[0]],u=Sub(body[a.index[1]],p),v=Sub(body[a.index[2]],p),n=Unit(Cross(u,v)),t=Unit(u),b=Cross(n,t),point{};
  for(unsigned k=0;k<3;++k)point=Add(point,Scale(body[a.index[k]],a.weight[k]));out[i]=Add(point,Add(Scale(t,a.offset[0]),Add(Scale(b,a.offset[1]),Scale(n,a.offset[2]))));normal[i]=n;if(!std::isfinite(Dot(out[i],out[i])))return false;}
 std::vector<V>sum(count);for(unsigned i=0;i<std::size(NalaUnderwearData::Indices);i+=3){unsigned a=NalaUnderwearData::Indices[i],b=NalaUnderwearData::Indices[i+1],c=NalaUnderwearData::Indices[i+2];V n=Cross(Sub(out[b],out[a]),Sub(out[c],out[a]));sum[a]=Add(sum[a],n);sum[b]=Add(sum[b],n);sum[c]=Add(sum[c],n);}
 for(unsigned i=0;i<count;++i)if(Dot(sum[i],sum[i])>1e-22)normal[i]=Unit(sum[i]);return true;
}
inline bool Covered(unsigned i){return i<std::size(NalaUnderwearData::Covered)&&NalaUnderwearData::Covered[i];}
template<class Mapping,class Indices>inline std::vector<std::uint16_t> BodyIndices(const Mapping&map,const Indices&indices){std::vector<std::uint16_t>out;for(unsigned i=0;i<std::size(indices);i+=3){unsigned a=indices[i],b=indices[i+1],c=indices[i+2];if(Covered(map[a])&&Covered(map[b])&&Covered(map[c]))continue;out.push_back(std::uint16_t(a));out.push_back(std::uint16_t(b));out.push_back(std::uint16_t(c));}return out;}
inline bool Packet(const std::vector<V>&positions,const std::vector<V>&normals,const M&root,const M&trigger,RenderPacket&packet){
 unsigned count=unsigned(positions.size());if(count!=std::size(NalaUnderwearData::Anchors)||count*2>65535)return false;
 std::vector<V>v=positions,n=normals;v.resize(count*2);n.resize(count*2);std::vector<float>uv(count*4);std::vector<std::uint16_t>indices;indices.reserve(std::size(NalaUnderwearData::Indices)*2);
 for(unsigned part=0;part<3;++part){const auto&p=NalaUnderwearData::Pieces[part];for(unsigned j=0;j<p.count;++j){unsigned i=p.first+j;float u=std::clamp(NalaUnderwearData::UV[i*2],0.f,1.f),w=std::clamp(NalaUnderwearData::UV[i*2+1],0.f,1.f);uv[i*2]=part==2?.25f:(u*.5f+(part==1?.5f:0));uv[i*2+1]=part==2?.75f:w*.5f;uv[(count+i)*2]=uv[i*2];uv[(count+i)*2+1]=uv[i*2+1];v[count+i]=Sub(v[i],Scale(n[i],.0007));n[count+i]=Scale(n[i],-1);}}
 for(unsigned i=0;i<std::size(NalaUnderwearData::Indices);i+=3){auto a=NalaUnderwearData::Indices[i],b=NalaUnderwearData::Indices[i+1],c=NalaUnderwearData::Indices[i+2];indices.insert(indices.end(),{a,b,c,std::uint16_t(count+a),std::uint16_t(count+c),std::uint16_t(count+b)});}
 for(const auto&e:NalaUnderwearData::RimEdges){auto a=e[0],b=e[1];indices.insert(indices.end(),{a,b,std::uint16_t(count+b),a,std::uint16_t(count+b),std::uint16_t(count+a)});}
 unsigned faces=unsigned(indices.size()/3);if(!InitializePacket(unsigned(v.size()),uv.data(),indices.data(),unsigned(indices.size()),&faces,1,packet))return false;return PreparePacket(v,n,root,trigger,packet);
}
}
