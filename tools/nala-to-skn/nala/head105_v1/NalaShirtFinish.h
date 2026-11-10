#pragma once
#include "../UnityRenderBridge.h"
#include "NalaShirtMaterials.h"
#include "NalaShirtFinishData.h"
namespace NalaShirtFinish {
using namespace UnityBody;
struct Data {const NalaShirtFinishData::Anchor*anchors=nullptr;unsigned vertices=0;const unsigned*source=nullptr;unsigned renderVertices=0;const float*uv=nullptr;const std::uint16_t*indices=nullptr;unsigned indexCount=0;const NalaShirtFinishData::Frame*frame=nullptr;const double(*local)[3]=nullptr;};
inline bool Get(unsigned ordinal,Data&d){
 d={};switch(ordinal){
 case 28:return true;
#define NALA_FINISH_CASE(ordinal,code) case ordinal:d={NalaShirtFinishData::code##Anchors,unsigned(std::size(NalaShirtFinishData::code##Anchors)),NalaShirtFinishData::code##Source,unsigned(std::size(NalaShirtFinishData::code##Source)),NalaShirtFinishData::code##UV,NalaShirtFinishData::code##Indices,unsigned(std::size(NalaShirtFinishData::code##Indices)),&NalaShirtFinishData::code##Frame,NalaShirtFinishData::code##Local};return true
 NALA_FINISH_CASE(29,cc);NALA_FINISH_CASE(30,cr);NALA_FINISH_CASE(31,cb);NALA_FINISH_CASE(44,at);
#undef NALA_FINISH_CASE
 default:return false;}
}
inline bool Compute(const std::vector<V>&cloth,const std::vector<V>&normals,unsigned ordinal,std::vector<V>&out,std::vector<V>&outNormals){
 Data d;if(!Get(ordinal,d)||cloth.size()!=normals.size())return false;out.assign(d.vertices,{});outNormals.assign(d.vertices,{});
 if(!d.vertices)return true;
 const auto&f=*d.frame;V points[3]{},center{};
 for(unsigned j=0;j<3;++j){for(unsigned k=0;k<3;++k){const auto&a=f.support[j];if(a.index[k]>=cloth.size())return false;points[j]=Add(points[j],Scale(cloth[a.index[k]],a.weight[k]));}center=Add(center,Scale(points[j],1./3));}
 V edge=Sub(points[1],points[0]),other=Sub(points[2],points[0]),normal=Unit(Cross(edge,other));
 double area=std::sqrt(Dot(Cross(edge,other),Cross(edge,other))),scale=std::sqrt(area/f.restArea);
 if(!std::isfinite(scale)||scale<.1||scale>10)return false;
 V tangent=Unit(edge),bitangent=Cross(normal,tangent);double shift=0;
 for(unsigned i=0;i<d.vertices;++i){const auto&a=d.anchors[i];const auto&p=d.local[i];V support{};
  for(unsigned k=0;k<3;++k){if(a.index[k]>=cloth.size())return false;support=Add(support,Scale(cloth[a.index[k]],a.weight[k]));}
  out[i]=Add(center,Scale(Add(Scale(tangent,p[0]),Add(Scale(bitangent,p[1]),Scale(normal,p[2]))),scale));
  shift=std::max(shift,.00005*scale-Dot(Sub(out[i],support),normal));
 }
 for(auto&p:out)p=Add(p,Scale(normal,shift));
 for(unsigned i=0;i<d.indexCount;i+=3){unsigned a=d.source[d.indices[i]],b=d.source[d.indices[i+1]],c=d.source[d.indices[i+2]];V normal=Cross(Sub(out[b],out[a]),Sub(out[c],out[a]));
  outNormals[a]=Add(outNormals[a],normal);outNormals[b]=Add(outNormals[b],normal);outNormals[c]=Add(outNormals[c],normal);
 }
 for(auto&n:outNormals){if(Dot(n,n)<1e-24)return false;n=Unit(n);}return true;
}
inline bool Append(const std::vector<V>&cloth,const std::vector<V>&normals,unsigned ordinal,std::vector<V>&v,std::vector<V>&n,std::vector<float>&uv,std::vector<std::uint16_t>&indices){
 Data d;if(!Get(ordinal,d))return false;if(!d.vertices)return true;
 if(v.size()+d.renderVertices>65535)return false;
 std::vector<V>finish,finishNormals;if(!Compute(cloth,normals,ordinal,finish,finishNormals))return false;
 unsigned start=unsigned(v.size());
 for(unsigned i=0;i<d.renderVertices;++i){unsigned source=d.source[i];if(source>=finish.size())return false;v.push_back(finish[source]);n.push_back(finishNormals[source]);uv.push_back(d.uv[2*i]);uv.push_back(d.uv[2*i+1]);}
 for(unsigned i=0;i<d.indexCount;++i){if(d.indices[i]>=d.renderVertices)return false;indices.push_back(std::uint16_t(start+d.indices[i]));}return true;
}
}
