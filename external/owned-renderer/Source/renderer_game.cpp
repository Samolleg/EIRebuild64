// Renderer game-thread halves (x86 only): begin_scene_game and the game-thread present (present, queue_present, scene mode and frame latency).
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
#include "scene_commands_ui.h"

namespace owned {

// The part of begin_scene that sets state the game thread's hooks read
// (widescreen layout, bridge switches, send ranges): game thread.
// A failing HUD frame layer fails every frame: one line when the reason
// changes, then at each power of two of the failure count (game and scene
// threads both report).
static void report_hud_frame_layer_failure(const char* reason) noexcept {
 static std::atomic<const char*> last{nullptr};static std::atomic<std::uint64_t> failures{0};
 const std::uint64_t count=++failures;
 if(last.exchange(reason)==reason&&(count&(count-1))!=0)return;
 char detail[160];std::snprintf(detail,sizeof(detail),"%s (failures=%llu)",reason,static_cast<unsigned long long>(count));
 bridge_report_event("hud_frame_layer_failed",detail);
}
void Renderer::begin_scene_game(){
#ifndef EID3D11_EMBEDDED
 if(!bridge_set_world_map_previews(this,configured_boolean(debug_overlay_.config(),"world_map_previews",false)))
  (void)debug_overlay_.config().set_boolean("world_map_previews",false);
#else
 sync_embedded_world_map_preview_option();
#endif
 // Owned party portraits are chosen on the game thread (engine_bridge.cpp),
 // in core mode too, where begin_scene_renderer runs on the host.
 bridge_set_portrait_options(configured_boolean(debug_overlay_.config(),"portrait_head",true),
  configured_boolean(debug_overlay_.config(),"portrait_helmet",false));
 widescreen_set_hud_frame_sink(this,[](void* owner) noexcept {
  auto& renderer=*static_cast<Renderer*>(owner);
  const auto layer=[&renderer](const std::vector<HudAssetFrame>* frames){
   HRESULT hr=renderer.flush_native();
   if(SUCCEEDED(hr))hr=renderer.composite_world();
   if(SUCCEEDED(hr))hr=renderer.render_pending_blits();
   if(SUCCEEDED(hr))hr=renderer.draw_hud_asset_frames(frames);
   if(FAILED(hr))report_hud_frame_layer_failure("HUD border composition failed before tooltips.");
  };
  if(renderer.scene_queue()){
   // The panel objects and Show flags are game memory: copy them here, on
   // the game thread; the scene thread draws only the copy.
   std::vector<HudAssetFrame> frames;
   try{widescreen_hud_asset_frames(frames);}
   catch(...){report_hud_frame_layer_failure("HUD border capture failed; owned frames omitted.");return;}
   renderer.scene_ui_pending_=false;renderer.scene_native_pending_=false;
   if(renderer.core_mode()){renderer.core_post_command(scene_ui::HudFrameLayer{std::move(frames)},"hud_frame_layer");return;}
   renderer.scene_post_frame([layer,frames=std::move(frames)]{layer(&frames);});
  }
  else layer(nullptr);
 });
 const auto& config=debug_overlay_.config();
 widescreen_set_ui_scale(configured_integer(config,"ui_scale",100));
 // The per-panel config keys, built once (this runs every frame).
 enum HudKey{hk_scale,hk_x,hk_y,hk_buttons_scale,hk_buttons_side,hk_direction,hk_padding,hk_border_padding,hk_preview,hk_preview_count,
  hk_slots_per_column,hk_auto_fade,hk_fade_transparency,hk_count};
 static const auto hud_keys=[]{
  static const char* const suffixes[hk_count]={"_scale","_x","_y","_buttons_scale","_buttons_side","_direction","_padding","_border_padding",
   "_preview","_preview_count","_slots_per_column","_auto_fade","_fade_transparency"};
  std::array<std::array<std::string,hk_count>,kHudPanels.size()> keys;
  for(unsigned i=0;i<kHudPanels.size();++i)for(unsigned k=0;k<hk_count;++k)keys[i][k]=std::string(kHudPanels[i].key)+suffixes[k];
  return keys;}();
 std::array<HudPanelSettings,7> panels{};
 for(unsigned i=0;i<panels.size();++i) {
     const auto& key=hud_keys[i];
     panels[i]={configured_floating_point(config,key[hk_scale].c_str(),100.f),
                configured_floating_point(config,key[hk_x].c_str(),-1.f),
                configured_floating_point(config,key[hk_y].c_str(),-1.f),
                float(configured_integer(config,key[hk_buttons_scale].c_str(),100)),
                configured_integer(config,key[hk_buttons_side].c_str(),0)};
     const char* direction=i==4?"portrait_direction":nullptr;
     panels[i].direction=configured_integer(config,direction?direction:key[hk_direction].c_str(),i==5?2:i==6?3:0);
     panels[i].padding=float(configured_integer(config,key[hk_padding].c_str(),0));
     panels[i].border_padding=float(configured_integer(config,key[hk_border_padding].c_str(),3));
     panels[i].preview=configured_boolean(config,key[hk_preview].c_str(),false);
     panels[i].preview_count=unsigned(configured_integer(config,key[hk_preview_count].c_str(),i==5||i==6?4:3));
     if(i!=4)panels[i].slots_per_line=unsigned((std::max)(0,configured_integer(config,key[hk_slots_per_column].c_str(),0)));
 }
 widescreen_set_hud_panels(panels);
 widescreen_set_quest_items({configured_floating_point(config,"quest_items_scale",100.f),
     configured_floating_point(config,"quest_items_x",-1.f),configured_floating_point(config,"quest_items_y",-1.f)});
 std::array<HudFadeSettings,kHudPanels.size()> fades{};
 for(unsigned i=0;i<fades.size();++i){const auto& key=hud_keys[i];
     fades[i]={configured_boolean(config,key[hk_auto_fade].c_str(),false),float(configured_integer(config,key[hk_fade_transparency].c_str(),80))};}
 widescreen_set_hud_fades(fades,{configured_floating_point(config,"stance_scale",100.f),configured_floating_point(config,"game_speed_scale",100.f)},
     {configured_integer(config,"hud_fade_duration",250),configured_integer(config,"hud_fade_in_duration",250),configured_integer(config,"hud_fade_delay",2000)});
 {RECT client{};const long output_height=hwnd_&&GetClientRect(hwnd_,&client)?client.bottom:0;
  widescreen_set_combat_text_scale(configured_integer(config,"combat_text_scale",100),output_height);}
 widescreen_set_message_panel({configured_floating_point(config,"message_panel_width",100.f),configured_floating_point(config,"message_panel_height",100.f),
                              configured_floating_point(config,"message_panel_x",50.f),configured_floating_point(config,"message_panel_y",0.f),float(configured_integer(config,"message_panel_text_scale",100)),
                              float(configured_integer(config,"message_panel_buttons_scale",100)),configured_integer(config,"message_panel_buttons_side",0),float(configured_integer(config,"message_panel_border_padding",3))});
 POINT hud_mouse{-1,-1};
 if(GetForegroundWindow()!=hwnd_ || !GetCursorPos(&hud_mouse) || !ScreenToClient(hwnd_,&hud_mouse))hud_mouse={-1,-1};
 if(hud_mouse.x>=0 && hud_mouse.y>=0)hud_mouse=game_output_to_native(hwnd_,hud_mouse);
 widescreen_update_hud_fades(float(hud_mouse.x),float(hud_mouse.y));
 cursor_scale_percent_=configured_integer(config,"cursor_scale",100);
 const float draw_distance=float(configured_integer(config,"draw_distance",300));
 widescreen_set_landscape_radius(draw_distance);
 set_game_effect_distance(std::min(draw_distance,float(configured_integer(config,"effect_distance",300))));
 bridge_set_skip_scenery_draw_walk(configured_boolean(config,"skip_scenery_draw_walk",true));
 bridge_set_merge_character_parts(configured_boolean(config,"merge_character_parts",true));
 bridge_set_highlight_options(configured_boolean(config,"highlight_briefing",true),
  configured_boolean(config,"highlight_selected",false),configured_boolean(config,"highlight_hover_characters",false),
  configured_boolean(config,"highlight_hover_objects",false));
 bridge_set_terrain_far_shadow_prewarm(configured_boolean(config,"terrain_far_shadow_prewarm",true));
 bridge_set_change_driven_poses(configured_boolean(config,"change_driven_object_poses",true));
 bridge_set_terrain_lod_enabled(configured_boolean(config,"terrain_lod",false));
 bridge_set_owned_sky(configured_integer(config,"sky_clouds",2)>0);
 bridge_set_fire_burn(configured_boolean(config,"fire_burn",false)&&configured_integer(config,"renderer_backend",0)==1);
}

void window_sampler_present(); // TEMPORARY (proxy.cpp)

// Game thread, at a frame boundary only. On: the scene thread starts and
// owns the renderer from the next queued command. Off: it drains, ends, and
// the game thread owns the renderer again.
void Renderer::set_scene_mode(bool active) noexcept{
 if(active==scene_mode()||!device_)return;
 if(active){
  if(!swap_)return;
  if(!scene_thread_){try{scene_thread_=std::make_unique<SceneThread>();}catch(...){return;}}
  if(!scene_thread_->start()){
   bridge_report_event("render_thread_unavailable","the scene thread could not be started; frames stay on the game thread");
   return;
  }
  scene_mode_frame_=presentation_frame_index_;
  scene_world_epoch_=world_state_epoch_; // the scene thread is not running yet
  scene_frame_latency_=0;apply_scene_frame_latency(); // before the scene thread presents
  scene_mode_.store(true,std::memory_order_release);
 }else{
  scene_sync("render_thread_off");
  scene_mode_.store(false,std::memory_order_release);
  scene_thread_->stop();
  stop_surface_conversion_worker();  // every write that used it has run
  // The game thread presents again with the device's own latency.
  if(scene_frame_latency_restore_){ComPtr<IDXGIDevice1> dxgi;if(SUCCEEDED(device_.As(&dxgi)))dxgi->SetMaximumFrameLatency(scene_frame_latency_restore_);}
  scene_frame_latency_=0;
  std::string waits;
  try{for(const auto& entry:scene_syncs_){if(!waits.empty())waits+=',';waits+=entry.first;waits+=':';waits+=std::to_string(entry.second);}}catch(...){}
  char summary[768];std::snprintf(summary,sizeof(summary),"frames=%llu waits=%s",
   static_cast<unsigned long long>(presentation_frame_index_-scene_mode_frame_),waits.empty()?"none":waits.c_str());
  bridge_report_event("render_thread_sync_summary",summary);
  scene_syncs_.clear();scene_sync_window_.clear();scene_window_frames_=0;
 }
 char detail[96];std::snprintf(detail,sizeof(detail),"active=%d frame=%llu",active?1:0,static_cast<unsigned long long>(presentation_frame_index_));
 bridge_report_event("render_thread",detail);
}

// owned_exceptions.log beside this module (as proxy.cpp's exception lines);
// the line goes through the log writer thread (bridge_async_append).
const wchar_t* frame_stall_log_path() noexcept {
 static wchar_t path[MAX_PATH]{};
 if(!path[0]){
  HMODULE self=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
     reinterpret_cast<LPCWSTR>(&frame_stall_log_path),&self))return nullptr;
  wchar_t module[MAX_PATH]{};if(!GetModuleFileNameW(self,module,MAX_PATH))return nullptr;
  wchar_t* slash=std::wcsrchr(module,L'\\');if(!slash)return nullptr;
  wcscpy_s(slash+1,MAX_PATH-static_cast<std::size_t>(slash+1-module),L"owned_exceptions.log");
  wcscpy_s(path,module);
 }
 return path;
}

// Frame stall detector (user decision 2026-11-14): the game thread's
// present-to-present interval, every frame (one QPC read and one thread-time
// read). An interval over kStallMs while the game clock runs (not paused, not
// a menu) and outside the first seconds of a world (the load hold) writes one
// loud line to owned_exceptions.log and bridge.jsonl/host.log: the interval,
// how much of it the game thread was on the CPU (the rest it was blocked:
// file I/O, a lock, a wait), the host present wait, the frame accounting's
// top parts when they are on (harness runs), and the latest combat hits.
// At most one line per 2 s and 200 a session; error_report's summary counts them.
namespace frame_stall {
constexpr double kStallMs = 250.0, kWorldGraceMs = 15000.0, kLineGapMs = 2000.0;
std::int64_t g_previous = 0, g_epoch_qpc = 0, g_last_line = 0;
std::uint64_t g_epoch = 0, g_thread_100ns = 0;
std::uint32_t g_clock = 0;
bool g_clock_valid = false;
double g_host_wait_ms = 0;   // the previous present's wait for a host slot (core mode)
// Captures (harness capture/screenshot/profile steps, F11) read the frame back
// on the host: the presents around them wait for it by design (0.3-0.8 s on
// the 3070 laptop). Stalls up to kCaptureQuietMs after the last one are skipped.
constexpr double kCaptureQuietMs = 1500.0;
std::int64_t g_capture_qpc = 0;
void note_capture() noexcept { LARGE_INTEGER now{}; QueryPerformanceCounter(&now); g_capture_qpc = now.QuadPart; }
void note_harness_step(bool valid, HarnessVerb verb) noexcept {
 if(valid&&(verb==HarnessVerb::capture||verb==HarnessVerb::screenshot||verb==HarnessVerb::profile))note_capture();
}
unsigned g_lines = 0;
std::uint64_t thread_time_100ns() noexcept {
 FILETIME created{},exited{},kernel{},user{};
 if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))return 0;
 return ((std::uint64_t(kernel.dwHighDateTime)<<32)|kernel.dwLowDateTime)+((std::uint64_t(user.dwHighDateTime)<<32)|user.dwLowDateTime);
}
// Game thread, at the top of Renderer::present (the frame record just closed).
void check(const frame_acct::Record& frame) noexcept {
 LARGE_INTEGER now_value{},frequency{};QueryPerformanceCounter(&now_value);QueryPerformanceFrequency(&frequency);
 const std::int64_t now=now_value.QuadPart;const double to_ms=1000.0/double(frequency.QuadPart);
 const std::uint64_t thread=thread_time_100ns();
 std::uint32_t clock=0;const bool clock_valid=bridge_game_clock_ms(clock);
 const std::uint64_t epoch=bridge_current_world_epoch();
 if(epoch!=g_epoch){g_epoch=epoch;g_epoch_qpc=now;}
 const double interval=g_previous?double(now-g_previous)*to_ms:0;
 const double cpu=g_thread_100ns&&thread>=g_thread_100ns?double(thread-g_thread_100ns)/10000.0:-1;
 const bool clock_ran=clock_valid&&g_clock_valid&&clock!=g_clock;
 const double host_wait=g_host_wait_ms;
 g_previous=now;g_thread_100ns=thread;g_clock=clock;g_clock_valid=clock_valid;g_host_wait_ms=0;
 if(GetAsyncKeyState(VK_F11)&0x8000)g_capture_qpc=now;  // F11 / Shift+F11 capture
 if(interval<kStallMs||!epoch||!clock_ran||double(now-g_epoch_qpc)*to_ms<kWorldGraceMs)return;
 if(g_capture_qpc&&double(now-g_capture_qpc)*to_ms<interval+kCaptureQuietMs)return;  // inside or just after a capture
 if(g_lines>=200||(g_last_line&&double(now-g_last_line)*to_ms<kLineGapMs))return;
 g_last_line=now;++g_lines;
 char parts[200]="scopes off (harness runs only)";
 if(frame.valid){
  std::array<unsigned,frame_acct::count> order{};for(unsigned i=0;i<order.size();++i)order[i]=i;
  std::partial_sort(order.begin(),order.begin()+3,order.end(),[&](unsigned a,unsigned b){return frame.ticks[a]>frame.ticks[b];});
  const double scale=frame_acct::ms_per_tick();
  std::snprintf(parts,sizeof(parts),"%s %.0f, %s %.0f, %s %.0f",frame_acct::part_name(order[0]),double(frame.ticks[order[0]])*scale,
   frame_acct::part_name(order[1]),double(frame.ticks[order[1]])*scale,frame_acct::part_name(order[2]),double(frame.ticks[order[2]])*scale);
 }
 std::uint32_t hits=0;std::int64_t hit_qpc=0;bridge_combat_hit_stats(hits,hit_qpc);
 char last_hit[64]="no hit yet";
 if(hit_qpc)std::snprintf(last_hit,sizeof(last_hit),"last hit %.1f s before the frame end",double(now-hit_qpc)*to_ms/1000.0);
 SYSTEMTIME t{};GetLocalTime(&t);
 char line[640];
 const int length=std::snprintf(line,sizeof(line),
  "!!!! FRAME STALL %.0f ms at %02u:%02u:%02u.%03u world_epoch=%llu | game thread on CPU %.0f ms, blocked %.0f ms | host present wait %.0f ms | "
  "top parts: %s | last events: %u combat hits so far, %s\r\n",
  interval,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,static_cast<unsigned long long>(epoch),cpu,cpu>=0?(std::max)(0.0,interval-cpu-host_wait):-1.0,
  host_wait,parts,hits,last_hit);
 if(length<=0)return;
 const int used=(std::min)(length,int(sizeof(line)-1));
 bridge_async_append(frame_stall_log_path(),line,std::size_t(used));
 int end=used;while(end>0&&(line[end-1]=='\r'||line[end-1]=='\n'))--end;
 line[end]=0;
 bridge_report_event("frame_stall",line);
}
}  // namespace frame_stall

// DD::presentVisual, game thread. Off: the frame is presented here. On: the
// game thread copies what only it may read (harness step, outer frame timing,
// bridge values), waits only while the previous frame has not presented, and
// queues this frame's present; it returns the previous Present result.
HRESULT Renderer::present(){
 PERF06_SCOPE("present_total");
 // The game thread's frame ends here (frame accounting); the present path
 // is charged to the next frame, outside its named steps as pf_other
 // (present_entry while the scene thread presents).
 fa_game_frame_=frame_acct::close_frame();
 frame_stall::check(fa_game_frame_);
 // After this present the game is outside its draw until the next
 // BeginFrame (game_other unless the outer-frame hooks name the segment).
 frame_acct::set_root(frame_acct::game_other);
 const frame_acct::Scope frame_acct_scope(scene_mode()?frame_acct::present_entry:frame_acct::pf_other);
 #ifdef EID3D11_EMBEDDED
 const bool wanted=false;
 #else
 window_sampler_present();
 if(core_mode_)return core_present();
 const bool wanted=device_&&swap_&&configured_boolean(debug_overlay_.config(),"render_thread",true);
#endif
 if(scene_mode()&&!wanted)set_scene_mode(false);
 if(scene_mode())return queue_present();
 const HRESULT hr=present_frame(nullptr);
 if(SUCCEEDED(hr)&&wanted)set_scene_mode(true);
 return hr;
}

// Game thread, every queued present: the waits of the last 600 frames per
// reason, so a per-frame sync shows without a clean exit.
void Renderer::report_scene_sync_window() noexcept{
 constexpr std::uint64_t kWindow=600;
 if(++scene_window_frames_<kWindow)return;
 std::string waits;
 try{for(const auto& entry:scene_sync_window_){if(!waits.empty())waits+=',';waits+=entry.first;waits+=':';waits+=std::to_string(entry.second);}}catch(...){}
 char detail[1024];std::snprintf(detail,sizeof(detail),"frames=%llu waits=%s",
  static_cast<unsigned long long>(scene_window_frames_),waits.empty()?"none":waits.c_str());
 bridge_report_event("render_thread_sync_window",detail);
 scene_sync_window_.clear();scene_window_frames_=0;
}

// Game thread: the DXGI maximum frame latency while the scene thread
// presents (render_thread_frame_latency). Frame accounting (st2 runs on
// 9161f42f, Suslanger cascade route): at the default 3 the scene thread,
// which starts the next frame as soon as Present returns, stalled 6 ms in
// D3D11 Map(WRITE_DISCARD) of per-frame dynamic buffers in 53 of 56 and 65
// of 68 frames over twice the median (6.5 ms per such frame; 430-460 stalls
// over 1 ms per run, render_thread 0: 0-2), mid-world instead of at Present. A lower latency moves that wait
// back into Present, at the frame boundary. Called with the scene thread
// stopped or drained (set_scene_mode) or between frames (queue_present,
// before posting); IDXGIDevice1 calls are thread-safe.
void Renderer::apply_scene_frame_latency() noexcept{
 const UINT wanted=static_cast<UINT>(std::clamp(configured_integer(debug_overlay_.config(),"render_thread_frame_latency",2),1,3));
 if(wanted==scene_frame_latency_||!device_)return;
 ComPtr<IDXGIDevice1> dxgi;if(FAILED(device_.As(&dxgi)))return;
 if(!scene_frame_latency_){UINT current=0;scene_frame_latency_restore_=SUCCEEDED(dxgi->GetMaximumFrameLatency(&current))?current:0;}
 const HRESULT hr=dxgi->SetMaximumFrameLatency(wanted);
 scene_frame_latency_=wanted;
 char detail[96];std::snprintf(detail,sizeof(detail),"frame_latency=%u previous=%u hr=%08lx",wanted,scene_frame_latency_restore_,static_cast<unsigned long>(hr));
 bridge_report_event("render_thread_frame_latency",detail);
}

HRESULT Renderer::queue_present(){
 report_scene_sync_window();
 apply_scene_frame_latency();
 ScenePresentInputs inputs;
 // The hitch recorder's state is scene-thread state; a stale read here only
 // decides whether this frame's outer timing is taken.
 if(placed_hitch_profile_.active()){
  inputs.recorder_present_entry=perf_now();
  inputs.outer_timing_taken=bridge_take_outer_frame_timing(inputs.recorder_present_entry,inputs.outer_timing);
 }
 owned_resource_runtime_poll();
 camp_inventory_set_wide(configured_integer(debug_overlay_.config(),"camp_ui_layout",1)!=0);
#ifndef EID3D11_EMBEDDED
 inputs.harness_command_valid=harness_present_step_copy(inputs.harness_command,inputs.harness_entry_id,
  inputs.harness_camera_steps,inputs.harness_on_travel_map);
 frame_stall::note_harness_step(inputs.harness_command_valid,inputs.harness_command.verb);
 inputs.harness_banner=harness_banner_text();
#endif
 if(!bridge_capture_frame_values(inputs.bridge))return E_FAIL;
 bridge_capture_profile_values(inputs.profile);
 {const stall_watch::Exempt wait;const frame_acct::Scope wait_scope(frame_acct::wait_present_slot);
 #ifdef EID3D11_EMBEDDED
 const CorePerf::AnyThreadScope coreWait(CorePerf::PresentSlotWait);
 const frame_waits::Scope presentSlotWait(frame_waits::present_slot);
 #endif
 scene_thread_->wait_present_slot();}
 inputs.game_frame=fa_game_frame_;
 // Present's HUD border step draws this copy, never the live panels.
 try{widescreen_hud_asset_frames(inputs.hud_frames);}catch(...){inputs.hud_frames.clear();}
 const HRESULT previous=scene_thread_->last_present_result();
 auto shared=std::make_shared<ScenePresentInputs>(std::move(inputs));
 const bool queued=scene_thread_->post([this,shared]{
  bridge_install_frame_values(shared->bridge);
  bridge_install_profile_values(shared->profile);
  const HRESULT hr=present_frame(shared.get());
  scene_thread_->set_present_result(hr);
 },true,"present_frame");
 // A load progress movie decode the scene thread asked for (the game's
 // binkw32 imports run on this thread only), after this frame's present was queued.
 if(queued){
#ifndef EID3D11_EMBEDDED
  service_load_progress_decode();
#endif
  return previous;
 }
 const SceneInlineScope scene_inline(this,"present_allocation_failed");
 return present_frame(shared.get());
}


// ---- Core mode (EI_RENDERER_X64_CORE=1, phase 2 step S3) ---------------------
// The game process keeps no D3D11 device: the x64 host's Renderer draws.
// This side loads the ini, runs the game-thread halves and sends typed
// commands (scene_core.h). A host that does not start, or is lost, closes
// the game through the named device-removed box: core mode has no local
// fallback (the call-stream mode keeps its own, separately selected).

void retained_publication_attach() noexcept; // retained_publication_client.cpp
void retained_publication_core_textures() noexcept; // retained_publication_client.cpp
bool retained_publication_failed() noexcept;   // retained_publication_client.cpp
void report_game_address_space(std::uint64_t world_epoch) noexcept; // renderer_diagnostics.cpp
void migrate_settings(RendererConfig& config,const wchar_t* save_path) noexcept; // renderer_device.cpp
void address_space_mb(std::uint64_t& used,std::uint64_t& largest_free,bool walk) noexcept; // renderer_diagnostics.cpp
const char* core_decode_progress_pixels(const std::string& path,unsigned width,unsigned height,unsigned frames,
 unsigned shown,std::vector<std::vector<std::uint8_t>>& output); // renderer_load_progress.cpp

#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
HRESULT Renderer::core_ensure() { return E_NOTIMPL; }
#else
HRESULT Renderer::core_ensure(){
 core_mode_=true;
 register_config_options();
 debug_overlay_.load_config_only();
 migrate_settings(debug_overlay_.config(),nullptr); // in memory: the host migrates and saves the ini (it alone writes it)
 apply_config_settings();
 char error[192]{};
 const UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
 if(!x64remote::link_start(hwnd_,flags,error,sizeof(error))){
  char reason[256];std::snprintf(reason,sizeof(reason),"core mode: the x64 host did not start: %s",error);
  bridge_report_event("x64_core_failed",reason);
  x64remote::lose_device(reason);
  return E_FAIL;
 }
 // Loading hooks can precede BeginScene. Register the loaded option once
 // the host can receive a notice as well as the later cache command.
#ifndef EID3D11_EMBEDDED
 if(!bridge_set_world_map_previews(this,configured_boolean(debug_overlay_.config(),"world_map_previews",false)))
  (void)debug_overlay_.config().set_boolean("world_map_previews",false);
#endif
 // The harness result directory: the host Renderer writes screenshots and
 // captures there and acks each renderer step (core_present retires them).
 if(harness_enabled())strncpy_s(x64remote::control().harness_directory,harness_directory().c_str(),_TRUNCATE);
 retained_publication_attach(); // the host's retained-scene replica (S2d), which its Renderer prepares from
 retained_publication_core_textures(); // archive texture bytes live in the host only (S5a)
 // The F10 overlay runs on the host; this process keeps the window hook
 // (camp input, focus swallowing, cursor confinement) and forwards the
 // overlay's messages while it takes input.
 {DebugOverlay::CoreInputHooks hooks;
  hooks.flags=[]() noexcept->long{return x64remote::control().overlay_flags;};
  hooks.forward=[](UINT message,WPARAM wparam,LPARAM lparam) noexcept{
   auto& control=x64remote::control();
   const long written=control.overlay_inputs_written;
   auto& slot=control.overlay_inputs[static_cast<std::uint32_t>(written)%x64remote::kOverlayInputs];
   slot.message=message;slot.wparam=static_cast<std::uint32_t>(wparam);slot.lparam=static_cast<std::int32_t>(lparam);
   MemoryBarrier();
   InterlockedExchange(&control.overlay_inputs_written,written+1);
  };
  const HRESULT hook=hwnd_?debug_overlay_.initialize_core_input(hwnd_,hooks):E_INVALIDARG;
  if(FAILED(hook)){char text[96];std::snprintf(text,sizeof(text),"core mode: the window hook failed (hr=%08lx); camp input and F10 are off",static_cast<unsigned long>(hook));
   bridge_report_event("core_input_hook_failed",text);}}
 try{core_send(static_cast<std::uint16_t>(scene_core::Command::layout),scene_wire::encode_layouts());}
 catch(...){x64remote::lose_device("core mode: the wire layout table could not be built");return E_FAIL;}
 core_send_config();
 core_send_mounts();
 // Every game-thread entry now posts: no scene thread exists here, the
 // commands go to the host (scene_post_command) or are refused.
 scene_mode_.store(true,std::memory_order_release);
 x64remote::publish();
 char detail[128];std::snprintf(detail,sizeof(detail),"output=%ux%u (F10 overlay on the host)",output_width_,output_height_);
 bridge_report_event("x64_core",detail);
 return S_OK;
}
#endif

#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
HRESULT Renderer::core_removed_reason() const noexcept { return E_NOTIMPL; }
#else
HRESULT Renderer::core_removed_reason() const noexcept{return static_cast<HRESULT>(x64remote::link_removed());}
#endif

#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
void Renderer::core_send(std::uint16_t command,const std::vector<std::uint8_t>& bytes) noexcept {  }
#else
void Renderer::core_send(std::uint16_t command,const std::vector<std::uint8_t>& bytes) noexcept{
 x64remote::scene_message(command,bytes.data(),bytes.size());
}
void Renderer::core_send_fill(std::uint16_t command,std::size_t bytes,void (*fill)(void*,std::uint8_t*),void* context) noexcept{
 x64remote::scene_message_fill(command,bytes,fill,context);
}

// ---- Core mode: the host's packet store (claude/stream-delta) ----------------
namespace {
constexpr std::size_t kPacketStoreMinimumBytes=4096;          // smaller arrays travel inline
constexpr std::uint64_t kPacketStoreUnusedFrames=300;         // then a hash leaves the store
constexpr std::uint64_t kPacketStoreBudget=256ull<<20;        // host bytes kept at most
constexpr std::uint64_t kPacketStoreTrimTo=192ull<<20;
}
void Renderer::core_packet_store(scene_ui::Packet& packet,
 const std::shared_ptr<const std::vector<std::uint8_t>>* texture_source) noexcept try{
 if(core_packet_store_enabled_<0){
  wchar_t value[8]{};
  core_packet_store_enabled_=GetEnvironmentVariableW(L"EID3D11_STREAM_STORE",value,8)&&value[0]==L'0'?0:1;
 }
 packet.store=scene_ui::PacketStore::inline_only;packet.store_hash=0;
 packet.texture_store=scene_ui::PacketStore::inline_only;packet.texture_hash=0;
 if(!core_packet_store_enabled_||core_packet_store_reset_)return;
 // The owned texture on its own: its content hash once per immutable source
 // buffer, the mips into the host's store once, then only the hash.
 const NativeGeometryView* source_view=packet.borrowed;
 if(texture_source&&*texture_source&&source_view&&source_view->owned_texture.identity&&
    source_view->owned_texture.mips&&source_view->owned_texture.mip_count){
  const auto& buffer=**texture_source;
  const auto& texture=source_view->owned_texture;
  // The slices the view takes from the buffer (all inside it, or no memo).
  scene_wire::Hasher layout_hash;bool inside=true;std::uint64_t texture_bytes=0;
  for(std::uint32_t index=0;index<texture.mip_count;++index){
   const auto& mip=texture.mips[index];
   const std::uintptr_t begin=reinterpret_cast<std::uintptr_t>(buffer.data()),at=reinterpret_cast<std::uintptr_t>(mip.data);
   if(!mip.data||at<begin||at-begin+mip.bytes>buffer.size()){inside=false;break;}
   const std::uint32_t slice[4]={static_cast<std::uint32_t>(at-begin),mip.width,mip.height,mip.bytes};
   layout_hash.update(slice,sizeof(slice));texture_bytes+=mip.bytes;
  }
  if(inside){
   const std::uint64_t layout=layout_hash.finish();
   auto& memo=core_texture_memo_[*texture_source];
   if(!memo.hash||memo.layout!=layout){
    scene_wire::HashOut hash;scene_ui::packet_mip_fields(hash,packet);
    memo.hash=hash.hasher.finish()^0x7E87A5E5D1F0C0DEull;  // apart from packet array hashes
    if(!memo.hash)memo.hash=1;
    memo.layout=layout;
   }
   auto& seen=core_packet_seen_[memo.hash];
   packet.texture_hash=memo.hash;
   if(seen.kept)packet.texture_store=scene_ui::PacketStore::stored;
   else{packet.texture_store=scene_ui::PacketStore::keep;seen.kept=true;core_packet_kept_bytes_+=texture_bytes;}
   seen.last_frame=core_packet_frame_;seen.bytes=static_cast<std::uint32_t>(texture_bytes);
  }
 }
 scene_wire::Count count;scene_ui::packet_arrays_fields(count,packet);
 if(count.bytes<kPacketStoreMinimumBytes)return;
 scene_wire::HashOut hash;scene_ui::packet_arrays_fields(hash,packet);
 const std::uint64_t key=hash.hasher.finish();
 auto& seen=core_packet_seen_[key];
 packet.store_hash=key;
 if(seen.kept)packet.store=scene_ui::PacketStore::stored;
 else if(seen.last_frame){
  // Seen in an earlier post: these bytes repeat, the host keeps them.
  packet.store=scene_ui::PacketStore::keep;seen.kept=true;core_packet_kept_bytes_+=count.bytes;
 }
 seen.last_frame=core_packet_frame_;seen.bytes=static_cast<std::uint32_t>(count.bytes);
}catch(...){
 // A texture already marked kept stays consistent: the reset resends all.
 packet.store=scene_ui::PacketStore::inline_only;packet.store_hash=0;
 packet.texture_store=scene_ui::PacketStore::inline_only;packet.texture_hash=0;
 core_packet_store_reset_=true;
}

// Each present: hashes unused for kPacketStoreUnusedFrames leave the mirror
// (and the host's store when kept); a store over its budget drops its least
// recently used arrays. After a refused post the whole store starts over.
void Renderer::core_packet_store_frame() noexcept try{
 const std::uint64_t frame=core_packet_frame_++;
 if(!core_packet_store_reset_&&(frame%30u)!=0&&core_packet_kept_bytes_<=kPacketStoreBudget)return;
 for(auto memo=core_texture_memo_.begin();memo!=core_texture_memo_.end();)
  memo=memo->first.expired()?core_texture_memo_.erase(memo):std::next(memo);
 scene_ui::PacketStoreEvict evict;
 if(core_packet_store_reset_){
  for(const auto& entry:core_packet_seen_)if(entry.second.kept)evict.hashes.push_back(entry.first);
  core_packet_seen_.clear();core_packet_kept_bytes_=0;
 }else{
  for(auto entry=core_packet_seen_.begin();entry!=core_packet_seen_.end();){
   if(entry->second.last_frame+kPacketStoreUnusedFrames>=frame){++entry;continue;}
   if(entry->second.kept){evict.hashes.push_back(entry->first);core_packet_kept_bytes_-=entry->second.bytes;}
   entry=core_packet_seen_.erase(entry);
  }
  if(core_packet_kept_bytes_>kPacketStoreBudget){
   std::vector<std::pair<std::uint64_t,std::uint64_t>> kept;  // last frame, hash
   for(const auto& entry:core_packet_seen_)if(entry.second.kept)kept.push_back({entry.second.last_frame,entry.first});
   std::sort(kept.begin(),kept.end());
   for(const auto& item:kept){
    if(core_packet_kept_bytes_<=kPacketStoreTrimTo)break;
    auto found=core_packet_seen_.find(item.second);
    core_packet_kept_bytes_-=found->second.bytes;evict.hashes.push_back(item.second);core_packet_seen_.erase(found);
   }
  }
 }
 core_packet_store_reset_=false;
 if(!evict.hashes.empty())core_post_command(evict,"scene_packet_evict");
}catch(...){
 core_packet_store_reset_=true;
}
#endif

// Game thread: every option of the loaded ini, when its revision moved or
// this process's overlay has changes to save (the host writes the ini).
#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
void Renderer::core_send_config() noexcept {  }
#else
void Renderer::core_send_config() noexcept try{
 const auto& config=debug_overlay_.config();
 const bool save=debug_overlay_.take_core_save_request();
 if(!save&&config.revision()==core_config_revision_)return;
 core_config_revision_=config.revision();
 scene_core::Config command;
 command.options.reserve(config.options().size());
 for(const auto& option:config.options()){
  scene_core::ConfigOption value;
  value.key=option.key;value.type=static_cast<std::uint8_t>(option.type);
  value.boolean_value=option.boolean_value;value.integer_value=option.integer_value;
  value.floating_value=option.floating_value;value.text_value=option.text_value;
  command.options.push_back(std::move(value));
 }
 command.overlay_mode=config.overlay_mode();command.debug_resource_view=config.debug_resource_view();
 command.config_path=renderer_config_path();
 command.save=save;
 std::vector<std::uint8_t> bytes{0};
 scene_wire::encode_into(bytes,command);
 core_send(static_cast<std::uint16_t>(scene_core::Command::config),bytes);
}catch(...){
 x64remote::lose_device("core mode: the options could not be sent (allocation)");
}
#endif

// A game-thread path core mode cannot send yet: one line the first time per
// site, a per-600-frame count of all of them (core_unconverted_window). The
// visual it would have produced is omitted.
#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
void Renderer::core_refuse(const char* site) noexcept {  }
#else
void Renderer::core_refuse(const char* site) noexcept{
 if(!site)site="?";
 bool known=false;
 for(auto& entry:core_refusals_)if(std::strcmp(entry.first,site)==0){++entry.second;known=true;break;}
 bool counted=false;
 for(auto& entry:core_refusal_window_)if(std::strcmp(entry.first,site)==0){++entry.second;counted=true;break;}
 try{
  if(!counted)core_refusal_window_.emplace_back(site,1);
  if(!known){
   core_refusals_.emplace_back(site,1);
   char detail[192];std::snprintf(detail,sizeof(detail),"site=%s frame=%llu (not sent to the host yet; its visual is omitted; logged once)",
    site,static_cast<unsigned long long>(core_present_serial_));
   bridge_report_event("core_unconverted",detail);
  }
 }catch(...){}
}
#endif

void Renderer::report_core_refusal_window() noexcept{
 constexpr std::uint64_t kWindow=600;
 if(++core_window_frames_<kWindow)return;
 std::string sites;
 try{for(const auto& entry:core_refusal_window_){if(!sites.empty())sites+=',';sites+=entry.first;sites+=':';sites+=std::to_string(entry.second);}}catch(...){}
 char detail[1024];std::snprintf(detail,sizeof(detail),"frames=%llu refused=%s",
  static_cast<unsigned long long>(core_window_frames_),sites.empty()?"none":sites.c_str());
 bridge_report_event("core_unconverted_window",detail);
 core_refusal_window_.clear();core_window_frames_=0;
}

// Game thread, core mode: the output size is this process's for its game
// halves (canvas mapping, camp layout); the host Renderer resizes its own.
#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
HRESULT Renderer::core_resize(UINT w,UINT h) { return E_NOTIMPL; }
#else
HRESULT Renderer::core_resize(UINT w,UINT h){
 w=std::max(1u,w);h=std::max(1u,h);
 if(w==output_width_&&h==output_height_)return S_OK;
 output_width_=w;output_height_=h;set_world_size();
 camp_inventory_output(canvas_width(),canvas_height());
 scene_core::Resize command;command.width=w;command.height=h;
 core_post_command(command,"resize");
 return S_OK;
}
#endif

// ---- Core mode: captures and readbacks (the host owns every GPU pixel) -----
#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
HRESULT Renderer::core_readback(bool back_buffer,std::uint64_t id,std::uint64_t generation,const RECT& box,
 std::vector<std::uint8_t>& bgra) { return E_NOTIMPL; }
#else
HRESULT Renderer::core_readback(bool back_buffer,std::uint64_t id,std::uint64_t generation,const RECT& box,
 std::vector<std::uint8_t>& bgra){
 const LONG w=box.right-box.left,h=box.bottom-box.top;
 if(w<=0||h<=0)return E_INVALIDARG;
 const std::size_t total=std::size_t(w)*std::size_t(h)*4u;
 try{bgra.resize(total);}catch(...){return E_OUTOFMEMORY;}
 const std::size_t area=x64remote::readback_bytes();
 if(!area)return E_FAIL;
 for(std::size_t first=0;first<total;first+=area){
  const std::uint32_t bytes=static_cast<std::uint32_t>(std::min(area,total-first));
  scene_core::Readback command;
  command.back_buffer=back_buffer?1:0;command.id=id;command.generation=generation;
  command.left=std::uint32_t(box.left);command.top=std::uint32_t(box.top);command.right=std::uint32_t(box.right);command.bottom=std::uint32_t(box.bottom);
  command.first_byte=static_cast<std::uint32_t>(first);command.bytes=bytes;
  command.reply=x64remote::next_reply_serial();
  core_post_command(command,"readback");
  const HRESULT hr=x64remote::wait_reply(command.reply);
  if(SUCCEEDED(hr))std::memcpy(bgra.data()+first,x64remote::readback_area(),bytes);
  x64remote::end_reply();
  if(FAILED(hr))return hr;
 }
 return S_OK;
}
#endif

// A game Lock of a GPU-captured surface: its pixels come back from the
// host's GPU home (readback's packing, from tight BGRA rows).
#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
HRESULT Renderer::core_readback_surface(SurfaceImage& image) { return E_NOTIMPL; }
#else
HRESULT Renderer::core_readback_surface(SurfaceImage& image){
 if(!image.gpu_captured)return S_OK;
 std::vector<std::uint8_t> bgra;
 const HRESULT hr=core_readback(false,image.id,image.generation,RECT{0,0,LONG(image.width),LONG(image.height)},bgra);
 if(FAILED(hr))return hr;
 const UINT bytes=std::max<UINT>(1,image.format.dwRGBBitCount/8);
 const size_t required=(size_t)image.pitch*image.height;
 std::uint8_t* pixels=nullptr;
 try{if(image.pixels.size()<required)image.pixels.resize(required);pixels=image.pixels.data();}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 const PackedChannelTables tables(image.format);
 for(UINT y=0;y<image.height;++y){
  const auto* source=reinterpret_cast<const std::uint32_t*>(bgra.data()+(size_t)y*image.width*4u);
  pack_bgra_row(pixels+(size_t)y*image.pitch,source,image.width,std::min<UINT>(bytes,4),tables);
 }
 clear_surface_dirty(image);update_surface_memory_accounting(image);
 return S_OK;
}
#endif

// Core mode: a released surface's bytes come back from the host's surface
// table as they were written, exact for every format (the D3D12 backend keeps
// them; the D3D11 backend restores a released entry from its GPU home first,
// Readback::run). If the host no longer holds the generation, its GPU home is
// read as BGRA and packed: exact for the round-trip formats, none for blocks.
#if !defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_TOOL)
HRESULT Renderer::core_restore_surface_pixels(SurfaceImage& image){
 const bool blocks=(image.format.dwFlags&DDPF_FOURCC)!=0;
 const UINT rows=blocks?std::max(1u,(image.height+3u)/4u):image.height;
 const std::size_t required=std::size_t(image.pitch)*rows;
 if(image.pitch<=0)return E_NOTIMPL;
 const std::size_t area=x64remote::readback_bytes();
 if(!area)return E_FAIL;
 std::vector<std::uint8_t> bytes;
 try{bytes.resize(required);}catch(...){return E_OUTOFMEMORY;}
 HRESULT hr=S_OK;
 for(std::size_t first=0;first<required&&SUCCEEDED(hr);first+=area){
  scene_core::Readback command;
  command.back_buffer=0;command.raw=1;command.id=image.id;command.generation=image.generation;
  command.first_byte=static_cast<std::uint32_t>(first);
  command.bytes=static_cast<std::uint32_t>(std::min(area,required-first));
  command.reply=x64remote::next_reply_serial();
  core_post_command(command,"readback");
  hr=x64remote::wait_reply(command.reply);
  if(SUCCEEDED(hr))std::memcpy(bytes.data()+first,x64remote::readback_area(),command.bytes);
  x64remote::end_reply();
 }
 if(SUCCEEDED(hr)){
  try{image.pixels=bytes;}catch(...){return E_OUTOFMEMORY;}
  return S_OK;
 }
 if(blocks)return hr;
 std::vector<std::uint8_t> bgra;
 hr=core_readback(false,image.id,image.generation,RECT{0,0,LONG(image.width),LONG(image.height)},bgra);
 if(FAILED(hr))return hr;
 if(bgra.size()<std::size_t(image.width)*image.height*4u)return E_FAIL;
 try{image.pixels.resize(required);}catch(...){return E_OUTOFMEMORY;}
 std::uint8_t* pixels=image.pixels.data();
 const UINT bytes_per_pixel=std::max<UINT>(1,image.format.dwRGBBitCount/8);
 const PackedChannelTables tables(image.format);
 for(UINT y=0;y<image.height;++y){
  const auto* source=reinterpret_cast<const std::uint32_t*>(bgra.data()+(size_t)y*image.width*4u);
  pack_bgra_row(pixels+(size_t)y*image.pitch,source,image.width,std::min<UINT>(bytes_per_pixel,4),tables);
 }
 return S_OK;
}

#endif

// capture() in core mode: an unstretched copy becomes the target's GPU home
// on the host (scene_ui::Capture, the target's new generation); a stretched
// one is read back and converted here, as the standard CPU path does.
#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
HRESULT Renderer::core_capture(SurfaceImage& destination,const RECT& destination_rect,const RECT& native_source_rect) { return E_NOTIMPL; }
#else
HRESULT Renderer::core_capture(SurfaceImage& destination,const RECT& destination_rect,const RECT& native_source_rect){
 const RECT source_rect{MulDiv(native_source_rect.left,output_width_,canvas_width()),MulDiv(native_source_rect.top,output_height_,canvas_height()),
  MulDiv(native_source_rect.right,output_width_,canvas_width()),MulDiv(native_source_rect.bottom,output_height_,canvas_height())};
 if(destination.cpu_released)return E_UNEXPECTED;
 const LONG source_width=source_rect.right-source_rect.left,source_height=source_rect.bottom-source_rect.top,
  destination_width=destination_rect.right-destination_rect.left,destination_height=destination_rect.bottom-destination_rect.top;
 if(source_width<=0||source_height<=0||destination_width<=0||destination_height<=0)return E_INVALIDARG;
 RECT clipped_destination{std::clamp<LONG>(destination_rect.left,0,(LONG)destination.width),std::clamp<LONG>(destination_rect.top,0,(LONG)destination.height),
  std::clamp<LONG>(destination_rect.right,0,(LONG)destination.width),std::clamp<LONG>(destination_rect.bottom,0,(LONG)destination.height)};
 if(clipped_destination.right<=clipped_destination.left||clipped_destination.bottom<=clipped_destination.top)return E_INVALIDARG;
 const bool stretched=source_width!=destination_width||source_height!=destination_height;
 if(!stretched){
  const POINT source_origin=capture_stretch_source_point(source_rect,destination_rect,clipped_destination.left,clipped_destination.top);
  const LONG copy_width=std::min<LONG>({clipped_destination.right-clipped_destination.left,(LONG)output_width_-source_origin.x}),
   copy_height=std::min<LONG>({clipped_destination.bottom-clipped_destination.top,(LONG)output_height_-source_origin.y});
  if(source_origin.x<0||source_origin.y<0||copy_width<=0||copy_height<=0)return E_INVALIDARG;
  advance_surface_generation(destination,nullptr);
  scene_ui::Capture command;command.target=scene_ui::surface_handle(destination);
  command.box_left=(UINT)source_origin.x;command.box_top=(UINT)source_origin.y;
  command.box_right=(UINT)(source_origin.x+copy_width);command.box_bottom=(UINT)(source_origin.y+copy_height);
  command.x=(UINT)clipped_destination.left;command.y=(UINT)clipped_destination.top;
  command.width=destination.width;command.height=destination.height;
  scene_ui_pending_=false;scene_native_pending_=false;
  core_post_command(command,"capture");
  core_content_release(destination.id);
  destination.uploaded_generation=destination.generation;destination.uploaded_shape=surface_upload_shape(destination);
  clear_surface_dirty(destination);destination.gpu_captured=true;update_surface_memory_accounting(destination);
  return S_OK;
 }
 if(!(destination.format.dwFlags&DDPF_RGB)||destination.format.dwFlags&DDPF_FOURCC)return E_NOTIMPL;
 HRESULT hr=S_OK;
 if(destination.gpu_captured&&FAILED(hr=core_readback_surface(destination)))return hr;
 const RECT clipped_source{std::clamp<LONG>(source_rect.left,0,(LONG)output_width_),std::clamp<LONG>(source_rect.top,0,(LONG)output_height_),
  std::clamp<LONG>(source_rect.right,0,(LONG)output_width_),std::clamp<LONG>(source_rect.bottom,0,(LONG)output_height_)};
 if(clipped_source.right<=clipped_source.left||clipped_source.bottom<=clipped_source.top)return E_INVALIDARG;
 std::vector<std::uint8_t> bgra;
 if(FAILED(hr=core_readback(true,0,0,clipped_source,bgra)))return hr;
 const size_t row_pitch=(size_t)(clipped_source.right-clipped_source.left)*4u;
 const DWORD bytes=std::max<DWORD>(1,destination.format.dwRGBBitCount/8);
 const size_t required=(size_t)destination.pitch*destination.height;
 std::uint8_t* pixels=nullptr;
 try{if(destination.pixels.size()<required)destination.pixels.resize(required);pixels=destination.pixels.data();}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 for(LONG y=clipped_destination.top;y<clipped_destination.bottom;++y)for(LONG x=clipped_destination.left;x<clipped_destination.right;++x){
  const POINT source=capture_stretch_source_point(source_rect,destination_rect,x,y);
  if(source.x<clipped_source.left||source.x>=clipped_source.right||source.y<clipped_source.top||source.y>=clipped_source.bottom)continue;
  const auto* row=reinterpret_cast<const std::uint32_t*>(bgra.data()+(size_t)(source.y-clipped_source.top)*row_pitch);
  const std::uint32_t pixel=row[source.x-clipped_source.left];
  const BYTE b=(BYTE)pixel,g=(BYTE)(pixel>>8),r=(BYTE)(pixel>>16),a=(BYTE)(pixel>>24);
  const DWORD raw=pack_channel(r,destination.format.dwRBitMask)|pack_channel(g,destination.format.dwGBitMask)|
   pack_channel(b,destination.format.dwBBitMask)|pack_channel(a,destination.format.dwRGBAlphaBitMask);
  std::memcpy(pixels+(size_t)y*destination.pitch+(size_t)x*bytes,&raw,std::min<DWORD>(bytes,4));
 }
 destination.gpu_captured=false;
 mark_surface_dirty(destination,clipped_destination);
 advance_surface_generation(destination,&clipped_destination);
 update_surface_memory_accounting(destination);
 return S_OK;
}
#endif

// Game thread, DD::presentVisual: what queue_present copies, as a record.
// Waits only while the host is more than frames_ahead presents behind.
#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
bool Renderer::core_take_display_apply(DisplaySettings& settings) noexcept { return false; }
#else
bool Renderer::core_take_display_apply(DisplaySettings& settings) noexcept{
 auto& control=x64remote::control();
 const long serial=control.display_apply_serial;
 if(serial==core_display_apply_seen_)return false;
 core_display_apply_seen_=serial;MemoryBarrier();
 settings.mode=static_cast<DisplayMode>(control.display_mode);
 settings.window_width=control.display_width;settings.window_height=control.display_height;
 return true;
}
#endif

#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
void Renderer::core_display_apply_result(const DisplaySettings& settings,HRESULT result,const char* message) noexcept {  }
#else
void Renderer::core_display_apply_result(const DisplaySettings& settings,HRESULT result,const char* message) noexcept try{
 scene_core::DisplayApplyResult value;
 value.mode=static_cast<std::int32_t>(settings.mode);value.width=settings.window_width;value.height=settings.window_height;
 value.result=static_cast<std::int32_t>(result);value.message=message?message:"";
 std::vector<std::uint8_t> bytes{0};scene_wire::encode_into(bytes,value);
 core_send(static_cast<std::uint16_t>(scene_core::Command::display_apply_result),bytes);
}catch(...){}
#endif

// The game's resource mounts for the host's own resource readers (terrain
// pages, tile families, critters, precipitation): when they change, from
// core_ensure on (a map published before the first present needs them).
#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
void Renderer::core_send_mounts() noexcept {  }
#else
void Renderer::core_send_mounts() noexcept try{
 scene_core::ResourceMounts mounts;
 if(!owned_resource_runtime_mounts(mounts.mounts))return;
 owned_text_resource_mounts(mounts.text_mounts);
 owned_data_resource_mounts(mounts.data_mounts);
 std::vector<std::uint8_t> encoded=scene_wire::encode(mounts);
 if(encoded==core_mounts_sent_)return;
 std::vector<std::uint8_t> bytes{0};bytes.insert(bytes.end(),encoded.begin(),encoded.end());
 core_send(static_cast<std::uint16_t>(scene_core::Command::resource_mounts),bytes);
 core_mounts_sent_=std::move(encoded);
}catch(...){}
#endif

#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
void Renderer::core_sync_combat() noexcept {  }
#else
void Renderer::core_sync_combat() noexcept try{
 const std::uint64_t revision=combat_ui_game_revision();
 if(revision==core_combat_revision_sent_)return;
 scene_core::CombatState state;
 if(!combat_ui_encode_game(state.bytes,core_combat_catalog_sent_,core_combat_font_sent_)){core_refuse("combat_state");return;}
 std::vector<std::uint8_t> bytes{0};scene_wire::encode_into(bytes,state);
 core_send(static_cast<std::uint16_t>(scene_core::Command::combat_state),bytes);
 core_combat_revision_sent_=revision;
}catch(...){core_refuse("combat_state");}
#endif

#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
HRESULT Renderer::core_present() { return E_NOTIMPL; }
#else
HRESULT Renderer::core_present(){
 report_core_refusal_window();
 // The retained stream stopped: the host's replica (its world) is incomplete.
 if(retained_publication_failed())x64remote::lose_device("core mode: the retained publication stream failed (see bridge.jsonl)");
 x64remote::check_host();
 if(const long lost=x64remote::link_removed())return static_cast<HRESULT>(lost);
 owned_resource_runtime_poll();
 camp_inventory_set_wide(configured_integer(debug_overlay_.config(),"camp_ui_layout",1)!=0);
 // Harness steps the host finished, in order, before this present takes the next one.
 {auto& control=x64remote::control();
  while(core_harness_acks_read_<control.harness_acks_written){
   MemoryBarrier();
   const auto& ack=control.harness_acks[static_cast<std::uint32_t>(core_harness_acks_read_)%x64remote::kHarnessAcks];
   char detail[sizeof(ack.detail)+1]{};std::memcpy(detail,ack.detail,sizeof(ack.detail));
   harness_complete_entry(ack.entry_id,ack.ok!=0,detail);
   ++core_harness_acks_read_;
  }
  // The host's camp popup outcomes (scene-thread answers in scene mode).
  for(int answer=0;answer<3;++answer){
   const long count=control.camp_answers[answer];
   if(count==core_camp_answers_seen_[answer])continue;
   core_camp_answers_seen_[answer]=count;
   if(answer==0)camp_inventory_popup_published(false);else if(answer==1)camp_inventory_popup_failed();else camp_inventory_art_failed();
  }
  // A progress-movie decode the host asked for: the game's binkw32 decodes
  // here (the game thread) and the frames go to the host.
  if(const std::uint64_t request=static_cast<std::uint64_t>(InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(&control.progress_serial),0,0));request&&request!=core_progress_seen_){
   MemoryBarrier();
   core_progress_seen_=request;
   char path[sizeof(control.progress_path)+1]{};std::memcpy(path,control.progress_path,sizeof(control.progress_path));
   try{
    scene_core::ProgressFrames frames;
    frames.serial=request;frames.width=control.progress_width;frames.height=control.progress_height;
    frames.failure=core_decode_progress_pixels(path,frames.width,frames.height,control.progress_frames,control.progress_shown,frames.frames);
    if(frames.failure)frames.frames.clear();
    std::vector<std::uint8_t> bytes{0};scene_wire::encode_into(bytes,frames);
    core_send(static_cast<std::uint16_t>(scene_core::Command::progress_frames),bytes);
   }catch(...){bridge_report_event("load_progress","core mode: the decoded frames could not be sent (allocation)");}
  }
  // The host dropped cached UI quads this mirror still claimed: resend them.
  if(control.overlay_mirror_resets!=core_overlay_resets_seen_){
   core_overlay_resets_seen_=control.overlay_mirror_resets;overlay_mirror_.clear();
  }
  bridge_set_terrain_lod_hidden_distance(float(control.terrain_lod_hidden_cm)*.01f);}
 try{
  scene_core::Present command;
  auto& inputs=command.inputs;
  // Import a completed host/F10 save before applying this present's config
  // command. Reloading afterward could discard the acknowledged session
  // value before core_send_config delivered it to the host.
  {const long saved=x64remote::control().config_saved;
   if(saved!=core_config_saved_seen_){core_config_saved_seen_=saved;debug_overlay_.reload_config();apply_config_settings();}}
  inputs.harness_command_valid=harness_present_step_copy(inputs.harness_command,inputs.harness_entry_id,
   inputs.harness_camera_steps,inputs.harness_on_travel_map);
  frame_stall::note_harness_step(inputs.harness_command_valid,inputs.harness_command.verb);
  inputs.harness_banner=harness_banner_text();
  // A config step changes this process's options; the host takes them with
  // the config command below, before this present (the host owns the ini).
  if(inputs.harness_command_valid&&inputs.harness_command.verb==HarnessVerb::config){
   if(inputs.harness_entry_id!=core_harness_config_entry_){
    std::string detail;const bool ok=apply_harness_config(inputs.harness_command,detail);
    harness_complete_entry(inputs.harness_entry_id,ok,detail);core_harness_config_entry_=inputs.harness_entry_id;
   }
   inputs.harness_command_valid=false;
  }
  debug_overlay_.maintain(); // cursor confinement and focus (window hook mode)
  // The host's neighbour border fog: the game's per-entity fog (WorldScene
  // fog table, neighbour_continued_border_fog) continues the band with it.
  {auto& control=x64remote::control();
   const long before=control.border_fog_sequence;
   if(!(before&1)){
    MemoryBarrier();
    const std::uint64_t revision=control.border_fog_revision;
    double v[17];std::memcpy(v,const_cast<const double*>(control.border_fog),sizeof(v));
    MemoryBarrier();
    if(control.border_fog_sequence==before&&revision&&revision!=core_border_fog_seen_){
     core_border_fog_seen_=revision;
     NeighbourBorderFog fog;
     fog.active={v[0],v[1],v[2],v[3]};fog.edges={v[4],v[5],v[6],v[7]};fog.range=v[8];
     for(int side=0;side<4;++side){fog.contacts.lo[side]=v[9+side];fog.contacts.hi[side]=v[13+side];}
     neighbour_border_fog_publish(fog);
    }
   }}
  core_send_config();
  // The game side's weather reader takes the visual test and strength
  // settings from the options (the host's precipitation pass sets them in
  // standard mode); the hook status goes to the host when it changes.
  {const auto& config=debug_overlay_.config();
   const int override_strength=configured_integer(config,"precip_strength_override",0);
   bridge_set_weather_visual(configured_integer(config,"precip_test",0),
    override_strength>0?override_strength:configured_integer(config,"precip_strength",0));
   scene_core::GameStatus status;
   status.owned_sky_installed=bridge_owned_sky_installed();status.owned_weather_problem=bridge_owned_weather_problem();
   status.semantic_slots_installed=bridge_semantic_slots_installed();status.game_sites_all_verified=bridge_game_sites_all_verified();
   status.camera_pan_unavailable=bridge_camera_pan_smoothing_unavailable_reason();
   status.addon_notice=bridge_addon_notice();
   const char* addon_notice=status.addon_notice?status.addon_notice:"";
   const std::uint8_t now[3]={std::uint8_t(status.owned_sky_installed),std::uint8_t(status.semantic_slots_installed),
    std::uint8_t(status.game_sites_all_verified)};
   if(std::memcmp(now,core_game_status_sent_,3)!=0||status.owned_weather_problem!=core_weather_problem_sent_||
      status.camera_pan_unavailable!=core_camera_pan_unavailable_sent_||core_addon_notice_sent_!=addon_notice){
    std::memcpy(core_game_status_sent_,now,3);core_weather_problem_sent_=status.owned_weather_problem;
    core_camera_pan_unavailable_sent_=status.camera_pan_unavailable;
    core_addon_notice_sent_=addon_notice;
    std::vector<std::uint8_t> status_bytes{0};scene_wire::encode_into(status_bytes,status);
    core_send(static_cast<std::uint16_t>(scene_core::Command::game_status),status_bytes);
   }}
  // The owned interface layout the host composes the HUD with.
  {scene_core::WidescreenState state;state.interface_owned=widescreen_interface_owned();
   if(state.interface_owned)state.layout=widescreen_layout();
   std::vector<std::uint8_t> encoded=scene_wire::encode(state);
   if(encoded!=core_widescreen_sent_){
    std::vector<std::uint8_t> bytes{0};bytes.insert(bytes.end(),encoded.begin(),encoded.end());
    core_send(static_cast<std::uint16_t>(scene_core::Command::widescreen_state),bytes);
    core_widescreen_sent_=std::move(encoded);
   }}
  core_send_mounts(); // the game's resource mounts for the host's own resource readers
  // The host's overlay draws the combat UI at every present, also on frames
  // that carry only a loading picture (PresentImage), where no world or UI
  // command synced it: its last mission, portraits and effects stayed and
  // were drawn over the loading screen.
  core_sync_combat();
  // This process's address space per load (the renderer's load_reveal line
  // reports the host's in core mode): 10 s after the world epoch changes.
  {const std::uint64_t epoch=bridge_current_world_epoch();const ULONGLONG now=GetTickCount64();
   if(epoch&&epoch!=core_va_epoch_){core_va_epoch_=epoch;core_va_due_ms_=now+10000;}
   if(core_va_due_ms_&&now>=core_va_due_ms_){core_va_due_ms_=0;report_game_address_space(epoch);}
   // The F10 memory line on the host: this process's address space, at most
   // once a second while the overlay takes input.
   static ULONGLONG va_line_ms=0;
   auto& control=x64remote::control();
   if((control.overlay_flags&1)&&now-va_line_ms>=1000){
    va_line_ms=now;std::uint64_t used=0,largest_free=0;address_space_mb(used,largest_free,true);
    InterlockedExchange(&control.game_va_used_mb,static_cast<long>(used));
    InterlockedExchange(&control.game_va_largest_free_mb,static_cast<long>(largest_free));
   }}
  // F11: the capture runs on the host Renderer (edge, game window focused).
  {const bool down=(GetAsyncKeyState(VK_F11)&0x8000)!=0;
   if(down&&!core_f11_down_&&GetForegroundWindow()==hwnd_){
    scene_core::CaptureRequest capture{};capture.package=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0; // Shift+F11: the diagnostics zip too
    std::vector<std::uint8_t> request{0};scene_wire::encode_into(request,capture);
    core_send(static_cast<std::uint16_t>(scene_core::Command::capture_request),request);
   }
   core_f11_down_=down;}
  command.serial=++core_present_serial_;
  {const HWND front=GetForegroundWindow();
   command.game_foreground=!hwnd_||front==hwnd_;
   // Which window took the front (one event per change): frame generation,
   // the background limit and the interface's input stop while it is not ours.
   if(front!=core_foreground_logged_){core_foreground_logged_=front;
    char cls[64]{},title[64]{},exe[MAX_PATH]{};DWORD pid=0;
    if(front){GetClassNameA(front,cls,sizeof(cls));GetWindowTextA(front,title,sizeof(title));GetWindowThreadProcessId(front,&pid);}
    if(pid){if(HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)){DWORD size=MAX_PATH;
      if(QueryFullProcessImageNameA(process,0,exe,&size)){const char* slash=std::strrchr(exe,'\\');if(slash)std::memmove(exe,slash+1,std::strlen(slash));}
      CloseHandle(process);}}
    char text[400];std::snprintf(text,sizeof(text),"present %llu: game window %s; foreground %p class '%s' title '%s' pid %lu %s%s (root owner %p)",
     static_cast<unsigned long long>(core_present_serial_),command.game_foreground?"in front":"NOT in front",static_cast<void*>(front),cls,title,
     static_cast<unsigned long>(pid),exe,pid==GetCurrentProcessId()?" (game process)":"",front?static_cast<void*>(GetAncestor(front,GA_ROOTOWNER)):nullptr);
    bridge_report_event("game_foreground",text);}}
  command.frame_start_qpc=core_frame_start_qpc_;
  if(!bridge_capture_frame_values(inputs.bridge))return E_FAIL;
  inputs.recorder_present_entry=perf_now();
  inputs.game_frame=fa_game_frame_;
  try{widescreen_hud_asset_frames(inputs.hud_frames);}catch(...){inputs.hud_frames.clear();}
  std::vector<std::uint8_t> bytes{0};
  scene_wire::encode_into(bytes,command);
  core_send(static_cast<std::uint16_t>(scene_core::Command::present),bytes);
  core_packet_store_frame();
  // Records reach the host only when published (the writer batches up to
  // 256 KB): the present closes the frame, so it publishes everything.
  x64remote::publish();
  {const stall_watch::Exempt wait;const frame_acct::Scope wait_scope(frame_acct::wait_present_slot);
   const std::int64_t wait_start=perf_now();
   x64remote::wait_presents(command.serial);
   frame_stall::g_host_wait_ms+=double(perf_now()-wait_start)*1000.0/double(perf_frequency());
   // latency_pacing (host-set): start the next frame later, so its present
   // reaches the host when it can take it instead of queueing in the ring.
   // A high-resolution timer to the last half millisecond, then a spin.
   if(const long delay_us=x64remote::control().release_delay_us;delay_us>0){
    const std::int64_t frequency=perf_frequency(),deadline=perf_now()+std::int64_t(delay_us)*frequency/1000000;
    static HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
    const std::int64_t spin=frequency/2000,left=deadline-perf_now();
    if(timer&&left>spin){LARGE_INTEGER due{};due.QuadPart=-static_cast<LONGLONG>((left-spin)*10000000/frequency);
     if(SetWaitableTimerEx(timer,&due,0,nullptr,nullptr,nullptr,0))WaitForSingleObject(timer,100);}
    while(perf_now()<deadline)YieldProcessor();
   }}
  core_frame_start_qpc_=perf_now();  // the next frame starts here (the host's latency measurement)
 }catch(...){
  x64remote::lose_device("core mode: a present could not be sent (allocation)");
 }
 x64remote::report_frame();
 return static_cast<HRESULT>(x64remote::link_removed());
}
#endif

// Core mode: the camp icons are decoded on the host (Host/core_camp.cpp), which
// answers their alpha facts; this process never runs a host command.
namespace scene_ui {
int core_camp_icon_home(Renderer&,std::uint64_t,std::uint64_t,CampIconHome&) noexcept { return 0; }
bool core_camp_icon_unmapped(std::uint64_t,std::uint64_t) noexcept { return false; }
void core_camp_icon_outside(std::uint64_t,std::uint64_t,bool,bool) noexcept {}
bool core_camp_icon_texels(std::uint64_t,std::uint64_t,std::uint64_t,float&,float&) noexcept { return false; }
void core_camp_icon_density(std::uint64_t,std::uint64_t,float,float) noexcept {}
}

} // namespace owned
