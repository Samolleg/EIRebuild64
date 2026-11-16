// Renderer shadows: shadow resources, ranges, begin/flush of native shadows, static shadow store and bakes, neighbour casters.
#include <intrin.h>
#include "point_shadow_policy.h"
#include "cursor_image.h"
#include "shadow_softness.h"
#include "shadow_foliage.h"
#include "contact_blend.h"
#include "native_instance_flags.h"
#include "terrain_cliff.h"
#include "ground_cover_surface.h"
#include "shadow_far_retention.h"
#include "liquid_surface_shader.h"
#include "world_fog.h"
#include "ambient_occlusion.h"
#include "game_particles.h"
#include "sky_clouds.h"
#include "cave_lighting.h"
#include "terrain_material.h"
#include "native_shader.h"
#include "owned_terrain_decode.h"
#include "renderer.h"
#include "retained_static_submission.h"
#include "graphics_settings.h"
#include "ui_language.h"
#include "game_focus.h"
#include "cursor_confinement.h"
#include "combat_ui.h"
#include "world_texture_sampler.h"
#include "texture_mips.h"
#include "character_pane_owned_text.h"
#include "owned_resource_runtime.h"
#include "neighbour_maps.h"
#include "native_prebuilt.h"
#include "neighbour_client.h"
#include "neighbour_piece.h"
#include "static_lod.h"
#include "owned_resource_store.h"
#include "res_mapping.h"
#include "res_cache_release.h"
#include "engine_bridge.h"
#include "camp_inventory.h"
#include "retained_static_scene.h"
#include "widescreen.h"
#include "test_harness.h"
#include "x64_remote_link.h"
#include "dlss_frame.h"
#include "stall_watchdog.h"
#include <optional>
#include <sstream>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <objbase.h>
#include <bcrypt.h>
#include <psapi.h>
#include <malloc.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iterator>
#include <thread>
#include <deque>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <unordered_set>
#include "renderer_internal.h"

namespace owned {

#if !defined(EID3D11_SHADER_EMBED)
void Renderer::retire_native_shadows() noexcept {
 native_shadow_static_depth_.Reset();for(auto& view:native_shadow_static_dsv_)view.Reset();native_shadow_static_valid_.fill(false);
 shadow12_.maps.reset();shadow12_.statics.reset();
 if(context_&&!d3d12_){std::array<ID3D11ShaderResourceView*,PointShadowTextureLayout::array_bindings> empty{};
  context_->PSSetShaderResources(PointShadowTextureLayout::first_srv,static_cast<UINT>(empty.size()),empty.data());}
 native_shadow_valid_.fill(false);native_shadow_frame_={};native_shadow_far_waits_={};
 native_shadow_dirty_mask_=native_shadow_cleared_mask_=0;
 native_shadow_collecting_=native_shadow_failed_=native_shadow_constants_dirty_=false;
 for(auto& view:native_shadow_dsv_)view.Reset();
 native_shadow_depth_srv_.Reset();native_shadow_depth_.Reset();
 release_native_shadow_range();native_shadow_reduce_cs_.Reset();native_shadow_dilate_cs_.Reset();
 native_shadow_range_constants_.Reset();native_shadow_range_failed_=false;
 native_point_shadow_pages_.clear();
 native_rain_depth_.Reset();native_rain_dsv_.Reset();native_rain_srv_.Reset();native_rain_constants_.Reset(); rain12_={};
 native_rain_foliage_.Reset();native_rain_foliage_rtv_.Reset();native_rain_foliage_srv_.Reset();
 native_rain_valid_=native_rain_active_=false;native_rain_side_=0;
 for(unsigned k=0;k<2;++k){native_shelter_soft_[k].Reset();native_shelter_soft_srv_[k].Reset();native_shelter_soft_uav_[k].Reset();}
 native_shelter_soft_cs_.Reset();native_shelter_blur_cs_.Reset();native_shelter_blur_fill_=-1;native_rain_rings_.Reset();native_rain_rings_srv_.Reset();native_rain_rings_uav_.Reset();native_rain_rings_cs_.Reset();native_rain_rings_constants_.Reset();native_rain_rings_key_[0]=native_rain_rings_key_[1]=-1.f;native_rain_rings_failed_=false;native_shelter_soft_constants_.Reset();native_shelter_soft_side_=0;native_shelter_soft_valid_=native_shelter_soft_failed_=false;
 native_point_shadow_generation_=0;
 native_point_shadow_active_=false;native_point_shadow_frame_={};native_point_shadow_constants_.Reset();
 native_shadow_constants_.Reset();native_shadow_sampler_.Reset();native_shadow_far_sampler_.Reset();native_shadow_gather_sampler_.Reset();
 native_point_shadow_sampler_.Reset();
}

HRESULT Renderer::upload_native_shadow_constants(UINT slice){
 auto* buffer=native_rain_active_?native_rain_constants_.Get():native_point_shadow_active_?native_point_shadow_constants_.Get():native_shadow_constants_.Get();
 if(!d3d12_&&(!context_||!buffer))return E_FAIL;
 NativeShadowConstants constants{};
 if(native_rain_active_){
  // Shelter map (renderer_precipitation.cpp): its one slice; no sun bits.
  constants.light[0]=native_rain_slice_.world_to_light;constants.camera[0]=native_rain_slice_.camera_slice;
  constants.bias[0]=native_rain_slice_.bias;
  constants.control[1]=slice;constants.control[2]=native_rain_side_;constants.control[3]=native_point_shadow_side_;
  if(d3d12_){rain12_.constants=constants;return S_OK;} // the D3D12 flush sets it per pass (b2)
  D3D11_MAPPED_SUBRESOURCE mapped{};const HRESULT hr=context_->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
  if(FAILED(hr))return hr;
  std::memcpy(mapped.pData,&constants,sizeof(constants));context_->Unmap(buffer,0);return S_OK;
 }
 for(UINT index=0;index<(native_point_shadow_active_?6u:5u);++index){
  const auto& value=native_point_shadow_active_?native_point_shadow_frame_.slices[index]:native_shadow_frame_.slices[index];
  constants.light[index]=value.world_to_light;constants.camera[index]=value.camera_slice;
  constants.bias[index]=value.bias;
  if(native_shadows_enabled_&&value.enabled&&!native_point_shadow_active_&&native_shadow_valid_[index])constants.control[0]|=1u<<index;
 }
 if(!native_point_shadow_active_){
  constants.bias[5]=native_shadow_softness_;
  constants.camera[5][0]=native_sun_filter_[0];constants.camera[5][1]=native_sun_filter_[1]; // nativeShadowShear, nativeSunFoliageFilter
 }
 // rt_sun_shadows: the layer of the renderer's own casters (nativeRtCoverShadow):
 // bit 8, its slice, the close map it was drawn with (valid, and still the only one).
 // Its matrix and bias stand at its (otherwise unused) slice of the block; its
 // camera selector stays empty, so no map selection ever picks it.
 if(!native_point_shadow_active_&&native_traced_sun_&&native_rt_cover_layer_<3&&native_rt_cover_source_<3&&
    !native_shadow_frame_.slices[native_rt_cover_layer_].enabled&&configured_boolean(debug_overlay_.config(),"rt_sun_cover_shadows",true)&&
    (native_rt_cover_drawing_||(constants.control[0]&7u)==(1u<<native_rt_cover_source_))){
  constants.light[native_rt_cover_layer_]=native_rt_cover_light_;constants.bias[native_rt_cover_layer_]=native_rt_cover_bias_;
  constants.control[0]|=256u|(UINT(native_rt_cover_layer_)<<9)|(UINT(native_rt_cover_source_)<<12);
 }
 constants.control[1]=slice;constants.control[2]=native_shadow_side_;constants.control[3]=native_point_shadow_side_;
 if(native_shadow_bake_active_&&!native_point_shadow_active_&&slice<5){
  // Masked (terrain) bakes carry the bridge's final masks; the store bake
  // culls a scrolled map's records against its exposed strip.
  if(native_shadow_bake_active_->masked)constants.scroll_control={0u,native_shadow_side_,1u,0u};
  else if(native_shadow_scroll_mask_&(1u<<slice)){
   const auto& copied=native_shadow_scroll_[slice].copied;
   for(unsigned i=0;i<4;++i)constants.scroll_copied[i]=static_cast<float>(copied[i]);
   constants.scroll_control={1u,native_shadow_side_,0u,0u};
  }
 }
 // Sun maps whose early-out cells are current (nativeShadowEarly).
 if(!native_point_shadow_active_&&native_shadow_early_out_)
  constants.scroll_control[3]=constants.control[0]&native_shadow_range_mask_;
 if(!native_point_shadow_active_){
  native_sun_shadows_live_=constants.control[0]!=0||constants.bias[4][2]>.5f; // NATIVE_SUN_SHADOWS_LIVE
  native_point_soft_live_=constants.bias[5][2]>.5f; // nativePointVisibility softQuality
 }
 if(d3d12_){
  (native_point_shadow_active_?shadow12_.point_constants:shadow12_.constants)=constants;
  return S_OK;
 } // set per pass and draw (b2); point faces never replace the sun receiver frame
 D3D11_MAPPED_SUBRESOURCE mapped{};const HRESULT hr=context_->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
 if(FAILED(hr))return hr;
 std::memcpy(mapped.pData,&constants,sizeof(constants));context_->Unmap(buffer,0);return S_OK;
}

HRESULT Renderer::create_native_shadow_resources(){
 if(d3d12_){ // D3D12: the five sun maps and their static layer as depth arrays
  if(shadow12_.maps&&shadow12_.statics)return S_OK;
  shadow12_.maps.reset();shadow12_.statics.reset();
  HRESULT hr=d3d12_->create_depth_array(native_shadow_side_,5,shadow12_.maps);
  if(SUCCEEDED(hr))hr=d3d12_->create_depth_array(native_shadow_side_,5,shadow12_.statics);
  if(FAILED(hr)){shadow12_.maps.reset();shadow12_.statics.reset();return hr;}
  native_shadow_static_valid_.fill(false);
  return S_OK;
 }
  if(native_shadow_depth_&&native_shadow_depth_srv_&&native_shadow_sampler_&&
     native_shadow_far_sampler_&&native_shadow_gather_sampler_&&native_shadow_static_depth_)return S_OK;
 if(!device_||!context_)return E_FAIL;
 D3D11_TEXTURE2D_DESC description{};description.Width=description.Height=native_shadow_side_;
 description.ArraySize=5;description.MipLevels=1;description.Format=DXGI_FORMAT_R32_TYPELESS;
 description.SampleDesc.Count=1;description.Usage=D3D11_USAGE_DEFAULT;
 description.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
 const auto bytes=static_cast<std::uint64_t>(description.Width)*description.Height*description.ArraySize*4u;
 HRESULT hr=reserve_native_owned_texture_bytes(bytes,true);if(FAILED(hr))return hr;
 ComPtr<ID3D11Texture2D> depth;hr=device_->CreateTexture2D(&description,nullptr,&depth);
 if(hr==E_OUTOFMEMORY && SUCCEEDED(retire_native_owned_texture_bytes(bytes,true)))
  hr=device_->CreateTexture2D(&description,nullptr,&depth);
 if(FAILED(hr))return hr;
 std::array<ComPtr<ID3D11DepthStencilView>,5> views;
 for(UINT slice=0;slice<5;++slice){
  D3D11_DEPTH_STENCIL_VIEW_DESC view{};view.Format=DXGI_FORMAT_D32_FLOAT;view.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
  view.Texture2DArray.ArraySize=1;view.Texture2DArray.FirstArraySlice=slice;
  if(FAILED(hr=device_->CreateDepthStencilView(depth.Get(),&view,&views[slice])))return hr;
 }
 D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_FLOAT;srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
 srv.Texture2DArray.MipLevels=1;srv.Texture2DArray.ArraySize=5;
 ComPtr<ID3D11ShaderResourceView> depth_srv;if(FAILED(hr=device_->CreateShaderResourceView(depth.Get(),&srv,&depth_srv)))return hr;
  D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
 sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;
 sampler.BorderColor[0]=sampler.BorderColor[1]=sampler.BorderColor[2]=sampler.BorderColor[3]=1;
  sampler.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;sampler.MaxLOD=D3D11_FLOAT32_MAX;
  ComPtr<ID3D11SamplerState> comparison_sampler;if(FAILED(hr=device_->CreateSamplerState(&sampler,&comparison_sampler)))return hr;
  sampler.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
  ComPtr<ID3D11SamplerState> far_sampler;if(FAILED(hr=device_->CreateSamplerState(&sampler,&far_sampler)))return hr;
  D3D11_SAMPLER_DESC gather{};gather.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
  gather.AddressU=gather.AddressV=gather.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;gather.MaxLOD=D3D11_FLOAT32_MAX;
  ComPtr<ID3D11SamplerState> gather_sampler;if(FAILED(hr=device_->CreateSamplerState(&gather,&gather_sampler)))return hr;
  // Static-layer cache: same format, depth-only, copied into the maps.
  D3D11_TEXTURE2D_DESC static_description=description;static_description.BindFlags=D3D11_BIND_DEPTH_STENCIL;
  if(FAILED(hr=reserve_native_owned_texture_bytes(bytes,true)))return hr;
  ComPtr<ID3D11Texture2D> static_depth;
  if(FAILED(hr=device_->CreateTexture2D(&static_description,nullptr,&static_depth)))return hr;
  std::array<ComPtr<ID3D11DepthStencilView>,5> static_views;
  for(UINT slice=0;slice<5;++slice){
   D3D11_DEPTH_STENCIL_VIEW_DESC view{};view.Format=DXGI_FORMAT_D32_FLOAT;view.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
   view.Texture2DArray.ArraySize=1;view.Texture2DArray.FirstArraySlice=slice;
   if(FAILED(hr=device_->CreateDepthStencilView(static_depth.Get(),&view,&static_views[slice])))return hr;
  }
  native_shadow_static_depth_=std::move(static_depth);native_shadow_static_dsv_=std::move(static_views);
  native_shadow_static_valid_.fill(false);
  native_shadow_depth_=std::move(depth);native_shadow_depth_srv_=std::move(depth_srv);
  native_shadow_dsv_=std::move(views);native_shadow_sampler_=std::move(comparison_sampler);
  native_shadow_far_sampler_=std::move(far_sampler);native_shadow_gather_sampler_=std::move(gather_sampler);return S_OK;
}
#endif
namespace {

// Sun-shadow early-out (shadow_early_out). Per 16x16-texel cell of each
// redrawn sun map: the nearest and farthest depth (reduce), then the same over
// the 5x5 cells around it (dilate: two cells reach 32 texels, more than the
// widest filter read, 24 search texels plus the 1.5 of the plane compare's
// Gather). Cells past the map edge make the farthest depth 1: texels outside
// the map compare lit, so such a lookup is never in umbra. Stored as (nearest,
// 1 - farthest) so an unbound view (zeros) never skips (nativeShadowEarly).
constexpr char kShadowRangeShader[] = R"(
cbuffer ShadowRangeConstants:register(b0){uint4 rangeControl;}; // x slice, y cells per side
Texture2DArray<float> rangeDepth:register(t0);
Texture2DArray<float2> rangeCells:register(t1);
RWTexture2DArray<float2> rangeOutput:register(u0);
groupshared float2 rangeShared[64];
[numthreads(8,8,1)]void shadowRangeReduceCS(uint3 group:SV_GroupID,uint3 thread:SV_GroupThreadID,uint index:SV_GroupIndex){
 int2 at=int2(group.xy*16+thread.xy*2);uint slice=rangeControl.x;
 float a=rangeDepth.Load(int4(at,slice,0)),b=rangeDepth.Load(int4(at+int2(1,0),slice,0));
 float c=rangeDepth.Load(int4(at+int2(0,1),slice,0)),d=rangeDepth.Load(int4(at+1,slice,0));
 rangeShared[index]=float2(min(min(a,b),min(c,d)),max(max(a,b),max(c,d)));
 GroupMemoryBarrierWithGroupSync();
 [unroll]for(uint step=32;step>0;step>>=1){
  if(index<step){float2 p=rangeShared[index],q=rangeShared[index+step];rangeShared[index]=float2(min(p.x,q.x),max(p.y,q.y));}
  GroupMemoryBarrierWithGroupSync();
 }
 if(index==0)rangeOutput[uint3(group.xy,slice)]=rangeShared[0];
}
[numthreads(8,8,1)]void shadowRangeDilateCS(uint3 id:SV_DispatchThreadID){
 int cells=int(rangeControl.y);uint slice=rangeControl.x;
 if(any(int2(id.xy)>=cells))return;
 float nearest=1,farthest=0;
 [unroll]for(int y=-2;y<=2;y++)[unroll]for(int x=-2;x<=2;x++){
  int2 cell=int2(id.xy)+int2(x,y);
  if(any(cell<0)||any(cell>=cells))farthest=1;
  else{float2 range=rangeCells.Load(int4(cell,slice,0));nearest=min(nearest,range.x);farthest=max(farthest,range.y);}
 }
 rangeOutput[uint3(id.xy,slice)]=float2(nearest,1-farthest);
}
)";
}  // namespace

void append_shadow_range_shader_programs(std::vector<ShaderProgram>& out){
 out.push_back({{kShadowRangeShader,sizeof(kShadowRangeShader)-1},nullptr,nullptr,"shadowRangeReduceCS","cs_5_0",0});
 out.push_back({{kShadowRangeShader,sizeof(kShadowRangeShader)-1},nullptr,nullptr,"shadowRangeDilateCS","cs_5_0",0});
}

#if !defined(EID3D11_SHADER_EMBED)
void Renderer::release_native_shadow_range() noexcept{
 if(d3d12_){
  d3d12_->set_resource(64,static_cast<const D3D12Texture*>(nullptr));
  shadow12_.range_cells.reset();shadow12_.range.reset();
 }
 if(context_&&!d3d12_){ID3D11ShaderResourceView* empty=nullptr;context_->PSSetShaderResources(64,1,&empty);}
 native_shadow_cells_.Reset();native_shadow_cells_srv_.Reset();native_shadow_cells_uav_.Reset();
 native_shadow_range_.Reset();native_shadow_range_srv_.Reset();native_shadow_range_uav_.Reset();
 native_shadow_range_side_=0;native_shadow_range_mask_=0;
}

// Before the first sun receiver: cells for every valid map without them (a
// redrawn map lost its cells when its collection began; a map kept from an
// earlier frame keeps them).
void Renderer::ensure_native_shadow_range() noexcept{
 if(!native_shadow_early_out_||native_shadow_range_failed_||native_shadow_collecting_||
    native_point_shadow_active_||native_rain_active_)return;
 std::uint8_t missing=0;
 for(UINT slice=0;slice<5;++slice)
  if(native_shadow_valid_[slice]&&!(native_shadow_range_mask_&(1u<<slice)))missing|=std::uint8_t(1u<<slice);
 if(missing&&build_native_shadow_range(missing)==S_OK)(void)upload_native_shadow_constants(0);
}

HRESULT Renderer::build_native_shadow_range(std::uint8_t slices) noexcept{
 if(d3d12_){
  if(!slices||!native_shadow_early_out_||native_shadow_range_failed_||!shadow12_.maps)return S_FALSE;
  const UINT side=native_shadow_side_;
  if(side<16||side%16)return S_FALSE;
  const UINT cells=side/16;
  const auto fail=[&](HRESULT result){
   native_shadow_range_failed_=true;release_native_shadow_range();
   d3d12_->omit("sun shadow early-out cells (compute failed)",false);return result;
  };
  HRESULT hr=S_OK;
  if(native_shadow_range_side_!=side||!shadow12_.range_cells||!shadow12_.range){
   release_native_shadow_range();
   hr=d3d12_->create_storage_texture(cells,cells,0,DXGI_FORMAT_R32G32_FLOAT,shadow12_.range_cells,1,5);
   if(SUCCEEDED(hr))hr=d3d12_->create_storage_texture(cells,cells,0,DXGI_FORMAT_R32G32_FLOAT,shadow12_.range,1,5);
   if(FAILED(hr))return fail(hr);
   native_shadow_range_side_=side;
  }
  std::vector<ShaderProgram> programs;append_shadow_range_shader_programs(programs);
  transition_profile_pass("shadow_range");
  // A pending PSO leaves the slice invalid and is retried before its next receiver.
  // Each dispatch uses the executor's tracked SRV/UAV transitions and UAV barriers.
  for(UINT slice=0;slice<5;++slice){
   if(!(slices&(1u<<slice)))continue;
   const std::array<UINT,4> control{slice,cells,0u,0u};
   d3d12_->clear_slots();d3d12_->set_constants(0,control.data(),sizeof(control));
   d3d12_->set_resource(0,shadow12_.maps.get());
   d3d12_->set_unordered(0,shadow12_.range_cells.get());
   hr=d3d12_->dispatch(programs[0],cells,cells,1);
   if(hr==S_OK){
    d3d12_->clear_slots();d3d12_->set_constants(0,control.data(),sizeof(control));
    d3d12_->set_resource(1,shadow12_.range_cells.get());
    d3d12_->set_unordered(0,shadow12_.range.get());
    hr=d3d12_->dispatch(programs[1],(cells+7)/8,(cells+7)/8,1);
   }
   d3d12_->clear_slots();
   if(FAILED(hr))return fail(hr);
   if(hr!=S_OK)return hr;
   native_shadow_range_mask_|=std::uint8_t(1u<<slice);
  }
  return S_OK;
 }
 if(!slices||!native_shadow_early_out_||native_shadow_range_failed_||!device_||!context_||!native_shadow_depth_srv_)return S_FALSE;
 const UINT side=native_shadow_side_;
 if(side<16||side%16)return S_FALSE;
 const UINT cells=side/16;
 HRESULT hr=S_OK;
 const auto fail=[&](HRESULT result){native_shadow_range_failed_=true;release_native_shadow_range();return result;};
 if(!native_shadow_reduce_cs_||!native_shadow_dilate_cs_||!native_shadow_range_constants_){
  std::vector<ShaderProgram> programs;append_shadow_range_shader_programs(programs);
  const auto compile=[&](const ShaderProgram& program,ComPtr<ID3D11ComputeShader>& shader){
   ComPtr<ID3DBlob> code,errors;HRESULT result=compile_shader(program,&code,&errors);
   if(SUCCEEDED(result))result=create_shader(device_.Get(),code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader);
   return result;
  };
  if(FAILED(hr=compile(programs[0],native_shadow_reduce_cs_))||FAILED(hr=compile(programs[1],native_shadow_dilate_cs_)))return fail(hr);
  D3D11_BUFFER_DESC constants{};constants.ByteWidth=16;constants.Usage=D3D11_USAGE_DYNAMIC;
  constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;constants.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  if(FAILED(hr=device_->CreateBuffer(&constants,nullptr,&native_shadow_range_constants_)))return fail(hr);
 }
 if(native_shadow_range_side_!=side){
  release_native_shadow_range();
  D3D11_TEXTURE2D_DESC description{};description.Width=description.Height=cells;description.ArraySize=5;description.MipLevels=1;
  description.Format=DXGI_FORMAT_R32G32_FLOAT;description.SampleDesc.Count=1;description.Usage=D3D11_USAGE_DEFAULT;
  description.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
  if(FAILED(hr=device_->CreateTexture2D(&description,nullptr,&native_shadow_cells_))||
     FAILED(hr=device_->CreateTexture2D(&description,nullptr,&native_shadow_range_)))return fail(hr);
  if(FAILED(hr=device_->CreateShaderResourceView(native_shadow_cells_.Get(),nullptr,&native_shadow_cells_srv_))||
     FAILED(hr=device_->CreateUnorderedAccessView(native_shadow_cells_.Get(),nullptr,&native_shadow_cells_uav_))||
     FAILED(hr=device_->CreateShaderResourceView(native_shadow_range_.Get(),nullptr,&native_shadow_range_srv_))||
     FAILED(hr=device_->CreateUnorderedAccessView(native_shadow_range_.Get(),nullptr,&native_shadow_range_uav_)))return fail(hr);
  native_shadow_range_side_=side;
 }
 transition_profile_pass("shadow_range");
 ID3D11ShaderResourceView* empty[2]{};ID3D11UnorderedAccessView* no_uav=nullptr;
 context_->PSSetShaderResources(64,1,empty);
 context_->CSSetConstantBuffers(0,1,native_shadow_range_constants_.GetAddressOf());
 for(UINT slice=0;slice<5;++slice){
  if(!(slices&(1u<<slice)))continue;
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(FAILED(hr=context_->Map(native_shadow_range_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return fail(hr);
  const std::array<UINT,4> control{slice,cells,0u,0u};std::memcpy(mapped.pData,control.data(),sizeof(control));
  context_->Unmap(native_shadow_range_constants_.Get(),0);
  ID3D11ShaderResourceView* reduce[2]={native_shadow_depth_srv_.Get(),nullptr};
  context_->CSSetShaderResources(0,2,reduce);
  context_->CSSetUnorderedAccessViews(0,1,native_shadow_cells_uav_.GetAddressOf(),nullptr);
  context_->CSSetShader(native_shadow_reduce_cs_.Get(),nullptr,0);context_->Dispatch(cells,cells,1);
  context_->CSSetUnorderedAccessViews(0,1,&no_uav,nullptr);
  ID3D11ShaderResourceView* dilate[2]={nullptr,native_shadow_cells_srv_.Get()};
  context_->CSSetShaderResources(0,2,dilate);
  context_->CSSetUnorderedAccessViews(0,1,native_shadow_range_uav_.GetAddressOf(),nullptr);
  context_->CSSetShader(native_shadow_dilate_cs_.Get(),nullptr,0);context_->Dispatch((cells+7)/8,(cells+7)/8,1);
  context_->CSSetUnorderedAccessViews(0,1,&no_uav,nullptr);
  context_->CSSetShaderResources(0,2,empty);
  native_shadow_range_mask_|=std::uint8_t(1u<<slice);
 }
 ID3D11Buffer* no_constants=nullptr;context_->CSSetConstantBuffers(0,1,&no_constants);
 context_->CSSetShader(nullptr,nullptr,0);
 return S_OK;
}

void Renderer::record_native_shadow_source_diagnostics(std::uint64_t world_serial,
 const NativeShadowSourceDiagnostics& values) noexcept {SCENE_ENTRY("record_native_shadow_source_diagnostics");
 if(!world_serial)return;
 auto& stats=native_frame_stats_;
 stats.native_shadow_source_world_serial=world_serial;
 ++stats.native_shadow_source_publications;
 stats.native_shadow_source_active_visuals+=values.active_visuals;
 stats.native_shadow_source_active_terrain+=values.active_terrain;
 stats.native_shadow_source_changed_visuals+=values.changed_visuals;
 stats.native_shadow_source_changed_terrain+=values.changed_terrain;
 stats.native_shadow_source_static_casters_reused+=values.static_casters_reused;
 stats.native_shadow_source_part_bounds_rebuilt+=values.part_bounds_rebuilt;
 stats.native_shadow_source_receiver_refits+=values.receiver_refits;
 stats.native_shadow_source_caster_refits+=values.caster_refits;
 stats.native_shadow_source_map_reuses+=values.map_reuses;
 stats.native_shadow_source_dirty_mask|=values.dirty_mask;
}

bool Renderer::begin_native_shadows(const NativeShadowFrame& frame,std::uint8_t& dirty_mask) noexcept {SCENE_ENTRY("begin_native_shadows");
 dirty_mask=0;native_shadow_retained_mask_=0;native_shadow_scroll_mask_=0;
 if(!native_shadows_enabled_||native_shadow_collecting_||native_point_shadow_active_||native_rain_active_)return false;
 // Finish old receiver commands before replacing any map or its constants.
 if(FAILED(flush_native())){
  // An explicit empty frame retires the old domain even if its queued color
  // failed. Ordinary replacement failures still preserve the prior maps.
  if(std::none_of(frame.slices.begin(),frame.slices.end(),[](const NativeShadowSlice& slice){return slice.enabled;})){
   native_shadow_valid_.fill(false);upload_native_shadow_constants(0);
  }
  return false;
 }
 auto& decision=native_frame_stats_.shadow_decision;
 const auto prior_selected=decision.selected_per_slice;
 const auto begin_calls=decision.begin_calls+1;
 decision={};decision.begin_calls=begin_calls;decision.selected_per_slice=prior_selected;
 // What this begin replaces, restored if its collection is aborted before
 // any map is cleared or drawn (end_native_shadows(false)): the bridge's
 // reuse check begins with its cached frame and aborts when maps are dirty.
 // Without the restore that check left the close maps invalid every frame,
 // so the real begin redrew their whole static layer each frame.
 {auto& r=native_shadow_rollback_;
  r.frame=native_shadow_frame_;r.valid=native_shadow_valid_;r.drawn_camera=native_shadow_drawn_camera_;
  r.far_waits=native_shadow_far_waits_;r.range_mask=native_shadow_range_mask_;r.extras_key=native_shadow_extras_key_;r.armed=true;}
 const auto reject=[&](){
  native_shadow_valid_.fill(false);native_shadow_collecting_=false;
  native_shadow_dirty_mask_=0;dirty_mask=0;
  upload_native_shadow_constants(0);return false;
 };
 try{
  // The scene owner can resubmit the same immutable value after a confirmed
  // no-change source publication. Current map resources and the small numeric
  // projection tuple still have to match; no validity vector is rescanned or
  // copied on that path.
  // Neighbour-map casters are renderer-owned static content: a static layer
  // drawn with another membership or level is stale like changed validity.
  const auto neighbour_key=object_caster_key();
  // Moving renderer-owned casters (critters that moved, swaying cover) redraw
  // the close maps' per-frame layer: no value reuse on those frames.
  // A changed set of those casters (ground_cover_shadows or critters
  // switched, the first placement ready) redraws the close maps once.
  const std::uint32_t extras_key=owned_shadow_casters_key();
  const bool extras_changed=extras_key!=native_shadow_extras_key_;
  native_shadow_extras_key_=extras_key;
  const std::uint8_t animate=extras_changed?std::uint8_t(7):owned_shadow_casters_animate();
  if(!animate&&frame.publication_serial &&
     frame.publication_serial==native_shadow_frame_.publication_serial &&
     frame.world_epoch==native_shadow_frame_.world_epoch &&
     (d3d12_?shadow12_.maps!=nullptr:native_shadow_depth_.Get() && native_shadow_depth_srv_.Get())){
   bool same=true;std::uint64_t reused=0;
   for(UINT index=0;index<5;++index){
    const auto& next=frame.slices[index];const auto& old=native_shadow_frame_.slices[index];
    if(next.enabled!=old.enabled || (next.enabled &&
       (!native_shadow_valid_[index] || native_shadow_neighbour_drawn_[index]!=neighbour_key ||
        next.world_to_light!=old.world_to_light ||
        next.camera_slice!=old.camera_slice || next.bias!=old.bias ||
        next.receiver_minimum!=old.receiver_minimum ||
        next.receiver_maximum!=old.receiver_maximum))){same=false;break;}
    reused+=next.enabled;
   }
   if(same){
    ++native_shadow_value_reuses_;
    ++native_frame_stats_.native_shadow_value_reuses;
    native_shadow_reused_slices_+=reused;
    native_shadow_dirty_mask_=0;native_shadow_cleared_mask_=0;native_shadow_extras_mask_=0;
    native_shadow_failed_=false;native_shadow_collecting_=true;
    if(FAILED(upload_native_shadow_constants(0)))return reject();
    native_shadow_constants_dirty_=false;
    return true;
   }
  }
  std::size_t key_bytes=0;bool enabled=false;
  for(const auto& slice:frame.slices){
   if(!slice.enabled)continue;enabled=true;
   if(slice.bias[0]<=0||slice.bias[1]<=0)return reject();
   for(float value:slice.world_to_light)if(!std::isfinite(value))return reject();
   for(float value:slice.camera_slice)if(!std::isfinite(value))return reject();
   for(float value:slice.bias)if(!std::isfinite(value))return reject();
   for(float value:slice.receiver_minimum)if(!std::isfinite(value))return reject();
   for(float value:slice.receiver_maximum)if(!std::isfinite(value))return reject();
   for(const auto& value:slice.validity){
   ++native_shadow_validity_scans_;
   ++native_frame_stats_.native_shadow_validity_scans;
    if(!value.revision)return reject();
   }
   for(const auto& value:slice.static_validity)if(!value.revision)return reject();
   key_bytes+=slice.static_validity.size()*sizeof(NativeShadowCasterRevision);
   if(slice.validity.size()>((32u<<20)-key_bytes)/sizeof(NativeShadowCasterRevision))return reject();
   key_bytes+=slice.validity.size()*sizeof(NativeShadowCasterRevision);
  }
  if(enabled&&FAILED(create_native_shadow_resources()))return reject();
  const auto same_validity=[&](const auto& old,const auto& next){
   if(old.size()!=next.size())return false;
   for(std::size_t entry=0;entry<old.size();++entry){
    ++native_shadow_validity_scans_;
    ++native_frame_stats_.native_shadow_validity_scans;
    if(old[entry]!=next[entry])return false;
   }
   return true;
  };
  std::uint8_t deferred_mask=0,retained_mask=0,static_dirty=0,scroll_mask=0;
  const bool membership_scroll=configured_boolean(debug_overlay_.config(),"shadow_membership_scroll",true);
  for(UINT index=0;index<5;++index){
   const auto& next=frame.slices[index];const auto& old=native_shadow_frame_.slices[index];
   const auto bit=static_cast<std::uint8_t>(1u<<index);
   if(native_shadow_valid_[index])decision.previous_valid_mask|=bit;
   if(old.enabled)decision.previous_enabled_mask|=bit;
   if(next.enabled)decision.current_enabled_mask|=bit;
   if(old.enabled&&native_shadow_frame_.slices[4].bias[2]>.5f)
    decision.previous_spatial_mask|=bit;
   if(next.enabled&&frame.slices[4].bias[2]>.5f)
    decision.current_spatial_mask|=bit;
   decision.previous_validity_counts[index]=old.validity.size();
   decision.current_validity_counts[index]=next.validity.size();
   const bool comparable=native_shadow_valid_[index]&&old.enabled&&next.enabled&&
    frame.world_epoch==native_shadow_frame_.world_epoch;
   const auto drawn_camera=native_shadow_drawn_camera_[index];
   // Temporary reuse witness (removed once directional reuse is settled).
   const auto witness=[&](ShadowRendererDifference cause){
    ++shadow_renderer_cumulative_.counts[static_cast<unsigned>(cause)];};
   if(next.enabled){
    if(!native_shadow_valid_[index])witness(ShadowRendererDifference::invalid_map);
    if(!old.enabled)witness(ShadowRendererDifference::enabled);
    if(frame.world_epoch!=native_shadow_frame_.world_epoch)witness(ShadowRendererDifference::epoch);
    if(comparable){
     if(old.world_to_light!=next.world_to_light)witness(ShadowRendererDifference::world_to_light);
     if(old.camera_slice!=next.camera_slice)witness(ShadowRendererDifference::camera_slice);
     if(old.bias!=next.bias)witness(ShadowRendererDifference::bias);
     if(old.receiver_minimum!=next.receiver_minimum||old.receiver_maximum!=next.receiver_maximum)
      witness(ShadowRendererDifference::receiver_bounds);
    }
   }
   if(comparable)decision.numeric_compared_mask|=bit;
   bool numeric_same=comparable;
   if(numeric_same&&old.world_to_light!=next.world_to_light){
    decision.light_transform_changed_mask|=bit;numeric_same=false;
   }
   // These values select/sample depth; neither the depth vertex shader nor
   // caster culling consumes them. Publish current selectors without redrawing
   // an unchanged light projection. Caster validity still owns content changes.
   if(comparable&&old.camera_slice!=next.camera_slice){
    decision.camera_slice_changed_mask|=bit;
   }
   if(comparable&&old.bias!=next.bias){
    decision.bias_changed_mask|=bit;
   }
   if(comparable&&(old.receiver_minimum!=next.receiver_minimum ||
                    old.receiver_maximum!=next.receiver_maximum)){
    decision.receiver_bounds_changed_mask|=bit;
   }
   if(comparable&&!numeric_same)decision.numeric_changed_mask|=bit;
   // A panning camera does not invalidate distant depth while the old light
   // projection still covers the current receivers. Refresh on scene/resource,
   // sun-axis or coverage changes; stagger the two far-map refresh frames.
   const bool scheduled=index==3 ? native_frame_index_%5u==0 :
    native_frame_index_%10u==7u;
   const bool covers=index>=3 && comparable && frame.world_epoch &&
    far_shadow_projection_covers(old,next,index);
   if(index>=3 && comparable && frame.world_epoch && !covers)
    witness(ShadowRendererDifference::far_coverage_rejected);
   // Deferral keeps the casters selected for the view the map was drawn for;
   // a camera jump since then shows receivers that view never selected.
   const bool deferred=covers && !scheduled &&
    far_shadow_view_continuous(native_shadow_drawn_camera_[index],next.camera_slice);
   if(!deferred)native_shadow_drawn_camera_[index]=next.camera_slice;
   // The scheduled refresh brings caster content up to date. While the
   // retained projection still holds the current casters at the current
   // texel size and the static layer is unchanged, it redraws only the
   // per-frame casters over the cached static layer in that projection, so
   // a pan or a small sun step does not rebuild the whole far map at once.
   const bool neighbour_same=native_shadow_neighbour_drawn_[index]==neighbour_key;
   const bool far_retained=index>=3 && covers && scheduled && !numeric_same &&
    far_shadow_projection_retainable(old,next) &&
    native_shadow_static_valid_[index] && neighbour_same &&
    same_validity(old.static_validity,next.static_validity);
   // A pan moves every map whose projection follows the camera: close maps
   // every frame, far maps once coverage is lost; each rebuilt its whole
   // static layer (L-077). A move by whole texels with the sun, texel size,
   // caster depth range and kept static content unchanged scrolls the cached
   // static layer instead: the flush shifts it and draws only static casters
   // reaching the exposed strip, plus terrain parts that entered the list.
   // An unchanged projection whose static content changed (a far map's
   // terrain membership follows its receivers, so a pan changes it every
   // few frames) goes through the same zero-shift scroll: compatible content
   // keeps the cached layer and draws only the entered parts, instead of
   // the whole layer (about 38% of far static redraws on the lows route).
   const bool membership=membership_scroll && comparable && frame.world_epoch && numeric_same &&
    !deferred && !same_validity(old.static_validity,next.static_validity);
   CloseShadowScroll scroll{};
   const bool moved=comparable && frame.world_epoch && (!numeric_same||membership) && !deferred && !far_retained;
   bool scrolled=moved && shadow_scroll_ps_ && native_shadow_static_valid_[index] && neighbour_same;
   if(scrolled){
    const auto refusal=close_shadow_scroll(old,next,native_shadow_side_,scroll);
    if(refusal!=ShadowScrollRefusal::none){
     scrolled=false;
     const bool far_map=index>=3;
     witness(refusal==ShadowScrollRefusal::axes?(far_map?ShadowRendererDifference::far_reject_axes:ShadowRendererDifference::scroll_reject_axes):
      refusal==ShadowScrollRefusal::texel?(far_map?ShadowRendererDifference::far_reject_texel:ShadowRendererDifference::scroll_reject_texel):
      (far_map?ShadowRendererDifference::far_reject_fraction:ShadowRendererDifference::scroll_reject_fraction));
    }
   }
   if(scrolled&&!far_shadow_projection_retainable(old,next)){
    scrolled=false;witness(index>=3?ShadowRendererDifference::far_reject_depth:ShadowRendererDifference::scroll_reject_depth);}
   bool summary_changed=false;
   if(scrolled&&!static_validity_scroll_compatible(old.static_validity,next.static_validity,
      native_shadow_entered_[index],summary_changed)){
    scrolled=false;witness(summary_changed?ShadowRendererDifference::scroll_reject_summary:
     index>=3?ShadowRendererDifference::far_reject_static:ShadowRendererDifference::scroll_reject_static);}
   // No whole-texel move (only the fitted depth row differs from the kept
   // one) and the same static content: the drawn projection is simply
   // retained. With changed membership the zero shift still goes through the
   // scroll, which draws only the entered terrain parts; a plain retain
   // redrew the whole layer (C4 every scheduled frame while standing).
   const bool held=scrolled && !scroll.dx && !scroll.dy &&
    same_validity(old.static_validity,next.static_validity);
   if(moved && !held)witness(scrolled?ShadowRendererDifference::map_scrolled:
    ShadowRendererDifference::scroll_rejected);
   if(scrolled && !held){scroll_mask|=bit;native_shadow_scroll_[index]=scroll;}
   const bool retained=held||far_retained;
   if(deferred)deferred_mask|=bit;
   else if(numeric_same||retained)decision.validity_compared_mask|=bit;
   if(retained){retained_mask|=bit;if(far_retained)witness(ShadowRendererDifference::far_retained_refresh);}
   const bool projection_same=numeric_same||retained;
   const bool static_same=deferred||(projection_same&&neighbour_same&&same_validity(old.static_validity,next.static_validity));
   bool same=deferred||(projection_same&&static_same&&same_validity(old.validity,next.validity));
   if(((animate>>index)&1u)&&index<3&&next.enabled&&!deferred)same=false;
   if(next.enabled&&(!static_same||!native_shadow_static_valid_[index])){
    static_dirty|=bit;
    if(!scrolled)witness(index>=3?ShadowRendererDifference::far_static_redraw:
     ShadowRendererDifference::close_static_redraw);
   }
   // Far static redraws are staggered (L-081): a far map whose static layer
   // must be redrawn waits while a close map redraws its whole static layer
   // or the other far map redraws in this frame. It waits like a deferred
   // map: valid, drawn in its own previous projection with its previous
   // casters, so the ground keeps a consistent (one or two frames older)
   // shadow and never a half-moved one. C3 waits at most one frame, C4 two,
   // so no far map is ever more than two frames behind.
   if(index>=3){
    auto& waits=native_shadow_far_waits_[index-3];
    const bool heavy=next.enabled&&!same&&(static_dirty&bit);
    const UINT redrawn=static_dirty&dirty_mask;
    const bool busy=(redrawn&~UINT(scroll_mask)&7u) || (index==4&&(redrawn&8u)) ||
     (index==3&&native_shadow_far_waits_[1]>=2);
    if(heavy&&busy&&waits<(index==3?1u:2u)&&comparable&&frame.world_epoch&&
       native_shadow_static_valid_[index]&&far_shadow_view_continuous(drawn_camera,next.camera_slice)){
     ++waits;witness(ShadowRendererDifference::far_staggered);
     native_shadow_drawn_camera_[index]=drawn_camera;
     static_dirty&=static_cast<std::uint8_t>(~bit);scroll_mask&=static_cast<std::uint8_t>(~bit);
     retained_mask&=static_cast<std::uint8_t>(~bit);deferred_mask|=bit;
     same=true;
    }else waits=0;
   }
   if(projection_same&&!same){decision.validity_changed_mask|=bit;witness(ShadowRendererDifference::validity);}
   if(next.enabled&&!same)dirty_mask|=static_cast<std::uint8_t>(1u<<index);
   if(same)++native_shadow_reused_slices_;else native_shadow_valid_[index]=false;
  }
  for(UINT index=0;index<5;++index){
   const auto bit=1u<<index;
   if(scroll_mask&bit){
    // A scrolled map keeps the depth row its cached static layer was drawn
    // with, so the shifted depth values stay exact.
    auto& slice=native_shadow_frame_.slices[index];
    const std::array<float,4> depth_row{slice.world_to_light[8],slice.world_to_light[9],
     slice.world_to_light[10],slice.world_to_light[11]};
    const float inverse_depth=slice.bias[1];
    slice=frame.slices[index];
    std::copy(depth_row.begin(),depth_row.end(),slice.world_to_light.begin()+8);
    slice.bias[1]=inverse_depth;
   }else if(!((deferred_mask|retained_mask)&bit))native_shadow_frame_.slices[index]=frame.slices[index];
   else{
    // Cascade selection follows the current camera while depth continues to
    // use the projection under which the retained map was drawn.
    auto& retained=native_shadow_frame_.slices[index];
    retained.camera_slice=frame.slices[index].camera_slice;
    retained.bias[2]=frame.slices[index].bias[2];
    retained.bias[3]=frame.slices[index].bias[3];
    // A retained refresh draws the current casters; later comparisons
    // start from that content.
    if(retained_mask&bit)retained.validity=frame.slices[index].validity;
   }
  }
  native_shadow_frame_.world_epoch=frame.world_epoch;
  // A deferred slice still has older validity, and a retained slice keeps an
  // older projection. Do not let the immutable-value fast path postpone a
  // scheduled refresh indefinitely.
  native_shadow_frame_.publication_serial=(deferred_mask|retained_mask|scroll_mask)?0:frame.publication_serial;
  ++native_shadow_frame_copies_;
  ++native_frame_stats_.native_shadow_frame_copies;
  native_frame_stats_.native_shadow_dirty_mask|=dirty_mask;
  native_shadow_dirty_mask_=dirty_mask;native_shadow_cleared_mask_=0;native_shadow_extras_mask_=0;
  // The bridge offers every casting part to a slice whose projection is not
  // the newly fitted one and culls it against the reported projection.
  native_shadow_retained_mask_=retained_mask|scroll_mask;native_shadow_scroll_mask_=scroll_mask;
  native_shadow_full_dirty_mask_=dirty_mask;native_shadow_static_dirty_mask_=static_dirty&dirty_mask;
  native_shadow_range_mask_&=std::uint8_t(~dirty_mask); // rebuilt once the maps are drawn
  native_shadow_static_cleared_mask_=0;native_shadow_phase_=0;
  native_shadow_failed_=false;native_shadow_collecting_=true;
  if(FAILED(upload_native_shadow_constants(0)))return reject();
  native_shadow_constants_dirty_=false;
  return true;
 }catch(...){return reject();}
}

bool Renderer::end_native_shadows(bool commit) noexcept {
 // The device's sink teardown aborts any open collection every EndScene; in
 // scene mode a collection is only ever open inside a scene-thread command,
 // so the abort is queued, never waited for.
 if(!commit&&scene_queue()){scene_post_command(scene_cmd::AbortDirectionalShadows{});return false;}
 SCENE_ENTRY("end_native_shadows");
 if(!native_shadow_collecting_)return false;
 HRESULT hr=E_FAIL;
 try{if(commit&&!native_shadow_failed_)hr=flush_native_shadows();else discard_native();}
 catch(...){discard_native();}
 auto& rollback=native_shadow_rollback_;
 if(!commit&&rollback.armed&&!native_shadow_cleared_mask_&&!native_shadow_static_cleared_mask_){
  // Aborted before any map was cleared or drawn: every map still holds the
  // content of the frame record begin replaced (see begin_native_shadows).
  native_shadow_frame_=rollback.frame;native_shadow_valid_=rollback.valid;native_shadow_drawn_camera_=rollback.drawn_camera;
  native_shadow_far_waits_=rollback.far_waits;native_shadow_range_mask_=rollback.range_mask;native_shadow_extras_key_=rollback.extras_key;
 }else if(FAILED(hr))for(UINT slice=0;slice<5;++slice)
  if(native_shadow_dirty_mask_&(1u<<slice))native_shadow_valid_[slice]=false;
 rollback.armed=false;
 native_shadow_collecting_=false;native_shadow_dirty_mask_=0;native_shadow_phase_=0;
 if(FAILED(upload_native_shadow_constants(0))){native_shadow_constants_dirty_=true;return false;}
 native_shadow_constants_dirty_=false;
 return SUCCEEDED(hr);
}

void Renderer::native_shadow_cull_projection(NativeShadowCullProjection& projection) const noexcept{SCENE_ENTRY("native_shadow_cull_projection");
 // The projection each map is actually drawn with this frame. A retained far
 // refresh keeps an older one than the published frame.
 for(UINT index=0;index<5;++index){
  projection.world_to_light[index]=native_shadow_frame_.slices[index].world_to_light;
  projection.scroll_copied[index]=native_shadow_scroll_[index].copied;
 }
 projection.retained_mask=native_shadow_retained_mask_;projection.scroll_mask=native_shadow_scroll_mask_;
 projection.side=native_shadow_side_;projection.scroll_entered=&native_shadow_entered_;
}

std::uint8_t Renderer::set_native_shadow_phase(unsigned phase) noexcept{SCENE_ENTRY("set_native_shadow_phase");
 if(!native_shadow_collecting_||native_point_shadow_active_||native_shadow_failed_)return 0;
 // Leaving the static layer draws it into the cache before the overlay.
 if(native_shadow_phase_==1&&phase!=1&&FAILED(flush_native_shadows()))return 0;
 native_shadow_phase_=phase;
 native_shadow_dirty_mask_=phase==1?native_shadow_static_dirty_mask_:native_shadow_full_dirty_mask_;
 return native_shadow_static_dirty_mask_;
}

HRESULT Renderer::flush_native_shadows(){
 if(d3d12_)return flush_native_shadows_d3d12();
 SceneRendererTimingScope scene_timing(
  SceneRendererTimingKind::native_flush);
 // The static sun caster bake draws from its own layout and work buffer
 // (slot 5) with nothing queued; everything else uses the shadow layout.
 auto* const active=native_point_shadow_active_?nullptr:native_shadow_bake_active_;
 const bool bake=active!=nullptr;
 auto& layout=native_submission_layouts_[bake?active->layout_slot:1];
 auto& native_draw_work_staging_=layout.work;auto& native_batch_staging_=layout.batches;
 auto& native_indirect_args_staging_=layout.arguments;
 if(!native_shadow_collecting_||native_shadow_failed_)return E_FAIL;
 if(bake&&(!native_pending_draws_.empty()||!layout.static_bake.work_srv))return E_FAIL;
 auto finish=[&](HRESULT hr){
  if(context_){
   ID3D11ShaderResourceView* empty[8]{};ID3D11UnorderedAccessView* uavs[4]{};
   context_->CSSetUnorderedAccessViews(0,4,uavs,nullptr);context_->CSSetShaderResources(1,8,empty);
   context_->VSSetShaderResources(1,8,empty);context_->PSSetShaderResources(0,1,empty);context_->PSSetShaderResources(6,1,empty);
   context_->CSSetShaderResources(10,5,empty);context_->VSSetShaderResources(10,1,empty);context_->VSSetShaderResources(12,2,empty);
   context_->CSSetShaderResources(15,2,empty);context_->CSSetUnorderedAccessViews(5,1,uavs,nullptr);context_->CSSetUnorderedAccessViews(6,1,uavs,nullptr);
   context_->CSSetShaderResources(35,1,empty);context_->VSSetShaderResources(36,1,empty);
   ID3D11Buffer* empty_constant=nullptr;context_->CSSetConstantBuffers(4,1,&empty_constant);
   context_->CSSetShader(nullptr,nullptr,0);context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());
   D3D11_VIEWPORT viewport=target_viewport();context_->RSSetViewports(1,&viewport);
   context_->VSSetShader(native_vs_.Get(),nullptr,0);context_->PSSetShader(native_ps_.Get(),nullptr,0);
  }
  if(FAILED(hr)){native_shadow_failed_=true;if(!native_point_shadow_active_&&!native_rain_active_)for(UINT slice=0;slice<5;++slice)if(native_shadow_dirty_mask_&(1u<<slice)){native_shadow_valid_[slice]=false;native_shadow_static_valid_[slice]=false;}}
  discard_native();end_profile_pass();return hr;
 };
 auto* depth_constants=native_rain_active_?native_rain_constants_.Get():native_point_shadow_active_?native_point_shadow_constants_.Get():native_shadow_constants_.Get();
 if(!context_||!depth_constants||!native_shadow_build_cs_)return native_flush_failure_line_=__LINE__,finish(E_FAIL);
#ifdef EID3D11_TESTING
 if(test_native_flush_failure_){test_native_flush_failure_=0;return native_flush_failure_line_=__LINE__,finish(E_FAIL);}
#endif
 if(!native_shadow_dirty_mask_)return native_flush_failure_line_=__LINE__,finish(S_OK);
 try{
  if(bake){
   // Work flags, preparations and per-slice batch totals were fixed when the
   // bake was built (rebuild_native_static_shadow_bake).
   begin_native_preparations();native_preparation_count_=active->preparations;
  }else{
   order_native_groups(true);
   const auto layout_result=prepare_native_submission_layout(true,1);
   if(FAILED(layout_result))return native_flush_failure_line_=__LINE__,finish(layout_result);
   if(native_draw_work_staging_.size()>UINT_MAX||native_batch_staging_.size()>UINT_MAX)
    return native_flush_failure_line_=__LINE__,finish(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
   refresh_native_draw_work(layout,true);
  }
  const UINT triangles=layout.triangles;
  const UINT records=static_cast<UINT>(native_draw_work_staging_.size()),batches=static_cast<UINT>(native_batch_staging_.size());
  HRESULT hr=S_OK;
  ID3D11ShaderResourceView* empty[8]{};ID3D11UnorderedAccessView* empty_uavs[4]{};
  context_->PSSetShaderResources(9,1,empty);{std::array<ID3D11ShaderResourceView*,PointShadowTextureLayout::array_bindings> vacant{};
   context_->PSSetShaderResources(PointShadowTextureLayout::first_srv,static_cast<UINT>(vacant.size()),vacant.data());}
  context_->VSSetShaderResources(1,8,empty);context_->CSSetShaderResources(1,8,empty);
  context_->CSSetUnorderedAccessViews(0,4,empty_uavs,nullptr);
  if(records){
   if(native_pending_upload_.size()>UINT_MAX)return native_flush_failure_line_=__LINE__,finish(E_OUTOFMEMORY);
   if(FAILED(hr=ensure_native_stream_buffers(bake?0u:static_cast<UINT>(native_pending_upload_.size()),records,static_cast<UINT>(triangles),0,batches)))return native_flush_failure_line_=__LINE__,finish(hr);
   auto upload=[&](ID3D11Buffer* buffer,const void* data,std::size_t bytes){D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT result=context_->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);if(FAILED(result))return result;
    std::memcpy(mapped.pData,data,bytes);context_->Unmap(buffer,0);return S_OK;};
   if(!bake){
    if(!native_pending_upload_.empty()&&FAILED(hr=upload(native_dynamic_.Get(),native_pending_upload_.data(),native_pending_upload_.size())))return native_flush_failure_line_=__LINE__,finish(hr);
    if(FAILED(hr=upload_native_terrain_phases()))return native_flush_failure_line_=__LINE__,finish(hr);
    if(FAILED(hr=upload(native_draw_work_.Get(),native_draw_work_staging_.data(),records*sizeof(NativeDrawWorkGpu))))return native_flush_failure_line_=__LINE__,finish(hr);
    profiler_.add_upload(native_pending_upload_.size()+records*sizeof(NativeDrawWorkGpu));
#ifdef EID3D11_TESTING
    test_native_work_upload_bytes_+=records*sizeof(NativeDrawWorkGpu);
#endif
   }
   if(native_world_lighting_dirty_){if(FAILED(hr=upload(native_world_lighting_constants_.Get(),&native_world_lighting_,sizeof(NativeWorldLighting))))return native_flush_failure_line_=__LINE__,finish(hr);
    native_world_lighting_dirty_=false;++native_world_lighting_uploads_;}
   if(FAILED(hr=upload_native_instances())||FAILED(hr=prepare_native_poses())||FAILED(hr=ensure_native_world_states()))return native_flush_failure_line_=__LINE__,finish(hr);
   context_->CSSetShaderResources(10,1,native_animation_srv_.GetAddressOf());
   context_->CSSetShaderResources(12,1,native_pose_node_srv_.GetAddressOf());
   context_->CSSetShaderResources(13,1,native_persistent_instance_srv_.GetAddressOf());
   context_->CSSetShaderResources(19,1,native_terrain_layer_shift_srv_.GetAddressOf());
   context_->CSSetShaderResources(16,1,bake?layout.static_bake.work_srv.GetAddressOf():native_draw_work_srv_.GetAddressOf());
   if(bake&&active->masked)context_->CSSetShaderResources(35,1,native_shadow_work_mask_srv_.GetAddressOf());
   context_->CSSetConstantBuffers(3,1,native_world_clip_constants_.GetAddressOf());
   ID3D11ShaderResourceView* inputs[5]={nullptr,native_dynamic_srv_.Get(),nullptr,nullptr,native_draw_srv_.Get()};
   context_->CSSetShaderResources(1,5,inputs);
   context_->CSSetConstantBuffers(2,1,&depth_constants);
   if(native_preparation_count_){
    context_->CSSetUnorderedAccessViews(6,1,native_world_state_uav_.GetAddressOf(),nullptr);
    context_->CSSetShader(native_shadow_prepare_cs_.Get(),nullptr,0);
    context_->CSSetUnorderedAccessViews(3,1,native_world_vertex_uav_.GetAddressOf(),nullptr);
    // Object motion: rewritten world vertices keep their old contents (u5, the b4 stamp); unbound after the dispatches (t46 reads it).
    ID3D11UnorderedAccessView* previous_world_uav=dlss_previous_world_uav();context_->CSSetUnorderedAccessViews(5,1,&previous_world_uav,nullptr);
    const UINT motion_stamp=previous_world_uav?dlss_motion_stamp_:0u;
    for(std::size_t dispatch=0;dispatch<layout.dispatches.size();++dispatch){
     const auto& range=layout.dispatches[dispatch];if(!range.preparations)continue;
     inputs[0]=native_geometry_page_srv(range.geometry_page);if(!inputs[0])return native_flush_failure_line_=__LINE__,finish(E_FAIL);
     context_->CSSetShaderResources(1,5,inputs);
     if(FAILED(hr=upload_native_work_range(range,motion_stamp)))return native_flush_failure_line_=__LINE__,finish(hr);
     UINT width=0,height=0;if(!native_dispatch_dimensions(range.count,width,height))return native_flush_failure_line_=__LINE__,finish(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
     const UINT chunks=bake?active->dispatch_chunks[dispatch]:native_shadow_chunks(layout,range,false);
     transition_profile_pass("compute_shadow_prepare");context_->Dispatch(width,height,chunks);++perf_native_compute_dispatches_;++native_frame_stats_.native_compute_dispatches;
     context_->CSSetShader(native_shadow_commit_cs_.Get(),nullptr,0);context_->Dispatch(width,height,1);
     context_->CSSetShader(native_shadow_prepare_cs_.Get(),nullptr,0);
    }
    context_->CSSetUnorderedAccessViews(3,1,empty_uavs,nullptr);context_->CSSetUnorderedAccessViews(5,1,empty_uavs,nullptr);
    context_->CSSetUnorderedAccessViews(6,1,empty_uavs,nullptr);
    if(bake){native_world_prepared_vertices_+=active->prepared_vertices;native_shadow_prepared_records_+=active->preparations;}
    else for(const auto& work:native_draw_work_staging_)if(work.record!=UINT_MAX&&(work.flags&2u)){native_world_prepared_vertices_+=native_instance_records_[work.record].command.component_count;++native_shadow_prepared_records_;}
   }
   context_->CSSetShaderResources(15,1,native_world_state_srv_.GetAddressOf());
   for(const auto& selected:native_pending_draws_){
    const auto& queued=native_instance_records_[selected.record].command;
    if(queued.world_slot>=native_prepared_mesh_slots_.size())continue;
    auto& slot=native_prepared_mesh_slots_[queued.world_slot];if(slot.world_generation==queued.world_generation&&slot.world_base==queued.world_base)slot.world_valid=true;
   }
   context_->IASetInputLayout(nullptr);context_->IASetVertexBuffers(0,0,nullptr,nullptr,nullptr);context_->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);
   context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
   context_->VSSetShader(native_shadow_vs_.Get(),nullptr,0);
   bind_draw_constants();
   context_->VSSetConstantBuffers(1,1,native_world_lighting_constants_.GetAddressOf());
   context_->VSSetConstantBuffers(2,1,&depth_constants);context_->CSSetConstantBuffers(2,1,&depth_constants);
    inputs[0]=nullptr;context_->VSSetShaderResources(1,3,inputs);context_->VSSetShaderResources(8,1,native_world_vertex_srv_.GetAddressOf());
   context_->VSSetShaderResources(10,1,native_animation_srv_.GetAddressOf());
   context_->VSSetShaderResources(12,1,native_pose_node_srv_.GetAddressOf());
   context_->VSSetShaderResources(13,1,native_persistent_instance_srv_.GetAddressOf());
   context_->VSSetShaderResources(19,1,native_terrain_layer_shift_srv_.GetAddressOf());
   // Units cast from where they are drawn: sunk into trail troughs (nativeFigureTrailSink).
   {ID3D11ShaderResourceView* snow=scene_composited_?nullptr:snow_terrain_srv_.Get();context_->VSSetShaderResources(36,1,&snow);}
   context_->VSSetConstantBuffers(3,1,native_world_clip_constants_.GetAddressOf());
  }
  const float shadow_side=float(native_rain_active_?native_rain_side_:native_point_shadow_active_?native_point_shadow_side_:native_shadow_side_);
  D3D11_VIEWPORT viewport{0,0,shadow_side,shadow_side,0,1};context_->RSSetViewports(1,&viewport);
  for(UINT slice=0;slice<(native_rain_active_?1u:native_point_shadow_active_?6u:5u);++slice){
   if(!(native_shadow_dirty_mask_&(1u<<slice)))continue;
   const bool static_layer=!native_point_shadow_active_&&!native_rain_active_&&native_shadow_phase_==1;
   // A point face draws into its atlas cell (renderer_point_shadows.cpp):
   // the cell's slice, the face inside its one-texel gutter.
   ID3D11DepthStencilView* depth=nullptr;D3D11_VIEWPORT cell{};
   if(native_point_shadow_active_){
    const UINT page=native_point_shadow_slot_/PointShadowTextureLayout::cubes_per_atlas;
    const UINT face=(native_point_shadow_slot_%PointShadowTextureLayout::cubes_per_atlas)*6u+slice;
    const UINT tile=face%PointShadowTextureLayout::faces_per_slice;const float pitch=shadow_side+2;
    depth=native_point_shadow_pages_[page].dsv[face/PointShadowTextureLayout::faces_per_slice].Get();
    cell={float(tile%PointShadowTextureLayout::atlas_tiles)*pitch,float(tile/PointShadowTextureLayout::atlas_tiles)*pitch,pitch,pitch,0,1};
    viewport={cell.TopLeftX+1,cell.TopLeftY+1,shadow_side,shadow_side,0,1};context_->RSSetViewports(1,&viewport);
   }else if(native_rain_active_)depth=native_rain_dsv_.Get();
   else depth=static_layer?native_shadow_static_dsv_[slice].Get():native_shadow_dsv_[slice].Get();
   if(static_layer){
    if(!(native_shadow_static_cleared_mask_&(1u<<slice))){
     // A scrolled close map starts from its shifted cached layer; only the
     // static casters reaching the exposed strip were queued for it.
     // A zero shift keeps the cache as it is (no copy round trip).
     if(native_shadow_scroll_mask_&(1u<<slice)){
      const auto& shift=native_shadow_scroll_[slice];
      if((shift.dx||shift.dy)&&FAILED(hr=scroll_native_static_shadow(slice)))return native_flush_failure_line_=__LINE__,finish(hr);
     }else context_->ClearDepthStencilView(depth,D3D11_CLEAR_DEPTH,1,0);
     native_shadow_static_cleared_mask_|=1u<<slice;
     native_shadow_static_valid_[slice]=false;
    }
   }else if(!(native_shadow_cleared_mask_&(1u<<slice))){
    // The dynamic overlay starts from the cached static layer.
    if(!native_point_shadow_active_&&!native_rain_active_&&native_shadow_phase_==2&&native_shadow_static_valid_[slice]){
     context_->OMSetRenderTargets(0,nullptr,nullptr);
     context_->CopySubresourceRegion(native_shadow_depth_.Get(),D3D11CalcSubresource(0,slice,1),0,0,0,
      native_shadow_static_depth_.Get(),D3D11CalcSubresource(0,slice,1),nullptr);
    }else if(native_point_shadow_active_){
     // Only this face's cell (face and gutter) of the shared slice: depth 1
     // through a full-cell triangle (a view clear would wipe the slice).
     context_->OMSetRenderTargets(0,nullptr,depth);context_->RSSetViewports(1,&cell);
     context_->RSSetState(native_point_clear_raster_.Get());context_->OMSetDepthStencilState(clear_depth_states_[0].Get(),0);
     context_->VSSetShader(native_depth_clear_vs_.Get(),nullptr,0);context_->PSSetShader(nullptr,nullptr,0);
     context_->Draw(3,0);
     context_->VSSetShader(native_shadow_vs_.Get(),nullptr,0);context_->RSSetViewports(1,&viewport);
    }else{
     context_->ClearDepthStencilView(depth,D3D11_CLEAR_DEPTH,1,0);
     if(native_rain_active_){const float none[4]{};context_->ClearRenderTargetView(native_rain_foliage_rtv_.Get(),none);}
    }
    native_shadow_cleared_mask_|=1u<<slice;
   }
   // The shelter bake also writes its foliage mask (nativeShadowPS colour).
   if(native_rain_active_)context_->OMSetRenderTargets(1,native_rain_foliage_rtv_.GetAddressOf(),depth);
   else context_->OMSetRenderTargets(0,nullptr,depth);
   if(records){
    if(FAILED(hr=upload_native_shadow_constants(slice)))return native_flush_failure_line_=__LINE__,finish(hr);
    D3D11_BOX args{};args.right=batches*16;args.bottom=args.back=1;
    context_->VSSetShaderResources(4,1,empty);
    context_->UpdateSubresource(native_indirect_args_.Get(),0,&args,native_indirect_args_staging_.data(),0,0);
    ID3D11UnorderedAccessView* outputs[2]={native_visible_triangle_uav_.Get(),native_indirect_args_uav_.Get()};
    context_->CSSetUnorderedAccessViews(0,2,outputs,nullptr);context_->CSSetShader(native_shadow_build_cs_.Get(),nullptr,0);
    for(std::size_t dispatch=0;dispatch<layout.geometry_dispatches.size();++dispatch){
     const auto& range=layout.geometry_dispatches[dispatch];
     ID3D11ShaderResourceView* geometry=native_geometry_page_srv(range.geometry_page);if(!geometry)return native_flush_failure_line_=__LINE__,finish(E_FAIL);
     context_->CSSetShaderResources(1,1,&geometry);
     if(FAILED(hr=upload_native_work_range(range)))return native_flush_failure_line_=__LINE__,finish(hr);
     UINT width=0,height=0;if(!native_dispatch_dimensions(range.count,width,height))return native_flush_failure_line_=__LINE__,finish(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
     const UINT chunks=bake?active->geometry_chunks[dispatch]:native_shadow_chunks(layout,range,true);
     transition_profile_pass("compute_shadow_build");context_->Dispatch(width,height,chunks);++perf_native_compute_dispatches_;++native_frame_stats_.native_compute_dispatches;
    }
    context_->CSSetUnorderedAccessViews(0,2,empty_uavs,nullptr);context_->VSSetShaderResources(4,1,native_visible_triangle_srv_.GetAddressOf());
    for(UINT group=0;group<batches;++group){
     const auto& batch=native_batch_staging_[group];
     const auto group_triangles=batch.slice_triangles[slice];const auto group_draw_count=batch.slice_records[slice];
     if(!group_triangles)continue; // CPU-known empty means no indirect call.
     ID3D11ShaderResourceView* geometry=native_geometry_page_srv(batch.geometry_page);if(!geometry)return native_flush_failure_line_=__LINE__,finish(E_FAIL);
     context_->VSSetShaderResources(1,1,&geometry);
     const auto& bucket=native_state_buckets_[batch.state_bucket];FixedState state=bucket.state;
     state.render[D3DRENDERSTATE_ZENABLE]=D3DZB_TRUE;state.render[D3DRENDERSTATE_ZWRITEENABLE]=TRUE;state.render[D3DRENDERSTATE_ZFUNC]=D3DCMP_LESSEQUAL;
     state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=FALSE;
     // A cut-out caster casts its two-sided silhouette (native_material_state).
     // Records queued as colour records keep the game's cull mode in their
     // bucket: the active map's object-space casters and the neighbour
     // decorations. One-sided, a fir's drooping branch cards turned away from
     // the sun dropped out and its shadow was the trunk with a few arms
     // (JMV zone11, save1006).
     if(bucket.opacity==NativeOpacityClass::cutout)state.render[D3DRENDERSTATE_CULLMODE]=D3DCULL_NONE;
     if(shadow_foliage_opaque(native_far_foliage_opaque_,native_point_shadow_active_,slice,
        batch.foliage?native_foliage_part_flag:0u,state.render[D3DRENDERSTATE_ALPHATESTENABLE]!=0))
      state.render[D3DRENDERSTATE_ALPHATESTENABLE]=FALSE;
     // The shelter bake looks straight down, where steep crown cards are
     // foreshortened: their alpha test sampled a coarse mip whose averaged
     // alpha fell under the game's reference, so the cards dropped out
     // (c6c05f03 debug view: no shelter under pines). Whole cards (5a63018f)
     // replaced that with every card's full quad: a pine's dry patch became
     // the union of its card rectangles, a hard-edged polygon about twice the
     // foliage (bfa1e170 F11, rain). Foliage in the shelter bake is alpha
     // tested at a low fixed reference instead, its mip taken along the
     // card's less foreshortened axis (nativeShadowPS, nativeContactLayout.z),
     // so only texels that hold leaves cover the ground.
     const bool shelter_foliage=native_rain_active_&&batch.foliage&&bucket.texture_srv;
     if(shelter_foliage){
      state.render[D3DRENDERSTATE_ALPHATESTENABLE]=TRUE;
      state.render[D3DRENDERSTATE_ALPHAFUNC]=D3DCMP_GREATER;state.render[D3DRENDERSTATE_ALPHAREF]=64; // alpha > 0.25
     }
     Constants constants{};constants.native_triangle_base=batch.output_triangle_base;
     // Foliage batches in the shelter bake: the textured cards' mip rule (the
     // foliage mask itself takes each draw's instance flag, nativeShadowPS).
     constants.layout_origin[2]=shelter_foliage?1.f:0.f;
     constants.textured=state.render[D3DRENDERSTATE_ALPHATESTENABLE]&&bucket.texture_srv;
     constants.alpha_test=state.render[D3DRENDERSTATE_ALPHATESTENABLE]!=0;constants.alpha_func=state.render[D3DRENDERSTATE_ALPHAFUNC];
     constants.alpha_ref=(state.render[D3DRENDERSTATE_ALPHAREF]&255)/256.f;
     constants.alpha_op=state.stage[0][D3DTSS_ALPHAOP];constants.alpha_arg1=state.stage[0][D3DTSS_ALPHAARG1];constants.alpha_arg2=state.stage[0][D3DTSS_ALPHAARG2];
     constants.texture_factor[3]=(state.render[D3DRENDERSTATE_TEXTUREFACTOR]>>24)/255.f;
     if(FAILED(hr=upload_draw_constants(&constants,sizeof(constants))))return native_flush_failure_line_=__LINE__,finish(hr);
     // Every shelter caster runs the pixel stage, so the mask holds the top
     // surface's class (a roof over a crown is solid).
     apply_state(state,constants.textured!=0);context_->PSSetShader(constants.alpha_test||native_rain_active_?native_shadow_ps_.Get():nullptr,nullptr,0);
     // Terrain sun casters (L-084): the terrain's cull mode leaves only its
     // faces turned away from the sun in the sun maps, so a ground pixel on
     // such a face (a pit wall the smooth lighting normal hides) is compared
     // with its own depth at grazing incidence: noise. Slope-scaled bias on
     // terrain casters only pushes that depth back by a couple of texels of
     // its own slope (clamped in world depth); figures and objects stay
     // unbiased, so low casters keep their shadows. Point cubes (L-085)
     // compare raw depth texels without a receiver plane, so terrain turning
     // edge-on to a lamp or lava light aliased into stripes; their terrain
     // casters take the same slope bias, unclamped (cube depth is not linear
     // in distance).
     // Neighbour-map casters are pancaked: the bridge fits each sun map's
     // depth range to the active map's casters and receivers, so a neighbour
     // crown upstream of it would be clipped. Without depth clipping it lands
     // on the near plane (depth 0) and still occludes every receiver behind
     // it along the sun; only its own depth is lost, which no receiver needs.
     if(active==&native_neighbour_shadow_bake_){
      const DWORD cull=state.render[D3DRENDERSTATE_CULLMODE];
      auto& pancake=raster_states_[0x20000u|(cull==D3DCULL_NONE?0u:cull==D3DCULL_CW?1u:2u)];
      if(!pancake){
       D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;
       rd.CullMode=cull==D3DCULL_NONE?D3D11_CULL_NONE:D3D11_CULL_BACK;rd.FrontCounterClockwise=cull==D3DCULL_CW;
       rd.DepthClipEnable=FALSE;
       if(FAILED(hr=device_->CreateRasterizerState(&rd,&pancake)))return native_flush_failure_line_=__LINE__,finish(hr);
      }
      context_->RSSetState(pancake.Get());
     }
     if(bucket.semantic_kind==SemanticKind::terrain){
      constexpr float terrain_slope_texels=2.f,terrain_clamp_world=.5f;
      const DWORD cull=state.render[D3DRENDERSTATE_CULLMODE];
      const float clamp=native_point_shadow_active_?0.f:
       terrain_clamp_world*native_shadow_frame_.slices[slice].bias[1];
      std::uint32_t clamp_bits=0;std::memcpy(&clamp_bits,&clamp,sizeof(clamp_bits));
      const std::uint64_t key=(std::uint64_t(clamp_bits)<<32)|0x10000u|(cull==D3DCULL_NONE?0u:cull==D3DCULL_CW?1u:2u);
      auto& terrain_raster=raster_states_[key];
      if(!terrain_raster){
       D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;
       rd.CullMode=cull==D3DCULL_NONE?D3D11_CULL_NONE:D3D11_CULL_BACK;rd.FrontCounterClockwise=cull==D3DCULL_CW;
       rd.DepthClipEnable=TRUE;rd.SlopeScaledDepthBias=terrain_slope_texels;rd.DepthBiasClamp=clamp;
       if(FAILED(hr=device_->CreateRasterizerState(&rd,&terrain_raster)))return native_flush_failure_line_=__LINE__,finish(hr);
      }
      context_->RSSetState(terrain_raster.Get());
     }
     ID3D11ShaderResourceView* texture=constants.textured&&!bucket.texture_array?bucket.texture_srv.Get():nullptr;
     ID3D11ShaderResourceView* array=constants.textured&&bucket.texture_array?bucket.texture_srv.Get():nullptr;
     context_->PSSetShaderResources(0,1,&texture);context_->PSSetShaderResources(6,1,&array);
     {// Temporary per-cascade split (optimization pass): which sun map owns the cost.
      static const char* const cascades[5]={"owned_directional_shadow_c0","owned_directional_shadow_c1","owned_directional_shadow_c2","owned_directional_shadow_c3","owned_directional_shadow_c4"};
      transition_profile_pass(native_rain_active_?"precip_shelter":native_point_shadow_active_?"owned_point_shadow":cascades[slice]);
      if(profiler_.pass_active()){ // profiling only: caster classes inside the cascade
       static const char* const shadow_scopes[3][3]={{"dynamic_terrain","dynamic_opaque","dynamic_cutout"},{"static_terrain","static_opaque","static_cutout"},
        {"neighbour_terrain","neighbour_opaque","neighbour_cutout"}};
       profiler_.sub_scope(shadow_scopes[active==&native_neighbour_shadow_bake_?2:static_layer?1:0][bucket.semantic_kind==SemanticKind::terrain?0:constants.alpha_test?2:1]);
       profiler_.add_sub_draw(group_draw_count,group_triangles);
      }}context_->DrawInstancedIndirect(native_indirect_args_.Get(),group*16);
     ++native_shadow_draws_;native_shadow_triangles_+=group_triangles;
     ++native_frame_stats_.native_shadow_draws;native_frame_stats_.native_shadow_triangles+=group_triangles;
     if(bucket.opacity==NativeOpacityClass::cutout&&
        bucket.state.render[D3DRENDERSTATE_CULLMODE]==D3DCULL_NONE){
      ++native_frame_stats_.native_shadow_two_sided_cutout_draws;
      native_frame_stats_.native_shadow_two_sided_cutout_selected_records+=group_draw_count;
     }
     perf_native_draws_+=group_draw_count;++perf_native_batches_;perf_native_vertices_+=group_triangles*3;
     ++native_frame_stats_.native_physical_batches;if(group_draw_count==1)++native_frame_stats_.native_one_draw_batches;
     native_frame_stats_.native_max_batch_draws=std::max<std::uint64_t>(native_frame_stats_.native_max_batch_draws,group_draw_count);
     profiler_.add_draw(group_triangles,group_draw_count);profiler_.add_batch();
    }
   }
   // Renderer-owned casters placed on the GPU or per frame (ground cover,
   // critters) join the close maps' per-frame layer, once per collection,
   // through the same map, viewport and slice constants as the records.
   if(!bake&&!native_point_shadow_active_&&!native_rain_active_&&!static_layer&&slice<3&&
      !(native_shadow_extras_mask_&(1u<<slice))){
    native_shadow_extras_mask_|=std::uint8_t(1u<<slice);
    if(FAILED(hr=upload_native_shadow_constants(slice)))return native_flush_failure_line_=__LINE__,finish(hr);
    // Their own profiler pass: the first flush of the per-frame layer (the
    // batch begin) has no records and so no cascade pass open. Children
    // ground_cover_large|ground_cover_all and critter_shadows split it.
    transition_profile_pass("owned_shadow_casters");
    context_->VSSetConstantBuffers(2,1,&depth_constants);
    draw_ground_cover_shadows(slice);draw_critter_shadows(slice);
    // The records of the next map expect their own program and no index buffer.
    context_->VSSetShader(native_shadow_vs_.Get(),nullptr,0);context_->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);
    if(records){ID3D11ShaderResourceView* none[3]{};context_->VSSetShaderResources(1,3,none);}
   }
   if(static_layer){native_shadow_static_valid_[slice]=true;native_shadow_neighbour_drawn_[slice]=object_caster_key();}
   else if(!native_point_shadow_active_&&!native_rain_active_)native_shadow_valid_[slice]=true;
  }
  return native_flush_failure_line_=__LINE__,finish(S_OK);
 }catch(const std::bad_alloc&){return native_flush_failure_line_=__LINE__,finish(E_OUTOFMEMORY);}
}

// Shifts the cached static layer of a close map by whole texels (the drawn
// square moved; L-077). Partial copies of depth resources are not allowed,
// so the slice goes to the shadow map whole (its dynamic overlay is rebuilt
// from the cache afterwards) and is drawn back shifted into the cleared cache.
HRESULT Renderer::scroll_native_static_shadow(UINT slice) noexcept{
 if(!shadow_scroll_vs_||!shadow_scroll_ps_||!shadow_scroll_constants_||!shadow_scroll_raster_||
    !clear_depth_states_[0]||!native_shadow_depth_srv_||slice>=5)return E_FAIL;
 const auto& scroll=native_shadow_scroll_[slice];
 const UINT subresource=D3D11CalcSubresource(0,slice,1);
 context_->OMSetRenderTargets(0,nullptr,nullptr);
 context_->CopySubresourceRegion(native_shadow_depth_.Get(),subresource,0,0,0,
  native_shadow_static_depth_.Get(),subresource,nullptr);
 ID3D11DepthStencilView* cache=native_shadow_static_dsv_[slice].Get();
 context_->ClearDepthStencilView(cache,D3D11_CLEAR_DEPTH,1,0);
 const int values[4]{scroll.dx,scroll.dy,static_cast<int>(slice),static_cast<int>(native_shadow_side_)};
 D3D11_MAPPED_SUBRESOURCE mapped{};HRESULT hr=context_->Map(shadow_scroll_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
 if(FAILED(hr))return hr;
 std::memcpy(mapped.pData,values,sizeof(values));context_->Unmap(shadow_scroll_constants_.Get(),0);
 context_->OMSetRenderTargets(0,nullptr,cache);
 context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 context_->VSSetShader(shadow_scroll_vs_.Get(),nullptr,0);context_->PSSetShader(shadow_scroll_ps_.Get(),nullptr,0);
 context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
 ID3D11Buffer* constants=shadow_scroll_constants_.Get();context_->PSSetConstantBuffers(7,1,&constants);
 ID3D11ShaderResourceView* source=native_shadow_depth_srv_.Get();context_->PSSetShaderResources(0,1,&source);
 context_->RSSetState(shadow_scroll_raster_.Get());context_->OMSetDepthStencilState(clear_depth_states_[0].Get(),0);
 context_->OMSetBlendState(nullptr,nullptr,0xffffffffu);
 const float side=float(native_shadow_side_);D3D11_VIEWPORT viewport{0,0,side,side,0,1};context_->RSSetViewports(1,&viewport);
 context_->Draw(3,0);
 ID3D11ShaderResourceView* empty=nullptr;context_->PSSetShaderResources(0,1,&empty);
 ID3D11Buffer* no_constants=nullptr;context_->PSSetConstantBuffers(7,1,&no_constants);
 // The depth batches that follow bind their own state; restore their shader.
 context_->VSSetShader(native_shadow_vs_.Get(),nullptr,0);
 return S_OK;
}

// Static depth records queued for a scrolled map are only those that reach
// its exposed strip (shadow_scroll_strip_reached), and terrain parts that
// entered the map's list this frame. Casters wholly outside the new square
// stay the GPU's to cull; queueing them kept all 8060 records per pan frame.
UINT Renderer::native_static_scroll_mask(const NativeWorldBounds& bounds,UINT mask,
 const NativeShadowCasterRevision* terrain) const noexcept{
 if(native_rain_active_)return mask;  // the shelter map never scrolls
 const UINT scrolled=mask&native_shadow_scroll_mask_;
 if(!scrolled||!bounds.valid)return mask;
 const std::uint64_t key=terrain&&terrain->terrain?
  (std::uint64_t(std::uint32_t(terrain->signed_id))<<32)|terrain->part:0;
 for(UINT slice=0;slice<5;++slice){
  if(!(scrolled&(1u<<slice)))continue;
  if(key&&std::binary_search(native_shadow_entered_[slice].begin(),native_shadow_entered_[slice].end(),key))continue;
  const auto& light=native_shadow_frame_.slices[slice].world_to_light;
  double low[2]{INFINITY,INFINITY},high[2]{-INFINITY,-INFINITY};
  for(unsigned corner=0;corner<8;++corner){
   const float point[3]{(corner&1u)?bounds.maximum[0]:bounds.minimum[0],
    (corner&2u)?bounds.maximum[1]:bounds.minimum[1],(corner&4u)?bounds.maximum[2]:bounds.minimum[2]};
   for(unsigned axis=0;axis<2;++axis){
    double value=light[axis*4+3];
    for(unsigned component=0;component<3;++component)value+=double(light[axis*4+component])*point[component];
    low[axis]=(std::min)(low[axis],value);high[axis]=(std::max)(high[axis],value);
   }
  }
  if(!shadow_scroll_strip_reached(low,high,native_shadow_scroll_[slice].copied,native_shadow_side_))
   mask&=~(1u<<slice);
 }
 return mask;
}

// A persistent static caster is queued only for the static layers whose
// square its world bounds reach in the projection the layer is drawn with
// (L-081). The GPU build culls the remaining triangles; queueing every store
// caster for every redrawn layer validated, ordered and uploaded the whole
// store (5k records) even for the small close squares.
UINT Renderer::native_static_shadow_reach(const NativeWorldBounds& bounds,UINT mask) const noexcept{
 if(!mask||!bounds.valid||native_point_shadow_active_)return mask;
 double center[3],extent[3];
 for(unsigned axis=0;axis<3;++axis){
  center[axis]=(double(bounds.minimum[axis])+bounds.maximum[axis])*.5;
  extent[axis]=(double(bounds.maximum[axis])-bounds.minimum[axis])*.5;
 }
 for(UINT slice=0;slice<5;++slice){
  if(!(mask&(1u<<slice)))continue;
  const auto& light=native_rain_active_?native_rain_slice_.world_to_light:native_shadow_frame_.slices[slice].world_to_light;
  for(unsigned axis=0;axis<2;++axis){
   double value=light[axis*4+3],reach=0;
   for(unsigned component=0;component<3;++component){
    value+=double(light[axis*4+component])*center[component];
    reach+=std::abs(double(light[axis*4+component]))*extent[component];
   }
   // A small guard keeps rounding at the square's edge on the drawn side.
   if(value-reach>1.0001||value+reach< -1.0001){mask&=~(1u<<slice);break;}
  }
 }
 return mask;
}

// A stored static depth record still draws its caster exactly: same caster
// revision, live record, mesh, material and world range at the generations
// it was registered with, and current pose and animation values.
bool Renderer::native_static_shadow_record_current(const NativeStaticShadowRecord& stored,
 const NativeShadowCasterRevision& revision) const noexcept{
 if(stored.revision!=revision||stored.instance>=native_instance_records_.size()||
    stored.mesh_slot>=native_prepared_mesh_slots_.size()||
    stored.material_slot>=native_prepared_material_slots_.size())return false;
 const auto& record=native_instance_records_[stored.instance];
 const auto& mesh=native_prepared_mesh_slots_[stored.mesh_slot];
 const auto& material=native_prepared_material_slots_[stored.material_slot];
 bool current=record.occupied&&record.owned&&!record.owner.expired()&&
  record.depth_current&&record.depth_revision==revision&&
  record.source_revision&&mesh.active&&mesh.generation==stored.mesh_generation&&
  mesh.source_revision==record.source_revision&&!mesh.owner.expired()&&mesh.mesh&&
  material.active&&material.generation==stored.material_generation&&
  !material.owner.expired()&&material.persistent_instance[1]==stored.instance&&
  record.command.world_slot==stored.mesh_slot&&
  record.command.world_generation==mesh.world_generation&&
  record.command.world_base==mesh.world_base&&
  record.command.world_allocation==mesh.world_allocation&&
  record.command.world_allocation<native_world_ranges_.size()&&
  native_world_ranges_[record.command.world_allocation].owned&&
  native_world_ranges_[record.command.world_allocation].generation==
   record.depth_world_generation;
 if(current&&record.command.pose_slot!=UINT_MAX){
  current=record.command.pose_slot<native_pose_slots_.size();
  if(current){
   const auto& pose=native_pose_slots_[record.command.pose_slot];
   current=pose.active&&pose.generation==record.depth_pose_generation&&
    pose.revision==record.depth_pose_revision;
  }
 }
 if(current&&record.command.animation_allocation!=UINT_MAX){
  current=record.command.animation_allocation<native_animation_ranges_.size();
  if(current){
   const auto& animation=native_animation_ranges_[record.command.animation_allocation];
   current=(animation.owned||animation.owners)&&
    animation.generation==record.depth_animation_generation;
  }
 }
 return current;
}

// A bake is drawable while the in-place record content and every record
// owner are unchanged. Records it references are never rewritten in place (a
// referenced record is copied on write), as for the static colour set.
// Casters registered since the build draw through the depth path; after 128
// new or changed registrations, or 512 switched-off entries, the bake is
// rebuilt to fold them in. The store bake also follows its store revision.
bool Renderer::native_shadow_bake_current(const NativeStaticShadowBake& bake,std::uint64_t records_serial) const noexcept{
 if(!bake.built||bake.built_content_revision!=native_static_content_revision_||
    records_serial-bake.built_records_serial>128||bake.excluded>512||
    bake.far_foliage_opaque!=native_far_foliage_opaque_)return false;
 for(std::size_t owner=0;owner<bake.owners.size();++owner)
  if(owner<bake.owner_live.size()&&bake.owner_live[owner]&&bake.owners[owner].expired())return false;
 return true;
}

void Renderer::release_native_shadow_bake(NativeStaticShadowBake& bake) noexcept{
 for(const auto& selected:bake.records)
  if(selected.record<native_instance_records_.size())release_native_queued_draw(selected.record,false);
 bake.records.clear();bake.owners.clear();bake.mesh_slots.clear();bake.material_slots.clear();
 bake.entries.clear();bake.excluded=0;bake.owner_of.clear();bake.owner_live.clear();
 bake.dispatch_chunks.clear();bake.geometry_chunks.clear();
 bake.preparations=0;bake.prepared_vertices=0;bake.built=false;
}

// Membership walk, references, layout and work upload: run only when the
// members, a record or the record content changed, never per redraw.
// absent receives the indices of members without a current record. With
// sweep, members are the whole caster list and records of others go.
HRESULT Renderer::rebuild_native_shadow_bake(NativeStaticShadowBake& bake,
 std::unordered_map<std::uint64_t,NativeStaticShadowRecord>& stored,
 const std::vector<std::pair<std::uint64_t,NativeShadowCasterRevision>>& members,
 std::vector<std::size_t>& absent,bool sweep,std::uint64_t records_serial) noexcept try{
 release_native_shadow_bake(bake);
 absent.clear();
 std::vector<std::uint64_t> kept;kept.reserve(members.size());
 bake.records.reserve(members.size());
 for(std::size_t member=0;member<members.size();++member){
  const auto key=members[member].first;const auto& revision=members[member].second;
  const auto found=stored.find(key);
  if(found==stored.end()){absent.push_back(member);continue;}
  if(!native_static_shadow_record_current(found->second,revision)){
   stored.erase(found);absent.push_back(member);continue;
  }
  // One baked record per key (terrain near and far details can share one).
  if(!kept.empty()&&std::find(kept.end()-(std::min<std::size_t>)(kept.size(),2),kept.end(),key)!=kept.end())continue;
  kept.push_back(key);
  bake.entries.push_back({key,revision,UINT_MAX,static_cast<UINT>(bake.records.size())});
  bake.records.push_back({found->second.instance,0x1fu<<8});
  bake.mesh_slots.push_back(found->second.mesh_slot);
  bake.material_slots.push_back(found->second.material_slot);
 }
 if(sweep){
  std::sort(kept.begin(),kept.end());
  for(auto it=stored.begin();it!=stored.end();)
   if(!std::binary_search(kept.begin(),kept.end(),it->first))it=stored.erase(it);
   else ++it;
 }
 if(const HRESULT hr=finish_native_shadow_bake(bake);FAILED(hr))return hr;
 bake.built_content_revision=native_static_content_revision_;
 bake.built_records_serial=records_serial;
 bake.far_foliage_opaque=native_far_foliage_opaque_;
 bake.built=true;
 return S_OK;
}catch(...){release_native_shadow_bake(bake);return E_OUTOFMEMORY;}

// Sorts the bake's slots, references its records and builds its layout and
// work (shared by the store, terrain and neighbour bakes).
HRESULT Renderer::finish_native_shadow_bake(NativeStaticShadowBake& bake) noexcept try{
 auto& layout=native_submission_layouts_[bake.layout_slot];
 for(auto* slots:{&bake.mesh_slots,&bake.material_slots}){
  std::sort(slots->begin(),slots->end());slots->erase(std::unique(slots->begin(),slots->end()),slots->end());
 }
 // Record and range references, as the static colour set keeps them (no
 // per-frame mesh pin, L-046).
 for(const auto& selected:bake.records){
  auto& record=native_instance_records_[selected.record];auto& draw=record.command;
  ++record.references;
  if(draw.pose_allocation<native_pose_ranges_.size())++native_pose_ranges_[draw.pose_allocation].references;
  if(draw.animation_allocation<native_animation_ranges_.size())++native_animation_ranges_[draw.animation_allocation].references;
  if(draw.world_allocation<native_world_ranges_.size())++native_world_ranges_[draw.world_allocation].references;
  if((!record.uploaded||!record.descriptor_uploaded)&&!record.scheduled){
   native_instance_dirty_.push_back(selected.record);record.scheduled=true;
  }
  const auto& owner=record.owner;
  if(bake.owners.empty()||bake.owners.back().owner_before(owner)||owner.owner_before(bake.owners.back())){
   bake.owners.push_back(owner);bake.owner_live.push_back(0);
  }
  bake.owner_of.push_back(static_cast<UINT>(bake.owners.size()-1));++bake.owner_live.back();
 }
 if(!bake.records.empty()){
  // The shared grouping and layout code reads the queue: give it exactly
  // the bake's records, every map marked.
  std::vector<NativeSelectedRecord> queued;queued.swap(native_pending_draws_);
  native_pending_draws_=bake.records;
  const auto dirty=native_shadow_dirty_mask_;native_shadow_dirty_mask_=0x1f;
  HRESULT hr=S_OK;
  try{order_native_groups(true);hr=prepare_native_submission_layout(true,bake.layout_slot);}
  catch(...){hr=E_OUTOFMEMORY;}
  native_shadow_dirty_mask_=dirty;
  if(SUCCEEDED(hr)){
   // Work flags fixed for every redraw: the bake's maps (the build culls per
   // map) and one world preparation per world range.
   for(auto& range:layout.dispatches)range.preparations=0;
   std::vector<std::uint8_t> prepared(native_world_ranges_.size(),0);
   std::size_t dispatch_index=0;
   for(std::size_t index=0;index<layout.work.size();++index){
    auto& work=layout.work[index];work.work_count=0;
    if(work.record==UINT_MAX)continue;
    const auto& draw=native_instance_records_[work.record].command;
    work.flags=(work.flags&1u)|(bake.slice_mask<<8);
    if(draw.instance.world_geometry)work.prepared_vertex_base=draw.world_base;
    if(draw.prepare_world&&draw.world_allocation<prepared.size()&&!prepared[draw.world_allocation]){
     prepared[draw.world_allocation]=1;work.flags|=2u|(0x1fu<<16);
     ++bake.preparations;bake.prepared_vertices+=draw.component_count;
     while(dispatch_index+1<layout.dispatches.size()&&
           index>=layout.dispatches[dispatch_index].first+layout.dispatches[dispatch_index].count)
      ++dispatch_index;
     if(dispatch_index<layout.dispatches.size()&&index>=layout.dispatches[dispatch_index].first)
      ++layout.dispatches[dispatch_index].preparations;
    }
   }
   for(auto& batch:layout.batches){
    std::uint64_t triangles=0;
    for(std::size_t n=batch.first_draw;n<batch.first_draw+batch.draw_count;++n)
     triangles+=native_queued_draw(native_draw_order_[n]).index_count/3u;
    for(unsigned slice=0;slice<6;++slice){
     const bool reached=slice<5&&(bake.slice_mask&(1u<<slice));
     batch.slice_triangles[slice]=reached?triangles:0;
     batch.slice_records[slice]=reached?static_cast<UINT>(batch.draw_count):0u;
    }
   }
   // Work index of each baked caster (records are distinct instances).
   {
    std::unordered_map<UINT,UINT> work_of_record;work_of_record.reserve(layout.work.size());
    for(std::size_t index=0;index<layout.work.size();++index)
     if(layout.work[index].record!=UINT_MAX)work_of_record[layout.work[index].record]=static_cast<UINT>(index);
    std::unordered_map<UINT,UINT> works_of_range;works_of_range.reserve(layout.work.size());
    for(const auto& work:layout.work)if(work.record!=UINT_MAX){
     const auto& draw=native_instance_records_[work.record].command;
     if(draw.prepare_world&&draw.world_allocation<native_world_ranges_.size())++works_of_range[draw.world_allocation];
    }
    for(auto& entry:bake.entries){
     const auto found=work_of_record.find(bake.records[entry.record].record);
     if(found==work_of_record.end())continue;
     entry.work=found->second;
     const auto& draw=native_instance_records_[layout.work[entry.work].record].command;
     entry.shared=(layout.work[entry.work].flags&2u)&&works_of_range[draw.world_allocation]>1;
    }
    std::sort(bake.entries.begin(),bake.entries.end(),[](const auto& a,const auto& b){return a.key<b.key;});
   }
   for(const auto& range:layout.dispatches)bake.dispatch_chunks.push_back(native_shadow_chunks(layout,range,false));
   for(const auto& range:layout.geometry_dispatches)bake.geometry_chunks.push_back(native_shadow_chunks(layout,range,true));
   hr=upload_native_static_work(layout);
  }
  native_pending_draws_.swap(queued);
  if(FAILED(hr)){release_native_shadow_bake(bake);layout.static_bake.work_srv.Reset();layout.static_bake.work12.reset();
   layout.static_bake.work.Reset();layout.static_bake.work_capacity=0;return hr;}
 }
 return S_OK;
}catch(...){release_native_shadow_bake(bake);return E_OUTOFMEMORY;}

// Draws a built bake into the static layers of this redraw (nothing queued).
// masked: per-work map masks from native_shadow_work_mask_ (terrain).
HRESULT Renderer::draw_native_shadow_bake(NativeStaticShadowBake& bake) noexcept{
 if(bake.records.empty())return S_OK;
 if(!native_pending_draws_.empty()){const HRESULT hr=flush_native_shadows();if(FAILED(hr))return hr;}
 for(const auto slot:bake.mesh_slots){if(slot>=native_prepared_mesh_slots_.size())continue;
  auto& mesh=native_prepared_mesh_slots_[slot];mesh.last_used_frame=native_frame_index_;
  if(mesh.mesh)mesh.mesh->last_used_frame=native_frame_index_;
 }
 for(const auto slot:bake.material_slots)if(slot<native_prepared_material_slots_.size())native_prepared_material_slots_[slot].last_used_frame=native_frame_index_;
 native_shadow_bake_active_=&bake;
 const HRESULT hr=flush_native_shadows();
 native_shadow_bake_active_=nullptr;
 return hr;
}

// Neighbour-map casters (shadow_neighbour_casters): 0 none, 1 the close sun
// maps (C0-C2), 2 all five; the shelter takes them at 1 and 2.
UINT Renderer::neighbour_caster_slices() const noexcept{
 const int level=configured_integer(debug_overlay_.config(),"shadow_neighbour_casters",2);
 return level<=0?0u:level==1?0x7u:0x1fu;
}
std::uint64_t Renderer::neighbour_caster_key() const noexcept{
 const UINT slices=neighbour_caster_slices();
 return slices&&neighbours_.owner?(neighbour_caster_revision_<<5)|slices:0u;
}

// The neighbour bake: every object-space decoration record of the rendered
// neighbours (terrain pieces are not casters here) as its own caster, with
// the colour record's material state (cut-outs alpha-tested, far foliage
// opaque by shadow_foliage_opaque like main-map plants). Rebuilt when the
// neighbour membership, the level or the record content changes; drawn into
// the static layer of every redrawn sun map and into the shelter slice.
HRESULT Renderer::draw_native_neighbour_shadows() noexcept try{
 auto& bake=native_neighbour_shadow_bake_;
 const auto key=neighbour_caster_key();
 if(!key){if(bake.built||!bake.records.empty())release_native_shadow_bake(bake);return S_OK;}
 if(!bake.built||bake.built_key!=key||bake.built_content_revision!=native_static_content_revision_||
    bake.far_foliage_opaque!=native_far_foliage_opaque_){
  release_native_shadow_bake(bake);
  bake.layout_slot=7;bake.slice_mask=neighbour_caster_slices();
  for(const auto& [part,draws]:neighbours_.opaque.objects){
   (void)part;
   for(const auto& selected:draws){
    if(selected.record>=native_instance_records_.size())continue;
    const auto& record=native_instance_records_[selected.record];
    if(!record.occupied||record.owner.expired()||!(record.command.instance.world_geometry&native_world_object_space))continue;
    bake.records.push_back({selected.record,bake.slice_mask<<8});
   }
  }
  if(const HRESULT hr=finish_native_shadow_bake(bake);FAILED(hr))return hr;
  bake.built_key=key;bake.built_content_revision=native_static_content_revision_;
  bake.far_foliage_opaque=native_far_foliage_opaque_;bake.built=true;
 }
 return draw_native_shadow_bake(bake);
}catch(...){release_native_shadow_bake(native_neighbour_shadow_bake_);return E_OUTOFMEMORY;}

std::uint64_t Renderer::object_caster_key() const noexcept{
 const auto casters=active_statics_casters();
 const std::uint64_t active=active_statics_.casters.empty()||!casters||casters->ids.empty()?0u:
  (casters->revision^(active_statics_.caster_revision<<40))*0x9E3779B97F4A7C15ull;
 return neighbour_caster_key()^active;
}

// The active map's object-space statics as sun and shelter casters (layout
// slot kActiveCasterLayout): the parked catalog records of every admitted
// object of the world (the bridge publishes that set from its caster store
// build, which then holds none of them), each casting once into every map.
// Rebuilt when the set changes. The doubled count checks the rule against
// the store each redraw: an admitted object found there casts twice.
HRESULT Renderer::draw_native_active_shadows() noexcept try{
 static_assert(kActiveCasterLayout==kMapOrderedLayout+kMapPreviewSections,"active caster layout slot");
 auto& bake=native_active_shadow_bake_;
 const auto casters=active_statics_casters();
 if(active_statics_.casters.empty()||!casters||casters->ids.empty()){
  if(bake.built||!bake.records.empty())release_native_shadow_bake(bake);
  native_active_casters_=native_active_doubled_casters_=0;return S_OK;
 }
 const std::uint64_t key=casters->revision^(active_statics_.caster_revision<<40);
 if(!bake.built||bake.built_key!=key||bake.built_content_revision!=native_static_content_revision_||
    bake.far_foliage_opaque!=native_far_foliage_opaque_){
  release_native_shadow_bake(bake);
  bake.layout_slot=kActiveCasterLayout;bake.slice_mask=0x1fu;
  // Objects that left the published set give their caster records back.
  {std::vector<std::int32_t> gone;
   for(const auto& [id,caster]:active_statics_.casters){(void)caster;
    if(!std::binary_search(casters->ids.begin(),casters->ids.end(),id))gone.push_back(id);}
   for(const auto id:gone)release_active_caster(id);}
  for(const auto id:casters->ids){
   const auto found=active_statics_.casters.find(id);
   if(found==active_statics_.casters.end())continue;
   for(const auto index:found->second.records){
    if(index>=native_instance_records_.size())continue;
    const auto& record=native_instance_records_[index];
    if(!record.occupied||record.owner.expired())continue;
    bake.records.push_back({index,bake.slice_mask<<8});
   }
  }
  if(const HRESULT hr=finish_native_shadow_bake(bake);FAILED(hr))return hr;
  bake.built_key=casters->revision^(active_statics_.caster_revision<<40);
  bake.built_content_revision=native_static_content_revision_;
  bake.far_foliage_opaque=native_far_foliage_opaque_;bake.built=true;
 }
 (void)key;
 native_active_casters_=bake.records.size();native_active_doubled_casters_=0;
 const auto& store=native_static_shadow_bake_.store;
 for(const auto id:casters->ids){
  const auto it=std::lower_bound(store.begin(),store.end(),id,
   [](const RetainedShadowCasterValue& value,std::int32_t object){return value.revision.signed_id<object;});
  if(it!=store.end()&&it->revision.signed_id==id&&it->persistent_static)++native_active_doubled_casters_;
 }
 return draw_native_shadow_bake(bake);
}catch(...){release_native_shadow_bake(native_active_shadow_bake_);return E_OUTOFMEMORY;}

// A store change usually touches a few casters: switch their baked work
// entries off in place (record UINT_MAX is skipped by every pass) and let
// their new values wait as absent casters. False asks for a full rebuild (a
// switched-off entry carries the world preparation of its range, the lists
// are not in caster order, or too much changed).
bool Renderer::update_native_static_shadow_store(const RetainedShadowCasterValue* next,
 std::uint32_t count) noexcept try{
 auto& bake=native_static_shadow_bake_;auto& layout=native_submission_layouts_[bake.layout_slot];
 const auto less=[](const RetainedShadowCasterValue& a,const RetainedShadowCasterValue& b){
  return a.revision.signed_id!=b.revision.signed_id?a.revision.signed_id<b.revision.signed_id:a.revision.part<b.revision.part;};
 const auto same=[](const RetainedShadowCasterValue& a,const RetainedShadowCasterValue& b){
  return a.revision==b.revision&&a.mask==b.mask&&a.persistent_static==b.persistent_static&&
   a.bounds.valid==b.bounds.valid&&a.bounds.minimum==b.bounds.minimum&&a.bounds.maximum==b.bounds.maximum;};
 const auto key_of=[](const RetainedShadowCasterValue& value){
  return (std::uint64_t(std::uint32_t(value.revision.signed_id))<<32)|value.revision.part;};
 const auto& old=bake.store;
 if(!std::is_sorted(old.begin(),old.end(),less)||!std::is_sorted(next,next+count,less))return false;
 std::vector<std::uint64_t> gone;std::vector<RetainedShadowCasterValue> added;
 std::size_t i=0,j=0;
 while(i<old.size()||j<count){
  if(j==count||(i<old.size()&&less(old[i],next[j]))){gone.push_back(key_of(old[i]));++i;continue;}
  if(i==old.size()||less(next[j],old[i])){added.push_back(next[j]);++j;continue;}
  if(!same(old[i],next[j])){gone.push_back(key_of(old[i]));added.push_back(next[j]);}
  ++i;++j;
 }
 if(gone.size()+added.size()>512)return false;
 const auto find=[&](std::uint64_t key){
  const auto it=std::lower_bound(bake.entries.begin(),bake.entries.end(),key,
   [](const NativeStaticShadowBake::Entry& entry,std::uint64_t value){return entry.key<value;});
  return it!=bake.entries.end()&&it->key==key?it:bake.entries.end();
 };
 for(const auto key:gone){
  const auto it=find(key);
  if(it!=bake.entries.end()&&it->shared)return false;
 }
 for(const auto key:gone){
  // A waiting value of this caster is stale too, also when its baked entry
  // was switched off by an earlier change (a stale value fails the bridge's
  // revision check and rejects the whole shadow pass).
  bake.absent.erase(std::remove_if(bake.absent.begin(),bake.absent.end(),
   [&](const RetainedShadowCasterValue& value){return key_of(value)==key;}),bake.absent.end());
  const auto it=find(key);
  if(it==bake.entries.end()||it->record==UINT_MAX)continue;
  // The GPU copy of the entry must go too: its record is released below and
  // can be reused by another caster, which the stale entry would then draw
  // with this caster's world preparation (D3D12: the giant streaked shadows
  // that appeared while panning; the D3D12 bake keeps its work in work12).
  if(it->work<layout.work.size()&&layout.work[it->work].record!=UINT_MAX&&
     (d3d12_?layout.static_bake.work12!=nullptr:layout.static_bake.work!=nullptr)){
   layout.work[it->work].record=UINT_MAX;
   const UINT offset=it->work*static_cast<UINT>(sizeof(NativeDrawWorkGpu));
   if(d3d12_){
    if(FAILED(d3d12_->write_buffer(*layout.static_bake.work12,offset,&layout.work[it->work],sizeof(NativeDrawWorkGpu))))return false;
   }else{
    const D3D11_BOX box{offset,0,0,offset+static_cast<UINT>(sizeof(NativeDrawWorkGpu)),1,1};
    context_->UpdateSubresource(layout.static_bake.work.Get(),0,&box,&layout.work[it->work],0,0);
   }
  }
  if(it->record<bake.records.size()){
   auto& selected=bake.records[it->record];
   if(selected.record<native_instance_records_.size())release_native_queued_draw(selected.record,false);
   selected.record=UINT_MAX;
   if(it->record<bake.owner_of.size()&&bake.owner_of[it->record]<bake.owner_live.size()&&
      bake.owner_live[bake.owner_of[it->record]])--bake.owner_live[bake.owner_of[it->record]];
  }
  it->work=it->record=UINT_MAX;++bake.excluded;
 }
 for(const auto& value:added)if(value.persistent_static)bake.absent.push_back(value);
 return true;
}catch(...){return false;}

bool Renderer::queue_native_static_shadow_depth(
 const RetainedShadowCasterValue* casters,std::uint32_t count,std::uint64_t store_revision,
 std::vector<RetainedShadowCasterValue>& missing) noexcept {SCENE_ENTRY("queue_native_static_shadow_depth");
 const auto reject=[](const char* reason) noexcept {
#ifdef EID3D11_EMBEDDED
  embedded_report_render_failure("owned_static_depth_rejected",reason);
#else
  (void)reason;
#endif
  return false;
 };
 if(!native_shadow_collecting_)return reject("not_collecting");
 if(native_shadow_failed_)return reject("prior_shadow_failure");
 if(!casters&&count)return reject("missing_casters");
 try {
  auto& bake=native_static_shadow_bake_;
  missing.clear();
  if(casters&&store_revision!=bake.store_revision){
   const bool kept=bake.built&&bake.built_store_revision==bake.store_revision&&
    update_native_static_shadow_store(casters,count);
   bake.store.assign(casters,casters+count);bake.store_revision=store_revision;
   bake.built_store_revision=kept?store_revision:0;
  }else if(!casters&&(!store_revision||store_revision!=bake.store_revision))return reject("store_revision_unpublished");
  // A call outside a static-layer redraw only keeps the store; the
  // precipitation shelter bake (renderer_precipitation.cpp) draws the same
  // baked store into its one straight-down slice.
  if(native_point_shadow_active_||(!native_rain_active_&&native_shadow_phase_!=1)||!native_shadow_dirty_mask_)return true;
  if(bake.built_store_revision!=bake.store_revision||
     !native_shadow_bake_current(bake,native_static_shadow_records_serial_)){
   std::vector<std::pair<std::uint64_t,NativeShadowCasterRevision>> members;members.reserve(bake.store.size());
   std::vector<std::size_t> member_caster;member_caster.reserve(bake.store.size());
   for(std::size_t index=0;index<bake.store.size();++index){
    const auto& caster=bake.store[index];if(!caster.persistent_static)continue;
    members.push_back({(std::uint64_t(std::uint32_t(caster.revision.signed_id))<<32)|caster.revision.part,caster.revision});
    member_caster.push_back(index);
   }
   std::vector<std::size_t> absent;
   if(FAILED(rebuild_native_shadow_bake(bake,native_static_shadow_records_,members,absent,true,
      native_static_shadow_records_serial_))){native_shadow_failed_=true;return reject("store_bake_build");}
   bake.absent.clear();for(const auto member:absent)bake.absent.push_back(bake.store[member_caster[member]]);
   bake.built_store_revision=bake.store_revision;
  }
  // caster_check (D3D12 GPU reports): tag the records of every admitted
  // main-map static drawn this redraw, the store's (path 1) and its
  // object-space casters' (path 2); the shadow build counts per object.
  if(d3d12_&&configured_boolean(debug_overlay_.config(),"caster_check",false))try{
   const auto ids=active_statics_casters();
   std::vector<std::uint32_t> table(native_instance_records_.size(),0u);
   UINT objects=0;
   std::unordered_map<std::int32_t,std::uint32_t> object_of;
   if(ids)for(const auto id:ids->ids){
    const std::uint32_t object=++objects;object_of.emplace(id,object);
    const auto active=active_statics_.casters.find(id);
    if(active!=active_statics_.casters.end())for(const auto record:active->second.records)
     if(record<table.size())table[record]=object|(2u<<30);
   }
   // Their store entries (keys id << 32 | part), if any remained.
   for(const auto& entry:bake.entries){
    const auto found=object_of.find(static_cast<std::int32_t>(entry.key>>32));
    if(found!=object_of.end()&&entry.record<bake.records.size()&&bake.records[entry.record].record<table.size())
     table[bake.records[entry.record].record]=found->second|(1u<<30);
   }
   d3d12_->set_caster_check(table.data(),static_cast<UINT>(table.size()),objects);
  }catch(...){}
  if(FAILED(draw_native_shadow_bake(bake)))return reject("store_bake_draw");
  // Neighbour-map decorations join the same static layer (and shelter).
  // A bake that cannot be built is omitted (named once); a failed draw has
  // already failed the pass.
  if(const HRESULT hr=draw_native_neighbour_shadows();FAILED(hr)){
   if(native_shadow_failed_)return reject("neighbour_bake_draw");
   static bool reported=false;
   if(!reported)std::fprintf(stderr,"Neighbour sun casters unavailable (0x%08lx); neighbour shadows omitted.\n",static_cast<unsigned long>(hr));
   reported=true;
  }
  // The active map's object-space statics: their own records cast (they
  // are not in the store).
  if(const HRESULT hr=draw_native_active_shadows();FAILED(hr)){
   if(native_shadow_failed_)return false;
   static bool reported=false;
   if(!reported)std::fprintf(stderr,"Main-map object-space sun casters unavailable (0x%08lx); their shadows omitted.\n",static_cast<unsigned long>(hr));
   reported=true;
  }
  // Store casters without a current record that a redrawn map reaches are
  // registered and drawn through the depth path this frame.
  for(const auto& caster:bake.absent){
   const UINT mask=native_static_shadow_reach(caster.bounds,
    native_static_scroll_mask(caster.bounds,caster.mask&native_shadow_dirty_mask_));
   if(mask)missing.push_back(caster);
  }
  return true;
 }catch(...){native_shadow_failed_=true;native_shadow_bake_active_=nullptr;return reject("store_exception");}
}

// Terrain sun casters of a static-layer redraw (issue #162). entries are the
// bridge's terrain casters of this frame with their final map masks (dirty
// static maps, scroll strip and entered parts already applied). Baked parts
// take their mask from a per-work mask buffer and draw with the terrain bake;
// absent receives the entries without a current baked record, which the
// bridge submits through the depth path. The bake's members are the terrain
// casters seen when it was built; a part that stops appearing keeps its
// baked record with mask 0 until the next rebuild.
bool Renderer::queue_native_terrain_shadow_bake(const NativeTerrainShadowEntry* entries,
 std::uint32_t count,std::vector<std::uint32_t>& absent) noexcept{SCENE_ENTRY("queue_native_terrain_shadow_bake");
 absent.clear();
 if(!native_shadow_collecting_||native_shadow_failed_||(!entries&&count)||native_point_shadow_active_||
    native_shadow_phase_!=1||!native_shadow_dirty_mask_)return false;
 try{
  auto& bake=native_terrain_shadow_bake_;
  bake.layout_slot=6;bake.masked=true;
  const auto key_of=[](const NativeShadowCasterRevision& revision){
   return (std::uint64_t(std::uint32_t(revision.signed_id))<<32)|revision.part;};
  if(!native_shadow_bake_current(bake,native_terrain_shadow_records_serial_)){
   std::vector<std::pair<std::uint64_t,NativeShadowCasterRevision>> members;members.reserve(count);
   for(std::uint32_t index=0;index<count;++index)members.push_back({key_of(entries[index].revision),entries[index].revision});
   std::sort(members.begin(),members.end(),[](const auto& a,const auto& b){return a.first<b.first;});
   std::vector<std::size_t> ignored;
   if(FAILED(rebuild_native_shadow_bake(bake,native_terrain_shadow_records_,members,ignored,false,
      native_terrain_shadow_records_serial_))){native_shadow_failed_=true;return false;}
  }
  auto& layout=native_submission_layouts_[bake.layout_slot];
  bake.masks.assign(layout.work.size(),0u);
  bool any=false;
  // Terrain casters arrive in caster order (sector, part), which is key
  // order: one forward walk, with a search only if that order breaks.
  auto cursor=bake.entries.begin();std::uint64_t previous_key=0;
  std::size_t registered_absent=0;
  for(std::uint32_t index=0;index<count;++index){
   const auto& entry=entries[index];
   const auto key=key_of(entry.revision);
   if(key<previous_key)cursor=bake.entries.begin();
   previous_key=key;
   while(cursor!=bake.entries.end()&&cursor->key<key)++cursor;
   const auto it=cursor;
   if(it!=bake.entries.end()&&it->key==key&&it->work<bake.masks.size()&&it->revision==entry.revision){
    bake.masks[it->work]|=entry.mask;any|=entry.mask!=0;
   }else if(entry.mask){
    absent.push_back(index);
    const auto stored=native_terrain_shadow_records_.find(key);
    if(stored!=native_terrain_shadow_records_.end()&&stored->second.revision==entry.revision)++registered_absent;
   }
  }
  // Many reached parts that already have records draw outside the bake (the
  // view moved on, LOD variants changed): fold them in at the next redraw.
  // Parts without a record are registered this frame and counted next time.
  if(registered_absent>(std::max<std::size_t>)(256,bake.records.size()/8))bake.built=false;
  if(!any)return true;
  // Per-work map masks (t35): sized by demand, written once per redraw.
  const UINT works=static_cast<UINT>(bake.masks.size());
  if(d3d12_){ // D3D12: the same masks in an executor buffer, written in stream order before the bake's build
   const UINT64 bytes=UINT64(works)*sizeof(UINT);
   if(FAILED(native_d3d12_buffer(native_shadow_work_mask12_,bytes,sizeof(UINT),0))||
      FAILED(d3d12_->write_buffer(*native_shadow_work_mask12_,0,bake.masks.data(),bytes)))return false;
   return SUCCEEDED(draw_native_shadow_bake(bake));
  }
  if(!native_shadow_work_mask_||works>native_shadow_work_mask_capacity_){
   const UINT capacity=demand_growth_capacity(works,native_shadow_work_mask_capacity_,kNativeRawElementLimit);
   native_shadow_work_mask_.Reset();native_shadow_work_mask_srv_.Reset();native_shadow_work_mask_capacity_=0;
   D3D11_BUFFER_DESC description{};description.ByteWidth=capacity*static_cast<UINT>(sizeof(UINT));
   description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
   description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;description.StructureByteStride=sizeof(UINT);
   if(FAILED(device_->CreateBuffer(&description,nullptr,&native_shadow_work_mask_)))return false;
   D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_UNKNOWN;
   view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=capacity;
   if(FAILED(device_->CreateShaderResourceView(native_shadow_work_mask_.Get(),&view,&native_shadow_work_mask_srv_))){
    native_shadow_work_mask_.Reset();return false;}
   native_shadow_work_mask_capacity_=capacity;
  }
  const D3D11_BOX box{0,0,0,works*static_cast<UINT>(sizeof(UINT)),1,1};
  context_->UpdateSubresource(native_shadow_work_mask_.Get(),0,&box,bake.masks.data(),0,0);
  return SUCCEEDED(draw_native_shadow_bake(bake));
 }catch(...){native_shadow_failed_=true;native_shadow_bake_active_=nullptr;return false;}
}

#endif
} // namespace owned
