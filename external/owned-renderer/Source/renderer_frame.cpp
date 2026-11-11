// Renderer frame: resize, clear, engine frame and scene begin/end, upscalers and world composition, scene-thread sync, gamma and present_frame.
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
#include "gamma_shader_programs.h"
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
#include <climits>
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
#ifdef EID3D11_EMBEDDED
bool bridge_live_wants_capture() noexcept;
void bridge_live_capture(ID3D11DeviceContext*,IDXGISwapChain*,unsigned long long) noexcept;
void bridge_live_capture_d3d12(D3D12Frame&,unsigned long long) noexcept;
#endif
namespace {
bool shadow_temporal_diagnostic_enabled(){static const bool enabled=[](){char value[8]{};return GetEnvironmentVariableA("EI_RENDERER_SHADOW_TEMPORAL_CAPTURE",value,sizeof(value))&&value[0]=='1';}();return enabled;}
}  // namespace

// One sub-pixel jitter per world frame (Halton 2,3), the same for every world
// pass through target_viewport(); the sequence length follows the scale.
void Renderer::advance_dlss_jitter() noexcept {
 if(!temporal_upscaler()){dlss_jitter_[0]=dlss_jitter_[1]=0.f;return;}
 const std::uint32_t phases=dlss_jitter_phases(output_width_,width_);
 dlss_jitter_index_=(dlss_jitter_index_+1)%phases;
 dlss_jitter_[0]=dlss_halton(dlss_jitter_index_+1,2)-.5f;
 dlss_jitter_[1]=dlss_halton(dlss_jitter_index_+1,3)-.5f;
}

// The shared start of a temporal upscaler frame in place of upscale():
// the reset decision and the camera motion from the world depth.
Renderer::UpscalerFrame Renderer::begin_upscaler_frame(){
 UpscalerFrame frame;
 const auto scene=bridge_scene_world_to_clip_snapshot();
 frame.camera=scene.valid&&ambient_occlusion_invert(scene.world_to_clip.data(),frame.clip_to_world);
 frame.world_to_clip=scene.world_to_clip;
 // Frames without world output (menus, video, loading) and gaps of more than
 // a quarter second (a paused or minimised game, a stall) also start over.
 const std::uint64_t now_ms=GetTickCount64();
 const bool gap=!dlss_last_evaluate_ms_||now_ms-dlss_last_evaluate_ms_>250u;dlss_last_evaluate_ms_=now_ms;
 bool reset=dlss_reset_||!frame.camera||!dlss_previous_valid_||gap||!scene_updated_;
 // A camera cut (map change, teleport, jump of the view): the screen centre
 // at mid depth lands more than a quarter screen away in the previous view.
 if(frame.camera&&!reset){
  float centre[4]={0,0,.5f,1},world[4]{},previous[4]{};
  for(int r=0;r<4;++r)for(int c=0;c<4;++c)world[r]+=frame.clip_to_world[r*4+c]*centre[c];
  if(std::fabs(world[3])>1e-12f){for(float& v:world)v/=world[3];
   for(int r=0;r<4;++r)for(int c=0;c<4;++c)previous[r]+=dlss_previous_clip_[r*4+c]*world[c];
   if(previous[3]<=1e-6f||std::fabs(previous[0]/previous[3])>.5f||std::fabs(previous[1]/previous[3])>.5f)reset=true;}
  else reset=true;
 }
 frame.reset=reset;
 const float* previous=reset||!frame.camera?scene.world_to_clip.data():dlss_previous_clip_.data();
 // Frame generation takes this frame's depth, motion and camera (D3D12).
 fg_upscaler_valid_=frame.camera;fg_upscaler_reset_=reset;
 std::memcpy(fg_clip_to_world_,frame.clip_to_world,sizeof(fg_clip_to_world_));fg_world_to_clip_=frame.world_to_clip;
 std::memcpy(fg_previous_clip_.data(),previous,sizeof(float)*16);fg_jitter_[0]=dlss_jitter_[0];fg_jitter_[1]=dlss_jitter_[1];
 if(frame.camera){
  transition_profile_pass("scene_dlss_motion");
  if(d3d12_)camera_motion_d3d12(frame.clip_to_world,previous);
  else{
   (void)plan_pass("scene_motion",nullptr,nullptr,plan_read(PlanTarget::world_depth)); // the upscaler steps bind their own targets
   scene_composite_.camera_motion(context_.Get(),depth_srv_.Get(),frame.clip_to_world,previous,dlss_jitter_[0],dlss_jitter_[1]);
  }
  profiler_.add_draw(1,1);profiler_.add_batch();
 }else if(d3d12_){const float zero[4]{};d3d12_->clear_texture(*upscale12_.motion,zero);}
 else scene_composite_.clear_motion(context_.Get());
 return frame;
}

// After a successful evaluation: this frame becomes the history, and the
// upscaled image (plus the world depth) goes to the output.
void Renderer::finish_upscaler_frame(const UpscalerFrame& frame){
 dlss_reset_=false;dlss_previous_valid_=frame.camera&&scene_updated_;if(frame.camera)dlss_previous_clip_=frame.world_to_clip;
 transition_profile_pass("scene_dlss_present");
 if(d3d12_){present_upscaled_d3d12();return;} // colour only: the D3D12 interface depth is never the world's
 (void)plan_pass("scene_upscaler_present",backbuffer_rtv_.Get(),interface_dsv_.Get()!=world_dsv_.Get()?interface_dsv_.Get():nullptr,plan_read(PlanTarget::world_depth));
 const bool separate_depth=interface_dsv_.Get()!=world_dsv_.Get();
 scene_composite_.dlss_present(context_.Get(),backbuffer_rtv_.Get(),separate_depth?depth_srv_.Get():nullptr,separate_depth?interface_dsv_.Get():nullptr);
 profiler_.add_draw(1,1);profiler_.add_batch();
}

// DLSS into the shared output-size image. Embedded failures refuse the
// world frame; the selected mode stays DLSS and the menu shows the reason.
bool Renderer::render_dlss(){
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
 const auto fail=[this](const char* reason){
  set_dlss_unavailable_reason(reason);bridge_report_event("dlss_frame_refused",reason);
  dlss_reset_=true;dlss_previous_valid_=false;return false;
 };
 if(dlss_unavailable_reason()){dlss_reset_=true;dlss_previous_valid_=false;return false;}
 // Menus/loading/video have no world camera. Never evaluate their UI or
 // accept it into history, even if the next world starts immediately.
 if(!scene_updated_||!bridge_scene_world_to_clip_snapshot().valid){
  dlss_reset_=true;dlss_previous_valid_=false;dlss_last_evaluate_ms_=0;
  const float black[4]{};context_->ClearRenderTargetView(backbuffer_rtv_.Get(),black);
  if(interface_dsv_)context_->ClearDepthStencilView(interface_dsv_.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1.f,0);
  return true;
 }
 const UpscalerFrame frame=begin_upscaler_frame();
 DlssFrame evaluate{};
 evaluate.color=scene_composite_.resolved();evaluate.depth=depth_.Get();evaluate.motion=scene_composite_.motion();
 evaluate.output=scene_composite_.dlss_output();
 // Guide 3.6.1-3.7.3: screen pixels, right/down positive, as applied by
 // jitter_clip/target_viewport. Motion has jitter removed; MVLowRes, scale 1.
 evaluate.jitter_x=dlss_jitter_[0];evaluate.jitter_y=dlss_jitter_[1];
 evaluate.shape.render_width=width_;evaluate.shape.render_height=height_;
 evaluate.shape.output_width=output_width_;evaluate.shape.output_height=output_height_;
 evaluate.shape.quality=static_cast<DlssQuality>(dlss_mode_);
 evaluate.reset=frame.reset; // LDR, non-inverted D24, auto exposure (no texture)
 transition_profile_pass("scene_dlss");
 ID3D11ShaderResourceView* empty[4]{};context_->PSSetShaderResources(0,4,empty);context_->OMSetRenderTargets(0,nullptr,nullptr);
 // Native device/context need no remote host's first-Present workaround.
 // Feature creation is lazy here; shape/quality changes recreate it.
 if(!dlss_backend_.evaluate(context_.Get(),evaluate))return fail(dlss_backend_.last_error());
#else
 // First-Present deferral: with DLSS saved on, a remote start ran the host's
 // DLSS feature creation and evaluation in the session's first frame and the
 // NVIDIA driver crashed inside that first host Present (integ2 7035b2f0,
 // 2026-10-24); DLSS switched on after any host present (menu or world) runs
 // clean (A/B runs). Until the host has presented once, these frames take the
 // owned upscale (the caller's path for a frame without DLSS) and the first
 // evaluated frame starts with a reset.
 if(!x64remote::host_presented()){dlss_reset_=true;dlss_previous_valid_=false;return false;}
 const UpscalerFrame frame=begin_upscaler_frame();
 static const float jitter_sign=[]{char value[8]{};return GetEnvironmentVariableA("EI_RENDERER_DLSS_JITTER_SIGN",value,sizeof(value))&&value[0]=='-'?-1.f:1.f;}();
 x64remote::DlssEvaluate evaluate{};
 evaluate.color=scene_composite_.resolved();evaluate.depth=depth_.Get();evaluate.motion=scene_composite_.motion();
 evaluate.output=scene_composite_.dlss_output();evaluate.exposure=nullptr;
 evaluate.jitter_x=jitter_sign*dlss_jitter_[0];evaluate.jitter_y=jitter_sign*dlss_jitter_[1];
 evaluate.mv_scale_x=evaluate.mv_scale_y=1.f;
 evaluate.render_w=width_;evaluate.render_h=height_;evaluate.out_w=output_width_;evaluate.out_h=output_height_;
 evaluate.mode=dlss_mode_;evaluate.sharpness=0.f;evaluate.reset=frame.reset;evaluate.hdr=false;evaluate.depth_inverted=false;
 transition_profile_pass("scene_dlss");
 (void)plan_pass("scene_dlss",nullptr,nullptr,plan_read(PlanTarget::world_color)|plan_read(PlanTarget::world_depth)); // evaluated by the host into the output-size image
 ID3D11ShaderResourceView* empty[4]{};context_->PSSetShaderResources(0,4,empty);context_->OMSetRenderTargets(0,nullptr,nullptr);
 const HRESULT hr=x64remote::dlss_evaluate(context_.Get(),evaluate);
 if(FAILED(hr)){
  char reason[96];std::snprintf(reason,sizeof(reason),"DLSS evaluation failed (0x%08lX)",static_cast<unsigned long>(hr));
  set_dlss_unavailable_reason(reason);bridge_append_probe_event("dlss_failed",reason);
  OutputDebugStringA("EID3D11 DLSS evaluation failed; DLSS turned off\n");
  dlss_previous_valid_=false;return false;
 }
#endif
 // The player's Sharpening after DLSS (fsr2_upscaler.h UpscaleSharpen):
 // NGX gets no sharpness; RCAS sharpens its output-size image in place.
 if(const float sharpness=std::clamp(configured_floating_point(debug_overlay_.config(),"upscale_sharpness",.2f),0.f,1.f);sharpness>0.f){
  transition_profile_pass("scene_dlss_rcas");
  if(FAILED(upscale_sharpen_.apply(device_.Get(),context_.Get(),scene_composite_.dlss_output(),scene_composite_.dlss_output_view(),sharpness))){
   static bool reported=false;
   if(!reported){reported=true;bridge_append_probe_event("dlss_sharpen_omitted","RCAS sharpening after DLSS could not be created; the DLSS image is shown unsharpened");}
  }
  profiler_.add_draw(1,1);profiler_.add_batch();
 }else upscale_sharpen_.reset();
 finish_upscaler_frame(frame);
 return true;
}

// AMD FSR 2 on this device (fsr2_upscaler.h); its images follow the render
// and output size. False: FSR failed; it is turned off for the session with
// the reason on the Upscaling list (that frame takes the owned upscale).
bool Renderer::render_fsr(){
 const auto fail=[this](const char* reason){
  set_fsr_unavailable_reason(reason);bridge_append_probe_event("fsr_failed",reason);
  OutputDebugStringA("EID3D11 FSR 2 failed; FSR turned off\n");
  fsr_.reset();dlss_previous_valid_=false;return false;
 };
 if(!fsr_.created(width_,height_,output_width_,output_height_)){
  if(const HRESULT hr=fsr_.create(device_.Get(),width_,height_,output_width_,output_height_);FAILED(hr)){
   char reason[96];std::snprintf(reason,sizeof(reason),"FSR could not create its resources (0x%08lX)",static_cast<unsigned long>(hr));
   return fail(reason);
  }
  dlss_reset_=true;
 }
 if(!scene_composite_.resolved_view()||!scene_composite_.motion_view()||!scene_composite_.dlss_output_view()||!depth_srv_)
  return fail("FSR inputs are missing");
 const UpscalerFrame frame=begin_upscaler_frame();
 Fsr2Upscaler::Frame input{};
 if(frame.camera&&!upscaler_camera_from_clip(frame.world_to_clip.data(),input.near_plane,input.far_plane,input.tan_half_fov_y))
  return fail("the camera is not a perspective projection FSR understands");
 const auto now=std::chrono::steady_clock::now();
 input.delta_seconds=fsr_last_frame_.time_since_epoch().count()?std::chrono::duration<float>(now-fsr_last_frame_).count():0.f;
 fsr_last_frame_=now;
 input.color=scene_composite_.resolved_view();input.depth=depth_srv_.Get();input.motion=scene_composite_.motion_view();
 input.output=scene_composite_.dlss_output_view();
 input.jitter_x=dlss_jitter_[0];input.jitter_y=dlss_jitter_[1];
 input.jitter_phases=float(dlss_jitter_phases(output_width_,width_));
 input.sharpness=std::clamp(configured_floating_point(debug_overlay_.config(),"upscale_sharpness",.2f),0.f,1.f);
 input.reset=frame.reset;
 ID3D11ShaderResourceView* empty[4]{};context_->PSSetShaderResources(0,4,empty);context_->OMSetRenderTargets(0,nullptr,nullptr);
 (void)plan_pass("scene_fsr",nullptr,nullptr,plan_read(PlanTarget::world_color)|plan_read(PlanTarget::world_depth)); // FSR's compute passes, into the output-size image
 fsr_.dispatch(context_.Get(),input,[this](const char* name){transition_profile_pass(name);profiler_.add_draw(1,1);profiler_.add_batch();});
 finish_upscaler_frame(frame);
 return true;
}

// Start of a world frame with DLSS: no object motion yet, and the world
// vertices as the previous frame left them (the used range, same slots), so
// a posed record's vertex can read where it was.
void Renderer::begin_dlss_object_motion() noexcept {
 if(!context_)return;
 if(!scene_composite_.dlss()||!native_world_vertices_||!native_world_vertex_used_){dlss_previous_world_.Reset();dlss_previous_world_srv_.Reset();}
 if(!scene_composite_.dlss())return;
 scene_composite_.clear_object_motion(context_.Get());
 dlss_motion_constants_frame_=UINT64_MAX;
 if(!native_world_vertices_||!native_world_vertex_used_)return;
 D3D11_BUFFER_DESC desc{};native_world_vertices_->GetDesc(&desc);
 if(dlss_previous_world_){
  D3D11_BUFFER_DESC have{};dlss_previous_world_->GetDesc(&have);
  if(have.ByteWidth!=desc.ByteWidth){dlss_previous_world_.Reset();dlss_previous_world_srv_.Reset();}
 }
 if(!dlss_previous_world_){
  desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=0;
  D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_UNKNOWN;view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;
  view.Buffer.NumElements=desc.StructureByteStride?desc.ByteWidth/desc.StructureByteStride:0;
  if(FAILED(device_->CreateBuffer(&desc,nullptr,&dlss_previous_world_))||
     FAILED(device_->CreateShaderResourceView(dlss_previous_world_.Get(),&view,&dlss_previous_world_srv_))){
   dlss_previous_world_.Reset();dlss_previous_world_srv_.Reset();
   static bool reported=false;
   if(!reported){reported=true;bridge_append_probe_event("dlss_object_motion_omitted","previous world vertex copy could not be created; figures use camera motion");}
   return;
  }
 }
 const D3D11_BOX box{0,0,0,native_world_vertex_used_*UINT(sizeof(NativeWorldVertexGpu)),1,1};
 context_->CopySubresourceRegion(dlss_previous_world_.Get(),0,0,0,0,native_world_vertices_.Get(),0,&box);
}

// After a posed batch's material draw (flush_native): the same indirect
// draw through nativeMotionVS/PS into the object motion targets, depth
// tested LESS_EQUAL against what the batch just wrote. Leaves the material
// vertex stage bound; the caller rebinds its targets.
bool Renderer::draw_dlss_object_motion(UINT args_offset){
 if(!scene_composite_.dlss()||scene_composited_||rtv_.Get()!=scene_composite_.target()||!native_motion_vs_||!native_motion_ps_||
    !dlss_previous_world_srv_||!scene_composite_.object_motion_target()||!dlss_motion_constants_current())return false;
 if(!dlss_motion_depth_){
  D3D11_DEPTH_STENCIL_DESC desc{};desc.DepthEnable=TRUE;desc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
  desc.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
  if(FAILED(device_->CreateDepthStencilState(&desc,&dlss_motion_depth_)))return false;
 }
 ID3D11RenderTargetView* targets[2]={scene_composite_.object_motion_target(),scene_composite_.object_depth_target()};
 context_->OMSetRenderTargets(2,targets,dsv_.Get());
 context_->OMSetDepthStencilState(dlss_motion_depth_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,0xffffffffu);
 context_->VSSetShader(native_motion_vs_.Get(),nullptr,0);
 // nativeMotionPS reads the jitter and render size from the same constants
 // (b11 is also the ground cover and critter frame slot: unbound after).
 context_->VSSetConstantBuffers(11,1,dlss_motion_constants_.GetAddressOf());
 context_->PSSetConstantBuffers(11,1,dlss_motion_constants_.GetAddressOf());
 context_->VSSetShaderResources(46,1,dlss_previous_world_srv_.GetAddressOf());
 context_->PSSetShader(native_motion_ps_.Get(),nullptr,0);
 context_->DrawInstancedIndirect(native_indirect_args_.Get(),args_offset);
 ID3D11Buffer* no_constants=nullptr;
 context_->VSSetConstantBuffers(11,1,&no_constants);context_->PSSetConstantBuffers(11,1,&no_constants);
 context_->VSSetShader(native_vs_.Get(),nullptr,0);
 return true;
}

// The object motion constants (b11 NativeMotionConstants: last frame's
// unjittered world clip, this frame's jitter and render size), written once
// a world frame.
bool Renderer::dlss_motion_constants_current(){
 if(dlss_motion_constants_frame_!=native_frame_index_){
  if(!dlss_motion_constants_){
   D3D11_BUFFER_DESC desc{};desc.ByteWidth=80;desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
   desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
   if(FAILED(device_->CreateBuffer(&desc,nullptr,&dlss_motion_constants_)))return false;
  }
  const auto scene=bridge_scene_world_to_clip_snapshot();
  const std::array<float,16>& previous=dlss_previous_valid_?dlss_previous_clip_:scene.valid?scene.world_to_clip:native_world_clip_;
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(FAILED(context_->Map(dlss_motion_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
  float* values=static_cast<float*>(mapped.pData);std::memcpy(values,previous.data(),64);
  values[16]=dlss_jitter_[0];values[17]=dlss_jitter_[1];values[18]=float(width_);values[19]=float(height_);
  context_->Unmap(dlss_motion_constants_.Get(),0);
  dlss_motion_constants_frame_=native_frame_index_;
 }
 return true;
}

// ---- Frame plan (frame_plan.h) ------------------------------------------------
// The ids name today's views; a view no id names (another file group's
// target) is carried by the pass as unowned.
const PlanPass& Renderer::plan_pass(const char* name,ID3D11RenderTargetView* color,ID3D11DepthStencilView* depth,
                                    std::uint32_t reads) noexcept{
 PlanPass pass;pass.name=name;pass.reads=reads;
 if(!color)pass.color=PlanTarget::none;
 else if(color==scene_composite_.target())pass.color=PlanTarget::world_color;
 else if(color==backbuffer_rtv_.Get())pass.color=PlanTarget::backbuffer;
 else if(color==camp_popup_rtv_.Get())pass.color=PlanTarget::camp_popup_color;
 else{pass.color=PlanTarget::unowned;pass.unowned_color=color;}
 if(!depth)pass.depth=PlanTarget::none;
 else if(depth==world_dsv_.Get())pass.depth=PlanTarget::world_depth;
 else if(depth==interface_dsv_.Get())pass.depth=PlanTarget::interface_depth;
 else if(depth==camp_popup_dsv_.Get())pass.depth=PlanTarget::camp_popup_depth;
 else{pass.depth=PlanTarget::unowned;pass.unowned_depth=depth;}
 return frame_plan_.add(pass);
}
ID3D11RenderTargetView* Renderer::plan_rtv(const PlanPass& pass) const noexcept{
 switch(pass.color){
 case PlanTarget::world_color:return scene_composite_.target();
 case PlanTarget::backbuffer:return backbuffer_rtv_.Get();
 case PlanTarget::camp_popup_color:return camp_popup_rtv_.Get();
 case PlanTarget::unowned:return pass.unowned_color;
 default:return nullptr;
 }
}
ID3D11DepthStencilView* Renderer::plan_dsv(const PlanPass& pass) const noexcept{
 switch(pass.depth){
 case PlanTarget::world_depth:return world_dsv_.Get();
 case PlanTarget::interface_depth:return interface_dsv_.Get();
 case PlanTarget::camp_popup_depth:return camp_popup_dsv_.Get();
 case PlanTarget::unowned:return pass.unowned_depth;
 default:return nullptr;
 }
}
void Renderer::bind_plan_targets(const PlanPass& pass) noexcept{
 ID3D11RenderTargetView* color=plan_rtv(pass);context_->OMSetRenderTargets(1,&color,plan_dsv(pass));
}

HRESULT Renderer::composite_world(){
 PERF06_SCOPE("post_composite");

 if(scene_queue()){scene_native_pending_=false;scene_post_command(scene_cmd::CompositeWorld{});return S_OK;}
 if(hit_test_active_)return S_OK; // the zone hit-test frame is never composited (#200)
 if(scene_composited_)return S_OK;
#if !defined(EID3D11_SHADER_EMBED)
 if(d3d12_)return composite_world_d3d12();
#endif
 const auto checked=[&](const char* operation,auto&& work)->HRESULT{
#ifdef EID3D11_EMBEDDED
  native_flush_failure_line_=0;
  HRESULT hr;const char* cause="hresult";
  try{hr=work();}
  catch(const std::bad_alloc&){hr=E_OUTOFMEMORY;cause="bad_alloc";}
  catch(...){hr=E_FAIL;cause="exception";}
  if(FAILED(hr)){
   char detail[256];std::snprintf(detail,sizeof(detail),"operation=%s hr=%08lx cause=%s native_line=%d",
    operation,static_cast<unsigned long>(hr),cause,native_flush_failure_line_);
   bridge_report_event("composite_step_failed",detail);
  }
  return hr;
#else
  (void)operation;return work();
#endif
 };
 if(!context_||!backbuffer_rtv_||!scene_composite_.target())
  return checked("composite_world.targets",[]{return E_FAIL;});
 const HRESULT world=checked("composite_world.flush_native",[&]{return flush_native();});if(FAILED(world))return world;
 // Neighbour-map liquids draw last in the world pass, after every figure the
 // game submitted, so the frame's single water scene copy (taken at its first
 // liquid batch) holds the figures: wading legs refract through the water.
 if(neighbour_liquids_pending_){
  neighbour_liquids_pending_=false;
  if(neighbour_liquids_frame_index_==native_frame_index_&&set_native_world_clip(neighbour_liquids_clip_)){
   const HRESULT liquids=checked("composite_world.neighbour_liquids",[&]{
    return render_neighbour_maps(neighbour_liquids_state_,neighbour_liquids_clip_,true);});
   if(FAILED(liquids))return liquids;
  }
 }
 // Ground marks held back from the world pass draw over the finished water.
 // The water writes the real depth, so on frames it drew they test against
 // its pre-water depth copy: marks on the bed stay visible, unrefracted.
 if(!deferred_ground_marks_.empty()){
  auto marks=std::move(deferred_ground_marks_);deferred_ground_marks_.clear();
  const bool under_water=water_scene_dsv_&&water_capture_ready_&&water_capture_frame_==native_frame_index_;
  ComPtr<ID3D11DepthStencilView> world_depth=dsv_;if(under_water)dsv_=water_scene_dsv_;
  drawing_ground_marks_=true;HRESULT marked=S_OK;
  for(auto& mark:marks)if(marked=checked("composite_world.ground_mark",[&]{return mark();});FAILED(marked))break;
  drawing_ground_marks_=false;dsv_=world_depth;
  context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());
  if(FAILED(marked))return marked;
 }
 // The occlusion pyramid describes the world pass only; interface 3D (HUD
 // icons, previews) draws after this point, often without depth testing.
 return checked("composite_world.postprocess",[&]()->HRESULT{
 native_hiz_frame_=UINT64_MAX;native_world_clip_dirty_=true;
 bool dlss_frame_refused=false;
 // The world composite reads the world colour and depth and writes the
 // output (and, upscaling with a separate interface depth, that depth). Its
 // steps bind their own state; nothing found before it is restored after it
 // (every following pass sets what it reads).
 (void)plan_pass("composite_world",backbuffer_rtv_.Get(),interface_dsv_.Get()!=world_dsv_.Get()?interface_dsv_.Get():nullptr,
                 plan_read(PlanTarget::world_color)|plan_read(PlanTarget::world_depth));
 {
  // AO needs the finished world depth and ambient share, and must precede
  // SMAA and every interface pass.
  if(ao_ambient_written_&&FAILED(render_ambient_occlusion())){
   OutputDebugStringA("EID3D11 ambient occlusion pass failed; AO omitted this frame\n");
   if(ao_ambient_rtv_){const float zero[4]{};context_->ClearRenderTargetView(ao_ambient_rtv_.Get(),zero);}
  }
  if(scene_updated_)render_ambient_particles();
  if(scene_updated_)render_wind_trails();
  if(scene_updated_)render_precipitation();
  capture_water_history();
  // DLSS and FSR anti-alias themselves: their colour input is the plain copy.
  const bool smaa=!scene_composite_.dlss()&&configured_boolean(debug_overlay_.config(),"smaa",true);
  transition_profile_pass(smaa?"scene_smaa":"scene_copy");
  // Below 100% render scale this is the world-size resolved image.
  ID3D11RenderTargetView* const resolved=scene_composite_.resolve_target(backbuffer_rtv_.Get());
  (void)plan_pass(smaa?"scene_smaa":"scene_copy",resolved,nullptr,plan_read(PlanTarget::world_color)); // the composite steps bind their own targets
  scene_composite_.render(context_.Get(),resolved,smaa);
  profiler_.add_draw(smaa?3:1,smaa?3:1);profiler_.add_batch(smaa?3:1);
  // Depth of field blends over the resolved world image (world size, before
  // any render-scale upscale) ahead of the additive passes, so sun rays and
  // bloom keep their full strength on the blur.
  if(const int dof=configured_integer(debug_overlay_.config(),"depth_of_field",0);dof>0&&depth_srv_&&fog_prepared_frame_==native_frame_index_*2u){
   const auto clip=bridge_scene_world_to_clip_snapshot();float clip_to_world[16];
   if(clip.valid&&ambient_occlusion_invert(clip.world_to_clip.data(),clip_to_world)){
    transition_profile_pass("scene_dof");UINT draws=0;(void)plan_pass("scene_dof",resolved,nullptr,plan_read(PlanTarget::world_color)|plan_read(PlanTarget::world_depth));
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if(SUCCEEDED(depth_of_field_.render(device_.Get(),context_.Get(),scene_composite_.color_view(),depth_srv_.Get(),resolved,width_,height_,dof,configured_floating_point(debug_overlay_.config(),"depth_of_field_strength",.2f),clip_to_world,seconds,&draws))){profiler_.add_draw(draws,draws);profiler_.add_batch(draws);}
   }
  }
  // Bloom reads the unclamped world colour and adds to the resolved world image, before any interface pass.
  // An unregistered option (renderer-only tests) means off; the registered default is high.
  // Screen-space sun rays (sun_shafts.h), prepared with this frame's fog.
  if(sun_rays_frame_==native_frame_index_&&depth_srv_){
   transition_profile_pass("scene_sun_rays");UINT draws=0;(void)plan_pass("scene_sun_rays",resolved,nullptr,plan_read(PlanTarget::world_color)|plan_read(PlanTarget::world_depth));
   if(SUCCEEDED(sun_rays_.render(device_.Get(),context_.Get(),scene_composite_.color_view(),depth_srv_.Get(),resolved,width_,height_,sun_rays_center_,sun_rays_tint_,sun_rays_haze_luma_,&draws))){profiler_.add_draw(draws,draws);profiler_.add_batch(draws);}
  }
  // Only frames that draw a map's terrain: the global map screen (owned
  // meshes under the last zone's lighting, no terrain) is no world scene
  // and bloomed whole (candidate 08638187 frames 000000/035402).
  if(const int bloom=configured_integer(debug_overlay_.config(),"bloom",0);bloom>0&&ambient_terrain_frame_==native_frame_index_){
   transition_profile_pass("scene_bloom");UINT draws=0;(void)plan_pass("scene_bloom",resolved,nullptr,plan_read(PlanTarget::world_color)|plan_read(PlanTarget::world_depth));
   if(SUCCEEDED(bloom_.render(device_.Get(),context_.Get(),scene_composite_.color_view(),depth_srv_.Get(),glow_frame_==native_frame_index_?glow_srv_.Get():nullptr,resolved,width_,height_,bloom,configured_floating_point(debug_overlay_.config(),"bloom_strength",1.f),bloom_ceiling(fog_prepared_frame_==native_frame_index_*2u?bloom_fog_headroom_:0.f),configured_floating_point(debug_overlay_.config(),"bloom_threshold",1.f),&draws,configured_boolean(debug_overlay_.config(),"bloom_debug_view",false)))){profiler_.add_draw(draws,draws);profiler_.add_batch(draws);}
  }
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
  if(dlss_mode_>=0){
   if(!render_dlss()){
    // Refuse this world's output, including DLAA. Neither the resolved
    // input nor stale NGX output may reach the backbuffer after failure.
    const float black[4]{};context_->ClearRenderTargetView(backbuffer_rtv_.Get(),black);
    if(interface_dsv_)context_->ClearDepthStencilView(interface_dsv_.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1.f,0);
    dlss_frame_refused=true;
   }
  }else if(scene_composite_.dlss()&&render_fsr()){}
#else
  if(scene_composite_.dlss()&&(fsr_mode_>=0?render_fsr():render_dlss())){}
#endif
  else if(scene_composite_.scaled()){
   transition_profile_pass("scene_upscale");
   (void)plan_pass("scene_upscale",backbuffer_rtv_.Get(),interface_dsv_.Get()!=world_dsv_.Get()?interface_dsv_.Get():nullptr,plan_read(PlanTarget::world_color)|plan_read(PlanTarget::world_depth));
   // DLAA (a failed DLSS frame) renders at output size: the world depth is the interface depth.
   const bool separate_depth=interface_dsv_.Get()!=world_dsv_.Get();
   scene_composite_.upscale(context_.Get(),backbuffer_rtv_.Get(),separate_depth?depth_srv_.Get():nullptr,separate_depth?interface_dsv_.Get():nullptr);
   profiler_.add_draw(1,1);profiler_.add_batch();
  }
  // Do not capture again while ending this pass: the backbuffer now contains the scene.
  scene_composited_=true;scene_updated_=false;end_profile_pass();
 }
 // AO settings take effect on whole world frames.
 ao_ambient_written_=false;update_ambient_occlusion_targets();
 rtv_=backbuffer_rtv_;dsv_=interface_dsv_;context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());
 // Seal the refused world and restore UI targets. S_FALSE means no world
 // image was accepted; UI/settings and Present must remain usable so the
 // player can explicitly change an unavailable selection to Off or FSR.
 // a later interface call cannot retry or expose the rejected world image.
 if(dlss_frame_refused){pending_screen_rectangles_.clear();return S_FALSE;}
 // Selection rectangles requested after the world pass and before this
 // composite are the first interface output (each records its own plan pass).
 for(const auto& pending:pending_screen_rectangles_)
  if(FAILED(draw_screen_rectangle(pending.request,pending.viewport)))
   OutputDebugStringA("EID3D11 owned selection rectangle failed; rectangle omitted\n");
 pending_screen_rectangles_.clear();
 if(debug_overlay_.hud_editing()) {
  transition_profile_pass("hud_layout_dim");
  const ScreenRectangleRequest dim{0,0,int(output_width_),int(output_height_),120,false};
  const D3DVIEWPORT7 viewport{0,0,output_width_,output_height_,0.f,1.f};
  const HRESULT dim_hr=draw_screen_rectangle(dim,viewport,debug_overlay_.hud_grid_step());
  if(FAILED(dim_hr))return dim_hr;
 }
 return S_OK;
 });
}


// The sun-shadow mask pass (native_shader.h nativeShadowMaskPS): full
// screen over the pixels a depth prepass marked (stencil 0x80, tested
// before the pixel stage and cleared), against the world depth bound
// read-only. It binds every input it reads itself, so another prepass
// (ground cover's depth-only alpha-tested draw, which writes the same
// receiver record and stencil mark with prepass_depth_states_) can call it
// between its prepass and its EQUAL shading pass. Afterwards the world
// targets (rtv_, dsv_) are bound with the mask at PS t5; the vertex and
// pixel stages, raster, blend and depth states and PS slots t1, t2, t4, b1,
// b2, b6, t9, t20, t21, s1, s3, s4, s10 and the fog frame are the caller's
// to restore.
HRESULT Renderer::resolve_world_shadow_mask() noexcept{
 const bool low=native_low_ps_&&native_low_liquid_ps_&&native_low_pixel_path(debug_overlay_.config());
 ID3D11PixelShader* const shader=low?native_pixel(&Renderer::native_low_shadow_mask_ps_):native_pixel(&Renderer::native_shadow_mask_ps_);
 if(!context_||!shader||!native_shadow_mask_vs_||!shadow_mask_rtv_||!world_depth_read_only_dsv_||!shadow_mask_depth_state_||!depth_srv_)return E_FAIL;
 if(!low)ensure_native_shadow_range();
 if(const HRESULT hr=bind_world_fog();FAILED(hr))return hr;
 const bool history=prepare_shadow_history();
 transition_profile_pass("shadow_mask");
 context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 context_->VSSetShader(native_shadow_mask_vs_.Get(),nullptr,0);
 context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
 ID3D11ShaderResourceView* none[2]{};context_->PSSetShaderResources(5,1,none);
 context_->OMSetRenderTargets(1,shadow_mask_rtv_.GetAddressOf(),world_depth_read_only_dsv_.Get());
 ID3D11ShaderResourceView* receiver[2]={prepass_rest_srv_.Get(),prepass_normal_srv_.Get()};
 context_->PSSetShaderResources(1,2,receiver);context_->PSSetShaderResources(4,1,depth_srv_.GetAddressOf());
 context_->PSSetConstantBuffers(1,1,native_world_lighting_constants_.GetAddressOf());
 context_->PSSetConstantBuffers(2,1,native_shadow_constants_.GetAddressOf());
 context_->PSSetConstantBuffers(6,1,shadow_history_constants_.GetAddressOf());
 context_->PSSetShaderResources(9,1,native_shadow_depth_srv_.GetAddressOf());context_->PSSetShaderResources(64,1,native_shadow_range_srv_.GetAddressOf());
 ID3D11ShaderResourceView* history_srvs[2]={history?shadow_history_srv_[shadow_history_previous_index_].Get():nullptr,
  history?shadow_history_depth_srv_.Get():nullptr};
 context_->PSSetShaderResources(20,2,history_srvs);
 ID3D11SamplerState* history_sampler=history?shadow_history_sampler_.Get():nullptr;
 context_->PSSetSamplers(3,1,&history_sampler);
 context_->PSSetSamplers(1,1,native_shadow_sampler_.GetAddressOf());
 context_->PSSetSamplers(4,1,native_shadow_far_sampler_.GetAddressOf());
 context_->PSSetSamplers(10,1,native_shadow_gather_sampler_.GetAddressOf());
 D3D11_VIEWPORT viewport{0,0,(float)width_,(float)height_,0,1};context_->RSSetViewports(1,&viewport);
 context_->RSSetState(shadow_mask_raster_.Get());context_->OMSetDepthStencilState(shadow_mask_depth_state_.Get(),0x80);
 context_->OMSetBlendState(nullptr,nullptr,0xffffffffu);
 context_->PSSetShader(shader,nullptr,0);
 context_->Draw(3,0);profiler_.add_draw(1,1);profiler_.add_batch();
 context_->PSSetShaderResources(1,2,none);context_->PSSetShaderResources(4,1,none);
 context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());
 context_->PSSetShaderResources(5,1,shadow_mask_srv_.GetAddressOf());
 return S_OK;
}

HRESULT Renderer::resize(UINT w,UINT h){if(core_mode_)return core_resize(w,h);SCENE_ENTRY("resize");
 const UINT old_w=output_width_,old_h=output_height_;
 w=std::max(1u,w);h=std::max(1u,h);
 if(w==old_w && h==old_h)return S_OK;
 if(d3d12_){
  const HRESULT hr=d3d12_->resize(w,h);
  if(FAILED(hr)){
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
   return fail_embedded_d3d12("resize",hr,embedded_d3d12_.failure());
#else
   return hr;
#endif
  }
  output_width_=w;output_height_=h;set_world_size();camp_inventory_output(canvas_width(),canvas_height());return S_OK;
 }
 if(!device_){output_width_=w;output_height_=h;return S_OK;}
 if(!swap_)return E_UNEXPECTED;
 const auto release_targets=[&]{
  context_->ClearState();camp_unclipped_raster_=nullptr;
  reset_camp_popup();reset_water_reflections();rtv_.Reset();backbuffer_rtv_.Reset();
  scene_composite_.reset_targets();dsv_.Reset();world_dsv_.Reset();interface_dsv_.Reset();
  interface_depth_.Reset();depth_.Reset();
 };
 release_targets();
 HRESULT hr=swap_->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,swap_flags_);
 const bool resized=SUCCEEDED(hr);
 if(resized){output_width_=w;output_height_=h;set_world_size();hr=create_targets();}
 if(FAILED(hr)){
  release_targets();
  const HRESULT restore=resized?swap_->ResizeBuffers(0,old_w,old_h,DXGI_FORMAT_UNKNOWN,swap_flags_):S_OK;
  output_width_=old_w;output_height_=old_h;set_world_size();
  const HRESULT targets=SUCCEEDED(restore)?create_targets():restore;
  if(FAILED(targets)){bridge_append_probe_event("display_restore_failed","Output targets could not be restored after rejected display Apply");return targets;}
  return hr;
 }
 camp_inventory_output(canvas_width(),canvas_height());return S_OK;
}

HRESULT Renderer::bind_embedded_targets(){
 SCENE_ENTRY("embedded_bind_targets");
 if(d3d12_){
  // Native draws bind their own targets. Rebinding here would lose a popup,
  // hit-test target or the world's motion/history attachments.
  return S_OK;
 }
 return visit_embedded_device(nullptr,[](void*,const EmbeddedDeviceView& view)->HRESULT{
  view.context->OMSetRenderTargets(1,&view.color,view.depth);
  const D3D11_VIEWPORT viewport{0,0,float(view.width),float(view.height),0,1};
  view.context->RSSetViewports(1,&viewport);
  return S_OK;
 });
}
HRESULT Renderer::clear_embedded_targets(const float* color,const float* depth){
 SCENE_ENTRY("embedded_clear_targets");
 if(!color&&!depth)return E_INVALIDARG;
 if(d3d12_){
  // clear() alone only updates the pre-draw output background. An explicit
  // rectangle clears after draws too and preserves a custom offscreen depth
  // value (clear_target uses 1.0). Do not run legacy Clear's world composite.
  if(color)d3d12_->clear(color);
  // The executor clips against the actual target (including popup/hit-test
  // sizes), rather than assuming the world's current render scale.
  const RECT rectangle{0,0,LONG_MAX,LONG_MAX};
  d3d12_->clear_rectangles(&rectangle,1,color,depth!=nullptr,depth!=nullptr,
   depth?*depth:1.f,0);
 #if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
  if(embedded_d3d12_.frame()==d3d12_){
   const char* reason=embedded_d3d12_.failure();
   if(reason&&*reason)return fail_embedded_d3d12("clear targets",E_FAIL,reason);
  }
 #endif
  return S_OK;
 }
 struct Values{const float* color;const float* depth;} values{color,depth};
 return visit_embedded_device(&values,[](void* user,const EmbeddedDeviceView& view)->HRESULT{
  const auto& values=*static_cast<const Values*>(user);
  if(values.color)view.context->ClearRenderTargetView(view.color,values.color);
  if(values.depth)view.context->ClearDepthStencilView(view.depth,
   D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,*values.depth,0);
  return S_OK;
 });
}

HRESULT Renderer::read_embedded_output_size(UINT& width,UINT& height){
 SCENE_ENTRY("embedded_output_size");
 width=height=0;
 if(d3d12_){
  if(hit_test_failed_)return E_FAIL;
  width=output_width_;height=output_height_;
  return width&&height?S_OK:DXGI_ERROR_INVALID_CALL;
 }
 struct Size{UINT width=0,height=0;} size;
 const HRESULT hr=visit_embedded_output(&size,[](void* user,ID3D11DeviceContext*,ID3D11Texture2D* output)->HRESULT{
  D3D11_TEXTURE2D_DESC desc{};output->GetDesc(&desc);
  if(!desc.Width||!desc.Height)return E_INVALIDARG;
  *static_cast<Size*>(user)={desc.Width,desc.Height};return S_OK;
 });
 if(SUCCEEDED(hr)){width=size.width;height=size.height;}
 return hr;
}

HRESULT Renderer::read_embedded_d3d12_output(std::vector<std::uint8_t>& pixels,UINT& width,UINT& height){
 SCENE_ENTRY("embedded_native_output_readback");
 pixels.clear();width=height=0;
 if(!d3d12_)return DXGI_ERROR_INVALID_CALL;
 if(hit_test_failed_)return E_FAIL;
 try{
  // Drain frontend batches before the executor records its copy. Hit tests
  // read their private target, just like the D3D11 output lease.
  HRESULT hr=has_pending_ui()?flush_ui():S_OK;
  if(SUCCEEDED(hr))hr=flush_native();
  if(FAILED(hr))return hr;
  std::vector<std::uint8_t> captured;UINT w=0,h=0;
  if(hit_test_active_){
   if(!hit_test12_)return DXGI_ERROR_INVALID_CALL;
   hr=d3d12_->read_texture(*hit_test12_,captured,w,h);
  }else hr=d3d12_->read_back_buffer(captured,w,h);
  if(hr!=S_OK)return hr;
  const std::size_t row=std::size_t(w)*4u;
  if(!w||!h||row/4u!=w||row>SIZE_MAX/h||captured.size()!=row*h)return E_FAIL;
  pixels=std::move(captured);width=w;height=h;
  return S_OK;
 }catch(...){return E_OUTOFMEMORY;}
}

HRESULT Renderer::read_embedded_viewport(D3DVIEWPORT7& viewport){
 SCENE_ENTRY("embedded_read_viewport");
 viewport={};
 if(d3d12_){
  // Embedded native submissions use the renderer's world/UI pixel space;
  // projection jitter remains in the camera, never in the viewport origin.
  viewport={0,0,target_width(),target_height(),0.f,1.f};
  return viewport.dwWidth&&viewport.dwHeight?S_OK:DXGI_ERROR_INVALID_CALL;
 }
 if(!context_)return DXGI_ERROR_INVALID_CALL;
 D3D11_VIEWPORT value{};UINT count=1;
 context_->RSGetViewports(&count,&value);
 if(count!=1||value.TopLeftX<0||value.TopLeftY<0||value.Width<1||value.Height<1)return E_INVALIDARG;
 viewport={DWORD(value.TopLeftX),DWORD(value.TopLeftY),DWORD(value.Width),DWORD(value.Height),
  value.MinDepth,value.MaxDepth};
 return S_OK;
}

HRESULT Renderer::clear(DWORD rectangle_count,const D3DRECT* rectangles,DWORD flags,D3DCOLOR color,float z,DWORD stencil){
 if(scene_queue()){
  // Camp placement reads camp state on the game thread; the scene thread
  // receives the placed rectangles.
  std::vector<D3DRECT> rects;
  try{
   if(rectangles&&rectangle_count){
    rects.assign(rectangles,rectangles+rectangle_count);
    for(auto& r:rects){RECT source{r.x1,r.y1,r.x2,r.y2},destination{};
     if(camp_inventory_clear(source,destination))r={destination.left,destination.top,destination.right,destination.bottom};}
   }
  }catch(...){scene_sync("clear_allocation");rects.clear();}
  scene_cmd::Clear command;command.rects=std::move(rects);command.flags=flags;command.color=color;command.z=z;command.stencil=stencil;
  scene_post_command(std::move(command));
  return S_OK;
 }
 std::vector<D3DRECT> placed;
 if(rectangles&&rectangle_count&&!clear_rects_placed_){
  for(DWORD i=0;i<rectangle_count;++i){
   RECT source{rectangles[i].x1,rectangles[i].y1,rectangles[i].x2,rectangles[i].y2},destination{};
   if(camp_inventory_clear(source,destination)){
    if(placed.empty())placed.assign(rectangles,rectangles+rectangle_count);
    placed[i]={destination.left,destination.top,destination.right,destination.bottom};
   }
  }
  if(!placed.empty())rectangles=placed.data();
 }
 // D3D12 backend: no world targets yet. A whole-target colour clear is the
 // frame's background (what the world composite would show where nothing
 // is drawn); a whole depth clear clears the interface depth (the native
 // HUD meshes' target); rectangle clears are named omitted.
 if(d3d12_){
  // The depth clear after a completed world traversal is where the D3D11
  // frame composites the world: the boundary after which game packets are
  // the interface 3D (Packet::run). Core mode sends no CompositeWorld for it.
  if((flags&D3DCLEAR_ZBUFFER)&&bridge_world_draw_completion_snapshot().valid)(void)composite_world();
  const bool whole=!rectangle_count||!rectangles||(rectangle_count==1&&rectangles[0].x1<=0&&rectangles[0].y1<=0&&
   rectangles[0].x2>=LONG(canvas_width())&&rectangles[0].y2>=LONG(canvas_height()));
  if(!whole){
   // As clear_rectangles: the panes' rectangles composed for the wide HUD,
   // clipped to the canvas, scaled to output pixels.
   if(!(flags&(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL)))return S_OK;
   std::vector<RECT> output;
   try{output.reserve(rectangle_count);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
   for(DWORD index=0;index<rectangle_count;++index){
    D3DRECT source=rectangles[index];D3D11_RECT rectangle{};
    if(widescreen::interface_owned){const auto& hud=widescreen::hud_blit;
     source.x1=LONG(std::floor(widescreen::compose_x(float(source.x1),hud)));
     source.y1=LONG(std::floor(widescreen::compose_y(float(source.y1),hud)));
     source.x2=LONG(std::ceil(widescreen::compose_x(float(source.x2),hud)));
     source.y2=LONG(std::ceil(widescreen::compose_y(float(source.y2),hud)));
    }
    if(normalize_legacy_clear_rectangle(source,canvas_width(),canvas_height(),rectangle))
     output.push_back(scale_screen_rectangle(rectangle,canvas_width(),canvas_height(),target_width(),target_height()));
   }
   const float colour[4]={float((color>>16)&0xff)/255.f,float((color>>8)&0xff)/255.f,float(color&0xff)/255.f,float((color>>24)&0xff)/255.f};
   if(!output.empty())d3d12_->clear_rectangles(output.data(),UINT(output.size()),(flags&D3DCLEAR_TARGET)?colour:nullptr,
    (flags&D3DCLEAR_ZBUFFER)!=0,(flags&D3DCLEAR_STENCIL)!=0,z,UINT8(stencil));
   return S_OK;
  }
  if(flags&D3DCLEAR_TARGET){
   const float colour[4]={float((color>>16)&0xff)/255.f,float((color>>8)&0xff)/255.f,float(color&0xff)/255.f,1.f};
   d3d12_->clear(colour);
  }
  if(flags&D3DCLEAR_ZBUFFER){
   d3d12_->clear_depth(z,UINT8(stencil));
   // At full render scale the D3D11 frame's world and interface share one
   // depth surface: the interface depth clear after the composite is what
   // leaves the next world a clear depth. The D3D12 world depth is its own
   // surface, so the same clear reaches it here.
   if(world_d3d12_&&!world_d3d12_bound_){d3d12_->clear_depth_of(*world_d3d12_,z,UINT8(stencil));d3d12_->count("world depth clears (with the interface depth)");}
   else if(world_d3d12_bound_)d3d12_->count("world depth clears (world bound)");
  }
  return S_OK;
 }
 if(!context_)return E_FAIL;const LegacyClearScope scope=legacy_clear_scope(rectangle_count,rectangles);
 if(!scope.valid)return E_INVALIDARG;
 if((flags&D3DCLEAR_TARGET)&&!scene_composited_)scene_updated_=true;
 if((flags&D3DCLEAR_ZBUFFER)&&bridge_world_draw_completion_snapshot().valid)
  commit_shadow_history();
 if((flags&D3DCLEAR_ZBUFFER)&&bridge_world_draw_completion_snapshot().valid){
  const HRESULT hr=composite_world();if(FAILED(hr))return hr;
 }
 // After composite_world: the deferred neighbour liquids it draws still use
 // this frame's water scene copy; the clear itself invalidates it.
 if(flags&(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER))water_capture_ready_=false;
 // Device::Clear has already flushed deferred world draws. This is the first
 // depth-destructive barrier after a verified completed world traversal.
 if((flags&D3DCLEAR_ZBUFFER)&&profiler_.shadow_temporal_armed()){
  const auto boundary=bridge_world_draw_completion_snapshot();
  if(boundary.valid&&profiler_.shadow_temporal_due(presentation_frame_index_,
      boundary.completion_serial)){
   ShadowTemporalFrameSnapshot snapshot{};
   snapshot.presentation_frame=presentation_frame_index_;
   snapshot.world_bridge_frame=boundary.bridge_frame;
   snapshot.world_completion_serial=boundary.completion_serial;
   snapshot.sample_qpc=perf_now();
   snapshot.world_time_valid=bridge_current_world_time(snapshot.world_time);
   snapshot.clear_flags=flags; snapshot.clear_rectangles=rectangle_count;
   snapshot.clear_full_surface=scope.full_surface;
   snapshot.publication_serial=native_shadow_frame_.publication_serial;
   const auto scene_clip=bridge_scene_world_to_clip_snapshot();
   snapshot.scene_frame_serial=scene_clip.scene_frame_serial;
   snapshot.world_to_clip_valid=scene_clip.valid;
   if(scene_clip.valid)snapshot.world_to_clip=scene_clip.world_to_clip;
   snapshot.authored_sun_valid=native_world_lighting_valid_;
   snapshot.shadows_enabled=native_shadows_enabled_;
   if(native_world_lighting_valid_){
    snapshot.authored_sun_direction=native_world_lighting_.sunlight_direction;
    snapshot.authored_sun_diffuse=native_world_lighting_.sunlight_diffuse;
   }
   const auto& decision=native_frame_stats_.shadow_decision;
   snapshot.shadow_begin_calls=decision.begin_calls;
   snapshot.previous_valid_mask=decision.previous_valid_mask;
   snapshot.current_enabled_mask=decision.current_enabled_mask;
   snapshot.world_to_light_changed_mask=decision.light_transform_changed_mask;
   snapshot.camera_slice_changed_mask=decision.camera_slice_changed_mask;
   snapshot.bias_changed_mask=decision.bias_changed_mask;
   snapshot.receiver_bounds_changed_mask=decision.receiver_bounds_changed_mask;
   snapshot.validity_changed_mask=decision.validity_changed_mask;
   snapshot.dirty_mask=decision.dirty_mask;
   snapshot.source_dirty_mask=native_frame_stats_.native_shadow_source_dirty_mask;
   snapshot.source_changed_visuals=native_frame_stats_.native_shadow_source_changed_visuals;
   snapshot.source_changed_terrain=native_frame_stats_.native_shadow_source_changed_terrain;
   for(unsigned index=0;index<5;++index){
    const auto& source=native_shadow_frame_.slices[index];
    auto& slice=snapshot.slices[index];
    slice.enabled=source.enabled; slice.valid=native_shadow_valid_[index];
    slice.caster_revisions=source.validity.size();
    slice.world_to_light=source.world_to_light;
    slice.camera_slice=source.camera_slice;
    slice.bias=source.bias;
    slice.receiver_minimum=source.receiver_minimum;
    slice.receiver_maximum=source.receiver_maximum;
   }
   ComPtr<ID3D11Texture2D> world_color;
   if(swap_)swap_->GetBuffer(0,IID_PPV_ARGS(&world_color));
   if(!profiler_.queue_shadow_temporal_frame(world_color.Get(),depth_.Get(),
       native_shadow_depth_.Get(),snapshot))
    report_placed_hitch_lifecycle("shadow_temporal_capture_failed",
     "world-boundary GPU copy rejected; pair unavailable");
  }
 }
 if(!scope.full_surface)return clear_rectangles(rectangle_count,rectangles,flags,color,z,stencil);
 const PlanPass& pass=plan_pass("clear",(flags&D3DCLEAR_TARGET)?rtv_.Get():nullptr,
                                (flags&(D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL))?dsv_.Get():nullptr);
 float c[]={((color>>16)&255)/255.f,((color>>8)&255)/255.f,(color&255)/255.f,((color>>24)&255)/255.f};
 if(flags&D3DCLEAR_TARGET)context_->ClearRenderTargetView(plan_rtv(pass),c);UINT f=0;
 if(flags&D3DCLEAR_ZBUFFER)f|=D3D11_CLEAR_DEPTH;if(flags&D3DCLEAR_STENCIL)f|=D3D11_CLEAR_STENCIL;
 // A depth clear clears the stencil too (to 0 unless the game names a value):
 // no game state reaches the stencil (apply_state), and the depth prepass's
 // marks (bit 0x80) must start each frame clear. A depth-only clear of the
 // shared D24S8 surface also keeps the driver from a full fast clear once
 // the stencil has been written.
 if(f&D3D11_CLEAR_DEPTH){if(!(f&D3D11_CLEAR_STENCIL))stencil=0;f|=D3D11_CLEAR_STENCIL;}
 if(f)context_->ClearDepthStencilView(plan_dsv(pass),f,z,(UINT8)stencil);return S_OK;
}

HRESULT Renderer::clear_rectangles(DWORD rectangle_count,const D3DRECT* rectangles,DWORD flags,D3DCOLOR color,float z,DWORD stencil){
 std::vector<D3D11_RECT> output;
 try{output.reserve(rectangle_count);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 for(DWORD index=0;index<rectangle_count;++index){D3D11_RECT rectangle{};
  D3DRECT source=rectangles[index];
  // Compose raw partial pane masks before clipping to the output surface.
  // Full-surface clears are handled by clear() before this path.
  if(widescreen::interface_owned){const auto& hud=widescreen::hud_blit;
   source.x1=LONG(std::floor(widescreen::compose_x(float(source.x1),hud)));
   source.y1=LONG(std::floor(widescreen::compose_y(float(source.y1),hud)));
   source.x2=LONG(std::ceil(widescreen::compose_x(float(source.x2),hud)));
   source.y2=LONG(std::ceil(widescreen::compose_y(float(source.y2),hud)));
  }
  if(normalize_legacy_clear_rectangle(source,canvas_width(),canvas_height(),rectangle))
   output.push_back(scale_screen_rectangle(rectangle,canvas_width(),canvas_height(),target_width(),target_height()));
 }
 return clear_target_rectangles(output.data(),DWORD(output.size()),flags,color,z,stencil);
}

void Renderer::clear_interface_depth(float left,float top,float right,float bottom) noexcept{
 widescreen::HudComposition hud{};
 hud.clip_native_screen=true;hud.clip_left=left;hud.clip_top=top;hud.clip_right=right;hud.clip_bottom=bottom;
 RECT rectangle{};
 if(!ui_clip_rect(hud,camp::Clip{},rectangle)||rectangle.right<=rectangle.left||rectangle.bottom<=rectangle.top)return;
 if(d3d12_){d3d12_->clear_rectangles(&rectangle,1,nullptr,true,false,1.f,0);return;}
 if(!context_)return;
 const D3D11_RECT target{rectangle.left,rectangle.top,rectangle.right,rectangle.bottom};
 (void)clear_target_rectangles(&target,1,D3DCLEAR_ZBUFFER,0,1.f,0);
}

// Rectangles in target pixels.
HRESULT Renderer::clear_target_rectangles(const D3D11_RECT* rectangles,DWORD rectangle_count,DWORD flags,D3DCOLOR color,float z,DWORD stencil){
 const DWORD supported=D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL;
 if((flags&supported)==0)return S_OK;
 if(!clear_vs_||!clear_ps_||!clear_constants_||!clear_raster_state_||!clear_blend_state_)
  return E_NOTIMPL;
 const bool clear_depth=(flags&D3DCLEAR_ZBUFFER)!=0;
 const bool clear_stencil=(flags&D3DCLEAR_STENCIL)!=0;
 const UINT depth_state_index=clear_depth?(clear_stencil?2u:0u):1u;
 if((clear_depth||clear_stencil)&&!clear_depth_states_[depth_state_index])return E_NOTIMPL;
 ClearConstants constants{{((color>>16)&255)/255.f,((color>>8)&255)/255.f,
  (color&255)/255.f,((color>>24)&255)/255.f},z,{0,0,0}};
 D3D11_MAPPED_SUBRESOURCE mapped{};HRESULT hr=context_->Map(clear_constants_.Get(),0,
  D3D11_MAP_WRITE_DISCARD,0,&mapped);if(FAILED(hr))return hr;
 std::memcpy(mapped.pData,&constants,sizeof(constants));context_->Unmap(clear_constants_.Get(),0);

 // The clear binds everything it draws with and restores nothing: every pass
 // sets what it reads (frame_plan.h). It leaves GS/HS/DS unbound, VS/PS b7 on
 // the clear values (b7 readers bind their own) and a scissor only the clear raster uses.
 const PlanPass& pass=plan_pass("clear_rectangles",(flags&D3DCLEAR_TARGET)?rtv_.Get():nullptr,
                                (clear_depth||clear_stencil)?dsv_.Get():nullptr);
 context_->IASetInputLayout(nullptr);ID3D11Buffer* no_vertex=nullptr;UINT zero=0;
 context_->IASetVertexBuffers(0,1,&no_vertex,&zero,&zero);context_->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);
 context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 context_->VSSetShader(clear_vs_.Get(),nullptr,0);context_->PSSetShader(clear_ps_.Get(),nullptr,0);
 context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
 ID3D11Buffer* clear_constants=clear_constants_.Get();context_->VSSetConstantBuffers(7,1,&clear_constants);
 context_->PSSetConstantBuffers(7,1,&clear_constants);
 context_->RSSetState(clear_raster_state_.Get());D3D11_VIEWPORT viewport{0,0,(float)target_width(),(float)target_height(),0,1};
 context_->RSSetViewports(1,&viewport);float blend_factor[4]{};
 context_->OMSetBlendState(clear_blend_state_.Get(),blend_factor,0xffffffffu);
 context_->OMSetDepthStencilState((clear_depth||clear_stencil)?clear_depth_states_[depth_state_index].Get():nullptr,(UINT8)stencil);
 bind_plan_targets(pass);
 for(DWORD index=0;index<rectangle_count;++index){
  context_->RSSetScissorRects(1,&rectangles[index]);context_->Draw(3,0);
 }
 return S_OK;
}

void Renderer::end_profile_pass(){if(!profiler_.pass_active())return;ComPtr<ID3D11Texture2D> back;if(profiler_.capture_active()&&swap_){if(!scene_composited_&&backbuffer_rtv_&&scene_composite_.target()){DebugViewStateGuard guard(context_.Get());scene_composite_.render(context_.Get(),scene_composite_.resolve_target(backbuffer_rtv_.Get()),false);scene_composite_.upscale(context_.Get(),backbuffer_rtv_.Get(),nullptr,nullptr);}swap_->GetBuffer(0,IID_PPV_ARGS(&back));}profiler_.end_pass(back.Get());}
void Renderer::transition_profile_pass(const char* name){if(d3d12_)d3d12_->marker(name);if(profiler_.current_pass_is(name))return;end_profile_pass();profiler_.begin_pass(name);}
const char* Renderer::terrain_profile_scope(bool neighbour,bool tessellated,TerrainProfilePart part) noexcept{
 static const char* const names[2][2][8]={
  {{"active_live","active_live_routed","active_cached","active_liquid_ground","active_liquid_water","active_liquid_foam","active_stock_water","active_live_flat"},
   {"active_live_tess","active_live_routed_tess","active_cached_tess","active_liquid_ground_tess","active_liquid_water_tess","active_liquid_foam_tess","active_stock_water_tess","active_live_flat_tess"}},
  {{"neighbour_live","neighbour_live_routed","neighbour_cached","neighbour_liquid_ground","neighbour_liquid_water","neighbour_liquid_foam","neighbour_stock_water","neighbour_live_flat"},
   {"neighbour_live_tess","neighbour_live_routed_tess","neighbour_cached_tess","neighbour_liquid_ground_tess","neighbour_liquid_water_tess","neighbour_liquid_foam_tess","neighbour_stock_water_tess","neighbour_live_flat_tess"}}};
 return names[neighbour?1:0][tessellated?1:0][static_cast<unsigned>(part)];
}
bool Renderer::owned_ui_text_enabled() const noexcept{return configured_boolean(debug_overlay_.config(),"owned_ui_text",false);}
HRESULT Renderer::retire_embedded_world(){
 const SceneInlineScope scene_inline(this,"world_retire");
 if(!context_)return S_FALSE;
 // Drain the last world's commands before dropping their owners. Do this
 // at host retirement, before any next-world decoding or neighbour prebuild.
 HRESULT hr=flush_native();if(FAILED(hr))return hr;
 if(FAILED(hr=composite_world()))return hr;
 if(FAILED(hr=render_pending_blits()))return hr;
 world_state_epoch_=bridge_current_world_epoch();
 release_world_state();scene_world_epoch_=world_state_epoch_;
 dlss_reset_=true;dlss_previous_valid_=false;dlss_last_evaluate_ms_=0;
 return S_OK;
}
RendererMemoryCounters Renderer::read_memory_counters(){
 const SceneInlineScope scene_inline(this,"memory_snapshot");
 RendererMemoryCounters out;
 out.available=context_!=nullptr;
 out.geometry=native_geometry_capacity_bytes();
 {std::lock_guard<std::mutex> guard(native_owned_textures_lock_);
  for(const auto& item:native_owned_textures_)if(!item.second.camp_surface_alias&&!item.second.surface_shared)
   out.textures+=texture_storage_bytes(item.second.texture.Get());}
 for(const auto& group:native_texture_arrays_)for(const auto& page:group.second)
  out.textures+=texture_storage_bytes(page.texture.Get());
 out.animation=native_animation_capacity_+std::uint64_t(native_pose_node_capacity_)*sizeof(NativePoseNodeGpu)+
  std::uint64_t(native_pose_input_capacity_)*sizeof(NativePoseGpu);
 out.world=std::uint64_t(native_world_vertex_capacity_)*sizeof(NativeWorldVertexGpu)+
  std::uint64_t(native_world_state_capacity_)*sizeof(NativeWorldStateGpu)+
  std::uint64_t(native_persistent_instance_capacity_)*(sizeof(NativeInstanceGpu)+sizeof(NativeDrawGpu));
 out.surfaceCpu=g_cpu_surface_bytes.load(std::memory_order_relaxed);
 out.surfaceGpu=g_gpu_surface_bytes.load(std::memory_order_relaxed);
 out.neighbours=neighbours_.accepted.size();
 return out;
}
HRESULT Renderer::begin_embedded_simulation(){
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
 if(!d3d12_)return S_FALSE;
 if(embedded_d3d12_failed_)return E_FAIL;
 return embedded_d3d12_.begin_simulation(configured_integer(debug_overlay_.config(),"reflex",0));
#else
 return S_FALSE;
#endif
}
void Renderer::finish_embedded_frame(){
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
 if(d3d12_)embedded_d3d12_.finish_render();
#endif
}

HRESULT Renderer::begin_engine_frame(){
 // The world epoch is read here, on the game thread, once. A new epoch
 // releases world state on both sides (release_world_state with the bridge's
 // world caches): in scene mode that frame boundary drains the scene thread
 // and runs inline as a whole, so the release never runs on the scene
 // thread. Queued frame begins carry no epoch and never release (a queued
 // begin that re-read the live epoch could release on the scene thread while
 // the game thread loads the next world). scene_world_epoch_ is the game
 // thread's copy of world_state_epoch_, refreshed only while inline; a
 // deferred release (draws still pending) keeps it old, so the next frame
 // goes inline again.
 if(scene_queue()){
  const std::uint64_t epoch=bridge_current_world_epoch();
  // Core mode: every frame begin carries the epoch; the host releases its
  // world state (release_world_state) once its draws allow, and this
  // process releases its own game-side world caches here, as the inline
  // release does in standard mode.
  if(core_mode_){
   if(epoch&&epoch!=scene_world_epoch_){
    scene_world_epoch_=epoch;
    bridge_release_world_caches();
    owned_resource_runtime_release_world_bookkeeping();
   }
   scene_ui_pending_=false;scene_native_pending_=false;
   scene_cmd::BeginEngineFrame command;command.world_epoch=epoch;
   scene_post_command(command);
   return S_OK;
  }
  if(!epoch||epoch==scene_world_epoch_){
   scene_ui_pending_=false;scene_native_pending_=false;
   scene_post_command(scene_cmd::BeginEngineFrame{});
   return S_OK;
  }
  const SceneInlineScope scene_inline(this,"world_change");
  const HRESULT hr=begin_engine_frame_at(epoch);
  scene_world_epoch_=world_state_epoch_;
  return hr;
 }
 return begin_engine_frame_at(on_scene_thread()?0:bridge_current_world_epoch());
}

// world_epoch 0: no world change is considered (queued frame begins).
HRESULT Renderer::begin_engine_frame_at(std::uint64_t world_epoch){
 // An aborted map Draw must never carry its selected arrow records into
 // the following engine frame. Normal map Draw consumes these itself.
 discard_world_map_markers();
 if(d3d12_){
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
  if(embedded_d3d12_failed_)return E_FAIL;
  const HRESULT phase=embedded_d3d12_.begin_render(configured_integer(debug_overlay_.config(),"reflex",0));
  if(FAILED(phase))return fail_embedded_d3d12("begin render phase",phase,embedded_d3d12_.failure());
#endif
  // D3D12 backend: the previous invocation's UI, then a new frame index for
  // the UI's per-frame bookkeeping and the world target bound (as rtv_/dsv_
  // are on D3D11) until the composite.
  HRESULT hr=flush_native();
  if(SUCCEEDED(hr))hr=render_pending_blits();
  // The first engine frame of a new world: a clean slate (as on D3D11).
  if(SUCCEEDED(hr)&&world_epoch&&world_epoch!=world_state_epoch_&&native_pending_draws_.empty()){world_state_epoch_=world_epoch;release_world_state();dlss_reset_=true;}
  // Ground marks of an engine frame that was never composited go, as at
  // D3D11's begin below (its composite consumed them, the clear drops the rest).
  ++native_frame_index_;advance_dlss_jitter();scene_composited_=false;scene_updated_=false;deferred_ground_marks_.clear();
  if(FAILED(hr))return hr;
  const HRESULT bound=bind_world_d3d12();
  if(SUCCEEDED(bound))begin_object_motion_d3d12();
  return bound;
 }
 if(!context_||!scene_composite_.target()||!world_dsv_)return E_FAIL;
 // Complete the previous invocation before its bridge/UI epoch expires.
 // In particular, a save's screen copy may already have sealed its world
 // composition before the next authoritative engine invocation begins.
 HRESULT hr=flush_native();if(FAILED(hr))return hr;
 if(FAILED(hr=composite_world()))return hr;
 if(FAILED(hr=render_pending_blits()))return hr;
 // The first engine frame of a new world (map or main menu): a clean slate.
 if(world_epoch&&world_epoch!=world_state_epoch_&&native_pending_draws_.empty()){
  world_state_epoch_=world_epoch;release_world_state();dlss_reset_=true;dlss_previous_valid_=false;dlss_last_evaluate_ms_=0;
 }
 reap_tile_family_jobs(); // dropped tile family jobs, once each has stopped
 ++native_frame_index_;advance_dlss_jitter();begin_dlss_object_motion();
 native_hiz_frame_=UINT64_MAX;native_world_clip_dirty_=true;
 scene_composited_=false;scene_updated_=false;deferred_ground_marks_.clear();
 rtv_=scene_composite_.target();dsv_=world_dsv_;
 if(hit_test_active_){rtv_=hit_test_rtv_;dsv_=hit_test_dsv_;} // a frame begin inside the zone hit-test frame
 context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());
 return S_OK;
}
void Renderer::begin_scene(){
 if(!ready())return;
 begin_scene_game();
 if(scene_queue()){scene_post_command(scene_cmd::BeginScene{});return;}
 begin_scene_renderer();
}

// The renderer-state part of begin_scene (the scene thread in scene mode).
void Renderer::begin_scene_renderer(){
 // D3D12 backend: the world draw and sun shadow settings, without the D3D11
 // resources below. The draw distance sets the far fade and cull (it stayed
 // at 300 once, so every surface past it faded to the border fog colour).
 // Crown impostors as on D3D11 (bake_neighbour_impostors_d3d12).
 if(d3d12_){
  const auto& config=debug_overlay_.config();
  native_draw_distance_=float(configured_integer(config,"draw_distance",300));
  native_lod_error_px_=configured_boolean(config,"lod",true)?configured_floating_point(config,"lod_pixel_error",1.f):0.f;
  native_impostor_pixels_=configured_boolean(config,"impostors",true)&&!impostors_.failed?configured_floating_point(config,"impostor_pixels",64.f):0.f;
  native_impostor_debug_=configured_boolean(config,"impostor_debug",false);
  const bool shadows=configured_boolean(config,"shadows",true);
  const UINT shadow_side=(1024u<<configured_integer(config,"shadow_cascade_quality",1))>>vram_shadow_shift();
  const UINT point_side=(128u<<configured_integer(config,"point_shadow_quality",2))>>vram_shadow_shift();
  const auto close_cascades=static_cast<std::uint8_t>(configured_integer(config,"shadow_close_cascades",3));
  const bool far_shadows=configured_boolean(config,"shadow_far",true);
  const bool far_foliage_opaque=configured_boolean(config,"shadow_far_foliage_opaque",true);
  const bool point_shadows=configured_boolean(config,"point_shadows",true);
  bridge_set_point_shadow_merge_distance(.5f*float(configured_integer(config,"point_shadow_merge",3)));
  bridge_set_point_shadow_fast_overlays(configured_integer(config,"point_shadow_fast_overlays",4));
  bridge_set_fur_mode(configured_integer(config,"fur_mode",0)); // fur prototype: the submission side (fur.h)
  {
   const int range=configured_integer(config,"shadow_close_range",0);
   bridge_set_shadow_close_range(float(range>0?range:64+16*std::clamp(configured_integer(config,"shadow_cascade_quality",1),0,2)));
  }
  const auto softness=shadow_softness_constants(configured_integer(config,"shadow_softness",1),
   configured_integer(config,"shadow_sun_angle",15),configured_integer(config,"point_shadow_light_size",100));
  if(softness!=native_shadow_softness_){native_shadow_softness_=softness;native_shadow_constants_dirty_=true;}
  // The early-out cells (shadowRange) are not on this backend: never current.
  native_shadow_early_out_=false;native_shadow_range_mask_=0;
  if(far_foliage_opaque!=native_far_foliage_opaque_&&SUCCEEDED(flush_native())){
   native_far_foliage_opaque_=far_foliage_opaque;
   for(UINT slice=3;slice<5;++slice){native_shadow_valid_[slice]=false;native_shadow_static_valid_[slice]=false;}
   shadow_history_valid_=false; // as the D3D11 settings path
   native_layout_changed();
  }
  const bool changed=shadows!=native_shadows_enabled_||shadow_side!=native_shadow_side_||
   point_side!=native_point_shadow_side_||close_cascades!=native_close_shadow_cascades_||
   far_shadows!=native_far_shadows_enabled_||point_shadows!=native_point_shadows_enabled_;
  if(changed&&SUCCEEDED(flush_native())){
   if(shadow_side!=native_shadow_side_){shadow12_.maps.reset();shadow12_.statics.reset();}
   if(point_side!=native_point_shadow_side_||!point_shadows){
    for(UINT page=0;page<PointShadowTextureLayout::array_bindings;++page)
     d3d12_->set_resource(PointShadowTextureLayout::first_srv+page,static_cast<const D3D12Texture*>(nullptr));
    native_point_shadow_pages_.clear();native_point_shadow_generation_=0;
   }
   native_shadows_enabled_=shadows;native_shadow_side_=shadow_side;native_close_shadow_cascades_=close_cascades;
   native_point_shadow_side_=point_side;native_far_shadows_enabled_=far_shadows;native_point_shadows_enabled_=point_shadows;
   bridge_set_shadow_settings(shadow_side,close_cascades,far_shadows,point_shadows);
   shadow_history_valid_=false;native_shadow_valid_.fill(false);native_shadow_static_valid_.fill(false);native_shadow_frame_={};
   native_shadow_dirty_mask_=native_shadow_cleared_mask_=0;native_shadow_failed_=false;
   native_shadow_constants_dirty_=true;
  }
  if(native_shadow_constants_dirty_&&!native_shadow_collecting_&&SUCCEEDED(upload_native_shadow_constants(0)))
   native_shadow_constants_dirty_=false;
  native_frame_stats_.native_shadows_enabled=native_shadows_enabled_;
  return;
 }
 // The last authoritative list/publication survives a skipped world pass.
 point_pass_seen_=point_publication_seen_=false;shadow_renderer_seen_.fill(false);
 const auto epoch=bridge_current_world_epoch();
 if(point_shadow_epoch_!=epoch){report_shadow_diagnostics("world_change");point_shadow_epoch_=epoch;point_shadows_cumulative_={};
  point_pass_skipped_frames_=point_publication_missing_frames_=0;
  shadow_renderer_cumulative_={};shadow_renderer_cumulative_.epoch=epoch;}
 const auto& config=debug_overlay_.config();
 combat_ui_set_options(config);
 const bool shadows=configured_boolean(config,"shadows",true);
 // The VRAM budget may halve both map sizes (renderer_vram_budget.cpp).
 const UINT shadow_side=(1024u<<configured_integer(config,"shadow_cascade_quality",1))>>vram_shadow_shift();
 const UINT point_side=(128u<<configured_integer(config,"point_shadow_quality",2))>>vram_shadow_shift();
 const auto close_cascades=static_cast<std::uint8_t>(configured_integer(config,"shadow_close_cascades",3));
 const bool far_shadows=configured_boolean(config,"shadow_far",true);
 const bool far_foliage_opaque=configured_boolean(config,"shadow_far_foliage_opaque",true);
 const bool point_shadows=configured_boolean(config,"point_shadows",true);
 bridge_set_point_shadow_merge_distance(.5f*float(configured_integer(config,"point_shadow_merge",3)));
 bridge_set_point_shadow_fast_overlays(configured_integer(config,"point_shadow_fast_overlays",4));
 bridge_set_fur_mode(configured_integer(config,"fur_mode",0)); // fur prototype: the submission side (fur.h)
 // Close shadow band length: shadow_close_range metres, 0 by cascade quality.
 {
  const int range=configured_integer(config,"shadow_close_range",0);
  bridge_set_shadow_close_range(float(range>0?range:64+16*std::clamp(configured_integer(config,"shadow_cascade_quality",1),0,2)));
 }
 // Softness only changes receiver constants; maps and history stay valid.
 const auto softness=shadow_softness_constants(configured_integer(config,"shadow_softness",1),
  configured_integer(config,"shadow_sun_angle",15),configured_integer(config,"point_shadow_light_size",100));
 if(softness!=native_shadow_softness_){native_shadow_softness_=softness;native_shadow_constants_dirty_=true;}
 // The early-out only skips filter work whose result it already knows.
 const bool early_out=configured_boolean(config,"shadow_early_out",true);
 if(early_out!=native_shadow_early_out_){
  native_shadow_early_out_=early_out;native_shadow_constants_dirty_=true;
  if(!early_out)release_native_shadow_range();
 }
 native_draw_distance_=float(configured_integer(config,"draw_distance",300));
 native_lod_error_px_=configured_boolean(config,"lod",true)?configured_floating_point(config,"lod_pixel_error",1.f):0.f;
 native_impostor_pixels_=configured_boolean(config,"impostors",true)&&!impostors_.failed?configured_floating_point(config,"impostor_pixels",64.f):0.f;
 native_impostor_debug_=configured_boolean(config,"impostor_debug",false);

 const bool changed=shadows!=native_shadows_enabled_||shadow_side!=native_shadow_side_||
  point_side!=native_point_shadow_side_||close_cascades!=native_close_shadow_cascades_||
  far_shadows!=native_far_shadows_enabled_||point_shadows!=native_point_shadows_enabled_;
 if(far_foliage_opaque!=native_far_foliage_opaque_&&SUCCEEDED(flush_native())){
  native_far_foliage_opaque_=far_foliage_opaque;
  // Both reuse paths must see invalid maps, including the immutable publication
  // fast path and the staggered far refresh. Rebuild their static base as well.
  for(UINT slice=3;slice<5;++slice){
   native_shadow_valid_[slice]=false;native_shadow_static_valid_[slice]=false;
  }
  shadow_history_valid_=false;
  native_layout_changed();
 }
 if(changed&&SUCCEEDED(flush_native())){
  ID3D11ShaderResourceView* empty=nullptr;
  if(shadow_side!=native_shadow_side_){
   context_->PSSetShaderResources(9,1,&empty);
   for(auto& view:native_shadow_dsv_)view.Reset();
   native_shadow_depth_srv_.Reset();native_shadow_depth_.Reset();
   release_native_shadow_range();
  }
  if(point_side!=native_point_shadow_side_||!point_shadows){
   std::array<ID3D11ShaderResourceView*,PointShadowTextureLayout::array_bindings> empty_points{};
   context_->PSSetShaderResources(PointShadowTextureLayout::first_srv,static_cast<UINT>(empty_points.size()),empty_points.data());
   native_point_shadow_pages_.clear();
   native_point_shadow_generation_=0;
  }
  native_shadows_enabled_=shadows;native_shadow_side_=shadow_side;
  native_point_shadow_side_=point_side;native_close_shadow_cascades_=close_cascades;
  native_far_shadows_enabled_=far_shadows;native_point_shadows_enabled_=point_shadows;
  bridge_set_shadow_settings(shadow_side,close_cascades,far_shadows,point_shadows);
  shadow_history_valid_=false;native_shadow_valid_.fill(false);native_shadow_frame_={};
  native_shadow_dirty_mask_=native_shadow_cleared_mask_=0;native_shadow_failed_=false;
  native_shadow_constants_dirty_=true;
  if(native_shadow_constants_&&SUCCEEDED(upload_native_shadow_constants(0)))
   native_shadow_constants_dirty_=false;
 }

 native_frame_stats_.native_shadows_enabled=native_shadows_enabled_;
 const bool profile=debug_overlay_.wants_profiling();profiler_.set_enabled(profile);
 bridge_set_profile_enabled(profile||placed_hitch_profile_.active());
 if(!profiler_.frame_active())profiler_.begin_frame(output_width_,output_height_);
 transition_profile_pass("legacy_scene");
}
void Renderer::end_scene(){
 if(scene_queue()){scene_post_command(scene_cmd::EndScene{});return;}
 if(d3d12_)return;
 if(!native_point_lights_.empty()){
  if(!point_pass_seen_)++point_pass_skipped_frames_;
  if(!point_publication_seen_)++point_publication_missing_frames_;
 }
 commit_shadow_history();end_profile_pass();
}

// ---- Typed frame commands (scene_commands_world.h), run on the scene thread
// (or inline in order when a post could not be allocated).
void scene_cmd::BeginEngineFrame::run(Renderer& r){Renderer::report_scene_failure("begin_engine_frame",r.begin_engine_frame_at(world_epoch));}
void scene_cmd::MarkLoadPicture::run(Renderer& r){r.load_picture_this_frame_=true;}
void scene_cmd::HitTestFrame::run(Renderer& r){r.hit_test_frame_apply(begin);}

void Renderer::hit_test_frame(bool begin){
 if(scene_queue()){scene_post_command(scene_cmd::HitTestFrame{begin});return;}
 hit_test_frame_apply(begin);
}

// The zone hit-test frame (#200). CI3DScreenGlobalMap::GenerateHitTestDDS
// gives every zone figure a flat default colour, (index + 1) * 16 + 8 on all
// channels, hides everything else, clears to 0, draws one frame without a
// flip and copies it into its mask, from which hover and click read the zone
// as ((B >> 4) & 15) - 1. Drawn lit into the HDR world target and through
// the composite and post chain, the codes drifted (Zone9 of Gipat came out
// near black). Between begin and end the world draws into an output-size
// B8G8R8A8 target with its own depth: the vertex stage takes the default
// colour instead of lighting and the pixel stage writes it with alpha 1
// (nativeCombinerTail), no jitter, never composited; the game's capture
// reads this target (capture_source). A target that cannot be created is
// named once and the mask capture is refused.
void Renderer::hit_test_frame_apply(bool begin) noexcept{
 if(!begin)hit_test_failed_=false;
 if(d3d12_){
  // D3D12: the same frame into hit_test12_, which stands in for the world
  // target (every world pass binds world_d3d12_, bind_world_targets_d3d12
  // alone); the composite skips it, and the game's capture or readback
  // copies it (Capture::run, Readback::run).
  if(begin==hit_test_active_)return;
  (void)flush_native();
  if(!begin){
   hit_test_active_=false;
   d3d12_->set_hit_test_frame(false);
   if(hit_test_saved_world12_)world_d3d12_=std::move(hit_test_saved_world12_);
   hit_test_saved_world12_.reset();
   // The world target takes its world role back first (set_targets keeps it).
   if(world_d3d12_bound_&&world_d3d12_&&!scene_composited_){d3d12_->set_target(world_d3d12_.get(),true);bind_world_targets_d3d12();}
   return;
  }
  const auto report=[this](const char* detail){
   static bool reported=false;
   if(!reported){reported=true;bridge_report_event("zone_hit_test",detail);}
  };
  // The verified scope starts before the game's nested DrawFrame. The
  // preceding frame may already be composited; that does not invalidate the
  // world texture saved here. Its new frame begin binds the mask below.
  if(!world_d3d12_){report("world target unavailable at the hit-test frame (D3D12); the zone mask is the normal frame");return;}
  if(!hit_test12_||!d3d12_->texture_matches(*hit_test12_,output_width_,output_height_,DXGI_FORMAT_B8G8R8A8_UNORM)){
   hit_test12_.reset();
   if(FAILED(d3d12_->create_target(output_width_,output_height_,hit_test12_,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_D24_UNORM_S8_UINT))){
    hit_test12_.reset();report("target allocation failed (D3D12); the zone mask is the normal frame");return;
   }
  }
  hit_test_saved_world12_=std::move(world_d3d12_);world_d3d12_=hit_test12_;
  hit_test_active_=true;
  d3d12_->set_hit_test_frame(true);
  bind_world_targets_d3d12();
  const float zero[4]{};d3d12_->clear_target(zero,true);
  return;
 }
 if(begin==hit_test_active_||!device_||!context_)return;
 (void)flush_native();
 if(!begin){
  hit_test_active_=false;
  rtv_=hit_test_saved_rtv_;dsv_=hit_test_saved_dsv_;hit_test_saved_rtv_.Reset();hit_test_saved_dsv_.Reset();
  context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());
  return;
 }
 hit_test_failed_=true;
 if(!hit_test_color_||hit_test_width_!=output_width_||hit_test_height_!=output_height_){
  hit_test_color_.Reset();hit_test_rtv_.Reset();hit_test_depth_.Reset();hit_test_dsv_.Reset();hit_test_width_=hit_test_height_=0;
  D3D11_TEXTURE2D_DESC color{};color.Width=output_width_;color.Height=output_height_;color.MipLevels=color.ArraySize=color.SampleDesc.Count=1;
  color.Format=DXGI_FORMAT_B8G8R8A8_UNORM;color.Usage=D3D11_USAGE_DEFAULT;color.BindFlags=D3D11_BIND_RENDER_TARGET;
  D3D11_TEXTURE2D_DESC depth=color;depth.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;depth.BindFlags=D3D11_BIND_DEPTH_STENCIL;
  if(world_dsv_){D3D11_DEPTH_STENCIL_VIEW_DESC view{};world_dsv_->GetDesc(&view);if(view.Format!=DXGI_FORMAT_UNKNOWN)depth.Format=view.Format;}
  if(FAILED(device_->CreateTexture2D(&color,nullptr,&hit_test_color_))||FAILED(device_->CreateRenderTargetView(hit_test_color_.Get(),nullptr,&hit_test_rtv_))||
     FAILED(device_->CreateTexture2D(&depth,nullptr,&hit_test_depth_))||FAILED(device_->CreateDepthStencilView(hit_test_depth_.Get(),nullptr,&hit_test_dsv_))){
   hit_test_color_.Reset();hit_test_rtv_.Reset();hit_test_depth_.Reset();hit_test_dsv_.Reset();
   static bool reported=false;
   if(!reported){reported=true;bridge_report_event("zone_hit_test","target allocation failed; zone mask capture refused");}
   return;
  }
  hit_test_width_=output_width_;hit_test_height_=output_height_;
 }
 hit_test_saved_rtv_=rtv_;hit_test_saved_dsv_=dsv_;
 rtv_=hit_test_rtv_;dsv_=hit_test_dsv_;
 const float zero[4]{};context_->ClearRenderTargetView(rtv_.Get(),zero);
 context_->ClearDepthStencilView(dsv_.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1.f,0);
 context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());
 hit_test_active_=true;hit_test_failed_=false;
}

HRESULT Renderer::capture_source(ComPtr<ID3D11Texture2D>& source){
 source.Reset();
 if(hit_test_failed_)return E_FAIL;
 if(hit_test_active_){const HRESULT hr=flush_native();if(SUCCEEDED(hr))source=hit_test_color_;return FAILED(hr)?hr:source?S_OK:E_FAIL;}
 HRESULT hr=composite_world();if(SUCCEEDED(hr))hr=render_pending_blits();
 if(SUCCEEDED(hr))hr=swap_?swap_->GetBuffer(0,IID_PPV_ARGS(&source)):E_FAIL;
 return hr;
}
void scene_cmd::BeginScene::run(Renderer& r){r.begin_scene_renderer();}
void scene_cmd::EndScene::run(Renderer& r){r.end_scene();}
void scene_cmd::CompositeWorld::run(Renderer& r){Renderer::report_scene_failure("composite_world",r.composite_world());}
void scene_cmd::Clear::run(Renderer& r){
 r.clear_rects_placed_=true;
 const HRESULT hr=r.clear(DWORD(rects.size()),rects.empty()?nullptr:rects.data(),flags,color,z,stencil);
 r.clear_rects_placed_=false;Renderer::report_scene_failure("clear",hr);
}

// ---- Scene thread (render_thread option, scene_thread.h) --------------------
// The game thread waits for the scene thread to drain (an unconverted path).
// Each reason is logged once and counted for render_thread_sync_summary.
void Renderer::scene_sync(const char* reason) noexcept{
 if(!scene_queue())return;
 // Core mode: no scene thread here; an unconverted entry is refused by name
 // (its body then runs against the device-free x86 Renderer and omits).
 if(core_mode_){core_refuse(reason);return;}
 const frame_acct::Scope wait_scope(frame_acct::wait_scene_sync);
 if(game_upload_pending_)flush_game_uploads();
 bool known=false;
 for(auto& entry:scene_syncs_)if(std::strcmp(entry.first,reason)==0){++entry.second;known=true;break;}
 {bool counted=false;
  for(auto& entry:scene_sync_window_)if(std::strcmp(entry.first,reason)==0){++entry.second;counted=true;break;}
  if(!counted)try{scene_sync_window_.emplace_back(reason,1);}catch(...){}}
 if(!known){
  try{scene_syncs_.emplace_back(reason,1);}catch(...){}
  char detail[192];std::snprintf(detail,sizeof(detail),"reason=%s frame=%llu (this path waits for the scene thread; logged once)",
   reason,static_cast<unsigned long long>(presentation_frame_index_));
  bridge_report_event("render_thread_sync",detail);
 }
 // The scene thread is busy while this thread waits; its own commands are
 // bracketed (slot 1), so a real stall is reported there, with its name,
 // and legitimate exempt work (shader compiles) is not a game-thread stall.
 const stall_watch::Exempt wait;
 {
 #ifdef EID3D11_EMBEDDED
 const CorePerf::AnyThreadScope coreWait(CorePerf::SceneSyncWait);
 #endif
 scene_thread_->sync();
 }
}

// A queued call's failure (scene thread): one line per operation, at most a
// few each, so a failing queued path is never silent.
void Renderer::report_scene_failure(const char* operation,HRESULT hr) noexcept{
 if(SUCCEEDED(hr))return;
#ifdef EID3D11_EMBEDDED
 // Fixed-storage host aggregation remains usable when allocation is failing.
 char embedded_detail[160];std::snprintf(embedded_detail,sizeof(embedded_detail),"operation=%s hr=%08lx",operation,static_cast<unsigned long>(hr));
 embedded_report_render_failure("render_thread_command_failed",embedded_detail);
 return;
#endif
 static std::mutex lock;static std::vector<std::pair<const char*,unsigned>> counts;
 unsigned count=0;
 {std::lock_guard<std::mutex> guard(lock);
  try{
   auto found=std::find_if(counts.begin(),counts.end(),[&](const auto& entry){return std::strcmp(entry.first,operation)==0;});
   if(found==counts.end()){counts.emplace_back(operation,0u);found=counts.end()-1;}
   count=++found->second;
  }catch(...){return;}}
 if(count>4)return;
 char detail[160];std::snprintf(detail,sizeof(detail),"operation=%s hr=%08lx count=%u",operation,static_cast<unsigned long>(hr),count);
 bridge_report_event("render_thread_command_failed",detail);
}
void Renderer::set_gamma_ramp(const WORD* red,const WORD* green,const WORD* blue) noexcept{
 if(!red||!green||!blue)return;
 std::lock_guard<std::mutex> lock(gamma_lock_);
 bool identity=true;
 for(unsigned i=0;i<256;++i){
  gamma_ramp_[i]=red[i];gamma_ramp_[256+i]=green[i];gamma_ramp_[512+i]=blue[i];
  identity=identity&&(red[i]>>8)==i&&(green[i]>>8)==i&&(blue[i]>>8)==i;
 }
 gamma_identity_=identity;gamma_changed_=gamma_set_=true;
}

void Renderer::gamma_ramp(WORD* red,WORD* green,WORD* blue) const noexcept{
 if(!red||!green||!blue)return;
 std::lock_guard<std::mutex> lock(gamma_lock_);
 for(unsigned i=0;i<256;++i){ // before the game sets one: the neutral ramp
  red[i]=gamma_set_?gamma_ramp_[i]:WORD(i*256);green[i]=gamma_set_?gamma_ramp_[256+i]:WORD(i*256);
  blue[i]=gamma_set_?gamma_ramp_[512+i]:WORD(i*256);
 }
}

HRESULT Renderer::apply_gamma_ramp() noexcept{
 std::array<WORD,768> ramp{};bool changed=false;
 {std::lock_guard<std::mutex> lock(gamma_lock_);
  if(gamma_identity_)return S_FALSE;
  ramp=gamma_ramp_;changed=gamma_changed_;gamma_changed_=false;}
 if(gamma_failed_||!device_||!context_||!backbuffer_rtv_)return S_FALSE;
 HRESULT hr=S_OK;
 const auto fail=[&](HRESULT result){gamma_failed_=true;return result;};
 if(!gamma_vs_||!gamma_ps_||!gamma_sampler_){
  const ShaderProgram programs[2]={{{kGammaShader,sizeof(kGammaShader)-1},nullptr,nullptr,"gammaVS","vs_4_0",0},
                                   {{kGammaShader,sizeof(kGammaShader)-1},nullptr,nullptr,"gammaPS","ps_4_0",0}};
  ComPtr<ID3DBlob> code,errors;
  if(FAILED(hr=compile_shader(programs[0],&code,&errors))||
     FAILED(hr=create_shader(device_.Get(),code->GetBufferPointer(),code->GetBufferSize(),nullptr,&gamma_vs_)))return fail(hr);
  code.Reset();errors.Reset();
  if(FAILED(hr=compile_shader(programs[1],&code,&errors))||
     FAILED(hr=create_shader(device_.Get(),code->GetBufferPointer(),code->GetBufferSize(),nullptr,&gamma_ps_)))return fail(hr);
  D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
  if(FAILED(hr=device_->CreateSamplerState(&sampler,&gamma_sampler_)))return fail(hr);
 }
 if(!gamma_lut_){
  D3D11_TEXTURE2D_DESC lut{};lut.Width=256;lut.Height=1;lut.MipLevels=lut.ArraySize=1;lut.Format=DXGI_FORMAT_R16G16B16A16_UNORM;
  lut.SampleDesc.Count=1;lut.Usage=D3D11_USAGE_DEFAULT;lut.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  if(FAILED(hr=device_->CreateTexture2D(&lut,nullptr,&gamma_lut_))||
     FAILED(hr=device_->CreateShaderResourceView(gamma_lut_.Get(),nullptr,&gamma_lut_srv_)))return fail(hr);
  changed=true;
 }
 if(changed){
  std::array<std::uint16_t,1024> texels{};
  const auto level=[](WORD value){return std::uint16_t(std::lround(std::min(1.0,value/65280.0)*65535.0));};
  for(unsigned i=0;i<256;++i){texels[i*4]=level(ramp[i]);texels[i*4+1]=level(ramp[256+i]);texels[i*4+2]=level(ramp[512+i]);texels[i*4+3]=65535;}
  context_->UpdateSubresource(gamma_lut_.Get(),0,nullptr,texels.data(),256*8,0);
 }
 ComPtr<ID3D11Resource> back_resource;backbuffer_rtv_->GetResource(&back_resource);
 ComPtr<ID3D11Texture2D> back;if(FAILED(hr=back_resource.As(&back)))return fail(hr);
 D3D11_TEXTURE2D_DESC desc{};back->GetDesc(&desc);
 D3D11_TEXTURE2D_DESC copy_desc{};if(gamma_copy_)gamma_copy_->GetDesc(&copy_desc);
 if(!gamma_copy_||copy_desc.Width!=desc.Width||copy_desc.Height!=desc.Height||copy_desc.Format!=desc.Format){
  gamma_copy_.Reset();gamma_copy_srv_.Reset();
  D3D11_TEXTURE2D_DESC copy=desc;copy.MipLevels=copy.ArraySize=1;copy.SampleDesc={1,0};copy.Usage=D3D11_USAGE_DEFAULT;
  copy.BindFlags=D3D11_BIND_SHADER_RESOURCE;copy.CPUAccessFlags=0;copy.MiscFlags=0;
  if(FAILED(hr=device_->CreateTexture2D(&copy,nullptr,&gamma_copy_))||
     FAILED(hr=device_->CreateShaderResourceView(gamma_copy_.Get(),nullptr,&gamma_copy_srv_)))return fail(hr);
 }
 transition_profile_pass("gamma_ramp");
 context_->OMSetRenderTargets(0,nullptr,nullptr);
 context_->CopyResource(gamma_copy_.Get(),back.Get());
 context_->OMSetRenderTargets(1,backbuffer_rtv_.GetAddressOf(),nullptr);
 D3D11_VIEWPORT viewport{0,0,float(desc.Width),float(desc.Height),0,1};context_->RSSetViewports(1,&viewport);
 context_->RSSetState(nullptr);context_->OMSetBlendState(nullptr,nullptr,0xffffffffu);context_->OMSetDepthStencilState(nullptr,0);
 context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);context_->GSSetShader(nullptr,nullptr,0);
 context_->VSSetShader(gamma_vs_.Get(),nullptr,0);context_->PSSetShader(gamma_ps_.Get(),nullptr,0);
 ID3D11ShaderResourceView* inputs[2]={gamma_copy_srv_.Get(),gamma_lut_srv_.Get()};context_->PSSetShaderResources(0,2,inputs);
 context_->PSSetSamplers(0,1,gamma_sampler_.GetAddressOf());
 context_->Draw(3,0);
 ID3D11ShaderResourceView* empty[2]{};context_->PSSetShaderResources(0,2,empty);
 return S_OK;
}

void Renderer::update_status_notice(const char* reset_notice){
#ifndef EID3D11_EMBEDDED
 bridge_verify_renderer_ownership_after_world();
 debug_overlay_.set_status_notice(!bridge_semantic_slots_installed()?
  "EID3D11: game.exe not verified - plain D3D wrapper only, no owned visuals (see bridge.jsonl)":
  x64remote::remote_notice()?x64remote::remote_notice():
  bridge_takeover_notice()?bridge_takeover_notice():
  !bridge_game_sites_all_verified()?"EID3D11: some game.exe sites differ - those features are off (see bridge.jsonl)":
  bridge_addon_notice()?bridge_addon_notice():reset_notice?reset_notice:camp_inventory_notice()?camp_inventory_notice():
  widescreen::notice?widescreen::notice:bridge_save_id_clash_notice()?bridge_save_id_clash_notice():
  material_notice_.empty()?nullptr:material_notice_.c_str());
#else
 debug_overlay_.set_status_notice(reset_notice?reset_notice:camp_inventory_notice()?camp_inventory_notice():
  material_notice_.empty()?nullptr:material_notice_.c_str());
#endif
}

// Author's native frame composition, SR/FG and final presentation. Both the
// remote host and the embedded executor use this body. Local game-owned
// publications are taken here when no remote frame snapshot was supplied.
#if !defined(EID3D11_SHADER_EMBED)
HRESULT Renderer::present_frame_d3d12(const ScenePresentInputs* inputs){
 struct PlanFrameEnd{FramePlan& plan;~PlanFrameEnd(){plan.begin_frame();}} plan_frame_end{frame_plan_};
 const std::int64_t recorder_present_entry=inputs?inputs->recorder_present_entry:
  placed_hitch_profile_.active()?perf_now():0;
 OuterFrameTimingSnapshot outer_timing{};
 if(inputs){if(inputs->outer_timing_taken)outer_timing=inputs->outer_timing;}
 else if(placed_hitch_profile_.active())
  (void)bridge_take_outer_frame_timing(
   recorder_present_entry,outer_timing);
 if(!inputs){
  owned_resource_runtime_poll();
  camp_inventory_set_wide(configured_integer(debug_overlay_.config(),"camp_ui_layout",1)!=0);
 }
 prestart_terrain_tile_families();
 const std::int64_t present_tick=perf_now();
 if(last_present_tick_&&present_tick>last_present_tick_){
  const double sample=1000.0*static_cast<double>(present_tick-last_present_tick_)/static_cast<double>(perf_frequency());
  if(std::isfinite(sample)&&sample>0.0&&sample<10000.0){present_interval_ms_=sample;present_smoothed_ms_=present_smoothed_ms_>0.0?present_smoothed_ms_*0.9+sample*0.1:sample;}
 }
 last_present_tick_=present_tick;
 // The profiler's frame and passes, named as on D3D11 so captures line up.
 const bool overlay_profile=debug_overlay_.wants_profiling();profiler_.set_enabled(overlay_profile);
 bridge_set_profile_enabled(overlay_profile||placed_hitch_profile_.active());
 if(!profiler_.frame_active())profiler_.begin_frame(output_width_,output_height_);
 transition_profile_pass("present_head");
 // fg_frame_image: the executor takes it from the next frame; the HUD-less
 // copy follows the same option (released when it is off).
 // frame_generation 1 (DLSS-G: the frame image and the pacer) or 2 (FSR:
 // the frame image and the FidelityFX proxy swap chain). DLSS-G's
 // availability is read once from the host; a mode that failed stays off for
 // the session. The F10 note shows the selected mode's reason.
 if(!fg_caps_checked_){fg_caps_checked_=true;x64remote::DlssgCaps caps{};
#ifdef EID3D11_EMBEDDED
  const bool available=embedded_d3d12_.fg_caps(caps);
#else
  const bool available=x64remote::dlssg_caps(caps);
#endif
  if(!available){caps.reason[sizeof(caps.reason)-1]=0;std::snprintf(fg_dlssg_unavailable_,sizeof(fg_dlssg_unavailable_),"%s",caps.reason[0]?caps.reason:"not available");}}
 {const int selected=std::clamp(configured_integer(debug_overlay_.config(),"frame_generation",0),0,2);
  for(int mode=1;mode<=2;++mode)if(const char* failure=d3d12_->frame_generation_failure(mode);failure&&!fg_failed_[mode][0])frame_generation_off(mode,failure);
  const char* reason=selected==0?nullptr:fg_failed_[selected][0]?fg_failed_[selected]:selected==1&&fg_dlssg_unavailable_[0]?fg_dlssg_unavailable_:nullptr;
  if(selected!=fg_reason_mode_||reason){fg_reason_mode_=selected;set_frame_generation_unavailable_reason(reason);}
  frame_generation_mode_=reason?0:selected;}
 if(frame_generation_mode_!=1&&fg_previous_generated_){fg_previous_generated_=false;
#ifdef EID3D11_EMBEDDED
  embedded_d3d12_.fg_release();
#else
  x64remote::dlssg_release();
#endif
 }
 if(frame_generation_mode_!=2)fsr_prepared_=false;
 fg_frame_image_=configured_boolean(debug_overlay_.config(),"fg_frame_image",false)||frame_generation_mode_;
 d3d12_->set_frame_image(fg_frame_image_);
 d3d12_->set_frame_generation(frame_generation_mode_);
 if(!fg_frame_image_){hudless_d3d12_.reset();hudless_gamma_d3d12_.reset();}
 HRESULT hr=scene_composited_?flush_native():composite_world();
#ifdef EID3D11_EMBEDDED
 // Source Draw2D completes the popup's decoration after the world draw.
 // Consume it at the same boundary as the embedded D3D11 frame.
 CampDecoration camp_frame;
 if(camp_inventory_take_decoration(camp_frame))queue_camp_decoration(camp_frame);
#endif
 if(SUCCEEDED(hr)){transition_profile_pass("ui_composition");hr=render_pending_blits();}
 if(FAILED(hr)){bridge_report_event("present_step_failed","step=render_pending_blits (D3D12)");return hr;}
 if(FAILED(hr=draw_hud_asset_frames(inputs?&inputs->hud_frames:nullptr))){bridge_report_event("present_step_failed","step=hud_asset_frames (D3D12)");return hr;}
 transition_profile_pass("present_bookkeeping");
 if(debug_overlay_.initialized())debug_overlay_.maintain();
 if(debug_overlay_.first_run_pending()){AmbientWorldSource world;if(bridge_ambient_world_source(world)&&world.terrain)debug_overlay_.open_first_run_settings();}
 // Before the overlay, so a harness screenshot is the game's image (as on D3D11).
 // A harness capture: the profiler's pass timings (no D3D11 back buffer images).
 const HarnessRequest harness_request=step_test_harness(inputs);
 if(harness_request==HarnessRequest::capture){
  const RendererResourceSnapshot capture_resources=resource_snapshot();
  const auto cutout_scope=cutout_shadow_scope_snapshot();
  profiler_.request_capture_next(bridge_latest_profile(),&capture_resources,&cutout_scope);
 }
 // The captured frame's composed image (world and interface, before the
 // overlay), as the D3D11 capture's ui_composition image: read back from the
 // back buffer. The captured frame already stalls for its diagnostics; its
 // normal_frame.json is the frame before.
 if(profiler_.capture_active()&&!profiler_.capture_directory().empty()){
  std::string detail;
  if(!write_harness_screenshot(profiler_.capture_directory()+"\\ui_composition.bmp",detail))
   bridge_report_event("d3d12_capture",("F11 image not written: "+detail).c_str());
  // fg_frame_image: frame generation's HUD-less input of this frame.
  if(hudless_this_present_&&!write_hudless_image(profiler_.capture_directory()+"\\hudless.bmp",detail))
   bridge_report_event("d3d12_capture",("F11 HUD-less image not written: "+detail).c_str());
 }
 // fg_frame_image: the frame image (world and interface: frame generation's
 // real frame) copied to the swap-chain buffer; the overlay, cursor, load
 // reveal and gamma below draw there, as they do on the back buffer without it.
 // Presents frame generation must not interpolate (logged on change while
 // the inputs are on). Movies come in as loading pictures (present_image).
 // A full-screen interface (interface quad cover, below) is not one either.
 fg_not_game_frame_=(world_output_this_present_?0u:unsigned(kFgNotWorld))|(load_picture_this_frame_?unsigned(kFgNotLoadingPicture):0u)|
  (load_reveal_holding_?unsigned(kFgNotRevealHold):0u)|(hwnd_&&!window_in_front()?unsigned(kFgNotInFront):0u);
 // Full-screen interface over a live world (trade, outfit, camp): the
 // interface quads drawn so far this present (the cursor comes later).
 const float ui_coverage=interface_coverage();
 ui_quad_cells_.fill(0);
 if(ui_coverage>=kFullInterfaceCoverage)fg_not_game_frame_|=kFgNotFullInterface;
 if(world_output_this_present_&&int(ui_coverage*20.f+.5f)!=ui_coverage_logged_){
  ui_coverage_logged_=int(ui_coverage*20.f+.5f);
  char text[160];std::snprintf(text,sizeof(text),"present %llu: interface cover %.0f%% (full-screen interface from %.0f%%)",
   static_cast<unsigned long long>(presentation_frame_index_),double(ui_coverage)*100.0,double(kFullInterfaceCoverage)*100.0);
  bridge_report_event("fg_ui_coverage",text);
 }
 if(fg_frame_image_&&fg_not_game_frame_!=fg_not_game_frame_logged_){
  fg_not_game_frame_logged_=fg_not_game_frame_;
  char text[200];
  std::snprintf(text,sizeof(text),"present %llu: %s%s%s%s%s%s",static_cast<unsigned long long>(presentation_frame_index_),
   fg_not_game_frame_?"not a game frame:":"game frame",(fg_not_game_frame_&kFgNotWorld)?" no world image":"",
   (fg_not_game_frame_&kFgNotLoadingPicture)?" loading picture or movie":"",(fg_not_game_frame_&kFgNotRevealHold)?" load reveal hold":"",
   (fg_not_game_frame_&kFgNotInFront)?" window not in front":"",(fg_not_game_frame_&kFgNotFullInterface)?" full-screen interface":"");
  bridge_report_event("fg_game_frame",text);
 }
 static const bool developer_mode=renderer_developer_mode();
 const PresentationSettings presentation=presentation_settings(debug_overlay_.config(),developer_mode);
 static const bool harness_run=[](){char v[8]{};return GetEnvironmentVariableA("EI_RENDERER_HARNESS_DIR",v,sizeof(v))>0;}();
 int frame_limit=static_cast<int>(presentation.frame_limit);
 // Frame generation's auto-off sees the player's cap only: a background
 // present is no game frame (fg_not_game_frame_) and never a low-rate sample,
 // so the background cap must not pause generation past the return.
 update_frame_generation_auto_off(frame_limit);
 set_background_mode(configured_integer(debug_overlay_.config(),"background_mode",0));
 if(!harness_run&&hwnd_&&!window_in_front()){
  const int background=configured_integer(debug_overlay_.config(),"background_frame_limit",30);
  if(background>0&&(frame_limit<=0||frame_limit>background))frame_limit=background;
 }
 // frame_generation: the generated frame between the previous real frame and
 // this one, recorded from the frame image before it is composited.
 const bool generated=frame_generation_mode_==1&&evaluate_frame_generation_d3d12();
 fsr_prepared_=frame_generation_mode_==2&&prepare_fsr_frame_generation_d3d12();
 fg_upscaler_valid_=false;
 if(fg_frame_image_){transition_profile_pass("fg_frame_composite");d3d12_->composite_frame_image();transition_profile_pass("present_bookkeeping");}
 world_output_this_present_=hudless_this_present_=false;
 const char* reset_notice=nullptr;
 if(debug_overlay_.graphics_reset_notice_pending()){AmbientWorldSource world;
  reset_notice=debug_overlay_.graphics_reset_notice(bridge_ambient_world_source(world)&&world.terrain);}
 update_status_notice(reset_notice);
 const bool overlay_visible=debug_overlay_.initialized()&&debug_overlay_.visible();
 const bool telemetry_wanted=debug_overlay_.initialized()&&debug_overlay_.mode()!=DebugOverlayMode::hidden;
 if(telemetry_wanted!=system_telemetry_active_){system_telemetry_.set_active(telemetry_wanted);system_telemetry_active_=telemetry_wanted;}
 system_telemetry_.try_get_snapshot(system_telemetry_snapshot_);
 const auto debug_view_availability=debug_resource_view_availability();
 const auto requested_debug_view=debug_overlay_.config().debug_resource_view();
 if(debug_overlay_.mode()==DebugOverlayMode::full&&debug_view_availability.available(requested_debug_view)){
  transition_profile_pass("debug_resource_view");
  const HRESULT debug_hr=render_debug_resource_view(requested_debug_view,debug_overlay_.mode());
  if(FAILED(debug_hr)||debug_hr==kD3D12Dropped)d3d12_->omit("debug_resource_view (draw failed)",false);
 }
 if(debug_overlay_.initialized()&&debug_overlay_pass_needed(overlay_visible||combat_ui_has_output(),
    debug_overlay_.has_status_notice()||debug_overlay_.has_harness_banner()||widescreen_hud_hovered(HudPanel::game_speed),!critters_summary_.labels.empty())){
  transition_profile_pass("debug_overlay");
  RendererResourceSnapshot presented{};presented.present_interval_ms=present_interval_ms_;presented.present_smoothed_ms=present_smoothed_ms_;
  presented.fg_presented_fps=frame_generation_mode_?fg_presented_fps_:0.0;presented.fg_real_fps=frame_generation_mode_?fg_real_fps_:0.0;
  presented.fg_mode=frame_generation_mode_;
  presented.fg_auto_off_fps=frame_generation_mode_&&fg_low_rate_?std::max(1,configured_integer(debug_overlay_.config(),"frame_generation_min_fps",30)):0;
  debug_overlay_.render(profiler_.latest_snapshot(),bridge_latest_profile(),presented,
                        debug_view_availability,&profiler_,debug_shadow_info());
 }
 PendingBlit generated_cursor{};bool generated_cursor_wanted=false;
 if(cursor_pending_){
  cursor_pending_=false;
  if(!debug_overlay_.captures_input()){
   // Frame generation: the same cursor (render rate) on the generated image,
   // the newest position on both presents (decision 2026-11-01). A midpoint
   // cursor would add latency in a click-driven game; a present-time OS
   // sample needs cursor composition at final presentation and breaks while the
   // camera handlers warp the cursor (RMB rotation): the plan if players
   // report cursor lag with frame generation. FSR takes generated-frame UI
   // from the real back buffer, so its cursor is the real frame's either way.
   if(generated){try{generated_cursor=pending_cursor_;generated_cursor_wanted=true;}catch(const std::bad_alloc&){}}
   try{pending_blits_.push_back(std::move(pending_cursor_));}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
   if(FAILED(hr=render_pending_blits())){bridge_report_event("present_step_failed","step=cursor_composition (D3D12)");return hr;}
  }
  pending_cursor_={};
 }
 if(!harness_overlay_shot_.empty()){
  std::string detail;const bool ok=write_harness_screenshot(harness_overlay_shot_,detail);
  if(inputs){harness_complete_entry(inputs->harness_entry_id,ok,detail+" (after the overlay: critter labels on)");harness_completed_entry_=inputs->harness_entry_id;}
  else harness_complete(ok,detail+" (after the overlay: critter labels on)");
  harness_stage_=0;harness_wait_=0;harness_overlay_shot_.clear();
 }
 // Capture and outer-frame recording use the same lifecycle on both backends.
 const bool f11_capture=debug_overlay_.consume_capture_request()||std::exchange(core_capture_request_,false);
 bool recorder_armed_this_present=false;
 if(f11_capture||harness_request==HarnessRequest::profile){
   const bool recorder_requested=f11_capture?placed_hitch_profile_enabled():
    harness_request==HarnessRequest::profile;
  const bool timing_supported=bridge_outer_frame_timing_supported();
  const bool recorder_available=recorder_requested&&timing_supported;
  char request_detail[192]{};
  std::snprintf(request_detail,sizeof(request_detail),
   "%s recorder_requested=%s timing_supported=%s active=%s",
   f11_capture?"focused_rising_edge_consumed":"harness_request",
   recorder_requested?"true":"false",timing_supported?"true":"false",
   placed_hitch_profile_.active()?"true":"false");
  report_placed_hitch_lifecycle(
   "outer_frame_gap_profile_f11_request",request_detail);
  const PlacedHitchCaptureAction action=placed_hitch_capture_action(
   recorder_available,placed_hitch_profile_);
  if(action==PlacedHitchCaptureAction::arm_recorder){
   const auto* bridge=bridge_latest_profile();
   RetainedSceneIdentitySnapshot arm_scene_identity{};
   PlacedHitchArmInputs arm_inputs{};
   bool arm_outcome_reported=false;
   try{
    arm_scene_identity=retained_static_scene_identity_snapshot();
    arm_inputs=placed_hitch_arm_inputs(bridge,arm_scene_identity);
    if(!arm_inputs.current_world_epoch_valid){
     arm_outcome_reported=true;
     report_placed_hitch_lifecycle("outer_frame_gap_profile_arm_rejected",
      "reason=current_world_epoch_unknown");
    }else{
     recorder_armed_this_present=arm_placed_hitch_profile(
      placed_hitch_profile_,arm_inputs);
    }
   }catch(const std::bad_alloc&){
    arm_outcome_reported=true;
    report_placed_hitch_lifecycle("outer_frame_gap_profile_arm_failed",
     "reason=allocation_failed");
   }catch(...){
    arm_outcome_reported=true;
    report_placed_hitch_lifecycle("outer_frame_gap_profile_arm_failed",
     "reason=unexpected_exception");
   }
   if(recorder_armed_this_present){
    placed_hitch_scene_json_.clear();
    placed_hitch_previous_trim_ticks_=0;
    placed_hitch_previous_trim_valid_=false;
    try{
     SYSTEMTIME utc{};GetSystemTime(&utc);char timestamp[32]{};
     std::snprintf(timestamp,sizeof(timestamp),
      "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
      utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,
      utc.wSecond,utc.wMilliseconds);
     placed_hitch_scene_json_=format_scene_identity_json(
      arm_scene_identity,
      bridge_scene_world_to_clip_snapshot(),timestamp,true,
      presentation_frame_index_);
    }catch(...){placed_hitch_scene_json_.clear();}
    bridge_set_profile_enabled(true);
    frame_statistics_.reset();
    char arm_detail[256]{};
    std::snprintf(arm_detail,sizeof(arm_detail),
     "sequence=%llu bridge_frame=%llu world_epoch=%llu scene_identity=%s",
     static_cast<unsigned long long>(placed_hitch_profile_.sequence_id()),
     static_cast<unsigned long long>(arm_inputs.previous_bridge_frame),
     static_cast<unsigned long long>(arm_inputs.current_world_epoch),
     placed_hitch_scene_json_.empty()?"unavailable":"captured");
    report_placed_hitch_lifecycle("outer_frame_gap_profile_armed",arm_detail);
   }else if(!arm_outcome_reported){
    report_placed_hitch_lifecycle("outer_frame_gap_profile_arm_rejected",
     "reason=state_rejected");
   }
  }else if(action==PlacedHitchCaptureAction::normal_capture){
   report_placed_hitch_lifecycle("outer_frame_gap_profile_not_armed",
    recorder_requested?"reason=timing_hooks_unsupported":
                       "reason=recorder_disabled_normal_capture");

   const RendererResourceSnapshot capture_resources=resource_snapshot();
   const auto cutout_scope=cutout_shadow_scope_snapshot();
   profiler_.request_capture_next(bridge_latest_profile(),&capture_resources,&cutout_scope);
   frame_statistics_.reset();
   }else{
    report_placed_hitch_lifecycle("outer_frame_gap_profile_arm_rejected",
     "reason=already_active");
   }
  }
 if(harness_request==HarnessRequest::profile)harness_profile_armed_=recorder_armed_this_present;

 finish_native_lod_frame();
 // Loading screens: the game's own pictures draw (present_image); the load
 // reveal keeps the last one and holds it over world frames until the world
 // and its neighbours are prepared (apply_load_reveal, as on D3D11).
 apply_load_reveal();
 // The game's gamma ramp over the finished frame, cursor included (after the
 // load reveal, as apply_gamma_ramp on D3D11).
 if(FAILED(apply_gamma_ramp_d3d12()))d3d12_->omit("gamma_ramp (D3D12 pass failed)",false);
#ifdef EID3D11_EMBEDDED
 if(bridge_live_wants_capture())bridge_live_capture_d3d12(*d3d12_,native_frame_index_);
#endif
 // Frame generation: the generated image gets the same layers as the real
 // one (overlay draw lists replayed, the cursor at the real frame's position,
 // gamma; the load reveal never holds on a generated frame). The pacer
 // presents it half a real-frame interval before the real image.
 if(generated){
  transition_profile_pass("fg_generated_layers");
  d3d12_->select_generated_screen();
  debug_overlay_.render_again();
  if(generated_cursor_wanted){
   try{pending_blits_.push_back(std::move(generated_cursor));}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
   if(FAILED(hr=render_pending_blits())){bridge_report_event("present_step_failed","step=generated cursor (D3D12)");return hr;}
  }
  if(FAILED(apply_gamma_ramp_d3d12()))d3d12_->omit("gamma_ramp on the generated frame (D3D12 pass failed)",false);
 }else debug_overlay_.forget_rendered_frame();
 // FSR: the HUD-less image FidelityFX compares with the back buffer (with
 // the same gamma ramp); null: no generation from this frame.
 if(frame_generation_mode_==2)d3d12_->set_fsr_hudless(fsr_prepared_?fsr_hudless_d3d12():nullptr);
 fsr_prepared_=false;
 update_frame_generation_rates();
 profiler_.end_frame(presentation.sync_interval,0);
 // The final presentation owner applies the output cap (both images with FG).
 hr=d3d12_->present(presentation.sync_interval,frame_limit);
 const std::int64_t recorder_present_return=placed_hitch_profile_.active()?perf_now():0;
#ifdef EID3D11_EMBEDDED
 if(SUCCEEDED(hr))embedded_d3d12_presented_=true;
#endif
 profiler_.present_return(output_width_,output_height_,presentation.sync_interval,0,hr);
 if(placed_hitch_profile_.active()){
  if(FAILED(hr)){
   finish_placed_hitch_profile(PlacedHitchFinishReason::present_failed);
  }else if(recorder_armed_this_present){
   if(placed_hitch_profile_.anchor_present(
       recorder_present_return,perf_frequency())){
    bridge_set_outer_frame_timing_active(false);
    export_placed_hitch_profile();
    bridge_set_profile_enabled(debug_overlay_.wants_profiling());
   }else{
    bridge_set_outer_frame_timing_active(true);
   }
  }else{
   // A harness run is unattended by design; focus loss is not a disturbance there.
   record_placed_hitch_frame(recorder_present_entry,
    recorder_present_return,window_in_front()||harness_enabled(),hr,outer_timing);
  }
 }
 HRESULT trim_hr=S_OK;
 const std::int64_t trim_begin=placed_hitch_profile_.active()&&SUCCEEDED(hr)?perf_now():0;
 if(SUCCEEDED(hr)){
  transition_profile_pass("frame_tail");
  d3d12_->clear_slots();
  const frame_acct::Scope scope(frame_acct::pf_trim);
  trim_hr=trim_native_mesh_cache();
  if(FAILED(trim_hr))bridge_report_event("present_step_failed","step=trim_native_mesh_cache (D3D12)");
 }
 const std::int64_t trim_end=trim_begin?perf_now():0;
 if(placed_hitch_profile_.active()){
  if(FAILED(trim_hr)){
   finish_placed_hitch_profile(PlacedHitchFinishReason::trim_failed);
  }else if(trim_begin>0&&trim_end>=trim_begin){
   placed_hitch_previous_trim_ticks_=trim_end-trim_begin;
   placed_hitch_previous_trim_valid_=true;
  }else{
   finish_placed_hitch_profile(PlacedHitchFinishReason::invalid_clock);
  }
 }
 // Exclude the host's wait for the next frame's records from renderer work.
 end_profile_pass();
 if(SUCCEEDED(hr)&&window_in_front())frame_statistics_.add_present(perf_now(),perf_frequency());else frame_statistics_.reset();
 // The next present's native counters start from zero (present_frame's reset).
 native_frame_stats_={};++presentation_frame_index_;
 // Render scale and upscaler option changes take effect between frames (apply_render_scale).
 apply_upscaler_mode_d3d12();
 finish_harness_path_row(inputs?inputs->game_frame:fa_game_frame_,nullptr);
 pending_screen_rectangles_.clear();
 return FAILED(trim_hr)?trim_hr:hr;
}

#endif // !EID3D11_SHADER_EMBED
HRESULT Renderer::present_frame(const ScenePresentInputs* inputs){
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
 if(d3d12_){
  struct FinishPhase {
   EmbeddedD3D12& owner;
   ~FinishPhase(){owner.finish_render();}
  } finish_phase{embedded_d3d12_};
  if(embedded_d3d12_failed_)return E_FAIL;
  HRESULT hr;
  try{hr=present_frame_d3d12(inputs);}
  catch(const std::bad_alloc&){hr=E_OUTOFMEMORY;}
  catch(...){hr=E_FAIL;}
  const HRESULT removed=embedded_d3d12_.device_removed_reason();
  const char* reason=embedded_d3d12_.failure();
  if(FAILED(removed))return fail_embedded_d3d12("device removed during frame",removed,reason);
  if(FAILED(hr))finish_placed_hitch_profile(PlacedHitchFinishReason::native_flush_failed);
  if(FAILED(hr)||(reason&&*reason))return fail_embedded_d3d12("frame composition/presentation",hr,reason);
  return hr;
 }
#elif !defined(EID3D11_SHADER_EMBED)
 if(d3d12_)return present_frame_d3d12(inputs);
#endif
 // The next frame's plan starts when this call returns, on every path.
 struct PlanFrameEnd{FramePlan& plan;~PlanFrameEnd(){plan.begin_frame();}} plan_frame_end{frame_plan_};
 // The present path's own CPU blocks as passes (present_head, then
 // present_bookkeeping after the UI, frame_tail after Present).
 transition_profile_pass("present_head");
 const std::int64_t recorder_present_entry=inputs?inputs->recorder_present_entry:
  placed_hitch_profile_.active()?perf_now():0;
 OuterFrameTimingSnapshot outer_timing{};
 if(inputs){if(inputs->outer_timing_taken)outer_timing=inputs->outer_timing;}
 else if(placed_hitch_profile_.active())
  (void)bridge_take_outer_frame_timing(
   recorder_present_entry,outer_timing);
 if(!inputs)owned_resource_runtime_poll();
 prestart_terrain_tile_families();
 if(!swap_){
  finish_placed_hitch_profile(PlacedHitchFinishReason::no_swap_chain);
  return S_OK;
 }
 const std::int64_t present_tick=perf_now();if(last_present_tick_&&present_tick>last_present_tick_){const double sample=1000.0*static_cast<double>(present_tick-last_present_tick_)/static_cast<double>(perf_frequency());if(std::isfinite(sample)&&sample>0.0&&sample<10000.0){present_interval_ms_=sample;present_smoothed_ms_=present_smoothed_ms_>0.0?present_smoothed_ms_*0.9+sample*0.1:sample;}}last_present_tick_=present_tick;
 // A failed step ends the frame early: name it (rate-limited per step:
 // the 1st, 2nd, 4th, 8th... failure) so no frame fails silently, and
 // still step the harness, so a failing frame can never silence it.
 const auto failed_frame=[this,inputs](unsigned step,const char* name,HRESULT hr){
  const std::uint64_t count=++present_failures_[step];
  if((count&(count-1))==0){
   char detail[160];std::snprintf(detail,sizeof(detail),"step=%s hr=%08lx count=%llu native_line=%d frame=%llu",
    name,static_cast<unsigned long>(hr),static_cast<unsigned long long>(count),native_flush_failure_line_,
    static_cast<unsigned long long>(native_frame_index_));
   bridge_report_event("present_step_failed",detail);
  }
  (void)step_test_harness(inputs);
  return hr;
 };
 // The renderer's own frame work up to the frame limiter (stall_watchdog.h);
 // Present has its own bracket below.
 std::optional<stall_watch::Bracket> frame_bracket;frame_bracket.emplace("frame");
 HRESULT nativeHr;{const frame_acct::Scope scope(frame_acct::pf_flush_native);nativeHr=flush_native();}if(FAILED(nativeHr)){
  finish_placed_hitch_profile(PlacedHitchFinishReason::native_flush_failed);
  return failed_frame(0,"flush_native",nativeHr);
 }
 const bool overlay_profile=debug_overlay_.wants_profiling();profiler_.set_enabled(overlay_profile);bridge_set_profile_enabled(overlay_profile||placed_hitch_profile_.active());
 if(profiler_.pass_active())profiler_.end_pass();HRESULT composite_hr;{const frame_acct::Scope scope(frame_acct::pf_composite);composite_hr=composite_world();}if(FAILED(composite_hr))return failed_frame(1,"composite_world",composite_hr);transition_profile_pass("ui_composition");
 std::optional<frame_acct::Scope> ui_scope;ui_scope.emplace(frame_acct::pf_ui);
#ifdef EID3D11_EMBEDDED
 // Host Draw2D appends the card text after the ordinary surface blits.
 // Publish the completed popup only after both interface passes have finished.
 CampDecoration camp_frame;
 if(camp_inventory_take_decoration(camp_frame))queue_camp_decoration(camp_frame);
#endif
 HRESULT pendingHr=render_pending_blits();if(FAILED(pendingHr)){
  finish_placed_hitch_profile(PlacedHitchFinishReason::ui_flush_failed);
  return failed_frame(2,"render_pending_blits",pendingHr);
 }
 const HRESULT frames_hr=draw_hud_asset_frames(inputs?&inputs->hud_frames:nullptr);if(FAILED(frames_hr))return failed_frame(2,"hud_asset_frames",frames_hr);
 ui_scope.reset();
 transition_profile_pass("present_bookkeeping");
 if(debug_overlay_.initialized())debug_overlay_.maintain();
 // First run (no [setup] first_run_done, not developer mode): the player
 // settings open once a world is on screen, which first is the main menu.
 if(debug_overlay_.first_run_pending()){AmbientWorldSource world;if(bridge_ambient_world_source(world)&&world.terrain)debug_overlay_.open_first_run_settings();}
 // Before the debug view and overlay, so a harness screenshot is the game's image
 // (with critter labels on it waits for the overlay pass below).
 const HarnessRequest harness_request=[&]{const frame_acct::Scope scope(frame_acct::pf_harness);return step_test_harness(inputs);}();
 // Over-budget safety net: the one-time reset notice once a world is on screen.
 const char* reset_notice=nullptr;
 if(debug_overlay_.graphics_reset_notice_pending()){AmbientWorldSource world;
  reset_notice=debug_overlay_.graphics_reset_notice(bridge_ambient_world_source(world)&&world.terrain);}
 // Without the verified bridge the frame is only the game's own draw stream
 // through this D3D11 backend (unknown game.exe or slot mismatch): say so.
 // game.exe first, then an unrecognised addon with the features it turned off
 // (it covers the camp grid), then the camp's own statuses. A hook entry or
 // the DirectDraw import held by another module (issue #70) comes right
 // after game.exe: those visuals are not drawn by us.
 update_status_notice(reset_notice);
 const bool overlay_visible=debug_overlay_.initialized()&&debug_overlay_.visible();
 // Telemetry feeds the developer windows only.
 const bool telemetry_wanted=debug_overlay_.initialized()&&debug_overlay_.mode()!=DebugOverlayMode::hidden;
 if(telemetry_wanted!=system_telemetry_active_){system_telemetry_.set_active(telemetry_wanted);system_telemetry_active_=telemetry_wanted;}
 system_telemetry_.try_get_snapshot(system_telemetry_snapshot_);
 const bool focused=window_in_front();
 bool measure_completed_present=focused&&!profiler_.capture_active();
 if(!measure_completed_present)frame_statistics_.reset();
 const DebugResourceView requested_debug_view=debug_overlay_.config().debug_resource_view();
 const DebugOverlayMode debug_overlay_mode=debug_overlay_.mode();
 DebugResourceViewAvailability debug_view_availability{};
 if(debug_overlay_mode==DebugOverlayMode::full)
  debug_view_availability=debug_resource_view_availability();
 if(debug_overlay_mode==DebugOverlayMode::full&&
    debug_view_availability.available(requested_debug_view)&&
    requested_debug_view!=DebugResourceView::disabled&&
    requested_debug_view!=DebugResourceView::shadow_cascade_coverage)
  transition_profile_pass("debug_resource_view");
 render_debug_resource_view(requested_debug_view,debug_overlay_mode);
 if(debug_overlay_.initialized()&&debug_overlay_pass_needed(overlay_visible || combat_ui_has_output(),debug_overlay_.has_status_notice() || debug_overlay_.has_harness_banner() || widescreen_hud_hovered(HudPanel::game_speed),!critters_summary_.labels.empty())){
  const frame_acct::Scope overlay_scope(frame_acct::pf_overlay);
  const auto shadows=debug_overlay_mode==DebugOverlayMode::full?debug_shadow_info():DebugShadowInfo{};
  transition_profile_pass("debug_overlay");
  // The overlay (Dear ImGui) draws into the bound target: bind it here.
  bind_plan_targets(plan_pass("debug_overlay",rtv_.Get(),nullptr));
  // HUD borders use the owned UI pass without collecting developer diagnostics.
  const auto overlay_resources=overlay_visible||debug_overlay_.wants_profiling()||!critters_summary_.labels.empty()?resource_snapshot():RendererResourceSnapshot{};
  debug_overlay_.render(profiler_.latest_snapshot(),bridge_latest_profile(),overlay_resources,
                        debug_view_availability,&profiler_,shadows);
 }
 // The actual game cursor follows owned hover cards as well as ordinary HUD.
 if(cursor_pending_){
  cursor_pending_=false;
  if(!debug_overlay_.captures_input()){
   try{pending_blits_.push_back(std::move(pending_cursor_));}catch(const std::bad_alloc&){return failed_frame(2,"cursor_queue",E_OUTOFMEMORY);}
   const HRESULT cursor_hr=render_pending_blits();if(FAILED(cursor_hr))return failed_frame(2,"cursor_composition",cursor_hr);
  }
  pending_cursor_={};
 }
 if(!inputs)camp_inventory_set_wide(configured_integer(debug_overlay_.config(),"camp_ui_layout",1)!=0);
 if(!harness_overlay_shot_.empty()){
  std::string detail;const bool ok=write_harness_screenshot(harness_overlay_shot_,detail);
#ifndef EID3D11_EMBEDDED
  if(inputs){harness_complete_entry(inputs->harness_entry_id,ok,detail+" (after the overlay: critter labels on)");harness_completed_entry_=inputs->harness_entry_id;}

  else
#endif
   harness_complete(ok,detail+" (after the overlay: critter labels on)");
  harness_stage_=0;harness_wait_=0;harness_overlay_shot_.clear();
 }
  ComPtr<ID3D11Texture2D> profiledBack;if(profiler_.capture_active())swap_->GetBuffer(0,IID_PPV_ARGS(&profiledBack));profiler_.end_pass(profiledBack.Get());
  // The call always consumes the camp scroll cost; the snapshot is read only while attribution is on.
  profiler_.record_attribution_workload(bridge_latest_profile(),profiler_.attribution_enabled()?resource_snapshot():RendererResourceSnapshot{});
  // Readback and disk output happen only after the second world sample.
  const auto temporal_outcome=profiler_.shadow_temporal_present(presentation_frame_index_);
  if(temporal_outcome==ShadowTemporalPresentOutcome::unavailable)
   report_placed_hitch_lifecycle("shadow_temporal_capture_unavailable",
    "no completed-world depth clear in the requested adjacent frame");
  else if(temporal_outcome==ShadowTemporalPresentOutcome::export_failed)
   report_placed_hitch_lifecycle("shadow_temporal_capture_failed",
    "bounded staged frame export failed");
  else if(temporal_outcome==ShadowTemporalPresentOutcome::complete)
   report_placed_hitch_lifecycle("shadow_temporal_capture_complete",
    "two staged world frames and shadow arrays exported");
  if(temporal_outcome!=ShadowTemporalPresentOutcome::none){
   frame_statistics_.reset();measure_completed_present=false;
  }
  bool recorder_armed_this_present=false;
  if(debug_overlay_.consume_cull_freeze_toggle()){
   native_cull_frozen_=!native_cull_frozen_&&native_world_clip_valid_;
   if(native_cull_frozen_)native_cull_clip_=native_world_clip_;
   native_world_clip_dirty_=true;
  }
  // Core mode: the game process detects F11 (the overlay is off there).
  const bool f11_capture=debug_overlay_.consume_capture_request()||std::exchange(core_capture_request_,false);
  if(f11_capture||harness_request!=HarnessRequest::none){
   // The opt-in shadow export also answers a harness capture, so a route
   // can collect it without a person pressing F11.
   if((f11_capture||harness_request==HarnessRequest::capture)&&shadow_temporal_diagnostic_enabled()){
    ComPtr<ID3D11Texture2D> color;
    (void)swap_->GetBuffer(0,IID_PPV_ARGS(&color));
    if(profiler_.request_shadow_temporal_pair(color.Get(),depth_.Get(),
        native_shadow_depth_.Get(),presentation_frame_index_+1,
        bridge_world_draw_completion_counter()))
     report_placed_hitch_lifecycle("shadow_temporal_capture_armed",
      "next two adjacent world frames; staging uses current world and shadow dimensions");
    else report_placed_hitch_lifecycle("shadow_temporal_capture_rejected",
      "another pair active or required world/shadow resources unavailable");
    frame_statistics_.reset();measure_completed_present=false;
   }else{
   // A harness request names its action; F11 arms the recorder when enabled.
   const bool recorder_requested=f11_capture?placed_hitch_profile_enabled():
    harness_request==HarnessRequest::profile;
  const bool timing_supported=bridge_outer_frame_timing_supported();
  const bool recorder_available=recorder_requested&&timing_supported;
  char request_detail[192]{};
  std::snprintf(request_detail,sizeof(request_detail),
   "%s recorder_requested=%s timing_supported=%s active=%s",
   f11_capture?"focused_rising_edge_consumed":"harness_request",
   recorder_requested?"true":"false",timing_supported?"true":"false",
   placed_hitch_profile_.active()?"true":"false");
  report_placed_hitch_lifecycle(
   "outer_frame_gap_profile_f11_request",request_detail);
  const PlacedHitchCaptureAction action=placed_hitch_capture_action(
   recorder_available,placed_hitch_profile_);
  if(action==PlacedHitchCaptureAction::arm_recorder){
   const auto* bridge=bridge_latest_profile();
   RetainedSceneIdentitySnapshot arm_scene_identity{};
   PlacedHitchArmInputs arm_inputs{};
   bool arm_outcome_reported=false;
   try{
    arm_scene_identity=retained_static_scene_identity_snapshot();
    arm_inputs=placed_hitch_arm_inputs(bridge,arm_scene_identity);
    if(!arm_inputs.current_world_epoch_valid){
     arm_outcome_reported=true;
     report_placed_hitch_lifecycle("outer_frame_gap_profile_arm_rejected",
      "reason=current_world_epoch_unknown");
    }else{
     recorder_armed_this_present=arm_placed_hitch_profile(
      placed_hitch_profile_,arm_inputs);
    }
   }catch(const std::bad_alloc&){
    arm_outcome_reported=true;
    report_placed_hitch_lifecycle("outer_frame_gap_profile_arm_failed",
     "reason=allocation_failed");
   }catch(...){
    arm_outcome_reported=true;
    report_placed_hitch_lifecycle("outer_frame_gap_profile_arm_failed",
     "reason=unexpected_exception");
   }
   if(recorder_armed_this_present){
    placed_hitch_scene_json_.clear();
    placed_hitch_previous_trim_ticks_=0;
    placed_hitch_previous_trim_valid_=false;
    try{
     SYSTEMTIME utc{};GetSystemTime(&utc);char timestamp[32]{};
     std::snprintf(timestamp,sizeof(timestamp),
      "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
      utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,
      utc.wSecond,utc.wMilliseconds);
     placed_hitch_scene_json_=format_scene_identity_json(
      arm_scene_identity,
      bridge_scene_world_to_clip_snapshot(),timestamp,true,
      presentation_frame_index_);
    }catch(...){placed_hitch_scene_json_.clear();}
    bridge_set_profile_enabled(true);
    frame_statistics_.reset();measure_completed_present=false;
    char arm_detail[256]{};
    std::snprintf(arm_detail,sizeof(arm_detail),
     "sequence=%llu bridge_frame=%llu world_epoch=%llu scene_identity=%s",
     static_cast<unsigned long long>(placed_hitch_profile_.sequence_id()),
     static_cast<unsigned long long>(arm_inputs.previous_bridge_frame),
     static_cast<unsigned long long>(arm_inputs.current_world_epoch),
     placed_hitch_scene_json_.empty()?"unavailable":"captured");
    report_placed_hitch_lifecycle("outer_frame_gap_profile_armed",arm_detail);
   }else if(!arm_outcome_reported){
    report_placed_hitch_lifecycle("outer_frame_gap_profile_arm_rejected",
     "reason=state_rejected");
   }
  }else if(action==PlacedHitchCaptureAction::normal_capture){
   report_placed_hitch_lifecycle("outer_frame_gap_profile_not_armed",
    recorder_requested?"reason=timing_hooks_unsupported":
                       "reason=recorder_disabled_normal_capture");
   bridge_request_hovered_shadow_capture();
   const RendererResourceSnapshot capture_resources=resource_snapshot();
   const auto cutout_scope=cutout_shadow_scope_snapshot();
   profiler_.request_capture_next(bridge_latest_profile(),&capture_resources,&cutout_scope);
   frame_statistics_.reset();measure_completed_present=false;
   }else{
    report_placed_hitch_lifecycle("outer_frame_gap_profile_arm_rejected",
     "reason=already_active");
   }
   }
  }
 if(harness_request==HarnessRequest::profile)harness_profile_armed_=recorder_armed_this_present;
 // Player vsync and frame limit; developer mode (the test launcher) forces
 // both off so captures stay uncapped. Without vsync, flip-model tearing stays allowed.
 static const bool developer_mode=renderer_developer_mode();
 const PresentationSettings presentation=presentation_settings(debug_overlay_.config(),developer_mode);
 const UINT interval=presentation.sync_interval;
 const UINT flags=interval==0&&tearing_supported_?DXGI_PRESENT_ALLOW_TEARING:0u;profiler_.end_frame(interval,flags);
 // In the background the game should idle quietly: cap to the player's
 // background limit (F10, default 30; 0 = off). Harness runs stay uncapped,
 // they drive a background window on purpose.
 static const bool harness_run=[](){char v[8]{};return GetEnvironmentVariableA("EI_RENDERER_HARNESS_DIR",v,sizeof(v))>0;}();
 int frame_limit=static_cast<int>(presentation.frame_limit);
 set_background_mode(configured_integer(debug_overlay_.config(),"background_mode",0));
 if(!harness_run&&hwnd_&&!window_in_front()){
  const int background=configured_integer(debug_overlay_.config(),"background_frame_limit",30);
  if(background>0&&(frame_limit<=0||frame_limit>background))frame_limit=background;
 }
 apply_load_reveal();
 (void)apply_gamma_ramp();
 frame_bracket.reset();
#ifdef EID3D11_EMBEDDED
 FrameLimiter::WaitTiming wait_timing;
 {const frame_acct::Scope scope(frame_acct::pf_limiter);
 const CorePerf::AnyThreadScope coreWait(CorePerf::LimiterWait);
 frame_limiter_.wait(frame_limit, &wait_timing);}
 // At most one pacing sample per second, on policy change or a slow wait.
 // Keep it outside the wait scope so journal formatting is not wait time.
 static std::int64_t pacing_report = 0;
 static int pacing_limit = -1;
 static UINT pacing_interval = UINT_MAX;
 static bool pacing_focused = false;
 const auto pacing_now = perf_now();
 const auto pacing_frequency = perf_frequency();
 const bool pacing_changed = pacing_limit != frame_limit ||
  pacing_interval != interval || pacing_focused != focused;
 if(pacing_frequency > 0 && (pacing_changed || wait_timing.actual > pacing_frequency * 33 / 1000) &&
    (!pacing_report || pacing_now - pacing_report >= pacing_frequency)){
  const double ms = 1000.0 / pacing_frequency;
  GameJournal::Event("RENDER", "frame_pacing",
   "frame=%llu limit=%d foreground=%u vsync_interval=%u requested_ms=%.3f actual_ms=%.3f overrun_ms=%.3f",
   CorePerf::Get().frameNumber, frame_limit, unsigned(focused), interval,
   wait_timing.requested * ms, wait_timing.actual * ms,
   (wait_timing.actual - wait_timing.requested) * ms);
  pacing_report = pacing_now; pacing_limit = frame_limit;
  pacing_interval = interval; pacing_focused = focused;
 }
#else
 {const frame_acct::Scope scope(frame_acct::pf_limiter);
 frame_limiter_.wait(frame_limit);}
#endif
 HRESULT hr;
#ifdef EID3D11_EMBEDDED
 // The host observer copies only the completed composed frame, before Present.
 if(bridge_live_wants_capture())bridge_live_capture(context_.Get(),swap_.Get(),native_frame_index_);
#endif
 (void)plan_pass("present",nullptr,nullptr,plan_read(PlanTarget::backbuffer));
 finish_native_lod_frame();
 {const stall_watch::Bracket present_bracket("present");const frame_acct::Scope scope(frame_acct::pf_present_call);hr=swap_->Present(interval,flags);}
 const std::int64_t recorder_present_return=
  placed_hitch_profile_.active()?perf_now():0;
 profiler_.present_return(output_width_,output_height_,interval,flags,hr);
#ifdef EID3D11_EMBEDDED
 // Sample the existing asynchronous profiler. No new query or GPU wait.
 static ULONGLONG perf06last=0;
 const auto perf06now=GetTickCount64();
 try { if(perf06now-perf06last>=2000){
  perf06last=perf06now;
  if(const auto* sample=profiler_.latest_snapshot()){
   char detail[384]{};
   std::snprintf(detail,sizeof(detail),"sample_frame=%llu cpu_ms=%.3f gpu_ms=%.3f gpu_valid=%u passes=%u sampled=1",
    static_cast<unsigned long long>(sample->frame),sample->frame_cpu_ms,sample->frame_gpu_ms,unsigned(sample->gpu_valid),sample->pass_count);
   embedded_perf06_report(detail);
   // Aggregate split passes with the same name before selecting the top 16.
   std::map<std::string,std::array<double,4>> sums;
   for(const auto& pass:sample->passes){auto& sum=sums[pass.name.data()];
    sum[0]+=pass.cpu_ms;if(pass.gpu_ms>=0)sum[1]+=pass.gpu_ms;
    sum[2]+=double(pass.counters.draws);sum[3]+=double(pass.counters.batches);}
   for(unsigned rank=0;rank<16 && !sums.empty();++rank){
    auto best=sums.begin();
    for(auto it=sums.begin();it!=sums.end();++it)
     if((std::max)(it->second[0],it->second[1])>(std::max)(best->second[0],best->second[1]))best=it;
    std::snprintf(detail,sizeof(detail),"sample_frame=%llu pass=%.96s cpu_ms=%.3f gpu_ms=%.3f logical_draws=%.0f batches=%.0f sampled=1",
     static_cast<unsigned long long>(sample->frame),best->first.c_str(),best->second[0],best->second[1],best->second[2],best->second[3]);
    embedded_perf06_report(detail);sums.erase(best);
   }
  }
 } } catch (...) {}
#endif
 if(SUCCEEDED(hr)&&measure_completed_present)frame_statistics_.add_present(perf_now(),perf_frequency());else frame_statistics_.reset();
 if(placed_hitch_profile_.active()){
  if(FAILED(hr)){
   finish_placed_hitch_profile(PlacedHitchFinishReason::present_failed);
  }else if(recorder_armed_this_present){
   if(placed_hitch_profile_.anchor_present(
       recorder_present_return,perf_frequency())){
    bridge_set_outer_frame_timing_active(false);
    export_placed_hitch_profile();
    bridge_set_profile_enabled(debug_overlay_.wants_profiling());
   }else{
    bridge_set_outer_frame_timing_active(true);
   }
  }else{
   // A harness run is unattended by design; focus loss is not a disturbance there.
   record_placed_hitch_frame(recorder_present_entry,
    recorder_present_return,focused||harness_enabled(),hr,outer_timing);
  }
 }
 if(SUCCEEDED(hr))transition_profile_pass("frame_tail"); // the next frame's first pass: mesh cache trim, shader preparation
 HRESULT trimHr=S_OK;
 const std::int64_t trim_begin=
  placed_hitch_profile_.active()&&SUCCEEDED(hr)?perf_now():0;
 if(SUCCEEDED(hr)){const frame_acct::Scope scope(frame_acct::pf_trim);trimHr=trim_native_mesh_cache();}
 const std::int64_t trim_end=trim_begin?perf_now():0;
 if(placed_hitch_profile_.active()){
  if(FAILED(trimHr)){
   finish_placed_hitch_profile(PlacedHitchFinishReason::trim_failed);
  }else if(trim_begin>0&&trim_end>=trim_begin){
   placed_hitch_previous_trim_ticks_=trim_end-trim_begin;
   placed_hitch_previous_trim_valid_=true;
  }else{
   finish_placed_hitch_profile(PlacedHitchFinishReason::invalid_clock);
  }
 }
 // A settings change in this frame's overlay prepares its new programs
 // before the next world frame would compile them on this thread.
 {const frame_acct::Scope scope(frame_acct::pf_shader_prep);
 // A completed present: the last preparation did not crash the driver.
 if(SUCCEEDED(hr)){close_shader_marker();prepare_shaders();}
 if(native_spec_updated_frame_!=native_frame_index_)pump_native_specialisation(true);}
 restart_upload_rings();native_frame_stats_={};++presentation_frame_index_;
 apply_render_scale();
 end_profile_pass(); // the game's own work until the next renderer pass stays outside
 // Frame accounting: the presenting thread's frame ends here (the scene
 // thread's; with render_thread off the game thread's frame ended at
 // present()), and this present's camera_path row is completed.
 {
  const bool scene=on_scene_thread();
  frame_acct::Record scene_frame{};
  if(scene)scene_frame=frame_acct::close_frame();
  finish_harness_path_row(inputs?inputs->game_frame:fa_game_frame_,scene?&scene_frame:nullptr);
 }
 // Keep this invocation sealed until begin_engine_frame. Image/video blits
 // can present again without starting another engine world draw.
 pending_screen_rectangles_.clear();
 return FAILED(trimHr)?trimHr:hr;
}

} // namespace owned
