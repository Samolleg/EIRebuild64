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
#include "x64_remote_device.h"
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
namespace {
// NativeTerrainFrameValues (register t127 in the native source).
constexpr UINT kNativeTerrainFrameSlot=PointShadowTextureLayout::terrain_frame_srv;
// The precipitation shelter map for wet surfaces (nativeShelter, t70).
constexpr UINT kNativeShelterSlot=70;
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
  D3D11_TEXTURE2D_DESC d{};d.Width=tiles_x;d.Height=tiles_y;d.MipLevels=d.ArraySize=1;
  d.Format=DXGI_FORMAT_R32G32B32A32_UINT;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  const D3D11_SUBRESOURCE_DATA initial{tiles.data(),tiles_x*16u,0};
  ComPtr<ID3D11Texture2D> texture;
  if(FAILED(hr=device_->CreateTexture2D(&d,&initial,&texture))||
     FAILED(hr=device_->CreateShaderResourceView(texture.Get(),nullptr,&maps.tiles)))return hr;
  maps.pending=used;
  if(rows)*rows=std::move(tiles); // terrain material blend: the active map keeps its rows
 return S_OK;
}

HRESULT Renderer::copy_contact_atlas(ID3D11Texture2D* source,UINT source_slice,UINT material,ContactBlendMaps& maps,
 char* detail,std::size_t detail_size){
 HRESULT hr=S_OK;
 const std::uint64_t bit=std::uint64_t{1}<<material;
  D3D11_TEXTURE2D_DESC s{};source->GetDesc(&s);
  if(!maps.texture){
   UINT skip=0;
   while(((s.Width>>skip)>contact_blend_atlas_side||(s.Height>>skip)>contact_blend_atlas_side)&&skip+1<s.MipLevels)++skip;
   D3D11_TEXTURE2D_DESC a{};a.Width=std::max(1u,s.Width>>skip);a.Height=std::max(1u,s.Height>>skip);
   a.MipLevels=s.MipLevels-skip;a.ArraySize=0;
   for(std::uint32_t m=0;m<contact_blend_max_materials;++m)if((maps.pending>>m)&1u)a.ArraySize=m+1;
   a.Format=s.Format;a.SampleDesc.Count=1;a.Usage=D3D11_USAGE_DEFAULT;a.BindFlags=D3D11_BIND_SHADER_RESOURCE;
   if(FAILED(hr=device_->CreateTexture2D(&a,nullptr,&maps.texture)))return hr;
   D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=a.Format;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
   view.Texture2DArray.MipLevels=a.MipLevels;view.Texture2DArray.ArraySize=a.ArraySize;
   if(FAILED(hr=device_->CreateShaderResourceView(maps.texture.Get(),&view,&maps.atlas)))return hr;
   maps.width=a.Width;maps.height=a.Height;maps.mips=a.MipLevels;maps.format=a.Format;
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
   context_->CopySubresourceRegion(maps.texture.Get(),D3D11CalcSubresource(mip,material,maps.mips),0,0,0,
    source,D3D11CalcSubresource(mip+skip,source_slice,s.MipLevels),nullptr);
  maps.ready|=bit;
 return S_OK;
}

void Renderer::release_contact_blend_maps() noexcept{
 contact_maps_.tiles.Reset();contact_maps_.texture.Reset();contact_maps_.atlas.Reset();
 contact_maps_.ready=0;contact_maps_.pending=0;contact_maps_.mips=0;
 contact_tile_rows_.clear();contact_tiles_x_=contact_tiles_y_=0;tile_blend_ready_=tile_blend_rows_=false;
}
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
  // Built when either user is enabled and the terrain source is readable; retried otherwise.
  if(configured_floating_point(debug_overlay_.config(),"contact_blend",.7f)<=0&&
     !configured_terrain_cliffs(debug_overlay_.config())&&
     configured_integer(debug_overlay_.config(),"terrain_blend",1)<=0&&
     configured_integer(debug_overlay_.config(),"ground_cover",2)<=0)return S_OK;
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
 if(configured_integer(debug_overlay_.config(),"terrain_blend",1)>0&&contact_maps_.tiles&&!tile_blend_ready_&&
    contact_map_generation_==ambient_map_generation_&&poll_terrain_tile_families()){
  if(FAILED(hr=apply_terrain_tile_blend(tile_families_)))return hr;
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
  if(hr==S_FALSE&&!contact_incompatible_logged_){
   contact_incompatible_logged_=true;
   char text[256];
   std::snprintf(text,sizeof(text),"terrain atlas format or size differs (%s); affected contact art omitted",detail);
   bridge_append_probe_event("contact_blend_atlas_incompatible",text);
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
 std::size_t blended=0,plain_materials=0,bed_families=0;
 const auto rows_start=std::chrono::steady_clock::now();
 const auto rows=owned_terrain_tile_blend_rows(owned_terrain_tile_blend_input(terrain,tiles_x,tiles_y),families,blended,plain_materials,bed_families);
 const double rows_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-rows_start).count();
 std::vector<std::array<std::uint32_t,4>> grid(contact_tile_rows_);
 grid.insert(grid.end(),rows.begin(),rows.end());
 D3D11_TEXTURE2D_DESC d{};d.Width=tiles_x;d.Height=tiles_y*2;d.MipLevels=d.ArraySize=1;
 d.Format=DXGI_FORMAT_R32G32B32A32_UINT;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 const D3D11_SUBRESOURCE_DATA initial{grid.data(),tiles_x*16u,0};
 ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
 HRESULT hr=S_OK;
 if(FAILED(hr=device_->CreateTexture2D(&d,&initial,&texture))||FAILED(hr=device_->CreateShaderResourceView(texture.Get(),nullptr,&view)))return hr;
 contact_maps_.tiles=view;tile_blend_rows_=true;
 char detail[400];
 std::snprintf(detail,sizeof(detail),"%s: %zu of %zu tiles near transitions blend, %zu plain materials, %zu liquid-bed families, rows %.2f ms",
  families.source.c_str(),blended,rows.size(),plain_materials,bed_families,rows_ms);
 bridge_append_probe_event("terrain_tile_blend",detail);
 return S_OK;
}catch(...){return E_OUTOFMEMORY;}

HRESULT Renderer::set_native_terrain_layer_shifts(const std::vector<float>& shifts) noexcept try{SCENE_ENTRY("set_native_terrain_layer_shifts");
 if(!device_||!context_)return E_FAIL;
 if(shifts==native_terrain_layer_shifts_)return S_OK;
 for(float value:shifts)if(!std::isfinite(value))return E_INVALIDARG;
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
 if(!device_||!context_)return E_FAIL;
 if(layer_shifts){const HRESULT hr=set_native_terrain_layer_shifts(*layer_shifts);if(FAILED(hr))return hr;}
 // The active map's saved-layout origin (once a map): its terrain material
 // layouts use the same frame as when it is drawn as a neighbour.
 if(const auto source=retained_static_terrain_source_snapshot();source.map&&source.map->generation!=terrain_layout_origin_generation_){
  terrain_layout_origin_generation_=source.map->generation;
  double x=0,y=0;ResourceStoreConfig mounts;
  const bool listed=owned_resource_runtime_mounts(mounts)&&neighbour_layout_origin(source.map->terrain_resource,mounts,x,y);
  terrain_layout_origin_={neighbour_layout_tile_origin(x),neighbour_layout_tile_origin(y)};
  char text[160];std::snprintf(text,sizeof(text),"%s origin=%.2f,%.2f frame=%.0f,%.0f",listed?"layout":"not_in_layout",x,y,
   terrain_layout_origin_[0],terrain_layout_origin_[1]);
  bridge_append_probe_event("terrain_layout_frame",text);
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
  kTerrainFrameFloats=kTerrainSnowFirst+4;
 if(!native_terrain_frame_constants_){
  D3D11_BUFFER_DESC description{};description.ByteWidth=sizeof(float)*kTerrainFrameFloats;description.Usage=D3D11_USAGE_DYNAMIC;
  description.BindFlags=D3D11_BIND_SHADER_RESOURCE;description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;description.StructureByteStride=description.ByteWidth;
  HRESULT hr=device_->CreateBuffer(&description,nullptr,&native_terrain_frame_constants_);
  if(SUCCEEDED(hr))hr=device_->CreateShaderResourceView(native_terrain_frame_constants_.Get(),nullptr,&native_terrain_frame_srv_);
  if(FAILED(hr)){native_terrain_frame_constants_.Reset();native_terrain_frame_srv_.Reset();return hr;}
 }
 D3D11_MAPPED_SUBRESOURCE mapped{};
 const HRESULT hr=context_->Map(native_terrain_frame_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
 if(FAILED(hr))return hr;
 const auto& config=debug_overlay_.config();
 // Tree sway reads the same wind and world clock; strength 0 disables it.
 static_assert(24*sizeof(float)+sizeof(WeatherWindGpu)==128,"terrain frame layout");
 const float tree_sway=configured_boolean(config,"tree_sway",true)?
  float(configured_integer(config,"tree_sway_strength",100))/100.f:0.f;
 // One renderer wind per world frame, from the same game wind and clock.
 if(weather_wind_frame_!=native_frame_index_){
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
  0.f,material.antitile?1.f:0.f, // x unused (the removed material side projection)
  material.height_blend?1.f:0.f,0.f}; // w unused (the removed large-scale variation)
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
 // on (2: fast plain reads below the high terrain detail, which alone has
 // anti-tiling), and the tessellation's largest factor and offset once its
 // stages exist.
 if(configured_terrain_cliffs(config)&&map_source.terrain)
  values[44]=configured_integer(config,"terrain_quality",1)>=1?1.f:2.f;
 // Terrain material blend (nativeTerrainCliff.zw): its rows are in the grid,
 // and a slot's atlas padding as a fraction of its side. (The material
 // cache's bake always runs the full blend, NATIVE_TERRAIN_BAKE_PASS.)
 if(const int blend=configured_integer(config,"terrain_blend",1);blend>0&&tile_blend_rows_&&contact_maps_.tiles&&map_source.terrain){values[46]=blend>=2?2.f:1.f;values[47]=tile_blend_margin_;}
 // The switched-off terrain features as uploaded (native specialisation key bits 0-4).
 native_spec_terrain_bits_=(values[17]==0?1u:0u)|(values[18]==0?2u:0u)|(values[32]==0?4u:0u)|(values[44]==0?8u:0u)|(values[46]==0?16u:0u);
 if(const float factor=terrain_tessellation_max_factor(configured_integer(config,"terrain_tessellation",1));
    factor>0&&map_source.terrain&&SUCCEEDED(ensure_terrain_tessellation())){
  values[45]=factor;
  values[48]=terrain_tessellation_height;
 }
 // Temporary: terrain blend diagnostic view (0 off, 1 field, 2 planes, 3 masks).
 values[49]=float(std::clamp(configured_integer(config,"terrain_blend_debug",0),0,6));
 // Far de-tiling of the blend's colour reads (nativeTerrainTess.w, terrain_tile_blend.hlsl).
 values[51]=configured_boolean(config,"terrain_far_detile",true)?1.f:0.f;
 // Live Cliff blend options (terrain_tile_blend.h).
 terrain_cliff_blend_values([&](const TerrainCliffBlendOption& option){return configured_floating_point(config,option.name,option.fallback);},
  values[50],values+kTerrainCliffBlendFirst);
 terrain_tessellation_frame_=values[45]>0?native_frame_index_:UINT64_MAX;
 // Snow trails: the field state current now. The cover pass writes only the
 // other ping-pong texture, so this texture and window stay paired until the
 // next terrain frame.
 snow_terrain_srv_=gc_snow_valid_&&gc_snow_map_generation_==ambient_map_generation_?gc_snow_srv_[gc_snow_index_]:nullptr;
 snow_terrain_window_[0]=float(gc_snow_origin_[0])*snow_trails::kTexel;snow_terrain_window_[1]=float(gc_snow_origin_[1])*snow_trails::kTexel;
 snow_terrain_window_[2]=1.f/snow_trails::kTexel;snow_terrain_window_[3]=snow_terrain_srv_?float(snow_trails::kSide):0.f;
 std::memcpy(snow_terrain_tiles_,gc_snow_active_.data(),sizeof(snow_terrain_tiles_));
 std::memcpy(values+kTerrainSnowTrailFirst,snow_terrain_window_,sizeof(snow_terrain_window_));
 std::memcpy(values+kTerrainSnowTrailFirst+4,snow_terrain_tiles_,sizeof(snow_terrain_tiles_));
 // The trail window's smooth centre (snowTrailRaw fades by distance from it).
 values[kTerrainSnowFirst+1]=gc_snow_centre_[0];values[kTerrainSnowFirst+2]=gc_snow_centre_[1];
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
   else if(new_world||elapsed<0||elapsed>60){snow_cover_=!wet_environment.cave&&wet_environment.weather_next==2&&
    configured_integer(config,"precip_test",0)==0?1.f:0.f;shelter_fill_=snow_cover_*.5f;}
   // About 35% faster than the first pace (heavy snow full in about 4.5
   // minutes), with a small floor so even light snow builds visibly (c71).
   else{
    snow_cover_=std::clamp(snow_cover_+float(elapsed)*(precipitation_snow_visible_>0?(precipitation_snow_visible_+.15f)/311.f:-1.f/900.f),0.f,1.f);
    // Snow creeps in under crowns on its own, slower clock (full after about
    // 15 minutes of heavy snow, still growing once the open cover is full):
    // a fresh snowfall shows wide bare patches that shrink slowly (c6c05f03).
    // (A fixed pace: the visible amount exceeds 1 in heavy snow and filled it in
    // about 4 minutes, c6c05f03 evidence.)
    shelter_fill_=std::clamp(shelter_fill_+float(elapsed)*(precipitation_snow_visible_>0?(std::min(precipitation_snow_visible_,1.f)*.85f+.15f)/900.f:-1.f/900.f),0.f,1.f);
   }
   snow_cover_hours_=snow_seconds;
   values[kTerrainSnowFirst]=wet_environment.cave?0.f:snow_cover_;values[kTerrainSnowFirst+3]=float(precipitation);
  }else{snow_cover_=0;snow_cover_hours_=-1;}
  // Without the soft map (its filter failed, logged as
  // precip_shelter_filter_unavailable) the raw map serves, flagged in row 2 w:
  // a plain height test, no feather, but never no shelter at all.
  if(native_rain_valid_&&native_rain_srv_&&(values[kTerrainWetFirst]>0||values[kTerrainSnowFirst]>0)){
   const auto& m=native_rain_slice_.world_to_light;float* row=values+kTerrainShelterFirst;
   row[0]=m[0];row[1]=m[1];row[2]=m[3];row[3]=1.f;row[4]=m[4];row[5]=m[5];row[6]=m[7];
   row[8]=std::abs(m[10])>1e-9f?-1.f/m[10]:0.f;row[9]=m[11]*row[8];row[11]=native_shelter_soft_valid_?0.f:1.f;
   row[7]=shelter_fill_; // the crown fill-in (shelterTexelKeep)
  }
  ID3D11ShaderResourceView* shelter=!native_rain_valid_?nullptr:native_shelter_soft_valid_?native_shelter_soft_srv_[1].Get():native_rain_srv_.Get();
  context_->PSSetShaderResources(kNativeShelterSlot,1,&shelter);
 }
 std::memcpy(mapped.pData,values,sizeof(values));
 context_->Unmap(native_terrain_frame_constants_.Get(),0);
 ambient_terrain_state_=terrain;ambient_terrain_frame_=native_frame_index_;
 // Every stage that runs native source reads the frame (the cliff
 // tessellation stages too, terrain_cliff.h).
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
