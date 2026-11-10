#pragma once
// Whole-source-body packets for the verified x86 geometry gateway.
// Texture/material binding is supplied by the host; UVs are never reinterpreted
// against the old Nala atlas. This header has no access to engine memory.
#include "UnityBodyKernel.h"
#include <limits>
namespace UnityBody {
struct PackedXYZW { float x[4],y[4],z[4],w[4]; };
struct Component { std::uint16_t vertex,normal,uv; };
struct LightComponent { std::uint16_t vertex,normal; };
static_assert(sizeof(PackedXYZW)==64&&sizeof(Component)==6&&sizeof(LightComponent)==4,"native gateway ABI");
struct RenderPacket {
 std::vector<PackedXYZW> plots,normals;
 std::vector<Component> components;
 std::vector<LightComponent> lights;
 std::vector<float> uv;
 std::vector<std::uint16_t> indices;
 std::vector<std::uint32_t> diffuse,specular;
 std::vector<unsigned> materialEnds;
 unsigned vertices=0;
};
inline bool ValidPose(const M&m){
 for(const auto&row:m.r)for(double v:row)if(!std::isfinite(v)||std::abs(v)>1.01)return false;
 if(!std::isfinite(m.t.x)||!std::isfinite(m.t.y)||!std::isfinite(m.t.z)||Dot(m.t,m.t)>3e8)return false;
 M test=Product(m,Inverse(m));
 for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)if(!std::isfinite(test.r[i][j])||std::abs(test.r[i][j]-(i==j))>1e-6)return false;
 M rotation=Proper(m);
 for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)if(!std::isfinite(rotation.r[i][j])||std::abs(rotation.r[i][j]-m.r[i][j])>2e-4)return false;
 return true;
}
inline bool InitializePacket(unsigned vertices,const float*uv,const std::uint16_t*indices,unsigned indexCount,
 const unsigned*faces,unsigned materials,RenderPacket&output){
 if(!vertices||vertices>65535||!uv||!indices||!indexCount||indexCount%3||!faces||!materials)return false;
 RenderPacket p;p.vertices=vertices;
 p.plots.resize((vertices+3)/4);p.normals.resize(p.plots.size());
 p.components.resize(vertices);p.lights.resize(vertices);p.uv.assign(uv,uv+vertices*2);
 p.indices.assign(indices,indices+indexCount);p.diffuse.assign(p.plots.size()*4,0xffffffff);p.specular.assign(p.plots.size()*4,0);
 for(float v:p.uv)if(!std::isfinite(v))return false;
 for(unsigned v:p.indices)if(v>=vertices)return false;
 for(unsigned i=0;i<vertices;++i){p.components[i]={std::uint16_t(i),std::uint16_t(i),std::uint16_t(i)};p.lights[i]={std::uint16_t(i),std::uint16_t(i)};}
 unsigned count=0;for(unsigned i=0;i<materials;++i){if(faces[i]>indexCount/3-count/3)return false;count+=faces[i]*3;p.materialEnds.push_back(count);}
 if(count!=indexCount)return false;
 output=std::move(p);return true;
}
inline bool PreparePacket(const std::vector<V>&positions,const std::vector<V>&normals,
 const M&bodyToWorld,const M&triggerToWorld,RenderPacket&packet){
 if(positions.size()!=packet.vertices||normals.size()!=packet.vertices||!ValidPose(bodyToWorld)||!ValidPose(triggerToWorld))return false;
 const M map=Product(Inverse(triggerToWorld),bodyToWorld);
 // Padding lanes repeat a valid vertex. Normals are directions (w=0), plots w=1.
 for(unsigned i=0;i<packet.plots.size()*4;++i){unsigned index=std::min(i,packet.vertices-1),lane=i%4,block=i/4;
  V p=Point(map,positions[index]),n=Unit(Rotate(map,normals[index]));
  if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||!std::isfinite(n.x)||!std::isfinite(n.y)||!std::isfinite(n.z))return false;
  packet.plots[block].x[lane]=float(p.x);packet.plots[block].y[lane]=float(p.y);packet.plots[block].z[lane]=float(p.z);packet.plots[block].w[lane]=1;
  packet.normals[block].x[lane]=float(n.x);packet.normals[block].y[lane]=float(n.y);packet.normals[block].z[lane]=float(n.z);packet.normals[block].w[lane]=0;
 }
 return true;
}
inline bool ColorPass(unsigned flags,unsigned matrix,unsigned uvOffset){return flags<=3&&matrix==0&&uvOffset==0;}
struct DrawState {
 std::uint32_t owner=0;bool drawn=false,stock=false;
 bool CanReplace(std::uint32_t actor,bool body,unsigned flags,unsigned matrix,unsigned uvOffset)const{
  return owner&&owner==actor&&body&&!drawn&&!stock&&ColorPass(flags,matrix,uvOffset);
 }
 bool Suppress(std::uint32_t actor,bool body,unsigned flags,unsigned matrix,unsigned uvOffset)const{
  return owner&&owner==actor&&body&&drawn&&ColorPass(flags,matrix,uvOffset);
 }
 void NewResult(unsigned result){if(result==1)drawn=true;}
 void StockResult(unsigned result,bool body,unsigned flags,unsigned matrix,unsigned uvOffset){
  if(result==1&&body&&ColorPass(flags,matrix,uvOffset))stock=true;
 }
};
}
