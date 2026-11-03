#pragma once
// Native sinks over a device-state copy: what the bridge's queued world
// commands (the retained scene seam, transparent terrain, wound layers, HUD
// figures) submit through on the thread that runs them. Shared by the scene
// thread of the game process (proxy.cpp: Device::captureSceneSinks makes the
// copy at the post) and the core-mode host (one tree, two builds), so both
// route a submission to the Renderer the same way. Game-free.
#include "engine_bridge.h"
#include "renderer.h"
#include "retained_static_scene.h"
#include "scene_commands_ui.h"

#include <cstdio>
#include <memory>

namespace owned {


// ---- Scene thread (render_thread option): native sinks over a device copy ---
// The bridge's queued world commands (the retained scene seam, transparent
// terrain, wound layers) submit through these on the scene thread. They do
// what the Device sinks do, with the D3D7 fixed state and frame state copied
// when the command was queued (Device::captureSceneSinks), the stage-0 texture
// the bridge binds as a snapshot, and a wound surface resolved to its
// snapshot. The device's own bookkeeping (its pending legacy draws, UI flush,
// native sequence) ran on the game thread before the command was queued.
struct SceneSinks {
 Renderer* renderer=nullptr;
 FixedState state{};
 NativeFrameState frame{};bool frame_valid=false;
 std::shared_ptr<SurfaceImage> bound;
 std::uintptr_t wound_interface=0;std::shared_ptr<SurfaceImage> wound;
 bool sequence=false,batch_active=false,object_active=false;
 Renderer::NativeQueueCheckpoint checkpoint{};
 // Core mode: the game thread's camp placement at the capture (camp
 // composition is sent, not drawn inline). Applied around each submission
 // only while composing; standard mode never captures a composing state.
 camp::SubmissionState camp_state{};
 void flushUi(){if(renderer->has_pending_ui())(void)renderer->flush_ui();}
 SurfaceImage* woundImage(std::uintptr_t surface_interface) const{return surface_interface&&surface_interface==wound_interface?wound.get():nullptr;}
 static SceneSinks& of(void* context){return *static_cast<SceneSinks*>(context);}
 static bool submit(void* context,const NativeGeometryView& view,NativeGeometryResult& result) noexcept{
  const auto& self=of(context);
  if(!self.camp_state.composing)return submitView(context,view,result);
  const camp::SubmissionScope placement(self.camp_state);
  return submitView(context,view,result);
 }
 static bool submitView(void* context,const NativeGeometryView& view,NativeGeometryResult& result) noexcept{
  try{
   auto& self=of(context);auto& renderer=*self.renderer;result={};
   if(!self.sequence){self.flushUi();self.sequence=true;}
   if(view.static_submission){
    const auto& request=*view.static_submission;
    using Operation=NativeStaticSubmission::Operation;
    request.generation=renderer.native_static_generation();
    if(request.operation==Operation::generation)return true;
    if(request.operation==Operation::shadow_static||request.operation==Operation::shadow_dynamic){
     request.generation=renderer.set_native_shadow_phase(request.operation==Operation::shadow_static?1u:2u);return true;}
    if(request.operation==Operation::terrain_frame){
     if(!request.terrain_state){result.failure_hresult=E_INVALIDARG;result.failure_operation="scene.terrain_frame";return false;}
     return SUCCEEDED(renderer.set_native_terrain_frame(*request.terrain_state,request.full_sun,request.layer_shifts));
    }
    if(request.operation==Operation::remove){renderer.remove_native_static_object(request.key,request.terrain);return true;}
    if(request.operation==Operation::flush){
     if(!request.world_to_clip){result.failure_hresult=E_INVALIDARG;result.failure_operation="scene.static_clip";return false;}
     const HRESULT hr=request.terrain?renderer.flush_native_static_terrain(self.state,*request.world_to_clip):
      renderer.flush_native_static(self.state,*request.world_to_clip);
     if(FAILED(hr)){result.failure_hresult=hr;result.failure_operation="scene.static_flush";
      char detail[96];std::snprintf(detail,sizeof(detail),"hr=%08lx draws=%zu",static_cast<unsigned long>(hr),renderer.native_static_draw_count());
      owned::bridge_append_probe_event("static_flush_failed",detail);return false;}
     return true;
    }
    bool registered=false;HRESULT hr=S_OK;
    if(request.terrain){
     if(!request.terrain_view){result.failure_hresult=E_INVALIDARG;result.failure_operation="scene.static_terrain_view";return false;}
     const auto& terrain=*request.terrain_view;
     const FixedState terrain_state=terrain.owned_fixed_state.enabled?native_owned_submission_state(self.state,terrain.owned_fixed_state):self.state;
     hr=renderer.set_native_static_terrain(request.key,terrain,terrain_state,
      terrain.owned_texture.identity?nullptr:self.bound.get(),registered,&result);
     request.registered=registered;return SUCCEEDED(hr);
    }
    if(!request.records){result.failure_hresult=E_INVALIDARG;result.failure_operation="scene.static_records";return false;}
    hr=renderer.set_native_static_object(request.key,*request.records,self.state,registered,&result);
    request.registered=registered;
    if(FAILED(hr)){char detail[160];std::snprintf(detail,sizeof(detail),"hr=%08lx op=%s",static_cast<unsigned long>(hr),
     result.failure_operation?result.failure_operation:"-");owned::bridge_append_probe_event("static_set_failed",detail);}
    return SUCCEEDED(hr);
   }
   if(view.depth_submission)return SUCCEEDED(renderer.queue_native_depth(*view.depth_submission,self.state,&result));
   if(view.prepared_submission){
    const auto& submission=*view.prepared_submission;SurfaceImage* wound=nullptr;
    if(submission.wound_surface_interface){
     wound=self.woundImage(submission.wound_surface_interface);
     if(!wound){result.failure_hresult=E_INVALIDARG;result.failure_operation="scene.prepared_wound";return false;}
    }
    return SUCCEEDED(renderer.queue_native_records(submission,self.state,wound,&result));
   }
   const bool prepared=view.prepared_mesh&&view.prepared_material&&view.prepared_identity.valid;
   const FixedState submission_state=view.owned_fixed_state.enabled?native_owned_submission_state(self.state,view.owned_fixed_state):self.state;
   const auto queue=[&](const NativeGeometryView& packet,const FixedState& state,SurfaceImage* texture,NativeGeometryResult* output){
    if(packet.owned_texture.identity)return renderer.queue_native_owned(packet,state,output);
    return prepared?renderer.queue_native_prepared(packet,state,texture,output):renderer.queue_native(packet,state,texture,output);
   };
   SurfaceImage* wound=view.wound_surface_interface?self.woundImage(view.wound_surface_interface):nullptr;
   if(view.wound_only&&!wound)return false;
   const bool owned_depth=view.owned_fixed_state.enabled&&view.contribution.known&&!view.contribution.color&&view.contribution.light_mask;
   if(!wound)return SUCCEEDED(queue(view,submission_state,view.owned_texture.identity||owned_depth?nullptr:self.bound.get(),&result));
   if(!view.wound_only&&FAILED(queue(view,submission_state,self.bound.get(),&result)))return false;
   FixedState wound_state=submission_state;
   wound_state.render[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_SRCALPHA;wound_state.render[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_INVSRCALPHA;
   wound_state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=TRUE;wound_state.stage[0][D3DTSS_TEXCOORDINDEX]=0;
   NativeGeometryView wound_view=view;wound_view.wound_surface_interface=0;wound_view.wound_only=false;wound_view.owned_texture={};
   if(prepared)wound_view.prepared_material=view.prepared_wound_material;
   if(prepared&&!wound_view.prepared_material)return false;
   if(FAILED(prepared?renderer.queue_native_prepared(wound_view,wound_state,wound,view.wound_only?&result:nullptr):
                      renderer.queue_native(wound_view,wound_state,wound,view.wound_only?&result:nullptr)))return !view.wound_only;
   return true;
  }catch(...){if(!result.failure_operation)result.failure_operation="scene.exception";return false;}
 }
 static bool beginBatch(void* context,SemanticKind) noexcept{
  try{auto& self=of(context);if(self.batch_active)return false;self.flushUi();
   if(FAILED(self.renderer->flush_native()))return false;self.sequence=true;self.batch_active=true;return true;}catch(...){return false;}
 }
 static void endBatch(void* context,bool commit) noexcept{
  try{auto& self=of(context);if(!self.batch_active)return;if(!commit)self.renderer->discard_native();self.batch_active=false;}catch(...){}
 }
 static bool beginObject(void* context) noexcept{
  try{auto& self=of(context);if(self.object_active||!self.renderer->checkpoint_native(self.checkpoint))return false;self.object_active=true;return true;}catch(...){return false;}
 }
 static void endObject(void* context,bool commit) noexcept{
  try{auto& self=of(context);if(!self.object_active)return;if(!commit)self.renderer->rollback_native(self.checkpoint);else self.renderer->release_native_checkpoint(self.checkpoint);self.object_active=false;}catch(...){}
 }
 static bool frameState(void* context,NativeFrameState& output) noexcept{
  const auto& self=of(context);if(!self.frame_valid)return false;output=self.frame;
  // The renderer that draws decides: shadows run only where its own switch
  // (applied at its BeginScene) is on as well (#204).
  output.shadows_enabled=output.shadows_enabled&&self.renderer->native_shadows_enabled();
  return true;
 }
 static bool worldLighting(void* context,const NativeWorldLighting& lighting) noexcept{
  try{return of(context).renderer->set_native_world_lighting(lighting);}catch(...){return false;}
 }
 static bool pointLights(void* context,const RetainedVisualFrame& frame) noexcept{
  try{return of(context).renderer->set_native_point_lights(frame.world_point_lights,frame.time);}catch(...){return false;}
 }
 static bool pointShadowBatch(void* context,std::uint64_t& generation,bool& pressure) noexcept{
  try{auto& self=of(context);if(self.batch_active)return false;self.flushUi();self.sequence=true;
   return self.renderer->begin_native_point_shadow_batch(generation,&pressure);}catch(...){return false;}
 }
 static bool pointShadowAllocate(void* context,bool overlay,int& slot) noexcept{return of(context).renderer->allocate_native_point_shadow(overlay,slot);}
 static void pointShadowRelease(void* context,std::uint64_t generation,int slot) noexcept{of(context).renderer->release_native_point_shadow(generation,slot);}
 static void pointShadowPublish(void* context,const std::vector<NativePointShadowPublication>& lights,const NativePointShadowStats& stats) noexcept{
  try{of(context).renderer->publish_native_point_shadows(lights,stats);}catch(...){}
 }
 static bool pointShadowFaces(void* context,std::uint32_t light,std::uint32_t first_face,const NativePointShadowFrame& faces) noexcept{
  try{auto& self=of(context);if(self.batch_active)return false;self.sequence=true;
   return self.renderer->begin_native_point_shadow_faces(light,first_face,faces);}catch(...){return false;}
 }
 static bool pointShadowFacesEnd(void* context,bool commit) noexcept{try{return of(context).renderer->end_native_point_shadow_faces(commit);}catch(...){return false;}}
 static void cullProjection(void* context,NativeShadowCullProjection& projection) noexcept{of(context).renderer->native_shadow_cull_projection(projection);}
 static bool shadowsBegin(void* context,const NativeShadowFrame& frame,std::uint8_t& dirty) noexcept{
  try{auto& self=of(context);if(self.batch_active)return false;self.flushUi();self.sequence=true;
   return self.renderer->begin_native_shadows(frame,dirty);}catch(...){return false;}
 }
 static bool shadowsEnd(void* context,bool commit) noexcept{try{return of(context).renderer->end_native_shadows(commit);}catch(...){return false;}}
 static bool staticDepth(void* context,const RetainedShadowCasterValue* casters,std::uint32_t count,std::uint64_t store_revision,
  std::vector<RetainedShadowCasterValue>& missing) noexcept{
  try{return of(context).renderer->queue_native_static_shadow_depth(casters,count,store_revision,missing);}catch(...){return false;}
 }
 static bool terrainBake(void* context,const NativeTerrainShadowEntry* entries,std::uint32_t count,std::vector<std::uint32_t>& absent) noexcept{
  try{return of(context).renderer->queue_native_terrain_shadow_bake(entries,count,absent);}catch(...){return false;}
 }
 static void shadowDiagnostics(void* context,std::uint64_t world_serial,const NativeShadowSourceDiagnostics& values) noexcept{
  try{of(context).renderer->record_native_shadow_source_diagnostics(world_serial,values);}catch(...){}
 }
 static bool rainBegin(void* context,const NativeShadowSlice& slice,std::uint32_t side) noexcept{
  try{auto& self=of(context);if(self.batch_active)return false;self.sequence=true;
   return self.renderer->begin_native_rain_occlusion(slice,side);}catch(...){return false;}
 }
 static bool rainEnd(void* context,bool commit) noexcept{try{return of(context).renderer->end_native_rain_occlusion(commit);}catch(...){return false;}}
 static void install(void* context) noexcept{
  // The shelter (rain occlusion) sinks are thread-local like the others: the
  // scene thread's world preparation bakes through these.
  if(!context){bridge_set_native_geometry_sink(nullptr,nullptr);bridge_set_rain_occlusion_sink(nullptr,nullptr);return;}
  bridge_set_rain_occlusion_sink(&rainBegin,&rainEnd);
  bridge_set_native_geometry_sink(context,&submit,&beginBatch,&endBatch,&beginObject,&endObject,&frameState,&worldLighting,
   &shadowsBegin,&shadowsEnd,&shadowDiagnostics,&pointLights,&pointShadowFaces,&pointShadowFacesEnd,&staticDepth,
   &pointShadowBatch,&pointShadowPublish,&pointShadowAllocate,&pointShadowRelease,&cullProjection,&terrainBake);
 }
 static void bind(void* context,const std::shared_ptr<SurfaceImage>& texture) noexcept{of(context).bound=texture;}
 static void adopt(void* context,const void* from) noexcept{
  auto& self=of(context);const auto& source=*static_cast<const SceneSinks*>(from);
  self.state=source.state;self.frame=source.frame;self.frame_valid=source.frame_valid;self.bound=source.bound;
  self.camp_state=source.camp_state;
 }
 static void setWound(void* context,std::uintptr_t surface_interface,const std::shared_ptr<SurfaceImage>& texture) noexcept{
  auto& self=of(context);self.wound_interface=surface_interface;self.wound=texture;
 }
};

// A packet's arrays copied off a borrowed view (the game process: a proxy
// scene packet or a HUD figure view). The view's array pointers are left null:
// Packet::bind points them at the copies when the command runs.
inline void copy_scene_packet(const NativeGeometryView& source,scene_ui::Packet& packet){
  auto& view=packet.view;view=source;
  if(!view.cache_identity&&!view.cache_pointers_valid){
   view.cache_pointers={reinterpret_cast<std::uintptr_t>(source.positions),reinterpret_cast<std::uintptr_t>(source.normals),
    reinterpret_cast<std::uintptr_t>(source.components),reinterpret_cast<std::uintptr_t>(source.texture_coordinates),
    reinterpret_cast<std::uintptr_t>(source.indices)};
   view.cache_pointers_valid=true;
  }
  const std::size_t colors=static_cast<std::size_t>(source.normal_groups)*4u;
  packet.positions.assign(source.positions,source.positions+source.position_groups);
  packet.normals.assign(source.normals,source.normals+source.normal_groups);
  if(source.diffuse)packet.diffuse.assign(source.diffuse,source.diffuse+colors);
  if(source.specular)packet.specular.assign(source.specular,source.specular+colors);
  packet.coordinates.assign(source.texture_coordinates,source.texture_coordinates+source.texture_coordinate_count);
  packet.components.assign(source.components,source.components+source.component_count);
  packet.indices.assign(source.indices,source.indices+source.index_count);
  if(source.morphs&&source.morph_count)packet.morphs.assign(source.morphs,source.morphs+source.morph_count);
  else view.morph_count=0;
  if(source.owned_texture.identity&&source.owned_texture.mips){
   packet.mips.assign(source.owned_texture.mips,source.owned_texture.mips+source.owned_texture.mip_count);
   packet.mip_bytes.resize(packet.mips.size());
   for(std::size_t index=0;index<packet.mips.size();++index){
    auto& mip=packet.mips[index];
    if(mip.data&&mip.bytes)packet.mip_bytes[index].assign(mip.data,mip.data+mip.bytes);
    mip.data=nullptr;
   }
  }
  view.positions=nullptr;view.normals=nullptr;view.diffuse=nullptr;view.specular=nullptr;
  view.texture_coordinates=nullptr;view.components=nullptr;view.indices=nullptr;view.morphs=nullptr;
  view.owned_texture.mips=nullptr;
 }

// One generic scene packet through sinks over its device-state copy (the
// game process's scene thread and the core-mode host). The packet's arrays
// are bound here; bound/wound are the resolved surface snapshots.
inline void submit_scene_packet(Renderer& renderer, scene_ui::Packet& packet, std::shared_ptr<SurfaceImage> bound,
                                std::shared_ptr<SurfaceImage> wound) {
    packet.bind();
    SceneSinks sinks;
    sinks.renderer = &renderer; sinks.state = packet.state; sinks.sequence = true;
    sinks.frame = packet.frame; sinks.frame_valid = packet.frame_valid;
    sinks.camp_state = packet.camp_state;
    sinks.bound = std::move(bound);
    if (packet.wound.present) { sinks.wound_interface = packet.view.wound_surface_interface; sinks.wound = std::move(wound); }
    NativeGeometryResult ignored{};
    if (!SceneSinks::submit(&sinks, packet.view, ignored))
        Renderer::report_scene_failure(ignored.failure_operation ? ignored.failure_operation : "scene.packet",
                                       ignored.failure_hresult ? ignored.failure_hresult : E_FAIL);
}

}  // namespace owned
