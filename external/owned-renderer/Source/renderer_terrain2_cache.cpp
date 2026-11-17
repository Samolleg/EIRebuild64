// Terrain 2.0 material cache (terrain2_cache.h, option terrain2_cache): a
// camera-centred clipmap of terrain2Color, baked by full-screen passes over
// world-aligned texel rectangles and read by the terrain pixel stage where
// it holds exactly what that pixel would compute.
#include "terrain2_cache.h"
#include "renderer.h"
#include "renderer_internal.h"
#include "engine_bridge.h"
#include "point_shadow_types.h"
#include "contact_blend.h"
#include "terrain2.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace owned {
namespace {

bool invert4(const std::array<float,16>& m,std::array<double,16>& r){
 double a[16];for(int k=0;k<16;++k)a[k]=m[k];
 r[0]=a[5]*a[10]*a[15]-a[5]*a[11]*a[14]-a[9]*a[6]*a[15]+a[9]*a[7]*a[14]+a[13]*a[6]*a[11]-a[13]*a[7]*a[10];
 r[4]=-a[4]*a[10]*a[15]+a[4]*a[11]*a[14]+a[8]*a[6]*a[15]-a[8]*a[7]*a[14]-a[12]*a[6]*a[11]+a[12]*a[7]*a[10];
 r[8]=a[4]*a[9]*a[15]-a[4]*a[11]*a[13]-a[8]*a[5]*a[15]+a[8]*a[7]*a[13]+a[12]*a[5]*a[11]-a[12]*a[7]*a[9];
 r[12]=-a[4]*a[9]*a[14]+a[4]*a[10]*a[13]+a[8]*a[5]*a[14]-a[8]*a[6]*a[13]-a[12]*a[5]*a[10]+a[12]*a[6]*a[9];
 r[1]=-a[1]*a[10]*a[15]+a[1]*a[11]*a[14]+a[9]*a[2]*a[15]-a[9]*a[3]*a[14]-a[13]*a[2]*a[11]+a[13]*a[3]*a[10];
 r[5]=a[0]*a[10]*a[15]-a[0]*a[11]*a[14]-a[8]*a[2]*a[15]+a[8]*a[3]*a[14]+a[12]*a[2]*a[11]-a[12]*a[3]*a[10];
 r[9]=-a[0]*a[9]*a[15]+a[0]*a[11]*a[13]+a[8]*a[1]*a[15]-a[8]*a[3]*a[13]-a[12]*a[1]*a[11]+a[12]*a[3]*a[9];
 r[13]=a[0]*a[9]*a[14]-a[0]*a[10]*a[13]-a[8]*a[1]*a[14]+a[8]*a[2]*a[13]+a[12]*a[1]*a[10]-a[12]*a[2]*a[9];
 r[2]=a[1]*a[6]*a[15]-a[1]*a[7]*a[14]-a[5]*a[2]*a[15]+a[5]*a[3]*a[14]+a[13]*a[2]*a[7]-a[13]*a[3]*a[6];
 r[6]=-a[0]*a[6]*a[15]+a[0]*a[7]*a[14]+a[4]*a[2]*a[15]-a[4]*a[3]*a[14]-a[12]*a[2]*a[7]+a[12]*a[3]*a[6];
 r[10]=a[0]*a[5]*a[15]-a[0]*a[7]*a[13]-a[4]*a[1]*a[15]+a[4]*a[3]*a[13]+a[12]*a[1]*a[7]-a[12]*a[3]*a[5];
 r[14]=-a[0]*a[5]*a[14]+a[0]*a[6]*a[13]+a[4]*a[1]*a[14]-a[4]*a[2]*a[13]-a[12]*a[1]*a[6]+a[12]*a[2]*a[5];
 r[3]=-a[1]*a[6]*a[11]+a[1]*a[7]*a[10]+a[5]*a[2]*a[11]-a[5]*a[3]*a[10]-a[9]*a[2]*a[7]+a[9]*a[3]*a[6];
 r[7]=a[0]*a[6]*a[11]-a[0]*a[7]*a[10]-a[4]*a[2]*a[11]+a[4]*a[3]*a[10]+a[8]*a[2]*a[7]-a[8]*a[3]*a[6];
 r[11]=-a[0]*a[5]*a[11]+a[0]*a[7]*a[9]+a[4]*a[1]*a[11]-a[4]*a[3]*a[9]-a[8]*a[1]*a[7]+a[8]*a[3]*a[5];
 r[15]=a[0]*a[5]*a[10]-a[0]*a[6]*a[9]-a[4]*a[1]*a[10]+a[4]*a[2]*a[9]+a[8]*a[1]*a[6]-a[8]*a[2]*a[5];
 const double det=a[0]*r[0]+a[1]*r[4]+a[2]*r[8]+a[3]*r[12];
 if(!(std::abs(det)>1e-30))return false;
 for(double& v:r)v/=det;
 return true;
}

std::uint64_t fnv(std::uint64_t h,const void* data,std::size_t bytes){
 const auto* p=static_cast<const unsigned char*>(data);
 for(std::size_t k=0;k<bytes;++k){h^=p[k];h*=1099511628211ull;}
 return h;
}

float level_density(unsigned l){return kTerrain2CacheTopDensity/float(1u<<l);}

}  // namespace

void append_terrain2_cache_shader_programs(std::vector<ShaderProgram>& out){
 const std::string& source=owned_native_shader_source();
 // NATIVE_TERRAIN2_POOLS: the bake reads every map's pool (terrain2.hlsl T2_POOL).
 static const D3D_SHADER_MACRO terrain[]={{"NATIVE_TERRAIN_PS","1"},{"NATIVE_TERRAIN2_POOLS","1"},{nullptr,nullptr}};
 out.push_back({source,nullptr,terrain,"nativeTerrain2CacheVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3}); // terrain: 2.0 is compiled in (native_shader.h)
 out.push_back({source,nullptr,terrain,"nativeTerrain2CachePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

// The D3D12 bake: the pixel bake's texels by compute (terrain2_cache.h).
void append_terrain2_cache_d3d12_programs(std::vector<ShaderProgram>& out){
 const std::string& source=owned_native_shader_source();
 static const D3D_SHADER_MACRO terrain[]={{"NATIVE_TERRAIN_PS","1"},{nullptr,nullptr}};
 out.push_back({source,nullptr,terrain,"nativeTerrain2CacheCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

// Shader objects with the startup (or terrain2 enabling) preparation, never
// on the first bake frame, plus the fixed states and buffers. D3D12: the
// compute bake's program (its pipeline is made on the first dispatch) and
// the params buffer.
#if !defined(EID3D11_SHADER_EMBED)
HRESULT Renderer::ensure_terrain2_cache_programs() noexcept{
 if(d3d12_){
  if(!terrain2_cache_programs12_.empty()&&terrain2_cache_params12_)return S_OK;
  if(terrain2_cache_failed_)return E_FAIL;
  try{if(terrain2_cache_programs12_.empty())append_terrain2_cache_d3d12_programs(terrain2_cache_programs12_);}catch(...){return E_OUTOFMEMORY;}
  const HRESULT hr=d3d12_->create_buffer(16ull*kTerrain2CacheParamCount,16,0,terrain2_cache_params12_);
  if(FAILED(hr)){
   terrain2_cache_params12_.reset();terrain2_cache_failed_=true;
   char text[160];std::snprintf(text,sizeof(text),"params buffer could not be created (0x%08lx, D3D12); terrain 2.0 stays per pixel",static_cast<unsigned long>(hr));
   bridge_append_probe_event("terrain2_cache_omitted",text);
  }
  return hr;
 }
 if(terrain2_cache_vs_&&terrain2_cache_ps_&&terrain2_cache_params_srv_)return S_OK;
 if(terrain2_cache_failed_||!device_||feature_level_<D3D_FEATURE_LEVEL_11_0)return E_FAIL;
 std::vector<ShaderProgram> programs;append_terrain2_cache_shader_programs(programs);
 HRESULT hr=S_OK;ComPtr<ID3DBlob> code[2];
 for(unsigned k=0;k<2&&SUCCEEDED(hr);++k){
  ComPtr<ID3DBlob> error;hr=compile_shader(programs[k],&code[k],&error);
  if(FAILED(hr)&&error)OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
 }
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),code[0]->GetBufferPointer(),code[0]->GetBufferSize(),nullptr,&terrain2_cache_vs_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),code[1]->GetBufferPointer(),code[1]->GetBufferSize(),nullptr,&terrain2_cache_ps_);
 if(SUCCEEDED(hr)){
  D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sampler.MaxLOD=0;
  hr=device_->CreateSamplerState(&sampler,&terrain2_cache_sampler_);
 }
 if(SUCCEEDED(hr)){
  D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
  hr=device_->CreateRasterizerState(&raster,&terrain2_cache_raster_);
 }
 if(SUCCEEDED(hr)){
  D3D11_BUFFER_DESC d{};d.ByteWidth=32;d.Usage=D3D11_USAGE_DYNAMIC;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  hr=device_->CreateBuffer(&d,nullptr,&terrain2_cache_bake_constants_);
 }
 if(SUCCEEDED(hr)){
  D3D11_BUFFER_DESC d{};d.ByteWidth=16*kTerrain2CacheParamCount;d.Usage=D3D11_USAGE_DYNAMIC;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;d.StructureByteStride=16;
  hr=device_->CreateBuffer(&d,nullptr,&terrain2_cache_params_);
  if(SUCCEEDED(hr))hr=device_->CreateShaderResourceView(terrain2_cache_params_.Get(),nullptr,&terrain2_cache_params_srv_);
 }
 if(FAILED(hr)){
  terrain2_cache_vs_.Reset();terrain2_cache_ps_.Reset();terrain2_cache_sampler_.Reset();terrain2_cache_raster_.Reset();
  terrain2_cache_bake_constants_.Reset();terrain2_cache_params_.Reset();terrain2_cache_params_srv_.Reset();
  terrain2_cache_failed_=true;
  char text[160];std::snprintf(text,sizeof(text),"programs or buffers could not be created (0x%08lx); terrain 2.0 stays per pixel",static_cast<unsigned long>(hr));
  bridge_append_probe_event("terrain2_cache_omitted",text);
 }
 return hr;
}

// The material cache's height layer: x64 adds the bed flag channel (terrain2_cache.h;
// +1/4 of the colour memory); the 32-bit renderer keeps R8 (address space) and its
// dry banks under liquids stay per pixel.
static constexpr DXGI_FORMAT kTerrain2CacheHeightFormat=sizeof(void*)==8?DXGI_FORMAT_R8G8_UNORM:DXGI_FORMAT_R8_UNORM;

// Textures and levels; the programs and fixed states stay.
void Renderer::release_terrain2_cache() noexcept{
 terrain2_cache_fine_.Reset();terrain2_cache_fine_srv_.Reset();terrain2_cache_fine_rtvs_.clear();
 terrain2_cache_coarse_.Reset();terrain2_cache_coarse_srv_.Reset();terrain2_cache_coarse_rtv_.Reset();
 terrain2_cache_height_fine_.Reset();terrain2_cache_height_fine_srv_.Reset();terrain2_cache_height_fine_rtvs_.clear();
 terrain2_cache_height_coarse_.Reset();terrain2_cache_height_coarse_srv_.Reset();terrain2_cache_height_coarse_rtv_.Reset();
 terrain2_cache_height_fine12_.reset();terrain2_cache_height_coarse12_.reset();
 terrain2_cache_fine12_.reset();terrain2_cache_coarse12_.reset();
 terrain2_cache_side_=terrain2_cache_slices_=0;terrain2_cache_coarse_size_[0]=terrain2_cache_coarse_size_[1]=0;
 for(auto& level:terrain2_cache_levels_)level=Terrain2CacheLevelState{};
 terrain2_cache_on_=false;
}

// Every flush: the cache's views for the terrain pixel stage (t116-t118,
// s15); the params say off until a frame has set them up. D3D12: the world
// flush binds them (bind_terrain2_d3d12).
void Renderer::bind_terrain2_cache() noexcept{
 if(d3d12_)return;
 ID3D11ShaderResourceView* views[3]{terrain2_cache_on_?terrain2_cache_coarse_srv_.Get():nullptr,
  terrain2_cache_on_?terrain2_cache_params_srv_.Get():nullptr,terrain2_cache_on_?terrain2_cache_fine_srv_.Get():nullptr};
 context_->PSSetShaderResources(116,3,views);
 ID3D11ShaderResourceView* heights[2]{terrain2_cache_on_?terrain2_cache_height_fine_srv_.Get():nullptr,terrain2_cache_on_?terrain2_cache_height_coarse_srv_.Get():nullptr};
 context_->PSSetShaderResources(78,2,heights); // S3 height layer
 ID3D11SamplerState* sampler=terrain2_cache_sampler_.Get();context_->PSSetSamplers(15,1,&sampler);
}

// One rectangle of global texels of a level: per piece of the target (a
// fine level's window wraps around its slice) and per map (neighbours first,
// the active map last over its own texels), a full-screen triangle through
// that map's batch constants and contact views. D3D12: the same rectangles
// by compute (nativeTerrain2CacheCS), one dispatch a part over its texels.
bool Renderer::bake_terrain2_cache_rect(unsigned l,const Terrain2CacheRect& rect) noexcept{
 auto& level=terrain2_cache_levels_[l];
 const float density=level_density(l);
 if(!d3d12_){
  ID3D11RenderTargetView* targets[2]{level.fine?terrain2_cache_fine_rtvs_[std::size_t(level.slice)].Get():terrain2_cache_coarse_rtv_.Get(),
   level.fine?terrain2_cache_height_fine_rtvs_[std::size_t(level.slice)].Get():terrain2_cache_height_coarse_rtv_.Get()}; // colour, S3 height
  context_->OMSetRenderTargets(2,targets,nullptr);
 }
 // Pieces: global rectangle and its target origin.
 struct Piece{Terrain2CacheRect g;int px,py;};
 Piece pieces[4];unsigned count=0;
 if(level.fine){
  const int n=terrain2_cache_side_;
  int xs[3]{rect.x0,rect.x1,rect.x1},ys[3]{rect.y0,rect.y1,rect.y1};unsigned nx=1,ny=1;
  const int ax=terrain2_cache_mod(rect.x0,n),ay=terrain2_cache_mod(rect.y0,n);
  if(ax+(rect.x1-rect.x0)>n){xs[1]=rect.x0+(n-ax);nx=2;}
  if(ay+(rect.y1-rect.y0)>n){ys[1]=rect.y0+(n-ay);ny=2;}
  for(unsigned j=0;j<ny;++j)for(unsigned i=0;i<nx;++i){
   const Terrain2CacheRect g{xs[i],ys[j],xs[i+1],ys[j+1]};
   pieces[count++]={g,terrain2_cache_mod(g.x0,n),terrain2_cache_mod(g.y0,n)};
  }
 }else pieces[count++]={rect,level.atlas[0]+(rect.x0-level.window.x0),level.atlas[1]+(rect.y0-level.window.y0)};
 const float* records=terrain2_cache_frame_values_.data()+24;
 const auto map_texels=[&](unsigned k,int margin){
  const float* r=records+4*kTerrain2Records*k;
  return Terrain2CacheRect{int(std::ceil(r[0]*density-.5f))-margin,int(std::ceil(r[1]*density-.5f))-margin,
                           int(std::ceil(r[2]*density-.5f))+margin,int(std::ceil(r[3]*density-.5f))+margin};
 };
 unsigned active=kTerrain2TableMaps;
 for(unsigned k=0;k<terrain2_table_count_;++k)if(terrain2_table_[k]==0)active=k;
 const bool active_art=(d3d12_?contact_maps_.tiles12&&contact_maps_.atlas12:contact_maps_.tiles&&contact_maps_.atlas)&&
  contact_map_generation_==ambient_map_generation_&&contact_constants_generation_==contact_map_generation_;
 const bool active_ground=gc_map_generation_==ambient_map_generation_&&
  (d3d12_?gc12_.map.ground&&gc12_.map.generation==ambient_map_generation_:bool(ambient_ground_srv_));
 for(unsigned order=0;order<terrain2_table_count_;++order){
  // Neighbours in table order, the active map last.
  unsigned k=order;
  if(active<terrain2_table_count_){if(order==terrain2_table_count_-1)k=active;else if(order>=active)k=order+1;}
  ID3D11ShaderResourceView* views[4]{};
  const D3D12Texture* views12[4]{}; // D3D12: ground, colour, grid, atlas
  Constants constants{};identity(constants.world);identity(constants.view);identity(constants.projection);
  constants.textured=1;constants.contact_origin[3]=1;
  std::uint64_t ready=0;
  if(terrain2_table_[k]==0){
   if(active_ground){views[0]=ambient_ground_srv_.Get();views[1]=gc_color_srv_.Get();views12[0]=gc12_.map.ground.get();views12[1]=gc12_.map.color.get();}
   if(active_art){views[2]=contact_maps_.tiles.Get();views[3]=contact_maps_.atlas.Get();ready=contact_maps_.ready;
    views12[2]=contact_maps_.tiles12.get();views12[3]=contact_maps_.atlas12.get();}
   constants.layout_origin[0]=terrain_layout_origin_[0];constants.layout_origin[1]=terrain_layout_origin_[1];
  }else{
   bool found=false;
   for(const auto& n:gc_neighbours_)if(n.contact_map==terrain2_table_[k]){
    if(n.prepared){views[0]=n.ground.Get();views[1]=n.color.Get();}
    if(n.cover12.color_ready){views12[0]=n.cover12.ground.get();views12[1]=n.cover12.color.get();}
    views12[2]=n.contact.tiles12.get();views12[3]=n.contact.atlas12.get();
    views[2]=n.contact.tiles.Get();views[3]=n.contact.atlas.Get();ready=n.contact.ready;
    constants.contact_origin[0]=n.origin[0];constants.contact_origin[1]=n.origin[1];constants.contact_origin[2]=n.origin[2];constants.contact_origin[3]=0;
    constants.layout_origin[0]=n.layout_origin[0];constants.layout_origin[1]=n.layout_origin[1];found=true;break;
   }
   if(!found)continue;
  }
  constants.layout_origin[3]=float(k+1);
  constants.contact_materials[0]=static_cast<UINT>(ready);constants.contact_materials[1]=static_cast<UINT>(ready>>32);
  const Terrain2CacheRect own=map_texels(k,kTerrain2CacheMargin);
  bool bound=false;
  for(unsigned p=0;p<count;++p){
   const Piece& piece=pieces[p];
   Terrain2CacheRect parts[4];unsigned parts_count=0;
   const Terrain2CacheRect area=terrain2_cache_intersect(piece.g,own);
   if(area.empty())continue;
   if(k!=active&&active<terrain2_table_count_)
    terrain2_cache_subtract(area,map_texels(active,0),[&](const Terrain2CacheRect& r){if(parts_count<4)parts[parts_count++]=r;});
   else parts[parts_count++]=area;
   for(unsigned q=0;q<parts_count;++q){
    const Terrain2CacheRect& r=parts[q];
    if(d3d12_){
     // b12: the pixel bake's offset and texel size, the part's target
     // rectangle and the fine slice (-1: the coarse atlas).
     struct{int offset[4];float texel[4];int target[4];}values{{piece.g.x0-piece.px,piece.g.y0-piece.py,0,0},
      {1.f/density,level.fine?float(level.slice):-1.f,0,0},{piece.px+(r.x0-piece.g.x0),piece.py+(r.y0-piece.g.y0),r.x1-r.x0,r.y1-r.y0}};
     D3D11_SAMPLER_DESC contact{};contact.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
     contact.AddressU=contact.AddressV=contact.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;contact.MaxLOD=D3D11_FLOAT32_MAX;
     D3D11_SAMPLER_DESC noise{};noise.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
     noise.AddressU=noise.AddressV=noise.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;noise.MaxLOD=0;
     d3d12_->clear_slots();
     d3d12_->set_constants(0,&constants,sizeof(constants));d3d12_->set_constants(12,&values,sizeof(values));
     d3d12_->set_resource(kContactGroundSlotD3D12,views12[0]);d3d12_->set_resource(kContactColorSlotD3D12,views12[1]);
     d3d12_->set_resource(15,views12[2]);d3d12_->set_resource(16,views12[3]);
     d3d12_->set_resource(127,native12_.terrain_frame.get());d3d12_->set_resource(72,fire_burn_mask12());if(fire_burn_bound_)d3d12_->set_resource(74,fire_burn_materials12());
     d3d12_->set_resource(PointShadowTextureLayout::terrain_noise_srv,native12_.terrain_noise.get());
     d3d12_->set_sampler(8,contact);d3d12_->set_sampler(9,contact);d3d12_->set_sampler(12,noise);
     d3d12_->set_unordered(0,terrain2_cache_fine12_.get());d3d12_->set_unordered(1,terrain2_cache_coarse12_.get());
     d3d12_->set_unordered(2,terrain2_cache_height_fine12_.get());d3d12_->set_unordered(3,terrain2_cache_height_coarse12_.get());
     const HRESULT hr=d3d12_->dispatch(terrain2_cache_programs12_[0],UINT(r.x1-r.x0+7)/8u,UINT(r.y1-r.y0+7)/8u,1);
     d3d12_->clear_slots();
     if(hr!=S_OK){if(hr==S_FALSE)d3d12_->omit("terrain 2.0 material cache bake (pipeline not ready; per pixel meanwhile)",false);return false;}
     continue;
    }
    if(!bound){
     bound=true;
     context_->PSSetShaderResources(13,4,views);
     (void)upload_draw_constants(&constants,sizeof(constants));
    }
    struct{int offset[4];float texel[4];}values{{piece.g.x0-piece.px,piece.g.y0-piece.py,0,0},{1.f/density,0,0,0}};
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context_->Map(terrain2_cache_bake_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
    std::memcpy(mapped.pData,&values,sizeof(values));context_->Unmap(terrain2_cache_bake_constants_.Get(),0);
    const D3D11_VIEWPORT viewport{float(piece.px+(r.x0-piece.g.x0)),float(piece.py+(r.y0-piece.g.y0)),float(r.x1-r.x0),float(r.y1-r.y0),0,1};
    context_->RSSetViewports(1,&viewport);
    context_->Draw(3,0);
   }
  }
 }
 return true;
}

// Everything terrain2Color reads besides the texel's position (both caches' input hash).
std::uint64_t Renderer::terrain2_inputs_hash() const noexcept{
 std::uint64_t inputs=1469598103934665603ull;
 inputs=fnv(inputs,terrain2_cache_frame_values_.data(),sizeof(terrain2_cache_frame_values_));
 inputs=fnv(inputs,terrain_surface_bake_.data(),sizeof(terrain_surface_bake_)); // S3: crevice strength and class roughness (baked alpha)
 inputs=fnv(inputs,&terrain2_cache_revision_,sizeof(terrain2_cache_revision_));
 inputs=fnv(inputs,&contact_maps_.ready,sizeof(contact_maps_.ready));
 inputs=fnv(inputs,&ambient_map_generation_,sizeof(ambient_map_generation_));
 inputs=fnv(inputs,&contact_map_generation_,sizeof(contact_map_generation_));
 inputs=fnv(inputs,&contact_constants_generation_,sizeof(contact_constants_generation_));
 inputs=fnv(inputs,terrain2_table_.data(),sizeof(terrain2_table_));
 inputs=fnv(inputs,terrain_layout_origin_.data(),sizeof(terrain_layout_origin_));
 // (D3D12: the same states of its resources; heights once their levels are made.)
 {const bool own_ground=gc_map_generation_==ambient_map_generation_&&
   (d3d12_?gc12_.map.ground&&gc12_.map.generation==ambient_map_generation_:bool(ambient_ground_srv_));
  inputs=fnv(inputs,&own_ground,sizeof(own_ground));}
 for(const auto& n:gc_neighbours_){
  const unsigned char state=d3d12_?
   (n.cover12.color_ready?1:0)|(n.contact.tiles12?2:0)|(n.contact.atlas12?4:0)|(n.terrain2.height12&&!n.terrain2.mips12?8:0):
   (n.prepared?1:0)|(n.contact.tiles?2:0)|(n.contact.atlas?4:0)|(n.terrain2.height_srv?8:0);
  inputs=fnv(inputs,&n.contact_map,sizeof(n.contact_map));inputs=fnv(inputs,&state,1);inputs=fnv(inputs,&n.contact.ready,sizeof(n.contact.ready));
 }
 return inputs;
}

// The D3D11 state a bake pass changes, saved before it and restored after
// (the material cache and the face cache bakes run inside the world flush).
void Renderer::save_bake_state(BakeStateD3D11& s) noexcept{
 context_->VSGetShader(&s.vs,nullptr,nullptr);context_->PSGetShader(&s.ps,nullptr,nullptr);context_->HSGetShader(&s.hs,nullptr,nullptr);
 context_->DSGetShader(&s.ds,nullptr,nullptr);context_->GSGetShader(&s.gs,nullptr,nullptr);
 context_->IAGetPrimitiveTopology(&s.topology);context_->IAGetInputLayout(&s.layout);context_->RSGetState(&s.raster);
 s.viewport_count=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;context_->RSGetViewports(&s.viewport_count,s.viewports);
 context_->OMGetBlendState(&s.blend,s.blend_factor,&s.sample_mask);context_->OMGetDepthStencilState(&s.depth,&s.stencil_ref);
 {ID3D11RenderTargetView* raw[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};ID3D11DepthStencilView* raw_depth=nullptr;
  context_->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,raw,&raw_depth);
  for(unsigned k=0;k<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;++k)s.targets[k].Attach(raw[k]);s.depth_view.Attach(raw_depth);}
 context_->PSGetConstantBuffers(12,1,&s.ps_b12);
 {ID3D11ShaderResourceView* raw[4]{};context_->PSGetShaderResources(13,4,raw);for(unsigned k=0;k<4;++k)s.contact[k].Attach(raw[k]);
  ID3D11ShaderResourceView* f=nullptr;context_->PSGetShaderResources(127,1,&f);s.frame.Attach(f);}
 try{s.constants.assign(draw_constants_copy_.begin(),draw_constants_copy_.begin()+draw_constants_bytes_);}catch(...){s.constants.clear();}
}
void Renderer::restore_bake_state(const BakeStateD3D11& s) noexcept{
 context_->VSSetShader(s.vs.Get(),nullptr,0);context_->PSSetShader(s.ps.Get(),nullptr,0);context_->HSSetShader(s.hs.Get(),nullptr,0);
 context_->DSSetShader(s.ds.Get(),nullptr,0);context_->GSSetShader(s.gs.Get(),nullptr,0);
 context_->IASetPrimitiveTopology(s.topology);context_->IASetInputLayout(s.layout.Get());
 context_->RSSetState(s.raster.Get());if(s.viewport_count)context_->RSSetViewports(s.viewport_count,s.viewports);
 context_->OMSetBlendState(s.blend.Get(),s.blend_factor,s.sample_mask);context_->OMSetDepthStencilState(s.depth.Get(),s.stencil_ref);
 {ID3D11RenderTargetView* raw[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};for(unsigned k=0;k<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;++k)raw[k]=s.targets[k].Get();
  context_->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,raw,s.depth_view.Get());}
 {ID3D11Buffer* b=s.ps_b12.Get();context_->PSSetConstantBuffers(12,1,&b);}
 {ID3D11ShaderResourceView* raw[4]{s.contact[0].Get(),s.contact[1].Get(),s.contact[2].Get(),s.contact[3].Get()};context_->PSSetShaderResources(13,4,raw);
  ID3D11ShaderResourceView* f=s.frame.Get();context_->PSSetShaderResources(127,1,&f);}
 if(!s.constants.empty())(void)upload_draw_constants(s.constants.data(),UINT(s.constants.size()));
}

// Once a world frame, before the terrain draws: levels for this view,
// invalidation, the bake within its budget, the pixel stage's params.
void Renderer::update_terrain2_cache() noexcept try{
 if(scene_composited_)return;
 const auto& config=debug_overlay_.config();
 const int mode=std::clamp(configured_integer(config,"terrain2_cache",1),0,2);
 const bool on=mode>0&&terrain2_table_count_>0&&native_world_clip_valid_&&
  !terrain2_cache_failed_&&feature_level_>=D3D_FEATURE_LEVEL_11_0;
 if(!on){
  if(terrain2_cache_fine_||terrain2_cache_coarse_||terrain2_cache_fine12_||terrain2_cache_coarse12_||terrain2_cache_on_)release_terrain2_cache();
  bind_terrain2_cache();return;
 }
 if(terrain2_cache_frame_==native_frame_index_||hit_test_active_){bind_terrain2_cache();return;}
 terrain2_cache_frame_=native_frame_index_;
 if(FAILED(ensure_terrain2_cache_programs())){release_terrain2_cache();bind_terrain2_cache();return;}
 if(ambient_map_generation_!=terrain2_cache_map_){release_terrain2_cache();terrain2_cache_map_=ambient_map_generation_;terrain2_cache_unsettled_logged_=false;}

 // The view: eye, pixels per metre at view depth 1, the nearest visible
 // ground distance.
 std::array<double,16> inverse{};
 if(!invert4(native_world_clip_,inverse)||std::abs(inverse[14])<1e-12){bind_terrain2_cache();return;}
 const auto& clip=native_world_clip_;
 const auto unproject=[&](double x,double y,double z,std::array<double,3>& out){
  const double h[4]{inverse[0]*x+inverse[1]*y+inverse[2]*z+inverse[3],inverse[4]*x+inverse[5]*y+inverse[6]*z+inverse[7],
                    inverse[8]*x+inverse[9]*y+inverse[10]*z+inverse[11],inverse[12]*x+inverse[13]*y+inverse[14]*z+inverse[15]};
  if(std::abs(h[3])<1e-12)return false;
  out={h[0]/h[3],h[1]/h[3],h[2]/h[3]};return true;
 };
 const std::array<double,3> eye{inverse[2]/inverse[14],inverse[6]/inverse[14],inverse[10]/inverse[14]};
 std::array<double,3> left{},right{};
 if(!unproject(-.5,0,.5,left)||!unproject(.5,0,.5,right)){bind_terrain2_cache();return;}
 const double axis[3]{right[0]-left[0],right[1]-left[1],right[2]-left[2]};
 const double axis_length=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
 if(axis_length<1e-12){bind_terrain2_cache();return;}
 const double pixels=std::abs(clip[0]*axis[0]+clip[1]*axis[1]+clip[2]*axis[2])/axis_length*width_*.5;
 if(!(pixels>1)){bind_terrain2_cache();return;}
 // Ground under the eye (the active map's vertex heights, clamped), and the
 // nearest point of it the bottom screen edge reaches.
 double ground=0;
 {
  const std::uint32_t vx=ambient_tiles_x_*2u+1u,vy=ambient_tiles_y_*2u+1u;
  if(ambient_height_.size()==std::size_t(vx)*vy){
   const int x=std::clamp(int(std::lround(eye[0])),0,int(vx)-1),y=std::clamp(int(std::lround(eye[1])),0,int(vy)-1);
   ground=ambient_height_[std::size_t(y)*vx+x];
  }
 }
 const double height=std::max(eye[2]-ground,1.0);
 double nearest=1e30;
 for(const double sx:{-1.0,0.0,1.0}){
  std::array<double,3> a{},b{};
  if(!unproject(sx,-1,.1,a)||!unproject(sx,-1,.9,b))continue;
  const double dz=b[2]-a[2];
  if(dz>=-1e-9)continue;
  const double t=(ground-a[2])/dz;
  if(t<0)continue;
  const double px=a[0]+(b[0]-a[0])*t-eye[0],py=a[1]+(b[1]-a[1])*t-eye[1],pz=ground-eye[2];
  nearest=std::min(nearest,std::sqrt(px*px+py*py+pz*pz));
 }
 // Never nearer than the eye's height, never past four times it. (Ground
 // rising toward the camera nearer than this reads the first level slightly
 // magnified.)
 nearest=std::clamp(nearest,height,4*height);
 // Option terrain2_cache_nearest: the estimate above takes the ground level
 // under the eye for the whole view, so ground that rises toward the camera
 // (the camera on or behind a hill) is nearer than it, wants a finer level
 // than the first and runs per pixel (probe on saves 1027, 1207 and 1202:
 // three quarters of the pixels the cache missed). With the option the
 // nearest visible vertex of the active map's ground grid counts too: its
 // view depth, which is what a pixel's footprint follows, less one grid
 // step (a pixel between vertices). Only vertices nearer than the estimate
 // can lower it, so the scan covers that radius around the eye.
 const bool nearest_visible=configured_integer(config,"terrain2_cache_nearest",1)>0;
 if(nearest_visible){
  const std::uint32_t vx=ambient_tiles_x_*2u+1u,vy=ambient_tiles_y_*2u+1u;
  if(ambient_height_.size()==std::size_t(vx)*vy&&vx&&vy){
   const int reach=int(std::ceil(nearest))+1;
   const int x0=std::clamp(int(std::floor(eye[0]))-reach,0,int(vx)-1),x1=std::clamp(int(std::ceil(eye[0]))+reach,0,int(vx)-1);
   const int y0=std::clamp(int(std::floor(eye[1]))-reach,0,int(vy)-1),y1=std::clamp(int(std::ceil(eye[1]))+reach,0,int(vy)-1);
   double depth=nearest+1;
   for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
    const double z=ambient_height_[std::size_t(y)*vx+x];
    const double w=clip[12]*x+clip[13]*y+clip[14]*z+clip[15];
    if(!(w>0)||w>=depth)continue;
    // Inside the view, with a margin for the triangles a vertex just outside it belongs to.
    const double cx=clip[0]*x+clip[1]*y+clip[2]*z+clip[3],cy=clip[4]*x+clip[5]*y+clip[6]*z+clip[7];
    const double margin=w*1.1+2.0*pixels/(width_*.5);
    if(std::abs(cx)>margin||std::abs(cy)>margin)continue;
    depth=w;
   }
   nearest=std::clamp(depth-1,1.0,nearest);
  }
 }

 // World rectangle: every map of the table.
 const float* records=terrain2_cache_frame_values_.data()+24;
 double wx0=1e30,wy0=1e30,wx1=-1e30,wy1=-1e30;
 for(unsigned k=0;k<terrain2_table_count_;++k){
  const float* r=records+4*kTerrain2Records*k;
  wx0=std::min(wx0,double(r[0]));wy0=std::min(wy0,double(r[1]));wx1=std::max(wx1,double(r[2]));wy1=std::max(wy1,double(r[3]));
 }
 if(!(wx1>wx0&&wy1>wy0)){bind_terrain2_cache();return;}
 const double extent=std::max(wx1-wx0,wy1-wy0);

 // Levels and side.
 const int density_percent=std::clamp(configured_integer(config,"terrain2_cache_density",100),25,200);
 const int side=std::clamp(int(std::ceil(kTerrain2CacheSideFactor*pixels*density_percent/100.0/256.0))*256,512,16384);
 int first_level=std::clamp(int(std::floor(std::log2(kTerrain2CacheTopDensity*nearest/pixels))),0,int(kTerrain2CacheLevels)-2);
 // With the option the first level only rises again once the nearest ground
 // is a quarter farther than the level needs: a camera moving along a rise
 // would otherwise drop and bake the finest level over and over.
 if(nearest_visible&&terrain2_cache_first_held_>=0&&first_level>terrain2_cache_first_held_&&
    std::clamp(int(std::floor(std::log2(kTerrain2CacheTopDensity*nearest*.8/pixels))),0,int(kTerrain2CacheLevels)-2)<=terrain2_cache_first_held_)
  first_level=terrain2_cache_first_held_;
 terrain2_cache_first_held_=nearest_visible?first_level:-1;
 const unsigned first=unsigned(first_level);
 unsigned last=first+1;
 while(last<kTerrain2CacheLevels-1&&extent*level_density(last)>128)++last;
 const auto texel_lo=[&](double v,float d){return int(std::floor(v*d))-kTerrain2CacheMargin;};
 const auto texel_hi=[&](double v,float d){return int(std::ceil(v*d))+kTerrain2CacheMargin;};
 // Fine: the world is wider than the window (or than half the 2D texture
 // limit, so the coarse row always fits).
 const int coarse_limit=std::min(side,8192);
 const auto fine_level=[&](unsigned l){const float d=level_density(l);return texel_hi(wx1,d)-texel_lo(wx0,d)>coarse_limit||texel_hi(wy1,d)-texel_lo(wy0,d)>coarse_limit;};
 unsigned fine_count=0;for(unsigned l=first;l<=last;++l)if(fine_level(l))++fine_count;
 // Coarse atlas: the largest coarse level at the origin, each smaller one
 // (at most half its size) in a column beside it.
 int coarse_w=0,coarse_h=0,coarse_at[kTerrain2CacheLevels][2]{},column=0,column_y=0;bool coarse_first=true;
 for(unsigned l=first;l<=last;++l)if(!fine_level(l)){
  const float d=level_density(l);
  const int w=texel_hi(wx1,d)-texel_lo(wx0,d),h=texel_hi(wy1,d)-texel_lo(wy0,d);
  if(coarse_first){coarse_first=false;coarse_w=column=w;coarse_h=h;continue;}
  coarse_at[l][0]=column;coarse_at[l][1]=column_y;column_y+=h;
  coarse_w=std::max(coarse_w,column+w);coarse_h=std::max(coarse_h,column_y);
 }
 // Allocation by demand: the fine array grows to the slices this view
 // needs (never shrinks within a map), the atlas follows the world.
 bool reset_all=false;HRESULT hr=S_OK;
 if(fine_count&&(side!=terrain2_cache_side_||int(fine_count)>terrain2_cache_slices_)){
  const int slices=int(fine_count);
  terrain2_cache_fine_.Reset();terrain2_cache_fine_srv_.Reset();terrain2_cache_fine_rtvs_.clear();terrain2_cache_fine12_.reset();
  terrain2_cache_height_fine_.Reset();terrain2_cache_height_fine_srv_.Reset();terrain2_cache_height_fine_rtvs_.clear();terrain2_cache_height_fine12_.reset();
  // The colour levels (RGBA8: colour, S3 roughness) and the height layer (S3
  // height; x64 R8G8 with the bed flag: terrain2_cache.h).
  const auto fine=[&](DXGI_FORMAT format,D3D12TextureRef& texture12,ComPtr<ID3D11Texture2D>& texture,ComPtr<ID3D11ShaderResourceView>& srv,
                      std::vector<ComPtr<ID3D11RenderTargetView>>& rtvs)->HRESULT{
   if(d3d12_)return d3d12_->create_storage_texture(UINT(side),UINT(side),0,format,texture12,1,UINT(slices));
   D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=UINT(side);d.MipLevels=1;d.ArraySize=UINT(slices);d.Format=format;
   d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
   HRESULT result=device_->CreateTexture2D(&d,nullptr,&texture);
   if(SUCCEEDED(result))result=device_->CreateShaderResourceView(texture.Get(),nullptr,&srv);
   for(int s=0;s<slices&&SUCCEEDED(result);++s){
    D3D11_RENDER_TARGET_VIEW_DESC v{};v.Format=d.Format;v.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    v.Texture2DArray.FirstArraySlice=UINT(s);v.Texture2DArray.ArraySize=1;
    ComPtr<ID3D11RenderTargetView> rtv;result=device_->CreateRenderTargetView(texture.Get(),&v,&rtv);
    rtvs.push_back(rtv);
   }
   return result;
  };
  hr=fine(DXGI_FORMAT_R8G8B8A8_UNORM,terrain2_cache_fine12_,terrain2_cache_fine_,terrain2_cache_fine_srv_,terrain2_cache_fine_rtvs_);
  if(SUCCEEDED(hr))hr=fine(kTerrain2CacheHeightFormat,terrain2_cache_height_fine12_,terrain2_cache_height_fine_,terrain2_cache_height_fine_srv_,terrain2_cache_height_fine_rtvs_);
  terrain2_cache_side_=side;terrain2_cache_slices_=slices;reset_all=true;
 }
 if(SUCCEEDED(hr)&&(coarse_w!=terrain2_cache_coarse_size_[0]||coarse_h!=terrain2_cache_coarse_size_[1])){
  terrain2_cache_coarse_.Reset();terrain2_cache_coarse_srv_.Reset();terrain2_cache_coarse_rtv_.Reset();terrain2_cache_coarse12_.reset();
  terrain2_cache_height_coarse_.Reset();terrain2_cache_height_coarse_srv_.Reset();terrain2_cache_height_coarse_rtv_.Reset();terrain2_cache_height_coarse12_.reset();
  const auto coarse=[&](DXGI_FORMAT format,D3D12TextureRef& texture12,ComPtr<ID3D11Texture2D>& texture,ComPtr<ID3D11ShaderResourceView>& srv,
                        ComPtr<ID3D11RenderTargetView>& rtv)->HRESULT{
   if(coarse_w<=0||coarse_h<=0)return S_OK;
   if(d3d12_)return d3d12_->create_storage_texture(UINT(coarse_w),UINT(coarse_h),0,format,texture12);
   D3D11_TEXTURE2D_DESC d{};d.Width=UINT(coarse_w);d.Height=UINT(coarse_h);d.MipLevels=1;d.ArraySize=1;d.Format=format;
   d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
   HRESULT result=device_->CreateTexture2D(&d,nullptr,&texture);
   if(SUCCEEDED(result))result=device_->CreateShaderResourceView(texture.Get(),nullptr,&srv);
   if(SUCCEEDED(result))result=device_->CreateRenderTargetView(texture.Get(),nullptr,&rtv);
   return result;
  };
  hr=coarse(DXGI_FORMAT_R8G8B8A8_UNORM,terrain2_cache_coarse12_,terrain2_cache_coarse_,terrain2_cache_coarse_srv_,terrain2_cache_coarse_rtv_);
  if(SUCCEEDED(hr))hr=coarse(kTerrain2CacheHeightFormat,terrain2_cache_height_coarse12_,terrain2_cache_height_coarse_,terrain2_cache_height_coarse_srv_,terrain2_cache_height_coarse_rtv_);
  terrain2_cache_coarse_size_[0]=coarse_w;terrain2_cache_coarse_size_[1]=coarse_h;reset_all=true;
 }
 if(FAILED(hr)){
  release_terrain2_cache();terrain2_cache_failed_=true;
  char text[192];std::snprintf(text,sizeof(text),"cache textures could not be created (side %d, %u fine levels, atlas %dx%d; 0x%08lx); terrain 2.0 stays per pixel",
   side,fine_count,coarse_w,coarse_h,static_cast<unsigned long>(hr));
  bridge_append_probe_event("terrain2_cache_omitted",text);bind_terrain2_cache();return;
 }
 if(reset_all){
  for(auto& level:terrain2_cache_levels_)level=Terrain2CacheLevelState{};
  const double mb=(double(side)*side*terrain2_cache_slices_*(fine_count?1:0)+double(coarse_w)*coarse_h)*(sizeof(void*)==8?6:5)/1048576.0; // RGBA8 colour + the height layer (S3; x64: R8G8 with the bed flag)
  char text[256];std::snprintf(text,sizeof(text),"side=%d px_per_m=%.0f nearest=%.1f levels=%u..%u fine=%u slices=%d atlas=%dx%d mb=%.1f",
   side,pixels,nearest,first,last,fine_count,terrain2_cache_slices_,coarse_w,coarse_h,mb);
  bridge_append_probe_event("terrain2_cache",text);
 }

 // Inputs: everything terrain2Color reads besides the texel's position.
 const std::uint64_t inputs=terrain2_inputs_hash();
 const bool changed=inputs!=terrain2_cache_inputs_;
 terrain2_cache_inputs_=inputs;
 // Inputs that never settle would keep every level empty (all per pixel,
 // plus a full bake budget each frame): named once a map.
 terrain2_cache_unsettled_=changed?terrain2_cache_unsettled_+1:0;
 if(terrain2_cache_unsettled_==120&&!terrain2_cache_unsettled_logged_){
  terrain2_cache_unsettled_logged_=true;
  bridge_append_probe_event("terrain2_cache_unsettled","terrain 2.0 inputs changed on 120 frames in a row; the cache stays empty and terrain 2.0 per pixel");
 }

 // Option terrain2_cache_ahead: the fine windows sit ahead of the eye. A
 // level is read as the coarser of a pixel's two levels by pixels up to the
 // view depth where the footprint is one of its texels, z = pixels / density
 // metres. Seen from above those pixels fill the view's wedge: forward up
 // to z, sideways up to z times the half width over pixels (1.48 at 16:9
 // and this field of view). Around the eye that wedge reaches 1.75 z along
 // a world axis when the view is diagonal to the grid, past the window's
 // half side of 1.5 z, and the pixels at the screen's sides ran per pixel
 // (probe: "coarser level outside its window", 21 to 35 thousand pixels a
 // save at 2560x1600). Around a point half of z ahead of the eye it reaches
 // 1.48 z at most. In texels of any level that offset is half of pixels
 // along the view's horizontal direction (shorter as the view turns down).
 double ahead[2]{0,0};
 if(configured_integer(config,"terrain2_cache_ahead",0)>0){
  std::array<double,3> a{},b{};
  if(unproject(0,0,.1,a)&&unproject(0,0,.9,b)){
   const double v[3]{b[0]-a[0],b[1]-a[1],b[2]-a[2]},length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
   if(length>1e-9){ahead[0]=.5*pixels*v[0]/length;ahead[1]=.5*pixels*v[1]/length;}
  }
 }
 // Levels: windows, validity, pending rectangles.
 for(unsigned l=0;l<kTerrain2CacheLevels;++l){
  auto& level=terrain2_cache_levels_[l];
  if(l<first||l>last){level=Terrain2CacheLevelState{};continue;}
  const float d=level_density(l);
  const bool fine=fine_level(l);
  const int slice=fine?int(l%unsigned(terrain2_cache_slices_)):-1;
  if(!level.active||level.fine!=fine||level.slice!=slice){level=Terrain2CacheLevelState{};level.active=true;level.fine=fine;level.slice=slice;}
  level.bakeable={texel_lo(wx0,d),texel_lo(wy0,d),texel_hi(wx1,d),texel_hi(wy1,d)};
  Terrain2CacheRect window;
  if(fine){
   // Centred on the eye (option terrain2_cache_ahead: ahead of it, below),
   // moved in whole steps (a step of hysteresis).
   const int wantx=int(std::lround(eye[0]*d+ahead[0]))-side/2,wanty=int(std::lround(eye[1]*d+ahead[1]))-side/2;
   if(level.window.empty())window={wantx-terrain2_cache_mod(wantx,kTerrain2CacheStep),wanty-terrain2_cache_mod(wanty,kTerrain2CacheStep),0,0};
   else{
    window=level.window;
    const auto follow=[&](int& lo,int want){
     const int delta=want-lo;
     if(delta>=kTerrain2CacheStep)lo+=delta/kTerrain2CacheStep*kTerrain2CacheStep;
     else if(delta<=-kTerrain2CacheStep)lo-=(-delta)/kTerrain2CacheStep*kTerrain2CacheStep;
    };
    follow(window.x0,wantx);follow(window.y0,wanty);
   }
   window.x1=window.x0+side;window.y1=window.y0+side;
  }else{
   window=level.bakeable;
  }
  // Its place in the coarse atlas.
  if(!fine&&(level.atlas[0]!=coarse_at[l][0]||level.atlas[1]!=coarse_at[l][1])){
   level.atlas[0]=coarse_at[l][0];level.atlas[1]=coarse_at[l][1];level.valid={};level.pending.clear();level.window={};
  }
  const bool moved=window.x0!=level.window.x0||window.y0!=level.window.y0||window.x1!=level.window.x1||window.y1!=level.window.y1;
  if(changed||moved){
   if(changed)level.valid={};
   level.valid=terrain2_cache_intersect(level.valid,window);
   level.window=window;
   level.pending.clear();
   terrain2_cache_subtract(terrain2_cache_intersect(window,level.bakeable),level.valid,[&](const Terrain2CacheRect& r){level.pending.push_back(r);});
   if(level.pending.empty())level.valid=window;
  }
 }

 // Bake: coarse levels first, within the texel budget.
 long long budget=std::max(256,configured_integer(config,"terrain2_cache_bake",4096))*1024ll;
 bool any=false;
 for(unsigned l=last+1;l-->first;)if(!terrain2_cache_levels_[l].pending.empty())any=true;
 if(any){
  transition_profile_pass("terrain2_cache");
  // The bake within the budget; false from a rectangle (D3D12: its pipeline
  // still compiling, or dropped) stops this frame's bake, the rest stays pending.
  const auto bake_all=[&]{
  long long baked=0;bool stopped=false;
  for(unsigned l=last+1;l-->first&&budget>0&&!stopped;){
   auto& level=terrain2_cache_levels_[l];
   while(!level.pending.empty()&&budget>0){
    Terrain2CacheRect& r=level.pending.back();
    const long long width=r.x1-r.x0;
    if(r.area()<=budget){
     const Terrain2CacheRect whole=r;
     if(!bake_terrain2_cache_rect(l,whole)){stopped=true;break;}
     level.pending.pop_back();budget-=whole.area();baked+=whole.area();
    }else{
     const int rows=int(std::max(1ll,budget/width));
     const Terrain2CacheRect band{r.x0,r.y0,r.x1,r.y0+rows};
     if(!bake_terrain2_cache_rect(l,band)){stopped=true;break;}
     r.y0+=rows;budget-=band.area();baked+=band.area();
    }
   }
   if(level.pending.empty())level.valid=level.window;
  }
  // A fill (map load, inputs changed, a new level): its texels and frames, once complete.
  terrain2_cache_fill_texels_+=std::uint64_t(baked);++terrain2_cache_fill_frames_;
  bool complete=true;for(unsigned l=first;l<=last;++l)complete=complete&&terrain2_cache_levels_[l].pending.empty();
  if(complete){
   if(terrain2_cache_fill_texels_>=std::uint64_t(side)*side){
    char text[160];std::snprintf(text,sizeof(text),"texels=%llu frames=%llu levels=%u..%u",static_cast<unsigned long long>(terrain2_cache_fill_texels_),
     static_cast<unsigned long long>(terrain2_cache_fill_frames_),first,last);
    bridge_append_probe_event("terrain2_cache_filled",text);
   }
   terrain2_cache_fill_texels_=terrain2_cache_fill_frames_=0;
  }
  };
  if(d3d12_)bake_all();
  else{
  BakeStateD3D11 state;save_bake_state(state);
  ID3D11ShaderResourceView* none[3]{};context_->PSSetShaderResources(116,3,none); // the targets
  context_->PSSetShaderResources(78,2,none); // the S3 height targets
  context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(terrain2_cache_vs_.Get(),nullptr,0);context_->PSSetShader(terrain2_cache_ps_.Get(),nullptr,0);
  context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);context_->GSSetShader(nullptr,nullptr,0);
  context_->RSSetState(terrain2_cache_raster_.Get());context_->OMSetBlendState(nullptr,nullptr,0xffffffffu);context_->OMSetDepthStencilState(nullptr,0);
  {ID3D11Buffer* b=terrain2_cache_bake_constants_.Get();context_->PSSetConstantBuffers(12,1,&b);}
  context_->PSSetShaderResources(127,1,native_terrain_frame_srv_.GetAddressOf());
  // The noise lattice set_native_terrain_frame binds for every native pixel stage (unchanged).
  {ID3D11ShaderResourceView* noise=terrain_noise_srv_.Get();ID3D11SamplerState* sampler=terrain_noise_sampler_.Get();
   context_->PSSetShaderResources(PointShadowTextureLayout::terrain_noise_srv,1,&noise);context_->PSSetSamplers(12,1,&sampler);}

  bake_all();

  restore_bake_state(state);
  }
  end_profile_pass();
 }

 // The pixel stage's params.
 float params[kTerrain2CacheParamCount][4]{};
 params[0][0]=float(mode);params[0][1]=float(side);params[0][2]=float(first);params[0][3]=float(last);
 params[1][0]=configured_floating_point(config,"terrain2_cache_bias",0.f);params[1][1]=float(side)/16.f;params[1][2]=2.5f; // filter margin: the Catmull-Rom read reaches 2 texels
 params[1][3]=kTerrain2CacheHeightFormat==DXGI_FORMAT_R8G8_UNORM?1.f:0.f; // the bed flag is stored: dry banks may read the cache
 for(unsigned l=first;l<=last;++l){
  const auto& level=terrain2_cache_levels_[l];
  float* p=params[2+4*l];
  const int fx=level.fine?terrain2_cache_floor_div(level.window.x0,side)*side:level.window.x0;
  const int fy=level.fine?terrain2_cache_floor_div(level.window.y0,side)*side:level.window.y0;
  if(level.valid.empty()){p[0]=p[1]=1;p[2]=p[3]=0;}
  else{p[0]=float(level.valid.x0-fx);p[1]=float(level.valid.y0-fy);p[2]=float(level.valid.x1-fx);p[3]=float(level.valid.y1-fy);}
  p[4]=float(level.window.x0-fx);p[5]=float(level.window.y0-fy);p[6]=float(level.window.x1-fx);p[7]=float(level.window.y1-fy);
  p[8]=float(fx);p[9]=float(fy);p[10]=level_density(l);p[11]=level.fine?float(level.slice):-1.f;
  if(!level.fine&&coarse_w&&coarse_h){p[12]=float(level.atlas[0]);p[13]=float(level.atlas[1]);p[14]=1.f/coarse_w;p[15]=1.f/coarse_h;}
 }
 if(d3d12_){
  // Stream order: the frame's terrain pixels read what this writes.
  terrain2_cache_on_=SUCCEEDED(d3d12_->write_buffer(*terrain2_cache_params12_,0,params,sizeof(params)));
  return;
 }
 D3D11_MAPPED_SUBRESOURCE mapped{};
 if(SUCCEEDED(context_->Map(terrain2_cache_params_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))){
  std::memcpy(mapped.pData,params,sizeof(params));context_->Unmap(terrain2_cache_params_.Get(),0);
  terrain2_cache_on_=true;
 }
 bind_terrain2_cache();
}catch(...){release_terrain2_cache();bind_terrain2_cache();}

#endif // !EID3D11_SHADER_EMBED
}  // namespace owned
