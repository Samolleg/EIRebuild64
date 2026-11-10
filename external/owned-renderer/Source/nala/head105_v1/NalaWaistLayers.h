#pragma once
#include "../UnityRenderBridge.h"
#include "NalaWaistLayersData.h"
#include "NalaStarterShirtData.h"
#include "NalaFormLighting.h"

// First shared waist interface: one under-wrap profile per shirt CUT; the
// pants declare an outer wrap. Fabric variants do not create new pair fits.
namespace NalaWaistLayers {
using namespace UnityBody;
// Client high16 selects the reviewed sleeveless cut. A future cut must register
// its own waist profile; never apply this cut's vertex IDs to another mesh.
inline bool SupportsShirtCut(unsigned subtype){return (subtype>>16)==1u;}
inline bool UnderWrap(bool wrapDrawn,bool restored,bool profileReady){return wrapDrawn&&restored&&profileReady;}
inline bool Resolve(const NalaWaistLayersData::Anchor&a,const std::vector<V>&body,const std::vector<V>&normal,V&center,V&offset){
 center={};V n{};
 for(unsigned k=0;k<3;++k){if(a.index[k]>=body.size()||a.index[k]>=normal.size())return false;center=Add(center,Scale(body[a.index[k]],a.weight[k]));n=Add(n,Scale(normal[a.index[k]],a.weight[k]));}
 n=Unit(n);const V edge=Sub(body[a.index[1]],body[a.index[0]]),t=Unit(Sub(edge,Scale(n,Dot(edge,n)))),b=Cross(n,t);
 offset=Add(Scale(t,a.offset[0]),Add(Scale(b,a.offset[1]),Scale(n,a.offset[2])));
 return std::isfinite(Dot(center,center))&&std::isfinite(Dot(offset,offset));
}
inline bool Compute(const std::vector<V>&body,const std::vector<V>&bodyNormals,bool overShirt,std::vector<V>&out,std::vector<V>&normal){
 if(body.size()!=bodyNormals.size())return false;
 const unsigned count=unsigned(std::size(NalaWaistLayersData::BareWrap));out.resize(count);normal.assign(count,{});
 const auto*anchors=overShirt?NalaWaistLayersData::OverWrap:NalaWaistLayersData::BareWrap;
 for(unsigned i=0;i<count;++i){V center,offset;if(!Resolve(anchors[i],body,bodyNormals,center,offset))return false;out[i]=Add(center,offset);}
 for(unsigned i=0;i<std::size(NalaWaistLayersData::Indices);i+=3){auto a=NalaWaistLayersData::Indices[i],b=NalaWaistLayersData::Indices[i+1],c=NalaWaistLayersData::Indices[i+2];
  if(a>=count||b>=count||c>=count)return false;auto n=Cross(Sub(out[b],out[a]),Sub(out[c],out[a]));normal[a]=Add(normal[a],n);normal[b]=Add(normal[b],n);normal[c]=Add(normal[c],n);}
 std::vector<V>shared(count);
 for(unsigned i=0;i<count;++i){auto group=NalaWaistLayersData::NormalGroups[i];if(group>=count)return false;shared[group]=Add(shared[group],normal[i]);}
 for(unsigned i=0;i<count;++i)normal[i]=Unit(shared[NalaWaistLayersData::NormalGroups[i]]);return true;
}
inline unsigned ShadeContacts(RenderPacket&packet,bool overShirt,unsigned start,bool enabled,unsigned strength){
 const unsigned count=unsigned(std::size(NalaWaistLayersData::BareWrap));
 if(!enabled||!strength||packet.vertices!=start+count*2||packet.diffuse.size()<packet.vertices||packet.specular.size()<packet.vertices)return 0;
 const auto*contact=overShirt?NalaWaistLayersData::ContactOver:NalaWaistLayersData::ContactBare;
 unsigned changed=0;
 for(unsigned i=0;i<count;++i){double weight=std::clamp(double(contact[i]),0.,1.);if(weight<=0)continue;
  unsigned gain=unsigned((1-.55*std::min(strength,100u)*.01*weight)*65536+.5);
  for(unsigned side=0;side<2;++side){unsigned at=start+side*count+i;packet.diffuse[at]=NalaFormLighting::Shade(packet.diffuse[at],gain);packet.specular[at]=NalaFormLighting::Shade(packet.specular[at],gain);++changed;}
 }
 return changed;
}
inline bool ShirtProfile(const std::vector<V>&body,const std::vector<V>&bodyNormals,const std::vector<V>&plain,std::vector<V>&out,std::vector<V>&normal){
 if(body.size()!=bodyNormals.size()||plain.size()!=std::size(NalaStarterShirtData::Cloth))return false;
 out=plain;normal.assign(out.size(),{});
 for(unsigned i=0;i<std::size(NalaWaistLayersData::ShirtIndices);++i){const auto id=NalaWaistLayersData::ShirtIndices[i];if(id>=out.size())return false;
  V center,delta;if(!Resolve(NalaWaistLayersData::ShirtDelta[i],body,bodyNormals,center,delta))return false;out[id]=Add(plain[id],delta);}
 for(unsigned i=0;i<std::size(NalaStarterShirtData::Indices);i+=3){auto a=NalaStarterShirtData::Source[NalaStarterShirtData::Indices[i]],b=NalaStarterShirtData::Source[NalaStarterShirtData::Indices[i+1]],c=NalaStarterShirtData::Source[NalaStarterShirtData::Indices[i+2]];
  auto n=Cross(Sub(out[b],out[a]),Sub(out[c],out[a]));normal[a]=Add(normal[a],n);normal[b]=Add(normal[b],n);normal[c]=Add(normal[c],n);}
 for(auto&n:normal)n=Unit(n);return true;
}
inline bool Append(const std::vector<V>&positions,const std::vector<V>&normals,std::vector<V>&v,std::vector<V>&n,std::vector<float>&uv,std::vector<std::uint16_t>&indices){
 const unsigned count=unsigned(std::size(NalaWaistLayersData::BareWrap)),start=unsigned(v.size());
 if(positions.size()!=count||normals.size()!=count||v.size()!=n.size()||uv.size()!=v.size()*2||start+count*2>65535)return false;
 for(unsigned side=0;side<2;++side)for(unsigned i=0;i<count;++i){v.push_back(side?Sub(positions[i],Scale(normals[i],.00035)):positions[i]);n.push_back(side?Scale(normals[i],-1):normals[i]);uv.push_back(NalaWaistLayersData::UV[i*2]);uv.push_back(NalaWaistLayersData::UV[i*2+1]);}
 for(unsigned i=0;i<std::size(NalaWaistLayersData::Indices);i+=3){auto a=NalaWaistLayersData::Indices[i],b=NalaWaistLayersData::Indices[i+1],c=NalaWaistLayersData::Indices[i+2];
  indices.insert(indices.end(),{std::uint16_t(start+a),std::uint16_t(start+b),std::uint16_t(start+c),std::uint16_t(start+count+a),std::uint16_t(start+count+c),std::uint16_t(start+count+b)});}
 for(const auto&e:NalaWaistLayersData::Rims){auto a=e[0],b=e[1];indices.insert(indices.end(),{std::uint16_t(start+a),std::uint16_t(start+b),std::uint16_t(start+count+b),std::uint16_t(start+a),std::uint16_t(start+count+b),std::uint16_t(start+count+a)});}
 return true;
}
}
