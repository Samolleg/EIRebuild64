// Renderer static set: adopted static draws, static objects/terrain and their baked layouts.
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

HRESULT Renderer::adopt_native_static_draws(NativeStaticSet& set,std::uint64_t key,std::size_t first,bool& registered,bool fixed_map_order){
 // World sets accept only reorderable color draws. Map layers have a fixed
 // section-local sequence: their private set may retain that exact order.
 fixed_map_order=fixed_map_order&&set.layout_slot>=kMapOrderedLayout&&
  set.layout_slot<native_submission_layouts_.size()&&native_static_set_for_layout(set.layout_slot)==&set;
 bool eligible=native_pending_draws_.size()>first;
 for(std::size_t i=first;i<native_pending_draws_.size()&&eligible;++i){
  const auto& selected=native_pending_draws_[i];
  const auto& command=native_instance_records_[selected.record].command;
  eligible=selected.passes==1u&&(command.reorderable||(fixed_map_order&&command.preserve_order));
 }
 try{
  if(eligible)set.objects[key].assign(native_pending_draws_.begin()+first,native_pending_draws_.end());
 }catch(const std::bad_alloc&){eligible=false;}
 // The set keeps record and range references (they must stay alive) but not
 // the per-frame mesh pin: geometry retirement requires none between frames.
 if(eligible)for(std::size_t i=first;i<native_pending_draws_.size();++i){
  auto* mesh=native_instance_records_[native_pending_draws_[i].record].command.mesh;
  if(mesh&&mesh->pending_references)--mesh->pending_references;
 }
 if(!eligible)for(std::size_t i=first;i<native_pending_draws_.size();++i)
  release_native_queued_draw(native_pending_draws_[i].record);
 // Registered draws keep their enqueue reference in the set.
 native_pending_draws_.resize(first);
 if(eligible){
  registered=true;
  // The object colour set takes a new key without rebaking: it joins the
  // set's added draws, drawn after the bake by an ordinary flush until the
  // next rebuild folds them in (flush_native_static_set).
  if(&set==&native_static_set_&&!set.dirty){
   try{set.added.push_back(key);set.added_draws+=set.objects[key].size();}
   catch(const std::bad_alloc&){set.dirty=true;}
  }else set.dirty=true;
 }
 return S_OK;
}

HRESULT Renderer::set_native_static_object(std::uint64_t key,const NativePreparedSubmission& submission,
 const FixedState& frame,bool& registered,NativeGeometryResult* result){SCENE_ENTRY("set_native_static_object");
 registered=false;
 if(native_shadow_collecting_)return E_FAIL;
 remove_native_static_object(key);
 const auto first=native_pending_draws_.size();
 const HRESULT hr=queue_native_records(submission,frame,nullptr,result);
 if(FAILED(hr))return hr;
 return adopt_native_static_draws(native_static_set_,key,first,registered);
}

HRESULT Renderer::set_native_static_terrain(std::uint64_t key,const NativeGeometryView& view,
 const FixedState& state,SurfaceImage* texture,bool& registered,NativeGeometryResult* result){SCENE_ENTRY("set_native_static_terrain");
 registered=false;
 if(native_shadow_collecting_||view.semantic_kind!=SemanticKind::terrain||!view.world.record_revision)return E_INVALIDARG;
 remove_native_static_object(key,true);
 const auto first=native_pending_draws_.size();
 const HRESULT hr=queue_native_terrain(view,state,texture,result);
 if(FAILED(hr))return hr;
 return adopt_native_static_draws(native_terrain_set_,key,first,registered);
}

void Renderer::remove_native_static_object(std::uint64_t key,bool terrain) noexcept{SCENE_ENTRY("remove_native_static_object");
 auto& set=terrain?native_terrain_set_:native_static_set_;
 const auto found=set.objects.find(key);
 if(found==set.objects.end())return;
 // An added key is not in the bake: removing it leaves the bake current.
 const auto added=std::find(set.added.begin(),set.added.end(),key);
 if(added!=set.added.end()){set.added_draws-=found->second.size();set.added.erase(added);}
 else set.dirty=true;
 for(const auto& selected:found->second)release_native_queued_draw(selected.record,false);
 set.objects.erase(found);
}

HRESULT Renderer::flush_native_static_set(NativeStaticSet& set,const FixedState& frame){
 // Expiry scan, enqueue and flush preparation of the persistent static set
 // are its own CPU work, not the preceding pass's (ground cover/critters).
 transition_profile_pass("static_set_prepare");
 const frame_acct::Scope accounted(frame_acct::ours_static_set);
 // Every draw's owner is among set.owners (built with set.draws), so an
 // unchanged set checks only those; objects adopted since the last rebuild
 // (dirty) and any expired owner take the full sweep.
 bool sweep=set.dirty;
 for(std::size_t i=0;i<set.owners.size()&&!sweep;++i)sweep=set.owners[i].expired();
 for(std::size_t i=0;i<set.added.size()&&!sweep;++i){
  const auto found=set.objects.find(set.added[i]);
  if(found==set.objects.end()){sweep=true;break;}
  for(const auto& selected:found->second)
   if(native_instance_records_[selected.record].owner.expired()){sweep=true;break;}
 }
 if(sweep)for(auto it=set.objects.begin();it!=set.objects.end();){
  bool expired=false;
  for(const auto& selected:it->second)
   if(native_instance_records_[selected.record].owner.expired()){expired=true;break;}
  if(!expired){++it;continue;}
  for(const auto& selected:it->second)release_native_queued_draw(selected.record,false);
  it=set.objects.erase(it);set.dirty=true;
 }
 // Added draws fold into a rebuild when they pass the threshold or when the
 // bake must be rebuilt anyway (in-place content change).
 if(!set.dirty&&!set.added.empty()){
  const auto& current=native_submission_layouts_[set.layout_slot].static_bake;
  const std::size_t threshold=(std::max)(std::size_t{kNativeStaticAddedFold},set.draws.size()/8);
  if(set.added_draws>threshold||!(d3d12_?current.work12!=nullptr:current.work_srv!=nullptr)||current.set_revision!=set.revision||
     current.content_revision!=native_static_content_revision_){
   set.dirty=true;
  }
 }
 if(set.dirty){
  set.added.clear();set.added_draws=0;
  try{
   set.draws.clear();set.owners.clear();
   for(const auto& [key,draws]:set.objects)
    set.draws.insert(set.draws.end(),draws.begin(),draws.end());
   for(const auto& selected:set.draws){
    const auto& owner=native_instance_records_[selected.record].owner;
    if(set.owners.empty()||set.owners.back().owner_before(owner)||owner.owner_before(set.owners.back()))
     set.owners.push_back(owner);
   }
  }catch(const std::bad_alloc&){set.draws.clear();set.owners.clear();return E_OUTOFMEMORY;}
  // One serial for every set: a set reset to its initial state (neighbours
  // released, poses retired) never repeats a revision a layout was baked at.
  set.revision=++native_static_revision_serial_;
  set.dirty=false;
 }
 const auto flush_added=[&]()->HRESULT{
  if(set.added.empty())return S_OK;
  // Ordinary per-frame draws (the set's draws are reorderable), with the
  // full enqueue references, released by the flush as for any queued draw.
  try{native_pending_draws_.reserve(native_pending_draws_.size()+set.added_draws);}
  catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  for(const auto key:set.added){
   const auto found=set.objects.find(key);
   if(found==set.objects.end())continue;
   for(const auto& selected:found->second)enqueue_native_record(selected);
  }
  native_static_frame_=&frame;
  const HRESULT added_hr=flush_native();
  native_static_frame_=nullptr;
  return added_hr;
 };
 if(set.draws.empty())return flush_added();
 // An unchanged set redraws from its bake: no queue, references, ordering,
 // layout check or work upload. The bake is keyed by the set revision and
 // the static content revision, and is built only from a queue that held
 // exactly the set's draws.
 const auto& bake=native_submission_layouts_[set.layout_slot].static_bake;
 const bool reuse=native_pending_draws_.empty()&&(d3d12_?bake.work12!=nullptr:bake.work_srv!=nullptr)&&
  bake.set_revision==set.revision&&bake.content_revision==native_static_content_revision_;
 if(reuse){
  native_frame_stats_.native_logical_draws+=bake.logical_draws;
  native_frame_stats_.native_reorderable_draws+=bake.reorderable_draws;
  native_frame_stats_.native_ordered_draws+=bake.ordered_draws;
  native_frame_stats_.native_opaque_draws+=bake.opaque_draws;
  native_frame_stats_.native_cutout_draws+=bake.cutout_draws;
  native_frame_stats_.native_material_buckets+=bake.material_buckets;
 }else{
  native_static_bake_=native_pending_draws_.empty();
  try{native_pending_draws_.reserve(native_pending_draws_.size()+set.draws.size());}
  catch(const std::bad_alloc&){native_static_bake_=false;return E_OUTOFMEMORY;}
  for(const auto& selected:set.draws)enqueue_native_record(selected);
 }
 native_static_reuse_=reuse;native_static_frame_=&frame;
 HRESULT hr;
 {const frame_acct::Scope submit(frame_acct::ours_static_submit);hr=flush_native(set.layout_slot);}
 native_static_frame_=nullptr;native_static_reuse_=native_static_bake_=false;
 if(FAILED(hr))return hr;
 return flush_added();
}

// Per-batch totals of a static layout just built from exactly the set's
// draws (native_static_bake_), for the frames that reuse it.
HRESULT Renderer::bake_native_static_layout(NativeSubmissionLayout& layout) try{
 auto& bake=layout.static_bake;
 bake.set_revision=bake.content_revision=0;
 bake.batches.clear();bake.meshes.clear();
 bake.batches.reserve(layout.batches.size());
 bake.prepared_vertices=0;bake.logical_draws=native_pending_draws_.size();
 bake.opaque_draws=bake.cutout_draws=bake.material_buckets=0;
 bake.reorderable_draws=bake.ordered_draws=0;
 for(const auto& selected:native_pending_draws_){
  if(native_instance_records_[selected.record].command.reorderable)++bake.reorderable_draws;
  else ++bake.ordered_draws;
 }
 for(const auto& work:layout.work)
  if(work.record!=UINT_MAX&&(work.flags&2u))bake.prepared_vertices+=native_instance_records_[work.record].command.component_count;
 UINT previous_bucket=UINT_MAX;
 for(const auto& batch:layout.batches){
  NativeSubmissionLayout::StaticBake::Batch totals;
  totals.first_record=native_pending_draws_[native_draw_order_[batch.first_draw]].record;
  totals.xy_min[0]=totals.xy_min[1]=std::numeric_limits<float>::max();totals.xy_max[0]=totals.xy_max[1]=-std::numeric_limits<float>::max();
  const auto& bucket=native_state_buckets_[batch.state_bucket];
  if(batch.state_bucket!=previous_bucket){++bake.material_buckets;previous_bucket=batch.state_bucket;}
  for(std::size_t n=batch.first_draw;n<batch.first_draw+batch.draw_count;++n){
   const auto& draw=native_queued_draw(native_draw_order_[n]);
   totals.triangles+=draw.index_count/3u;
   for(int axis=0;axis<2;++axis){totals.xy_min[axis]=std::min(totals.xy_min[axis],draw.bounds_min[axis]);totals.xy_max[axis]=std::max(totals.xy_max[axis],draw.bounds_max[axis]);}
   if(draw.instance.lighting_enabled){++totals.lit_draws;totals.unlit=false;}
   if(bucket.semantic_kind==SemanticKind::terrain){totals.liquid|=draw.mesh->has_liquid;totals.ground|=draw.mesh->has_ground;totals.liquid_types|=draw.mesh->liquid_types;}
   totals.neighbour|=(draw.instance.flags&native_neighbour_flag)!=0;
   if(bucket.opacity==NativeOpacityClass::opaque)++bake.opaque_draws;
   else if(bucket.opacity==NativeOpacityClass::cutout)++bake.cutout_draws;
   if(bake.meshes.empty()||bake.meshes.back()!=draw.mesh)bake.meshes.push_back(draw.mesh);
  }
  bake.batches.push_back(totals);
 }
 return S_OK;
}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}

// A static layout's own draw work buffer: written when the layout is built,
// read by every later frame's build dispatches.
HRESULT Renderer::upload_native_static_work(NativeSubmissionLayout& layout){
 auto& bake=layout.static_bake;
 const UINT count=static_cast<UINT>(layout.work.size());
 if(!count)return S_OK;
 if(d3d12_){ // D3D12 (the shadow bakes): the same list as an executor buffer
  const UINT64 bytes=UINT64(count)*sizeof(NativeDrawWorkGpu);
  const HRESULT hr=native_d3d12_buffer(bake.work12,bytes,sizeof(NativeDrawWorkGpu),0);
  return FAILED(hr)?hr:d3d12_->write_buffer(*bake.work12,0,layout.work.data(),bytes);
 }
 if(!bake.work||count>bake.work_capacity){
  constexpr UINT maximum=(std::min)(kNativeRawElementLimit,UINT_MAX/static_cast<UINT>(sizeof(NativeDrawWorkGpu)));
  if(count>maximum)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
  const UINT capacity=demand_growth_capacity(count,bake.work_capacity,maximum);
  bake.work.Reset();bake.work_srv.Reset();bake.work_capacity=0;
  D3D11_BUFFER_DESC description{};description.ByteWidth=capacity*static_cast<UINT>(sizeof(NativeDrawWorkGpu));
  description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;description.StructureByteStride=sizeof(NativeDrawWorkGpu);
  HRESULT hr=device_->CreateBuffer(&description,nullptr,&bake.work);if(FAILED(hr))return hr;
  D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_UNKNOWN;
  view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=capacity;
  if(FAILED(hr=device_->CreateShaderResourceView(bake.work.Get(),&view,&bake.work_srv))){bake.work.Reset();return hr;}
  bake.work_capacity=capacity;
 }
 const D3D11_BOX box{0,0,0,count*static_cast<UINT>(sizeof(NativeDrawWorkGpu)),1,1};
 context_->UpdateSubresource(bake.work.Get(),0,&box,layout.work.data(),0,0);
 return S_OK;
}

HRESULT Renderer::flush_native_static_terrain(const FixedState& frame,const std::array<float,16>& world_to_clip){SCENE_ENTRY("flush_native_static_terrain");
 if(native_shadow_collecting_)return E_FAIL;
 if(!native_pending_draws_.empty()){const HRESULT hr=flush_native();if(FAILED(hr))return hr;}
 if(!set_native_world_clip(world_to_clip))return E_FAIL;
 const HRESULT hr=flush_native_static_set(native_terrain_set_,frame);
 if(FAILED(hr))return hr;
 return render_neighbour_maps(frame,world_to_clip,false);
}

HRESULT Renderer::flush_native_static(const FixedState& frame,const std::array<float,16>& world_to_clip){SCENE_ENTRY("flush_native_static");
 const frame_acct::Scope accounted(frame_acct::ours_static_flush);
 PERF06_SCOPE("static_submit");
 if(native_shadow_collecting_)return E_FAIL;
 if(!native_pending_draws_.empty()){const HRESULT hr=flush_native();if(FAILED(hr))return hr;}
 ambient_fog_color_=frame.render[D3DRENDERSTATE_FOGCOLOR];
 // Depth now holds opaque terrain (water and objects come later): the
 // occlusion pyramid for this frame's object draws.
 if(d3d12_){
  // Native cover runs at the same opaque-terrain boundary as D3D11.
  if(!set_native_world_clip(world_to_clip))return E_FAIL;
  {const frame_acct::Scope hiz(frame_acct::ours_static_hiz);(void)build_native_hiz_d3d12();}
  {const frame_acct::Scope cover(frame_acct::ours_ground_cover);
   const HRESULT cover_hr=render_ground_cover(frame,world_to_clip);if(FAILED(cover_hr))return cover_hr;}
  // Ambient critters, as on D3D11 below: opaque, depth-correct, lit like world objects.
  if(critters_||critters_failed_||configured_integer(debug_overlay_.config(),"critters",2)>0||configured_integer(debug_overlay_.config(),"critter_showcase",0)>0)
   {const frame_acct::Scope critters(frame_acct::ours_critters);(void)render_critters(frame,world_to_clip);}
  const HRESULT static_hr=flush_native_static_set(native_static_set_,frame);
  if(FAILED(static_hr))return static_hr;
  // Neighbour liquids wait for composite_world_d3d12, as on D3D11 below.
  neighbour_liquids_pending_=true;neighbour_liquids_frame_index_=native_frame_index_;
  neighbour_liquids_state_=frame;neighbour_liquids_clip_=world_to_clip;
  return S_OK;
 }
 if(!set_native_world_clip(world_to_clip))return E_FAIL;
 {const frame_acct::Scope hiz(frame_acct::ours_static_hiz);build_native_hiz();}
 if(!set_native_world_clip(world_to_clip))return E_FAIL;
 // Procedural ground cover joins the opaque world here: after terrain (its
 // depth and the cascades are ready), before objects and liquids.
 // Off (level 0) touches no state and creates nothing once released.
 // Both passes bind what they read and restore nothing (frame_plan.h); the
 // static flush after them binds its own targets and state.
 if(gc_place_cs_||gc_failed_||configured_integer(debug_overlay_.config(),"ground_cover",2)>0)
  {const frame_acct::Scope cover(frame_acct::ours_ground_cover);(void)plan_pass("ground_cover",rtv_.Get(),dsv_.Get());(void)render_ground_cover(frame,world_to_clip);}
 // Ambient critters: opaque, depth-correct, lit like world objects.
 if(critters_||critters_failed_||configured_integer(debug_overlay_.config(),"critters",2)>0||configured_integer(debug_overlay_.config(),"critter_showcase",0)>0)
  {const frame_acct::Scope critters(frame_acct::ours_critters);(void)plan_pass("critters",rtv_.Get(),dsv_.Get());(void)render_critters(frame,world_to_clip);}
 const HRESULT hr=flush_native_static_set(native_static_set_,frame);
 if(FAILED(hr))return hr;
 // Neighbour liquids (ordered) wait for composite_world: they draw after the
 // party figures the game submits later, with the active map's water.
 neighbour_liquids_pending_=true;neighbour_liquids_frame_index_=native_frame_index_;
 neighbour_liquids_state_=frame;neighbour_liquids_clip_=world_to_clip;
 return S_OK;
}

} // namespace owned
