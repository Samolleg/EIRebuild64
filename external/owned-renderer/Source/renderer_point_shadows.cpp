// Point-light shadow residency: demand-sized cube-face atlases, slot
// allocation/release, per-lamp publication and cube face rendering. The bake
// scheduling policy lives in point_shadow_policy.h and the bridge.
#include "point_shadow_policy.h"
#include "renderer.h"
#include "engine_bridge.h"
#include "cave_irradiance.h"
#include "emissive_lights.h"
#include "point_light_grid.h"
#include "owned_asset_loader.h"
#include "shader_cache.h"
#include "owned_resource_runtime.h"
#include "owned_resource_store.h"
#include "world_fog.h"
#include "ambient_particles.h"
#include <fstream>
#include <iterator>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <vector>

namespace owned {
#if !defined(EID3D11_SHADER_EMBED)
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
  bytes+=std::uint64_t(point_atlas_slices(static_cast<UINT>(page.used.size())))*width*width*(page.overlay?4u:2u);
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
 const UINT old_slices=point_atlas_slices(static_cast<UINT>(old.used.size()));
 const auto bytes=std::uint64_t(width)*width*(slices-std::min(slices,old_slices))*(overlay?4u:2u);
 // adapter3_ belongs to the helper D3D11 device, not necessarily the D3D12
 // device. D3D12 allocation failures still take the existing fade/retry path.
 if(!d3d12_&&adapter3_){DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
  if(SUCCEEDED(adapter3_->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&memory))&&memory.Budget&&
     bytes>memory.Budget-std::min(memory.Budget,memory.CurrentUsage))return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
 }
 NativePointShadowPage next;next.overlay=overlay;next.used=old.used;
 next.used.resize(std::min(PointShadowTextureLayout::cubes_per_atlas,slices*PointShadowTextureLayout::faces_per_slice/6u),false);
 if(d3d12_){
  transition_profile_pass("point_shadow_pool_growth");
  const frame_acct::Scope growth_scope(frame_acct::point_pool_growth);
  HRESULT hr=d3d12_->create_depth_array(width,slices,next.depth12,kPointShadowDepthFormats[overlay?1:0]);
  if(FAILED(hr))return hr;
  if(old.depth12)for(UINT slice=0;slice<old_slices;++slice)
   if(FAILED(hr=d3d12_->copy_texture(*next.depth12,slice,*old.depth12,slice)))return hr;
  old=std::move(next);return S_OK;
 }
 D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=width;desc.ArraySize=slices;
 desc.MipLevels=1;desc.Format=overlay?DXGI_FORMAT_R32_TYPELESS:DXGI_FORMAT_R16_TYPELESS;desc.SampleDesc.Count=1;
 desc.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
 transition_profile_pass("point_shadow_pool_growth"); // temporary: growth stall witness
 const frame_acct::Scope growth_scope(frame_acct::point_pool_growth);
 HRESULT hr=device_->CreateTexture2D(&desc,nullptr,&next.depth);if(FAILED(hr))return hr;
 next.dsv.resize(desc.ArraySize);
 for(UINT slice=0;slice<desc.ArraySize;++slice){
  D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};dsv.Format=kPointShadowDepthFormats[overlay?1:0];
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
bool Renderer::allocate_native_point_shadow(bool overlay,int& slot) noexcept {SCENE_ENTRY("allocate_native_point_shadow");
 slot=-1;
 try{
  if(native_point_shadow_pages_.size()<2)native_point_shadow_pages_.resize(2);
  const std::size_t p=overlay?1u:0u;auto& page=native_point_shadow_pages_[p];
  const int base=overlay?static_cast<int>(PointShadowTextureLayout::cubes_per_atlas):0;
  for(unsigned s=0;s<page.used.size();++s)if(!page.used[s]){page.used[s]=true;slot=base+static_cast<int>(s);return true;}
  if(page.used.size()>=PointShadowTextureLayout::cubes_per_atlas){
   point_shadow_capacity_failure("point shadow atlas full (2048-slice limit)",E_OUTOFMEMORY);return false;}
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
void Renderer::release_native_point_shadow(std::uint64_t generation,int slot) noexcept {SCENE_ENTRY("release_native_point_shadow");
 if(generation!=native_point_shadow_generation_||slot<0)return;
 const auto p=static_cast<unsigned>(slot)/PointShadowTextureLayout::cubes_per_atlas;
 const auto s=static_cast<unsigned>(slot)%PointShadowTextureLayout::cubes_per_atlas;
 if(p>=native_point_shadow_pages_.size()||s>=native_point_shadow_pages_[p].used.size())return;
 auto& page=native_point_shadow_pages_[p];page.used[s]=false;
 if(std::none_of(page.used.begin(),page.used.end(),[](bool used){return used;})){
  if(d3d12_)d3d12_->set_resource(PointShadowTextureLayout::first_srv+p,static_cast<const D3D12Texture*>(nullptr));
  else{ID3D11ShaderResourceView* empty=nullptr;
   context_->PSSetShaderResources(PointShadowTextureLayout::first_srv+p,1,&empty);}
  page={};page.overlay=p!=0;
 }
}
bool Renderer::begin_native_point_shadow_batch(std::uint64_t& generation,bool* pressure) noexcept {SCENE_ENTRY("begin_native_point_shadow_batch");
 if(pressure)*pressure=false;
 if(native_shadow_collecting_||native_point_shadow_active_||!native_point_shadows_enabled_)return false;
 if(FAILED(flush_native()))return false;
 if(d3d12_)for(UINT page=0;page<PointShadowTextureLayout::array_bindings;++page)
  d3d12_->set_resource(PointShadowTextureLayout::first_srv+page,static_cast<const D3D12Texture*>(nullptr));
 if(!d3d12_){
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
 }
 if(!native_point_shadow_generation_)native_point_shadow_generation_=g_point_shadow_pool_generation.fetch_add(1,std::memory_order_relaxed)+1;
 generation=native_point_shadow_generation_;
 if(!d3d12_&&adapter3_&&pressure){DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
  if(SUCCEEDED(adapter3_->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&memory))&&memory.Budget&&memory.CurrentUsage>memory.Budget){
   *pressure=true;point_shadow_capacity_failure("live WDDM budget pressure",HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY));
  }
 }
 return true;
}
void Renderer::publish_native_point_shadows(const std::vector<NativePointShadowPublication>& lights,
 const NativePointShadowStats& stats) noexcept {SCENE_ENTRY("publish_native_point_shadows");
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
 point_shadows_cumulative_.casting_lights=stats.casting_lights;
 native_frame_stats_.point_shadows.strength_zeroed+=stats.strength_zeroed;
}

bool Renderer::begin_native_point_shadow_faces(UINT slot,UINT light_index,
 const NativePointShadowFrame& faces) noexcept {SCENE_ENTRY("begin_native_point_shadow_faces");
 const UINT page=slot/PointShadowTextureLayout::cubes_per_atlas,cube=slot%PointShadowTextureLayout::cubes_per_atlas;
 if(native_shadow_collecting_||native_point_shadow_active_||page>=native_point_shadow_pages_.size()||
    cube>=native_point_shadow_pages_[page].used.size()||!native_point_shadow_pages_[page].used[cube]||
    light_index>=native_point_lights_.size()||
    (d3d12_?!native_point_shadow_pages_[page].depth12:!native_point_shadow_constants_||!native_depth_clear_vs_))return false;
 if(!d3d12_&&!native_point_clear_raster_){
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
 // As end_native_shadows: the per-EndScene abort is queued in scene mode.
 if(!commit&&scene_queue()){scene_post_command(scene_cmd::AbortPointShadowFaces{});return false;}
 SCENE_ENTRY("end_native_point_shadow_faces");
 if(!native_point_shadow_active_)return false;
 HRESULT hr=E_FAIL;
 try{if(commit&&!native_shadow_failed_)hr=flush_native_shadows();else discard_native();}
 catch(...){discard_native();}
 native_shadow_collecting_=false;native_point_shadow_active_=false;
 native_shadow_dirty_mask_=native_shadow_cleared_mask_=0;native_shadow_failed_=false;
 // Directional frame, valid bits and buffer were never modified. Color binds its
 // own b2 at submission; there is no directional save/restore operation here.
 return hr==S_OK;
}

// Typed collection aborts (scene_commands_world.h). The directional abort's
// body is end_native_shadows (renderer_shadows.cpp); both are queued only by
// the per-EndScene sink teardown in scene mode.
void scene_cmd::AbortPointShadowFaces::run(Renderer& r){(void)r.end_native_point_shadow_faces(false);}
void scene_cmd::AbortDirectionalShadows::run(Renderer& r){(void)r.end_native_shadows(false);}

// ---- Bounce light volume (cave_irradiance.h) ------------------------------

#endif // !EID3D11_SHADER_EMBED
void append_cave_irradiance_shader_programs(std::vector<ShaderProgram>& out){
 out.push_back({{kCaveInjectShader,sizeof(kCaveInjectShader)-1},nullptr,nullptr,"caveInjectCS","cs_5_0",0});
 // The glow pass shares the exit glow formula with the native pixel shader.
 static const std::string glow=std::string(kCaveExitGlowHlsl)+kCaveGlowShader;
 out.push_back({glow,nullptr,nullptr,"caveGlowVS","vs_4_0",0});
 out.push_back({glow,nullptr,nullptr,"caveGlowPS","ps_5_0",0});
}

#if !defined(EID3D11_SHADER_EMBED)
void Renderer::release_cave_irradiance() noexcept {
 if(context_){
  ID3D11ShaderResourceView* empty[5]{};context_->PSSetShaderResources(80,5,empty);
 }
 for(auto& t:cave_volume_)t.Reset();
 for(auto& v:cave_volume_srv_)v.Reset();
 for(auto& v:cave_volume_uav_)v.Reset();
 cave_inject_cs_.Reset();cave_inject_constants_.Reset();cave_volume_sampler_.Reset();
 cave_glow_vs_.Reset();cave_glow_ps_.Reset();cave_glow_constants_.Reset();cave_glow_depth_.Reset();
 cave_glow_blend_.Reset();cave_glow_raster_.Reset();cave_glow_pending_=cave_glow_failed_=false;
 cave_exit_buffer_.Reset();cave_exit_srv_.Reset();cave_exit_capacity_=0;cave_exit_count_=0;
 cave_vpl_buffer_.Reset();cave_vpl_srv_.Reset();cave_vpl_capacity_=0;cave_vpls_.clear();cave_vpl_key_=0;cave_vpl_lit_=0;
 cave_sources_uploaded_.clear();cave_sources_uploaded_capacity_=0;cave_grid_={};cave_volume_heights_=false;
 cave_source_buffer_.Reset();cave_source_srv_.Reset();cave_source_capacity_=0;
 cave_grid_buffer_.Reset();cave_grid_srv_.Reset();cave_grid_capacity_=0;
 cave_sources_.clear();
 cave_volume_size_={};cave_volume_slab_=0;cave_volume_quality_=0;cave_volume_outdoor_=false;
 cave_volume_frame_=UINT64_MAX;cave_volume_map_generation_=UINT64_MAX;
 cave_volume_active_=false;cave_volume_failed_=false;
}

namespace {
// One text resource of the active text archives (texts.res and the mod's).
std::string cave_text_resource(const char* name){
 ResourceStoreConfig mounts;
 if(!owned_text_resource_mounts(mounts))return {};
 ResourceStore store;ResourceFailure failure;ResourceLocation location;ResourceBlob blob;
 if(!store.open(mounts,failure)||!store.locate(name,location,failure)||!store.read(location,blob,failure)||!blob.bytes)return {};
 return std::string(reinterpret_cast<const char*>(blob.bytes->data()),blob.bytes->size());
}
// The game's Config\lights<region>.ini: its work directory is the parent of
// the Maps loose root (derive_owned_resource_mounts).
std::string cave_lights_ini(int region){
 const wchar_t* name=region==fog_region_gipat?L"gipat":region==fog_region_ingos?L"ingos":
  region==fog_region_suslanger?L"suslanger":nullptr;
 ResourceStoreConfig mounts;
 if(!name||!owned_resource_runtime_mounts(mounts)||mounts.loose_roots.empty())return {};
 std::wstring maps=mounts.loose_roots.back();
 while(!maps.empty()&&(maps.back()==L'\\'||maps.back()==L'/'))maps.pop_back();
 const auto separator=maps.find_last_of(L"\\/");
 if(separator==std::wstring::npos)return {};
 std::ifstream file(maps.substr(0,separator+1)+L"Config\\lights"+name+L".ini",std::ios::binary);
 if(!file)return {};
 return std::string(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
}
std::string cave_map_name(std::string name){
 const auto slash=name.find_last_of("/\\");
 if(slash!=std::string::npos)name.erase(0,slash+1);
 for(auto& c:name)if(c>='A'&&c<='Z')c=char(c+32);
 const auto dot=name.find_last_of('.');
 if(dot!=std::string::npos)name.resize(dot);
 return name;
}
}

// Colour flushes only (never inside a shadow collection), once per frame:
// after the point lights of this flush are uploaded. A new map or quality
// fills the whole volume; otherwise one round-robin slab is re-injected.
// Runs on every map (2026-10-26 user scope).
void Renderer::update_cave_irradiance() noexcept try {
 if(cave_volume_frame_==native_frame_index_)return;
 cave_volume_frame_=native_frame_index_;
 const auto& config=debug_overlay_.config();
 const int quality=std::clamp(configured_integer(config,"cave_gi",2),0,2);
 // Only frames that draw the world map's terrain (this frame or the last):
 // never the global map screen or its zone hit-test frame, whose flat zone
 // colours the bounce would overwrite (08638187 frames 029214/035403).
 const bool world_frame=!hit_test_active_&&ambient_terrain_frame_!=UINT64_MAX&&native_frame_index_-ambient_terrain_frame_<=1;
 const bool wanted=quality>0&&!ambient_menu_&&world_frame&&(d3d12_||feature_level_>=D3D_FEATURE_LEVEL_11_0)&&!cave_volume_failed_;
 if(!wanted){cave_volume_active_=false;cave_glow_pending_=false;return;}
 const bool outdoor=!native_world_cave_;
 // Per map: the terrain bounds and, for a cave, its exits to normal-sky
 // zones (map.txt).
 bool full=!cave_volume_active_||cave_volume_quality_!=quality||cave_volume_outdoor_!=outdoor;
 if(cave_volume_map_generation_!=ambient_map_generation_){
  AmbientWorldSource inputs;
  if(!bridge_ambient_world_source(inputs)||!inputs.terrain||inputs.terrain->sectors.empty()){cave_volume_active_=false;return;}
  auto low=inputs.terrain->sectors.front().bounds.minimum,high=inputs.terrain->sectors.front().bounds.maximum;
  for(const auto& sector:inputs.terrain->sectors)for(int k=0;k<3;++k){
   low[k]=std::min(low[k],sector.bounds.minimum[k]);high[k]=std::max(high[k],sector.bounds.maximum[k]);}
  cave_terrain_low_=low;cave_terrain_high_=high;cave_volume_map_generation_=ambient_map_generation_;
  cave_portals_.clear();cave_mushrooms_.clear();
  if(inputs.map){
   cave_portals_=parse_cave_portals(cave_text_resource("map.txt"),cave_map_name(inputs.map->terrain_resource));
   // The giant mushrooms (shipped MOB models, ambient::mushroom_model) have
   // no game light; their faint glow bounces like an emissive surface.
   for(const auto& object:inputs.map->objects)
    if(ambient::mushroom_model(object.visual_reference.c_str())&&std::isfinite(object.position[0])&&
       std::isfinite(object.position[1])&&std::isfinite(object.position[2]))
     cave_mushrooms_.push_back({float(object.position[0]),float(object.position[1]),float(object.position[2])});
  }
  full=true;
 }
 if(cave_light_table_region_!=native_world_region_){
  cave_light_table_region_=native_world_region_;
  cave_light_table_=parse_outdoor_light_table(cave_lights_ini(native_world_region_));
 }
 const auto layout=cave_volume_layout(cave_terrain_low_,cave_terrain_high_,quality);
 if(!layout.valid()){cave_volume_active_=false;return;}
 // The terrain height grid (1 m vertices) of this map, once built.
 const std::uint32_t height_width=ambient_tiles_x_*2u+1u,height_height=ambient_tiles_y_*2u+1u;
 // (D3D12: the shared grid texture ambient12_.height.)
 const bool height_grid=ambient_tiles_x_&&ambient_height_.size()==std::size_t(height_width)*height_height;
 ID3D11ShaderResourceView* heights_view=!d3d12_&&height_grid&&ambient_height_srv_?ambient_height_srv_.Get():nullptr;
 const D3D12Texture* heights12=d3d12_&&height_grid?ambient12_.height.get():nullptr;
 const bool heights=heights_view||heights12;
 // The height grid arrives after the map's first frames: fill again then.
 if(heights&&!cave_volume_heights_)full=true;
 cave_volume_heights_=heights;
 const auto height_at=[&](float x,float y){
  const auto ix=std::clamp<long>(std::lround(x),0,long(height_width)-1),iy=std::clamp<long>(std::lround(y),0,long(height_height)-1);
  return ambient_height_[std::size_t(iy)*height_width+std::size_t(ix)];
 };
 // Bounce sources: every published point light, plus the emissive-surface
 // clusters (lava and other self-lit ground) when the lava glow option does
 // not already publish them as point lights (their pulse is left out), plus
 // a cave's exits (colour.w -2) with the outdoor light of the hour.
 auto& sources=cave_sources_;sources.clear();
 sources.insert(sources.end(),native_point_lights_.begin(),native_point_lights_.end());
 const bool published=configured_integer(config,"emissive_lights",1)>0;
 if(!published&&emissive_generation_==ambient_map_generation_){
  const float strength=.45f*std::clamp(configured_floating_point(config,"emissive_light_strength",1.f),0.f,3.f);
  const std::uint32_t cell_tiles=emissive_level_>=2?2u:4u;
  for(std::size_t i=0;i<emissive_clusters_.size();++i){
   const auto colour=i<emissive_colors_.size()?emissive_colors_[i]:emissive::rendered_color(emissive_clusters_[i].emission,{});
   const auto shape=emissive::light_shape(emissive_clusters_[i],cell_tiles,colour);
   if(!(shape.radius>0))continue;
   NativePointLightGpu gpu{};
   gpu.center_inverse_radius_squared={shape.centre[0],shape.centre[1],shape.centre[2],1.f/(shape.radius*shape.radius)};
   gpu.color={shape.color[0]*strength,shape.color[1]*strength,shape.color[2]*strength,-1.f};
   sources.push_back(gpu);
  }
 }
 for(const auto& at:cave_mushrooms_){
  // Art (#208): the cave spore colour (ambient_particles.h), faint, 3 m
  // like the spores' source; centred 1 m above the model's base.
  NativePointLightGpu gpu{};
  gpu.center_inverse_radius_squared={at[0],at[1],at[2]+1.f,1.f/9.f};
  gpu.color={.45f*kMushroomGlow,.8f*kMushroomGlow,.65f*kMushroomGlow,-1.f};
  sources.push_back(gpu);
 }
 if(!outdoor&&heights&&!cave_portals_.empty()){
  const auto colour=outdoor_portal_color(cave_light_table_,native_world_hour_);
  for(const auto& portal:cave_portals_){
   // The opening's outer side: the rectangle's side toward the nearest map
   // edge, widened 3 m each way (the deploy rectangle is narrower than the
   // mouth), emitting inward (away from that edge).
   const float x=.5f*(portal.x1+portal.x2),y=.5f*(portal.y1+portal.y2);
   const float edges[4]={x-cave_terrain_low_[0],cave_terrain_high_[0]-x,y-cave_terrain_low_[1],cave_terrain_high_[1]-y};
   const int side=int(std::min_element(edges,edges+4)-edges);
   float cx=x,cy=y,ax=0,ay=0,nx=0,ny=0;
   if(side<2){cx=side==0?portal.x1:portal.x2;ay=.5f*(portal.y2-portal.y1)+3.f;nx=side==0?1.f:-1.f;}
   else{cy=side==2?portal.y1:portal.y2;ax=.5f*(portal.x2-portal.x1)+3.f;ny=side==2?1.f:-1.f;}
   const float width=2.f*std::max(ax,ay),reach=(kCavePortalReach+.5f*width)/kCaveBounceReach;
   NativePointLightGpu gpu{};
   gpu.center_inverse_radius_squared={cx,cy,height_at(cx,cy),1.f/(reach*reach)};
   gpu.color={colour[0],colour[1],colour[2],-2.f};
   gpu.shadow_center={ax,ay,kCavePortalHeight,0.f};
   gpu.shadow_slots={nx,ny,0.f,0.f};
   sources.push_back(gpu);
  }
 }
 HRESULT hr=S_OK;
 const auto fail=[&](HRESULT result,const char* what){
  cave_volume_failed_=true;cave_volume_active_=false;
  std::fprintf(stderr,"Bounce light volume: %s failed (0x%08lx); bounce light omitted.\n",what,static_cast<unsigned long>(result));
  bridge_append_probe_event("cave_gi_failed",what);
 };
 // D3D12 backend: the same grids, buffers and dispatch through the executor
 // (cave12_); the CPU sources, grid, virtual lights and exits are shared.
 auto& cave12=cave12_;
 if(d3d12_){
  if(cave12.programs.empty())append_cave_irradiance_shader_programs(cave12.programs);
  if(cave12.programs.size()<3)return fail(E_FAIL,"inject shader");
  if(!d3d12_->storage_supported(DXGI_FORMAT_R16G16B16A16_FLOAT))return fail(E_FAIL,"volume format not writable by compute");
 }else if(!cave_inject_cs_){
  std::vector<ShaderProgram> programs;append_cave_irradiance_shader_programs(programs);
  ComPtr<ID3DBlob> code,errors;
  if(FAILED(hr=compile_shader(programs[0],&code,&errors))||
     FAILED(hr=create_shader(device_.Get(),code->GetBufferPointer(),code->GetBufferSize(),nullptr,&cave_inject_cs_)))
   return fail(hr,"inject shader");
  D3D11_BUFFER_DESC constants{};constants.Usage=D3D11_USAGE_DYNAMIC;
  constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;constants.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  constants.ByteWidth=sizeof(CaveInjectConstants);
  if(FAILED(hr=device_->CreateBuffer(&constants,nullptr,&cave_inject_constants_)))return fail(hr,"inject constants");
  D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=D3D11_FLOAT32_MAX;
  if(FAILED(hr=device_->CreateSamplerState(&sampler,&cave_volume_sampler_)))return fail(hr,"sampler");
 }
 const bool resized=cave_volume_size_!=layout.size;
 if(resized){
  // Sized by the map (demand): three RGBA16F grids (L1 red, green, blue).
  for(auto& t:cave_volume_)t.Reset();
  for(auto& v:cave_volume_srv_)v.Reset();
  for(auto& v:cave_volume_uav_)v.Reset();
  for(auto& t:cave12.volume)t.reset();
  cave_volume_size_={};
  if(d3d12_){
   for(int c=0;c<3;++c)
    if(FAILED(hr=d3d12_->create_storage_texture(layout.size[0],layout.size[1],layout.size[2],DXGI_FORMAT_R16G16B16A16_FLOAT,cave12.volume[c])))
     return fail(hr,"volume textures");
  }else{
   D3D11_TEXTURE3D_DESC description{};description.Width=layout.size[0];description.Height=layout.size[1];
   description.Depth=layout.size[2];description.MipLevels=1;description.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
   description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
   for(int c=0;c<3;++c){
    if(FAILED(hr=device_->CreateTexture3D(&description,nullptr,&cave_volume_[c]))||
       FAILED(hr=device_->CreateShaderResourceView(cave_volume_[c].Get(),nullptr,&cave_volume_srv_[c]))||
       FAILED(hr=device_->CreateUnorderedAccessView(cave_volume_[c].Get(),nullptr,&cave_volume_uav_[c])))
     return fail(hr,"volume textures");
   }
  }
  cave_volume_size_=layout.size;full=true;
 }
 // The bounce grid: the point-light grid's binning over each source's
 // bounce reach (kCaveBounceReach radii), so a probe visits only the sources
 // that can reach it. Demand-sized buffers, grown to the largest seen.
 // Rebuilt and uploaded only when the sources changed (most frames reuse them).
 const bool sources_changed=sources.size()!=cave_sources_uploaded_.size()||
  (!sources.empty()&&std::memcmp(sources.data(),cave_sources_uploaded_.data(),sources.size()*sizeof(NativePointLightGpu))!=0);
 auto& grid=cave_grid_;
 if(sources_changed){
  std::vector<std::array<float,4>> reach;reach.reserve(sources.size());
  for(const auto& light:sources){auto r=light.center_inverse_radius_squared;r[3]/=kCaveBounceReach*kCaveBounceReach;reach.push_back(r);}
  grid={};
  if(!sources.empty()&&!build_point_light_grid(reach,grid))return fail(E_INVALIDARG,"bounce grid");
 }
 const auto ensure_buffer=[&](ComPtr<ID3D11Buffer>& buffer,ComPtr<ID3D11ShaderResourceView>& srv,UINT& capacity,
                             UINT count,UINT stride,bool dynamic,ComPtr<ID3D11UnorderedAccessView>* uav)->HRESULT{
  if(count<=capacity&&buffer)return S_OK;
  D3D11_BUFFER_DESC description{};description.ByteWidth=count*stride;description.StructureByteStride=stride;
  description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  description.Usage=dynamic?D3D11_USAGE_DYNAMIC:D3D11_USAGE_DEFAULT;description.CPUAccessFlags=dynamic?D3D11_CPU_ACCESS_WRITE:0;
  description.BindFlags=D3D11_BIND_SHADER_RESOURCE|(uav?D3D11_BIND_UNORDERED_ACCESS:0);
  ComPtr<ID3D11Buffer> next;ComPtr<ID3D11ShaderResourceView> view;HRESULT result;
  if(FAILED(result=device_->CreateBuffer(&description,nullptr,&next)))return result;
  if(FAILED(result=device_->CreateShaderResourceView(next.Get(),nullptr,&view)))return result;
  if(uav&&FAILED(result=device_->CreateUnorderedAccessView(next.Get(),nullptr,uav->ReleaseAndGetAddressOf())))return result;
  buffer=std::move(next);srv=std::move(view);capacity=count;return S_OK;
 };
 // D3D12: a demand-sized structured buffer (contents not kept on growth:
 // the caller uploads again then; capacity counts its elements).
 const auto ensure_buffer12=[&](D3D12BufferRef& buffer,UINT& capacity,UINT count,UINT stride)->HRESULT{
  if(count<=capacity&&buffer)return S_OK;
  D3D12BufferRef next;
  const HRESULT result=d3d12_->create_buffer(UINT64(count)*stride,stride,0,next);
  if(FAILED(result))return result;
  buffer=std::move(next);capacity=count;return S_OK;
 };
 const UINT source_count=static_cast<UINT>(sources.size());
 const UINT buffer_count=std::max<UINT>(source_count,1),grid_entries=static_cast<UINT>(std::max<std::size_t>(grid.data.size(),1));
 const UINT vpl_count=quality>=2?32u:16u;
 if(d3d12_){
  if(FAILED(hr=ensure_buffer12(cave12.sources,cave_source_capacity_,buffer_count,sizeof(NativePointLightGpu)))||
     FAILED(hr=ensure_buffer12(cave12.grid,cave_grid_capacity_,grid_entries,sizeof(UINT)))||
     FAILED(hr=ensure_buffer12(cave12.vpls,cave_vpl_capacity_,buffer_count*vpl_count,sizeof(CaveVirtualLight))))
   return fail(hr,"bounce buffers");
 }else if(FAILED(hr=ensure_buffer(cave_source_buffer_,cave_source_srv_,cave_source_capacity_,buffer_count,sizeof(NativePointLightGpu),true,nullptr))||
    FAILED(hr=ensure_buffer(cave_grid_buffer_,cave_grid_srv_,cave_grid_capacity_,grid_entries,sizeof(UINT),true,nullptr))||
    FAILED(hr=ensure_buffer(cave_vpl_buffer_,cave_vpl_srv_,cave_vpl_capacity_,buffer_count*vpl_count,sizeof(CaveVirtualLight),true,nullptr)))
  return fail(hr,"bounce buffers");
 D3D11_MAPPED_SUBRESOURCE mapped{};
 if(source_count&&(sources_changed||cave_source_capacity_!=cave_sources_uploaded_capacity_)){
  cave_sources_uploaded_=sources;cave_sources_uploaded_capacity_=cave_source_capacity_;
  if(d3d12_){
   const UINT zero=0;
   if(FAILED(hr=d3d12_->write_buffer(*cave12.sources,0,sources.data(),UINT64(source_count)*sizeof(NativePointLightGpu)))||
      FAILED(hr=d3d12_->write_buffer(*cave12.grid,0,grid.data.empty()?&zero:grid.data.data(),
                                     grid.data.empty()?sizeof(UINT):UINT64(grid.data.size())*sizeof(UINT))))
    return fail(hr,"source upload");
  }else{
   if(FAILED(hr=context_->Map(cave_source_buffer_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return fail(hr,"source map");
   std::memcpy(mapped.pData,sources.data(),source_count*sizeof(NativePointLightGpu));context_->Unmap(cave_source_buffer_.Get(),0);
   if(FAILED(hr=context_->Map(cave_grid_buffer_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return fail(hr,"grid map");
   if(!grid.data.empty())std::memcpy(mapped.pData,grid.data.data(),grid.data.size()*sizeof(UINT));
   else std::memset(mapped.pData,0,sizeof(UINT));
   context_->Unmap(cave_grid_buffer_.Get(),0);
  }
 }
 // The lamps' virtual lights, from the CPU height grid, rebuilt when the
 // sources, the grid (a new map or a water level step) or the count change.
 {
  std::uint64_t key=1469598103934665603ull;
  const std::uint64_t parts[5]={ambient_map_generation_,ambient_liquid_level_updates_,vpl_count,heights?1u:0u,
   std::uint64_t(cave_source_capacity_)};
  for(const auto part:parts){key^=part;key*=1099511628211ull;}
  if(sources_changed||key!=cave_vpl_key_){
   cave_vpl_key_=key;cave_vpl_lit_=0;
   cave_vpls_.assign(std::size_t(buffer_count)*vpl_count,CaveVirtualLight{});
   const float strength=std::clamp(configured_floating_point(config,"cave_gi_strength",kDefaultCaveBounceStrength),0.f,8.f);
   for(UINT n=0;n<source_count&&heights;++n){
    const auto& light=sources[n];
    if(light.color[3]<0)continue; // emissive surfaces and cave exits
    const float radius=1.f/std::sqrt(std::max(light.center_inverse_radius_squared[3],1e-12f));
    if(cave_lamp_virtual_lights(light.center_inverse_radius_squared.data(),radius,light.color.data(),strength,
       ambient_height_.data(),height_width,height_height,vpl_count,&cave_vpls_[std::size_t(n)*vpl_count]))++cave_vpl_lit_;
   }
   if(d3d12_){
    if(FAILED(hr=d3d12_->write_buffer(*cave12.vpls,0,cave_vpls_.data(),UINT64(cave_vpls_.size())*sizeof(CaveVirtualLight))))
     return fail(hr,"virtual light upload");
   }else{
    if(FAILED(hr=context_->Map(cave_vpl_buffer_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return fail(hr,"virtual light map");
    std::memcpy(mapped.pData,cave_vpls_.data(),cave_vpls_.size()*sizeof(CaveVirtualLight));
    context_->Unmap(cave_vpl_buffer_.Get(),0);
   }
  }
 }
 // The cave exits for the glow pass and the border fog (CaveExitGpu).
 if(sources_changed){
  std::vector<CaveExitGpu> exits;
  for(const auto& light:sources)if(light.color[3]<-1.5f){
   CaveExitGpu e{};
   std::copy_n(light.center_inverse_radius_squared.data(),3,e.centre);
   e.across[0]=light.shadow_center[0];e.across[1]=light.shadow_center[1];
   e.inward_height[0]=light.shadow_slots[0];e.inward_height[1]=light.shadow_slots[1];e.inward_height[2]=light.shadow_center[2];
   std::copy_n(light.color.data(),3,e.colour);
   exits.push_back(e);
  }
  cave_exit_count_=static_cast<UINT>(exits.size());
  if(!exits.empty()){
   if(d3d12_){
    if(FAILED(hr=ensure_buffer12(cave12.exits,cave_exit_capacity_,cave_exit_count_,sizeof(CaveExitGpu)))||
       FAILED(hr=d3d12_->write_buffer(*cave12.exits,0,exits.data(),UINT64(exits.size())*sizeof(CaveExitGpu))))
     return fail(hr,"exit buffer");
   }else{
    if(FAILED(hr=ensure_buffer(cave_exit_buffer_,cave_exit_srv_,cave_exit_capacity_,cave_exit_count_,sizeof(CaveExitGpu),true,nullptr)))
     return fail(hr,"exit buffer");
    if(FAILED(hr=context_->Map(cave_exit_buffer_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return fail(hr,"exit map");
    std::memcpy(mapped.pData,exits.data(),exits.size()*sizeof(CaveExitGpu));context_->Unmap(cave_exit_buffer_.Get(),0);
   }
  }
 }
 // Sun cascades for the sun bounce, outdoors, unless the depth array is
 // still the bound depth target (a read-write hazard D3D11 resolves by
 // unbinding the read; the D3D12 executor transitions the array itself).
 bool sun_maps=false;
 if(d3d12_)sun_maps=outdoor&&shadow12_.maps!=nullptr;
 else if(outdoor&&native_shadow_depth_srv_&&native_shadow_constants_){
  ComPtr<ID3D11DepthStencilView> bound;context_->OMGetRenderTargets(0,nullptr,&bound);
  ComPtr<ID3D11Resource> target,depth;
  if(bound)bound->GetResource(&target);
  native_shadow_depth_srv_->GetResource(&depth);
  sun_maps=!target||target.Get()!=depth.Get();
 }
 // The painted ground colour (albedo), once the ground cover painted it for this map.
 ID3D11ShaderResourceView* ground_color=!d3d12_&&gc_color_srv_&&gc_map_generation_==ambient_map_generation_&&!gc_color_unpainted_?
  gc_color_srv_.Get():nullptr;
 const D3D12Texture* ground_color12=d3d12_&&gc12_.map.color&&gc12_.map.color_ready&&gc12_.map.generation==ambient_map_generation_?
  gc12_.map.color.get():nullptr;
 CaveInjectConstants values{};
 for(int k=0;k<3;++k){values.origin[k]=layout.origin[k];values.size[k]=layout.size[k];}
 values.cell=layout.cell;values.vpl_count=vpl_count;
 values.grid_origin[0]=grid.origin[0];values.grid_origin[1]=grid.origin[1];values.grid_inverse_cell=grid.inverse_cell;
 values.albedo=kCaveBounceAlbedo;values.grid_width=grid.width;values.grid_height=grid.height;
 values.light_count=source_count;
 values.mode=(outdoor?kCaveModeOutdoor:0u)|(sun_maps?kCaveModeSunMaps:0u)|(ground_color||ground_color12?kCaveModeGroundColor:0u)|
  (heights?kCaveModeHeights:0u);
 {
  const auto& d=native_world_lighting_.sunlight_direction;
  const float length=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
  for(int k=0;k<3;++k){
   // Toward the sun, as the native shaders read it (nativeWorldSunDirection;
   // L-086: z > 0 by day).
   values.sun_toward[k]=length>1e-6f?d[k]/length:0.f;
   values.sun_color[k]=std::max(native_world_lighting_.sunlight_diffuse[k],0.f);
  }
 }
 values.point_strength=std::clamp(configured_floating_point(config,"cave_gi_strength",kDefaultCaveBounceStrength),0.f,8.f);
 values.sun_strength=std::clamp(configured_floating_point(config,"sun_bounce_strength",kDefaultSunBounceStrength),0.f,4.f);
 values.height_width=height_width;values.height_height=height_height;
 values.sun_samples=quality>=2?16u:8u;values.sun_radius=3.f*layout.cell;
 // The slab: all group rows on a full fill, else the next 1/slabs of them.
 const UINT groups_x=(layout.size[0]+3)/4,groups_y=(layout.size[1]+3)/4,groups_z=(layout.size[2]+3)/4;
 const UINT per_slab=(groups_y+cave_volume_slabs(quality)-1)/cave_volume_slabs(quality);
 const UINT slab_count=(groups_y+per_slab-1)/per_slab;
 UINT first_row=0,rows=groups_y;
 if(!full){
  cave_volume_slab_%=slab_count;
  first_row=cave_volume_slab_*per_slab;rows=std::min(per_slab,groups_y-first_row);
  cave_volume_slab_=(cave_volume_slab_+1)%slab_count;
 }else cave_volume_slab_=0;
 values.slab_first=first_row*4u;
 transition_profile_pass("cave_gi_inject");
 if(d3d12_){
  // A slab still compiling leaves the grids as they were (a full fill is
  // retried next frame); the bounce shows once a fill has run.
  d3d12_->clear_slots();
  d3d12_->set_constants(0,&values,sizeof(values));
  if(sun_maps)d3d12_->set_constants(1,&shadow12_.constants,sizeof(shadow12_.constants));
  d3d12_->set_resource(0,source_count?cave12.sources.get():nullptr);
  d3d12_->set_resource(1,source_count?cave12.grid.get():nullptr);
  d3d12_->set_resource(3,heights12);
  d3d12_->set_resource(4,ground_color12);
  d3d12_->set_resource(5,sun_maps?shadow12_.maps.get():nullptr);
  d3d12_->set_resource(6,cave12.vpls.get());
  for(UINT c=0;c<3;++c)d3d12_->set_unordered(c,cave12.volume[c].get());
  hr=rows?d3d12_->dispatch(cave12.programs[0],groups_x,rows,groups_z):S_OK;
  d3d12_->clear_slots();
  if(FAILED(hr))return fail(hr,"inject dispatch");
  if(hr!=S_OK){
   d3d12_->count("bounce light injections waiting for their pipeline");
   if(full){cave_volume_active_=false;cave_glow_pending_=false;return;} // nothing filled yet: a full fill again next frame
  }
 }else{
  if(FAILED(hr=context_->Map(cave_inject_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return fail(hr,"inject constants map");
  std::memcpy(mapped.pData,&values,sizeof(values));context_->Unmap(cave_inject_constants_.Get(),0);
  {ID3D11ShaderResourceView* empty[5]{};context_->PSSetShaderResources(80,5,empty); // the grids become UAVs
   context_->VSSetShaderResources(80,5,empty);context_->CSSetShaderResources(80,5,empty);}
  ID3D11Buffer* constant_buffers[2]={cave_inject_constants_.Get(),sun_maps?native_shadow_constants_.Get():nullptr};
  ID3D11ShaderResourceView* inputs[7]={source_count?cave_source_srv_.Get():nullptr,source_count?cave_grid_srv_.Get():nullptr,nullptr,
   heights_view,ground_color,sun_maps?native_shadow_depth_srv_.Get():nullptr,cave_vpl_srv_.Get()};
  context_->CSSetConstantBuffers(0,2,constant_buffers);
  context_->CSSetShaderResources(0,7,inputs);
  ID3D11UnorderedAccessView* outputs[3]={cave_volume_uav_[0].Get(),cave_volume_uav_[1].Get(),cave_volume_uav_[2].Get()};
  context_->CSSetUnorderedAccessViews(0,3,outputs,nullptr);
  context_->CSSetShader(cave_inject_cs_.Get(),nullptr,0);
  if(rows)context_->Dispatch(groups_x,rows,groups_z);
  ID3D11ShaderResourceView* no_inputs[7]{};ID3D11UnorderedAccessView* no_outputs[4]{};ID3D11Buffer* no_constants[2]{};
  context_->CSSetUnorderedAccessViews(0,4,no_outputs,nullptr);context_->CSSetShaderResources(0,7,no_inputs);
  context_->CSSetConstantBuffers(0,2,no_constants);context_->CSSetShader(nullptr,nullptr,0);
 }
 cave_volume_origin_=layout.origin;cave_volume_cell_=layout.cell;
 cave_volume_quality_=quality;cave_volume_outdoor_=outdoor;cave_volume_active_=true;
 cave_glow_pending_=!outdoor&&cave_exit_count_>0;
 if(full){
  std::size_t lamps=0;for(const auto& light:sources)if(light.color[3]>=0)++lamps;
  char line[288];
  std::snprintf(line,sizeof(line),"grid %ux%ux%u cell %.0f m, %s, %u slabs, %u sources (%zu point lights, %zu cave exits), heights %s, light table %s, %u of %zu lamps light ground",
   layout.size[0],layout.size[1],layout.size[2],layout.cell,outdoor?"outdoor":"cave",slab_count,source_count,
   native_point_lights_.size(),outdoor?std::size_t(0):cave_portals_.size(),heights?"yes":"no",cave_light_table_.valid?"yes":"no",
   cave_vpl_lit_,lamps);
  bridge_append_probe_event("cave_gi",line);
 }
}catch(...){cave_volume_active_=false;}

// Caves only, once per frame from the colour flush where the sky would
// draw: the daylight haze past each exit (kCaveGlowShader). Additive, behind
// everything (pixels still at the cleared far plane), no depth write.
void Renderer::draw_cave_exit_glow() noexcept try {
 if(!cave_glow_pending_||scene_composited_||rtv_.Get()!=scene_composite_.target()||!fog_frame_constants_)return;
 cave_glow_pending_=false;
 if(cave_glow_failed_)return;
 HRESULT hr=S_OK;
 if(!cave_glow_ps_){
  std::vector<ShaderProgram> programs;append_cave_irradiance_shader_programs(programs);
  const ShaderProgram& vs=programs[1];const ShaderProgram& ps=programs[2];
  ComPtr<ID3DBlob> vs_code,ps_code,errors;
  D3D11_BUFFER_DESC constants{};constants.Usage=D3D11_USAGE_DYNAMIC;constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  constants.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;constants.ByteWidth=sizeof(CaveGlowConstants);
  D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=TRUE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
  depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
  D3D11_BLEND_DESC blend{};auto& target=blend.RenderTarget[0];target.BlendEnable=TRUE;
  target.SrcBlend=target.DestBlend=target.SrcBlendAlpha=target.DestBlendAlpha=D3D11_BLEND_ONE;
  target.BlendOp=target.BlendOpAlpha=D3D11_BLEND_OP_ADD;target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_RED|
   D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
  D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=FALSE;
  if(FAILED(hr=compile_shader(vs,&vs_code,&errors))||FAILED(hr=compile_shader(ps,&ps_code,&errors))||
     FAILED(hr=create_shader(device_.Get(),vs_code->GetBufferPointer(),vs_code->GetBufferSize(),nullptr,&cave_glow_vs_))||
     FAILED(hr=create_shader(device_.Get(),ps_code->GetBufferPointer(),ps_code->GetBufferSize(),nullptr,&cave_glow_ps_))||
     FAILED(hr=device_->CreateBuffer(&constants,nullptr,&cave_glow_constants_))||
     FAILED(hr=device_->CreateDepthStencilState(&depth,&cave_glow_depth_))||
     FAILED(hr=device_->CreateBlendState(&blend,&cave_glow_blend_))||
     FAILED(hr=device_->CreateRasterizerState(&raster,&cave_glow_raster_))){
   cave_glow_failed_=true;cave_glow_ps_.Reset();
   std::fprintf(stderr,"Cave exit glow: setup failed (0x%08lx); the glow past cave exits is omitted.\n",static_cast<unsigned long>(hr));
   bridge_append_probe_event("cave_glow_failed","shader or state creation");
   return;
  }
 }
 D3D11_MAPPED_SUBRESOURCE mapped{};
 const CaveGlowConstants values{cave_exit_count_,1.f,{0,0}};
 if(FAILED(context_->Map(cave_glow_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return;
 std::memcpy(mapped.pData,&values,sizeof(values));context_->Unmap(cave_glow_constants_.Get(),0);
 transition_profile_pass("cave_glow");
 context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 context_->VSSetShader(cave_glow_vs_.Get(),nullptr,0);context_->PSSetShader(cave_glow_ps_.Get(),nullptr,0);
 context_->PSSetConstantBuffers(0,1,cave_glow_constants_.GetAddressOf());
 context_->PSSetConstantBuffers(10,1,fog_frame_constants_.GetAddressOf());
 context_->PSSetShaderResources(0,1,cave_exit_srv_.GetAddressOf());
 context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());
 context_->OMSetDepthStencilState(cave_glow_depth_.Get(),0);
 const float factor[4]={1,1,1,1};context_->OMSetBlendState(cave_glow_blend_.Get(),factor,0xffffffffu);
 context_->RSSetState(cave_glow_raster_.Get());
 D3D11_VIEWPORT viewport=target_viewport();context_->RSSetViewports(1,&viewport);
 context_->Draw(3,0);
 ID3D11ShaderResourceView* none=nullptr;context_->PSSetShaderResources(0,1,&none);
 profiler_.add_draw(1,1);profiler_.add_batch();
}catch(...){}

// draw_cave_exit_glow on D3D12: the same program, constants and fixed state
// (additive colour, depth tested LESS_EQUAL, never written, no cull or depth
// clip) into the bound world targets, once per frame where the sky draws.
void Renderer::draw_cave_exit_glow_d3d12() noexcept try {
 if(!cave_glow_pending_||scene_composited_||!world_d3d12_bound_||!cave12_.exits)return;
 cave_glow_pending_=false;
 if(cave12_.programs.size()<3)return;
 const CaveGlowConstants values{cave_exit_count_,1.f,{0,0}};
 transition_profile_pass("cave_glow");
 d3d12_->clear_slots();
 d3d12_->set_constants(0,&values,sizeof(values));
 d3d12_->set_constants(10,&fog_frame_d3d12_,sizeof(fog_frame_d3d12_));
 d3d12_->set_resource(0,cave12_.exits.get());
 D3D12NativeState state;
 state.raster.FillMode=D3D11_FILL_SOLID;state.raster.CullMode=D3D11_CULL_NONE;state.raster.DepthClipEnable=FALSE;
 state.depth.DepthEnable=TRUE;state.depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;state.depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
 auto& blend=state.custom_blend;blend.custom=1;
 blend.source=blend.destination=blend.source_alpha=blend.destination_alpha=D3D11_BLEND_ONE;
 blend.write_mask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
 const HRESULT hr=d3d12_->draw(cave12_.programs[1],cave12_.programs[2],state,nullptr,3);
 d3d12_->clear_slots();
 if(FAILED(hr))d3d12_->omit("cave exit glow (draw failed)",false);
 else if(hr!=S_OK)d3d12_->count("cave exit glows waiting for their pipeline");
 else{d3d12_->count("cave exit glows");profiler_.add_draw(1,1);profiler_.add_batch();}
}catch(...){}

// The grids, the exits and their sampler for every D3D12 stage that runs the
// native source (bind_cave_irradiance_stages; one table serves all stages):
// none while the volume is off (the sampling strength is 0 then).
void Renderer::bind_cave_irradiance_d3d12() noexcept {
 for(UINT c=0;c<3;++c)d3d12_->set_resource(80+c,cave_volume_active_?cave12_.volume[c].get():nullptr);
 d3d12_->set_resource(84,cave_volume_active_&&cave_exit_count_?cave12_.exits.get():nullptr);
 D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
 sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=D3D11_FLOAT32_MAX;
 d3d12_->set_sampler(13,sampler);
}

// The sampling constants (the tail of the point-light constants): the layout
// the grids were injected with, or strength 0. The strengths are applied at
// injection; flags: bit 0 bounce_debug, bit 1 outdoors, bits 8+ the exit count
// (outdoors).
CaveSampleConstants Renderer::cave_sample_constants() const noexcept {
 CaveSampleConstants values{};
 if(!cave_volume_active_||!(cave_volume_cell_>0)||hit_test_active_)return values;
 for(int k=0;k<3;++k){
  values.origin[k]=cave_volume_origin_[k];
  values.inverse_extent[k]=cave_volume_size_[k]?1.f/(cave_volume_size_[k]*cave_volume_cell_):0.f;
 }
 values.strength=1.f;
 values.flags=float((configured_boolean(debug_overlay_.config(),"bounce_debug",false)?1u:0u)|(cave_volume_outdoor_?2u:0u)|
  ((cave_volume_outdoor_?0u:cave_exit_count_)<<8));
 return values;
}

// Every colour flush: the grids and their sampler for the native pixel shader
// (none while the volume is off; the strength in the constants is 0 then).
void Renderer::bind_cave_irradiance() noexcept {bind_cave_irradiance_stages(true,false,false);}
// The same inputs for the stages of passes that run the native source
// outside the world flush: ground cover (per-vertex lamps, gcPrepCS fog) and
// critters. Compute also takes the point-light constants (b5) that hold the
// volume's sampling constants.
void Renderer::bind_cave_irradiance_stages(bool pixel,bool vertex,bool compute) noexcept {
 if(!context_)return;
 ID3D11ShaderResourceView* grids[5]{};
 if(cave_volume_active_)for(int c=0;c<3;++c)grids[c]=cave_volume_srv_[c].Get();
 if(cave_volume_active_&&cave_exit_count_)grids[4]=cave_exit_srv_.Get();
 ID3D11SamplerState* sampler=cave_volume_active_?cave_volume_sampler_.Get():nullptr;
 if(pixel){context_->PSSetShaderResources(80,5,grids);if(sampler)context_->PSSetSamplers(13,1,&sampler);}
 if(vertex){context_->VSSetShaderResources(80,5,grids);context_->VSSetSamplers(13,1,&sampler);}
 if(compute){
  context_->CSSetShaderResources(80,5,grids);context_->CSSetSamplers(13,1,&sampler);
  context_->CSSetConstantBuffers(5,1,native_point_light_constants_.GetAddressOf());
 }
}
#endif // !EID3D11_SHADER_EMBED
} // namespace owned

