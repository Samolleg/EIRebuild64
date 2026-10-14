#pragma once

// Seamless liquid textures, generated once (no asset files): the normal map
// and detail map the liquid surface shader samples (liquid_surface.hlsl t22,
// t23). Integer frequencies and wrapped cells make them tile exactly; full
// mip chains let the GPU filter detail by distance instead of shader
// band-limiting. Shared by the renderer and Prototypes/shader-lab.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace owned {

struct LiquidMapLevels {
    static constexpr unsigned normal_size = 256;
    static constexpr unsigned detail_size = 512; // finer swamp and lava detail up close
    std::vector<std::array<float,4>> normals;      // unit normal xyz, 1
    std::vector<std::array<float,4>> detail;       // R lava veins, G lava heat, B fine turbulence, A swamp algae mask
    std::vector<std::array<float,2>> lava_samples; // {heat, brightness} of the drawn lava (emissive lights)
};

inline LiquidMapLevels generate_liquid_map_levels() {
 LiquidMapLevels out;
 constexpr unsigned size=LiquidMapLevels::normal_size;constexpr int wave_count=40;
 struct Wave{float kx,ky,amplitude,phase;};
 std::array<Wave,wave_count> waves{};
 std::uint32_t state=0x9E3779B9u;
 const auto next=[&](){state=state*1664525u+1013904223u;return float(state>>8)*(1.0f/16777216.0f);};
 // Swell-like spectrum: low integer frequencies (seamless), directions spread
 // around the tile's +X axis, and slope falling with frequency so broad waves
 // dominate instead of a fine carpet of noise.
 for(auto& wave:waves){
  const float frequency=1.0f+11.0f*next()*next(),angle=2.2f*(next()-.5f)+(next()<.2f?3.14159265f:0.f);
  wave.kx=std::round(std::cos(angle)*frequency);wave.ky=std::round(std::sin(angle)*frequency);
  if(wave.kx==0&&wave.ky==0)wave.kx=1;
  const float length=std::sqrt(wave.kx*wave.kx+wave.ky*wave.ky);
  wave.amplitude=1.0f/std::pow(length,2.2f);wave.phase=6.2831853f*next();
 }
 std::vector<std::array<float,2>> slopes(size*size);double energy=0;
 for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
  float dx=0,dy=0;const float u=6.2831853f*x/size,v=6.2831853f*y/size;
  for(const auto& wave:waves){const float c=std::cos(wave.kx*u+wave.ky*v+wave.phase)*wave.amplitude;dx+=c*wave.kx;dy+=c*wave.ky;}
  slopes[y*size+x]={dx,dy};energy+=double(dx)*dx+double(dy)*dy;
 }
 constexpr float target_rms_slope=.3f;
 const float scale=target_rms_slope/float(std::sqrt((std::max)(energy/(size*size),1e-12)));
 out.normals.resize(size*size);
 for(unsigned i=0;i<size*size;++i){
  float n[3]={-slopes[i][0]*scale,-slopes[i][1]*scale,1};const float l=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
  out.normals[i]={n[0]/l,n[1]/l,n[2]/l,1};
 }
 // Detail map: periodic gradient noise (every octave's lattice wraps the
 // tile) with domain warping gives organic, seamless shapes without lattice
 // artefacts.
 const auto gradient_noise=[](float x,float y,int period,std::uint32_t seed){
  const int x0=int(std::floor(x)),y0=int(std::floor(y));const float fx=x-x0,fy=y-y0;
  const auto corner=[&](int cx,int cy,float dx,float dy){
   const std::uint32_t wx=std::uint32_t((cx%period+period)%period),wy=std::uint32_t((cy%period+period)%period);
   std::uint32_t h=wx*374761393u+wy*668265263u+seed*1442695041u;h=(h^(h>>13))*1274126177u;h^=h>>16;
   const float angle=float(h)*(6.2831853f/4294967296.0f);
   return std::cos(angle)*dx+std::sin(angle)*dy;
  };
  const auto fade=[](float t){return t*t*t*(t*(t*6-15)+10);};
  const float u=fade(fx),v=fade(fy);
  const float a=corner(x0,y0,fx,fy),b=corner(x0+1,y0,fx-1,fy),c=corner(x0,y0+1,fx,fy-1),d=corner(x0+1,y0+1,fx-1,fy-1);
  return (a+(b-a)*u)+((c+(d-c)*u)-(a+(b-a)*u))*v;
 };
 const auto fbm=[&](float u,float v,int period,int octaves,std::uint32_t seed){
  float sum=0,amplitude=.5f;
  for(int octave=0;octave<octaves;++octave,period*=2,amplitude*=.5f)
   sum+=amplitude*gradient_noise(u*period,v*period,period,seed+octave*7919u);
  return sum;
 };
 constexpr unsigned detail_size=LiquidMapLevels::detail_size;
 auto& detail=out.detail;detail.resize(detail_size*detail_size);
 std::array<float,4> low{1e9f,1e9f,1e9f,1e9f},high{-1e9f,-1e9f,-1e9f,-1e9f};
 for(unsigned y=0;y<detail_size;++y)for(unsigned x=0;x<detail_size;++x){
  const float u=(x+.5f)/detail_size,v=(y+.5f)/detail_size;
  const float qx=fbm(u,v,3,4,11),qy=fbm(u,v,3,4,23);
  const float heat=fbm(u+.45f*qx,v+.45f*qy,3,6,37);
  const float ridge=std::abs(fbm(u+.3f*qy,v+.3f*qx,4,5,53));
  const float veins=std::pow(std::clamp(1-ridge*4.5f,0.f,1.f),6.f);
  const std::array<float,4> value{veins,heat,fbm(u,v,16,3,71),fbm(u+.2f*qx,v+.2f*qy,5,5,97)};
  detail[y*detail_size+x]=value;
  for(int k=0;k<4;++k){low[k]=(std::min)(low[k],value[k]);high[k]=(std::max)(high[k],value[k]);}
 }
 for(auto& value:detail)for(int k=1;k<4;++k)value[k]=(value[k]-low[k])/(std::max)(high[k]-low[k],1e-6f);
 // {heat, brightness} of the lava the shader draws from this map: its two
 // drifting layers read unrelated texels (l1 coarse, l2 rotated and offset),
 // so pairs of hashed texels; the slow pulse at its mean (heat formula and
 // brightness lerp(.72,1.22,l2.b) as in liquid_surface.hlsl). The emissive
 // lights take their colour from these (emissive::rendered_color).
 out.lava_samples.resize(4096);
 for(unsigned k=0;k<out.lava_samples.size();++k){
  std::uint32_t h=k*2654435761u+0x9e3779b9u;h^=h>>15;h*=2246822519u;h^=h>>13;
  const auto& l1=detail[h%(detail_size*detail_size)];
  h*=3266489917u;h^=h>>16;
  const auto& l2=detail[h%(detail_size*detail_size)];
  const float veins=(std::max)(l1[0],l2[0]*.7f);
  const float heat=std::clamp((l1[1]*.75f+l2[1]*.25f-.5f)*2.1f+.40f+veins*.15f*.875f,0.f,1.f);
  out.lava_samples[k]={heat,.72f+.5f*l2[2]};
 }
 return out;
}

// RGBA8 texels of a generated texture and its full box-filtered mip chain
// (finest first). Unit normals (xyz in [-1,1]) are renormalised per mip and
// their alpha stores the length of the plain average of the finest normals
// under each texel (Toksvig variance); other data averages as is.
inline std::vector<std::vector<std::uint32_t>> liquid_map_mips(unsigned size,std::vector<std::array<float,4>> level,bool unit_normal) {
 std::vector<std::vector<std::uint32_t>> mips;
 std::vector<std::array<float,3>> average;
 if(unit_normal)for(const auto& v:level)average.push_back({v[0],v[1],v[2]});
 const auto byte=[&](float value){
  if(unit_normal)value=value*.5f+.5f;
  return std::uint32_t(std::lround(std::clamp(value,0.f,1.f)*255));
 };
 for(unsigned side=size;;side/=2){
  std::vector<std::uint32_t> packed(side*side);
  for(unsigned i=0;i<side*side;++i){
   const auto& v=level[i];
   std::uint32_t alpha=byte(v[3]);
   if(unit_normal){const auto& a=average[i];
    alpha=std::uint32_t(std::lround(std::clamp(std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]),0.f,1.f)*255));}
   packed[i]=byte(v[0])|(byte(v[1])<<8)|(byte(v[2])<<16)|(alpha<<24);
  }
  mips.push_back(std::move(packed));
  if(side==1)break;
  const unsigned half=side/2;std::vector<std::array<float,4>> reduced(half*half);
  for(unsigned y=0;y<half;++y)for(unsigned x=0;x<half;++x){
   std::array<float,4> sum{};
   for(unsigned oy=0;oy<2;++oy)for(unsigned ox=0;ox<2;++ox){const auto& v=level[(y*2+oy)*side+x*2+ox];for(int a=0;a<4;++a)sum[a]+=v[a]*.25f;}
   if(unit_normal){
    const float l=std::sqrt(sum[0]*sum[0]+sum[1]*sum[1]+sum[2]*sum[2]);
    for(int a=0;a<3;++a)sum[a]/=l;
   }
   reduced[y*half+x]=sum;
  }
  level=std::move(reduced);
  if(unit_normal){std::vector<std::array<float,3>> next(half*half);
   for(unsigned y=0;y<half;++y)for(unsigned x=0;x<half;++x)for(unsigned oy=0;oy<2;++oy)for(unsigned ox=0;ox<2;++ox)
    for(int a=0;a<3;++a)next[y*half+x][a]+=average[(y*2+oy)*side+x*2+ox][a]*.25f;
   average=std::move(next);}
 }
 return mips;
}

} // namespace owned
