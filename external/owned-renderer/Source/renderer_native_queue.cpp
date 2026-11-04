// Renderer native queue (record building): owned textures, meshes, materials, poses, instance records, world ranges, queue_native*, checkpoints and discards.
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
#include "fur.h"
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
using ColorResourceDiagnostic = PlacedHitchRendererFrame::ColorResourceDiagnostic;
struct ColorDiagnosticActivation {
 ColorResourceDiagnostic*& slot;ColorResourceDiagnostic* previous;
 bool& mesh;bool previous_mesh;bool& texture;bool previous_texture;
 bool& cold;bool previous_cold;
 ColorDiagnosticActivation(ColorResourceDiagnostic*& target,bool& mesh_active,
  bool& texture_active,bool& cold_active,ColorResourceDiagnostic* value) noexcept
  :slot(target),previous(target),mesh(mesh_active),previous_mesh(mesh_active),
   texture(texture_active),previous_texture(texture_active),
   cold(cold_active),previous_cold(cold_active){
  slot=value;mesh=false;texture=false;cold=false;
 }
 ~ColorDiagnosticActivation() noexcept {
  slot=previous;mesh=previous_mesh;texture=previous_texture;cold=previous_cold;
 }
};
struct ColorDiagnosticResult {
 ColorResourceDiagnostic* value=nullptr;bool complete=false;
 explicit ColorDiagnosticResult(ColorResourceDiagnostic* diagnostic) noexcept:value(diagnostic){}
 ~ColorDiagnosticResult() noexcept {if(value&&!complete)++value->failed_calls;}
};
UINT align_up(UINT value,UINT alignment){return (value+alignment-1u)&~(alignment-1u);}
std::uint64_t hash_bytes(std::uint64_t hash,const void* bytes,std::size_t size){
 const auto* data=static_cast<const std::uint8_t*>(bytes);for(std::size_t index=0;index<size;++index){hash^=data[index];hash*=1099511628211ull;}return hash;
}
}  // namespace

static NativeAlphaEvidence native_alpha_evidence(const NativeGeometryView& view,
 const FixedState&,bool owned_texture_bound,const NativePreparedPartValues* values=nullptr) noexcept {
 NativeAlphaEvidence result;
 result.ordered_blend=(values?static_cast<NativeOrdering>(values->ordering):view.ordering)==NativeOrdering::preserve;
 if(owned_texture_bound)result.texture_opaque=native_texture_alpha_opaque(
  view.owned_texture,D3DTFP_LINEAR);
 // Terrain has per-vertex opacity. A default instance alpha is not its proof.
 if(view.semantic_kind!=SemanticKind::terrain){
  const auto flags=values?values->flags:view.flags;
  if(values?values->lighting_enabled:view.lighting.enabled)
   result.diffuse_alpha=values?values->alpha:view.lighting.material_alpha;
  else if(flags&4u)result.diffuse_alpha=float((values?values->default_diffuse:view.default_diffuse)>>24)/255.f;
 }
 return result;
}

static void native_opacity_state(FixedState& state,NativeOpacityClass opacity) noexcept {
 if(opacity==NativeOpacityClass::opaque){
  state.render[D3DRENDERSTATE_ALPHATESTENABLE]=FALSE;
  state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=FALSE;
 }
}

// The material state and opacity class of one draw. The game's alpha test on
// a part whose every fragment passes it (native_alpha_test_passes_all: opaque
// texture, opaque diffuse) is dropped, so the part is opaque: it joins the
// depth prepass and static LOD and casts without a pixel stage. A sun caster
// the game cuts out keeps its two-sided silhouette either way. material points
// at state, or at copy when the draw needs either change (copy may be state).
static NativeOpacityClass native_material_state(const FixedState& state,float material_alpha,
 NativeAlphaEvidence alpha,bool shadow,FixedState& copy,const FixedState*& material) noexcept {
 auto opacity=native_opacity_class(state,material_alpha,alpha);
 const bool untested=native_alpha_test_passes_all(state,alpha);
 const bool two_sided=shadow&&opacity==NativeOpacityClass::cutout;
 material=&state;
 if(!untested&&!two_sided&&opacity!=NativeOpacityClass::opaque)return opacity;
 if(&copy!=&state)copy=state;
 material=&copy;
 if(two_sided)copy.render[D3DRENDERSTATE_CULLMODE]=D3DCULL_NONE;
 if(untested){
  copy.render[D3DRENDERSTATE_ALPHATESTENABLE]=FALSE;
  copy.render[D3DRENDERSTATE_ALPHAFUNC]=D3DCMP_ALWAYS;copy.render[D3DRENDERSTATE_ALPHAREF]=0;
  opacity=native_opacity_class(copy,material_alpha,alpha);
 }
 native_opacity_state(copy,opacity);
 return opacity;
}

// Static LOD: opaque, effectively reorderable rigid records with reduced
// levels. Retained statics keep their prepared world vertices; cut-outs,
// units, swaying and animated parts keep their complete index list.
// Crown impostors: cut-out, reorderable object-space records with a baked
// impostor and a mesh that holds its bounding sphere.
bool native_record_takes_impostor(const NativeGeometryView& view,NativeOpacityClass opacity,bool sphere_block) noexcept{
 return view.world.impostor&&sphere_block&&view.world.object_space&&opacity==NativeOpacityClass::cutout&&
  view.ordering==NativeOrdering::reorderable;
}
bool native_record_takes_lod(const NativeGeometryView& view,NativeOpacityClass opacity,UINT lod_levels) noexcept{
 const bool reorderable=view.ordering==NativeOrdering::reorderable ||
  (view.ordering==NativeOrdering::derive_from_state &&
   (view.semantic_kind==SemanticKind::figure || view.semantic_kind==SemanticKind::terrain ||
    view.semantic_kind==SemanticKind::simple_geometry) && opacity!=NativeOpacityClass::ordered);
 return lod_levels&&view.world.enabled&&(view.world.object_space||view.world.static_lod)&&
  !view.morph_count&&opacity==NativeOpacityClass::opaque&&reorderable;
}

std::size_t Renderer::NativeTextureArrayKeyHash::operator()(
    const NativeTextureArrayKey& key) const noexcept {
 std::uint64_t hash=1469598103934665603ull;
 hash=hash_bytes(hash,&key.width,sizeof(key.width));
 hash=hash_bytes(hash,&key.height,sizeof(key.height));
 hash=hash_bytes(hash,&key.format,sizeof(key.format));
 hash=hash_bytes(hash,&key.mip_count,sizeof(key.mip_count));
 hash=hash_bytes(hash,&key.immutable_values,sizeof(key.immutable_values));
 return static_cast<std::size_t>(hash^(hash>>32));
}

std::size_t Renderer::NativeMeshKeyHash::operator()(const NativeMeshKey& key) const noexcept{
 std::uint64_t hash=hash_bytes(1469598103934665603ull,key.resource_key.data(),key.resource_key.size());auto mix=[&](std::uint64_t value){hash^=value+0x9e3779b97f4a7c15ull+(hash<<6)+(hash>>2);};const auto& selector=key.selector;mix(selector.cache_identity);mix(selector.object_token);mix(selector.figure_token);mix(selector.geometry_token);mix(selector.positions);mix(selector.normals);mix(selector.components);mix(selector.texture_coordinates);mix(selector.indices);mix(selector.content_fingerprint);mix(selector.content_keyed?1u:0u);mix(key.world_part);mix(key.static_lods?1u:0u);mix(key.shape_count);mix(key.shape_bits[0]);mix(key.shape_bits[1]);mix(key.shape_bits[2]);mix(key.position_groups);mix(key.normal_groups);mix(key.component_count);mix(key.texture_coordinate_count);mix(key.index_count);mix(key.decoded_channels);return static_cast<std::size_t>(hash^(hash>>32));
}

std::size_t Renderer::NativeMaterialKeyHash::operator()(const NativeMaterialKey& key) const noexcept{
 std::uint64_t hash=1469598103934665603ull;
 hash=hash_bytes(hash,key.state.data(),key.state.size()*sizeof(key.state[0]));
 hash=hash_bytes(hash,&key.shadow,sizeof(key.shadow));
 hash=hash_bytes(hash,&key.contact_map,sizeof(key.contact_map));
 hash=hash_bytes(hash,&key.texture_backing,sizeof(key.texture_backing));
 hash=hash_bytes(hash,&key.texture_generation,sizeof(key.texture_generation));
 return static_cast<std::size_t>(hash^(hash>>32));
}

HRESULT Renderer::retire_stale_native_geometry(std::uint64_t before){
 if(!device_||!context_||native_geometry_pages_.empty())
  return E_FAIL;
 if(!native_pending_draws_.empty())return E_UNEXPECTED;
 for(const auto& entry:native_meshes_)
  if(entry.second.pending_references)return E_UNEXPECTED;
 std::uint64_t older_owned=0;bool retired=false;
 for(auto iterator=native_meshes_.begin();iterator!=native_meshes_.end();){
  const auto& mesh=iterator->second;
  if(mesh.owned_references&&mesh.last_used_frame<native_cache_epoch_frame_)++older_owned;
  if(mesh.owned_references||mesh.last_used_frame>=before){++iterator;continue;}
  if(!release_native_geometry_span(iterator->second.allocation)){++iterator;continue;}
  iterator->second.retiring=true;retired=true;++iterator;
 }
 native_cache_older_owned_meshes_=older_owned;
 if(retired)erase_retiring_native_meshes();
 release_empty_native_geometry_pages();
 return S_OK;
}

// After a sweep (no draw pending): a general page left without a live span
// gives its buffer back and grows again on demand. Page 0 stays the general
// page with the arena's initial 16 bytes; a later page keeps its slot empty
// for the next new page (span page indices must stay stable). A page that
// still holds one live span keeps its size: spans are never moved.
void Renderer::release_empty_native_geometry_pages() noexcept{
 for(UINT index=0;index<native_geometry_pages_.size();++index){
  auto& page=native_geometry_pages_[index];
  if(page.dedicated||(!page.buffer&&!page.d3d12)||page.capacity<=16)continue;
  const bool empty=page.high_water<=16||(page.free.size()==1&&page.free[0].offset==16&&
   page.free[0].bytes==page.high_water-16u);
  if(!empty)continue;
  if(index){
   page.buffer.Reset();page.srv.Reset();page.d3d12.reset();page.capacity=0;
  }else if(d3d12_){
   D3D12BufferRef buffer;
   if(FAILED(d3d12_->create_buffer(16,0,0,buffer)))continue;
   // The executor defers the previous resource's release to its fence.
   page.d3d12=std::move(buffer);page.capacity=16;
  }else{
   ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11ShaderResourceView> srv;
   if(FAILED(create_native_geometry_resource(16,buffer,srv)))continue;
   page.buffer=std::move(buffer);page.srv=std::move(srv);page.capacity=16;
  }
  page.high_water=16;page.free.clear();
 }
}

void Renderer::adopt_prebuilt_pages(const NeighbourPiece& upload) noexcept try{
 if(upload.pages.empty()||native_geometry_pages_.empty())return;
 std::vector<UINT> slots(upload.pages.size(),UINT_MAX);
 for(std::size_t slot=0;slot<slots.size();++slot){
  UINT index=UINT_MAX;
  for(UINT i=1;i<native_geometry_pages_.size();++i)
   if(native_geometry_pages_[i].dedicated&&!native_geometry_pages_[i].buffer&&!native_geometry_pages_[i].d3d12){index=i;break;}
  if(index==UINT_MAX){native_geometry_pages_.emplace_back();index=static_cast<UINT>(native_geometry_pages_.size()-1);}
  const auto& source=upload.pages[slot];
  auto& page=native_geometry_pages_[index];
  page.buffer=source.buffer;page.srv=source.srv;page.capacity=source.bytes;page.high_water=source.bytes;
  if(d3d12_&&source.d3d12){ // D3D12 backend: the stream created the page; only its copy is recorded here
   if(FAILED(d3d12_->finish_buffer(*source.d3d12,0,0,page.d3d12))){page.d3d12.reset();page.capacity=0;}
  }
  page.free.clear();page.dedicated=true;page.live=0;slots[slot]=index;
 }
 const auto adopt=[&](const NativePrebuiltMesh& mesh){
  if(!mesh.bytes||mesh.page_slot>=slots.size())return;
  mesh.page=slots[mesh.page_slot];mesh.used=false;
  native_geometry_pages_[mesh.page].live+=mesh.bytes;native_geometry_live_bytes_+=mesh.bytes;
 };
 for(const auto& mesh:upload.meshes)adopt(mesh);
 native_mesh_resident_bytes_=native_geometry_live_bytes_;
 for(const UINT index:slots){ // a page no mesh lives on is released at once
  auto& page=native_geometry_pages_[index];
  if(!page.live){page.buffer.Reset();page.srv.Reset();page.d3d12.reset();page.capacity=0;page.high_water=16;}
 }
}catch(...){}

// Textures the stream created join the owned texture cache under their key,
// within the texture budget; one already resident (another neighbour, the
// active map) wins and the duplicate is dropped.
void Renderer::adopt_prebuilt_textures(const NeighbourPiece& upload) noexcept{
 for(const auto& made:upload.prebuilt_textures)try{
  if(d3d12_){ // D3D12 backend: the stream created the texture; only its copies are recorded here
   if(!made.d3d12)continue;
   const auto existing=native_owned_textures_.find(made.key);
   if(existing!=native_owned_textures_.end()&&existing->second.generation==made.generation&&existing->second.d3d12)continue;
   if(native_owned_texture_serial_>=((1ull<<63)-1))continue;
   D3D12TextureRef texture;
   if(FAILED(d3d12_->finish_texture(*made.d3d12,texture)))continue;
   NativeOwnedTexture value{};
   value.backing=(1ull<<63)|++native_owned_texture_serial_;
   value.generation=made.generation;value.format=made.format;value.width=made.width;value.height=made.height;
   value.mip_count=made.mip_count;value.last_used=native_queue_epoch_;value.last_used_frame=native_frame_index_;value.d3d12=texture;
   neighbour_texture_resident(made.key,true);
   {std::lock_guard<std::mutex> guard(native_owned_textures_lock_);native_owned_textures_.insert_or_assign(made.key,std::move(value));}
   ++native_owned_texture_uploads_;
   continue;
  }
  if(!made.texture||!made.srv)continue;
  const auto existing=native_owned_textures_.find(made.key);
  if(existing!=native_owned_textures_.end()&&existing->second.generation==made.generation)continue;
  if(native_owned_texture_serial_>=((1ull<<63)-1))return;
  if(FAILED(reserve_native_owned_texture_bytes(made.bytes,false)))continue; // get_native_owned_texture retries
  NativeOwnedTexture value{};
  value.backing=(1ull<<63)|++native_owned_texture_serial_;
  value.generation=made.generation;value.format=made.format;value.source_fourcc=0;
  value.width=made.width;value.height=made.height;value.mip_count=made.mip_count;
  value.last_used=native_queue_epoch_;value.last_used_frame=native_frame_index_;value.texture=made.texture;value.srv=made.srv;
  neighbour_texture_resident(made.key,true);
  {std::lock_guard<std::mutex> guard(native_owned_textures_lock_);native_owned_textures_.insert_or_assign(made.key,std::move(value));}
  ++native_owned_texture_uploads_;
 }catch(...){}
}

void Renderer::release_unused_prebuilt(const NeighbourPiece& upload) noexcept{
 const auto release=[&](const NativePrebuiltMesh& mesh){
  if(!mesh.bytes||mesh.used||mesh.page>=native_geometry_pages_.size())return;
  mesh.used=true;release_native_geometry_span({mesh.page,mesh.offset,mesh.bytes});
 };
 for(const auto& mesh:upload.meshes)release(mesh);
}

// Meshes on dedicated pages that nothing references any more (their
// neighbours were released) go now, not at the next arena pressure, so each
// page is freed with its last mesh. Same eviction rule as the stale trim.
// A sweep walks every cached mesh. Live neighbours' meshes are drawn every
// frame, so they always count as remaining: re-sweeping on that alone walked
// the whole mesh map after every Present for as long as any neighbour was
// loaded (~4% of the main thread in a pan sample). A release sweeps at once
// and for the next frames (what it frees stops being drawn within a frame);
// after that a mesh still held by a reference is retried every
// kDedicatedSweepFrames, so it goes at most that much later. The eviction
// rule itself is unchanged.
void Renderer::retire_dedicated_geometry() noexcept{
 bool remaining=false,retired=false;
 const auto page_count=native_geometry_pages_.size();
 for(auto iterator=native_meshes_.begin();iterator!=native_meshes_.end();++iterator){
  auto& mesh=iterator->second;const UINT page=mesh.allocation.page;
  if(page>=page_count||!native_geometry_pages_[page].dedicated)continue;
  if(mesh.owned_references||mesh.pending_references||mesh.last_used_frame==native_frame_index_){remaining=true;continue;}
  if(!release_native_geometry_span(mesh.allocation))continue;
  mesh.retiring=true;retired=true;
 }
 if(retired)erase_retiring_native_meshes();
 dedicated_geometry_remaining_=remaining;dedicated_geometry_sweep_frame_=native_frame_index_;
}

// After every Present. The cache holds the world's working set: a mesh or
// morph set that leaves the view stays resident and is drawn again without
// a decode or an upload. Unowned geometry goes with its world: two frames
// into a new world epoch everything unused since that epoch began retires,
// and while that sweep left older meshes owned, again whenever an expired
// owner hands back a prepared mesh. Within one world nothing retires for
// being out of view; only when the pages had to grow does geometry unused
// for kNativeCacheIdleFrames retire, so leftovers (a rebuilt appearance's
// old mesh) are reused before the pages take more memory. (The previous trim
// retired everything not drawn in the current frame whenever free page
// space was below the largest allocation one frame had ever made, the load
// included, which held on every frame once the pages were that full: the
// camera-motion churn of issue #57.)
HRESULT Renderer::trim_native_mesh_cache(){
 if(!native_pending_draws_.empty())return E_UNEXPECTED;
 // Budget reductions still operate on D3D11 textures and shadow pools.
 if(!d3d12_)update_vram_budget(); // every kPollFrames presents
 const std::uint64_t released=native_released_mesh_slots_;
 retire_native_source_values();
 trim_native_tables();
 if(native_world_reclaim_pending_)reclaim_native_world_ranges();
 if(dedicated_geometry_stale_){
  dedicated_geometry_stale_=false;dedicated_geometry_release_frame_=native_frame_index_;
  retire_dedicated_geometry();
 }else if(dedicated_geometry_remaining_&&
          (native_frame_index_-dedicated_geometry_release_frame_<kDedicatedFollowFrames||
           native_frame_index_-dedicated_geometry_sweep_frame_>=kDedicatedSweepFrames))
  retire_dedicated_geometry();
 const std::uint64_t epoch=bridge_current_world_epoch();
 if(epoch!=native_cache_world_epoch_){
  native_cache_world_epoch_=epoch;native_cache_epoch_frame_=native_frame_index_;
  native_cache_epoch_sweep_pending_=true;
  native_texture_epoch_queue_=native_queue_epoch_;native_texture_epoch_sweep_pending_=true;
  release_world_upload_peaks();
 }
 if(native_texture_epoch_sweep_pending_&&native_frame_index_-native_cache_epoch_frame_>=2){
  native_texture_epoch_sweep_pending_=false;retire_world_textures(native_texture_epoch_queue_);
 }
 if(!device_||!context_||native_geometry_pages_.empty())return S_FALSE;
 constexpr std::uint64_t kNativeCacheIdleFrames=1800;
 std::uint64_t before=0;
 if(native_cache_epoch_sweep_pending_&&native_frame_index_-native_cache_epoch_frame_>=2){
  native_cache_epoch_sweep_pending_=false;before=native_cache_epoch_frame_;
 }else if(!native_cache_epoch_sweep_pending_&&native_cache_older_owned_meshes_&&
          native_released_mesh_slots_!=released)before=native_cache_epoch_frame_;
 if(native_geometry_growths_!=native_cache_seen_growths_){
  native_cache_seen_growths_=native_geometry_growths_;
  if(native_frame_index_>kNativeCacheIdleFrames)
   before=(std::max)(before,native_frame_index_-kNativeCacheIdleFrames);
 }
 if(!before)return S_FALSE;
 const HRESULT hr=retire_stale_native_geometry(before);
 // A mesh still pending in some reference refuses the whole sweep; the
 // world's geometry must not then stay until the next page growth (the
 // menu never grows): sweep again on the next frame.
 if(hr==E_UNEXPECTED&&before==native_cache_epoch_frame_)native_cache_epoch_sweep_pending_=true;
 return hr;
}

void Renderer::trim_native_tables() noexcept try{
 constexpr std::uint64_t kStaleFrames=600; // the slot reuse rule (prepare/resolve)
 const auto stale=[&](std::uint64_t last_used){
  return native_frame_index_>last_used&&native_frame_index_-last_used>kStaleFrames;};
 const auto shrink=[](auto& table){
  if(table.capacity()>2*table.size()+1024)table.shrink_to_fit();};
 // Material slots first: retiring one releases its instance records.
 auto& materials=native_prepared_material_slots_;
 while(!materials.empty()){
  auto& slot=materials.back();
  if(slot.active&&(slot.persistent||!stale(slot.last_used_frame)))break;
  retire_native_material_instances(slot);
  native_prepared_material_generation_floor_=(std::max)(native_prepared_material_generation_floor_,slot.generation);
  materials.pop_back();
 }
 native_prepared_material_reuse_cursor_=(std::min)(native_prepared_material_reuse_cursor_,materials.size());
 shrink(materials);
 auto& meshes=native_prepared_mesh_slots_;
 while(!meshes.empty()){
  auto& slot=meshes.back();
  if(slot.active&&(slot.source_revision||!stale(slot.last_used_frame)))break;
  retire_native_world_range(slot);
  native_prepared_mesh_generation_floor_=(std::max)(native_prepared_mesh_generation_floor_,slot.generation);
  meshes.pop_back();
 }
 native_prepared_mesh_reuse_cursor_=(std::min)(native_prepared_mesh_reuse_cursor_,meshes.size());
 shrink(meshes);
 // Free records at the end of the table (the free list hands out the
 // lowest index first). Nothing references a free record; the static
 // shadow records bounds-check and revalidate theirs.
 auto& records=native_instance_records_;
 std::size_t keep=records.size();
 while(keep&&!records[keep-1].occupied)--keep;
 if(records.size()-keep>=256||(keep==0&&!records.empty())){
  records.erase(records.begin()+static_cast<std::ptrdiff_t>(keep),records.end());
  auto& free=native_instance_free_;
  free.erase(std::remove_if(free.begin(),free.end(),[keep](UINT index){return index>=keep;}),free.end());
  std::make_heap(free.begin(),free.end(),std::greater<UINT>());
 }
 if(records.capacity()>2*records.size()+1024){
  records.shrink_to_fit();
  // release_native_instance appends without allocating: the free list keeps
  // room for every record.
  std::vector<UINT> free;free.reserve(records.size()+1);
  free.assign(native_instance_free_.begin(),native_instance_free_.end());
  native_instance_free_.swap(free);
 }
}catch(...){}
namespace {

bool valid_prepared_identity(const NativePreparedIdentity& value) noexcept {
 return value.valid&&value.lifecycle_generation!=0&&value.world_epoch!=0&&
  value.plan_generation!=0&&value.part_index!=UINT_MAX&&
  value.set_index<2&&value.presentation_serial!=0;
}

bool same_prepared_identity(const NativePreparedIdentity& left,
                            const NativePreparedIdentity& right,
                            bool include_part) noexcept {
 return valid_prepared_identity(left)&&valid_prepared_identity(right)&&
  left.signed_id==right.signed_id&&
  left.terrain_source==right.terrain_source&&
  left.lifecycle_generation==right.lifecycle_generation&&
  left.world_epoch==right.world_epoch&&
  left.plan_generation==right.plan_generation&&
  left.set_index==right.set_index&&
  (!include_part||left.part_index==right.part_index);
}
}  // namespace

void Renderer::advance_native_queue_epoch() noexcept{
 if(++native_queue_epoch_==0){
  ++native_queue_epoch_;
  for(auto& slot:native_prepared_material_slots_)slot.queue_epoch=0;
  for(auto& group:native_state_buckets_)group.selection_epoch=0;
 }
}

// One pass over the slots for the whole batch: a map change retires thousands
// of meshes, and a slot scan per mesh cost ~1 s around the load reveal.
void Renderer::erase_retiring_native_meshes() noexcept{
 for(std::size_t index=0;index<native_prepared_mesh_slots_.size();++index){
  auto& slot=native_prepared_mesh_slots_[index];
  if(slot.active&&slot.mesh&&slot.mesh->retiring){
   if(slot.source_revision&&slot.mesh->owned_references)--slot.mesh->owned_references;
   slot.mesh=nullptr;slot.active=false;
   retire_native_world_range(slot);
   if(++slot.generation==0)++slot.generation;
   native_prepared_mesh_reuse_cursor_=
    std::min(native_prepared_mesh_reuse_cursor_,index);
  }
 }
 for(auto iterator=native_meshes_.begin();iterator!=native_meshes_.end();){
  if(!iterator->second.retiring){++iterator;continue;}
  iterator=native_meshes_.erase(iterator);++perf_native_mesh_evictions_;
 }
}

void Renderer::invalidate_native_prepared_handles() noexcept{
 native_static_content_changed();
 neighbour_client_stop();release_neighbour_maps();
 native_shadow_valid_.fill(false);
 native_world_reclaim_pending_|=native_world_vertex_used_!=0;
 for(auto& slot:native_prepared_mesh_slots_){
  if(slot.active&&slot.source_revision&&slot.mesh&&slot.mesh->owned_references)--slot.mesh->owned_references;
  slot.mesh=nullptr;slot.active=false;
  retire_native_world_range(slot);
  if(++slot.generation==0)++slot.generation;}
 for(auto& slot:native_prepared_material_slots_){retire_native_material_instances(slot);slot.active=false;
  if(++slot.generation==0)++slot.generation;}
 native_prepared_mesh_reuse_cursor_=0;
 native_prepared_material_reuse_cursor_=0;
 trim_native_material_groups();
 advance_native_queue_epoch();
}

#ifdef EID3D11_TESTING
void Renderer::test_invalidate_native_prepared_handles() noexcept{
 invalidate_native_prepared_handles();
}

bool Renderer::test_native_world_vertex(const NativePreparedMeshHandle& handle,UINT component,
 std::array<float,3>& position,std::array<float,3>& normal) noexcept{
 if(!device_||!context_||!native_world_vertices_||handle.slot>=native_prepared_mesh_slots_.size())return false;
 const auto& slot=native_prepared_mesh_slots_[handle.slot];
 if(!slot.active||slot.generation!=handle.generation||!slot.world_valid||component>=slot.world_count)return false;
 if(!native_world_states_||slot.world_allocation>=native_world_ranges_.size())return false;
 D3D11_BUFFER_DESC state_description{};state_description.ByteWidth=sizeof(NativeWorldStateGpu);
 state_description.Usage=D3D11_USAGE_STAGING;state_description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Buffer> state_staging;
 if(FAILED(device_->CreateBuffer(&state_description,nullptr,&state_staging)))return false;
 D3D11_BOX state_box{};state_box.left=slot.world_allocation*sizeof(NativeWorldStateGpu);
 state_box.right=state_box.left+sizeof(NativeWorldStateGpu);state_box.bottom=state_box.back=1;
 context_->CopySubresourceRegion(state_staging.Get(),0,0,0,0,native_world_states_.Get(),0,&state_box);
 D3D11_MAPPED_SUBRESOURCE state_mapped{};
 if(FAILED(context_->Map(state_staging.Get(),0,D3D11_MAP_READ,0,&state_mapped)))return false;
 NativeWorldStateGpu prepared{};std::memcpy(&prepared,state_mapped.pData,sizeof(prepared));
 context_->Unmap(state_staging.Get(),0);
 const auto generation=[](const std::array<UINT,2>& value){return std::uint64_t(value[0])|(std::uint64_t(value[1])<<32);};
 if(generation(prepared.geometry)!=native_world_ranges_[slot.world_allocation].generation)return false;
 const auto current_pose=slot.world_key.pose_slot<native_pose_slots_.size()?native_pose_slots_[slot.world_key.pose_slot].revision:0;
 if(generation(prepared.pose)!=current_pose)return false;
 D3D11_BUFFER_DESC description{};description.ByteWidth=sizeof(NativeWorldVertexGpu);
 description.Usage=D3D11_USAGE_STAGING;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Buffer> staging;
 if(FAILED(device_->CreateBuffer(&description,nullptr,&staging)))return false;
 D3D11_BOX box{};box.left=(slot.world_base+component)*sizeof(NativeWorldVertexGpu);
 box.right=box.left+sizeof(NativeWorldVertexGpu);box.bottom=box.back=1;
 context_->CopySubresourceRegion(staging.Get(),0,0,0,0,native_world_vertices_.Get(),0,&box);
 D3D11_MAPPED_SUBRESOURCE mapped{};
 if(FAILED(context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return false;
 NativeWorldVertexGpu value{};std::memcpy(&value,mapped.pData,sizeof(value));
 context_->Unmap(staging.Get(),0);
 std::copy_n(value.position,3,position.begin());std::copy_n(value.normal,3,normal.begin());
 return true;
}

std::uint64_t Renderer::test_native_world_prepared_vertices() const {
 if(!native_world_states_)return 0;
 D3D11_BUFFER_DESC description{};description.ByteWidth=native_world_state_capacity_*sizeof(NativeWorldStateGpu);
 description.Usage=D3D11_USAGE_STAGING;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Buffer> staging;
 if(FAILED(device_->CreateBuffer(&description,nullptr,&staging)))return UINT64_MAX;
 context_->CopyResource(staging.Get(),native_world_states_.Get());
 D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return UINT64_MAX;
 const auto* values=static_cast<const NativeWorldStateGpu*>(mapped.pData);std::uint64_t total=0;
 for(UINT index=0;index<native_world_state_capacity_;++index)total+=values[index].components;
 context_->Unmap(staging.Get(),0);return total;
}

bool Renderer::test_native_shadow_depth(UINT slice,UINT x,UINT y,float& depth) noexcept {
 if(!device_||!context_||!native_shadow_depth_||slice>=5||x>=native_shadow_side_||y>=native_shadow_side_)return false;
 D3D11_TEXTURE2D_DESC description{};native_shadow_depth_->GetDesc(&description);
 description.ArraySize=1;description.Usage=D3D11_USAGE_STAGING;description.BindFlags=0;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> staging;if(FAILED(device_->CreateTexture2D(&description,nullptr,&staging)))return false;
 context_->CopySubresourceRegion(staging.Get(),0,0,0,0,native_shadow_depth_.Get(),slice,nullptr);
 D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return false;
 std::memcpy(&depth,static_cast<const std::uint8_t*>(mapped.pData)+std::size_t(y)*mapped.RowPitch+x*4,4);
 context_->Unmap(staging.Get(),0);return std::isfinite(depth);
}

std::uint64_t Renderer::test_native_prepared_texture_generation(
 const NativePreparedMaterialHandle& handle) const{
 if(handle.slot>=native_prepared_material_slots_.size())return 0;
 const auto& slot=native_prepared_material_slots_[handle.slot];
 return slot.active&&slot.generation==handle.generation
  ?slot.material_key.texture_generation:0;
}

bool Renderer::test_native_terrain_phase_word(UINT offset,UINT& value) noexcept{
 value=0;if(!context_||!native_animation_||offset%4||std::uint64_t(offset)+4>native_animation_used_)return false;
 D3D11_BUFFER_DESC description{};description.ByteWidth=4;description.Usage=D3D11_USAGE_STAGING;
 description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Buffer> staging;
 if(FAILED(device_->CreateBuffer(&description,nullptr,&staging)))return false;
 D3D11_BOX box{};box.left=offset;box.right=offset+4;box.bottom=box.back=1;
 context_->CopySubresourceRegion(staging.Get(),0,0,0,0,native_animation_.Get(),0,&box);
 D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return false;
 std::memcpy(&value,mapped.pData,4);context_->Unmap(staging.Get(),0);return true;
}
#endif

HRESULT Renderer::get_native_prepared_mesh(const NativeGeometryView& view,
                                            NativeMesh*& output){
 output=nullptr;
 if(!view.prepared_mesh||!valid_prepared_identity(view.prepared_identity)||
    !view.geometry_revision_valid)return E_INVALIDARG;
 auto& handle=*view.prepared_mesh;
 const auto needs_late_color=[&](const NativeMesh* mesh){
  return mesh&&((view.diffuse&&mesh->diffuse_offset==UINT_MAX)||
                (view.specular&&mesh->specular_offset==UINT_MAX));
 };
 if(view.world.record_revision&&handle.slot<native_prepared_mesh_slots_.size()){
  auto& slot=native_prepared_mesh_slots_[handle.slot];
  if(slot.active&&slot.mesh&&slot.generation==handle.generation&&
     slot.source_revision==view.world.record_revision&&
     same_prepared_identity(slot.identity,view.prepared_identity,true)&&
     !needs_late_color(slot.mesh)){
   slot.last_used_frame=native_frame_index_;slot.mesh->last_used_frame=native_frame_index_;
   output=slot.mesh;++native_prepared_mesh_hits_;return S_OK;
  }
 }
 const std::string resource_key=view.world.enabled&&view.world.identity?
  view.world.identity->key():std::string{};
 const UINT shape_count=view.world.enabled?view.world.shape_count:1u;
 if(handle.slot<native_prepared_mesh_slots_.size()){
  auto& slot=native_prepared_mesh_slots_[handle.slot];
  if(slot.active&&slot.generation==handle.generation&&slot.mesh&&
     slot.resource_key==resource_key&&slot.world_part==view.world.part&&slot.shape_count==shape_count&&
     same_prepared_identity(slot.identity,view.prepared_identity,true)&&
     slot.geometry_revision_valid&&slot.geometry_revision==view.geometry_revision&&
     slot.mesh->geometry_revision_valid&&slot.mesh->geometry_revision==view.geometry_revision&&
     slot.color_revision_valid==view.color_revision_valid&&
     (!view.color_revision_valid||slot.color_revision==view.color_revision)&&
     slot.mesh->color_revision_valid==view.color_revision_valid&&
     (!view.color_revision_valid||
      slot.mesh->color_revision==view.color_revision)&&
     slot.position_groups==view.position_groups&&slot.normal_groups==view.normal_groups&&
     slot.component_count==view.component_count&&
     slot.texture_coordinate_count==view.texture_coordinate_count&&
     slot.index_count==view.index_count&&slot.decoded_channels==view.decoded_channels&&
     !needs_late_color(slot.mesh)){
   if(view.world.record_revision){
    if(!slot.source_revision)++slot.mesh->owned_references;
    slot.source_revision=view.world.record_revision;
    slot.owner=view.world.owner?*view.world.owner:std::weak_ptr<const void>{};
   }
   slot.last_used_frame=native_frame_index_;slot.mesh->last_used_frame=native_frame_index_;
   output=slot.mesh;++native_prepared_mesh_hits_;return S_OK;
  }
 }
 HRESULT hr=get_native_mesh(view,output);if(FAILED(hr)||!output)return FAILED(hr)?hr:E_FAIL;
 std::uint32_t slot_index=UINT_MAX;
 if(handle.slot<native_prepared_mesh_slots_.size()){
  const auto& previous=native_prepared_mesh_slots_[handle.slot];
  if(!previous.active||
     (previous.generation==handle.generation&&
      same_prepared_identity(previous.identity,
                             view.prepared_identity,true)))
   slot_index=handle.slot;
 }
 if(slot_index==UINT_MAX){
  if(native_frame_index_>native_prepared_mesh_reuse_sweep_frame_&&
     native_frame_index_-native_prepared_mesh_reuse_sweep_frame_>600){
   native_prepared_mesh_reuse_cursor_=0;
   native_prepared_mesh_reuse_sweep_frame_=native_frame_index_;
  }
  while(native_prepared_mesh_reuse_cursor_<native_prepared_mesh_slots_.size()){
   const auto index=native_prepared_mesh_reuse_cursor_++;
   const auto& candidate=native_prepared_mesh_slots_[index];
#ifdef EID3D11_TESTING
   ++test_native_prepared_mesh_slot_visits_;
#endif
   if(!candidate.active||
      (!candidate.source_revision&&native_frame_index_>candidate.last_used_frame&&
       native_frame_index_-candidate.last_used_frame>600)){
    slot_index=static_cast<UINT>(index);break;
   }
  }
 }
 if(slot_index==UINT_MAX){
  if(native_prepared_mesh_slots_.size()>=UINT_MAX||
     native_prepared_mesh_slots_.size()>=native_prepared_mesh_slots_.max_size())
   return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
  try{native_prepared_mesh_slots_.push_back({});
   native_prepared_mesh_slots_.back().generation=native_prepared_mesh_generation_floor_;
   slot_index=static_cast<std::uint32_t>(native_prepared_mesh_slots_.size()-1);}
  catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  native_prepared_mesh_reuse_cursor_=native_prepared_mesh_slots_.size();
 }
 auto& slot=native_prepared_mesh_slots_[slot_index];
 if(slot.active&&slot.source_revision&&slot.mesh&&slot.mesh->owned_references)--slot.mesh->owned_references;
 if(++slot.generation==0)++slot.generation;
 retire_native_world_range(slot);
 slot.mesh=output;slot.identity=view.prepared_identity;
 slot.source_revision=view.world.record_revision;
 if(slot.source_revision)++output->owned_references;
 slot.owner=view.world.owner?*view.world.owner:std::weak_ptr<const void>{};
 slot.resource_key=resource_key;slot.world_part=view.world.part;slot.shape_count=shape_count;
 slot.geometry_revision=view.geometry_revision;slot.geometry_revision_valid=true;
 slot.color_revision=view.color_revision;
 slot.color_revision_valid=view.color_revision_valid;
 slot.position_groups=view.position_groups;slot.normal_groups=view.normal_groups;
 slot.component_count=view.component_count;
 slot.texture_coordinate_count=view.texture_coordinate_count;
 slot.index_count=view.index_count;slot.decoded_channels=view.decoded_channels;
 slot.last_used_frame=native_frame_index_;slot.active=true;
 handle.slot=slot_index;handle.generation=slot.generation;
 return S_OK;
}

void Renderer::refresh_native_mesh_records(NativeMesh& mesh) noexcept{
 for(auto& record:native_instance_records_)if(record.occupied&&record.command.mesh==&mesh){
  auto& draw=record.command;draw.geometry_page=mesh.allocation.page;
  draw.instance.geometry_offsets={mesh.position_offset,mesh.normal_offset,
   mesh.texture_coordinate_offset,mesh.component_offset};
  draw.instance.color_offsets={mesh.diffuse_offset,mesh.specular_offset};
  draw.instance.terrain_vertex_offset=mesh.terrain_vertex_offset;
  record.descriptor.component_offset=mesh.component_offset;
  record.descriptor.index_offset=mesh.index_offset;record.descriptor.steep_first_triangle=mesh.steep_first_triangle;
  if(!mesh.lod_levels)draw.instance.world_geometry&=~native_world_lod;
  record.uploaded=false;record.descriptor_uploaded=false;
 }
 native_layout_changed();native_static_content_changed();
}

HRESULT Renderer::get_native_mesh(const NativeGeometryView& view,NativeMesh*& output){
 output=nullptr;
 if(!device_||native_geometry_pages_.empty()||!view.position_groups||!view.normal_groups||
    !view.component_count||!view.texture_coordinate_count||!view.index_count)
  return E_INVALIDARG;
 if(view.position_groups>UINT_MAX/4u||view.normal_groups>UINT_MAX/4u)
  return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 const std::uint32_t position_count=view.position_groups*4u;
 const std::uint32_t normal_count=view.normal_groups*4u;
 const UINT shape_count=view.world.enabled?view.world.shape_count:1u;
 if(shape_count!=1&&shape_count!=8)return E_INVALIDARG;
 if(view.world.enabled&&(!view.world.identity||!view.geometry_revision_valid||
    (view.decode&&shape_count!=1)))return E_INVALIDARG;
 const std::uint64_t position_bytes=(std::uint64_t)view.position_groups*sizeof(NativePackedGroup)*shape_count;
 const std::uint64_t normal_bytes=(std::uint64_t)view.normal_groups*sizeof(NativePackedGroup);
 const std::uint64_t component_bytes=(std::uint64_t)view.component_count*sizeof(NativeGpuComponent);
 const std::uint64_t texture_bytes=(std::uint64_t)view.texture_coordinate_count*sizeof(NativeTextureCoordinate);
 const std::uint64_t color_bytes=(std::uint64_t)normal_count*sizeof(std::uint32_t);
 const bool has_terrain=(view.decoded_channels&native_decoded_terrain)!=0;
 const std::uint64_t terrain_bytes=has_terrain?
  (std::uint64_t)view.component_count*sizeof(NativeTerrainVertex):0;
 const std::uint64_t index_bytes=(std::uint64_t)view.index_count*sizeof(std::uint32_t);
 if(position_bytes>kNativeRawBufferLimit||normal_bytes>kNativeRawBufferLimit||
    component_bytes>kNativeRawBufferLimit||texture_bytes>kNativeRawBufferLimit||
    color_bytes>kNativeRawBufferLimit||terrain_bytes>kNativeRawBufferLimit||
    index_bytes>kNativeRawBufferLimit)
  return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 NativeMeshContentKey content{};
 if(!view.geometry_revision_valid){content=native_mesh_content_key(view);
  if(!content.valid)return E_INVALIDARG;}
 NativeMeshKey key{};key.selector=native_mesh_cache_selector(view,content);
 key.shape_count=shape_count;
 key.static_lods=view.lod_figure!=nullptr || view.lods!=nullptr;
 if(view.world.enabled){key.resource_key=view.world.identity->key();key.world_part=view.world.part;key.selector={};}
 // An object-space record's shape-resolved mesh is shared by every record
 // of that figure with exactly this shape.
 if(view.world.enabled&&shape_count==1&&view.world.object_space)
  std::memcpy(key.shape_bits.data(),view.world.shape.data(),sizeof(key.shape_bits));
 key.position_groups=view.position_groups;
 key.normal_groups=view.normal_groups;key.component_count=view.component_count;
 key.texture_coordinate_count=view.texture_coordinate_count;key.index_count=view.index_count;
 key.decoded_channels=view.decoded_channels;
 auto found=native_meshes_.find(key);
 bool revision_changed=false;
 if(found!=native_meshes_.end())revision_changed=native_mesh_revision_requires_refresh(
  true,found->second.geometry_revision_valid,found->second.geometry_revision,view);
 const bool color_revision_changed=found!=native_meshes_.end()&&
  native_mesh_color_revision_requires_refresh(
   true,revision_changed,found->second.color_revision_valid,
   found->second.color_revision,view);
 const bool late_color=found!=native_meshes_.end()&&
  ((view.diffuse&&found->second.diffuse_offset==UINT_MAX)||
   (view.specular&&found->second.specular_offset==UINT_MAX));
 if(found!=native_meshes_.end()&&!revision_changed&&!color_revision_changed&&!late_color){
  auto& mesh=found->second;mesh.last_used_frame=native_frame_index_;output=&mesh;
  ++perf_native_mesh_hits_;return S_OK;
 }

 auto* color=active_color_resource_;
 ColorDiagnosticTicks mesh_timing(color?&color->mesh_ticks:nullptr,
  nullptr,color?&color_mesh_active_:nullptr);
 if(color)++color->mesh_cold_calls;

 if(found!=native_meshes_.end()&&color_revision_changed){
  auto& mesh=found->second;
  const bool wants_diffuse=(view.decoded_channels&native_decoded_diffuse)!=0;
  const bool wants_specular=(view.decoded_channels&native_decoded_specular)!=0;
  if((wants_diffuse&&mesh.diffuse_storage_offset==UINT_MAX)||
     (wants_specular&&mesh.specular_storage_offset==UINT_MAX))return E_INVALIDARG;
  std::vector<std::uint32_t> decoded_diffuse,decoded_specular;
  const std::uint32_t* diffuse=view.diffuse;
  const std::uint32_t* specular=view.specular;
  if(view.decode_colors){
   try{
    if(wants_diffuse)decoded_diffuse.resize(normal_count);
    if(wants_specular)decoded_specular.resize(normal_count);
   }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
   NativeGeometryColorDecodeTarget target{};
   target.diffuse=decoded_diffuse.empty()?nullptr:decoded_diffuse.data();
   target.specular=decoded_specular.empty()?nullptr:decoded_specular.data();
   target.color_count=normal_count;
   if(!view.decode_colors(view.decode_context,target))return E_FAIL;
   diffuse=target.diffuse;specular=target.specular;
  }
  if((wants_diffuse&&!diffuse)||(wants_specular&&!specular))return E_INVALIDARG;
  HRESULT hr=S_OK;
  if(mesh.pending_references&&FAILED(hr=flush_native()))return hr;
  mesh.color_revision_valid=false;
  if(wants_diffuse&&FAILED(hr=upload_native_geometry(mesh.allocation.page,
       mesh.diffuse_storage_offset,diffuse,static_cast<UINT>(color_bytes))))return hr;
#ifdef EID3D11_TESTING
  if(wants_specular&&test_fail_native_specular_color_upload_){
   test_fail_native_specular_color_upload_=false;return E_FAIL;
  }
#endif
  if(wants_specular&&FAILED(hr=upload_native_geometry(mesh.allocation.page,
       mesh.specular_storage_offset,specular,static_cast<UINT>(color_bytes))))return hr;
  // Publish only after both authoritative channels reached the GPU. A failed
  // channel leaves resident color state invalid, forcing a complete refresh
  // before any old or new revision can be accepted.
  bool offsets_changed=false;
  if(wants_diffuse&&mesh.diffuse_offset==UINT_MAX){mesh.diffuse_offset=mesh.diffuse_storage_offset;offsets_changed=true;}
  if(wants_specular&&mesh.specular_offset==UINT_MAX){mesh.specular_offset=mesh.specular_storage_offset;offsets_changed=true;}
  if(offsets_changed)refresh_native_mesh_records(mesh);
  mesh.color_revision=view.color_revision;
  mesh.color_revision_valid=true;
  if(view.semantic_kind==SemanticKind::terrain){
   ++native_frame_stats_.native_terrain_color_only_uploads;
   native_frame_stats_.native_terrain_color_only_upload_vertices+=
    view.component_count;
  }
  mesh.last_used_frame=native_frame_index_;output=&mesh;
  ++perf_native_mesh_hits_;return S_OK;
 }

 if(found==native_meshes_.end()&&view.prebuilt_mesh){
  const auto& prebuilt=*view.prebuilt_mesh;
  if(!prebuilt.used&&prebuilt.page<native_geometry_pages_.size()&&native_geometry_pages_[prebuilt.page].dedicated&&
     prebuilt.position_groups==view.position_groups&&prebuilt.normal_groups==view.normal_groups&&
     prebuilt.component_count==view.component_count&&prebuilt.texture_coordinate_count==view.texture_coordinate_count&&
     prebuilt.index_count==view.index_count&&prebuilt.decoded_channels==view.decoded_channels&&
     !view.diffuse&&!view.specular&&prebuilt.shape_count==shape_count){
   NativeMesh mesh{};
   mesh.allocation={prebuilt.page,prebuilt.offset,prebuilt.bytes};
   mesh.position_offset=prebuilt.position_offset;mesh.normal_offset=prebuilt.normal_offset;
   mesh.component_offset=prebuilt.component_offset;mesh.texture_coordinate_offset=prebuilt.texture_coordinate_offset;
   mesh.diffuse_storage_offset=prebuilt.diffuse_storage_offset;mesh.specular_storage_offset=prebuilt.specular_storage_offset;
   mesh.terrain_vertex_offset=prebuilt.terrain_vertex_offset==UINT32_MAX?UINT_MAX:prebuilt.terrain_vertex_offset;
   mesh.index_offset=prebuilt.index_offset;mesh.lod_levels=prebuilt.lod_levels;mesh.lod_extra_triangles=prebuilt.lod_extra_triangles;
   mesh.sphere_block=prebuilt.sphere_block;
   mesh.has_liquid=prebuilt.has_liquid;mesh.has_ground=prebuilt.has_ground;mesh.liquid_types=prebuilt.liquid_types;
   mesh.steep_first_triangle=prebuilt.steep_first_triangle==UINT32_MAX?UINT_MAX:prebuilt.steep_first_triangle;
   std::copy(std::begin(prebuilt.bounds_min),std::end(prebuilt.bounds_min),mesh.bounds_min);
   std::copy(std::begin(prebuilt.bounds_max),std::end(prebuilt.bounds_max),mesh.bounds_max);
   mesh.geometry_revision=view.geometry_revision;mesh.geometry_revision_valid=view.geometry_revision_valid;
   mesh.color_revision=view.color_revision;mesh.color_revision_valid=view.color_revision_valid;
   mesh.bytes=prebuilt.bytes;mesh.last_used_frame=native_frame_index_;
   try{auto inserted=native_meshes_.emplace(key,std::move(mesh));
    if(inserted.second){prebuilt.used=true;output=&inserted.first->second;++perf_native_mesh_misses_;
     if(view.world.enabled){++perf_native_mesh_world_misses_;
      frame_acct::count_world_mesh(view.semantic_kind==SemanticKind::terrain,view.contribution.known&&!view.contribution.color,true,view.world.part);}
     return S_OK;}
   }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  }
 }
 NativeGeometryView decoded=view;
 std::vector<NativePackedGroup> decoded_positions,decoded_normals;
 std::vector<NativeTextureCoordinate> decoded_texture_coordinates;
 std::vector<NativeVertexComponent> decoded_components;
 std::vector<std::uint16_t> decoded_indices;
 std::vector<std::uint32_t> decoded_diffuse,decoded_specular;
 std::vector<NativeTerrainVertex> decoded_terrain;
 std::uint32_t steep_index_begin=UINT32_MAX;
 if(view.decode){
  try{
   decoded_positions.resize(view.position_groups);decoded_normals.resize(view.normal_groups);
   decoded_texture_coordinates.resize(view.texture_coordinate_count);
   decoded_components.resize(view.component_count);decoded_indices.resize(view.index_count);
    if(view.decoded_channels&native_decoded_diffuse)decoded_diffuse.resize(normal_count);
    if(view.decoded_channels&native_decoded_specular)decoded_specular.resize(normal_count);
    if(has_terrain)decoded_terrain.resize(view.component_count);
  }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  NativeGeometryDecodeTarget target{};
  target.positions=decoded_positions.data();target.position_groups=view.position_groups;
  target.normals=decoded_normals.data();target.normal_groups=view.normal_groups;
  target.diffuse=decoded_diffuse.empty()?nullptr:decoded_diffuse.data();
  target.specular=decoded_specular.empty()?nullptr:decoded_specular.data();
  target.texture_coordinates=decoded_texture_coordinates.data();
  target.texture_coordinate_count=view.texture_coordinate_count;
   target.components=decoded_components.data();target.component_count=view.component_count;
   target.indices=decoded_indices.data();target.index_count=view.index_count;
   target.terrain_vertices=decoded_terrain.empty()?nullptr:decoded_terrain.data();
   target.terrain_vertex_count=static_cast<std::uint32_t>(decoded_terrain.size());
  if(!view.decode(view.decode_context,target))return E_FAIL;
  steep_index_begin=target.steep_index_begin;
  decoded.positions=decoded_positions.data();decoded.normals=decoded_normals.data();
  decoded.diffuse=target.diffuse;decoded.specular=target.specular;
   decoded.texture_coordinates=decoded_texture_coordinates.data();
   decoded.components=decoded_components.data();decoded.indices=decoded_indices.data();
   decoded.terrain_vertices=target.terrain_vertices;
   decoded.terrain_vertex_count=target.terrain_vertex_count;
 }
 const NativeGeometryView& source=decoded;
 if(!source.positions||!source.normals||!source.components||
     !source.texture_coordinates||!source.indices)return E_INVALIDARG;
 if(has_terrain&&(!source.terrain_vertices||
                  source.terrain_vertex_count!=source.component_count))return E_INVALIDARG;
 auto build_topology=[&](std::vector<NativeGpuComponent>& components,
                         std::vector<std::uint32_t>& indices){
  try{components.resize(source.component_count);indices.resize(source.index_count);}
  catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  for(std::uint32_t index=0;index<source.component_count;++index){
   const auto& component=source.components[index];
   if(component.position>=position_count||component.normal>=normal_count||
      component.texture>=source.texture_coordinate_count)return E_INVALIDARG;
   components[index]={component.position,component.normal,component.texture,
    source.component_parts?source.component_parts[index]:0u};
  }
  for(std::uint32_t index=0;index<source.index_count;++index){
   if(source.indices[index]>=source.component_count)return E_INVALIDARG;
   indices[index]=source.indices[index];
  }
  return S_OK;
 };
 auto update_bounds=[&](NativeMesh& mesh){
  mesh.steep_first_triangle=steep_index_begin<=source.index_count?steep_index_begin/3u:UINT_MAX;
  mesh.has_liquid=false;mesh.has_ground=source.terrain_vertex_count==0;mesh.liquid_types=0;
  for(UINT i=0;i<source.terrain_vertex_count;++i){
   const bool liquid=(source.terrain_vertices[i].flags&native_terrain_water_vertex)!=0;
   mesh.has_liquid|=liquid;mesh.has_ground|=!liquid;
   if(liquid)mesh.liquid_types|=native_liquid_vertex_types(source.terrain_vertices[i]);
  }
  mesh.bounds_min[0]=mesh.bounds_min[1]=mesh.bounds_min[2]=std::numeric_limits<float>::max();
  mesh.bounds_max[0]=mesh.bounds_max[1]=mesh.bounds_max[2]=-std::numeric_limits<float>::max();
  for(std::uint32_t index=0;index<position_count;++index){
   const auto& group=source.positions[index>>2];const auto lane=index&3u;
   const float value[3]={group.x[lane],group.y[lane],group.z[lane]};
   for(UINT axis=0;axis<3;++axis)if(std::isfinite(value[axis])){
    mesh.bounds_min[axis]=std::min(mesh.bounds_min[axis],value[axis]);
    mesh.bounds_max[axis]=std::max(mesh.bounds_max[axis],value[axis]);
   }
  }
  for(UINT axis=0;axis<3;++axis)if(mesh.bounds_min[axis]>mesh.bounds_max[axis])
   mesh.bounds_min[axis]=mesh.bounds_max[axis]=0;
 };
 // A changed source with a LOD table needs a fresh contiguous layout:
 // reduced level sizes can change even when the full mesh counts do not.
 if(found!=native_meshes_.end()&&!(revision_changed&&key.static_lods)){
  auto& mesh=found->second;
  if(revision_changed){
   std::vector<NativeGpuComponent> components;std::vector<std::uint32_t> indices;
   HRESULT hr=build_topology(components,indices);if(FAILED(hr))return hr;
   if(mesh.pending_references&&FAILED(hr=flush_native()))return hr;
   native_static_content_changed(); // liquid/ground parts of static terrain bakes
   if(FAILED(hr=upload_native_geometry(mesh.allocation.page,mesh.position_offset,source.positions,
                                       static_cast<UINT>(position_bytes))))return hr;
   if(FAILED(hr=upload_native_geometry(mesh.allocation.page,mesh.normal_offset,source.normals,
                                       static_cast<UINT>(normal_bytes))))return hr;
   if(FAILED(hr=upload_native_geometry(mesh.allocation.page,mesh.component_offset,components.data(),
                                       static_cast<UINT>(component_bytes))))return hr;
   if(FAILED(hr=upload_native_geometry(mesh.allocation.page,mesh.texture_coordinate_offset,source.texture_coordinates,
                                       static_cast<UINT>(texture_bytes))))return hr;
   if(FAILED(hr=upload_native_geometry(mesh.allocation.page,mesh.index_offset,indices.data(),
                                       static_cast<UINT>(index_bytes))))return hr;
   if(source.diffuse&&FAILED(hr=upload_native_geometry(mesh.allocation.page,
      mesh.diffuse_storage_offset,source.diffuse,
                                       static_cast<UINT>(color_bytes))))return hr;
    if(source.specular&&FAILED(hr=upload_native_geometry(mesh.allocation.page,
       mesh.specular_storage_offset,source.specular,
                                        static_cast<UINT>(color_bytes))))return hr;
    if(has_terrain&&mesh.terrain_vertex_offset!=UINT_MAX&&
       FAILED(hr=upload_native_geometry(mesh.allocation.page,mesh.terrain_vertex_offset,source.terrain_vertices,
                                        static_cast<UINT>(terrain_bytes))))return hr;
   bool offsets_changed=false;
   if(source.diffuse&&mesh.diffuse_offset==UINT_MAX){mesh.diffuse_offset=mesh.diffuse_storage_offset;offsets_changed=true;}
   if(source.specular&&mesh.specular_offset==UINT_MAX){mesh.specular_offset=mesh.specular_storage_offset;offsets_changed=true;}
   if(offsets_changed)refresh_native_mesh_records(mesh);
   update_bounds(mesh);mesh.geometry_revision=source.geometry_revision;
   mesh.geometry_revision_valid=true;
   mesh.color_revision=source.color_revision;
   mesh.color_revision_valid=source.color_revision_valid;
   if(source.semantic_kind==SemanticKind::terrain){
    ++native_frame_stats_.native_terrain_topology_uploads;
    native_frame_stats_.native_terrain_topology_upload_vertices+=
     source.component_count;
   }
  }
  const bool publish_diffuse=source.diffuse&&mesh.diffuse_offset==UINT_MAX;
  const bool publish_specular=source.specular&&mesh.specular_offset==UINT_MAX;
  if((publish_diffuse||publish_specular)&&mesh.pending_references){
   const HRESULT flush_hr=flush_native();if(FAILED(flush_hr))return flush_hr;
  }
  auto upload_color=[&](const std::uint32_t* values,UINT storage,bool publish){
   if(!publish)return S_OK;
   if(storage==UINT_MAX)return E_FAIL;
   return upload_native_geometry(mesh.allocation.page,storage,values,
                                 static_cast<UINT>(color_bytes));
  };
  const auto old_offsets=std::array<UINT,2>{mesh.diffuse_offset,mesh.specular_offset};
  HRESULT color_hr=upload_color(source.diffuse,mesh.diffuse_storage_offset,publish_diffuse);
  if(SUCCEEDED(color_hr))color_hr=upload_color(
   source.specular,mesh.specular_storage_offset,publish_specular);
  if(FAILED(color_hr))return color_hr;
  if(publish_diffuse)mesh.diffuse_offset=mesh.diffuse_storage_offset;
  if(publish_specular)mesh.specular_offset=mesh.specular_storage_offset;
  if(old_offsets!=std::array<UINT,2>{mesh.diffuse_offset,mesh.specular_offset})
   refresh_native_mesh_records(mesh);
  mesh.last_used_frame=native_frame_index_;output=&mesh;++perf_native_mesh_hits_;
  return S_OK;
 }
 const frame_acct::Scope build_scope(frame_acct::mesh_build);
 std::vector<NativeGpuComponent> components;std::vector<std::uint32_t> indices;
 HRESULT hr=build_topology(components,indices);if(FAILED(hr))return hr;
 NativeMeshLods generated_lods;
 const NativeMeshLods* lods=view.lods;
 if(view.lod_figure){build_static_lods(*view.lod_figure,generated_lods);lods=&generated_lods;}
 if(lods&&!lods->levels)lods=nullptr;
 if(lods){
  if(lods->levels>kStaticLodMaxLevels||has_terrain)return E_INVALIDARG;
  for(UINT level=0;level<lods->levels;++level){
   if(lods->indices[level].empty()||lods->indices[level].size()%3||
      !std::isfinite(lods->error[level])||lods->error[level]<0)return E_INVALIDARG;
   for(const auto index:lods->indices[level])if(index>=source.component_count)return E_INVALIDARG;
  }
 }
 NativeMesh mesh{};UINT cursor=0,lod_table_at=0;
 std::array<UINT,kStaticLodMaxLevels> lod_at{};
 auto reserve=[&](std::uint64_t bytes,UINT& offset){
  if(!align_up_checked(cursor,16,cursor)||cursor>native_geometry_page_limit()||
   bytes>native_geometry_page_limit()-cursor)return false;
  offset=cursor;cursor+=static_cast<UINT>(bytes);return true;
 };
 if(!reserve(position_bytes,mesh.position_offset)||!reserve(normal_bytes,mesh.normal_offset)||
    !reserve(component_bytes,mesh.component_offset)||
    !reserve(texture_bytes,mesh.texture_coordinate_offset)||
     !reserve(color_bytes,mesh.diffuse_storage_offset)||
     !reserve(color_bytes,mesh.specular_storage_offset)||
     (has_terrain&&!reserve(terrain_bytes,mesh.terrain_vertex_offset))||
     (lods&&!reserve(64,lod_table_at))||!reserve(index_bytes,mesh.index_offset))
  return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 for(UINT level=0;lods&&level<lods->levels;++level)
  if(!reserve(std::uint64_t(lods->indices[level].size())*sizeof(std::uint32_t),lod_at[level]))
   return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 if(!align_up_checked(cursor,16,cursor))return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 if(FAILED(hr=allocate_native_geometry_span(cursor,mesh.allocation)))return hr;
 frame_acct::add(frame_acct::mesh_uploads,1);frame_acct::add(frame_acct::mesh_upload_bytes,cursor);
 const auto abandon=[&](HRESULT result){release_native_geometry_span(mesh.allocation);return result;};
 // One contiguous span: assemble its streams on the CPU and upload them with
 // one UpdateSubresource instead of up to eight (frame accounting 2026-10-13:
 // a 325-mesh shadow-depth burst spent 4.4 ms in mesh_upload, ~13 us per mesh
 // in eight small calls). Unused colour storage is zero, as never-read bytes.
 {
  std::vector<std::uint8_t> blob;
  try{blob.assign(cursor,0);}catch(const std::bad_alloc&){return abandon(E_OUTOFMEMORY);}
  const auto put=[&](UINT offset,const void* data,std::uint64_t bytes){if(data&&bytes)std::memcpy(blob.data()+offset,data,static_cast<std::size_t>(bytes));};
  put(mesh.position_offset,source.positions,position_bytes);
  put(mesh.normal_offset,source.normals,normal_bytes);
  put(mesh.component_offset,components.data(),component_bytes);
  put(mesh.texture_coordinate_offset,source.texture_coordinates,texture_bytes);
  if(source.diffuse)put(mesh.diffuse_storage_offset,source.diffuse,color_bytes);
  if(source.specular)put(mesh.specular_storage_offset,source.specular,color_bytes);
  if(has_terrain)put(mesh.terrain_vertex_offset,source.terrain_vertices,terrain_bytes);
  put(mesh.index_offset,indices.data(),index_bytes);
  if(lods){
   auto* table=reinterpret_cast<UINT*>(blob.data()+lod_table_at);
   table[0]=lods->levels;
   for(UINT level=0;level<lods->levels;++level){
    const auto& list=lods->indices[level];
    put(lod_at[level],list.data(),list.size()*sizeof(std::uint32_t));
    auto* entry=table+4*(level+1);
    entry[0]=mesh.allocation.offset+lod_at[level];entry[1]=static_cast<UINT>(list.size()/3);
    std::memcpy(&entry[2],&lods->error[level],sizeof(float));
   }
   mesh.lod_levels=lods->levels;mesh.lod_extra_triangles=static_cast<UINT>(lods->indices[0].size()/3);
  }
  if(FAILED(hr=upload_native_geometry(mesh.allocation.page,mesh.allocation.offset,blob.data(),cursor)))return abandon(hr);
 }
 const UINT base=mesh.allocation.offset;
 mesh.position_offset+=base;mesh.normal_offset+=base;mesh.component_offset+=base;
 mesh.texture_coordinate_offset+=base;mesh.diffuse_storage_offset+=base;
 mesh.specular_storage_offset+=base;if(has_terrain)mesh.terrain_vertex_offset+=base;
 mesh.index_offset+=base;
 if(source.diffuse)mesh.diffuse_offset=mesh.diffuse_storage_offset;
 if(source.specular)mesh.specular_offset=mesh.specular_storage_offset;
 update_bounds(mesh);mesh.geometry_revision=source.geometry_revision;
 mesh.geometry_revision_valid=source.geometry_revision_valid;
 mesh.color_revision=source.color_revision;
 mesh.color_revision_valid=source.color_revision_valid;
 mesh.bytes=mesh.allocation.bytes;mesh.last_used_frame=native_frame_index_;
 const NativeGeometrySpan allocated_span=mesh.allocation;
 if(found!=native_meshes_.end()){
  auto& old=found->second;
  if(old.pending_references&&FAILED(hr=flush_native()))return abandon(hr);
  mesh.pending_references=old.pending_references;mesh.owned_references=old.owned_references;
  const auto retired=old.allocation;
  old=std::move(mesh);refresh_native_mesh_records(old);
  release_native_geometry_span(retired);output=&old;++perf_native_mesh_misses_;return S_OK;
 }
 try{auto inserted=native_meshes_.emplace(key,std::move(mesh));
  if(!inserted.second){release_native_geometry_span(allocated_span);return E_FAIL;}
  output=&inserted.first->second;++perf_native_mesh_misses_;
  if(key.selector.content_keyed)++perf_native_mesh_content_misses_;
  if(view.world.enabled){++perf_native_mesh_world_misses_;
   frame_acct::count_world_mesh(source.semantic_kind==SemanticKind::terrain,view.contribution.known&&!view.contribution.color,false,view.world.part);}
  if(source.semantic_kind==SemanticKind::terrain){
   ++native_frame_stats_.native_terrain_topology_uploads;
   native_frame_stats_.native_terrain_topology_upload_vertices+=
    source.component_count;
  }
  return S_OK;}
 catch(const std::bad_alloc&){release_native_geometry_span(allocated_span);return E_OUTOFMEMORY;}
}

HRESULT Renderer::append_native_dynamic(const NativeGeometryView& view,UINT& morph_offset,
                                        UINT& terrain_phase_offset){
 if(view.morph_count&&!view.morphs)return E_INVALIDARG;
 if(view.terrain.animation_phase_count&&!view.terrain_animation_phases)return E_INVALIDARG;
 const std::uint64_t morph_bytes=(std::uint64_t)view.morph_count*sizeof(NativeMorphOffset);
 const std::size_t original=native_pending_upload_.size();
 const std::uint64_t aligned=(original+15ull)&~15ull;
 if(aligned>UINT_MAX||morph_bytes>UINT_MAX-aligned||
    aligned+morph_bytes>256ull*1024ull*1024ull)return E_NOTIMPL;
 morph_offset=static_cast<UINT>(aligned);terrain_phase_offset=0;
 try{
  native_pending_upload_.resize(static_cast<std::size_t>(aligned+morph_bytes));
  if(morph_bytes)std::memcpy(native_pending_upload_.data()+morph_offset,view.morphs,
                             static_cast<std::size_t>(morph_bytes));
  if(view.terrain.animation_phase_count&&!view.terrain_phase_values){
   const std::uintptr_t token=view.object_token?view.object_token:
    reinterpret_cast<std::uintptr_t>(view.terrain_animation_phases);
   const auto found=native_terrain_phase_allocations_.find(token);
   if(found!=native_terrain_phase_allocations_.end()&&
      found->second.count==view.terrain.animation_phase_count&&
      (std::uint64_t)found->second.offset+found->second.count*4ull<=
       native_pending_upload_.size()){
    terrain_phase_offset=found->second.offset;
   }else{
    const std::uint64_t phase_begin=(native_pending_upload_.size()+15ull)&~15ull;
    const std::uint64_t phase_bytes=(std::uint64_t)view.terrain.animation_phase_count*4ull;
    if(phase_begin>UINT_MAX||phase_bytes>UINT_MAX-phase_begin||
       phase_begin+phase_bytes>256ull*1024ull*1024ull){
     native_pending_upload_.resize(original);return E_NOTIMPL;
    }
    terrain_phase_offset=static_cast<UINT>(phase_begin);
    native_pending_upload_.resize(static_cast<std::size_t>(phase_begin+phase_bytes));
    std::memcpy(native_pending_upload_.data()+terrain_phase_offset,
                view.terrain_animation_phases,static_cast<std::size_t>(phase_bytes));
    native_terrain_phase_allocations_[token]={terrain_phase_offset,
                                             view.terrain.animation_phase_count};
   }
  }
 }catch(const std::bad_alloc&){native_pending_upload_.resize(original);return E_OUTOFMEMORY;}
 perf_native_dynamic_bytes_+=native_pending_upload_.size()-original;
 return S_OK;
}

HRESULT Renderer::ensure_native_stream_buffers(UINT dynamic_bytes,UINT draw_count,UINT triangle_count,UINT component_count,UINT batch_count){
 if(!device_||!draw_count)return E_INVALIDARG;HRESULT hr;dynamic_bytes=std::max(16u,dynamic_bytes);
 if(!native_dynamic_||dynamic_bytes>native_dynamic_capacity_){if(dynamic_bytes>256u*1024u*1024u)return E_OUTOFMEMORY;const UINT capacity=grow_capacity(dynamic_bytes,16u*1024u*1024u,256u*1024u*1024u);D3D11_BUFFER_DESC description{};description.ByteWidth=capacity;description.Usage=D3D11_USAGE_DYNAMIC;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;native_dynamic_.Reset();native_dynamic_srv_.Reset();if(FAILED(hr=device_->CreateBuffer(&description,nullptr,&native_dynamic_)))return hr;D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_TYPELESS;srv.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX;srv.BufferEx.NumElements=capacity/4;srv.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW;if(FAILED(hr=device_->CreateShaderResourceView(native_dynamic_.Get(),&srv,&native_dynamic_srv_))){native_dynamic_.Reset();return hr;}native_dynamic_capacity_=capacity;}
 if(!native_draw_work_||draw_count>native_draw_work_capacity_){
  constexpr UINT maximum=(std::min)(kNativeRawElementLimit,
   UINT_MAX/static_cast<UINT>(sizeof(NativeDrawWorkGpu)));
  if(draw_count>maximum)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
  const UINT target=demand_growth_capacity(draw_count,native_draw_work_capacity_,maximum);
  const auto create=[&](UINT capacity,ComPtr<ID3D11Buffer>& buffer,
                        ComPtr<ID3D11ShaderResourceView>& view){
   const std::uint64_t bytes=static_cast<std::uint64_t>(capacity)*sizeof(NativeDrawWorkGpu);
   if(bytes>UINT_MAX)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
#ifdef EID3D11_TESTING
   if(test_native_draw_work_buffer_failures_){--test_native_draw_work_buffer_failures_;return E_OUTOFMEMORY;}
#endif
   D3D11_BUFFER_DESC description{};description.ByteWidth=static_cast<UINT>(bytes);
   description.Usage=D3D11_USAGE_DYNAMIC;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
   description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
   description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
   description.StructureByteStride=sizeof(NativeDrawWorkGpu);
   HRESULT result=device_->CreateBuffer(&description,nullptr,&buffer);if(FAILED(result))return result;
   D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_UNKNOWN;
   srv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;srv.Buffer.NumElements=capacity;
#ifdef EID3D11_TESTING
   if(test_native_draw_work_view_failures_){--test_native_draw_work_view_failures_;buffer.Reset();return E_FAIL;}
#endif
   result=device_->CreateShaderResourceView(buffer.Get(),&srv,&view);
   if(FAILED(result))buffer.Reset();return result;
  };
  ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11ShaderResourceView> view;
  UINT capacity=target;hr=create(capacity,buffer,view);
  if(FAILED(hr)&&capacity!=draw_count){capacity=draw_count;buffer.Reset();view.Reset();hr=create(capacity,buffer,view);}
  if(FAILED(hr))return hr;
  native_draw_work_=std::move(buffer);native_draw_work_srv_=std::move(view);
  native_draw_work_capacity_=capacity;
 }
 if(triangle_count&&(!native_visible_triangles_||triangle_count>native_visible_triangle_capacity_)){if(triangle_count>16u*1024u*1024u)return E_OUTOFMEMORY;const UINT capacity=grow_capacity(triangle_count,256u*1024u,16u*1024u*1024u);const std::uint64_t bytes=(std::uint64_t)capacity*16u;if(bytes>UINT_MAX)return E_OUTOFMEMORY;D3D11_BUFFER_DESC description{};description.ByteWidth=static_cast<UINT>(bytes);description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;description.StructureByteStride=16;native_visible_triangles_.Reset();native_visible_triangle_srv_.Reset();native_visible_triangle_uav_.Reset();if(FAILED(hr=device_->CreateBuffer(&description,nullptr,&native_visible_triangles_)))return hr;D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_UNKNOWN;srv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;srv.Buffer.NumElements=capacity;if(FAILED(hr=device_->CreateShaderResourceView(native_visible_triangles_.Get(),&srv,&native_visible_triangle_srv_)))return hr;D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_UNKNOWN;uav.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;uav.Buffer.NumElements=capacity;if(FAILED(hr=device_->CreateUnorderedAccessView(native_visible_triangles_.Get(),&uav,&native_visible_triangle_uav_)))return hr;native_visible_triangle_capacity_=capacity;}
 if(component_count&&(!native_prepared_vertices_||component_count>native_prepared_vertex_capacity_)){if(component_count>8u*1024u*1024u)return E_OUTOFMEMORY;const UINT capacity=grow_capacity(component_count,256u*1024u,8u*1024u*1024u);const std::uint64_t bytes=(std::uint64_t)capacity*sizeof(NativePreparedVertexGpu);if(bytes>UINT_MAX)return E_OUTOFMEMORY;D3D11_BUFFER_DESC description{};description.ByteWidth=static_cast<UINT>(bytes);description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;description.StructureByteStride=sizeof(NativePreparedVertexGpu);native_prepared_vertices_.Reset();native_prepared_vertex_srv_.Reset();native_prepared_vertex_uav_.Reset();if(FAILED(hr=device_->CreateBuffer(&description,nullptr,&native_prepared_vertices_)))return hr;D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_UNKNOWN;srv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;srv.Buffer.NumElements=capacity;if(FAILED(hr=device_->CreateShaderResourceView(native_prepared_vertices_.Get(),&srv,&native_prepared_vertex_srv_)))return hr;D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_UNKNOWN;uav.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;uav.Buffer.NumElements=capacity;if(FAILED(hr=device_->CreateUnorderedAccessView(native_prepared_vertices_.Get(),&uav,&native_prepared_vertex_uav_)))return hr;native_prepared_vertex_capacity_=capacity;}
 if(batch_count&&(!native_indirect_args_||batch_count>native_indirect_args_capacity_)){
  constexpr UINT maximum=kNativeRawElementLimit/4u;
  if(batch_count>maximum)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
  const UINT target=demand_growth_capacity(batch_count,native_indirect_args_capacity_,maximum);
  const auto create=[&](UINT capacity,ComPtr<ID3D11Buffer>& buffer,
                        ComPtr<ID3D11UnorderedAccessView>& view){
   const std::uint64_t bytes=static_cast<std::uint64_t>(capacity)*16u;
   if(bytes>UINT_MAX)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
#ifdef EID3D11_TESTING
   if(test_native_indirect_buffer_failures_){--test_native_indirect_buffer_failures_;return E_OUTOFMEMORY;}
#endif
   D3D11_BUFFER_DESC description{};description.ByteWidth=static_cast<UINT>(bytes);
   description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
   description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS|
                         D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
   HRESULT result=device_->CreateBuffer(&description,nullptr,&buffer);if(FAILED(result))return result;
   D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R32_TYPELESS;
   uav.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;uav.Buffer.NumElements=capacity*4u;
   uav.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
#ifdef EID3D11_TESTING
   if(test_native_indirect_view_failures_){--test_native_indirect_view_failures_;buffer.Reset();return E_FAIL;}
#endif
   result=device_->CreateUnorderedAccessView(buffer.Get(),&uav,&view);
   if(FAILED(result))buffer.Reset();return result;
  };
  ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11UnorderedAccessView> view;
  UINT capacity=target;hr=create(capacity,buffer,view);
  if(FAILED(hr)&&capacity!=batch_count){capacity=batch_count;buffer.Reset();view.Reset();hr=create(capacity,buffer,view);}
  if(FAILED(hr))return hr;
  native_indirect_args_=std::move(buffer);native_indirect_args_uav_=std::move(view);
  native_indirect_args_capacity_=capacity;
 }
 return S_OK;
}

HRESULT Renderer::get_native_material_group(const NativeMaterialKey& key,const FixedState& state,
 ID3D11ShaderResourceView* texture,bool texture_array,SemanticKind semantic,
 NativeOpacityClass opacity,std::uint64_t state_key,UINT& group,const D3D12TextureRef* d3d12_texture){
 const auto found=native_state_lookup_.find(key);
 if(found!=native_state_lookup_.end()){group=found->second;return S_OK;}
 group=UINT_MAX;
 try{
  auto available=std::find_if(native_state_buckets_.begin(),native_state_buckets_.end(),
   [](const NativeStateBucket& bucket){return !bucket.occupied;});
  if(available==native_state_buckets_.end()){
   if(native_state_buckets_.size()>=UINT_MAX)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
   group=static_cast<UINT>(native_state_buckets_.size());native_state_buckets_.emplace_back();
  }else group=static_cast<UINT>(available-native_state_buckets_.begin());
  auto& bucket=native_state_buckets_[group];bucket.state=state;bucket.texture_srv=texture;
  bucket.texture_d3d12=d3d12_texture?*d3d12_texture:nullptr;
  bucket.material_key=key;bucket.semantic_kind=semantic;bucket.opacity=opacity;
  bucket.state_key=state_key;bucket.texture_array=texture_array;bucket.occupied=true;
  native_state_lookup_.emplace(key,group);
  native_material_groups_dirty_=true;
#ifdef EID3D11_TESTING
  ++test_native_material_groups_created_;
#endif
  return S_OK;
 }catch(const std::bad_alloc&){
  if(group<native_state_buckets_.size())native_state_buckets_[group]={};
  group=UINT_MAX;return E_OUTOFMEMORY;
 }
}

void Renderer::bind_native_material_group(NativeInstanceRecord& record,UINT group) noexcept{
 if(record.group_bound&&record.command.state_bucket==group)return;
 if(record.group_bound){
  auto& previous=native_state_buckets_[record.command.state_bucket];
  if(!--previous.references)native_material_groups_dirty_=true;
 }
 ++native_state_buckets_[group].references;record.group_bound=true;native_layout_changed();
}

void Renderer::trim_native_material_groups() noexcept{
 if(!native_material_groups_dirty_)return;
 bridge_post_engine_surface_material_group_scan();
#ifdef EID3D11_TESTING
 ++test_native_material_group_trim_scans_;
#endif
 for(auto& bucket:native_state_buckets_)if(bucket.occupied&&!bucket.references){
  native_state_lookup_.erase(bucket.material_key);bucket={};
 }
 native_material_groups_dirty_=false;
}

void Renderer::order_native_groups(bool shadows){
 native_draw_order_.clear();native_draw_order_.reserve(native_pending_draws_.size());
 // Depth batches also split on the foliage bit (far maps draw foliage
 // opaque), so depth groups order by page and then that bit. In caster order
 // the trunks and crowns of one bucket interleave and broke the static layer
 // into about one batch per part.
 const auto emit=[&]{
  for(UINT group:native_active_groups_){auto& selected=native_state_buckets_[group].selected;
   std::stable_sort(selected.begin(),selected.end(),[&](std::size_t left,std::size_t right){
    const auto& a=native_queued_draw(left);const auto& b=native_queued_draw(right);
    if(a.geometry_page!=b.geometry_page||!shadows)return a.geometry_page<b.geometry_page;
    return (a.instance.flags&native_foliage_part_flag)<(b.instance.flags&native_foliage_part_flag);
   });
   native_draw_order_.insert(native_draw_order_.end(),selected.begin(),selected.end());selected.clear();}
  native_active_groups_.clear();
 };
 for(std::size_t index=0;index<native_pending_draws_.size();++index){
  const auto& draw=native_queued_draw(index);
  const UINT passes=native_pending_draws_[index].passes;
  if(shadows?!(passes&(UINT(native_shadow_dirty_mask_)<<8)):!(passes&1u))continue;
  auto& bucket=native_state_buckets_[draw.state_bucket];
  if(bucket.selection_epoch!=native_queue_epoch_){bucket.selection_epoch=native_queue_epoch_;++native_frame_stats_.native_material_buckets;}
  if(!shadows&&!draw.reorderable){emit();native_draw_order_.push_back(index);continue;}
  if(bucket.selected.empty())native_active_groups_.push_back(draw.state_bucket);
  bucket.selected.push_back(index);
 }
 emit();
}

HRESULT Renderer::resolve_native_prepared_material(
 const NativeGeometryView& view,const FixedState& st,SurfaceImage*,
 ID3D11ShaderResourceView* texture_srv,bool texture_array,
 NativeOpacityClass opacity,bool reorderable,
 const NativeMaterialKey& material_key,std::uint64_t state_key,
 UINT& state_bucket,const D3D12TextureRef* d3d12_texture){
 state_bucket=UINT_MAX;
 if(!view.prepared_material||
    !valid_prepared_identity(view.prepared_identity))return E_INVALIDARG;
 auto& handle=*view.prepared_material;
 NativePreparedMaterialSlot* slot=nullptr;
 if(handle.slot<native_prepared_material_slots_.size()){
  auto& candidate=native_prepared_material_slots_[handle.slot];
  if(candidate.active&&candidate.generation==handle.generation&&
     same_prepared_identity(candidate.identity,view.prepared_identity,false))
   slot=&candidate;
 }
 if(!slot){
  std::uint32_t slot_index=UINT_MAX;
  if(handle.slot<native_prepared_material_slots_.size()&&
     !native_prepared_material_slots_[handle.slot].active)
   slot_index=handle.slot;
  if(slot_index==UINT_MAX){
   if(native_frame_index_>native_prepared_material_reuse_sweep_frame_&&
      native_frame_index_-native_prepared_material_reuse_sweep_frame_>600){
    native_prepared_material_reuse_cursor_=0;
    native_prepared_material_reuse_sweep_frame_=native_frame_index_;
   }
   while(native_prepared_material_reuse_cursor_<native_prepared_material_slots_.size()){
    const auto index=native_prepared_material_reuse_cursor_++;
    const auto& candidate=native_prepared_material_slots_[index];
#ifdef EID3D11_TESTING
    ++test_native_prepared_material_slot_visits_;
#endif
    if(!candidate.active||
       (!candidate.persistent&&native_frame_index_>candidate.last_used_frame&&
        native_frame_index_-candidate.last_used_frame>600)){
     slot_index=static_cast<UINT>(index);break;
    }
   }
  }
  if(slot_index==UINT_MAX){
   if(native_prepared_material_slots_.size()>=UINT_MAX||
      native_prepared_material_slots_.size()>=native_prepared_material_slots_.max_size())
    return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
   try{native_prepared_material_slots_.push_back({});
    native_prepared_material_slots_.back().generation=native_prepared_material_generation_floor_;
    slot_index=static_cast<std::uint32_t>(native_prepared_material_slots_.size()-1);}
   catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
   native_prepared_material_reuse_cursor_=native_prepared_material_slots_.size();
  }
  slot=&native_prepared_material_slots_[slot_index];
  retire_native_material_instances(*slot);
  if(++slot->generation==0)++slot->generation;
  slot->identity=view.prepared_identity;slot->active=true;
  handle.slot=slot_index;handle.generation=slot->generation;
 }
 slot->material_key=material_key;slot->state_key=state_key;slot->opacity=opacity;
 slot->reorderable=reorderable;
 slot->preserve_order=view.ordering==NativeOrdering::preserve;
 slot->presentation_serial=view.prepared_identity.presentation_serial;
 slot->last_used_frame=native_frame_index_;
 slot->persistent=view.world.record_revision!=0;
 slot->owner=view.world.owner?*view.world.owner:std::weak_ptr<const void>{};
 UINT bucket_index=UINT_MAX;
 const HRESULT hr=get_native_material_group(material_key,st,texture_srv,texture_array,
  view.semantic_kind,opacity,state_key,bucket_index,d3d12_texture);if(FAILED(hr))return hr;
 slot->queue_epoch=native_queue_epoch_;slot->state_bucket=bucket_index;
 state_bucket=bucket_index;return S_OK;
}

HRESULT Renderer::get_native_skeleton_asset(
 const std::shared_ptr<const VisualSkeletonBinding>& binding,UINT& offset,UINT& allocation){
 offset=0;allocation=UINT_MAX;
 if(!binding||!binding->model||binding->tracks.size()!=binding->model->nodes.size()||
    binding->level_offsets.size()<2||binding->level_nodes.size()>binding->tracks.size()||
    binding->level_offsets.back()!=binding->level_nodes.size())return E_INVALIDARG;
 try{
  std::string key;
  const auto add=[&](const ResourceIdentity& identity){const auto value=identity.key();
   const auto length=static_cast<UINT>(value.size());key.append(reinterpret_cast<const char*>(&length),4);key.append(value);};
  add(binding->model->model_container);add(binding->model->bone_container);
  key.push_back(binding->rigid?1:(binding->neutral?2:3));if(binding->clip)add(binding->clip->identity);
  const auto found=native_skeleton_assets_.find(key);
  if(found!=native_skeleton_assets_.end()){
   found->second.model=binding->model;found->second.clip=binding->clip;
   offset=found->second.offset;allocation=found->second.allocation;return S_OK;
  }
  struct Node{UINT parent,bones,bone_count,rotations,rotation_count,translations,translation_count,padding;
   UINT morphs,morph_frames,morph_points,tail;};
  const auto count=static_cast<UINT>(binding->tracks.size());
  std::vector<Node> nodes(count);
  std::vector<std::uint8_t> bytes(32);
  const auto append=[&](const void* data,std::uint64_t length,UINT& target){
   const std::uint64_t start=(bytes.size()+15ull)&~15ull;
   if(start>UINT_MAX||length>UINT_MAX-start)return false;
   target=static_cast<UINT>(start);bytes.resize(static_cast<std::size_t>(start+length));
   if(length)std::memcpy(bytes.data()+start,data,static_cast<std::size_t>(length));return true;
  };
  UINT node_offset=0,level_offsets=0,level_nodes=0;
  if(!append(nodes.data(),nodes.size()*sizeof(Node),node_offset)||
     !append(binding->level_offsets.data(),binding->level_offsets.size()*4ull,level_offsets)||
     !append(binding->level_nodes.data(),binding->level_nodes.size()*4ull,level_nodes))
   return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
  for(UINT i=0;i<count;++i){
   const auto& model_node=binding->model->nodes[i];auto& node=nodes[i];
   if(!model_node.transform_available)continue; // no level dispatch references this index
   node.parent=binding->model->links[i].parent_index;node.bone_count=static_cast<UINT>(model_node.bone_positions.size()/3);
   if(!append(model_node.bone_positions.data(),model_node.bone_positions.size()*4ull,node.bones))return E_OUTOFMEMORY;
   if(const auto* track=binding->tracks[i]){
    if(track->rotations.size()>UINT_MAX||track->translations.size()>UINT_MAX)return E_INVALIDARG;
    node.rotation_count=static_cast<UINT>(track->rotations.size());node.translation_count=static_cast<UINT>(track->translations.size());
    if(!append(track->rotations.data(),track->rotations.size()*16ull,node.rotations)||
       !append(track->translations.data(),track->translations.size()*12ull,node.translations))return E_OUTOFMEMORY;
    node.morph_frames=track->morph_frames;node.morph_points=track->morph_points;
    if(std::uint64_t(node.morph_frames)*node.morph_points!=track->morphs.size())return E_INVALIDARG;
    if(!append(track->morphs.data(),track->morphs.size()*12ull,node.morphs))return E_OUTOFMEMORY;
   }
  }
  std::memcpy(bytes.data()+node_offset,nodes.data(),nodes.size()*sizeof(Node));
  const UINT header[8]={count,static_cast<UINT>(binding->level_offsets.size()-1),node_offset,
   level_offsets,level_nodes,binding->rigid?1u:0u,0,0};std::memcpy(bytes.data(),header,sizeof(header));
  if(bytes.size()>UINT_MAX-15u)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
  const UINT aligned=static_cast<UINT>((bytes.size()+15ull)&~15ull);
  HRESULT hr=allocate_native_range(native_animation_ranges_,aligned,NativeRangeBuffer::animation,allocation);
  if(FAILED(hr))return hr;
  const UINT start=native_animation_ranges_[allocation].base;
  NativeSkeletonAsset asset{start,static_cast<UINT>(bytes.size()),allocation,binding->model,binding->clip,bool(binding->clip)};
  try{native_skeleton_assets_.emplace(std::move(key),std::move(asset));}
  catch(...){native_animation_ranges_[allocation].owned=false;native_range_freed(native_animation_ranges_,allocation);throw;}
  D3D11_BOX box{};box.left=start;box.right=start+static_cast<UINT>(bytes.size());box.bottom=box.back=1;
  if(d3d12_){ // D3D12 backend: the skeleton asset into the executor's animation buffer
   const HRESULT written=d3d12_->write_buffer(*native12_.animation,start,bytes.data(),bytes.size());if(FAILED(written))return written;
  }else context_->UpdateSubresource(native_animation_.Get(),0,&box,bytes.data(),0,0);
  profiler_.add_upload(bytes.size());
  offset=start;
  return S_OK;
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
}

HRESULT Renderer::ensure_native_animation_buffer(UINT required){
 if(required<=native_animation_capacity_)return S_OK;
 if(required>UINT_MAX-15u)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 const UINT capacity=static_cast<UINT>((std::min)(std::uint64_t(UINT_MAX-15u),
  (std::max)({std::uint64_t(required),std::uint64_t(native_animation_capacity_)*2,65536ull})));
 if(d3d12_){ // D3D12 backend: a raw buffer on the executor, kept across growth
  const UINT bytes=align_up(capacity,16);
  const HRESULT hr=native_d3d12_persistent(native12_.animation,bytes,0,native_animation_used_);
  if(SUCCEEDED(hr))native_animation_capacity_=bytes;
  return hr;
 }
 ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11ShaderResourceView> srv;
 ComPtr<ID3D11UnorderedAccessView> uav;
 const auto create=[&](UINT bytes){
  buffer.Reset();srv.Reset();uav.Reset();
  D3D11_BUFFER_DESC desc{};desc.ByteWidth=align_up(bytes,16);desc.Usage=D3D11_USAGE_DEFAULT;
  desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
  desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
  HRESULT result=device_->CreateBuffer(&desc,nullptr,&buffer);if(FAILED(result))return result;
  D3D11_SHADER_RESOURCE_VIEW_DESC read{};read.Format=DXGI_FORMAT_R32_TYPELESS;
  read.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX;read.BufferEx.NumElements=desc.ByteWidth/4;
  read.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW;
  if(FAILED(result=device_->CreateShaderResourceView(buffer.Get(),&read,&srv)))return result;
  D3D11_UNORDERED_ACCESS_VIEW_DESC write{};write.Format=DXGI_FORMAT_R32_TYPELESS;
  write.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;write.Buffer.NumElements=desc.ByteWidth/4;
  write.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
  return device_->CreateUnorderedAccessView(buffer.Get(),&write,&uav);
 };
 HRESULT hr=create(capacity);
 if(hr==E_OUTOFMEMORY&&capacity!=required)hr=create(required);
 if(FAILED(hr))return hr;
 if(native_animation_){D3D11_BOX box{};box.right=native_animation_used_;box.bottom=box.back=1;
  context_->CopySubresourceRegion(buffer.Get(),0,0,0,0,native_animation_.Get(),0,&box);}
 D3D11_BUFFER_DESC description{};buffer->GetDesc(&description);
 native_animation_=std::move(buffer);native_animation_srv_=std::move(srv);
 native_animation_uav_=std::move(uav);native_animation_capacity_=description.ByteWidth;
 return S_OK;
}

HRESULT Renderer::resolve_native_terrain_phases(const NativeGeometryView& view,NativeQueuedDraw& draw){
 const auto* values=view.terrain_phase_values;
 if(!values || !view.terrain.animation_phase_count)return S_OK;
 const UINT count=view.terrain.animation_phase_count;
 if(!values->revision || !view.terrain_animation_phases || count>65536 ||
    !view.world.owner || view.world.owner->expired())return E_INVALIDARG;
 // The slot is found by identity in the renderer's own lookup. An invalid
 // identity cannot name a slot (it used to take a fresh slot every frame).
 if(!valid_prepared_identity(view.prepared_identity))return E_INVALIDARG;
 const auto& identity=view.prepared_identity;
 const NativeTerrainPhaseKey key{identity.signed_id,identity.set_index,identity.lifecycle_generation,
  identity.world_epoch,identity.plan_generation,identity.terrain_source};
 UINT index=UINT_MAX;
 try{
  const auto found=native_terrain_phase_lookup_.find(key);
  if(found!=native_terrain_phase_lookup_.end()){
   index=found->second;
   // Same identity, a not yet reaped expired owner: the live owner takes over.
   if(native_terrain_phases_[index].owner.expired())native_terrain_phases_[index].owner=*view.world.owner;
  }else{
   const bool reuse=!native_terrain_phase_free_.empty();
   if(reuse)index=native_terrain_phase_free_.back();
   else{native_terrain_phases_.emplace_back();index=static_cast<UINT>(native_terrain_phases_.size()-1);}
   native_terrain_phase_lookup_.emplace(key,index);
   if(reuse)native_terrain_phase_free_.pop_back();
   auto& slot=native_terrain_phases_[index];
   slot.active=true;slot.key=key;slot.owner=*view.world.owner;
   slot.revision=0;slot.count=0;slot.valid=slot.scheduled=false;
   // Growth guard: active slots are bounded by the map's animated sectors,
   // so a table far above this frame's live count means the lookup leaks.
   const std::size_t active=native_terrain_phase_lookup_.size();
   if(!native_terrain_phase_growth_logged_&&
      active>(std::max)(std::size_t(4096),std::size_t(native_terrain_phase_live_)*16u)){
    native_terrain_phase_growth_logged_=true;
    char detail[160];std::snprintf(detail,sizeof(detail),"active=%zu slots=%zu live_this_frame=%u frame=%llu",
     active,native_terrain_phases_.size(),native_terrain_phase_live_,
     static_cast<unsigned long long>(native_frame_index_));
    bridge_append_probe_event("terrain_phase_slots_growth",detail);
   }
  }
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 auto& slot=native_terrain_phases_[index];
 if(slot.last_frame!=native_frame_index_){
  slot.last_frame=native_frame_index_;
  if(native_terrain_phase_live_frame_!=native_frame_index_){
   native_terrain_phase_live_frame_=native_frame_index_;native_terrain_phase_live_=0;}
  ++native_terrain_phase_live_;
 }
 if(slot.revision!=values->revision || slot.count!=count){
  if(slot.allocation>=native_animation_ranges_.size() || slot.count!=count ||
     native_animation_ranges_[slot.allocation].references){
   UINT allocation=UINT_MAX;
   HRESULT hr=allocate_native_range(native_animation_ranges_,align_up(count*4,16),NativeRangeBuffer::animation,allocation);
   if(FAILED(hr))return hr;
   if(slot.allocation<native_animation_ranges_.size()){native_animation_ranges_[slot.allocation].owned=false;
    native_range_freed(native_animation_ranges_,slot.allocation);}
   slot.allocation=allocation;
  }
  slot.revision=values->revision;slot.count=count;slot.valid=false;slot.scheduled=false;
 }
 if(!slot.valid&&!slot.scheduled){
  const auto original=native_pending_upload_.size();
  const std::uint64_t offset=(native_pending_upload_.size()+15ull)&~15ull;
  if(offset+count*4ull>256ull*1024ull*1024ull)return E_OUTOFMEMORY;
  native_terrain_phase_work_.reserve(native_terrain_phase_work_.size()+1);
  native_pending_upload_.resize(static_cast<std::size_t>(offset+count*4ull));
  std::memcpy(native_pending_upload_.data()+offset,view.terrain_animation_phases,count*4u);
  native_terrain_phase_work_.push_back({index,slot.allocation,static_cast<UINT>(offset),count,slot.revision});
  perf_native_dynamic_bytes_+=native_pending_upload_.size()-original;
  slot.scheduled=true;
 }
 draw.animation_allocation=slot.allocation;
 draw.instance.terrain_phase_offset=native_animation_ranges_[slot.allocation].base;
 draw.instance.terrain_phase_count=count;draw.instance.terrain_flags|=4u;
 return S_OK;
}

HRESULT Renderer::upload_native_terrain_phases(){
 PERF06_SCOPE("upload_terrain_phases");
 native_terrain_phase_copies_.clear();
 for(const auto& work:native_terrain_phase_work_)if(native_animation_ranges_[work.allocation].references){
  if(std::uint64_t(work.offset)+work.count*4ull>native_pending_upload_.size())return E_FAIL;
  NativeDrawWorkGpu copy{};copy.record=work.offset;
  copy.output_triangle_base=native_animation_ranges_[work.allocation].base;
  copy.indirect_args_byte_offset=work.count;native_terrain_phase_copies_.push_back(copy);
 }
 if(native_terrain_phase_copies_.empty())return S_OK;
 if(native_terrain_phase_copies_.size()>native_draw_work_capacity_)return E_OUTOFMEMORY;
 D3D11_MAPPED_SUBRESOURCE mapped{};
 HRESULT hr=context_->Map(native_draw_work_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped);if(FAILED(hr))return hr;
 std::memcpy(mapped.pData,native_terrain_phase_copies_.data(),native_terrain_phase_copies_.size()*sizeof(NativeDrawWorkGpu));
 context_->Unmap(native_draw_work_.Get(),0);
 ID3D11ShaderResourceView* empty=nullptr;
 context_->CSSetShaderResources(10,1,&empty);
 context_->CSSetShaderResources(2,1,native_dynamic_srv_.GetAddressOf());
 context_->CSSetShaderResources(16,1,native_draw_work_srv_.GetAddressOf());
 context_->CSSetUnorderedAccessViews(0,1,native_animation_uav_.GetAddressOf(),nullptr);
 context_->CSSetShader(native_terrain_phase_cs_.Get(),nullptr,0);
 transition_profile_pass("compute_terrain_phase");context_->Dispatch(static_cast<UINT>(native_terrain_phase_copies_.size()),1,1);
 ++perf_native_compute_dispatches_;++native_frame_stats_.native_compute_dispatches;
 ID3D11UnorderedAccessView* no_uav=nullptr;context_->CSSetUnorderedAccessViews(0,1,&no_uav,nullptr);
 context_->CSSetShaderResources(2,1,&empty);context_->CSSetShaderResources(16,1,&empty);
 for(const auto& work:native_terrain_phase_work_)if(native_animation_ranges_[work.allocation].references){
  auto& slot=native_terrain_phases_[work.slot];
  if(slot.allocation==work.allocation&&slot.revision==work.revision)slot.valid=true;
#ifdef EID3D11_TESTING
  test_native_terrain_phase_upload_bytes_+=work.count*4ull;
#endif
 }
 profiler_.add_upload(native_terrain_phase_copies_.size()*sizeof(NativeDrawWorkGpu));
 return S_OK;
}

void Renderer::discard_native_terrain_phase_work() noexcept{
 for(const auto& work:native_terrain_phase_work_)if(work.slot<native_terrain_phases_.size()){
  auto& slot=native_terrain_phases_[work.slot];
  if(slot.allocation==work.allocation&&slot.revision==work.revision)slot.scheduled=false;
 }
 native_terrain_phase_work_.clear();native_terrain_phase_copies_.clear();
}

HRESULT Renderer::ensure_native_pose_nodes(UINT required){
 if(required<=native_pose_node_capacity_)return S_OK;
 constexpr UINT limit=UINT_MAX/sizeof(NativePoseNodeGpu);
 if(required>limit)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 UINT capacity=static_cast<UINT>((std::min)(std::uint64_t(limit),
  (std::max)({std::uint64_t(required),std::uint64_t(native_pose_node_capacity_)*2,1024ull})));
 if(d3d12_){ // D3D12 backend: structured nodes on the executor, kept across growth
  const HRESULT hr=native_d3d12_persistent(native12_.pose_nodes,UINT64(capacity)*sizeof(NativePoseNodeGpu),sizeof(NativePoseNodeGpu),
   UINT64(native_pose_node_used_)*sizeof(NativePoseNodeGpu));
  if(SUCCEEDED(hr))native_pose_node_capacity_=capacity;
  return hr;
 }
 ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11UnorderedAccessView> uav;
 const auto create=[&](UINT count){buffer.Reset();srv.Reset();uav.Reset();
  D3D11_BUFFER_DESC description{};description.ByteWidth=count*sizeof(NativePoseNodeGpu);
  description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
  description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;description.StructureByteStride=sizeof(NativePoseNodeGpu);
  HRESULT hr=device_->CreateBuffer(&description,nullptr,&buffer);if(FAILED(hr))return hr;
  if(FAILED(hr=device_->CreateShaderResourceView(buffer.Get(),nullptr,&srv)))return hr;
  return device_->CreateUnorderedAccessView(buffer.Get(),nullptr,&uav);};
 HRESULT hr=create(capacity);if(hr==E_OUTOFMEMORY&&capacity!=required){capacity=required;hr=create(capacity);}
 if(FAILED(hr))return hr;
 if(native_pose_nodes_&&native_pose_node_used_){D3D11_BOX box{};
  box.right=native_pose_node_used_*sizeof(NativePoseNodeGpu);box.bottom=box.back=1;
  context_->CopySubresourceRegion(buffer.Get(),0,0,0,0,native_pose_nodes_.Get(),0,&box);}
 native_pose_nodes_=std::move(buffer);native_pose_node_srv_=std::move(srv);native_pose_node_uav_=std::move(uav);
 native_pose_node_capacity_=capacity;return S_OK;
}

HRESULT Renderer::resolve_native_pose(NativeWorldGeometry::Pose* pose,const NativePreparedIdentity& identity,
 UINT node,NativeQueuedDraw& draw){
 if(!pose)return S_OK;
 auto& source=*pose;
 if(!source.binding||!source.binding->model||node>=source.binding->tracks.size()||
    !std::isfinite(source.fraction)||source.fraction<0||source.fraction>1||!std::isfinite(source.height_scale))return E_INVALIDARG;
 NativePoseGpu value{};value.frame=source.frame;value.fraction=source.fraction;value.height_scale=source.height_scale;
 value.root_to_world=source.root_to_world;std::copy(source.shape.begin(),source.shape.end(),value.shape);
 std::copy(source.center_shift.begin(),source.center_shift.end(),value.center_shift);
 for(float f:value.root_to_world)if(!std::isfinite(f))return E_INVALIDARG;
 for(float f:value.shape)if(!std::isfinite(f))return E_INVALIDARG;
 for(float f:value.center_shift)if(!std::isfinite(f))return E_INVALIDARG;
 try{
  UINT index=source.slot;
  const auto same_entity=[&](const NativePoseSlot& slot){return slot.active&&slot.generation==source.generation&&
   slot.identity.signed_id==identity.signed_id&&
   slot.identity.lifecycle_generation==identity.lifecycle_generation&&
   slot.identity.world_epoch==identity.world_epoch&&slot.identity.plan_generation==identity.plan_generation;};
  if(index>=native_pose_slots_.size()||!same_entity(native_pose_slots_[index])){
   index=UINT_MAX;
   for(UINT i=0;i<native_pose_slots_.size();++i)if(!native_pose_slots_[i].active){index=i;break;}
   if(index==UINT_MAX){index=static_cast<UINT>(native_pose_slots_.size());native_pose_slots_.emplace_back();}
   auto& slot=native_pose_slots_[index];slot.active=true;slot.identity=identity;slot.owner=source.owner;
   slot.binding.reset();slot.valid=false;slot.revision=slot.scheduled=0;slot.count=0;
   source.slot=index;source.generation=slot.generation;
  }
  auto& slot=native_pose_slots_[index];
  // The scene has already resolved exact immutable identity equality. A pinned
  // binding pointer here is only a local fast path, never an engine-state key.
  UINT animation_allocation=slot.animation_allocation;
  if(slot.binding==source.binding)value.asset=slot.value.asset;
  else {HRESULT hr=get_native_skeleton_asset(source.binding,value.asset,animation_allocation);if(FAILED(hr))return hr;}
  value.output=slot.value.output;
  value.generation=slot.value.generation;
  const UINT count=static_cast<UINT>(source.binding->tracks.size());
  const bool changed=slot.count!=count||std::memcmp(&slot.value,&value,sizeof(value))!=0||slot.binding!=source.binding;
  if(changed){
   if(slot.allocation>=native_pose_ranges_.size()||slot.count!=count||native_pose_ranges_[slot.allocation].references){
    UINT allocation=UINT_MAX;
    const HRESULT hr=allocate_native_range(native_pose_ranges_,count,NativeRangeBuffer::pose,allocation);if(FAILED(hr))return hr;
    if(slot.allocation<native_pose_ranges_.size()){native_pose_ranges_[slot.allocation].owned=false;
     native_range_freed(native_pose_ranges_,slot.allocation);}
    slot.allocation=allocation;value.output=native_pose_ranges_[allocation].base;
   }
   if(slot.animation_allocation!=animation_allocation){
    if(slot.animation_allocation<native_animation_ranges_.size()&&native_animation_ranges_[slot.animation_allocation].owners)
     --native_animation_ranges_[slot.animation_allocation].owners;
    slot.animation_allocation=animation_allocation;++native_animation_ranges_[animation_allocation].owners;
   }
   if(++native_pose_ranges_[slot.allocation].generation==0)++native_pose_ranges_[slot.allocation].generation;
   if(++slot.revision==0)++slot.revision;
   value.generation={static_cast<UINT>(slot.revision),static_cast<UINT>(slot.revision>>32)};
   slot.value=value;slot.count=count;slot.binding=source.binding;slot.valid=false;
  }
  if(!slot.valid&&slot.scheduled!=slot.revision){
   native_pose_work_.push_back({slot.value,index,slot.count,slot.allocation,slot.revision,
                               native_pose_ranges_[slot.allocation].generation});slot.scheduled=slot.revision;
  }
  draw.pose_slot=index;draw.pose_allocation=slot.allocation;
  draw.animation_allocation=slot.animation_allocation;
  draw.instance.pose_node=slot.value.output+node;
  return S_OK;
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
}

HRESULT Renderer::prepare_native_poses(){
 if(native_pose_work_.empty())return S_OK;
 if(!native_pose_cs_||!native_pose_node_uav_)return E_FAIL;
 try{
  native_pose_upload_.clear();
  for(const auto& job:native_pose_work_){
   if(job.allocation>=native_pose_ranges_.size()||!native_pose_ranges_[job.allocation].references||
      native_pose_ranges_[job.allocation].generation!=job.allocation_generation)continue;
   native_pose_upload_.push_back(job.value);
  }
  if(native_pose_upload_.empty())return S_OK;
  const auto count=static_cast<UINT>(native_pose_upload_.size());
  const UINT width=(std::min)(count,65535u),height=(count+width-1)/width;
  if(height>65535u)return E_OUTOFMEMORY;
  UINT first=0;
  if(structured_ring_supported_){
   const HRESULT hr=native_pose_input_ring_.write(device_.Get(),context_.Get(),native_pose_upload_.data(),
    count*UINT(sizeof(NativePoseGpu)),native_pose_input_srv_,first);
   if(FAILED(hr))return hr;
   native_pose_input_capacity_=UINT((std::min)(std::uint64_t(UINT_MAX),native_pose_input_ring_.capacity_bytes()/sizeof(NativePoseGpu)));
  }else{
  if(count>native_pose_input_capacity_){
   if(count>UINT_MAX/sizeof(NativePoseGpu))return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
   D3D11_BUFFER_DESC description{};description.ByteWidth=count*sizeof(NativePoseGpu);description.Usage=D3D11_USAGE_DYNAMIC;
   description.BindFlags=D3D11_BIND_SHADER_RESOURCE;description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
   description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;description.StructureByteStride=sizeof(NativePoseGpu);
   ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11ShaderResourceView> srv;
   HRESULT hr=device_->CreateBuffer(&description,nullptr,&buffer);if(FAILED(hr))return hr;
   if(FAILED(hr=device_->CreateShaderResourceView(buffer.Get(),nullptr,&srv)))return hr;
   native_pose_inputs_=std::move(buffer);native_pose_input_srv_=std::move(srv);native_pose_input_capacity_=count;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  HRESULT hr=context_->Map(native_pose_inputs_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped);if(FAILED(hr))return hr;
  std::memcpy(mapped.pData,native_pose_upload_.data(),count*sizeof(NativePoseGpu));context_->Unmap(native_pose_inputs_.Get(),0);
  }
  profiler_.add_upload(count*sizeof(NativePoseGpu));
  {NativeDispatchRange range;range.first=first;range.count=count;
   const HRESULT hr=upload_native_work_range(range);if(FAILED(hr))return hr;}
  ID3D11ShaderResourceView* inputs[3]={native_animation_srv_.Get(),native_pose_input_srv_.Get(),nullptr};
  context_->CSSetShaderResources(10,3,inputs);context_->CSSetUnorderedAccessViews(4,1,native_pose_node_uav_.GetAddressOf(),nullptr);
  context_->CSSetShader(native_pose_cs_.Get(),nullptr,0);
  // Each group owns one entity; all of its levels stay in that group. Dispatch
  // dimensions cover the API limit without a dispatch per actor or joint.
  transition_profile_pass("compute_pose");context_->Dispatch(width,height,1);++perf_native_compute_dispatches_;++native_frame_stats_.native_compute_dispatches;
  ID3D11UnorderedAccessView* empty_uav=nullptr;context_->CSSetUnorderedAccessViews(4,1,&empty_uav,nullptr);
  ID3D11ShaderResourceView* empty[3]{};context_->CSSetShaderResources(10,3,empty);context_->CSSetShader(nullptr,nullptr,0);
  for(const auto& job:native_pose_work_){auto& slot=native_pose_slots_[job.slot];
#ifdef EID3D11_TESTING
   if(job.allocation<native_pose_ranges_.size()&&native_pose_ranges_[job.allocation].references&&
      native_pose_ranges_[job.allocation].generation==job.allocation_generation)test_native_pose_nodes_evaluated_+=job.count;
#endif
   if(job.allocation<native_pose_ranges_.size()&&native_pose_ranges_[job.allocation].references&&
      native_pose_ranges_[job.allocation].generation==job.allocation_generation&&
      slot.revision==job.revision&&slot.value.output==job.value.output)slot.valid=true;}
  return S_OK;
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
}

bool Renderer::set_native_world_clip(const std::array<float,16>& clip){
 for(float value:clip)if(!std::isfinite(value))return false;
 if(native_world_clip_valid_&&native_world_clip_==clip)return true;
 if(!native_pending_draws_.empty()&&FAILED(flush_native()))return false;
 native_world_clip_=clip;native_world_clip_valid_=native_world_clip_dirty_=true;return true;
}

UINT Renderer::allocate_native_instance_record(){
 native_layout_changed();
 UINT index=UINT_MAX;
 // The free list is a min-heap: the lowest free index is reused first, so
 // the records of a world that is gone end up free at the end of the table
 // where trim_native_tables can drop them.
 if(!native_instance_free_.empty()){
  std::pop_heap(native_instance_free_.begin(),native_instance_free_.end(),std::greater<UINT>());
  index=native_instance_free_.back();native_instance_free_.pop_back();
 }else {
  if(native_instance_records_.size()>=UINT_MAX/sizeof(NativeInstanceGpu))return UINT_MAX;
  if(native_instance_free_.capacity()<native_instance_records_.size()+1)
   native_instance_free_.reserve((std::max)(native_instance_free_.capacity()*2,native_instance_records_.size()+1));
  index=static_cast<UINT>(native_instance_records_.size());native_instance_records_.emplace_back();
 }
 native_instance_records_[index]={};native_instance_records_[index].occupied=true;return index;
}

HRESULT Renderer::resolve_native_instance(const NativeGeometryView& view,NativeQueuedDraw& draw){
 if(!view.world.record_revision)return S_OK;
 if(!view.prepared_material||view.prepared_material->slot>=native_prepared_material_slots_.size())return E_INVALIDARG;
 auto& material=native_prepared_material_slots_[view.prepared_material->slot];
 UINT& current=material.persistent_instance[native_shadow_collecting_?1:0];
 const HRESULT hr=store_native_instance(current,view.world.owner?*view.world.owner:std::weak_ptr<const void>{},draw);
 if(SUCCEEDED(hr)&&!native_shadow_collecting_&&view.world.enabled&&
    view.semantic_kind==SemanticKind::figure&&current<native_instance_records_.size()){
  auto& record=native_instance_records_[current];
  if(!record.lod_observer){
   auto identity=std::make_shared<NativeLodIdentity>();
   if(view.lod_observer)*identity=*view.lod_observer;
   else{
    identity->id=view.prepared_identity.signed_id;
    identity->generation=view.prepared_identity.lifecycle_generation;
    identity->part=view.prepared_identity.part_index;
    if(view.world.pose&&view.world.pose->binding&&view.world.pose->binding->model)
     identity->model=view.world.pose->binding->model->visual_reference;
    else if(view.world.identity)identity->model=view.world.identity->canonical_name;
   }
   record.lod_observer=std::move(identity);
  }
 }
 // Fur prototype: an owned preview part's profile (its colour record only;
 // never its wound layer, see queue_native_records).
 if(SUCCEEDED(hr)&&!native_shadow_collecting_&&current<native_instance_records_.size()){
  auto& record=native_instance_records_[current];
  record.fur=view.fur&&!view.wound_surface_interface?*view.fur:nullptr;record.fur_node_base=view.fur_node_base;
 }
 return hr;
}

HRESULT Renderer::store_native_instance(UINT& current,const std::weak_ptr<const void>& owner,NativeQueuedDraw& draw){
 auto value=draw.instance;value.object_to_clip={};
 if(value.pose_node!=UINT_MAX){
  value.object_to_world={}; // the shared GPU node owns the full matrix and current bounds
  std::fill(std::begin(draw.bounds_min),std::end(draw.bounds_min),0.f);
  std::fill(std::begin(draw.bounds_max),std::end(draw.bounds_max),0.f);
 }
 draw.instance=value;
 const auto same_command=[&](const NativeQueuedDraw& old){
  return old.mesh==draw.mesh && old.index_count==draw.index_count && old.component_count==draw.component_count &&
   std::equal(std::begin(old.bounds_min),std::end(old.bounds_min),std::begin(draw.bounds_min)) &&
   std::equal(std::begin(old.bounds_max),std::end(old.bounds_max),std::begin(draw.bounds_max)) &&
   old.state_bucket==draw.state_bucket && old.camp_clip==draw.camp_clip && widescreen::same_composition(old.hud_composition,draw.hud_composition) && old.reorderable==draw.reorderable && old.preserve_order==draw.preserve_order &&
   old.world_slot==draw.world_slot && old.world_base==draw.world_base && old.world_allocation==draw.world_allocation &&
   old.world_generation==draw.world_generation && old.geometry_radius==draw.geometry_radius && old.prepare_world==draw.prepare_world &&
   old.pose_slot==draw.pose_slot && old.pose_allocation==draw.pose_allocation &&
   old.animation_allocation==draw.animation_allocation &&
   old.geometry_page==draw.geometry_page;
 };
 try{
  bool instance_changed=current>=native_instance_records_.size()||!native_instance_records_[current].owned||
     std::memcmp(&native_instance_records_[current].command.instance,&value,sizeof(value))!=0;
  auto descriptor=native_draw_descriptor(draw,current|0x80000000u);
  bool descriptor_changed=current>=native_instance_records_.size()||!native_instance_records_[current].owned||
     std::memcmp(&native_instance_records_[current].descriptor,&descriptor,sizeof(descriptor))!=0;
  const bool command_changed=instance_changed||descriptor_changed||!same_command(native_instance_records_[current].command);
  if(command_changed){
   if(current<native_instance_records_.size()&&native_instance_records_[current].references){
    native_instance_records_[current].owned=false;current=UINT_MAX;
   }
   if(current>=native_instance_records_.size()||!native_instance_records_[current].owned){
    current=allocate_native_instance_record();if(current==UINT_MAX)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    instance_changed=descriptor_changed=true;
   }
   auto& record=native_instance_records_[current];
   if(record.command.index_count!=draw.index_count||record.command.component_count!=draw.component_count||
      record.command.instance.world_geometry!=draw.instance.world_geometry||
      record.command.reorderable!=draw.reorderable||record.command.preserve_order!=draw.preserve_order||
      ((record.command.instance.flags^draw.instance.flags)&native_foliage_part_flag)!=0||
      record.command.geometry_page!=draw.geometry_page)
     native_layout_changed();
   bind_native_material_group(record,draw.state_bucket);
   record.depth_current=false; // A changed command must be accepted by depth selection again.
   record.command=draw;record.command.persistent_instance=current;
   descriptor.instance_index=current|0x80000000u;record.descriptor=descriptor;
#ifdef EID3D11_TESTING
   ++test_native_command_values_written_;
#endif
   record.owner=owner;
   if(instance_changed)record.uploaded=false;
   if(descriptor_changed)record.descriptor_uploaded=false;
  }
  auto& record=native_instance_records_[current];
  if((!record.uploaded||!record.descriptor_uploaded)&&!record.scheduled){native_instance_dirty_.push_back(current);record.scheduled=true;}
  draw.persistent_instance=current;return S_OK;
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
}

void Renderer::release_native_instance(UINT index) noexcept{
 if(index>=native_instance_records_.size())return;
 auto& record=native_instance_records_[index];if(!record.occupied)return;
 if(record.references)--record.references;
 if(!record.references&&!record.owned&&!record.scheduled){
  if(record.group_bound){
   auto& group=native_state_buckets_[record.command.state_bucket];
   if(!--group.references)native_material_groups_dirty_=true;
   record.group_bound=false;
  }
  record.owner.reset();record.fur.reset();record.lod_observer.reset();record.command={};record.occupied=false;record.uploaded=false;native_instance_free_.push_back(index);
  std::push_heap(native_instance_free_.begin(),native_instance_free_.end(),std::greater<UINT>());
 }
}

void Renderer::retire_native_material_instances(NativePreparedMaterialSlot& slot) noexcept{
 for(auto& index:slot.persistent_instance){
  if(index<native_instance_records_.size()){
   auto& record=native_instance_records_[index];
   record.owned=false;
   if(!record.references)release_native_instance(index);
  }
  index=UINT_MAX;
 }
}

Renderer::NativeDrawGpu Renderer::native_draw_descriptor(const NativeQueuedDraw& draw,UINT index) const noexcept{
 NativeDrawGpu value{};value.instance_index=index;
 value.component_offset=draw.mesh->component_offset;value.index_offset=draw.mesh->index_offset;
 value.index_count=draw.index_count;value.component_count=draw.component_count;value.steep_first_triangle=draw.mesh->steep_first_triangle;
 std::copy_n(draw.bounds_min,3,value.bounds_min);std::copy_n(draw.bounds_max,3,value.bounds_max);
 value.bounds_min[3]=value.bounds_max[3]=1;
 if(draw.instance.world_geometry){value.prepared_vertex_base=draw.world_base;native_world_draw_values(draw,value);}
 return value;
}

// The world-clip constants (b3, NativeWorldClip) both executors upload.
void Renderer::native_world_clip_values(std::array<std::uint8_t,sizeof(std::array<float,16>)*2+64>& values){
 float distance_cull=native_draw_distance_;
 if(fog_cull_distance_>0&&fog_cull_frame_+1>=native_frame_index_)distance_cull=(std::min)(distance_cull,fog_cull_distance_);
 std::uint8_t* const out=values.data();
 // The drawn camera clip carries the upscaler jitter; the cull clip does not.
 const auto drawn_clip=jittered_clip(native_world_clip_);
 std::memcpy(out,drawn_clip.data(),sizeof(native_world_clip_));
 std::memcpy(out+sizeof(native_world_clip_),
  (native_cull_frozen_?native_cull_clip_:native_world_clip_).data(),sizeof(native_world_clip_));
 const float cull_params[4]={float(width_),float(height_),native_cull_min_screen_,native_shadow_cull_min_texels_};
 std::memcpy(out+sizeof(native_world_clip_)*2,cull_params,sizeof(cull_params));
 const float hiz_params[4]={native_occlusion_active()?1.f:0.f,float(native_hiz_width_),float(native_hiz_height_),float(native_hiz_mips_)};
 std::memcpy(out+sizeof(native_world_clip_)*2+16,hiz_params,sizeof(hiz_params));
 // Clip w grows by the length of its row per metre of planar view depth.
 const auto& cull=native_cull_frozen_?native_cull_clip_:native_world_clip_;
 const float w_per_metre=std::sqrt(cull[12]*cull[12]+cull[13]*cull[13]+cull[14]*cull[14]);
 // z, w: the static LOD pixel limit (0: full detail) and its crossfade band
 // (nativeStaticLodSelect), a fraction of the limit.
 const float distance_params[4]={distance_cull>0&&std::isfinite(w_per_metre)?distance_cull*w_per_metre:0.f,distance_cull,
  native_lod_error_px_,kStaticLodBand};
 std::memcpy(out+sizeof(native_world_clip_)*2+32,distance_params,sizeof(distance_params));
 // The crown impostor size (0: off) and crossfade band (nativeImpostorSelect).
 const float impostor_params[4]={native_impostor_pixels_,kImpostorBand,native_impostor_debug_?1.f:0.f,0.f};
 std::memcpy(out+sizeof(native_world_clip_)*2+48,impostor_params,sizeof(impostor_params));
 native_distance_cull_uploaded_=distance_cull;native_lod_error_uploaded_=native_lod_error_px_;
 native_impostor_pixels_uploaded_=native_impostor_pixels_;native_impostor_debug_uploaded_=native_impostor_debug_;
}

HRESULT Renderer::upload_native_instances(){
 PERF06_SCOPE("upload_instances");
 // Draw distance, or nearer where the last world frame's fog fully covers.
 float distance_cull=native_draw_distance_;
 if(fog_cull_distance_>0&&fog_cull_frame_+1>=native_frame_index_)distance_cull=(std::min)(distance_cull,fog_cull_distance_);
 if(distance_cull!=native_distance_cull_uploaded_||native_lod_error_px_!=native_lod_error_uploaded_||
    native_impostor_pixels_!=native_impostor_pixels_uploaded_||native_impostor_debug_!=native_impostor_debug_uploaded_)native_world_clip_dirty_=true;
 if(native_world_clip_dirty_){
  if(!native_world_clip_constants_){D3D11_BUFFER_DESC description{};description.ByteWidth=sizeof(native_world_clip_)*2+64;
   description.Usage=D3D11_USAGE_DYNAMIC;description.BindFlags=D3D11_BIND_CONSTANT_BUFFER;description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
   HRESULT hr=device_->CreateBuffer(&description,nullptr,&native_world_clip_constants_);if(FAILED(hr))return hr;}
  std::array<std::uint8_t,sizeof(native_world_clip_)*2+64> values;native_world_clip_values(values);
  D3D11_MAPPED_SUBRESOURCE mapped{};HRESULT hr=context_->Map(native_world_clip_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
  if(FAILED(hr))return hr;
  std::memcpy(mapped.pData,values.data(),values.size());
  context_->Unmap(native_world_clip_constants_.Get(),0);native_world_clip_dirty_=false;profiler_.add_upload(sizeof(native_world_clip_)*2);
 }
 if(native_instance_dirty_.empty())return S_OK;
 try{
  native_instance_updates_staging_.clear();
  for(UINT index:native_instance_dirty_){const auto& record=native_instance_records_[index];
   const UINT flags=(record.uploaded?0u:1u)|(record.descriptor_uploaded?0u:2u);
   if(record.references&&flags)native_instance_updates_staging_.push_back({index,0,flags,record.command.instance,record.descriptor});}
  if(native_instance_updates_staging_.empty())return S_OK;
  const UINT count=static_cast<UINT>(native_instance_updates_staging_.size());
  const UINT required=static_cast<UINT>(native_instance_records_.size());
  if(required>native_persistent_instance_capacity_){
   constexpr UINT limit=UINT_MAX/sizeof(NativeInstanceGpu);
   if(required>limit)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
   UINT capacity=static_cast<UINT>((std::min)(std::uint64_t(limit),
    (std::max)({std::uint64_t(required),std::uint64_t(native_persistent_instance_capacity_)*2,64ull})));
   ComPtr<ID3D11Buffer> buffer,draws;ComPtr<ID3D11ShaderResourceView> srv,draw_srv;ComPtr<ID3D11UnorderedAccessView> uav,draw_uav;
   const auto create_buffer=[&](UINT size,UINT stride,ComPtr<ID3D11Buffer>& target,
       ComPtr<ID3D11ShaderResourceView>& target_srv,ComPtr<ID3D11UnorderedAccessView>& target_uav){
    target.Reset();target_srv.Reset();target_uav.Reset();
    D3D11_BUFFER_DESC description{};description.ByteWidth=size*stride;description.Usage=D3D11_USAGE_DEFAULT;
    description.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;description.StructureByteStride=stride;
    HRESULT hr=device_->CreateBuffer(&description,nullptr,&target);if(FAILED(hr))return hr;
    if(FAILED(hr=device_->CreateShaderResourceView(target.Get(),nullptr,&target_srv)))return hr;
    return device_->CreateUnorderedAccessView(target.Get(),nullptr,&target_uav);};
   const auto create=[&](UINT size){
    HRESULT hr=create_buffer(size,sizeof(NativeInstanceGpu),buffer,srv,uav);if(FAILED(hr))return hr;
    return create_buffer(size,sizeof(NativeDrawGpu),draws,draw_srv,draw_uav);};
   HRESULT hr=create(capacity);if(hr==E_OUTOFMEMORY&&capacity!=required){capacity=required;hr=create(capacity);}if(FAILED(hr))return hr;
   if(native_persistent_instances_){D3D11_BOX box{};box.right=native_persistent_instance_capacity_*sizeof(NativeInstanceGpu);box.bottom=box.back=1;
    context_->CopySubresourceRegion(buffer.Get(),0,0,0,0,native_persistent_instances_.Get(),0,&box);}
   if(native_draws_){D3D11_BOX box{};box.right=native_persistent_instance_capacity_*sizeof(NativeDrawGpu);box.bottom=box.back=1;
    context_->CopySubresourceRegion(draws.Get(),0,0,0,0,native_draws_.Get(),0,&box);}
   native_persistent_instances_=std::move(buffer);native_persistent_instance_srv_=std::move(srv);
   native_persistent_instance_uav_=std::move(uav);native_persistent_instance_capacity_=capacity;
   native_draws_=std::move(draws);native_draw_srv_=std::move(draw_srv);native_draw_uav_=std::move(draw_uav);
  }
  UINT first=0;
  if(structured_ring_supported_){
   if(count>UINT_MAX/sizeof(NativeInstanceUpdate))return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
   const HRESULT hr=native_instance_update_ring_.write(device_.Get(),context_.Get(),native_instance_updates_staging_.data(),
    count*UINT(sizeof(NativeInstanceUpdate)),native_instance_update_srv_,first);
   if(FAILED(hr))return hr;
   native_instance_update_capacity_=UINT((std::min)(std::uint64_t(UINT_MAX),native_instance_update_ring_.capacity_bytes()/sizeof(NativeInstanceUpdate)));
  }else{
  if(count>native_instance_update_capacity_){
   constexpr UINT limit=UINT_MAX/sizeof(NativeInstanceUpdate);
   if(count>limit)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
   const UINT capacity=UINT((std::min)(std::uint64_t(limit),
    (std::max)({std::uint64_t(count),std::uint64_t(native_instance_update_capacity_)*2,64ull})));
   D3D11_BUFFER_DESC description{};description.ByteWidth=capacity*sizeof(NativeInstanceUpdate);description.Usage=D3D11_USAGE_DYNAMIC;
   description.BindFlags=D3D11_BIND_SHADER_RESOURCE;description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
   description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;description.StructureByteStride=sizeof(NativeInstanceUpdate);
   ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11ShaderResourceView> srv;
   HRESULT hr=device_->CreateBuffer(&description,nullptr,&buffer);if(FAILED(hr))return hr;
   if(FAILED(hr=device_->CreateShaderResourceView(buffer.Get(),nullptr,&srv)))return hr;
   native_instance_updates_=std::move(buffer);native_instance_update_srv_=std::move(srv);native_instance_update_capacity_=capacity;

  }
  D3D11_MAPPED_SUBRESOURCE mapped{};HRESULT hr=context_->Map(native_instance_updates_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
  if(FAILED(hr))return hr;std::memcpy(mapped.pData,native_instance_updates_staging_.data(),count*sizeof(NativeInstanceUpdate));context_->Unmap(native_instance_updates_.Get(),0);
  }
  {NativeDispatchRange range;range.first=first;range.count=count;
   const HRESULT hr=upload_native_work_range(range);if(FAILED(hr))return hr;}
  profiler_.add_upload(count*sizeof(NativeInstanceUpdate));
  ID3D11ShaderResourceView* empty=nullptr;context_->CSSetShaderResources(13,1,&empty);context_->VSSetShaderResources(13,1,&empty);
  context_->CSSetShaderResources(5,1,&empty);
  context_->CSSetShaderResources(14,1,native_instance_update_srv_.GetAddressOf());
  context_->CSSetUnorderedAccessViews(5,1,native_persistent_instance_uav_.GetAddressOf(),nullptr);
  context_->CSSetUnorderedAccessViews(7,1,native_draw_uav_.GetAddressOf(),nullptr);
  context_->CSSetShader(native_instance_update_cs_.Get(),nullptr,0);
  const UINT groups=(count+63)/64,width=(std::min)(groups,65535u),height=(groups+width-1)/width;
  transition_profile_pass("compute_instances");context_->Dispatch(width,height,1);++perf_native_compute_dispatches_;++native_frame_stats_.native_compute_dispatches;
  ID3D11UnorderedAccessView* empty_uav=nullptr;context_->CSSetUnorderedAccessViews(5,1,&empty_uav,nullptr);
  context_->CSSetUnorderedAccessViews(7,1,&empty_uav,nullptr);
  context_->CSSetShaderResources(14,1,&empty);context_->CSSetShader(nullptr,nullptr,0);
  for(const auto& update:native_instance_updates_staging_){
   auto& record=native_instance_records_[update.destination];
   if(update.flags&1u)record.uploaded=true;
   if(update.flags&2u)record.descriptor_uploaded=true;
#ifdef EID3D11_TESTING
   if(update.flags&1u)++test_native_instance_rows_uploaded_;
   if(update.flags&2u)++test_native_descriptors_uploaded_;
#endif
  }
  // Completed rows must be rescheduled on the next mutation, not scanned by
  // every subsequent ordered flush. Keep unreferenced work for discard cleanup.
  native_instance_dirty_.erase(std::remove_if(native_instance_dirty_.begin(),native_instance_dirty_.end(),
   [&](UINT index){auto& record=native_instance_records_[index];
    if(!record.references||!record.uploaded||!record.descriptor_uploaded)return false;
    record.scheduled=false;return true;}),native_instance_dirty_.end());
  return S_OK;
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
}

void Renderer::retire_native_poses() noexcept{
 release_native_lod_observer();
 native_instance_update_cs_.Reset();native_persistent_instances_.Reset();native_persistent_instance_srv_.Reset();native_persistent_instance_uav_.Reset();
 native_instance_updates_.Reset();native_instance_update_srv_.Reset();native_world_clip_constants_.Reset();
 native_instance_update_ring_.release();native_pose_input_ring_.release();
 native_draws_.Reset();native_draw_srv_.Reset();native_draw_uav_.Reset();
 neighbour_client_stop();release_neighbour_maps();impostors_={};
 native_instance_records_.clear();native_static_shadow_records_.clear();
 native_static_shadow_bake_={}; // its records were cleared above
 native_terrain_shadow_bake_={};native_neighbour_shadow_bake_={};native_terrain_shadow_records_.clear();
 native_submission_layouts_[5].static_bake.set_revision=0;
 for(auto* set:{&native_static_set_,&native_terrain_set_}){set->objects.clear();set->draws.clear();set->owners.clear();set->added.clear();set->added_draws=0;set->dirty=false;}
 if(++native_static_generation_==0)++native_static_generation_;native_static_content_changed();
 native_instance_free_.clear();native_instance_dirty_.clear();native_instance_updates_staging_.clear();
 native_state_buckets_.clear();native_state_lookup_.clear();native_material_groups_dirty_=false;native_active_groups_.clear();
 native_persistent_instance_capacity_=native_instance_update_capacity_=0;native_world_clip_valid_=native_world_clip_dirty_=false;
 native_pose_work_.clear();native_pose_upload_.clear();native_pose_slots_.clear();native_skeleton_assets_.clear();
 native_pose_ranges_.clear();native_world_ranges_.clear();native_animation_ranges_.clear();
 for(auto& free_ranges:native_free_ranges_)free_ranges.clear();native_free_ranges_complete_.fill(true);
 native_world_states_.Reset();native_world_state_srv_.Reset();native_world_state_uav_.Reset();native_world_state_capacity_=0;
 native12_.world_states.reset(); // D3D12: the states go with their ranges (release_world_state)
 native_pose_inputs_.Reset();native_pose_input_srv_.Reset();native_pose_nodes_.Reset();native_pose_node_srv_.Reset();native_pose_node_uav_.Reset();
 native_animation_.Reset();native_animation_srv_.Reset();native_pose_input_capacity_=native_pose_node_capacity_=native_pose_node_used_=0;
 native_animation_capacity_=0;native_animation_used_=16;
 native_animation_uav_.Reset();native_terrain_phase_cs_.Reset();
 native_terrain_phases_.clear();native_terrain_phase_lookup_.clear();native_terrain_phase_free_.clear();native_terrain_phase_work_.clear();native_terrain_phase_copies_.clear();
}

HRESULT Renderer::create_native_world_resource(UINT capacity,ComPtr<ID3D11Buffer>& buffer,
    ComPtr<ID3D11ShaderResourceView>& srv,ComPtr<ID3D11UnorderedAccessView>& uav){
 const std::uint64_t bytes=std::uint64_t(capacity)*sizeof(NativeWorldVertexGpu);
 if(!capacity||bytes>UINT_MAX)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 if(!device_||!context_)return E_FAIL;
 D3D11_BUFFER_DESC description{};
 description.ByteWidth=static_cast<UINT>(bytes);
 description.Usage=D3D11_USAGE_DEFAULT;
 description.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
 description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
 description.StructureByteStride=sizeof(NativeWorldVertexGpu);
 #ifdef EID3D11_TESTING
 ++test_native_world_allocation_attempts_;
 if(test_native_world_allocation_failure_)return E_OUTOFMEMORY;
 #endif
 HRESULT hr=device_->CreateBuffer(&description,nullptr,&buffer);
 if(FAILED(hr))return hr;
 D3D11_SHADER_RESOURCE_VIEW_DESC sr{};sr.Format=DXGI_FORMAT_UNKNOWN;
 sr.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;sr.Buffer.NumElements=capacity;
 if(FAILED(hr=device_->CreateShaderResourceView(buffer.Get(),&sr,&srv)))return hr;
 D3D11_UNORDERED_ACCESS_VIEW_DESC ur{};ur.Format=DXGI_FORMAT_UNKNOWN;
 ur.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ur.Buffer.NumElements=capacity;
 return device_->CreateUnorderedAccessView(buffer.Get(),&ur,&uav);
}

HRESULT Renderer::ensure_native_world_buffer(UINT required){
 constexpr UINT addressable=UINT_MAX/sizeof(NativeWorldVertexGpu);
 if(!required)return S_OK;
 if(required>addressable)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 if(d3d12_){ // D3D12 backend: the same growth on the executor's buffer
  if(native12_.world_vertices&&required<=native_world_vertex_capacity_)return S_OK;
  const UINT capacity=grow_capacity(required,64u*1024u,addressable);
  const HRESULT hr=native_d3d12_persistent(native12_.world_vertices,UINT64(capacity)*sizeof(NativeWorldVertexGpu),sizeof(NativeWorldVertexGpu),
   UINT64(native_world_vertex_used_)*sizeof(NativeWorldVertexGpu));
  if(SUCCEEDED(hr))native_world_vertex_capacity_=capacity;
  return hr;
 }
 if(!device_||!context_)return E_FAIL;
 if(native_world_vertices_&&required<=native_world_vertex_capacity_)return S_OK;
 UINT capacity=grow_capacity(required,64u*1024u,addressable);
 ComPtr<ID3D11Buffer> replacement;
 ComPtr<ID3D11ShaderResourceView> srv;
 ComPtr<ID3D11UnorderedAccessView> uav;
 HRESULT hr=create_native_world_resource(capacity,replacement,srv,uav);
 if(hr==E_OUTOFMEMORY&&capacity!=required){
  // Growth headroom is optional; a real device failure may still permit the
  // exact required size. There is no software VRAM budget here.
  replacement.Reset();srv.Reset();uav.Reset();capacity=required;
  hr=create_native_world_resource(capacity,replacement,srv,uav);
 }
 if(FAILED(hr))return hr;
 if(native_world_vertices_&&native_world_vertex_used_){
  D3D11_BOX box{};box.right=native_world_vertex_used_*sizeof(NativeWorldVertexGpu);
  box.bottom=box.back=1;
  context_->CopySubresourceRegion(replacement.Get(),0,0,0,0,native_world_vertices_.Get(),0,&box);
 }
 native_world_vertices_=std::move(replacement);native_world_vertex_srv_=std::move(srv);
 native_world_vertex_uav_=std::move(uav);native_world_vertex_capacity_=capacity;
 return S_OK;
}

HRESULT Renderer::allocate_native_range(std::vector<NativeGpuRange>& ranges,UINT count,
                                       NativeRangeBuffer buffer,UINT& allocation){
 if(!count)return E_INVALIDARG;
 // Best fit preserves active addresses. The allocation keeps any small spare
 // tail until its next semantic reuse; no live vertex or node data is moved.
 UINT best=UINT_MAX;
 const std::size_t kind=static_cast<std::size_t>(buffer);
 auto& free_ranges=native_free_ranges_[kind];
 if(native_free_ranges_complete_[kind]){
  for(auto entry=free_ranges.lower_bound({count,0u});entry!=free_ranges.end();){
   const UINT i=entry->second;
   const bool valid=i<ranges.size()&&!ranges[i].owned&&!ranges[i].references&&ranges[i].count==entry->first;
   entry=free_ranges.erase(entry);
   if(valid){best=i;break;}
  }
 }else{
  for(UINT i=0;i<ranges.size();++i){const auto& range=ranges[i];
   if(!range.owned&&!range.references&&range.count>=count&&
      (best==UINT_MAX||range.count<ranges[best].count))best=i;
  }
 }
 if(best!=UINT_MAX){ranges[best].owned=true;if(++ranges[best].generation==0)++ranges[best].generation;
  allocation=best;return S_OK;}
 auto& used=buffer==NativeRangeBuffer::pose?native_pose_node_used_:
            (buffer==NativeRangeBuffer::world?native_world_vertex_used_:native_animation_used_);
 const UINT stride=buffer==NativeRangeBuffer::pose?sizeof(NativePoseNodeGpu):
                   (buffer==NativeRangeBuffer::world?sizeof(NativeWorldVertexGpu):1u);
 const std::uint64_t required=std::uint64_t(used)+count;
 if(required>UINT_MAX/stride)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 try{if(ranges.size()==ranges.capacity())ranges.reserve((std::max)(ranges.size()+1,ranges.capacity()*2));}
 catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 const HRESULT hr=buffer==NativeRangeBuffer::pose?ensure_native_pose_nodes(static_cast<UINT>(required)):
  (buffer==NativeRangeBuffer::world?ensure_native_world_buffer(static_cast<UINT>(required)):
                                   ensure_native_animation_buffer(static_cast<UINT>(required)));
 if(FAILED(hr))return hr;
 allocation=static_cast<UINT>(ranges.size());ranges.push_back({used,count,0,true});
 used=static_cast<UINT>(required);return S_OK;
}

void Renderer::native_range_freed(std::vector<NativeGpuRange>& ranges,UINT index) noexcept{
 if(index>=ranges.size()||ranges[index].owned||ranges[index].references)return;
 const std::size_t kind=&ranges==&native_world_ranges_?static_cast<std::size_t>(NativeRangeBuffer::world):
  &ranges==&native_pose_ranges_?static_cast<std::size_t>(NativeRangeBuffer::pose):
  static_cast<std::size_t>(NativeRangeBuffer::animation);
 if(!native_free_ranges_complete_[kind])return;
 try{native_free_ranges_[kind].insert({ranges[index].count,index});}
 catch(...){native_free_ranges_complete_[kind]=false;native_free_ranges_[kind].clear();}
}

HRESULT Renderer::ensure_native_world_states(){
 if(native_world_ranges_.size()<=native_world_state_capacity_)return S_OK;
 if(native_world_ranges_.size()>UINT_MAX/sizeof(NativeWorldStateGpu))return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 try{
  const UINT capacity=static_cast<UINT>((std::min)(std::uint64_t(UINT_MAX/sizeof(NativeWorldStateGpu)),
   (std::max)({std::uint64_t(native_world_ranges_.size()),std::uint64_t(native_world_state_capacity_)*2,64ull})));
  if(d3d12_){ // D3D12 backend: committed buffers start zeroed; the old states are kept
   const HRESULT hr=native_d3d12_persistent(native12_.world_states,UINT64(capacity)*sizeof(NativeWorldStateGpu),sizeof(NativeWorldStateGpu),
    UINT64(native_world_state_capacity_)*sizeof(NativeWorldStateGpu));
   if(SUCCEEDED(hr))native_world_state_capacity_=capacity;
   return hr;
  }
  std::vector<NativeWorldStateGpu> zero(capacity);
  D3D11_BUFFER_DESC description{};description.ByteWidth=capacity*sizeof(NativeWorldStateGpu);description.Usage=D3D11_USAGE_DEFAULT;
  description.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
  description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;description.StructureByteStride=sizeof(NativeWorldStateGpu);
  D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=zero.data();
  ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11UnorderedAccessView> uav;
  HRESULT hr=device_->CreateBuffer(&description,&initial,&buffer);if(FAILED(hr))return hr;
  if(FAILED(hr=device_->CreateShaderResourceView(buffer.Get(),nullptr,&srv)))return hr;
  if(FAILED(hr=device_->CreateUnorderedAccessView(buffer.Get(),nullptr,&uav)))return hr;
  if(native_world_states_){D3D11_BOX box{};box.right=native_world_state_capacity_*sizeof(NativeWorldStateGpu);box.bottom=box.back=1;
   context_->CopySubresourceRegion(buffer.Get(),0,0,0,0,native_world_states_.Get(),0,&box);}
  native_world_states_=std::move(buffer);native_world_state_srv_=std::move(srv);
  native_world_state_uav_=std::move(uav);native_world_state_capacity_=capacity;return S_OK;
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
}

void Renderer::native_world_draw_values(const NativeQueuedDraw& source,NativeDrawGpu& target) const noexcept{
 if(source.world_allocation>=native_world_ranges_.size())return;
 const auto& range=native_world_ranges_[source.world_allocation];
 target.world_state=source.world_allocation;target.world_generation_low=static_cast<UINT>(range.generation);
 target.world_generation_high=static_cast<UINT>(range.generation>>32);target.geometry_radius=source.geometry_radius;
}

void Renderer::begin_native_preparations() noexcept{
 native_preparation_count_=0;
 if(++native_preparation_epoch_==0){
  for(auto& range:native_world_ranges_)range.preparation_epoch=0;
  ++native_preparation_epoch_;
 }
}

void Renderer::native_layout_changed() noexcept{
 if(++native_layout_revision_==0){
  native_layout_revision_=1;for(auto& layout:native_submission_layouts_)layout.source_revision=0;
 }
}

HRESULT Renderer::prepare_native_submission_layout(bool shadows,unsigned slot){
 auto& layout=native_submission_layouts_[slot];
 std::size_t prefix=0;
#ifdef EID3D11_TESTING
 prefix=test_native_work_prefix_[shadows?1:0];
#endif
 if(prefix>layout.work.max_size()-native_draw_order_.size())return E_OUTOFMEMORY;
 constexpr UINT maximum_work=(std::min)(kNativeRawElementLimit,
  UINT_MAX/static_cast<UINT>(sizeof(NativeDrawWorkGpu)));
 if(prefix>maximum_work||native_draw_order_.size()>maximum_work-prefix)
  return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 bool same=layout.source_revision==native_layout_revision_&&
  layout.source_draws==native_draw_order_&&
  layout.source_records.size()==native_draw_order_.size();
 if(same)for(std::size_t index=0;index<native_draw_order_.size();++index){
  const auto pending=native_draw_order_[index];
  if(pending>=native_pending_draws_.size()||
     layout.source_records[index]!=native_pending_draws_[pending].record){same=false;break;}
 }
 if(same)return S_OK;
 layout.source_revision=0;layout.work.clear();layout.work_draws.clear();
 layout.source_draws.clear();layout.source_records.clear();
 layout.dispatches.clear();layout.geometry_dispatches.clear();
 layout.batches.clear();layout.arguments.clear();
 std::uint64_t triangles=0,components=0;
 try{
  struct PendingWork {NativeDrawWorkGpu value{};std::size_t draw=0;
   UINT geometry=UINT_MAX;};
  std::vector<PendingWork> pending;pending.reserve(native_draw_order_.size());
  UINT split_batches=0;
  for(std::size_t first=0;first<native_draw_order_.size();){
   constexpr UINT maximum_batches=kNativeRawElementLimit/4u;
   if(layout.batches.size()>=maximum_batches)
    return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
   const auto& draw=native_queued_draw(native_draw_order_[first]);
   const auto supports_order=[](const NativeQueuedDraw& item){
    return item.preserve_order&&(!item.instance.lighting_enabled||item.instance.world_geometry);
   };
   NativeBatch batch;batch.first_draw=first;batch.state_bucket=draw.state_bucket;
   batch.foliage=(draw.instance.flags&native_foliage_part_flag)!=0;
   batch.geometry_page=draw.geometry_page;
   batch.preserves_order=!shadows&&native_state_buckets_[draw.state_bucket].semantic_kind==SemanticKind::figure&&supports_order(draw);
   std::size_t last=first+1;
   if(shadows||draw.reorderable||batch.preserves_order)
    while(last<native_draw_order_.size()&&last-first<4096){
     const auto& candidate=native_queued_draw(native_draw_order_[last]);
     if(candidate.state_bucket!=draw.state_bucket||!(candidate.camp_clip==draw.camp_clip)||candidate.geometry_page!=draw.geometry_page||
        !widescreen::same_composition(candidate.hud_composition,draw.hud_composition)||
        (shadows&&native_far_foliage_opaque_&&
         ((candidate.instance.flags&native_foliage_part_flag)!=0)!=batch.foliage)||
        (!shadows&&(draw.reorderable?!candidate.reorderable:!supports_order(candidate))))break;
     ++last;
    }
   batch.draw_count=last-first;batch.output_triangle_base=static_cast<UINT>(triangles);
   const UINT group=static_cast<UINT>(layout.batches.size());
   // Terrain tile classes: the batch's steep-class triangles (each mesh's
   // from steep_first_triangle on) take a region after its flat ones and an
   // arguments slot after all batch slots (numbered below, once the batch
   // count is known), so the flush draws them with the steep variant.
   UINT steep_triangles=0,batch_triangles=0;
   if(!shadows&&!batch.preserves_order&&native_state_buckets_[draw.state_bucket].semantic_kind==SemanticKind::terrain){
    batch.flat_pixels=true;
    for(std::size_t i=first;i<last;++i){
     const auto& queued=native_queued_draw(native_draw_order_[i]);
     const UINT count=queued.index_count/3u;batch_triangles+=count;
     const UINT steep_first=queued.mesh?queued.mesh->steep_first_triangle:UINT_MAX;
     if(steep_first==UINT_MAX)batch.flat_pixels=false;
     else if(steep_first<count)steep_triangles+=count-steep_first;
    }
   }
   // A batch with an unclassified draw keeps one region and the full variant.
   if(!batch.flat_pixels)steep_triangles=0;
   // Ground contact: terrain never takes it; other batches only when a draw
   // can reach the contact band (the flush then binds the contact variant).
   if(!shadows){
    const auto& kind=native_state_buckets_[draw.state_bucket];
    for(std::size_t i=first;!batch.posed&&i<last;++i)batch.posed=native_queued_draw(native_draw_order_[i]).instance.pose_node!=UINT_MAX;
    batch.contact=false;
    for(std::size_t i=first;kind.semantic_kind!=SemanticKind::terrain&&!batch.contact&&i<last;++i)
     batch.contact=native_draw_may_contact(native_queued_draw(native_draw_order_[i]),kind.material_key.contact_map);
   }
   if(steep_triangles){batch.steep_args_slot=split_batches++;batch.steep_triangle_base=batch.output_triangle_base+(batch_triangles-steep_triangles);}
   for(std::size_t i=first;i<last;++i){
    if(prefix+pending.size()>=maximum_work)
     return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    const auto& queued=native_queued_draw(native_draw_order_[i]);
    if(shadows&&(!queued.instance.world_geometry||!queued.mesh))return E_INVALIDARG;
    NativeDrawWorkGpu work;work.record=native_pending_draws_[native_draw_order_[i]].record;
    work.output_triangle_base=batch.output_triangle_base;work.indirect_args_byte_offset=group*16u;
    work.ordered_triangle_offset=static_cast<UINT>(triangles-batch.output_triangle_base);
    work.flags=batch.preserves_order?1u:0u;
    work.prepared_vertex_base=queued.instance.world_geometry?queued.world_base:static_cast<UINT>(components);
    if(batch.steep_args_slot!=UINT_MAX){work.steep_output_triangle_base=batch.steep_triangle_base;work.steep_args_byte_offset=batch.steep_args_slot;}
    pending.push_back({work,native_draw_order_[i],queued.geometry_page});
    // A static LOD record may emit two levels in its crossfade band (at most
    // the full list and the first reduced level).
    triangles+=queued.index_count/3u+((queued.instance.world_geometry&native_world_lod)?queued.mesh->lod_extra_triangles:0u)+
     ((queued.instance.world_geometry&native_world_impostor)?2u:0u); // a crown's impostor quad
    if(!queued.instance.world_geometry)components+=queued.component_count;
    if(triangles>16u*1024u*1024u||components>8u*1024u*1024u)return E_OUTOFMEMORY;
   }
   layout.arguments.insert(layout.arguments.end(),
    {batch.preserves_order?static_cast<UINT>((triangles-batch.output_triangle_base)*3u):0u,1u,0u,0u});
   layout.batches.push_back(batch);first=last;
  }
  // The split batches' steep arguments slots follow every batch's own slot
  // (batch_index*16 keeps addressing a batch's first slot).
  if(split_batches){
   const UINT batch_slots=static_cast<UINT>(layout.batches.size());
   for(auto& item:pending)if(item.value.steep_args_byte_offset!=UINT_MAX)item.value.steep_args_byte_offset=(batch_slots+item.value.steep_args_byte_offset)*16u;
   for(auto& batch:layout.batches)if(batch.steep_args_slot!=UINT_MAX){
    batch.steep_args_slot+=batch_slots;layout.arguments.insert(layout.arguments.end(),{0u,1u,0u,0u});
   }
  }
  std::stable_sort(pending.begin(),pending.end(),[](const PendingWork& left,
                                                   const PendingWork& right){
   return left.geometry<right.geometry;
  });
  layout.work.reserve(prefix+pending.size());layout.work_draws.reserve(prefix+pending.size());
  if(prefix){NativeDrawWorkGpu padding{};padding.record=UINT_MAX;
   layout.work.resize(prefix,padding);layout.work_draws.resize(prefix,SIZE_MAX);}
  for(const auto& item:pending){
   const UINT index=static_cast<UINT>(layout.work.size());
   layout.work.push_back(item.value);layout.work_draws.push_back(item.draw);
   if(layout.dispatches.empty()||layout.dispatches.back().geometry_page!=item.geometry)
    layout.dispatches.push_back({index,1,item.geometry,0});
   else ++layout.dispatches.back().count;
   if(layout.geometry_dispatches.empty()||
      layout.geometry_dispatches.back().geometry_page!=item.geometry)
    layout.geometry_dispatches.push_back({index,1,item.geometry,0});
   else ++layout.geometry_dispatches.back().count;
  }
  layout.source_draws=native_draw_order_;
  layout.source_records.reserve(native_draw_order_.size());
  for(const auto pending_index:native_draw_order_)
   layout.source_records.push_back(native_pending_draws_[pending_index].record);
  layout.triangles=static_cast<UINT>(triangles);layout.components=static_cast<UINT>(components);
  layout.source_revision=native_layout_revision_;
#ifdef EID3D11_TESTING
  ++test_native_layout_rebuilds_;
#endif
  return S_OK;
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
}

// Dispatch depth (z) for a shadow compute range: chunks of the largest
// record's components (preparation) or triangles (build).
UINT Renderer::native_shadow_chunks(const NativeSubmissionLayout& layout,const NativeDispatchRange& range,bool triangles) const noexcept{
 constexpr UINT chunk=512;UINT largest=1;
 for(UINT index=range.first;index<range.first+range.count&&index<layout.work.size();++index){
  const auto record=layout.work[index].record;if(record>=native_instance_records_.size())continue;
  const auto& draw=native_instance_records_[record].command;
  largest=(std::max)(largest,triangles?draw.index_count/3u:draw.component_count);
 }
 return (std::min)(65535u,(largest+chunk-1)/chunk);
}

void Renderer::refresh_native_draw_work(NativeSubmissionLayout& layout,bool shadows){
 begin_native_preparations();
 for(auto& range:layout.dispatches)range.preparations=0;
 std::size_t dispatch_index=0;
 for(std::size_t index=0;index<layout.work.size();++index){
  auto& work=layout.work[index];work.work_count=0;
  if(work.record==UINT_MAX)continue;
  if(index>=layout.work_draws.size()||layout.work_draws[index]>=native_pending_draws_.size())continue;
  const std::size_t draw_index=layout.work_draws[index];
  const auto& draw=native_instance_records_[work.record].command;
  const UINT mask=shadows?native_pending_draws_[draw_index].passes&(UINT(native_shadow_dirty_mask_)<<8):256u;
  const UINT previous_mask=work.flags&0xff00u;
  if(shadows&&previous_mask!=mask){
   auto& batch=layout.batches[work.indirect_args_byte_offset/16u];
   for(UINT slice=0;slice<6;++slice)if((previous_mask^mask)&(256u<<slice)){
    if(mask&(256u<<slice)){batch.slice_triangles[slice]+=draw.index_count/3u;++batch.slice_records[slice];}
    else {batch.slice_triangles[slice]-=draw.index_count/3u;--batch.slice_records[slice];}
   }
  }
  work.flags=(work.flags&1u)|mask;
  if(draw.instance.world_geometry)work.prepared_vertex_base=draw.world_base;
  if(draw.prepare_world&&draw.world_allocation<native_world_ranges_.size()){
   auto& range=native_world_ranges_[draw.world_allocation];
   if(range.preparation_epoch!=native_preparation_epoch_){
   range.preparation_epoch=native_preparation_epoch_;range.preparation_work=static_cast<UINT>(index);
    work.flags|=2u|(mask<<8);++native_preparation_count_;
   }else layout.work[range.preparation_work].flags|=mask<<8;
  }
  while(dispatch_index+1<layout.dispatches.size()&&
        index>=layout.dispatches[dispatch_index].first+layout.dispatches[dispatch_index].count)
   ++dispatch_index;
  if((work.flags&2u)&&dispatch_index<layout.dispatches.size()&&
     index>=layout.dispatches[dispatch_index].first)
   ++layout.dispatches[dispatch_index].preparations;
 }
}

void Renderer::retire_native_world_range(NativePreparedMeshSlot& slot) noexcept{
 if(slot.world_allocation<native_world_ranges_.size()){
  native_world_ranges_[slot.world_allocation].owned=false;native_world_reclaim_pending_=true;
  native_range_freed(native_world_ranges_,slot.world_allocation);
 }
 slot.world_allocation=UINT_MAX;slot.world_valid=false;slot.world_base=UINT_MAX;slot.world_count=0;
}

void Renderer::retire_native_source_values() noexcept{
 for(std::size_t index=0;index<native_prepared_mesh_slots_.size();++index){
  auto& slot=native_prepared_mesh_slots_[index];if(slot.active&&slot.source_revision&&slot.owner.expired()){
  if(slot.mesh&&slot.mesh->owned_references)--slot.mesh->owned_references;
  ++native_released_mesh_slots_;
  retire_native_world_range(slot);slot.active=false;slot.mesh=nullptr;slot.owner.reset();
  if(++slot.generation==0)++slot.generation;
  native_prepared_mesh_reuse_cursor_=std::min(native_prepared_mesh_reuse_cursor_,index);
 }
 }
 for(std::size_t index=0;index<native_prepared_material_slots_.size();++index){
  auto& slot=native_prepared_material_slots_[index];if(slot.active&&slot.persistent&&slot.owner.expired()){
  retire_native_material_instances(slot);slot.active=false;slot.owner.reset();
  if(++slot.generation==0)++slot.generation;
  native_prepared_material_reuse_cursor_=std::min(native_prepared_material_reuse_cursor_,index);
 }
 }
 for(auto& slot:native_pose_slots_)if(slot.active&&slot.owner.expired()){
  if(slot.allocation<native_pose_ranges_.size()){native_pose_ranges_[slot.allocation].owned=false;
   native_range_freed(native_pose_ranges_,slot.allocation);}
  if(slot.animation_allocation<native_animation_ranges_.size()&&native_animation_ranges_[slot.animation_allocation].owners)
   --native_animation_ranges_[slot.animation_allocation].owners;
  slot.animation_allocation=UINT_MAX;
  slot.active=false;slot.valid=false;slot.count=0;slot.allocation=UINT_MAX;slot.binding.reset();
  if(++slot.generation==0)++slot.generation;
 }
 for(UINT index=0;index<native_terrain_phases_.size();++index){
  auto& slot=native_terrain_phases_[index];
  if(!slot.active||!slot.owner.expired())continue;
  if(slot.allocation<native_animation_ranges_.size()){native_animation_ranges_[slot.allocation].owned=false;
   native_range_freed(native_animation_ranges_,slot.allocation);}
  slot.active=slot.valid=slot.scheduled=false;slot.allocation=UINT_MAX;slot.count=0;slot.revision=0;
  native_terrain_phase_lookup_.erase(slot.key);
  // The free list never outgrows the slot table it indexes.
  try{native_terrain_phase_free_.push_back(index);}catch(const std::bad_alloc&){}
 }
 for(auto iterator=native_skeleton_assets_.begin();iterator!=native_skeleton_assets_.end();){
  const auto& asset=iterator->second;auto& range=native_animation_ranges_[asset.allocation];
  if(!range.owners&&!range.references&&(asset.model.expired()||(asset.animated&&asset.clip.expired()))){
   range.owned=false;native_range_freed(native_animation_ranges_,asset.allocation);
   iterator=native_skeleton_assets_.erase(iterator);
  }else ++iterator;
 }
 trim_native_material_groups();
}

HRESULT Renderer::reclaim_native_world_ranges(){
 if(!native_pending_draws_.empty())return E_UNEXPECTED;
 retire_native_source_values();
 // Retired entries are immediately eligible for best-fit reuse. Keeping the
 // high-water buffer avoids a full allocation/copy merely because the camera
 // or a material pass temporarily did not select a live source.
 native_world_reclaim_pending_=false;
 return S_OK;
}

HRESULT Renderer::resolve_native_world_range(const NativeGeometryView& view,
                                            NativeQueuedDraw& draw){
 if(!view.world.enabled)return S_OK;
 if(!view.contribution.known||!view.prepared_mesh||
    view.prepared_mesh->slot>=native_prepared_mesh_slots_.size()||view.morph_count)return E_INVALIDARG;
 if(view.world.pose&&(!std::isfinite(view.world.geometry_radius)||view.world.geometry_radius<0))return E_INVALIDARG;
 const UINT slot_index=view.prepared_mesh->slot;
 auto& slot=native_prepared_mesh_slots_[slot_index];
 if(!slot.active||slot.generation!=view.prepared_mesh->generation)return E_INVALIDARG;
 // Object space: the vertex stage evaluates the shared mesh with the record's
 // transform and shape, so the record owns no world vertices. Rigid colour-only
 // sources: no pose, morph or terrain channels, and no shadow caster (the
 // depth records key on world allocations).
 if(view.world.object_space){
  if(view.world.pose||(view.world.shape_count!=1&&view.world.shape_count!=8)||
     (view.decoded_channels&native_decoded_terrain)||view.contribution.light_mask)return E_INVALIDARG;
  draw.world_slot=UINT_MAX;draw.world_base=0;draw.world_allocation=UINT_MAX;draw.world_generation=0;
  draw.geometry_radius=view.world.geometry_radius;draw.prepare_world=false;
  return S_OK;
 }
 NativePreparedMeshSlot::DeformationKey key{};
 std::copy(view.world.object_to_world.begin(),view.world.object_to_world.end(),key.values.begin());
 if(view.world.pose)std::fill(key.values.begin(),key.values.begin()+16,0.f);
 std::copy(view.world.shape.begin(),view.world.shape.end(),key.values.begin()+16);
 key.values[19]=view.terrain.wind_direction[0];key.values[20]=view.terrain.wind_direction[1];
 key.values[21]=view.terrain.wind_force;key.values[22]=view.terrain.wind_phase;
 key.values[23]=view.terrain.precise_time;key.wave_flags=view.terrain.waves_enabled?1u:0u;
 if(!key.wave_flags)std::fill(key.values.begin()+19,key.values.end(),0.0f);
 for(float value:key.values)if(!std::isfinite(value))return E_INVALIDARG;
 for(float value:view.world.world_to_clip)if(!std::isfinite(value))return E_INVALIDARG;
 key.pose_slot=draw.pose_slot;key.pose_allocation=draw.pose_allocation;
 // Layer vertices are lifted by the live shifts while their world vertices
 // are prepared; a level step re-prepares them on the GPU (no rebuild).
 if(view.semantic_kind==SemanticKind::terrain)key.layer_shift_revision=native_terrain_layer_shift_revision_;
 const bool same=slot.world_base!=UINT_MAX&&slot.world_count==view.component_count&&slot.world_key==key;
 if(!same){
  if(slot.world_allocation>=native_world_ranges_.size()||slot.world_count!=view.component_count||
     native_world_ranges_[slot.world_allocation].references){
   UINT allocation=UINT_MAX;
   const HRESULT hr=allocate_native_range(native_world_ranges_,view.component_count,NativeRangeBuffer::world,allocation);
   if(FAILED(hr))return hr;
   retire_native_world_range(slot);slot.world_allocation=allocation;
   slot.world_base=native_world_ranges_[allocation].base;slot.world_count=view.component_count;
  }
  slot.world_key=std::move(key);slot.world_valid=false;
  if(++native_world_ranges_[slot.world_allocation].generation==0)++native_world_ranges_[slot.world_allocation].generation;
  if(++slot.world_generation==0)++slot.world_generation;
 }
 draw.world_slot=slot_index;draw.world_base=slot.world_base;
 draw.world_allocation=slot.world_allocation;
 draw.world_generation=slot.world_generation;draw.geometry_radius=view.world.geometry_radius;
 // The CPU knows that work was dispatched, not whether the GPU contribution
 // test admitted it. GPU range generations decide whether vertices are needed.
 draw.prepare_world=true;
 if(slot.world_valid)++native_world_reused_records_;
 return S_OK;
}

bool Renderer::reject_native_contribution(const NativeGeometryView& view,
                                          NativeGeometryResult* result) noexcept{
 if(!view.contribution.known||view.contribution.color||view.contribution.light_mask)return false;
 if(result){*result={};result->visible=false;result->screen_extents_valid=(view.flags&1u)!=0;}
 ++native_contribution_rejects_;return true;
}

void Renderer::release_native_queued_draw(UINT index,bool mesh_pending) noexcept{
 auto& draw=native_instance_records_[index].command;
 if(draw.animation_allocation<native_animation_ranges_.size()){
  auto& range=native_animation_ranges_[draw.animation_allocation];if(range.references)--range.references;
  native_range_freed(native_animation_ranges_,draw.animation_allocation);
 }
 if(draw.pose_allocation<native_pose_ranges_.size()){
  auto& range=native_pose_ranges_[draw.pose_allocation];if(range.references)--range.references;
  native_range_freed(native_pose_ranges_,draw.pose_allocation);
 }
 if(mesh_pending&&draw.mesh&&draw.mesh->pending_references)--draw.mesh->pending_references;
 if(draw.world_allocation<native_world_ranges_.size()){
  auto& range=native_world_ranges_[draw.world_allocation];if(range.references)--range.references;
  native_range_freed(native_world_ranges_,draw.world_allocation);
 }
 release_native_instance(index);
}
namespace {

// The game's per-object entries into the renderer, between passes: their CPU
// time per frame (RenderProfiler::add_cpu_time).
struct ProfileCpuTimer{
 RenderProfiler& profiler;const char* name;std::int64_t begin;
 ProfileCpuTimer(RenderProfiler& p,const char* n) noexcept:profiler(p),name(n),begin(p.enabled()?perf_now():0){}
 ~ProfileCpuTimer(){if(begin)profiler.add_cpu_time(name,perf_now()-begin);}
};
}  // namespace
HRESULT Renderer::queue_native(const NativeGeometryView& view,const FixedState& st,SurfaceImage* texture,NativeGeometryResult* result){SCENE_ENTRY("queue_native");
 const ProfileCpuTimer timer(profiler_,"queue_native");
 ++native_generic_ingress_calls_;
 if(view.prepared_identity.valid)
  ++native_frame_stats_.native_object_generic_ingress_calls;
 return queue_native_impl(view,st,texture,result,false);
}

HRESULT Renderer::queue_native_direct(const NativeGeometryView& view,const FixedState& st,
 ID3D11ShaderResourceView* texture,NativeGeometryResult* result){
 if(texture){ComPtr<ID3D11Device> actual;texture->GetDevice(&actual);if(actual.Get()!=device_.Get())return E_INVALIDARG;}
 return queue_native_impl(view,st,nullptr,result,false,texture,
  static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texture)),1,false,0,nullptr);
}

HRESULT Renderer::queue_native_owned(
 const NativeGeometryView& view,const FixedState& st,
 NativeGeometryResult* result){SCENE_ENTRY("queue_native_owned");
 const ProfileCpuTimer timer(profiler_,"queue_native_owned");
 if(view.semantic_kind==SemanticKind::terrain&&view.world.record_revision)
  return queue_native_terrain(view,st,nullptr,result);
 return queue_native_owned_impl(view,st,result,nullptr);
}

HRESULT Renderer::native_packet_extents(const NativeGeometryView& view,const FixedState& st,RECT& extents) const noexcept{
 extents={};
 if(view.world.enabled||!view.positions||!view.position_groups||view.position_groups>UINT_MAX/4u)return E_INVALIDARG;
 // The mesh bounds get_native_mesh records (update_bounds) ...
 float minimum[3],maximum[3];
 for(UINT axis=0;axis<3;++axis){minimum[axis]=std::numeric_limits<float>::max();maximum[axis]=-std::numeric_limits<float>::max();}
 const std::uint32_t position_count=view.position_groups*4u;
 for(std::uint32_t index=0;index<position_count;++index){
  const auto& group=view.positions[index>>2];const auto lane=index&3u;
  const float value[3]={group.x[lane],group.y[lane],group.z[lane]};
  for(UINT axis=0;axis<3;++axis)if(std::isfinite(value[axis])){
   minimum[axis]=std::min(minimum[axis],value[axis]);maximum[axis]=std::max(maximum[axis],value[axis]);
  }
 }
 for(UINT axis=0;axis<3;++axis)if(minimum[axis]>maximum[axis])minimum[axis]=maximum[axis]=0;
 // ... plus the morph range and the wave margin queue_native_impl adds ...
 float morph_min[3]={0,0,0},morph_max[3]={0,0,0};
 for(std::uint32_t index=0;view.morphs&&index<view.morph_count;++index){
  const float delta[3]={view.morphs[index].x,view.morphs[index].y,view.morphs[index].z};
  for(UINT axis=0;axis<3;++axis)if(std::isfinite(delta[axis])){
   morph_min[axis]=std::min(morph_min[axis],delta[axis]);morph_max[axis]=std::max(morph_max[axis],delta[axis]);
  }
 }
 for(UINT axis=0;axis<3;++axis){minimum[axis]+=morph_min[axis];maximum[axis]+=morph_max[axis];}
 if(view.terrain.waves_enabled&&std::isfinite(view.terrain.wind_force)&&std::isfinite(view.terrain.maximum_wind_multiplier)){
  const float margin=std::abs(view.terrain.wind_force)*std::max(0.0f,view.terrain.maximum_wind_multiplier);
  minimum[0]-=margin*3.0f;maximum[0]+=margin*3.0f;minimum[1]-=margin*3.0f;maximum[1]+=margin*3.0f;
  minimum[2]-=margin*0.25f;maximum[2]+=margin*0.25f;
 }
 // ... projected through the clip with its HUD composition.
 auto object_to_clip=view.object_to_clip;
 if(widescreen::interface_owned)
  widescreen::compose_hud_clip(object_to_clip.data(),widescreen::hud_blit,float(canvas_width()),float(canvas_height()));
 return calculate_native_aabb_screen_extents(minimum,maximum,object_to_clip,st.viewport,extents);
}

HRESULT Renderer::queue_native_owned_impl(const NativeGeometryView& view,const FixedState& st,
 NativeGeometryResult* result,UINT* registered){
 if(result)*result={};
 if(reject_native_contribution(view,result))return S_OK;
 if(native_shadow_collecting_&&(!view.contribution.light_mask||
    !(view.contribution.light_mask&native_shadow_dirty_mask_)))return S_OK;
 if(native_shadow_collecting_&&!st.render[D3DRENDERSTATE_ALPHATESTENABLE])
  return queue_native_impl(view,st,nullptr,result,false,nullptr,0,0,false,0,registered);
 // A neighbour piece texture (archive member) may carry no mips: its
 // resident cache entry serves it (get_native_owned_texture).
 if(!view.owned_texture.identity||!view.owned_texture.generation||
    (!view.owned_texture.surface&&(!view.owned_texture.mip_count||(!view.owned_texture.mips&&
     (view.owned_texture.value_source!=1||!view.owned_texture.resource)))))
  return native_geometry_failure(result,E_INVALIDARG,"texture.descriptor",true);
 if(d3d12_){
  // D3D12 backend: the owned texture as a D3D12 texture on the same key.
  D3D12TextureRef d3d12_texture;std::uint64_t d3d12_backing=0;
  const HRESULT d3d12_hr=get_native_owned_texture_d3d12(view.owned_texture,d3d12_texture,d3d12_backing,result,
   !view.owned_texture.camp_surface_alias&&!scene_composited_&&view.semantic_kind!=SemanticKind::ui);
  if(FAILED(d3d12_hr))return d3d12_hr;
  ++native_generic_ingress_calls_;
  // A retained figure part's composed atlas: its material profile (as below).
  // A client surface's texture is keyed by the surface itself (2<<62|id, no
  // owned-texture entry): its profile goes with the surface (retire_surface).
  if(!native_shadow_collecting_&&view.owned_texture.material_name)
   note_material_texture(*view.owned_texture.material_name,d3d12_backing,(view.flags&native_unit_flag)!=0,
    view.owned_texture.surface&&d3d12_backing==((2ull<<62)|view.owned_texture.identity));
  return queue_native_impl(view,st,nullptr,result,false,nullptr,d3d12_backing,view.owned_texture.generation,false,0,registered,&d3d12_texture);
 }
 ComPtr<ID3D11ShaderResourceView> texture;
 std::uint64_t backing=0;
 HRESULT hr=get_native_owned_texture(view.owned_texture,texture,backing,result,
  !view.owned_texture.camp_surface_alias&&!scene_composited_&&view.semantic_kind!=SemanticKind::ui,
  view.semantic_kind!=SemanticKind::terrain); // terrain atlases keep their texel gutters
 if(FAILED(hr))return hr;
 if(!texture)return native_geometry_failure(result,E_FAIL,"texture.srv",true);
 // Alpha-tested shadow casters sample the same owned texture as the colour
 // pass: one texture per image, no shadow texture-array copy (#142).
 if(!native_shadow_collecting_)++native_generic_ingress_calls_;
 // A retained figure part's composed atlas: its material profile (colour only).
 if(!native_shadow_collecting_&&view.owned_texture.material_name)note_material_texture(*view.owned_texture.material_name,backing,(view.flags&native_unit_flag)!=0);
 return queue_native_impl(view,st,nullptr,result,false,texture.Get(),
  backing,view.owned_texture.generation,false,0,registered);
}

HRESULT Renderer::queue_native_prepared(const NativeGeometryView& view,const FixedState& st,SurfaceImage* texture,NativeGeometryResult* result){SCENE_ENTRY("queue_native_prepared");
 if(result)*result={};
 if(!view.prepared_mesh||!view.prepared_material||
    !valid_prepared_identity(view.prepared_identity))return native_geometry_failure(result,E_INVALIDARG,"prepared_identity");
 // Mesh prewarm: only the prepared mesh lookup, before the terrain path's
 // clip transition and instance records (queue_native_terrain).
 if(view.prepare_only){
  if(!view.world.enabled)return native_geometry_failure(result,E_INVALIDARG,"prepare.world_input");
  NativeMesh* mesh=nullptr;const HRESULT hr=get_native_prepared_mesh(view,mesh);
  return FAILED(hr)||!mesh?native_geometry_failure(result,FAILED(hr)?hr:E_FAIL,"prepare.mesh"):S_OK;
 }
 ++native_prepared_ingress_calls_;
 ++native_frame_stats_.native_object_prepared_ingress_calls;
 if(view.semantic_kind==SemanticKind::terrain&&view.world.record_revision)
  return queue_native_terrain(view,st,texture,result);
 return queue_native_impl(view,st,texture,result,true);
}

#ifdef EID3D11_EMBEDDED
HRESULT Renderer::queue_embedded_wound(const NativeGeometryView& source,const FixedState& frame,
 const OwnedVisualSurface& surface,NativeGeometryResult* result){
 if(result)*result={};
 if(!source.wound_only||!source.prepared_mesh||!source.prepared_wound_material||
    !source.prepared_identity.valid||!surface.id||!surface.generation)
  return native_geometry_failure(result,E_INVALIDARG,"embedded.wound_identity");
 NativeGeometryView view=source;
 view.prepared_material=source.prepared_wound_material;
 view.owned_texture={};
 view.owned_texture.identity=surface.id;
 view.owned_texture.generation=surface.generation;
 view.owned_texture.surface=&surface;
 view.ordering=NativeOrdering::preserve;
 FixedState state=frame;
 state.render[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_SRCALPHA;
 state.render[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_INVSRCALPHA;
 state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=TRUE;
 state.stage[0][D3DTSS_TEXCOORDINDEX]=0;
 ComPtr<ID3D11ShaderResourceView> texture;
 std::uint64_t backing=0;
 HRESULT hr=get_native_owned_texture(view.owned_texture,texture,backing,result,false);
 if(FAILED(hr))return hr;
 if(!texture)return native_geometry_failure(result,E_FAIL,"embedded.wound_texture");
 return queue_native_impl(view,state,nullptr,result,true,texture.Get(),backing,
  surface.generation,false,0);
}
#endif

void Renderer::native_terrain_values(NativeInstanceGpu& value,const NativeTerrainState& terrain,
                                     bool full_sun_comparison) noexcept{
 value.terrain_phase_count=terrain.animation_phase_count;value.terrain_tiles_per_side=terrain.tiles_per_texture_side;
 value.terrain_flags=(terrain.waves_enabled?1u:0u)|(terrain.animated_tiles_enabled?2u:0u)|
                     (full_sun_comparison?8u:0u);
 std::copy(std::begin(terrain.wind_direction),std::end(terrain.wind_direction),value.terrain_wind_direction);
 value.terrain_wind_force=terrain.wind_force;value.terrain_wind_force_normalized=terrain.wind_force_normalized;
 value.terrain_wind_phase=terrain.wind_phase;value.terrain_precise_time=terrain.precise_time;
 // Spare instance word carries only the fractional detail clock, not deformation time.
 std::memcpy(&value.padding[2],&terrain.liquid_detail_time,sizeof(float));
 std::copy(std::begin(terrain.fog_min),std::end(terrain.fog_min),value.terrain_fog_min);
 std::copy(std::begin(terrain.fog_max),std::end(terrain.fog_max),value.terrain_fog_max);
 value.terrain_fog_distance=terrain.fog_distance;value.terrain_fog_enabled=terrain.fog_enabled;
}

HRESULT Renderer::queue_native_terrain(const NativeGeometryView& view,const FixedState& state,
 SurfaceImage* texture,NativeGeometryResult* result){
 if(result)*result={};
 const auto fail=[&](HRESULT hr,const char* why){
  discard_native_unqueued_work();return native_geometry_failure(result,hr,why);
 };
 if(reject_native_contribution(view,result))return S_OK;
 if(!view.world.enabled||!view.contribution.known||!view.world.owner||view.world.owner->expired()||
    !view.prepared_mesh||!view.prepared_material||!valid_prepared_identity(view.prepared_identity))
  return fail(E_INVALIDARG,"terrain.record_identity");
 const UINT pass=native_shadow_collecting_?1:0;
 const UINT passes=(!pass&&view.contribution.color?1u:0u)|
  (UINT(view.contribution.light_mask&(pass?native_shadow_dirty_mask_:31u))<<8);
 if(!passes)return S_OK;
 if(pass&&(native_shadow_failed_||!state.render[D3DRENDERSTATE_ZENABLE]||!state.render[D3DRENDERSTATE_ZWRITEENABLE]))
  return fail(E_INVALIDARG,"terrain.depth_state");
 if(view.lighting.enabled&&!native_world_lighting_valid_)return fail(E_INVALIDARG,"terrain.lighting");
 if(!set_native_world_clip(view.world.world_to_clip))return fail(E_FAIL,"terrain.clip_transition");
 const bool sampled=!pass||state.render[D3DRENDERSTATE_ALPHATESTENABLE]!=0;
 const bool owned=sampled&&view.owned_texture.identity!=0;
 const std::uint64_t texture_id=sampled?(owned?view.owned_texture.identity:(texture?texture->id:0)):0;
 const std::uint64_t texture_generation=sampled?(owned?view.owned_texture.generation:(texture?texture->generation:0)):0;
 try{
  UINT current=UINT_MAX;const auto& handle=*view.prepared_material;
  if(handle.slot<native_prepared_material_slots_.size()){
   const auto& slot=native_prepared_material_slots_[handle.slot];
   if(slot.active&&slot.generation==handle.generation&&same_prepared_identity(slot.identity,view.prepared_identity,false))
    current=slot.persistent_instance[pass];
  }
  const UINT mesh_index=view.prepared_mesh->slot;
  const bool mesh_current=mesh_index<native_prepared_mesh_slots_.size()&&
   native_prepared_mesh_slots_[mesh_index].active&&native_prepared_mesh_slots_[mesh_index].generation==view.prepared_mesh->generation&&
   native_prepared_mesh_slots_[mesh_index].source_revision==view.world.record_revision&&
   same_prepared_identity(native_prepared_mesh_slots_[mesh_index].identity,view.prepared_identity,true);
  const bool warm=mesh_current&&current<native_instance_records_.size()&&native_instance_records_[current].owned&&
   native_instance_records_[current].source_revision==view.world.record_revision&&
   native_instance_records_[current].terrain_texture_id==texture_id&&
   native_instance_records_[current].terrain_texture_generation==texture_generation&&
   native_instance_records_[current].terrain_texture_owned==owned;
  HRESULT hr=S_OK;
  if(!warm){
   hr=view.owned_texture.identity?queue_native_owned_impl(view,state,result,&current):
    queue_native_impl(view,state,texture,result,true,nullptr,0,0,false,0,&current);
   if(FAILED(hr)){discard_native_unqueued_work();return hr;}
  }else{
   NativeQueuedDraw draw=native_instance_records_[current].command;
   auto& mesh=native_prepared_mesh_slots_[mesh_index];mesh.last_used_frame=native_frame_index_;
   mesh.mesh->last_used_frame=native_frame_index_;
   native_terrain_values(draw.instance,view.terrain,
     !configured_boolean(debug_overlay_.config(),"terrain_normal_sunlight",true));
   draw.animation_allocation=UINT_MAX;draw.instance.terrain_phase_offset=0;
   std::memcpy(&draw.instance.object_to_world,view.world.object_to_world.data(),sizeof(D3DMATRIX));
   draw.instance.flags=view.flags&~native_preview_specular_rgb_flag;draw.instance.default_diffuse=view.default_diffuse;draw.instance.default_specular=view.default_specular;
   draw.instance.material_alpha=view.lighting.material_alpha;draw.instance.lighting_enabled=view.lighting.enabled;
   std::copy(view.lighting.material_diffuse.begin(),view.lighting.material_diffuse.end(),draw.instance.material_diffuse);
   std::copy(view.lighting.emissive.begin(),view.lighting.emissive.end(),draw.instance.emissive);
   std::copy(view.lighting.specular.begin(),view.lighting.specular.end(),draw.instance.specular);
   draw.instance.uv_shift[0]=view.u_shift;draw.instance.uv_shift[1]=view.v_shift;
   std::copy(view.bounds_minimum.begin(),view.bounds_minimum.end(),draw.bounds_min);
   std::copy(view.bounds_maximum.begin(),view.bounds_maximum.end(),draw.bounds_max);
   if(FAILED(hr=resolve_native_world_range(view,draw)))return fail(hr,"terrain.world_range");
   const auto& group=native_state_buckets_[draw.state_bucket];
   FixedState shadow_state;
   const FixedState* material_state=nullptr;
   const auto opacity=native_material_state(state,view.lighting.material_alpha,
    native_alpha_evidence(view,state,view.owned_texture.identity!=0),native_shadow_collecting_,shadow_state,material_state);
   draw.instance.world_geometry=(draw.instance.world_geometry&~(native_world_lod|native_world_impostor))|
    (native_record_takes_lod(view,opacity,draw.mesh->lod_levels)?native_world_lod:0u)|
    (native_record_takes_impostor(view,opacity,draw.mesh->sphere_block)?native_world_impostor:0u);
   if(draw.instance.world_geometry&native_world_impostor)draw.instance.second_morph_offset=view.world.impostor-1;
   else if((draw.instance.world_geometry&native_world_lod)&&!view.world.object_space)
    draw.instance.second_morph_offset=draw.world_base;
   const auto key=native_material_key(*material_state,SemanticKind::terrain,opacity,view.lighting.enabled!=0,
    group.texture_srv!=nullptr||group.texture_d3d12,group.texture_array,group.material_key.texture_backing,group.material_key.texture_generation,group.material_key.contact_map);
   if(!(key==group.material_key)){
    const auto srv=group.texture_srv;const bool array=group.texture_array;const D3D12TextureRef texture12=group.texture_d3d12;
    if(FAILED(hr=get_native_material_group(key,*material_state,srv.Get(),array,SemanticKind::terrain,opacity,
       native_material_state_key(key),draw.state_bucket,texture12?&texture12:nullptr)))return fail(hr,"terrain.material_group");
   }
   draw.reorderable=view.ordering==NativeOrdering::reorderable||
    (view.ordering==NativeOrdering::derive_from_state&&opacity!=NativeOpacityClass::ordered);
   draw.preserve_order=view.ordering==NativeOrdering::preserve;
   if(FAILED(hr=resolve_native_terrain_phases(view,draw)))return fail(hr,"terrain.phases");
   auto& slot=native_prepared_material_slots_[view.prepared_material->slot];
   if(FAILED(hr=store_native_instance(slot.persistent_instance[pass],*view.world.owner,draw)))return fail(hr,"terrain.instance");
   current=draw.persistent_instance;
  }
  if(current>=native_instance_records_.size())return fail(E_FAIL,"terrain.registration");
  auto& record=native_instance_records_[current];record.source_revision=view.world.record_revision;
  record.terrain_texture_id=texture_id;record.terrain_texture_generation=texture_generation;record.terrain_texture_owned=owned;
  if((!record.uploaded||!record.descriptor_uploaded)&&!record.scheduled){native_instance_dirty_.push_back(current);record.scheduled=true;}
  enqueue_native_record({current,passes});return S_OK;
 }catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"terrain.allocation");}
}

HRESULT Renderer::queue_native_depth(const NativeDepthSubmission& submission,
 const FixedState& frame,NativeGeometryResult* result){SCENE_ENTRY("queue_native_depth");
 if(result)*result={};
 NativeQueueCheckpoint checkpoint;checkpoint_native(checkpoint);
 const auto fail=[&](HRESULT hr,const char* why){
  rollback_native(checkpoint);native_shadow_failed_=true;
  return native_geometry_failure(result,hr,result&&result->failure_operation?result->failure_operation:why,
   result&&result->failure_texture);
 };
 if(!native_shadow_collecting_||native_shadow_failed_)return fail(E_FAIL,"depth_selection.pass");
 if(!submission.count)return S_OK;
 if(!submission.parts||!submission.world_to_clip||!submission.register_part)
  return fail(E_INVALIDARG,"depth_selection.packet");
 if(!set_native_world_clip(*submission.world_to_clip))return fail(E_FAIL,"depth_selection.clip");
 // Entity depth records inherit only sampler state consumed by the shadow
 // shader. Fog enable/color remain exact color-pass state and cannot invalidate
 // depth. Terrain supplies a complete captured depth state in its caster
 // revision and never inherits the later proxy sampler.
 const UINT inherited_mip=frame.stage[0][D3DTSS_MIPFILTER];
 enum class NativeDepthColdReason : std::uint8_t {
  none,
  handle_owner_identity,
  source_revision,
  inherited_context,
  caster_revision,
  world_allocation_generation,
  pose_revision_generation_allocation,
  animation_generation_ownership,
  bounds,
 };
 const bool profile_depth_selection=placed_hitch_profile_.active()||profiler_.enabled();
 const auto current_record=[&](const NativeDepthSelection& part,
  NativeDepthColdReason* cold_reason)->UINT {
  const auto reject=[&](NativeDepthColdReason reason){
   if(cold_reason)*cold_reason=reason;
   return UINT_MAX;
  };
  if(!part.mesh||!part.material||!valid_prepared_identity(part.identity))
   return reject(NativeDepthColdReason::handle_owner_identity);
  if(!part.revision.revision)
   return reject(NativeDepthColdReason::caster_revision);
  if(part.mesh->slot>=native_prepared_mesh_slots_.size()||
     part.material->slot>=native_prepared_material_slots_.size())
   return reject(NativeDepthColdReason::handle_owner_identity);
  const auto& mesh=native_prepared_mesh_slots_[part.mesh->slot];
  const auto& material=native_prepared_material_slots_[part.material->slot];
  if(!mesh.active||mesh.generation!=part.mesh->generation||mesh.owner.expired()||
     !same_prepared_identity(mesh.identity,part.identity,true)||
     !material.active||material.generation!=part.material->generation||material.owner.expired()||
     !same_prepared_identity(material.identity,part.identity,false))
   return reject(NativeDepthColdReason::handle_owner_identity);
  const UINT index=material.persistent_instance[1];
  if(index>=native_instance_records_.size())
   return reject(NativeDepthColdReason::handle_owner_identity);
  const auto& record=native_instance_records_[index];
  if(!record.occupied||!record.owned||record.owner.expired())
   return reject(NativeDepthColdReason::handle_owner_identity);
  if(!record.source_revision||record.source_revision!=mesh.source_revision)
   return reject(NativeDepthColdReason::source_revision);
  return index;
 };
 const auto backend_current=[&](const NativeDepthSelection& part,
  const NativeInstanceRecord& record,NativeDepthColdReason* cold_reason){
  const auto reject=[&](NativeDepthColdReason reason){
   if(cold_reason)*cold_reason=reason;
   return false;
  };
  const auto& draw=record.command;const auto& mesh=native_prepared_mesh_slots_[part.mesh->slot];
  if(draw.world_slot!=part.mesh->slot||draw.world_generation!=mesh.world_generation||
     draw.world_base!=mesh.world_base||draw.world_allocation!=mesh.world_allocation||
     draw.world_allocation>=native_world_ranges_.size())
   return reject(NativeDepthColdReason::world_allocation_generation);
  const auto& world=native_world_ranges_[draw.world_allocation];
  if(!world.owned||world.generation!=record.depth_world_generation)
   return reject(NativeDepthColdReason::world_allocation_generation);
  if(draw.pose_slot!=UINT_MAX){
   if(draw.pose_slot>=native_pose_slots_.size())
    return reject(NativeDepthColdReason::pose_revision_generation_allocation);
   const auto& pose=native_pose_slots_[draw.pose_slot];
   if(!pose.active||pose.generation!=record.depth_pose_generation||pose.revision!=record.depth_pose_revision||
      pose.allocation!=draw.pose_allocation||pose.animation_allocation!=draw.animation_allocation)
    return reject(NativeDepthColdReason::pose_revision_generation_allocation);
  }
  if(draw.animation_allocation!=UINT_MAX){
   if(draw.animation_allocation>=native_animation_ranges_.size())
    return reject(NativeDepthColdReason::animation_generation_ownership);
   const auto& range=native_animation_ranges_[draw.animation_allocation];
   if((!range.owned&&!range.owners)||range.generation!=record.depth_animation_generation)
    return reject(NativeDepthColdReason::animation_generation_ownership);
  }
  // GPU pose nodes own current bounds; persistent commands intentionally zero
  // their bounds. Their generation/revision above validates the current node.
  if(draw.pose_slot!=UINT_MAX)return true;
  if(!std::equal(part.bounds.minimum.begin(),part.bounds.minimum.end(),draw.bounds_min)||
     !std::equal(part.bounds.maximum.begin(),part.bounds.maximum.end(),draw.bounds_max))
   return reject(NativeDepthColdReason::bounds);
  return true;
 };
 const auto refresh_inherited_mip=[&](const NativeDepthSelection& part,UINT& index)->HRESULT {
  if(part.identity.terrain_source)return S_OK;
  auto& material=native_prepared_material_slots_[part.material->slot];
  if(index>=native_instance_records_.size()||material.persistent_instance[1]!=index)return E_FAIL;
  auto& record=native_instance_records_[index];
  if(record.inherited_state[2]==inherited_mip)return S_OK;
  if(record.command.state_bucket>=native_state_buckets_.size())return E_FAIL;
  const auto group_index=record.command.state_bucket;
  const auto& group=native_state_buckets_[group_index];
  if(!group.occupied)return E_FAIL;
  FixedState state=group.state;state.stage[0][D3DTSS_MIPFILTER]=inherited_mip;
  const auto key=native_material_key(state,group.semantic_kind,group.opacity,false,
   group.texture_srv!=nullptr||group.texture_d3d12,group.texture_array,group.material_key.texture_backing,
   group.material_key.texture_generation);
  if(!(key==group.material_key)){
   NativeQueuedDraw draw=record.command;
   const auto texture=group.texture_srv;const bool texture_array=group.texture_array;
   const D3D12TextureRef texture12=group.texture_d3d12; // the D3D12 texture stays with the re-keyed bucket
   const auto semantic=group.semantic_kind;const auto opacity=group.opacity;
   const auto owner=record.owner;const auto source_revision=record.source_revision;
   const auto values_revision=record.values_revision;const auto depth_revision=record.depth_revision;
   const auto depth_world_generation=record.depth_world_generation;
   const auto depth_pose_revision=record.depth_pose_revision;
   const auto depth_animation_generation=record.depth_animation_generation;
   const auto depth_pose_generation=record.depth_pose_generation;
   const auto inherited_state=record.inherited_state;
   const auto wound_id=record.wound_id;const auto wound_generation=record.wound_generation;
   const auto terrain_texture_id=record.terrain_texture_id;
   const auto terrain_texture_generation=record.terrain_texture_generation;
   const bool terrain_texture_owned=record.terrain_texture_owned;
   HRESULT hr=get_native_material_group(key,state,texture.Get(),texture_array,semantic,opacity,
    native_material_state_key(key),draw.state_bucket,texture12?&texture12:nullptr);
   if(FAILED(hr))return hr;
   if(FAILED(hr=store_native_instance(material.persistent_instance[1],owner,draw)))return hr;
   index=draw.persistent_instance;
   auto& updated=native_instance_records_[index];
   updated.source_revision=source_revision;updated.values_revision=values_revision;
   updated.depth_revision=depth_revision;updated.depth_world_generation=depth_world_generation;
   updated.depth_pose_revision=depth_pose_revision;
   updated.depth_animation_generation=depth_animation_generation;
   updated.depth_pose_generation=depth_pose_generation;updated.inherited_state=inherited_state;
   updated.wound_id=wound_id;updated.wound_generation=wound_generation;
   updated.terrain_texture_id=terrain_texture_id;
   updated.terrain_texture_generation=terrain_texture_generation;
   updated.terrain_texture_owned=terrain_texture_owned;
  }
  auto& updated=native_instance_records_[index];
  updated.inherited_state[2]=inherited_mip;updated.depth_current=true;
  return S_OK;
 };
 try{
  if(submission.count>native_pending_draws_.max_size()-native_pending_draws_.size())
   return fail(E_OUTOFMEMORY,"depth_selection.capacity");
  native_pending_draws_.reserve(native_pending_draws_.size()+submission.count);
  for(UINT i=0;i<submission.count;++i){
   const auto& part=submission.parts[i];
   // Static-layer parts (terrain, missing static records) of a scrolled map
   // are drawn only where they reach the exposed strip.
   const UINT mask=native_shadow_phase_==1?native_static_scroll_mask(part.bounds,part.mask&native_shadow_dirty_mask_,&part.revision):
    part.mask&native_shadow_dirty_mask_;
   if(!mask)continue;
   if(placed_hitch_profile_.active())
    for(unsigned slice=0;slice<5;++slice)
     if(mask&(1u<<slice))++native_frame_stats_.shadow_decision.selected_per_slice[slice];
    NativeDepthColdReason cold_reason=NativeDepthColdReason::none;
    auto* const cold_reason_output=profile_depth_selection?&cold_reason:nullptr;
    UINT index=current_record(part,cold_reason_output);
    bool warm=false;
    if(index!=UINT_MAX){
     const auto& record=native_instance_records_[index];
     if(!part.source_revision||record.source_revision!=part.source_revision){
      if(cold_reason_output)*cold_reason_output=NativeDepthColdReason::source_revision;
     }else if(!record.depth_current){
      if(cold_reason_output)*cold_reason_output=NativeDepthColdReason::inherited_context;
     }else if(record.depth_revision!=part.revision){
      if(cold_reason_output)*cold_reason_output=NativeDepthColdReason::caster_revision;
     }else{
      warm=backend_current(part,record,cold_reason_output);
      if(warm&&!part.identity.terrain_source&&record.inherited_state[2]!=inherited_mip){
       const HRESULT hr=refresh_inherited_mip(part,index);
       if(FAILED(hr))return fail(hr,"depth_selection.material");
      }
     }
    }
    if(profile_depth_selection){
     auto& stats=native_frame_stats_;
     ++stats.native_depth_selected;
     if(warm){
      ++stats.native_depth_warm;
     }else{
      ++stats.native_depth_cold;
      switch(cold_reason){
      case NativeDepthColdReason::handle_owner_identity: ++stats.native_depth_cold_handle_owner_identity; break;
      case NativeDepthColdReason::source_revision: ++stats.native_depth_cold_source_revision; break;
      case NativeDepthColdReason::inherited_context: ++stats.native_depth_cold_inherited_context; break;
      case NativeDepthColdReason::caster_revision: ++stats.native_depth_cold_caster_revision; break;
      case NativeDepthColdReason::world_allocation_generation: ++stats.native_depth_cold_world_allocation_generation; break;
      case NativeDepthColdReason::pose_revision_generation_allocation: ++stats.native_depth_cold_pose_revision_generation_allocation; break;
      case NativeDepthColdReason::animation_generation_ownership: ++stats.native_depth_cold_animation_generation_ownership; break;
      case NativeDepthColdReason::bounds: ++stats.native_depth_cold_bounds; break;
      case NativeDepthColdReason::none: break;
      }
     }
    }
    if(!warm){
     if(profile_depth_selection)++native_frame_stats_.native_depth_registration_callbacks;
#ifdef EID3D11_TESTING
     ++test_native_depth_registrations_;
#endif
     NativeGeometryResult detail{};
    if(!submission.register_part(submission.context,part,detail)){
     if(result)*result=detail;
     return fail(FAILED(detail.failure_hresult)?detail.failure_hresult:E_FAIL,"depth_selection.registration");
    }
     index=current_record(part,nullptr);
    if(index==UINT_MAX)return fail(E_FAIL,"depth_selection.registered_handle");
    auto& record=native_instance_records_[index];const auto& draw=record.command;
    if(draw.world_allocation>=native_world_ranges_.size())return fail(E_FAIL,"depth_selection.world");
    record.depth_revision=part.revision;record.depth_current=true;
    record.depth_world_generation=native_world_ranges_[draw.world_allocation].generation;
    record.depth_pose_generation=0;record.depth_pose_revision=0;record.depth_animation_generation=0;
    if(draw.pose_slot!=UINT_MAX){
     if(draw.pose_slot>=native_pose_slots_.size())return fail(E_FAIL,"depth_selection.pose");
     record.depth_pose_generation=native_pose_slots_[draw.pose_slot].generation;
     record.depth_pose_revision=native_pose_slots_[draw.pose_slot].revision;
    }
    if(draw.animation_allocation!=UINT_MAX){
     if(draw.animation_allocation>=native_animation_ranges_.size())return fail(E_FAIL,"depth_selection.animation");
     record.depth_animation_generation=native_animation_ranges_[draw.animation_allocation].generation;
    }
    // The existing modern registration already enqueued and pinned this part.
   }else{
    auto& mesh=native_prepared_mesh_slots_[part.mesh->slot];
    mesh.last_used_frame=native_frame_index_;mesh.mesh->last_used_frame=native_frame_index_;
    native_prepared_material_slots_[part.material->slot].last_used_frame=native_frame_index_;
    auto& record=native_instance_records_[index];
    if((!record.uploaded||!record.descriptor_uploaded)&&!record.scheduled){
     native_instance_dirty_.push_back(index);record.scheduled=true;
    }
    enqueue_native_record({index,mask<<8});
#ifdef EID3D11_TESTING
    ++test_native_depth_selections_;
#endif
   }
   if(part.persistent_static) {
    const auto key=(std::uint64_t(std::uint32_t(part.revision.signed_id))<<32)|
     part.revision.part;
    auto& entry=native_static_shadow_records_[key];
    const NativeStaticShadowRecord next{
     part.revision,index,part.mesh->slot,part.mesh->generation,
     part.material->slot,part.material->generation,native_static_shadow_visit_};
    // A warm resubmission of the same record changes nothing the bake holds.
    if(entry.revision!=next.revision||entry.instance!=next.instance||entry.mesh_slot!=next.mesh_slot||
       entry.mesh_generation!=next.mesh_generation||entry.material_slot!=next.material_slot||
       entry.material_generation!=next.material_generation){
     if(++native_static_shadow_records_serial_==0)++native_static_shadow_records_serial_;
    }
    entry=next;
   }else if(part.identity.terrain_source){
    const auto key=(std::uint64_t(std::uint32_t(part.revision.signed_id))<<32)|part.revision.part;
    auto& entry=native_terrain_shadow_records_[key];
    const NativeStaticShadowRecord next{
     part.revision,index,part.mesh->slot,part.mesh->generation,
     part.material->slot,part.material->generation,native_static_shadow_visit_};
    if(entry.revision!=next.revision||entry.instance!=next.instance||entry.mesh_slot!=next.mesh_slot||
       entry.mesh_generation!=next.mesh_generation||entry.material_slot!=next.material_slot||
       entry.material_generation!=next.material_generation){
     if(++native_terrain_shadow_records_serial_==0)++native_terrain_shadow_records_serial_;
    }
    entry=next;
   }
  }
  return S_OK;
 }catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"depth_selection.allocation");}
 catch(...){return fail(E_FAIL,"depth_selection.exception");}
}

HRESULT Renderer::queue_native_records(const NativePreparedSubmission& submission,
 const FixedState& frame,SurfaceImage* wound,NativeGeometryResult* result){SCENE_ENTRY("queue_native_records");
 bool color_diagnostic_enabled=placed_hitch_profile_.active();
#ifdef EID3D11_TESTING
 color_diagnostic_enabled|=test_color_resource_diagnostic_;
#endif
 auto* color=submission.retained_color_diagnostic&&!native_shadow_collecting_&&
  color_diagnostic_enabled&&
  !active_color_resource_?
  &native_frame_stats_.color_resource:nullptr;
 ColorDiagnosticActivation color_activation(active_color_resource_,color_mesh_active_,
  color_texture_active_,color_cold_active_,color);
 ColorDiagnosticTicks color_timing(color?&color->total_ticks:nullptr);
 ColorDiagnosticResult color_result(color);
 if(color)++color->tagged_calls;
 if(result)*result={};
 NativeQueueCheckpoint publication;
 const auto fail=[&](HRESULT hr,const char* reason){
  if(publication.epoch)rollback_native(publication);
  else discard_native_unqueued_work();
  return native_geometry_failure(result,hr,reason);
 };
 if(!submission.count){color_result.complete=true;return S_OK;}
 if(!submission.parts||!submission.world_to_clip||!submission.pose||
    !valid_prepared_identity(submission.identity)||submission.pose->owner.expired())
  return fail(E_INVALIDARG,"records.identity");
 if(bool(submission.wound_surface_interface)!=bool(wound)||(wound&&native_shadow_collecting_))
  return fail(E_INVALIDARG,"records.wound");
 if(native_shadow_collecting_&&native_shadow_failed_)return fail(E_FAIL,"records.shadow_state");
 try{
  native_registration_.clear();native_registration_.reserve(submission.count);
  NativeQueuedDraw pose;
  HRESULT hr=S_OK;bool pose_resolved=false;
  const UINT pass=native_shadow_collecting_?1:0;
  for(UINT i=0;i<submission.count;++i){
   const auto* part=submission.parts[i];
   if(!part||!part->source||!part->values_revision)return fail(E_INVALIDARG,"records.part");
   const auto& source=*part->source;const auto& values=part->values;
   if(!part->contribution.known)return fail(E_INVALIDARG,"records.contribution");
   // Fur prototype, strands (fur.h): a unit's hair part is drawn by the fur
   // pass. Only its colour record takes alpha 0 (its fragments fail the
   // alpha test); its sun shadow caster keeps the part's own alpha.
   const float alpha=!pass&&fur_strand_hair_record(part->fur.get(),part->fur_node_base,values.flags,g_fur_mode)?0.f:values.alpha;
   const UINT passes=(!pass&&part->contribution.color?1u:0u)|
    (UINT(part->contribution.light_mask&(pass?native_shadow_dirty_mask_:31u))<<8);
   if(!passes){++native_contribution_rejects_;continue;}
   if(!source.world.enabled||!source.world.record_revision||!source.owned_fixed_state.enabled||
      !source.prepared_mesh||!source.prepared_material||!submission.pose->binding||
      source.world.pose_node>=submission.pose->binding->tracks.size())
    return fail(E_INVALIDARG,"records.source");
   if(!pose_resolved){
    if(!set_native_world_clip(*submission.world_to_clip))return fail(E_FAIL,"records.clip_transition");
    if(FAILED(hr=resolve_native_pose(submission.pose,submission.identity,0,pose)))return fail(hr,"records.pose");
    pose_resolved=true;
   }
   if(values.lighting_enabled&&!native_world_lighting_valid_)return fail(E_INVALIDARG,"records.lighting");
   NativePreparedIdentity identity=submission.identity;identity.part_index=source.prepared_identity.part_index;
   auto* handle=wound?source.prepared_wound_material:source.prepared_material;
   if(!handle)return fail(E_INVALIDARG,"records.material_handle");
   const std::array<UINT,3> inherited={frame.render[D3DRENDERSTATE_FOGENABLE],
    frame.render[D3DRENDERSTATE_FOGCOLOR],source.owned_fixed_state.mip_filter==UINT_MAX?
     frame.stage[0][D3DTSS_MIPFILTER]:source.owned_fixed_state.mip_filter};
   UINT current=UINT_MAX;
   if(handle->slot<native_prepared_material_slots_.size()){
    const auto& slot=native_prepared_material_slots_[handle->slot];
    if(slot.active&&slot.generation==handle->generation&&same_prepared_identity(slot.identity,identity,false))
     current=slot.persistent_instance[pass];
   }
   const auto mesh_index=source.prepared_mesh->slot;
   const bool mesh_current=mesh_index<native_prepared_mesh_slots_.size()&&
    native_prepared_mesh_slots_[mesh_index].active&&native_prepared_mesh_slots_[mesh_index].generation==source.prepared_mesh->generation&&
    native_prepared_mesh_slots_[mesh_index].source_revision==source.world.record_revision&&
    same_prepared_identity(native_prepared_mesh_slots_[mesh_index].identity,identity,true);
   const bool registered=mesh_current&&current<native_instance_records_.size()&&native_instance_records_[current].owned&&
    native_instance_records_[current].source_revision==source.world.record_revision&&
    native_instance_records_[current].wound_id==(wound?wound->id:0)&&
    native_instance_records_[current].wound_generation==(wound?wound->generation:0);
   if(color){if(registered)++color->warm_parts;else ++color->cold_parts;}
   if(!registered){
    ColorDiagnosticTicks cold_timing(color?&color->cold_registration_ticks:nullptr,
     nullptr,color?&color_cold_active_:nullptr);
    // Source reconstruction is confined to a new/replaced binding. The warm
    // path below never resolves immutable geometry or texture residency.
    NativeGeometryView view=source;
    view.prepared_identity=identity;view.prepared_material=handle;
    view.world.pose=submission.pose;view.world.owner=&submission.pose->owner;
    view.world.world_to_clip=*submission.world_to_clip;view.object_to_clip=*submission.world_to_clip;
    view.world.object_to_world=submission.pose->root_to_world;view.world.shape=submission.pose->shape;
    view.contribution=part->contribution;view.bounds_minimum=part->bounds.minimum;view.bounds_maximum=part->bounds.maximum;
    view.contact_world_height=submission.pose->contact_world_height;
    view.flags=values.flags;view.default_diffuse=values.default_diffuse;view.default_specular=values.default_specular;
    view.lighting.enabled=values.lighting_enabled;view.lighting.material_alpha=alpha;
    view.lighting.material_diffuse=values.diffuse;view.lighting.emissive=values.emissive;view.lighting.specular=values.specular;
    view.u_shift=values.u_shift;view.v_shift=values.v_shift;
    view.ordering=wound?NativeOrdering::preserve:static_cast<NativeOrdering>(values.ordering);
    view.owned_fixed_state.specular_enable=values.specular_enable;
    auto state=native_owned_submission_state(frame,view.owned_fixed_state);
    if(wound){
     view.owned_texture={};
     state.render[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_SRCALPHA;
     state.render[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_INVSRCALPHA;
     state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=TRUE;state.stage[0][D3DTSS_TEXCOORDINDEX]=0;
     hr=queue_native_impl(view,state,wound,result,true,nullptr,0,0,false,0,&current);
    }else hr=queue_native_owned_impl(view,state,result,&current);
    if(FAILED(hr)){discard_native_unqueued_work();return hr;}
    if(current>=native_instance_records_.size())return fail(E_FAIL,"records.registration");
   }else{
    auto& old=native_instance_records_[current];
    auto& mesh_slot=native_prepared_mesh_slots_[mesh_index];
    mesh_slot.last_used_frame=native_frame_index_;mesh_slot.mesh->last_used_frame=native_frame_index_;
    if(mesh_slot.world_valid)++native_world_reused_records_;
    bool material_changed=old.inherited_state!=inherited;
    if(!material_changed&&(old.values_revision!=part->values_revision||old.command.instance.material_alpha!=alpha)){
     // Color and depth share the producer revision, but retain separate commands.
     // A change for the other pass need not reconstruct this pass's material.
     const auto& retained=old.command.instance;
     const auto& group=native_state_buckets_[old.command.state_bucket];
     const auto ordering=wound?NativeOrdering::preserve:static_cast<NativeOrdering>(values.ordering);
     const bool reorderable=ordering==NativeOrdering::reorderable||
      (ordering==NativeOrdering::derive_from_state&&group.opacity!=NativeOpacityClass::ordered);
     material_changed=retained.flags!=values.flags||retained.default_diffuse!=values.default_diffuse||
      retained.default_specular!=values.default_specular||retained.lighting_enabled!=values.lighting_enabled||
      retained.material_alpha!=alpha||
      !std::equal(values.diffuse.begin(),values.diffuse.end(),retained.material_diffuse)||
      !std::equal(values.emissive.begin(),values.emissive.end(),retained.emissive)||
      !std::equal(values.specular.begin(),values.specular.end(),retained.specular)||
      retained.uv_shift[0]!=values.u_shift||retained.uv_shift[1]!=values.v_shift||
      group.state.render[D3DRENDERSTATE_SPECULARENABLE]!=values.specular_enable||
      old.command.reorderable!=reorderable||old.command.preserve_order!=(ordering==NativeOrdering::preserve);
    }
    const bool pose_changed=old.command.pose_slot!=pose.pose_slot||old.command.pose_allocation!=pose.pose_allocation||
     old.command.animation_allocation!=pose.animation_allocation;
    const float contact_height=submission.pose->contact_world_height;
    const bool contact_changed=std::memcmp(&old.command.instance.padding[3],&contact_height,sizeof(float))!=0;
    // The FIG8 endpoint blend reads the instance shape and keys the prepared
    // world vertices. A pose slot updated in place keeps its allocation, so a
    // complection change (lever/drawbridge grace, #144) must re-resolve here.
    const bool shape_changed=!std::equal(submission.pose->shape.begin(),submission.pose->shape.end(),
     old.command.instance.shape);
    if(material_changed||pose_changed||contact_changed||shape_changed){
     NativeQueuedDraw draw=old.command;
     std::memcpy(&draw.instance.padding[3],&contact_height,sizeof(float));
     if(pose_changed||shape_changed){
      draw.pose_slot=pose.pose_slot;draw.pose_allocation=pose.pose_allocation;draw.animation_allocation=pose.animation_allocation;
      draw.instance.pose_node=pose.instance.pose_node+source.world.pose_node;
      std::copy(submission.pose->shape.begin(),submission.pose->shape.end(),draw.instance.shape);
      NativeGeometryView view=source;view.world.pose=submission.pose;
      view.world.shape=submission.pose->shape;view.world.world_to_clip=*submission.world_to_clip;
      view.contribution=part->contribution;
      if(FAILED(hr=resolve_native_world_range(view,draw)))return fail(hr,"records.world_range");
     }
     if(material_changed){
#ifdef EID3D11_TESTING
      ++test_native_material_reconstructions_;
#endif
      auto& value=draw.instance;value.flags=values.flags&~native_preview_specular_rgb_flag;value.default_diffuse=values.default_diffuse;
      value.default_specular=values.default_specular;value.lighting_enabled=values.lighting_enabled;
      value.material_alpha=alpha;std::copy(values.diffuse.begin(),values.diffuse.end(),value.material_diffuse);
      std::copy(values.emissive.begin(),values.emissive.end(),value.emissive);
      std::copy(values.specular.begin(),values.specular.end(),value.specular);
      value.uv_shift[0]=values.u_shift;value.uv_shift[1]=values.v_shift;
      const auto& group=native_state_buckets_[draw.state_bucket];
      // Rebuild from the authored state: the prior opaque command has its
      // blend/test disabled, and cannot describe a later fade or mask.
      FixedState state=native_owned_submission_state(frame,source.owned_fixed_state);
      state.render[D3DRENDERSTATE_FOGENABLE]=inherited[0];state.render[D3DRENDERSTATE_FOGCOLOR]=inherited[1];
      state.stage[0][D3DTSS_MIPFILTER]=inherited[2];state.render[D3DRENDERSTATE_SPECULARENABLE]=values.specular_enable;
      // The group may hold a dropped test (queue_native_impl); new values
      // decide again from the part's own test.
      state.render[D3DRENDERSTATE_ALPHATESTENABLE]=source.owned_fixed_state.alpha_test_enable;
      state.render[D3DRENDERSTATE_ALPHAFUNC]=source.owned_fixed_state.alpha_func;
      state.render[D3DRENDERSTATE_ALPHAREF]=source.owned_fixed_state.alpha_ref;
      const FixedState* material_state=nullptr;
      const auto opacity=native_material_state(state,alpha,
       native_alpha_evidence(source,state,!wound,&values),native_shadow_collecting_,state,material_state);
      const auto key=native_material_key(state,source.semantic_kind,opacity,values.lighting_enabled!=0,
       group.texture_srv!=nullptr||group.texture_d3d12,group.texture_array,group.material_key.texture_backing,group.material_key.texture_generation,group.material_key.contact_map);
      const auto ordering=wound?NativeOrdering::preserve:static_cast<NativeOrdering>(values.ordering);
      draw.reorderable=ordering==NativeOrdering::reorderable||
       (ordering==NativeOrdering::derive_from_state&&opacity!=NativeOpacityClass::ordered);
      draw.preserve_order=ordering==NativeOrdering::preserve;
      {const D3D12TextureRef texture12=group.texture_d3d12; // the D3D12 texture stays with the re-keyed bucket
      if(FAILED(hr=get_native_material_group(key,state,group.texture_srv.Get(),group.texture_array,source.semantic_kind,
          opacity,native_material_state_key(key),draw.state_bucket,texture12?&texture12:nullptr)))return fail(hr,"records.material");}
     }
     NativeGeometryView lod_view=source;
     lod_view.ordering=wound?NativeOrdering::preserve:static_cast<NativeOrdering>(values.ordering);
     draw.instance.world_geometry&=~native_world_lod;
     if(native_record_takes_lod(lod_view,native_state_buckets_[draw.state_bucket].opacity,draw.mesh->lod_levels)){
      draw.instance.world_geometry|=native_world_lod;
      if(!lod_view.world.object_space)draw.instance.second_morph_offset=draw.world_base;
     }
     auto& slot=native_prepared_material_slots_[handle->slot];
     const auto lod_observer=old.lod_observer;
     if(FAILED(hr=store_native_instance(slot.persistent_instance[pass],submission.pose->owner,draw)))
      return fail(hr,"records.instance");
     current=draw.persistent_instance;
     native_instance_records_[current].lod_observer=lod_observer;
    }
   }
   auto& record=native_instance_records_[current];
   record.source_revision=source.world.record_revision;record.values_revision=part->values_revision;
   record.inherited_state=inherited;record.wound_id=wound?wound->id:0;record.wound_generation=wound?wound->generation:0;
   // Fur grows from the part's own colour record only. The wound layer is a
   // second record of the same mesh with the blended wound texture (black
   // where it carries no blood): fur on it drew hurt and dead creatures black.
   if(!pass){record.fur=wound?nullptr:part->fur;record.fur_node_base=part->fur_node_base;}
   if((!record.uploaded||!record.descriptor_uploaded)&&!record.scheduled){
    native_instance_dirty_.push_back(current);record.scheduled=true;
   }
   native_registration_.push_back({current,passes});
  }
  // No part has entered the ordering queue yet. Reserve first so failure
  // leaves the prior queue and complete-entity omission contract intact.
  if(native_registration_.size()>native_pending_draws_.max_size()-native_pending_draws_.size())
   return fail(E_OUTOFMEMORY,"records.queue_capacity");
  native_pending_draws_.reserve(native_pending_draws_.size()+native_registration_.size());
  native_instance_dirty_.reserve(native_instance_dirty_.size()+native_registration_.size());
  // A cold source replacement can finish an older resource generation while
  // this entity is still being registered. Reschedule its unqueued new rows
  // and pose after that drain, before publishing any of its handles.
  if(pose_resolved&&!native_pose_slots_[pose.pose_slot].valid&&!native_pose_slots_[pose.pose_slot].scheduled)
   if(FAILED(hr=resolve_native_pose(submission.pose,submission.identity,0,pose)))return fail(hr,"records.pose_after_drain");
  for(const auto selected:native_registration_){
   auto& record=native_instance_records_[selected.record];
   if((!record.uploaded||!record.descriptor_uploaded)&&!record.scheduled){
    native_instance_dirty_.push_back(selected.record);record.scheduled=true;
   }
  }
  // Registration can drain an older queue. Only this post-registration epoch
  // belongs to the atomic enqueue below; earlier draw counts are not a rollback.
  checkpoint_native(publication);
  for(const auto selected:native_registration_)enqueue_native_record(selected);
  color_result.complete=true;
  return S_OK;
 }catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"records.allocation");}
}

bool Renderer::set_native_world_lighting(
    const NativeWorldLighting& lighting) noexcept {
 if(scene_queue()){
  scene_cmd::WorldLighting command;command.lighting=lighting;bridge_read_world_environment(command.environment);
  scene_post_command(command);
  return true;
 }
 return set_native_world_lighting(lighting,nullptr);
}
void scene_cmd::WorldLighting::run(Renderer& r){(void)r.set_native_world_lighting(lighting,&environment);}

bool Renderer::set_native_world_lighting(
    const NativeWorldLighting& lighting,const WorldEnvironment* captured) noexcept {
 for(float value:lighting.ambient)if(!std::isfinite(value))return false;
 for(float value:lighting.sunlight_direction)if(!std::isfinite(value))return false;
 for(float value:lighting.sunlight_diffuse)if(!std::isfinite(value))return false;
 const float length_squared=lighting.sunlight_direction[0]*lighting.sunlight_direction[0]+
  lighting.sunlight_direction[1]*lighting.sunlight_direction[1]+
  lighting.sunlight_direction[2]*lighting.sunlight_direction[2];
 if(!std::isfinite(length_squared)||length_squared<=1e-12f)return false;
 // Cave zones (the game's own GMS_CAVE flag, read with the rest of the world
 // environment) take the renderer's cave profile; every consumer of the
 // shared world light (world, cover, critters, liquids, fog) sees it.
 NativeWorldLighting shaped=lighting;
 WorldEnvironment environment;
 if(captured)environment=*captured;else bridge_read_world_environment(environment);
 native_world_cave_=environment.valid&&environment.cave;
 if(environment.valid){native_world_hour_=environment.hour;native_world_region_=environment.region;}
 if(environment.valid)
  cave_lighting_profile(environment.cave,configured_floating_point(debug_overlay_.config(),
   "cave_brightness",kDefaultCaveBrightness),configured_floating_point(debug_overlay_.config(),
   "cave_ambient_floor",kDefaultCaveAmbientFloor),shaped.ambient,shaped.sunlight_diffuse);
 if(native_world_lighting_valid_&&
    std::memcmp(&native_world_lighting_,&shaped,sizeof(shaped))==0)return true;
 // Pending native commands consume the existing shared constant. Finish that
 // domain before accepting the next one; legacy/UI queues own their transitions.
 const bool previous_dirty=native_world_lighting_dirty_;
 if(FAILED(flush_native())){
  native_world_lighting_dirty_=previous_dirty;
  return false;
 }
 native_world_lighting_=shaped;native_world_lighting_dirty_=true;
 native_world_lighting_valid_=true;
 return true;
}

// Emissive-surface lights for the active map at the current F10 level, from
// the shared terrain grids (rebuilt per terrain source). Runs once per frame
// from the world flush, where the terrain source is read for the other
// per-map features; set_native_point_lights publishes the result next frame.
void Renderer::refresh_emissive_lights() noexcept try{
 if(emissive_update_frame_==native_frame_index_)return;
 emissive_update_frame_=native_frame_index_;
 int level=std::clamp(configured_integer(debug_overlay_.config(),"emissive_lights",1),0,2);
 // With the lava glow lights off, the clusters are still found for the
 // bounce light (cave_irradiance.h), which never needs that option; they
 // are not published as point lights then (set_native_point_lights).
 if(level<=0){
  if(configured_integer(debug_overlay_.config(),"cave_gi",2)<=0)return;
  level=1;
 }
 AmbientWorldSource inputs;
 if(!bridge_ambient_world_source(inputs)||!inputs.terrain)return;
 // Particle textures may fail independently; the CPU tile list is kept either way.
 rebuild_ambient_particle_map(inputs,bridge_world_fog_snapshot().environment);
 if(ambient_terrain_.expired())return;
 // The light colour is the lava the liquid shader draws: its detail map's
 // heat samples exist once that map is generated (also on demand here).
 if(liquid_lava_samples_.empty()&&!emissive_tiles_.empty())ensure_liquid_normal_map();
 const bool rebuild=emissive_generation_!=ambient_map_generation_||emissive_level_!=level;
 if(!rebuild&&(emissive_colors_sampled_||liquid_lava_samples_.empty()))return;
 if(rebuild)emissive_clusters_=emissive::cluster_tiles(emissive_tiles_,ambient_tiles_x_,level>=2?2u:4u);
 emissive_colors_.resize(emissive_clusters_.size());
 for(std::size_t i=0;i<emissive_clusters_.size();++i)
  emissive_colors_[i]=emissive::rendered_color(emissive_clusters_[i].emission,liquid_lava_samples_);
 emissive_colors_sampled_=!liquid_lava_samples_.empty();
 emissive_generation_=ambient_map_generation_;emissive_level_=level;
 char line[200];
 const auto colour=emissive_colors_.empty()?std::array<float,3>{}:emissive_colors_.front();
 std::snprintf(line,sizeof(line),"EID3D11 emissive lights: %zu lights from %zu emissive tiles (level %d), lava colour %.2f %.2f %.2f (%s).\n",
  emissive_clusters_.size(),emissive_tiles_.size(),level,colour[0],colour[1],colour[2],
  emissive_colors_sampled_?"sampled":"ramp default");
 OutputDebugStringA(line);
 if(const auto length=std::strlen(line))line[length-1]=0; // the event log adds its own line end
 bridge_append_probe_event("emissive_lights",line);
}catch(const std::bad_alloc&){
 emissive_clusters_.clear();emissive_colors_.clear();emissive_generation_=UINT64_MAX;
 OutputDebugStringA("EID3D11 emissive lights: out of memory; emissive lighting omitted on this map.\n");
}

bool Renderer::set_native_point_lights(
    const std::vector<RetainedWorldPointLight>& lights,double world_time) noexcept {SCENE_ENTRY("set_native_point_lights");
 const auto& config=debug_overlay_.config();
 const bool hero_light=configured_boolean(config,"hero_light",false);
 const bool emissive=configured_integer(config,"emissive_lights",1)>0&&!ambient_menu_&&
  emissive_generation_==ambient_map_generation_;
 const std::size_t emissive_count=emissive?emissive_clusters_.size():0;
 if(lights.size()>UINT_MAX/sizeof(NativePointLightGpu)-emissive_count)return false;
 std::vector<NativePointLightGpu> next;std::vector<RetainedWorldPointLight> next_inputs;
 try {
  // A strike light not listed last frame is a new game lightning strike: the
  // owned sky flashes the deck above it (sky_strike_light, game seconds).
  std::vector<std::array<std::int32_t,4>> strikes;
  for(const auto& light:lights){
   if(!sky_strike_light(light))continue;
   const std::array<std::int32_t,4> key{light.source_id,static_cast<std::int32_t>(std::lround(light.center[0]*2)),
    static_cast<std::int32_t>(std::lround(light.center[1]*2)),static_cast<std::int32_t>(std::lround(light.center[2]*2))};
   if(std::isfinite(world_time)&&std::find(sky_strike_keys_.begin(),sky_strike_keys_.end(),key)==sky_strike_keys_.end()){
    sky_strike_seconds_=world_time*.055;sky_strike_xy_[0]=light.center[0];sky_strike_xy_[1]=light.center[1];}
   strikes.push_back(key);
  }
  sky_strike_keys_.swap(strikes);
  next_inputs=lights;next.reserve(lights.size()+emissive_count);
  for(const auto& light:lights){
   if(!std::isfinite(light.radius)||light.radius<=0)return false;
   for(float value:light.center)if(!std::isfinite(value))return false;
   for(float value:light.color)if(!std::isfinite(value))return false;
   const double inverse_radius_squared=1.0/
       (static_cast<double>(light.radius)*light.radius);
   if(!std::isfinite(inverse_radius_squared)||inverse_radius_squared<=0||
      inverse_radius_squared>std::numeric_limits<float>::max())return false;
   NativePointLightGpu gpu{};
   gpu.center_inverse_radius_squared={light.center[0],light.center[1],
       light.center[2],static_cast<float>(inverse_radius_squared)};
   const float illumination=light.hero&&!hero_light?0.f:1.f;
   gpu.color={light.color[0]*illumination,light.color[1]*illumination,light.color[2]*illumination,
       native_point_shadows_enabled_&&light.casts_shadow?1.f:0.f};
   if(light.casts_shadow){
    const auto shadow_center=retained_point_shadow_center(light);
    for(float value:shadow_center)if(!std::isfinite(value))return false;
    // w: cube range the shadow was baked with (published by the bridge).
    gpu.shadow_center={shadow_center[0],shadow_center[1],shadow_center[2],light.radius};
   }
   next.push_back(gpu);
  }
  const auto matches=point_shadow_matches(native_point_light_inputs_,lights);
  for(std::size_t i=0;i<matches.size();++i)if(matches[i]<native_point_lights_.size())
   {next[i].shadow_slots=native_point_lights_[matches[i]].shadow_slots;
    next[i].shadow_center[3]=native_point_lights_[matches[i]].shadow_center[3];}
  // Emissive-surface lights follow the game's lights (whose indices the point
  // shadow publication addresses); they never cast shadows. The pulse runs on
  // the world clock in the liquid shader's game seconds, so it freezes on pause.
  // colour.w = -1 marks them for the shaders: their light is added after the
  // game's clamped lighting sum (visible in daylight), in the rendered lava colour.
  if(emissive_count){
   const float strength=.45f*std::clamp(configured_floating_point(config,"emissive_light_strength",1.f),0.f,3.f);
   const float seconds=std::isfinite(world_time)?static_cast<float>(std::fmod(world_time*.055,1.0e5)):0.f;
   const std::uint32_t cell_tiles=emissive_level_>=2?2u:4u;
   for(std::size_t i=0;i<emissive_clusters_.size();++i){
    const auto& cluster=emissive_clusters_[i];
    const auto colour=i<emissive_colors_.size()?emissive_colors_[i]:emissive::rendered_color(cluster.emission,{});
    const auto shape=emissive::light_shape(cluster,cell_tiles,colour);
    const float scale=strength*emissive::flicker(cluster.cell,seconds);
    NativePointLightGpu gpu{};
    gpu.center_inverse_radius_squared={shape.centre[0],shape.centre[1],shape.centre[2],1.f/(shape.radius*shape.radius)};
    gpu.color={shape.color[0]*scale,shape.color[1]*scale,shape.color[2]*scale,-1.f};
    next.push_back(gpu);
   }
  }
  emissive_published_=emissive_count;
 }catch(const std::bad_alloc&){return false;}
 if(next.size()==native_point_lights_.size()&&
    std::equal(next.begin(),next.end(),native_point_lights_.begin(),[](const auto& a,const auto& b){
     return native_point_light_inputs_equal(a,b)&&a.shadow_slots==b.shadow_slots;})){
  native_point_light_inputs_=std::move(next_inputs);return true;}
 // A queued world draw consumes the previous completed scene-light state.
 if(FAILED(flush_native()))return false;
 native_point_lights_.swap(next);native_point_light_inputs_=std::move(next_inputs);
 native_point_lights_dirty_=true;
 return true;
}

Renderer::NativeMaterialKey Renderer::native_material_key(const FixedState& st,SemanticKind kind,
 NativeOpacityClass opacity,bool lighting,bool textured,bool texture_array,
 std::uint64_t backing,std::uint64_t generation,UINT contact_map) const noexcept{
 NativeMaterialKey material_key{};
 material_key.contact_map=native_shadow_collecting_?0:contact_map;
  material_key.state={
   static_cast<UINT>(kind),static_cast<UINT>(opacity),
   st.render[D3DRENDERSTATE_CULLMODE],st.render[D3DRENDERSTATE_FILLMODE],
   st.render[D3DRENDERSTATE_ZENABLE],st.render[D3DRENDERSTATE_ZWRITEENABLE],
   st.render[D3DRENDERSTATE_ZFUNC],st.render[D3DRENDERSTATE_ALPHABLENDENABLE],
   st.render[D3DRENDERSTATE_SRCBLEND],st.render[D3DRENDERSTATE_DESTBLEND],
   st.render[D3DRENDERSTATE_ALPHATESTENABLE],st.render[D3DRENDERSTATE_ALPHAFUNC],
   st.render[D3DRENDERSTATE_ALPHAREF],st.stage[0][D3DTSS_COLOROP],
   st.stage[0][D3DTSS_ALPHAOP],st.stage[0][D3DTSS_COLORARG1],
   st.stage[0][D3DTSS_COLORARG2],st.stage[0][D3DTSS_ALPHAARG1],
   st.stage[0][D3DTSS_ALPHAARG2],st.stage[0][D3DTSS_ADDRESSU],
   st.stage[0][D3DTSS_ADDRESSV],st.stage[0][D3DTSS_MINFILTER],
   st.stage[0][D3DTSS_MAGFILTER],st.stage[0][D3DTSS_MIPFILTER],
   st.render[D3DRENDERSTATE_FOGENABLE],
   st.render[D3DRENDERSTATE_FOGCOLOR],st.render[D3DRENDERSTATE_TEXTUREFACTOR],
   native_specular_addition_enabled(st.render[D3DRENDERSTATE_SPECULARENABLE]),
   textured?1u:0u,lighting?1u:0u};
  material_key.texture_backing=backing;
  material_key.texture_generation=generation;
  material_key.shadow=native_shadow_collecting_?1u:0u;
  if(native_shadow_collecting_){
   // These are the depth pass's existing compatibility fields. Color ordering,
   // lighting mode and actor/mesh/pose identity cannot fragment a depth group.
   material_key.state={};
   material_key.state[0]=st.render[D3DRENDERSTATE_CULLMODE];
   material_key.state[1]=st.render[D3DRENDERSTATE_FILLMODE];
   if(st.render[D3DRENDERSTATE_ALPHATESTENABLE]){
    auto& key=material_key.state;key[2]=1;key[3]=st.render[D3DRENDERSTATE_ALPHAFUNC];key[4]=st.render[D3DRENDERSTATE_ALPHAREF];
    key[5]=st.stage[0][D3DTSS_ALPHAOP];key[6]=st.stage[0][D3DTSS_ALPHAARG1];key[7]=st.stage[0][D3DTSS_ALPHAARG2];
    key[8]=st.stage[0][D3DTSS_ADDRESSU];key[9]=st.stage[0][D3DTSS_ADDRESSV];
    key[10]=st.stage[0][D3DTSS_MINFILTER];key[11]=st.stage[0][D3DTSS_MAGFILTER];key[12]=st.stage[0][D3DTSS_MIPFILTER];
    key[13]=st.render[D3DRENDERSTATE_TEXTUREFACTOR]>>24;key[14]=texture_array?1u:0u;
   }
  }
 return material_key;
}

std::uint64_t Renderer::native_material_state_key(const NativeMaterialKey& material_key) noexcept{
 std::uint64_t state_key=hash_bytes(1469598103934665603ull,material_key.state.data(),
   material_key.state.size()*sizeof(material_key.state[0]));
  state_key=hash_bytes(state_key,&material_key.shadow,sizeof(material_key.shadow));
  state_key=hash_bytes(state_key,&material_key.contact_map,sizeof(material_key.contact_map));
  state_key=hash_bytes(state_key,&material_key.texture_backing,sizeof(material_key.texture_backing));
  state_key=hash_bytes(state_key,&material_key.texture_generation,sizeof(material_key.texture_generation));
 return state_key;
}

HRESULT Renderer::queue_native_impl(const NativeGeometryView& view,const FixedState& st,SurfaceImage* texture,NativeGeometryResult* result,bool prepared,ID3D11ShaderResourceView* direct_texture,std::uint64_t direct_texture_identity,std::uint64_t direct_texture_generation,bool direct_array,UINT direct_slice,UINT* registered,const D3D12TextureRef* d3d12_texture){
 if(result)*result={};
 const auto fail=[&](HRESULT hr,const char* operation,bool texture_failure=false){
  return native_geometry_failure(result,hr,operation,texture_failure);
 };
 // A prepare-only view is handled by queue_native_prepared; it never draws.
 if(view.prepare_only)return fail(E_INVALIDARG,"prepare.unprepared_route");
 if(reject_native_contribution(view,result))return S_OK;
 if(view.world.record_revision){
  // Persistent previews read the shared clip constants on the GPU. Place
  // that camera copy, rather than the per-instance matrix they replace.
  auto clip=view.world.world_to_clip;
  if(camp::composing&&camp::primary_output)
   camp::compose_clip(clip.data(),camp::visual,float(canvas_width()),float(canvas_height()));
  if(!set_native_world_clip(clip))return fail(E_FAIL,"world.clip_transition");
 }
 if(native_shadow_collecting_){
  if(native_shadow_failed_||!view.world.enabled)return fail(E_INVALIDARG,"shadow.world_input");
  if(!(view.contribution.light_mask&native_shadow_dirty_mask_))return S_OK;
  if(!st.render[D3DRENDERSTATE_ZWRITEENABLE]||!st.render[D3DRENDERSTATE_ZENABLE])return fail(E_INVALIDARG,"shadow.depth_state");
  if(!st.render[D3DRENDERSTATE_ALPHATESTENABLE]){texture=nullptr;direct_texture=nullptr;direct_texture_identity=0;direct_texture_generation=0;}
 }
 if(view.world.enabled){
  if(!view.contribution.known||!view.prepared_mesh||!view.prepared_material||
     !valid_prepared_identity(view.prepared_identity))return fail(E_INVALIDARG,"world.prepared_identity");
  prepared=true;
 }else if(view.contribution.known&&!view.contribution.color)return fail(E_INVALIDARG,"contribution.non_world_light");
 PerfScope perf(&perf_draw_ticks_);if(perf_enabled())++perf_draw_calls_;remember_debug_projection(st,view.semantic_kind);if(d3d12_?!native12_.ready:(!native_vs_||!native_ps_||!native_build_cs_))return fail(E_NOTIMPL,"native_pipeline");NativeMesh* mesh=nullptr;HRESULT hr=prepared?get_native_prepared_mesh(view,mesh):get_native_mesh(view,mesh);if(FAILED(hr)||!mesh)return fail(FAILED(hr)?hr:E_FAIL,prepared?"get_native_prepared_mesh":"get_native_mesh");const std::size_t upload_start=native_pending_upload_.size();UINT morph_offset=0,terrain_phase_offset=0;if(FAILED(hr=append_native_dynamic(view,morph_offset,terrain_phase_offset)))return fail(hr,"append_native_dynamic");
 if(view.lighting.enabled&&!native_world_lighting_valid_){native_pending_upload_.resize(upload_start);return fail(E_INVALIDARG,"world_lighting");}
 if(view.semantic_kind==SemanticKind::terrain&&view.terrain.fog_enabled){
  if(!std::isfinite(view.terrain.fog_distance)||view.terrain.fog_distance<=0){native_pending_upload_.resize(upload_start);return fail(E_INVALIDARG,"terrain.fog_distance");}
  for(float value:view.terrain.fog_min)if(!std::isfinite(value)){native_pending_upload_.resize(upload_start);return fail(E_INVALIDARG,"terrain.fog_min");}
  for(float value:view.terrain.fog_max)if(!std::isfinite(value)){native_pending_upload_.resize(upload_start);return fail(E_INVALIDARG,"terrain.fog_max");}
 }
 auto object_to_clip=view.object_to_clip;
 if(widescreen::interface_owned&&!view.world.enabled)
  widescreen::compose_hud_clip(object_to_clip.data(),widescreen::hud_blit,float(canvas_width()),float(canvas_height()));
 NativeQueuedDraw draw{};if(widescreen::interface_owned)draw.hud_composition=widescreen::hud_blit;draw.mesh=mesh;draw.geometry_page=mesh->allocation.page;draw.index_count=view.index_count;draw.component_count=view.component_count;std::memcpy(&draw.instance.object_to_clip,object_to_clip.data(),sizeof(draw.instance.object_to_clip));jitter_clip(&draw.instance.object_to_clip._11);if(view.lighting.enabled)std::memcpy(&draw.instance.object_to_world,view.lighting.object_to_world.data(),sizeof(draw.instance.object_to_world));draw.instance.geometry_offsets={mesh->position_offset,mesh->normal_offset,mesh->texture_coordinate_offset,mesh->component_offset};draw.instance.color_offsets={mesh->diffuse_offset,mesh->specular_offset};draw.instance.uv_shift[0]=view.u_shift;draw.instance.uv_shift[1]=view.v_shift;draw.instance.flags=view.flags&~native_preview_specular_rgb_flag;draw.instance.default_diffuse=view.default_diffuse;draw.instance.default_specular=view.default_specular;draw.instance.morph_offset=morph_offset;draw.instance.morph_count=view.morph_count;draw.instance.terrain_vertex_offset=mesh->terrain_vertex_offset;draw.instance.terrain_phase_offset=terrain_phase_offset;native_terrain_values(draw.instance,view.terrain,view.semantic_kind==SemanticKind::terrain && !configured_boolean(debug_overlay_.config(),"terrain_normal_sunlight",true));draw.instance.lighting_enabled=view.lighting.enabled;std::copy(view.lighting.emissive.begin(),view.lighting.emissive.end(),draw.instance.emissive);std::copy(view.lighting.specular.begin(),view.lighting.specular.end(),draw.instance.specular);draw.instance.material_alpha=view.lighting.material_alpha;std::copy(view.lighting.material_diffuse.begin(),view.lighting.material_diffuse.end(),draw.instance.material_diffuse);
 std::uint32_t preview_rgb=0;
 if(camp_inventory_preview_specular_rgb(preview_rgb)){
  draw.instance.flags|=native_preview_specular_rgb_flag;
  draw.instance.default_specular=(draw.instance.default_specular&0xff000000u)|(preview_rgb&0x00ffffffu);
 }
 std::memcpy(&draw.instance.padding[3],&view.contact_world_height,sizeof(float));
 const UINT passes=(!native_shadow_collecting_&&(!view.contribution.known||view.contribution.color)?1u:0u)|
  (UINT(view.contribution.light_mask & (native_shadow_collecting_?native_shadow_dirty_mask_:31u))<<8);
 if(view.world.enabled){
  draw.instance.world_geometry=native_world_geometry|(view.world.object_space?native_world_object_space:0u);
  draw.instance.shape_count=view.world.shape_count;
  draw.instance.position_groups=view.position_groups;
  std::copy(view.world.shape.begin(),view.world.shape.end(),draw.instance.shape);
  std::memcpy(&draw.instance.object_to_world,view.world.object_to_world.data(),sizeof(D3DMATRIX));
  std::memcpy(&draw.instance.object_to_clip,view.world.world_to_clip.data(),sizeof(D3DMATRIX));
  hr=resolve_native_pose(view.world.pose,view.prepared_identity,view.world.pose_node,draw);
  if(FAILED(hr)){native_pending_upload_.resize(upload_start);return fail(hr,"resolve_native_pose");}
  // World records carry no morph (resolve_native_world_range rejects a
  // legacy morph stream). The per-frame dynamic offset must not reach the
  // persistent instance: it would change the record every frame.
  draw.instance.morph_offset=0;
  std::copy(view.bounds_minimum.begin(),view.bounds_minimum.end(),draw.bounds_min);
  std::copy(view.bounds_maximum.begin(),view.bounds_maximum.end(),draw.bounds_max);
  hr=resolve_native_world_range(view,draw);
  if(FAILED(hr)){native_pending_upload_.resize(upload_start);return fail(hr,"resolve_native_world_range");}
  // Object space has no morph (resolve_native_world_range rejects one): its
  // morph words carry the sway anchor instead (treeSwayBase).
  if(view.world.object_space){
   std::memcpy(&draw.instance.morph_offset,&view.world.sway_root[0],sizeof(float));
   std::memcpy(&draw.instance.morph_count,&view.world.sway_root[1],sizeof(float));
   draw.instance.morph_fraction=view.world.sway_root[2];
  }
 }else{
  float morph_min[3]={0,0,0},morph_max[3]={0,0,0};
  for(std::uint32_t index=0;index<view.morph_count;++index){
   const float delta[3]={view.morphs[index].x,view.morphs[index].y,view.morphs[index].z};
   for(UINT axis=0;axis<3;++axis)if(std::isfinite(delta[axis])){
    morph_min[axis]=std::min(morph_min[axis],delta[axis]);morph_max[axis]=std::max(morph_max[axis],delta[axis]);
   }
  }
  for(UINT axis=0;axis<3;++axis){draw.bounds_min[axis]=mesh->bounds_min[axis]+morph_min[axis];draw.bounds_max[axis]=mesh->bounds_max[axis]+morph_max[axis];}
 }
 if(!view.world.record_revision&&camp::composing&&camp::primary_output){
  camp::compose_clip(&draw.instance.object_to_clip._11,camp::visual,float(canvas_width()),float(canvas_height()));
  draw.camp_clip=camp::visual.clip;
 }
 if(!view.world.enabled&&view.terrain.waves_enabled&&std::isfinite(view.terrain.wind_force)&&
    std::isfinite(view.terrain.maximum_wind_multiplier)){
  const float maximum=std::abs(view.terrain.wind_force)*
   std::max(0.0f,view.terrain.maximum_wind_multiplier);
  draw.bounds_min[0]-=maximum*3.0f;draw.bounds_max[0]+=maximum*3.0f;
  draw.bounds_min[1]-=maximum*3.0f;draw.bounds_max[1]+=maximum*3.0f;
  draw.bounds_min[2]-=maximum*0.25f;draw.bounds_max[2]+=maximum*0.25f;
 }
 if(result&&(view.flags&1u)){RECT extents{};const HRESULT extent_hr=calculate_native_aabb_screen_extents(draw.bounds_min,draw.bounds_max,object_to_clip,st.viewport,extents);if(FAILED(extent_hr)){native_pending_upload_.resize(upload_start);return fail(extent_hr,"screen_extents");}result->visible=extent_hr==S_OK;result->screen_extents_valid=true;result->screen_extents=extents;}
 D3D12TextureRef surface_d3d12; // D3D12 backend: a bound surface's texture (never an array slice there)
 ComPtr<ID3D11ShaderResourceView> texture_srv;bool texture_array=direct_array;UINT texture_slice=direct_slice;if(direct_texture){texture_srv=direct_texture;}if(view.semantic_kind==SemanticKind::terrain){if(view.ordering==NativeOrdering::reorderable)++native_frame_stats_.native_terrain_reorderable_contracts;else ++native_frame_stats_.native_terrain_preserve_contracts;if(texture||direct_texture)++native_frame_stats_.native_terrain_textured_draws;else ++native_frame_stats_.native_terrain_untextured_draws;}if(texture&&!direct_texture){texture->world_mip_use=!scene_composited_&&view.semantic_kind!=SemanticKind::ui;if(!texture->world_mip_use)texture->static_mip_hint=false;if(!d3d12_&&view.semantic_kind==SemanticKind::terrain&&view.ordering==NativeOrdering::reorderable){hr=get_native_texture_array_slice(*texture,texture_srv,texture_slice,view.terrain_page,view.terrain_page_count);if(hr==S_OK)texture_array=true;else if(FAILED(hr)){native_pending_upload_.resize(upload_start);return fail(hr,"get_native_texture_array_slice",true);}}if(!texture_array){if(d3d12_&&view.semantic_kind==SemanticKind::terrain){if(FAILED(hr=terrain_texture_d3d12(*texture,view.terrain_page,surface_d3d12))){native_pending_upload_.resize(upload_start);return fail(hr,"terrain_texture_d3d12",true);}}else{if(FAILED(hr=upload(*texture))){native_pending_upload_.resize(upload_start);return fail(hr,"upload_surface_texture",true);}texture_srv=texture->srv;if(d3d12_)surface_d3d12=texture->d3d12;}}}draw.instance.padding[0]=texture_slice;draw.instance.padding[1]=texture_array?1u:0u;
 const std::uint64_t current_texture_backing=direct_texture_identity?direct_texture_identity:(texture_array?
  static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texture_srv.Get())):
  (texture&&texture->id?texture->id:
   static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texture_srv.Get()))));
 const std::uint64_t current_texture_generation=direct_texture_identity?
  direct_texture_generation:(texture_array?0:(texture?texture->generation:0));
 FixedState shadow_state;
 const FixedState* material_state=nullptr;
 const NativeOpacityClass opacity=native_material_state(st,view.lighting.material_alpha,
  native_alpha_evidence(view,st,direct_texture!=nullptr && texture==nullptr),native_shadow_collecting_,shadow_state,material_state);
 if(native_record_takes_lod(view,opacity,mesh->lod_levels)){
  draw.instance.world_geometry|=native_world_lod;
  if(!view.world.object_space)draw.instance.second_morph_offset=draw.world_base;
 }
 if(native_record_takes_impostor(view,opacity,mesh->sphere_block)){
  draw.instance.world_geometry|=native_world_impostor;draw.instance.second_morph_offset=view.world.impostor-1;
 }
 const bool native_world=view.semantic_kind==SemanticKind::figure||
  view.semantic_kind==SemanticKind::terrain||
  view.semantic_kind==SemanticKind::simple_geometry;
 bool reorderable=view.ordering==NativeOrdering::reorderable||
  (view.ordering==NativeOrdering::derive_from_state&&native_world&&
   opacity!=NativeOpacityClass::ordered);
 UINT bucket_index=UINT_MAX;
 bool prepared_material_hit=false;
 if(prepared&&view.prepared_material){
  const auto& handle=*view.prepared_material;
  if(handle.slot<native_prepared_material_slots_.size()){
   auto& slot=native_prepared_material_slots_[handle.slot];
   if(slot.active&&slot.generation==handle.generation&&
      same_prepared_identity(slot.identity,view.prepared_identity,false)&&
      slot.presentation_serial==view.prepared_identity.presentation_serial&&
      slot.material_key.texture_backing==current_texture_backing&&
      slot.material_key.texture_generation==current_texture_generation&&
      slot.material_key.shadow==UINT(native_shadow_collecting_)&&
      slot.material_key.state[native_shadow_collecting_?0:2]==
       material_state->render[D3DRENDERSTATE_CULLMODE]&&
      (native_shadow_collecting_||slot.material_key.state[29]==(view.lighting.enabled?1u:0u))&&
      slot.opacity==opacity&&slot.reorderable==reorderable&&
      slot.queue_epoch==native_queue_epoch_&&
      slot.state_bucket<native_state_buckets_.size()&&
      native_state_buckets_[slot.state_bucket].occupied&&
      native_state_buckets_[slot.state_bucket].material_key==slot.material_key){
    bucket_index=slot.state_bucket;slot.last_used_frame=native_frame_index_;
    prepared_material_hit=true;
#ifdef EID3D11_TESTING
    ++test_native_prepared_material_hits_;
#endif
   }
  }
 }
 if(!prepared_material_hit){
  // D3D12 backend (bindless): the texture is the draw's own index, not part
  // of its bucket, so draws of one state share a batch across textures.
  const auto material_key=native_material_key(*material_state,view.semantic_kind,opacity,view.lighting.enabled!=0,
   texture_srv!=nullptr||(d3d12_texture&&*d3d12_texture)||surface_d3d12,texture_array,d3d12_?0:current_texture_backing,
   d3d12_?0:current_texture_generation,view.contact_map);
  const auto state_key=native_material_state_key(material_key);
  if(prepared){
   hr=resolve_native_prepared_material(view,*material_state,texture,texture_srv.Get(),
    texture_array,opacity,reorderable,material_key,state_key,bucket_index,
    d3d12_texture?d3d12_texture:(surface_d3d12?&surface_d3d12:nullptr));
   if(FAILED(hr)){native_pending_upload_.resize(upload_start);return fail(hr,"resolve_native_prepared_material");}
  }else{
   hr=get_native_material_group(material_key,*material_state,texture_srv.Get(),texture_array,view.semantic_kind,opacity,state_key,bucket_index,d3d12_texture?d3d12_texture:(surface_d3d12?&surface_d3d12:nullptr));
   if(FAILED(hr)){native_pending_upload_.resize(upload_start);return fail(hr,"material_bucket_allocation");}
  }
 }
 if(d3d12_){
  // The material texture's bindless index in the instance's texture
  // reference (x: index, y: 0); NATIVE_SAMPLE reads it (native_shader.h).
  const D3D12TextureRef* texture12=d3d12_texture&&*d3d12_texture?d3d12_texture:(surface_d3d12?&surface_d3d12:nullptr);
  if(texture12){
   const std::uint32_t index=d3d12_->bindless_index(**texture12);
   if(index==UINT32_MAX){native_pending_upload_.resize(upload_start);return fail(E_OUTOFMEMORY,"bindless_index");}
   draw.instance.padding[0]=index;
   // Its material profile (S1) by bindless index, 0 none; refreshed on the
   // record when the profiles change (refresh_material_profile_indices_d3d12).
   draw.material_backing=current_texture_backing;draw.instance.padding[1]=material_profile_index_d3d12(current_texture_backing);
   // The record holds the texture its index names: a D3D12 material group
   // keeps only its first draw's texture, so a warm record (no lookup) lost
   // its texture to retire_world_textures after a reload and sampled a freed
   // bindless index (the flickering HUD meshes and figures).
   draw.texture12=*texture12;
  }else{
   // No D3D12 texture: the executor's permanent null view (index 0 used to
   // be whichever texture first took it, a stale descriptor once freed: the
   // cave TDR). A textured material without one is named, never hidden.
   draw.instance.padding[0]=kD3D12NullTextureIndex;draw.instance.padding[1]=0;
   if(texture_srv!=nullptr||texture_array)d3d12_->omit("native draw textures with no D3D12 texture (drawn with the null view)",false);
  }
 }
 draw.state_bucket=bucket_index;draw.reorderable=reorderable;draw.preserve_order=view.ordering==NativeOrdering::preserve;
 if(FAILED(hr=resolve_native_terrain_phases(view,draw)))return fail(hr,"terrain.phases");
 if(FAILED(hr=resolve_native_instance(view,draw)))return fail(hr,"resolve_native_instance");
 UINT record_index=draw.persistent_instance;
 try{
  if(record_index==UINT_MAX){
   record_index=allocate_native_instance_record();if(record_index==UINT_MAX)return fail(E_OUTOFMEMORY,"draw_record_capacity");
   auto& record=native_instance_records_[record_index];record.owned=false;bind_native_material_group(record,draw.state_bucket);record.command=draw;
   record.command.persistent_instance=record_index;record.descriptor=native_draw_descriptor(draw,record_index);
   native_instance_dirty_.push_back(record_index);record.scheduled=true;
#ifdef EID3D11_TESTING
   ++test_native_command_values_written_;
#endif
  }
  if(registered){*registered=record_index;return S_OK;}
  enqueue_native_record({record_index,passes,view.semantic_kind==SemanticKind::ui||view.interface_preview});
   if(texture&&!direct_texture)sampled_surface_pixels(*texture);}

 catch(const std::bad_alloc&){
  if(record_index<native_instance_records_.size()&&!native_instance_records_[record_index].references)
   release_native_instance(record_index);
  native_pending_upload_.resize(upload_start);return fail(E_OUTOFMEMORY,"draw_queue_allocation");
 }
 return S_OK;
}

void Renderer::enqueue_native_record(NativeSelectedRecord selected){
#ifdef EID3D11_TESTING
 if(test_native_enqueue_failure_&&!--test_native_enqueue_failure_)throw std::bad_alloc();
#endif
 native_pending_draws_.push_back(selected);
 auto& record=native_instance_records_[selected.record];auto& draw=record.command;
 ++record.references;++draw.mesh->pending_references;
 if(draw.pose_allocation<native_pose_ranges_.size())++native_pose_ranges_[draw.pose_allocation].references;
 if(draw.animation_allocation<native_animation_ranges_.size())++native_animation_ranges_[draw.animation_allocation].references;
 if(draw.world_allocation<native_world_ranges_.size())++native_world_ranges_[draw.world_allocation].references;
 ++native_frame_stats_.native_logical_draws;
 if(draw.reorderable)++native_frame_stats_.native_reorderable_draws;
 else ++native_frame_stats_.native_ordered_draws;
 const auto opacity=native_state_buckets_[draw.state_bucket].opacity;
 if(opacity==NativeOpacityClass::opaque)++native_frame_stats_.native_opaque_draws;
 else if(opacity==NativeOpacityClass::cutout)++native_frame_stats_.native_cutout_draws;
}

HRESULT Renderer::draw_native(const NativeGeometryView& view,const FixedState& state,SurfaceImage* texture){SCENE_ENTRY("draw_native");HRESULT hr=queue_native(view,state,texture,nullptr);return FAILED(hr)?hr:flush_native();}

void Renderer::discard_native_unqueued_work() noexcept{
 // Registration does not pin GPU work. Preserve exactly the generations that
 // the real ordering queue still references, including older COW generations.
 native_instance_dirty_.erase(std::remove_if(native_instance_dirty_.begin(),native_instance_dirty_.end(),
  [&](UINT index){
   auto& record=native_instance_records_[index];if(record.references)return false;
   record.scheduled=false;if(!record.owned)release_native_instance(index);return true;
  }),native_instance_dirty_.end());
 native_pose_work_.erase(std::remove_if(native_pose_work_.begin(),native_pose_work_.end(),
  [&](const NativePoseWork& work){
   const auto& range=native_pose_ranges_[work.allocation];
   if(range.references&&range.generation==work.allocation_generation)return false;
   auto& slot=native_pose_slots_[work.slot];
   if(slot.allocation==work.allocation&&slot.revision==work.revision&&
      range.generation==work.allocation_generation)slot.scheduled=0;
   return true;
  }),native_pose_work_.end());
 // A failed persistent terrain registration appends its phase bytes last.
 // Trim only that abandoned tail; unrelated transient uploads keep their offsets.
 for(auto i=native_terrain_phase_work_.rbegin();i!=native_terrain_phase_work_.rend();++i)
  if(!native_animation_ranges_[i->allocation].references&&
     std::uint64_t(i->offset)+i->count*4ull==native_pending_upload_.size())
   native_pending_upload_.resize(i->offset);
 native_terrain_phase_work_.erase(std::remove_if(native_terrain_phase_work_.begin(),native_terrain_phase_work_.end(),
  [&](const NativeTerrainPhaseWork& work){
   if(native_animation_ranges_[work.allocation].references)return false;
   auto& slot=native_terrain_phases_[work.slot];
   if(slot.allocation==work.allocation&&slot.revision==work.revision)slot.scheduled=false;
   return true;
  }),native_terrain_phase_work_.end());
 trim_native_material_groups();
}

void Renderer::discard_native() noexcept{
 if(scene_queue()){scene_post_command(scene_cmd::DiscardNative{});return;}
 for(UINT group:native_active_groups_)native_state_buckets_[group].selected.clear();
 native_active_groups_.clear();
 for(UINT index:native_instance_dirty_)if(index<native_instance_records_.size()){
  auto& record=native_instance_records_[index];record.scheduled=false;
  if(!record.owned&&!record.references)release_native_instance(index);
 }
 native_instance_dirty_.clear();native_instance_updates_staging_.clear();
 for(const auto& work:native_pose_work_)if(work.slot<native_pose_slots_.size())native_pose_slots_[work.slot].scheduled=0;
 native_pose_work_.clear();native_pose_upload_.clear();discard_native_terrain_phase_work();
 for(const auto& draw:native_pending_draws_)release_native_queued_draw(draw.record);
 native_pending_draws_.clear();trim_native_material_groups();
 native_pending_upload_.clear();native_terrain_phase_allocations_.clear();
 native_draw_order_.clear();
 advance_native_queue_epoch();
}

bool Renderer::checkpoint_native(NativeQueueCheckpoint& checkpoint) const noexcept {
 if(scene_queue()){
  // The scene thread holds the real checkpoint under this id.
  auto& self=const_cast<Renderer&>(*this);
  checkpoint={};checkpoint.scene_token=++self.scene_checkpoint_serial_;
  scene_cmd::TakeNativeCheckpoint command;command.checkpoint_id=checkpoint.scene_token;
  self.scene_post_command(command);
  return true;
 }
 checkpoint.draws=native_pending_draws_.size();
 checkpoint.dynamic_bytes=native_pending_upload_.size();
 checkpoint.epoch=native_queue_epoch_;
 return true;
}

void Renderer::release_native_checkpoint(const NativeQueueCheckpoint& checkpoint) noexcept{
 if(!checkpoint.scene_token)return;
 scene_cmd::ReleaseNativeCheckpoint command;command.checkpoint_id=checkpoint.scene_token;
 scene_post_command(command);
}

void Renderer::rollback_native(const NativeQueueCheckpoint& checkpoint) noexcept {
 if(checkpoint.scene_token){
  scene_cmd::RollbackNative command;command.checkpoint_id=checkpoint.scene_token;
  if(scene_queue())scene_post_command(command);else command.run(*this);
  return;
 }
 SCENE_ENTRY("rollback_native");
 if(checkpoint.epoch!=native_queue_epoch_||checkpoint.draws>native_pending_draws_.size()||
    checkpoint.dynamic_bytes>native_pending_upload_.size()){
  // An internal resource drain ended that queue. Never apply its vector
  // lengths to a later queue, whose valid consumers may be unrelated.
  discard_native_unqueued_work();
  return;
 }
 for(std::size_t index=checkpoint.draws;index<native_pending_draws_.size();++index)
  release_native_queued_draw(native_pending_draws_[index].record);
 native_pending_draws_.resize(checkpoint.draws);
 native_pending_upload_.resize(checkpoint.dynamic_bytes);
 for(auto i=native_terrain_phase_work_.begin();i!=native_terrain_phase_work_.end();){
  if(std::uint64_t(i->offset)+i->count*4ull>checkpoint.dynamic_bytes){
   auto& slot=native_terrain_phases_[i->slot];
   if(slot.allocation==i->allocation&&slot.revision==i->revision)slot.scheduled=false;
   i=native_terrain_phase_work_.erase(i);
  }else ++i;
 }
 discard_native_unqueued_work();
 advance_native_queue_epoch();
}

// ---- Typed native-queue commands (scene_commands_world.h), run on the scene
// thread (or inline in order when a post could not be allocated).
void scene_cmd::DiscardNative::run(Renderer& r){r.discard_native();}
void scene_cmd::TakeNativeCheckpoint::run(Renderer& r){
 Renderer::NativeQueueCheckpoint value{};r.checkpoint_native(value);
 try{r.scene_checkpoints_[checkpoint_id]=value;}catch(...){}
}
void scene_cmd::ReleaseNativeCheckpoint::run(Renderer& r){r.scene_checkpoints_.erase(checkpoint_id);}
void scene_cmd::RollbackNative::run(Renderer& r){
 const auto found=r.scene_checkpoints_.find(checkpoint_id);
 if(found==r.scene_checkpoints_.end()){r.discard_native_unqueued_work();return;}
 const Renderer::NativeQueueCheckpoint value=found->second;r.scene_checkpoints_.erase(found);
 r.rollback_native(value);
}


namespace {
std::mutex lod_snapshot_mutex;
std::shared_ptr<const NativeLodSnapshot> lod_snapshot;
std::atomic<std::uint64_t> lod_request_counter{0},lod_requested{0},lod_requested_epoch{0},lod_consumed{0};
bool publish_lod_snapshot(std::shared_ptr<const NativeLodSnapshot> value) noexcept {
 std::unique_lock<std::mutex> lock(lod_snapshot_mutex,std::try_to_lock);
 if(!lock.owns_lock())return false;
 lod_snapshot=std::move(value);return true;
}
}
std::shared_ptr<const NativeLodSnapshot> native_lod_snapshot() noexcept {
 std::unique_lock<std::mutex> lock(lod_snapshot_mutex,std::try_to_lock);
 return lock.owns_lock()?lod_snapshot:nullptr;
}

std::uint64_t native_lod_request(std::uint64_t epoch) noexcept {
 const auto pending=lod_requested.load(std::memory_order_acquire);
 if(pending>lod_consumed.load(std::memory_order_acquire)&&lod_requested_epoch.load()==epoch)return pending;
 const auto next=lod_request_counter.fetch_add(1,std::memory_order_relaxed)+1;
 lod_requested_epoch.store(epoch,std::memory_order_relaxed);lod_requested.store(next,std::memory_order_release);
 return next;
}
void native_lod_acknowledge(std::uint64_t request) noexcept {lod_consumed.store(request,std::memory_order_release);}

void Renderer::release_native_lod_observer() noexcept {
 native_lod_observations_.Reset();native_lod_observation_uav_.Reset();
 for(auto& slot:native_lod_readbacks_){slot.buffer.Reset();slot.snapshot.reset();}
 native_lod_capture_.reset();native_lod_frame_=native_lod_copied_frame_=UINT64_MAX;
 native_lod_published_frame_=0;native_lod_failed_=false;native_lod_request_=0;
 publish_lod_snapshot(nullptr);
}

void Renderer::begin_native_lod_frame() noexcept try {
 const auto request=lod_requested.load(std::memory_order_acquire);
 if(!request||request==native_lod_request_||request<=lod_consumed.load(std::memory_order_acquire)||
    lod_requested_epoch.load(std::memory_order_relaxed)!=world_state_epoch_||!context_||!device_)return;
 native_lod_request_=request;
 if(native_lod_failed_){
  auto failed=std::make_shared<NativeLodSnapshot>();failed->world_epoch=world_state_epoch_;
  failed->request=request;failed->unavailable=true;native_lod_readbacks_[0].snapshot=std::move(failed);return;
 }
 if(!native_lod_observations_){
  D3D11_BUFFER_DESC desc{};desc.ByteWidth=kNativeLodObservationCapacity*sizeof(NativeLodDecisionGpu);
  desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
  desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
  ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11UnorderedAccessView> uav;
  HRESULT hr=device_->CreateBuffer(&desc,nullptr,&buffer);
  D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_TYPELESS;
  view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;view.Buffer.NumElements=desc.ByteWidth/4;
  view.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
  if(SUCCEEDED(hr))hr=device_->CreateUnorderedAccessView(buffer.Get(),&view,&uav);
  desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
  for(auto& slot:native_lod_readbacks_)if(SUCCEEDED(hr))hr=device_->CreateBuffer(&desc,nullptr,&slot.buffer);
  if(FAILED(hr)){
   native_lod_failed_=true;
   auto failed=std::make_shared<NativeLodSnapshot>();failed->world_epoch=world_state_epoch_;failed->request=request;failed->unavailable=true;
   native_lod_readbacks_[0].snapshot=std::move(failed);return;
  }
  native_lod_observations_=std::move(buffer);native_lod_observation_uav_=std::move(uav);
 }
 const UINT zero[4]{};context_->ClearUnorderedAccessViewUint(native_lod_observation_uav_.Get(),zero);
 native_lod_row_indices_.fill(UINT_MAX);
 native_lod_capture_=std::make_shared<NativeLodSnapshot>();
 native_lod_capture_->frame=native_frame_index_;native_lod_capture_->world_epoch=world_state_epoch_;
 native_lod_capture_->request=request;native_lod_frame_=native_frame_index_;
} catch(...) {native_lod_failed_=true;native_lod_capture_.reset();}

bool Renderer::begin_native_lod_capture(const NativeSubmissionLayout& layout) noexcept try {
 if(!native_lod_capture_||native_lod_frame_!=native_frame_index_||native_lod_failed_||
    scene_composited_||hit_test_active_||native_shadow_collecting_||native_point_shadow_active_||native_rain_active_||
    rtv_.Get()!=scene_composite_.target()||dsv_.Get()!=world_dsv_.Get()||!native_world_clip_valid_)return false;
 // The host publishes the player's world camera before drawing the scene.
 // Matching both camera and targets admits its split colour flushes (including
 // neighbours), never a preview, picking or off-screen camera in the same frame.
 const auto camera=bridge_scene_world_to_clip_snapshot();
 if(!camera.valid||!camera.scene_frame_serial||camera.world_to_clip!=native_world_clip_)return false;
 for(const auto& work:layout.work){
  if(work.record>=native_instance_records_.size())continue;
  const auto& identity=native_instance_records_[work.record].lod_observer;if(!identity)continue;
  if(work.record>=kNativeLodObservationCapacity){native_lod_capture_->truncated=true;continue;}
  auto& index=native_lod_row_indices_[work.record];
  if(index==UINT_MAX){index=UINT(native_lod_capture_->rows.size());native_lod_capture_->rows.push_back({work.record,identity,{}});}
  else native_lod_capture_->rows[index].identity=identity; // record reused in this frame: last submission wins
 }
 return true;
} catch(...) {native_lod_failed_=true;native_lod_capture_.reset();return false;}

void Renderer::finish_native_lod_frame() noexcept try {
 if(!context_)return;
 // Three independent slots: never overwrite an in-flight copy, never Flush or wait.
 for(auto& slot:native_lod_readbacks_)if(slot.snapshot){
  if(slot.snapshot->unavailable){
   if(publish_lod_snapshot(slot.snapshot))slot.snapshot.reset();
   continue;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  const HRESULT hr=context_->Map(slot.buffer.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
  if(hr==DXGI_ERROR_WAS_STILL_DRAWING)continue;
  if(SUCCEEDED(hr)){
   const auto* rows=static_cast<const NativeLodDecisionGpu*>(mapped.pData);
   for(auto& row:slot.snapshot->rows)row.decision=rows[row.record];
   context_->Unmap(slot.buffer.Get(),0);
   if(slot.snapshot->world_epoch==world_state_epoch_&&slot.snapshot->frame>native_lod_published_frame_){
    if(!publish_lod_snapshot(slot.snapshot))continue; // retry next frame without losing the request
    native_lod_published_frame_=slot.snapshot->frame;
   }
  }else{slot.snapshot->unavailable=true;if(!publish_lod_snapshot(slot.snapshot))continue;}
  slot.snapshot.reset();
 }
 if(!native_lod_capture_||native_lod_copied_frame_==native_lod_frame_)return;
 for(auto& slot:native_lod_readbacks_)if(!slot.snapshot&&slot.buffer){
  context_->CopyResource(slot.buffer.Get(),native_lod_observations_.Get());
  slot.snapshot=std::move(native_lod_capture_);native_lod_copied_frame_=native_lod_frame_;break;
 }
} catch(...) {} // Observation failure must not change the rendering result.

} // namespace owned

namespace owned {

} // namespace owned
