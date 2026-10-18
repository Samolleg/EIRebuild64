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
 constexpr unsigned size=LiquidMapLevels::normal_size;
 // Ripple spectrum: every integer wave vector with 2..24 cycles per tile
 // (seamless), a random phase and a Rayleigh amplitude each (a Gaussian
 // field, so no single wave stands out), slope falling with frequency and a
 // mild bias toward the tile's X axis. 40 waves at 1..12 cycles, rounded to
 // integer vectors, snapped most of them onto a few directions, several
 // onto (1,0) itself: every tile carried straight tile-wide bands that read
 // as banding on open water (save1146, Gipat south-west sea).
 constexpr int k_min=2,k_max=24;
 std::uint32_t state=0x9E3779B9u;
 const auto next=[&](){state=state*1664525u+1013904223u;return (float(state>>8)+.5f)*(1.0f/16777216.0f);};
 // Separable sum: per kx, the ky waves' cos/sin over v, then over u.
 std::vector<float> cos_u(size),sin_u(size);
 std::vector<std::array<float,2>> slopes(size*size,{0.f,0.f});double energy=0;
 std::vector<float> c1(size),s1(size),c2(size),s2(size);
 for(int kx=0;kx<=k_max;++kx){
  std::fill(c1.begin(),c1.end(),0.f);std::fill(s1.begin(),s1.end(),0.f);
  std::fill(c2.begin(),c2.end(),0.f);std::fill(s2.begin(),s2.end(),0.f);
  bool any=false;
  for(int ky=-k_max;ky<=k_max;++ky){
   if(kx==0&&ky<=0)continue; // one of each +-k pair
   const float k=std::sqrt(float(kx*kx+ky*ky));
   if(k<k_min||k>k_max)continue;
   const float along=float(kx)/k;
   const float amplitude=std::pow(k,-2.7f)*(.55f+.45f*along*along)*std::sqrt(-2.f*std::log(next()))*
                         (std::min)(1.f,(k_max-k)/6.f+.05f);
   const float phase=6.2831853f*next();
   for(unsigned v=0;v<size;++v){
    const float a=6.2831853f*float(ky)*float(v)/float(size)+phase;
    const float c=std::cos(a)*amplitude,s=std::sin(a)*amplitude;
    c1[v]+=c*float(kx);s1[v]+=s*float(kx);c2[v]+=c*float(ky);s2[v]+=s*float(ky);
   }
   any=true;
  }
  if(!any)continue;
  for(unsigned u=0;u<size;++u){const float a=6.2831853f*float(kx)*float(u)/float(size);cos_u[u]=std::cos(a);sin_u[u]=std::sin(a);}
  for(unsigned v=0;v<size;++v)for(unsigned u=0;u<size;++u){
   auto& slope=slopes[v*size+u];
   slope[0]+=c1[v]*cos_u[u]-s1[v]*sin_u[u];slope[1]+=c2[v]*cos_u[u]-s2[v]*sin_u[u];
  }
 }
 for(const auto& slope:slopes)energy+=double(slope[0])*slope[0]+double(slope[1])*slope[1];
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
