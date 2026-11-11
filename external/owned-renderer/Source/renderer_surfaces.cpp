// Renderer surfaces: game surface uploads, pixel release and restore, owned texture slices and their budget.
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
#include "surface_convert.h"
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
namespace {


// The texture holds exactly this generation and nothing needs the CPU bytes.
bool surface_pixels_releasable(const SurfaceImage& image) noexcept {
 return !image.scene_snapshot&&!(image.visual_source&&image.published_generation!=image.generation)&&
    image.gpu_primary&&!image.cpu_pinned&&!image.cpu_released&&!image.cpu_access_active&&
    !image.gpu_captured&&!image.pixels.empty()&&image.texture&&image.srv&&
    image.uploaded_generation==image.generation&&!image.dirty_full&&!image.dirty_rect_valid&&
    surface_pixels_round_trip_exact(image);
}
void drop_surface_pixels(SurfaceImage& image) noexcept {
 image.pixels.clear();
 image.cpu_released=true;
 update_surface_memory_accounting(image);
}
// Upload alone is not consumption: loading and composition still need these
// bytes. Only draw-only generations that survived the sampling grace may drop
// their exact CPU copy. Known CPU readers remain resident.
void release_uploaded_surface_pixels(SurfaceImage& image) noexcept {
 // A published visual source is consumed as soon as its publication took
 // its CPU facts: the owned renderer draws the published value, never this
 // proxy surface, so its sampling grace would never start (the per-load
 // character and HUD textures kept 40+ MB until the idle sweep).
 const bool published=image.visual_source&&image.published_generation==image.generation;
 if(!published&&(image.sampled_generation!=image.generation||
    image.sampled_frames<Renderer::kSurfaceCpuGraceFrames))return;
 if(!surface_pixels_releasable(image))return;
 drop_surface_pixels(image);
}
}  // namespace

// A surface the renderer never samples (the game's own textures for objects
// the owned scene draws from its archive copies; DrawAll creates hundreds of
// MB of them, #188) still drops its CPU copy once the game has left it alone
// for a while. The caller counts presents, so a blocking load (no presents)
// keeps every byte it may still read; a later Lock restores exactly.
void Renderer::release_idle_surface_pixels(SurfaceImage& image) noexcept {
 if(surface_pixels_releasable(image))drop_surface_pixels(image);
}

HRESULT Renderer::upload(SurfaceImage& i,SurfaceUploadResult* output){
 const frame_acct::Scope upload_scope(frame_acct::ours_surface_upload);
 // Scene thread option: the game thread owns every proxy surface and makes
 // its uploads itself (game_upload); queued commands carry pixel-free
 // snapshots whose GPU home is already current.
 if(scene_queue())return game_upload(i,output);
 // D3D12 backend: a snapshot carries its surface's D3D12 texture. World use
 // of a static surface whose texture lacks mips takes one mipped copy per
 // generation and texture object, as D3D11 below, built from the host
 // table's pixels of that generation (this backend keeps them).
 if(d3d12_&&i.scene_snapshot){
  if(output)*output={};
  UINT width=0,height=0,levels=0;DXGI_FORMAT stored=DXGI_FORMAT_UNKNOWN;
  if(!i.d3d12||!d3d12_->texture_info(*i.d3d12,width,height,levels,stored)){
   // Rebuild only from the exact generation retained in the surface table.
   // A pixel-free snapshot must never reuse a previous executor's texture.
   const HRESULT restored=upload_d3d12(i,output);if(FAILED(restored))return restored;
  }
  DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;UINT blocks=0;
  if(!surface_dxgi_format(i.format,format,blocks))return E_UNEXPECTED;
  if(!(i.static_mip_hint&&i.world_mip_use&&!i.gpu_captured&&!blocks)||
     (d3d12_->texture_info(*i.d3d12,width,height,levels,stored)&&levels>1))return S_OK;
  auto& cached=scene_mip_textures_[i.id];
  if(!(cached.d3d12&&cached.generation==i.generation&&cached.texture_serial==i.texture_serial)){
   cached={};
   const auto found=core_surfaces_.find(i.id);
   if(found==core_surfaces_.end()||!found->second||found->second->generation!=i.generation||found->second->cpu_released){
    // The host moved past this snapshot's generation: its bytes are gone.
    scene_mip_textures_.erase(i.id);
    d3d12_->omit("world surface textures: a superseded snapshot keeps one level",false);
    return S_OK;
   }
   D3D12TextureRef texture;
   const HRESULT hr=surface_mips_d3d12(*found->second,texture);
   if(FAILED(hr)){scene_mip_textures_.erase(i.id);return hr;}
   cached.generation=i.generation;cached.texture_serial=i.texture_serial;cached.d3d12=std::move(texture);
  }
  i.d3d12=cached.d3d12;
  return S_OK;
 }
 if(i.scene_snapshot){
  if(output)*output={};
  D3D11_TEXTURE2D_DESC desc{};if(i.texture)i.texture->GetDesc(&desc);
  DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;UINT blocks=0;
  if(!i.texture||!i.srv||i.uploaded_generation!=i.generation||!surface_dxgi_format(i.format,format,blocks))return E_UNEXPECTED;
  const bool mips=i.static_mip_hint&&i.world_mip_use&&!i.gpu_captured&&!blocks;
  if(!mips||desc.MipLevels>1)return S_OK;
  // World use wants mips the game's texture lacks: one GPU-built mipped
  // copy per generation and texture object, reused by later snapshots.
  auto& cached=scene_mip_textures_[i.id];
  if(!(cached.texture&&cached.generation==i.generation&&cached.texture_serial==i.texture_serial)){
   cached={};
   SurfaceImage copy=i;copy.cpu_released=true;copy.scene_snapshot=false;
   SurfaceUploadResult result{};
   const HRESULT hr=upload_surface_image(device_.Get(),context_.Get(),copy,&result);
   copy.scene_snapshot=true;  // never accounted
   if(FAILED(hr)||!copy.texture||!copy.srv){scene_mip_textures_.erase(i.id);return SUCCEEDED(hr)?E_FAIL:hr;}
   cached.generation=i.generation;cached.texture_serial=i.texture_serial;cached.texture=copy.texture;cached.srv=copy.srv;
  }
  i.texture=cached.texture;i.srv=cached.srv;
  return S_OK;
 }
 // An output blit queued from this GPU home precedes a later CPU upload.
 // Submit it before changing the resource, so queued UI keeps its old pixels.
 if(i.uploaded_generation!=i.generation || i.dirty_full || i.dirty_rect_valid ||
    !same_surface_upload_shape(i.uploaded_shape,surface_upload_shape(i))){
  const bool queued=std::any_of(pending_blits_.begin(),pending_blits_.end(),
   [&](const PendingBlit& blit){return blit.gpu&&blit.source_is_surface&&blit.source_id==i.id;});
  if(queued){const HRESULT flushed=render_pending_blits();if(FAILED(flushed))return flushed;}
 }
 if(d3d12_)return upload_d3d12(i,output);
 if(i.cpu_released&&(i.uploaded_generation!=i.generation||
    !same_surface_upload_shape(i.uploaded_shape,surface_upload_shape(i)))){
  // A key change re-converts the unchanged CPU bytes (its writer marked them dirty).
  const HRESULT restored=restore_surface_pixels(i);if(FAILED(restored))return restored;
 }
 // A new generation is written into the existing texture. When the owned
 // cache shares it, move the surface to an exact copy first.
 if(i.texture_shared&&i.texture&&(i.uploaded_generation!=i.generation||i.dirty_full||i.dirty_rect_valid)){
  const HRESULT detached=detach_shared_surface_texture(i,"upload");if(FAILED(detached))return detached;
 }
 ID3D11Texture2D* const shared_before=i.texture_shared?i.texture.Get():nullptr; // compared only, never dereferenced
 SurfaceUploadResult result{};const HRESULT hr=upload_surface_image(device_.Get(),context_.Get(),i,&result);
 if(shared_before&&i.texture.Get()!=shared_before)unshare_owned_texture(i,shared_before); // recreated (mip layout)
 if(result.uploaded||FAILED(hr))update_surface_memory_accounting(i);if(SUCCEEDED(hr)&&result.uploaded){if(perf_enabled()){++perf_upload_misses_;perf_upload_bytes_+=result.upload_bytes;}profiler_.add_upload(result.upload_bytes);release_uploaded_surface_pixels(i);}if(output)*output=result;return hr;
}

// x64 host, renderer_backend=d3d12: the surface's D3D12 texture from its CPU
// pixels (whole on a new shape, else the dirty rows). The pixels stay: this
// backend has no readback to restore released bytes from.
HRESULT Renderer::upload_d3d12(SurfaceImage& i,SurfaceUploadResult* output){
 if(output)*output={};
 DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;UINT block_bytes=0;
 if(!surface_dxgi_format(i.format,format,block_bytes))return E_NOTIMPL;
 const SurfaceUploadShape shape=surface_upload_shape(i);
 // D3D11 (upload_surface_image): a static surface in world use carries its
 // full mip chain, rebuilt whole from level 0 on each upload.
 const bool mips=i.static_mip_hint&&i.world_mip_use&&!i.gpu_captured&&!block_bytes;
 const UINT levels=block_bytes?1u+static_cast<UINT>(i.bc_mips.size()):(mips?full_mip_count(i.width,i.height):1u);
 if(levels>full_mip_count(i.width,i.height))return E_INVALIDARG;
 UINT stored_width=0,stored_height=0,stored_levels=0;DXGI_FORMAT stored_format=DXGI_FORMAT_UNKNOWN;
 const bool matches=same_surface_upload_shape(i.uploaded_shape,shape)&&i.d3d12&&
  d3d12_->texture_matches(*i.d3d12,i.width,i.height,format)&&
  d3d12_->texture_info(*i.d3d12,stored_width,stored_height,stored_levels,stored_format)&&
  stored_levels==levels;
 if(matches&&i.uploaded_generation==i.generation)return S_OK;
 // Captures have no current CPU copy. A replacement executor cannot restore
 // their content from the old allocation or from the stale pixels vector.
 if(i.gpu_captured&&!matches)return DXGI_ERROR_DEVICE_RESET;
 if(i.cpu_released){
  // A queued command's pixel-free snapshot (surface_snapshot): the host
  // table's image of the same generation keeps the bytes on this backend;
  // it uploads, and the snapshot takes its GPU home.
  const auto found=core_surfaces_.find(i.id);
  if(found==core_surfaces_.end()||!found->second||found->second.get()==&i||found->second->generation!=i.generation||
     found->second->cpu_released)return E_UNEXPECTED;
  const HRESULT uploaded=upload_d3d12(*found->second,output);if(FAILED(uploaded))return uploaded;
  i.d3d12=found->second->d3d12;i.texture_serial=found->second->texture_serial;
  i.uploaded_generation=found->second->uploaded_generation;i.uploaded_shape=found->second->uploaded_shape;
  return S_OK;
 }
 if(mips){
  D3D12TextureRef texture=matches?i.d3d12:nullptr;
  const HRESULT hr=surface_mips_d3d12(i,texture);if(FAILED(hr))return hr;
  if(texture!=i.d3d12){i.d3d12=std::move(texture);++i.texture_serial;}
  d3d12_account_surface(*i.d3d12,texture_storage_bytes(i.d3d12.get()));
  i.uploaded_generation=i.generation;i.uploaded_shape=shape;i.mip_source_used=true;
  clear_surface_dirty(i);
  if(output){output->uploaded=true;output->recreated=!matches;output->dirty_rect=RECT{0,0,LONG(i.width),LONG(i.height)};
   output->upload_bytes=uncompressed_mip_bytes(i.width,i.height,4);output->full_surface=true;}
  return S_OK;
 }
 RECT rect{};surface_convert::Prepared prepared{};
 HRESULT hr=prepare_surface_upload(i,!matches,block_bytes,rect,prepared);if(FAILED(hr))return hr;
 D3D12TextureRef texture=matches?i.d3d12:nullptr;
 if(!texture&&FAILED(hr=d3d12_->create_texture(i.width,i.height,format,texture,levels)))return hr;
 if(FAILED(hr=d3d12_->write_texture(*texture,rect,prepared.data,prepared.pitch)))return hr;
 for(UINT level=1;block_bytes&&level<levels;++level){
  const UINT w=(std::max)(1u,i.width>>level),h=(std::max)(1u,i.height>>level);
  const UINT pitch=((w+3)/4)*block_bytes,rows=(h+3)/4;
  const auto& bytes=i.bc_mips[level-1];
  if(bytes.size()!=std::size_t(pitch)*rows)return E_INVALIDARG;
  if(FAILED(hr=d3d12_->write_texture(*texture,RECT{0,0,LONG(w),LONG(h)},bytes.data(),pitch,level)))return hr;
  prepared.bytes+=std::uint64_t(pitch)*rows;
 }
 if(texture!=i.d3d12){i.d3d12=std::move(texture);++i.texture_serial;}
  d3d12_account_surface(*i.d3d12,texture_storage_bytes(i.d3d12.get()));
 i.uploaded_generation=i.generation;i.uploaded_shape=shape;i.gpu_captured=false;i.mip_source_used=true;
 clear_surface_dirty(i);
 if(output){output->uploaded=true;output->recreated=!matches;output->dirty_rect=rect;output->upload_bytes=prepared.bytes;
  output->full_surface=rect.left==0&&rect.top==0&&rect.right==LONG(i.width)&&rect.bottom==LONG(i.height);}
 return S_OK;
}

HRESULT Renderer::write_box_mips_d3d12(D3D12Texture& texture,std::vector<std::uint32_t>& level,UINT width,UINT height,UINT levels){
 UINT w=width,h=height;
 std::vector<std::uint32_t> next;
 for(UINT index=0;;++index){
  const HRESULT hr=d3d12_->write_texture(texture,RECT{0,0,LONG(w),LONG(h)},level.data(),w*4u,index);
  if(FAILED(hr)||index+1>=levels)return hr;
  const UINT nw=(std::max)(1u,w/2),nh=(std::max)(1u,h/2);
  try{next.resize(std::size_t(nw)*nh);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  for(UINT y=0;y<nh;++y){
   const std::uint32_t* const r0=level.data()+std::size_t((std::min)(y*2,h-1))*w;
   const std::uint32_t* const r1=level.data()+std::size_t((std::min)(y*2+1,h-1))*w;
   for(UINT x=0;x<nw;++x){
    const UINT x0=(std::min)(x*2,w-1),x1=(std::min)(x*2+1,w-1);
    const std::uint32_t a=r0[x0],b=r0[x1],c=r1[x0],d=r1[x1];
    std::uint32_t out=0;
    for(UINT shift=0;shift<32;shift+=8)
     out|=((((a>>shift)&255u)+((b>>shift)&255u)+((c>>shift)&255u)+((d>>shift)&255u)+2u)/4u)<<shift;
    next[std::size_t(y)*nw+x]=out;
   }
  }
  level.swap(next);w=nw;h=nh;
 }
}

HRESULT Renderer::surface_mips_d3d12(SurfaceImage& image,D3D12TextureRef& texture){
 DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;UINT block_bytes=0;
 if(!surface_dxgi_format(image.format,format,block_bytes)||block_bytes||format!=DXGI_FORMAT_B8G8R8A8_UNORM)return E_INVALIDARG;
 RECT rect{};surface_convert::Prepared prepared{};
 HRESULT hr=prepare_surface_upload(image,true,0,rect,prepared);if(FAILED(hr))return hr;
 const UINT width=image.width,height=image.height,levels=full_mip_count(width,height);
 std::vector<std::uint32_t> level;
 if(prepared.data==prepared.converted.data()&&prepared.pitch==width*4u&&prepared.converted.size()>=std::size_t(width)*height)
  level.swap(prepared.converted);
 else{
  try{level.resize(std::size_t(width)*height);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  for(UINT y=0;y<height;++y)
   std::memcpy(level.data()+std::size_t(y)*width,static_cast<const std::uint8_t*>(prepared.data)+std::size_t(y)*prepared.pitch,width*4u);
 }
 UINT stored_width=0,stored_height=0,stored_levels=0;DXGI_FORMAT stored_format=DXGI_FORMAT_UNKNOWN;
 if(!(texture&&d3d12_->texture_info(*texture,stored_width,stored_height,stored_levels,stored_format)&&stored_width==width&&
      stored_height==height&&stored_levels==levels&&stored_format==format)){
  texture.reset();
  if(FAILED(hr=d3d12_->create_texture(width,height,format,texture,levels)))return hr;
 }
 return write_box_mips_d3d12(*texture,level,width,height,levels);
}

void Renderer::release_surface_pixels(SurfaceImage& image) noexcept{if(!image.scene_snapshot&&!d3d12_)release_uploaded_surface_pixels(image);}

HRESULT Renderer::detach_shared_surface_texture(SurfaceImage& image,const char* writer){
 if(!image.texture_shared||!image.texture)return S_OK;
 if(!device_||!context_)return E_FAIL;
 D3D11_TEXTURE2D_DESC description{};image.texture->GetDesc(&description);
 ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> srv;
 HRESULT hr=device_->CreateTexture2D(&description,nullptr,&texture);if(FAILED(hr))return hr;
 if(FAILED(hr=device_->CreateShaderResourceView(texture.Get(),nullptr,&srv)))return hr;
 context_->CopyResource(texture.Get(),image.texture.Get());
 const ComPtr<ID3D11Texture2D> previous=image.texture;
 image.texture=std::move(texture);image.srv=std::move(srv);++image.texture_serial;
 unshare_owned_texture(image,previous.Get());
 // Evidence that rewrites of shared world textures stay rare (#142).
 const auto count=++shared_texture_rewrites_;
 if(count<=32||!(count&(count-1))){
  char detail[160];std::snprintf(detail,sizeof(detail),"writer=%s id=%llu %ux%u format=%u count=%llu",writer,
   static_cast<unsigned long long>(image.id),description.Width,description.Height,unsigned(description.Format),
   static_cast<unsigned long long>(count));
  bridge_append_probe_event("shared_texture_rewrite",detail);
 }
 return S_OK;
}

void Renderer::unshare_owned_texture(SurfaceImage& image,ID3D11Texture2D* texture) noexcept{
 image.texture_shared=false;
 {std::lock_guard<std::mutex> holds(scene_surface_holds_lock_); // a scene-mode publication of it ends too
  const auto found=scene_surface_holds_.find(image.id);
  if(found!=scene_surface_holds_.end()&&found->second==texture)scene_surface_holds_.erase(found);}
 std::lock_guard<std::mutex> guard(native_owned_textures_lock_);
 for(auto& entry:native_owned_textures_)
  if(entry.second.surface_shared&&entry.second.surface_id==image.id&&entry.second.texture.Get()==texture)
   entry.second.surface_shared=false;
}

void Renderer::sampled_surface_pixels(SurfaceImage& image,bool resolved) noexcept {
 // Residency belongs to the game thread's image (scene_surface samples it).
 if(image.scene_snapshot)return;
 if(image.sampled_generation!=image.generation){
  image.sampled_generation=image.generation;image.sampled_frames=0;
 }
 if(!image.sampled_frames||image.last_sample_frame!=native_frame_index_){
  image.last_sample_frame=native_frame_index_;
  if(image.sampled_frames<kSurfaceCpuGraceFrames)++image.sampled_frames;
 }
 if(resolved)image.sampled_frames=kSurfaceCpuGraceFrames;
 release_surface_pixels(image);
}

// ---- Scene thread: game-thread surface uploads and snapshots ---------------
// The game thread keeps owning every proxy SurfaceImage. Its uploads record
// on a deferred context; flush_game_uploads queues the list so the scene
// thread executes it at this point of the stream, after flushing any queued
// output blit that samples one of those surfaces (as upload does inline).
HRESULT Renderer::game_upload(SurfaceImage& i,SurfaceUploadResult* output){
 const frame_acct::Scope upload_scope(frame_acct::ours_surface_upload);
 if(output)*output={};
 // Core mode: the write goes to the host's surface table; the CPU pixels
 // stay (core_surface_write).
 if(core_mode_)return core_surface_write(i);
 if(!device_)return E_FAIL;
 if(i.cpu_released&&(i.uploaded_generation!=i.generation||
    !same_surface_upload_shape(i.uploaded_shape,surface_upload_shape(i)))){
  // The released bytes come back by a GPU readback: synchronous.
  scene_sync("surface_restore_for_upload");
  const HRESULT restored=restore_surface_pixels(i);if(FAILED(restored))return restored;
 }
 if(!game_upload_context_){const HRESULT created=device_->CreateDeferredContext(0,&game_upload_context_);if(FAILED(created))return created;}
 // A published (possibly shared) GPU home is never written in place.
 if(i.texture_shared&&i.texture&&(i.uploaded_generation!=i.generation||i.dirty_full||i.dirty_rect_valid)){
  const HRESULT detached=scene_detach_surface_texture(i,"upload");if(FAILED(detached))return detached;
 }
 ID3D11Texture2D* const shared_before=i.texture_shared?i.texture.Get():nullptr; // compared only, never dereferenced
 SurfaceUploadResult result{};
 const HRESULT hr=upload_surface_image(device_.Get(),game_upload_context_.Get(),i,&result);
 if(shared_before&&i.texture.Get()!=shared_before){i.texture_shared=false;scene_unshare_surface_texture(i.id,shared_before);} // recreated (mip layout)
 if(result.uploaded||FAILED(hr))update_surface_memory_accounting(i);
 if(SUCCEEDED(hr)&&result.uploaded){
  game_upload_pending_=true;game_upload_bytes_+=result.upload_bytes;
  try{game_upload_surfaces_.push_back(i.id);}catch(...){}
  release_uploaded_surface_pixels(i);
 }
 if(output)*output=result;
 return hr;
}

void Renderer::flush_game_uploads() noexcept{
 if(!game_upload_pending_)return;
 if(!game_upload_context_)return;
 const frame_acct::Scope upload_scope(frame_acct::ours_surface_upload);
 game_upload_pending_=false;
 ComPtr<ID3D11CommandList> list;
 if(FAILED(game_upload_context_->FinishCommandList(FALSE,&list))||!list){
  static unsigned reported=0;
  if(reported<4){++reported;bridge_report_event("render_thread_upload_failed","a surface upload list could not be finished; those textures keep older pixels");}
  game_upload_surfaces_.clear();game_upload_bytes_=0;return;
 }
 std::vector<std::uint64_t> surfaces;surfaces.swap(game_upload_surfaces_);
 const std::uint64_t bytes=game_upload_bytes_;game_upload_bytes_=0;
 const auto run=[this,list,surfaces=std::move(surfaces),bytes]{
  const bool queued=std::any_of(pending_blits_.begin(),pending_blits_.end(),[&](const PendingBlit& blit){
   return blit.gpu&&blit.source_is_surface&&std::find(surfaces.begin(),surfaces.end(),blit.source_id)!=surfaces.end();});
  if(queued)(void)render_pending_blits();
  context_->ExecuteCommandList(list.Get(),TRUE);
  profiler_.add_upload(bytes);
 };
 if(scene_thread_->post(run))return;
 // Allocation failed: run it here, in order, with the scene thread drained.
 {
 #ifdef EID3D11_EMBEDDED
 const CorePerf::AnyThreadScope coreWait(CorePerf::SceneSyncWait);
 #endif
 scene_thread_->sync();
 }
 run();
}

// Game thread, scene mode: the surface's current GPU home becomes visible to
// the scene thread's owned cache, which may share it from now on.
void Renderer::scene_publish_surface_texture(SurfaceImage& image) noexcept{
 if(!scene_queue()||!image.texture)return;
 try{std::lock_guard<std::mutex> guard(scene_surface_holds_lock_);scene_surface_holds_[image.id]=image.texture.Get();}catch(...){return;}
 image.texture_shared=true;
}

bool Renderer::scene_surface_holds(std::uint64_t id,ID3D11Texture2D* texture) const noexcept{
 std::lock_guard<std::mutex> guard(scene_surface_holds_lock_);
 const auto found=scene_surface_holds_.find(id);
 return found!=scene_surface_holds_.end()&&found->second==texture;
}

// Game thread, scene mode: detach_shared_surface_texture without the
// immediate context. The copy is recorded on the game-upload deferred
// context ahead of the write that follows it, so the scene thread executes
// both after every queued command that read the old texture.
HRESULT Renderer::scene_detach_surface_texture(SurfaceImage& image,const char* writer){
 if(!image.texture_shared||!image.texture)return S_OK;
 if(!device_)return E_FAIL;
 if(!game_upload_context_){const HRESULT created=device_->CreateDeferredContext(0,&game_upload_context_);if(FAILED(created))return created;}
 D3D11_TEXTURE2D_DESC description{};image.texture->GetDesc(&description);
 ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> srv;
 HRESULT hr=device_->CreateTexture2D(&description,nullptr,&texture);if(FAILED(hr))return hr;
 if(FAILED(hr=device_->CreateShaderResourceView(texture.Get(),nullptr,&srv)))return hr;
 game_upload_context_->CopyResource(texture.Get(),image.texture.Get());
 game_upload_pending_=true;
 const ComPtr<ID3D11Texture2D> previous=image.texture;
 image.texture=std::move(texture);image.srv=std::move(srv);++image.texture_serial;
 image.texture_shared=false;
 update_surface_memory_accounting(image);
 scene_unshare_surface_texture(image.id,previous.Get());
 // Evidence that rewrites of published world textures stay rare (#142).
 const auto count=++shared_texture_rewrites_;
 if(count<=32||!(count&(count-1))){
  char detail[192];std::snprintf(detail,sizeof(detail),"writer=%s id=%llu %ux%u format=%u count=%llu thread=game",writer,
   static_cast<unsigned long long>(image.id),description.Width,description.Height,unsigned(description.Format),
   static_cast<unsigned long long>(count));
  bridge_append_probe_event("shared_texture_rewrite",detail);
 }
 return S_OK;
}

// Game thread, scene mode: the surface left `texture`. New cache entries stop
// marking it shared at once; entries made by commands already queued are
// unmarked by a command behind them (queue order), so the cache owns, counts
// and may evict that texture alone afterwards.
void Renderer::scene_unshare_surface_texture(std::uint64_t id,ID3D11Texture2D* texture) noexcept{
 {std::lock_guard<std::mutex> guard(scene_surface_holds_lock_);
  const auto found=scene_surface_holds_.find(id);
  if(found!=scene_surface_holds_.end()&&found->second==texture)scene_surface_holds_.erase(found);}
 // The pointer is compared only: the texture may already be gone when this runs.
 const auto unshare=[this,id,texture]{
  std::lock_guard<std::mutex> guard(native_owned_textures_lock_);
  for(auto& entry:native_owned_textures_)
   if(entry.second.surface_shared&&entry.second.surface_id==id&&entry.second.texture.Get()==texture)
    entry.second.surface_shared=false;
 };
 if(!scene_queue()){unshare();return;}
 try{scene_post(unshare);}catch(...){}
}

// A pixel-free copy of a surface's current GPU home and description for one
// queued command (scene mode's game thread; the core-mode host's table).
std::shared_ptr<SurfaceImage> surface_snapshot(const SurfaceImage& image){
 std::shared_ptr<SurfaceImage> copy;
 try{copy=std::make_shared<SurfaceImage>();copy->bc_mips=image.bc_mips;}catch(...){return nullptr;}
 SurfaceImage& c=*copy;
 c.id=image.id;c.width=image.width;c.height=image.height;c.pitch=image.pitch;c.format=image.format;
 c.has_color_key=image.has_color_key;c.color_key=image.color_key;c.generation=image.generation;
 c.uploaded_generation=image.uploaded_generation;c.uploaded_shape=image.uploaded_shape;
 c.gpu_captured=image.gpu_captured;c.static_mip_hint=image.static_mip_hint;c.world_mip_use=image.world_mip_use;
 c.mip_source_used=image.mip_source_used;c.texture=image.texture;c.srv=image.srv;c.d3d12=image.d3d12;
 c.gpu_primary=image.gpu_primary;c.visual_source=image.visual_source;c.published_generation=image.published_generation;
 c.texture_serial=image.texture_serial;
 // No CPU bytes travel: every scene-thread reader takes the GPU path.
 c.cpu_released=true;c.dirty_full=false;c.dirty_rect_valid=false;c.scene_snapshot=true;
 return copy;
}

std::shared_ptr<SurfaceImage> Renderer::scene_surface(SurfaceImage* image){
 if(!image)return nullptr;
 if(!scene_queue())return std::shared_ptr<SurfaceImage>(image,[](SurfaceImage*){});
 // Core mode: the pending write goes to the host's surface table (there is
 // no GPU home in this process).
 if(core_mode_)(void)core_surface_write(*image);
 // A GPU-captured image's texture is its content; others upload pending CPU writes.
 else if(!image->gpu_captured&&(image->uploaded_generation!=image->generation||image->dirty_full||image->dirty_rect_valid||
    !image->texture||!same_surface_upload_shape(image->uploaded_shape,surface_upload_shape(*image)))){
  if(!image->pixels.empty()||image->cpu_released)(void)game_upload(*image,nullptr);
 }
 sampled_surface_pixels(*image);
 return surface_snapshot(*image);
}

HRESULT Renderer::restore_surface_pixels(SurfaceImage& image,bool pin_now){
 // Resident pixels: a pin request only marks the game thread's own image.
 // It touches no renderer state, so it must not drain the scene thread (the
 // cursor and HUD atlas pin on every frame change: 6% of frames waited a
 // whole scene frame here, frame accounting 2026-10-13). Only a readback of
 // released pixels uses the immediate context.
 if(!image.cpu_released){
  if(pin_now&&!image.cpu_pinned&&image.gpu_primary){image.cpu_pinned=true;g_pinned_surfaces.fetch_add(1,std::memory_order_relaxed);}
  return S_OK;
 }
 SCENE_ENTRY("restore_surface_pixels");
 if(!device_||!context_||!image.texture)return E_FAIL;
 LARGE_INTEGER start{};QueryPerformanceCounter(&start);
 D3D11_TEXTURE2D_DESC description{};image.texture->GetDesc(&description);
 const bool blocks=(image.format.dwFlags&DDPF_FOURCC)!=0;
 if(description.Width!=image.width||description.Height!=image.height||
    (!blocks&&description.Format!=DXGI_FORMAT_B8G8R8A8_UNORM))return E_NOTIMPL;
 description.MipLevels=1;description.ArraySize=1;description.Usage=D3D11_USAGE_STAGING;
 description.BindFlags=0;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;description.MiscFlags=0;
 ComPtr<ID3D11Texture2D> staging;HRESULT hr=device_->CreateTexture2D(&description,nullptr,&staging);if(FAILED(hr))return hr;
 context_->CopySubresourceRegion(staging.Get(),0,0,0,0,image.texture.Get(),0,nullptr);
 D3D11_MAPPED_SUBRESOURCE mapped{};hr=context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped);if(FAILED(hr))return hr;
 const UINT rows=blocks?std::max(1u,(image.height+3u)/4u):image.height;
 try{image.pixels.resize(static_cast<std::size_t>(image.pitch)*rows);}catch(const std::bad_alloc&){context_->Unmap(staging.Get(),0);return E_OUTOFMEMORY;}
 auto* destination=image.pixels.data();
 if(blocks){
  for(UINT y=0;y<rows;++y)std::memcpy(destination+static_cast<std::size_t>(y)*image.pitch,
   static_cast<const std::uint8_t*>(mapped.pData)+static_cast<std::size_t>(y)*mapped.RowPitch,static_cast<std::size_t>(image.pitch));
 }else{
  const UINT bytes=image.format.dwRGBBitCount/8;
  const PackedChannelTables tables(image.format);
  for(UINT y=0;y<image.height;++y){
   const auto* source=reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(mapped.pData)+static_cast<std::size_t>(y)*mapped.RowPitch);
   pack_bgra_row(destination+static_cast<std::size_t>(y)*image.pitch,source,image.width,bytes,tables);
  }
 }
 context_->Unmap(staging.Get(),0);
 staging.Reset(); // Release the readback resource/VA before accounting or logging.
 image.cpu_released=false;
 ++image.restore_readbacks;
 const bool repeated=image.restore_readbacks>1&&native_frame_index_-image.last_restore_frame<=kSurfaceRepeatReadFrames;
 image.last_restore_frame=native_frame_index_;
 if((pin_now||repeated)&&!image.cpu_pinned){image.cpu_pinned=true;g_pinned_surfaces.fetch_add(1,std::memory_order_relaxed);}
 update_surface_memory_accounting(image);
 LARGE_INTEGER end{};QueryPerformanceCounter(&end);
 g_restore_readbacks.fetch_add(1,std::memory_order_relaxed);
 g_restore_readback_qpc.fetch_add(static_cast<std::uint64_t>(end.QuadPart-start.QuadPart),std::memory_order_relaxed);
 // One line per restore so a hot reader is visible; pinning bounds it to two per surface.
 LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
 char detail[192];std::snprintf(detail,sizeof(detail),"id=%llu %ux%u fourcc=%08lx bpp=%lu count=%u pinned=%d elapsed_ms=%.3f",
  static_cast<unsigned long long>(image.id),image.width,image.height,image.format.dwFourCC,
  image.format.dwRGBBitCount,image.restore_readbacks,image.cpu_pinned?1:0,
  frequency.QuadPart?1000.0*double(end.QuadPart-start.QuadPart)/frequency.QuadPart:0.0);
 bridge_append_probe_event("surface_restore_readback",detail);
 return S_OK;
}

HRESULT Renderer::get_native_texture_array_slice(
    SurfaceImage& image, ComPtr<ID3D11ShaderResourceView>& output_srv, UINT& output_slice,
    const TerrainTexturePage* authored_page, UINT demand_slices) {
 ++native_frame_stats_.native_texture_array_attempts;
 output_srv.Reset();output_slice=0;if(!device_||!context_||!image.width||!image.height){++native_frame_stats_.native_texture_array_fallback_unavailable;return S_FALSE;}
  if(!image.cpu_released&&image.pixels.empty()){++native_frame_stats_.native_texture_array_fallback_unavailable;return S_FALSE;}
 bool compressed=false;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if((image.format.dwFlags&DDPF_FOURCC)!=0){
  if(image.format.dwFourCC==MAKEFOURCC('D','X','T','1'))format=DXGI_FORMAT_BC1_UNORM;
  else if(image.format.dwFourCC==MAKEFOURCC('D','X','T','2')||image.format.dwFourCC==MAKEFOURCC('D','X','T','3'))format=DXGI_FORMAT_BC2_UNORM;
  else if(image.format.dwFourCC==MAKEFOURCC('D','X','T','4')||image.format.dwFourCC==MAKEFOURCC('D','X','T','5'))format=DXGI_FORMAT_BC3_UNORM;
  else{++native_frame_stats_.native_texture_array_fallback_format;return S_FALSE;}
  compressed=true;
 }else if((image.format.dwFlags&DDPF_RGB)!=0)format=DXGI_FORMAT_B8G8R8A8_UNORM;
 else{++native_frame_stats_.native_texture_array_fallback_format;return S_FALSE;}
 const bool generate_mips=image.static_mip_hint&&image.world_mip_use&&!compressed&&!image.gpu_captured;
 const std::uint64_t source_id=image.id?image.id:static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&image));
 if(authored_page&&format==DXGI_FORMAT_BC1_UNORM&&!image.gpu_captured){
  // A resident generation has already passed the byte comparison. Do not
  // reread its authored payload on every draw after releasing the CPU copy.
  const NativeTextureArrayKey resident_key{image.width,image.height,format,authored_page->mip_count};
  const auto resident=native_texture_arrays_.find(resident_key);
  if(resident!=native_texture_arrays_.end())for(const auto& page:resident->second){
   const auto slot=page.slots.find(source_id);
   if(slot!=page.slots.end()&&slot->second.generation==image.generation){
    release_surface_pixels(image);
    output_srv=page.srv;output_slice=slot->second.slice;++native_frame_stats_.native_texture_array_successes;return S_OK;
   }
  }
  ResourceFailure failure;
  if(!authored_page->ensure_upload_bytes(failure)){
   OutputDebugStringA("EID3D11: could not reread the selected terrain mip chain; terrain slice upload failed");
   return E_FAIL;
  }
  // A proxy write may already have released this generation's CPU bytes.
  // Restore once for the exact BC1 comparison, using the counted readback
  // path; the successful upload below releases this temporary copy again.
  if(image.cpu_released){const HRESULT restored=restore_surface_pixels(image);if(FAILED(restored))return restored;}
 }
 const auto* authored_mips=authored_page&&!authored_page->mips.empty()?authored_page->mips.data():nullptr;
 const auto authored_mip_count=authored_page?authored_page->mip_count:0u;
 // Terrain pages are BC1 surfaces without a DirectDraw mip chain. The map
 // loader read their shipped chain from the same [RES] priority; it is used
 // only when its level 0 is exactly the game's surface, so the levels below
 // belong to the bytes being drawn. Otherwise the page keeps one level and
 // the terrain material stays off for it.
 const bool authored=format==DXGI_FORMAT_BC1_UNORM&&!generate_mips&&!image.gpu_captured&&authored_mips&&
  authored_bc1_chain_matches(authored_mips,authored_mip_count,image.width,image.height,image.pixels.data(),
   image.pixels.size(),image.pitch>0?static_cast<std::size_t>(image.pitch):0);
 if(format==DXGI_FORMAT_BC1_UNORM&&!authored&&!image.gpu_captured){static bool reported=false;if(!reported){reported=true;
  OutputDebugStringA(authored_mips?"EID3D11: a terrain page's shipped mip chain does not match its game surface; that page keeps one level and the terrain material stays off for it"
   :"EID3D11: a terrain page has no shipped mip chain from the map loader; it keeps one level and the terrain material stays off for it");}}
 const UINT mip_count=generate_mips?full_mip_count(image.width,image.height):(authored?authored_mip_count:1u);
 // The current proxy texture supplies level 0; authored BC1 levels are
 // uploaded from the loader. Other surfaces keep the GPU-copy path.
 if(image.gpu_primary){const HRESULT uploaded=upload(image);if(FAILED(uploaded))return uploaded;}
 const bool gpu_source=image.cpu_released;
 const UINT source_rows=compressed?std::max(1u,(image.height+3u)/4u):image.height;
 if(image.pitch<=0||(!gpu_source&&static_cast<std::uint64_t>(image.pitch)*source_rows>image.pixels.size())){++native_frame_stats_.native_texture_array_invalid_layout;return E_INVALIDARG;}
 const UINT source_bytes=std::max<UINT>(1,image.format.dwRGBBitCount/8);
 if(!compressed&&static_cast<std::uint64_t>(source_bytes)*image.width>static_cast<UINT>(image.pitch)){++native_frame_stats_.native_texture_array_invalid_layout;return E_INVALIDARG;}
 std::vector<std::uint32_t> converted;const void* upload_data=image.pixels.data();UINT upload_pitch=static_cast<UINT>(image.pitch);
 if(!compressed&&!gpu_source){
  try{converted.resize(static_cast<std::size_t>(image.width)*image.height);}catch(const std::bad_alloc&){++native_frame_stats_.native_texture_array_allocation_failures;return E_OUTOFMEMORY;}
  for(UINT y=0;y<image.height;++y)for(UINT x=0;x<image.width;++x){const auto* source=image.pixels.data()+static_cast<std::size_t>(y)*image.pitch+static_cast<std::size_t>(x)*source_bytes;std::uint32_t raw{};std::memcpy(&raw,source,std::min<UINT>(source_bytes,sizeof(raw)));const auto r=expand_channel(raw,image.format.dwRBitMask,0),g=expand_channel(raw,image.format.dwGBitMask,0),b=expand_channel(raw,image.format.dwBBitMask,0);auto a=expand_channel(raw,image.format.dwRGBAlphaBitMask,255);if(image.has_color_key&&raw>=image.color_key.dwColorSpaceLowValue&&raw<=image.color_key.dwColorSpaceHighValue)a=0;converted[static_cast<std::size_t>(y)*image.width+x]=(std::uint32_t(a)<<24)|(std::uint32_t(r)<<16)|(std::uint32_t(g)<<8)|b;}
  upload_data=converted.data();upload_pitch=image.width*4u;
 }
 const std::uint64_t row_bytes=compressed?static_cast<std::uint64_t>(std::max(1u,(image.width+3u)/4u))*(format==DXGI_FORMAT_BC1_UNORM?8u:16u):static_cast<std::uint64_t>(image.width)*4u;
 const std::uint64_t slice_bytes=row_bytes*source_rows;if(!slice_bytes||slice_bytes>UINT_MAX)return E_OUTOFMEMORY;
 // Demand-sized: the map's terrain material count, else doubling with the
 // slices this class already holds (a queued page SRV is never resized).
 const NativeTextureArrayKey key{image.width,image.height,format,mip_count};
 try{
  auto& pages=native_texture_arrays_[key];NativeTextureArrayPage* selected=nullptr;NativeTextureArraySlot* existing=nullptr;
  for(auto& page:pages){const auto found=page.slots.find(source_id);if(found!=page.slots.end()){selected=&page;existing=&found->second;break;}}
  if(!selected){for(auto& page:pages)if(page.next<page.capacity){selected=&page;break;}}
  if(!selected){
   NativeTextureArrayPage page{};HRESULT create_hr=E_OUTOFMEMORY;
   UINT held=0;for(const auto& other:pages)held+=other.capacity;
   const UINT preferred_capacity=(std::min)(UINT(D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION),demand_slices?demand_slices:(std::max)(1u,held));
   for(UINT capacity=preferred_capacity;capacity>=1;capacity/=2){D3D11_TEXTURE2D_DESC description{};description.Width=image.width;description.Height=image.height;description.MipLevels=mip_count;description.ArraySize=capacity;description.Format=format;description.SampleDesc.Count=1;description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;if(generate_mips){description.BindFlags|=D3D11_BIND_RENDER_TARGET;description.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;}if(FAILED(create_hr=device_->CreateTexture2D(&description,nullptr,&page.texture))){if(capacity==1)break;continue;}D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=format;srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;srv.Texture2DArray.MipLevels=mip_count;srv.Texture2DArray.ArraySize=capacity;if(SUCCEEDED(create_hr=device_->CreateShaderResourceView(page.texture.Get(),&srv,&page.srv))){page.capacity=capacity;break;}page.texture.Reset();if(capacity==1)break;}
   if(FAILED(create_hr)||!page.capacity||!page.srv){++native_frame_stats_.native_texture_array_allocation_failures;return FAILED(create_hr)?create_hr:E_OUTOFMEMORY;}
   pages.push_back(std::move(page));selected=&pages.back();
  }
  const UINT slice=existing?existing->slice:selected->next;
  if(!existing||existing->generation!=image.generation){
   if(gpu_source)context_->CopySubresourceRegion(selected->texture.Get(),D3D11CalcSubresource(0,slice,mip_count),0,0,0,image.texture.Get(),0,nullptr);
   else context_->UpdateSubresource(selected->texture.Get(),D3D11CalcSubresource(0,slice,mip_count),nullptr,upload_data,upload_pitch,0);
   for(UINT level=1;authored&&level<mip_count;++level){const UINT w=std::max(1u,image.width>>level);
    context_->UpdateSubresource(selected->texture.Get(),D3D11CalcSubresource(level,slice,mip_count),nullptr,
     authored_mips[level].data,std::max(1u,(w+3u)/4u)*8u,0);}
   if(generate_mips){D3D11_SHADER_RESOURCE_VIEW_DESC d{};d.Format=format;d.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;d.Texture2DArray.MipLevels=mip_count;d.Texture2DArray.FirstArraySlice=slice;d.Texture2DArray.ArraySize=1;ComPtr<ID3D11ShaderResourceView> slice_view;HRESULT hr=device_->CreateShaderResourceView(selected->texture.Get(),&d,&slice_view);if(FAILED(hr))return hr;context_->GenerateMips(slice_view.Get());}image.mip_source_used=true;profiler_.add_upload(slice_bytes);if(existing)existing->generation=image.generation;else{auto inserted=selected->slots.emplace(source_id,NativeTextureArraySlot{slice,image.generation});if(!inserted.second)return E_FAIL;++selected->used;++selected->next;}}
  if(authored)authored_page->release_upload_bytes();
  release_surface_pixels(image);
  output_srv=selected->srv;output_slice=slice;++native_frame_stats_.native_texture_array_successes;return S_OK;
 }catch(const std::bad_alloc&){++native_frame_stats_.native_texture_array_allocation_failures;return E_OUTOFMEMORY;}
}

std::uint64_t Renderer::native_owned_texture_bytes(bool arrays) const {
 std::uint64_t bytes=0;
 if(arrays){for(const auto& group:native_texture_arrays_)if(group.first.immutable_values)
  for(const auto& page:group.second)bytes+=texture_storage_bytes(page.texture.Get());}
 else{
  for(const auto& item:native_owned_textures_)if(!item.second.camp_surface_alias&&!item.second.surface_shared){ // shared: counted as surface bytes
   bytes+=texture_storage_bytes(item.second.texture.Get());
   UINT width=0,height=0,mips=0;DXGI_FORMAT format{};  // renderer_backend=d3d12: the executor's texture
   if(d3d12_&&item.second.d3d12&&d3d12_->texture_info(*item.second.d3d12,width,height,mips,format))
    bytes+=texture_storage_bytes(width,height,mips,format);
  }
  // Detached statics keep copies alive after their cache entry retires.
  std::vector<std::shared_ptr<ID3D11Texture2D>> retained;
  retained_visual_scene().visual_detached_surface_textures(retained);
  std::unordered_set<ID3D11Texture2D*> counted;
  for(const auto& item:native_owned_textures_)counted.insert(item.second.texture.Get());
  for(const auto& texture:retained)if(counted.insert(texture.get()).second)bytes+=texture_storage_bytes(texture.get());
 }
 return bytes;
}

std::shared_ptr<ID3D11Texture2D> Renderer::retain_detached_surface_texture(
 void* context,const OwnedVisualSurface& surface) noexcept{
 const auto& self=*static_cast<const Renderer*>(context);
 std::lock_guard<std::mutex> guard(self.native_owned_textures_lock_);
 const auto& textures=self.native_owned_textures_;
 try{
  const std::string key="surface:"+std::to_string(surface.id)+"|generation="+std::to_string(surface.generation);
  for(const char* mips:{"|mips=full","|mips=authored"}){
   const auto found=textures.find(key+mips);
   if(found==textures.end()||found->second.generation!=surface.generation||!found->second.texture)continue;
   ID3D11Texture2D* texture=found->second.texture.Get();texture->AddRef();
   return std::shared_ptr<ID3D11Texture2D>(texture,[](ID3D11Texture2D* value){value->Release();});
  }
 }catch(...){}
 return nullptr;
}

#ifdef EID3D11_TESTING
std::uint64_t Renderer::test_native_owned_texture_bytes(bool arrays) const {return native_owned_texture_bytes(arrays);}
#endif

std::uint64_t Renderer::native_shadow_fixed_bytes() const {
 return texture_storage_bytes(native_shadow_depth_.Get())+
  native_point_shadow_bytes()+
  ((native_shadow_constants_?sizeof(NativeShadowConstants):0u)+(native_point_shadow_constants_?sizeof(NativeShadowConstants):0u));
}

HRESULT Renderer::reserve_native_owned_texture_bytes(std::uint64_t bytes,bool arrays){
#ifdef EID3D11_TESTING
 const auto budget=arrays?test_native_shadow_budget_:test_native_owned_texture_budget_;
 if(bytes>budget)return E_OUTOFMEMORY;
 const auto used=native_owned_texture_bytes(arrays)+(arrays?native_shadow_fixed_bytes():0u);
 if(used>budget-bytes)return retire_native_owned_texture_bytes(used-(budget-bytes),arrays);
#else
 (void)bytes;(void)arrays;
#endif
 return S_OK;
}

HRESULT Renderer::retire_native_owned_texture_bytes(std::uint64_t bytes,bool arrays){
 // Production callers reach this only after an actual E_OUTOFMEMORY. Retire
 // at most the oldest unqueued entries needed for the failed allocation, then
 // retry that CreateTexture2D once. Source and shadow pools stay independent.
 const auto queued=[&](ID3D11ShaderResourceView* view){return std::any_of(native_state_buckets_.begin(),native_state_buckets_.end(),
  [&](const NativeStateBucket& bucket){return bucket.texture_srv.Get()==view;});};
 while(bytes){
  std::uint64_t retired=0;
  if(arrays){
   std::vector<NativeTextureArrayPage>* pages=nullptr;std::size_t victim=0;std::uint64_t age=UINT64_MAX;
   for(auto& group:native_texture_arrays_)if(group.first.immutable_values)
    for(std::size_t i=0;i<group.second.size();++i){const auto& page=group.second[i];
     if(page.last_used<=age&&!queued(page.srv.Get())){pages=&group.second;victim=i;age=page.last_used;}}
   if(!pages)return E_OUTOFMEMORY;
   retired=texture_storage_bytes((*pages)[victim].texture.Get());pages->erase(pages->begin()+victim);
  }else{
   // A copy a detached static still holds frees nothing when its entry retires.
   std::vector<std::shared_ptr<ID3D11Texture2D>> retained;
   retained_visual_scene().visual_detached_surface_textures(retained);
   const auto held=[&](ID3D11Texture2D* texture){return std::any_of(retained.begin(),retained.end(),
    [&](const std::shared_ptr<ID3D11Texture2D>& value){return value.get()==texture;});};
   auto victim=native_owned_textures_.end();std::uint64_t age=UINT64_MAX;
   for(auto i=native_owned_textures_.begin();i!=native_owned_textures_.end();++i)
    if(!i->second.camp_surface_alias&&!i->second.surface_shared&&i->second.last_used<=age&&!queued(i->second.srv.Get())&&!held(i->second.texture.Get())){victim=i;age=i->second.last_used;}
   if(victim==native_owned_textures_.end())return E_OUTOFMEMORY;
   retired=texture_storage_bytes(victim->second.texture.Get());
   drop_native_texture_array_slots(victim->second.backing,true);
   neighbour_texture_resident(victim->first,false);
   {std::lock_guard<std::mutex> guard(native_owned_textures_lock_);native_owned_textures_.erase(victim);}
  }
  bytes=bytes>retired?bytes-retired:0;
 }
 // Only cache references retire here. Already executed D3D commands retain
 // their resources under the immediate-context lifetime contract. Never flush
 // a pending batch to make room: that would turn pressure into caster draws.
 return S_OK;
}

bool native_texture_alpha_opaque(const NativeOwnedTextureView& texture,
                                std::uint32_t mip_filter) noexcept {
 if(!texture.identity || !texture.generation)return false;
 if(texture.surface)return texture.surface->id==texture.identity &&
  texture.surface->generation==texture.generation && texture.surface->alpha_fully_opaque;
 if(!texture.mip_count || texture.mip_count>32)return false;
 const std::uint32_t selected=mip_filter==D3DTFP_NONE?1u:
  texture.mip_count==32?UINT32_MAX:(std::uint32_t{1}<<texture.mip_count)-1;
 return (texture.opaque_mips&selected)==selected;
}

static bool native_alpha_is_one(const FixedState& state,NativeAlphaEvidence alpha) noexcept {
 const auto argument=[&](DWORD selector){
  if((selector&~0x3fu) || (selector&15)>D3DTA_SPECULAR)
   return std::numeric_limits<float>::quiet_NaN();
  float value=std::numeric_limits<float>::quiet_NaN();
  switch(selector&15){
  case D3DTA_DIFFUSE:case D3DTA_CURRENT:value=alpha.diffuse_alpha;break;
  case D3DTA_TEXTURE:if(alpha.texture_opaque)value=1;break;
  case D3DTA_TFACTOR:value=float(state.render[D3DRENDERSTATE_TEXTUREFACTOR]>>24)/255.f;break;
  }
  if(!std::isfinite(value) || value<0 || value>1)return std::numeric_limits<float>::quiet_NaN();
  // Alpha replication does not change alpha. SPECULAR/fog remains unknown.
  return (selector&D3DTA_COMPLEMENT)?1.f-value:value;
 };
 const auto& stage=state.stage[0];
 switch(stage[D3DTSS_ALPHAOP]){
 case D3DTOP_DISABLE:return alpha.diffuse_alpha==1;
 case D3DTOP_SELECTARG1:return argument(stage[D3DTSS_ALPHAARG1])==1;
 case D3DTOP_SELECTARG2:return argument(stage[D3DTSS_ALPHAARG2])==1;
 case D3DTOP_MODULATE:return argument(stage[D3DTSS_ALPHAARG1])==1 && argument(stage[D3DTSS_ALPHAARG2])==1;
 case D3DTOP_MODULATE2X:return 2.0*double(argument(stage[D3DTSS_ALPHAARG1]))*
  double(argument(stage[D3DTSS_ALPHAARG2]))>=1.0;
 case D3DTOP_ADD:return double(argument(stage[D3DTSS_ALPHAARG1]))+
  double(argument(stage[D3DTSS_ALPHAARG2]))>=1.0;
 default:return false;
 }
}

// Owned figure parts always carry the game's alpha test, also on textures
// without transparency. With a proved fragment alpha of 1 (every mip of the
// texture opaque, diffuse alpha 1) and a function that 1 passes for every
// reference (the shader's reference is at most 255/256), the test rejects
// nothing: dropping it keeps coverage exact.
bool native_alpha_test_passes_all(const FixedState& state,NativeAlphaEvidence alpha) noexcept {
 if(state.render[D3DRENDERSTATE_ALPHATESTENABLE]==FALSE)return false;
 switch(state.render[D3DRENDERSTATE_ALPHAFUNC]){
 case D3DCMP_GREATER:case D3DCMP_GREATEREQUAL:case D3DCMP_NOTEQUAL:case D3DCMP_ALWAYS:break;
 default:return false;
 }
 return native_alpha_is_one(state,alpha);
}

NativeOpacityClass native_opacity_class(const FixedState& state,
                                      float material_alpha,NativeAlphaEvidence alpha) noexcept {
 constexpr float material_opaque_threshold=254.5f/255.f;
 const bool blend=state.render[D3DRENDERSTATE_ALPHABLENDENABLE]!=FALSE;
 const bool overwrite=!blend || (state.render[D3DRENDERSTATE_SRCBLEND]==D3DBLEND_ONE &&
                                state.render[D3DRENDERSTATE_DESTBLEND]==D3DBLEND_ZERO);
 const bool standard_alpha=blend && state.render[D3DRENDERSTATE_SRCBLEND]==D3DBLEND_SRCALPHA &&
                                    state.render[D3DRENDERSTATE_DESTBLEND]==D3DBLEND_INVSRCALPHA;
 const bool test=state.render[D3DRENDERSTATE_ALPHATESTENABLE]!=FALSE;
 const bool owns_depth=state.render[D3DRENDERSTATE_ZENABLE]!=D3DZB_FALSE &&
                       state.render[D3DRENDERSTATE_ZWRITEENABLE]!=FALSE;
 // Preserve a real blend even when its scalar happens to be one (for
 // example, a texture with fractional alpha). Alpha facts are cached by
 // the immutable texture/surface loader, never inspected here per frame.
 if(alpha.ordered_blend && !overwrite)return NativeOpacityClass::ordered;
 const auto function=state.render[D3DRENDERSTATE_ALPHAFUNC];
 const auto reference=state.render[D3DRENDERSTATE_ALPHAREF]&255u;
 const bool alpha_one_passes=!test || function==D3DCMP_ALWAYS ||
  function==D3DCMP_GREATEREQUAL || (function==D3DCMP_GREATER && reference<255u);
 if(owns_depth && (overwrite || standard_alpha) && alpha_one_passes &&
    material_alpha>=material_opaque_threshold && native_alpha_is_one(state,alpha))
  return NativeOpacityClass::opaque;
 if(material_alpha>=material_opaque_threshold && owns_depth) {
  if(overwrite)return test ? NativeOpacityClass::cutout : NativeOpacityClass::opaque;
  if(standard_alpha && test)return NativeOpacityClass::cutout;
 }
 // Standard alpha with a proved fragment alpha1 has the same coverage as an
 // overwrite. Never infer it from the blend flag or an unused material scalar.
 if(standard_alpha && !test && owns_depth && material_alpha>=material_opaque_threshold && native_alpha_is_one(state,alpha))
  return NativeOpacityClass::opaque;
 return NativeOpacityClass::ordered;
}

NativeOwnedFixedState native_owned_fixed_state(const FixedState& state) noexcept {
 NativeOwnedFixedState result;result.enabled=true;
 result.cull_mode=state.render[D3DRENDERSTATE_CULLMODE];
 result.fill_mode=state.render[D3DRENDERSTATE_FILLMODE];
 result.z_enable=state.render[D3DRENDERSTATE_ZENABLE];
 result.z_write_enable=state.render[D3DRENDERSTATE_ZWRITEENABLE];
 result.z_func=state.render[D3DRENDERSTATE_ZFUNC];
 result.alpha_blend_enable=state.render[D3DRENDERSTATE_ALPHABLENDENABLE];
 result.source_blend=state.render[D3DRENDERSTATE_SRCBLEND];
 result.destination_blend=state.render[D3DRENDERSTATE_DESTBLEND];
 result.alpha_test_enable=state.render[D3DRENDERSTATE_ALPHATESTENABLE];
 result.alpha_func=state.render[D3DRENDERSTATE_ALPHAFUNC];
 result.alpha_ref=state.render[D3DRENDERSTATE_ALPHAREF];
 result.texture_factor=state.render[D3DRENDERSTATE_TEXTUREFACTOR];
 result.specular_enable=state.render[D3DRENDERSTATE_SPECULARENABLE];
 result.color_op=state.stage[0][D3DTSS_COLOROP];
 result.alpha_op=state.stage[0][D3DTSS_ALPHAOP];
 result.color_arg1=state.stage[0][D3DTSS_COLORARG1];
 result.color_arg2=state.stage[0][D3DTSS_COLORARG2];
 result.alpha_arg1=state.stage[0][D3DTSS_ALPHAARG1];
 result.alpha_arg2=state.stage[0][D3DTSS_ALPHAARG2];
 result.address_u=state.stage[0][D3DTSS_ADDRESSU];
 result.address_v=state.stage[0][D3DTSS_ADDRESSV];
 result.min_filter=state.stage[0][D3DTSS_MINFILTER];
 result.mag_filter=state.stage[0][D3DTSS_MAGFILTER];
 result.mip_filter=state.stage[0][D3DTSS_MIPFILTER];
 result.captured_viewport={state.viewport.dwX,state.viewport.dwY,state.viewport.dwWidth,state.viewport.dwHeight};
 result.captured_depth_range={state.viewport.dvMinZ,state.viewport.dvMaxZ};
 result.captured_fog_enable=state.render[D3DRENDERSTATE_FOGENABLE];
 result.captured_fog_color=state.render[D3DRENDERSTATE_FOGCOLOR];
 return result;
}

FixedState native_owned_submission_state(
    const FixedState& frame_state,
    const NativeOwnedFixedState& owned_state) noexcept {
 FixedState result{};
 result.viewport=frame_state.viewport;
 result.render[D3DRENDERSTATE_FOGENABLE]=
  frame_state.render[D3DRENDERSTATE_FOGENABLE];
 result.render[D3DRENDERSTATE_FOGCOLOR]=
  frame_state.render[D3DRENDERSTATE_FOGCOLOR];
 if(!owned_state.enabled)return result;
 if(owned_state.replay_frame_state){
  const auto& viewport=owned_state.captured_viewport;
  result.viewport={viewport[0],viewport[1],viewport[2],viewport[3],
   owned_state.captured_depth_range[0],owned_state.captured_depth_range[1]};
  result.render[D3DRENDERSTATE_FOGENABLE]=owned_state.captured_fog_enable;
  result.render[D3DRENDERSTATE_FOGCOLOR]=owned_state.captured_fog_color;
 }
 result.render[D3DRENDERSTATE_CULLMODE]=owned_state.cull_mode;
 result.render[D3DRENDERSTATE_FILLMODE]=owned_state.fill_mode;
 result.render[D3DRENDERSTATE_ZENABLE]=owned_state.z_enable;
 result.render[D3DRENDERSTATE_ZWRITEENABLE]=owned_state.z_write_enable;
 result.render[D3DRENDERSTATE_ZFUNC]=owned_state.z_func;
 result.render[D3DRENDERSTATE_ALPHABLENDENABLE]=owned_state.alpha_blend_enable;
 result.render[D3DRENDERSTATE_SRCBLEND]=owned_state.source_blend;
 result.render[D3DRENDERSTATE_DESTBLEND]=owned_state.destination_blend;
 result.render[D3DRENDERSTATE_ALPHATESTENABLE]=owned_state.alpha_test_enable;
 result.render[D3DRENDERSTATE_ALPHAFUNC]=owned_state.alpha_func;
 result.render[D3DRENDERSTATE_ALPHAREF]=owned_state.alpha_ref;
 result.render[D3DRENDERSTATE_TEXTUREFACTOR]=owned_state.texture_factor;
 result.render[D3DRENDERSTATE_SPECULARENABLE]=owned_state.specular_enable;
 result.stage[0][D3DTSS_COLOROP]=owned_state.color_op;
 result.stage[0][D3DTSS_ALPHAOP]=owned_state.alpha_op;
 result.stage[0][D3DTSS_COLORARG1]=owned_state.color_arg1;
 result.stage[0][D3DTSS_COLORARG2]=owned_state.color_arg2;
 result.stage[0][D3DTSS_ALPHAARG1]=owned_state.alpha_arg1;
 result.stage[0][D3DTSS_ALPHAARG2]=owned_state.alpha_arg2;
 result.stage[0][D3DTSS_ADDRESSU]=owned_state.address_u;
 result.stage[0][D3DTSS_ADDRESSV]=owned_state.address_v;
 result.stage[0][D3DTSS_MINFILTER]=owned_state.min_filter;
 result.stage[0][D3DTSS_MAGFILTER]=owned_state.mag_filter;
 result.stage[0][D3DTSS_MIPFILTER]=owned_state.mip_filter==UINT32_MAX ?
  frame_state.stage[0][D3DTSS_MIPFILTER] : owned_state.mip_filter;
 return result;
}

HRESULT Renderer::get_native_owned_texture(
 const NativeOwnedTextureView& source,
 ComPtr<ID3D11ShaderResourceView>& output,std::uint64_t& backing,NativeGeometryResult* result,bool world_mips,
 bool budget_reducible){
 const auto fail=[&](HRESULT hr,const char* operation){return native_geometry_failure(result,hr,operation,true);};
 backing=0;
 output.Reset();if(!device_||!source.identity||!source.generation||
  (!source.surface&&(!source.mip_count||source.mip_count>32||(!source.mips&&(source.value_source!=1||!source.resource)))))
  return fail(E_INVALIDARG,"texture.source");
 if(source.surface&&(source.surface->id!=source.identity ||
     source.surface->generation!=source.generation))return fail(E_INVALIDARG,"texture.surface_generation");
 if(source.camp_surface_alias&&(!source.surface||world_mips))return fail(E_INVALIDARG,"texture.camp_source");
 std::string key;
 try{
  if(source.surface)key="surface:"+std::to_string(source.surface->id);
  else if(source.value_source==1){
   if(!source.resource)return fail(E_INVALIDARG,"texture.archive_identity");
   // One file version's member decodes identically in every store, so the
   // active map and its neighbours (each with its own store) share one upload.
   key=native_owned_archive_texture_key(*source.resource,source.generation,false);
   key.resize(key.size()-std::strlen("|mips=authored")); // the mips suffix follows below
  }else key=std::to_string(source.value_source)+":"+std::to_string(source.identity)+
   "|generation="+std::to_string(source.generation);
 }catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.key_allocation");}
 // A neighbour piece texture the stream created arrives without mips
 // (neighbour_piece.h): a block-compressed archive member with its authored
 // chain, served only by its resident cache entry.
 if(!source.surface&&!source.mips){
  try{key+="|mips=authored";}catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.key_allocation");}
  const auto existing=native_owned_textures_.find(key);
  if(existing==native_owned_textures_.end()||existing->second.generation!=source.generation)
   return fail(E_FAIL,"texture.not_resident");
  auto& texture=existing->second;
  texture.last_used_frame=native_frame_index_;
  if(native_texture_share_floor_&&(texture.backing&~(1ull<<63))<native_texture_share_floor_&&
     texture.shared_floor!=native_texture_share_floor_){
   texture.shared_floor=native_texture_share_floor_;++native_texture_shared_;
   native_texture_shared_bytes_+=texture_storage_bytes(texture.texture.Get());
  }
  texture.last_used=native_queue_epoch_;output=texture.srv;backing=texture.backing;
  return output?S_OK:fail(E_FAIL,"texture.cached_srv");
 }
 const auto width=source.surface?source.surface->width:source.mips[0].width;
 const auto height=source.surface?source.surface->height:source.mips[0].height;
 const auto mip_count=source.surface?1u:source.mip_count;
 const DWORD source_fourcc=source.surface && (source.surface->format.dwFlags&DDPF_FOURCC) ?
  source.surface->format.dwFourCC : 0;
 if(!width||!height||width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||
    height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)return fail(E_INVALIDARG,"texture.dimensions");
 DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;UINT block_bytes=0,pixel_bytes=0;
 if(source.surface) {
  if(!surface_dxgi_format(source.surface->format,format,block_bytes))
   return fail(E_INVALIDARG,"texture.surface_format");
 }else switch(source.format){
 case NativeOwnedTextureFormat::dxt1:format=DXGI_FORMAT_BC1_UNORM;block_bytes=8;break;
 case NativeOwnedTextureFormat::dxt3:format=DXGI_FORMAT_BC2_UNORM;block_bytes=16;break;
 case NativeOwnedTextureFormat::dxt5:format=DXGI_FORMAT_BC3_UNORM;block_bytes=16;break;
 case NativeOwnedTextureFormat::bc5:format=DXGI_FORMAT_BC5_UNORM;block_bytes=16;break;
 case NativeOwnedTextureFormat::bc7:format=DXGI_FORMAT_BC7_UNORM;block_bytes=16;break;
 case NativeOwnedTextureFormat::bc7_srgb:format=DXGI_FORMAT_BC7_UNORM_SRGB;block_bytes=16;break;
 case NativeOwnedTextureFormat::rgb565:format=DXGI_FORMAT_B5G6R5_UNORM;pixel_bytes=2;break;
 case NativeOwnedTextureFormat::argb1555:format=DXGI_FORMAT_B5G5R5A1_UNORM;pixel_bytes=2;break;
 case NativeOwnedTextureFormat::argb4444:format=DXGI_FORMAT_B4G4R4A4_UNORM;pixel_bytes=2;break;
 case NativeOwnedTextureFormat::argb8888:format=DXGI_FORMAT_B8G8R8A8_UNORM;pixel_bytes=4;break;
 default:return fail(E_INVALIDARG,"texture.format");
 }
 const bool static_source=source.surface?source.surface->static_mip_hint:
  (source.value_source==1||source.value_source==3);
 const bool generate_mips=world_mips&&static_source&&mip_count==1&&!block_bytes;
 // Generated levels belong to this immutable source generation and domain.
 key+=generate_mips?"|mips=full":"|mips=authored";
 if(source.camp_surface_alias)key+="|camp";
 // VRAM budget (renderer_vram_budget.cpp): a world texture that carries a
 // mip chain may leave out its top levels while the local budget is short.
 const UINT full_levels=generate_mips?full_mip_count(width,height):mip_count;
 const UINT skip=budget_reducible&&world_mips&&!source.camp_surface_alias?
  vram_texture_skip(width,height,full_levels):0u;
 const auto existing=native_owned_textures_.find(key);
 if(existing!=native_owned_textures_.end()&&
    existing->second.generation==source.generation){
  auto& texture=existing->second;
  if(texture.format!=format||texture.source_fourcc!=source_fourcc||texture.width!=width||
     texture.height!=height||texture.mip_count!=mip_count)
   return fail(E_INVALIDARG,"texture.immutable_descriptor");
  texture.last_used_frame=native_frame_index_;
  // Built at another budget level: rebuild when this frame's rebuild work
  // allows (the old texture goes once its material bucket is rebound).
  if(texture.budget_skip==skip||texture.detached_copy||!vram_take_rebuild(texture_storage_bytes(texture.texture.Get()))){
  if(native_texture_share_floor_&&(texture.backing&~(1ull<<63))<native_texture_share_floor_&&
     texture.shared_floor!=native_texture_share_floor_){
   texture.shared_floor=native_texture_share_floor_;++native_texture_shared_;
   native_texture_shared_bytes_+=texture_storage_bytes(texture.texture.Get());
  }
  texture.last_used=native_queue_epoch_;output=texture.srv;backing=texture.backing;return output?S_OK:fail(E_FAIL,"texture.cached_srv");
  }
 }
 auto* color=active_color_resource_;
 ColorDiagnosticTicks texture_timing(color?&color->texture_ticks:nullptr,
  nullptr,color?&color_texture_active_:nullptr);
 if(color)++color->texture_cold_calls;
 try{
  std::vector<D3D11_SUBRESOURCE_DATA> subresources(mip_count);
  std::vector<std::uint32_t> converted;
  std::uint64_t uploaded=0;
  ComPtr<ID3D11Texture2D> surface_texture;
  SurfaceImage* surface_image=nullptr; // the live client surface, when resolved
  std::shared_ptr<ID3D11Texture2D> retained;
  if(source.surface){
   // A completed source carries no pixels: resolve its GPU home, then share
   // it (one texture per image, copy-on-write) or copy it when the layout
   // differs. A detached static's generation outlives its client surface.
   HRESULT resolved=S_OK;
   if(!source.camp_surface_alias)
    retained=retained_visual_scene().visual_detached_surface_texture(source.surface->id,source.surface->generation);
   if(retained) surface_texture=retained.get();
   else {
    if(!surface_texture_resolver_)return fail(E_FAIL,"texture.surface_resolver");
    resolved=surface_texture_resolver_(surface_texture_resolver_context_,device_.Get(),
     source.surface->id,source.surface->generation,surface_texture,surface_image);
   }
   if(FAILED(resolved)||!surface_texture)
    return fail(FAILED(resolved)?resolved:E_FAIL,"texture.surface_unavailable");
   D3D11_TEXTURE2D_DESC surface_description{};surface_texture->GetDesc(&surface_description);
   if(surface_description.Width!=width||surface_description.Height!=height||
      surface_description.Format!=format||surface_description.SampleDesc.Count!=1)
    return fail(E_INVALIDARG,"texture.surface_layout");
   uploaded=block_bytes?static_cast<std::uint64_t>((width+3u)/4u)*block_bytes*(std::max)(1u,(height+3u)/4u):
    static_cast<std::uint64_t>(width)*height*4u;
  }else for(UINT index=0;index<mip_count;++index){
   const NativeOwnedTextureMip& mip=source.mips[index];
   if(!mip.data||!mip.width||!mip.height||!mip.bytes||
      mip.width!=(std::max)(1u,width>>index)||
      mip.height!=(std::max)(1u,height>>index))return fail(E_INVALIDARG,"texture.mip_layout");
   const UINT row_pitch=block_bytes?
    std::max(1u,(mip.width+3u)/4u)*block_bytes:mip.width*pixel_bytes;
   const UINT rows=block_bytes?std::max(1u,(mip.height+3u)/4u):mip.height;
   const std::uint64_t required=static_cast<std::uint64_t>(row_pitch)*rows;
   if(required>mip.bytes)return fail(E_INVALIDARG,"texture.mip_bytes");
   subresources[index].pSysMem=mip.data;
   subresources[index].SysMemPitch=row_pitch;
   subresources[index].SysMemSlicePitch=static_cast<UINT>(required);
   uploaded+=required;
  }
  if(generate_mips&&!source.surface&&pixel_bytes==2){
   converted.resize(static_cast<std::size_t>(width)*height);
   const DWORD rm=source.format==NativeOwnedTextureFormat::rgb565?0xf800:
    source.format==NativeOwnedTextureFormat::argb1555?0x7c00:0x0f00;
   const DWORD gm=source.format==NativeOwnedTextureFormat::rgb565?0x07e0:
    source.format==NativeOwnedTextureFormat::argb1555?0x03e0:0x00f0;
   const DWORD bm=source.format==NativeOwnedTextureFormat::argb4444?0x000f:0x001f;
   const DWORD am=source.format==NativeOwnedTextureFormat::argb1555?0x8000:
    source.format==NativeOwnedTextureFormat::argb4444?0xf000:0;
   for(std::size_t i=0;i<converted.size();++i){std::uint16_t raw;
    std::memcpy(&raw,source.mips[0].data+i*2,2);
    converted[i]=(DWORD(expand_channel(raw,am,255))<<24)|(DWORD(expand_channel(raw,rm,0))<<16)|
     (DWORD(expand_channel(raw,gm,0))<<8)|expand_channel(raw,bm,0);
   }
   subresources[0]={converted.data(),width*4,static_cast<UINT>(converted.size()*4)};
  }
  D3D11_TEXTURE2D_DESC description{};
  description.Width=width;
  description.Height=height;
  description.MipLevels=generate_mips?full_mip_count(width,height):mip_count;
  description.ArraySize=1;
  description.Format=generate_mips?DXGI_FORMAT_B8G8R8A8_UNORM:format;
  description.SampleDesc.Count=1;
  const bool initial_data=!generate_mips&&!surface_texture;
  description.Usage=initial_data?D3D11_USAGE_IMMUTABLE:D3D11_USAGE_DEFAULT;
  description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  if(generate_mips){description.BindFlags|=D3D11_BIND_RENDER_TARGET;description.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;}
  NativeOwnedTexture value{};
  if(native_owned_texture_serial_>=((1ull<<63)-1))return fail(E_OUTOFMEMORY,"texture.serial");
  value.backing=(1ull<<63)|++native_owned_texture_serial_;
  value.generation=source.generation;value.format=format;value.source_fourcc=source_fourcc;
  value.surface_id=source.surface?source.surface->id:0;
  value.camp_surface_alias=source.camp_surface_alias;
  value.width=description.Width;value.height=description.Height;
  value.mip_count=mip_count;value.last_used=native_queue_epoch_;value.last_used_frame=native_frame_index_;
  // A retained copy built for this exact layout re-enters the cache as is:
  // copying it again would keep two live GPU copies of one generation.
  D3D11_TEXTURE2D_DESC retained_description{};
  if(retained)retained->GetDesc(&retained_description);
  const bool reuse_retained=retained&&retained_description.MipLevels==description.MipLevels&&
   retained_description.Format==description.Format&&retained_description.BindFlags==description.BindFlags&&
   retained_description.MiscFlags==description.MiscFlags;
  value.detached_copy=reuse_retained;
  HRESULT hr=S_OK;
  bool reuse_camp=false;
  if(source.camp_surface_alias){
   D3D11_TEXTURE2D_DESC current{};surface_texture->GetDesc(&current);
   reuse_camp=current.ArraySize==1&&current.MipLevels==description.MipLevels&&
    current.Format==description.Format&&(current.BindFlags&D3D11_BIND_SHADER_RESOURCE)&&
    !current.MiscFlags;
   if(!reuse_camp)return fail(E_INVALIDARG,"texture.camp_descriptor");
  }
  // One texture per image: a client surface whose texture has exactly this
  // entry's layout is the entry's texture. Its in-place writes go through
  // detach_shared_surface_texture first, so this generation keeps its pixels.
  // Scene thread: the resolver hands out only textures the game thread
  // published (never written in place again, scene_publish_surface_texture);
  // the entry is shared while the game thread still holds it, else it owns
  // the texture alone. Either way no copy is made.
  const bool scene_source=!surface_image&&surface_texture&&!retained&&on_scene_thread();
  bool share_surface=false;
  if(((surface_image&&surface_image->texture.Get()==surface_texture.Get())||scene_source)&&
     !retained&&!reuse_camp&&!skip&&!generate_mips){
   D3D11_TEXTURE2D_DESC current{};surface_texture->GetDesc(&current);
   share_surface=current.ArraySize==1&&current.MipLevels==description.MipLevels&&
    current.Format==description.Format&&current.SampleDesc.Count==1&&
    current.Usage==D3D11_USAGE_DEFAULT&&current.BindFlags==D3D11_BIND_SHADER_RESOURCE&&!current.MiscFlags;
  }
  if(reuse_retained||reuse_camp||share_surface){
   if(reuse_camp||share_surface)value.texture=surface_texture;
   else value.texture=retained.get();
   if(share_surface){
    if(surface_image){value.surface_shared=true;surface_image->texture_shared=true;}
    else value.surface_shared=scene_surface_holds(value.surface_id,surface_texture.Get());
   }
   uploaded=0;
   if(FAILED(hr=device_->CreateShaderResourceView(value.texture.Get(),nullptr,&value.srv)))
    return fail(hr,"texture.CreateShaderResourceView");
  }else if(skip){
   // VRAM budget copy without the top `skip` levels. Authored levels upload
   // from level `skip`; generated levels come from a transient full chain.
   D3D11_TEXTURE2D_DESC reduced=description;
   reduced.Width=(std::max)(1u,width>>skip);reduced.Height=(std::max)(1u,height>>skip);
   reduced.MipLevels=description.MipLevels-skip;
   reduced.BindFlags=D3D11_BIND_SHADER_RESOURCE;reduced.MiscFlags=0;
   ColorDiagnosticTicks create_timing(color?&color->texture_create_ticks:nullptr);
   if(initial_data)hr=device_->CreateTexture2D(&reduced,subresources.data()+skip,&value.texture);
   else{
    ComPtr<ID3D11Texture2D> full;ComPtr<ID3D11ShaderResourceView> full_srv;
    hr=device_->CreateTexture2D(&description,nullptr,&full);
    if(SUCCEEDED(hr))hr=device_->CreateShaderResourceView(full.Get(),nullptr,&full_srv);
    if(SUCCEEDED(hr))hr=device_->CreateTexture2D(&reduced,nullptr,&value.texture);
    if(SUCCEEDED(hr)){
     if(surface_texture)context_->CopySubresourceRegion(full.Get(),0,0,0,0,surface_texture.Get(),0,nullptr);
     else context_->UpdateSubresource(full.Get(),0,nullptr,subresources[0].pSysMem,subresources[0].SysMemPitch,0);
     if(generate_mips)context_->GenerateMips(full_srv.Get());
     for(UINT level=0;level<reduced.MipLevels;++level)
      context_->CopySubresourceRegion(value.texture.Get(),level,0,0,0,full.Get(),level+skip,nullptr);
    }
   }
   if(FAILED(hr))return fail(hr,"texture.budget_reduced_create");
   if(FAILED(hr=device_->CreateShaderResourceView(value.texture.Get(),nullptr,&value.srv)))
    return fail(hr,"texture.CreateShaderResourceView");
   value.budget_skip=skip;
   if(color)color->texture_upload_bytes+=uploaded;
  }else{
  const std::uint64_t allocation_bytes=generate_mips?uncompressed_mip_bytes(width,height,4):uploaded;
  hr=reserve_native_owned_texture_bytes(allocation_bytes,false);if(FAILED(hr))return fail(hr,"texture.budget");
  ColorDiagnosticTicks create_timing(color?&color->texture_create_ticks:nullptr);
  hr=device_->CreateTexture2D(&description,initial_data?subresources.data():nullptr,
                                      &value.texture);
  if(hr==E_OUTOFMEMORY && SUCCEEDED(retire_native_owned_texture_bytes(allocation_bytes,false)))
   hr=device_->CreateTexture2D(&description,initial_data?subresources.data():nullptr,&value.texture);
  if(FAILED(hr))return fail(hr,"texture.CreateTexture2D");
  if(color)color->texture_upload_bytes+=uploaded;
  hr=device_->CreateShaderResourceView(value.texture.Get(),nullptr,&value.srv);
  if(FAILED(hr))return fail(hr,"texture.CreateShaderResourceView");
  if(surface_texture)context_->CopySubresourceRegion(value.texture.Get(),0,0,0,0,surface_texture.Get(),0,nullptr);
  else if(generate_mips)context_->UpdateSubresource(value.texture.Get(),0,nullptr,subresources[0].pSysMem,subresources[0].SysMemPitch,0);
  if(generate_mips)context_->GenerateMips(value.srv.Get());
  }
  if(!reuse_camp&&!share_surface){++native_owned_texture_uploads_;profiler_.add_upload(uploaded);}
  if(source.value_source==1)neighbour_texture_resident(key,true);
  // A new generation replaces its key's entry: the old backing's array slots go.
  if(const auto replaced=native_owned_textures_.find(key);replaced!=native_owned_textures_.end())
   drop_native_texture_array_slots(replaced->second.backing,true);
  std::unique_lock<std::mutex> guard(native_owned_textures_lock_);
  auto inserted=native_owned_textures_.insert_or_assign(
      std::move(key),std::move(value));
  output=inserted.first->second.srv;
  backing=inserted.first->second.backing;
  guard.unlock();
  return output?S_OK:fail(E_FAIL,"texture.created_srv");
 }catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.cpu_allocation");}
}

void Renderer::restore_camp_clip() noexcept {
 if(!camp_unclipped_raster_)return;
 context_->RSSetState(camp_unclipped_raster_);
 context_->RSSetScissorRects(0,nullptr);
 camp_unclipped_raster_=nullptr;
}

} // namespace owned
