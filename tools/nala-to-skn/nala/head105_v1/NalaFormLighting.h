#pragma once
// Optional presentation lighting. No albedo, geometry, UV or render-state writes.
#include "../UnityRenderBridge.h"
namespace NalaFormLighting {
using namespace UnityBody;
struct Settings {bool enabled=true;unsigned strength=70;};
struct Stats {unsigned vertices=0,changed=0;double minimum=1,maximum=0;};
struct Light {
 V key,up;
 explicit Light(const M&triggerToWorld){
  // Transform WORLD light into the packet's trigger frame, not actor-local.
  // Normals in PreparePacket are already unit length in this same frame.
  M inverse=Inverse(triggerToWorld);
  key=Unit(Rotate(inverse,Unit({.35,-.40,.85})));
  up=Unit(Rotate(inverse,{0,0,1}));
 }
};
inline double Factor(V normal,double mask,const Light&light,Settings settings){
 if(!settings.enabled||!settings.strength||!std::isfinite(mask)||mask<=0)return 1;
 if(!std::isfinite(Dot(normal,normal))||Dot(normal,normal)<.5)return 1;
 const double key=std::max(0.,Dot(normal,light.key));
 const double sky=.5+.5*std::clamp(Dot(normal,light.up),-1.,1.);
 const double form=std::clamp(.50+.38*key+.12*sky,.50,1.);
 const double amount=std::min(settings.strength,100u)*.01*std::clamp(mask,0.,1.);
 return 1-amount*(1-form);
}
inline std::uint32_t Shade(std::uint32_t color,unsigned gain){
 if(gain>=65536)return color;
 // Specular alpha carries legacy vertex fog. Preserve it exactly, along with
 // diffuse alpha; only the RGB contribution is modulated.
 std::uint32_t out=color&0xff000000u;
 for(unsigned shift:{0u,8u,16u})out|=((((color>>shift)&255)*gain+32768)>>16)<<shift;
 return out;
}
inline Stats Apply(RenderPacket&packet,const M&triggerToWorld,Settings settings,
                   const std::vector<double>*mask=nullptr){
 Stats stats;
 if(!settings.enabled||!settings.strength)return stats; // Exact former path.
 if(!packet.vertices||packet.normals.size()*4<packet.vertices||
    packet.diffuse.size()<packet.vertices||packet.specular.size()<packet.vertices||
    (mask&&mask->size()!=packet.vertices)||!ValidPose(triggerToWorld))return stats;
 Light light(triggerToWorld);
 for(unsigned i=0;i<packet.diffuse.size();++i){
  const unsigned vertex=std::min(i,packet.vertices-1),lane=i%4;
  if(i/4>=packet.normals.size()||i>=packet.specular.size())break;
  const auto&n=packet.normals[i/4];
  const double factor=Factor({n.x[lane],n.y[lane],n.z[lane]},mask?(*mask)[vertex]:1,light,settings);
  if(i<packet.vertices){++stats.vertices;stats.minimum=std::min(stats.minimum,factor);stats.maximum=std::max(stats.maximum,factor);if(factor<1)++stats.changed;}
  const unsigned gain=unsigned(factor*65536+.5);
  packet.diffuse[i]=Shade(packet.diffuse[i],gain);packet.specular[i]=Shade(packet.specular[i],gain);
 }
 return stats;
}
}
