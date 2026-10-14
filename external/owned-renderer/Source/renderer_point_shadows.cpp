// Point-light shadow residency: demand-sized cube-face atlases, slot
// allocation/release, per-lamp publication and cube face rendering. The bake
// scheduling policy lives in point_shadow_policy.h and the bridge.
#include "point_shadow_policy.h"
#include "renderer.h"
#include "engine_bridge.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <vector>

namespace owned {
namespace {
// Distinguishes pool incarnations across device resets (renderer-local serial).
std::atomic<std::uint64_t> g_point_shadow_pool_generation{0};
}
// The atlases (point_shadow_types.h): page 0 the static cubes (D16), page 1
// the overlay cubes (D32). A slice holds faces_per_slice faces of
// native_point_shadow_side_ texels, each in a one-texel gutter.
namespace {
UINT point_atlas_width(UINT side) noexcept {return PointShadowTextureLayout::atlas_tiles*(side+2u);}
UINT point_atlas_slices(UINT cubes) noexcept {
 return (cubes*6u+PointShadowTextureLayout::faces_per_slice-1u)/PointShadowTextureLayout::faces_per_slice;
}
}
std::uint64_t Renderer::native_point_shadow_bytes() const noexcept {
 std::uint64_t bytes=0;
 const std::uint64_t width=point_atlas_width(native_point_shadow_side_);
 for(const auto& page:native_point_shadow_pages_)
  bytes+=std::uint64_t(page.dsv.size())*width*width*(page.overlay?4u:2u);
 return bytes;
}
void Renderer::point_shadow_capacity_failure(const char* reason,HRESULT hr) noexcept {
 if(!native_point_shadow_failure_reported_)
  std::fprintf(stderr,"Point shadow residency: %s (0x%08lx); fading least-important lamps.\n",reason,static_cast<unsigned long>(hr));
 native_point_shadow_failure_reported_=true;
}
// Grows atlas index to hold cubes (whole slices; the cube capacity of those
// slices is kept), copying the slices it already has. Slots stay stable.
HRESULT Renderer::grow_native_point_shadow_page(std::size_t index,UINT cubes,bool overlay){
 static_assert(PointShadowTextureLayout::max_slices==D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION);
 static_assert(PointShadowTextureLayout::array_bindings==2&&PointShadowTextureLayout::first_srv+2<=PointShadowTextureLayout::terrain_noise_srv);
 if(!cubes||cubes>PointShadowTextureLayout::cubes_per_atlas||index>=native_point_shadow_pages_.size())return E_INVALIDARG;
 const UINT width=point_atlas_width(native_point_shadow_side_);
 if(width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)return E_INVALIDARG;
 auto& old=native_point_shadow_pages_[index];
 const UINT slices=point_atlas_slices(cubes);
 const auto bytes=std::uint64_t(width)*width*(slices-std::min<UINT>(slices,static_cast<UINT>(old.dsv.size())))*(overlay?4u:2u);
 if(adapter3_){DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
  if(SUCCEEDED(adapter3_->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&memory))&&memory.Budget&&
     bytes>memory.Budget-std::min(memory.Budget,memory.CurrentUsage))return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
 }
 NativePointShadowPage next;next.overlay=overlay;next.used=old.used;
 next.used.resize(std::min(PointShadowTextureLayout::cubes_per_atlas,slices*PointShadowTextureLayout::faces_per_slice/6u),false);
 D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=width;desc.ArraySize=slices;
 desc.MipLevels=1;desc.Format=overlay?DXGI_FORMAT_R32_TYPELESS:DXGI_FORMAT_R16_TYPELESS;desc.SampleDesc.Count=1;
 desc.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
 transition_profile_pass("point_shadow_pool_growth"); // temporary: growth stall witness
 HRESULT hr=device_->CreateTexture2D(&desc,nullptr,&next.depth);if(FAILED(hr))return hr;
 next.dsv.resize(desc.ArraySize);
 for(UINT slice=0;slice<desc.ArraySize;++slice){
  D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};dsv.Format=overlay?DXGI_FORMAT_D32_FLOAT:DXGI_FORMAT_D16_UNORM;
  dsv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
  dsv.Texture2DArray.ArraySize=1;dsv.Texture2DArray.FirstArraySlice=slice;
  if(FAILED(hr=device_->CreateDepthStencilView(next.depth.Get(),&dsv,&next.dsv[slice])))return hr;
 }
 D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=overlay?DXGI_FORMAT_R32_FLOAT:DXGI_FORMAT_R16_UNORM;
 srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
 srv.Texture2DArray.MipLevels=1;srv.Texture2DArray.ArraySize=desc.ArraySize;
 if(FAILED(hr=device_->CreateShaderResourceView(next.depth.Get(),&srv,&next.srv)))return hr;
 // Copy existing slices before replacing this atlas. Stable slot indices and
 // the pool generation survive growth; committed bakes are never invalidated by it.
 if(old.depth)for(UINT slice=0;slice<old.dsv.size();++slice)
  context_->CopySubresourceRegion(next.depth.Get(),slice,0,0,0,old.depth.Get(),slice,nullptr);
 old=std::move(next);return S_OK;
}
bool Renderer::allocate_native_point_shadow(bool overlay,int& slot) noexcept {
 slot=-1;
 try{
  if(native_point_shadow_pages_.size()<2)native_point_shadow_pages_.resize(2);
  const std::size_t p=overlay?1u:0u;auto& page=native_point_shadow_pages_[p];
  const int base=overlay?static_cast<int>(PointShadowTextureLayout::cubes_per_atlas):0;
  for(unsigned s=0;s<page.used.size();++s)if(!page.used[s]){page.used[s]=true;slot=base+static_cast<int>(s);return true;}
  if(page.used.size()>=PointShadowTextureLayout::cubes_per_atlas){
   point_shadow_capacity_failure("point shadow atlas full (D3D11 2048-slice limit)",E_OUTOFMEMORY);return false;}
  const auto required=static_cast<UINT>(page.used.size()+1);
  const auto target=std::min(PointShadowTextureLayout::cubes_per_atlas,std::max(required,static_cast<UINT>(page.used.size()*2)));
  HRESULT failure=grow_native_point_shadow_page(p,target,overlay);
  if(FAILED(failure)&&target!=required)failure=grow_native_point_shadow_page(p,required,overlay);
  if(SUCCEEDED(failure)){
   auto& grown=native_point_shadow_pages_[p];grown.used[required-1]=true;
   slot=base+static_cast<int>(required-1);native_point_shadow_failure_reported_=false;return true;
  }
  point_shadow_capacity_failure(failure==HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY)?
   "live WDDM budget pressure":"depth atlas or view creation failed",failure);
 }catch(...){point_shadow_capacity_failure("slot metadata allocation failed",E_OUTOFMEMORY);}
 return false;
}
void Renderer::release_native_point_shadow(std::uint64_t generation,int slot) noexcept {
 if(generation!=native_point_shadow_generation_||slot<0)return;
 const auto p=static_cast<unsigned>(slot)/PointShadowTextureLayout::cubes_per_atlas;
 const auto s=static_cast<unsigned>(slot)%PointShadowTextureLayout::cubes_per_atlas;
 if(p>=native_point_shadow_pages_.size()||s>=native_point_shadow_pages_[p].used.size())return;
 auto& page=native_point_shadow_pages_[p];page.used[s]=false;
 if(std::none_of(page.used.begin(),page.used.end(),[](bool used){return used;})){
  ID3D11ShaderResourceView* empty=nullptr;
  context_->PSSetShaderResources(PointShadowTextureLayout::first_srv+p,1,&empty);page={};page.overlay=p!=0;
 }
}
bool Renderer::begin_native_point_shadow_batch(std::uint64_t& generation,bool* pressure) noexcept {
 if(pressure)*pressure=false;
 if(native_shadow_collecting_||native_point_shadow_active_||!native_point_shadows_enabled_)return false;
 if(FAILED(flush_native()))return false;
 std::array<ID3D11ShaderResourceView*,PointShadowTextureLayout::array_bindings> empty{};
 context_->PSSetShaderResources(PointShadowTextureLayout::first_srv,static_cast<UINT>(empty.size()),empty.data());
 if(!native_point_shadow_constants_){
  D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(NativeShadowConstants);
  desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  if(FAILED(device_->CreateBuffer(&desc,nullptr,&native_point_shadow_constants_)))return false;
 }
 if(!native_point_shadow_sampler_){
  D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
  sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;
  sampler.BorderColor[0]=sampler.BorderColor[1]=sampler.BorderColor[2]=sampler.BorderColor[3]=1;
  sampler.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;sampler.MaxLOD=D3D11_FLOAT32_MAX;
  if(FAILED(device_->CreateSamplerState(&sampler,&native_point_shadow_sampler_)))return false;
 }
 if(!native_point_shadow_generation_)native_point_shadow_generation_=g_point_shadow_pool_generation.fetch_add(1,std::memory_order_relaxed)+1;
 generation=native_point_shadow_generation_;
 if(adapter3_&&pressure){DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
  if(SUCCEEDED(adapter3_->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&memory))&&memory.Budget&&memory.CurrentUsage>memory.Budget){
   *pressure=true;point_shadow_capacity_failure("live WDDM budget pressure",HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY));
  }
 }
 return true;
}
void Renderer::publish_native_point_shadows(const std::vector<NativePointShadowPublication>& lights,
 const NativePointShadowStats& stats) noexcept {
 for(std::size_t i=0;i<native_point_lights_.size();++i){
  std::array<float,4> next{};
  if(i<lights.size()&&lights[i].static_slot>=0)
   next={float(lights[i].static_slot+1),float(lights[i].overlay_slot+1),lights[i].strength,lights[i].overlay_strength};
  if(native_point_lights_[i].shadow_slots!=next){native_point_lights_[i].shadow_slots=next;native_point_lights_dirty_=true;}
  if(i<lights.size()&&lights[i].bake_radius>0&&native_point_lights_[i].shadow_center[3]!=lights[i].bake_radius){
   native_point_lights_[i].shadow_center[3]=lights[i].bake_radius;native_point_lights_dirty_=true;}
  if(i<lights.size()&&i<native_point_light_inputs_.size()){
   // A merged cluster casts from its shared centre; otherwise the light's own.
   const auto& own=native_point_light_inputs_[i].center;
   const std::array<float,3> centre=lights[i].merged?lights[i].shadow_center:own;
   auto& gpu=native_point_lights_[i].shadow_center;
   if(gpu[0]!=centre[0]||gpu[1]!=centre[1]||gpu[2]!=centre[2]){
    gpu[0]=centre[0];gpu[1]=centre[1];gpu[2]=centre[2];native_point_lights_dirty_=true;}
  }
 }
 point_pass_seen_|=stats.pass_ran;point_publication_seen_=true;
 point_shadows_cumulative_.baked=stats.baked;
 native_frame_stats_.point_shadows.baked=stats.baked;
 point_shadows_cumulative_.rebaked+=stats.rebaked;
 native_frame_stats_.point_shadows.rebaked+=stats.rebaked;
 point_shadows_cumulative_.overlays+=stats.overlays;
 native_frame_stats_.point_shadows.overlays+=stats.overlays;
 point_shadows_cumulative_.budget+=stats.budget;
 native_frame_stats_.point_shadows.budget+=stats.budget;
 point_shadows_cumulative_.radius_dirty+=stats.radius_dirty;
 native_frame_stats_.point_shadows.radius_dirty+=stats.radius_dirty;
 point_shadows_cumulative_.terrain+=stats.terrain;
 native_frame_stats_.point_shadows.terrain+=stats.terrain;
 point_shadows_cumulative_.light+=stats.light;
 native_frame_stats_.point_shadows.light+=stats.light;
 point_shadows_cumulative_.list+=stats.list;
 native_frame_stats_.point_shadows.list+=stats.list;
 point_shadows_cumulative_.reset+=stats.reset;
 native_frame_stats_.point_shadows.reset+=stats.reset;
 point_shadows_cumulative_.allocation_failures+=stats.allocation_failures;
 native_frame_stats_.point_shadows.allocation_failures+=stats.allocation_failures;
 point_shadows_cumulative_.pressure_drops+=stats.pressure_drops;
 native_frame_stats_.point_shadows.pressure_drops+=stats.pressure_drops;
 point_shadows_cumulative_.reorder_only+=stats.reorder_only;
 native_frame_stats_.point_shadows.reorder_only+=stats.reorder_only;
 point_shadows_cumulative_.reset_epoch+=stats.reset_epoch;
 native_frame_stats_.point_shadows.reset_epoch+=stats.reset_epoch;
 point_shadows_cumulative_.reset_journal_full+=stats.reset_journal_full;
 native_frame_stats_.point_shadows.reset_journal_full+=stats.reset_journal_full;
 point_shadows_cumulative_.reset_class_flags+=stats.reset_class_flags;
 native_frame_stats_.point_shadows.reset_class_flags+=stats.reset_class_flags;
 point_shadows_cumulative_.reset_undetected+=stats.reset_undetected;
 native_frame_stats_.point_shadows.reset_undetected+=stats.reset_undetected;
 point_shadows_cumulative_.reset_pool+=stats.reset_pool;
 native_frame_stats_.point_shadows.reset_pool+=stats.reset_pool;
 point_shadows_cumulative_.eligibility_lost+=stats.eligibility_lost;
 native_frame_stats_.point_shadows.eligibility_lost+=stats.eligibility_lost;
 point_shadows_cumulative_.strength_zeroed+=stats.strength_zeroed;
 point_shadows_cumulative_.static_to_dynamic+=stats.static_to_dynamic;
 point_shadows_cumulative_.merged_lights=stats.merged_lights;
 point_shadows_cumulative_.casting_lights=stats.casting_lights;point_shadows_cumulative_.neighbour_within_1=stats.neighbour_within_1;
 point_shadows_cumulative_.neighbour_within_2=stats.neighbour_within_2;point_shadows_cumulative_.neighbour_within_4=stats.neighbour_within_4;
 native_frame_stats_.point_shadows.strength_zeroed+=stats.strength_zeroed;
}

bool Renderer::begin_native_point_shadow_faces(UINT slot,UINT light_index,
 const NativePointShadowFrame& faces) noexcept {
 const UINT page=slot/PointShadowTextureLayout::cubes_per_atlas,cube=slot%PointShadowTextureLayout::cubes_per_atlas;
 if(native_shadow_collecting_||native_point_shadow_active_||page>=native_point_shadow_pages_.size()||
    cube>=native_point_shadow_pages_[page].used.size()||!native_point_shadow_pages_[page].used[cube]||
    light_index>=native_point_lights_.size()||!native_point_shadow_constants_||!native_depth_clear_vs_)return false;
 if(!native_point_clear_raster_){
  D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
  if(FAILED(device_->CreateRasterizerState(&raster,&native_point_clear_raster_)))return false;
 }
 native_point_shadow_frame_=faces;native_point_shadow_slot_=slot;
 native_shadow_dirty_mask_=63;native_shadow_cleared_mask_=0;
 native_shadow_failed_=false;native_shadow_collecting_=true;native_point_shadow_active_=true;
 if(SUCCEEDED(upload_native_shadow_constants(0)))return true;
 end_native_point_shadow_faces(false);return false;
}

bool Renderer::end_native_point_shadow_faces(bool commit) noexcept {
 if(!native_point_shadow_active_)return false;
 HRESULT hr=E_FAIL;
 try{if(commit&&!native_shadow_failed_)hr=flush_native_shadows();else discard_native();}
 catch(...){discard_native();}
 native_shadow_collecting_=false;native_point_shadow_active_=false;
 native_shadow_dirty_mask_=native_shadow_cleared_mask_=0;native_shadow_failed_=false;
 // Directional frame, valid bits and buffer were never modified. Color binds its
 // own b2 at submission; there is no directional save/restore operation here.
 return SUCCEEDED(hr);
}
} // namespace owned
