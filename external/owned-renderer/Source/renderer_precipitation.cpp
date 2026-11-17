// Owned precipitation: the rain and snow of the game's CWeather, drawn by the
// renderer instead of the stock CPU-built triangle sets (whose DrawRain and
// DrawSnow calls the bridge suppresses). Drawn into the world colour after
// the wind trails and before SMAA: a depth-aware veil with falling sheets for
// the distant rain, then one instanced draw of streaks (rain) or soft flakes
// (snow), lit by the world light, fogged like world surfaces and faded
// against the scene depth. Policy (what falls, how strongly, how many drops
// per quality) in precipitation.h; shader in precipitation_shader.h.
#include "precipitation.h"
#include "precipitation_shader.h"
#include "ambient_particles.h"
#include "ambient_particle_frame.h"
#include "renderer.h"
#include "engine_bridge.h"
#include "world_fog.h"
#include "owned_asset_loader.h"
#include "owned_resource_runtime.h"
#include "owned_resource_store.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace owned {
namespace {
using namespace precipitation;

struct PrecipitationConstants {
    float clip[16];
    float eye[4], window[4], fall[4], motion[4], shape[4], color[4], glint[4], sun_direction[4];
    float thin[4], veil[4], veil_color[4], screen[4], shelter0[4], shelter1[4], shelter2[4], splash[4], snow[4], sheet[4], drift[4], sand[4], band[4], sand2[4];
    float fog_range[4], fog_control[4], fog_color[4], depth[4];
    float fog_seam[4], fog_curve[4], fog_span_lo[4], fog_span_hi[4];
    float snow_light[4], snow_sun[4];
    float flow[4], frame[4];
};
static_assert(sizeof(PrecipitationConstants) % 16 == 0);

constexpr float kTilePeriod = 10.f;  // world-periodic tile of the drop field (metres)
} // namespace

void append_precipitation_shader_programs(std::vector<ShaderProgram>& out) {
    static const std::string source = std::string(kAtmosphereShader) + kNeighbourFogHlsl + kPrecipitationShader;
    out.push_back({source, "precipitation", nullptr, "precipitationVS", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "precipitation", nullptr, "precipitationPS", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "precipitation", nullptr, "precipitationVeilVS", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "precipitation", nullptr, "precipitationVeilPS", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "precipitation", nullptr, "precipitationSplashVS", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "precipitation", nullptr, "precipitationSplashPS", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({kShelterFilterShader, "precipitation_shelter", nullptr, "shelterFilterCS", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({kShelterFilterShader, "precipitation_shelter", nullptr, "shelterBlurCS", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({kRainRingsShader, "precipitation_rings", nullptr, "rainRingsCS", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "precipitation", nullptr, "precipitationSandPS", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});  // 9: sand (bands, ground sheets, grains)
}

#if !defined(EID3D11_SHADER_EMBED)
// Shelter map (precipitation occlusion): the persistent static casters drawn
// straight down into one depth map through the shadow depth path, so no
// rain, splash or wetness reaches under roofs, bridges or dense canopies.
// The bridge decides when to bake (execute_owned_rain_occlusion_pass) from
// the request render_precipitation publishes; depth 0 is the top of the
// request's slab, 1 its bottom (the highest surface wins).
bool Renderer::begin_native_rain_occlusion(const NativeShadowSlice& slice,UINT side) noexcept {
 SCENE_ENTRY("begin_native_rain_occlusion");
 if(native_shadow_collecting_||native_point_shadow_active_||native_rain_active_||(!side&&!d3d12_)||(!d3d12_&&!native_shadow_build_cs_))return false;
 if(FAILED(flush_native()))return false;
 native_rain_map_=side!=0; // 0: the store's world preparation alone, no map resources
 if(d3d12_&&native_rain_map_){ // D3D12 backend: one D32 slice read as Texture2D and its R8 foliage mask, the same flush (flush_native_shadows_d3d12)
  if(!rain12_.shelter||!rain12_.foliage||native_rain_side_!=side){
   rain12_.shelter.reset();rain12_.foliage.reset();native_rain_valid_=false;
   if(FAILED(d3d12_->create_depth_array(side,1,rain12_.shelter,DXGI_FORMAT_D32_FLOAT,true))||
      FAILED(d3d12_->create_target(side,side,rain12_.foliage,DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_UNKNOWN))){rain12_.shelter.reset();rain12_.foliage.reset();return false;}
   native_rain_side_=side;
  }
 } else if(!d3d12_) {
 if(!native_rain_constants_){
  D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(NativeShadowConstants);
  desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  if(FAILED(device_->CreateBuffer(&desc,nullptr,&native_rain_constants_)))return false;
 }
 if(!native_rain_depth_||native_rain_side_!=side){
  native_rain_depth_.Reset();native_rain_dsv_.Reset();native_rain_srv_.Reset();native_rain_valid_=false;
  D3D11_TEXTURE2D_DESC description{};description.Width=description.Height=side;description.ArraySize=1;description.MipLevels=1;
  description.Format=DXGI_FORMAT_R32_TYPELESS;description.SampleDesc.Count=1;description.Usage=D3D11_USAGE_DEFAULT;
  description.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
  D3D11_DEPTH_STENCIL_VIEW_DESC view{};view.Format=DXGI_FORMAT_D32_FLOAT;view.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
  D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_FLOAT;srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;
  native_rain_foliage_.Reset();native_rain_foliage_rtv_.Reset();native_rain_foliage_srv_.Reset();
  D3D11_TEXTURE2D_DESC mask=description;mask.Format=DXGI_FORMAT_R8_UNORM;mask.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
  if(FAILED(device_->CreateTexture2D(&description,nullptr,&native_rain_depth_))||
     FAILED(device_->CreateDepthStencilView(native_rain_depth_.Get(),&view,&native_rain_dsv_))||
     FAILED(device_->CreateShaderResourceView(native_rain_depth_.Get(),&srv,&native_rain_srv_))||
     FAILED(device_->CreateTexture2D(&mask,nullptr,&native_rain_foliage_))||
     FAILED(device_->CreateRenderTargetView(native_rain_foliage_.Get(),nullptr,&native_rain_foliage_rtv_))||
     FAILED(device_->CreateShaderResourceView(native_rain_foliage_.Get(),nullptr,&native_rain_foliage_srv_))){
   native_rain_depth_.Reset();native_rain_dsv_.Reset();native_rain_srv_.Reset();
   native_rain_foliage_.Reset();native_rain_foliage_rtv_.Reset();native_rain_foliage_srv_.Reset();return false;
  }
  native_rain_side_=side;
 }
 }
 native_rain_slice_=slice;if(native_rain_map_)native_rain_valid_=false;
 native_shadow_dirty_mask_=1;native_shadow_cleared_mask_=0;
 native_shadow_failed_=false;native_shadow_collecting_=true;native_rain_active_=true;
 if(SUCCEEDED(upload_native_shadow_constants(0)))return true;
 end_native_rain_occlusion(false);return false;
}

bool Renderer::end_native_rain_occlusion(bool commit) noexcept {
 SCENE_ENTRY("end_native_rain_occlusion");
 if(!native_rain_active_)return false;
 HRESULT hr=E_FAIL;
 try{if(commit&&!native_shadow_failed_)hr=flush_native_shadows();else discard_native();}
 catch(...){discard_native();}
 native_shadow_collecting_=false;native_rain_active_=false;
 native_shadow_dirty_mask_=native_shadow_cleared_mask_=0;native_shadow_failed_=false;
 // The preparation alone leaves the shelter map as it was.
 if(!native_rain_map_){native_rain_map_=true;return hr==S_OK;}
 // D3D12: S_FALSE is a pass that waited for its pipeline (map unwritten), never published.
 native_rain_valid_=d3d12_?hr==S_OK:SUCCEEDED(hr);
 if(native_rain_valid_)++native_rain_bakes_;
 native_shelter_soft_valid_=native_rain_valid_&&filter_native_rain_shelter();
 // The soft map serves the snow and wet surfaces (t70); without it they
 // take no shelter (a filter failure is named in the debug output).
 if(context_){ID3D11ShaderResourceView* shelter=!native_rain_valid_?nullptr:native_shelter_soft_valid_?native_shelter_soft_srv_[0].Get():native_rain_srv_.Get();context_->PSSetShaderResources(70,1,&shelter);}
 return native_rain_valid_;
}

// The soft shelter map for snow and wet surfaces, made once per bake (the
// bake runs on a store or request change, at most every 60 frames): a
// closing (0.5 m dilate, then erode) fills the leaf gaps of crowns at their
// height (d9bc4d22: the cover traced each leaf cut-out, frame_000359); an
// exact distance transform (within 3 m) gives each texel its distance inside
// the closed outline, filled gaps within 2 m mark a crown, and the largest
// inside distance within 3 m is the crown's radius (c3c9fea1: shelter must
// fall off from the centre to the rim, frame_001039).
// D3D11: the soft-shelter compute shaders and their constants. Created at
// pipeline creation (renderer_device.cpp): at the first bake in play they
// cost 20.7 + 24.2 ms on the scene thread (laptop j192,
// shader_created_in_play). False once they failed (named once).
bool Renderer::create_shelter_filter_shaders() noexcept {
 if(d3d12_||!device_||native_shelter_soft_failed_)return false;
 if(!native_shelter_soft_cs_){
  std::vector<ShaderProgram> programs;append_precipitation_shader_programs(programs);
  ComPtr<ID3DBlob> blob,errors;
  // int4 mode, float4 radii. (397cdd05 put a comment mid-line here and it
  // swallowed the usage and bind flags: CreateBuffer failed on every run, so
  // the soft map never existed and snow took no shelter at all.)
  D3D11_BUFFER_DESC b{};b.ByteWidth=32;b.Usage=D3D11_USAGE_DYNAMIC;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  HRESULT hr=programs.size()<7?E_FAIL:compile_shader(programs[6],&blob,&errors);const char* step="compile";
  if(SUCCEEDED(hr)){step="shader";hr=device_->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&native_shelter_soft_cs_);}
  if(SUCCEEDED(hr)){step="blur compile";blob.Reset();hr=programs.size()<8?E_FAIL:compile_shader(programs[7],&blob,&errors);}
  if(SUCCEEDED(hr)){step="blur shader";hr=device_->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&native_shelter_blur_cs_);}
  if(SUCCEEDED(hr)){step="constants";hr=device_->CreateBuffer(&b,nullptr,&native_shelter_soft_constants_);}
  if(FAILED(hr)){
   if(errors)OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
   char reason[128];std::snprintf(reason,sizeof(reason),"step=%s hr=%08lx; snow and wet surfaces use the raw shelter map",step,static_cast<unsigned long>(hr));
   bridge_report_event("precip_shelter_filter_unavailable",reason);
   native_shelter_soft_failed_=true;native_shelter_soft_cs_.Reset();native_shelter_blur_cs_.Reset();return false;
  }
 }
 return true;
}

bool Renderer::filter_native_rain_shelter() noexcept {
 if(d3d12_)return filter_native_rain_shelter_d3d12();
 if(native_shelter_soft_failed_||!native_rain_srv_||!native_rain_side_||!context_)return false;
 const UINT side=native_rain_side_;
 if(!create_shelter_filter_shaders())return false;
 if(native_shelter_soft_side_!=side){
  for(unsigned k=0;k<2;++k){native_shelter_soft_[k].Reset();native_shelter_soft_srv_[k].Reset();native_shelter_soft_uav_[k].Reset();}
  native_shelter_soft_side_=0;
  D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=side;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R16G16B16A16_UNORM;
  d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
  for(unsigned k=0;k<2;++k){
   HRESULT hr=device_->CreateTexture2D(&d,nullptr,&native_shelter_soft_[k]);
   if(SUCCEEDED(hr))hr=device_->CreateShaderResourceView(native_shelter_soft_[k].Get(),nullptr,&native_shelter_soft_srv_[k]);
   if(SUCCEEDED(hr))hr=device_->CreateUnorderedAccessView(native_shelter_soft_[k].Get(),nullptr,&native_shelter_soft_uav_[k]);
   if(FAILED(hr)){
    for(unsigned n=0;n<2;++n){native_shelter_soft_[n].Reset();native_shelter_soft_srv_[n].Reset();native_shelter_soft_uav_[n].Reset();}
    char reason[128];std::snprintf(reason,sizeof(reason),"maps side=%u hr=%08lx; snow and wet surfaces use the raw shelter map",side,static_cast<unsigned long>(hr));
    bridge_report_event("precip_shelter_filter_unavailable",reason);
    return false;
   }
  }
  native_shelter_soft_side_=side;
 }
 // Radii in texels from world metres (the map spans 96 m). The distance
 // reach is 3 m at every map size (64 texels at 2048, read in 2-texel steps
 // past 32, so the tap count stays bounded); at 32 texels Ultra measured
 // crowns only to 1.5 m, so a big pine's rim faded over half a metre and
 // its dry patch read as a hard-edged polygon (bfa1e170 F11).
 const float texels=float(side)/96.f;
 // Closing 1.25 m (a5650978 debug view: with 0.5 m a pine's crown stayed a
 // lace of cards, so its inside distance was a few texels everywhere and the
 // crown rule gave it no cover; up to 2.5 m gaps now close at crown height).
 const int close=(std::min)(32,(std::max)(1,int(1.25f*texels+.5f))),gap_reach=(std::min)(32,(std::max)(2,int(2.f*texels+.5f))),
           reach=(std::min)(64,(std::max)(2,int(3.f*texels+.5f)));
 // Its own profiler pass (the bake's precip_shelter before it).
 transition_profile_pass("precip_shelter_filter");
 // It binds every compute slot it reads and unbinds them after; every other
 // compute pass sets its own shader, b0, t0-t1 and u0.
 // The pixel stage must not hold the map while it is written.
 ID3D11ShaderResourceView* none=nullptr;context_->PSSetShaderResources(70,1,&none);
 context_->CSSetShader(native_shelter_soft_cs_.Get(),nullptr,0);
 context_->CSSetConstantBuffers(0,1,native_shelter_soft_constants_.GetAddressOf());
 // Passes: dilate x (from the raw depth), dilate y, erode x, erode y (and
 // mark the filled gaps), distance rows, distance columns, radius rows,
 // radius columns. Ping-pong 0, 1, 0, ...; the eighth pass writes map 1.
 const int modes[8][3]={{0,1,0},{1,0,1},{2,1,0},{3,0,1},{4,1,0},{5,0,1},{6,1,0},{7,0,1}};
 bool ok=true;
 for(unsigned pass=0;pass<8;++pass){
  const unsigned target=pass&1u;
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(FAILED(context_->Map(native_shelter_soft_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))){ok=false;break;}
  const int mode[4]={modes[pass][0],modes[pass][1],modes[pass][2],int(side)};
  const float radii[4]={float(close),float(gap_reach),float(reach),texels};
  std::memcpy(mapped.pData,mode,sizeof(mode));std::memcpy(static_cast<char*>(mapped.pData)+sizeof(mode),radii,sizeof(radii));context_->Unmap(native_shelter_soft_constants_.Get(),0);
  ID3D11UnorderedAccessView* no_uav=nullptr;context_->CSSetUnorderedAccessViews(0,1,&no_uav,nullptr);
  ID3D11ShaderResourceView* inputs[3]={native_rain_srv_.Get(),pass?native_shelter_soft_srv_[target^1u].Get():nullptr,native_rain_foliage_srv_.Get()};
  context_->CSSetShaderResources(0,3,inputs);
  context_->CSSetUnorderedAccessViews(0,1,native_shelter_soft_uav_[target].GetAddressOf(),nullptr);
  context_->Dispatch((side+255)/256,side,1); // one 256-texel line segment per group
 }
 ID3D11UnorderedAccessView* no_uav=nullptr;context_->CSSetUnorderedAccessViews(0,1,&no_uav,nullptr);
 ID3D11ShaderResourceView* no_inputs[3]{};context_->CSSetShaderResources(0,3,no_inputs);
 ID3D11Buffer* no_buffer=nullptr;context_->CSSetConstantBuffers(0,1,&no_buffer);context_->CSSetShader(nullptr,nullptr,0);
 return ok&&blur_native_rain_shelter();
}

// The pre-filtered cover the pixel shaders read (shelterBlurCS: soft map 1
// in, map 0 out, one 16 x 16 tile per group), once per bake and again when
// the crown fill-in has moved by 1/256 (renderer_terrain.cpp; every few game
// seconds while it rains or snows). The Gaussian's sigma, 0.14 m, matches
// the spread of the former per-pixel feather (a bilinear centre at 0.4 and
// six taps at 0.25 m, 0.1 each: 0.0375 m^2 over two axes), cut at 2.5 sigma.
bool Renderer::blur_native_rain_shelter() noexcept {
 if(d3d12_)return blur_native_rain_shelter_d3d12();
 if(!native_shelter_blur_cs_||!native_shelter_soft_side_||!context_)return false;
 const UINT side=native_shelter_soft_side_;
 const float texels=float(side)/96.f,sigma=.14f*texels;
 D3D11_MAPPED_SUBRESOURCE mapped{};
 if(FAILED(context_->Map(native_shelter_soft_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
 const int mode[4]={8,0,0,int(side)};
 const float radii[4]={float((std::min)(8,int(std::ceil(2.5f*sigma)))),sigma,shelter_fill_,texels};
 std::memcpy(mapped.pData,mode,sizeof(mode));std::memcpy(static_cast<char*>(mapped.pData)+sizeof(mode),radii,sizeof(radii));context_->Unmap(native_shelter_soft_constants_.Get(),0);
 // The pixel stage must not hold map 0 while it is written.
 ID3D11ShaderResourceView* none=nullptr;context_->PSSetShaderResources(70,1,&none);
 context_->CSSetShader(native_shelter_blur_cs_.Get(),nullptr,0);
 context_->CSSetConstantBuffers(0,1,native_shelter_soft_constants_.GetAddressOf());
 ID3D11ShaderResourceView* inputs[2]={nullptr,native_shelter_soft_srv_[1].Get()};context_->CSSetShaderResources(0,2,inputs);
 context_->CSSetUnorderedAccessViews(0,1,native_shelter_soft_uav_[0].GetAddressOf(),nullptr);
 context_->Dispatch((side+15)/16,(side+15)/16,1);
 ID3D11UnorderedAccessView* no_uav=nullptr;context_->CSSetUnorderedAccessViews(0,1,&no_uav,nullptr);
 ID3D11ShaderResourceView* no_inputs[2]{};context_->CSSetShaderResources(0,2,no_inputs);
 ID3D11Buffer* no_buffer=nullptr;context_->CSSetConstantBuffers(0,1,&no_buffer);context_->CSSetShader(nullptr,nullptr,0);
 native_shelter_blur_fill_=shelter_fill_;
 return true;
}

// The rain ring texture wet ground reads (rainRingsCS, nativeRainRingSlope
// t71): 512 x 512 RG16F (1 MB) over a 6.25 m tile, 1.2 cm texels, nine per
// ring wavelength. One dispatch (64 x 64 groups of 8 x 8)
// dispatch whenever the game time or the rain amount changed (every drawn
// frame while it rains, nothing while paused). False without it (the
// failure is named once; wet ground then shows no rings).
bool Renderer::update_native_rain_rings(float seconds,float rain) noexcept {
 if(d3d12_)return update_native_rain_rings_d3d12(seconds,rain);
 if(native_rain_rings_failed_||!device_||!context_)return false;
 constexpr UINT side=512;
 if(!native_rain_rings_cs_){
  std::vector<ShaderProgram> programs;append_precipitation_shader_programs(programs);
  ComPtr<ID3DBlob> blob,errors;const char* step="compile";
  HRESULT hr=programs.size()<9?E_FAIL:compile_shader(programs[8],&blob,&errors);
  if(SUCCEEDED(hr)){step="shader";hr=device_->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&native_rain_rings_cs_);}
  D3D11_BUFFER_DESC b{};b.ByteWidth=16;b.Usage=D3D11_USAGE_DYNAMIC;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  if(SUCCEEDED(hr)){step="constants";hr=device_->CreateBuffer(&b,nullptr,&native_rain_rings_constants_);}
  D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=side;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R16G16_FLOAT;
  d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
  if(SUCCEEDED(hr)){step="texture";hr=device_->CreateTexture2D(&d,nullptr,&native_rain_rings_);}
  if(SUCCEEDED(hr))hr=device_->CreateShaderResourceView(native_rain_rings_.Get(),nullptr,&native_rain_rings_srv_);
  if(SUCCEEDED(hr))hr=device_->CreateUnorderedAccessView(native_rain_rings_.Get(),nullptr,&native_rain_rings_uav_);
  if(FAILED(hr)){
   if(errors)OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
   char reason[96];std::snprintf(reason,sizeof(reason),"step=%s hr=%08lx; wet ground shows no rain rings",step,static_cast<unsigned long>(hr));
   bridge_report_event("precip_rain_rings_unavailable",reason);
   native_rain_rings_failed_=true;native_rain_rings_cs_.Reset();native_rain_rings_constants_.Reset();
   native_rain_rings_.Reset();native_rain_rings_srv_.Reset();native_rain_rings_uav_.Reset();return false;
  }
 }
 if(seconds==native_rain_rings_key_[0]&&rain==native_rain_rings_key_[1])return true;
 D3D11_MAPPED_SUBRESOURCE mapped{};
 if(FAILED(context_->Map(native_rain_rings_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
 const float time[4]={seconds,rain,float(side),0.f};std::memcpy(mapped.pData,time,sizeof(time));
 context_->Unmap(native_rain_rings_constants_.Get(),0);
 // The pixel stage must not hold the texture while it is written.
 ID3D11ShaderResourceView* none=nullptr;context_->PSSetShaderResources(71,1,&none);
 context_->CSSetShader(native_rain_rings_cs_.Get(),nullptr,0);
 context_->CSSetConstantBuffers(0,1,native_rain_rings_constants_.GetAddressOf());
 context_->CSSetUnorderedAccessViews(0,1,native_rain_rings_uav_.GetAddressOf(),nullptr);
 context_->Dispatch(side/8,side/8,1);
 ID3D11UnorderedAccessView* no_uav=nullptr;context_->CSSetUnorderedAccessViews(0,1,&no_uav,nullptr);
 ID3D11Buffer* no_buffer=nullptr;context_->CSSetConstantBuffers(0,1,&no_buffer);context_->CSSetShader(nullptr,nullptr,0);
 native_rain_rings_key_[0]=seconds;native_rain_rings_key_[1]=rain;
 return true;
}

// filter_native_rain_shelter on D3D12: the same eight shelterFilterCS passes
// (radii, modes and ping-pong as above) over the shelter map at t0, into
// two RGBA16 storage textures; the eighth pass writes soft[1], and the blur
// (blur_native_rain_shelter_d3d12) writes the cover the shaders read, soft[0].
// The bake's foliage mask (rain12_.foliage, written beside the slice) at t2.
bool Renderer::filter_native_rain_shelter_d3d12() noexcept try {
 if(native_shelter_soft_failed_||!rain12_.shelter||!rain12_.foliage||!native_rain_side_)return false;
 const UINT side=native_rain_side_;
 auto& programs=ambient12_.precipitation;
 if(programs.empty())append_precipitation_shader_programs(programs);
 const auto unavailable=[&](const char* step,HRESULT hr){
  char reason[128];std::snprintf(reason,sizeof(reason),"D3D12 step=%s hr=%08lx; snow and wet surfaces use the raw shelter map",step,static_cast<unsigned long>(hr));
  bridge_report_event("precip_shelter_filter_unavailable",reason);
  native_shelter_soft_failed_=true;rain12_.soft={};native_shelter_soft_side_=0;return false;
 };
 if(programs.size()<7)return unavailable("program",E_FAIL);
 if(native_shelter_soft_side_!=side||!rain12_.soft[0]||!rain12_.soft[1]){
  rain12_.soft={};native_shelter_soft_side_=0;
  if(!d3d12_->storage_supported(DXGI_FORMAT_R16G16B16A16_UNORM))return unavailable("format",E_NOTIMPL);
  for(auto& map:rain12_.soft)
   if(const HRESULT hr=d3d12_->create_storage_texture(side,side,0,DXGI_FORMAT_R16G16B16A16_UNORM,map);FAILED(hr))return unavailable("maps",hr);
  native_shelter_soft_side_=side;
 }
 const float texels=float(side)/96.f;
 const int close=(std::min)(32,(std::max)(1,int(1.25f*texels+.5f))),gap_reach=(std::min)(32,(std::max)(2,int(2.f*texels+.5f))),
           reach=(std::min)(64,(std::max)(2,int(3.f*texels+.5f)));
 transition_profile_pass("precip_shelter_filter");
 const int modes[8][3]={{0,1,0},{1,0,1},{2,1,0},{3,0,1},{4,1,0},{5,0,1},{6,1,0},{7,0,1}};
 HRESULT hr=S_OK;
 for(unsigned pass=0;pass<8&&hr==S_OK;++pass){
  const unsigned target=pass&1u;
  struct{int mode[4];float radii[4];} values{{modes[pass][0],modes[pass][1],modes[pass][2],int(side)},{float(close),float(gap_reach),float(reach),texels}};
  d3d12_->clear_slots();
  d3d12_->set_constants(0,&values,sizeof(values));
  d3d12_->set_resource(0,rain12_.shelter.get());
  d3d12_->set_resource(1,pass?rain12_.soft[target^1u].get():nullptr);
  d3d12_->set_resource(2,rain12_.foliage.get());
  d3d12_->set_unordered(0,rain12_.soft[target].get());
  hr=d3d12_->dispatch(programs[6],(side+255)/256,side,1); // one 256-texel line segment per group
 }
 d3d12_->clear_slots();
 if(FAILED(hr))return unavailable("dispatch",hr);
 if(hr!=S_OK){if(hr==S_FALSE)d3d12_->omit("rain shelter soft map (pipeline not ready)",false);return false;}
 return blur_native_rain_shelter_d3d12();
}catch(...){return false;}

// blur_native_rain_shelter on D3D12: shelterBlurCS, soft[1] at t1 into
// soft[0], with the same constants.
bool Renderer::blur_native_rain_shelter_d3d12() noexcept try {
 auto& programs=ambient12_.precipitation;
 if(programs.size()<8||!native_shelter_soft_side_||!rain12_.soft[0]||!rain12_.soft[1])return false;
 const UINT side=native_shelter_soft_side_;
 const float texels=float(side)/96.f,sigma=.14f*texels;
 struct{int mode[4];float radii[4];} values{{8,0,0,int(side)},{float((std::min)(8,int(std::ceil(2.5f*sigma)))),sigma,shelter_fill_,texels}};
 d3d12_->clear_slots();
 d3d12_->set_constants(0,&values,sizeof(values));
 d3d12_->set_resource(1,rain12_.soft[1].get());
 d3d12_->set_unordered(0,rain12_.soft[0].get());
 const HRESULT hr=d3d12_->dispatch(programs[7],(side+15)/16,(side+15)/16,1);
 d3d12_->clear_slots();
 if(hr!=S_OK){d3d12_->omit(FAILED(hr)?"rain shelter blur (dispatch failed)":"rain shelter blur (pipeline not ready)",false);return false;}
 native_shelter_blur_fill_=shelter_fill_;
 return true;
}catch(...){return false;}

// update_native_rain_rings on D3D12: rainRingsCS into a 512 x 512 RG16F
// storage texture (rain12_.rings, t71), keyed on the same game time and rain.
bool Renderer::update_native_rain_rings_d3d12(float seconds,float rain) noexcept try {
 if(native_rain_rings_failed_)return false;
 constexpr UINT side=512;
 auto& programs=ambient12_.precipitation;
 if(programs.empty())append_precipitation_shader_programs(programs);
 if(programs.size()<9)return false;
 if(!rain12_.rings){
  native_rain_rings_key_[0]=native_rain_rings_key_[1]=-1.f;
  HRESULT hr=d3d12_->storage_supported(DXGI_FORMAT_R16G16_FLOAT)?d3d12_->create_storage_texture(side,side,0,DXGI_FORMAT_R16G16_FLOAT,rain12_.rings):E_NOTIMPL;
  if(FAILED(hr)){
   char reason[96];std::snprintf(reason,sizeof(reason),"D3D12 texture hr=%08lx; wet ground shows no rain rings",static_cast<unsigned long>(hr));
   bridge_report_event("precip_rain_rings_unavailable",reason);
   native_rain_rings_failed_=true;rain12_.rings.reset();return false;
  }
 }
 if(seconds==native_rain_rings_key_[0]&&rain==native_rain_rings_key_[1])return true;
 const float time[4]={seconds,rain,float(side),0.f};
 d3d12_->clear_slots();
 d3d12_->set_constants(0,time,sizeof(time));
 d3d12_->set_unordered(0,rain12_.rings.get());
 const HRESULT hr=d3d12_->dispatch(programs[8],side/8,side/8,1);
 d3d12_->clear_slots();
 if(hr!=S_OK){d3d12_->omit(FAILED(hr)?"rain rings (dispatch failed)":"rain rings (pipeline not ready)",false);return false;}
 native_rain_rings_key_[0]=seconds;native_rain_rings_key_[1]=rain;
 return true;
}catch(...){return false;}

// A game texture atlas by name through the game's own resource mounts (mods
// included), uploaded once into an owned texture of the active backend.
const char* Renderer::load_game_atlas(const char* name, ComPtr<ID3D11ShaderResourceView>& srv, D3D12TextureRef& texture12) noexcept {
    try {
        ResourceStoreConfig mounts;
        if (!owned_resource_runtime_mounts(mounts)) return "resource mounts unavailable";
        ResourceStore store;ResourceFailure why;ResourceBlob blob;TextureCpuAsset asset;
        if (!store.open(mounts,why) || !store.resolve(name,blob,why) || !parse_texture_asset(blob,asset,why))
            return "atlas unreadable";
        NativeOwnedTextureFormat format{};
        switch (asset.format) {
        case MmpFormat::dxt1: format = NativeOwnedTextureFormat::dxt1; break;
        case MmpFormat::dxt3: format = NativeOwnedTextureFormat::dxt3; break;
        case MmpFormat::dxt5: format = NativeOwnedTextureFormat::dxt5; break;
        case MmpFormat::rgb565: format = NativeOwnedTextureFormat::rgb565; break;
        case MmpFormat::argb1555: format = NativeOwnedTextureFormat::argb1555; break;
        case MmpFormat::argb4444: format = NativeOwnedTextureFormat::argb4444; break;
        case MmpFormat::argb8888: format = NativeOwnedTextureFormat::argb8888; break;
        default: return "atlas format unsupported";
        }
        std::vector<NativeOwnedTextureMip> mips;mips.reserve(asset.mips.size());
        for (const auto& mip : asset.mips) mips.push_back({asset.source->data()+mip.byte_offset,mip.width,mip.height,mip.bytes});
        NativeOwnedTextureView view{};view.identity = asset.identity.container_incarnation;view.generation = 1;
        view.format = format;view.mips = mips.data();view.mip_count = UINT(mips.size());view.resource = &asset.identity;
        view.value_source = 1;view.opaque_mips = asset.opaque_mips;
        std::uint64_t backing = 0;
        if (d3d12_) {  // D3D12 backend: the D3D12 owned-texture ingress
            if (FAILED(get_native_owned_texture_d3d12(view,texture12,backing,nullptr))) { texture12.reset();return "atlas upload failed"; }
            return nullptr;
        }
        if (FAILED(get_native_owned_texture(view,srv,backing,nullptr))) { srv.Reset();return "atlas upload failed"; }
        return nullptr;
    } catch (...) { srv.Reset();texture12.reset();return "atlas allocation failed"; }
}

// The game's weather atlas (CWeather's "RainSnow", rainsnow.mmp), uploaded once.
const char* Renderer::load_precipitation_flakes() noexcept {
    if (const char* why = load_game_atlas("rainsnow.mmp",precipitation_flakes_srv_,ambient12_.flakes)) return why;
    if (d3d12_) return nullptr;
    D3D11_SAMPLER_DESC sampler{};sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&sampler,&precipitation_sampler_))) {
        precipitation_flakes_srv_.Reset();return "flake sampler unavailable";
    }
    return nullptr;
}
void Renderer::render_precipitation() {
    const auto& config = debug_overlay_.config();
    // The environment reader applies the render-only test and the strength
    // setting to every visual weather consumer (one frame later).
    const int override_strength = configured_integer(config,"precip_strength_override",0);
    bridge_set_weather_visual(configured_integer(config,"precip_test",0),
                              override_strength > 0 ? override_strength : configured_integer(config,"precip_strength",0));
    precipitation_status_frame_ = native_frame_index_;
    // Harness evidence: the snow cover and the weather level every ~10 s.
    if (const auto now = GetTickCount64(); now - precipitation_cover_log_ >= 10000) {
        precipitation_cover_log_ = now;
        char line[128];
        std::snprintf(line, sizeof(line), "snow_cover=%.3f level=%.3f kind=%d paused=%d", snow_cover_, precipitation_level_,
                      precipitation_kind_, precipitation_paused_ ? 1 : 0);
        bridge_report_event("precipitation_cover", line);
    }
    const auto status = [&](bool problem, const char* format, auto... values) {
        std::snprintf(precipitation_status_,sizeof(precipitation_status_),format,values...);
        precipitation_status_problem_ = problem;
        // Evidence for runs: the line's state (its text up to the first
        // digit, so counts and percentages do not spam) goes to the bridge
        // log whenever it changes.
        char state[96]{};std::size_t n = 0;
        for (const char* s = precipitation_status_; *s && n + 1 < sizeof(state) && !(*s >= '0' && *s <= '9'); ++s) state[n++] = *s;
        if (std::strcmp(state, precipitation_status_state_) != 0) {
            std::snprintf(precipitation_status_state_, sizeof(precipitation_status_state_), "%s", state);
            bridge_report_event("precipitation_status", precipitation_status_);
        }
    };
    // The stock draws stay suppressed whatever this pass does; a rejected
    // suppression is a problem line (the stock weather then draws as well).
    const char* stock = bridge_owned_weather_problem();
    const char* stock_sep = stock ? "; " : "";
    const char* stock_text = stock ? stock : "";
    const int level = configured_integer(config,"precipitation",3);
    if (level <= 0) {
        precipitation_rain_visible_ = precipitation_snow_visible_ = precipitation_sand_visible_ = 0.f;precipitation_level_ = 0.f;precipitation_kind_ = kind_none;
        return status(stock != nullptr,"Precipitation: off%s%s",stock_sep,stock_text);
    }
    if (precipitation_failed_) return status(true,"Precipitation: disabled after a resource/shader failure (see debug output)");
    if (ambient_terrain_frame_ != native_frame_index_ || !native_world_clip_valid_ || !world_effect_targets())
        return status(stock != nullptr,"Precipitation: no world frame%s%s",stock_sep,stock_text);
    AmbientWorldSource inputs;
    if (!bridge_ambient_world_source(inputs) || !inputs.terrain) return status(true,"Precipitation: no terrain source");
    WorldEnvironment environment = bridge_world_fog_snapshot().environment;
    if (!environment.valid) bridge_read_world_environment(environment);
    if (!environment.valid) return status(true,"Precipitation: game clock/weather unreadable");
    // The shared area/height grid gives the ground every drop lands on.
    if (!rebuild_ambient_particle_map(inputs,environment)) return status(true,"Precipitation: height grid unavailable");

    const int test = configured_integer(config,"precip_test",0);
    // precip_test 3 forces the sand storm whatever the game's weather does
    // (a fading game shower must not hold it off).
    Fall fall = test == 3 ? Fall{} : current_fall(environment);
    const double seconds = environment.world_time*kWeatherSecondsPerSegment;
    precipitation_front_arrival_ = sky_front_arrival();
    precipitation_local_cover_ = sky_local_cover();
    float strength = environment.precipitation_strength;
    // Sand storm (visual only, no game state): desert zones in dry weather,
    // from the shared wind's windy spells; precip_test 3 forces one anywhere,
    // its strength from the override (default heavy). Its haze is capped so
    // the view stays readable (gameplay sight does not change).
    if (fall.kind == kind_none && !environment.cave &&
        (test == 3 || (ambient_zone_ == ambient::zone_desert && !ambient_menu_))) {
        const float storm_level = test == 3 ? (override_strength > 0 ? fixed_strength(override_strength) : 1.f)
                                      : sand_storm_level(weather_wind().speed);
        if (storm_level > .05f) { fall = {kind_sand, 1.f}; strength = storm_level; }
    }
    // Build-up and fade (run 7: strong rain started at once). The game's own
    // fade lasts 1-2 seconds; the picture ramps on game seconds instead
    // (pause holds it): rain builds over 50 s and fades over 35 s, snow 70/45,
    // the sand storm 45/35, starting light (the curve is level^1.6). A change
    // of kind fades the old one out first. A load or time jump settles at
    // once. Gameplay sight switches with the game's state; the picture trails
    // it by the ramp.
    {
        const double dt = precipitation_time_ < 0 ? -1.0 : seconds - precipitation_time_;
        precipitation_time_ = seconds;
        // Only a load, a backward or long jump, or a map change settles; a
        // few skipped frames (no world composite) just step the ramp (run p2:
        // a 2 s gap snapped the sand storm to full).
        const bool settle = !(dt >= 0 && dt < 60) || precipitation_ramp_generation_ != ambient_map_generation_;
        precipitation_ramp_generation_ = ambient_map_generation_;
        const int want = fall.presence > 0.f ? fall.kind : kind_none;
        // The game clock stands still (paused): the build-up waits for it.
        precipitation_paused_ = dt == 0.0;precipitation_wanted_ = want;
        float target = want != kind_none && (want == precipitation_kind_ || precipitation_level_ <= 0.f) ? fall.presence : 0.f;
        if (want != kind_none && precipitation_level_ <= 0.f) precipitation_kind_ = want;
        // Settling (load, jump, map change) takes the weather the game has
        // restored at its full level: the client replays its fade-in after a
        // load, but a save in a downpour must come back as a downpour.
        if (settle) {
            int settled = want;
            if (settled == kind_none && !environment.cave && (environment.weather_next == kind_rain || environment.weather_next == kind_snow))
                settled = environment.weather_next;
            precipitation_kind_ = settled;precipitation_level_ = settled != kind_none ? 1.f : 0.f;
            precipitation_onset_ = settled == kind_rain || settled == kind_snow ? seconds - 1000.0 : -1.0;
        }
        else if ((precipitation_kind_ == kind_rain || precipitation_kind_ == kind_snow) && want == precipitation_kind_) {
            // Rain onset (user-approved, with the sky's storm front): a light,
            // slow drizzle (20% of the shower) starts at once so the game's
            // rain sound has rain to it; the front sweeps in over about 24 s
            // and the rain intensifies to full as it passes overhead, full
            // about 60 s after onset. The sky publishes the front's arrival at
            // the eye (precipitation_front_arrival_, 0..1); without it a 24 s timeline
            // stands in. Never dips: rain resuming mid-fade continues from
            // where it is.
            // Snow (user-approved): flurries at 15% at once, full as the front
            // passes, a gentler 90 s pace. The start is heavier under the
            // forming clouds (precipitation_local_cover_, the sky's scattered
            // cover overhead, 0..1; -1 not published).
            const bool flurries = precipitation_kind_ == kind_snow;
            const float pace = flurries ? 90.f : 60.f;
            if (precipitation_onset_ < 0) precipitation_onset_ = seconds - pace * precipitation_level_;
            const float since = float(seconds - precipitation_onset_);
            const auto ease = [](float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.f, 1.f); return t * t * (3.f - 2.f * t); };
            const float cover = precipitation_local_cover_ >= 0.f ? .7f + .7f * precipitation_local_cover_ : 1.f;
            const float drizzle = std::min((flurries ? .15f : .2f) * cover, .35f) * std::clamp(since / 3.f, 0.f, 1.f);
            const float front = precipitation_front_arrival_ >= 0.f ? precipitation_front_arrival_ : ease(0.f, flurries ? 36.f : 24.f, since);
            // The cap: full by 90 s (snow 120 s) after onset even if the
            // front never arrives over the eye, so it never sticks at drizzle.
            const float full = std::max(ease(flurries ? 12.f : 8.f, pace, since) * front,
                                        ease(pace, flurries ? 120.f : 90.f, since));
            precipitation_level_ = std::max(precipitation_level_, drizzle + (1.f - drizzle) * full);
        }
        else {
            const int k = precipitation_kind_;
            const float in = k == kind_snow ? 70.f : k == kind_sand ? 45.f : 50.f, out = k == kind_snow ? 45.f : 35.f;
            const float step = float(dt) / (target > precipitation_level_ ? in : out);
            precipitation_level_ += std::clamp(target - precipitation_level_, -step, step);
        }
        if (want != kind_rain && want != kind_snow) precipitation_onset_ = -1.0;
        if (precipitation_level_ <= 0.f) { precipitation_level_ = 0.f; if (want == kind_none) precipitation_kind_ = kind_none; }
        if (precipitation_kind_ != kind_none) {
            if (precipitation_kind_ != fall.kind) {
                // Fading out a kind the weather no longer has: keep its look.
                fall.kind = precipitation_kind_;
                if (fall.kind == kind_rain || fall.kind == kind_snow) strength = environment.precipitation_strength;
            }
            // Rain's onset curve is already shaped (drizzle, then the front);
            // the other kinds start light through the curve.
            fall.presence = fall.kind == kind_rain || fall.kind == kind_snow ? precipitation_level_ : std::pow(precipitation_level_, 1.6f);
        } else fall = {};
        // Strength changes cross-fade over about 20 game seconds from the
        // current value, never from zero (run p2).
        if (settle || precipitation_strength_seen_ < 0.f) precipitation_strength_seen_ = strength;
        else {
            const float smooth_step = float(dt) / 20.f;
            precipitation_strength_seen_ += std::clamp(strength - precipitation_strength_seen_, -smooth_step, smooth_step);
        }
        strength = precipitation_strength_seen_;
        precipitation_rain_visible_ = fall.kind == kind_rain ? fall.presence * (.35f + .65f * strength) : 0.f;
        precipitation_snow_visible_ = fall.kind == kind_snow ? fall.presence * (.35f + .65f * strength) : 0.f;
        precipitation_sand_visible_ = fall.kind == kind_sand ? fall.presence * (.35f + .65f * strength) : 0.f;
    }
    const char* kind_name = fall.kind == kind_rain ? "rain" : fall.kind == kind_snow ? "snow" : fall.kind == kind_sand ? "sand storm" : "none";
    if (fall.kind == kind_none || fall.presence <= 0.f) {
        precipitation_request_.active = false;bridge_set_rain_occlusion_request(precipitation_request_);
        if (precipitation_paused_ && precipitation_wanted_ != kind_none)
            return status(false,"Precipitation: paused: the %s waits for game time%s%s",
                          precipitation_wanted_ == kind_rain ? "rain" : precipitation_wanted_ == kind_snow ? "snow" : "sand storm",
                          stock_sep,stock_text);
        if (snow_cover_ > 0.f)
            return status(stock != nullptr,"Precipitation: dry%s, snow cover %.0f%% (melting)%s%s",environment.cave ? " (cave)" : "",
                          snow_cover_*100.f,stock_sep,stock_text);
        return status(stock != nullptr,"Precipitation: dry%s%s%s",environment.cave ? " (cave)" : "",stock_sep,stock_text);
    }

    const auto fail = [&](const char* reason) {
        precipitation_failed_ = true;OutputDebugStringA(reason);
        status(true,"Precipitation: disabled after a resource/shader failure (see debug output)");
    };
    if (!d3d12_ && !precipitation_vs_) {
        ComPtr<ID3DBlob> vs, ps, sand_ps, veil_vs, veil_ps, splash_vs, splash_ps, errors;
        std::vector<ShaderProgram> programs;
        append_precipitation_shader_programs(programs);
        if (FAILED(compile_shader(programs[0], &vs, &errors)) || FAILED(compile_shader(programs[1], &ps, &errors)) ||
            FAILED(compile_shader(programs[2], &veil_vs, &errors)) || FAILED(compile_shader(programs[3], &veil_ps, &errors)) ||
            FAILED(compile_shader(programs[4], &splash_vs, &errors)) || FAILED(compile_shader(programs[5], &splash_ps, &errors)) || FAILED(compile_shader(programs[9], &sand_ps, &errors))) {
            if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
            return fail("EID3D11 precipitation: shaders unavailable; rain and snow omitted.\n");
        }
        D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(PrecipitationConstants);b.Usage=D3D11_USAGE_DYNAMIC;
        b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        D3D11_BLEND_DESC blend{};auto& t=blend.RenderTarget[0];t.BlendEnable=TRUE;
        t.SrcBlend=t.SrcBlendAlpha=D3D11_BLEND_ONE;t.DestBlend=t.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        t.BlendOp=t.BlendOpAlpha=D3D11_BLEND_OP_ADD;t.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        if (FAILED(create_shader(device_.Get(),vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&precipitation_vs_)) ||
            FAILED(create_shader(device_.Get(),ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&precipitation_ps_)) ||
            FAILED(create_shader(device_.Get(),sand_ps->GetBufferPointer(),sand_ps->GetBufferSize(),nullptr,&precipitation_sand_ps_)) ||
            FAILED(create_shader(device_.Get(),veil_vs->GetBufferPointer(),veil_vs->GetBufferSize(),nullptr,&precipitation_veil_vs_)) ||
            FAILED(create_shader(device_.Get(),veil_ps->GetBufferPointer(),veil_ps->GetBufferSize(),nullptr,&precipitation_veil_ps_)) ||
            FAILED(create_shader(device_.Get(),splash_vs->GetBufferPointer(),splash_vs->GetBufferSize(),nullptr,&precipitation_splash_vs_)) ||
            FAILED(create_shader(device_.Get(),splash_ps->GetBufferPointer(),splash_ps->GetBufferSize(),nullptr,&precipitation_splash_ps_)) ||
            FAILED(device_->CreateBuffer(&b,nullptr,&precipitation_constants_)) ||
            FAILED(device_->CreateBlendState(&blend,&precipitation_blend_)) ||
            FAILED(device_->CreateRasterizerState(&raster,&precipitation_raster_))) {
            precipitation_vs_.Reset();
            return fail("EID3D11 precipitation: pipeline objects unavailable; rain and snow omitted.\n");
        }
    }

    // Snow crystals come from the game's own weather atlas; without it the
    // snow is omitted (named on the F10 line), never drawn as substitutes.
    if (fall.kind == kind_snow && !(d3d12_ ? bool(ambient12_.flakes) : bool(precipitation_flakes_srv_)) && !precipitation_flakes_failed_) {
        const char* why = load_precipitation_flakes();
        if (why) { precipitation_flakes_failed_ = true; bridge_report_event("precipitation_flakes_unavailable", why); }
    }
    if (fall.kind == kind_snow && !(d3d12_ ? bool(ambient12_.flakes) : bool(precipitation_flakes_srv_)))
        return status(true,"Precipitation: snow omitted: the game's weather atlas (rainsnow.mmp) is unavailable");
    // Camera, focus and the window of world tiles covering the view.
    ambient::AmbientView view;
    const ambient::Quality window_quality{kTilePeriod*.5f,1.f,1.f};
    if (const char* failure = ambient::ambient_view(native_world_clip_,target_width(),ambient_height_,ambient_tiles_x_,
            ambient_tiles_y_,window_quality,seconds,1.f,view))
        return status(true,"Precipitation: %s",failure);
    const bool snow = fall.kind == kind_snow, sand = fall.kind == kind_sand, rain = fall.kind == kind_rain;
    const precipitation::Quality quality = precipitation::quality_level(level);
    const float amount = strength*fall.presence;
    // Light snow keeps enough flakes to read at gameplay distance.
    // Sand grains: the dense fine grains only where the eye resolves them.
    // The main draw carries 30% of the preset's density over the whole sand
    // window (with the bands and billows); a second draw adds more grains
    // (50%) over a small box near the camera, fading out by 0.8x the focus. The
    // 3dc3ba0a probe: 20 -> 8 grains per square metre over the window saved
    // 0.47 of 0.83 ms; the grain shape, the 9 m taps and the bands cost little.
    const float sand_density = quality.sand_density*.3f;
    const float density = sand ? sand_density*amount : snow ? quality.snow_density*(.35f+.65f*strength)*fall.presence
                                                               : quality.rain_density*amount;
    // The drops fall through a fixed world period; only the share inside the
    // view's column is visible, so the count keeps the visible density.
    constexpr float kFallPeriod = 80.f;
    const auto& eye_view = view.constants.eye;
    const float column_focus_z = view.focus_found ? float(view.focus[2]) : eye_view[2]-20.f;
    const float column_top = std::max(eye_view[2],column_focus_z+6.f)+4.f, column_bottom = column_focus_z-12.f;
    const float column = std::clamp(column_top-column_bottom,4.f,kFallPeriod);
    const unsigned per_tile = static_cast<unsigned>(std::ceil(density*kTilePeriod*kTilePeriod*(sand ? 1.f : kFallPeriod/column)));
    // The falling field drifts with the wind as a whole: the CPU integrates a
    // smoothed wind on game seconds, so gusts never jerk the drops (run 7).
    const WeatherWindGpu& wind_now = weather_wind();
    const float push_now = wind_now.force*wind_now.speed*wind_now.pulse;
    const float drift_now = sand ? 4.f+10.f*strength : snow ? .3f+3.5f*push_now*(.7f+.5f*strength)
                                                       : 1.f+4.f*push_now*(.7f+.5f*strength);
    {
        const double dt = precipitation_drift_time_ < 0 ? -1.0 : seconds - precipitation_drift_time_;
        precipitation_drift_time_ = seconds;
        const float target[2] = {wind_now.direction[0]*drift_now, wind_now.direction[1]*drift_now};
        if (!(dt >= 0 && dt < 2) || precipitation_drift_generation_ != ambient_map_generation_) {
            precipitation_wind_[0] = target[0];precipitation_wind_[1] = target[1];
            if (precipitation_drift_generation_ != ambient_map_generation_)
                precipitation_drift_[0] = precipitation_drift_[1] = precipitation_flow_[0] = precipitation_flow_[1] = 0;
            precipitation_drift_generation_ = ambient_map_generation_;
        } else {
            const float k = 1.f-std::exp(-float(dt)/1.5f);
            // Blowing sand on the ground runs on the shared gust pulse
            // (weather_wind.h, ~5 s swells), its contrast raised to a storm's
            // (the map's calm gust of .1 barely moved it): integrated here, so
            // a gust quickens the sand without a jump.
            const float sand_gust = sand ? std::max(wind_now.gust,.3f+.4f*strength) : 0.f;
            const float sand_pulse = sand ? std::max(.35f,1.f+(wind_now.pulse-1.f)*sand_gust/std::max(wind_now.gust,.05f)) : 1.f;
            for (int a = 0; a < 2; ++a) {
                precipitation_wind_[a] += (target[a]-precipitation_wind_[a])*k;
                precipitation_drift_[a] += double(precipitation_wind_[a])*dt;
                precipitation_flow_[a] = std::fmod(precipitation_flow_[a]+double(precipitation_wind_[a]*sand_pulse)*dt,1048576.0);
            }
            precipitation_gust_time_ = std::fmod(precipitation_gust_time_+double(sand_pulse)*dt,3600.0);
        }
    }
    // The lattice the drops hash on moves with the drift; one extra column
    // and row keep the shifted lattice covering the view window.
    const double lattice_x = std::floor((double(view.constants.window[0])-precipitation_drift_[0])/kTilePeriod)*kTilePeriod;
    const double lattice_y = std::floor((double(view.constants.window[1])-precipitation_drift_[1])/kTilePeriod)*kTilePeriod;
    const unsigned lattice_columns = view.columns+1, lattice_rows = view.rows+1;
    // Stable distances (c71: parts of the storm popped while panning): the
    // focus distance that sets the thinning, band and reach ranges is eased
    // over about 0.6 s of real time, so a focus ray jumping across uneven
    // ground no longer moves every range at once.
    {
        const auto& e = view.constants.eye;
        const float raw = view.focus_found ? float(std::sqrt((view.focus[0]-e[0])*(view.focus[0]-e[0])+(view.focus[1]-e[1])*(view.focus[1]-e[1])+
                                                            (view.focus[2]-e[2])*(view.focus[2]-e[2]))) : 30.f;
        const auto now = GetTickCount64();
        const double real_dt = precipitation_focus_tick_ ? double(now - precipitation_focus_tick_) / 1000.0 : -1.0;
        precipitation_focus_tick_ = now;
        if (precipitation_focus_smooth_ <= 0.f || !(real_dt >= 0 && real_dt < 1)) precipitation_focus_smooth_ = raw;
        else precipitation_focus_smooth_ += (raw - precipitation_focus_smooth_) * float(1.0 - std::exp(-real_dt / .6));
    }
    // Sand reaches toward the horizon: the window extends ahead of the view
    // to max(2.2 x focus, 90 m) (c71: at a near-horizontal camera the storm
    // ended mid-screen, where the window's reach of focus + 5 m ran out);
    // far tiles are mostly culled early in the vertex stage and the veil and
    // ground streaks carry the depth.
    double sand_reach = view.reach, window_x = view.constants.window[0], window_y = view.constants.window[1];
    unsigned window_columns = view.columns, window_rows = view.rows;
    if (sand) {
        const auto& e = view.constants.eye;
        sand_reach = std::max(double(precipitation_focus_smooth_) * 2.2, 90.0);
        double fx = view.focus_found ? view.focus[0]-e[0] : view.constants.up[0], fy = view.focus_found ? view.focus[1]-e[1] : view.constants.up[1];
        double fl = std::sqrt(fx*fx+fy*fy);
        if (fl < 1e-6) { fx = 1; fy = 0; fl = 1; }
        fx /= fl; fy /= fl;
        double lo[2]{view.window_min[0],view.window_min[1]}, hi[2]{view.window_max[0],view.window_max[1]};
        const double sx = -fy, sy = fx;
        const double points[5][2] = {{e[0]+fx*sand_reach+sx*sand_reach*.9,e[1]+fy*sand_reach+sy*sand_reach*.9},
                                     {e[0]+fx*sand_reach-sx*sand_reach*.9,e[1]+fy*sand_reach-sy*sand_reach*.9},
                                     {e[0]+sx*sand_reach*.5,e[1]+sy*sand_reach*.5},{e[0]-sx*sand_reach*.5,e[1]-sy*sand_reach*.5},
                                     {e[0]+fx*sand_reach,e[1]+fy*sand_reach}};
        for (const auto& pt : points) for (int a = 0; a < 2; ++a) { lo[a] = std::min(lo[a],pt[a]); hi[a] = std::max(hi[a],pt[a]); }
        const double x0 = std::floor((lo[0]-1)/kTilePeriod), y0 = std::floor((lo[1]-1)/kTilePeriod);
        window_columns = unsigned(std::floor((hi[0]+1)/kTilePeriod)-x0)+1u;
        window_rows = unsigned(std::floor((hi[1]+1)/kTilePeriod)-y0)+1u;
        window_x = x0*kTilePeriod;window_y = y0*kTilePeriod;
    }
    const unsigned tiles = sand ? window_columns*window_rows : lattice_columns*lattice_rows;
    if (!per_tile || !tiles) return status(stock != nullptr,"Precipitation: %s too faint to draw",kind_name);
    const UINT instances = per_tile*tiles;

    const auto& eye = view.constants.eye;
    const float focus_z = view.focus_found ? float(view.focus[2]) : eye[2]-20.f;
    // Shelter map request: a 96 m square around the focus, from 50 m above
    // its ground (8 m steps) down through a 120 m slab. The bridge bakes it
    // when this request or the static objects change. The square moves in
    // 12 m steps, a whole number of texels at every map size (128 / 256 /
    // 64), so a re-bake lands every surface on the same texel and nothing
    // pops (16 m was 170.67 texels at 1024: c71 popping as the camera moved).
    {
        RainOcclusionRequest request;request.active = true;
        const double snap = 12.0;
        const double cx = std::floor((view.focus_found ? view.focus[0] : eye[0])/snap+.5)*snap;
        const double cy = std::floor((view.focus_found ? view.focus[1] : eye[1])/snap+.5)*snap;
        request.center[0] = float(cx);request.center[1] = float(cy);request.half = 48.f;
        request.top = float(std::floor(focus_z/8.f+.5f)*8.f)+50.f;request.span = 120.f;
        request.side = level <= 1 ? 512u : level >= 4 ? 2048u : 1024u;
        request.casters = object_caster_key();  // neighbour decorations and object-space statics shelter too
        const auto& last = precipitation_request_;
        const bool same = last.active && last.center[0] == request.center[0] && last.center[1] == request.center[1] &&
                          last.top == request.top && last.side == request.side && last.casters == request.casters;
        request.serial = same ? last.serial : last.serial+1;
        precipitation_request_ = request;
        bridge_set_rain_occlusion_request(request);
    }
    const float focus_distance = precipitation_focus_smooth_;  // eased: no range jumps while panning
    // Columns run from just above the eye down past the focus ground; every
    // drop disappears at the ground under it.
    const float top = std::max(eye[2],focus_z+6.f)+4.f;
    const float bottom = focus_z-12.f;
    const WeatherWindGpu& wind = wind_now;
    const float push = push_now;
    // Snow follows the game's wind (weather_wind, drift_now above): heavy snow
    // in calm air falls mostly straight down; it blows sideways only in strong
    // wind. Sand races along the wind just above the ground.
    // Constant fall speeds (a strength-dependent speed rescaled every drop's
    // phase as the shower breathed: run 7's oscillating rain).
    const float fall_speed = sand ? 1.f : snow ? 1.4f : 8.5f;

    PrecipitationConstants c{};
    std::memcpy(c.clip,view.constants.clip,sizeof(c.clip));
    jitter_clip(c.clip);
    std::memcpy(c.eye,view.constants.eye,sizeof(c.eye));
    c.eye[3] = view.constants.right[3];  // pixel scale
    if (sand) {
        c.window[0] = float(window_x);c.window[1] = float(window_y);c.window[2] = float(window_columns);
    } else {
        c.window[0] = float(lattice_x);c.window[1] = float(lattice_y);c.window[2] = float(lattice_columns);
        c.drift[0] = float(precipitation_drift_[0]);c.drift[1] = float(precipitation_drift_[1]);
    }
    c.window[3] = kTilePeriod;
    (void)top;
    c.fall[0] = float(per_tile);c.fall[1] = fall_speed;c.fall[2] = bottom;c.fall[3] = kFallPeriod;
    c.motion[0] = precipitation_wind_[0];c.motion[1] = precipitation_wind_[1];
    c.motion[2] = .07f;  // rain streak exposure: about 0.6 m of fall
    c.motion[3] = float(std::fmod(seconds,3600.0));
    // Heavy snow has bigger flakes.
    // Crystals cover less of their quad than the old discs: heavy snow's
    // flakes are larger to keep its density (run 6).
    c.shape[0] = sand ? .007f : snow ? .05f*(.85f+.85f*strength) : .022f;c.shape[1] = sand ? 1.f : snow ? 2.2f : 1.4f;c.shape[2] = float(fall.kind);
    c.shape[3] = sand ? .45f+.35f*strength : snow ? .9f : .38f+.22f*strength;
    // Lit by the world light: ambient plus a share of the sun or moon light,
    // lifted toward the sky's brightness (drops refract the sky).
    float ambient_light[3]{.2f,.2f,.2f}, sun[3]{.6f,.6f,.6f}, sun_direction[3]{0,0,1};
    if (native_world_lighting_valid_) {
        const auto& d = native_world_lighting_.sunlight_direction;
        const float l = std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        for (unsigned k = 0; k < 3; ++k) {
            ambient_light[k] = native_world_lighting_.ambient[k];sun[k] = native_world_lighting_.sunlight_diffuse[k];
            if (l > 1e-6f) sun_direction[k] = d[k]/l;
        }
    }
    const float tint[3] = {sand ? .72f : snow ? .95f : .8f, sand ? .6f : snow ? .97f : .85f, sand ? .44f : snow ? 1.f : .92f};
    float light[3];
    // Rain and snow take the scene's brightness but stay neutral grey-blue:
    // a tinted world light (swamp dusk, run 7 capture) turned them pink.
    const float luma_ambient = .2126f*ambient_light[0]+.7152f*ambient_light[1]+.0722f*ambient_light[2];
    const float luma_sun = .2126f*sun[0]+.7152f*sun[1]+.0722f*sun[2];
    if (!sand) for (unsigned k = 0; k < 3; ++k) {
        ambient_light[k] = luma_ambient+(ambient_light[k]-luma_ambient)*.2f;
        sun[k] = luma_sun+(sun[k]-luma_sun)*.2f;
    }
    // Sand dust is lit like the ground it blew from: ambient plus part of
    // the sun, no lift and no floor, so it is dark at night and sand-coloured
    // by day (ce6849f4: the lifted light made bluish-white surf at dusk).
    for (unsigned k = 0; k < 3; ++k) {
        light[k] = sand ? std::min(ambient_light[k]+sun[k]*.6f,1.2f)
                        : std::min(ambient_light[k]*1.6f+sun[k]*(snow ? .7f : .45f)+.08f,1.4f);
        c.color[k] = tint[k]*light[k];
        c.glint[k] = sun[k]*1.6f;
        c.sun_direction[k] = sun_direction[k];
    }
    c.color[3] = strength;
    // Snow as a material (pcSnowLit): the world ambient and sun as the
    // surfaces take them (no lift, no floor: dark at night like the snow on
    // the ground), and the scene's lamps.
    const bool snow_lamps = snow && !native_point_lights_.empty();
    for (unsigned k = 0; k < 3; ++k) { c.snow_light[k] = tint[k]*ambient_light[k]; c.snow_sun[k] = tint[k]*sun[k]; }
    c.snow_light[3] = snow_lamps ? 1.f : 0.f;c.snow_sun[3] = 1.f;
    c.glint[3] = snow ? .25f+.35f*strength : 0.f;
    c.sun_direction[3] = float(sand ? sand_reach : view.reach);
    // Beyond the focus the drops thin to a third and the veil carries the rain.
    // Sand: the dense fine grains belong near the camera (from 0.85 x the focus
    // they thin to 6%; the veil and ground streaks carry the depth).
    c.thin[0] = focus_distance*(sand ? .85f : 1.05f);c.thin[1] = float(sand ? sand_reach : view.reach);c.thin[2] = sand ? .06f : quality.sheets ? .35f : .6f;
    // Veil: opacity at distance with strength; rain sheets fall fast and
    // narrow, snow drifts slowly in broad blotches, sand streams sideways in
    // long ochre bands (capped at half opacity: hazy but readable).
    // Sand: the storm continues into the distance as muted dusty air that
    // thickens with distance and strength (run p2: no depth beyond the near
    // grains).
    // Sand: a dusty-air layer to the horizon that darkens and tints the far
    // land at heavy strength (c71: no depth); the near haze is cut inside
    // wind shadows (the veil's own lee test).
    const float veil_max = (sand ? .15f+.5f*strength : snow ? .1f+.3f*strength*strength : .12f+.28f*strength)*fall.presence;
    c.veil[0] = veil_max;c.veil[1] = sand ? 80.f-50.f*strength : snow ? 45.f-20.f*strength : 70.f-30.f*strength;
    c.veil[2] = quality.sheets ? (sand ? .6f : snow ? .45f : .7f) : 0.f;c.veil[3] = sand ? 0.f : snow ? .12f : .9f;
    // Dusty air is lit mostly by the sky, not the sun: a muted brown-grey
    // that never outshines the scene (run 6: the sun-lit ochre veil read as a
    // glowing yellow haze over the far land and sky, and fed bloom).
    const float veil_tint[3] = {sand ? .66f : snow ? .92f : .72f, sand ? .58f : snow ? .94f : .76f, sand ? .48f : snow ? .98f : .82f};
    // Rain and snow air is overcast: lit mostly by the (already desaturated)
    // ambient, a little sun, capped well below white. It covers the sky
    // (depth 1) at full veil strength, and the lifted colour (ambient x 1.4 +
    // sun x 0.35 + 0.05, up to 1.2) washed a sunset sky to white in heavy
    // snow (9d856c0f user note, ingos frame_000812): the veil now darkens a
    // bright sky and dims the sun glow by its own opacity.
    for (unsigned k = 0; k < 3; ++k)
        c.veil_color[k] = veil_tint[k]*(sand ? std::min(ambient_light[k]+sun[k]*.2f,.68f)
                                             : std::min(ambient_light[k]*.85f+sun[k]*.08f,.55f));
    // The sheets follow the camera heading so a turn slides them sideways.
    const auto& right = view.constants.right;
    c.veil_color[3] = std::atan2(right[1],right[0])*1.6f;
    c.screen[0] = 1.f/float(target_width());c.screen[1] = 1.f/float(target_height());
    c.screen[2] = float(target_width())/float(target_height());c.screen[3] = sand ? .22f : snow ? 1.4f : 7.f;
    // Blowing sand on the ground (pcSandGround, pcSandHaze, pcSandGust): the
    // integrated gusty flow, the map's wind as the streaks' fixed frame and
    // the current wind for the shared gust fronts.
    if (sand) {
        c.flow[0] = float(precipitation_flow_[0]);c.flow[1] = float(precipitation_flow_[1]);
        c.flow[2] = float(precipitation_gust_time_);c.flow[3] = std::max(wind.gust,.3f+.4f*strength);
        c.frame[0] = weather_wind_.base[0];c.frame[1] = weather_wind_.base[1];
        c.frame[2] = wind.direction[0];c.frame[3] = wind.direction[1];
    }
    // Sand sheets: a quarter of the sand is 2-6 m dust bands near the ground.
    // Dust bands only around the focus, where they read (their far overdraw
    // set run 5's 1.19 ms); none on Low.
    // Bands and ground sheets per square metre at full strength by quality
    // (none on Low); a share of the sand population, so they scale with the
    // storm. Billows: a share of grains near walls and steep ground.
    // The budget follows the camera's pitch: a grazing or side view keeps the
    // full upright density the user liked (about 2 per square metre at High),
    // a steep top-down view sees upright bands edge-on, so it moves the budget
    // to flat ground sheets (each covers far more ground) and few bands.
    float steep = .7f;
    if (view.focus_found) {
        const double fx = view.focus[0]-view.constants.eye[0], fy = view.focus[1]-view.constants.eye[1],
                     fz = view.focus[2]-view.constants.eye[2];
        const double fl = std::sqrt(fx*fx+fy*fy+fz*fz);
        if (fl > 1e-6) steep = float(-fz/fl);
    }
    const float top_view = std::clamp((steep-.55f)/.3f,0.f,1.f);
    const float blend = top_view*top_view*(3.f-2.f*top_view);
    // Run p2: too strong from the side, too faint from above. Candidates are
    // a constant share (the most either view uses); the angle picks how many
    // of each kind show (a soft per-band threshold in the vertex stage).
    // Cost (sand-check2 on ce6849f4: 3.1 ms at High, 4K): the bands' overdraw
    // dominated, so there are about 2.8x fewer bands, each about 2x more
    // opaque (the same layered coverage: 1-(1-a)^n), and the hidden ones no
    // longer rasterize.
    const float band_side = level >= 4 ? .8f : level == 3 ? .5f : level == 2 ? .25f : 0.f;
    const float band_top = level >= 4 ? .12f : level == 3 ? .09f : level == 2 ? .05f : 0.f;
    // No flat ground sheets: their panning sand texture read as unnatural
    // (#253, user 2026-11-15: remove it).
    const float ground_side = 0.f;
    const float ground_top = 0.f;
    const float band_density = band_side+(band_top-band_side)*blend;
    const float ground_density = ground_side+(ground_top-ground_side)*blend;
    c.sheet[1] = sand && sand_density > 0.f ? std::min(1.f,(band_side+ground_top)/sand_density) : 0.f;
    c.sheet[2] = 1.5f+1.5f*strength;c.sheet[3] = std::min((.1f+.18f*strength)*2.2f,.7f);
    c.sand[0] = band_side+ground_top > 0.f ? ground_top/(band_side+ground_top) : 0.f;
    c.band[0] = band_side > 0.f ? band_density/band_side : 0.f;c.band[1] = ground_top > 0.f ? ground_density/ground_top : 0.f;
    c.band[2] = focus_distance*.9f;c.band[3] = focus_distance*1.4f;
    // Ground streaks: world-anchored blowing-sand streaks on the ground in the
    // veil (read from above and to the horizon; a little stronger in a steep
    // view, ce6849f4: full-strength streaks were white smears), and the
    // canyon test that protects narrow streets.
    // Removed (#253, user 2026-11-15): the streaks' panning texture read as
    // unnatural on the ground; grains, bands and billows remain.
    c.sand2[0] = 0.f;
    c.sand2[1] = sand ? 1.f : 0.f;
    // Billows: a share of the main draw's grains (the 9b32ff4a count: 2 per
    // square metre at High).
    c.sand[1] = sand ? std::min(1.f,(level >= 4 ? .14f : level == 3 ? .1f : level == 2 ? .06f : 0.f)/.3f) : 0.f;
    c.sand[2] = 1.f+2.f*strength;
    // Wind shadow march steps (1.5 to 20 m upwind at Ultra).
    c.sand[3] = sand ? float(std::clamp(level+1,2,5)) : 0.f;
    // Ground splashes (rain, Medium and up): slots per tile from the impact
    // rate and life; each crown stands about 9 cm tall.
    constexpr float kSplashLife = .32f;
    const unsigned splash_slots = !rain ? 0u : static_cast<unsigned>(std::ceil(quality.splash_rate*amount*kSplashLife*kTilePeriod*kTilePeriod));
    // Blowing snow: up to a third of the flakes skim the ground in a blizzard.
    // Sand: every grain skims the ground, most of them low.
    // Blowing snow only where the game's wind is strong.
    // blowing snow (0 none, 1 sparse, 2 full, from the precipitation level): the blown share at full blizzard,
    // 30% of the flakes at level 2 (a third before, with large near-blurred
    // blobs: too prominent, 1102 user report).
    // Blowing snow follows the precipitation level: none at Low, sparse at Medium/High, full at Ultra.
    const int precipitation_level = configured_integer(config,"precipitation",3);
    const int snow_drift = precipitation_level <= 1 ? 0 : precipitation_level >= 4 ? 2 : 1;
    c.snow[0] = sand ? 1.f : snow ? .3f*.5f*float(snow_drift)*strength*strength*std::clamp((push-.4f)/.4f,0.f,1.f) : 0.f;c.snow[1] = sand ? 1.f : 1.8f;
    c.snow[2] = sand ? 1.f+5.f*strength : 1.4f;c.snow[3] = sand ? 1.6f : 2.5f;
    c.splash[0] = float(splash_slots);c.splash[1] = kSplashLife;c.splash[2] = .07f+.04f*strength;c.splash[3] = .55f;
    // The baked shelter map (its own square, which may trail the request by a frame).
    const bool shelter = native_rain_valid_ && (d3d12_ ? bool(rain12_.shelter) : bool(native_rain_srv_));
    if (shelter) {
        const auto& m = native_rain_slice_.world_to_light;
        c.shelter0[0] = m[0];c.shelter0[1] = m[1];c.shelter0[2] = m[3];c.shelter0[3] = 1.f;
        c.shelter1[0] = m[4];c.shelter1[1] = m[5];c.shelter1[2] = m[7];
        c.shelter2[0] = std::abs(m[10]) > 1e-9f ? -1.f/m[10] : 0.f;c.shelter2[1] = m[11]*c.shelter2[0];
    }
    const auto& fog = ambient_terrain_state_;
    c.fog_range[0] = fog.fog_min[0];c.fog_range[1] = fog.fog_min[1];c.fog_range[2] = fog.fog_max[0];c.fog_range[3] = fog.fog_max[1];
    c.fog_control[0] = fog.fog_distance;c.fog_control[1] = fog.fog_enabled ? 1.f : 0.f;
    c.fog_color[0] = ((ambient_fog_color_>>16)&255)/255.f;c.fog_color[1] = ((ambient_fog_color_>>8)&255)/255.f;
    c.fog_color[2] = (ambient_fog_color_&255)/255.f;
    neighbour_fog_constants(neighbour_fog_, c.fog_seam, c.fog_curve, c.fog_span_lo, c.fog_span_hi);
    c.depth[0] = view.constants.depth[0];c.depth[1] = view.constants.depth[1];c.depth[2] = .25f;

    // Precipitation takes the shared world atmosphere; without it it is omitted.
    if (FAILED(bind_world_fog())) return status(true,"Precipitation: world atmosphere unavailable");
    if (d3d12_) {
        // The same draws on D3D12, from the same programs: height t0, base
        // ground t1, shelter t3 (both stages), world depth t2, flakes t4 (s0);
        // the vertex stage's atmosphere reads the same b10 and sky probe.
        auto& programs = ambient12_.precipitation;
        if (programs.empty()) append_precipitation_shader_programs(programs);
        if (programs.size() < 10 || !ambient12_.height) return status(true,"Precipitation: D3D12 programs or height grid missing (omitted)");
        const D3D12NativeState state = world_effect_state_d3d12(false);
        bind_world_effect_d3d12(false);
        d3d12_->set_constants(0,&c,sizeof(c));
        d3d12_->set_resource(0,ambient12_.height.get());
        d3d12_->set_resource(1,ambient12_.ground ? ambient12_.ground.get() : ambient12_.height.get());
        d3d12_->set_resource(3,shelter ? rain12_.shelter.get() : nullptr);
        d3d12_->set_resource(4,ambient12_.flakes.get());
        // Snow as a material (pcSnowLit): the scene's lamp records (b5, t17,
        // t18); the cloud shadow (t110) is bound by bind_world_effect_d3d12.
        d3d12_->set_constants(5,native12_.point_light_constants.data(),sizeof(native12_.point_light_constants));
        d3d12_->set_resource(17,c.snow_light[3] > 0.f ? native12_.point_light_grid.get() : nullptr);
        d3d12_->set_resource(18,c.snow_light[3] > 0.f ? native12_.point_lights.get() : nullptr);
        D3D11_SAMPLER_DESC sampler{};sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD = D3D11_FLOAT32_MAX;
        d3d12_->set_sampler(0,sampler);
        const char* problem = nullptr;
        const auto draw = [&](const char* pass, const ShaderProgram& vs, const ShaderProgram& ps, UINT vertices, UINT count, bool strip) {
            if (problem || !count) return;
            transition_profile_pass(pass);
            const HRESULT hr = d3d12_->draw_instanced(vs,ps,state,nullptr,vertices,count,strip);
            if (FAILED(hr)) problem = "draw failed";
            else if (hr == S_FALSE) problem = "pipeline not ready";
            else if (hr != S_OK) problem = "descriptor ring full";
            else { profiler_.add_draw(1,1);profiler_.add_batch(1); }
        };
        draw("precipitation_veil",programs[2],programs[3],3,1,false);
        draw("precipitation",programs[0],programs[sand ? 9 : 1],4,instances,true);
        UINT near_instances = 0;
        if (sand) {  // the near sand grains, as the D3D11 draw below
            const unsigned near_per_tile = static_cast<unsigned>(std::ceil(quality.sand_density*.5f*amount*kTilePeriod*kTilePeriod));
            const float radius = std::clamp(focus_distance*.8f,12.f,32.f);
            const double nx0 = std::floor((eye[0]-radius)/kTilePeriod), ny0 = std::floor((eye[1]-radius)/kTilePeriod);
            const unsigned near_columns = unsigned(std::floor((eye[0]+radius)/kTilePeriod)-nx0)+1u;
            const unsigned near_rows = unsigned(std::floor((eye[1]+radius)/kTilePeriod)-ny0)+1u;
            near_instances = near_per_tile*near_columns*near_rows;
            if (near_instances) {
                c.window[0] = float(nx0*kTilePeriod);c.window[1] = float(ny0*kTilePeriod);c.window[2] = float(near_columns);
                c.fall[0] = float(near_per_tile);
                c.thin[0] = radius*(.5f/.8f);c.thin[1] = radius;c.thin[2] = 0.f;
                c.sheet[1] = 0.f;c.sand[1] = 0.f;c.sand2[2] = float(per_tile);
                d3d12_->set_constants(0,&c,sizeof(c));
                draw("precipitation",programs[0],programs[9],4,near_instances,true);
            }
        }
        draw("precipitation_splash",programs[4],programs[5],4,splash_slots*tiles,true);
        d3d12_->clear_slots();bind_world_targets_d3d12();
        if (problem) {
            char name[96];std::snprintf(name,sizeof(name),"rain and snow (%s)",problem);
            d3d12_->omit(name,false);
            return status(true,"Precipitation: %s %s on D3D12: omitted this frame",kind_name,problem);
        }
        d3d12_->count("precipitation draws");
        return status(stock != nullptr,"Precipitation: %s %.0f%% strength %.2f, %u drops, %u splashes (%u tiles), shelter %s (%llu bakes), snow cover %.0f%%%s%s%s%s",kind_name,
               fall.presence*100.f,strength,unsigned(instances+near_instances),splash_slots*tiles,tiles,shelter ? "baked" : native_shadows_enabled_ ? "pending" : "off (sun shadows off)",
               native_rain_bakes_,snow_cover_*100.f,test ? " [precip_test]" : "",precipitation_paused_ ? " [paused: game time stopped]" : "",
               stock_sep,stock_text);
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(precipitation_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))
        return status(true,"Precipitation: constant upload failed");
    std::memcpy(mapped.pData,&c,sizeof(c));context_->Unmap(precipitation_constants_.Get(),0);

    // Runs inside composite_world. It binds every state it reads and restores
    // nothing; afterwards it unbinds its views and the vertex-stage fog
    // mirror (every following pass binds its own).
    bind_plan_targets(plan_pass("precipitation",rtv_.Get(),nullptr,plan_read(PlanTarget::world_depth))); // depth is read as a texture
    D3D11_VIEWPORT viewport = target_viewport();context_->RSSetViewports(1,&viewport);
    context_->RSSetState(precipitation_raster_.Get());context_->OMSetDepthStencilState(nullptr,0);
    const float factor[4]{1,1,1,1};context_->OMSetBlendState(precipitation_blend_.Get(),factor,~0u);
    context_->IASetInputLayout(nullptr);context_->IASetVertexBuffers(0,0,nullptr,nullptr,nullptr);
    context_->GSSetShader(nullptr,nullptr,0);
    context_->VSSetConstantBuffers(0,1,precipitation_constants_.GetAddressOf());
    context_->PSSetConstantBuffers(0,1,precipitation_constants_.GetAddressOf());
    {ID3D11ShaderResourceView* base = ambient_ground_srv_ ? ambient_ground_srv_.Get() : ambient_height_srv_.Get();context_->VSSetShaderResources(1,1,&base);}
    context_->VSSetShaderResources(0,1,ambient_height_srv_.GetAddressOf());context_->PSSetShaderResources(2,1,depth_srv_.GetAddressOf());
    {ID3D11ShaderResourceView* map = shelter ? native_rain_srv_.Get() : nullptr;context_->VSSetShaderResources(3,1,&map);context_->PSSetShaderResources(3,1,&map);}
    bind_owned_sky_pixel_inputs();  // borderFogColor reads the sky probe
    {ID3D11ShaderResourceView* flakes=precipitation_flakes_srv_.Get();context_->PSSetShaderResources(4,1,&flakes);}
    context_->PSSetSamplers(0,1,precipitation_sampler_.GetAddressOf());
    // The drops take the atmosphere per corner (precipitationVS): the world
    // fog frame (b10), fog volume (t11), sky probe (t12) and fog sampler (s7),
    // from their owners exactly as bind_world_fog binds them for the pixel stage.
    {ID3D11ShaderResourceView* fog_views[2]{fog_volume_active_ ? fog_integrated_srv_.Get() : nullptr,
                                            sky_inputs_active_ ? sky_probe_srv_[sky_index_].Get() : nullptr};
     context_->VSSetShaderResources(11,2,fog_views);}
    context_->VSSetConstantBuffers(10,1,fog_frame_constants_.GetAddressOf());context_->VSSetSamplers(7,1,fog_sampler_.GetAddressOf());
    // Snow as a material (pcSnowLit): the cloud shadow (t10) and the scene's
    // lamp records (b5, t17 grid, t18 lights) for the vertex stage.
    {ID3D11ShaderResourceView* shadow = sky_inputs_active_ ? sky_shadow_srv_.Get() : nullptr;context_->VSSetShaderResources(10,1,&shadow);
     ID3D11ShaderResourceView* lamps[2]{c.snow_light[3] > 0.f ? native_point_light_grid_srv_.Get() : nullptr,
                                        c.snow_light[3] > 0.f ? native_point_light_srv_.Get() : nullptr};
     context_->VSSetShaderResources(17,2,lamps);context_->VSSetConstantBuffers(5,1,native_point_light_constants_.GetAddressOf());}
    // The veil first (behind every drop), then the drops; each part is its
    // own profiler pass (precipitation_veil / precipitation / _splash).
    transition_profile_pass("precipitation_veil");
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(precipitation_veil_vs_.Get(),nullptr,0);context_->PSSetShader(precipitation_veil_ps_.Get(),nullptr,0);
    context_->Draw(3,0);
    transition_profile_pass("precipitation");
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context_->VSSetShader(precipitation_vs_.Get(),nullptr,0);context_->PSSetShader(sand ? precipitation_sand_ps_.Get() : precipitation_ps_.Get(),nullptr,0);
    context_->DrawInstanced(4,instances,0,0);
    // Near sand grains: half the preset's density again over a box of
    // tiles around the eye, the same field (its own drop indices), full to
    // 0.5x the focus and gone by 0.8x (at most 32 m). No bands or billows.
    UINT near_instances = 0;
    if (sand) {
        const unsigned near_per_tile = static_cast<unsigned>(std::ceil(quality.sand_density*.5f*amount*kTilePeriod*kTilePeriod));
        const float radius = std::clamp(focus_distance*.8f,12.f,32.f);
        const double nx0 = std::floor((eye[0]-radius)/kTilePeriod), ny0 = std::floor((eye[1]-radius)/kTilePeriod);
        const unsigned near_columns = unsigned(std::floor((eye[0]+radius)/kTilePeriod)-nx0)+1u;
        const unsigned near_rows = unsigned(std::floor((eye[1]+radius)/kTilePeriod)-ny0)+1u;
        near_instances = near_per_tile*near_columns*near_rows;
        if (near_instances) {
            c.window[0] = float(nx0*kTilePeriod);c.window[1] = float(ny0*kTilePeriod);c.window[2] = float(near_columns);
            c.fall[0] = float(near_per_tile);
            c.thin[0] = radius*(.5f/.8f);c.thin[1] = radius;c.thin[2] = 0.f;
            c.sheet[1] = 0.f;c.sand[1] = 0.f;c.sand2[2] = float(per_tile);
            D3D11_MAPPED_SUBRESOURCE near_mapped{};
            if (SUCCEEDED(context_->Map(precipitation_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&near_mapped))) {
                std::memcpy(near_mapped.pData,&c,sizeof(c));context_->Unmap(precipitation_constants_.Get(),0);
                context_->DrawInstanced(4,near_instances,0,0);
            } else near_instances = 0;
        }
    }
    if (splash_slots) {
        transition_profile_pass("precipitation_splash");
        context_->VSSetShader(precipitation_splash_vs_.Get(),nullptr,0);context_->PSSetShader(precipitation_splash_ps_.Get(),nullptr,0);
        context_->DrawInstanced(4,splash_slots*tiles,0,0);
        profiler_.add_draw(1,1);profiler_.add_batch(1);
    }
    {ID3D11Buffer* no_buffer=nullptr;ID3D11SamplerState* no_sampler=nullptr;ID3D11ShaderResourceView* no_views[4]{};
     context_->VSSetConstantBuffers(10,1,&no_buffer);context_->VSSetConstantBuffers(5,1,&no_buffer);context_->VSSetSamplers(7,1,&no_sampler);
     context_->VSSetShaderResources(10,3,no_views);context_->VSSetShaderResources(17,2,no_views);context_->VSSetShaderResources(0,2,no_views);context_->VSSetShaderResources(3,1,no_views);
     context_->PSSetShaderResources(2,3,no_views);}
    profiler_.add_draw(2,2);profiler_.add_batch(2);
    status(stock != nullptr,"Precipitation: %s %.0f%% strength %.2f, %u drops, %u splashes (%u tiles), shelter %s (%llu bakes), snow cover %.0f%%%s%s%s%s",kind_name,
           fall.presence*100.f,strength,unsigned(instances+near_instances),splash_slots*tiles,tiles,shelter ? "baked" : native_shadows_enabled_ ? "pending" : "off (sun shadows off)",
           native_rain_bakes_,snow_cover_*100.f,test ? " [precip_test]" : "",precipitation_paused_ ? " [paused: game time stopped]" : "",
           stock_sep,stock_text);
}

#endif
} // namespace owned
