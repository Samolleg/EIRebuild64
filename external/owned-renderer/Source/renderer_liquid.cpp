// Liquid rendering: generated normal/detail maps, screen-space reflections,
// the interactive ripple simulation and ripple constants. Owns every water
// resource of the Renderer; the liquid surface shading itself lives in
// liquid_surface_shader.h; the generated maps in liquid_maps.h.
#include "liquid_surface_shader.h"
#include "liquid_maps.h"
#include "liquid_waves.h"
#include "renderer.h"
#include "engine_bridge.h"
#include "owned_asset_loader.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace owned {
namespace {
// Uploads a generated RGBA8 texture with its full mip chain (liquid_map_mips).
HRESULT create_generated_liquid_texture(ID3D11Device* device,UINT size,std::vector<std::array<float,4>> level,
                                        bool unit_normal,ComPtr<ID3D11ShaderResourceView>& view) {
 const auto mips=liquid_map_mips(size,std::move(level),unit_normal);
 std::vector<D3D11_SUBRESOURCE_DATA> data;
 UINT side=size;for(const auto& mip:mips){data.push_back({mip.data(),side*4u,0});side=std::max(1u,side/2);}
 D3D11_TEXTURE2D_DESC description{};description.Width=description.Height=size;
 description.MipLevels=static_cast<UINT>(mips.size());description.ArraySize=1;
 description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.SampleDesc.Count=1;
 description.Usage=D3D11_USAGE_IMMUTABLE;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 ComPtr<ID3D11Texture2D> texture;
 HRESULT hr=device->CreateTexture2D(&description,data.data(),&texture);if(FAILED(hr))return hr;
 return device->CreateShaderResourceView(texture.Get(),nullptr,&view);
}
}

// Seamless liquid textures (liquid_maps.h), generated once per device on a
// worker started at device creation (the generation costs about half a second
// of CPU; on the render thread it stalled the first world frame with water).
// The device is free-threaded, so the worker creates the immutable textures
// too; no CPU copy outlives the job.
void Renderer::start_liquid_maps_job() noexcept {
 if(liquid_maps_job_.valid()||(liquid_normal_srv_&&liquid_detail_srv_)||!device_)return;
 try{
  liquid_maps_job_=std::async(std::launch::async,[device=device_]()->LiquidMapsPrepared{
   LiquidMapsPrepared out;
   try{
    auto levels=generate_liquid_map_levels();
    out.lava_samples=std::move(levels.lava_samples);
    out.hr=create_generated_liquid_texture(device.Get(),LiquidMapLevels::normal_size,std::move(levels.normals),true,out.normal);
    if(SUCCEEDED(out.hr))out.hr=create_generated_liquid_texture(device.Get(),LiquidMapLevels::detail_size,std::move(levels.detail),false,out.detail);
   }catch(const std::bad_alloc&){out.hr=E_OUTOFMEMORY;}
   return out;
  });
 }catch(...){} // ensure_liquid_normal_map generates them on first use instead
}

HRESULT Renderer::ensure_liquid_normal_map() try {
 if(liquid_normal_srv_&&liquid_detail_srv_)return S_OK;
 ComPtr<ID3D11ShaderResourceView> normal_view,detail_view;ComPtr<ID3D11SamplerState> sampler;
 HRESULT hr=S_OK;
 if(liquid_maps_job_.valid()){
  auto prepared=liquid_maps_job_.get();
  if(FAILED(hr=prepared.hr))return hr;
  liquid_lava_samples_=std::move(prepared.lava_samples);
  normal_view=std::move(prepared.normal);detail_view=std::move(prepared.detail);
 }else{
  auto levels=generate_liquid_map_levels();
  liquid_lava_samples_=std::move(levels.lava_samples);
  if(FAILED(hr=create_generated_liquid_texture(device_.Get(),LiquidMapLevels::normal_size,std::move(levels.normals),true,normal_view)))return hr;
  if(FAILED(hr=create_generated_liquid_texture(device_.Get(),LiquidMapLevels::detail_size,std::move(levels.detail),false,detail_view)))return hr;
 }
 D3D11_SAMPLER_DESC sampling{};sampling.Filter=D3D11_FILTER_ANISOTROPIC;sampling.MaxAnisotropy=8;
 sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sampling.MaxLOD=D3D11_FLOAT32_MAX;
 if(FAILED(hr=device_->CreateSamplerState(&sampling,&sampler)))return hr;
 liquid_normal_srv_=std::move(normal_view);liquid_detail_srv_=std::move(detail_view);
 liquid_normal_sampler_=std::move(sampler);return S_OK;
}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}

void Renderer::bind_liquid_swamp_frame() noexcept {
 if(!device_||!context_)return;
 if(liquid_swamp_frame_generation_!=ambient_map_generation_){
  liquid_swamp_frame_srv_.Reset();
  if(const auto terrain=ambient_terrain_.lock()){
   liquid_swamp_frame_generation_=ambient_map_generation_;
   const UINT side=terrain->swamp_frame_side;
   if(side&&terrain->swamp_frame_texels.size()==std::size_t(side)*side)
    try{if(FAILED(create_generated_liquid_texture(device_.Get(),side,terrain->swamp_frame_texels,false,liquid_swamp_frame_srv_)))liquid_swamp_frame_srv_.Reset();}
    catch(const std::bad_alloc&){liquid_swamp_frame_srv_.Reset();}
  }
 }
 // Unbound (no frame), flagged tiles take no swamp (nativeSwampArt).
 ID3D11ShaderResourceView* frame=liquid_swamp_frame_srv_.Get();
 context_->PSSetShaderResources(33,1,&frame);
}

void Renderer::clear_water_reflection_bindings() noexcept {
 if(!context_)return;
 ID3D11ShaderResourceView* empty=nullptr;
 ID3D11Buffer* constant=nullptr;ID3D11SamplerState* sampler=nullptr;
 context_->PSSetShaderResources(3,1,&empty);context_->PSSetShaderResources(19,1,&empty);context_->PSSetShaderResources(28,1,&empty);
 context_->PSSetConstantBuffers(7,1,&constant);context_->PSSetConstantBuffers(8,1,&constant);context_->PSSetSamplers(5,1,&sampler);
}

void append_liquid_wave_shader_programs(std::vector<ShaderProgram>& out){
 out.push_back({{kLiquidWaveShader,sizeof(kLiquidWaveShader)-1},nullptr,nullptr,"liquidWaveCS","cs_5_0",0});
}

void Renderer::step_liquid_waves(NativeLiquidRipples& values,double seconds) {
 values.wave={};
 if(liquid_wave_failed_)return;
 const auto fail=[&](const char* reason){liquid_wave_failed_=true;OutputDebugStringA(reason);};
 if(!liquid_wave_cs_) {
  ComPtr<ID3DBlob> code,errors;std::vector<ShaderProgram> programs;append_liquid_wave_shader_programs(programs);
  if(FAILED(compile_shader(programs[0],&code,&errors))||
     FAILED(device_->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&liquid_wave_cs_)))
   return fail("EID3D11 liquid ripples: wave shader unavailable; character ripples omitted.\n");
  D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=kLiquidWaveSize;desc.MipLevels=desc.ArraySize=1;
  desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.SampleDesc.Count=1; // height, previous height, trail
  desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
  for(unsigned k=0;k<2;++k)
   if(FAILED(device_->CreateTexture2D(&desc,nullptr,&liquid_wave_texture_[k]))||
      FAILED(device_->CreateShaderResourceView(liquid_wave_texture_[k].Get(),nullptr,&liquid_wave_srv_[k]))||
      FAILED(device_->CreateUnorderedAccessView(liquid_wave_texture_[k].Get(),nullptr,&liquid_wave_uav_[k])))
    return fail("EID3D11 liquid ripples: wave field unavailable; character ripples omitted.\n");
  const float zero[4]{};for(auto& view:liquid_wave_uav_)context_->ClearUnorderedAccessViewFloat(view.Get(),zero);
  D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(LiquidWaveStepConstants);buffer.Usage=D3D11_USAGE_DYNAMIC;
  buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  if(FAILED(device_->CreateBuffer(&buffer,nullptr,&liquid_wave_constants_)))
   return fail("EID3D11 liquid ripples: wave constants unavailable; character ripples omitted.\n");
 }
 // The window follows the mean position of units in water, in whole texels.
 const unsigned count=std::min<unsigned>(static_cast<unsigned>(values.meta[0]),16u);
 std::array<int,2> origin=liquid_wave_origin_;
 if(count) {
  float x=0,y=0;for(unsigned n=0;n<count;++n){x+=values.position_speed[n][0];y+=values.position_speed[n][1];}
  origin={int(std::floor(x/count/kLiquidWaveCell))-kLiquidWaveSize/2,int(std::floor(y/count/kLiquidWaveCell))-kLiquidWaveSize/2};
 }
 if(!liquid_wave_origin_valid_&&!count)return;
 std::array<int,2> shift{};
 if(liquid_wave_origin_valid_){shift={origin[0]-liquid_wave_origin_[0],origin[1]-liquid_wave_origin_[1]};}
 else shift={kLiquidWaveSize,kLiquidWaveSize}; // first use: read nothing, start flat
 liquid_wave_origin_=origin;liquid_wave_origin_valid_=true;
 // Fixed 30 Hz game-time steps keep the explicit integration stable at any frame rate.
 constexpr double step=kLiquidWaveStep;
 if(liquid_wave_clock_<0||seconds<liquid_wave_clock_)liquid_wave_clock_=seconds;
 // Up to 8 steps per frame: x3 game speed at 30 fps needs about 5.
 liquid_wave_accumulator_=std::min(liquid_wave_accumulator_+seconds-liquid_wave_clock_,8*step);liquid_wave_clock_=seconds;
 unsigned steps=static_cast<unsigned>(liquid_wave_accumulator_/step);liquid_wave_accumulator_-=steps*step;
 const bool copy_only=steps==0;
 if(copy_only&&(shift[0]||shift[1]))steps=1;
 ID3D11ShaderResourceView* none=nullptr;context_->PSSetShaderResources(24,1,&none);
 context_->CSSetShader(liquid_wave_cs_.Get(),nullptr,0);
 context_->CSSetConstantBuffers(0,1,liquid_wave_constants_.GetAddressOf());
 for(unsigned n=0;n<steps;++n) {
  LiquidWaveStepConstants constants{};
  constants.origin[0]=origin[0]*kLiquidWaveCell;constants.origin[1]=origin[1]*kLiquidWaveCell;
  constants.cell=kLiquidWaveCell;constants.courant=copy_only?0.f:kLiquidWaveCourant;constants.damping=kLiquidWaveDamping;
  constants.shift[0]=n==0?shift[0]:0;constants.shift[1]=n==0?shift[1]:0;constants.units=count;
  const double step_seconds=seconds-(steps-1-n)*step;
  for(unsigned u=0;u<count;++u){
   liquid_wave_unit(values.position_speed[u][0],values.position_speed[u][1],values.position_speed[u][3],
                    values.phase[u][1],values.phase[u][0],step_seconds,constants.unit[u]);
   liquid_river_velocity(values.flow[u][0],values.flow[u][1],constants.flow[u]);
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(FAILED(context_->Map(liquid_wave_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))break;
  std::memcpy(mapped.pData,&constants,sizeof(constants));context_->Unmap(liquid_wave_constants_.Get(),0);
  const unsigned next=liquid_wave_current_^1u;
  context_->CSSetShaderResources(0,1,liquid_wave_srv_[liquid_wave_current_].GetAddressOf());
  context_->CSSetUnorderedAccessViews(0,1,liquid_wave_uav_[next].GetAddressOf(),nullptr);
  context_->Dispatch(kLiquidWaveSize/8,kLiquidWaveSize/8,1);
  ID3D11UnorderedAccessView* no_uav=nullptr;context_->CSSetUnorderedAccessViews(0,1,&no_uav,nullptr);
  context_->CSSetShaderResources(0,1,&none);
  liquid_wave_current_=next;
 }
 context_->CSSetShader(nullptr,nullptr,0);
 values.wave={origin[0]*kLiquidWaveCell,origin[1]*kLiquidWaveCell,1.f/(kLiquidWaveSize*kLiquidWaveCell),1.f};
}

// Wake easing: motion (the running V wake and contact foam) rises over 0.5
// game seconds once a unit moves faster than 0.3 m/s and falls over 1 s when
// it stops; presence (parting and contact line) does the same on entering
// and leaving the water. While the motion fades the unit keeps the speed and
// heading it last ran with, so the wedge keeps its shape and only fades.
// The wave field already stepped with the raw units (its trail decays as
// before); this changes only what the liquid shader draws (flow z, w).
void Renderer::ease_liquid_units(NativeLiquidRipples& values,double seconds) {
 constexpr float kMotionIn=.5f,kMotionOut=1.f,kPresenceIn=.5f,kPresenceOut=1.f,kMoving=.3f*kLiquidWaveSecondsPerTick,kHeadingLag=.6f;
 float dt=liquid_ease_clock_<0?0.f:float(seconds-liquid_ease_clock_);
 if(!(dt>=0&&dt<.5f))dt=0; // paused holds, loads and jumps do not fade
 liquid_ease_clock_=seconds;
 for(auto& unit:liquid_unit_ease_)unit.seen=false;
 const unsigned count=std::min<unsigned>(static_cast<unsigned>(values.meta[0]),16u);
 for(unsigned n=0;n<count;++n) {
  auto found=std::find_if(liquid_unit_ease_.begin(),liquid_unit_ease_.end(),[&](const LiquidUnitEase& u){return u.id==values.id[n];});
  if(found==liquid_unit_ease_.end()){liquid_unit_ease_.push_back({});found=liquid_unit_ease_.end()-1;found->id=values.id[n];}
  auto& unit=*found;unit.seen=true;
  const bool moving=values.position_speed[n][3]>kMoving&&
      values.phase[n][2]*values.phase[n][2]+values.phase[n][3]*values.phase[n][3]>.25f;
  unit.motion=moving?std::min(1.f,unit.motion+dt/kMotionIn):std::max(0.f,unit.motion-dt/kMotionOut);
  unit.presence=std::min(1.f,unit.presence+dt/kPresenceIn);
  // The wedge's heading turns toward the unit's along the shortest arc with
  // a 0.6 s time constant: a wake is left behind along the path, so it bends
  // after a turning unit instead of rotating 1:1 with the body.
  if(moving){
   const float target=std::atan2(values.phase[n][3],values.phase[n][2]);
   if(!unit.heading_valid){unit.heading=target;unit.heading_valid=true;}
   else{
    float turn=std::remainder(target-unit.heading,6.2831853f);
    unit.heading=std::remainder(unit.heading+turn*(1.f-std::exp(-dt/kHeadingLag)),6.2831853f);
   }
  }
  // Stopped: the current place with the last running speed (the wedge fades).
  const float speed=unit.position_speed[3];
  unit.position_speed=values.position_speed[n];unit.phase=values.phase[n];
  if(!moving&&unit.motion>0)unit.position_speed[3]=speed;
  if(unit.heading_valid){unit.phase[2]=std::cos(unit.heading);unit.phase[3]=std::sin(unit.heading);}
  unit.flow={values.flow[n][0],values.flow[n][1],unit.motion,unit.presence};
 }
 // Units gone from the water fade out where they were last.
 for(auto& unit:liquid_unit_ease_)if(!unit.seen){
  unit.motion=std::max(0.f,unit.motion-dt/kMotionOut);unit.presence=std::max(0.f,unit.presence-dt/kPresenceOut);
  unit.flow[2]=unit.motion;unit.flow[3]=unit.presence;
 }
 liquid_unit_ease_.erase(std::remove_if(liquid_unit_ease_.begin(),liquid_unit_ease_.end(),
  [](const LiquidUnitEase& u){return !u.seen&&u.motion<=0&&u.presence<=0;}),liquid_unit_ease_.end());
 // The shader's unit list: present units in their order, then fading ones.
 unsigned slot=0;
 for(int pass=0;pass<2;++pass)for(const auto& unit:liquid_unit_ease_){
  if(unit.seen!=(pass==0)||slot>=16)continue;
  values.position_speed[slot]=unit.position_speed;values.phase[slot]=unit.phase;values.flow[slot]=unit.flow;values.id[slot]=unit.id;++slot;
 }
 values.meta[0]=float(slot);
}

// Distance from a unit's position beyond which liquid_surface.hlsl's unit
// terms add exactly nothing (it mirrors those functions and constants):
// - liquidUnitWake: 0 below 0.2 m/s or with |heading| < 0.5; else only for
//   behind in [-r, len] and lateral <= len*liquidWakeTan + 2r, measured along
//   a heading of length >= 0.5 (so divided by min(|heading|,1)); evaluated
//   up to 0.04 away on each axis (the finite difference);
// - liquidUnitContactFoam: exp(-d^2/(1.3 r^2)) is 0 in float32 (denormals
//   flush) past about 10.7 r; 12 r leaves room for the hardware exp;
// - liquidUnitContact: its own box, max(4, 1 + liquidContactTail) r_c + 0.3
//   with r_c = liquidContactRadius r, so its diagonal.
static float liquid_unit_reach(const NativeLiquidRipples& values,unsigned u) noexcept {
 constexpr float seconds_per_tick=.055f,wake_length=2.5f,wake_tan=.354f,contact_radius=.55f,contact_tail=3.f,difference=.04f;
 const auto& ps=values.position_speed[u];const auto& ph=values.phase[u];
 const float r=std::max(ph[1],.05f),speed=ps[3]/seconds_per_tick;
 const float heading=std::sqrt(ph[2]*ph[2]+ph[3]*ph[3]);
 float reach=12.f*r;
 if(!(speed<.19f)&&!(heading<.49f)){
  const float len=r*(wake_length+1.5f*std::min(speed,4.f));
  reach=std::max(reach,(len*(1.f+wake_tan)+3.f*r)/std::min(heading,1.f)+1.415f*difference);
 }
 const float contact=std::max(4.f,1.f+contact_tail)*r*contact_radius+.3f;
 reach=std::max(reach,1.415f*contact);
 return std::isfinite(reach)?reach:std::numeric_limits<float>::infinity();
}

// Binds, for one liquid batch, only the frame's wading units within reach of
// its water (in their frame order, so each pixel's sums and maxima are
// unchanged); the frame's full set stays bound when every unit is kept.
// lo/hi: the batch's rest XY bounds. The water moves from rest by the live
// wind sway (prepareNativeVertex: up to 3 x |wind force| x the wave
// multiplier), the liquid tessellation (|liquidTessDisplacement| <=
// liquidTessLip + liquidTessColumns/2 + liquidTessRipple.x = .5, along a
// horizontal of length <= 1) and the foam layer lift (liquidFoamLayerLift
// .06 along a unit normal).
void Renderer::bind_liquid_ripple_units(const float lo[2],const float hi[2]) {
 if(!liquid_ripple_ready_)return;
 const auto& values=liquid_ripple_values_;
 const unsigned count=std::min<unsigned>(static_cast<unsigned>(values.meta[0]),16u);
 if(!count)return;
 const float margin=3.f*std::fabs(native_terrain_wind_force_)*values.reach[0]+.5f+.06f;
 if(!std::isfinite(margin))return;
 std::array<std::uint8_t,17> list{};unsigned kept=0;
 for(unsigned u=0;u<count;++u){
  const float r=liquid_unit_reach_[u]+margin;const auto& p=values.position_speed[u];
  if(!(p[0]<lo[0]-r||p[0]>hi[0]+r||p[1]<lo[1]-r||p[1]>hi[1]+r))list[1+kept++]=static_cast<std::uint8_t>(u);
 }
 if(kept==count)return;
 list[0]=static_cast<std::uint8_t>(kept);
 if(liquid_ripple_batch_serial_!=liquid_ripple_values_serial_||list!=liquid_ripple_batch_list_){
  NativeLiquidRipples culled=values;culled.meta[0]=static_cast<float>(kept);
  for(unsigned n=0;n<kept;++n){
   const unsigned u=list[1+n];
   culled.position_speed[n]=values.position_speed[u];culled.phase[n]=values.phase[u];
   culled.flow[n]=values.flow[u];culled.id[n]=values.id[u];
  }
  HRESULT hr=S_OK;
  if(!liquid_ripple_batch_constants_){
   D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(culled);desc.Usage=D3D11_USAGE_DYNAMIC;
   desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
   hr=device_->CreateBuffer(&desc,nullptr,&liquid_ripple_batch_constants_);
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(SUCCEEDED(hr))hr=context_->Map(liquid_ripple_batch_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
  if(FAILED(hr))return; // the full set stays bound: the same pixels, without the saving
  std::memcpy(mapped.pData,&culled,sizeof(culled));context_->Unmap(liquid_ripple_batch_constants_.Get(),0);
  liquid_ripple_batch_serial_=liquid_ripple_values_serial_;liquid_ripple_batch_list_=list;
 }
 context_->PSSetConstantBuffers(8,1,liquid_ripple_batch_constants_.GetAddressOf());
}

void Renderer::prepare_liquid_ripples() {
 if(liquid_ripple_frame_!=native_frame_index_) {
  liquid_ripple_ready_=false;
  NativeLiquidRipples values{};
  const bool available=bridge_liquid_ripples(values);
  // Terrain draws can precede the frame's visual publication; retry at later
  // batches instead of caching an empty frame.
  if(available)liquid_ripple_frame_=native_frame_index_;
  // Caustics, interactive waves and ring intensity share the game clock.
  const auto& environment=bridge_world_fog_snapshot().environment;
  const double seconds=environment.valid?environment.world_time*kWeatherSecondsPerSegment:
      (liquid_wave_clock_<0?0:liquid_wave_clock_);
  values.meta[2]=static_cast<float>(std::fmod(seconds,4096.0));
  const float rain=update_weather_surface_rain(weather_wind_,environment);
  values.meta[3]=configured_boolean(debug_overlay_.config(),"water_physical",true)?rain:0.f;
  if(available){step_liquid_waves(values,seconds);ease_liquid_units(values,seconds);}
  liquid_ripple_values_=values;++liquid_ripple_values_serial_;
  for(unsigned u=0;u<16u;++u)liquid_unit_reach_[u]=liquid_unit_reach(values,u);
  HRESULT hr=S_OK;
  if(!liquid_ripple_constants_) {
   D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(values);desc.Usage=D3D11_USAGE_DYNAMIC;
   desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
   hr=device_->CreateBuffer(&desc,nullptr,&liquid_ripple_constants_);
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(SUCCEEDED(hr))hr=context_->Map(liquid_ripple_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
  if(SUCCEEDED(hr)) {
   std::memcpy(mapped.pData,&values,sizeof(values));context_->Unmap(liquid_ripple_constants_.Get(),0);
   liquid_ripple_ready_=true;
  }
  if(FAILED(hr)&&!liquid_ripple_failure_reported_) {
   liquid_ripple_failure_reported_=true;
   OutputDebugStringA("EID3D11 liquid ripples: frame values or buffer unavailable; ripples omitted.\n");
  }
 }
 ID3D11Buffer* buffer=liquid_ripple_ready_?liquid_ripple_constants_.Get():nullptr;
 context_->PSSetConstantBuffers(8,1,&buffer);
 context_->PSSetShaderResources(24,1,liquid_wave_srv_[liquid_wave_current_].GetAddressOf());
}
void Renderer::reset_water_reflections() noexcept {
 clear_water_reflection_bindings();
 water_scene_srv_.Reset();water_history_srv_.Reset();water_scene_color_.Reset();water_scene_depth_srv_.Reset();water_scene_depth_.Reset();
 water_history_frame_=UINT64_MAX;
 for(UINT slot=0;slot<2;++slot){particle_depth_srv_[slot].Reset();particle_depth_rtv_[slot].Reset();particle_depth_[slot].Reset();}
 particle_depth_width_=particle_depth_height_=0;particle_depth_frame_=particle_depth_history_frame_=UINT64_MAX;
 game_particle_depth_ps_.Reset();particle_depth_blend_.Reset();particle_depth_test_.Reset();particle_depth_failed_=false;
 water_reflection_constants_.Reset();water_reflection_sampler_.Reset();
 for(auto& texture:water_ssr_textures_)texture.Reset();for(auto& view:water_ssr_rtv_)view.Reset();for(auto& view:water_ssr_srv_)view.Reset();
 water_ssr_depth_.Reset();water_ssr_dsv_.Reset();water_ssr_depth_state_.Reset();water_ssr_width_=water_ssr_height_=0;water_ssr_failure_reported_=false;
 liquid_ripple_constants_.Reset();liquid_ripple_frame_=UINT64_MAX;
 liquid_ripple_batch_constants_.Reset();liquid_ripple_batch_serial_=0;
 liquid_ripple_ready_=false;liquid_ripple_failure_reported_=false;
 water_capture_frame_=UINT64_MAX;water_capture_ready_=false;water_failure_reported_=false;
}
HRESULT Renderer::create_water_reflection_targets() {
 if(water_scene_srv_)return S_OK;
 if(!rtv_||!depth_||!dsv_||!depth_srv_)return E_FAIL;
 ComPtr<ID3D11Resource> resource;rtv_->GetResource(&resource);
 ComPtr<ID3D11Texture2D> source;HRESULT hr=resource.As(&source);if(FAILED(hr))return hr;
 D3D11_TEXTURE2D_DESC description{};source->GetDesc(&description);
 if(description.SampleDesc.Count!=1)return E_NOTIMPL;
 // Two slices: 0 is this frame's world colour at the first liquid batch
 // (refraction reads it), 1 the previous frame's finished world colour
 // (reflections read it: it holds every particle, fire, spell and other
 // transparent pass the game and the renderer draw after the water, #140).
 // Slice 1 has a short mip chain: reflections read a level matching their
 // cone footprint. Formats without mip generation keep one level.
 UINT support=0;
 const bool mips=SUCCEEDED(device_->CheckFormatSupport(description.Format,&support))&&
  (support&D3D11_FORMAT_SUPPORT_MIP_AUTOGEN)&&(support&D3D11_FORMAT_SUPPORT_RENDER_TARGET);
 UINT levels=1;
 if(mips)for(UINT size=(std::max)(description.Width,description.Height);size>1&&levels<kWaterSceneMipLevels;size>>=1)++levels;
 description.MipLevels=levels;description.ArraySize=2;
 description.BindFlags=D3D11_BIND_SHADER_RESOURCE|(levels>1?D3D11_BIND_RENDER_TARGET:0u);description.CPUAccessFlags=0;
 description.MiscFlags=levels>1?D3D11_RESOURCE_MISC_GENERATE_MIPS:0u;description.Usage=D3D11_USAGE_DEFAULT;
 ComPtr<ID3D11Texture2D> color;ComPtr<ID3D11ShaderResourceView> srv;
 ComPtr<ID3D11Texture2D> depth_copy;ComPtr<ID3D11ShaderResourceView> depth_copy_srv;ComPtr<ID3D11Buffer> constants;
 ComPtr<ID3D11SamplerState> sampler;
 if(FAILED(hr=device_->CreateTexture2D(&description,nullptr,&color)))return hr;
 if(FAILED(hr=device_->CreateShaderResourceView(color.Get(),nullptr,&srv)))return hr;
 // The history slice alone, for its mip generation.
 ComPtr<ID3D11ShaderResourceView> history_srv;
 D3D11_SHADER_RESOURCE_VIEW_DESC history_view{};history_view.Format=description.Format;
 history_view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;history_view.Texture2DArray.MipLevels=levels;
 history_view.Texture2DArray.FirstArraySlice=1;history_view.Texture2DArray.ArraySize=1;
 if(FAILED(hr=device_->CreateShaderResourceView(color.Get(),&history_view,&history_srv)))return hr;
 // A private copy of scene depth: liquids keep the game's depth writes on the
 // real depth buffer while the reflection march reads this snapshot.
 D3D11_TEXTURE2D_DESC depth_description{};depth_->GetDesc(&depth_description);
 depth_description.BindFlags=D3D11_BIND_SHADER_RESOURCE;depth_description.CPUAccessFlags=0;
 depth_description.MiscFlags=0;depth_description.Usage=D3D11_USAGE_DEFAULT;
 if(FAILED(hr=device_->CreateTexture2D(&depth_description,nullptr,&depth_copy)))return hr;
 D3D11_SHADER_RESOURCE_VIEW_DESC depth_view{};depth_srv_->GetDesc(&depth_view);
 if(FAILED(hr=device_->CreateShaderResourceView(depth_copy.Get(),&depth_view,&depth_copy_srv)))return hr;
 D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(WaterReflectionConstants);
 buffer.Usage=D3D11_USAGE_DYNAMIC;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
 buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
 if(FAILED(hr=device_->CreateBuffer(&buffer,nullptr,&constants)))return hr;
 D3D11_SAMPLER_DESC sampling{};sampling.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
 sampling.MaxLOD=D3D11_FLOAT32_MAX;
 if(FAILED(hr=device_->CreateSamplerState(&sampling,&sampler)))return hr;
 water_scene_color_=std::move(color);water_scene_srv_=std::move(srv);water_history_srv_=std::move(history_srv);
 water_scene_levels_=levels;water_history_frame_=UINT64_MAX;
 water_scene_depth_=std::move(depth_copy);water_scene_depth_srv_=std::move(depth_copy_srv);water_reflection_constants_=std::move(constants);
 water_reflection_sampler_=std::move(sampler);return S_OK;
}
// The half-resolution water reflection targets (flush_native trace_water),
// half the world target's size rounded up.
HRESULT Renderer::create_water_ssr_targets() {
 const UINT width=(width_+1)/2,height=(height_+1)/2;
 if(water_ssr_dsv_&&width==water_ssr_width_&&height==water_ssr_height_)return S_OK;
 if(!width||!height)return E_FAIL;
 for(auto& texture:water_ssr_textures_)texture.Reset();for(auto& view:water_ssr_rtv_)view.Reset();for(auto& view:water_ssr_srv_)view.Reset();
 water_ssr_depth_.Reset();water_ssr_dsv_.Reset();water_ssr_width_=water_ssr_height_=0;
 // Rays (direction, facing), water points (world xyz needs 32-bit floats;
 // w the spread per footprint), colour and key.
 static constexpr DXGI_FORMAT formats[6]={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,
  DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT};
 HRESULT hr=S_OK;
 for(unsigned n=0;n<6;++n) {
  D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
  desc.Format=formats[n];desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
  if(FAILED(hr=device_->CreateTexture2D(&desc,nullptr,&water_ssr_textures_[n]))||
     FAILED(hr=device_->CreateRenderTargetView(water_ssr_textures_[n].Get(),nullptr,&water_ssr_rtv_[n]))||
     FAILED(hr=device_->CreateShaderResourceView(water_ssr_textures_[n].Get(),nullptr,&water_ssr_srv_[n])))return hr;
 }
 D3D11_TEXTURE2D_DESC depth{};depth.Width=width;depth.Height=height;depth.MipLevels=depth.ArraySize=depth.SampleDesc.Count=1;
 depth.Format=DXGI_FORMAT_D32_FLOAT;depth.Usage=D3D11_USAGE_DEFAULT;depth.BindFlags=D3D11_BIND_DEPTH_STENCIL;
 if(FAILED(hr=device_->CreateTexture2D(&depth,nullptr,&water_ssr_depth_))||
    FAILED(hr=device_->CreateDepthStencilView(water_ssr_depth_.Get(),nullptr,&water_ssr_dsv_)))return hr;
 if(!water_ssr_depth_state_) {
  D3D11_DEPTH_STENCIL_DESC state{};state.DepthEnable=TRUE;state.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;state.DepthFunc=D3D11_COMPARISON_LESS;
  if(FAILED(hr=device_->CreateDepthStencilState(&state,&water_ssr_depth_state_)))return hr;
 }
 water_ssr_width_=width;water_ssr_height_=height;
 char text[64];std::snprintf(text,sizeof(text),"half-resolution reflection %ux%u",width,height);
 bridge_report_event("water_ssr",text);
 return S_OK;
}
bool Renderer::prepare_water_reflections(const FixedState& state) {
 const auto unavailable=[&](const char* reason) {
  if(!water_failure_reported_){water_failure_reported_=true;OutputDebugStringA(reason);}
  return false;
 };
 const bool enabled=configured_boolean(debug_overlay_.config(),"water_reflections",true);
 if(water_capture_frame_!=native_frame_index_) {
  water_capture_frame_=native_frame_index_;water_capture_ready_=false;
  if(!enabled)return false;
  if(!native_world_clip_valid_||FAILED(create_water_reflection_targets()))
   {++water_reflection_resource_failures_;return unavailable("EID3D11 water reflections: scene resources unavailable; using analytic sky\n");}
  WaterReflectionConstants constants{};constants.clip=native_world_clip_;
  // Invert the renderer-owned clip matrix; no camera or engine layout inference.
  double matrix[4][8]{};
  for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c) {
   matrix[r][c]=constants.clip[r*4+c];matrix[r][c+4]=r==c?1:0;
  }
  for(unsigned c=0;c<4;++c) {
   unsigned pivot=c;
   for(unsigned r=c+1;r<4;++r)if(std::abs(matrix[r][c])>std::abs(matrix[pivot][c]))pivot=r;
   if(std::abs(matrix[pivot][c])<1e-12)
    return unavailable("EID3D11 water reflections: singular clip; using analytic sky\n");
   for(unsigned k=0;k<8;++k)std::swap(matrix[pivot][k],matrix[c][k]);
   const double divisor=matrix[c][c];
   for(unsigned k=0;k<8;++k)matrix[c][k]/=divisor;
   for(unsigned r=0;r<4;++r)if(r!=c) {
    const double factor=matrix[r][c];
    for(unsigned k=0;k<8;++k)matrix[r][k]-=factor*matrix[c][k];
   }
  }
  for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)constants.inverse[r*4+c]=float(matrix[r][c+4]);
  water_capture_inverse_=constants.inverse;
  constants.target={float(width_),float(height_),1,
   configured_boolean(debug_overlay_.config(),"water_physical",true)?1.f:0.f};
  // Reflection march steps by quality: Low 12, Medium 20 (the former fixed
  // count), High 40 (half the step length closes the #121 thin-edge gaps).
  constexpr float kWaterReflectionSteps[3]={12.f,20.f,40.f};
  const int quality=std::clamp(configured_integer(debug_overlay_.config(),"water_reflection_quality",2),0,2);
  // The previous frame's world colour, if it was captured on the frame right
  // before this one, with the camera it was drawn with.
  const bool history=water_history_frame_!=UINT64_MAX&&water_history_frame_+1u==native_frame_index_;
  // That frame's particle depth (written only at Medium and High).
  const bool particles=history&&particle_depth_history_frame_==water_history_frame_&&quality>0;
  constants.quality={kWaterReflectionSteps[quality],float(water_scene_levels_-1),history?1.f:0.f,particles?1.f:0.f};
  constants.history_clip=history?water_history_clip_:native_world_clip_;
  constants.history_inverse=history?water_history_inverse_:constants.inverse;
  if(quality!=water_reflection_quality_logged_) {
   water_reflection_quality_logged_=quality;
   char line[96];
   std::snprintf(line,sizeof(line),"EID3D11 water reflections: quality %d, %.0f march steps",quality,constants.quality[0]);
   bridge_append_probe_event("water_reflections",line);
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(FAILED(context_->Map(water_reflection_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))
   return unavailable("EID3D11 water reflections: constant upload failed; using analytic sky\n");
  std::memcpy(mapped.pData,&constants,sizeof(constants));context_->Unmap(water_reflection_constants_.Get(),0);
  clear_water_reflection_bindings();
  ComPtr<ID3D11Resource> source;rtv_->GetResource(&source);
  // Preserve the exact ordered submission boundary and current history/AO MRT.
  ID3D11RenderTargetView* targets[3]{};ID3D11DepthStencilView* depth=nullptr;
  context_->OMGetRenderTargets(3,targets,&depth);
  context_->OMSetRenderTargets(0,nullptr,nullptr);
  transition_profile_pass("liquid_scene_copy");
  context_->CopySubresourceRegion(water_scene_color_.Get(),D3D11CalcSubresource(0,0,water_scene_levels_),0,0,0,source.Get(),0,nullptr);
  context_->CopyResource(water_scene_depth_.Get(),depth_.Get());
  end_profile_pass();
  context_->OMSetRenderTargets(targets[2]?3u:targets[1]?2u:1u,targets,depth);
  for(auto* target:targets)if(target)target->Release();if(depth)depth->Release();
  water_capture_clip_=native_world_clip_;water_capture_ready_=true;
 }
 if(!enabled||!water_capture_ready_)return false;
 (void)state; // Depth writes stay as the game sets them; the march reads a depth snapshot.
 // The copy and its constants belong to the capture camera. Later world draws
 // in the same frame may carry a re-derived clip with tiny float drift; only a
 // genuinely different camera rejects the reflection.
 float scale=0,delta=0;
 for(unsigned k=0;k<16;++k){
  scale=std::max(scale,std::abs(water_capture_clip_[k]));
  delta=std::max(delta,std::abs(native_world_clip_[k]-water_capture_clip_[k]));
 }
 return delta<=scale*.01f;
}
// The finished world colour of a frame whose liquids reflected, kept for the
// next frame's reflections (Blightbound: reflections read the previous
// frame, so transparent passes drawn after the water are in them). Taken
// after every world pass and before the composite, SMAA and post effects.
void Renderer::capture_water_history() {
 if(!water_scene_srv_||!water_capture_ready_||water_capture_frame_!=native_frame_index_||!rtv_)return;
 clear_water_reflection_bindings();
 ComPtr<ID3D11Resource> source;rtv_->GetResource(&source);
 ID3D11RenderTargetView* targets[3]{};ID3D11DepthStencilView* depth=nullptr;
 context_->OMGetRenderTargets(3,targets,&depth);
 context_->OMSetRenderTargets(0,nullptr,nullptr);
 transition_profile_pass("liquid_history_copy");
 context_->CopySubresourceRegion(water_scene_color_.Get(),D3D11CalcSubresource(0,1,water_scene_levels_),0,0,0,source.Get(),0,nullptr);
 if(water_scene_levels_>1)context_->GenerateMips(water_history_srv_.Get());
 end_profile_pass();
 context_->OMSetRenderTargets(targets[2]?3u:targets[1]?2u:1u,targets,depth);
 for(auto* target:targets)if(target)target->Release();if(depth)depth->Release();
 water_history_clip_=water_capture_clip_;water_history_inverse_=water_capture_inverse_;water_history_frame_=native_frame_index_;
 // The particle depth this frame wrote goes with its colour; the next
 // frame writes the other slot.
 if(particle_depth_frame_==native_frame_index_) {
  particle_depth_history_frame_=native_frame_index_;particle_depth_current_^=1u;particle_depth_frame_=UINT64_MAX;
 }
}
// Particle depth is drawn only while reflections can read it: reflections on
// at Medium or High (Low skips it), and water reflected this frame or the
// one before (neighbour-map water is captured after the game particles).
bool Renderer::particle_depth_wanted() const {
 if(particle_depth_failed_||feature_level_<D3D_FEATURE_LEVEL_11_0)return false;
 if(!configured_boolean(debug_overlay_.config(),"water_reflections",true))return false;
 if(configured_integer(debug_overlay_.config(),"water_reflection_quality",2)<1)return false;
 return (water_capture_ready_&&water_capture_frame_==native_frame_index_)||
        (water_history_frame_!=UINT64_MAX&&water_history_frame_+1u==native_frame_index_);
}
// Second draw of the game particle call just made (same vertex shader,
// structured buffer, constants, texture, viewport and rasterizer): the depth
// pixel shader min-blends the screen depth of visible particle coverage into
// the current particle depth slot, depth-tested against the scene without
// writing it. Sets and restores its own targets, blend, depth state and
// pixel shader. A failure turns particle reflections off (logged once).
void Renderer::draw_game_particle_depth(UINT vertex_count) {
 const auto fail=[&](const char* reason) {
  particle_depth_failed_=true;OutputDebugStringA(reason);bridge_report_event("particle_reflections","unavailable");
 };
 if(!game_particle_depth_ps_) {
  UINT support=0;
  if(FAILED(device_->CheckFormatSupport(DXGI_FORMAT_R32_FLOAT,&support))||!(support&D3D11_FORMAT_SUPPORT_BLENDABLE))
   return fail("EID3D11 particle reflections: R32F blending unsupported; particles omitted from reflections\n");
  std::vector<ShaderProgram> programs;append_world_support_shader_programs(programs);
  ComPtr<ID3DBlob> pixel,error;
  if(FAILED(compile_shader(programs[7],&pixel,&error))) {
   if(error)OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
   return fail("EID3D11 particle reflections: depth shader compile failed; particles omitted from reflections\n");
  }
  D3D11_BLEND_DESC blend{};auto& target=blend.RenderTarget[0];
  target.BlendEnable=TRUE;target.SrcBlend=target.DestBlend=target.SrcBlendAlpha=target.DestBlendAlpha=D3D11_BLEND_ONE;
  target.BlendOp=target.BlendOpAlpha=D3D11_BLEND_OP_MIN;target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_RED;
  D3D11_DEPTH_STENCIL_DESC test{};test.DepthEnable=TRUE;test.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
  test.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
  if(FAILED(device_->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&game_particle_depth_ps_))||
     FAILED(device_->CreateBlendState(&blend,&particle_depth_blend_))||
     FAILED(device_->CreateDepthStencilState(&test,&particle_depth_test_))) {
   game_particle_depth_ps_.Reset();particle_depth_blend_.Reset();particle_depth_test_.Reset();
   return fail("EID3D11 particle reflections: pipeline creation failed; particles omitted from reflections\n");
  }
 }
 if(!particle_depth_[0]||particle_depth_width_!=width_||particle_depth_height_!=height_) {
  for(UINT slot=0;slot<2;++slot){particle_depth_srv_[slot].Reset();particle_depth_rtv_[slot].Reset();particle_depth_[slot].Reset();}
  particle_depth_frame_=particle_depth_history_frame_=UINT64_MAX;
  D3D11_TEXTURE2D_DESC description{};description.Width=width_;description.Height=height_;
  description.MipLevels=description.ArraySize=description.SampleDesc.Count=1;description.Format=DXGI_FORMAT_R32_FLOAT;
  description.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
  for(UINT slot=0;slot<2;++slot)
   if(FAILED(device_->CreateTexture2D(&description,nullptr,&particle_depth_[slot]))||
      FAILED(device_->CreateRenderTargetView(particle_depth_[slot].Get(),nullptr,&particle_depth_rtv_[slot]))||
      FAILED(device_->CreateShaderResourceView(particle_depth_[slot].Get(),nullptr,&particle_depth_srv_[slot]))) {
    for(UINT k=0;k<2;++k){particle_depth_srv_[k].Reset();particle_depth_rtv_[k].Reset();particle_depth_[k].Reset();}
    return fail("EID3D11 particle reflections: depth targets unavailable; particles omitted from reflections\n");
   }
  particle_depth_width_=width_;particle_depth_height_=height_;
 }
 transition_profile_pass("particle_reflection_depth");
 ID3D11RenderTargetView* targets[3]{};ID3D11DepthStencilView* depth=nullptr;
 context_->OMGetRenderTargets(3,targets,&depth);
 ID3D11BlendState* blend=nullptr;float factor[4]{};UINT mask=0;context_->OMGetBlendState(&blend,factor,&mask);
 ID3D11DepthStencilState* test=nullptr;UINT reference=0;context_->OMGetDepthStencilState(&test,&reference);
 ID3D11PixelShader* pixel=nullptr;context_->PSGetShader(&pixel,nullptr,nullptr);
 ID3D11ShaderResourceView* empty=nullptr;context_->PSSetShaderResources(28,1,&empty);
 ID3D11RenderTargetView* const output=particle_depth_rtv_[particle_depth_current_].Get();
 if(particle_depth_frame_!=native_frame_index_) {
  const float far_depth[4]={1,1,1,1};context_->ClearRenderTargetView(output,far_depth);
  particle_depth_frame_=native_frame_index_;
 }
 context_->OMSetRenderTargets(1,&output,dsv_.Get());
 context_->OMSetBlendState(particle_depth_blend_.Get(),nullptr,0xffffffffu);
 context_->OMSetDepthStencilState(particle_depth_test_.Get(),0);
 context_->PSSetShader(game_particle_depth_ps_.Get(),nullptr,0);
 context_->Draw(vertex_count,0);
 profiler_.add_draw(vertex_count/3u,1);profiler_.add_batch();
 context_->PSSetShader(pixel,nullptr,0);
 context_->OMSetDepthStencilState(test,reference);
 context_->OMSetBlendState(blend,factor,mask);
 context_->OMSetRenderTargets(targets[2]?3u:targets[1]?2u:1u,targets,depth);
 for(auto* target:targets)if(target)target->Release();
 if(depth)depth->Release();if(blend)blend->Release();if(test)test->Release();if(pixel)pixel->Release();
}

} // namespace owned
