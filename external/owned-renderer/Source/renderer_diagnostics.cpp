// Renderer diagnostics: status and resource snapshots, placed-hitch profile, test harness steps, load reveal and release_world_state.
#include <intrin.h>
#include "build_version.h"
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
namespace {
void placed_hitch_log_path(const std::string& path,char* output,
 std::size_t capacity) noexcept {
 if(!output||capacity==0)return;
 if(path.empty()){
  strcpy_s(output,capacity,"<unavailable>");
  return;
 }
 const std::size_t count=(std::min)(path.size(),capacity-1);
 for(std::size_t index=0;index<count;++index){
  const unsigned char value=static_cast<unsigned char>(path[index]);
  output[index]=value=='\\'?'/':(value=='"'||value<0x20?'_':path[index]);
 }
 output[count]='\0';
}
void report_placed_hitch_export(const PlacedHitchExportResult& result,
 std::uint64_t sequence_id) noexcept {
 char path[MAX_PATH]{};
 placed_hitch_log_path(result.path,path,sizeof(path));
 char detail[768]{};
 std::snprintf(detail,sizeof(detail),
  "sequence=%llu status=%s path=%s expected_bytes=%zu written_bytes=%zu error=%d",
  static_cast<unsigned long long>(sequence_id),
  placed_hitch_export_status_name(result.status),path,result.expected_bytes,
  result.written_bytes,result.error_code);
 report_placed_hitch_lifecycle(
  result.saved()?"outer_frame_gap_profile_saved":
                 "outer_frame_gap_profile_save_failed",detail);
}
}  // namespace

// Temporary per-world summary of shadow reuse/stability witnesses, appended to
// bridge.jsonl at world change and teardown. Removed once both questions settle.
#ifndef EID3D11_EMBEDDED
void Renderer::report_shadow_diagnostics(const char* reason) noexcept {
 if(!point_shadow_epoch_)return;
 try{
  std::ostringstream out;out.imbue(std::locale::classic());
  const auto& p=point_shadows_cumulative_;
  out<<"reason="<<(reason?reason:"")<<" epoch="<<point_shadow_epoch_
     <<" point{list="<<p.list<<",reorder="<<p.reorder_only<<",reset="<<p.reset
     <<",reset_epoch="<<p.reset_epoch<<",reset_full="<<p.reset_journal_full
     <<",reset_flags="<<p.reset_class_flags<<",reset_undetected="<<p.reset_undetected
     <<",reset_pool="<<p.reset_pool<<",light="<<p.light<<",radius_dirty="<<p.radius_dirty
     <<",terrain="<<p.terrain<<",eligibility_lost="<<p.eligibility_lost
     <<",strength_zeroed="<<p.strength_zeroed<<",rebaked="<<p.rebaked<<",static_to_dynamic="<<p.static_to_dynamic<<",budget_deferred="<<p.budget<<",overlays="<<p.overlays
     <<",merged_lights="<<p.merged_lights<<",casting_lights="<<p.casting_lights
     <<",pass_skipped="<<point_pass_skipped_frames_<<",publication_missing="<<point_publication_missing_frames_<<"}";
  const auto input=bridge_shadow_reuse_diagnostics();
  out<<" input{attempts="<<input.attempts;
  for(unsigned i=0;i<input.counts.size();++i)if(input.counts[i])out<<','<<shadow_input_difference_names[i]<<'='<<input.counts[i];
  out<<",only_clip="<<input.only_world_to_clip_frames<<",only_clip_max_delta="<<input.only_world_to_clip_max_abs_delta<<"}";
  out<<" water{liquid_batches="<<water_liquid_batches_<<",reflection_batches="<<water_reflection_batches_
     <<",resource_failures="<<water_reflection_resource_failures_<<",clip_valid="<<native_world_clip_valid_<<"}";
  water_liquid_batches_=water_reflection_batches_=water_reflection_resource_failures_=0;
  out<<" renderer{";
  for(unsigned i=0;i<shadow_renderer_cumulative_.counts.size();++i)if(shadow_renderer_cumulative_.counts[i])
   out<<shadow_renderer_difference_names[i]<<'='<<shadow_renderer_cumulative_.counts[i]<<',';
  out<<"}";
  bridge_report_event("shadow_diagnostics",out.str().c_str());
 }catch(...){}
}
#else
void Renderer::report_shadow_diagnostics(const char* reason) noexcept {  }
#endif

bool Renderer::status_text_wanted() const noexcept {
 return debug_overlay_.visible()||debug_overlay_.wants_profiling()||profiler_.attribution_enabled()||
  placed_hitch_profile_.active()||harness_enabled()||!critters_summary_.labels.empty();
}
RendererResourceSnapshot Renderer::resource_snapshot() const {
 RendererResourceSnapshot snapshot=native_frame_stats_;
 if(debug_overlay_.settings_open()&&samgbur_lod_frame_==presentation_frame_index_){
  snapshot.samgbur_lod=samgbur_lod_snapshot_;
  snapshot.samgbur_lod.instances=static_cast<UINT>(samgbur_lod_records_.size());
  const auto selected=debug_overlay_.samgbur_lod_instance();
  if(selected>=samgbur_lod_records_.size()||samgbur_lod_records_[selected]!=snapshot.samgbur_lod.record){
   snapshot.samgbur_lod.state=samgbur_lod_records_.empty()?SamgburLodSnapshot::absent:SamgburLodSnapshot::waiting;
  }
  snapshot.samgbur_lod.age_frames=presentation_frame_index_>=snapshot.samgbur_lod.sample_frame?
   presentation_frame_index_-snapshot.samgbur_lod.sample_frame:0;
 }
 {const auto& grid=native_point_light_grid_;const std::uint64_t cells=std::uint64_t(grid.width)*grid.height;
  snapshot.point_light_grid={native_point_lights_.size(),grid.width,grid.height,
   grid.data.size()>cells?grid.data.size()-cells-1:0,grid.max_per_cell,emissive_published_};}
 {const int level=configured_integer(debug_overlay_.config(),"emissive_lights",1);
  if(level<=0)std::snprintf(snapshot.emissive_status,sizeof(snapshot.emissive_status),"Emissive lights: off");
  else if(emissive_generation_!=ambient_map_generation_)std::snprintf(snapshot.emissive_status,sizeof(snapshot.emissive_status),"Emissive lights: not built for this map yet");
  else{const auto c=emissive_colors_.empty()?std::array<float,3>{}:emissive_colors_.front();
   std::snprintf(snapshot.emissive_status,sizeof(snapshot.emissive_status),"Emissive lights: %zu published of %zu (%zu lava tiles, level %d), colour %.2f %.2f %.2f%s",
    emissive_published_,emissive_clusters_.size(),emissive_tiles_.size(),level,c[0],c[1],c[2],emissive_colors_sampled_?"":" (ramp default)");}}
 snapshot.point_shadows_cumulative=point_shadows_cumulative_;snapshot.point_shadow_epoch=point_shadow_epoch_;
 snapshot.point_pass_skipped_frames=point_pass_skipped_frames_;snapshot.point_publication_missing_frames=point_publication_missing_frames_;
 snapshot.shadow_input_cumulative=bridge_shadow_reuse_diagnostics();snapshot.shadow_renderer_cumulative=shadow_renderer_cumulative_;
 snapshot.native_shadows_enabled=native_shadows_enabled_;snapshot.culling_frozen=native_cull_frozen_;
 std::snprintf(snapshot.fog_status,sizeof(snapshot.fog_status),"%s",fog_status_);snapshot.fog_problem=fog_status_problem_;if(ambient_status_frame_==UINT64_MAX||ambient_status_frame_+2<native_frame_index_){std::snprintf(snapshot.particle_status,sizeof(snapshot.particle_status),"Particles: pass not reached (no world composite this frame)");snapshot.particle_problem=configured_integer(debug_overlay_.config(),"ambient_particles",1)>0;}else{std::snprintf(snapshot.particle_status,sizeof(snapshot.particle_status),"%s",ambient_status_);snapshot.particle_problem=ambient_status_problem_;}std::snprintf(snapshot.precipitation_status,sizeof(snapshot.precipitation_status),"%s",precipitation_status_frame_==UINT64_MAX||precipitation_status_frame_+2<native_frame_index_?"Precipitation: pass not reached (no world composite this frame)":precipitation_status_);snapshot.precipitation_problem=precipitation_status_problem_&&precipitation_status_frame_+2>=native_frame_index_;
 if(critters_status_frame_==UINT64_MAX||critters_status_frame_+2<native_frame_index_)
  std::snprintf(snapshot.critter_status,sizeof(snapshot.critter_status),"Critters: pass not reached (no world draw this frame)");
 else{std::snprintf(snapshot.critter_status,sizeof(snapshot.critter_status),"%s",critters_status_);snapshot.critters=critters_summary_;}
 std::snprintf(snapshot.sky_status,sizeof(snapshot.sky_status),"%s",sky_status_);snapshot.sky_problem=sky_status_problem_;if(configured_integer(debug_overlay_.config(),"sky_clouds",2)>0&&!bridge_owned_sky_installed()){
  std::snprintf(snapshot.sky_status,sizeof(snapshot.sky_status),"Sky PROBLEM: owned hook rejected; stock sky omitted");snapshot.sky_problem=true;}

 const ResMappingSnapshot mappings=res_mapping_snapshot();
 snapshot.res_mapping_status=static_cast<std::uint32_t>(mappings.status);
 snapshot.res_mapping_hooks_active=mappings.hooks_active;
 snapshot.res_mapping_tracked_handles=mappings.tracked_mapping_handles;
 snapshot.res_mapping_logical_views=mappings.logical_views;
 snapshot.res_mapping_physical_views=mappings.physical_views;
 snapshot.res_mapping_physical_bytes=mappings.physical_view_bytes;
 snapshot.res_mapping_active_saved_bytes=mappings.active_saved_view_bytes;
 snapshot.res_mapping_peak_saved_bytes=mappings.peak_saved_view_bytes;
 snapshot.res_mapping_shared_reuses=mappings.shared_reuses;
 snapshot.res_mapping_deferred_unmaps=mappings.deferred_unmaps;
 snapshot.res_mapping_passthrough_maps=mappings.passthrough_maps;
 snapshot.res_mapping_identity_rejections=mappings.identity_rejections;
 snapshot.res_mapping_capacity_failures=mappings.tracking_capacity_failures;
 snapshot.res_mapping_validation_failures=mappings.hook_validation_failures;
 const ResCacheReleaseSnapshot cache=res_cache_release_snapshot();
 snapshot.res_cache_status=static_cast<std::uint32_t>(cache.status);
 snapshot.res_cache_hooks_active=cache.hooks_active;
 snapshot.res_cache_override_gets=cache.override_gets;
 snapshot.res_cache_override_releases=cache.override_releases;
 snapshot.res_cache_icon_releases=cache.icon_releases;
 snapshot.presentation_frame=presentation_frame_index_;snapshot.present_interval_ms=present_interval_ms_;snapshot.present_smoothed_ms=present_smoothed_ms_;
 const FrameStatisticsSnapshot rolling=frame_statistics_.snapshot();
 snapshot.rolling_frame_samples=static_cast<std::uint64_t>(rolling.sample_count);
 snapshot.rolling_frame_seconds=rolling.covered_seconds;
 snapshot.rolling_average_fps=rolling.average_fps;
 snapshot.rolling_lowest_fps=rolling.lowest_fps;
 snapshot.rolling_frame_warming_up=rolling.warming_up;
 snapshot.rolling_frame_complete=rolling.complete;
 snapshot.rolling_frame_capacity_truncated=rolling.capacity_truncated;
 snapshot.system_telemetry_stale=system_telemetry_snapshot_.stale;
 snapshot.system_telemetry_age_ms=system_telemetry_snapshot_.age_ms;
 if(!system_telemetry_snapshot_.stale){
  const auto valid_percent=[](const TelemetryValue& value){return value.valid&&std::isfinite(value.value)&&value.value>=0.0;};
  snapshot.process_cpu_valid=valid_percent(system_telemetry_snapshot_.cpu_percent);
  if(snapshot.process_cpu_valid)snapshot.process_cpu_percent=system_telemetry_snapshot_.cpu_percent.value;
  snapshot.gpu_3d_percent_valid=valid_percent(system_telemetry_snapshot_.gpu_3d_percent);
  if(snapshot.gpu_3d_percent_valid)snapshot.gpu_3d_percent=system_telemetry_snapshot_.gpu_3d_percent.value;
  const auto valid_bytes=[](const TelemetryValue& value){return value.valid&&std::isfinite(value.value)&&value.value>=0.0&&value.value<=static_cast<double>((std::numeric_limits<std::uint64_t>::max)());};
  snapshot.process_private_bytes_valid=valid_bytes(system_telemetry_snapshot_.ram_private_bytes);
  if(snapshot.process_private_bytes_valid)snapshot.process_private_bytes=static_cast<std::uint64_t>(system_telemetry_snapshot_.ram_private_bytes.value);
  snapshot.gpu_memory_valid=valid_bytes(system_telemetry_snapshot_.vram_usage_bytes)&&valid_bytes(system_telemetry_snapshot_.vram_budget_bytes);
  if(snapshot.gpu_memory_valid){snapshot.gpu_usage_bytes=static_cast<std::uint64_t>(system_telemetry_snapshot_.vram_usage_bytes.value);snapshot.gpu_budget_bytes=static_cast<std::uint64_t>(system_telemetry_snapshot_.vram_budget_bytes.value);}
 }
 snapshot.cpu_surface_bytes=g_cpu_surface_bytes.load(std::memory_order_relaxed);
 snapshot.gpu_texture_bytes=g_gpu_surface_bytes.load(std::memory_order_relaxed);
 snapshot.double_resident_bytes=g_double_resident_bytes.load(std::memory_order_relaxed);
 {const auto residency=surface_residency_stats();LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
  snapshot.released_surface_bytes=residency.released_bytes;snapshot.released_surfaces=residency.released_surfaces;
  snapshot.surface_restore_readbacks=residency.restore_readbacks;snapshot.pinned_surfaces=residency.pinned_surfaces;
  snapshot.surface_restore_readback_ms=frequency.QuadPart?1000.0*static_cast<double>(residency.restore_readback_qpc)/static_cast<double>(frequency.QuadPart):0.0;}
 snapshot.transient_upload_bytes=static_cast<std::uint64_t>(dynamic_vertex_capacity_)+dynamic_index_capacity_+native_dynamic_capacity_+(native_world_lighting_constants_?sizeof(NativeWorldLighting):0u)+(native_point_light_constants_?32u:0u)+static_cast<std::uint64_t>(native_point_light_capacity_)*sizeof(NativePointLightGpu)+static_cast<std::uint64_t>(native_point_light_grid_capacity_)*sizeof(UINT)+static_cast<std::uint64_t>(native_draw_work_capacity_)*sizeof(NativeDrawWorkGpu)+static_cast<std::uint64_t>(native_visible_triangle_capacity_)*16u+static_cast<std::uint64_t>(native_prepared_vertex_capacity_)*sizeof(NativePreparedVertexGpu)+static_cast<std::uint64_t>(native_indirect_args_capacity_)*16u;
 // renderer_backend=d3d12: the native queue's frame buffers on the executor
 // (the D3D11 capacities above stay zero there).
 if(d3d12_){
  snapshot.d3d12_backend=true;
  for(const auto* buffer:{native12_.dynamic.get(),native12_.work.get(),native12_.arguments.get(),native12_.visible.get(),
                          native12_.prepared.get(),native12_.point_lights.get(),native12_.point_light_grid.get()})
   if(buffer)snapshot.transient_upload_bytes+=d3d12_->buffer_bytes(*buffer);
 }
 for(const auto& pending:pending_blits_)snapshot.transient_upload_bytes+=pending.pixels.capacity()*sizeof(std::uint32_t);
 std::uint64_t overlay_bytes=0;for(const auto& entry:overlay_textures_)overlay_bytes+=texture_storage_bytes(entry.second.texture.Get());std::uint64_t native_texture_array_bytes=0;snapshot.native_texture_array_classes=native_texture_arrays_.size();for(const auto& group:native_texture_arrays_)for(const auto& page:group.second){native_texture_array_bytes+=texture_storage_bytes(page.texture.Get());++snapshot.native_texture_array_pages;snapshot.native_texture_array_slices+=page.used;}
 snapshot.owned_texture_entries=native_owned_textures_.size();snapshot.owned_texture_bytes=native_owned_texture_bytes(false);
 snapshot.texture_array_bytes=native_texture_array_bytes;snapshot.geometry_page_bytes=native_geometry_capacity_bytes();
 snapshot.geometry_live_bytes=native_geometry_live_bytes_;
 const std::uint64_t target_bytes=static_cast<std::uint64_t>(output_width_)*output_height_*4u+static_cast<std::uint64_t>(width_)*height_*4u+texture_storage_bytes(interface_depth_.Get())+scene_composite_.storage_bytes()+fsr_.storage_bytes()+upscale_sharpen_.storage_bytes()+bloom_.storage_bytes()+static_cast<std::uint64_t>(glow_?glow_width_:0)*glow_height_*4u+depth_of_field_.storage_bytes()+sun_rays_.storage_bytes()+
  texture_storage_bytes(native_shadow_depth_.Get())+
  native_point_shadow_bytes()+
  texture_storage_bytes(shadow_history_mask_[0].Get())+
  texture_storage_bytes(shadow_history_mask_[1].Get())+
  texture_storage_bytes(shadow_history_depth_.Get())+
  texture_storage_bytes(ao_ambient_texture_.Get())+texture_storage_bytes(ao_raw_texture_.Get())+
  texture_storage_bytes(ao_filtered_texture_.Get())+texture_storage_bytes(ao_depth_texture_.Get());
 snapshot.renderer_resident_bytes=snapshot.gpu_texture_bytes+snapshot.geometry_page_bytes+overlay_bytes+native_texture_array_bytes+snapshot.owned_texture_bytes+snapshot.transient_upload_bytes+target_bytes+
  static_cast<std::uint64_t>(native_world_vertex_capacity_)*sizeof(NativeWorldVertexGpu)+
  std::uint64_t(native_world_state_capacity_)*sizeof(NativeWorldStateGpu)+((native_shadow_constants_?sizeof(NativeShadowConstants):0u)+(native_point_shadow_constants_?sizeof(NativeShadowConstants):0u));
 snapshot.renderer_resident_bytes+=native_animation_capacity_+std::uint64_t(native_pose_node_capacity_)*sizeof(NativePoseNodeGpu)+
  std::uint64_t(native_pose_input_capacity_)*sizeof(NativePoseGpu)+std::uint64_t(native_persistent_instance_capacity_)*(sizeof(NativeInstanceGpu)+sizeof(NativeDrawGpu))+
  std::uint64_t(native_instance_update_capacity_)*sizeof(NativeInstanceUpdate)+(native_world_clip_constants_?sizeof(native_world_clip_):0u);
 if(debug_overlay_.settings_open())snapshot.video_memory=video_memory_breakdown();
 snapshot.vram_budget=vram_budget_snapshot();
 return snapshot;
}

VideoMemoryBreakdown Renderer::video_memory_breakdown() const {
 if(video_memory_.valid&&native_frame_index_>=video_memory_frame_&&native_frame_index_<video_memory_frame_+15)return video_memory_;
 VideoMemoryBreakdown out;
 try{
  // A resource reached through several views or members counts once.
  std::unordered_set<ID3D11Resource*> counted;
  using G=VideoMemoryGroup;
  const auto add=[&](G group,ID3D11Resource* resource){
   if(resource&&counted.insert(resource).second)out.bytes[static_cast<std::size_t>(group)]+=resource_video_bytes(resource);
  };
  const auto add_view=[&](G group,ID3D11View* view){
   if(!view)return;ComPtr<ID3D11Resource> resource;view->GetResource(&resource);add(group,resource.Get());
  };
  const auto add_bytes=[&](G group,std::uint64_t bytes){out.bytes[static_cast<std::size_t>(group)]+=bytes;};

  if(swap_){DXGI_SWAP_CHAIN_DESC d{};if(SUCCEEDED(swap_->GetDesc(&d)))
   add_bytes(G::screen,std::uint64_t(d.BufferDesc.Width)*d.BufferDesc.Height*4u*std::max(1u,d.BufferCount));}
  add(G::screen,depth_.Get());add(G::screen,interface_depth_.Get());add(G::screen,glow_.Get());add(G::screen,native_hiz_.Get());
  add(G::screen,prepass_rest_.Get());add(G::screen,prepass_normal_.Get());add(G::screen,shadow_mask_.Get());add(G::screen,load_picture_.Get());
  add_bytes(G::screen,scene_composite_.storage_bytes()+fsr_.storage_bytes()+upscale_sharpen_.storage_bytes()+bloom_.storage_bytes()+depth_of_field_.storage_bytes()+sun_rays_.storage_bytes());

  add(G::water,water_scene_color_.Get());add(G::water,water_scene_depth_.Get());
  for(const auto& texture:water_ssr_textures_)add(G::water,texture.Get());
  add(G::water,water_ssr_depth_.Get());
  for(const auto& texture:particle_depth_)add(G::water,texture.Get());
  for(const auto& texture:liquid_wave_texture_)add(G::water,texture.Get());
  add_view(G::water,liquid_normal_srv_.Get());add_view(G::water,liquid_detail_srv_.Get());

  for(auto* texture:{ao_ambient_texture_.Get(),ao_raw_texture_.Get(),ao_filtered_texture_.Get(),ao_depth_texture_.Get()})
   add(G::ambient_occlusion,texture);

  for(const auto& texture:fog_scatter_)add(G::fog,texture.Get());
  add(G::fog,fog_integrated_.Get());add_view(G::fog,fog_mist_srv_.Get());

  for(auto* view:{sky_shape_srv_.Get(),sky_detail_srv_.Get(),sky_weather_srv_.Get(),sky_lut_srv_.Get(),sky_shadow_srv_.Get(),sky_shadow_raw_srv_.Get(),
                  sky_march_srv_.Get(),sky_cloud_srv_[0].Get(),sky_cloud_srv_[1].Get(),sky_probe_srv_[0].Get(),sky_probe_srv_[1].Get()})
   add_view(G::sky,view);

  add(G::sun_shadows,native_shadow_depth_.Get());add(G::sun_shadows,native_shadow_static_depth_.Get());
  for(const auto& texture:shadow_history_mask_)add(G::sun_shadows,texture.Get());
  add(G::sun_shadows,shadow_history_depth_.Get());
  add_bytes(G::lamp_shadows,native_point_shadow_bytes());

  add(G::terrain_blend,contact_maps_.texture.Get());add_view(G::terrain_blend,contact_maps_.tiles.Get());

  for(auto* buffer:{gc_vertices_.Get(),gc_indices_.Get(),gc_rules_.Get(),gc_args_.Get(),gc_counts_.Get(),gc_instances_.Get(),
                    gc_zone_pieces_.Get(),gc_overflow_.Get(),gc_objects_.Get(),gc_footprints_.Get(),gc_snow_segments_.Get()})
   add(G::ground_cover,buffer);
  for(auto* texture:{gc_color_.Get(),gc_object_mask_.Get(),gc_trail_[0].Get(),gc_trail_[1].Get(),gc_trail_sources_.Get(),
                     gc_snow_[0].Get(),gc_snow_[1].Get()})
   add(G::ground_cover,texture);
  for(auto* view:{gc_fields_srv_.Get(),gc_tiles_srv_.Get(),gc_bark_srv_.Get(),gc_stone_srv_.Get(),gc_trees_srv_.Get(),
                  ambient_area_srv_.Get(),ambient_height_srv_.Get(),ambient_ground_srv_.Get(),ambient_type_srv_.Get(),
                  ambient_sources_srv_.Get(),ambient_fall_tiles_srv_.Get(),ambient_falls_srv_.Get(),ambient_fall_shell_srv_.Get()})
   add_view(G::ground_cover,view);
  for(const auto& n:gc_neighbours_){
   add(G::ground_cover,n.color_texture.Get());
   for(auto* view:{n.area.Get(),n.height.Get(),n.ground.Get(),n.type.Get(),n.trees.Get(),n.fields.Get(),n.sea.Get(),n.tiles.Get()})
    add_view(G::ground_cover,view);
  }
  add_bytes(G::critters,critter_video_bytes());
  add(G::geometry,impostors_.albedo.Get());add(G::geometry,impostors_.normal.Get());add(G::geometry,impostors_.depth.Get());

  std::uint64_t overlay_bytes=0;for(const auto& entry:overlay_textures_)overlay_bytes+=texture_storage_bytes(entry.second.texture.Get());
  add_bytes(G::game_textures,g_gpu_surface_bytes.load(std::memory_order_relaxed)+native_owned_texture_bytes(false)+overlay_bytes);
  for(const auto& group:native_texture_arrays_)for(const auto& page:group.second)add_bytes(G::texture_arrays,texture_storage_bytes(page.texture.Get()));
  add_bytes(G::geometry,native_geometry_capacity_bytes()+
   std::uint64_t(native_world_vertex_capacity_)*sizeof(NativeWorldVertexGpu)+std::uint64_t(native_world_state_capacity_)*sizeof(NativeWorldStateGpu)+
   native_animation_capacity_+std::uint64_t(native_pose_node_capacity_)*sizeof(NativePoseNodeGpu)+
   std::uint64_t(native_pose_input_capacity_)*sizeof(NativePoseGpu)+
   std::uint64_t(native_persistent_instance_capacity_)*(sizeof(NativeInstanceGpu)+sizeof(NativeDrawGpu))+
   std::uint64_t(native_instance_update_capacity_)*sizeof(NativeInstanceUpdate));
  add_bytes(G::frame_uploads,static_cast<std::uint64_t>(dynamic_vertex_capacity_)+dynamic_index_capacity_+native_dynamic_capacity_+
   std::uint64_t(native_point_light_capacity_)*sizeof(NativePointLightGpu)+std::uint64_t(native_point_light_grid_capacity_)*sizeof(UINT)+
   std::uint64_t(native_draw_work_capacity_)*sizeof(NativeDrawWorkGpu)+std::uint64_t(native_visible_triangle_capacity_)*16u+
   std::uint64_t(native_prepared_vertex_capacity_)*sizeof(NativePreparedVertexGpu)+std::uint64_t(native_indirect_args_capacity_)*16u);
  // Terrain 2.0 material cache (terrain2_cache): fine slices and coarse atlas.
  for(auto* texture:{terrain2_cache_fine_.Get(),terrain2_cache_coarse_.Get()})out.terrain_cache_bytes+=resource_video_bytes(texture);
  out.terrain_cache_known=true;
  out.valid=true;
 }catch(const std::bad_alloc&){return video_memory_;}
 video_memory_=out;video_memory_frame_=native_frame_index_;
 return out;
}

CutoutShadowScopeSnapshot Renderer::cutout_shadow_scope_snapshot() const noexcept {
 try {
  std::vector<CutoutShadowScopeRecord> records;
  for(const auto& material:native_prepared_material_slots_){
   if(!material.active||material.owner.expired())continue;
   const UINT index=material.persistent_instance[1];
   if(index>=native_instance_records_.size())continue;
   const auto& record=native_instance_records_[index];
   if(!record.occupied||!record.owned||!record.depth_current||record.owner.expired())continue;
   const auto& draw=record.command;
   if(draw.state_bucket>=native_state_buckets_.size()||
      draw.world_slot>=native_prepared_mesh_slots_.size())continue;
   const auto& bucket=native_state_buckets_[draw.state_bucket];
   const auto& mesh=native_prepared_mesh_slots_[draw.world_slot];
   if(!bucket.occupied||bucket.material_key.shadow!=1||
      bucket.opacity!=NativeOpacityClass::cutout||
      bucket.state.render[D3DRENDERSTATE_CULLMODE]!=D3DCULL_NONE||
      !mesh.active||mesh.mesh!=draw.mesh)continue;
   records.push_back({mesh.resource_key,material.identity.terrain_source,
    material.identity.signed_id,material.identity.lifecycle_generation,
    material.identity.world_epoch});
  }
  return summarize_cutout_shadow_scope(presentation_frame_index_,records);
 }catch(...){CutoutShadowScopeSnapshot unavailable{};
  unavailable.presentation_frame=presentation_frame_index_;return unavailable;}
}

bool Renderer::export_placed_hitch_profile() noexcept {
 if(!placed_hitch_profile_.take_export_request())return false;
 char finish_detail[256]{};
 std::snprintf(finish_detail,sizeof(finish_detail),
  "sequence=%llu reason=%s frames=%zu duration_ms=%.3f",
  static_cast<unsigned long long>(placed_hitch_profile_.sequence_id()),
  placed_hitch_finish_reason_name(placed_hitch_profile_.finish_reason()),
  placed_hitch_profile_.frames().size(),placed_hitch_profile_.elapsed_ms());
 report_placed_hitch_lifecycle("outer_frame_gap_profile_finished",finish_detail);

 PlacedHitchExportResult result{};
 try{
  char root[MAX_PATH]{};
  SetLastError(ERROR_SUCCESS);
  const DWORD root_length=GetEnvironmentVariableA(
   "EI_RENDERER_DEBUG_DIR",root,MAX_PATH);
  if(!root_length){
   result.status=PlacedHitchExportStatus::debug_directory_missing;
   const DWORD error=GetLastError();
   result.error_code=error==ERROR_SUCCESS?ERROR_ENVVAR_NOT_FOUND:
                                          static_cast<int>(error);
  }else if(root_length>=MAX_PATH){
   result.status=PlacedHitchExportStatus::path_too_long;
   result.error_code=ERROR_FILENAME_EXCED_RANGE;
  }else{
   const char* build=kBuildCommit;
   result=export_placed_hitch_profile_recording(
    root,build,GetCurrentProcessId(),width_,height_,
    placed_hitch_scene_json_,placed_hitch_profile_);
  }
 }catch(const std::bad_alloc&){
  result.status=PlacedHitchExportStatus::allocation_failed;
  result.error_code=ENOMEM;
 }catch(...){
  result.status=PlacedHitchExportStatus::serialization_failed;
  result.error_code=EIO;
 }
 report_placed_hitch_export(result,placed_hitch_profile_.sequence_id());
 return result.saved();
}

void Renderer::finish_placed_hitch_profile(
 PlacedHitchFinishReason reason) noexcept {
 if(!placed_hitch_profile_.finish_invalid(reason))return;
 bridge_set_outer_frame_timing_active(false);
 placed_hitch_previous_trim_ticks_=0;
 placed_hitch_previous_trim_valid_=false;
 export_placed_hitch_profile();
 bridge_set_profile_enabled(debug_overlay_.wants_profiling());
}

// Test harness: the commands that need the renderer. Each one stays at the
// front of the harness queue until it is completed here.
// Frame accounting (frame_accounting.h): completes this present's camera_path
// row with the game thread's frame (gt_*, ending at present() entry: the
// previous present_frame is inside it unless the scene thread runs it), the
// scene thread's frame (st_*, ending now; only with render_thread on), this
// frame's D3D11 call counters (d3d_*), the sun shadow map decisions (shw_*),
// the longest Map stall and the swap chain's queued presents. Each thread's
// parts sum to its wall_ms (sum_check_ms is the difference, 0 unless ticks
// were lost). Rows are buffered and written by step_test_harness once the
// path ends.
void Renderer::finish_harness_path_row(const frame_acct::Record& game_frame,const frame_acct::Record* scene_frame) noexcept try{
 // Every per-frame witness advances on every present, with or without a
 // row, so each row covers exactly its own frame.
 const auto counters=frame_acct::counters_snapshot();
 const auto previous=fa_counters_;fa_counters_=counters;
 const auto shadow_now=shadow_renderer_cumulative_.counts;
 const auto shadow_previous=fa_shadow_witness_;fa_shadow_witness_=shadow_now;
 auto& map_stall=frame_acct::vt::g_map_stall;
 const std::int64_t stall=map_stall.ticks.exchange(0,std::memory_order_relaxed);
 if(harness_path_line_.empty())return;
 const frame_acct::Scope scope(frame_acct::pf_harness);
 std::string& line=harness_path_line_;
 const double scale=frame_acct::ms_per_tick();
 char text[192];
 std::snprintf(text,sizeof(text),",\"presentation_frame\":%llu,\"render_thread\":%d",
  static_cast<unsigned long long>(presentation_frame_index_),scene_frame?1:0);
 line+=text;
 const auto emit=[&](const char* prefix,const frame_acct::Record& record){
  if(!record.valid)return;
  std::snprintf(text,sizeof(text),",\"%s_serial\":%llu,\"%s_wall_ms\":%.3f,\"%s_oncpu_ms\":%.3f",
   prefix,static_cast<unsigned long long>(record.serial),prefix,double(record.wall)*scale,prefix,record.oncpu_ms);
  line+=text;
  std::int64_t sum=0;
  for(unsigned part=0;part<frame_acct::count;++part){
   const std::int64_t ticks=record.ticks[part];
   if(!ticks)continue;
   sum+=ticks;
   std::snprintf(text,sizeof(text),",\"%s_%s\":%.3f",prefix,frame_acct::part_name(part),double(ticks)*scale);
   line+=text;
  }
  std::snprintf(text,sizeof(text),",\"%s_sum_check_ms\":%.3f",prefix,double(record.wall-sum)*scale);
  line+=text;
  if(record.overflow){std::snprintf(text,sizeof(text),",\"%s_scope_overflow\":%u",prefix,record.overflow);line+=text;}
 };
 emit("gt",game_frame);
 if(scene_frame)emit("st",*scene_frame);
 for(unsigned counter=0;counter<frame_acct::counter_count;++counter){
  const std::int64_t delta=counters[counter]-previous[counter];
  if(!delta)continue;
  if(frame_acct::counter_is_ticks(counter))
   std::snprintf(text,sizeof(text),",\"d3d_%s\":%.3f",frame_acct::counter_name(counter),double(delta)*scale);
  else std::snprintf(text,sizeof(text),",\"d3d_%s\":%lld",frame_acct::counter_name(counter),static_cast<long long>(delta));
  line+=text;
 }
 // Sun shadow map decisions in this frame (begin_native_shadows witness
 // counts: why a slice was redrawn, scrolled, retained or deferred).
 for(std::size_t i=0;i<shadow_now.size();++i){
  const std::uint64_t delta=shadow_now[i]>=shadow_previous[i]?shadow_now[i]-shadow_previous[i]:shadow_now[i];
  if(!delta)continue;
  std::snprintf(text,sizeof(text),",\"shw_%s\":%llu",shadow_renderer_difference_names[i],static_cast<unsigned long long>(delta));
  line+=text;
 }
 // The longest Map stall in this frame (map type | 0x100 with flags;
 // usage: buffer D3D11_USAGE, 10+ for a 2D texture).
 if(stall){
  const int part=map_stall.part.load(std::memory_order_relaxed);
  std::snprintf(text,sizeof(text),",\"map_stall_max_ms\":%.3f,\"map_stall_type\":%d,\"map_stall_usage\":%d,\"map_stall_bytes\":%u,\"map_stall_part\":\"%s\"",
   double(stall)*scale,map_stall.type.load(std::memory_order_relaxed),map_stall.usage.load(std::memory_order_relaxed),
   map_stall.bytes.load(std::memory_order_relaxed),part>=0&&part<frame_acct::count?frame_acct::part_name(unsigned(part)):"none");
  line+=text;
 }
 // Presents the swap chain has accepted but not yet shown (DXGI queue).
 if(swap_){
  UINT last=0;DXGI_FRAME_STATISTICS statistics{};
  if(SUCCEEDED(swap_->GetLastPresentCount(&last))&&SUCCEEDED(swap_->GetFrameStatistics(&statistics))){
   std::snprintf(text,sizeof(text),",\"present_queue\":%d",static_cast<int>(last-statistics.PresentCount));
   line+=text;
  }
 }
 harness_path_rows_+=line;harness_path_rows_+="}\n";
 line.clear();
 if(harness_path_rows_.size()>(4u<<20))write_harness_path_rows();
}catch(...){harness_path_line_.clear();}

// Buffered camera_path rows to disk (frame accounting).
void Renderer::write_harness_path_rows() noexcept{
 if(harness_path_rows_.empty())return;
 if(FILE* file=nullptr;fopen_s(&file,(harness_directory()+"camera_path.jsonl").c_str(),"ab")==0&&file){
  std::fwrite(harness_path_rows_.data(),1,harness_path_rows_.size(),file);std::fclose(file);
 }
 harness_path_rows_.clear();
}

Renderer::HarnessRequest Renderer::step_test_harness(const ScenePresentInputs* inputs) noexcept {
 // Scene thread: the game thread stepped the queue when it queued this
 // present; a command completed here may reach one more present before the
 // game thread sees it retired, and is skipped there.
 const HarnessCommand* command=inputs?(inputs->harness_command_valid&&inputs->harness_entry_id!=harness_completed_entry_?
  &inputs->harness_command:nullptr):harness_present_step();
 // camera_path rows are buffered while a path runs (frame accounting);
 // they go to disk once another command (or none) is at the front.
 if(!harness_path_rows_.empty()&&(!command||command->verb!=HarnessVerb::camera_path)){
  const frame_acct::Scope io_scope(frame_acct::pf_harness);
  write_harness_path_rows();
 }
 if(!command)return HarnessRequest::none;
 const auto finish=[this,inputs](bool ok,const std::string& detail){
#ifndef EID3D11_EMBEDDED
  if(inputs){harness_complete_entry(inputs->harness_entry_id,ok,detail);harness_completed_entry_=inputs->harness_entry_id;}

  else
#endif
   harness_complete(ok,detail);
  harness_stage_=0;harness_wait_=0;};
 std::string detail;
 // D3D12 backend: no world yet (wait_ready) and no outer frame recorder
 // (profile), so these fail at once (named) and the route goes on; captures
 // (the profiler's pass timings), screenshots, config and paths run.
 if(d3d12_&&(command->verb==HarnessVerb::profile||command->verb==HarnessVerb::wait_ready)){
  finish(false,"not on the Direct3D 12 backend yet (no world, no outer frame recorder)");
  return HarnessRequest::none;
 }
 switch(command->verb){
 case HarnessVerb::capture:
  if(harness_stage_==0){
   // Another capture or the outer frame recorder holds the profiler: wait
   // for it, but not forever (600 presents), and say which.
   if(profiler_.capture_active()||placed_hitch_profile_.active()){
    if(++harness_wait_>600)finish(false,profiler_.capture_active()?"capture blocked for 600 presents: another profiler capture is active":
     "capture blocked for 600 presents: the outer frame gap recorder is active");
    return HarnessRequest::none;
   }
   harness_wait_=0;harness_stage_=1;return HarnessRequest::capture;
  }
  if(harness_stage_==1){
   if(profiler_.capture_active()){harness_capture_directory_=profiler_.capture_directory();harness_stage_=2;}
   else if(++harness_wait_>8)finish(false,"capture did not start");
   return HarnessRequest::none;
  }
  // The composed frame is written at the capture's present; its timing
  // metadata resolves a few presents later.
  if(++harness_stage_<10)return HarnessRequest::none;
  finish(true,harness_capture_directory_);
  return HarnessRequest::none;
 case HarnessVerb::profile:
  if(harness_stage_==0){
   if(!placed_hitch_profile_enabled()||!bridge_outer_frame_timing_supported()){
    finish(false,"needs EI_RENDERER_OUTER_FRAME_GAP_PROFILE=1 at launch");
    return HarnessRequest::none;
   }
   if(placed_hitch_profile_.active()||profiler_.capture_active())return HarnessRequest::none;
   placed_hitch_profile_.set_window_seconds(static_cast<std::int64_t>(command->number[0]));
   harness_profile_armed_=false;harness_stage_=1;
   return HarnessRequest::profile;
  }
  if(harness_stage_==1){
   if(!harness_profile_armed_){
    placed_hitch_profile_.set_window_seconds(PlacedHitchProfile::window_seconds);
    finish(false,"arm rejected; see outer_frame_gap_profile lifecycle log");
    return HarnessRequest::none;
   }
   harness_stage_=2;
  }
  if(placed_hitch_profile_.active())return HarnessRequest::none;
  {
   const auto reason=placed_hitch_profile_.finish_reason();
   char text[192]{};
   std::snprintf(text,sizeof(text),"sequence=%llu reason=%s frames=%zu duration_ms=%.1f",
    static_cast<unsigned long long>(placed_hitch_profile_.sequence_id()),
    placed_hitch_finish_reason_name(reason),placed_hitch_profile_.frames().size(),
    placed_hitch_profile_.elapsed_ms());
   finish(reason==PlacedHitchFinishReason::complete||reason==PlacedHitchFinishReason::truncated,text);
  }
  return HarnessRequest::none;
 case HarnessVerb::screenshot:{
  // With critter labels on (or harness_shot_after_overlay, to check the F10
  // overlay), the shot is written after this present's overlay pass and
  // cursor; otherwise it is the game's image.
  if(harness_stage_==1)return HarnessRequest::none;
  if(configured_integer(debug_overlay_.config(),"critter_labels",0)||
     configured_boolean(debug_overlay_.config(),"harness_shot_after_overlay",false)){
   harness_overlay_shot_=harness_directory()+command->name+".bmp";harness_stage_=1;
   return HarnessRequest::none;
  }
  const bool ok=write_harness_screenshot(harness_directory()+command->name+".bmp",detail);
  finish(ok,detail);
  return HarnessRequest::none;
 }
 case HarnessVerb::config:{
  const bool ok=apply_harness_config(*command,detail);
  finish(ok,detail);
  return HarnessRequest::none;
 }
 case HarnessVerb::camera_path:{
#ifndef EID3D11_EMBEDDED
  // Temporary (camera-movement frame cost; remove once found): one line per
  // present while the camera moves: the present-to-present time and the
  // newest resolved profiler frame (CPU, GPU, its top passes) when the
  // profiler runs (EI_RENDERER_PROFILE=1 or the overlay).
  LARGE_INTEGER now{};QueryPerformanceCounter(&now);
  const std::int64_t ticks=static_cast<std::int64_t>(now.QuadPart);
  // A new path (no step applied yet) starts its own count.
  if((inputs?inputs->harness_camera_steps:harness_camera_path_steps())==0){harness_path_previous_=0;harness_path_present_=0;}
  const double wall=harness_path_previous_?1000.0*double(ticks-harness_path_previous_)/double(perf_frequency()):-1.0;
  harness_path_previous_=ticks;++harness_path_present_;
  std::string line;
  char head[160];
  std::snprintf(head,sizeof(head),"{\"present\":%u,\"wall_ms\":%.3f",harness_path_present_,wall);
  line=head;
  if(const RenderProfileSnapshot* snap=profiler_.latest_snapshot();snap&&snap->frame!=harness_path_profile_frame_){
   harness_path_profile_frame_=snap->frame;
   char text[160];
   std::snprintf(text,sizeof(text),",\"profile_frame\":%llu,\"cpu_ms\":%.3f,\"gpu_ms\":%.3f,\"gpu_valid\":%s,\"passes\":[",
    static_cast<unsigned long long>(snap->frame),snap->frame_cpu_ms,snap->frame_gpu_ms,snap->gpu_valid?"true":"false");
   line+=text;
   // The eight passes with the most GPU time (CPU time where none resolved).
   std::array<UINT,8> top{};UINT count=0;
   for(UINT n=0;n<snap->pass_count&&n<snap->passes.size();++n){
    const auto key=[&](UINT k){return snap->passes[k].gpu_ms>=0?snap->passes[k].gpu_ms:snap->passes[k].cpu_ms;};
    UINT at=count<top.size()?count++:UINT(top.size());
    if(at==top.size()){if(key(n)<=key(top[top.size()-1]))continue;at=UINT(top.size()-1);}
    top[at]=n;
    while(at>0&&key(top[at])>key(top[at-1])){std::swap(top[at],top[at-1]);--at;}
   }
   for(UINT k=0;k<count;++k){
    const auto& pass=snap->passes[top[k]];
    std::snprintf(text,sizeof(text),"%s{\"name\":\"%s\",\"cpu_ms\":%.3f,\"gpu_ms\":%.3f}",k?",":"",pass.name.data(),pass.cpu_ms,pass.gpu_ms);
    line+=text;
   }
   line+="]";
   // Frame accounting: every pass name with its summed CPU and GPU time
   // and counters [cpu_ms,gpu_ms,draws,triangles] (names repeat), so a whole frame's pass budget is in the row.
   {
    std::vector<std::pair<std::string,std::array<double,4>>> sums;
    for(UINT n=0;n<snap->pass_count&&n<snap->passes.size();++n){
     const auto& pass=snap->passes[n];
     const std::string name(pass.name.data());
     auto found=std::find_if(sums.begin(),sums.end(),[&](const auto& entry){return entry.first==name;});
     if(found==sums.end()){sums.push_back({name,{0.0,0.0,0.0,0.0}});found=sums.end()-1;}
     found->second[0]+=pass.cpu_ms;found->second[1]+=(std::max)(0.0,double(pass.gpu_ms));
     found->second[2]+=double(pass.counters.draws);found->second[3]+=double(pass.counters.triangles);
    }
    line+=",\"pass_sums\":{";
    for(std::size_t k=0;k<sums.size();++k){
     std::snprintf(text,sizeof(text),"%s\"%s\":[%.3f,%.3f,%.0f,%.0f]",k?",":"",sums[k].first.c_str(),sums[k].second[0],sums[k].second[1],sums[k].second[2],sums[k].second[3]);
     line+=text;
    }
    line+="}";
   }
   for(UINT k=0;k<snap->cpu_scope_count;++k){
    std::snprintf(text,sizeof(text),",\"%s_cpu_ms\":%.3f,\"%s_calls\":%llu",snap->cpu_scopes[k].name.data(),snap->cpu_scopes[k].cpu_ms,
     snap->cpu_scopes[k].name.data(),static_cast<unsigned long long>(snap->cpu_scopes[k].calls));
    line+=text;
   }
  }
  {// Mesh cache traffic since the previous line (camera-motion churn).
   const std::array<std::uint64_t,4> now_counters{perf_native_mesh_misses_,perf_native_mesh_content_misses_,
    perf_native_mesh_world_misses_,perf_native_mesh_evictions_};
   char text[256];
   std::snprintf(text,sizeof(text),",\"mesh_misses\":%llu,\"mesh_content_misses\":%llu,\"mesh_world_misses\":%llu,"
    "\"mesh_evictions\":%llu,\"meshes\":%zu,\"geometry_mb\":%.1f",
    static_cast<unsigned long long>(now_counters[0]-harness_path_cache_counters_[0]),
    static_cast<unsigned long long>(now_counters[1]-harness_path_cache_counters_[1]),
    static_cast<unsigned long long>(now_counters[2]-harness_path_cache_counters_[2]),
    static_cast<unsigned long long>(now_counters[3]-harness_path_cache_counters_[3]),
    native_meshes_.size(),double(native_geometry_live_bytes_)/1048576.0);
   if(harness_path_present_>1)line+=text;
   harness_path_cache_counters_=now_counters;
   // Shader specialisation (post-load cost): the key, resident sets, the set building (-1 none).
   std::snprintf(text,sizeof(text),",\"spec_key\":%u,\"spec_sets\":%zu,\"spec_building\":%lld",
    native_spec_current_key_,native_spec_sets_.size(),native_spec_jobs_.empty()?-1ll:static_cast<long long>(native_spec_jobs_.front()->key));
   line+=text;
  }
  // The row is completed with this frame's accounting at the end of
  // present_frame (finish_harness_path_row).
  if(!harness_path_line_.empty())try{harness_path_rows_+=harness_path_line_+"}\n";}catch(...){}
  harness_path_line_=std::move(line);
  return HarnessRequest::none; // the harness retires it after its last step
#endif
 }
 case HarnessVerb::wait_ready:{
  if(inputs?inputs->harness_on_travel_map:harness_on_travel_map()){finish(false,"game is on the world map (no mission); no world to prepare");return HarnessRequest::none;}
  if(harness_stage_==0){harness_stage_=1;harness_started_ms_=GetTickCount64();}
  const std::string unready=harness_unready_parts(detail);
  if(unready.empty())finish(true,detail);
  else if(harness_timed_out(harness_started_ms_,GetTickCount64(),command->frames))
   finish(false,"not ready after "+std::to_string(command->frames)+" s: "+unready);
  return HarnessRequest::none;
 }
 default:
  finish(false,"not a renderer command");
  return HarnessRequest::none;
 }
}

// wait_ready: the loaded world is prepared when, at this present, the
// retained main scene is published for the current world epoch, this frame
// published a terrain frame, ground cover drew with its map work done (shader
// compile, tile families, every atlas painted, neighbour cover prepared), and
// the neighbour stream has settled. Empty when ready; otherwise each unready
// part with its reason. ready_detail describes the ready state.
std::string Renderer::harness_unready_parts(std::string& ready_detail) noexcept try{
 std::string parts;
 const auto add=[&](const char* part,const char* why){
  if(!parts.empty())parts+="; ";
  parts+=part;parts+=": ";parts+=why;};
 const std::uint64_t epoch=bridge_current_world_epoch();
 const auto identity=retained_static_scene_identity_snapshot();
 if(!epoch)add("statics","no world epoch");
 else if(identity.status!=RetainedSceneIdentityStatus::published)
  add("statics",retained_scene_identity_status_name(identity.status));
 else if(identity.pending_transactions)add("statics","source transactions pending");
 else if(identity.positive_world_epoch!=epoch)add("statics","published scene belongs to an earlier world epoch");
 const auto source=retained_static_terrain_source_snapshot();
 if(!source.map||!source.terrain)add("terrain","no retained terrain source");
 else if(ambient_terrain_frame_!=native_frame_index_)add("terrain","no terrain frame published this frame");
 const int cover=configured_integer(debug_overlay_.config(),"ground_cover",2);
 if(cover>0){
  if(gc_failed_)add("ground_cover","omitted after a failure (see probe log)");
  else if(gc_drawn_frame_!=native_frame_index_)add("ground_cover",gc_skip_reason_?gc_skip_reason_:"not drawn this frame");
  else if(gc_skip_reason_)add("ground_cover",gc_skip_reason_);
  else if(gc_map_generation_!=ambient_map_generation_||gc_sand_generation_!=ambient_map_generation_)
   add("ground_cover","tile families still loading");
  else if(!gc_material_state_.empty())add("ground_cover",gc_paint_failure_?gc_paint_failure_:"terrain atlases still painting");
  else for(const auto& n:gc_neighbours_)
   if(!n.prepared||n.families.valid()){add("ground_cover","neighbour cover still preparing");break;}
 }
 const auto* option=debug_overlay_.config().find("neighbour_maps");
 const bool neighbours=option&&option->boolean_value;
 if(neighbours&&source.map){
  if(neighbours_.epoch!=source.map->generation)add("neighbours","stream not started for this map");
  else if(!neighbour_client_settled(neighbours_.epoch))add("neighbours","worker still streaming");
 }
 char text[320]{};
 std::snprintf(text,sizeof(text),"world_epoch=%llu map=%s frame=%llu ground_cover=%d neighbours=%s accepted=%zu",
  static_cast<unsigned long long>(epoch),identity.main_outer_resource.c_str(),
  static_cast<unsigned long long>(native_frame_index_),cover,neighbours?"on":"off",neighbours_.accepted.size());
 ready_detail=text;
 return parts;
}catch(...){return "harness: allocation failed";}

// Load reveal: what still pops in after a load, or nullptr when the world is
// ready to show. The wait_ready parts, but ground cover waits only for
// preparation that completes by itself (shaders, grids, tile families, atlas
// painting while atlases keep arriving, neighbour cover): a camera showing no
// ground or a map without cover never holds the screen.
// The retained statics published for this epoch and this frame's terrain.
const char* Renderer::load_reveal_world_wait() noexcept{
 const std::uint64_t epoch=bridge_current_world_epoch();
 const auto identity=retained_static_scene_identity_snapshot();
 if(identity.status!=RetainedSceneIdentityStatus::published)return "statics loading";
 if(identity.pending_transactions)return "statics: source transactions pending";
 if(identity.positive_world_epoch!=epoch)return "statics: earlier world";
 const auto source=retained_static_terrain_source_snapshot();
 if(!source.map||!source.terrain)return "terrain source";
 if(ambient_terrain_frame_!=native_frame_index_)return "terrain frame";
 return nullptr;
}

// Ground cover preparation that completes by itself.
const char* Renderer::load_reveal_ground_cover_wait() noexcept{
 if(configured_integer(debug_overlay_.config(),"ground_cover",2)<=0||gc_failed_)return nullptr;
 if(gc_skip_reason_){
  for(const char* transient:{"shaders still compiling","no terrain frame published this frame",
   "shared area/height grid unavailable","terrain atlases not painted yet"})
   if(std::strcmp(gc_skip_reason_,transient)==0)return gc_skip_reason_;
 }else if(gc_drawn_frame_==native_frame_index_){
  if(gc_map_generation_!=ambient_map_generation_||gc_sand_generation_!=ambient_map_generation_)
   return "ground cover tile families";
  // Atlases arrive as the game draws its terrain; wait while they still do.
  if(!gc_material_state_.empty()&&gc_painted_frame_+3>native_frame_index_)return "ground cover atlases painting";
  // D3D12: neighbours keep only their blend maps (no cover pass to prepare).
  for(const auto& n:gc_neighbours_)if((!d3d12_&&!n.prepared)||n.families.valid())return "neighbour ground cover";
 }
 return nullptr;
}

const char* Renderer::load_reveal_waiting_on(LoadRevealWait& kind) noexcept try{
 kind=LoadRevealWait::other;
 if(const char* world=load_reveal_world_wait())return world;
 const auto source=retained_static_terrain_source_snapshot();
 // An empty/disabled stream is already settled even when no colour pass
 // adopted a neighbour. Renderer adoption is not evidence of pending work.
 if(configured_boolean(debug_overlay_.config(),"neighbour_maps",true)&&
    !neighbour_client_settled(source.map->generation)){
  kind=LoadRevealWait::neighbours;
  return neighbours_.epoch!=source.map->generation?"neighbours not started":"neighbours streaming";
 }
 // Critter perches are chosen once per tree model, 20-50 ms each; under a
 // held picture they are chosen now (30 ms of each held frame) instead of
 // in the first seconds of play. Main-thread work: before the wait for the
 // worker-built shader sets, so a hold capped on those still does it.
#ifndef EID3D11_EMBEDDED
 if(load_reveal_holding_&&(configured_integer(debug_overlay_.config(),"critters",2)>0||
    configured_integer(debug_overlay_.config(),"critter_showcase",0)>0)&&
    !(load_reveal_perches_ready_=bridge_prepare_critter_perch_models(30000)))return "critter perches";
#endif
 // The general programs serve a key until its specialised sets are built
 // (driver compile on worker threads, 1.2-2.7 s a set with the driver's
 // disk cache off); revealing a mission before that leaves seconds of
 // heavier frames. The general programs are exact, so the menu's 3D
 // background (same key as play) shows at once and the sets keep building.
 if(bridge_local_single_player_mission()&&!native_specialisation_settled())return "shader specialisation";
 if(const char* cover=load_reveal_ground_cover_wait())return cover;
 // The driver has compiled every combination a later first draw would
 // (pump_pipeline_warmup runs each held frame, alongside the waits above).
 if(load_reveal_holding_&&!warmup_done_)return "pipeline warm-up";
 return nullptr;
}catch(...){return nullptr;}

// The hold's real progress: per component the work units still outstanding
// against the most seen outstanding during this hold (units: neighbours not
// uploaded, specialised programs not created, ground cover programs not
// loaded, warm-up draws not issued; the other waits count as one unit while
// they wait). Components that never waited during this hold do not count.
// 1 means every component has finished. Called after load_reveal_waiting_on.
float Renderer::load_reveal_progress() noexcept try{
 std::array<std::size_t,6> remaining{};
 remaining[0]=load_reveal_world_wait()?1:0;
 const auto source=retained_static_terrain_source_snapshot();
 if(configured_boolean(debug_overlay_.config(),"neighbour_maps",true)&&source.map){
  std::size_t left=0;
  if(!neighbour_client_remaining(source.map->generation,left))left=(std::max<std::size_t>)(left,1);
  remaining[1]=left;
 }
 remaining[2]=load_reveal_holding_&&(configured_integer(debug_overlay_.config(),"critters",2)>0||
  configured_integer(debug_overlay_.config(),"critter_showcase",0)>0)&&!load_reveal_perches_ready_?1:0;
 if(bridge_local_single_player_mission()&&!native_specialisation_settled())
  remaining[3]=(std::max<std::size_t>)(native_specialisation_programs_remaining(),1);
 if(load_reveal_ground_cover_wait()){
  std::size_t left=1; // the pipeline objects, or the other cover steps
  if(!gc_place_cs_&&gc_compile_done_&&gc_compile_total_>gc_compile_done_->load())left+=gc_compile_total_-gc_compile_done_->load();
  remaining[4]=left;
 }
 if(load_reveal_holding_&&!warmup_done_)
  remaining[5]=1+(warmup_items_>warmup_next_?warmup_items_-warmup_next_:0)+(warmup_items_?0:1);
 float sum=0;unsigned counted=0;
 for(std::size_t n=0;n<remaining.size();++n){
  auto& peak=load_reveal_progress_peak_[n];
  peak=(std::max)(peak,remaining[n]);
  if(!peak)continue;
  sum+=1.f-static_cast<float>(remaining[n])/static_cast<float>(peak);++counted;
 }
 return counted?sum/static_cast<float>(counted):1.f;
}catch(...){return 0.f;}

// Used and largest free user address space in MB (the 32-bit game's scarce
// domain). The walk over every region is done once per load, and at most
// once a second while the F10 memory line is shown (debug_overlay.cpp).
void address_space_mb(std::uint64_t& used,std::uint64_t& largest_free,bool walk) noexcept {
 MEMORYSTATUSEX status{};status.dwLength=sizeof(status);
 used=GlobalMemoryStatusEx(&status)?(status.ullTotalVirtual-status.ullAvailVirtual)>>20:0;
 largest_free=0;if(!walk)return;
 SYSTEM_INFO info{};GetSystemInfo(&info);
 auto* address=static_cast<std::uint8_t*>(info.lpMinimumApplicationAddress);
 MEMORY_BASIC_INFORMATION region{};
 while(address<info.lpMaximumApplicationAddress&&VirtualQuery(address,&region,sizeof(region))==sizeof(region)){
  if(region.State==MEM_FREE)largest_free=(std::max<std::uint64_t>)(largest_free,region.RegionSize);
  address=static_cast<std::uint8_t*>(region.BaseAddress)+region.RegionSize;
 }
 largest_free>>=20;
}
namespace {

// Temporary (VA diet, 2026-10-12): who holds the address space at a load,
// one va_regions line per load_reveal. Reserved and committed bytes by
// region type; mapped files grouped by allocation and named (the NVIDIA
// disk shader cache .nvph, the game .res archives, others); the largest
// private allocations (driver and heap reservations). Remove once the VA
// owners are settled.
#ifndef EID3D11_EMBEDDED
void report_address_space_regions() noexcept try{
 struct Allocation{std::uint64_t bytes=0;const void* base=nullptr;wchar_t name[48]{};};
 constexpr std::size_t kListed=4; // lines listed per class (report width only)
 Allocation mapped_top[kListed]{},private_top[kListed]{};
 const auto keep=[](Allocation (&top)[kListed],const Allocation& a){
  for(std::size_t n=0;n<kListed;++n)if(a.bytes>top[n].bytes){
   for(std::size_t m=kListed-1;m>n;--m)top[m]=top[m-1];
   top[n]=a;return;
  }
 };
 std::uint64_t image=0,mapped=0,nvph=0,res=0,private_commit=0,private_reserve=0,free_bytes=0;
 unsigned mapped_count=0,private_count=0;
 SYSTEM_INFO info{};GetSystemInfo(&info);
 auto* address=static_cast<std::uint8_t*>(info.lpMinimumApplicationAddress);
 MEMORY_BASIC_INFORMATION region{};
 Allocation current{};DWORD current_type=0;
 const auto finish=[&]{
  if(!current.base)return;
  if(current_type==MEM_MAPPED){
   ++mapped_count;
   wchar_t path[MAX_PATH]{};
   if(GetMappedFileNameW(GetCurrentProcess(),const_cast<void*>(current.base),path,MAX_PATH)){
    const wchar_t* slash=std::wcsrchr(path,L'\\');const wchar_t* name=slash?slash+1:path;
    wcsncpy_s(current.name,name,_TRUNCATE);
    const std::size_t length=std::wcslen(name);
    const auto ends=[&](const wchar_t* suffix){const std::size_t s=std::wcslen(suffix);return length>=s&&_wcsicmp(name+length-s,suffix)==0;};
    if(ends(L".nvph"))nvph+=current.bytes;else if(ends(L".res"))res+=current.bytes;
   }
   keep(mapped_top,current);
  }else if(current_type==MEM_PRIVATE){++private_count;keep(private_top,current);}
  current={};
 };
 while(address<info.lpMaximumApplicationAddress&&VirtualQuery(address,&region,sizeof(region))==sizeof(region)){
  const std::uint64_t size=region.RegionSize;
  if(region.State==MEM_FREE){finish();free_bytes+=size;}
  else{
   if(region.AllocationBase!=current.base){finish();current.base=region.AllocationBase;current_type=region.Type;}
   current.bytes+=size;
   if(region.Type==MEM_IMAGE)image+=size;
   else if(region.Type==MEM_MAPPED)mapped+=size;
   else if(region.State==MEM_COMMIT)private_commit+=size;else private_reserve+=size;
  }
  address=static_cast<std::uint8_t*>(region.BaseAddress)+region.RegionSize;
 }
 finish();
 std::string text;char part[160];
 std::snprintf(part,sizeof(part),"image_mb=%llu mapped_mb=%llu (nvph=%llu res=%llu allocations=%u) private_commit_mb=%llu private_reserve_mb=%llu (allocations=%u) free_mb=%llu",
  static_cast<unsigned long long>(image>>20),static_cast<unsigned long long>(mapped>>20),static_cast<unsigned long long>(nvph>>20),
  static_cast<unsigned long long>(res>>20),mapped_count,static_cast<unsigned long long>(private_commit>>20),
  static_cast<unsigned long long>(private_reserve>>20),private_count,static_cast<unsigned long long>(free_bytes>>20));
 text=part;
 text+=" mapped_top=";
 for(const auto& a:mapped_top)if(a.bytes){std::snprintf(part,sizeof(part),"%ls:%llu,",a.name[0]?a.name:L"?",static_cast<unsigned long long>(a.bytes>>20));text+=part;}
 text+=" private_top=";
 for(const auto& a:private_top)if(a.bytes){std::snprintf(part,sizeof(part),"%p:%llu,",a.base,static_cast<unsigned long long>(a.bytes>>20));text+=part;}
 // Whose heap holds the private commit: the process heap is the
 // renderer's (static UCRT) shared with Windows and the driver; the other
 // heap over 4 MB is game.exe's own CRT heap (a heap walk found the game's
 // figure objects there, 2026-10-13). committed/reserved MB per heap.
 {HANDLE heaps[64];const DWORD count=GetProcessHeaps(64,heaps);const HANDLE process_heap=GetProcessHeap();
  text+=" heaps=";
  for(DWORD n=0;n<count&&n<64;++n){
   HEAP_SUMMARY summary{};summary.cb=sizeof(summary);
   if(!HeapSummary(heaps[n],0,&summary)||summary.cbCommitted<(4u<<20))continue;
   std::snprintf(part,sizeof(part),"%s%p:%llu/%llu,",heaps[n]==process_heap?"process@":"",heaps[n],
    static_cast<unsigned long long>(summary.cbCommitted>>20),static_cast<unsigned long long>(summary.cbReserved>>20));
   text+=part;
  }}
 bridge_report_event("va_regions",text.c_str());
}catch(...){}
#else
void report_address_space_regions() noexcept {  }
#endif
}  // namespace

// Core mode, game process: one core_game_va line (used and largest free) and
// the same va_regions line per load (renderer_game.cpp core_present).
void report_game_address_space(std::uint64_t world_epoch) noexcept try{
 std::uint64_t used=0,largest=0;address_space_mb(used,largest,true);
 char text[128];std::snprintf(text,sizeof(text),"world_epoch=%llu va_used_mb=%llu va_largest_free_mb=%llu",
  static_cast<unsigned long long>(world_epoch),static_cast<unsigned long long>(used),static_cast<unsigned long long>(largest));
 bridge_report_event("core_game_va",text);
 report_address_space_regions();
}catch(...){}

// Upload buffers and staging grow to the largest single frame's demand (a
// load-reveal flush of every neighbour and morph) and never shrank: the
// largest map's peak stayed allocated on the main menu. A new world epoch
// drops them; the next flush recreates each at that world's demand (every
// use goes through its ensure step, which creates on a null buffer).
void Renderer::release_world_upload_peaks() noexcept{
 // These buffers hold only a flush's uploads. Their next use ensures their
 // size again; D3D12 releases remain deferred behind the recording fence.
 native12_.dynamic.reset();native12_.work.reset();native12_.pose_inputs.reset();
 native_dynamic_.Reset();native_dynamic_srv_.Reset();native_dynamic_capacity_=0;
 native_draw_work_.Reset();native_draw_work_srv_.Reset();native_draw_work_capacity_=0;
 native_instance_updates_.Reset();native_instance_update_srv_.Reset();native_instance_update_capacity_=0;
 native_instance_update_ring_.release();native_pose_input_ring_.release();
 native_pose_inputs_.Reset();native_pose_input_srv_.Reset();native_pose_input_capacity_=0;
 dynamic_vertices_.Reset();dynamic_vertex_capacity_=0;dynamic_vertex_offset_=0;
 dynamic_indices_.Reset();dynamic_index_capacity_=0;dynamic_index_offset_=0;
 if(native_pending_upload_.empty())std::vector<std::uint8_t>().swap(native_pending_upload_);
 if(native_instance_updates_staging_.empty())std::vector<NativeInstanceUpdate>().swap(native_instance_updates_staging_);
 if(native_pose_upload_.empty())std::vector<NativePoseGpu>().swap(native_pose_upload_);
}
namespace {

template<class T> void release_storage(T& value) noexcept { T().swap(value); }
// A hash map keeps the bucket array of its largest size after erasure; move
// the survivors into one sized for them.
template<class Map> void compact_map(Map& map) {
 Map kept;kept.reserve(map.size());
 for(auto& entry:map)kept.emplace(entry.first,std::move(entry.second));
 map.swap(kept);
}
}  // namespace

// A world change is a clean slate (user decision 2026-10-13): every
// world-scoped resource and store of the previous world goes here, at the
// first engine frame of the new world epoch (begin_engine_frame, after the
// previous frame's flush: no draw pending). Nothing is kept at the previous
// world's high-water size; each store regrows through its own ensure step at
// the new world's demand. Kept: shader programs and specialisation sets,
// pipeline states, resolution/setting-sized targets (swap chain, post,
// shadow maps, AO, fog volume, sky), UI caches and live client-surface
// textures (the game owns those surfaces; they go with retire_surface).
//
// Handles held outside (bridge plans, retained scene, terrain front end) are
// all generation/identity checked; prepared slots restart above their
// generation floor and native_static_generation_ steps, so every holder
// re-prepares and re-registers. A new map's load already started its own
// neighbour stream (keyed by the new loader generation) and keeps it. The
// stream of the generation whose neighbours are released here is stopped:
// a reload of the same map reuses its main source and loader generation, and
// neighbour_maps_begin would otherwise accept that drained stream as current
// and nothing would be uploaded again. Unless that reload's load start
// already restarted it for this new world (held: neighbour_maps_restart,
// core mode), which this releases instead.
void Renderer::release_world_state() noexcept try{
 // Neighbour draws first: they index the instance records cleared below.
 neighbour_client_world_changed(neighbours_.epoch);
 release_neighbour_maps();
 release_storage(neighbours_.accepted);
 // Prepared handles (invalidate_native_prepared_handles without the stream stop).
 native_static_content_changed();native_shadow_valid_.fill(false);
 for(auto& slot:native_prepared_mesh_slots_){
  native_prepared_mesh_generation_floor_=(std::max)(native_prepared_mesh_generation_floor_,slot.generation);
  slot.mesh=nullptr;slot.active=false;}
 for(auto& slot:native_prepared_material_slots_)
  native_prepared_material_generation_floor_=(std::max)(native_prepared_material_generation_floor_,slot.generation);
 release_storage(native_prepared_mesh_slots_);release_storage(native_prepared_material_slots_);
 native_prepared_mesh_reuse_cursor_=native_prepared_material_reuse_cursor_=0;
 advance_native_queue_epoch();
 // Persistent tables: instance records, static sets and their baked layouts,
 // static depth records, material groups.
 for(auto* set:{&native_static_set_,&native_terrain_set_}){
  release_storage(set->objects);release_storage(set->draws);release_storage(set->owners);release_storage(set->added);set->added_draws=0;set->dirty=false;}
 if(++native_static_generation_==0)++native_static_generation_;
 release_storage(native_static_shadow_records_);
 native_static_shadow_bake_={}; // its records are cleared below
 native_terrain_shadow_bake_={};native_neighbour_shadow_bake_={};release_storage(native_terrain_shadow_records_);
 release_storage(native_instance_records_);release_storage(native_instance_free_);
 release_storage(native_instance_dirty_);release_storage(native_instance_updates_staging_);
 release_storage(native_state_buckets_);release_storage(native_state_lookup_);
 native_material_groups_dirty_=false;release_storage(native_active_groups_);release_storage(native_draw_order_);
 release_storage(native_prepass_batches_);
 for(auto& layout:native_submission_layouts_)layout={};
 native_layout_changed();
 // Poses, skeletons, morphs, terrain phases and their GPU range buffers.
 release_storage(native_pose_work_);release_storage(native_pose_upload_);release_storage(native_pose_slots_);
 release_storage(native_skeleton_assets_);
 release_storage(native_pose_ranges_);release_storage(native_world_ranges_);release_storage(native_animation_ranges_);
 for(auto& free_ranges:native_free_ranges_)release_storage(free_ranges);native_free_ranges_complete_.fill(true);
 release_storage(native_terrain_phases_);release_storage(native_terrain_phase_lookup_);release_storage(native_terrain_phase_free_);release_storage(native_terrain_phase_work_);release_storage(native_terrain_phase_copies_);
 native_world_states_.Reset();native_world_state_srv_.Reset();native_world_state_uav_.Reset();native_world_state_capacity_=0;
 // D3D12: the prepared world states and vertices go with their ranges. The
 // range generations restart, so a kept state of the previous map matched a
 // new range's generation and its record was never prepared (the screen-
 // centre streak: a zero world vertex projects to clip (0,0,0,0)).
 native12_.world_states.reset();native12_.world_vertices.reset();
 native_pose_nodes_.Reset();native_pose_node_srv_.Reset();native_pose_node_uav_.Reset();
 native_pose_node_capacity_=native_pose_node_used_=0;
 native_animation_.Reset();native_animation_srv_.Reset();native_animation_uav_.Reset();
 native_animation_capacity_=0;native_animation_used_=16;
 native_world_vertices_.Reset();native_world_vertex_srv_.Reset();native_world_vertex_uav_.Reset();
 native_world_vertex_capacity_=native_world_vertex_used_=0;native_world_reclaim_pending_=false;
 native_persistent_instances_.Reset();native_persistent_instance_srv_.Reset();native_persistent_instance_uav_.Reset();
 native_draws_.Reset();native_draw_srv_.Reset();native_draw_uav_.Reset();native_persistent_instance_capacity_=0;
 native_world_clip_valid_=false;native_world_clip_dirty_=true;
 // Per-frame working buffers at the previous world's peak.
 native_visible_triangles_.Reset();native_visible_triangle_srv_.Reset();native_visible_triangle_uav_.Reset();
 native_visible_triangle_capacity_=0;
 native_prepared_vertices_.Reset();native_prepared_vertex_srv_.Reset();native_prepared_vertex_uav_.Reset();
 native_prepared_vertex_capacity_=0;
 reset_samgbur_lod_diagnostic();
 native_indirect_args_.Reset();native_indirect_args_uav_.Reset();native_indirect_args_capacity_=0;
 release_world_upload_peaks();
 // Geometry: every mesh and morph, and every page (general and neighbour
 // prebuilt); the arena restarts at its initial 16 bytes.
 native_meshes_.clear();release_storage(native_meshes_);
 if(FAILED(create_native_geometry_arena())){
  native_geometry_pages_.clear();
  bridge_append_probe_event("world_gc_failed","geometry arena could not be recreated; world geometry omitted until the next device");
 }
 dedicated_geometry_stale_=dedicated_geometry_remaining_=false;native_cache_older_owned_meshes_=0;
 // Owned textures: everything but live client-surface copies and camp
 // aliases, with their immutable array slices and pages.
 {std::lock_guard<std::mutex> guard(native_owned_textures_lock_);
 for(auto entry=native_owned_textures_.begin();entry!=native_owned_textures_.end();){
  const auto& value=entry->second;
  if(value.camp_surface_alias||(value.surface_id&&!value.detached_copy)){++entry;continue;}
  drop_native_texture_array_slots(value.backing,true);
  entry=native_owned_textures_.erase(entry);
 }}
 neighbour_textures_clear();
 terrain_textures_d3d12_.clear();terrain_pages_d3d12_={}; // D3D12 backend: the terrain pages of the released world
 {std::lock_guard<std::mutex> guard(native_owned_textures_lock_);compact_map(native_owned_textures_);}compact_map(native_texture_arrays_);
 // Impostor atlas (the bake pipeline and its fixed depth target stay).
 impostors_.albedo.Reset();impostors_.normal.Reset();impostors_.albedo_srv.Reset();impostors_.normal_srv.Reset();
 impostors_.albedo12.reset();impostors_.normal12.reset(); // D3D12 backend (the bake targets stay)
 impostors_.capacity=impostors_.count=0;release_storage(impostors_.slices);
 // Shadows: static layers and point atlases belong to the world; the maps
 // themselves are sized by settings and stay.
 native_shadow_static_valid_.fill(false);native_shadow_frame_={};native_shadow_dirty_mask_=native_shadow_cleared_mask_=0;
 native_shadow_constants_dirty_=true;shadow_history_valid_=false;
 if(context_&&!d3d12_){std::array<ID3D11ShaderResourceView*,PointShadowTextureLayout::array_bindings> empty{};
  context_->PSSetShaderResources(PointShadowTextureLayout::first_srv,static_cast<UINT>(empty.size()),empty.data());}
 release_storage(native_point_shadow_pages_);native_point_shadow_generation_=0;
 native_point_shadow_active_=false;native_point_shadow_frame_={};
 // Point lights and their grid.
 native_point_light_buffer_.Reset();native_point_light_srv_.Reset();native_point_light_capacity_=0;
 native_point_light_grid_buffer_.Reset();native_point_light_grid_srv_.Reset();native_point_light_grid_capacity_=0;
 native_point_light_grid_={};release_storage(native_point_lights_);release_storage(native_point_light_inputs_);
 release_storage(native_point_light_grid_inputs_);native_point_lights_dirty_=true;
 // Subsystems keyed by map: shared grids (and everything keyed by them),
 // ground cover pools, critter director state.
 release_ambient_world();
 release_ground_cover_world();
 release_critters_world();
 // Outside the renderer: bridge world caches, expired identity-index entries
 // and the retained scene's frame scratch.
 bridge_release_world_caches();
 owned_resource_runtime_release_world_bookkeeping();
 // The mesh cache's own epoch bookkeeping: nothing older is left to sweep.
 native_cache_world_epoch_=bridge_current_world_epoch();native_cache_epoch_frame_=native_frame_index_;
 native_cache_epoch_sweep_pending_=native_texture_epoch_sweep_pending_=false;
 native_texture_epoch_queue_=native_queue_epoch_;
 // Give the memory back: unbind everything (bindings hold references), let
 // the driver destroy the released objects, trim its internal allocations,
 // and decommit free process-heap pages.
 if(context_&&!d3d12_){context_->ClearState();camp_unclipped_raster_=nullptr;context_->Flush();} // D3D12: the adopted device has no state
 if(device_){ComPtr<IDXGIDevice3> dxgi;if(SUCCEEDED(device_.As(&dxgi)))dxgi->Trim();}
 HeapCompact(GetProcessHeap(),0);
 _heapmin();
}catch(...){
 bridge_append_probe_event("world_gc_failed","world release threw; the next world rebuilds what remains");
}

// Called once per present, just before Present. A frame that showed a
// full-screen picture (loading screens, movies) leaves a GPU copy of itself;
// world frames of a new epoch show that copy instead until the world is
// ready. One load_reveal line per load gives its timing and address space.
void Renderer::apply_load_reveal() noexcept try{
 // D3D12: the same hold; the picture is kept and shown through the executor
 // (capture into load_picture12_, a full-screen interface draw back).
 if(!d3d12_&&(!swap_||!context_||!device_))return;
 const std::uint64_t draws=bridge_world_draw_completion_counter();
 const bool world_frame=draws!=load_reveal_draws_;load_reveal_draws_=draws;
 const bool picture=load_picture_this_frame_;load_picture_this_frame_=false;
 const bool fitted=load_picture_fitted_;load_picture_fitted_=false;
 const bool enabled=configured_boolean(debug_overlay_.config(),"load_reveal_hold",true);
 // The world Draw entry pauses the game while an epoch is not yet revealed.
 bridge_set_load_reveal(enabled,load_reveal_revealed_epoch_);
 ComPtr<ID3D11Texture2D> back;
 D3D11_TEXTURE2D_DESC back_desc{};
 if(!d3d12_){
  if(FAILED(swap_->GetBuffer(0,IID_PPV_ARGS(&back))))return;
  back->GetDesc(&back_desc);
 }
 const auto picture_matches=[&]{
  if(d3d12_)return load_picture12_&&d3d12_->texture_matches(*load_picture12_,output_width_,output_height_,DXGI_FORMAT_B8G8R8A8_UNORM);
  if(!load_picture_)return false;
  D3D11_TEXTURE2D_DESC kept{};load_picture_->GetDesc(&kept);
  return kept.Width==back_desc.Width&&kept.Height==back_desc.Height&&kept.Format==back_desc.Format;
 };
 if(picture&&!world_frame){
  if(!enabled)return;
  if(d3d12_){
   if(!picture_matches()){
    load_picture12_.reset();
    if(FAILED(d3d12_->create_texture(output_width_,output_height_,DXGI_FORMAT_B8G8R8A8_UNORM,load_picture12_))){load_picture12_.reset();return;}
   }
   if(FAILED(d3d12_->capture(*load_picture12_,0,0,RECT{0,0,LONG(output_width_),LONG(output_height_)})))load_picture12_.reset();
   return;
  }
  if(!picture_matches()){
   D3D11_TEXTURE2D_DESC desc=back_desc;desc.BindFlags=0;desc.CPUAccessFlags=0;desc.MiscFlags=0;
   desc.Usage=D3D11_USAGE_DEFAULT;desc.MipLevels=1;desc.ArraySize=1;desc.SampleDesc={1,0};
   load_picture_.Reset();if(FAILED(device_->CreateTexture2D(&desc,nullptr,&load_picture_)))return;
  }
  context_->CopyResource(load_picture_.Get(),back.Get());
  capture_load_progress_movie(fitted); // typed host source or verified retail owner
  return;
 }
 const std::uint64_t epoch=bridge_current_world_epoch();
 if(epoch&&epoch!=load_reveal_epoch_){
  load_reveal_epoch_=epoch;load_reveal_tracking_=true;load_reveal_holding_=enabled&&picture_matches();
  bridge_set_load_reveal(load_reveal_holding_,load_reveal_revealed_epoch_);
  load_reveal_first_world_=load_reveal_last_world_=0;load_reveal_held_ms_=load_reveal_neighbour_held_ms_=0;
  load_reveal_frames_=0;load_reveal_va_peak_=0;load_reveal_waits_={};
  load_reveal_progress_peak_={};load_reveal_perches_ready_=false;
  (void)take_load_cursor_presents(); // counted from this hold on
  reset_pipeline_warmup();
  // D3D12: no D3D11 warm-up draws; pipelines build ahead on the executor's
  // workers from its pipeline library, so the hold does not wait for them.
  if(d3d12_)warmup_done_=true;
 }
 if(!load_reveal_tracking_){if(world_frame){load_picture_.Reset();load_picture12_.reset();load_progress_movie_={};load_progress_movie_valid_=false;cancel_load_progress_decode();}return;}
 std::uint64_t used=0,largest=0;address_space_mb(used,largest,false);
 load_reveal_va_peak_=(std::max)(load_reveal_va_peak_,used);
 if(!world_frame)return; // menus, movies and loading pictures present as they are
 const std::int64_t now=perf_now();
 if(!load_reveal_first_world_)load_reveal_first_world_=load_reveal_last_world_=now;
 const double ms_per_tick=1000.0/static_cast<double>(perf_frequency());
 const double prepared_ms=static_cast<double>(now-load_reveal_first_world_)*ms_per_tick;
 // The stuck-load caps count held time, each world frame adding at most
 // 250 ms: one slow frame (pipeline creation on a cold driver shader cache,
 // a weak PC) took the whole 3 s and revealed with neighbours still streaming.
 // Frames held for the neighbour stream count against their own 10 s cap
 // (its below-normal decoders lose a contended CPU: lod_ms 1741 against 182
 // for the same map in one run), every other wait against 3 s.
 constexpr double kCapMs=3000.0,kNeighbourCapMs=10000.0,kCapFrameMs=250.0;
 const double frame_ms=(std::min)(kCapFrameMs,static_cast<double>(now-load_reveal_last_world_)*ms_per_tick);
 load_reveal_last_world_=now;
 if(!d3d12_&&load_reveal_holding_&&(load_reveal_held_ms_<kCapMs||load_reveal_neighbour_held_ms_<kNeighbourCapMs))
  pump_pipeline_warmup();
 LoadRevealWait kind=LoadRevealWait::other;
 const char* waiting=load_reveal_waiting_on(kind);
 const bool neighbour_wait=waiting&&kind==LoadRevealWait::neighbours;
 (neighbour_wait?load_reveal_neighbour_held_ms_:load_reveal_held_ms_)+=frame_ms;
 if(waiting&&(neighbour_wait?load_reveal_neighbour_held_ms_<kNeighbourCapMs:load_reveal_held_ms_<kCapMs)){
  for(auto& entry:load_reveal_waits_){
   if(entry.first==waiting){++entry.second;break;}
   if(!entry.first){entry={waiting,1};break;}
  }
  if(load_reveal_holding_){
   ++load_reveal_frames_;
   if(d3d12_){
    // The held picture over the whole frame (opaque), else black. The
    // progress movie's later frames are not drawn on this backend (named).
    if(picture_matches()){
     const float w=float(output_width_),h=float(output_height_);
     const Vertex quad[4]={{0,0,0,1,0xffffffffu,0xffffffffu,0,0},{w,0,0,1,0xffffffffu,0xffffffffu,1,0},
                           {0,h,0,1,0xffffffffu,0xffffffffu,0,1},{w,h,0,1,0xffffffffu,0xffffffffu,1,1}};
     Constants block{};block.viewport[2]=w;block.viewport[3]=h;
     block.texture_factor[0]=block.texture_factor[1]=block.texture_factor[2]=block.texture_factor[3]=1.0f;
     block.transformed=TRUE;block.textured=TRUE;
     block.color_op=D3DTOP_SELECTARG1;block.color_arg1=D3DTA_TEXTURE;block.color_arg2=D3DTA_CURRENT;
     block.alpha_op=D3DTOP_SELECTARG1;block.alpha_arg1=D3DTA_TEXTURE;block.alpha_arg2=D3DTA_CURRENT;
     block.alpha_func=D3DCMP_ALWAYS;block.hud_opacity=1.0f;
     D3D12UiDraw draw;draw.texture=load_picture12_.get();draw.program=D3D12UiProgram::game;
     draw.blend=fixed_blend_choice(FALSE,D3DBLEND_ONE,D3DBLEND_ZERO,false);
     draw.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
     draw.sampler.AddressU=draw.sampler.AddressV=draw.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
     draw.sampler.MaxLOD=D3D11_FLOAT32_MAX;
     draw.first_vertex=0;draw.vertex_count=4;
     D3D12UiPass ui;
     ui.vertices=quad;ui.vertex_bytes=sizeof(quad);ui.vertex_stride=sizeof(Vertex);
     ui.constants=&block;ui.constant_block_bytes=sizeof(Constants);ui.constant_blocks=1;
     ui.draws=&draw;ui.draw_count=1;
     (void)d3d12_->draw_ui(ui);
     if(load_progress_movie_valid_)d3d12_->omit("load progress movie frames during the hold (the held picture shows)",false);
    }else{
     const float black[4]={0,0,0,1};
     const RECT whole{0,0,LONG(output_width_),LONG(output_height_)};
     d3d12_->clear_rectangles(&whole,1,black,false,false,1.f,0);
    }
    frame_statistics_.reset();
    return;
   }
   if(picture_matches()){
    context_->CopyResource(back.Get(),load_picture_.Get());
    // The progress movie's later frames follow the hold's real progress.
    if(load_progress_movie_valid_)draw_load_progress(load_reveal_progress());
   }else{
    ComPtr<ID3D11RenderTargetView> target;
    if(SUCCEEDED(device_->CreateRenderTargetView(back.Get(),nullptr,&target))){
     const float black[4]={0,0,0,1};context_->ClearRenderTargetView(target.Get(),black);
    }
   }
   frame_statistics_.reset();
  }
  return;
 }
 // Ready (or capped): this frame shows the world.
 address_space_mb(used,largest,true);
 load_reveal_va_peak_=(std::max)(load_reveal_va_peak_,used);
 const MainLoadTiming timing=retained_static_main_load_timing();
 const double load_ms=timing.begin_qpc&&load_reveal_first_world_>timing.begin_qpc?
  static_cast<double>(load_reveal_first_world_-timing.begin_qpc)*ms_per_tick:-1.0;
 const auto identity=retained_static_scene_identity_snapshot();
 std::string waits;
 for(const auto& entry:load_reveal_waits_)if(entry.first){
  if(!waits.empty())waits+=',';
  waits+=entry.first;waits+=':';waits+=std::to_string(entry.second);
 }
 if(waits.empty())waits="none";
 char text[896];
 // The progress movie during the hold: its held frame, frame count and the
 // frames advanced past the held one (none: no progress movie behind the hold).
 char progress[96]="none";
 if(load_progress_movie_valid_)
  std::snprintf(progress,sizeof(progress),"shown=%u/%u advanced=%.2f%s",load_progress_movie_.shown,load_progress_movie_.frames,
   static_cast<double>(load_progress_movie_.position),(!load_progress_movie_.source && load_progress_movie_.after.empty())?" (not decoded)":"");
 std::snprintf(text,sizeof(text),
  "waited=%s result=%s hold=%s map=%s world_epoch=%llu game_load_ms=%.0f owned_decode_wait_ms=%.0f prepare_ms=%.0f held_ms=%.0f neighbour_held_ms=%.0f held_frames=%u waiting_on=%s neighbours=%zu va_used_mb=%llu va_peak_mb=%llu va_largest_free_mb=%llu spec_sets=%zu spec_resident=%zu spec_building=%d warm_draws=%u load_progress=%s held_present_hz=%.1f load_cursor_presents=%u",
  waits.c_str(),waiting?"capped":"ready",load_reveal_holding_?"on":"off",identity.main_outer_resource.c_str(),
  static_cast<unsigned long long>(epoch),load_ms,static_cast<double>(timing.owned_wait_qpc)*ms_per_tick,prepared_ms,load_reveal_held_ms_,load_reveal_neighbour_held_ms_,
  load_reveal_frames_,waiting?waiting:"none",neighbours_.accepted.size(),
  static_cast<unsigned long long>(used),static_cast<unsigned long long>(load_reveal_va_peak_),static_cast<unsigned long long>(largest),
  native_spec_sets_.size(),static_cast<std::size_t>(std::count_if(native_spec_sets_.begin(),native_spec_sets_.end(),[](const auto& set){return set.second[0]!=nullptr;})),static_cast<int>(native_spec_jobs_.size()),warmup_draws_,progress,
  prepared_ms>0.0?1000.0*load_reveal_frames_/prepared_ms:0.0,take_load_cursor_presents());
 bridge_report_event("load_reveal",text);
 report_address_space_regions();
 load_reveal_tracking_=load_reveal_holding_=false;load_picture_.Reset();load_picture12_.reset();
 load_progress_movie_={};load_progress_movie_valid_=false;cancel_load_progress_decode();
 load_reveal_revealed_epoch_=epoch;bridge_set_load_reveal(enabled,epoch);
}catch(...){
 load_reveal_tracking_=load_reveal_holding_=false;load_picture_.Reset();load_picture12_.reset();
 load_progress_movie_={};load_progress_movie_valid_=false;cancel_load_progress_decode();
 load_reveal_revealed_epoch_=bridge_current_world_epoch();bridge_set_load_reveal(false,load_reveal_revealed_epoch_);
}

// Sets an F10 option for this session only (nothing is saved). Options that
// the renderer reads once at device creation take effect on the next launch.
bool Renderer::apply_harness_config(const HarnessCommand& command,std::string& detail) noexcept {
 RendererConfig& config=debug_overlay_.config();
 const char* key=command.name.c_str();
 const char* value=command.value.c_str();
 if(command.name=="overlay"){
  DebugOverlayMode mode{};
  if(!parse_debug_overlay_mode(value,mode)){detail="overlay: hidden|compact|full";return false;}
  config.set_overlay_mode(mode);detail="overlay set";return true;
 }
 if(command.name=="graphics_preset"){
  static constexpr const char* names[]={"low","medium","high","ultra"};
  for(int p=0;p<4;++p) if(command.value==names[p]){
   const std::size_t changed=apply_graphics_preset(config,static_cast<GraphicsPreset>(p));
   detail="graphics_preset "+command.value+" applied ("+std::to_string(changed)+" options changed)";return true;
  }
  detail="graphics_preset: low|medium|high|ultra";return false;
 }
 if(command.name=="debug_view"){
  DebugResourceView view{};
  if(!parse_debug_resource_view(value,view)){detail="unknown debug_view";return false;}
  config.set_debug_resource_view(view);detail="debug_view set";return true;
 }
 const RendererOption* option=config.find(key);
 if(!option){detail="unknown option";return false;}
 bool ok=false;
 char* end=nullptr;
 switch(option->type){
 case RendererOptionType::boolean:{
  const bool on=command.value=="1"||command.value=="on"||command.value=="true";
  const bool off=command.value=="0"||command.value=="off"||command.value=="false";
  ok=(on||off)&&config.set_boolean(key,on);
  break;
 }
 case RendererOptionType::integer:{
  const long number=std::strtol(value,&end,10);
  ok=end&&!*end&&config.set_integer(key,static_cast<int>(number));
  break;
 }
 case RendererOptionType::floating_point:{
  const float number=std::strtof(value,&end);
  ok=end&&!*end&&config.set_floating_point(key,number);
  break;
 }
 }
 detail=!ok?"value rejected":option->restart_required?"set; takes effect after restart":"set (not saved)";
 return ok;
}

// B8G8R8A8 rows as a 24-bit bottom-up BMP (harness screenshots, both backends).
static bool write_bgra_bmp(const std::string& path,const std::uint8_t* rows,UINT width,UINT height,UINT pitch,std::string& detail){
 const UINT row=(width*3u+3u)&~3u;
 std::vector<std::uint8_t> pixels(std::size_t(row)*height);
 for(UINT y=0;y<height;++y){
  const auto* source=rows+std::size_t(height-1u-y)*pitch;
  auto* target=pixels.data()+std::size_t(y)*row;
  for(UINT x=0;x<width;++x){target[x*3]=source[x*4];target[x*3+1]=source[x*4+1];target[x*3+2]=source[x*4+2];}
 }
 BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=LONG(width);info.biHeight=LONG(height);
 info.biPlanes=1;info.biBitCount=24;info.biCompression=BI_RGB;info.biSizeImage=DWORD(pixels.size());
 BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);
 file.bfSize=DWORD(file.bfOffBits+pixels.size());
 FILE* output=nullptr;
 if(fopen_s(&output,path.c_str(),"wb")||!output){detail="cannot write "+path;return false;}
 const bool written=std::fwrite(&file,sizeof(file),1,output)==1&&std::fwrite(&info,sizeof(info),1,output)==1&&
  std::fwrite(pixels.data(),1,pixels.size(),output)==pixels.size();
 const bool closed=std::fclose(output)==0;
 char text[64]{};std::snprintf(text,sizeof(text)," %ux%u",width,height);
 detail=written&&closed?path+text:"write failed: "+path;
 return written&&closed;
}

// fg_frame_image: this frame's HUD-less world copy (D3D12), as a capture image.
bool Renderer::write_hudless_image(const std::string& path,std::string& detail) noexcept {
 const stall_watch::Exempt disk_export;
 try{
  if(!d3d12_||!hudless_d3d12_){detail="no HUD-less copy";return false;}
  std::vector<std::uint8_t> bgra;UINT width=0,height=0;
  if(d3d12_->read_texture(*hudless_d3d12_,bgra,width,height)!=S_OK){detail="HUD-less readback failed";return false;}
  return write_bgra_bmp(path,bgra.data(),width,height,width*4u,detail);
 }catch(...){detail="allocation failed";return false;}
}

// The back buffer as the game composed it this present (before the debug
// view and overlay), as a 24-bit bottom-up BMP.
bool Renderer::write_harness_screenshot(const std::string& path,std::string& detail) noexcept {
 const stall_watch::Exempt disk_export;
 try{
  if(d3d12_){
   // D3D12 backend: the real back buffer, copied and read back on the GPU queue.
   std::vector<std::uint8_t> bgra;UINT width=0,height=0;
   const HRESULT hr=d3d12_->read_back_buffer(bgra,width,height);
   if(hr==S_FALSE){detail="back buffer unavailable (minimized)";return false;}
   if(FAILED(hr)){char text[80]{};std::snprintf(text,sizeof(text),"D3D12 back buffer readback failed (%08lx)",static_cast<unsigned long>(hr));detail=text;return false;}
   return write_bgra_bmp(path,bgra.data(),width,height,width*4u,detail);
  }
  ComPtr<ID3D11Texture2D> back;
  if(!swap_||FAILED(swap_->GetBuffer(0,IID_PPV_ARGS(&back)))){detail="back buffer unavailable";return false;}
  D3D11_TEXTURE2D_DESC desc{};back->GetDesc(&desc);
  if(desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM||desc.SampleDesc.Count!=1){detail="unexpected back buffer format";return false;}
  desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  desc.MiscFlags=0;desc.MipLevels=1;desc.ArraySize=1;
  ComPtr<ID3D11Texture2D> staging;
  if(FAILED(device_->CreateTexture2D(&desc,nullptr,&staging))){detail="staging texture failed";return false;}
  context_->CopyResource(staging.Get(),back.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  // Bounded (2 s): a GPU that stops completing work fails the harness
  // command (gpu_readback_timeout) instead of hanging the present thread.
  if(FAILED(gpu_map_read_bounded(context_.Get(),device_.Get(),staging.Get(),0,mapped,"harness screenshot",&detail)))return false;
  // Copied out of the mapping before Unmap (the D3D11 order of before).
  std::vector<std::uint8_t> bgra(std::size_t(desc.Width)*4u*desc.Height);
  for(UINT y=0;y<desc.Height;++y)
   std::memcpy(bgra.data()+std::size_t(y)*desc.Width*4u,static_cast<const std::uint8_t*>(mapped.pData)+std::size_t(y)*mapped.RowPitch,std::size_t(desc.Width)*4u);
  context_->Unmap(staging.Get(),0);
  return write_bgra_bmp(path,bgra.data(),desc.Width,desc.Height,desc.Width*4u,detail);
 }catch(...){detail="allocation failed";return false;}
}

void populate_placed_hitch_engine_frame(
 PlacedHitchFrame& frame,const BridgeProfileSnapshot* bridge,
 bool bridge_fresh) noexcept {
 if(!bridge)return;
 frame.bridge_frame=bridge->frame;
 frame.bridge_fresh=bridge_fresh;
 if(!bridge_fresh)return;
 frame.world_epoch=bridge->lifecycle.positive_world_epoch;
 auto& engine=frame.engine;
 engine.frame_cpu_ms=bridge->frame_cpu_ms;
 engine.outside_scene_interval_ms=bridge->outside_scene_interval_ms;
 engine.outside_entry_cpu_ms=bridge->outside_entry_cpu_ms;
 engine.world_draw_cpu_ms=bridge->world_draw_cpu_ms;
 engine.world_unattributed_cpu_ms=bridge->world_unattributed_cpu_ms;
 engine.scene_3d_cpu_ms=bridge->scene_3d_cpu_ms;
 engine.scene_3d_unattributed_cpu_ms=bridge->scene_3d_unattributed_cpu_ms;
 engine.scene_partition_retained_nonqueue_cpu_ms=
  bridge->scene_partition_retained_nonqueue_cpu_ms;
 engine.city_retained_owner_cpu_ms=bridge->city_retained_owner_cpu_ms;
 engine.scene_partition_object_nonqueue_cpu_ms=
  bridge->scene_partition_object_nonqueue_cpu_ms;
 engine.scene_partition_queue_native_flush_cpu_ms=
  bridge->scene_partition_queue_native_flush_cpu_ms;
 engine.scene_partition_queue_ui_flush_cpu_ms=
  bridge->scene_partition_queue_ui_flush_cpu_ms;
 engine.scene_partition_queue_packet_or_control_cpu_ms=
  bridge->scene_partition_queue_packet_or_control_cpu_ms;
 engine.scene_partition_remaining_game_cpu_ms=
  bridge->scene_partition_remaining_game_cpu_ms;
 engine.outside_scene_3d_cpu_ms=bridge->outside_scene_3d_cpu_ms;
 engine.object_draw_cpu_ms=bridge->object_draw_cpu_ms;
 engine.object_draw_unattributed_cpu_ms=bridge->object_draw_unattributed_cpu_ms;
 engine.object_plan_bootstrap_cpu_ms=bridge->object_plan_bootstrap_cpu_ms;
 engine.object_plan_submit_cpu_ms=bridge->object_plan_submit_cpu_ms;
 engine.simple_cpu_ms=bridge->simple_cpu_ms;
 engine.figure_placed_cpu_ms=bridge->figure_placed_cpu_ms;
 engine.figure_lit_cpu_ms=bridge->figure_lit_cpu_ms;
 engine.figure_lit_morphed_cpu_ms=bridge->figure_lit_morphed_cpu_ms;
 engine.figure_lit_rigid_cpu_ms=bridge->figure_lit_rigid_cpu_ms;
 engine.terrain_cpu_ms=bridge->terrain_cpu_ms;
 engine.terrain_outer_cpu_ms=bridge->terrain_outer_cpu_ms;
 engine.terrain_capture_cpu_ms=bridge->terrain_capture_cpu_ms;
 engine.terrain_submit_cpu_ms=bridge->terrain_submit_cpu_ms;
 engine.world_draw_calls=bridge->world_draw_calls;
 engine.scene_3d_calls=bridge->scene_3d_calls;
 engine.scene_partition_calls=bridge->scene_partition_calls;
 engine.scene_partition_retained_calls=
  bridge->scene_partition_retained_calls;
 engine.scene_partition_object_outer_calls=
  bridge->scene_partition_object_outer_calls;
 engine.scene_partition_queue_calls=bridge->scene_partition_queue_calls;
 engine.object_draw_calls=bridge->object_draw_calls;
 engine.object_plan_original_calls=bridge->object_plan_original_calls;
 engine.object_plan_owned_calls=bridge->object_plan_owned_calls;
 engine.object_plan_submitted_figures=bridge->object_plan_submitted_figures;
 engine.object_plan_early_omitted_calls=bridge->object_plan_early_omitted_calls;
 engine.simple_calls=bridge->simple_calls;
 engine.figure_placed_calls=bridge->figure_placed_calls;
 engine.figure_lit_calls=bridge->figure_lit_calls;
 engine.figure_gpu_owned_calls=bridge->figure_gpu_owned_calls;
 engine.figure_cpu_fallback_calls=bridge->figure_cpu_fallback_calls;
 engine.figure_gpu_submit_failures=bridge->figure_gpu_submit_failures;
 engine.terrain_calls=bridge->terrain_calls;
 engine.terrain_outer_calls=bridge->terrain_outer_calls;
 engine.terrain_gpu_owned_calls=bridge->terrain_gpu_owned_calls;
 engine.terrain_cpu_fallback_calls=bridge->terrain_cpu_fallback_calls;
 engine.terrain_gpu_submit_failures=bridge->terrain_gpu_submit_failures;
 engine.terrain_spatial_bounds_prepared=bridge->terrain_spatial_bounds_prepared;
 engine.terrain_spatial_bounds_reused=bridge->terrain_spatial_bounds_reused;
 engine.scene_partition_valid=bridge->scene_partition_valid;
}

void Renderer::record_placed_hitch_frame(
 std::int64_t present_entry,std::int64_t present_return,bool focused,
 HRESULT present_result,const OuterFrameTimingSnapshot& outer_timing) noexcept {
 if(!placed_hitch_profile_.active()||!placed_hitch_profile_.anchored())return;
 const std::int64_t frequency=perf_frequency();
 if(present_entry<=0||present_return<=present_entry||frequency<=0){
  finish_placed_hitch_profile(PlacedHitchFinishReason::invalid_clock);
  return;
 }

 PlacedHitchFrame frame{};
 frame.presentation_frame=presentation_frame_index_;
 frame.focused=focused;
 frame.present_succeeded=SUCCEEDED(present_result);
 frame.previous_trim_succeeded=placed_hitch_previous_trim_valid_;
 frame.renderer_present_entry_to_present_return_ms=
  static_cast<double>(present_return-present_entry)*1000.0/
  static_cast<double>(frequency);

 const auto& resources=native_frame_stats_;
 frame.renderer.color_resource=resources.color_resource;
 frame.renderer.color_tick_to_ms=1000.0/static_cast<double>(frequency);
 if(const auto* gpu=profiler_.latest_snapshot()){
  frame.renderer.gpu_snapshot_available=true;
  frame.renderer.gpu_snapshot_frame=gpu->frame;
  frame.renderer.gpu_snapshot_cpu_ms=gpu->frame_cpu_ms;
  frame.renderer.gpu_snapshot_ms=gpu->frame_gpu_ms;
  frame.renderer.gpu_snapshot_valid=gpu->gpu_valid;
 }
 frame.renderer.logical_draws=resources.native_logical_draws;
 frame.renderer.physical_batches=resources.native_physical_batches;
 frame.renderer.texture_array_attempts=resources.native_texture_array_attempts;
 frame.renderer.texture_array_successes=resources.native_texture_array_successes;
 frame.renderer.texture_array_unavailable=
  resources.native_texture_array_fallback_unavailable;
 frame.renderer.texture_array_format_failures=
  resources.native_texture_array_fallback_format;
 frame.renderer.texture_array_layout_failures=
  resources.native_texture_array_invalid_layout;
 frame.renderer.texture_array_allocation_failures=
  resources.native_texture_array_allocation_failures;
 frame.renderer.terrain_topology_uploads=
  resources.native_terrain_topology_uploads;
 frame.renderer.terrain_topology_upload_vertices=
  resources.native_terrain_topology_upload_vertices;
 frame.renderer.terrain_color_uploads=
  resources.native_terrain_color_only_uploads;
 frame.renderer.terrain_color_upload_vertices=
  resources.native_terrain_color_only_upload_vertices;
 frame.renderer.native_depth_selected=resources.native_depth_selected;
 frame.renderer.native_depth_warm=resources.native_depth_warm;
 frame.renderer.native_depth_cold=resources.native_depth_cold;
 frame.renderer.native_depth_registration_callbacks=
  resources.native_depth_registration_callbacks;
 frame.renderer.native_shadow_draws=resources.native_shadow_draws;
 frame.renderer.native_shadow_triangles=resources.native_shadow_triangles;
 frame.renderer.native_shadows_enabled=native_shadows_enabled_;
 frame.renderer.shadow_decision=resources.shadow_decision;
 auto& shadow=frame.renderer.shadow_decision;
 shadow.dirty_mask=resources.native_shadow_dirty_mask;
 shadow.source_dirty_mask=resources.native_shadow_source_dirty_mask;
 shadow.source_publications=resources.native_shadow_source_publications;
 shadow.source_changed_visuals=resources.native_shadow_source_changed_visuals;
 shadow.source_changed_terrain=resources.native_shadow_source_changed_terrain;
 shadow.source_receiver_refits=resources.native_shadow_source_receiver_refits;
 shadow.source_caster_refits=resources.native_shadow_source_caster_refits;
 shadow.source_map_reuses=resources.native_shadow_source_map_reuses;
 shadow.value_reuses=resources.native_shadow_value_reuses;
 shadow.frame_copies=resources.native_shadow_frame_copies;
 frame.renderer.native_depth_cold_handle_owner_identity=
  resources.native_depth_cold_handle_owner_identity;
 frame.renderer.native_depth_cold_source_revision=
  resources.native_depth_cold_source_revision;
 frame.renderer.native_depth_cold_inherited_context=
  resources.native_depth_cold_inherited_context;
 frame.renderer.native_depth_cold_caster_revision=
  resources.native_depth_cold_caster_revision;
 frame.renderer.native_depth_cold_world_allocation_generation=
  resources.native_depth_cold_world_allocation_generation;
 frame.renderer.native_depth_cold_pose_revision_generation_allocation=
  resources.native_depth_cold_pose_revision_generation_allocation;
 frame.renderer.native_depth_cold_animation_generation_ownership=
  resources.native_depth_cold_animation_generation_ownership;
 frame.renderer.native_depth_cold_bounds=resources.native_depth_cold_bounds;
 if(!placed_hitch_native_depth_partition_valid(frame.renderer)){
  finish_placed_hitch_profile(
   PlacedHitchFinishReason::native_depth_partition_invalid);
  return;
 }

 const auto* bridge=bridge_latest_profile();
 const bool bridge_fresh=bridge&&
  placed_hitch_profile_.observe_bridge_frame(bridge->frame);
 populate_placed_hitch_engine_frame(frame,bridge,bridge_fresh);
 frame.outer_timing_fresh=bridge_fresh&&
  bridge_outer_frame_timing_matches(outer_timing,bridge)&&
  placed_hitch_previous_trim_valid_;
 if(frame.outer_timing_fresh){
  const double scale=1000.0/static_cast<double>(frequency);
  frame.count_time_after_clock_ms=outer_timing.count_time_ticks*scale;
  frame.interface_on_idle_ms=outer_timing.interface_on_idle_ticks*scale;
  frame.message_handling_ms=outer_timing.message_handling_ticks*scale;
  frame.post_end_frame_to_present_entry_ms=
   outer_timing.post_end_frame_to_present_entry_ticks*scale;
  frame.previous_post_present_trim_ms=
   placed_hitch_previous_trim_ticks_*scale;
  frame.visual_unit_update_original_cpu_ms=
   outer_timing.visual_unit_update_original_ticks*scale;
  frame.visual_unit_update_finish_cpu_ms=
   outer_timing.visual_unit_update_finish_ticks*scale;
  frame.visual_unit_update_finish_appearance_cpu_ms=
   outer_timing.visual_unit_update_finish_appearance_ticks*scale;
  frame.visual_unit_update_finish_selected_clip_cpu_ms=
   outer_timing.visual_unit_update_finish_selected_clip_ticks*scale;
  frame.visual_unit_update_finish_outer_unit_values_cpu_ms=
   outer_timing.visual_unit_update_finish_outer_unit_values_ticks*scale;
  std::array<PlacedHitchDirectCall,3> direct_calls{};
  for(std::size_t i=0;i<direct_calls.size();++i){
   direct_calls[i].original_ticks=outer_timing.direct_calls[i].original_ticks;
   direct_calls[i].unit_original_ticks=
    outer_timing.direct_calls[i].unit_original_ticks;
   direct_calls[i].unit_finish_ticks=
    outer_timing.direct_calls[i].unit_finish_ticks;
   direct_calls[i].calls=outer_timing.direct_calls[i].calls;
  }
  static_assert(static_cast<std::size_t>(NativeTickOwnedCallback::count)==6);
  std::array<PlacedHitchNativeOwnedCallback,6> owned_callbacks{};
  for(std::size_t i=0;i<owned_callbacks.size();++i){
   owned_callbacks[i].exclusive_ticks=
    outer_timing.native_tick_owned_callbacks[i].exclusive_ticks;
   owned_callbacks[i].calls=outer_timing.native_tick_owned_callbacks[i].calls;
  }
  static_assert(kPlacedHitchVisualEventCapacity==kOuterFrameVisualEventCapacity);
  frame.visual_event_count=(std::min)(outer_timing.visual_event_count,
   static_cast<std::uint32_t>(frame.visual_events.size()));
  frame.visual_event_overflow=outer_timing.visual_event_overflow;
  std::uint64_t appearance_events=0;
  std::int64_t appearance_event_ticks=0;
  bool visual_events_valid=outer_timing.visual_event_count<=frame.visual_events.size();
  for(std::size_t i=0;i<frame.visual_event_count;++i){
   const auto& source=outer_timing.visual_events[i];
   auto& target=frame.visual_events[i];
   target.sequence=source.sequence;
   target.serial=source.serial;
   target.exclusive_cpu_ms=source.exclusive_ticks*scale;
   target.signed_id=source.signed_id;
   target.source_mask=source.source_mask;
   target.change_mask=source.change_mask;
   target.kind=static_cast<std::uint8_t>(source.kind);
   target.callback=static_cast<std::uint8_t>(source.callback);
   target.phase=source.phase;
   target.assembly_before=source.assembly_before;
   target.assembly_after=source.assembly_after;
   target.succeeded=source.succeeded;
   if(source.kind==OuterFrameVisualEventKind::appearance){
    ++appearance_events;
    if(source.exclusive_ticks<0 || appearance_event_ticks>
       (std::numeric_limits<std::int64_t>::max)()-source.exclusive_ticks)
     visual_events_valid=false;
    else appearance_event_ticks+=source.exclusive_ticks;
   }
  }
  const auto& outside=outer_timing.native_tick_owned_callbacks[
   static_cast<std::size_t>(NativeTickOwnedCallback::appearance_outside_finish)];
  frame.visual_events_complete=visual_events_valid && !frame.visual_event_overflow &&
   appearance_events==outside.calls && appearance_event_ticks==outside.exclusive_ticks;
  placed_hitch_account_count_time(frame,outer_timing.count_time_ticks,
   outer_timing.native_lifecycle_tick_original_ticks,
   outer_timing.native_tick_unit_original_ticks,
   outer_timing.native_tick_unit_finish_ticks,
   outer_timing.visual_unit_update_original_ticks,
   outer_timing.visual_unit_update_finish_ticks,
   outer_timing.visual_world_tick_ticks,scale,direct_calls,owned_callbacks);
  frame.visual_world_tick_cpu_ms=
   outer_timing.visual_world_tick_ticks*scale;
  frame.visual_world_tick_objects_cpu_ms=
   outer_timing.visual_world_tick_objects_ticks*scale;
  frame.visual_world_tick_terrain_cpu_ms=
   outer_timing.visual_world_tick_terrain_ticks*scale;
  frame.visual_world_tick_bridge_forced_cpu_ms=
   outer_timing.visual_world_tick_bridge_forced_ticks*scale;
  frame.visual_world_tick_commit_cpu_ms=
   outer_timing.visual_world_tick_commit_ticks*scale;
  frame.post_engine_surface_proxy_cpu_ms=
   outer_timing.post_engine_surface_proxy_ticks*scale;
  frame.post_engine_surface_flush_cpu_ms=
   outer_timing.post_engine_surface_flush_ticks*scale;
  frame.post_engine_surface_cpu_rect_cpu_ms=
   outer_timing.post_engine_surface_cpu_rect_ticks*scale;
  frame.post_engine_completed_surface_cpu_ms=
   outer_timing.post_engine_completed_surface_ticks*scale;
  auto finish_remaining_ticks=outer_timing.visual_unit_update_finish_ticks;
  const auto consume_finish_ticks=[&](std::int64_t ticks){
   if(ticks<0||ticks>finish_remaining_ticks)return false;
   finish_remaining_ticks-=ticks;
   return true;
  };
  if(finish_remaining_ticks<0||
     !consume_finish_ticks(outer_timing.visual_unit_update_finish_appearance_ticks)||
     !consume_finish_ticks(outer_timing.visual_unit_update_finish_selected_clip_ticks)||
     !consume_finish_ticks(outer_timing.visual_unit_update_finish_outer_unit_values_ticks)){
   finish_placed_hitch_profile(PlacedHitchFinishReason::invalid_clock);
   return;
  }
  frame.visual_unit_update_finish_other_cpu_ms=
   finish_remaining_ticks*scale;
  frame.count_time_calls=outer_timing.count_time_calls;
  frame.interface_on_idle_calls=outer_timing.interface_on_idle_calls;
  frame.message_handler_calls=outer_timing.message_handler_calls;
  frame.visual_unit_update_original_calls=
   outer_timing.visual_unit_update_original_calls;
  frame.visual_unit_update_finish_calls=
   outer_timing.visual_unit_update_finish_calls;
  frame.visual_unit_update_finish_appearance_calls=
   outer_timing.visual_unit_update_finish_appearance_calls;
  frame.visual_unit_update_finish_selected_clip_calls=
   outer_timing.visual_unit_update_finish_selected_clip_calls;
  frame.visual_unit_update_finish_outer_unit_values_calls=
   outer_timing.visual_unit_update_finish_outer_unit_values_calls;
  frame.native_lifecycle_tick_original_calls=
   outer_timing.native_lifecycle_tick_original_calls;
  frame.visual_world_tick_calls=outer_timing.visual_world_tick_calls;
  frame.visual_world_tick_manager_objects=
   outer_timing.visual_world_tick_manager_objects;
  frame.visual_world_tick_terrain_layers=
   outer_timing.visual_world_tick_terrain_layers;
  frame.visual_world_tick_terrain_noise_values=
   outer_timing.visual_world_tick_terrain_noise_values;
  frame.visual_world_tick_bridge_planes=
   outer_timing.visual_world_tick_bridge_planes;
  frame.visual_world_tick_forced_ids=
   outer_timing.visual_world_tick_forced_ids;
  frame.visual_world_tick_committed_entities=
   outer_timing.visual_world_tick_committed_entities;
  frame.post_engine_surface_proxy_calls=
   outer_timing.post_engine_surface_proxy_calls;
  frame.post_engine_surface_flush_calls=
   outer_timing.post_engine_surface_flush_calls;
  frame.post_engine_surface_cpu_rect_calls=
   outer_timing.post_engine_surface_cpu_rect_calls;
  frame.post_engine_surface_cpu_rect_pixels=
   outer_timing.post_engine_surface_cpu_rect_pixels;
  frame.post_engine_completed_surface_calls=
   outer_timing.post_engine_completed_surface_calls;
  frame.post_engine_completed_surface_bytes=
   outer_timing.post_engine_completed_surface_bytes;
  frame.post_engine_surface_material_group_full_scans=
   outer_timing.post_engine_surface_material_group_full_scans;
  const auto& finish_source=
   outer_timing.slowest_visual_unit_update_finish;
  auto& finish=frame.slowest_visual_unit_update_finish;
  finish.finish_cpu_ms=finish_source.finish_ticks*scale;
  finish.appearance_cpu_ms=finish_source.appearance_ticks*scale;
  finish.selected_clip_cpu_ms=finish_source.selected_clip_ticks*scale;
  finish.outer_unit_values_cpu_ms=
   finish_source.outer_unit_values_ticks*scale;
  auto slowest_remaining_ticks=finish_source.finish_ticks;
  const auto consume_slowest_ticks=[&](std::int64_t ticks){
   if(ticks<0||ticks>slowest_remaining_ticks)return false;
   slowest_remaining_ticks-=ticks;
   return true;
  };
  if(slowest_remaining_ticks<0||
     !consume_slowest_ticks(finish_source.appearance_ticks)||
     !consume_slowest_ticks(finish_source.selected_clip_ticks)||
     !consume_slowest_ticks(finish_source.outer_unit_values_ticks)){
   finish_placed_hitch_profile(PlacedHitchFinishReason::invalid_clock);
   return;
  }
  finish.other_cpu_ms=slowest_remaining_ticks*scale;
  finish.signed_id=finish_source.signed_id;
  finish.resource_serial=finish_source.resource_serial;
  finish.appearance_calls=finish_source.appearance_calls;
  finish.selected_clip_calls=finish_source.selected_clip_calls;
  finish.outer_unit_values_calls=finish_source.outer_unit_values_calls;
  finish.failure_status=finish_source.failure_status;
  finish.has_scope=finish_source.has_scope;
  finish.completion_succeeded=finish_source.completion_succeeded;
  finish.appearance_attempted=finish_source.appearance_attempted;
  finish.appearance_succeeded=finish_source.appearance_succeeded;
  finish.appearance_changed=finish_source.appearance_changed;
  finish.appearance_missing_assembly=
   finish_source.appearance_missing_assembly;
  const auto& appearance_source=finish_source.appearance_diagnostic;
  auto& appearance=finish.appearance_diagnostic;
  appearance.available=appearance_source.available;
  if(appearance.available){
   appearance.model_native_ms=appearance_source.model_native_ticks*scale;
   appearance.model_owned_ms=appearance_source.model_owned_ticks*scale;
   const auto owned=appearance_source.model_owned_ticks;
   const auto provenance=appearance_source.model_provenance_ticks;
   const auto resolve=appearance_source.model_resolve_ticks;
   const auto file_read=appearance_source.model_file_read_ticks;
   const auto comparison=appearance_source.model_comparison_ticks;
   appearance.resource_partition_valid=owned>=0&&provenance>=0&&resolve>=0&&
    file_read>=0&&comparison>=0&&provenance<=owned&&resolve<=owned-provenance&&
    file_read<=resolve&&comparison<=resolve-file_read;
   if(appearance.resource_partition_valid){
    appearance.model_provenance_ms=provenance*scale;
    appearance.model_file_read_ms=file_read*scale;
    appearance.model_comparison_ms=comparison*scale;
    appearance.model_resolve_other_ms=(resolve-file_read-comparison)*scale;
    appearance.model_owned_other_ms=(owned-provenance-resolve)*scale;
   }
   appearance.model_archive_ms=appearance_source.model_archive_ticks*scale;
   appearance.asset_lookup_ms=appearance_source.asset_lookup_ticks*scale;
   appearance.asset_decode_ms=appearance_source.asset_decode_ticks*scale;
   appearance.material_ms=appearance_source.material_ticks*scale;
   appearance.texture_ms=appearance_source.texture_ticks*scale;
   appearance.publication_ms=appearance_source.publication_ticks*scale;
   const std::array<std::int64_t,7> leaves{
    appearance_source.model_native_ticks,appearance_source.model_owned_ticks,
    appearance_source.model_archive_ticks,appearance_source.asset_lookup_ticks,
    appearance_source.material_ticks,appearance_source.texture_ticks,
    appearance_source.publication_ticks};
   std::int64_t remaining=finish_source.appearance_ticks;
   appearance.accounting_valid=appearance_source.asset_decode_ticks>=0&&
    appearance_source.asset_decode_ticks<=appearance_source.asset_lookup_ticks;
   for(const auto leaf:leaves){
    if(leaf<0||leaf>remaining){appearance.accounting_valid=false;break;}
    remaining-=leaf;
   }
   if(appearance.accounting_valid)appearance.remainder_ms=remaining*scale;
   appearance.model_acquisitions=appearance_source.model_acquisitions;
   appearance.model_selected_bytes=appearance_source.model_selected_bytes;
   appearance.model_unique_identities=appearance_source.model_unique_identities;
   appearance.model_repeated_identities=appearance_source.model_repeated_identities;
   appearance.model_repeated_selected_bytes=appearance_source.model_repeated_selected_bytes;
   appearance.model_file_read_bytes=appearance_source.model_file_read_bytes;
   appearance.model_file_read_calls=appearance_source.model_file_read_calls;
   appearance.model_repeated_file_read_bytes=appearance_source.model_repeated_file_read_bytes;
   appearance.model_backing_cache_hits=appearance_source.model_backing_cache_hits;
   appearance.model_comparison_span_bytes=appearance_source.model_comparison_span_bytes;
   appearance.model_repeated_comparison_span_bytes=
    appearance_source.model_repeated_comparison_span_bytes;
   appearance.skeleton_cache_hits=appearance_source.skeleton_cache_hits;
   appearance.skeleton_cache_misses=appearance_source.skeleton_cache_misses;
   appearance.figure_cache_hits=appearance_source.figure_cache_hits;
   appearance.figure_cache_misses=appearance_source.figure_cache_misses;
   appearance.decoded_figure_bytes=appearance_source.decoded_figure_bytes;
   appearance.identity_complete=appearance_source.identity_complete&&
    appearance_source.model_acquisitions==
     appearance_source.model_unique_identities+
     appearance_source.model_repeated_identities;
   appearance.mixed_resolver_generation=
    appearance_source.mixed_resolver_generation;
   appearance.selected_identity_count=appearance_source.selected_identity_count;
   appearance.selected_identity_overflow=appearance_source.selected_identity_overflow;
  }
  finish.failure_reason=finish_source.failure_reason;
  finish.appearance_reason=finish_source.appearance_reason;
  finish.failure_status_name=finish_source.failure_status_name;
 }
 if(!placed_hitch_finish_partition_valid(frame)){
  finish_placed_hitch_profile(PlacedHitchFinishReason::invalid_clock);
  return;
 }

 if(placed_hitch_profile_.observe_world_epoch(frame.world_epoch,bridge_fresh)){
  bridge_set_outer_frame_timing_active(false);
  export_placed_hitch_profile();
  bridge_set_profile_enabled(debug_overlay_.wants_profiling());
  return;
 }
 if(!placed_hitch_scene_partition_usable(frame)){
  char rejection_detail[512]{};
  std::snprintf(rejection_detail,sizeof(rejection_detail),
   "sequence=%llu presentation_frame=%llu bridge_frame=%llu bridge_fresh=%u "
   "scene_partition_valid=%u scene_partition_calls=%llu scene_3d_calls=%llu "
   "scene_partition_retained_calls=%llu",
   static_cast<unsigned long long>(placed_hitch_profile_.sequence_id()),
   static_cast<unsigned long long>(frame.presentation_frame),
   static_cast<unsigned long long>(frame.bridge_frame),
   static_cast<unsigned>(frame.bridge_fresh),
   static_cast<unsigned>(frame.engine.scene_partition_valid),
   static_cast<unsigned long long>(frame.engine.scene_partition_calls),
   static_cast<unsigned long long>(frame.engine.scene_3d_calls),
   static_cast<unsigned long long>(frame.engine.scene_partition_retained_calls));
  report_placed_hitch_lifecycle(
   "outer_frame_gap_profile_scene_partition_rejected",rejection_detail);
  finish_placed_hitch_profile(
   PlacedHitchFinishReason::scene_partition_invalid);
  return;
 }
 const auto prior_frames=placed_hitch_profile_.frames().size();
 const bool finished=placed_hitch_profile_.observe_present(
     present_return,frequency,std::move(frame));
 if(placed_hitch_profile_.frames().size()>prior_frames){
  const auto& recorded=placed_hitch_profile_.frames().back();
  const auto& source=outer_timing.slowest_visual_unit_update_finish.appearance_diagnostic;
  placed_hitch_profile_.observe_selected_identity(
   recorded.presentation_frame,recorded.bridge_frame,
   recorded.slowest_visual_unit_update_finish.resource_serial,
   recorded.slowest_visual_unit_update_finish.appearance_cpu_ms,
   source.selected_identity_keys,source.selected_identity_generations,
   source.selected_identity_count,
   source.identity_complete&&!source.selected_identity_overflow);
 }
 if(!finished)return;
 bridge_set_outer_frame_timing_active(false);
 const bool saved=export_placed_hitch_profile();
 bridge_set_profile_enabled(debug_overlay_.wants_profiling());
 if(placed_hitch_post_complete_capture_allowed(placed_hitch_profile_,saved)){
  const RendererResourceSnapshot capture_resources=resource_snapshot();
  const auto cutout_scope=cutout_shadow_scope_snapshot();
  profiler_.request_capture_next(bridge_latest_profile(),&capture_resources,&cutout_scope);
  report_placed_hitch_lifecycle("outer_frame_gap_profile_post_complete_capture_queued",
   "next_eligible_normal_frame_separate_from_timed_sequence");
 }
}

#ifdef EID3D11_TESTING
bool Renderer::test_arm_shadow_temporal_pair() noexcept {
 ComPtr<ID3D11Texture2D> color;
 if(swap_)swap_->GetBuffer(0,IID_PPV_ARGS(&color));
 return profiler_.request_shadow_temporal_pair(color.Get(),depth_.Get(),
  native_shadow_depth_.Get(),native_frame_index_,
  bridge_world_draw_completion_counter());
}
#endif

} // namespace owned
