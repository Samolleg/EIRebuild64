// Liquid rendering: generated normal/detail maps, screen-space reflections,
// the interactive ripple simulation and ripple constants. Owns every water
// resource of the Renderer; the liquid surface shading itself lives in
// liquid_surface_shader.h; the generated maps in liquid_maps.h.
#include "liquid_surface_shader.h"
#include "liquid_maps.h"
#include "liquid_waves.h"
#include "liquid_surf.h"
#include "renderer.h"
#include "engine_bridge.h"
#include "neighbour_maps.h"
#include "neighbour_piece.h"
#include "owned_asset_loader.h"
#include "worker_priority.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <chrono>
#include <climits>
#include <future>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace owned {
#if !defined(EID3D11_SHADER_EMBED)
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
// The same texture on the D3D12 executor from its CPU levels.
HRESULT create_generated_liquid_texture_d3d12(D3D12Frame& frame,UINT size,const std::vector<std::vector<std::uint32_t>>& mips,D3D12TextureRef& out) {
 D3D12TextureRef texture;
 HRESULT hr=frame.create_texture(size,size,DXGI_FORMAT_R8G8B8A8_UNORM,texture,static_cast<UINT>(mips.size()));if(FAILED(hr))return hr;
 UINT side=size;
 for(UINT mip=0;mip<mips.size();++mip){
  if(FAILED(hr=frame.write_texture(*texture,RECT{0,0,LONG(side),LONG(side)},mips[mip].data(),side*4u,mip)))return hr;
  side=std::max(1u,side/2);
 }
 out=std::move(texture);return S_OK;
}
}

// Seamless liquid textures (liquid_maps.h), generated once per device on a
// worker started at device creation (the generation costs about half a second
// of CPU; on the render thread it stalled the first world frame with water).
// The device is free-threaded, so the worker creates the immutable textures
// too; no CPU copy outlives the job.
void Renderer::start_liquid_maps_job() noexcept {
 if(d3d12_){ // D3D12: CPU levels only; ensure_liquid_maps_d3d12 uploads them
  if(liquid_maps_job_.valid()||(liquid12_.normal&&liquid12_.detail))return;
  try{
   liquid_maps_job_=std::async(std::launch::async,[]()->LiquidMapsPrepared{
    const BackgroundWorkerPriority priority;
    LiquidMapsPrepared out;
    try{
     auto levels=generate_liquid_map_levels();
     out.lava_samples=std::move(levels.lava_samples);
     out.normal_mips=liquid_map_mips(LiquidMapLevels::normal_size,std::move(levels.normals),true);
     out.detail_mips=liquid_map_mips(LiquidMapLevels::detail_size,std::move(levels.detail),false);
     out.hr=S_OK;
    }catch(const std::bad_alloc&){out.hr=E_OUTOFMEMORY;}
    return out;
   });
  }catch(...){}
  return;
 }
 if(liquid_maps_job_.valid()||(liquid_normal_srv_&&liquid_detail_srv_)||!device_)return;
 try{
  liquid_maps_job_=std::async(std::launch::async,[device=device_]()->LiquidMapsPrepared{
   const BackgroundWorkerPriority priority;
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
 if(d3d12_)return ensure_liquid_maps_d3d12();
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

HRESULT Renderer::ensure_liquid_maps_d3d12() try {
 if(liquid12_.normal&&liquid12_.detail)return S_OK;
 LiquidMapsPrepared prepared;
 if(liquid_maps_job_.valid())prepared=liquid_maps_job_.get();
 else{
  auto levels=generate_liquid_map_levels();
  prepared.lava_samples=std::move(levels.lava_samples);
  prepared.normal_mips=liquid_map_mips(LiquidMapLevels::normal_size,std::move(levels.normals),true);
  prepared.detail_mips=liquid_map_mips(LiquidMapLevels::detail_size,std::move(levels.detail),false);
  prepared.hr=S_OK;
 }
 if(FAILED(prepared.hr))return prepared.hr;
 D3D12TextureRef normal,detail;
 HRESULT hr=create_generated_liquid_texture_d3d12(*d3d12_,LiquidMapLevels::normal_size,prepared.normal_mips,normal);
 if(SUCCEEDED(hr))hr=create_generated_liquid_texture_d3d12(*d3d12_,LiquidMapLevels::detail_size,prepared.detail_mips,detail);
 if(FAILED(hr))return hr;
 liquid_lava_samples_=std::move(prepared.lava_samples);
 liquid12_.normal=std::move(normal);liquid12_.detail=std::move(detail);
 return S_OK;
}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}

// The D3D12 world draws' liquid inputs, as the D3D11 flush and
// prepare_liquid_ripples bind them (the vertex and domain stages read b8 and
// t62 from the same table).
void Renderer::bind_liquid_d3d12() {
 if(liquid_ripple_ready_)d3d12_->set_constants(8,&liquid_ripple_values_,sizeof(liquid_ripple_values_));
 d3d12_->set_resource(22,liquid12_.normal.get());d3d12_->set_resource(23,liquid12_.detail.get());
 D3D11_SAMPLER_DESC sampling{};sampling.Filter=D3D11_FILTER_ANISOTROPIC;sampling.MaxAnisotropy=8;
 sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sampling.MaxLOD=D3D11_FLOAT32_MAX;
 d3d12_->set_sampler(6,sampling);
 d3d12_->set_resource(24,liquid12_.wave[liquid_wave_current_].get());
 d3d12_->set_resource(33,liquid12_.swamp_frame.get());
 d3d12_->set_resource(62,liquid12_.surf.get());d3d12_->set_resource(63,liquid12_.surf_level.get());
}

void Renderer::bind_liquid_swamp_frame() noexcept {
 if(d3d12_){ // D3D12: the texture only; bind_liquid_d3d12 binds it
  if(liquid_swamp_frame_generation_==ambient_map_generation_)return;
  liquid12_.swamp_frame.reset();
  if(const auto terrain=ambient_terrain_.lock()){
   liquid_swamp_frame_generation_=ambient_map_generation_;
   const UINT side=terrain->swamp_frame_side;
   if(side&&terrain->swamp_frame_texels.size()==std::size_t(side)*side)
    try{if(FAILED(create_generated_liquid_texture_d3d12(*d3d12_,side,liquid_map_mips(side,terrain->swamp_frame_texels,false),liquid12_.swamp_frame)))liquid12_.swamp_frame.reset();}
    catch(const std::bad_alloc&){liquid12_.swamp_frame.reset();}
  }
  return;
 }
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

#endif
void append_liquid_wave_shader_programs(std::vector<ShaderProgram>& out){
 out.push_back({{kLiquidWaveShader,sizeof(kLiquidWaveShader)-1},nullptr,nullptr,"liquidWaveCS","cs_5_0",0});
}

#if !defined(EID3D11_SHADER_EMBED)
void Renderer::step_liquid_waves(NativeLiquidRipples& values,double seconds) {
 values.wave={};
 if(liquid_wave_failed_)return;
 const auto fail=[&](const char* reason){liquid_wave_failed_=true;OutputDebugStringA(reason);};
 if(d3d12_){
  if(!liquid12_.wave[1]){
   if(liquid12_.wave_program.empty())append_liquid_wave_shader_programs(liquid12_.wave_program);
   // A first dispatch can return S_FALSE while its pipeline compiles. The
   // surface can already sample either ping-pong texture before that step;
   // start both flat, as on D3D11, without depending on a successful dispatch.
   for(unsigned k=0;k<2;++k)
    if(FAILED(d3d12_->create_storage_texture(kLiquidWaveSize,kLiquidWaveSize,0,DXGI_FORMAT_R32G32B32A32_FLOAT,liquid12_.wave[k])))
     return fail("EID3D11 liquid ripples: wave field unavailable; character ripples omitted.\n");
   const float zero[4]{};for(auto& texture:liquid12_.wave)d3d12_->clear_texture(*texture,zero);
  }
 }
 else if(!liquid_wave_cs_) {
  ComPtr<ID3DBlob> code,errors;std::vector<ShaderProgram> programs;append_liquid_wave_shader_programs(programs);
  if(FAILED(compile_shader(programs[0],&code,&errors))||
     FAILED(create_shader(device_.Get(),code->GetBufferPointer(),code->GetBufferSize(),nullptr,&liquid_wave_cs_)))
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
 // A new map starts with calm water: the next step reads nothing (first-use
 // shift) instead of carrying the old map's field over.
 if(liquid_wave_map_generation_!=ambient_map_generation_){
  liquid_wave_map_generation_=ambient_map_generation_;
  liquid_wave_origin_valid_=false;liquid_wave_accumulator_=0;
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
 ID3D11ShaderResourceView* none=nullptr;
 if(!d3d12_){
 context_->PSSetShaderResources(24,1,&none);
 context_->CSSetShader(liquid_wave_cs_.Get(),nullptr,0);
 context_->CSSetConstantBuffers(0,1,liquid_wave_constants_.GetAddressOf());
 }
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
  if(d3d12_){
   const unsigned next=liquid_wave_current_^1u;
   d3d12_->clear_slots();
   d3d12_->set_constants(0,&constants,sizeof(constants));
   d3d12_->set_resource(0,liquid12_.wave[liquid_wave_current_].get());
   d3d12_->set_unordered(0,liquid12_.wave[next].get());
   const HRESULT hr=d3d12_->dispatch(liquid12_.wave_program[0],kLiquidWaveSize/8,kLiquidWaveSize/8,1);
   d3d12_->clear_slots();
   if(hr!=S_OK){if(hr==S_FALSE)d3d12_->count("liquid wave steps waiting for their program");break;}
   liquid_wave_current_=next;
   continue;
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
 if(!d3d12_)context_->CSSetShader(nullptr,nullptr,0);
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
 if(d3d12_){ // set_constants copies the block: no cached buffer
  NativeLiquidRipples culled=values;culled.meta[0]=static_cast<float>(kept);
  for(unsigned n=0;n<kept;++n){
   const unsigned u=list[1+n];
   culled.position_speed[n]=values.position_speed[u];culled.phase[n]=values.phase[u];
   culled.flow[n]=values.flow[u];culled.id[n]=values.id[u];
  }
  d3d12_->set_constants(8,&culled,sizeof(culled));
  return;
 }
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
  // Rain rings follow the game's fade (and the render-only precipitation
  // test) and thin with the shower's visual strength.
  // Rain rings follow the precipitation's own build-up and fade (ramped
  // rain x strength), outdoors only.
  const float rain=environment.valid&&!environment.cave?precipitation_rain_visible_:0.f;
  values.meta[3]=configured_boolean(debug_overlay_.config(),"water_physical",true)?rain:0.f;
  update_liquid_surf();
  values.surf_window=liquid_surf_window_;
  values.surf_mode={liquid_surf_srv_||liquid12_.surf?1.f:0.f,
                    float(std::clamp(configured_integer(debug_overlay_.config(),"water_surf_debug",0),0,3)+
                          4*std::clamp(configured_integer(debug_overlay_.config(),"water_surf_quality",2),0,2)), // debug + 4 * quality
                    // half a texel in uv (.5/side): with 2 m texels the window's 1/extent
                    liquid_surf_window_[2],liquid_surf_window_[3]};
  {
   const auto& config=debug_overlay_.config();
   const auto value=[&](const char* key,float fallback,float low,float high){return std::clamp(configured_floating_point(config,key,fallback),low,high);};
   values.surf_wave={value("water_surf_wave_height",.6f,0.f,2.f),value("water_surf_wavelength",9.f,2.f,30.f),
                     value("water_surf_speed",1.2f,.1f,4.f),value("water_surf_choppiness",2.5f,1.f,6.f)};
   values.surf_shape={float(std::clamp(configured_integer(config,"water_surf_octaves",4),1,6)),value("water_surf_segment",18.f,4.f,60.f),
                      value("water_surf_randomness",.75f,0.f,1.f),value("water_surf_chop_height",.25f,0.f,1.f)};
   values.surf_foam={value("water_surf_shore_foam",1.f,0.f,2.f),value("water_surf_shore_width",.6f,.05f,1.5f),
                     value("water_surf_crest_foam",.95f,.6f,1.f),value("water_surf_foam_cell",1.6f,.3f,4.f)};
   values.surf_look={value("water_surf_shallow_tint",.7f,0.f,1.f),value("water_surf_absorption",1.f,0.f,3.f),
                     value("water_surf_cliff_burst",1.f,0.f,3.f),liquid_surf_fade_};
   values.surf_swell=liquid_surf_swell_;
   // Wind: the renderer wind (weather_wind.h: the map's force times calm,
   // windy and storm spells, smooth in game time) raises and lowers the
   // swell and chop gradually and brings whitecaps on (swell w, 0..1). A
   // typical map wind (~.3) keeps the configured height; calm ~.7x, storms
   // up to 1.8x.
   const float drive=weather_wind_.gpu.force*weather_wind_.gpu.speed;
   const float scale=std::clamp(.5f+1.6f*drive,.5f,1.8f);
   values.surf_wave[0]*=scale;values.surf_shape[3]*=scale;
   values.surf_foam[2]=std::clamp(1.f-(1.f-values.surf_foam[2])*scale,.6f,1.f);
   values.surf_swell[3]=std::clamp((drive-.2f)/.8f,0.f,1.f);
  }
  if(available){step_liquid_waves(values,seconds);ease_liquid_units(values,seconds);}
  liquid_ripple_values_=values;++liquid_ripple_values_serial_;
  for(unsigned u=0;u<16u;++u)liquid_unit_reach_[u]=liquid_unit_reach(values,u);
  if(d3d12_){liquid_ripple_ready_=true;return;} // bind_liquid_d3d12 sets b8
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
 if(d3d12_)return;
 ID3D11Buffer* buffer=liquid_ripple_ready_?liquid_ripple_constants_.Get():nullptr;
 context_->PSSetConstantBuffers(8,1,&buffer);
 context_->PSSetShaderResources(24,1,liquid_wave_srv_[liquid_wave_current_].GetAddressOf());
 context_->PSSetShaderResources(62,1,liquid_surf_srv_.GetAddressOf());
 context_->PSSetShaderResources(63,1,liquid_surf_level_srv_.GetAddressOf()); // its sea level and shore impact (cliff bursts, beach run-up)
 // The terrain tess stages read the surf field and its window too (sea surf
 // wave height on tessellated water; nativeTessVS, nativeTerrainDomain).
 context_->VSSetConstantBuffers(8,1,&buffer);context_->DSSetConstantBuffers(8,1,&buffer);
 context_->VSSetShaderResources(62,1,liquid_surf_srv_.GetAddressOf());context_->DSSetShaderResources(62,1,liquid_surf_srv_.GetAddressOf());
 context_->DSSetShaderResources(63,1,liquid_surf_level_srv_.GetAddressOf()); // the wave height fades at steep shores
}

// Sea surf (liquid_surf.h): adopt the active map's field when its worker is
// done (starting it once the map's area grid exists), and composite the
// active and neighbour fields that have surf into one world-space texture
// whenever that set changes. Pieces are placed at whole tiles; the active
// map is written last, so it wins where pieces overlap.
void Renderer::update_liquid_surf() noexcept try {
 neighbour_maps_set_no_sea_maps(liquid_surf_no_sea_maps());
 if(liquid_surf_job_.valid()&&liquid_surf_job_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
  auto field=liquid_surf_job_.get();
  if(field&&liquid_surf_generation_==ambient_map_generation_){
   liquid_surf_active_=std::move(field);++liquid_surf_serial_;
   const auto& s=liquid_surf_active_->impact_spots;
   char line[400];std::snprintf(line,sizeof(line),"active map: %u tiles with surf, max effective fetch %.0f m (%ux%u tiles); "
    "steep-shore spots x,y,score,incoming_deg: %.0f,%.0f,%.2f,%.0f | %.0f,%.0f,%.2f,%.0f | %.0f,%.0f,%.2f,%.0f",
    liquid_surf_active_->surf_cells,liquid_surf_active_->max_exposure,liquid_surf_active_->tiles_x,liquid_surf_active_->tiles_y,
    s[0][0],s[0][1],s[0][2],s[0][3],s[1][0],s[1][1],s[1][2],s[1][3],s[2][0],s[2][1],s[2][2],s[2][3]);
   bridge_report_event("water_surf",line);
  }
 }
 if(!liquid_surf_job_.valid()&&liquid_surf_generation_!=ambient_map_generation_&&!ambient_terrain_.expired()&&
    ambient_tiles_x_&&ambient_tiles_y_&&ambient_area_.size()==std::size_t(ambient_tiles_x_)*ambient_tiles_y_){
  liquid_surf_generation_=ambient_map_generation_;
  if(liquid_surf_active_){liquid_surf_active_.reset();++liquid_surf_serial_;}
  liquid_surf_job_=std::async(std::launch::async,[area=ambient_area_,liquid=ambient_liquid_level_.liquid_area,height=ambient_height_,ground=ambient_liquid_level_.ground,
                                                     x=ambient_tiles_x_,y=ambient_tiles_y_,sea=liquid_surf_map_takes_sea(ambient_map_name_,liquid_surf_no_sea_maps())]()->std::shared_ptr<const LiquidSurfField>{
   const BackgroundWorkerPriority priority;
   try{return std::make_shared<LiquidSurfField>(build_liquid_surf_field(area,liquid,height,ground,x,y,sea));}catch(...){return nullptr;}
  });
 }
 const float fade=std::clamp(configured_floating_point(debug_overlay_.config(),"water_surf_edge_fade",40.f),0.f,120.f);
 if(fade!=liquid_surf_fade_){liquid_surf_fade_=fade;++liquid_surf_serial_;} // the composite carries the edge fade
 if(liquid_surf_built_serial_==liquid_surf_serial_||!device_)return;
 liquid_surf_built_serial_=liquid_surf_serial_;
 liquid_surf_srv_.Reset();liquid_surf_level_srv_.Reset();liquid_surf_window_={};liquid_surf_swell_={};
 liquid12_.surf.reset();liquid12_.surf_level.reset();
 struct Piece{long x,y;const LiquidSurfField* field;};
 std::vector<Piece> pieces;
 for(const auto& n:liquid_surf_neighbours_)
  pieces.push_back({std::lround(n.origin[0]*.5f),std::lround(n.origin[1]*.5f),n.field.get()});
 if(liquid_surf_active_&&liquid_surf_active_->surf_cells)pieces.push_back({0,0,liquid_surf_active_.get()});
 if(pieces.empty())return;
 long x0=LONG_MAX,y0=LONG_MAX,x1=LONG_MIN,y1=LONG_MIN;
 for(const auto& p:pieces){
  x0=std::min(x0,p.x);y0=std::min(y0,p.y);
  x1=std::max(x1,p.x+long(p.field->tiles_x));y1=std::max(y1,p.y+long(p.field->tiles_y));
 }
 const long width=x1-x0,height=y1-y0;
 if(width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION){
  bridge_report_event("water_surf","omitted: the maps with surf span more than the largest texture side");
  return;
 }
 std::vector<std::array<std::uint8_t,4>> texels(std::size_t(width)*height,std::array<std::uint8_t,4>{0,128,128,255});
 // t63: sea level, shore impact, smooth swell distance (m), smooth depth (m) (liquid_surf.h).
 std::vector<std::array<float,4>> levels(std::size_t(width)*height,std::array<float,4>{liquidSurfNoLevel,0.f,liquidSurfSwellReach,0.f});
 // covered: 1 + the index of the piece that owns the texel (0: none). A
 // later piece owns a texel whole (surf with its own depth and distance:
 // one map's surf over another's depth broke waves along the seam).
 std::vector<std::uint8_t> covered(std::size_t(width)*height,0);
 std::vector<std::uint8_t> water(std::size_t(width)*height,0); // the owning piece's water tiles: their waterline distance
 for(std::size_t piece=0;piece<pieces.size()&&piece<255;++piece){
  const auto& p=pieces[piece];
  for(std::uint32_t y=0;y<p.field->tiles_y;++y)for(std::uint32_t x=0;x<p.field->tiles_x;++x){
   const std::size_t source=std::size_t(y)*p.field->tiles_x+x,target=std::size_t(p.y-y0+long(y))*width+std::size_t(p.x-x0+long(x));
   covered[target]=static_cast<std::uint8_t>(piece+1);
   levels[target]={liquidSurfNoLevel,0.f,liquidSurfSwellReach,0.f};
   if(p.field->level[source]!=liquidSurfNoLevel){levels[target][0]=p.field->level[source];levels[target][1]=p.field->impact[source];}
   if(p.field->swell_distance.size()==p.field->texels.size()){levels[target][2]=p.field->swell_distance[source];levels[target][3]=p.field->swell_depth[source];}
   const auto& t=p.field->texels[source];
   water[target]=t[3]>=64?t[3]:0; // waterline distance (water .25 and up, land under), 0 on land
   texels[target]=t[0]?t:std::array<std::uint8_t,4>{0,128,128,255}; // no surf: the default texel
  }
 }
 // Sea across map seams: each field marks its own sea (a core run along its
 // edge, then a city-block reach) and spills it into its other water over
 // liquidSurfSpill, but water a neighbour map's sea runs into is not sea in
 // its own field, so the sea look stopped at the seam in a straight line or,
 // past a weak first spill, in the field's staircase sea outline (Dejavu
 // gipat1 sea beside zone1 coast water). Every water texel's exposure spills
 // through water, fading over kSeaSpill metres, and raises the exposure of
 // water outside the sea band (no sea level: other water, open sea); colour
 // only (r), no swell (gb stays level). Sea band texels keep their own.
 {
  constexpr float kSeaSpill=40.f; // m
  const std::size_t count=texels.size();
  std::vector<float> spill(count,0.f);
  for(std::size_t i=0;i<count;++i)if(water[i])spill[i]=texels[i][0]/255.f;
  const float step=2.f/kSeaSpill,diagonal=step*1.41421356f;
  const auto relax=[&](std::size_t i,std::size_t j,float d){if(water[i])spill[i]=std::max(spill[i],spill[j]-d);};
  for(long y=0;y<height;++y)for(long x=0;x<width;++x){
   const std::size_t i=std::size_t(y)*width+x;
   if(x)relax(i,i-1,step);
   if(y){relax(i,i-width,step);if(x)relax(i,i-width-1,diagonal);if(x+1<width)relax(i,i-width+1,diagonal);}
  }
  for(long y=height-1;y>=0;--y)for(long x=width-1;x>=0;--x){
   const std::size_t i=std::size_t(y)*width+x;
   if(x+1<width)relax(i,i+1,step);
   if(y+1<height){relax(i,i+width,step);if(x+1<width)relax(i,i+width+1,diagonal);if(x)relax(i,i+width-1,diagonal);}
  }
  for(std::size_t i=0;i<count;++i){
   if(!water[i]||levels[i][0]!=liquidSurfNoLevel)continue;
   const float t=std::min(spill[i],1.f);
   const auto r=static_cast<std::uint8_t>(std::lround(t*t*(3-2*t)*255.f));
   if(r<=texels[i][0])continue;
   if(!texels[i][0])texels[i][3]=water[i]; // keep the shore distance the default texel dropped
   texels[i][0]=r;
  }
 }
 // One regional swell direction: the exposure-weighted mean of the coastal
 // band's incoming directions (open water toward the shores). Its length is
 // how well they agree; where they agree (not an island map, directions all
 // round) all water of the map drifts its ripples this way.
 {
  double sx=0,sy=0,sw=0;
  for(const auto& t:texels){
   if(!t[0])continue;
   const double dx=t[1]/127.5-1,dy=t[2]/127.5-1,l=std::sqrt(dx*dx+dy*dy);
   if(l<.3)continue; // out at sea: no direction
   const double w=t[0]/255.0;sx+=w*dx/l;sy+=w*dy/l;sw+=w;
  }
  const double l=std::sqrt(sx*sx+sy*sy);
  liquid_surf_swell_=sw>0&&l>1e-6?std::array<float,4>{float(sx/l),float(sy/l),float(std::clamp(l/sw,0.0,1.0)),0.f}:std::array<float,4>{};
 }
 // Where the sea runs off the covered maps (a map edge with no surf field
 // beyond it, or the texture's edge), the surf fades to none over
 // water_surf_edge_fade metres instead of stopping at a line: chamfer
 // distance (2 m texels) from the covered texels touching the uncovered.
 if(liquid_surf_fade_>0){
  const std::size_t count=texels.size();
  std::vector<float> distance(count,1e9f);
  for(long y=0;y<height;++y)for(long x=0;x<width;++x){
   const std::size_t i=std::size_t(y)*width+x;
   if(!covered[i])continue;
   if(x==0||y==0||x+1==width||y+1==height||!covered[i-1]||!covered[i+1]||!covered[i-width]||!covered[i+width])distance[i]=0;
  }
  const float diagonal=1.41421356f;
  for(long y=0;y<height;++y)for(long x=0;x<width;++x){
   float& d=distance[std::size_t(y)*width+x];
   if(x)d=std::min(d,distance[std::size_t(y)*width+x-1]+1);
   if(y){
    d=std::min(d,distance[std::size_t(y-1)*width+x]+1);
    if(x)d=std::min(d,distance[std::size_t(y-1)*width+x-1]+diagonal);
    if(x+1<width)d=std::min(d,distance[std::size_t(y-1)*width+x+1]+diagonal);
   }
  }
  for(long y=height-1;y>=0;--y)for(long x=width-1;x>=0;--x){
   float& d=distance[std::size_t(y)*width+x];
   if(x+1<width)d=std::min(d,distance[std::size_t(y)*width+x+1]+1);
   if(y+1<height){
    d=std::min(d,distance[std::size_t(y+1)*width+x]+1);
    if(x+1<width)d=std::min(d,distance[std::size_t(y+1)*width+x+1]+diagonal);
    if(x)d=std::min(d,distance[std::size_t(y+1)*width+x-1]+diagonal);
   }
  }
  for(std::size_t i=0;i<count;++i){
   if(!texels[i][0])continue;
   const float t=std::clamp(distance[i]*2.f/liquid_surf_fade_,0.f,1.f);
   texels[i][0]=static_cast<std::uint8_t>(std::lround(texels[i][0]*t*t*(3-2*t)));
  }
 }
 // Map seams: each map's field measures its own shores, so across a seam
 // between two maps' fields (or into uncovered water) the swell distance and
 // depth jump and the crests cut off or broke in long straight lines (Dejavu
 // gipat1 beside zone1). The swell share (gb, the incoming direction's
 // length: crests, breaking and crest foam) fades to none over kSeamFade
 // metres either side; the exposure (r, the sea colour) is left alone.
 {
  constexpr float kSeamFade=16.f; // m
  const std::size_t count=texels.size();
  std::vector<float> seam(count,1e9f);
  for(long y=0;y<height;++y)for(long x=0;x<width;++x){
   const std::size_t i=std::size_t(y)*width+x;
   if(!covered[i])continue;
   const auto differs=[&](std::size_t j){return covered[j]!=covered[i];};
   if((x&&differs(i-1))||(x+1<width&&differs(i+1))||(y&&differs(i-width))||(y+1<height&&differs(i+width)))seam[i]=0;
  }
  const float diagonal=1.41421356f;
  for(long y=0;y<height;++y)for(long x=0;x<width;++x){
   float& d=seam[std::size_t(y)*width+x];
   if(x)d=std::min(d,seam[std::size_t(y)*width+x-1]+1);
   if(y){
    d=std::min(d,seam[std::size_t(y-1)*width+x]+1);
    if(x)d=std::min(d,seam[std::size_t(y-1)*width+x-1]+diagonal);
    if(x+1<width)d=std::min(d,seam[std::size_t(y-1)*width+x+1]+diagonal);
   }
  }
  for(long y=height-1;y>=0;--y)for(long x=width-1;x>=0;--x){
   float& d=seam[std::size_t(y)*width+x];
   if(x+1<width)d=std::min(d,seam[std::size_t(y)*width+x+1]+1);
   if(y+1<height){
    d=std::min(d,seam[std::size_t(y+1)*width+x]+1);
    if(x+1<width)d=std::min(d,seam[std::size_t(y+1)*width+x+1]+diagonal);
    if(x)d=std::min(d,seam[std::size_t(y+1)*width+x-1]+diagonal);
   }
  }
  for(std::size_t i=0;i<count;++i){
   if(!texels[i][0]||seam[i]*2.f>=kSeamFade)continue;
   const float t=std::clamp(seam[i]*2.f/kSeamFade,0.f,1.f),s=t*t*(3-2*t);
   for(int c=1;c<3;++c)texels[i][c]=static_cast<std::uint8_t>(std::lround(128.f+(texels[i][c]-128.f)*s));
  }
 }
 if(d3d12_){
  D3D12TextureRef surf,level;
  if(FAILED(d3d12_->create_texture(UINT(width),UINT(height),DXGI_FORMAT_R8G8B8A8_UNORM,surf))||
     FAILED(d3d12_->write_texture(*surf,RECT{0,0,LONG(width),LONG(height)},texels.data(),UINT(width)*4u))||
     FAILED(d3d12_->create_texture(UINT(width),UINT(height),DXGI_FORMAT_R32G32B32A32_FLOAT,level))||
     FAILED(d3d12_->write_texture(*level,RECT{0,0,LONG(width),LONG(height)},levels.data(),UINT(width)*16u))){
   bridge_report_event("water_surf","omitted: texture unavailable; water shows no surf");
   return;
  }
  liquid12_.surf=std::move(surf);liquid12_.surf_level=std::move(level);
  liquid_surf_window_={float(x0)*2.f,float(y0)*2.f,.5f/float(width),.5f/float(height)};
  return;
 }
 D3D11_TEXTURE2D_DESC desc{};desc.Width=UINT(width);desc.Height=UINT(height);desc.MipLevels=desc.ArraySize=1;
 desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 const D3D11_SUBRESOURCE_DATA data{texels.data(),UINT(width)*4u,0};
 D3D11_TEXTURE2D_DESC levelDesc=desc;levelDesc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
 const D3D11_SUBRESOURCE_DATA levelData{levels.data(),UINT(width)*16u,0};
 ComPtr<ID3D11Texture2D> texture,levelTexture;
 if(FAILED(device_->CreateTexture2D(&desc,&data,&texture))||FAILED(device_->CreateShaderResourceView(texture.Get(),nullptr,&liquid_surf_srv_))||
    FAILED(device_->CreateTexture2D(&levelDesc,&levelData,&levelTexture))||FAILED(device_->CreateShaderResourceView(levelTexture.Get(),nullptr,&liquid_surf_level_srv_))){
  liquid_surf_srv_.Reset();liquid_surf_level_srv_.Reset();
  bridge_report_event("water_surf","omitted: texture unavailable; water shows no surf");
  return;
 }
 liquid_surf_window_={float(x0)*2.f,float(y0)*2.f,.5f/float(width),.5f/float(height)};
} catch(const std::bad_alloc&) {
 liquid_surf_srv_.Reset();liquid_surf_level_srv_.Reset();liquid_surf_window_={};liquid_surf_swell_={};
 bridge_report_event("water_surf","omitted: CPU allocation failed; water shows no surf");
}

void Renderer::add_liquid_surf_neighbour(const NeighbourPiece& upload,const std::array<float,16>& active_to_world) noexcept try {
 const auto& field=upload.liquid_surf;
 if(!field)return; // no water (or no grids): nothing to say
 std::array<float,3> origin{};
 char line[400];
 if(!ground_cover::neighbour_cover_origin(active_to_world,upload.selection.x,upload.selection.y,origin)){
  std::snprintf(line,sizeof(line),"map=%s water_surf=omitted reason=placement_not_translation",upload.selection.placement.mpr.c_str());
  bridge_neighbour_diagnostic(line);return;
 }
 const auto& s=field->impact_spots;
 std::snprintf(line,sizeof(line),"map=%s water_surf tiles=%u max_effective_fetch=%.0fm steep_spots_world=%.0f,%.0f,%.2f,%.0f|%.0f,%.0f,%.2f,%.0f|%.0f,%.0f,%.2f,%.0f",
  upload.selection.placement.mpr.c_str(),field->surf_cells,field->max_exposure,
  s[0][0]+origin[0],s[0][1]+origin[1],s[0][2],s[0][3],s[1][0]+origin[0],s[1][1]+origin[1],s[1][2],s[1][3],s[2][0]+origin[0],s[2][1]+origin[1],s[2][2],s[2][3]);
 bridge_neighbour_diagnostic(line);
 if(!field->surf_cells)return;
 liquid_surf_neighbours_.push_back({{origin[0],origin[1]},upload.selection.placement.mpr,field});
 ++liquid_surf_serial_;
} catch(...) {}

void Renderer::reset_water_reflections() noexcept {
 clear_water_reflection_bindings();
 water_scene_srv_.Reset();water_history_srv_.Reset();water_scene_color_.Reset();water_scene_depth_srv_.Reset();water_scene_dsv_.Reset();water_scene_depth_.Reset();
 water_history_frame_=UINT64_MAX;
 for(UINT slot=0;slot<2;++slot){particle_depth_srv_[slot].Reset();particle_depth_rtv_[slot].Reset();particle_depth_[slot].Reset();} particle_depth12_={};
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
 depth_description.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_DEPTH_STENCIL;depth_description.CPUAccessFlags=0;
 depth_description.MiscFlags=0;depth_description.Usage=D3D11_USAGE_DEFAULT;
 if(FAILED(hr=device_->CreateTexture2D(&depth_description,nullptr,&depth_copy)))return hr;
 D3D11_SHADER_RESOURCE_VIEW_DESC depth_view{};depth_srv_->GetDesc(&depth_view);
 if(FAILED(hr=device_->CreateShaderResourceView(depth_copy.Get(),&depth_view,&depth_copy_srv)))return hr;
 // Ground marks held past the water test against it (the water writes the real depth).
 ComPtr<ID3D11DepthStencilView> depth_copy_dsv;D3D11_DEPTH_STENCIL_VIEW_DESC depth_target{};dsv_->GetDesc(&depth_target);
 if(FAILED(hr=device_->CreateDepthStencilView(depth_copy.Get(),&depth_target,&depth_copy_dsv)))return hr;
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
 water_scene_depth_=std::move(depth_copy);water_scene_depth_srv_=std::move(depth_copy_srv);water_scene_dsv_=std::move(depth_copy_dsv);water_reflection_constants_=std::move(constants);
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
 // (kWaterSsrFormats, renderer.h)
 HRESULT hr=S_OK;
 for(unsigned n=0;n<6;++n) {
  D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
  desc.Format=kWaterSsrFormats[n];desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
  if(FAILED(hr=device_->CreateTexture2D(&desc,nullptr,&water_ssr_textures_[n]))||
     FAILED(hr=device_->CreateRenderTargetView(water_ssr_textures_[n].Get(),nullptr,&water_ssr_rtv_[n]))||
     FAILED(hr=device_->CreateShaderResourceView(water_ssr_textures_[n].Get(),nullptr,&water_ssr_srv_[n])))return hr;
 }
 D3D11_TEXTURE2D_DESC depth{};depth.Width=width;depth.Height=height;depth.MipLevels=depth.ArraySize=depth.SampleDesc.Count=1;
 depth.Format=kWaterSsrDepthFormat;depth.Usage=D3D11_USAGE_DEFAULT;depth.BindFlags=D3D11_BIND_DEPTH_STENCIL;
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
// The reflection scene copy and depth snapshot on D3D12 (create_water_reflection_targets):
// two slices of the world target's format, the history with a short mip chain
// where compute can write the format (generate_mips; else one level, as D3D11
// keeps formats without mip generation), and a depth-only target of the world
// depth's format. Remade when the world target's size changes.
HRESULT Renderer::create_water_reflection_targets_d3d12() {
 if(!world_d3d12_)return E_FAIL;
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 if(water12_.scene&&d3d12_->texture_matches(*water12_.scene,width,height,format))return S_OK;
 water12_.scene.reset();water12_.depth.reset();
 const bool mips=d3d12_->storage_supported(format);
 levels=1;
 if(mips)for(UINT size=(std::max)(width,height);size>1&&levels<kWaterSceneMipLevels;size>>=1)++levels;
 D3D12TextureRef scene,depth;
 HRESULT hr=mips?d3d12_->create_storage_texture(width,height,0,format,scene,levels,2):d3d12_->create_texture_array(width,height,format,1,2,scene);
 if(FAILED(hr))return hr;
 if(FAILED(hr=d3d12_->create_target(width,height,depth,DXGI_FORMAT_UNKNOWN,DXGI_FORMAT_D24_UNORM_S8_UINT)))return hr;
 water12_.scene=std::move(scene);water12_.depth=std::move(depth);
 water_scene_levels_=levels;water_history_frame_=UINT64_MAX;
 return S_OK;
}
// The half-resolution trace targets on D3D12 (create_water_ssr_targets).
HRESULT Renderer::create_water_ssr_targets_d3d12() {
 if(!world_d3d12_)return E_FAIL;
 UINT world_width=0,world_height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,world_width,world_height,levels,format))return E_FAIL;
 const UINT width=(world_width+1)/2,height=(world_height+1)/2;
 if(water12_.ssr_depth&&width==water_ssr_width_&&height==water_ssr_height_)return S_OK;
 for(auto& target:water12_.ssr)target.reset();
 water12_.ssr_depth.reset();water_ssr_width_=water_ssr_height_=0;
 HRESULT hr=S_OK;
 for(unsigned n=0;n<6;++n)
  if(FAILED(hr=d3d12_->create_target(width,height,water12_.ssr[n],kWaterSsrFormats[n],DXGI_FORMAT_UNKNOWN)))return hr;
 if(FAILED(hr=d3d12_->create_target(width,height,water12_.ssr_depth,DXGI_FORMAT_UNKNOWN,kWaterSsrDepthFormat)))return hr;
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
 // The snapshot is the scene behind this flush's water: taken at the first
 // liquid batch of every flush that draws liquid. Once per frame, a frame
 // whose liquid came in two flushes (an early flush, forced by the game's
 // next draw, carrying a few liquid records; then the main liquid flush after
 // the particles and later world work) refracted the main water through the
 // early snapshot on those frames and through a complete one on the others:
 // whatever was drawn between the two flushes (waterfall spray, mist) showed
 // through the water or was hidden by it frame to frame (save1152 waterfall
 // and lake popping, user captures 526/527: the copy before or after the game
 // particles). Everything drawn before a flush's water is now behind it.
 if(water_capture_frame_!=native_frame_index_||water_capture_flush_!=native_flush_serial_) {
  water_capture_frame_=native_frame_index_;water_capture_flush_=native_flush_serial_;water_capture_ready_=false;
  if(!enabled)return false;
  if(!native_world_clip_valid_||FAILED(d3d12_?create_water_reflection_targets_d3d12():create_water_reflection_targets()))
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
  // Screen-bounded march steps (liquidSceneReflection): 0 keeps the fixed count.
  constants.march={std::max(configured_floating_point(debug_overlay_.config(),"water_reflection_step_pixels",8.f),0.f),0,0,0};
  constants.history_inverse=history?water_history_inverse_:constants.inverse;
  if(quality!=water_reflection_quality_logged_) {
   water_reflection_quality_logged_=quality;
   char line[96];
   std::snprintf(line,sizeof(line),"EID3D11 water reflections: quality %d, %.0f march steps",quality,constants.quality[0]);
   bridge_append_probe_event("water_reflections",line);
  }
  if(d3d12_){ // D3D12: the constants stay here (b7, set per draw); the copies in stream order
   water12_.constants=constants;
   transition_profile_pass("liquid_scene_copy");
   HRESULT copied=d3d12_->copy_texture(*water12_.scene,0,*world_d3d12_,0); // slice 0, level 0
   if(SUCCEEDED(copied))copied=d3d12_->copy_depth(*water12_.depth,*world_d3d12_);
   end_profile_pass();
   if(FAILED(copied))return unavailable("EID3D11 water reflections: scene copy failed; using analytic sky\n");
   water_capture_clip_=native_world_clip_;water_capture_ready_=true;
  }else{
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(FAILED(context_->Map(water_reflection_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))
   return unavailable("EID3D11 water reflections: constant upload failed; using analytic sky\n");
  std::memcpy(mapped.pData,&constants,sizeof(constants));context_->Unmap(water_reflection_constants_.Get(),0);
  clear_water_reflection_bindings();
  ComPtr<ID3D11Resource> source;rtv_->GetResource(&source);
  // The copy leaves the targets unbound; the native flush (the only caller)
  // binds its history/AO targets again for the batch that follows.
  context_->OMSetRenderTargets(0,nullptr,nullptr);
  transition_profile_pass("liquid_scene_copy");
  context_->CopySubresourceRegion(water_scene_color_.Get(),D3D11CalcSubresource(0,0,water_scene_levels_),0,0,0,source.Get(),0,nullptr);
  context_->CopyResource(water_scene_depth_.Get(),depth_.Get());
  end_profile_pass();
  water_capture_clip_=native_world_clip_;water_capture_ready_=true;
  }
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
 if(d3d12_){
  if(!water12_.scene||!water_capture_ready_||water_capture_frame_!=native_frame_index_||!world_d3d12_)return;
  transition_profile_pass("liquid_history_copy");
  HRESULT copied=d3d12_->copy_texture(*water12_.scene,D3D11CalcSubresource(0,1,water_scene_levels_),*world_d3d12_,0);
  if(SUCCEEDED(copied)&&water_scene_levels_>1){
   copied=d3d12_->generate_mips(*water12_.scene,d3d12_mip_array_program(),1);
   if(copied==S_FALSE)d3d12_->count("water history mips waiting for their program");
  }
  end_profile_pass();
  if(FAILED(copied))return;
 }else{
 if(!water_scene_srv_||!water_capture_ready_||water_capture_frame_!=native_frame_index_||!rtv_)return;
 clear_water_reflection_bindings();
 ComPtr<ID3D11Resource> source;rtv_->GetResource(&source);
 // Targets stay unbound: the composite steps after it bind their own.
 context_->OMSetRenderTargets(0,nullptr,nullptr);
 transition_profile_pass("liquid_history_copy");
 context_->CopySubresourceRegion(water_scene_color_.Get(),D3D11CalcSubresource(0,1,water_scene_levels_),0,0,0,source.Get(),0,nullptr);
 if(water_scene_levels_>1)context_->GenerateMips(water_history_srv_.Get());
 end_profile_pass();
 }
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
namespace {
void particle_depth_unavailable(bool& failed,const char* reason) {
 failed=true;OutputDebugStringA(reason);bridge_report_event("particle_reflections","unavailable");
}
}
// The depth program and its blend and depth states: made on the first
// particle depth draw or by the load hold's pipeline warm-up.
bool Renderer::ensure_particle_depth_pipeline() {
 if(game_particle_depth_ps_)return true;
 if(particle_depth_failed_)return false;
 const auto fail=[&](const char* reason){particle_depth_unavailable(particle_depth_failed_,reason);return false;};
 UINT support=0;
 if(FAILED(device_->CheckFormatSupport(kParticleDepthFormat,&support))||!(support&D3D11_FORMAT_SUPPORT_BLENDABLE))
  return fail("EID3D11 particle reflections: R32F blending unsupported; particles omitted from reflections\n");
 ComPtr<ID3DBlob> pixel,error;
 if(FAILED(compile_shader(world_support_shader_program(WorldSupportShader::game_particle_depth_ps),&pixel,&error))) {
  if(error)OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
  return fail("EID3D11 particle reflections: depth shader compile failed; particles omitted from reflections\n");
 }
 D3D11_BLEND_DESC blend{};auto& target=blend.RenderTarget[0];
 target.BlendEnable=TRUE;target.SrcBlend=target.DestBlend=target.SrcBlendAlpha=target.DestBlendAlpha=D3D11_BLEND_ONE;
 target.BlendOp=target.BlendOpAlpha=D3D11_BLEND_OP_MIN;target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_RED;
 D3D11_DEPTH_STENCIL_DESC test{};test.DepthEnable=TRUE;test.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
 test.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
 if(FAILED(create_shader(device_.Get(),pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&game_particle_depth_ps_))||
    FAILED(device_->CreateBlendState(&blend,&particle_depth_blend_))||
    FAILED(device_->CreateDepthStencilState(&test,&particle_depth_test_))) {
  game_particle_depth_ps_.Reset();particle_depth_blend_.Reset();particle_depth_test_.Reset();
  return fail("EID3D11 particle reflections: pipeline creation failed; particles omitted from reflections\n");
 }
 return true;
}
void Renderer::draw_game_particle_depth(UINT vertex_count) {
 const auto fail=[&](const char* reason){particle_depth_unavailable(particle_depth_failed_,reason);};
 if(!ensure_particle_depth_pipeline())return;
 if(!particle_depth_[0]||particle_depth_width_!=width_||particle_depth_height_!=height_) {
  for(UINT slot=0;slot<2;++slot){particle_depth_srv_[slot].Reset();particle_depth_rtv_[slot].Reset();particle_depth_[slot].Reset();}
  particle_depth_frame_=particle_depth_history_frame_=UINT64_MAX;
  D3D11_TEXTURE2D_DESC description{};description.Width=width_;description.Height=height_;
  description.MipLevels=description.ArraySize=description.SampleDesc.Count=1;description.Format=kParticleDepthFormat;
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
 // The second draw of the game particle it follows (same vertex stage and
 // inputs). It restores nothing: every following draw binds its own targets,
 // pixel shader, blend and depth state (draw, draw_game_effect, apply_state,
 // the native flush and the world passes). Its target is unbound after it.
 ID3D11ShaderResourceView* empty=nullptr;context_->PSSetShaderResources(28,1,&empty);
 ID3D11RenderTargetView* const output=particle_depth_rtv_[particle_depth_current_].Get();
 if(particle_depth_frame_!=native_frame_index_) {
  const float far_depth[4]={1,1,1,1};context_->ClearRenderTargetView(output,far_depth);
  particle_depth_frame_=native_frame_index_;
 }
 bind_plan_targets(plan_pass("particle_reflection_depth",output,dsv_.Get()));
 context_->OMSetBlendState(particle_depth_blend_.Get(),nullptr,0xffffffffu);
 context_->OMSetDepthStencilState(particle_depth_test_.Get(),0);
 context_->PSSetShader(game_particle_depth_ps_.Get(),nullptr,0);
 context_->Draw(vertex_count,0);
 profiler_.add_draw(vertex_count/3u,1);profiler_.add_batch();
 context_->OMSetRenderTargets(0,nullptr,nullptr);
}

#endif
} // namespace owned
