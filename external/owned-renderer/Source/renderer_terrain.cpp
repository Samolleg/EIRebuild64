// Renderer terrain: contact tiles and atlas, terrain tile families and blend, terrain frame and tessellation.
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
#include "graphics_settings.h"
#include "ui_language.h"
#include "game_focus.h"
#include "cursor_confinement.h"
#include "combat_ui.h"
#include "world_texture_sampler.h"
#include "texture_mips.h"
#include "character_pane_owned_text.h"
#include "owned_resource_runtime.h"
#include "material_library.h"
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
#include "terrain2.h"
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
namespace {
// NativeTerrainFrameValues (register t127 in the native source).
constexpr UINT kNativeTerrainFrameSlot=PointShadowTextureLayout::terrain_frame_srv;
// The precipitation shelter map for wet surfaces (nativeShelter, t70).
constexpr UINT kNativeShelterSlot=70;
constexpr UINT kNativeRainRingsSlot=71; // the rain ring texture (nativeRainRings)
}  // namespace

#if !defined(EID3D11_SHADER_EMBED)
HRESULT Renderer::build_contact_tiles(const TerrainCpuAsset& terrain,UINT tiles_x,UINT tiles_y,ContactBlendMaps& maps,
 std::vector<std::array<std::uint32_t,4>>* rows){
  std::uint64_t used=0;
  auto tiles=owned_terrain_contact_tiles(terrain,tiles_x,tiles_y,used);
  return build_contact_tiles(std::move(tiles),used,tiles_x,tiles_y,maps,rows);
}

// The same grid from rows the neighbour stream computed (ground_cover_neighbour.h).
HRESULT Renderer::build_contact_tiles(std::vector<std::array<std::uint32_t,4>> tiles,std::uint64_t used,
 UINT tiles_x,UINT tiles_y,ContactBlendMaps& maps,std::vector<std::array<std::uint32_t,4>>* rows){
 HRESULT hr=S_OK;
  if(!used)return S_OK;
  if(tiles.size()!=std::size_t(tiles_x)*tiles_y)return E_INVALIDARG;
  if(d3d12_){ // D3D12 backend: the grid as a D3D12 texture
   if(FAILED(hr=d3d12_->create_texture(tiles_x,tiles_y,DXGI_FORMAT_R32G32B32A32_UINT,maps.tiles12))||
      FAILED(hr=d3d12_->write_texture(*maps.tiles12,RECT{0,0,LONG(tiles_x),LONG(tiles_y)},tiles.data(),tiles_x*16u)))return hr;
   maps.pending=used;
   if(rows)*rows=std::move(tiles);
   return S_OK;
  }
  D3D11_TEXTURE2D_DESC d{};d.Width=tiles_x;d.Height=tiles_y;d.MipLevels=d.ArraySize=1;
  d.Format=DXGI_FORMAT_R32G32B32A32_UINT;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  const D3D11_SUBRESOURCE_DATA initial{tiles.data(),tiles_x*16u,0};
  ComPtr<ID3D11Texture2D> texture;
  if(FAILED(hr=device_->CreateTexture2D(&d,&initial,&texture))||
     FAILED(hr=device_->CreateShaderResourceView(texture.Get(),nullptr,&maps.tiles)))return hr;
  maps.tiles_serial=++terrain2_tiles_serials_;
  maps.pending=used;
  if(rows)*rows=std::move(tiles); // terrain material blend: the active map keeps its rows
 return S_OK;
}

HRESULT Renderer::copy_contact_atlas(ID3D11Texture2D* source,UINT source_slice,UINT material,ContactBlendMaps& maps,
 char* detail,std::size_t detail_size){
 HRESULT hr=S_OK;
 const std::uint64_t bit=std::uint64_t{1}<<material;
  D3D11_TEXTURE2D_DESC s{};source->GetDesc(&s);
  if(maps.pool_refused)return S_FALSE; // named once (terrain2_pool_range)
  if(!maps.texture){
   UINT skip=0;
   while(((s.Width>>skip)>contact_blend_atlas_side||(s.Height>>skip)>contact_blend_atlas_side)&&skip+1<s.MipLevels)++skip;
   D3D11_TEXTURE2D_DESC a{};a.Width=std::max(1u,s.Width>>skip);a.Height=std::max(1u,s.Height>>skip);
   a.MipLevels=s.MipLevels-skip;a.ArraySize=0;
   for(std::uint32_t m=0;m<contact_blend_max_materials;++m)if((maps.pending>>m)&1u)a.ArraySize=m+1;
   a.Format=s.Format;
   // A slice range of the terrain 2.0 pool of this format, size and levels.
   if(FAILED(hr=terrain2_pool_range(maps,a,&maps==&contact_maps_?"active":"neighbour")))return hr;
   if(maps.pool_refused)return S_FALSE;
  }
  // Each page from its mip of the atlas size: full density up to the cap,
  // and mixed page sizes on one map (1024 and 2048 HD pages) share the array.
  // A page smaller than the array or of another format stays omitted.
  UINT skip=0;
  while(skip<s.MipLevels&&std::max(1u,s.Width>>skip)>maps.width)++skip;
  maps.pending&=~bit;
  if(s.Format!=maps.format||skip+maps.mips>s.MipLevels||
     std::max(1u,s.Width>>skip)!=maps.width||std::max(1u,s.Height>>skip)!=maps.height){
   if(detail&&detail_size)
    std::snprintf(detail,detail_size,"material=%u page=%ux%u mips=%u format=%u atlas=%ux%u mips=%u format=%u",
     material,s.Width,s.Height,s.MipLevels,unsigned(s.Format),maps.width,maps.height,maps.mips,unsigned(maps.format));
   return S_FALSE;
  }
  for(UINT mip=0;mip<maps.mips;++mip)
   context_->CopySubresourceRegion(maps.texture.Get(),D3D11CalcSubresource(mip,maps.base+material,maps.mips),0,0,0,
    source,D3D11CalcSubresource(mip+skip,source_slice,s.MipLevels),nullptr);
  maps.ready|=bit;
 return S_OK;
}

void Renderer::release_contact_blend_maps() noexcept{
 contact_maps_.tiles.Reset();contact_maps_.texture.Reset();contact_maps_.atlas.Reset();contact_maps_.tiles12.reset();contact_maps_.atlas12.reset();
 contact_maps_.ready=0;contact_maps_.pending=0;contact_maps_.mips=0;
 contact_maps_.seam_margin.clear();contact_maps_.seam_margin_sides=0;
 contact_maps_.cells.clear();contact_maps_.borrowed.clear();
 contact_tile_rows_.clear();contact_tiles_x_=contact_tiles_y_=0;tile_blend_ready_=tile_blend_rows_=false;
 tile_blend_input_.reset();tile_blend_vertex_.clear();tile_blend_edge_.clear();tile_blend_drawable_.clear();
 terrain2_map_=Terrain2Map{};terrain2_marker_logged_=false;contact_maps_.rows_own=false;
 contact_maps_.pool=-1;contact_maps_.base=contact_maps_.slices=0;contact_maps_.pool_refused=false;
 trim_terrain2_pools();
}

// ---- Terrain 2.0 pools (D3D11; Terrain2Pool) -------------------------------

// A pool nobody's range uses any more is released (its memory back before
// another map's atlas grows a pool).
void Renderer::trim_terrain2_pools() noexcept{
 for(unsigned p=0;p<kTerrain2Pools;++p){
  bool used=contact_maps_.pool==int(p)&&contact_maps_.texture;
  for(const auto& n:gc_neighbours_)used=used||(n.contact.pool==int(p)&&n.contact.texture);
  if(!used&&terrain2_pools_[p].atlas){
   const auto& pool=terrain2_pools_[p];
   char text[160];std::snprintf(text,sizeof(text),"pool=%u released format=%u %ux%u mips=%u slices=%u",p,unsigned(pool.format),pool.width,pool.height,pool.mips,pool.slices);
   bridge_append_probe_event("terrain2_pool",text);
   terrain2_pools_[p]=Terrain2Pool{};
  }
 }
}

// A range's atlas view, and its heights' read view and the bake's write
// view (level 0).
HRESULT Renderer::terrain2_range_views(const Terrain2Pool& pool,ContactBlendMaps& maps) noexcept{
 D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=pool.format;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
 view.Texture2DArray.MipLevels=pool.mips;view.Texture2DArray.FirstArraySlice=maps.base;view.Texture2DArray.ArraySize=maps.slices;
 maps.texture=pool.atlas;maps.atlas.Reset();
 return device_->CreateShaderResourceView(pool.atlas.Get(),&view,&maps.atlas);
}
HRESULT Renderer::terrain2_height_views(const Terrain2Pool& pool,const ContactBlendMaps& maps,Terrain2Map& heights) noexcept{
 HRESULT hr=S_OK;
 heights.height_srv.Reset();heights.height_uav.Reset();
 D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R8_UNORM;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
 view.Texture2DArray.MipLevels=pool.mips;view.Texture2DArray.FirstArraySlice=maps.base;view.Texture2DArray.ArraySize=maps.slices;
 D3D11_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=DXGI_FORMAT_R8_UNORM;u.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
 u.Texture2DArray.FirstArraySlice=maps.base;u.Texture2DArray.ArraySize=maps.slices;
 if(FAILED(hr=device_->CreateShaderResourceView(pool.heights.Get(),&view,&heights.height_srv))||
    FAILED(hr=device_->CreateUnorderedAccessView(pool.heights.Get(),&u,&heights.height_uav))){heights.height_srv.Reset();heights.height_uav.Reset();}
 return hr;
}

// maps' atlas (atlas: its size, levels, format and slice count) as a slice
// range of the pool of that format, size and levels: first fit among the
// ranges the active map and the neighbours hold (a released map's range is
// reused); a pool too small grows to exactly the slices in use plus this
// range (a new pair of arrays, the ranges copied over, every holder's views
// made again: old + new memory for a moment). The range's
// heights start flat (.5). A fifth kind of atlas is named once and that map
// shows the marker colour (update_terrain2_table). Logged per grow.
HRESULT Renderer::terrain2_pool_range(ContactBlendMaps& maps,const D3D11_TEXTURE2D_DESC& atlas,const char* name) noexcept try{
 HRESULT hr=S_OK;
 trim_terrain2_pools();
 struct Holder{ContactBlendMaps* maps;Terrain2Map* heights;};
 std::vector<Holder> holders{{&contact_maps_,&terrain2_map_}};
 for(auto& n:gc_neighbours_)holders.push_back({&n.contact,&n.terrain2});
 int p=-1;
 for(unsigned k=0;k<kTerrain2Pools&&p<0;++k){
  const auto& pool=terrain2_pools_[k];
  if(pool.atlas&&pool.format==atlas.Format&&pool.width==atlas.Width&&pool.height==atlas.Height&&pool.mips==atlas.MipLevels)p=int(k);
 }
 for(unsigned k=0;k<kTerrain2Pools&&p<0;++k)if(!terrain2_pools_[k].atlas)p=int(k);
 if(p<0){
  maps.pool_refused=true;
  char text[256];std::snprintf(text,sizeof(text),"map=%s atlas format=%u %ux%u mips=%u: all %u pools hold other kinds; terrain 2.0 shows the marker colour on this map",
   name,unsigned(atlas.Format),atlas.Width,atlas.Height,atlas.MipLevels,kTerrain2Pools);
  bridge_append_probe_event("terrain2_pool_full",text);
  return S_OK;
 }
 Terrain2Pool& pool=terrain2_pools_[std::size_t(p)];
 const UINT want=atlas.ArraySize;
 // The ranges in use, in order; first fit.
 std::vector<std::pair<UINT,UINT>> used;
 for(const auto& h:holders)if(h.maps!=&maps&&h.maps->pool==p&&h.maps->texture)used.push_back({h.maps->base,h.maps->base+h.maps->slices});
 std::sort(used.begin(),used.end());
 UINT base=0;
 for(const auto& r:used){if(r.first>=base+want)break;base=std::max(base,r.second);}
 if(!pool.atlas||base+want>pool.slices){
  // Exactly the demand (no doubling: its padding held up to ~90 MB unused).
  const UINT slices=base+want;
  if(slices>D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION)return E_OUTOFMEMORY;
  D3D11_TEXTURE2D_DESC a{};a.Width=atlas.Width;a.Height=atlas.Height;a.MipLevels=atlas.MipLevels;a.ArraySize=slices;
  a.Format=atlas.Format;a.SampleDesc.Count=1;a.Usage=D3D11_USAGE_DEFAULT;a.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  D3D11_TEXTURE2D_DESC h=a;h.Format=DXGI_FORMAT_R8_UNORM;
  h.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_RENDER_TARGET;h.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
  Terrain2Pool grown{atlas.Format,atlas.Width,atlas.Height,atlas.MipLevels,slices};
  if(FAILED(hr=device_->CreateTexture2D(&a,nullptr,&grown.atlas))||FAILED(hr=device_->CreateTexture2D(&h,nullptr,&grown.heights))||
     FAILED(hr=device_->CreateShaderResourceView(grown.atlas.Get(),nullptr,&grown.atlas_srv))||
     FAILED(hr=device_->CreateShaderResourceView(grown.heights.Get(),nullptr,&grown.heights_srv)))return hr;
  for(const auto& r:used)for(UINT slice=r.first;slice<r.second;++slice)for(UINT mip=0;mip<a.MipLevels;++mip){
   const UINT sub=D3D11CalcSubresource(mip,slice,a.MipLevels);
   context_->CopySubresourceRegion(grown.atlas.Get(),sub,0,0,0,pool.atlas.Get(),sub,nullptr);
   context_->CopySubresourceRegion(grown.heights.Get(),sub,0,0,0,pool.heights.Get(),sub,nullptr);
  }
  const UINT old=pool.slices;
  pool=std::move(grown);
  for(auto& holder:holders)if(holder.maps!=&maps&&holder.maps->pool==p&&holder.maps->texture){
   if(FAILED(hr=terrain2_range_views(pool,*holder.maps)))return hr;
   if(holder.heights->height_srv&&FAILED(hr=terrain2_height_views(pool,*holder.maps,*holder.heights)))return hr;
  }
  UINT in_use=want;for(const auto& r:used)in_use+=r.second-r.first;
  const double slice_mb=double(a.Width)*a.Height*(a.Format==DXGI_FORMAT_BC1_UNORM?.5:a.Format==DXGI_FORMAT_BC3_UNORM||a.Format==DXGI_FORMAT_BC2_UNORM?1.:4.)*4/3/1048576.0+
                        double(a.Width)*a.Height*4/3/1048576.0;
  char text[256];std::snprintf(text,sizeof(text),"pool=%d map=%s format=%u %ux%u mips=%u slices=%u->%u in_use=%u (%.1f MB, %.1f MB padding)",
   p,name,unsigned(a.Format),a.Width,a.Height,a.MipLevels,old,slices,in_use,slice_mb*slices,slice_mb*(slices-in_use));
  bridge_append_probe_event("terrain2_pool",text);
 }
 maps.pool=p;maps.base=base;maps.slices=want;
 maps.width=atlas.Width;maps.height=atlas.Height;maps.mips=atlas.MipLevels;maps.format=atlas.Format;
 if(FAILED(hr=terrain2_range_views(pool,maps))){maps.texture.Reset();maps.atlas.Reset();maps.pool=-1;return hr;}
 // Its heights start flat (a range may hold an earlier map's); the bake
 // makes its own views (bake_terrain2_heights: the map may not be listed yet).
 {
  D3D11_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=DXGI_FORMAT_R8_UNORM;u.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
  u.Texture2DArray.FirstArraySlice=base;u.Texture2DArray.ArraySize=want;
  ComPtr<ID3D11UnorderedAccessView> clear;
  if(FAILED(hr=device_->CreateUnorderedAccessView(pool.heights.Get(),&u,&clear))){maps.texture.Reset();maps.atlas.Reset();maps.pool=-1;return hr;}
  const float flat[4]{.5f,.5f,.5f,.5f};context_->ClearUnorderedAccessViewFloat(clear.Get(),flat);
 }
 return S_OK;
}catch(...){return E_OUTOFMEMORY;}

// ---- Terrain 2.0 (terrain2.h) ---------------------------------------------

#endif // !EID3D11_SHADER_EMBED
void append_terrain2_shader_programs(std::vector<ShaderProgram>& out){
 static constexpr const char* entries[6]{"terrain2BakeLoadCS","terrain2BakeFftCS","terrain2BakeGaussCS",
  "terrain2BakeCombineCS","terrain2BakeReduceCS","terrain2BakeWriteCS"};
 for(const char* entry:entries)out.push_back({{kTerrain2HeightShader,sizeof(kTerrain2HeightShader)-1},nullptr,nullptr,entry,"cs_5_0",0});
}

#if !defined(EID3D11_SHADER_EMBED)
// A map's material heights (kTerrain2HeightShader; the active map's and
// each neighbour's), once a map per material cell whose atlas slice is
// copied: a parallel R8 array with its atlas's size, slices and mips, so a
// cell's colour and height share one uv. The bake time (GPU work waited
// for) and the array's size are logged once a batch. A failure is logged
// once a map; its ground then blends with the flat height .5 (the array's
// clear value: the field and edge masks alone, visibly flatter).
void Renderer::bake_terrain2_heights(Terrain2Map& map,const ContactBlendMaps& contact,const char* name) noexcept try{
 map.dirty=false;map.seen_ready=contact.ready;
 if(map.failed||!contact.texture||!contact.atlas)return;
 std::vector<const Terrain2BakeCell*> pending;
 for(const auto& c:map.bake_cells)
  if(c.slice<64&&((contact.ready>>c.slice)&1u)&&!map.baked.count(c.key))pending.push_back(&c);
 if(pending.empty())return;
 HRESULT hr=S_OK;
 const auto fail=[&](const char* what){
  map.failed=true;
  char text[224];std::snprintf(text,sizeof(text),"map=%s %s failed (0x%08lx); terrain 2.0 blends with flat material heights",name,what,static_cast<unsigned long>(hr));
  bridge_append_probe_event("terrain2_height_omitted",text);
 };
 if(!terrain2_bake_cs_[0]){
  std::vector<ShaderProgram> programs;append_terrain2_shader_programs(programs);
  for(std::size_t n=0;n<programs.size()&&SUCCEEDED(hr);++n){
   ComPtr<ID3DBlob> code;
   if(SUCCEEDED(hr=compile_shader(programs[n],&code)))
    hr=create_shader(device_.Get(),code->GetBufferPointer(),code->GetBufferSize(),nullptr,&terrain2_bake_cs_[n]);
  }
  D3D11_BUFFER_DESC constants{};constants.ByteWidth=96;constants.Usage=D3D11_USAGE_DEFAULT;constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  if(SUCCEEDED(hr))hr=device_->CreateBuffer(&constants,nullptr,&terrain2_bake_constants_);
  D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
  if(SUCCEEDED(hr))hr=device_->CreateSamplerState(&sampler,&terrain2_bake_sampler_);
  if(FAILED(hr)){for(auto& cs:terrain2_bake_cs_)cs.Reset();return fail("height bake shaders");}
 }
 const UINT width=contact.width,height=contact.height;
 if(width%128u||height!=width)return fail("height bake atlas layout");
 // The heights: the contact atlas's range of its pool's R8 array (flat until baked).
 if(!map.height_uav&&(contact.pool<0||FAILED(hr=terrain2_height_views(terrain2_pools_[std::size_t(contact.pool)],contact,map))))
  return fail("height array");
 // Work planes (4 complex 256x256) and the statistics, for this batch only.
 ComPtr<ID3D11Buffer> planes,stats;ComPtr<ID3D11UnorderedAccessView> planes_uav,stats_uav;
 {
  D3D11_BUFFER_DESC b{};b.ByteWidth=4u*65536u*8u;b.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
  b.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;b.StructureByteStride=8;
  D3D11_BUFFER_DESC s{};s.ByteWidth=8u*4u;s.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
  s.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;s.StructureByteStride=4;
  if(FAILED(hr=device_->CreateBuffer(&b,nullptr,&planes))||FAILED(hr=device_->CreateUnorderedAccessView(planes.Get(),nullptr,&planes_uav))||
     FAILED(hr=device_->CreateBuffer(&s,nullptr,&stats))||FAILED(hr=device_->CreateUnorderedAccessView(stats.Get(),nullptr,&stats_uav)))
   return fail("height bake scratch");
 }
 // The GPU work already queued is waited for first, so the logged time is the bake's own.
 // Bounded: a GPU that never completes the queued work (07-07, 2026-11-14:
 // the host spun here for 40 min) names the stall and skips the bake this
 // frame (map.dirty: tried again) instead of spinning forever.
 D3D11_QUERY_DESC done_desc{D3D11_QUERY_EVENT,0};ComPtr<ID3D11Query> done;
 const auto wait=[&](const char* phase)->bool{
  if(!done&&FAILED(device_->CreateQuery(&done_desc,&done)))return true;
  context_->End(done.Get());BOOL finished=FALSE;
  const auto begin=std::chrono::steady_clock::now();
  constexpr auto kLimit=std::chrono::seconds(5);
  while(context_->GetData(done.Get(),&finished,sizeof(finished),0)==S_FALSE){
   if(std::chrono::steady_clock::now()-begin>kLimit){
    char text[192];std::snprintf(text,sizeof(text),"map=%s GPU work %s the height bake not finished after 5 s; bake skipped this frame",name,phase);
    bridge_append_probe_event("terrain2_height_bake_wait_timeout",text);
    return false;
   }
   std::this_thread::yield();
  }
  return true;
 };
 if(!wait("queued before")){map.dirty=true;return;}
 const auto start=std::chrono::steady_clock::now();
 // The height pools are written: the map table's (t90-t93) unbound.
 ID3D11ShaderResourceView* empty_ps[kTerrain2Pools]{};context_->PSSetShaderResources(90,kTerrain2Pools,empty_ps);
 ID3D11ShaderResourceView* source=contact.atlas.Get();
 ID3D11UnorderedAccessView* uavs[3]{planes_uav.Get(),stats_uav.Get(),map.height_uav.Get()};
 ID3D11Buffer* constant_buffer=terrain2_bake_constants_.Get();ID3D11SamplerState* sampler=terrain2_bake_sampler_.Get();
 context_->CSSetShaderResources(0,1,&source);context_->CSSetUnorderedAccessViews(0,3,uavs,nullptr);
 context_->CSSetConstantBuffers(0,1,&constant_buffer);context_->CSSetSamplers(0,1,&sampler);
 struct Constants{float lo[2],size[2],dir[2],fold,aw;std::uint32_t n,slice,src,dst;float sigma,weight;std::uint32_t inverse,axis;
  std::uint32_t origin[2],side,mode;float amp,clip,pad[2];}k{};
 static_assert(sizeof(Constants)==96);
 const auto run=[&](unsigned shader,UINT groups_x,UINT groups_y){
  context_->UpdateSubresource(constant_buffer,0,nullptr,&k,0,0);
  context_->CSSetShader(terrain2_bake_cs_[shader].Get(),nullptr,0);context_->Dispatch(groups_x,groups_y,1);
 };
 enum{Load,Fft,Gauss,Combine,Reduce,Write};
 const auto fft=[&](std::uint32_t plane,bool inverse){
  k.src=plane;k.inverse=inverse;for(std::uint32_t axis=0;axis<2;++axis){k.axis=axis;run(Fft,256,1);}
 };
 const auto gauss=[&](std::uint32_t from,std::uint32_t to,float sigma){k.src=from;k.dst=to;k.sigma=sigma;run(Gauss,16,16);};
 const auto combine=[&](std::uint32_t mode){k.mode=mode;run(Combine,16,16);};
 const auto reduce=[&](std::uint32_t mode){k.mode=mode;run(Reduce,1,1);};
 const float aw=float(width),margin=tile_blend_margin_*.125f,size=.125f-2*margin;
 const UINT slot=width/8u;
 for(const Terrain2BakeCell* c:pending){
  const Terrain2BakeStyle style=terrain2_bake_style(c->style);
  k=Constants{};k.aw=aw;k.slice=c->slice;k.amp=style.amp;k.clip=style.dc;
  k.lo[0]=(c->pos%8u)*.125f+margin;k.lo[1]=1-(c->pos/8u+1)*.125f+margin;k.size[0]=k.size[1]=size;k.dir[0]=k.dir[1]=1;
  if(c->folded){
   // terrainTileBlendCellFrom: lo at the quadrant's outer corner, dir to the slot centre.
   const float cx=(c->quadrant==1||c->quadrant==2)?1.f:0.f,cy=c->quadrant>=2?1.f:0.f;
   k.lo[0]+=cx*size;k.lo[1]+=cy*size;k.dir[0]=1-2*cx;k.dir[1]=1-2*cy;k.fold=size*.3f;
   k.n=std::max(2u,UINT(std::lround(2*k.fold*aw)));
  }else k.n=std::max(1u,UINT(std::lround(size*aw)));
  run(Load,16,16);                                                        // B0 = B1 = L
  fft(0,false);gauss(0,0,64);fft(0,true);combine(0);                      // D = L / blur64(L)
  if(style.hp>0){combine(1);fft(0,false);gauss(0,0,style.hp);fft(0,true);combine(2);} // D - blur_hp(D) + 1
  if(style.dc>0){reduce(0);combine(3);}                                   // dark-tail clip
  combine(1);fft(0,false);combine(4);                                     // B0 = FFT(D), G = 0
  const float scales[3]{1,4,12},weights[3]{style.w1,style.w4,style.w12};
  for(int s=0;s<3;++s){
   if(weights[s]<=0)continue;
   gauss(0,2,scales[s]);fft(2,true);combine(6);reduce(1);k.weight=weights[s];combine(5);
  }
  fft(1,false);combine(7);fft(2,true);reduce(2);                          // integrate, percentiles
  k.side=slot;k.origin[0]=(c->pos%8u)*slot;k.origin[1]=width-(c->pos/8u+1)*slot;
  run(Write,(slot+15u)/16u,(slot+15u)/16u);
  map.baked.insert(c->key);
 }
 ID3D11UnorderedAccessView* no_uavs[3]{};ID3D11ShaderResourceView* no_source=nullptr;ID3D11Buffer* no_constants=nullptr;ID3D11SamplerState* no_sampler=nullptr;
 context_->CSSetUnorderedAccessViews(0,3,no_uavs,nullptr);context_->CSSetShaderResources(0,1,&no_source);
 context_->CSSetConstantBuffers(0,1,&no_constants);context_->CSSetSamplers(0,1,&no_sampler);context_->CSSetShader(nullptr,nullptr,0);
 context_->GenerateMips(map.height_srv.Get());
 wait("of"); // map load only: the log carries the bake's real time
 const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 const double mb=double(width)*height*contact.slices*4/3/1048576.0;
 char text[224];std::snprintf(text,sizeof(text),"map=%s cells=%zu baked=%zu ms=%.2f array=%ux%ux%u (%.1f MB) pool=%d base=%u",name,pending.size(),map.baked.size(),ms,
  width,height,contact.slices,mb,contact.pool,contact.base);
 bridge_append_probe_event("terrain2_heights",text);
 ++terrain2_cache_revision_; // the material cache bakes again with the new heights
}catch(...){}

#endif // !EID3D11_SHADER_EMBED
void append_snow_surface_program(std::vector<ShaderProgram>& out){
 out.push_back({{kSnowSurfaceShader,sizeof(kSnowSurfaceShader)-1},nullptr,nullptr,"snowSurfaceCS","cs_5_0",0});
}

#if !defined(EID3D11_SHADER_EMBED)
// The active map's snow material cell (style snow) once its height is baked,
// a plain slot before a folded quadrant; key: the map generation and the cell.
const Renderer::Terrain2BakeCell* Renderer::snow_surface_cell(std::uint64_t& key) const noexcept{
 const Terrain2BakeCell* cell=nullptr;
 for(const auto& c:terrain2_map_.bake_cells)
  if(c.style==10u&&terrain2_map_.baked.count(c.key)&&(!cell||(cell->folded&&!c.folded)))cell=&c;
 key=cell?((ambient_map_generation_<<32)^cell->key^(std::uint64_t{1}<<63)):0;
 return cell;
}

// The snow surface on D3D11 (kSnowSurfaceShader): one dispatch from the map's
// contact atlas and baked heights into a 256 px RGBA8 texture, then its
// levels (GenerateMips). Map load only. A failure is logged and leaves no
// texture: the object build-up keeps its drift relief.
void Renderer::update_snow_surface() noexcept try{
 std::uint64_t key=0;const Terrain2BakeCell* cell=snow_surface_cell(key);
 if(key==snow_surface_key_)return;
 snow_surface_key_=key;snow_surface_srv_.Reset();
 if(!cell||!contact_maps_.atlas||!terrain2_map_.height_srv||!terrain2_bake_sampler_)return;
 HRESULT hr=S_OK;
 const auto fail=[&](const char* what){
  snow_surface_srv_.Reset();
  char text[160];std::snprintf(text,sizeof(text),"%s failed (0x%08lx); snowed objects keep the drift relief",what,static_cast<unsigned long>(hr));
  bridge_append_probe_event("snow_surface_omitted",text);
 };
 if(!snow_surface_cs_){
  std::vector<ShaderProgram> program;append_snow_surface_program(program);
  ComPtr<ID3DBlob> code;
  if(SUCCEEDED(hr=compile_shader(program[0],&code)))hr=create_shader(device_.Get(),code->GetBufferPointer(),code->GetBufferSize(),nullptr,&snow_surface_cs_);
  D3D11_BUFFER_DESC constants{};constants.ByteWidth=32;constants.Usage=D3D11_USAGE_DEFAULT;constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  if(SUCCEEDED(hr))hr=device_->CreateBuffer(&constants,nullptr,&snow_surface_constants_);
  if(FAILED(hr)){snow_surface_cs_.Reset();return fail("snow surface shader");}
 }
 D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=kSnowSurfaceSide;d.MipLevels=0;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
 d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_RENDER_TARGET;
 d.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
 ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11UnorderedAccessView> uav;ComPtr<ID3D11ShaderResourceView> srv;
 if(FAILED(hr=device_->CreateTexture2D(&d,nullptr,&texture))||FAILED(hr=device_->CreateUnorderedAccessView(texture.Get(),nullptr,&uav))||
    FAILED(hr=device_->CreateShaderResourceView(texture.Get(),nullptr,&srv)))return fail("snow surface texture");
 // The cell as the height bake reads it (bake_terrain2_heights' lo, size, fold).
 const float margin=tile_blend_margin_*.125f,size=.125f-2*margin;
 struct{float lo[2],size[2],dir[2],fold,slice;}k{};
 k.lo[0]=(cell->pos%8u)*.125f+margin;k.lo[1]=1-(cell->pos/8u+1)*.125f+margin;k.size[0]=k.size[1]=size;k.dir[0]=k.dir[1]=1;k.slice=float(cell->slice);
 if(cell->folded){
  const float cx=(cell->quadrant==1||cell->quadrant==2)?1.f:0.f,cy=cell->quadrant>=2?1.f:0.f;
  k.lo[0]+=cx*size;k.lo[1]+=cy*size;k.dir[0]=1-2*cx;k.dir[1]=1-2*cy;k.fold=size*.3f;
 }
 context_->UpdateSubresource(snow_surface_constants_.Get(),0,nullptr,&k,0,0);
 ID3D11ShaderResourceView* sources[2]{contact_maps_.atlas.Get(),terrain2_map_.height_srv.Get()};
 ID3D11UnorderedAccessView* target=uav.Get();ID3D11Buffer* constants=snow_surface_constants_.Get();ID3D11SamplerState* sampler=terrain2_bake_sampler_.Get();
 context_->CSSetShaderResources(0,2,sources);context_->CSSetUnorderedAccessViews(0,1,&target,nullptr);
 context_->CSSetConstantBuffers(0,1,&constants);context_->CSSetSamplers(0,1,&sampler);
 context_->CSSetShader(snow_surface_cs_.Get(),nullptr,0);context_->Dispatch(kSnowSurfaceSide/16u,kSnowSurfaceSide/16u,1);
 ID3D11ShaderResourceView* no_sources[2]{};ID3D11UnorderedAccessView* no_target=nullptr;ID3D11Buffer* no_constants=nullptr;ID3D11SamplerState* no_sampler=nullptr;
 context_->CSSetShaderResources(0,2,no_sources);context_->CSSetUnorderedAccessViews(0,1,&no_target,nullptr);
 context_->CSSetConstantBuffers(0,1,&no_constants);context_->CSSetSamplers(0,1,&no_sampler);context_->CSSetShader(nullptr,nullptr,0);
 context_->GenerateMips(srv.Get());
 snow_surface_srv_=srv;
 char text[96];std::snprintf(text,sizeof(text),"cell=%u slice=%u folded=%d side=%u",cell->key,cell->slice,cell->folded?1:0,kSnowSurfaceSide);
 bridge_append_probe_event("snow_surface",text);
}catch(...){}


// The terrain 2.0 map table (terrain2.hlsl terrain2Record): entry 0 the
// active map, then every neighbour with a contact grid and atlas (in
// gc_neighbours_ order; at most kTerrain2TableMaps entries, the rest are
// named once and keep the old terrain), each with its styles in the shared
// buffer (t121, uploaded when an entry's styles change) and four records:
// the world rectangle, the world origin and 1 once its rows and styles are
// in, the layout origin and the copied atlas slices, and where its grid,
// atlas and heights are (record 3, terrain2Index). Off: no entries.
// D3D11: the entries' grids stacked in one array (t85, made again when an
// entry or its grid changes), the atlas and height pools at t86-t89 and
// t90-t93 (terrain2_pool_range); record 3 x first slice | pool << 16, y grid
// width | height << 16, z first grid row, w the atlas width. A map whose
// atlas found no pool shows the marker colour (record 1 w 0).
// D3D12: the entries' D3D12 grid, atlas and heights (terrain2_table12_);
// record 3 holds their bindless indices (terrain2_records_d3d12, at upload);
// no styles buffer (no program reads it).
void Renderer::update_terrain2_table(bool on,float* records) noexcept{
 terrain2_table_count_=0;terrain2_table12_={};
 std::array<const ContactBlendMaps*,kTerrain2TableMaps> contacts{};
 std::array<const Terrain2Map*,kTerrain2TableMaps> maps{};
 const auto add=[&](UINT contact_map,const ContactBlendMaps& contact,const Terrain2Map& t,const std::array<float,3>& origin,
                    const std::array<float,2>& layout,UINT tiles_x,UINT tiles_y,bool rows,const char* name){
  if(d3d12_?!contact.tiles12||!contact.atlas12:!contact.tiles||(!contact.atlas&&!contact.pool_refused))return;
  if(terrain2_table_count_>=kTerrain2TableMaps){
   if(!terrain2_table_full_logged_){
    terrain2_table_full_logged_=true;
    char text[192];std::snprintf(text,sizeof(text),"map=%s more maps than the terrain 2.0 table holds (%u); its ground keeps the current terrain",name,kTerrain2TableMaps);
    bridge_neighbour_diagnostic(text);
   }
   return;
  }
  const unsigned k=terrain2_table_count_++;
  terrain2_table_[k]=contact_map;maps[k]=&t;contacts[k]=&contact;
  if(d3d12_){terrain2_table12_[k]=contact.tiles12;terrain2_table12_[kTerrain2TableMaps+k]=contact.atlas12;terrain2_table12_[2*kTerrain2TableMaps+k]=t.height12;}
  float* r=records+4*kTerrain2Records*k;
  r[0]=origin[0];r[1]=origin[1];r[2]=origin[0]+float(tiles_x)*2;r[3]=origin[1]+float(tiles_y)*2;
  r[4]=origin[0];r[5]=origin[1];r[6]=origin[2];r[7]=rows&&t.style_serial&&!contact.pool_refused?1.f:0.f;
  const std::uint32_t ready[2]{static_cast<std::uint32_t>(contact.ready),static_cast<std::uint32_t>(contact.ready>>32)};
  r[8]=layout[0];r[9]=layout[1];std::memcpy(r+10,ready,sizeof(ready));
  const float width=float(contact.width);std::memcpy(r+15,&width,sizeof(width));
 };
 if(on){
  if(contact_map_generation_==ambient_map_generation_&&contact_constants_generation_==contact_map_generation_)
   add(0,contact_maps_,terrain2_map_,{0,0,0},terrain_layout_origin_,contact_tiles_x_,contact_tiles_y_,tile_blend_rows_,"active");
  if(configured_boolean(debug_overlay_.config(),"terrain_blend_neighbours",true))
   for(const auto& n:gc_neighbours_)
    add(n.contact_map,n.contact,n.terrain2,n.origin,n.layout_origin,n.tiles_x,n.tiles_y,n.blend_rows,n.name.c_str());
 }
 if(d3d12_)return;
 // The stacked grid: made again when an entry or its grid changes.
 std::array<std::uint64_t,kTerrain2TableMaps> key{};
 std::array<D3D11_TEXTURE2D_DESC,kTerrain2TableMaps> grid{};
 std::array<ComPtr<ID3D11Texture2D>,kTerrain2TableMaps> grid_texture{};
 UINT stack_width=0,stack_height=0;
 for(unsigned k=0;k<terrain2_table_count_;++k){
  key[k]=contacts[k]->tiles_serial;
  ComPtr<ID3D11Resource> resource;contacts[k]->tiles->GetResource(&resource);
  if(SUCCEEDED(resource.As(&grid_texture[k])))grid_texture[k]->GetDesc(&grid[k]);
  float* r=records+4*kTerrain2Records*k;
  const std::uint32_t index[3]{contacts[k]->pool_refused?0u:(contacts[k]->base|(std::uint32_t(contacts[k]->pool)<<16)),
                               grid[k].Width|(grid[k].Height<<16),stack_height};
  std::memcpy(r+12,index,sizeof(index));
  stack_width=std::max(stack_width,grid[k].Width);stack_height+=grid[k].Height;
 }
 if(key!=terrain2_grids_key_){
  terrain2_grids_.Reset();terrain2_grids_srv_.Reset();terrain2_grids_key_=key;
  if(stack_width&&stack_height){
   D3D11_TEXTURE2D_DESC d{};d.Width=stack_width;d.Height=stack_height;d.MipLevels=d.ArraySize=1;
   d.Format=DXGI_FORMAT_R32G32B32A32_UINT;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
   if(FAILED(device_->CreateTexture2D(&d,nullptr,&terrain2_grids_))||FAILED(device_->CreateShaderResourceView(terrain2_grids_.Get(),nullptr,&terrain2_grids_srv_))){
    terrain2_grids_.Reset();terrain2_grids_srv_.Reset();
    bridge_append_probe_event("terrain2_no_materials","stacked grid creation failed; terrain 2.0 ground shows the marker colour");
   }else{
    UINT row=0;
    for(unsigned k=0;k<terrain2_table_count_;++k){
     if(grid_texture[k])context_->CopySubresourceRegion(terrain2_grids_.Get(),0,0,row,0,grid_texture[k].Get(),0,nullptr);
     row+=grid[k].Height;
    }
   }
  }
 }
 // The styles: one 4096-entry R16 range per entry, uploaded when it changed.
 if(terrain2_table_count_&&!terrain2_style_buffer_){
  D3D11_BUFFER_DESC d{};d.ByteWidth=UINT(sizeof(std::uint16_t)*4096*kTerrain2TableMaps);d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R16_UINT;view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=4096*kTerrain2TableMaps;
  if(FAILED(device_->CreateBuffer(&d,nullptr,&terrain2_style_buffer_))||FAILED(device_->CreateShaderResourceView(terrain2_style_buffer_.Get(),&view,&terrain2_style_srv_))){
   terrain2_style_buffer_.Reset();terrain2_style_srv_.Reset();
   bridge_append_probe_event("terrain2_no_materials","cell style buffer creation failed; terrain 2.0 ground shows the marker colour");
  }
  terrain2_style_uploaded_.fill(0);
 }
 for(unsigned k=0;k<terrain2_table_count_;++k){
  if(!terrain2_style_buffer_||!terrain2_grids_srv_){records[4*kTerrain2Records*k+7]=0;continue;}
  if(terrain2_style_uploaded_[k]==maps[k]->style_serial)continue;
  const D3D11_BOX box{UINT(k*4096*sizeof(std::uint16_t)),0,0,UINT((k+1)*4096*sizeof(std::uint16_t)),1,1};
  context_->UpdateSubresource(terrain2_style_buffer_.Get(),0,&box,maps[k]->styles.data(),0,0);
  terrain2_style_uploaded_[k]=maps[k]->style_serial;
 }
 ID3D11ShaderResourceView* views[1+2*kTerrain2Pools]{terrain2_grids_srv_.Get()};
 for(unsigned p=0;p<kTerrain2Pools;++p){views[1+p]=terrain2_pools_[p].atlas_srv.Get();views[1+kTerrain2Pools+p]=terrain2_pools_[p].heights_srv.Get();}
 context_->PSSetShaderResources(85,UINT(std::size(views)),views);
}

// D3D12: record 3 of every table entry (records: the frame's table), the
// bindless indices of its grid, atlas and heights; heights 0 (the flat
// height) until their levels are made (they are shader-readable only then:
// bindless reads have no per-draw transition). True when a value changed.
bool Renderer::terrain2_records_d3d12(float* records) noexcept{
 bool changed=false;
 for(unsigned k=0;k<terrain2_table_count_;++k){
  const auto& grid=terrain2_table12_[k];const auto& atlas=terrain2_table12_[kTerrain2TableMaps+k];const auto& heights=terrain2_table12_[2*kTerrain2TableMaps+k];
  const Terrain2Map* map=&terrain2_map_;
  if(terrain2_table_[k])for(const auto& n:gc_neighbours_)if(n.contact_map==terrain2_table_[k]){map=&n.terrain2;break;}
  std::uint32_t index[3]{grid?d3d12_->bindless_index(*grid):0u,atlas?d3d12_->bindless_index(*atlas):0u,
                         heights&&!map->mips12&&!map->baked.empty()?d3d12_->bindless_index(*heights):0u};
  for(auto& i:index)if(i==UINT32_MAX)i=0; // the region is full this frame: it grows; the marker meanwhile
  float* r=records+4*kTerrain2Records*k+12;
  if(std::memcmp(r,index,sizeof(index))){std::memcpy(r,index,sizeof(index));changed=true;}
 }
 return changed;
}

// The tile masks, once a session (camp_art.rc EID3D11_T2M_S<style>_L<layer>):
// decoded, cut to kTerrain2MaskSide and pre-blurred per style
// (terrain2_mask_sigma, clamped at the tile's border), uploaded once into one
// R8 array with mips. No CPU copy is kept. A style missing a variant reuses
// its other variant.
void Renderer::load_terrain2_masks() noexcept try{
 terrain2_masks_tried_=true;
 HRESULT hr=S_OK;
 const auto fail=[&](const char* what){
  char text[200];std::snprintf(text,sizeof(text),"%s (0x%08lx); terrain 2.0 tiles use bilinear shapes and height only",what,static_cast<unsigned long>(hr));
  bridge_append_probe_event("terrain2_masks_omitted",text);
 };
 HMODULE module=nullptr;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
     reinterpret_cast<LPCWSTR>(&append_terrain2_shader_programs),&module))return hr=E_FAIL,fail("module lookup");
 const HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 if(FAILED(apartment)&&apartment!=RPC_E_CHANGED_MODE)return hr=apartment,fail("COM");
 struct Apartment{bool release;~Apartment(){if(release)CoUninitialize();}}scope{SUCCEEDED(apartment)};
 ComPtr<IWICImagingFactory> factory;
 if(FAILED(hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return fail("WIC");
 // One decoded 256 px mask as floats (0..1), or empty.
 const auto decode=[&](std::uint32_t style,std::uint32_t layer,std::vector<float>& out){
  out.clear();
  wchar_t name[40];swprintf_s(name,L"EID3D11_T2M_S%u_L%u",style,layer);
  const HRSRC resource=FindResourceW(module,name,MAKEINTRESOURCEW(10));
  const HGLOBAL loaded=resource?LoadResource(module,resource):nullptr;
  auto* bytes=loaded?static_cast<BYTE*>(LockResource(loaded)):nullptr;
  const DWORD size=resource?SizeofResource(module,resource):0;
  if(!bytes||!size)return;
  ComPtr<IWICStream> stream;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
  UINT w=0,h=0;
  if(FAILED(factory->CreateStream(&stream))||FAILED(stream->InitializeFromMemory(bytes,size))||
     FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder))||
     FAILED(decoder->GetFrame(0,&frame))||FAILED(frame->GetSize(&w,&h))||w!=256||h!=256||
     FAILED(factory->CreateFormatConverter(&converter))||
     FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat8bppGray,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return;
  std::vector<BYTE> pixels(256*256);
  if(FAILED(converter->CopyPixels(nullptr,256,UINT(pixels.size()),pixels.data())))return;
  out.resize(pixels.size());for(std::size_t i=0;i<pixels.size();++i)out[i]=pixels[i]/255.f;
 };
 // Bilinear resample of the source rectangle [lo, lo+span) to the output side.
 const auto resample=[](const std::vector<float>& src,float lo,float span,UINT side){
  std::vector<float> out(std::size_t(side)*side);
  const auto at=[&](int x,int y){return src[std::size_t(std::clamp(y,0,255))*256+std::size_t(std::clamp(x,0,255))];};
  for(UINT y=0;y<side;++y)for(UINT x=0;x<side;++x){
   const float sx=lo+(x+.5f)*span/side-.5f,sy=lo+(y+.5f)*span/side-.5f;
   const int x0=int(std::floor(sx)),y0=int(std::floor(sy));const float fx=sx-x0,fy=sy-y0;
   out[std::size_t(y)*side+x]=(at(x0,y0)*(1-fx)+at(x0+1,y0)*fx)*(1-fy)+(at(x0,y0+1)*(1-fx)+at(x0+1,y0+1)*fx)*fy;
  }
  return out;
 };
 // Separable Gaussian, clamped.
 const auto blur=[](std::vector<float>& m,UINT side,float sigma){
  if(sigma<=0)return;
  const int r=int(std::ceil(3*sigma));std::vector<float> k(2*r+1);float total=0;
  for(int i=-r;i<=r;++i)total+=k[i+r]=std::exp(-.5f*i*i/(sigma*sigma));
  for(float& v:k)v/=total;
  std::vector<float> t(m.size());const int n=int(side);
  for(int y=0;y<n;++y)for(int x=0;x<n;++x){float s=0;
   for(int i=-r;i<=r;++i)s+=k[i+r]*m[std::size_t(y)*n+std::size_t(std::clamp(x+i,0,n-1))];
   t[std::size_t(y)*n+x]=s;}
  for(int y=0;y<n;++y)for(int x=0;x<n;++x){float s=0;
   for(int i=-r;i<=r;++i)s+=k[i+r]*t[std::size_t(std::clamp(y+i,0,n-1))*n+x];
   m[std::size_t(y)*n+x]=s;}
 };
 constexpr UINT side=kTerrain2MaskSide;
 std::vector<std::uint8_t> layers;std::uint32_t bits=0;std::vector<float> a;
 for(std::uint32_t style=0;style<kTerrain2StyleCount;++style){
  std::vector<std::uint8_t> mine;bool complete=true;
  for(std::uint32_t layer=0;layer<kTerrain2MaskLayers&&complete;++layer){
   decode(style,layer,a);
   if(a.empty())decode(style,layer^1u,a); // the other variant of the topology
   if(a.empty()){complete=false;break;}
   auto m=resample(a,0.f,256.f,side);
   blur(m,side,terrain2_mask_sigma(style)*side/256.f);
   for(float v:m)mine.push_back(std::uint8_t(std::lround(std::clamp(v,0.f,1.f)*255.f)));
  }
  if(!complete)continue; // no masks: this style's tiles take bilinear shapes
  layers.insert(layers.end(),mine.begin(),mine.end());bits|=1u<<style;
 }
 const UINT count=UINT(layers.size()/(std::size_t(side)*side));
 if(!count)return hr=E_FAIL,fail("no mask resources");
 UINT mips=0;for(UINT s=side;s;s>>=1)++mips;
 // Mips by 2x2 box per layer, all levels built here and uploaded at once.
 std::vector<std::vector<std::uint8_t>> levels(mips);levels[0]=std::move(layers);
 for(UINT level=1;level<mips;++level){
  const UINT ps=side>>(level-1),s=side>>level;levels[level].resize(std::size_t(s)*s*count);
  for(UINT l=0;l<count;++l)for(UINT y=0;y<s;++y)for(UINT x=0;x<s;++x){
   const auto* p=levels[level-1].data()+std::size_t(l)*ps*ps;
   const unsigned sum=p[(2*y)*ps+2*x]+p[(2*y)*ps+2*x+1]+p[(2*y+1)*ps+2*x]+p[(2*y+1)*ps+2*x+1];
   levels[level][std::size_t(l)*s*s+std::size_t(y)*s+x]=std::uint8_t((sum+2)/4);
  }
 }
 std::vector<D3D11_SUBRESOURCE_DATA> initial(std::size_t(count)*mips);
 for(UINT l=0;l<count;++l)for(UINT level=0;level<mips;++level){
  const UINT s=side>>level;
  initial[std::size_t(l)*mips+level]={levels[level].data()+std::size_t(l)*s*s,s,0};
 }
 D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=side;d.MipLevels=mips;d.ArraySize=count;d.Format=DXGI_FORMAT_R8_UNORM;
 d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 ComPtr<ID3D11Texture2D> texture;
 if(FAILED(hr=device_->CreateTexture2D(&d,initial.data(),&texture))||
    FAILED(hr=device_->CreateShaderResourceView(texture.Get(),nullptr,&terrain2_masks_srv_)))return terrain2_masks_srv_.Reset(),fail("mask array");
 D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
 if(FAILED(hr=device_->CreateSamplerState(&sampler,&terrain2_mask_sampler_)))return terrain2_masks_srv_.Reset(),fail("mask sampler");
 terrain2_mask_bits_=bits;
 char text[96];std::snprintf(text,sizeof(text),"styles=0x%03x layers=%u side=%u",bits,count,side);
 bridge_append_probe_event("terrain2_masks",text);
}catch(...){}

// A map's cell styles (terrain2CellStyle, its table entry's 4096): cell & 4095 -> the
// Mapper style of the family that cell draws, from this map's own names (a
// plain slot: the family of its signature, so every placed variant has one),
// and the material cells its rows draw, to bake (a folded cell with a donor bakes the
// donor's plain slot, which it reads).
void Renderer::update_terrain2_styles(Terrain2Map& map,const TerrainTileFamilies& families,const std::map<std::uint32_t,std::uint32_t>& cells,
 const std::vector<std::array<std::uint32_t,4>>& rows,const std::vector<std::uint32_t>& fold_table,
 const std::vector<std::array<std::uint32_t,4>>& art) noexcept try{
 std::array<std::uint8_t,4096> styles;styles.fill(std::uint8_t(kTerrain2NoStyle));
 for(const auto& [family,cell]:cells){
  const auto name=families.names.find(family);
  if(name!=families.names.end())styles[cell&4095u]=std::uint8_t(terrain2_style(name->second));
 }
 std::set<std::uint32_t> drawn;
 for(const auto& row:rows)for(const std::uint32_t lane:{row[0],row[1]})for(const std::uint32_t c:{lane&65535u,lane>>16})
  if(c!=terrain_tile_blend_none)drawn.insert(c&4095u);
 // Protected homogeneous tiles sample their own placed slot, although the
 // family field uses a canonical cell. Bake that slot's matching height too,
 // from the same affine art rows the pixel shader reads (no donor height).
 for(std::size_t t=0;t<rows.size();++t){
  const auto& tile=art[t];
  if((tile[3]&terrain_tile_authored_plain)==0)continue;
  const std::uint32_t page=tile[3]&255u;
  if(page>=terrain_tile_fold_page)continue;
  const float u=(float(tile[1]&65535u)+float(tile[2]&65535u))*.5f/65535.f;
  const float v=(float(tile[1]>>16)+float(tile[2]>>16))*.5f/65535.f;
  const auto x=std::min(7u,std::uint32_t(u*8)),y=std::min(7u,std::uint32_t(v*8));
  drawn.insert(page*64u+(7u-y)*8u+x);
 }
 map.bake_cells.clear();
 for(const std::uint32_t c:drawn){
  std::uint32_t page=c>>6,pos=c&63u,quadrant=0;bool folded=false;
  if(page==terrain_tile_fold_page){
   if(pos>=fold_table.size())continue;
   const std::uint32_t e=fold_table[pos];
   if(e==0xffffffffu)continue; // protected family with no own plain source: marker, no bake
   if((e>>26)&1u){page=((e>>14)&4095u)>>6;pos=(e>>14)&63u;}
   else{page=(e>>6)&63u;pos=e&63u;quadrant=(e>>12)&3u;folded=true;}
  }else if(c<families.signatures.size()&&styles[c]==kTerrain2NoStyle){
   const auto name=families.names.find(families.signatures[c][0]);
   if(name!=families.names.end())styles[c]=std::uint8_t(terrain2_style(name->second));
  }
  const std::uint32_t key=((page*64u+pos)<<3)|(folded?4u+quadrant:0u);
  bool known=false;for(const auto& b:map.bake_cells)known=known||b.key==key;
  if(!known)map.bake_cells.push_back({key,page,pos,quadrant,styles[c],folded});
 }
 map.dirty=true;
 for(std::size_t i=0;i<map.styles.size();++i)map.styles[i]=styles[i];
 map.style_serial=++terrain2_style_serials_;
}catch(...){map.style_serial=0;}
namespace {


struct TileFamilyReaper{std::mutex lock;std::vector<std::future<TerrainTileFamilies>> pending;};
// Never destroyed: at process exit a pending future would wait in the static
// destructors for a job thread that was already killed.
TileFamilyReaper& tile_family_reaper(){static auto* reaper=new TileFamilyReaper;return *reaper;}
}  // namespace

TileFamiliesJob load_terrain_tile_families_async(const TerrainCpuAsset& terrain){
 ResourceStoreConfig mounts;
 if(!owned_resource_runtime_mounts(mounts))return {};
 auto cancel=std::make_shared<std::atomic<bool>>(false);
 return {std::async(std::launch::async,[mounts=std::move(mounts),input=terrain_tile_map_input(terrain),cancel]() mutable{
  const BackgroundWorkerPriority priority;
  return load_terrain_tile_families(std::move(mounts),std::move(input),cancel.get());
 }),cancel};
}

void TileFamiliesJob::retire() noexcept{
 if(!result_.valid())return;
 if(cancel_)cancel_->store(true,std::memory_order_relaxed);
 cancel_.reset();
 if(result_.wait_for(std::chrono::seconds(0))==std::future_status::ready){result_={};return;}
 auto& reaper=tile_family_reaper();
 try{std::lock_guard<std::mutex> guard(reaper.lock);reaper.pending.push_back(std::move(result_));}
 catch(...){result_.wait();result_={};} // no room in the list: the cancelled job ends at its next check
}

void reap_tile_family_jobs() noexcept{
 auto& reaper=tile_family_reaper();
 std::lock_guard<std::mutex> guard(reaper.lock);
 auto& pending=reaper.pending;
 pending.erase(std::remove_if(pending.begin(),pending.end(),[](const std::future<TerrainTileFamilies>& job){
  return job.wait_for(std::chrono::seconds(0))==std::future_status::ready;}),pending.end());
}

void join_tile_family_jobs() noexcept{
 std::vector<std::future<TerrainTileFamilies>> pending;
 {auto& reaper=tile_family_reaper();std::lock_guard<std::mutex> guard(reaper.lock);pending.swap(reaper.pending);}
 for(auto& job:pending)job.wait(); // cancelled: each ends at its next check
}

bool Renderer::poll_terrain_tile_families() noexcept try{
 if(tile_families_generation_==ambient_map_generation_)return true;
 if(tile_families_job_.valid()){
  if(!tile_families_job_.ready())return false;
  auto families=tile_families_job_.get();
  if(tile_families_job_generation_==ambient_map_generation_){
   tile_families_=std::move(families);tile_families_generation_=ambient_map_generation_;return true;
  }
 }
 // The job loads the terrain the shared grids were built from (a previous map's result is dropped).
 AmbientWorldSource inputs;
 if(!bridge_ambient_world_source(inputs)||!inputs.terrain||ambient_terrain_.owner_before(inputs.terrain)||
    inputs.terrain.owner_before(ambient_terrain_))return false;
 tile_families_job_generation_=ambient_map_generation_;
 if(tile_families_prestart_.valid()&&tile_families_prestart_terrain_==inputs.terrain){
  tile_families_job_=std::move(tile_families_prestart_); // started during the load
  tile_families_prestart_terrain_.reset();
  return poll_terrain_tile_families();
 }
 tile_families_job_=load_terrain_tile_families_async(*inputs.terrain);
 return false;
}catch(...){return false;}

// The tile families depend only on the terrain and the resource mounts. The
// first cover frame started them, so a map with many atlas maps to pool kept
// the loading screen up for the job; it now starts at the present that first
// sees the new published main terrain (during the game's own load).
void Renderer::prestart_terrain_tile_families() noexcept try{
 const auto source=retained_static_terrain_source_snapshot();
 // The map the shared grids were built from is the poll's own business.
 if(!source.terrain||source.terrain==tile_families_prestart_terrain_||source.terrain==ambient_terrain_.lock())return;
 // A previous map's unfinished job is cancelled and reaped, never waited for.
 tile_families_prestart_terrain_=source.terrain;
 tile_families_prestart_=load_terrain_tile_families_async(*source.terrain);
}catch(...){}

// Ground contact blend: per map, the terrain tile grid (atlas corners and
// material of every base tile, the same rule the terrain draws use) and the
// drawn terrain atlases copied, all mips, into one array as each becomes
// resident. Sources are the terrain's own array slices (authored or HD), so
// the band shows exactly the art the adjacent ground shows.
HRESULT Renderer::update_contact_blend_maps() noexcept try{
 HRESULT hr=S_OK;
 if(contact_map_generation_!=ambient_map_generation_){
  // Built whenever the terrain source is readable (terrain 2.0 draws from them); retried otherwise.
  AmbientWorldSource inputs;
  if(!bridge_ambient_world_source(inputs)||!inputs.terrain)return S_OK;
  release_contact_blend_maps();
  contact_map_generation_=ambient_map_generation_;contact_retry_frame_=0;contact_incompatible_logged_=false;
  const auto& terrain=*inputs.terrain;
  const UINT tiles_x=ambient_tiles_x_,tiles_y=ambient_tiles_y_;
  if(!tiles_x||!tiles_y)return S_OK;
  if(FAILED(hr=build_contact_tiles(terrain,tiles_x,tiles_y,contact_maps_,&contact_tile_rows_)))return hr;
  if(!contact_tile_rows_.empty()){contact_tiles_x_=tiles_x;contact_tiles_y_=tiles_y;}
  tile_blend_margin_=terrain.tile_size?8.f/float(terrain.tile_size):0.f;
  if(!contact_sampler_){
   D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
   sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
   if(FAILED(hr=device_->CreateSamplerState(&sampler,&contact_sampler_)))return hr;
  }
 }
 // Terrain material blend: the map's tile families load off the render
 // thread (poll_terrain_tile_families); their rows join the grid once in.
 if(contact_maps_.tiles&&!tile_blend_ready_&&
    contact_map_generation_==ambient_map_generation_&&poll_terrain_tile_families()){
  if(FAILED(hr=apply_terrain_tile_blend(tile_families_)))return hr;
 }
 // Terrain 2.0: each map's heights follow its copied atlas slices (rows keep
 // their own edge families, tile_blend_own_edges_). A map without rows shows
 // the marker colour.
 {
  if(terrain2_map_.dirty||contact_maps_.ready!=terrain2_map_.seen_ready)bake_terrain2_heights(terrain2_map_,contact_maps_,"active");
  for(auto& n:gc_neighbours_)
   if(n.terrain2.dirty||n.contact.ready!=n.terrain2.seen_ready)bake_terrain2_heights(n.terrain2,n.contact,n.name.c_str());
  update_snow_surface();
 }
 if(tile_blend_ready_&&!tile_blend_rows_&&!terrain2_marker_logged_){
  terrain2_marker_logged_=true;
  bridge_append_probe_event("terrain2_no_materials","the map has no material families (atlas profile or preset); terrain 2.0 ground shows the marker colour");
 }
 // Copy every material whose terrain slice is resident; retry the rest now and then.
 if(!contact_maps_.pending||native_frame_index_<contact_retry_frame_)return S_OK;
 contact_retry_frame_=native_frame_index_+(load_reveal_holding_?1:30);
 for(std::uint32_t material=0;material<contact_blend_max_materials;++material){
  const std::uint64_t bit=std::uint64_t{1}<<material;
  if(!(contact_maps_.pending&bit))continue;
  std::shared_ptr<const OwnedVisualSurface> surface;
  if(!bridge_terrain_atlas(material,surface)||!surface)continue;
  ID3D11Texture2D* source=nullptr;UINT source_slice=0;
#ifdef EID3D11_EMBEDDED
  // Terrain 2.0 reads the host's complete atlas, including its authored mips
  // and material pages not drawn by the camera. The native draw cache holds
  // only mip 0 of an immutable surface, not the terrain atlas input.
  ComPtr<ID3D11Texture2D> host_atlas;
  SurfaceImage* image=nullptr;
  hr=surface_texture_resolver_?surface_texture_resolver_(surface_texture_resolver_context_,device_.Get(),
   surface->id,surface->generation,host_atlas,image):E_FAIL;
  if(FAILED(hr)||!host_atlas){
   char text[192];std::snprintf(text,sizeof(text),"material=%u surface=%llu generation=%llu hr=0x%08lx",
    material,static_cast<unsigned long long>(surface->id),static_cast<unsigned long long>(surface->generation),
    static_cast<unsigned long>(FAILED(hr)?hr:E_FAIL));
   embedded_report_render_failure("terrain2_atlas_unavailable",text);
   continue; // Keep this material pending until its exact host surface is published.
  }
  source=host_atlas.Get();
#else
  // Embedded terrain publishes immutable surfaces. Their array slots are
  // keyed by GPU backing, not by surface id, and carry generation 1. Resolve
  // the original uploaded texture by the same key as get_native_owned_texture.
  const std::string surface_key="surface:"+std::to_string(surface->id);
  for(const char* mips:{"|mips=full","|mips=authored"}){
   const auto found=native_owned_textures_.find(surface_key+mips);
   if(found!=native_owned_textures_.end()&&found->second.generation==surface->generation&&found->second.texture){
    source=found->second.texture.Get();break;
   }
  }
  // Retail surfaces use the mutable array cache. Its id namespace must not
  // be confused with immutable GPU backing ids, even when numbers coincide.
  if(!source)for(const auto& entry:native_texture_arrays_){
   if(entry.first.immutable_values)continue;
   for(const auto& page:entry.second){
    const auto found=page.slots.find(surface->id);
    if(found!=page.slots.end()&&found->second.generation==surface->generation){
     source=page.texture.Get();source_slice=found->second.slice;break;
    }
   }
   if(source)break;
  }
#endif
  if(!source)continue; // not drawn yet: the band keeps the painted colour meanwhile
  char detail[160]{};
  hr=copy_contact_atlas(source,source_slice,material,contact_maps_,detail,sizeof(detail));
  if(FAILED(hr))return hr;
  if(hr==S_OK){
   char detail[224];std::snprintf(detail,sizeof(detail),
    "epoch=%llu material=%u surface=%llu generation=%llu ready=%016llx pending=%016llx",
    static_cast<unsigned long long>(bridge_current_world_epoch()),material,
    static_cast<unsigned long long>(surface->id),static_cast<unsigned long long>(surface->generation),
    static_cast<unsigned long long>(contact_maps_.ready),static_cast<unsigned long long>(contact_maps_.pending));
   bridge_report_event("contact_terrain_atlas",detail);
  }
  if(hr==S_FALSE){ // each material is refused once (its pending bit is cleared)
   char text[256];
   std::snprintf(text,sizeof(text),"terrain atlas format or size differs (%s); affected contact art omitted",detail);
#ifdef EID3D11_EMBEDDED
   embedded_report_render_failure("contact_blend_atlas_incompatible",text);
#else
   bridge_append_probe_event("contact_blend_atlas_incompatible",text);
#endif
  }
 }
 return S_OK;
}catch(...){return E_OUTOFMEMORY;}

// Terrain material blend rows (terrain_tile_blend.h) for the current map:
// every base tile's world-corner families from its slot and corner lanes, the
// families' plain slots among those the map draws, and the grid rebuilt with
// the blend rows as its second half. Without families the tiles keep their art.
HRESULT Renderer::apply_terrain_tile_blend(const TerrainTileFamilies& families) noexcept try{
 tile_blend_ready_=true; // one attempt a map; failures keep the authored tiles
 AmbientWorldSource inputs;
 if(!bridge_ambient_world_source(inputs)||!inputs.terrain||!contact_tiles_x_||!contact_tiles_y_)return S_OK;
 const auto& terrain=*inputs.terrain;
 const UINT tiles_x=contact_tiles_x_,tiles_y=contact_tiles_y_;
 const std::uint32_t slots=terrain.texture_count*terrain.tiles_per_texture;
 if(terrain.tiles_per_texture_side!=8||families.signatures.size()!=slots||contact_tile_rows_.size()!=std::size_t(tiles_x)*tiles_y){
  bridge_append_probe_event("terrain_tile_blend",(families.source+"; the transitions keep their tile art").c_str());
  return S_OK;
 }
 // Kept for rebuilding the rows when a neighbour shares an edge
 // (update_terrain_tile_blend_seams builds them).
 auto input=std::make_shared<TerrainTileBlendInput>(owned_terrain_tile_blend_input(terrain,tiles_x,tiles_y));
 tile_blend_vertex_=owned_terrain_tile_vertex_families(*input,families);
 tile_blend_drawable_=owned_terrain_tile_blend_drawable(*input,families);
 tile_blend_input_=std::move(input);tile_blend_edge_.clear();
 update_terrain_tile_blend_seams();
 return S_OK;
}catch(...){return E_OUTOFMEMORY;}

HRESULT Renderer::set_native_terrain_layer_shifts(const std::vector<float>& shifts) noexcept try{SCENE_ENTRY("set_native_terrain_layer_shifts");
 if(!d3d12_&&(!device_||!context_))return E_FAIL;
 if(shifts==native_terrain_layer_shifts_)return S_OK;
 for(float value:shifts)if(!std::isfinite(value))return E_INVALIDARG;
 if(d3d12_){ // D3D12 backend: the values only; flush_native_d3d12 uploads them
  native_terrain_layer_shifts_=shifts;
  if(++native_terrain_layer_shift_revision_==0)++native_terrain_layer_shift_revision_;
  return S_OK;
 }
 if(shifts.size()>native_terrain_layer_shift_capacity_){
  // Sized by the map's terrain object count; null reads (no map) return 0.
  native_terrain_layer_shift_srv_.Reset();native_terrain_layer_shift_buffer_.Reset();native_terrain_layer_shift_capacity_=0;
  D3D11_BUFFER_DESC description{};description.ByteWidth=static_cast<UINT>(shifts.size()*sizeof(float));
  description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;description.StructureByteStride=sizeof(float);
  HRESULT hr=device_->CreateBuffer(&description,nullptr,&native_terrain_layer_shift_buffer_);
  if(FAILED(hr))return hr;
  D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_UNKNOWN;view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;
  view.Buffer.NumElements=static_cast<UINT>(shifts.size());
  if(FAILED(hr=device_->CreateShaderResourceView(native_terrain_layer_shift_buffer_.Get(),&view,&native_terrain_layer_shift_srv_))){
   native_terrain_layer_shift_buffer_.Reset();return hr;
  }
  native_terrain_layer_shift_capacity_=static_cast<UINT>(shifts.size());
 }
 // Unused tail entries (a map with fewer objects) are never indexed.
 if(!shifts.empty()){
  D3D11_BOX box{0,0,0,static_cast<UINT>(shifts.size()*sizeof(float)),1,1};
  context_->UpdateSubresource(native_terrain_layer_shift_buffer_.Get(),0,&box,shifts.data(),0,0);
 }
 native_terrain_layer_shifts_=shifts;
 if(++native_terrain_layer_shift_revision_==0)++native_terrain_layer_shift_revision_;
 return S_OK;
}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}

HRESULT Renderer::set_native_terrain_frame(const NativeTerrainState& game_terrain,bool full_sun,
                                           const std::vector<float>* layer_shifts) noexcept{SCENE_ENTRY("set_native_terrain_frame");
 if(!d3d12_&&(!device_||!context_))return E_FAIL;
 if(layer_shifts){const HRESULT hr=set_native_terrain_layer_shifts(*layer_shifts);if(FAILED(hr))return hr;}
 // The active map's saved-layout origin: its terrain material layouts use
 // the same frame as when it is drawn as a neighbour. The neighbour stream
 // reads it (the layout file is known where the addon is loaded), so a new
 // map starts at 0,0 and takes its frame once the stream has read it; the
 // shared-edge blend rows then follow (update_terrain_tile_blend_seams).
 if(const auto source=retained_static_terrain_source_snapshot();source.map&&
    (source.map->generation!=terrain_layout_origin_generation_||terrain_layout_name_.empty())){
  const bool fresh=source.map->generation!=terrain_layout_origin_generation_;
  terrain_layout_origin_generation_=source.map->generation;
  double x=0,y=0;
  const bool listed=neighbour_layout_origin(source.map->terrain_resource,x,y);
  if(fresh||listed){
   terrain_layout_origin_={neighbour_layout_tile_origin(x),neighbour_layout_tile_origin(y)};
   terrain_layout_name_=listed?neighbour_map_name(source.map->terrain_resource):std::string();
   char text[160];std::snprintf(text,sizeof(text),"%s origin=%.2f,%.2f frame=%.0f,%.0f",listed?"layout":"not_in_layout_yet",x,y,
    terrain_layout_origin_[0],terrain_layout_origin_[1]);
   bridge_append_probe_event("terrain_layout_frame",text);
   if(listed&&tile_blend_input_)update_terrain_tile_blend_seams();
  }
 }
 // The game's border-fog band moves out over the sides that accepted
 // neighbour maps continue, where the neighbour seam curve takes over;
 // uncovered sides keep the band. Everything reading the frame constants,
 // ambient_terrain_state_ or neighbour_fog_ (terrain, liquids, neighbour
 // decorations, ground cover, critters, ambient particles) shares them.
 NativeTerrainState terrain=game_terrain;
 native_terrain_wind_force_=terrain.wind_force;
 const auto border_fog=neighbour_border_fog();
 neighbour_fog_={};
 if(terrain.fog_enabled){
  terrain.fog_min[0]+=float(border_fog.edges.x0-border_fog.active.x0);
  terrain.fog_min[1]+=float(border_fog.edges.y0-border_fog.active.y0);
  terrain.fog_max[0]+=float(border_fog.edges.x1-border_fog.active.x1);
  terrain.fog_max[1]+=float(border_fog.edges.y1-border_fog.active.y1);
  std::array<float,16> placement{};
  if(bridge_neighbour_world_transform(placement)){
   // Every rendered piece, active map first, for the neighbour pieces' bands.
   auto field=neighbour_fog_field(border_fog.active,border_fog.contacts,border_fog.range);
   bool listed=neighbour_fog_add_piece(field,border_fog.active);
   for(const auto& piece:neighbours_.accepted)listed=neighbour_fog_add_piece(field,piece)&&listed;
   if(!listed&&!neighbour_pieces_overflow_logged_){
    neighbour_pieces_overflow_logged_=true;
    bridge_neighbour_diagnostic("more rendered pieces than the fog list holds; the extra pieces' shared edges keep their band");
   }
   neighbour_fog_=neighbour_fog_translated(field,placement[3],placement[7]);
  }
 }
 // Terrain frame values (NativeTerrainFrameValues, t127): 52 floats of terrain
 // registers, the seam runs and fog pieces, then the live Cliff blend options
 // (8 floats). A one-element structured buffer rather than a constant buffer:
 // the driver builds a new specialised variant of the world pixel shader for
 // every new set of rarely changing cbuffer values (each settings change).
 constexpr unsigned kTerrainCliffBlendFirst=64+4*kNeighbourFogPieces,kTerrainSnowTrailFirst=kTerrainCliffBlendFirst+8,
  kTerrainWetFirst=kTerrainSnowTrailFirst+8,kTerrainShelterFirst=kTerrainWetFirst+4,kTerrainSnowFirst=kTerrainShelterFirst+12,
  kTerrain2First=kTerrainSnowFirst+4,kTerrain2MapsFirst=kTerrain2First+4,
  kTerrainMaterialFirst=kTerrain2MapsFirst+4*kTerrain2Records*kTerrain2TableMaps,kTerrainFrameFloats=kTerrainMaterialFirst+20;
 // D3D12 backend: the values are kept (native_terrain_frame_d3d12_) and
 // uploaded by flush_native_d3d12; no D3D11 buffer or binding.
 if(!d3d12_&&!native_terrain_frame_constants_){
  D3D11_BUFFER_DESC description{};description.ByteWidth=sizeof(float)*kTerrainFrameFloats;description.Usage=D3D11_USAGE_DYNAMIC;
  description.BindFlags=D3D11_BIND_SHADER_RESOURCE;description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;description.StructureByteStride=description.ByteWidth;
  HRESULT hr=device_->CreateBuffer(&description,nullptr,&native_terrain_frame_constants_);
  if(SUCCEEDED(hr))hr=device_->CreateShaderResourceView(native_terrain_frame_constants_.Get(),nullptr,&native_terrain_frame_srv_);
  if(FAILED(hr)){native_terrain_frame_constants_.Reset();native_terrain_frame_srv_.Reset();return hr;}
 }
 D3D11_MAPPED_SUBRESOURCE mapped{};
 const HRESULT hr=d3d12_?S_OK:context_->Map(native_terrain_frame_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
 if(FAILED(hr))return hr;
 const auto& config=debug_overlay_.config();
 // Tree sway reads the same wind and world clock; strength 0 disables it.
 static_assert(24*sizeof(float)+sizeof(WeatherWindGpu)==128,"terrain frame layout");
 const float tree_sway=configured_boolean(config,"tree_sway",true)?
  float(configured_integer(config,"tree_sway_strength",100))/100.f:0.f;
 // One renderer wind per world frame, from the same game wind and clock.
 if(weather_wind_frame_!=native_frame_index_){
  // Tree sway clocks for the upscaler's motion: last world frame's and this one's.
  sway_clock_[0]=weather_wind_frame_+1u==native_frame_index_?sway_clock_[1]:terrain.precise_time;sway_clock_[1]=terrain.precise_time;
  weather_wind_frame_=native_frame_index_;
  update_weather_wind(weather_wind_,terrain.wind_direction,terrain.wind_force_normalized,
   bridge_world_fog_snapshot().environment,configured_boolean(config,"weather_wind_variation",true),
   configured_floating_point(config,"wind_floor",kDefaultWindFloor));
 }
 terrain_full_sun_=full_sun||!configured_boolean(config,"terrain_normal_sunlight",true);
 const auto material=configured_terrain_material(config);
 float values[kTerrainFrameFloats]={terrain.wind_direction[0],terrain.wind_direction[1],terrain.wind_force,terrain.wind_force_normalized,
  terrain.wind_phase,terrain.precise_time,
  (full_sun||!configured_boolean(config,"terrain_normal_sunlight",true))?1.f:0.f,1.f,
  terrain.fog_min[0],terrain.fog_min[1],terrain.fog_max[0],terrain.fog_max[1],
  terrain.fog_distance,terrain.fog_enabled?1.f:0.f,tree_sway,terrain.waves_enabled?1.f:0.f,
  configured_integer(config,"terrain_quality",2)<=1?float(0x100000):0.f,material.antitile?1.f:0.f, // x: bit 20 T2_LEAN (terrain2.hlsl: Medium and Low)
  material.height_blend?1.f:0.f,0.f}; // w: set below
 values[19]=configured_integer(config,"terrain_quality",2)<=0?1.f:0.f; // switches w: Terrain 2.0's Low path (T2_LOW)
 std::memcpy(values+20,&weather_wind_.gpu,sizeof(WeatherWindGpu));
 const auto map_source=retained_static_terrain_source_snapshot();
 if(map_source.terrain){
  std::array<float,16> placement{};bridge_neighbour_world_transform(placement);
  values[28]=placement[3];values[29]=placement[7];
  values[30]=values[28]+map_source.terrain->sector_count_x*32.f;
  values[31]=values[29]+map_source.terrain->sector_count_y*32.f;
 }
 // Ground contact blend (nativeContactBlend): strength 0 disables it.
 values[32]=std::clamp(configured_floating_point(config,"contact_blend",.7f),0.f,1.f);
 values[33]=std::clamp(configured_floating_point(config,"contact_blend_height",.4f),.05f,2.f);
 // The contact batch test's reach (contactBlendWeight's top: the band is at
 // most this height), plus a metre for swaying leaves.
 if(const float reach=std::max(values[33]*contact_blend_top_bands,.15f)+1.f;reach!=native_contact_reach_){
  native_contact_reach_=reach;++native_contact_generation_;
 }
 // Contact atlas slices copied so far (nativeContactMaterials); flush_native
 // binds the atlas only while these bits belong to its current map.
 const std::uint32_t copied[2]{static_cast<std::uint32_t>(contact_maps_.ready),
  static_cast<std::uint32_t>(contact_maps_.ready>>32)};
 std::memcpy(values+34,copied,sizeof(copied));
 contact_constants_generation_=contact_map_generation_;
 neighbour_fog_constants(neighbour_fog_,values+36,values+40,values+52,values+56);
 neighbour_fog_piece_constants(neighbour_fog_,values+60,reinterpret_cast<float(*)[4]>(values+64));
 // Terrain cliffs (nativeTerrainCliff, nativeTerrainTess): side projection
 // on, and the tessellation's largest factor and offset once its stages exist.
 if(configured_terrain_cliffs(config)&&map_source.terrain)values[44]=1.f;
 // Terrain material blend (nativeTerrainCliff.zw): its rows are in the grid,
 // and a slot's atlas padding as a fraction of its side. Rows may stay
 // resident while blending is off; their existence is not activation.
 const bool terrain_blend=configured_terrain_blend(config);
 if(tile_blend_rows_&&(contact_maps_.tiles||contact_maps_.tiles12)&&map_source.terrain){values[46]=terrain_blend?1.f:0.f;values[47]=tile_blend_margin_;}
 // The switched-off terrain features as uploaded (native specialisation key
 // bits 0-4) and the Low terrain path (bit 14, NATIVE_TERRAIN_LOW).
 native_spec_terrain_bits_=(values[17]==0?1u:0u)|(values[18]==0?2u:0u)|(values[32]==0?4u:0u)|(values[44]==0?8u:0u)|(values[46]==0?16u:0u)|
  (values[19]!=0?16384u:0u);
 if(const float factor=terrain_tessellation_max_factor(configured_integer(config,"terrain_tessellation",1));
    factor>0&&map_source.terrain&&(d3d12_?native12_.tessellation.size()>=5:SUCCEEDED(ensure_terrain_tessellation()))){ // D3D12: the executor's hull/domain programs
  values[45]=factor;
  values[48]=terrain_tessellation_height;
 }
 // Terrain 2.0 (nativeTerrain2, terrain2.hlsl) and its resources: the edge
 // masks (shared by every map) and the map table.
 {
  // D3D12 loads no masks: no terrain 2.0 program reads them (nor the styles
  // buffer), so its mask bits stay 0 (read only by the cache's input hash).
  if(!terrain2_masks_tried_&&!d3d12_)load_terrain2_masks();
  const bool on=terrain_blend&&map_source.terrain;
  values[kTerrain2First]=on?1.f:0.f;
  values[kTerrain2First+2]=float(terrain2_mask_bits_);
  update_terrain2_table(on,values+kTerrain2MapsFirst);terrain2_records_first_=kTerrain2MapsFirst;
  if(!d3d12_){ // D3D12: bind_terrain2_d3d12 in the flush
  ID3D11ShaderResourceView* masks=on?terrain2_masks_srv_.Get():nullptr,*styles=on?terrain2_style_srv_.Get():nullptr;
  context_->PSSetShaderResources(119,1,&masks);context_->PSSetShaderResources(121,1,&styles); // t120: per batch (renderer_flush.cpp)
  ID3D11SamplerState* samplers[1]{terrain2_mask_sampler_.Get()};context_->PSSetSamplers(14,1,samplers);
  ID3D11SamplerState* contact=on?contact_sampler_.Get():nullptr;context_->PSSetSamplers(9,1,&contact);
  }
  values[kTerrain2First+3]=float(terrain2_table_count_);
 }
 // Live Cliff blend options (terrain_tile_blend.h).
 terrain_cliff_blend_values([&](const TerrainCliffBlendOption& option){return configured_floating_point(config,option.name,option.fallback);},
  values[50],values+kTerrainCliffBlendFirst);
 // What terrain2Color reads from these values (material switches, cliff and
 // Cliff blend values, terrain 2.0 and its map table), for the material
 // cache's input hash (renderer_terrain2_cache.cpp).
 {
  float* cache=terrain2_cache_frame_values_.data();
  std::memcpy(cache,values+16,4*sizeof(float));std::memcpy(cache+4,values+44,8*sizeof(float));
  std::memcpy(cache+12,values+kTerrainCliffBlendFirst,8*sizeof(float));
  std::memcpy(cache+20,values+kTerrain2First,(4+4*kTerrain2Records*kTerrain2TableMaps)*sizeof(float));
  static_assert(4+8+8+4+4*kTerrain2Records*kTerrain2TableMaps==std::tuple_size_v<decltype(terrain2_cache_frame_values_)>,"cache input copy");
 }
 terrain_tessellation_frame_=values[45]>0?native_frame_index_:UINT64_MAX;
 // Snow trails: the field state current now. The cover pass writes only the
 // other ping-pong texture, so this texture and window stay paired until the
 // next terrain frame.
 snow_terrain_srv_=gc_snow_valid_&&gc_snow_map_generation_==ambient_map_generation_?gc_snow_srv_[gc_snow_index_]:nullptr;
 // D3D12: the cover's own field (gc12_.snow), under the same rule.
 snow_terrain12_=d3d12_&&gc_snow_valid_&&gc_snow_map_generation_==ambient_map_generation_?gc12_.snow[gc_snow_index_]:nullptr;
 snow_terrain_window_[0]=float(gc_snow_origin_[0])*snow_trails::kTexel;snow_terrain_window_[1]=float(gc_snow_origin_[1])*snow_trails::kTexel;
 snow_terrain_window_[2]=1.f/snow_trails::kTexel;snow_terrain_window_[3]=snow_terrain_srv_||snow_terrain12_?float(snow_trails::kSide):0.f;
 std::memcpy(snow_terrain_tiles_,gc_snow_active_.data(),sizeof(snow_terrain_tiles_));
 std::memcpy(values+kTerrainSnowTrailFirst,snow_terrain_window_,sizeof(snow_terrain_window_));
 std::memcpy(values+kTerrainSnowTrailFirst+4,snow_terrain_tiles_,sizeof(snow_terrain_tiles_));
 // The trail window's smooth centre (snowTrailRaw fades by distance from it).
 values[kTerrainSnowFirst+1]=gc_snow_centre_[0];values[kTerrainSnowFirst+2]=gc_snow_centre_[1];
 // Material classes (nativeMaterialValue): level, metal diffuse retention
 // (classes.json), debug view, reflection scale. Level 0 until the class
 // library loaded: its absence draws no material response anywhere.
 values[kTerrainMaterialFirst]=material_library_?float(material_level()):0.f;
 values[kTerrainMaterialFirst+1]=material_library_?material_library_->metal_diffuse_keep():.35f;
 values[kTerrainMaterialFirst+2]=float(std::clamp(configured_integer(config,"material_view",0),0,3));
 values[kTerrainMaterialFirst+3]=configured_floating_point(config,"material_reflections",1.f);
 // World objects (S2, nativeMaterialWorldValue): derived from each object's
 // own texture, so no class library is needed. Level, relief, roughness offset.
 values[kTerrainMaterialFirst+4]=configured_boolean(config,"material_world",true)?float(material_level()):0.f;
 values[kTerrainMaterialFirst+5]=configured_floating_point(config,"material_relief",1.f);
 values[kTerrainMaterialFirst+6]=configured_floating_point(config,"material_world_roughness",0.f);
 // Metal masks (characters): chain-mail gaps lose metal relative to their surroundings.
 values[kTerrainMaterialFirst+7]=configured_boolean(config,"material_metal_mask",true)?1.f:0.f;
 // Terrain (S3, nativeMaterialTerrainValue): terrain 2.0 ground; no class
 // library needed (the roughness table is classes.json "terrain", with
 // defaults). Level, relief (metres per unit of baked height), crevice
 // strength; then the cell classes' roughness. The material cache bakes
 // the roughness and crevices (terrain_surface_bake_ joins its input hash).
 {
  if(!terrain_roughness_tried_){terrain_roughness_tried_=true;load_terrain_roughness(terrain_roughness_);}
  values[kTerrainMaterialFirst+8]=configured_boolean(config,"material_terrain",true)?float(material_level()):0.f;
  values[kTerrainMaterialFirst+9]=std::max(configured_floating_point(config,"material_terrain_relief",.06f),0.f);
  values[kTerrainMaterialFirst+10]=std::max(configured_floating_point(config,"material_terrain_crevices",1.f),0.f);
  std::copy(terrain_roughness_.begin(),terrain_roughness_.end(),values+kTerrainMaterialFirst+12);
  terrain_surface_bake_[0]=values[kTerrainMaterialFirst+10];
  std::copy(terrain_roughness_.begin(),terrain_roughness_.end(),terrain_surface_bake_.begin()+1);
 }
 // Wet surfaces (owned precipitation): the ramped outdoor rain of the shared
 // wind, lingering fog_after_rain_hours of game time after it (pause holds,
 // a load or jump snaps); ripples follow the rain on the ground now.
 {
  const auto& wet_environment=bridge_world_fog_snapshot().environment;
  const int precipitation=configured_integer(config,"precipitation",3);
  if(wet_environment.valid&&precipitation>0){
   const double elapsed=wet_hours_<0?-1.0:wet_environment.game_hours-wet_hours_;
   const float hours=std::max(configured_floating_point(config,"fog_after_rain_hours",4.f),.1f);
   const float rain=weather_wind_.gpu.rain;
   // A world change (map, menu, load) settles the wetness from that world's
   // own weather (wet in a restored game rain, dry otherwise); it never
   // carries across worlds (c71), and the menu is always dry.
   const bool new_world=wet_generation_!=ambient_map_generation_;
   wet_generation_=ambient_map_generation_;
   if(ambient_menu_)wet_level_=0;
   else if(new_world)wet_level_=!wet_environment.cave&&(wet_environment.weather_current==1||wet_environment.weather_next==1)&&
    configured_integer(config,"precip_test",0)==0?1.f:0.f;
   else wet_level_=elapsed<0||elapsed>1?rain:std::max(rain,wet_level_-float(elapsed)/hours);
   wet_hours_=wet_environment.game_hours;
   values[kTerrainWetFirst]=wet_environment.cave?0.f:std::clamp(wet_level_,0.f,1.f);
   values[kTerrainWetFirst+1]=wet_environment.cave?0.f:precipitation_rain_visible_;
   values[kTerrainWetFirst+2]=float(std::fmod(wet_environment.world_time*kWeatherSecondsPerSegment,4096.0));
   values[kTerrainWetFirst+3]=float(precipitation);
  }else{wet_level_=0;wet_hours_=-1;}
  // The shelter map keeps surfaces under roofs dry (nativeShelterCover).
  // Snow build-up on the game-seconds clock the precipitation ramps use
  // (user decision: watched gradually, never instant): heavy snow reaches
  // full cover in about 7 minutes at normal game speed, light snow
  // proportionally slower; it melts over about 15 minutes. Pause holds it,
  // game speed scales it; a load or jump settles it to the restored weather.
  if(wet_environment.valid&&precipitation>0){
   const double snow_seconds=wet_environment.world_time*kWeatherSecondsPerSegment;
   const double elapsed=snow_cover_hours_<0?-1.0:snow_seconds-snow_cover_hours_;
   // Only a restored game snowfall settles to full cover; the render-only
   // precip_test builds up from bare like a real shower (run p2).
   // Every world change (load, reload, map or menu) settles too: the cover
   // never carries across worlds (c71: heavy snow followed into the menu).
   const bool new_world=snow_cover_generation_!=ambient_map_generation_;
   snow_cover_generation_=ambient_map_generation_;
   if(ambient_menu_)snow_cover_=shelter_fill_=0;
   else if(new_world||elapsed<0||elapsed>60){
    const bool restored=!wet_environment.cave&&configured_integer(config,"precip_test",0)==0;
    snow_cover_=restored&&wet_environment.weather_next==2?1.f:0.f;
    // A restored rain settles the crown fill-in like a restored snowfall.
    shelter_fill_=restored&&(wet_environment.weather_next==2||wet_environment.weather_current==1||wet_environment.weather_next==1)?.5f:0.f;}
   // About 35% faster than the first pace (heavy snow full in about 4.5
   // minutes), with a small floor so even light snow builds visibly (c71).
   else{
    snow_cover_=std::clamp(snow_cover_+float(elapsed)*(precipitation_snow_visible_>0?(precipitation_snow_visible_+.15f)/311.f:-1.f/900.f),0.f,1.f);
    // Snow creeps in under crowns on its own, slower clock (full after about
    // 15 minutes of heavy snow, still growing once the open cover is full):
    // a fresh snowfall shows wide bare patches that shrink slowly (c6c05f03).
    // (A fixed pace: the visible amount exceeds 1 in heavy snow and filled it in
    // about 4 minutes, c6c05f03 evidence.)
    // Rain creeps in under crowns on the same clock (user note on e4ab0f76:
    // the longer it rains, the more it encroaches on the cover's edge); rain
    // and snow are never visible together, so one fill serves both.
    const float falling=(std::max)(precipitation_snow_visible_,precipitation_rain_visible_);
    // After rain the fill holds while the ground stays wet (wet_level_, which
    // dries over fog_after_rain_hours): a 15 minute decay brought the dry
    // patches back under trees hours before the open ground dried. After
    // snow it recedes with the melt as before.
    const bool wet_after_rain=falling<=0&&wet_level_>0&&snow_cover_<=0;
    shelter_fill_=std::clamp(shelter_fill_+float(elapsed)*(falling>0?((std::min)(falling,1.f)*.85f+.15f)/900.f:wet_after_rain?0.f:-1.f/900.f),0.f,1.f);
   }
   snow_cover_hours_=snow_seconds;
   values[kTerrainSnowFirst]=wet_environment.cave?0.f:snow_cover_;values[kTerrainSnowFirst+3]=float(precipitation);
  }else{snow_cover_=0;snow_cover_hours_=-1;}
  // Without the soft map (its filter failed, logged as
  // precip_shelter_filter_unavailable) the raw map serves, flagged in row 2 w:
  // a plain height test, no feather, but never no shelter at all.
  if(native_rain_valid_&&(d3d12_?bool(rain12_.shelter):bool(native_rain_srv_))&&(values[kTerrainWetFirst]>0||values[kTerrainSnowFirst]>0)){
   const auto& m=native_rain_slice_.world_to_light;float* row=values+kTerrainShelterFirst;
   row[0]=m[0];row[1]=m[1];row[2]=m[3];row[3]=1.f;row[4]=m[4];row[5]=m[5];row[6]=m[7];
   row[8]=std::abs(m[10])>1e-9f?-1.f/m[10]:0.f;row[9]=m[11]*row[8];row[11]=native_shelter_soft_valid_?0.f:1.f;
   row[7]=shelter_fill_; // the crown fill-in (baked into the pre-filtered cover)
   // The pre-filtered cover follows the fill-in in 1/256 steps (a blur pass,
   // blur_native_rain_shelter; the bake re-blurs too).
   if(native_shelter_soft_valid_&&std::abs(shelter_fill_-native_shelter_blur_fill_)>=1.f/256.f)blur_native_rain_shelter();
  }
  ID3D11ShaderResourceView* shelter=!native_rain_valid_?nullptr:native_shelter_soft_valid_?native_shelter_soft_srv_[0].Get():native_rain_srv_.Get();
  if(!d3d12_)context_->PSSetShaderResources(kNativeShelterSlot,1,&shelter);
  // The snow surface for the object build-up (none: its drift relief only).
  if(!d3d12_){
   ID3D11ShaderResourceView* snow=snow_surface_srv_.Get();context_->PSSetShaderResources(kSnowSurfaceSlot,1,&snow);
   if(snow){
    try{
     const auto sampling=world_texture_sampler(D3DTFN_LINEAR,D3DTFG_LINEAR,D3DTFP_LINEAR,
      D3DTADDRESS_WRAP,D3DTADDRESS_WRAP,true,1u<<configured_integer(config,"anisotropy",3));
     auto& sampler=sampler_states_[sampling.key];
     if(!sampler){const HRESULT hr=device_->CreateSamplerState(&sampling.description,&sampler);if(FAILED(hr))return hr;}
     snow_surface_sampler_=sampler;
    }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
    ID3D11SamplerState* wrap=snow_surface_sampler_.Get();context_->PSSetSamplers(kSnowSurfaceSampler,1,&wrap);
   }
  }
  // The rain ring texture (nativeRainRingSlope: wet flat ground while it
  // rains, precipitation quality 3+), re-rendered on the game clock. The
  // D3D12 world bind reads native_rain_rings_bound_ (renderer_native_d3d12.cpp).
  native_rain_rings_bound_=values[kTerrainWetFirst]>0&&values[kTerrainWetFirst+1]>0&&values[kTerrainWetFirst+3]>=3&&
   update_native_rain_rings(values[kTerrainWetFirst+2],values[kTerrainWetFirst+1]);
  if(!d3d12_){
   ID3D11ShaderResourceView* rings=native_rain_rings_bound_?native_rain_rings_srv_.Get():nullptr;
   context_->PSSetShaderResources(kNativeRainRingsSlot,1,&rings);
  }
 }
 // Fire burn (renderer_fire_burn.cpp, option fire_burn, D3D12): the area's
 // burns on the game clock (pause holds them, game speed scales them), their
 // mask (t72) and the frame's values in its last row.
 {
  const auto& environment=bridge_world_fog_snapshot().environment;
  (void)update_fire_burns(environment.valid?environment.world_time*kWeatherSecondsPerSegment:-1.0);
  update_fire_burn_substitutes();
 }
 if(d3d12_){
  try{native_terrain_frame_d3d12_.assign(values,values+kTerrainFrameFloats);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  // The shader tests of the values just kept (nativeWet.x>0, nativeSnowCover.x>0).
  native_frame_dry_=!(values[kTerrainWetFirst]>0)&&!(values[kTerrainSnowFirst]>0);
 }else{
  std::memcpy(mapped.pData,values,sizeof(values));
  context_->Unmap(native_terrain_frame_constants_.Get(),0);
 }
 ambient_terrain_state_=terrain;ambient_terrain_frame_=native_frame_index_;
 // Every stage that runs native source reads the frame (the cliff
 // tessellation stages too, terrain_cliff.h).
 if(d3d12_)return S_OK; // the D3D12 flush binds its own copy
 ID3D11ShaderResourceView* frame=native_terrain_frame_srv_.Get();
 context_->CSSetShaderResources(kNativeTerrainFrameSlot,1,&frame);context_->VSSetShaderResources(kNativeTerrainFrameSlot,1,&frame);
 context_->PSSetShaderResources(kNativeTerrainFrameSlot,1,&frame);context_->HSSetShaderResources(kNativeTerrainFrameSlot,1,&frame);
 context_->DSSetShaderResources(kNativeTerrainFrameSlot,1,&frame);
 // The terrain noise lattice for every native pixel stage (the blend, the
 // terrain material and the contact blend's macro brightness).
 ID3D11ShaderResourceView* noise=terrain_noise_srv_.Get();ID3D11SamplerState* noise_sampler=terrain_noise_sampler_.Get();
 context_->PSSetShaderResources(PointShadowTextureLayout::terrain_noise_srv,1,&noise);context_->PSSetSamplers(12,1,&noise_sampler);
 return S_OK;
}

#endif
// Cliff tessellation stages, compiled on first use (terrain_tessellation > 0).
// A failure is reported once and leaves the standard terrain path.
void append_terrain_tessellation_shader_programs(std::vector<ShaderProgram>& out){
 const std::string& source=native_shader_source();
 out.push_back({source,nullptr,nullptr,"nativeTessVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,nullptr,"nativeTerrainHS","hs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,nullptr,"nativeTerrainDS","ds_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,nullptr,"nativeTerrainHSPlain","hs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,nullptr,"nativeTerrainDSPlain","ds_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // The depth prepass's tessellated terrain stages (positions through the
 // same functions; see nativePrepassTessVS).
 for(const char* entry:{"nativePrepassTessVS","nativePrepassHS","nativePrepassSnowHS","nativePrepassDS","nativePrepassSnowDS"})
  out.push_back({source,nullptr,nullptr,entry,entry[std::strlen(entry)-2]=='V'?"vs_5_0":entry[std::strlen(entry)-2]=='H'?"hs_5_0":"ds_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

#if !defined(EID3D11_SHADER_EMBED)
HRESULT Renderer::ensure_terrain_tessellation() noexcept{
 if(terrain_tess_vs_&&terrain_hs_&&terrain_ds_&&terrain_hs_plain_&&terrain_ds_plain_)return S_OK;
 if(terrain_tessellation_failed_||!device_||feature_level_<D3D_FEATURE_LEVEL_11_0)return E_FAIL;
 std::vector<ShaderProgram> programs;append_terrain_tessellation_shader_programs(programs);
 const auto compile=[&](const ShaderProgram& program,ComPtr<ID3DBlob>& code){
  ComPtr<ID3DBlob> error;
  const HRESULT hr=compile_shader(program,&code,&error);
  if(FAILED(hr)&&error)OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
  return hr;
 };
 ComPtr<ID3DBlob> vs,hs,ds,hs_plain,ds_plain;HRESULT hr=S_OK;
 if(FAILED(hr=compile(programs[0],vs))||FAILED(hr=compile(programs[1],hs))||
    FAILED(hr=compile(programs[2],ds))||FAILED(hr=compile(programs[3],hs_plain))||FAILED(hr=compile(programs[4],ds_plain))||
    FAILED(hr=create_shader(device_.Get(),vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&terrain_tess_vs_))||
    FAILED(hr=create_shader(device_.Get(),hs->GetBufferPointer(),hs->GetBufferSize(),nullptr,&terrain_hs_))||
    FAILED(hr=create_shader(device_.Get(),ds->GetBufferPointer(),ds->GetBufferSize(),nullptr,&terrain_ds_))||
    FAILED(hr=create_shader(device_.Get(),hs_plain->GetBufferPointer(),hs_plain->GetBufferSize(),nullptr,&terrain_hs_plain_))||
    FAILED(hr=create_shader(device_.Get(),ds_plain->GetBufferPointer(),ds_plain->GetBufferSize(),nullptr,&terrain_ds_plain_))){
  terrain_tess_vs_.Reset();terrain_hs_.Reset();terrain_ds_.Reset();terrain_hs_plain_.Reset();terrain_ds_plain_.Reset();terrain_tessellation_failed_=true;
  bridge_append_probe_event("terrain_tessellation_omitted","cliff tessellation shaders failed to compile or create; terrain draws untessellated");
  return FAILED(hr)?hr:E_FAIL;
 }
 // The prepass stages: without them tessellated terrain stays out of the
 // depth prepass (flush_native).
 ComPtr<ID3DBlob> prepass[5];
 for(unsigned n=0;n<5&&SUCCEEDED(hr);++n)hr=compile(programs[5+n],prepass[n]);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),prepass[0]->GetBufferPointer(),prepass[0]->GetBufferSize(),nullptr,&terrain_prepass_tess_vs_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),prepass[1]->GetBufferPointer(),prepass[1]->GetBufferSize(),nullptr,&terrain_prepass_hs_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),prepass[2]->GetBufferPointer(),prepass[2]->GetBufferSize(),nullptr,&terrain_prepass_snow_hs_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),prepass[3]->GetBufferPointer(),prepass[3]->GetBufferSize(),nullptr,&terrain_prepass_ds_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),prepass[4]->GetBufferPointer(),prepass[4]->GetBufferSize(),nullptr,&terrain_prepass_snow_ds_);
 if(FAILED(hr)){
  terrain_prepass_tess_vs_.Reset();terrain_prepass_hs_.Reset();terrain_prepass_snow_hs_.Reset();terrain_prepass_ds_.Reset();terrain_prepass_snow_ds_.Reset();
  bridge_append_probe_event("terrain_prepass_tessellation_omitted","prepass tessellation stages failed to compile or create; tessellated terrain draws without the depth prepass");
 }
 return S_OK;
}

#endif
} // namespace owned
