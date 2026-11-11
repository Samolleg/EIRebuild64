// Native packets on the x64 host's D3D12 executor (renderer_backend=d3d12):
// the native queue's own records (layout, batches, work, arguments, instance
// rows, b0 constants) drawn through Source/d3d12_frame.h instead of the D3D11
// context. Interface (SemanticKind::ui) batches draw; world batches are
// omitted with the marker until M2 moves the world passes to bindless
// indices. Every function here runs only with Renderer::d3d12_ set (null in
// the x86 DLL and on the D3D11 host).
//
// Binding: the shared native HLSL keeps its D3D11 registers; each dispatch
// and draw sets its slots on the executor, which copies them into ring
// tables (a thin per-flush translation, not a D3D11 emulation layer).
#include "renderer.h"
#include "gamma_shader_programs.h"
#include "embedded_figure_shader.h"
#include "renderer_internal.h"
#include "world_texture_sampler.h"
#include "contact_blend.h"
#include "terrain2.h"
#include "texture_mips.h"
#include "owned_asset_loader.h"
#include "neighbour_maps.h"
#include "neighbour_piece.h"
#include "ground_cover.h"
#include "ground_cover_neighbour.h"
#include "native_instance_flags.h"
#include "owned_resource_runtime.h"
#include "retained_static_scene.h"
#include "shadow_foliage.h"
#include "ambient_occlusion.h"
#include "world_fog.h"
#include "dlss_frame.h"
#include "x64_remote_link.h"
#include <future>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <chrono>
#include <thread>

namespace owned {

// D3D11 GenerateMips on D3D12 (D3D12Frame::generate_mips): each level the 2x2
// box of the one above, clamped at odd edges. Only the x64 host's DXIL table
// holds it (shader_embed --dxil-sources); the D3D11 table never sees it.
namespace {
constexpr char kD3D12MipShader[] =
"Texture2D<float4> mipSource:register(t0);RWTexture2D<float4> mipTarget:register(u0);\n"
"[numthreads(8,8,1)]void mipBoxCS(uint3 id:SV_DispatchThreadID){\n"
" uint w,h,sw,sh;mipTarget.GetDimensions(w,h);mipSource.GetDimensions(sw,sh);if(id.x>=w||id.y>=h)return;\n"
" uint2 a=min(id.xy*2,uint2(sw-1,sh-1)),b=min(id.xy*2+1,uint2(sw-1,sh-1));\n"
" mipTarget[id.xy]=(mipSource[a]+mipSource[uint2(b.x,a.y)]+mipSource[uint2(a.x,b.y)]+mipSource[b])*.25;}\n";
const ShaderProgram kD3D12MipProgram{{kD3D12MipShader, sizeof(kD3D12MipShader) - 1}, nullptr, nullptr, "mipBoxCS", "cs_5_0",
                                     D3DCOMPILE_OPTIMIZATION_LEVEL3};
// The same for one slice of an array (its views hold that slice alone).
constexpr char kD3D12MipArrayShader[] =
"Texture2DArray<float4> mipSource:register(t0);RWTexture2DArray<float4> mipTarget:register(u0);\n"
"[numthreads(8,8,1)]void mipBoxArrayCS(uint3 id:SV_DispatchThreadID){\n"
" uint w,h,n,sw,sh,sn;mipTarget.GetDimensions(w,h,n);mipSource.GetDimensions(sw,sh,sn);if(id.x>=w||id.y>=h)return;\n"
" uint2 a=min(id.xy*2,uint2(sw-1,sh-1)),b=min(id.xy*2+1,uint2(sw-1,sh-1));\n"
" mipTarget[uint3(id.xy,0)]=(mipSource[uint3(a,0)]+mipSource[uint3(b.x,a.y,0)]+mipSource[uint3(a.x,b.y,0)]+mipSource[uint3(b,0)])*.25;}\n";
const ShaderProgram kD3D12MipArrayProgram{{kD3D12MipArrayShader, sizeof(kD3D12MipArrayShader) - 1}, nullptr, nullptr, "mipBoxArrayCS",
                                          "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3};
// The same for every slice of the views at once, dispatch z a slice
// (generate_mips with a slice count: the crown impostor atlas's new slices).
constexpr char kD3D12MipArrayRangeShader[] =
"Texture2DArray<float4> mipSource:register(t0);RWTexture2DArray<float4> mipTarget:register(u0);\n"
"[numthreads(8,8,1)]void mipBoxArrayRangeCS(uint3 id:SV_DispatchThreadID){\n"
" uint w,h,n,sw,sh,sn;mipTarget.GetDimensions(w,h,n);mipSource.GetDimensions(sw,sh,sn);if(id.x>=w||id.y>=h||id.z>=n)return;\n"
" uint2 a=min(id.xy*2,uint2(sw-1,sh-1)),b=min(id.xy*2+1,uint2(sw-1,sh-1));\n"
" mipTarget[id]=(mipSource[uint3(a,id.z)]+mipSource[uint3(b.x,a.y,id.z)]+mipSource[uint3(a.x,b.y,id.z)]+mipSource[uint3(b,id.z)])*.25;}\n";
const ShaderProgram kD3D12MipArrayRangeProgram{{kD3D12MipArrayRangeShader, sizeof(kD3D12MipArrayRangeShader) - 1}, nullptr, nullptr,
                                               "mipBoxArrayRangeCS", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3};
D3D12NativeState post_state(bool additive,bool over){
 D3D12NativeState state;
 state.raster.FillMode=D3D11_FILL_SOLID;state.raster.CullMode=D3D11_CULL_BACK;state.raster.DepthClipEnable=TRUE;
 auto& blend=state.custom_blend;
 if(additive){ // colour added, the target alpha kept
  blend.custom=1;blend.source=blend.destination=D3D11_BLEND_ONE;
  blend.source_alpha=D3D11_BLEND_ZERO;blend.destination_alpha=D3D11_BLEND_ONE;
  blend.write_mask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
 }else if(over){ // lerp(target, source, source alpha), the target alpha kept
  blend.custom=1;blend.source=D3D11_BLEND_SRC_ALPHA;blend.destination=D3D11_BLEND_INV_SRC_ALPHA;
  blend.source_alpha=D3D11_BLEND_ZERO;blend.destination_alpha=D3D11_BLEND_ONE;
  blend.write_mask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
 }
 return state;
}
D3D11_SAMPLER_DESC post_linear_clamp(){
 D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
 s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=0;
 return s;
}
}  // namespace

void append_d3d12_shader_programs(std::vector<ShaderProgram>& out) {
 out.push_back(kD3D12MipProgram);
 out.push_back(kD3D12MipArrayProgram);
 out.push_back(kD3D12MipArrayRangeProgram);
 append_gamma_shader_programs(out);
 out.push_back(embedded_figure_layer_program(false));
 out.push_back(embedded_figure_layer_program(true));
 append_terrain2_cache_d3d12_programs(out);
 append_native_off_material_programs(out);
}
const ShaderProgram& d3d12_mip_program() noexcept { return kD3D12MipProgram; }
const ShaderProgram& d3d12_mip_array_program() noexcept { return kD3D12MipArrayProgram; }
const ShaderProgram& d3d12_mip_array_range_program() noexcept { return kD3D12MipArrayRangeProgram; }

// The native programs and the first geometry page on the D3D12 backend.
#if !defined(EID3D11_SHADER_EMBED)
#ifdef EID3D11_EMBEDDED
HRESULT Renderer::compose_embedded_figure_texture_d3d12(const D3D12TextureRef& primary,
 const D3D12TextureRef& secondary,D3D12TextureRef& combined){
 combined.reset();
 if(!d3d12_||!primary||!secondary)return E_INVALIDARG;
 UINT width=0,height=0,mips=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*primary,width,height,mips,format)||!width||!height)return E_INVALIDARG;
 UINT sw=0,sh=0,sm=0;DXGI_FORMAT sf=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*secondary,sw,sh,sm,sf))return E_INVALIDARG;
 // RGBA keeps sampled channel values and permits typed UAV mip writes.
 D3D12TextureRef target,output;
 HRESULT hr=d3d12_->create_target(width,height,target,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_UNKNOWN);
 if(FAILED(hr))return hr;
 UINT levels=1;for(UINT size=(std::max)(width,height);size>1;size>>=1)++levels;
 hr=d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R8G8B8A8_UNORM,output,levels);
 if(FAILED(hr))return hr;
 D3D11_SAMPLER_DESC sampling{};sampling.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
 sampling.MaxLOD=D3D11_FLOAT32_MAX;
 D3D12NativeState state;
 state.raster.FillMode=D3D11_FILL_SOLID;state.raster.CullMode=D3D11_CULL_BACK;state.raster.DepthClipEnable=TRUE;
 d3d12_->clear_slots();d3d12_->set_resource(0,primary.get());d3d12_->set_resource(1,secondary.get());
 d3d12_->set_sampler(0,sampling);d3d12_->set_target(target.get());
 hr=d3d12_->draw(embedded_figure_layer_program(false),embedded_figure_layer_program(true),state,nullptr,3);
 d3d12_->clear_slots();
 if(world_d3d12_bound_)bind_world_targets_d3d12();else d3d12_->set_target(nullptr);
 // A pending pipeline must never publish an unwritten composite.
 if(hr!=S_OK)return hr;
 hr=d3d12_->copy_texture(*output,0,*target,0);
 if(hr!=S_OK)return hr;
 hr=d3d12_->generate_mips(*output,d3d12_mip_program());
 if(hr==S_OK)combined=std::move(output);
 return hr;
}
#endif

HRESULT Renderer::native_d3d12_ensure(){
 if(native12_.ready)return S_OK;
 if(native12_.failed||!d3d12_)return E_FAIL;
 try{
  native12_.programs.clear();append_native_shader_programs(native12_.programs);
  append_native_off_material_programs(native12_.programs); // after the general list (programs[n] stay)
  native12_.tessellation.clear();append_terrain_tessellation_shader_programs(native12_.tessellation);
  if(native12_.programs.size()<3){native12_.failed=true;return E_FAIL;}
  if(native_geometry_pages_.empty()){
   NativeGeometryPage page;page.capacity=16;page.high_water=16;page.free.reserve(1);
   const HRESULT hr=d3d12_->create_buffer(16,0,0,page.d3d12);
   if(FAILED(hr)){native12_.failed=true;return hr;}
   native_geometry_pages_.push_back(std::move(page));
   native_meshes_.clear();native_geometry_live_bytes_=0;native_mesh_resident_bytes_=0;
  }
 }catch(const std::bad_alloc&){native12_.failed=true;return E_OUTOFMEMORY;}
 native12_.ready=true;
 return S_OK;
}

// A frame buffer of at least `bytes` (grown by demand; contents not kept).
HRESULT Renderer::native_d3d12_buffer(D3D12BufferRef& buffer,UINT64 bytes,UINT stride,UINT flags){
 bytes=(std::max<UINT64>)(bytes,16);
 if(buffer&&d3d12_->buffer_bytes(*buffer)>=bytes)return S_OK;
 const UINT64 current=buffer?d3d12_->buffer_bytes(*buffer):0;
 UINT64 target=(std::max<UINT64>)({bytes,current*2,64u*1024u});
 if(stride)target=(target+stride-1)/stride*stride;
 D3D12BufferRef created;
 HRESULT hr=d3d12_->create_buffer(target,stride,flags,created);
 if(FAILED(hr)&&target!=bytes)hr=d3d12_->create_buffer(bytes,stride,flags,created);
 if(FAILED(hr))return hr;
 buffer=std::move(created);
 return S_OK;
}

HRESULT Renderer::native_d3d12_persistent(D3D12BufferRef& buffer,UINT64 bytes,UINT stride,UINT64 keep){
 bytes=(std::max<UINT64>)(bytes,16);
 if(buffer&&d3d12_->buffer_bytes(*buffer)>=bytes)return S_OK;
 D3D12BufferRef created;
 HRESULT hr=d3d12_->create_buffer(bytes,stride,kD3D12BufferUnordered,created);
 if(FAILED(hr))return hr;
 if(buffer&&keep&&FAILED(hr=d3d12_->copy_buffer(*created,*buffer,(std::min)(keep,d3d12_->buffer_bytes(*buffer)))))return hr;
 buffer=std::move(created);
 return S_OK;
}

HRESULT Renderer::terrain_texture_d3d12(SurfaceImage& image,const TerrainTexturePage* page,D3D12TextureRef& output){
 output.reset();
 const bool bc1=(image.format.dwFlags&DDPF_FOURCC)&&image.format.dwFourCC==MAKEFOURCC('D','X','T','1');
 auto& entry=terrain_textures_d3d12_[image.id];
 if(entry.texture&&entry.generation==image.generation){output=entry.texture;return S_OK;}
 // Every terrain page texture is recorded (the contact atlas copies find
 // them by surface id and generation).
 const auto record=[&](const D3D12TextureRef& texture,const char* kind)->HRESULT{
  if(!texture)return E_FAIL;
  entry.generation=image.generation;entry.texture=texture;output=texture;
  d3d12_->count(kind);
  return S_OK;
 };
 if(!bc1){ // not a terrain page format: the surface's own texture (one level; D3D11 generates mips here)
  const HRESULT hr=upload_d3d12(image,nullptr);if(FAILED(hr))return hr;
  return record(image.d3d12,"terrain pages: not BC1 (one level)");
 }
 // A queued command carries a pixel-free snapshot (surface_snapshot): the
 // host table's image of the same generation holds the bytes (this backend
 // never releases them). Without bytes the surface keeps its own single
 // level, as the D3D11 slice does without the shipped chain.
 const SurfaceImage* source=&image;
 if(image.pixels.empty()){
  const auto found=core_surfaces_.find(image.id);
  if(found!=core_surfaces_.end()&&found->second&&found->second->generation==image.generation&&!found->second->pixels.empty())
   source=found->second.get();
 }
 const UINT block_rows=(std::max)(1u,(image.height+3u)/4u);
 if(source->pixels.empty()||source->pitch<=0||std::uint64_t(source->pitch)*block_rows>source->pixels.size()){
  // upload_d3d12 also checks executor ownership before its generation hit.
  const HRESULT hr=upload_d3d12(image,nullptr);if(FAILED(hr))return hr;
  return record(image.d3d12,"terrain pages: BC1 without bytes (one level)");
 }
 UINT mip_count=1;
 bool authored=false;
 if(page){
  prepare_terrain_pages_d3d12(*page); // the whole terrain's pages at its first miss, on workers
  ResourceFailure failure;
  authored=page->ensure_upload_bytes(failure)&&!page->mips.empty()&&
   authored_bc1_chain_matches(page->mips.data(),page->mip_count,image.width,image.height,source->pixels.data(),
    source->pixels.size(),static_cast<std::size_t>(source->pitch));
  if(authored)mip_count=page->mip_count;
 }
 // Its prepared texture: the shipped chain, whose level 0 the match above
 // found equal to the surface's bytes; only the copies are recorded here.
 if(authored&&terrain_pages_d3d12_.terrain){
  const auto& pages=terrain_pages_d3d12_.terrain->pages;
  if(page>=pages.data()&&page<pages.data()+pages.size()){
   auto prepared=std::move(terrain_pages_d3d12_.uploads[std::size_t(page-pages.data())]);
   D3D12TextureRef texture;
   if(prepared&&SUCCEEDED(d3d12_->finish_texture(*prepared,texture))){
    page->release_upload_bytes();
    return record(texture,"terrain pages: BC1 with the shipped chain (prepared)");
   }
  }
 }
 D3D12TextureRef texture;
 HRESULT hr=d3d12_->create_texture(image.width,image.height,DXGI_FORMAT_BC1_UNORM,texture,mip_count);
 if(SUCCEEDED(hr))hr=d3d12_->write_texture(*texture,RECT{0,0,LONG(image.width),LONG(image.height)},source->pixels.data(),UINT(source->pitch),0);
 for(UINT level=1;SUCCEEDED(hr)&&level<mip_count;++level){
  const UINT w=(std::max)(1u,image.width>>level),h=(std::max)(1u,image.height>>level);
  hr=d3d12_->write_texture(*texture,RECT{0,0,LONG(w),LONG(h)},page->mips[level].data,(std::max)(1u,(w+3u)/4u)*8u,level);
 }
 if(authored)page->release_upload_bytes();
 if(FAILED(hr))return hr;
 return record(texture,authored?"terrain pages: BC1 with the shipped chain":"terrain pages: BC1 one level (chain absent or different)");
}

// Every page of the active terrain prepared at once at its first miss: the
// pages' archive reads and parses split over workers, then one heap and one
// upload buffer for all of them (prepare_texture_batch; a texture and an
// upload buffer per page cost 624 ms of creation over 54 pages). Each worker
// reads and keeps the bytes of its own pages only (the render thread waits,
// so no page is touched twice); the match against the game surface stays at
// each page's miss.
void Renderer::prepare_terrain_pages_d3d12(const TerrainTexturePage& page){
 const auto contains=[&page](const TerrainCpuAsset& terrain){
  return !terrain.pages.empty()&&&page>=terrain.pages.data()&&&page<terrain.pages.data()+terrain.pages.size();};
 if(terrain_pages_d3d12_.terrain&&contains(*terrain_pages_d3d12_.terrain))return; // prepared already
 const auto source=retained_static_terrain_source_snapshot();
 if(!source.terrain||!contains(*source.terrain))return; // not the active terrain's page: one at a time as before
 const auto preparer=d3d12_->preparer();
 if(!preparer)return;
 terrain_pages_d3d12_={};
 try{
  const auto& terrain=*source.terrain;
  const std::size_t count=terrain.pages.size();
  // Per page: its levels as the batch reads them (empty: not prepared).
  struct Levels {std::vector<const void*> data;std::vector<UINT> pitches;UINT width=0,height=0;};
  std::vector<Levels> pages(count);
  const unsigned workers=(std::max)(1u,(std::min)(8u,std::thread::hardware_concurrency()/2));
  const auto read=[&](unsigned first) noexcept {
   for(std::size_t index=first;index<count;index+=workers)try{
    const TerrainTexturePage& p=terrain.pages[index];
    ResourceFailure failure;
    const bool loaded=p.store&&p.ensure_upload_bytes(failure);
    if(!loaded||p.mips.empty()||p.mip_count!=p.mips.size())continue;
    const UINT width=p.mips[0].width,height=p.mips[0].height;
    Levels levels;levels.width=width;levels.height=height;
    levels.data.resize(p.mip_count);levels.pitches.resize(p.mip_count);
    bool usable=width&&height;
    for(UINT level=0;usable&&level<p.mip_count;++level){
     const UINT w=(std::max)(1u,width>>level),h=(std::max)(1u,height>>level);
     const UINT row=(std::max)(1u,(w+3u)/4u)*8u,rows=(std::max)(1u,(h+3u)/4u);
     const auto& mip=p.mips[level];
     usable=mip.data&&mip.width==w&&mip.height==h&&mip.bytes>=std::uint64_t(row)*rows;
     levels.data[level]=mip.data;levels.pitches[level]=row;
    }
    if(usable)pages[index]=std::move(levels);
   }catch(...){}
  };
  std::vector<std::thread> threads;threads.reserve(workers);
  unsigned started=1;
  try{for(;started<workers;++started)threads.emplace_back(read,started);}catch(...){}
  read(0);
  for(unsigned first=started;first<workers;++first)read(first); // threads that could not start
  for(auto& thread:threads)thread.join();
  std::vector<D3D12Preparer::TextureLevels> batch(count);
  for(std::size_t index=0;index<count;++index)if(!pages[index].data.empty()){
   auto& entry=batch[index];
   entry.width=pages[index].width;entry.height=pages[index].height;entry.mips=static_cast<UINT>(pages[index].data.size());
   entry.format=DXGI_FORMAT_BC1_UNORM;entry.levels=pages[index].data.data();entry.pitches=pages[index].pitches.data();
  }
  std::vector<D3D12PreparedUploadRef> uploads;
  (void)preparer->prepare_texture_batch(batch.data(),count,uploads);
  uploads.resize(count);
  terrain_pages_d3d12_.terrain=source.terrain;terrain_pages_d3d12_.uploads=std::move(uploads);
 }catch(...){terrain_pages_d3d12_={};}
}

HRESULT Renderer::copy_contact_atlas_d3d12(D3D12Texture& source,UINT material,ContactBlendMaps& maps){
 UINT width=0,height=0,mips=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(source,width,height,mips,format))return E_FAIL;
 if(!maps.atlas12){
  UINT skip=0;
  while(((width>>skip)>contact_blend_atlas_side||(height>>skip)>contact_blend_atlas_side)&&skip+1<mips)++skip;
  UINT slices=0;for(std::uint32_t m=0;m<contact_blend_max_materials;++m)if((maps.pending>>m)&1u)slices=m+1;
  const HRESULT hr=d3d12_->create_texture_array((std::max)(1u,width>>skip),(std::max)(1u,height>>skip),format,mips-skip,(std::max)(1u,slices),maps.atlas12);
  if(FAILED(hr))return hr;
  maps.width=(std::max)(1u,width>>skip);maps.height=(std::max)(1u,height>>skip);maps.mips=mips-skip;maps.format=format;
  maps.slices=(std::max)(1u,slices);
 }
 UINT skip=0;
 while(skip<mips&&(std::max)(1u,width>>skip)>maps.width)++skip;
 maps.pending&=~(std::uint64_t{1}<<material);
 if(format!=maps.format||skip+maps.mips>mips||(std::max)(1u,width>>skip)!=maps.width||(std::max)(1u,height>>skip)!=maps.height)return S_FALSE;
 for(UINT mip=0;mip<maps.mips;++mip){
  const HRESULT hr=d3d12_->copy_texture(*maps.atlas12,mip+material*maps.mips,source,mip+skip);
  if(FAILED(hr))return hr;
 }
 maps.ready|=std::uint64_t{1}<<material;
 return S_OK;
}

// bake_terrain2_heights on D3D12: the same six programs and passes per
// pending material cell (kTerrain2HeightShader), the scratch planes and
// statistics as structured unordered buffers, the heights an R8 storage
// array with the atlas's size, levels and slices (cleared to .5, the flat
// height), its levels made once a batch is in. A dispatch still compiling
// (S_FALSE) or dropped stops the batch before that cell's write; the map
// retries next frame. The D3D12 backend does not wait for the GPU here, so
// the log carries no bake time.
void Renderer::bake_terrain2_heights_d3d12(Terrain2Map& map,const ContactBlendMaps& contact,const char* name) noexcept try{
 map.dirty=false;map.seen_ready=contact.ready;
 if(map.failed||!contact.atlas12)return;
 std::vector<const Terrain2BakeCell*> pending;
 for(const auto& c:map.bake_cells)
  if(c.slice<64&&((contact.ready>>c.slice)&1u)&&!map.baked.count(c.key))pending.push_back(&c);
 if(pending.empty()&&!map.mips12)return;
 HRESULT hr=S_OK;
 const auto fail=[&](const char* what){
  map.failed=true;
  char text[224];std::snprintf(text,sizeof(text),"map=%s %s failed (0x%08lx, D3D12); terrain 2.0 blends with flat material heights",name,what,static_cast<unsigned long>(hr));
  bridge_append_probe_event("terrain2_height_omitted",text);
 };
 if(terrain2_bake12_programs_.empty())append_terrain2_shader_programs(terrain2_bake12_programs_);
 if(terrain2_bake12_programs_.size()<6){hr=E_FAIL;return fail("height bake programs");}
 const UINT width=contact.width,height=contact.height;
 if(width%128u||height!=width){hr=E_INVALIDARG;return fail("height bake atlas layout");}
 if(!map.height12){
  if(!d3d12_->storage_supported(DXGI_FORMAT_R8_UNORM)){hr=E_NOTIMPL;return fail("height array format");}
  if(FAILED(hr=d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R8_UNORM,map.height12,(std::max)(1u,contact.mips),(std::max)(1u,contact.slices)))){
   map.height12.reset();return fail("height array");
  }
  const float flat[4]{.5f,.5f,.5f,.5f};d3d12_->clear_texture(*map.height12,flat);
  map.mips12=true;
 }
 // Work planes (4 complex 256x256) and the statistics, for this batch only.
 D3D12BufferRef planes,stats;
 if(!pending.empty()&&(FAILED(hr=d3d12_->create_buffer(4ull*65536u*8u,8,kD3D12BufferUnordered,planes))||
                       FAILED(hr=d3d12_->create_buffer(8u*4u,4,kD3D12BufferUnordered,stats))))
  return fail("height bake scratch");
 D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
 struct Constants{float lo[2],size[2],dir[2],fold,aw;std::uint32_t n,slice,src,dst;float sigma,weight;std::uint32_t inverse,axis;
  std::uint32_t origin[2],side,mode;float amp,clip,pad[2];}k{};
 static_assert(sizeof(Constants)==96);
 bool stopped=false;
 const auto run=[&](unsigned shader,UINT groups_x,UINT groups_y){
  if(stopped)return;
  d3d12_->clear_slots();
  d3d12_->set_constants(0,&k,sizeof(k));
  d3d12_->set_resource(0,contact.atlas12.get());d3d12_->set_sampler(0,sampler);
  d3d12_->set_unordered(0,planes.get());d3d12_->set_unordered(1,stats.get());d3d12_->set_unordered(2,map.height12.get());
  const HRESULT done=d3d12_->dispatch(terrain2_bake12_programs_[shader],groups_x,groups_y,1);
  if(done!=S_OK){stopped=true;if(FAILED(done))hr=done;}
 };
 enum{Load,Fft,Gauss,Combine,Reduce,Write};
 const auto fft=[&](std::uint32_t plane,bool inverse){
  k.src=plane;k.inverse=inverse;for(std::uint32_t axis=0;axis<2;++axis){k.axis=axis;run(Fft,256,1);}
 };
 const auto gauss=[&](std::uint32_t from,std::uint32_t to,float sigma){k.src=from;k.dst=to;k.sigma=sigma;run(Gauss,16,16);};
 const auto combine=[&](std::uint32_t mode){k.mode=mode;run(Combine,16,16);};
 const auto reduce=[&](std::uint32_t mode){k.mode=mode;run(Reduce,1,1);};
 const float aw=float(width),margin=tile_blend_margin_*.125f,size=.125f-2*margin;
 const UINT slot=width/8u;
 std::size_t baked=0;
 for(const Terrain2BakeCell* c:pending){
  const Terrain2BakeStyle style=terrain2_bake_style(c->style);
  k=Constants{};k.aw=aw;k.slice=c->slice;k.amp=style.amp;k.clip=style.dc;
  k.lo[0]=(c->pos%8u)*.125f+margin;k.lo[1]=1-(c->pos/8u+1)*.125f+margin;k.size[0]=k.size[1]=size;k.dir[0]=k.dir[1]=1;
  if(c->folded){
   const float cx=(c->quadrant==1||c->quadrant==2)?1.f:0.f,cy=c->quadrant>=2?1.f:0.f;
   k.lo[0]+=cx*size;k.lo[1]+=cy*size;k.dir[0]=1-2*cx;k.dir[1]=1-2*cy;k.fold=size*.3f;
   k.n=std::max(2u,UINT(std::lround(2*k.fold*aw)));
  }else k.n=std::max(1u,UINT(std::lround(size*aw)));
  run(Load,16,16);
  fft(0,false);gauss(0,0,64);fft(0,true);combine(0);
  if(style.hp>0){combine(1);fft(0,false);gauss(0,0,style.hp);fft(0,true);combine(2);}
  if(style.dc>0){reduce(0);combine(3);}
  combine(1);fft(0,false);combine(4);
  const float scales[3]{1,4,12},weights[3]{style.w1,style.w4,style.w12};
  for(int s=0;s<3;++s){
   if(weights[s]<=0)continue;
   gauss(0,2,scales[s]);fft(2,true);combine(6);reduce(1);k.weight=weights[s];combine(5);
  }
  fft(1,false);combine(7);fft(2,true);reduce(2);
  k.side=slot;k.origin[0]=(c->pos%8u)*slot;k.origin[1]=width-(c->pos/8u+1)*slot;
  run(Write,(slot+15u)/16u,(slot+15u)/16u);
  if(stopped)break;
  map.baked.insert(c->key);++baked;map.mips12=true;
 }
 d3d12_->clear_slots();
 if(FAILED(hr))return fail("height bake dispatch");
 if(stopped){map.dirty=true;d3d12_->omit("terrain 2.0 material heights (pipeline not ready; retried)",false);}
 // The levels once nothing of the batch is outstanding (D3D11 GenerateMips).
 if(!stopped&&map.mips12){
  const HRESULT mips=d3d12_->generate_mips(*map.height12,d3d12_mip_array_range_program(),0,(std::max)(1u,contact.slices));
  if(FAILED(mips)){hr=mips;return fail("height levels");}
  if(mips==S_OK){map.mips12=false;++terrain2_cache_revision_;}else map.dirty=true;
 }
 if(baked){
  char text[224];std::snprintf(text,sizeof(text),"map=%s cells=%zu baked=%zu array=%ux%ux%u (%.1f MB) backend=D3D12",name,baked,map.baked.size(),
   width,height,(std::max)(1u,contact.slices),double(width)*height*(std::max)(1u,contact.slices)*4/3/1048576.0);
  bridge_append_probe_event("terrain2_heights",text);
  ++terrain2_cache_revision_; // the material cache bakes again with the new heights
 }
}catch(...){}

// The snow surface on D3D12 (update_snow_surface's copy): one dispatch into a
// 256 px RGBA8 storage texture, then its levels. A dispatch or downsample not
// ready (S_FALSE) retries next frame; the texture is bound once complete.
void Renderer::update_snow_surface_d3d12() noexcept try{
 std::uint64_t key=0;const Terrain2BakeCell* cell=snow_surface_cell(key);
 if(key==snow_surface_key_)return;
 if(!cell||!contact_maps_.atlas12||!terrain2_map_.height12){snow_surface_key_=key;snow_surface12_.reset();return;}
 HRESULT hr=S_OK;
 const auto fail=[&](const char* what){
  snow_surface_key_=key;snow_surface12_.reset();
  char text[160];std::snprintf(text,sizeof(text),"%s failed (0x%08lx, D3D12); snowed objects keep the drift relief",what,static_cast<unsigned long>(hr));
  bridge_append_probe_event("snow_surface_omitted",text);
 };
 if(snow_surface12_program_.empty())append_snow_surface_program(snow_surface12_program_);
 if(!d3d12_->storage_supported(DXGI_FORMAT_R8G8B8A8_UNORM)){hr=E_NOTIMPL;return fail("snow surface format");}
 D3D12TextureRef texture;
 UINT mips=1;for(UINT n=kSnowSurfaceSide;n>1;n>>=1)++mips;
 if(FAILED(hr=d3d12_->create_storage_texture(kSnowSurfaceSide,kSnowSurfaceSide,0,DXGI_FORMAT_R8G8B8A8_UNORM,texture,mips)))return fail("snow surface texture");
 const float margin=tile_blend_margin_*.125f,size=.125f-2*margin;
 struct{float lo[2],size[2],dir[2],fold,slice;}k{};
 k.lo[0]=(cell->pos%8u)*.125f+margin;k.lo[1]=1-(cell->pos/8u+1)*.125f+margin;k.size[0]=k.size[1]=size;k.dir[0]=k.dir[1]=1;k.slice=float(cell->slice);
 if(cell->folded){
  const float cx=(cell->quadrant==1||cell->quadrant==2)?1.f:0.f,cy=cell->quadrant>=2?1.f:0.f;
  k.lo[0]+=cx*size;k.lo[1]+=cy*size;k.dir[0]=1-2*cx;k.dir[1]=1-2*cy;k.fold=size*.3f;
 }
 D3D11_SAMPLER_DESC clamp{};clamp.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 clamp.AddressU=clamp.AddressV=clamp.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;clamp.MaxLOD=D3D11_FLOAT32_MAX;
 d3d12_->clear_slots();d3d12_->set_constants(0,&k,sizeof(k));
 d3d12_->set_resource(0,contact_maps_.atlas12.get());d3d12_->set_resource(1,terrain2_map_.height12.get());d3d12_->set_sampler(0,clamp);
 d3d12_->set_unordered(0,texture.get());
 hr=d3d12_->dispatch(snow_surface12_program_[0],kSnowSurfaceSide/16u,kSnowSurfaceSide/16u,1);
 d3d12_->clear_slots();
 if(FAILED(hr))return fail("snow surface dispatch");
 if(hr!=S_OK)return; // compiling: next frame
 hr=d3d12_->generate_mips(*texture,d3d12_mip_program());
 if(FAILED(hr))return fail("snow surface levels");
 if(hr!=S_OK)return;
 snow_surface_key_=key;snow_surface12_=std::move(texture);
 char text[96];std::snprintf(text,sizeof(text),"cell=%u slice=%u folded=%d side=%u backend=D3D12",cell->key,cell->slice,cell->folded?1:0,kSnowSurfaceSide);
 bridge_append_probe_event("snow_surface",text);
}catch(...){}

// The terrain 2.0 map table on D3D12: its entries are read by bindless
// index (record 3, terrain2_records_d3d12); a height bake in this flush
// (after the frame values went up) changes which heights are readable, so
// the values go up again when an index changed. Then the contact sampler
// terrain2Sampler reads (s9). Once a world flush.
void Renderer::bind_terrain2_d3d12() noexcept{
 if(terrain2_records_first_&&native_terrain_frame_d3d12_.size()>terrain2_records_first_&&
    terrain2_records_d3d12(native_terrain_frame_d3d12_.data()+terrain2_records_first_))
  (void)upload_native_terrain_frame_d3d12();
 D3D11_SAMPLER_DESC contact{};contact.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 contact.AddressU=contact.AddressV=contact.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;contact.MaxLOD=D3D11_FLOAT32_MAX;
 d3d12_->set_sampler(9,contact);
 // The material cache (bind_terrain2_cache's t116-t118 and s15, at
 // kTerrain2Cache*SlotD3D12); the params say off until a frame set them up.
 const bool cache=terrain2_cache_on_;
 d3d12_->set_resource(kTerrain2CacheCoarseSlotD3D12,cache?terrain2_cache_coarse12_.get():nullptr);
 d3d12_->set_resource(kTerrain2CacheParamsSlotD3D12,cache?terrain2_cache_params12_.get():nullptr);
 d3d12_->set_resource(kTerrain2CacheFineSlotD3D12,cache?terrain2_cache_fine12_.get():nullptr);
 d3d12_->set_resource(78,cache?terrain2_cache_height_fine12_.get():nullptr); // S3 height layer (terrain2_cache.h t78/t79)
 d3d12_->set_resource(79,cache?terrain2_cache_height_coarse12_.get():nullptr);
 D3D11_SAMPLER_DESC wrap{};wrap.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 wrap.AddressU=wrap.AddressV=wrap.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;wrap.MaxLOD=0;
 d3d12_->set_sampler(15,wrap);
}

void Renderer::add_neighbour_blend_maps_d3d12(const NeighbourPiece& upload,const std::array<float,16>& active_to_world,UINT contact_map) noexcept{
 const bool tile_blend=configured_integer(debug_overlay_.config(),"terrain_blend",1)>0;
 const bool cover=configured_integer(debug_overlay_.config(),"ground_cover",2)>0;
 if(!tile_blend&&!cover&&!configured_terrain_cliffs(debug_overlay_.config()))return;
 const auto report=[&](const char* text){
  char line[224];std::snprintf(line,sizeof(line),"map=%s blend_maps=%s",upload.selection.placement.mpr.c_str(),text);
  bridge_neighbour_diagnostic(line);
 };
 try{
  GcNeighbour n;n.contact_map=contact_map;n.name=upload.selection.placement.mpr;for(char& c:n.name)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);
  if(!ground_cover::neighbour_cover_origin(active_to_world,upload.selection.x,upload.selection.y,n.origin))return report("omitted reason=placement_not_translation");
  n.layout_origin={neighbour_layout_tile_origin(upload.selection.placement.x),neighbour_layout_tile_origin(upload.selection.placement.y)};
  const auto& inputs=upload.ground_cover;
  if(!inputs||!inputs->grids_valid)return report("omitted reason=empty_terrain");
  n.tiles_x=inputs->grids.tiles_x;n.tiles_y=inputs->grids.tiles_y;
  n.lowest=inputs->lowest;n.highest=inputs->highest;
  n.fog_band={active_to_world[3]+float(upload.fog_edges.x0),active_to_world[7]+float(upload.fog_edges.y0),
              active_to_world[3]+float(upload.fog_edges.x1),active_to_world[7]+float(upload.fog_edges.y1)};
  const UINT tx=n.tiles_x,ty=n.tiles_y;
  if(FAILED(build_contact_tiles(inputs->contact_tiles,inputs->contact_used,tx,ty,n.contact,&n.contact_rows)))return report("omitted reason=tile_upload_failed");
  if(!n.contact_rows.empty()){
   // Its blend half: no material until its families load (as on D3D11).
   auto grid=n.contact_rows;
   grid.resize(grid.size()*2,std::array<std::uint32_t,4>{~0u,~0u,0u,~0u});
   D3D12TextureRef tiles;
   if(FAILED(d3d12_->create_texture(tx,ty*2,DXGI_FORMAT_R32G32B32A32_UINT,tiles))||
      FAILED(d3d12_->write_texture(*tiles,RECT{0,0,LONG(tx),LONG(ty*2)},grid.data(),tx*16u)))return report("omitted reason=tile_upload_failed");
   n.contact.tiles12=std::move(tiles);
   if(tile_blend&&inputs->families_valid&&inputs->tiles_per_texture_side==8&&inputs->blend_input){
    n.blend_input=inputs->blend_input;
    std::promise<TerrainTileFamilies> loaded;loaded.set_value(inputs->families);
    n.families=TileFamiliesJob(loaded.get_future(),nullptr);
   }else std::vector<std::array<std::uint32_t,4>>().swap(n.contact_rows);
  }
  std::vector<NativeOwnedTextureMip> page_mips;
  for(std::size_t material=0;material<inputs->used.size()&&material<contact_blend_max_materials;++material){
   if(!inputs->used[material]||!(n.contact.pending&(std::uint64_t{1}<<material)))continue;
   const auto texture=material<inputs->atlas_textures.size()?inputs->atlas_textures[material]:UINT32_MAX;
   if(texture>=upload.textures.size())continue;
   D3D12TextureRef page;std::uint64_t backing=0;
   if(FAILED(get_native_owned_texture_d3d12(neighbour_piece_texture_view(upload.textures[texture],page_mips),page,backing,nullptr,true))||!page)continue;
   if(copy_contact_atlas_d3d12(*page,static_cast<UINT>(material),n.contact)!=S_OK)report("contact art omitted for one material");
  }
  // Its ground cover map: the same grids, fields and paint the active map
  // builds (build_ground_cover_map_d3d12), from the stream's inputs, with its
  // own terrain atlas pages; 2 colour texels per tile (contact reads mip 0).
  if(cover){
   const auto& grids=inputs->grids;
   const auto types=inputs->families_valid?ambient::terrain_cover_types(inputs->base_slots,inputs->base_lanes,inputs->slot_types,
     terrain_tile_sand_slots(inputs->families),terrain_tile_bare_corners(inputs->families),terrain_tile_soft_sand_corners(inputs->families)):
     std::vector<std::uint16_t>(grids.types.begin(),grids.types.end());
   auto& map=n.cover12;
   if(FAILED(build_ground_cover_map_d3d12(map,tx,ty,2,grids.area,grids.height,grids.ground,types,grids.types,inputs->tiles,inputs->used,inputs->trees))){
    n.cover12={};report("ground_cover=omitted reason=grid_upload_failed");
   }else{
    map.atlases.resize(map.paint.size());
    UINT missing=0;
    for(std::size_t material=0;material<map.paint.size();++material){
     if(!map.paint[material])continue;
     const auto texture=material<inputs->atlas_textures.size()?inputs->atlas_textures[material]:UINT32_MAX;
     std::uint64_t backing=0;
     if(texture>=upload.textures.size()||FAILED(get_native_owned_texture_d3d12(neighbour_piece_texture_view(upload.textures[texture],page_mips),
        map.atlases[material],backing,nullptr,true))||!map.atlases[material]){map.atlases[material].reset();map.paint[material]=0;++missing;}
    }
    char text[160];std::snprintf(text,sizeof(text),"ground_cover=ready tiles=%ux%u origin=%.0f,%.0f unpainted_materials=%u%s",tx,ty,
     n.origin[0],n.origin[1],missing,inputs->families_valid?"":" beach_sand=terrain_type_only");
    report(text);
   }
  }
  gc_neighbours_.push_back(std::move(n));
 }catch(...){report("omitted reason=cpu_allocation");}
}

// The ground contact and terrain material maps on D3D12 (update_contact_blend_maps):
// the tile grid (t15) and the terrain atlases copied, every level, into one
// array (t16) as each becomes resident, from the D3D12 terrain textures.
HRESULT Renderer::update_contact_maps_d3d12(){
 const auto& config=debug_overlay_.config();
 if(contact_map_generation_!=ambient_map_generation_){
  AmbientWorldSource inputs;
  if(!bridge_ambient_world_source(inputs)||!inputs.terrain)return S_OK;
  release_contact_blend_maps();
  contact_map_generation_=ambient_map_generation_;contact_retry_frame_=0;contact_incompatible_logged_=false;
  const auto& terrain=*inputs.terrain;
  const UINT tiles_x=ambient_tiles_x_,tiles_y=ambient_tiles_y_;
  if(!tiles_x||!tiles_y)return S_OK;
  HRESULT hr=build_contact_tiles(terrain,tiles_x,tiles_y,contact_maps_,&contact_tile_rows_);
  if(FAILED(hr))return hr;
  if(!contact_tile_rows_.empty()){contact_tiles_x_=tiles_x;contact_tiles_y_=tiles_y;}
  tile_blend_margin_=terrain.tile_size?8.f/float(terrain.tile_size):0.f;
 }
 if(contact_maps_.tiles12&&!tile_blend_ready_&&
    contact_map_generation_==ambient_map_generation_&&poll_terrain_tile_families()){
  const HRESULT hr=apply_terrain_tile_blend(tile_families_);if(FAILED(hr))return hr;
 }
 // Terrain 2.0 (update_contact_blend_maps' rules): own edge families while
 // on; each map's heights follow its copied atlas slices.
 if(!tile_blend_own_edges_){tile_blend_own_edges_=true;update_terrain_tile_blend_seams();}
 {
  if(terrain2_map_.dirty||contact_maps_.ready!=terrain2_map_.seen_ready)bake_terrain2_heights_d3d12(terrain2_map_,contact_maps_,"active");
  for(auto& n:gc_neighbours_)
   if(n.terrain2.dirty||n.contact.ready!=n.terrain2.seen_ready)bake_terrain2_heights_d3d12(n.terrain2,n.contact,n.name.c_str());
  update_snow_surface_d3d12();
 }
 if(tile_blend_ready_&&!tile_blend_rows_&&!terrain2_marker_logged_){
  terrain2_marker_logged_=true;
  bridge_append_probe_event("terrain2_no_materials","the map has no material families (atlas profile or preset); terrain 2.0 ground shows the marker colour");
 }
 if(!contact_maps_.pending||native_frame_index_<contact_retry_frame_)return S_OK;
 contact_retry_frame_=native_frame_index_+(load_reveal_holding_?1:30);
 for(std::uint32_t material=0;material<contact_blend_max_materials;++material){
  const std::uint64_t bit=std::uint64_t{1}<<material;
  if(!(contact_maps_.pending&bit))continue;
  std::shared_ptr<const OwnedVisualSurface> surface;
  if(!bridge_terrain_atlas(material,surface)||!surface){d3d12_->count("contact atlas: atlas surface not captured (retried)");continue;}
  const auto found=terrain_textures_d3d12_.find(surface->id);
  if(found==terrain_textures_d3d12_.end()||found->second.generation!=surface->generation||!found->second.texture){d3d12_->count("contact atlas: page not drawn yet (retried)");continue;}
  D3D12Texture& source=*found->second.texture;
  const HRESULT copied=copy_contact_atlas_d3d12(source,material,contact_maps_);
  if(FAILED(copied))return copied;
  if(copied==S_FALSE){
   if(!contact_incompatible_logged_){
    contact_incompatible_logged_=true;
    bridge_append_probe_event("contact_blend_atlas_incompatible","terrain atlas format or size differs; affected contact art omitted");
   }
   continue;
  }
  d3d12_->count("contact atlas: materials copied");
 }
 return S_OK;
}

// The authoritative terrain frame and live liquid-layer shifts serve both
// colour and caster shaders. Shadow submission can precede the colour flush.
HRESULT Renderer::upload_native_terrain_frame_d3d12(){
 HRESULT hr=S_OK;
 if(!native_terrain_frame_d3d12_.empty()){
  if(terrain2_records_first_&&native_terrain_frame_d3d12_.size()>terrain2_records_first_)
   (void)terrain2_records_d3d12(native_terrain_frame_d3d12_.data()+terrain2_records_first_); // the table's bindless indices
  const UINT64 bytes=native_terrain_frame_d3d12_.size()*sizeof(float);
  if(FAILED(hr=native_d3d12_buffer(native12_.terrain_frame,bytes,UINT(bytes),0))||
     FAILED(hr=d3d12_->write_buffer(*native12_.terrain_frame,0,native_terrain_frame_d3d12_.data(),bytes)))return hr;
 }
 if(native12_.layer_shift_revision!=native_terrain_layer_shift_revision_&&!native_terrain_layer_shifts_.empty()){
  const UINT64 bytes=native_terrain_layer_shifts_.size()*sizeof(float);
  if(FAILED(hr=native_d3d12_buffer(native12_.layer_shifts,bytes,sizeof(float),0))||
     FAILED(hr=d3d12_->write_buffer(*native12_.layer_shifts,0,native_terrain_layer_shifts_.data(),bytes)))return hr;
  native12_.layer_shift_revision=native_terrain_layer_shift_revision_;
 }
 return S_OK;
}

// Point lights (t18), their grid (t17) and constants (b5), as the D3D11 flush
// uploads them when set_native_point_lights changed them.
HRESULT Renderer::upload_native_point_lights_d3d12(){
 if(!native_point_lights_dirty_)return S_OK;
 const UINT count=static_cast<UINT>(native_point_lights_.size());
 HRESULT hr=S_OK;
 if(count){
  const UINT64 bytes=UINT64(count)*sizeof(NativePointLightGpu);
  if(FAILED(hr=native_d3d12_buffer(native12_.point_lights,bytes,sizeof(NativePointLightGpu),0))||
     FAILED(hr=d3d12_->write_buffer(*native12_.point_lights,0,native_point_lights_.data(),bytes)))return hr;
 }
 bool grid_changed=native_point_light_grid_inputs_.size()!=native_point_lights_.size();
 for(std::size_t n=0;!grid_changed&&n<native_point_lights_.size();++n)
  grid_changed=native_point_lights_[n].center_inverse_radius_squared!=native_point_light_grid_inputs_[n];
 if(grid_changed){
  std::vector<std::array<float,4>> grid_inputs;
  try{grid_inputs.reserve(count);for(const auto& light:native_point_lights_)grid_inputs.push_back(light.center_inverse_radius_squared);
   if(!build_point_light_grid(grid_inputs,native_point_light_grid_))return E_INVALIDARG;}
  catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  native_point_light_grid_inputs_=std::move(grid_inputs);
  const UINT64 bytes=UINT64(native_point_light_grid_.data.size())*sizeof(UINT);
  if(bytes&&(FAILED(hr=native_d3d12_buffer(native12_.point_light_grid,bytes,sizeof(UINT),0))||
             FAILED(hr=d3d12_->write_buffer(*native12_.point_light_grid,0,native_point_light_grid_.data.data(),bytes))))return hr;
 }
 const auto& grid=native_point_light_grid_;
 // The shared b5 layout; the bounce-volume tail is written every flush
 // (write_cave_sample_constants_d3d12).
 struct{UINT count,width,height,glow;float origin[2],inverse_cell,tail;CaveSampleConstants cave;}values{
  count,grid.width,grid.height,static_cast<UINT>(emissive_published_),{grid.origin[0],grid.origin[1]},grid.inverse_cell,0,{}};
 static_assert(sizeof(values)==sizeof(native12_.point_light_constants),"point-light constant layout");
 std::memcpy(native12_.point_light_constants.data(),&values,sizeof(values));
 native_point_lights_dirty_=false;
 return S_OK;
}

// The terrain value-noise lattice (t122, terrainNoise): the same bytes the
// D3D11 device creation makes, as a D3D12 texture.
HRESULT Renderer::ensure_terrain_noise_d3d12(){
 if(native12_.terrain_noise)return S_OK;
 constexpr UINT side=1024;
 std::vector<std::uint8_t> lattice;
 try{lattice.resize(std::size_t(side)*side);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 for(std::uint32_t y=0;y<side;++y)for(std::uint32_t x=0;x<side;++x){
  std::uint32_t h=x*0x8da6b343u^y*0xd8163841u;
  h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;
  lattice[std::size_t(y)*side+x]=static_cast<std::uint8_t>(h>>24);
 }
 D3D12TextureRef texture;
 HRESULT hr=d3d12_->create_texture(side,side,DXGI_FORMAT_R8_UNORM,texture);
 if(SUCCEEDED(hr))hr=d3d12_->write_texture(*texture,RECT{0,0,LONG(side),LONG(side)},lattice.data(),side);
 if(FAILED(hr))return hr;
 native12_.terrain_noise=std::move(texture);
 return S_OK;
}

// upload_native_terrain_phases on the D3D12 backend: the same copies
// (nativeTerrainPhaseCS: the frame's phase values from the dynamic bytes into
// each animated range of the animation buffer), on their own work list.
HRESULT Renderer::upload_native_terrain_phases_d3d12(){
 native_terrain_phase_copies_.clear();
 for(const auto& work:native_terrain_phase_work_)if(native_animation_ranges_[work.allocation].references){
  if(std::uint64_t(work.offset)+work.count*4ull>native_pending_upload_.size())return E_FAIL;
  NativeDrawWorkGpu copy{};copy.record=work.offset;
  copy.output_triangle_base=native_animation_ranges_[work.allocation].base;
  copy.indirect_args_byte_offset=work.count;native_terrain_phase_copies_.push_back(copy);
 }
 if(native_terrain_phase_copies_.empty())return S_OK;
 if(!native12_.animation)return E_FAIL;
 const UINT64 bytes=UINT64(native_terrain_phase_copies_.size())*sizeof(NativeDrawWorkGpu);
 HRESULT hr=native_d3d12_buffer(native12_.phase_work,bytes,sizeof(NativeDrawWorkGpu),0);
 if(SUCCEEDED(hr))hr=d3d12_->write_buffer(*native12_.phase_work,0,native_terrain_phase_copies_.data(),bytes);
 if(FAILED(hr))return hr;
 transition_profile_pass("compute_terrain_phase");
 d3d12_->clear_slots();
 d3d12_->set_resource(2,native12_.dynamic.get());
 d3d12_->set_resource(16,native12_.phase_work.get());
 d3d12_->set_unordered(0,native12_.animation.get());
 hr=d3d12_->dispatch(native12_.programs[3],static_cast<UINT>(native_terrain_phase_copies_.size()),1,1);
 if(FAILED(hr))return hr;
 if(d3d12_not_done(hr)){if(hr==S_FALSE)d3d12_->omit("terrain phases (pipeline not ready)",false);return S_OK;}
 for(const auto& work:native_terrain_phase_work_)if(native_animation_ranges_[work.allocation].references){
  auto& slot=native_terrain_phases_[work.slot];
  if(slot.allocation==work.allocation&&slot.revision==work.revision)slot.valid=true;
 }
 return S_OK;
}

// The persistent instance rows and draw descriptors (t13, t5): the dirty
// records written straight in (the D3D11 path scatters the same rows with
// nativeInstanceUpdateCS).
HRESULT Renderer::upload_native_instances_d3d12(){
 const UINT required=static_cast<UINT>(native_instance_records_.size());
 if(required>native12_.instance_capacity||!native12_.instances||!native12_.draws){
  const UINT capacity=(std::max)({required,native12_.instance_capacity*2,64u});
  D3D12BufferRef instances,draws;
  HRESULT hr=d3d12_->create_buffer(UINT64(capacity)*sizeof(NativeInstanceGpu),sizeof(NativeInstanceGpu),0,instances);
  if(SUCCEEDED(hr))hr=d3d12_->create_buffer(UINT64(capacity)*sizeof(NativeDrawGpu),sizeof(NativeDrawGpu),0,draws);
  if(FAILED(hr))return hr;
  if(native12_.instances&&native12_.instance_capacity){
   if(FAILED(hr=d3d12_->copy_buffer(*instances,*native12_.instances,UINT64(native12_.instance_capacity)*sizeof(NativeInstanceGpu))))return hr;
   if(FAILED(hr=d3d12_->copy_buffer(*draws,*native12_.draws,UINT64(native12_.instance_capacity)*sizeof(NativeDrawGpu))))return hr;
  }
  native12_.instances=std::move(instances);native12_.draws=std::move(draws);native12_.instance_capacity=capacity;
 }
 for(UINT index:native_instance_dirty_){
  auto& record=native_instance_records_[index];
  if(!record.references)continue;
  if(!record.uploaded){
   const HRESULT hr=d3d12_->write_buffer(*native12_.instances,UINT64(index)*sizeof(NativeInstanceGpu),&record.command.instance,sizeof(NativeInstanceGpu));
   if(FAILED(hr))return hr;
   record.uploaded=true;
  }
  if(!record.descriptor_uploaded){
   const HRESULT hr=d3d12_->write_buffer(*native12_.draws,UINT64(index)*sizeof(NativeDrawGpu),&record.descriptor,sizeof(NativeDrawGpu));
   if(FAILED(hr))return hr;
   record.descriptor_uploaded=true;
  }
 }
 return S_OK;
}

// get_native_owned_texture for the D3D12 backend: an owned source with its
// mip chain (the HUD's atlas textures) becomes one D3D12 texture per key
// and generation, cached in native_owned_textures_ like the D3D11 copy.
HRESULT Renderer::get_native_owned_texture_d3d12(const NativeOwnedTextureView& source,D3D12TextureRef& output,
 std::uint64_t& backing,NativeGeometryResult* result,bool world_mips,bool budget_reducible){
 const auto fail=[&](HRESULT hr,const char* operation){return native_geometry_failure(result,hr,operation,true);};
 output.reset();backing=0;
 // A camp icon: the surface table's image (the host icon worker's decode),
 // uploaded once per generation; its entry retires with retire_camp_texture.
 if(source.surface&&source.camp_surface_alias){
  if(source.surface->id!=source.identity||source.surface->generation!=source.generation)return fail(E_INVALIDARG,"texture.surface_generation");
  std::string key;
  try{key="surface:"+std::to_string(source.identity)+"|mips=authored|camp";}catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.key_allocation");}
  const auto existing=native_owned_textures_.find(key);
  if(existing!=native_owned_textures_.end()&&existing->second.generation==source.generation&&existing->second.d3d12){
   auto& texture=existing->second;
   texture.last_used=native_queue_epoch_;texture.last_used_frame=native_frame_index_;
   output=texture.d3d12;backing=texture.backing;return S_OK;
  }
  if(!surface_texture_resolver_)return fail(E_FAIL,"texture.surface_resolver");
  ComPtr<ID3D11Texture2D> unused;SurfaceImage* image=nullptr;
  HRESULT hr=surface_texture_resolver_(surface_texture_resolver_context_,device_.Get(),source.identity,source.generation,unused,image);
  if(FAILED(hr)||!image)return fail(FAILED(hr)?hr:E_FAIL,"texture.surface_unavailable");
  if(FAILED(hr=upload_d3d12(*image,nullptr))||!image->d3d12)return fail(FAILED(hr)?hr:E_FAIL,"texture.camp_upload");
  if(native_owned_texture_serial_>=((1ull<<63)-1))return fail(E_OUTOFMEMORY,"texture.serial");
  try{
   NativeOwnedTexture value{};
   value.backing=(1ull<<63)|++native_owned_texture_serial_;
   value.generation=source.generation;value.width=image->width;value.height=image->height;value.mip_count=1;
   value.surface_id=source.identity;value.camp_surface_alias=true;
   value.last_used=native_queue_epoch_;value.last_used_frame=native_frame_index_;value.d3d12=image->d3d12;
   backing=value.backing;output=image->d3d12;
   native_owned_textures_[key]=std::move(value);
  }catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.cache_allocation");}
  return S_OK;
 }
 // A completed client surface (not a camp icon): its own texture from the
 // host table's bytes. A static world surface takes its full mip chain, as
 // D3D11's per-generation copy: the surface texture itself when its upload
 // already built the chain, else one entry per generation built from the
 // same bytes (retired with the surface, retire_surface).
 if(source.surface){
  if(source.surface->id!=source.identity||source.surface->generation!=source.generation)return fail(E_INVALIDARG,"texture.surface_generation");
  if(!surface_texture_resolver_)return fail(E_FAIL,"texture.surface_resolver");
  ComPtr<ID3D11Texture2D> unused;SurfaceImage* image=nullptr;
  HRESULT hr=surface_texture_resolver_(surface_texture_resolver_context_,device_.Get(),source.identity,source.generation,unused,image);
  if(FAILED(hr)||!image)return fail(FAILED(hr)?hr:E_FAIL,"texture.surface_unavailable");
  if(FAILED(hr=upload_d3d12(*image,nullptr))||!image->d3d12)return fail(FAILED(hr)?hr:E_FAIL,"texture.surface_upload");
  backing=(2ull<<62)|source.identity;output=image->d3d12;
  if(!(world_mips&&source.surface->static_mip_hint&&!(source.surface->format.dwFlags&DDPF_FOURCC)))return S_OK;
  UINT stored_width=0,stored_height=0,stored_levels=0;DXGI_FORMAT stored_format=DXGI_FORMAT_UNKNOWN;
  if(d3d12_->texture_info(*image->d3d12,stored_width,stored_height,stored_levels,stored_format)&&stored_levels>1)return S_OK;
  if(image->gpu_captured){
   // GPU-written content: its CPU bytes are stale, so no box chain.
   d3d12_->omit("world surface textures: a GPU-captured surface keeps one level",false);
   return S_OK;
  }
  std::string key;
  try{key="surface:"+std::to_string(source.identity)+"|mips=full";}catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.key_allocation");}
  const auto existing=native_owned_textures_.find(key);
  if(existing!=native_owned_textures_.end()&&existing->second.generation==source.generation&&existing->second.d3d12){
   auto& texture=existing->second;
   texture.last_used=native_queue_epoch_;texture.last_used_frame=native_frame_index_;
   output=texture.d3d12;backing=texture.backing;return S_OK;
  }
  D3D12TextureRef texture;
  if(FAILED(hr=surface_mips_d3d12(*image,texture)))return fail(hr,"texture.surface_mips");
  if(native_owned_texture_serial_>=((1ull<<63)-1))return fail(E_OUTOFMEMORY,"texture.serial");
  try{
   NativeOwnedTexture value{};
   value.backing=(1ull<<63)|++native_owned_texture_serial_;
   value.generation=source.generation;value.format=DXGI_FORMAT_B8G8R8A8_UNORM;value.width=image->width;value.height=image->height;
   value.mip_count=1;value.surface_id=source.identity;
   value.last_used=native_queue_epoch_;value.last_used_frame=native_frame_index_;value.d3d12=texture;
   backing=value.backing;
   native_owned_textures_[key]=std::move(value);
  }catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.cache_allocation");}
  output=std::move(texture);
  return S_OK;
 }
 if(!source.mips&&source.value_source==1&&source.resource){
  // A neighbour piece texture (archive member) without mips: its resident
  // entry (adopt_prebuilt_textures) serves it, as on D3D11.
  std::string key;
  try{key=native_owned_archive_texture_key(*source.resource,source.generation,false);}catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.key_allocation");}
  const auto existing=native_owned_textures_.find(key);
  if(existing==native_owned_textures_.end()||existing->second.generation!=source.generation||!existing->second.d3d12)
   return fail(E_FAIL,"texture.not_resident");
  auto& texture=existing->second;
  texture.last_used=native_queue_epoch_;texture.last_used_frame=native_frame_index_;
  output=texture.d3d12;backing=texture.backing;return S_OK;
 }
 if(!source.mips||!source.mip_count||source.mip_count>32)return fail(E_INVALIDARG,"texture.source");
 const bool static_source=source.value_source==1||source.value_source==3;
 const bool uncompressed=source.format!=NativeOwnedTextureFormat::dxt1&&source.format!=NativeOwnedTextureFormat::dxt3&&
  source.format!=NativeOwnedTextureFormat::dxt5;
 const bool generate=world_mips&&static_source&&source.mip_count==1&&uncompressed;
 std::string key;
 // A neighbour's uncompressed textures arrive under this key prepared by the
 // stream (build_native_prebuilt_texture), the same chain or levels.
 try{key=native_owned_mip_texture_key(source,generate);}
 catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.key_allocation");}
 // VRAM budget (renderer_vram_budget.cpp): a reducible world texture with a
 // mip chain leaves out its top levels while the local budget is short, as
 // get_native_owned_texture does; small textures stay whole (vram_texture_skip).
 const UINT top_width=source.mips[0].width,top_height=source.mips[0].height;
 UINT skip=budget_reducible&&world_mips?vram_texture_skip(top_width,top_height,generate?full_mip_count(top_width,top_height):source.mip_count):0u;
 if(!uncompressed&&((std::max)(1u,top_width>>skip)%4u||(std::max)(1u,top_height>>skip)%4u))skip=0; // block formats: whole blocks at the top level
 const auto existing=native_owned_textures_.find(key);
 if(existing!=native_owned_textures_.end()&&existing->second.generation==source.generation&&existing->second.d3d12){
  auto& texture=existing->second;
  texture.last_used=native_queue_epoch_;texture.last_used_frame=native_frame_index_;
  // Built at another budget level: rebuilt when this frame's rebuild work
  // allows (the old texture goes with the last record that holds it).
  UINT stored_width=0,stored_height=0,stored_levels=0;DXGI_FORMAT stored_format=DXGI_FORMAT_UNKNOWN;
  if(texture.budget_skip==skip||!d3d12_->texture_info(*texture.d3d12,stored_width,stored_height,stored_levels,stored_format)||
     !vram_take_rebuild(texture_storage_bytes(stored_width,stored_height,stored_levels,stored_format))){
   output=texture.d3d12;backing=texture.backing;return S_OK;
  }
 }
 DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;UINT block_bytes=0,pixel_bytes=0;
 switch(source.format){
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
 const UINT width=source.mips[0].width,height=source.mips[0].height;
 if(!width||!height||width>16384||height>16384)return fail(E_INVALIDARG,"texture.dimensions");
 if(generate){
  // The BGRA8 expansion (the D3D11 path's conversion), then each level a 2x2
  // box of the one above (native_owned_box_mips, shared with the stream).
  const NativeOwnedTextureMip& top=source.mips[0];
  if(!top.data||std::uint64_t(width)*height*pixel_bytes>top.bytes)return fail(E_INVALIDARG,"texture.mip_bytes");
  std::vector<std::vector<std::uint32_t>> chain;
  try{if(!native_owned_box_mips(source,chain))return fail(E_INVALIDARG,"texture.mip_bytes");}
  catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.mip_allocation");}
  const UINT levels=static_cast<UINT>(chain.size());
  const UINT kept=levels>skip?skip:0u; // the top levels the budget leaves out
  D3D12TextureRef texture;
  HRESULT hr=d3d12_->create_texture((std::max)(1u,width>>kept),(std::max)(1u,height>>kept),DXGI_FORMAT_B8G8R8A8_UNORM,texture,levels-kept);
  if(FAILED(hr))return fail(hr,"texture.create");
  for(UINT index=0,w=width,h=height;index<levels;++index,w=(std::max)(1u,w/2),h=(std::max)(1u,h/2))
   if(index>=kept&&FAILED(hr=d3d12_->write_texture(*texture,RECT{0,0,LONG(w),LONG(h)},chain[index].data(),w*4u,index-kept)))return fail(hr,"texture.write");
  if(native_owned_texture_serial_>=((1ull<<63)-1))return fail(E_OUTOFMEMORY,"texture.serial");
  try{
   NativeOwnedTexture value{};
   value.backing=(1ull<<63)|++native_owned_texture_serial_;
   value.generation=source.generation;value.format=format;value.width=width;value.height=height;value.mip_count=source.mip_count;
   value.budget_skip=kept;
   value.last_used=native_queue_epoch_;value.last_used_frame=native_frame_index_;value.d3d12=texture;
   backing=value.backing;
   native_owned_textures_[key]=std::move(value);
  }catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.cache_allocation");}
  output=std::move(texture);
  return S_OK;
 }
 // Authored levels upload from level `skip` (the budget's top levels left out).
 const UINT kept=source.mip_count>skip?skip:0u;
 D3D12TextureRef texture;
 HRESULT hr=d3d12_->create_texture((std::max)(1u,width>>kept),(std::max)(1u,height>>kept),format,texture,source.mip_count-kept);
 if(FAILED(hr))return fail(hr,"texture.create");
 for(UINT index=0;index<source.mip_count;++index){
  const NativeOwnedTextureMip& mip=source.mips[index];
  if(!mip.data||mip.width!=(std::max)(1u,width>>index)||mip.height!=(std::max)(1u,height>>index))return fail(E_INVALIDARG,"texture.mip_layout");
  const UINT row_pitch=block_bytes?(std::max)(1u,(mip.width+3u)/4u)*block_bytes:mip.width*pixel_bytes;
  const UINT rows=block_bytes?(std::max)(1u,(mip.height+3u)/4u):mip.height;
  if(std::uint64_t(row_pitch)*rows>mip.bytes)return fail(E_INVALIDARG,"texture.mip_bytes");
  if(index>=kept&&FAILED(hr=d3d12_->write_texture(*texture,RECT{0,0,LONG(mip.width),LONG(mip.height)},mip.data,row_pitch,index-kept)))return fail(hr,"texture.write");
 }
 if(native_owned_texture_serial_>=((1ull<<63)-1))return fail(E_OUTOFMEMORY,"texture.serial");
 try{
  NativeOwnedTexture value{};
  value.backing=(1ull<<63)|++native_owned_texture_serial_;
  value.generation=source.generation;value.format=format;value.width=width;value.height=height;value.mip_count=source.mip_count;
  value.budget_skip=kept;
  value.last_used=native_queue_epoch_;value.last_used_frame=native_frame_index_;value.d3d12=texture;
  backing=value.backing;
  native_owned_textures_[key]=std::move(value);
 }catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.cache_allocation");}
 output=std::move(texture);
 return S_OK;
}

// draw_game_effect on the D3D12 backend: the same programs, constants and
// fixed-function translation; the elements in t7 (written in stream order
// before the draw), the texture in t0, world fog in b10.
HRESULT Renderer::draw_game_effect_d3d12(const GameEffectDraw& draw,const FixedState& st,SurfaceImage* texture){
 const UINT count=draw.element_count;
 if(!scene_composited_)scene_updated_=true;
 transition_profile_pass(draw.profile_pass);
 const UINT64 bytes=UINT64(count)*sizeof(GameParticleGpu);
 HRESULT hr=native_d3d12_buffer(native12_.game_effects,bytes,sizeof(GameParticleGpu),0);
 if(SUCCEEDED(hr))hr=d3d12_->write_buffer(*native12_.game_effects,0,game_effect_elements_.data(),bytes);
 if(FAILED(hr))return hr;
 GameParticleConstants c{};game_effect_constants(draw,st,texture!=nullptr,c);
 const D3D12Texture* texture12=nullptr;
 if(texture){
  texture->world_mip_use=!scene_composited_;if(scene_composited_)texture->static_mip_hint=false;
  if(FAILED(hr=upload(*texture)))return hr;
  texture12=texture->d3d12.get();
 }
 if(c.fog_mode&&FAILED(hr=bind_world_fog()))return hr;
 d3d12_->clear_slots();
 d3d12_->set_constants(0,&c,sizeof(c));
 d3d12_->set_constants(10,&fog_frame_d3d12_,sizeof(fog_frame_d3d12_));
 bind_world_fog_volume_d3d12();
 d3d12_->set_resource(0,texture12);
 d3d12_->set_resource(7,native12_.game_effects.get());
 const auto sampling=world_texture_sampler(st.stage[0][D3DTSS_MINFILTER],st.stage[0][D3DTSS_MAGFILTER],st.stage[0][D3DTSS_MIPFILTER],
  st.stage[0][D3DTSS_ADDRESSU],st.stage[0][D3DTSS_ADDRESSV],!scene_composited_,1u<<configured_integer(debug_overlay_.config(),"anisotropy",3));
 d3d12_->set_sampler(0,sampling.description);
 {D3D11_SAMPLER_DESC linear{};linear.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;linear.MaxLOD=D3D11_FLOAT32_MAX;d3d12_->set_sampler(7,linear);}
 D3D12NativeState state;
 state.raster=fixed_raster_desc(st.render[D3DRENDERSTATE_CULLMODE],st.render[D3DRENDERSTATE_FILLMODE]==D3DFILL_WIREFRAME,true);
 state.depth=fixed_depth_desc(st.render[D3DRENDERSTATE_ZENABLE],st.render[D3DRENDERSTATE_ZWRITEENABLE],st.render[D3DRENDERSTATE_ZFUNC]);
 state.blend=fixed_blend_choice(st.render[D3DRENDERSTATE_ALPHABLENDENABLE],st.render[D3DRENDERSTATE_SRCBLEND],st.render[D3DRENDERSTATE_DESTBLEND],false);
 // Blended world particles also feed the bloom glow target (cleared on first
 // use each frame), as the D3D11 draw_game_effect binds it: the world colour
 // and the glow as two targets over the depth the world pass has bound now
 // (the pre-water snapshot for ground marks under water, composite_world_d3d12).
 D3D12Texture* glow=nullptr;D3D12Texture* depth=nullptr;
 if(!scene_composited_&&world_d3d12_bound_&&world_d3d12_&&st.render[D3DRENDERSTATE_ALPHABLENDENABLE]&&c.coverage_mode!=3u&&
    configured_integer(debug_overlay_.config(),"bloom",0)>0){
  auto& bloom=post12_.bloom;
  UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
  if(d3d12_->texture_info(*world_d3d12_,width,height,levels,format)&&
     (!bloom.glow||!d3d12_->texture_matches(*bloom.glow,width,height,kGlowFormat))){
   bloom.glow.reset();bloom.glow_frame=UINT64_MAX;
   if(FAILED(d3d12_->create_target(width,height,bloom.glow,kGlowFormat,DXGI_FORMAT_UNKNOWN))){
    bloom.glow.reset();bridge_report_event("bloom_glow","target_creation_failed (D3D12)");}
  }
  if(bloom.glow){
   if(bloom.glow_frame!=native_frame_index_){const float black[4]{};d3d12_->clear_texture(*bloom.glow,black);bloom.glow_frame=native_frame_index_;}
   glow=bloom.glow.get();
   depth=drawing_ground_marks_&&ground_marks_under_water_d3d12()?water12_.depth.get():world_d3d12_.get();
   D3D12Texture* targets[2]={world_d3d12_.get(),glow};
   d3d12_->set_targets(targets,2,depth);
  }
 }
 hr=d3d12_->draw(world_support_shader_program(draw.vertex_program),world_support_shader_program(WorldSupportShader::game_particle_ps),
                 state,nullptr,draw.vertex_count);
 // The particle's depth for the water reflections: the second draw of the
 // same call, as D3D11 draw_game_particle_depth follows it.
 if(hr==S_OK&&draw.reflection_depth&&c.coverage_mode!=3u&&!scene_composited_&&particle_depth_wanted())
  (void)draw_game_particle_depth_d3d12(world_support_shader_program(draw.vertex_program),draw.vertex_count,state);
 d3d12_->clear_slots();
 if(glow){ // back to the world target and the depth bound before
  if(depth!=world_d3d12_.get()){D3D12Texture* world=world_d3d12_.get();d3d12_->set_targets(&world,1,depth);}
  else bind_world_targets_d3d12();
 }
 if(FAILED(hr))return hr;
 if(d3d12_not_done(hr)){if(hr==S_FALSE)d3d12_->omit("game effects (pipeline not ready)",false);return S_OK;}
 d3d12_->count("game effect draws");
 return S_OK;
}

// draw_game_particle_depth on D3D12: the particle call just drawn again
// (same vertex stage, elements, texture, constants) with gameParticleDepthPS,
// min-blending the screen depth of its visible coverage into the current R32F
// slot, tested against the world depth without writing it. Slots, clears and
// the hand-over to the next frame's reflections (capture_water_history) are
// the D3D11 ones. A failure turns particle reflections off, named once.
HRESULT Renderer::draw_game_particle_depth_d3d12(const ShaderProgram& vertex,UINT vertex_count,const D3D12NativeState& particle){
 if(!world_d3d12_)return E_FAIL;
 const auto unavailable=[&](HRESULT hr,const char* name){
  particle_depth12_={};particle_depth_failed_=true;
  bridge_report_event("particle_reflections","unavailable");d3d12_->omit(name,false);return hr;
 };
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 HRESULT hr=S_OK;
 if(!particle_depth12_[0]||!particle_depth12_[1]||particle_depth_width_!=width||particle_depth_height_!=height){
  particle_depth12_={};particle_depth_frame_=particle_depth_history_frame_=UINT64_MAX;particle_depth_width_=particle_depth_height_=0;
  for(auto& slot:particle_depth12_)
   if(FAILED(hr=d3d12_->create_target(width,height,slot,kParticleDepthFormat,DXGI_FORMAT_UNKNOWN)))
    return unavailable(hr,"particle depth for water reflections (targets unavailable)");
  particle_depth_width_=width;particle_depth_height_=height;
 }
 transition_profile_pass("particle_reflection_depth");
 D3D12Texture* const output=particle_depth12_[particle_depth_current_].get();
 if(particle_depth_frame_!=native_frame_index_){
  const float far_depth[4]={1,1,1,1};d3d12_->clear_texture(*output,far_depth);
  particle_depth_frame_=native_frame_index_;
 }
 D3D12NativeState state=particle;
 auto& blend=state.custom_blend;
 blend.custom=1;blend.source=blend.destination=blend.source_alpha=blend.destination_alpha=D3D11_BLEND_ONE;
 blend.op=blend.op_alpha=D3D11_BLEND_OP_MIN;blend.write_mask=D3D11_COLOR_WRITE_ENABLE_RED;
 state.depth={};state.depth.DepthEnable=TRUE;state.depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;state.depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
 D3D12Texture* targets[1]={output};
 d3d12_->set_targets(targets,1,world_d3d12_.get());
 hr=d3d12_->draw(vertex,world_support_shader_program(WorldSupportShader::game_particle_depth_ps),state,nullptr,vertex_count);
 bind_world_targets_d3d12();
 if(FAILED(hr))return unavailable(hr,"particle depth for water reflections (draw failed)");
 if(hr==S_FALSE)d3d12_->omit("particle depth for water reflections (pipeline not ready)",false);
 if(hr==S_OK){profiler_.add_draw(vertex_count/3u,1);profiler_.add_batch();d3d12_->count("particle reflection depth draws");}
 return hr;
}

void Renderer::d3d12_omit(const char* name,bool world) noexcept{if(d3d12_)d3d12_->omit(name,world);}

// prepare_native_poses on the D3D12 backend: the frame's pose jobs evaluated
// by nativePoseCS (skeleton assets in the animation buffer, t10; the jobs,
// t11; the work range, b4) into the pose nodes (u4).
HRESULT Renderer::prepare_native_poses_d3d12(){
 if(native_pose_work_.empty())return S_OK;
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
  const ShaderProgram* pose_cs=nullptr;
  for(const auto& program:native12_.programs)if(std::strcmp(program.entry,"nativePoseCS")==0){pose_cs=&program;break;}
  if(!pose_cs||!native12_.animation||!native12_.pose_nodes)return E_FAIL;
  const UINT64 bytes=UINT64(count)*sizeof(NativePoseGpu);
  HRESULT hr=native_d3d12_buffer(native12_.pose_inputs,bytes,sizeof(NativePoseGpu),0);
  if(SUCCEEDED(hr))hr=d3d12_->write_buffer(*native12_.pose_inputs,0,native_pose_upload_.data(),bytes);
  if(FAILED(hr))return hr;
  transition_profile_pass("compute_pose");
  d3d12_->clear_slots();
  const std::array<UINT,4> work_range{0,count,0,0};
  d3d12_->set_constants(4,work_range.data(),sizeof(work_range));
  d3d12_->set_resource(10,native12_.animation.get());
  d3d12_->set_resource(11,native12_.pose_inputs.get());
  d3d12_->set_unordered(4,native12_.pose_nodes.get());
  hr=d3d12_->dispatch(*pose_cs,width,height,1);
  if(FAILED(hr))return hr;
  if(d3d12_not_done(hr)){if(hr==S_FALSE)d3d12_->omit("poses (pipeline not ready)",false);return S_OK;}
  for(const auto& job:native_pose_work_){auto& slot=native_pose_slots_[job.slot];
   if(job.allocation<native_pose_ranges_.size()&&native_pose_ranges_[job.allocation].references&&
      native_pose_ranges_[job.allocation].generation==job.allocation_generation&&
      slot.revision==job.revision&&slot.value.output==job.value.output)slot.valid=true;}
  return S_OK;
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
}

// The world target for this engine frame: created at the world size (the
// render scale of the output size, or a temporal upscaler's render size),
// bound and cleared as D3D11 binds scene_composite_.target() and world_dsv_
// at begin_engine_frame_at. With an upscaler its targets too; below 100%
// render scale without one, the world-size resolved image the composite
// writes and upscales (SceneComposite::create's resolved_).
HRESULT Renderer::bind_world_d3d12(){
 // A frame begin inside the zone hit-test frame keeps drawing into its target
 // (begin_engine_frame_at's hit-test rebind on D3D11).
 if(hit_test_active_){world_d3d12_bound_=true;bind_world_targets_d3d12();return S_OK;}
 if(!world_d3d12_||!d3d12_->texture_matches(*world_d3d12_,width_,height_,DXGI_FORMAT_R11G11B10_FLOAT)){
  world_d3d12_.reset();
  const HRESULT hr=d3d12_->create_target(width_,height_,world_d3d12_,DXGI_FORMAT_R11G11B10_FLOAT);
  if(FAILED(hr)){d3d12_->omit("world target (creation failed)",true);return hr;}
 }
 if(temporal_upscaler()){if(FAILED(ensure_upscaler_d3d12()))d3d12_->omit("temporal upscaler targets (creation failed)",true);}
 else if(width_!=output_width_||height_!=output_height_){
  auto& up=upscale12_;
  if(!up.resolved||!d3d12_->texture_matches(*up.resolved,width_,height_,DXGI_FORMAT_B8G8R8A8_UNORM)){
   up.resolved.reset();
   if(FAILED(d3d12_->create_target(width_,height_,up.resolved,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_UNKNOWN))){
    up.resolved.reset();d3d12_->omit("render-scale resolved image (creation failed)",true);
   }
  }
 }else upscale12_.resolved.reset();
 world_d3d12_bound_=true;
 if(FAILED(ensure_shadow_history_d3d12()))d3d12_->omit("temporal sun-shadow history (targets unavailable)",false);
 // The AO ambient share for this world frame (cleared at its start), then the
 // world's targets: its colour, and with AO the share at slot 2.
 if(FAILED(update_ambient_occlusion_d3d12()))d3d12_->omit("screen-space ambient occlusion (targets unavailable)",false);
 bind_world_targets_d3d12();
 return S_OK;
}

// create_shadow_history_targets on D3D12: two R8 visibility targets and a
// depth-only target of the world depth's format (D24S8), at the world
// target's size. Remade with the world size; a remake starts without history.
HRESULT Renderer::ensure_shadow_history_d3d12(){
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!world_d3d12_||!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 if(shadow_history_mask12_[0]&&shadow_history_mask12_[1]&&shadow_history_depth12_&&
    d3d12_->texture_matches(*shadow_history_mask12_[0],width,height,DXGI_FORMAT_R8_UNORM))return S_OK;
 for(auto& mask:shadow_history_mask12_)mask.reset();shadow_history_depth12_.reset();
 shadow_history_valid_=shadow_history_written_=shadow_history_committed_=false;
 shadow_history_previous_frame_=shadow_history_current_frame_=UINT64_MAX;
 HRESULT hr=S_OK;
 for(auto& mask:shadow_history_mask12_)
  if(FAILED(hr=d3d12_->create_target(width,height,mask,DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_UNKNOWN)))break;
 if(SUCCEEDED(hr))hr=d3d12_->create_target(width,height,shadow_history_depth12_,DXGI_FORMAT_UNKNOWN,DXGI_FORMAT_D24_UNORM_S8_UINT);
 if(FAILED(hr)){for(auto& mask:shadow_history_mask12_)mask.reset();shadow_history_depth12_.reset();}
 return hr;
}

// The prepass receiver record (rest offset and flags, RGBA16F; terrain
// normal, RG16 UNORM) and the mask (RG16F), colour only, at the world
// target's size (the D3D11 create_targets formats). A failed size is not
// retried until the world size changes.
HRESULT Renderer::ensure_prepass_targets_d3d12(){
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!world_d3d12_||!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 if(prepass_rest12_&&prepass_normal12_&&shadow_mask12_&&d3d12_->texture_matches(*prepass_rest12_,width,height,DXGI_FORMAT_R16G16B16A16_FLOAT))return S_OK;
 prepass_rest12_.reset();prepass_normal12_.reset();shadow_mask12_.reset();
 if(prepass_failed_width12_==width&&prepass_failed_height12_==height)return E_FAIL;
 HRESULT hr=d3d12_->create_target(width,height,prepass_rest12_,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_UNKNOWN);
 if(SUCCEEDED(hr))hr=d3d12_->create_target(width,height,prepass_normal12_,DXGI_FORMAT_R16G16_UNORM,DXGI_FORMAT_UNKNOWN);
 if(SUCCEEDED(hr))hr=d3d12_->create_target(width,height,shadow_mask12_,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_UNKNOWN);
 if(FAILED(hr)){
  prepass_rest12_.reset();prepass_normal12_.reset();shadow_mask12_.reset();
  prepass_failed_width12_=width;prepass_failed_height12_=height;
  return hr;
 }
 prepass_failed_width12_=prepass_failed_height12_=0;
 return S_OK;
}

// The receiver record's flags read zero (no receiver: the mask pass leaves
// the pixel) until a prepass writes them; then its targets with the world depth.
void Renderer::begin_world_prepass_d3d12() noexcept{
 const float zero[4]{};
 d3d12_->clear_texture(*prepass_rest12_,zero);
 D3D12Texture* const targets[2]={prepass_rest12_.get(),prepass_normal12_.get()};
 d3d12_->set_targets(targets,2,world_d3d12_.get());
}

// The mask pass (nativeShadowMaskVS/PS, full screen): the world depth read as
// a shader input (no depth target bound), the receiver record at t117/t118,
// the depth at t119 and the owned sky's cloud shadow it takes with the sun
// shadow. The mask is then bound at t120 for the material pass. S_FALSE: the
// pipeline is not ready (the caller shades unmasked).
HRESULT Renderer::resolve_world_shadow_mask_d3d12(){
 const ShaderProgram* vertex=nullptr;const ShaderProgram* pixel=nullptr;
 for(const auto& program:native12_.programs){
  if(program.defines)continue;
  if(std::strcmp(program.entry,"nativeShadowMaskVS")==0)vertex=&program;
  else if(std::strcmp(program.entry,"nativeShadowMaskPS")==0)pixel=&program;
 }
 if(!vertex||!pixel||!shadow_mask12_)return E_FAIL;
 transition_profile_pass("shadow_mask");
 D3D12Texture* const mask=shadow_mask12_.get();
 d3d12_->set_targets(&mask,1,nullptr);
 d3d12_->set_resource(kShadowMaskSlotD3D12,static_cast<const D3D12Texture*>(nullptr));
 d3d12_->set_resource(kPrepassRestSlotD3D12,prepass_rest12_.get());
 d3d12_->set_resource(kPrepassNormalSlotD3D12,prepass_normal12_.get());
 d3d12_->set_depth_resource(kPrepassDepthSlotD3D12,world_d3d12_.get());
 d3d12_->set_resource(kSkyCloudShadowSlotD3D12,sky_inputs_active_?sky12_.shadow.get():nullptr);
 {D3D11_SAMPLER_DESC linear{};linear.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;linear.MaxLOD=D3D11_FLOAT32_MAX;d3d12_->set_sampler(7,linear);}
 D3D12NativeState state;
 state.raster.FillMode=D3D11_FILL_SOLID;state.raster.CullMode=D3D11_CULL_NONE;state.raster.DepthClipEnable=TRUE;
 const HRESULT hr=d3d12_->draw(*vertex,*pixel,state,nullptr,3);
 // The receiver inputs leave; the depth returns to the world target's use.
 d3d12_->set_resource(kPrepassRestSlotD3D12,static_cast<const D3D12Texture*>(nullptr));
 d3d12_->set_resource(kPrepassNormalSlotD3D12,static_cast<const D3D12Texture*>(nullptr));
 d3d12_->set_depth_resource(kPrepassDepthSlotD3D12,nullptr);
 if(hr!=S_OK){
  if(hr==S_FALSE)d3d12_->omit("shadow mask (pipeline not ready: unmasked shading)",false);
  return FAILED(hr)?hr:S_FALSE;
 }
 profiler_.add_draw(1,1);profiler_.add_batch();
 d3d12_->set_resource(kShadowMaskSlotD3D12,mask);
 return S_OK;
}

// The history inputs of a pass that reads it (the D3D11 flush's b6, t20,
// t21 and s3): the b6 values prepare_shadow_history just wrote (zero when
// inactive), the previous visibility and the previous world depth.
void Renderer::bind_shadow_history_d3d12(bool active) noexcept{
 d3d12_->set_constants(6,shadow_history_constants12_.data(),UINT(shadow_history_constants12_.size()));
 d3d12_->set_resource(20,active?shadow_history_mask12_[shadow_history_previous_index_].get():nullptr);
 d3d12_->set_depth_resource(21,active?shadow_history_depth12_.get():nullptr);
 D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
 sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
 d3d12_->set_sampler(3,sampler);
}

// The world pass's targets, as the D3D11 flush binds them for world batches:
// the world colour and depth, with history the current shadow-history target
// at slot 1 (until the frame's history is committed), and while AO runs this
// frame the ambient share at slot 2. Every world draw returns to these; the
// world pixel programs write the visibility (SV_Target1) and share (SV_Target2).
void Renderer::bind_world_targets_d3d12(bool history) noexcept{
 // The zone hit-test frame: its target alone (as rtv_ alone on D3D11), not
 // the world's role (no marker colour in the zone mask; hit_test_frame_apply).
 if(hit_test_active_){world_history_bound12_=false;d3d12_->set_target(world_d3d12_.get(),false);return;}
 D3D12Texture* const visibility=history&&!scene_composited_&&!shadow_history_committed_&&
  shadow_history_current_frame_==native_frame_index_?shadow_history_mask12_[shadow_history_current_index_].get():nullptr;
 world_history_bound12_=visibility!=nullptr;
 if(ao12_.frame==native_frame_index_&&ao12_.ambient&&!scene_composited_){
  D3D12Texture* targets[3]={world_d3d12_.get(),visibility,ao12_.ambient.get()};
  d3d12_->set_targets(targets,3,world_d3d12_.get());
 }else if(visibility){
  D3D12Texture* targets[2]={world_d3d12_.get(),visibility};
  d3d12_->set_targets(targets,2,world_d3d12_.get());
 }else d3d12_->set_target(world_d3d12_.get(),true);
}

// update_ambient_occlusion_targets on D3D12: the full-size ambient share and
// the low-resolution depth, raw and filtered AO for the preset's divisor,
// created on demand (a failed size is not retried until the size or preset
// changes); the share is cleared for this world frame.
HRESULT Renderer::update_ambient_occlusion_d3d12(){
 auto& ao=ao12_;
 ao.frame=UINT64_MAX;
 const auto preset=ambient_occlusion_preset(configured_integer(debug_overlay_.config(),"ao_quality",2));
 if(!preset.divisor||!world_d3d12_){ao.ambient.reset();ao.raw.reset();ao.filtered.reset();ao.depth.reset();ao.divisor=0;return S_OK;}
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 const UINT low_width=ambient_occlusion_extent(width,preset.divisor),low_height=ambient_occlusion_extent(height,preset.divisor);
 if(!ao.ambient||ao.divisor!=preset.divisor||!d3d12_->texture_matches(*ao.ambient,width,height,DXGI_FORMAT_R16G16B16A16_FLOAT)){
  if(ao.failed_width==width&&ao.failed_height==height&&ao.failed_divisor==preset.divisor)return E_FAIL;
  ao.ambient.reset();ao.raw.reset();ao.filtered.reset();ao.depth.reset();ao.divisor=0;
  HRESULT hr=S_OK;
  if(FAILED(hr=d3d12_->create_target(width,height,ao.ambient,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_UNKNOWN))||
     FAILED(hr=d3d12_->create_target(low_width,low_height,ao.raw,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_UNKNOWN))||
     FAILED(hr=d3d12_->create_target(low_width,low_height,ao.filtered,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_UNKNOWN))||
     FAILED(hr=d3d12_->create_target(low_width,low_height,ao.depth,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_UNKNOWN))){
   ao.ambient.reset();ao.raw.reset();ao.filtered.reset();ao.depth.reset();
   ao.failed_width=width;ao.failed_height=height;ao.failed_divisor=preset.divisor;
   return hr;
  }
  ao.divisor=preset.divisor;ao.failed_width=ao.failed_height=ao.failed_divisor=0;
 }
 const float zero[4]{};d3d12_->clear_texture(*ao.ambient,zero);
 ao.frame=native_frame_index_;
 return S_OK;
}

// render_ambient_occlusion on D3D12: the same four passes, constants and
// targets (compact low-resolution depth, horizon AO, depth-aware denoise,
// upsample removing the occluded ambient share from the world colour by a
// reverse-subtract blend that keeps its alpha). S_FALSE: no camera this frame.
HRESULT Renderer::ambient_occlusion_d3d12(){
 auto& ao=ao12_;
 if(ao.programs.empty())append_ambient_occlusion_shader_programs(ao.programs); // aoFullscreenVS, prepare, horizon, denoise, resolve
 if(ao.programs.size()<5)return E_FAIL;
 const auto preset=ambient_occlusion_preset(configured_integer(debug_overlay_.config(),"ao_quality",2));
 const auto scene=bridge_scene_world_to_clip_snapshot();
 AmbientOcclusionConstants constants{};
 if(preset.divisor!=ao.divisor||!scene.valid||!ambient_occlusion_invert(scene.world_to_clip.data(),constants.clip_to_world))return S_FALSE;
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 const auto& config=debug_overlay_.config();
 const auto environment=bridge_world_fog_snapshot().environment;
 const float low_light=environment.valid?(std::max)(fog_night(environment.hour),
  environment.cave?0.f:.6f*fog_precipitation(environment)):0.f;
 const auto frame=ambient_occlusion_frame(configured_floating_point(config,"ao_strength",.75f),
  configured_floating_point(config,"ao_radius",1.5f),configured_floating_point(config,"ao_low_light_boost",1.6f),
  native_world_lighting_.ambient.data(),native_world_lighting_.sunlight_diffuse.data(),low_light);
 const UINT low_width=ambient_occlusion_extent(width,ao.divisor),low_height=ambient_occlusion_extent(height,ao.divisor);
 std::memcpy(constants.world_to_clip,scene.world_to_clip.data(),sizeof(constants.world_to_clip));
 const float full[4]={float(width),float(height),1.f/width,1.f/height};
 const float low[4]={float(low_width),float(low_height),1.f/low_width,1.f/low_height};
 std::memcpy(constants.full_size,full,sizeof(full));std::memcpy(constants.low_size,low,sizeof(low));
 const float params[4]={frame.radius,(std::max)(8.f,height*.08f),frame.strength,.03f*ao.divisor};
 std::memcpy(constants.params,params,sizeof(params));
 constants.control[0]=ao.divisor;constants.control[1]=preset.slices;constants.control[2]=preset.steps;
 const auto pass=[&](D3D12Texture* target,const ShaderProgram& pixel,const D3D12Texture* low_input,bool resolve)->HRESULT{
  d3d12_->clear_slots();
  d3d12_->set_constants(1,&constants,sizeof(constants));
  d3d12_->set_depth_resource(0,world_d3d12_.get());
  d3d12_->set_resource(1,low_input);
  d3d12_->set_resource(2,ao.ambient.get());
  d3d12_->set_targets(&target,1,nullptr); // no depth: the world depth is read
  D3D12NativeState state=post_state(false,false);
  if(resolve){ // world = world - occluded share; its alpha kept
   auto& blend=state.custom_blend;blend.custom=1;
   blend.source=blend.destination=D3D11_BLEND_ONE;blend.op=D3D11_BLEND_OP_REV_SUBTRACT;
   blend.source_alpha=D3D11_BLEND_ZERO;blend.destination_alpha=D3D11_BLEND_ONE;blend.op_alpha=D3D11_BLEND_OP_ADD;
  }
  return d3d12_->draw(ao.programs[0],pixel,state,nullptr,3);
 };
 HRESULT hr=S_OK;
 if((hr=pass(ao.depth.get(),ao.programs[1],nullptr,false))==S_OK)
  hr=pass(ao.raw.get(),ao.programs[2],ao.depth.get(),false);
 if(hr==S_OK){
  transition_profile_pass("ambient_occlusion_resolve");
  if((hr=pass(ao.filtered.get(),ao.programs[3],ao.raw.get(),false))==S_OK)
   hr=pass(world_d3d12_.get(),ao.programs[4],ao.filtered.get(),true);
 }
 d3d12_->clear_slots();
 if(hr==S_OK){profiler_.add_draw(4,4);profiler_.add_batch(4);}
 return hr;
}

// Ground marks after the world pass test against the pre-water depth
// snapshot on frames the water reflected (composite_world_d3d12).
bool Renderer::ground_marks_under_water_d3d12() const noexcept{
 return world_d3d12_bound_&&world_d3d12_&&water12_.depth&&water_capture_ready_&&water_capture_frame_==native_frame_index_;
}

// The composite_world effects' shared state on D3D12 (D3D11: their own
// blend/raster objects, rtv_ alone with depth_srv_ at t2, or world_dsv_
// read-only for the waterfall shells; bind_world_fog's b10/t11/s7 and the
// sky probe).
D3D12NativeState Renderer::world_effect_state_d3d12(bool depth_test) const noexcept{
 D3D12NativeState state;
 state.raster.FillMode=D3D11_FILL_SOLID;state.raster.CullMode=D3D11_CULL_NONE;state.raster.DepthClipEnable=TRUE;
 auto& blend=state.custom_blend; // premultiplied: ONE, INV_SRC_ALPHA for colour and alpha
 blend.custom=1;blend.source=blend.source_alpha=D3D11_BLEND_ONE;
 blend.destination=blend.destination_alpha=D3D11_BLEND_INV_SRC_ALPHA;
 if(depth_test){state.depth.DepthEnable=TRUE;state.depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;state.depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;}
 return state;
}
void Renderer::bind_world_effect_d3d12(bool depth_test){
 D3D12Texture* world=world_d3d12_.get();
 d3d12_->clear_slots();
 d3d12_->set_targets(&world,1,depth_test?world:nullptr);
 if(!depth_test)d3d12_->set_depth_resource(2,world);
 d3d12_->set_constants(10,&fog_frame_d3d12_,sizeof(fog_frame_d3d12_));
 bind_world_fog_volume_d3d12(); // t11 while this frame's fog volume ran
 d3d12_->set_resource(kSkyCloudShadowSlotD3D12,sky_inputs_active_?sky12_.shadow.get():nullptr);
 d3d12_->set_resource(kSkyProbeSlotD3D12,sky_inputs_active_?sky12_.probe[sky_index_].get():nullptr);
 D3D11_SAMPLER_DESC linear{};linear.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;linear.MaxLOD=D3D11_FLOAT32_MAX;
 d3d12_->set_sampler(7,linear);
}

// composite_world on the D3D12 backend: the world's queue, then the world
// target copied into the back buffer (D3D11 scene_copy). The steps D3D11
// runs around that copy are named omitted while they are configured on.
HRESULT Renderer::composite_world_d3d12(){
 const HRESULT world=flush_native();
 // This frame's shadow history: the world depth its visibility was drawn
 // with, for the next frame's reprojection (D3D11 commits at the world's
 // depth clear and end_scene, before composite_world). After the flush,
 // so the depth includes every batch that wrote visibility.
 if(world_d3d12_bound_)commit_shadow_history();
 // Neighbour-map liquids last in the world pass (composite_world), after the
 // figures the game submitted.
 if(neighbour_liquids_pending_){
  neighbour_liquids_pending_=false;
  if(neighbour_liquids_frame_index_==native_frame_index_&&set_native_world_clip(neighbour_liquids_clip_)){
   const HRESULT liquids=render_neighbour_maps(neighbour_liquids_state_,neighbour_liquids_clip_,true);
   if(FAILED(liquids))d3d12_->omit("neighbour liquids (their flush failed)",false);
  }
 }
 // Ground marks held back from the world pass (selection, prints) draw over
 // the finished water, as composite_world draws them: on frames the water
 // reflected, against its pre-water depth snapshot (marks on the bed stay
 // visible, unrefracted).
 if(!deferred_ground_marks_.empty()){
  auto marks=std::move(deferred_ground_marks_);deferred_ground_marks_.clear();
  const bool under_water=ground_marks_under_water_d3d12();
  if(under_water){D3D12Texture* world=world_d3d12_.get();d3d12_->set_targets(&world,1,water12_.depth.get());}
  drawing_ground_marks_=true;
  for(auto& mark:marks)if(FAILED(mark())){d3d12_->omit("ground marks (a draw failed)",false);break;}
  drawing_ground_marks_=false;
  if(under_water)bind_world_targets_d3d12();
 }
 // The occlusion pyramid describes the world pass only (composite_world's
 // rule): the interface 3D after this point (HUD spell and skill icons,
 // previews) must not be culled against the world's depth.
 native_hiz_frame_=UINT64_MAX;native_world_clip_dirty_=true;
 // composite_world's world-space effects before the history copy that are
 // not on this backend yet: named while configured on, never silently absent.
 if(world_d3d12_bound_&&world_d3d12_){
  // AO needs the finished world depth and ambient share and precedes SMAA
  // and every interface pass (the world targets bound again after it).
  if(ao12_.frame==native_frame_index_){
   transition_profile_pass("ambient_occlusion");
   const HRESULT ao=ambient_occlusion_d3d12();
   if(FAILED(ao))d3d12_->omit("screen-space ambient occlusion (pass failed)",false);
   else if(ao==S_OK)d3d12_->count("ambient occlusion passes");
   else d3d12_->count("ambient occlusion frames without a camera or pipeline");
   bind_world_targets_d3d12();
  }
  // The world effects composite_world draws before the history copy, in its
  // order; each binds what it reads and returns the world target bound.
  render_ambient_particles();
  render_wind_trails();
  render_precipitation();
 }
 // The finished world colour for the next frame's reflections.
 if(world_d3d12_bound_&&world_d3d12_)capture_water_history();
 d3d12_->set_target(nullptr);
 scene_composited_=true;
 // Selection rectangles requested after the world pass and before this
 // composite: the first interface output (composite_world's order), on the
 // back buffer, after the HUD-less copy.
 const auto draw_pending_rectangles=[this]{
  for(const auto& pending:pending_screen_rectangles_)
   if(FAILED(draw_screen_rectangle_d3d12(pending.request,pending.viewport)))d3d12_->omit("selection rectangle (draw failed)",false);
  pending_screen_rectangles_.clear();
 };
 if(!world_d3d12_bound_||!world_d3d12_){
  draw_pending_rectangles();
  const HRESULT dimmed=dim_hud_layout_d3d12();
  transition_profile_pass("ui_composition");return FAILED(world)?world:dimmed;
 }
 world_d3d12_bound_=false;
 const auto& config=debug_overlay_.config();
 // The world image to the output: SMAA 1x or the plain copy (SceneComposite::render);
 // with a temporal upscaler the plain copy into the render-size resolved
 // image (the upscalers anti-alias themselves), and the post passes over it.
 const bool smaa=!temporal_upscaler()&&configured_boolean(config,"smaa",true);
 transition_profile_pass(smaa?"scene_smaa":"scene_copy");
 const HRESULT copied=resolve_world_d3d12(smaa);
 if(SUCCEEDED(copied))d3d12_->count(smaa?"world composites anti-aliased (SMAA)":"world composites copied");
 // Depth of field, sun rays and bloom over the resolved image, as composite_world orders them.
 const auto post=[&](const char* name,HRESULT result){
  if(result==S_OK)d3d12_->count(name);
  else if(FAILED(result))d3d12_->omit(name,false); // logged once with the pass named
 };
 if(const int dof=configured_integer(config,"depth_of_field",0);dof>0&&fog_prepared_frame_==native_frame_index_*2u){
  const auto clip=bridge_scene_world_to_clip_snapshot();float clip_to_world[16];
  if(clip.valid&&ambient_occlusion_invert(clip.world_to_clip.data(),clip_to_world)){
   transition_profile_pass("scene_dof");
   const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
   post("scene_dof",depth_of_field_d3d12(dof,configured_floating_point(config,"depth_of_field_strength",.2f),clip_to_world,seconds));
  }
 }
 if(sun_rays_frame_==native_frame_index_){
  transition_profile_pass("scene_sun_rays");
  post("scene_sun_rays",sun_rays_d3d12());
 }
 // Only frames that draw a map's terrain (the D3D11 rule): the global map
 // screen is no world scene and is not bloomed whole.
 if(const int bloom=configured_integer(config,"bloom",0);bloom>0&&ambient_terrain_frame_==native_frame_index_){
  transition_profile_pass("scene_bloom");
  post("scene_bloom",bloom_d3d12(bloom,configured_floating_point(config,"bloom_strength",1.f),
   bloom_ceiling(fog_prepared_frame_==native_frame_index_*2u?bloom_fog_headroom_:0.f),
   configured_floating_point(config,"bloom_threshold",1.f),configured_boolean(config,"bloom_debug_view",false)));
 }
 // The upscaler (or, failing it this frame, the filtered upscale) into the
 // output; below 100% render scale without one, the filtered upscale
 // (composite_world's scene_upscale).
 const HRESULT upscaled=!scene_output_d3d12()?S_OK:temporal_upscaler()?upscale_world_d3d12():filtered_upscale_d3d12();
 d3d12_->clear_slots();d3d12_->set_target(nullptr);
 world_output_this_present_=true;
 capture_hudless_d3d12();
 draw_pending_rectangles();
 const HRESULT dimmed=dim_hud_layout_d3d12();
 transition_profile_pass("ui_composition");
 return FAILED(world)?world:FAILED(copied)?copied:FAILED(upscaled)?upscaled:dimmed;
}

// ---- frame_generation (DLSS-G) ----
namespace{
const char* const kFgLowRate="rendered rate below frame_generation_min_fps (auto-off, fg_auto_off)";
// Our matrices act on column vectors (m * v), stored row by row.
void fg_multiply(const float* a,const float* b,float* out) noexcept{
 for(int r=0;r<4;++r)for(int c=0;c<4;++c){float sum=0;for(int k=0;k<4;++k)sum+=a[r*4+k]*b[k*4+c];out[r*4+c]=sum;}
}
// NGX takes row-major matrices for row vectors (v * M): the transpose.
void fg_transposed(const float* in,float* out) noexcept{for(int r=0;r<4;++r)for(int c=0;c<4;++c)out[c*4+r]=in[r*4+c];}
bool fg_unproject(const float* clip_to_world,float x,float y,float z,float w,float out[3]) noexcept{
 const float clip[4]={x,y,z,w};float world[4]{};
 for(int r=0;r<4;++r)for(int c=0;c<4;++c)world[r]+=clip_to_world[r*4+c]*clip[c];
 if(std::fabs(world[3])<1e-12f)return false;
 for(int i=0;i<3;++i)out[i]=world[i]/world[3];
 return true;
}
float fg_length(const float v[3]) noexcept{return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);}
}

// The camera block from the scene's world-to-clip matrices, the same ones
// the DLSS SR motion pass uses (not a separate view/projection record):
// the eye is the clip point (0,0,1,0) unprojected, the basis and planes come
// from the unprojected screen centre and edges. False: no perspective camera.
static bool fg_camera(const float* clip_to_world,const float* world_to_clip,x64remote::DlssgEvaluate& e) noexcept{
 float eye[3],near_c[3],far_c[3],right_c[3],up_c[3];
 if(!fg_unproject(clip_to_world,0,0,1,0,eye)||!fg_unproject(clip_to_world,0,0,0,1,near_c)||!fg_unproject(clip_to_world,0,0,1,1,far_c)||
    !fg_unproject(clip_to_world,1,0,0,1,right_c)||!fg_unproject(clip_to_world,0,1,0,1,up_c))return false;
 float forward[3],right[3],up[3],to_near[3],to_far[3];
 for(int i=0;i<3;++i){forward[i]=far_c[i]-near_c[i];right[i]=right_c[i]-near_c[i];up[i]=up_c[i]-near_c[i];to_near[i]=near_c[i]-eye[i];to_far[i]=far_c[i]-eye[i];}
 const float lf=fg_length(forward),half_w=fg_length(right),half_h=fg_length(up),near_d=fg_length(to_near),far_d=fg_length(to_far);
 if(lf<=0||half_w<=0||half_h<=0||near_d<=0||far_d<=near_d)return false;
 for(int i=0;i<3;++i){forward[i]/=lf;right[i]/=half_w;up[i]/=half_h;}
 std::memcpy(e.position,eye,sizeof(eye));std::memcpy(e.forward,forward,sizeof(forward));
 std::memcpy(e.right,right,sizeof(right));std::memcpy(e.up,up,sizeof(up));
 e.near_plane=near_d;e.far_plane=far_d;e.fov_y=2.f*std::atan(half_h/near_d);e.aspect=half_w/half_h;
 // View space: right, up, forward at the eye (left-handed, +z forward).
 const float view_to_world[16]={right[0],up[0],forward[0],eye[0], right[1],up[1],forward[1],eye[1],
                                right[2],up[2],forward[2],eye[2], 0,0,0,1};
 float world_to_view[16],view_to_clip[16],clip_to_view[16];
 if(!ambient_occlusion_invert(view_to_world,world_to_view))return false;
 fg_multiply(world_to_clip,view_to_world,view_to_clip);
 fg_multiply(world_to_view,clip_to_world,clip_to_view);
 fg_transposed(view_to_clip,e.view_to_clip);fg_transposed(clip_to_view,e.clip_to_view);
 return true;
}

// One generated frame between the previous real frame and this one: the
// frame image (world and interface) as the backbuffer, the HUD-less copy,
// and the upscaler's render-size world depth and motion (camera and object
// motion, current to previous, in render pixels: DLSS-FG guide, scale 1).
// Recorded before the frame image is composited. False: none this present.
bool Renderer::evaluate_frame_generation_d3d12() noexcept{
 D3D12Texture* const frame=d3d12_->frame_image_texture();
 D3D12Texture* const output=d3d12_->generated_image();
 const char* const skipped=fg_not_game_frame_?"not a game frame (fg_game_frame)":fg_low_rate_?kFgLowRate:!d3d12_->frame_generation_active(1)||!frame||!output?"the presenter is not in this mode yet":
  !hudless_this_present_||!hudless_d3d12_?"no HUD-less copy this present":!fg_upscaler_valid_?"no upscaler camera frame this present (no scene camera or the upscaler did not run)":
  !world_d3d12_||!upscale12_.motion?"no world depth or upscaler motion":nullptr;
 note_frame_generation(skipped);
 if(skipped){fg_previous_generated_=false;return false;}
 x64remote::DlssgEvaluate e{};
 e.width=output_width_;e.height=output_height_;e.render_w=width_;e.render_h=height_;
 float world_to_clip[16],previous_inverse[16],clip_to_prev[16],prev_to_clip[16];
 std::memcpy(world_to_clip,fg_world_to_clip_.data(),sizeof(world_to_clip));
 if(!fg_camera(fg_clip_to_world_,world_to_clip,e)||!ambient_occlusion_invert(fg_previous_clip_.data(),previous_inverse)){
  fg_previous_generated_=false;d3d12_->count("frame generation skipped (no perspective camera)");return false;
 }
 fg_multiply(fg_previous_clip_.data(),fg_clip_to_world_,clip_to_prev);
 fg_multiply(world_to_clip,previous_inverse,prev_to_clip);
 fg_transposed(clip_to_prev,e.clip_to_prev_clip);fg_transposed(prev_to_clip,e.prev_clip_to_clip);
 // The upscaler's pixel jitter as a clip offset (x64_remote_link.h DlssEvaluate).
 e.jitter_x=2.f*fg_jitter_[0]/float(std::max(1u,width_));e.jitter_y=-2.f*fg_jitter_[1]/float(std::max(1u,height_));
 e.mv_scale_x=e.mv_scale_y=1.f;
 e.reset=fg_upscaler_reset_||!fg_previous_generated_;
 e.depth_inverted=false;
 transition_profile_pass("fg_dlssg");
 d3d12_->clear_slots();
 const D3D12Frame::ExternalUse uses[5]={{frame,false,false},{world_d3d12_.get(),true,false},{upscale12_.motion.get(),false,false},
                                        {hudless_d3d12_.get(),false,false},{output,false,true}};
 const bool first=!fg_previous_generated_&&!fg_stats_tick_;
 const HRESULT hr=d3d12_->record_external("dlss-g",uses,5,[&](ID3D12GraphicsCommandList* list,ID3D12Resource* const* resources){
#ifdef EID3D11_EMBEDDED
  return embedded_d3d12_.fg_evaluate(list,resources,e);
#else
  return x64remote::dlssg_evaluate_d3d12(list,resources,e);
#endif
 });
 transition_profile_pass("present_bookkeeping");
 if(FAILED(hr)){
  char reason[120];std::snprintf(reason,sizeof(reason),
#ifdef EID3D11_EMBEDDED
   "DLSS Frame Generation failed (0x%08lX); see engine journal"
#else
   "DLSS Frame Generation failed (0x%08lX); see host.log"
#endif
   ,static_cast<unsigned long>(hr));
  frame_generation_off(1,reason);return false;
 }
 if(first){
#ifdef EID3D11_EMBEDDED
  const std::uint64_t bytes=embedded_d3d12_.fg_vram(output_width_,output_height_,width_,height_);
#else
  const std::uint64_t bytes=x64remote::dlssg_vram_estimate(output_width_,output_height_,width_,height_);
#endif
  char text[160];std::snprintf(text,sizeof(text),"DLSS Frame Generation on: %ux%u, depth/motion %ux%u; NGX VRAM estimate %.1f MiB",
   output_width_,output_height_,width_,height_,double(bytes)/(1024.0*1024.0));
  bridge_report_event("frame_generation",text);
 }
 fg_previous_generated_=true;
 d3d12_->count("frame generation evaluations");
 profiler_.add_draw(1,1);profiler_.add_batch();
 return true;
}

// frame_generation_min_fps: pauses generation while the rendered rate stays
// low and resumes with hysteresis (FidelityFX frame interpolation swap chain
// guide, "Minimum Framerate": disable it when the rate consistently drops
// below the minimum; revert only when the rate changes by more than what
// disabling it explains). Only intervals between two game frames count, and
// stalls over 200 ms are left out (a load or hitch is no low rate).
void Renderer::update_frame_generation_auto_off(int output_limit) noexcept{
 const int min_fps=frame_generation_mode_?configured_integer(debug_overlay_.config(),"frame_generation_min_fps",30):0;
 if(min_fps<=0){
  if(fg_low_rate_)bridge_report_event("fg_auto_off","frame generation resumed: auto-off disabled or frame generation off");
  fg_low_rate_=fg_rate_previous_game_=false;fg_low_rate_windows_=0;fg_rate_window_ms_=0.0;fg_rate_window_count_=0;fg_rate_cost_ms_=-1.0;
  return;
 }
 // A total-output cap cannot sustain more than half that rate while
 // generating. Keep the user's minimum guard, and do not repeatedly resume
 // from the faster one-present rate while the same cap still forbids it.
 if(output_limit>0&&double(output_limit)*.5<min_fps){
  if(!fg_low_rate_){char text[192];std::snprintf(text,sizeof(text),
   "frame generation paused: output cap %d FPS allows less than frame_generation_min_fps %d rendered FPS",output_limit,min_fps);
   bridge_report_event("fg_auto_off",text);}
  fg_low_rate_=true;fg_rate_previous_game_=false;fg_low_rate_windows_=0;
  fg_rate_window_ms_=0;fg_rate_window_count_=0;fg_rate_cost_ms_=0;
  return;
 }
 const bool game=!fg_not_game_frame_;
 const bool sample=game&&fg_rate_previous_game_&&present_interval_ms_>0.0&&present_interval_ms_<200.0;
 fg_rate_previous_game_=game;
 if(!sample)return;
 fg_rate_window_ms_+=present_interval_ms_;++fg_rate_window_count_;
 if(fg_rate_window_ms_<1000.0)return;
 const double mean=fg_rate_window_ms_/double(fg_rate_window_count_);
 fg_rate_window_ms_=0.0;fg_rate_window_count_=0;
 const double target_ms=1000.0/double(min_fps);
 char text[220];
 if(!fg_low_rate_){
  if(mean<=target_ms){fg_low_rate_windows_=0;return;}
  if(++fg_low_rate_windows_<2)return;
  fg_low_rate_=true;fg_low_rate_windows_=0;fg_rate_on_ms_=mean;fg_rate_cost_ms_=-1.0;
  std::snprintf(text,sizeof(text),"present %llu: frame generation paused: rendered %.1f FPS (%.2f ms) below frame_generation_min_fps %d for 2 s",
   static_cast<unsigned long long>(presentation_frame_index_),1000.0/mean,mean,min_fps);
  bridge_report_event("fg_auto_off",text);
  return;
 }
 if(fg_rate_cost_ms_<0.0){
  // The first window off: what generating cost (bounded to a quarter of
  // the frame, so a scene change at the switch cannot block resuming).
  fg_rate_cost_ms_=std::clamp(fg_rate_on_ms_-mean,0.0,0.25*fg_rate_on_ms_);
  std::snprintf(text,sizeof(text),"present %llu: frame generation off: rendered %.2f ms (on: %.2f ms); generating counted as %.2f ms",
   static_cast<unsigned long long>(presentation_frame_index_),mean,fg_rate_on_ms_,fg_rate_cost_ms_);
  bridge_report_event("fg_auto_off",text);
  return;
 }
 if((output_limit>0&&double(output_limit)*.5<double(min_fps)*1.1)||
    (mean+fg_rate_cost_ms_)*1.1>target_ms){fg_low_rate_windows_=0;return;}
 if(++fg_low_rate_windows_<3)return;
 fg_low_rate_=false;fg_low_rate_windows_=0;
 std::snprintf(text,sizeof(text),"present %llu: frame generation resumed: rendered %.1f FPS off (%.1f with generating) clears frame_generation_min_fps %d by 10%% for 3 s",
  static_cast<unsigned long long>(presentation_frame_index_),1000.0/mean,1000.0/(mean+fg_rate_cost_ms_),min_fps);
 bridge_report_event("fg_auto_off",text);
}

// Why frame generation did or did not generate from this present, one
// event on each change (the no-perspective-camera skip is counted instead).
void Renderer::note_frame_generation(const char* skipped) noexcept{
 if(skipped==fg_skip_logged_)return;
 fg_skip_logged_=skipped;
 char text[200];std::snprintf(text,sizeof(text),"present %llu: %s%s",static_cast<unsigned long long>(presentation_frame_index_),
  skipped?"not generated: ":"generating",skipped?skipped:"");
 bridge_report_event("fg_generation",text);
}

// A failure turns that frame generation mode off for the session: one log
// line, the F10 note on the option (blocked_option_note), the feature
// released (DLSS-G here; FSR's context goes with the proxy swap chain).
void Renderer::frame_generation_off(int mode,const char* reason) noexcept{
 if(mode<1||mode>2)return;
 std::snprintf(fg_failed_[mode],sizeof(fg_failed_[mode]),"%s",reason&&reason[0]?reason:"failed");
 if(frame_generation_mode_==mode){frame_generation_mode_=0;set_frame_generation_unavailable_reason(fg_failed_[mode]);fg_reason_mode_=mode;}
 char text[240];std::snprintf(text,sizeof(text),"%s: %s",mode==1?"DLSS Frame Generation":"FSR frame generation",fg_failed_[mode]);
 bridge_report_event("frame_generation_failed",text);
 if(mode==1){fg_previous_generated_=false;
#ifdef EID3D11_EMBEDDED
  embedded_d3d12_.fg_release();
#else
  x64remote::dlssg_release();
#endif
 }
 else fsr_prepared_=false;
 if(d3d12_&&frame_generation_mode_==0)d3d12_->set_frame_generation(0);
}

// FSR frame generation (mode 2): FidelityFX's prepare for this frame on the
// frame's list (the upscaler's render-size depth and motion, current to
// previous in render pixels: scale 1; the same camera block as DLSS-G).
// The proxy swap chain interpolates inside Present from the back buffer and
// the HUD-less image (set_fsr_hudless). False: no generation this present.
bool Renderer::prepare_fsr_frame_generation_d3d12() noexcept{
 const char* const skipped=fg_not_game_frame_?"not a game frame (fg_game_frame)":fg_low_rate_?kFgLowRate:!d3d12_->frame_generation_active(2)?"the presenter is not in this mode yet":
  !hudless_this_present_||!hudless_d3d12_?"no HUD-less copy this present":!fg_upscaler_valid_?"no upscaler camera frame this present (no scene camera or the upscaler did not run)":
  !world_d3d12_||!upscale12_.motion?"no world depth or upscaler motion":nullptr;
 note_frame_generation(skipped);
 if(skipped){fg_previous_generated_=false;return false;}
 x64remote::DlssgEvaluate e{};
 float world_to_clip[16];
 std::memcpy(world_to_clip,fg_world_to_clip_.data(),sizeof(world_to_clip));
 if(!fg_camera(fg_clip_to_world_,world_to_clip,e)){
  fg_previous_generated_=false;d3d12_->count("frame generation skipped (no perspective camera)");return false;
 }
 D3D12Frame::FsrPrepare p{};
 p.render_w=width_;p.render_h=height_;
 p.jitter_x=fg_jitter_[0];p.jitter_y=fg_jitter_[1];  // the upscaler's, as FSR takes it
 const std::int64_t now=perf_now();
 p.frame_ms=fsr_prepare_tick_?float(double(now-fsr_prepare_tick_)*1000.0/double(perf_frequency())):16.7f;
 fsr_prepare_tick_=now;
 p.reset=fg_upscaler_reset_||!fg_previous_generated_;
 p.near_plane=e.near_plane;p.far_plane=e.far_plane;p.fov_y=e.fov_y;
 std::memcpy(p.position,e.position,sizeof(p.position));std::memcpy(p.up,e.up,sizeof(p.up));
 std::memcpy(p.right,e.right,sizeof(p.right));std::memcpy(p.forward,e.forward,sizeof(p.forward));
 transition_profile_pass("fg_fsr_prepare");
 d3d12_->clear_slots();
 const D3D12Frame::ExternalUse uses[2]={{world_d3d12_.get(),true,false},{upscale12_.motion.get(),false,false}};
 const bool first=!fg_previous_generated_&&!fg_stats_tick_;
 const HRESULT hr=d3d12_->record_external("fsr-fg prepare",uses,2,[&](ID3D12GraphicsCommandList* list,ID3D12Resource* const* resources){
  return d3d12_->fsr_prepare(list,resources[0],resources[1],p);
 });
 transition_profile_pass("present_bookkeeping");
 if(FAILED(hr)){
#ifdef EID3D11_EMBEDDED
  frame_generation_off(2,"FidelityFX frame generation prepare failed; see engine journal");
#else
  frame_generation_off(2,"FidelityFX frame generation prepare failed; see host.log");
#endif
  return false;
 }
 if(first){
  char text[160];std::snprintf(text,sizeof(text),"FSR frame generation on: %ux%u, depth/motion %ux%u; FidelityFX context VRAM %.1f MiB",
   output_width_,output_height_,width_,height_,double(d3d12_->fsr_vram())/(1024.0*1024.0));
  bridge_report_event("frame_generation",text);
 }
 fg_previous_generated_=true;
 d3d12_->count("frame generation prepares (FSR)");
 return true;
}

// FSR: the HUD-less image as presented colours: itself under an identity
// gamma ramp, else a gamma-mapped copy (the back buffer carries the ramp,
// and FidelityFX takes every difference from it as interface). Null: none.
D3D12Texture* Renderer::fsr_hudless_d3d12() noexcept{
 if(!hudless_d3d12_)return nullptr;
 {std::lock_guard<std::mutex> lock(gamma_lock_);if(gamma_identity_)return hudless_d3d12_.get();}
 if(!hudless_gamma_d3d12_||!d3d12_->texture_matches(*hudless_gamma_d3d12_,output_width_,output_height_,DXGI_FORMAT_B8G8R8A8_UNORM)){
  hudless_gamma_d3d12_.reset();
  if(FAILED(d3d12_->create_target(output_width_,output_height_,hudless_gamma_d3d12_,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_UNKNOWN))){
   hudless_gamma_d3d12_.reset();d3d12_->omit("FSR frame generation HUD-less gamma copy (texture creation failed)",false);return nullptr;
  }
 }
 if(apply_gamma_ramp_d3d12(hudless_d3d12_.get(),hudless_gamma_d3d12_.get())!=S_OK){
  d3d12_->omit("FSR frame generation HUD-less gamma copy (pass failed)",false);return nullptr;
 }
 return hudless_gamma_d3d12_.get();
}

// Presents per second from the pacer's counts, every half second (measured,
// never assumed 2x).
void Renderer::update_frame_generation_rates() noexcept{
 if(!d3d12_)return;
 const auto stats=d3d12_->frame_generation_stats();
 const std::int64_t now=perf_now();
 if(!stats.active){fg_stats_tick_=0;fg_presented_fps_=fg_real_fps_=0.0;return;}
 if(!fg_stats_tick_){fg_stats_tick_=now;fg_stats_real_=stats.real_presents;fg_stats_generated_=stats.generated_presents;return;}
 const double seconds=double(now-fg_stats_tick_)/double(perf_frequency());
 if(seconds<0.5)return;
 const double real=double(stats.real_presents-fg_stats_real_),generated=double(stats.generated_presents-fg_stats_generated_);
 fg_real_fps_=real/seconds;fg_presented_fps_=(real+generated)/seconds;
 fg_stats_tick_=now;fg_stats_real_=stats.real_presents;fg_stats_generated_=stats.generated_presents;
}

// fg_frame_image: the finished world in the frame image (upscaled and
// post-processed, before the HUD-editor dim and any interface), copied as
// frame generation's HUD-less input. One full-screen copy a world frame.
void Renderer::capture_hudless_d3d12() noexcept{
 if(!fg_frame_image_)return;
 transition_profile_pass("fg_hudless_copy");
 if(!hudless_d3d12_||!d3d12_->texture_matches(*hudless_d3d12_,output_width_,output_height_,DXGI_FORMAT_B8G8R8A8_UNORM)){
  hudless_d3d12_.reset();
  if(FAILED(d3d12_->create_texture(output_width_,output_height_,DXGI_FORMAT_B8G8R8A8_UNORM,hudless_d3d12_))){
   hudless_d3d12_.reset();d3d12_->omit("frame generation HUD-less copy (texture creation failed)",false);return;
  }
 }
 const HRESULT hr=d3d12_->capture(*hudless_d3d12_,0,0,RECT{0,0,LONG(output_width_),LONG(output_height_)});
 if(hr==S_OK){hudless_this_present_=true;d3d12_->count("frame generation HUD-less copies");}
 else if(FAILED(hr))d3d12_->omit("frame generation HUD-less copy (capture failed)",false);
}

// The HUD layout editor dims the composited world under its grid (the end
// of the D3D11 composite_world), before every interface pass.
HRESULT Renderer::dim_hud_layout_d3d12(){
 if(!debug_overlay_.hud_editing())return S_OK;
 transition_profile_pass("hud_layout_dim");
 const ScreenRectangleRequest dim{0,0,int(output_width_),int(output_height_),120,false};
 const D3DVIEWPORT7 viewport{0,0,output_width_,output_height_,0.f,1.f};
 return draw_screen_rectangle_d3d12(dim,viewport,debug_overlay_.hud_grid_step());
}

// SceneComposite::render on D3D12: SMAA 1x (edges, weights, then the
// neighbourhood blend into the output) or copyPS, with the composite's own
// programs, lookup textures, targets and samplers; the output is the back
// buffer, or with a temporal upscaler the render-size resolved image.
HRESULT Renderer::resolve_world_d3d12(bool smaa){
 auto& post=post12_;
 D3D12Texture* const output=scene_output_d3d12();
 if(post.programs.empty())SceneComposite::shader_programs(post.programs); // sceneVS, edge, weight, blend, copy, ...
 if(post.programs.size()<5){d3d12_->omit("scene composite programs (SMAA library missing)",true);return S_OK;}
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 HRESULT hr=S_OK;
 if(smaa){
  if(!post.area){
   D3D12TextureRef area,search;
   if(FAILED(hr=d3d12_->create_texture(AREATEX_WIDTH,AREATEX_HEIGHT,DXGI_FORMAT_R8G8_UNORM,area))||
      FAILED(hr=d3d12_->write_texture(*area,RECT{0,0,AREATEX_WIDTH,AREATEX_HEIGHT},areaTexBytes,AREATEX_PITCH))||
      FAILED(hr=d3d12_->create_texture(SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT,DXGI_FORMAT_R8_UNORM,search))||
      FAILED(hr=d3d12_->write_texture(*search,RECT{0,0,SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT},searchTexBytes,SEARCHTEX_PITCH)))return hr;
   post.area=std::move(area);post.search=std::move(search);
  }
  if(!post.edges||!d3d12_->texture_matches(*post.edges,width,height,DXGI_FORMAT_R8G8_UNORM)){
   post.edges.reset();post.weights.reset();
   if(FAILED(hr=d3d12_->create_target(width,height,post.edges,DXGI_FORMAT_R8G8_UNORM,DXGI_FORMAT_UNKNOWN))||
      FAILED(hr=d3d12_->create_target(width,height,post.weights,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_UNKNOWN)))return hr;
  }
 }
 const float metrics[4]={1.f/float(width),1.f/float(height),float(width),float(height)};
 D3D11_SAMPLER_DESC linear{};linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
 linear.MaxLOD=0;linear.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
 D3D11_SAMPLER_DESC point=linear;point.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
 D3D12NativeState state; // opaque, no depth; the D3D11 default raster
 state.raster.FillMode=D3D11_FILL_SOLID;state.raster.CullMode=D3D11_CULL_BACK;state.raster.DepthClipEnable=TRUE;
 const auto pass=[&](D3D12Texture* target,const ShaderProgram& pixel,const D3D12Texture* a,const D3D12Texture* b,const D3D12Texture* c)->HRESULT{
  d3d12_->clear_slots();
  d3d12_->set_constants(0,metrics,sizeof(metrics));
  d3d12_->set_sampler(0,linear);d3d12_->set_sampler(1,point);
  d3d12_->set_resource(0,a);d3d12_->set_resource(1,b);d3d12_->set_resource(2,c);
  d3d12_->set_target(target);
  return d3d12_->draw(post.programs[0],pixel,state,nullptr,3);
 };
 if(smaa){
  const float zero[4]{};d3d12_->clear_texture(*post.edges,zero);
  if((hr=pass(post.edges.get(),post.programs[1],world_d3d12_.get(),nullptr,nullptr))==S_OK)
   hr=pass(post.weights.get(),post.programs[2],post.edges.get(),post.area.get(),post.search.get());
  if(hr==S_OK)hr=pass(output,post.programs[3],world_d3d12_.get(),post.weights.get(),nullptr);
 }else hr=pass(output,post.programs[4],world_d3d12_.get(),nullptr,nullptr);
 d3d12_->clear_slots();d3d12_->set_target(nullptr);
 if(d3d12_not_done(hr)){ // a program still compiling (or a ring exhausted): the plain copy through the interface program
  if(hr==S_FALSE)d3d12_->count("world composites waiting for their pipeline");
  const float w=float(output?width:output_width_),h=float(output?height:output_height_);
  const Vertex quad[4]={{0,0,0,1,0xffffffffu,0xffffffffu,0,0},{w,0,0,1,0xffffffffu,0xffffffffu,1,0},
                        {0,h,0,1,0xffffffffu,0xffffffffu,0,1},{w,h,0,1,0xffffffffu,0xffffffffu,1,1}};
  Constants block{};block.viewport[2]=w;block.viewport[3]=h;
  block.texture_factor[0]=block.texture_factor[1]=block.texture_factor[2]=block.texture_factor[3]=1.0f;
  block.transformed=TRUE;block.textured=TRUE;
  block.color_op=D3DTOP_SELECTARG1;block.color_arg1=D3DTA_TEXTURE;block.color_arg2=D3DTA_CURRENT;
  block.alpha_op=D3DTOP_SELECTARG1;block.alpha_arg1=D3DTA_TEXTURE;block.alpha_arg2=D3DTA_CURRENT;
  block.alpha_func=D3DCMP_ALWAYS;block.hud_opacity=1.0f;
  D3D12UiDraw draw;draw.texture=world_d3d12_.get();draw.program=D3D12UiProgram::game;
  draw.blend=fixed_blend_choice(FALSE,D3DBLEND_ONE,D3DBLEND_ZERO,false);
  draw.sampler=point;
  draw.first_vertex=0;draw.vertex_count=4;
  D3D12UiPass ui;
  ui.vertices=quad;ui.vertex_bytes=sizeof(quad);ui.vertex_stride=sizeof(Vertex);
  ui.constants=&block;ui.constant_block_bytes=sizeof(Constants);ui.constant_blocks=1;
  ui.draws=&draw;ui.draw_count=1;
  d3d12_->set_target(output);
  const HRESULT drawn=d3d12_->draw_ui(ui);
  d3d12_->set_target(nullptr);
  return drawn;
 }
 return hr;
}

// flush_native on the D3D12 backend: the same record preparation, then the
// build dispatches and the interface batches through the executor.
HRESULT Renderer::flush_native_d3d12(unsigned layout_slot){
 if(native_shadow_collecting_)return flush_native_shadows(); // the sun maps (flush_native_shadows_d3d12)
 // A reused static bake draws with an empty queue (flush_native_static_set).
 // Taken once: any flush nested in this one is an ordinary flush.
 const bool static_reuse=native_static_reuse_,static_bake=native_static_bake_;
 native_static_reuse_=native_static_bake_=false;
 if(native_pending_draws_.empty()&&!static_reuse){discard_native();return S_OK;}
 struct Discard{Renderer& renderer;~Discard(){renderer.discard_native();}} discard{*this};
 // The static object (2), terrain (3) and neighbour opaque (4) sets draw.
 if(layout_slot>=5&&layout_slot<kMapOpaqueLayout){
  d3d12_->omit("native static sets",true);
  return S_OK;
 }
 if(FAILED(native_d3d12_ensure())){d3d12_->omit("native packets (native resources failed)",false);return E_FAIL;}
 // Material classes (S1): profiles of newly composed atlases, then every
 // record's profile index once the set changed; profiles of retired textures go.
 prepare_material_profiles();refresh_material_profile_indices_d3d12();trim_material_profiles();
 auto& layout=native_submission_layouts_[layout_slot];
 // The static sets (the D3D11 flush's rule): an unchanged set reuses its
 // bake -- the layout, work and per-batch totals of the frame that built it
 // (flush_native_static_set checked its keys); any other use of a static
 // layout rebuilds it, so its bake stops being valid first.
 const NativeStaticSet* static_set=native_static_set_for_layout(layout_slot);
 auto& bake=layout.static_bake;
 HRESULT hr=S_OK;
 if(!static_reuse){
  if(static_set)bake.set_revision=bake.content_revision=0;
  const bool static_order=static_set&&layout.static_revision==static_set->revision&&
   layout.source_revision==native_layout_revision_&&layout.source_draws.size()==native_pending_draws_.size();
  if(static_order)native_draw_order_=layout.source_draws;
  else try{order_native_groups(false);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  if(FAILED(hr=prepare_native_submission_layout(false,layout_slot)))return hr;
  layout.contact_generation=native_contact_generation_;
  if(static_set)layout.static_revision=static_set->revision;
 }
 if(layout.work.empty())return S_OK;
 if(!static_reuse){
  refresh_native_draw_work(layout,false);
  if(static_set&&static_bake&&FAILED(hr=bake_native_static_layout(layout)))return hr;
 }
 // Every queued native batch uses the shared build and draw programs,
 // including world particle meshes with their captured blend and depth state.
 // A world flush with work: the frame has world output (the D3D11 flush's
 // rule; the upscalers start their history over on frames without it).
 if(!scene_composited_)scene_updated_=true;
 const auto& programs=native12_.programs;
 const UINT draw_count=static_cast<UINT>(layout.work.size());
 // Frame buffers: dynamic bytes, the work list, the arguments (written by the
 // build dispatch from the batches' starting values) and its outputs.
 if(FAILED(hr=native_d3d12_buffer(native12_.dynamic,native_pending_upload_.size(),0,0))||
    FAILED(hr=native_d3d12_buffer(native12_.work,UINT64(draw_count)*sizeof(NativeDrawWorkGpu),sizeof(NativeDrawWorkGpu),0))||
    FAILED(hr=native_d3d12_buffer(native12_.arguments,UINT64(layout.arguments.size())*sizeof(UINT),0,kD3D12BufferUnordered|kD3D12BufferIndirect))||
    FAILED(hr=native_d3d12_buffer(native12_.visible,UINT64((std::max)(1u,layout.triangles))*16u,16u,kD3D12BufferUnordered))||
    FAILED(hr=native_d3d12_buffer(native12_.prepared,UINT64((std::max)(1u,layout.components))*sizeof(NativePreparedVertexGpu),sizeof(NativePreparedVertexGpu),kD3D12BufferUnordered))||
    FAILED(hr=native_d3d12_persistent(native12_.world_vertices,UINT64(sizeof(NativeWorldVertexGpu))*64u,sizeof(NativeWorldVertexGpu),0))||
    FAILED(hr=ensure_native_world_states())||
    FAILED(hr=native_d3d12_persistent(native12_.world_states,UINT64(sizeof(NativeWorldStateGpu))*64u,sizeof(NativeWorldStateGpu),0))||
    FAILED(hr=native_d3d12_persistent(native12_.animation,64u,0,0))||
    FAILED(hr=native_d3d12_persistent(native12_.pose_nodes,UINT64(sizeof(NativePoseNodeGpu))*16u,sizeof(NativePoseNodeGpu),0)))
  return hr;
 if(!native_pending_upload_.empty()&&FAILED(hr=d3d12_->write_buffer(*native12_.dynamic,0,native_pending_upload_.data(),native_pending_upload_.size())))return hr;
 // Static layouts keep their work in their own buffer (bake.work12), written
 // only when the layout is built; the bake becomes reusable once it is.
 if(static_set){
  if(!static_reuse){
   if(FAILED(hr=upload_native_static_work(layout)))return hr;
   if(static_bake){bake.set_revision=static_set->revision;bake.content_revision=native_static_content_revision_;}
  }
 }else if(FAILED(hr=d3d12_->write_buffer(*native12_.work,0,layout.work.data(),UINT64(draw_count)*sizeof(NativeDrawWorkGpu))))return hr;
 D3D12Buffer* const work=static_set?bake.work12.get():native12_.work.get();
 if(!work)return E_FAIL;
 // A reused bake's meshes stay current like every drawn mesh
 // (retire_stale_native_geometry keeps a mesh used this frame).
 if(static_reuse)for(auto* mesh:bake.meshes)mesh->last_used_frame=native_frame_index_;
 if(!layout.arguments.empty()&&FAILED(hr=d3d12_->write_buffer(*native12_.arguments,0,layout.arguments.data(),layout.arguments.size()*sizeof(UINT))))return hr;
 if(FAILED(hr=upload_native_instances_d3d12()))return hr;
 // The terrain frame (t127) and layer shifts (t19) the set_* calls kept.
 if(FAILED(hr=upload_native_terrain_frame_d3d12()))return hr;
 if(FAILED(hr=upload_native_terrain_phases_d3d12()))return hr;
 if(FAILED(hr=prepare_native_poses_d3d12()))return hr;
 // Lava and other self-lit ground as point lights for the next frame's
 // set_native_point_lights and as bounce sources (the D3D11 flush's call).
 if(!scene_composited_)refresh_emissive_lights();
 if(FAILED(hr=upload_native_point_lights_d3d12()))return hr;
 // The point lights' first bounce (cave_irradiance.h) from the lights just
 // uploaded, and its sampling constants in the b5 tail (the D3D11 flush order).
 update_cave_irradiance();
 {const CaveSampleConstants cave=cave_sample_constants();
  static_assert(sizeof(cave)==32&&sizeof(native12_.point_light_constants)==64,"point-light constant layout");
  std::memcpy(reinterpret_cast<std::uint8_t*>(native12_.point_light_constants.data())+32,&cave,sizeof(cave));}
 if(!scene_composited_&&contact_update_frame_!=native_frame_index_){
  contact_update_frame_=native_frame_index_;
  // The shared per-map grids (tile counts) without the ground cover.
  AmbientWorldSource inputs;
  if(bridge_ambient_world_source(inputs)&&inputs.terrain)rebuild_ambient_particle_map(inputs,bridge_world_fog_snapshot().environment);
  if(FAILED(update_contact_maps_d3d12())){
   release_contact_blend_maps();
   bridge_append_probe_event("contact_blend_art_omitted","contact atlas or tile grid unavailable; contact bands show the painted ground colour on this map");
  }
 }
 if(FAILED(hr=ensure_terrain_noise_d3d12()))return hr;
 // The game's hourly fog colour of this world flush (the D3D11 flush sets it
 // before bind_world_fog): the owned sky's horizon and the shaft haze meet the
 // fog in it. Unset, both used the initial (.5, .6, .7).
 if(!scene_composited_&&!layout.batches.empty()){
  const auto& first=native_state_buckets_[layout.batches.front().state_bucket].state;
  const DWORD fog=(native_static_frame_?*native_static_frame_:first).render[D3DRENDERSTATE_FOGCOLOR];
  sky_fog_color_[0]=((fog>>16)&255)/255.f;sky_fog_color_[1]=((fog>>8)&255)/255.f;sky_fog_color_[2]=(fog&255)/255.f;
 }
 // The world fog frame values (b10) for this flush (bind_world_fog keeps them).
 if(FAILED(hr=bind_world_fog()))return hr;
 std::array<std::uint8_t,sizeof(native_world_clip_)*2+64> clip;native_world_clip_values(clip);
 transition_profile_pass("compute_build");
 // The build dispatches (nativeBuildCS), per geometry page range.
 d3d12_->clear_slots();
 // prepareNativeVertex also consumes the flat-colour flag for records
 // without retained world geometry; raster-stage constants arrive later.
 Constants build_constants{};build_constants.combiner_padding=hit_test_active_?1u:0u;
 d3d12_->set_constants(0,&build_constants,sizeof(build_constants));
 d3d12_->set_constants(1,&native_world_lighting_,sizeof(native_world_lighting_));
 d3d12_->set_constants(3,clip.data(),UINT(clip.size()));
 d3d12_->set_resource(2,native12_.dynamic.get());
 d3d12_->set_resource(5,native12_.draws.get());
 d3d12_->set_resource(13,native12_.instances.get());
 d3d12_->set_resource(126,native12_.instances.get());
 d3d12_->set_resource(16,work);
 // The occlusion pyramid (nativeHiZ, t26) while b3 says it is active.
 d3d12_->set_resource(26,native_occlusion_active()?native_hiz12_.get():nullptr);
 d3d12_->set_unordered(0,native12_.visible.get());
 d3d12_->set_unordered(1,native12_.arguments.get());
 d3d12_->set_unordered(2,native12_.prepared.get());
 d3d12_->set_unordered(3,native12_.world_vertices.get());
 // The prepared world's inputs and outputs (as the D3D11 flush binds them).
 d3d12_->set_resource(10,native12_.animation.get());
 d3d12_->set_resource(12,native12_.pose_nodes.get());
 d3d12_->set_resource(19,native12_.layer_shifts.get());
 d3d12_->set_resource(127,native12_.terrain_frame.get());
 d3d12_->set_unordered(6,native12_.world_states.get());
 for(const auto& range:layout.dispatches){
  if(range.geometry_page>=native_geometry_pages_.size()||!native_geometry_pages_[range.geometry_page].d3d12)return E_FAIL;
  d3d12_->set_resource(1,native_geometry_pages_[range.geometry_page].d3d12.get());
  const std::array<UINT,4> work_range{range.first,range.count,0,0};
  d3d12_->set_constants(4,work_range.data(),sizeof(work_range));
  UINT width=0,height=0;
  if(!native_dispatch_dimensions(range.count,width,height))return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
  hr=d3d12_->dispatch(programs[2],width,height,1);
  if(FAILED(hr))return hr;
  if(d3d12_not_done(hr)){if(hr==S_FALSE)d3d12_->omit("native packets (build pipeline not ready)",false);return S_OK;}
  d3d12_->count("native build dispatches");
 }
 // Terrain 2.0 material cache: this frame's bake (after the terrain frame
 // values are uploaded), then bind_terrain2_d3d12 binds its views.
 update_terrain2_cache();
 // The owned sky behind the frame's first world batch (draw_owned_sky).
 if(sky_draw_pending_)draw_owned_sky_d3d12();
 // Caves: the daylight haze past each exit, behind everything (draw_cave_exit_glow).
 draw_cave_exit_glow_d3d12();
 // The liquid frame (the D3D11 batch loop prepares it at its batches): the
 // ripple values and their wave steps, the generated maps, the swamp frame.
 prepare_liquid_ripples();
 (void)ensure_liquid_normal_map(); // unavailable: the water draws without them, as on D3D11
 bind_liquid_swamp_frame();
 // The interface batches: nativeVS and the object nativePS over the build's
 // outputs, each batch's fixed state and texture as the D3D11 flush binds them.
 d3d12_->clear_slots();
 d3d12_->set_constants(1,&native_world_lighting_,sizeof(native_world_lighting_));
 d3d12_->set_constants(3,clip.data(),UINT(clip.size()));
 d3d12_->set_resource(2,native12_.dynamic.get());
 d3d12_->set_resource(4,native12_.visible.get());
 d3d12_->set_resource(5,native12_.draws.get());
 d3d12_->set_resource(7,native12_.prepared.get());
 d3d12_->set_resource(8,native12_.world_vertices.get());
 d3d12_->set_resource(13,native12_.instances.get());
 d3d12_->set_resource(126,native12_.instances.get());
 d3d12_->set_resource(10,native12_.animation.get());
 d3d12_->set_resource(12,native12_.pose_nodes.get());
 d3d12_->set_resource(19,native12_.layer_shifts.get());
 d3d12_->set_resource(127,native12_.terrain_frame.get());
 // Snow trail field (nativeSnowTrailWindow.w is 0 without it; the D3D11
 // flush's t8 and t36): the terrain pixel and cliff tessellation stages read
 // it at kFieldSlotD3D12; the vertex stage at t36 sinks units into troughs
 // (nativeFigureTrailSink).
 const D3D12Texture* const snow_field=scene_composited_?nullptr:snow_terrain12_.get();
 d3d12_->set_resource(snow_trails::kFieldSlotD3D12,snow_field);
 d3d12_->set_resource(36,snow_field);
 // The rain shelter for wet and snowy surfaces (t70): the soft map, else the raw one (renderer_terrain.cpp rows).
 d3d12_->set_resource(70,!native_rain_valid_?nullptr:native_shelter_soft_valid_?rain12_.soft[0].get():rain12_.shelter.get());
 // The rain ring texture (t71) while the terrain frame reads it (renderer_terrain.cpp).
 d3d12_->set_resource(71,native_rain_rings_bound_?rain12_.rings.get():nullptr);
 // The snow surface for the object build-up (t73, s11; none: its drift relief only).
 d3d12_->set_resource(kSnowSurfaceSlot,snow_surface12_.get());
 d3d12_->set_sampler(kSnowSurfaceSampler,world_texture_sampler(D3DTFN_LINEAR,D3DTFG_LINEAR,D3DTFP_LINEAR,
  D3DTADDRESS_WRAP,D3DTADDRESS_WRAP,true,1u<<configured_integer(debug_overlay_.config(),"anisotropy",3)).description);
 // Crown impostor atlas (t44/t45, nativeImpostorTexel in nativePS and nativePrepassPS); null before any bake.
 d3d12_->set_resource(44,impostors_.albedo12.get());d3d12_->set_resource(45,impostors_.normal12.get());
 // Lights, fog and the noise lattice the world pixel variants read.
 d3d12_->set_constants(5,native12_.point_light_constants.data(),sizeof(native12_.point_light_constants));
 d3d12_->set_constants(10,&fog_frame_d3d12_,sizeof(fog_frame_d3d12_));
 bind_world_fog_volume_d3d12();
 d3d12_->set_resource(17,native_point_lights_.empty()?nullptr:native12_.point_light_grid.get());
 d3d12_->set_resource(18,native_point_lights_.empty()?nullptr:native12_.point_lights.get());
 d3d12_->set_resource(PointShadowTextureLayout::terrain_noise_srv,native12_.terrain_noise.get());
 bind_cave_irradiance_d3d12(); // the bounce light and the cave fog (t80-t84, s13)
 {D3D11_SAMPLER_DESC wrap{};wrap.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
  wrap.AddressU=wrap.AddressV=wrap.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;wrap.MaxLOD=0;d3d12_->set_sampler(12,wrap);}
 {D3D11_SAMPLER_DESC linear{};linear.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;linear.MaxLOD=D3D11_FLOAT32_MAX;d3d12_->set_sampler(7,linear);}
 // The sun maps and their constants (b2, t9; the samplers as the D3D11 flush
 // sets them). Unbound (no pass yet), the constants read zero: no map valid.
 // A map left unwritten because its pass waited for a compiling pipeline
 // (flush_native_shadows_d3d12 wait) is not a contract failure: with the
 // spatial selection off its receivers read lit instead of the failure
 // magenta. A failed pass keeps the magenta.
 {NativeShadowConstants receivers=shadow12_.constants;
  if(!native_shadow_failed_&&native_shadows_enabled_)
   for(UINT slice=0;slice<5;++slice)
    if(native_shadow_frame_.slices[slice].enabled&&!(receivers.control[0]&(1u<<slice))){receivers.bias[4][2]=0;break;}
  d3d12_->set_constants(2,&receivers,sizeof(receivers));}
 d3d12_->set_resource(9,shadow12_.maps.get());
 // The temporal sun-shadow history (b6, t20/t21, s3): read by every world
 // batch, written (slot 1) by the batches the D3D11 flush's write_history picks.
 const bool history_active=prepare_shadow_history();
 bind_shadow_history_d3d12(history_active);
 // The interface after composition (portraits, panel doll, HUD meshes) sits
 // in its own space, not the world's: no sun or cloud shadow, history, lamps
 // or bounce light (as its fog, bind_world_fog). With the world's b2 the
 // portrait faces sampled the sun maps at their HUD coordinates and flickered
 // as those maps changed. Zero blocks read as "nothing bound" (renderer_flush.cpp).
 if(scene_composited_){
  const NativeShadowConstants no_shadows{};
  const decltype(native12_.point_light_constants) no_lights{};
  const decltype(shadow_history_constants12_) no_history{};
  d3d12_->set_constants(2,&no_shadows,sizeof(no_shadows));
  d3d12_->set_constants(5,no_lights.data(),UINT(sizeof(no_lights)));
  d3d12_->set_constants(6,no_history.data(),UINT(sizeof(no_history)));
 }
 for(UINT page=0;page<PointShadowTextureLayout::array_bindings;++page)
  d3d12_->set_resource(PointShadowTextureLayout::first_srv+page,
   page<native_point_shadow_pages_.size()?native_point_shadow_pages_[page].depth12.get():nullptr);
 {D3D11_SAMPLER_DESC compare{};compare.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
  compare.AddressU=compare.AddressV=compare.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;
  compare.BorderColor[0]=compare.BorderColor[1]=compare.BorderColor[2]=compare.BorderColor[3]=1;
  compare.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;compare.MaxLOD=D3D11_FLOAT32_MAX;
  d3d12_->set_sampler(1,compare);
  compare.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
  d3d12_->set_sampler(2,compare);d3d12_->set_sampler(4,compare);
  D3D11_SAMPLER_DESC gather{};gather.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
  gather.AddressU=gather.AddressV=gather.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;gather.MaxLOD=D3D11_FLOAT32_MAX;
  d3d12_->set_sampler(10,gather);}
 bind_liquid_d3d12();
 // The owned sky's cloud shadow and probe (t110/t111 in the DXIL, world_fog.h).
 if(sky_inputs_active_){
  d3d12_->set_resource(kSkyCloudShadowSlotD3D12,sky12_.shadow.get());
  d3d12_->set_resource(kSkyProbeSlotD3D12,sky12_.probe[sky_index_].get());
 }
 // Terrain 2.0's map table and contact sampler (no entries while it is off).
 bind_terrain2_d3d12();
 // The terrain pixel variants (append_native_shader_programs: NATIVE_TERRAIN_PS,
 // and with NATIVE_TERRAIN_FLAT for the flat tile class); the general
 // combiner programs serve every combiner configuration (the D3D11 fast
 // variants compile the same results with the general path left out).
 // Material quality Off: the same programs without the material code
 // (append_native_off_material_programs).
 const bool off_materials=!material_level();
 const auto pixel_program=[&](std::initializer_list<const char*> names)->const ShaderProgram*{
  for(const auto& program:programs){
   if(std::strcmp(program.entry,"nativePS")!=0)continue;
   std::size_t count=0;bool all=true;
   for(const D3D_SHADER_MACRO* define=program.defines;define&&define->Name;++define){
    ++count;bool listed=off_materials&&std::strcmp(define->Name,"NATIVE_OFF_MATERIALS")==0;
    for(const char* name:names)listed|=std::strcmp(define->Name,name)==0;all&=listed;
   }
   if(all&&count==names.size()+(off_materials?1u:0u))return &program;
  }
  return nullptr;
 };
 const ShaderProgram* const object_ps=pixel_program({});
 // With a trail field: the snow trail variants (NATIVE_SNOW_TRAILS), as the D3D11 flush picks them.
 const ShaderProgram* const terrain_ps=snow_field?pixel_program({"NATIVE_TERRAIN_PS","NATIVE_SNOW_TRAILS"}):pixel_program({"NATIVE_TERRAIN_PS"});
 const ShaderProgram* const terrain_flat_ps=snow_field?pixel_program({"NATIVE_TERRAIN_PS","NATIVE_SNOW_TRAILS","NATIVE_TERRAIN_FLAT"}):
  pixel_program({"NATIVE_TERRAIN_PS","NATIVE_TERRAIN_FLAT"});
 // The liquid programs (append_native_shader_programs' nativeLiquidPS variants).
 const auto liquid_program=[&](std::initializer_list<const char*> names)->const ShaderProgram*{
  for(const auto& program:programs){
   if(std::strcmp(program.entry,"nativeLiquidPS")!=0)continue;
   std::size_t count=0;bool all=true;
   for(const D3D_SHADER_MACRO* define=program.defines;define&&define->Name;++define){
    ++count;bool listed=false;for(const char* name:names)listed|=std::strcmp(define->Name,name)==0;all&=listed;
   }
   if(all&&count==names.size())return &program;
  }
  return nullptr;
 };
 const ShaderProgram* const water_low_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LIQUID_WATER","NATIVE_LOW"});
 const ShaderProgram* const liquid_low_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LOW"});
 const ShaderProgram* const lava_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LIQUID_LAVA"});
 const ShaderProgram* const foam_ps=liquid_program({"NATIVE_LIQUID_FOAM"});
 const bool water_simple=configured_boolean(debug_overlay_.config(),"water_simple",false);
 // With a reflection frame (prepare_water_reflections): the full, the
 // no-physical and the half-resolution trace programs.
 const ShaderProgram* const water_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LIQUID_WATER"});
 const ShaderProgram* const water_ssr_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LIQUID_WATER","NATIVE_LIQUID_NO_PHYSICAL"});
 const ShaderProgram* const liquid_ps=liquid_program({"NATIVE_LIQUID_BODY"});
 const ShaderProgram* const liquid_ssr_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LIQUID_NO_PHYSICAL"});
 const ShaderProgram* const ssr_input_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LIQUID_SSR_INPUT"});
 const auto plain_program=[&](const char* entry)->const ShaderProgram*{
  for(const auto& program:programs)if(!program.defines&&std::strcmp(program.entry,entry)==0)return &program;
  return nullptr;
 };
 const ShaderProgram* const ssr_march_ps=plain_program("nativeLiquidSsrPS");
 const ShaderProgram* const ssr_filter_ps=plain_program("nativeLiquidSsrFilterPS");
 const ShaderProgram* const full_screen_vs=plain_program("nativeShadowMaskVS");
 const bool water_physical=configured_boolean(debug_overlay_.config(),"water_physical",true);
 bool water_ssr_done=false;
 D3D11_SAMPLER_DESC water_sampler{};water_sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 water_sampler.AddressU=water_sampler.AddressV=water_sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;water_sampler.MaxLOD=D3D11_FLOAT32_MAX;
 // A batch's wading units (the D3D11 bind_batch_units): only those that can
 // reach its water; a neighbour map's water keeps the frame's full set.
 // A reused bake has no queue: its per-batch totals stand in for the draw walks.
 const auto baked_batch=[&](std::size_t index){return static_reuse?&bake.batches[index]:nullptr;};
 const auto bind_batch_units=[&](const NativeBatch& item,const NativeSubmissionLayout::StaticBake::Batch* baked){
  bool neighbour=false;
  float lo[2]{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},hi[2]{-std::numeric_limits<float>::max(),-std::numeric_limits<float>::max()};
  if(baked){
   neighbour=baked->neighbour;
   for(int axis=0;axis<2;++axis){lo[axis]=baked->xy_min[axis];hi[axis]=baked->xy_max[axis];}
  }else for(std::size_t n=item.first_draw;n<item.first_draw+item.draw_count;++n){
   const auto& queued=native_queued_draw(native_draw_order_[n]);
   neighbour|=(queued.instance.flags&native_neighbour_flag)!=0;
   for(int axis=0;axis<2;++axis){lo[axis]=std::min(lo[axis],queued.bounds_min[axis]);hi[axis]=std::max(hi[axis],queued.bounds_max[axis]);}
  }
  if(!neighbour)bind_liquid_ripple_units(lo,hi);
 };
 const bool water_reflections_wanted=configured_boolean(debug_overlay_.config(),"water_reflections",true);
 // Cliff tessellation (the D3D11 flush's choice): with a snow trail field the
 // hull and domain stages with the trail code (nativeTerrainHS/DS: the trail
 // groove and berm displacement), else the plain ones (exact: the trail terms
 // need nativeSnowTrailWindow.w > 0, set only with a field).
 const bool tessellate_terrain=!scene_composited_&&terrain_tessellation_frame_==native_frame_index_&&native12_.tessellation.size()>=5;
 const ShaderProgram* const terrain_hull=tessellate_terrain?&native12_.tessellation[snow_field?1:3]:nullptr;
 const ShaderProgram* const terrain_domain=tessellate_terrain?&native12_.tessellation[snow_field?2:4]:nullptr;
 const bool neighbour_terrain_blend=configured_boolean(debug_overlay_.config(),"terrain_blend_neighbours",true);
 const bool neighbour_fog_skip=configured_boolean(debug_overlay_.config(),"neighbour_fog_skip",true);
 // Depth prepass and sun-shadow mask (the D3D11 flush's, native_shader.h
 // nativePrepassPS and nativeShadowMaskPS): batches whose depth is final
 // before shading draw depth and the receiver record first; the mask pass
 // takes their pixels' sun and cloud shadow once; their material pass then
 // tests LESS_EQUAL against that depth (only the visible surface passes)
 // and reads the mask (NATIVE_SHADOW_MASK variants). No stencil mark: the
 // receiver record is cleared before the prepass (begin_world_prepass_d3d12).
 const ShaderProgram* const prepass_vs=plain_program("nativePrepassVS");
 const ShaderProgram* const motion_vs=plain_program("nativeMotionVS");
 const ShaderProgram* const motion_ps=plain_program("nativeMotionPS");
 const ShaderProgram* const prepass_ps=plain_program("nativePrepassPS");
 const ShaderProgram* const masked_object_ps=pixel_program({"NATIVE_SHADOW_MASK"});
 const ShaderProgram* const masked_terrain_ps=snow_field?pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS","NATIVE_SNOW_TRAILS"}):
  pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS"});
 const ShaderProgram* const masked_terrain_flat_ps=snow_field?pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS","NATIVE_SNOW_TRAILS","NATIVE_TERRAIN_FLAT"}):
  pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS","NATIVE_TERRAIN_FLAT"});
 // append_terrain_tessellation_shader_programs [5] nativePrepassTessVS, [6] HS, [7] snow HS, [8] DS, [9] snow DS.
 const bool prepass_tessellation=native12_.tessellation.size()>=10;
 const bool prepass_ready=configured_boolean(debug_overlay_.config(),"depth_prepass",true)&&world_d3d12_bound_&&!scene_composited_&&
  !hit_test_active_&&fog_camera_valid_&&prepass_vs&&prepass_ps&&full_screen_vs&&masked_object_ps&&masked_terrain_ps&&masked_terrain_flat_ps&&
  SUCCEEDED(ensure_prepass_targets_d3d12());
 // Per batch: 0 not prepassed, 1 its depth is drawn (material LESS_EQUAL),
 // 2 drawn and the mask is ready (the masked program).
 std::vector<std::uint8_t> prepassed;
 try{prepassed.assign(layout.batches.size(),0);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 bool any_prepass=false;
 for(std::size_t batch_index=0;prepass_ready&&batch_index<layout.batches.size();++batch_index){
  const NativeBatch& batch=layout.batches[batch_index];
  const NativeStateBucket& bucket=native_state_buckets_[batch.state_bucket];
  if(batch.posed||bucket.opacity==NativeOpacityClass::ordered)continue;
  const auto* baked=baked_batch(batch_index);
  const NativeQueuedDraw& draw=baked?native_instance_records_[baked->first_record].command:native_queued_draw(native_draw_order_[batch.first_draw]);
  if(draw.hud_composition.opacity<1.f)continue;
  const FixedState& st=bucket.state;
  const bool blend=st.render[D3DRENDERSTATE_ALPHABLENDENABLE]!=FALSE;
  const bool overwrite=!blend||(st.render[D3DRENDERSTATE_SRCBLEND]==D3DBLEND_ONE&&st.render[D3DRENDERSTATE_DESTBLEND]==D3DBLEND_ZERO);
  if(bucket.opacity==NativeOpacityClass::cutout&&!overwrite)continue;
  if(st.render[D3DRENDERSTATE_ZENABLE]==D3DZB_FALSE||!st.render[D3DRENDERSTATE_ZWRITEENABLE])continue;
  const DWORD depth_func=st.render[D3DRENDERSTATE_ZFUNC];
  if(depth_func!=D3DCMP_LESS&&depth_func!=D3DCMP_LESSEQUAL)continue;
  const bool terrain=bucket.semantic_kind==SemanticKind::terrain;
  if(terrain&&(st.render[D3DRENDERSTATE_ALPHATESTENABLE]!=FALSE||(tessellate_terrain&&!prepass_tessellation)))continue;
  bool liquid=baked&&baked->liquid;
  for(std::size_t n=batch.first_draw;!baked&&terrain&&!liquid&&n<batch.first_draw+batch.draw_count;++n)
   liquid=native_queued_draw(native_draw_order_[n]).mesh->has_liquid;
  if(liquid)continue;
  prepassed[batch_index]=1;any_prepass=true;
 }
 if(any_prepass){
  const bool history=world_history_bound12_;
  begin_world_prepass_d3d12();
  transition_profile_pass("depth_prepass");
  const auto& stages=native12_.tessellation;
  const ShaderProgram* const prepass_hull=prepass_tessellation?&stages[snow_field?7:6]:nullptr;
  const ShaderProgram* const prepass_domain=prepass_tessellation?&stages[snow_field?9:8]:nullptr;
  for(std::size_t batch_index=0;batch_index<layout.batches.size();++batch_index){
   if(!prepassed[batch_index])continue;
   const NativeBatch& batch=layout.batches[batch_index];
   const NativeStateBucket& bucket=native_state_buckets_[batch.state_bucket];
   const auto* baked=baked_batch(batch_index);
   const NativeQueuedDraw& draw=baked?native_instance_records_[baked->first_record].command:native_queued_draw(native_draw_order_[batch.first_draw]);
   const FixedState& st=bucket.state;
   ID3D11ShaderResourceView* contact_views[4]{};
   Constants constants;native_batch_constants(batch,draw,bucket,contact_views,neighbour_terrain_blend,neighbour_fog_skip,false,&constants);
   d3d12_->set_constants(0,&constants,sizeof(constants));
   d3d12_->set_resource(1,native_geometry_pages_[batch.geometry_page].d3d12.get());
   D3D12NativeState state;
   state.raster=fixed_raster_desc(st.render[D3DRENDERSTATE_CULLMODE],st.render[D3DRENDERSTATE_FILLMODE]==D3DFILL_WIREFRAME,true);
   state.depth=fixed_depth_desc(D3DZB_TRUE,TRUE,st.render[D3DRENDERSTATE_ZFUNC]);
   state.blend=fixed_blend_choice(FALSE,D3DBLEND_ONE,D3DBLEND_ZERO,false);
   d3d12_->set_sampler(0,world_texture_sampler(st.stage[0][D3DTSS_MINFILTER],st.stage[0][D3DTSS_MAGFILTER],st.stage[0][D3DTSS_MIPFILTER],
    st.stage[0][D3DTSS_ADDRESSU],st.stage[0][D3DTSS_ADDRESSV],true,1u<<configured_integer(debug_overlay_.config(),"anisotropy",3)).description);
   profiler_.add_draw(0,batch.draw_count);profiler_.add_batch();
   // The material pass's regions and stages (below), so both passes meet the same depth.
   const bool terrain=bucket.semantic_kind==SemanticKind::terrain;
   const bool tessellated_batch=terrain&&tessellate_terrain&&bucket.material_key.contact_map==0;
   bool drawn=true;
   const auto depth_draw=[&](UINT offset,bool tessellate)->HRESULT{
    const HRESULT result=tessellate?
     d3d12_->draw_indirect(stages[5],*prepass_ps,state,nullptr,*native12_.arguments,offset,prepass_hull,prepass_domain):
     d3d12_->draw_indirect(*prepass_vs,*prepass_ps,state,nullptr,*native12_.arguments,offset);
    if(result==S_FALSE){drawn=false;d3d12_->omit("depth prepass batch (pipeline not ready: unmasked shading)",false);}
    return result;
   };
   if(!terrain){
    if(FAILED(hr=depth_draw(static_cast<UINT>(batch_index)*16u,false)))return hr;
   }else{
    if(tessellated_batch&&batch.flat_pixels&&snow_field){
     for(const UINT layer_bits:{16u,32u}){
      Constants part=constants;part.liquid_layer|=layer_bits;
      d3d12_->set_constants(0,&part,sizeof(part));
      if(FAILED(hr=depth_draw(static_cast<UINT>(batch_index)*16u,layer_bits==32u)))return hr;
     }
    }else if(FAILED(hr=depth_draw(static_cast<UINT>(batch_index)*16u,tessellated_batch&&!batch.flat_pixels)))return hr;
    if(batch.steep_args_slot!=UINT_MAX){
     Constants steep=constants;steep.native_triangle_base=batch.steep_triangle_base;
     d3d12_->set_constants(0,&steep,sizeof(steep));
     if(FAILED(hr=depth_draw(batch.steep_args_slot*16u,tessellated_batch)))return hr;
    }
   }
   // A part left undrawn (its pipeline compiling) leaves the receiver record
   // incomplete: the batch shades unmasked, still testing LESS_EQUAL.
   if(!drawn)prepassed[batch_index]=0x81;
  }
  const HRESULT masked=resolve_world_shadow_mask_d3d12();
  if(FAILED(masked)&&masked!=E_FAIL)return masked;
  if(masked==S_OK)for(auto& value:prepassed)if(value==1)value=2;
  for(auto& value:prepassed)if(value==0x81)value=1;
  bind_world_targets_d3d12(history);
 }
 for(std::size_t batch_index=0;batch_index<layout.batches.size();++batch_index){
  const NativeBatch& batch=layout.batches[batch_index];
  const NativeStateBucket& bucket=native_state_buckets_[batch.state_bucket];
  if(batch.geometry_page>=native_geometry_pages_.size()||!native_geometry_pages_[batch.geometry_page].d3d12)return E_FAIL;
  // write_history (the D3D11 flush): opaque and cutout world batches write
  // this frame's sun visibility into the history target at slot 1.
  {const bool write_history=history_active&&!scene_composited_&&!hit_test_active_&&bucket.opacity!=NativeOpacityClass::ordered;
   if(world_d3d12_bound_&&!scene_composited_&&write_history!=world_history_bound12_)bind_world_targets_d3d12(write_history);
   if(world_history_bound12_)shadow_history_written_=true;}
  const auto* baked=baked_batch(batch_index);
  const NativeQueuedDraw& draw=baked?native_instance_records_[baked->first_record].command:native_queued_draw(native_draw_order_[batch.first_draw]);
  const FixedState& st=bucket.state;
  ID3D11ShaderResourceView* contact_views[4]{};
  Constants constants;native_batch_constants(batch,draw,bucket,contact_views,neighbour_terrain_blend,neighbour_fog_skip,false,&constants);
  d3d12_->set_constants(0,&constants,sizeof(constants));
  d3d12_->set_resource(1,native_geometry_pages_[batch.geometry_page].d3d12.get());
  // The batch's contact maps (native_batch_constants chose the active map's).
  // As native_batch_constants' active_art: the active map's grid and atlas
  // while they belong to this map (the ready bits select atlas slices in the
  // shader; they do not gate the binding).
  {const ContactBlendMaps* maps=nullptr;
   if(!scene_composited_&&bucket.material_key.contact_map==0){
    if(contact_maps_.tiles12&&contact_maps_.atlas12&&contact_map_generation_==ambient_map_generation_&&
       contact_constants_generation_==contact_map_generation_)maps=&contact_maps_;
   }else if(!scene_composited_&&(bucket.semantic_kind!=SemanticKind::terrain||neighbour_terrain_blend)){
    // A neighbour's own grid and atlas (native_batch_constants' rule).
    for(const auto& n:gc_neighbours_)if(n.contact_map==bucket.material_key.contact_map){maps=&n.contact;break;}
   }
   d3d12_->set_resource(15,maps?maps->tiles12.get():nullptr);
   d3d12_->set_resource(16,maps?maps->atlas12.get():nullptr);
   // Its map's painted cover colour and ground (the active map's or the
   // neighbour's own; native_batch_constants sets their origin and mip).
   const GcMapD3D12* cover=contact_cover_map_d3d12(bucket.material_key.contact_map);
   d3d12_->set_resource(kContactGroundSlotD3D12,cover?cover->ground.get():nullptr);
   d3d12_->set_resource(kContactColorSlotD3D12,cover?cover->color.get():nullptr);}
  {D3D11_SAMPLER_DESC contact{};contact.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
   contact.AddressU=contact.AddressV=contact.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;contact.MaxLOD=D3D11_FLOAT32_MAX;d3d12_->set_sampler(8,contact);}
  // The material texture: each draw's bindless index (instance texture
  // reference), so a batch may span textures; no t0/t6 binding.
  // apply_state's choices, as the D3D11 flush makes them per batch.
  const DWORD cull=st.render[D3DRENDERSTATE_CULLMODE];
  const bool wireframe=st.render[D3DRENDERSTATE_FILLMODE]==D3DFILL_WIREFRAME;
  D3D12NativeState state;
  state.raster=fixed_raster_desc(cull,wireframe,true);
  state.depth=fixed_depth_desc(st.render[D3DRENDERSTATE_ZENABLE],st.render[D3DRENDERSTATE_ZWRITEENABLE],st.render[D3DRENDERSTATE_ZFUNC]);
  state.blend=fixed_blend_choice(st.render[D3DRENDERSTATE_ALPHABLENDENABLE],st.render[D3DRENDERSTATE_SRCBLEND],
                                 st.render[D3DRENDERSTATE_DESTBLEND],draw.hud_composition.opacity<1.f);
  // Drawn in this flush's prepass: only its visible surface passes (depth
  // final, nothing written), shaded once with the mask when it is ready.
  const bool masked=prepassed[batch_index]==2;
  if(prepassed[batch_index]){
   state.depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;state.depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
  }
  const auto sampling=world_texture_sampler(st.stage[0][D3DTSS_MINFILTER],st.stage[0][D3DTSS_MAGFILTER],st.stage[0][D3DTSS_MIPFILTER],
   st.stage[0][D3DTSS_ADDRESSU],st.stage[0][D3DTSS_ADDRESSV],!scene_composited_,
   1u<<configured_integer(debug_overlay_.config(),"anisotropy",3));
  d3d12_->set_sampler(0,sampling.description);
  RECT clip_rect{};
  const bool clipped=ui_clip_rect(draw.hud_composition,draw.camp_clip,clip_rect);
  const bool terrain=bucket.semantic_kind==SemanticKind::terrain&&!scene_composited_;
  // A liquid batch counts as the D3D11 flush names it (liquid_reflections
  // with a reflection frame, else liquid_surface): set at its branch below.
  const char* const native_pass=bucket.semantic_kind==SemanticKind::terrain?"gpu_driven_terrain":
   (draw.instance.lighting_enabled?"gpu_driven_figures":"gpu_driven_object_meshes");
  bool liquid=false,ground=false;std::uint8_t liquid_types=0;
  if(terrain){
   if(baked){liquid=baked->liquid;ground=baked->ground;liquid_types=baked->liquid_types;}
   else for(std::size_t n=batch.first_draw;n<batch.first_draw+batch.draw_count;++n){
    const NativeMesh& mesh=*native_queued_draw(native_draw_order_[n]).mesh;
    liquid|=mesh.has_liquid;ground|=mesh.has_ground;liquid_types|=mesh.liquid_types;
   }
  }
  if(!liquid){transition_profile_pass(native_pass);profiler_.add_draw(0,batch.draw_count);profiler_.add_batch();}
  if(!terrain){
   hr=d3d12_->draw_indirect(programs[0],masked?*masked_object_ps:object_ps?*object_ps:programs[1],state,clipped?&clip_rect:nullptr,*native12_.arguments,
                            static_cast<UINT>(batch_index)*16u);
   if(FAILED(hr))return hr;
   const char* drawn_name=scene_composited_?"interface batches drawn":bucket.semantic_kind==SemanticKind::figure?"world figure batches drawn":
    bucket.semantic_kind==SemanticKind::simple_geometry?"world object batches drawn":"world batches drawn (other kinds)";
   if(hr==S_FALSE)d3d12_->omit(scene_composited_?"native interface batch (pipeline not ready)":"world batch (pipeline not ready)",false);
   else if(hr==S_OK)d3d12_->count(drawn_name);
   // Upscaling: posed records (units, animated figures) also write their own
   // motion (draw_dlss_object_motion); the world targets come back after.
   if(hr==S_OK&&batch.posed&&bucket.opacity!=NativeOpacityClass::ordered&&motion_vs&&motion_ps&&
      draw_object_motion_d3d12(*motion_vs,*motion_ps,state,clipped?&clip_rect:nullptr,static_cast<UINT>(batch_index)*16u))
    bind_world_targets_d3d12(world_history_bound12_);
   continue;
  }
  // Terrain (the D3D11 batch loop's choices): a liquid batch draws its ground
  // part only (the water surfaces are M2e); a split batch draws its flat
  // region with the flat variant, then its steep region from its own
  // arguments slot and triangle base with the full one; an unsplit batch of
  // flat tiles takes the flat variant.
  if(!terrain_ps||!terrain_flat_ps){d3d12_->omit("terrain (pixel variant missing)",true);continue;}
  // A prepassed batch with the mask: the NATIVE_SHADOW_MASK variants.
  const ShaderProgram& full_ps=masked?*masked_terrain_ps:*terrain_ps;
  const ShaderProgram& flat_ps=masked?*masked_terrain_flat_ps:*terrain_flat_ps;
  // Neighbour-map terrain draws untessellated (the D3D11 rule); the active
  // map's flat region too without a trail field.
  const bool tessellated_batch=tessellate_terrain&&bucket.material_key.contact_map==0;
  const auto terrain_draw=[&](const ShaderProgram& pixel,UINT offset,bool tessellate)->HRESULT{
   const auto& stages=native12_.tessellation;
   const auto draw=[&](const ShaderProgram& program){
    return tessellate?
     d3d12_->draw_indirect(stages[0],program,state,clipped?&clip_rect:nullptr,*native12_.arguments,offset,terrain_hull,terrain_domain):
     d3d12_->draw_indirect(programs[0],program,state,clipped?&clip_rect:nullptr,*native12_.arguments,offset);
   };
   HRESULT drawn=draw(pixel);
   // Missing ground is a visible failure (world marker, F11), never a gap.
   if(drawn==S_FALSE)d3d12_->omit("terrain batch (pipeline not ready)",true);
   else if(drawn==S_OK)d3d12_->count("terrain batches drawn");
   return drawn;
  };
  if(liquid){
   // The D3D11 batch loop's liquid draws without a reflection frame (the
   // water_reflections 0 programs): stock-look water whole through the
   // ground variant; else the ground part, the water body, then the foam.
   const auto layer_draw=[&](const ShaderProgram& pixel,UINT layer_bits,const D3D12NativeState& layer_state)->HRESULT{
    Constants layer=constants;layer.liquid_layer=layer_bits;
    d3d12_->set_constants(0,&layer,sizeof(layer));
    const auto& stages=native12_.tessellation;
    const auto draw=[&](const ShaderProgram& program){
     return tessellated_batch?
      d3d12_->draw_indirect(stages[0],program,layer_state,clipped?&clip_rect:nullptr,*native12_.arguments,static_cast<UINT>(batch_index)*16u,terrain_hull,terrain_domain):
      d3d12_->draw_indirect(programs[0],program,layer_state,clipped?&clip_rect:nullptr,*native12_.arguments,static_cast<UINT>(batch_index)*16u);
    };
    HRESULT drawn=draw(pixel);
    if(drawn==S_FALSE){
     const bool ground=&pixel==terrain_ps||&pixel==terrain_flat_ps; // missing ground shows the world marker
     d3d12_->omit(ground?"liquid batch ground (pipeline not ready)":"liquid batch (pipeline not ready)",ground);
    }
    return drawn;
   };
   if(water_simple){
    transition_profile_pass("liquid_surface");profiler_.add_draw(0,batch.draw_count);profiler_.add_batch();
    if(FAILED(hr=layer_draw(*terrain_ps,8u,state)))return hr;
    d3d12_->count("stock-look water batches drawn");
    continue;
   }
   // The reflection frame: this flush's scene copy and depth snapshot (taken
   // at its first liquid batch), and the half-resolution trace once.
   const bool water_reflections=water_reflections_wanted&&prepare_water_reflections(st);
   const char* const liquid_pass=water_reflections?"liquid_reflections":"liquid_surface";
   transition_profile_pass(liquid_pass);profiler_.add_draw(0,batch.draw_count);profiler_.add_batch();
   if(water_reflections&&!water_ssr_done){
    water_ssr_done=true;
    for(UINT slot=29;slot<33;++slot)d3d12_->set_resource(slot,static_cast<const D3D12Texture*>(nullptr));
    if(!ssr_input_ps||!ssr_march_ps||!full_screen_vs||FAILED(create_water_ssr_targets_d3d12())){
     if(!water_ssr_failure_reported_){
      water_ssr_failure_reported_=true;
      bridge_report_event("water_ssr","unavailable: half-resolution reflection programs or targets; water shows no screen reflections");
     }
    }else{
     transition_profile_pass("water_ssr");
     const float zero[4]{};
     for(const auto& target:water12_.ssr)d3d12_->clear_texture(*target,zero);
     d3d12_->clear_depth_of(*water12_.ssr_depth,1.f,0);
     D3D12Texture* inputs[2]={water12_.ssr[0].get(),water12_.ssr[1].get()};
     d3d12_->set_targets(inputs,2,water12_.ssr_depth.get());
     d3d12_->set_constants(7,&water12_.constants,sizeof(water12_.constants));
     // Every liquid batch of the flush writes its rays and water points (the
     // input program, its own depth: the nearest water), with the stages its
     // water body draws with.
     for(std::size_t index=0;index<layout.batches.size();++index){
      const NativeBatch& item=layout.batches[index];
      const NativeStateBucket& item_bucket=native_state_buckets_[item.state_bucket];
      if(item_bucket.semantic_kind!=SemanticKind::terrain)continue;
      const auto* item_baked=baked_batch(index);
      bool item_liquid=item_baked&&item_baked->liquid;
      for(std::size_t n=item.first_draw;!item_baked&&!item_liquid&&n<item.first_draw+item.draw_count;++n)
       item_liquid=native_queued_draw(native_draw_order_[n]).mesh->has_liquid;
      if(!item_liquid)continue;
      const NativeQueuedDraw& item_draw=item_baked?native_instance_records_[item_baked->first_record].command:
       native_queued_draw(native_draw_order_[item.first_draw]);
      ID3D11ShaderResourceView* item_contact[4]{};Constants item_constants;
      native_batch_constants(item,item_draw,item_bucket,item_contact,neighbour_terrain_blend,neighbour_fog_skip,false,&item_constants);
      item_constants.liquid_layer=2u; // the liquid triangles only
      d3d12_->set_constants(0,&item_constants,sizeof(item_constants));
      d3d12_->set_resource(1,native_geometry_pages_[item.geometry_page].d3d12.get());
      D3D12NativeState input_state;
      input_state.raster=fixed_raster_desc(item_bucket.state.render[D3DRENDERSTATE_CULLMODE],item_bucket.state.render[D3DRENDERSTATE_FILLMODE]==D3DFILL_WIREFRAME,true);
      input_state.depth.DepthEnable=TRUE;input_state.depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;input_state.depth.DepthFunc=D3D11_COMPARISON_LESS;
      bind_batch_units(item,item_baked);
      const bool item_tessellated=tessellate_terrain&&item_bucket.material_key.contact_map==0;
      const auto& stages=native12_.tessellation;
      const HRESULT traced=item_tessellated?
       d3d12_->draw_indirect(stages[0],*ssr_input_ps,input_state,nullptr,*native12_.arguments,static_cast<UINT>(index)*16u,terrain_hull,terrain_domain):
       d3d12_->draw_indirect(programs[0],*ssr_input_ps,input_state,nullptr,*native12_.arguments,static_cast<UINT>(index)*16u);
      if(FAILED(traced))return traced;
      if(traced==S_FALSE)d3d12_->count("water trace inputs waiting for their pipeline");
     }
     if(liquid_ripple_ready_)d3d12_->set_constants(8,&liquid_ripple_values_,sizeof(liquid_ripple_values_));
     // The march: one full-screen triangle over the half-resolution targets.
     D3D12NativeState full_screen;
     full_screen.raster.FillMode=D3D11_FILL_SOLID;full_screen.raster.CullMode=D3D11_CULL_NONE;full_screen.raster.DepthClipEnable=TRUE;
     D3D12Texture* traced_targets[2]={water12_.ssr[2].get(),water12_.ssr[3].get()};
     d3d12_->set_targets(traced_targets,2,nullptr);
     d3d12_->set_resource(31,water12_.ssr[1].get());d3d12_->set_resource(32,water12_.ssr[0].get()); // water points, rays
     d3d12_->set_resource(3,water12_.scene.get());d3d12_->set_depth_resource(112,water12_.depth.get());
     d3d12_->set_resource(28,particle_depth12_[particle_depth_current_^1u].get()); // the previous frame's particle depth (constants say if it is valid)
     d3d12_->set_sampler(5,water_sampler);
     HRESULT marched=d3d12_->draw(*full_screen_vs,*ssr_march_ps,full_screen,nullptr,3);
     if(FAILED(marched))return marched;
     // The filter: the traced colour and key in, the filtered pair out.
     bool filtered=false;
     if(marched==S_OK&&ssr_filter_ps){
      D3D12Texture* smoothed[2]={water12_.ssr[4].get(),water12_.ssr[5].get()};
      d3d12_->set_targets(smoothed,2,nullptr);
      d3d12_->set_resource(29,water12_.ssr[2].get());d3d12_->set_resource(30,water12_.ssr[3].get());d3d12_->set_resource(31,water12_.ssr[1].get());
      const HRESULT result=d3d12_->draw(*full_screen_vs,*ssr_filter_ps,full_screen,nullptr,3);
      if(FAILED(result))return result;
      filtered=result==S_OK;
     }
     bind_world_targets_d3d12(world_history_bound12_);
     if(marched==S_OK){
      // The water bodies read the result (t29-t31).
      d3d12_->set_resource(29,water12_.ssr[filtered?4:2].get());d3d12_->set_resource(30,water12_.ssr[filtered?5:3].get());
      d3d12_->set_resource(31,water12_.ssr[1].get());
      d3d12_->count("water traces");
     }else{
      for(UINT slot=29;slot<33;++slot)d3d12_->set_resource(slot,static_cast<const D3D12Texture*>(nullptr));
      d3d12_->count("water traces waiting for their pipeline");
     }
     d3d12_->set_resource(32,static_cast<const D3D12Texture*>(nullptr));
     // This batch's geometry page again (its constants come with each draw).
     d3d12_->set_resource(1,native_geometry_pages_[batch.geometry_page].d3d12.get());
     transition_profile_pass(liquid_pass);
    }
   }
   // The reflection frame's inputs (t3 scene copy, t112 depth snapshot, b7, s5), else none.
   const WaterReflectionConstants no_reflection{};
   d3d12_->set_resource(3,water_reflections?water12_.scene.get():nullptr);
   d3d12_->set_depth_resource(112,water_reflections?water12_.depth.get():nullptr);
   d3d12_->set_resource(28,water_reflections?particle_depth12_[particle_depth_current_^1u].get():nullptr);
   d3d12_->set_constants(7,water_reflections?&water12_.constants:&no_reflection,sizeof(no_reflection));
   d3d12_->set_sampler(5,water_sampler);
   if(ground&&FAILED(hr=layer_draw(*terrain_ps,4u,state)))return hr;
   // The water body's program (the D3D11 choice; this backend has no Low
   // pixel path): without a reflection frame the reflection-free variants.
   const ShaderProgram* const body=liquid_types==2u?lava_ps:
    liquid_types==1u?(!water_reflections?water_low_ps:!water_physical?water_ssr_ps:water_ps):
    (!water_reflections?liquid_low_ps:!water_physical?liquid_ssr_ps:liquid_ps);
   if(!body||!foam_ps){d3d12_->omit("water surfaces (liquid program missing)",true);continue;}
   bind_batch_units(batch,baked);
   if(FAILED(hr=layer_draw(*body,2u,state)))return hr;
   // Foam: the liquid triangles again (layer bit 0), depth tested, never written.
   D3D12NativeState foam_state=state;foam_state.depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
   if(FAILED(hr=layer_draw(*foam_ps,3u,foam_state)))return hr;
   if(liquid_ripple_ready_)d3d12_->set_constants(8,&liquid_ripple_values_,sizeof(liquid_ripple_values_));
   d3d12_->count("liquid batches drawn");
   continue;
  }
  if(tessellated_batch&&batch.flat_pixels&&snow_field){
   // With a trail field the active map's flat region draws in two parts (the
   // D3D11 trail split): untessellated with the triangles near an active
   // trail tile collapsed (layer bit 16), then tessellated with only those
   // (bit 32; nativeTriangleNearTrail), where the trail displacement needs the stages.
   for(const UINT layer_bits:{16u,32u}){
    Constants part=constants;part.liquid_layer|=layer_bits;
    d3d12_->set_constants(0,&part,sizeof(part));
    if(FAILED(hr=terrain_draw(flat_ps,static_cast<UINT>(batch_index)*16u,layer_bits==32u)))return hr;
   }
  }else if(FAILED(hr=terrain_draw(batch.flat_pixels?flat_ps:full_ps,static_cast<UINT>(batch_index)*16u,
     tessellated_batch&&!batch.flat_pixels)))return hr;
  if(batch.steep_args_slot!=UINT_MAX){
   Constants steep=constants;steep.native_triangle_base=batch.steep_triangle_base;
   d3d12_->set_constants(0,&steep,sizeof(steep));
   if(FAILED(hr=terrain_draw(full_ps,batch.steep_args_slot*16u,tessellated_batch)))return hr;
  }
 }
 // Fur prototype (renderer_fur.cpp): this flush's unit records with fur,
 // while their world vertices and the flush's lighting are still bound.
 draw_fur_records();
 // Later world passes (effects, marks) never write the history.
 if(world_history_bound12_&&world_d3d12_bound_&&!scene_composited_)bind_world_targets_d3d12();
 d3d12_->clear_slots();
 return S_OK;
}

// flush_native_shadows on D3D12 (M2e shadows): the shadow layout's records,
// or a bake's from its own layout and work list, into the sun maps, in the
// D3D11 order: the world preparations (prepare, commit), then per redrawn map
// its clear or static-layer copy, the build dispatches and the depth draws.
// Point faces use the same submission with atlas-cell targets, the rain
// shelter map its one plain slice. This does not
// include the static-layer scroll
// (shadow_scroll_ps_ is never made here: moved maps redraw their layer).
HRESULT Renderer::flush_native_shadows_d3d12(){
 const bool point=native_point_shadow_active_;
 const bool rain=native_rain_active_; // the shelter map: one plain slice, no sun-map state
 auto* const active=point?nullptr:native_shadow_bake_active_;
 const bool bake=active!=nullptr;
 auto& layout=native_submission_layouts_[bake?active->layout_slot:1];
 if(!native_shadow_collecting_||native_shadow_failed_)return E_FAIL;
 if(bake&&(!native_pending_draws_.empty()||!layout.static_bake.work12))return E_FAIL;
 const auto finish=[&](HRESULT hr){
  d3d12_->clear_slots();
  if(world_d3d12_bound_)bind_world_targets_d3d12();else d3d12_->set_target(nullptr);
  if(FAILED(hr)){
   native_shadow_failed_=true;
   if(!point&&!rain)for(UINT slice=0;slice<5;++slice)if(native_shadow_dirty_mask_&(1u<<slice)){native_shadow_valid_[slice]=false;native_shadow_static_valid_[slice]=false;}
  }
  discard_native();end_profile_pass();return hr;
 };
 // A program still compiling (or a ring exhausted, named by the executor):
 // this pass's maps stay invalid (receivers unshadowed there) and are
 // redrawn next frame; never a failed pass.
 const auto wait=[&](HRESULT why=S_FALSE){
  // A collection can flush between terrain and figure batches. Keep its
  // existing failure bit until end_point_faces so a later empty flush cannot
  // turn this incomplete bake into a successful publication.
  if(point||rain)native_shadow_failed_=true;
  if(!point&&!rain)for(UINT slice=0;slice<5;++slice)if(native_shadow_dirty_mask_&(1u<<slice)){native_shadow_valid_[slice]=false;native_shadow_static_valid_[slice]=false;}
  if(why==S_FALSE)d3d12_->count(point?"point shadow passes waiting for their pipelines":"sun shadow passes waiting for their pipelines");
  return finish(point||rain?S_FALSE:S_OK); // an incomplete point bake must not be published
 };
 if(!native_shadow_dirty_mask_)return finish(S_OK);
 if(FAILED(native_d3d12_ensure())||(rain?!rain12_.shelter||!rain12_.foliage:!point&&(!shadow12_.maps||!shadow12_.statics)))return finish(E_FAIL);
 try{
  HRESULT hr=S_OK;
  if(bake){begin_native_preparations();native_preparation_count_=active->preparations;}
  else{
   order_native_groups(true);
   if(FAILED(hr=prepare_native_submission_layout(true,1)))return finish(hr);
   refresh_native_draw_work(layout,true);
  }
  const auto& programs=native12_.programs;
  const auto program=[&](const char* entry)->const ShaderProgram*{
   for(const auto& candidate:programs)if(std::strcmp(candidate.entry,entry)==0)return &candidate;
   return nullptr;
  };
  const ShaderProgram* const prepare_cs=program("nativeShadowPrepareCS");
  const ShaderProgram* const commit_cs=program("nativeShadowCommitCS");
  const ShaderProgram* const build_cs=program("nativeShadowBuildCS");
  const ShaderProgram* const shadow_vs=program("nativeShadowVS");
  const ShaderProgram* const shadow_ps=program("nativeShadowPS");
  if(!prepare_cs||!commit_cs||!build_cs||!shadow_vs||!shadow_ps)return finish(E_FAIL);
  const UINT records=static_cast<UINT>(layout.work.size()),batches=static_cast<UINT>(layout.batches.size());
  std::array<std::uint8_t,sizeof(native_world_clip_)*2+64> clip;native_world_clip_values(clip);
  D3D12Buffer* work=nullptr;
  if(records){
   if(FAILED(hr=native_d3d12_buffer(native12_.dynamic,native_pending_upload_.size(),0,0))||
      FAILED(hr=native_d3d12_buffer(native12_.work,UINT64(records)*sizeof(NativeDrawWorkGpu),sizeof(NativeDrawWorkGpu),0))||
      FAILED(hr=native_d3d12_buffer(native12_.arguments,UINT64(layout.arguments.size())*sizeof(UINT),0,kD3D12BufferUnordered|kD3D12BufferIndirect))||
      FAILED(hr=native_d3d12_buffer(native12_.visible,UINT64((std::max)(1u,layout.triangles))*16u,16u,kD3D12BufferUnordered))||
      FAILED(hr=native_d3d12_persistent(native12_.world_vertices,UINT64(sizeof(NativeWorldVertexGpu))*64u,sizeof(NativeWorldVertexGpu),0))||
      FAILED(hr=ensure_native_world_states())||
      FAILED(hr=native_d3d12_persistent(native12_.world_states,UINT64(sizeof(NativeWorldStateGpu))*64u,sizeof(NativeWorldStateGpu),0))||
      FAILED(hr=native_d3d12_persistent(native12_.animation,64u,0,0))||
      FAILED(hr=native_d3d12_persistent(native12_.pose_nodes,UINT64(sizeof(NativePoseNodeGpu))*16u,sizeof(NativePoseNodeGpu),0)))
    return finish(hr);
   if(!bake){
    if(!native_pending_upload_.empty()&&FAILED(hr=d3d12_->write_buffer(*native12_.dynamic,0,native_pending_upload_.data(),native_pending_upload_.size())))return finish(hr);
    if(FAILED(hr=d3d12_->write_buffer(*native12_.work,0,layout.work.data(),UINT64(records)*sizeof(NativeDrawWorkGpu))))return finish(hr);
    profiler_.add_upload(native_pending_upload_.size()+records*sizeof(NativeDrawWorkGpu));
   }
   work=bake?layout.static_bake.work12.get():native12_.work.get();
   if(FAILED(hr=upload_native_instances_d3d12()))return finish(hr);
   if(FAILED(hr=upload_native_terrain_frame_d3d12()))return finish(hr);
   if(FAILED(hr=upload_native_terrain_phases_d3d12()))return finish(hr);
   if(FAILED(hr=prepare_native_poses_d3d12()))return finish(hr);
  }
  // The slots every shadow pass reads (as the D3D11 flush binds them).
  const auto bind_common=[&](){
   d3d12_->clear_slots();
   d3d12_->set_constants(1,&native_world_lighting_,sizeof(native_world_lighting_));
   const auto& constants=rain?rain12_.constants:point?shadow12_.point_constants:shadow12_.constants;
   d3d12_->set_constants(2,&constants,sizeof(constants));
   d3d12_->set_constants(3,clip.data(),UINT(clip.size()));
   d3d12_->set_resource(2,native12_.dynamic.get());
   d3d12_->set_resource(5,native12_.draws.get());
   d3d12_->set_resource(10,native12_.animation.get());
   d3d12_->set_resource(12,native12_.pose_nodes.get());
   d3d12_->set_resource(13,native12_.instances.get());
 d3d12_->set_resource(126,native12_.instances.get());
   d3d12_->set_resource(16,work);
   if(bake&&active->masked)d3d12_->set_resource(35,native_shadow_work_mask12_.get()); // terrain bake: this redraw's per-work map masks
   d3d12_->set_resource(19,native12_.layer_shifts.get());
   d3d12_->set_resource(127,native12_.terrain_frame.get());
   // Units cast from where they are drawn: sunk into trail troughs (nativeFigureTrailSink, t36).
   d3d12_->set_resource(36,scene_composited_?nullptr:snow_terrain12_.get());
  };
  if(records&&native_preparation_count_){
   bind_common();
   d3d12_->set_unordered(3,native12_.world_vertices.get());
   d3d12_->set_unordered(6,native12_.world_states.get());
   transition_profile_pass("compute_shadow_prepare");
   for(std::size_t dispatch=0;dispatch<layout.dispatches.size();++dispatch){
    const auto& range=layout.dispatches[dispatch];if(!range.preparations)continue;
    if(range.geometry_page>=native_geometry_pages_.size()||!native_geometry_pages_[range.geometry_page].d3d12)return finish(E_FAIL);
    d3d12_->set_resource(1,native_geometry_pages_[range.geometry_page].d3d12.get());
    const std::array<UINT,4> work_range{range.first,range.count,0,0};
    d3d12_->set_constants(4,work_range.data(),sizeof(work_range));
    UINT width=0,height=0;if(!native_dispatch_dimensions(range.count,width,height))return finish(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
    const UINT chunks=bake?active->dispatch_chunks[dispatch]:native_shadow_chunks(layout,range,false);
    if((hr=d3d12_->dispatch(*prepare_cs,width,height,chunks))!=S_OK||(hr=d3d12_->dispatch(*commit_cs,width,height,1))!=S_OK)
     return FAILED(hr)?finish(hr):wait(hr);
    ++perf_native_compute_dispatches_;++native_frame_stats_.native_compute_dispatches;
   }
   if(bake){native_world_prepared_vertices_+=active->prepared_vertices;native_shadow_prepared_records_+=active->preparations;}
   else for(const auto& item:layout.work)if(item.record!=UINT_MAX&&(item.flags&2u)){native_world_prepared_vertices_+=native_instance_records_[item.record].command.component_count;++native_shadow_prepared_records_;}
  }
  if(records)for(const auto& selected:native_pending_draws_){
   const auto& queued=native_instance_records_[selected.record].command;
   if(queued.world_slot>=native_prepared_mesh_slots_.size())continue;
   auto& slot=native_prepared_mesh_slots_[queued.world_slot];if(slot.world_generation==queued.world_generation&&slot.world_base==queued.world_base)slot.world_valid=true;
  }
  const UINT side=rain?native_rain_side_:point?native_point_shadow_side_:native_shadow_side_;
  for(UINT slice=0;slice<(rain?1u:point?6u:5u);++slice){
   const UINT bit=1u<<slice;
   if(!(native_shadow_dirty_mask_&bit))continue;
   const bool static_layer=!point&&!rain&&native_shadow_phase_==1;
   D3D12Texture* target=rain?rain12_.shelter.get():static_layer?shadow12_.statics.get():shadow12_.maps.get();
   UINT target_slice=slice;
   RECT viewport{0,0,LONG(side),LONG(side)},cell{};
   if(point){
    const UINT page=native_point_shadow_slot_/PointShadowTextureLayout::cubes_per_atlas;
    const UINT face=(native_point_shadow_slot_%PointShadowTextureLayout::cubes_per_atlas)*6u+slice;
    const UINT tile=face%PointShadowTextureLayout::faces_per_slice,pitch=side+2;
    target=native_point_shadow_pages_[page].depth12.get();
    target_slice=face/PointShadowTextureLayout::faces_per_slice;
    cell={LONG(tile%PointShadowTextureLayout::atlas_tiles*pitch),LONG(tile/PointShadowTextureLayout::atlas_tiles*pitch),0,0};
    cell.right=cell.left+LONG(pitch);cell.bottom=cell.top+LONG(pitch);
    viewport={cell.left+1,cell.top+1,cell.right-1,cell.bottom-1};
   }
   D3D12Texture& maps=*target;
   if(static_layer){
    if(!(native_shadow_static_cleared_mask_&bit)){
     d3d12_->clear_depth_slice(maps,slice,1.f);
     native_shadow_static_cleared_mask_|=bit;native_shadow_static_valid_[slice]=false;
    }
   }else if(!(native_shadow_cleared_mask_&bit)){
    // The dynamic overlay starts from the cached static layer.
    if(point)d3d12_->clear_depth_slice(maps,target_slice,1.f,&cell);
    else if(!rain&&native_shadow_phase_==2&&native_shadow_static_valid_[slice]){
     if(FAILED(hr=d3d12_->copy_texture(maps,slice,*shadow12_.statics,slice)))return finish(hr);
    }else{
     d3d12_->clear_depth_slice(maps,slice,1.f);
     if(rain){const float none[4]{};d3d12_->clear_texture(*rain12_.foliage,none);}
    }
    native_shadow_cleared_mask_|=bit;
   }
   if(records){
    if(FAILED(hr=upload_native_shadow_constants(slice)))return finish(hr);
    // The build: this map's visible triangles and arguments from the batches' starting values.
    if(!layout.arguments.empty()&&FAILED(hr=d3d12_->write_buffer(*native12_.arguments,0,layout.arguments.data(),layout.arguments.size()*sizeof(UINT))))return finish(hr);
    bind_common();
    d3d12_->set_resource(15,native12_.world_states.get());
    d3d12_->set_unordered(0,native12_.visible.get());
    d3d12_->set_unordered(1,native12_.arguments.get());
    transition_profile_pass("compute_shadow_build");
    for(std::size_t dispatch=0;dispatch<layout.geometry_dispatches.size();++dispatch){
     const auto& range=layout.geometry_dispatches[dispatch];
     if(range.geometry_page>=native_geometry_pages_.size()||!native_geometry_pages_[range.geometry_page].d3d12)return finish(E_FAIL);
     d3d12_->set_resource(1,native_geometry_pages_[range.geometry_page].d3d12.get());
     const std::array<UINT,4> work_range{range.first,range.count,0,0};
     d3d12_->set_constants(4,work_range.data(),sizeof(work_range));
     UINT width=0,height=0;if(!native_dispatch_dimensions(range.count,width,height))return finish(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
     const UINT chunks=bake?active->geometry_chunks[dispatch]:native_shadow_chunks(layout,range,true);
     if((hr=d3d12_->dispatch(*build_cs,width,height,chunks))!=S_OK)return FAILED(hr)?finish(hr):wait(hr);
     ++perf_native_compute_dispatches_;++native_frame_stats_.native_compute_dispatches;
    }
    // The depth draws: every batch with triangles in this map.
    bind_common();
    d3d12_->set_resource(4,native12_.visible.get());
    d3d12_->set_resource(8,native12_.world_vertices.get());
    // The shelter bake also writes its foliage mask (nativeShadowPS colour);
    // sun and point maps bind no colour target.
    d3d12_->set_depth_target(&maps,target_slice,viewport,rain?rain12_.foliage.get():nullptr);
    static const char* const cascades[5]={"owned_directional_shadow_c0","owned_directional_shadow_c1","owned_directional_shadow_c2","owned_directional_shadow_c3","owned_directional_shadow_c4"};
    transition_profile_pass(rain?"precip_shelter":point?"owned_point_shadow":cascades[slice]);
    for(UINT group=0;group<batches;++group){
     const auto& batch=layout.batches[group];
     const auto group_triangles=batch.slice_triangles[slice];const auto group_draw_count=batch.slice_records[slice];
     if(!group_triangles)continue; // CPU-known empty means no indirect call
     if(batch.geometry_page>=native_geometry_pages_.size()||!native_geometry_pages_[batch.geometry_page].d3d12)return finish(E_FAIL);
     d3d12_->set_resource(1,native_geometry_pages_[batch.geometry_page].d3d12.get());
     const auto& bucket=native_state_buckets_[batch.state_bucket];FixedState state=bucket.state;
     state.render[D3DRENDERSTATE_ZENABLE]=D3DZB_TRUE;state.render[D3DRENDERSTATE_ZWRITEENABLE]=TRUE;state.render[D3DRENDERSTATE_ZFUNC]=D3DCMP_LESSEQUAL;
     state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=FALSE;
     if(shadow_foliage_opaque(native_far_foliage_opaque_,point,slice,batch.foliage?native_foliage_part_flag:0u,
        state.render[D3DRENDERSTATE_ALPHATESTENABLE]!=0))
      state.render[D3DRENDERSTATE_ALPHATESTENABLE]=FALSE;
     // Shelter bake foliage, as the D3D11 flush: alpha tested at a low fixed
     // reference, its mip along the card's less foreshortened axis
     // (nativeShadowPS, nativeContactLayout.z), so only leaf texels cover.
     const bool shelter_foliage=rain&&batch.foliage&&(bucket.texture_srv||bucket.texture_d3d12);
     if(shelter_foliage){
      state.render[D3DRENDERSTATE_ALPHATESTENABLE]=TRUE;
      state.render[D3DRENDERSTATE_ALPHAFUNC]=D3DCMP_GREATER;state.render[D3DRENDERSTATE_ALPHAREF]=64; // alpha > 0.25
     }
     Constants constants{};constants.native_triangle_base=batch.output_triangle_base;
     constants.layout_origin[2]=shelter_foliage?1.f:0.f;
     constants.textured=state.render[D3DRENDERSTATE_ALPHATESTENABLE]&&(bucket.texture_srv||bucket.texture_d3d12)?1u:0u;
     constants.alpha_test=state.render[D3DRENDERSTATE_ALPHATESTENABLE]!=0;constants.alpha_func=state.render[D3DRENDERSTATE_ALPHAFUNC];
     constants.alpha_ref=(state.render[D3DRENDERSTATE_ALPHAREF]&255)/256.f;
     constants.alpha_op=state.stage[0][D3DTSS_ALPHAOP];constants.alpha_arg1=state.stage[0][D3DTSS_ALPHAARG1];constants.alpha_arg2=state.stage[0][D3DTSS_ALPHAARG2];
     constants.texture_factor[3]=(state.render[D3DRENDERSTATE_TEXTUREFACTOR]>>24)/255.f;
     d3d12_->set_constants(0,&constants,sizeof(constants));
     D3D12NativeState draw_state;
     const DWORD cull=state.render[D3DRENDERSTATE_CULLMODE];
     draw_state.raster=fixed_raster_desc(cull,false,true);
     draw_state.depth=fixed_depth_desc(state.render[D3DRENDERSTATE_ZENABLE],state.render[D3DRENDERSTATE_ZWRITEENABLE],state.render[D3DRENDERSTATE_ZFUNC]);
     // Neighbour-map casters are pancaked (no depth clip), as on D3D11.
     if(active==&native_neighbour_shadow_bake_)draw_state.raster.DepthClipEnable=FALSE;
     // Terrain sun casters take a slope-scaled bias clamped in world depth (L-084).
     // D3D12 bakes the bias into the pipeline. The exact clamp follows the
     // map's depth range (bias[1], 16-unit bins that move with pans and zoom),
     // so each new range was a new pipeline whose first use left every map of
     // the pass unshadowed for that frame (wait below): the D3D12 shadow
     // blink. Quarter-octave floor of the clamp: at most 16% under the D3D11
     // value, a small fixed pipeline set the start warm-up keeps.
     if(bucket.semantic_kind==SemanticKind::terrain){
      constexpr float terrain_slope_texels=2.f,terrain_clamp_world=.5f;
      draw_state.raster.SlopeScaledDepthBias=terrain_slope_texels;
      const float clamp=point?0.f:terrain_clamp_world*native_shadow_frame_.slices[slice].bias[1];
      draw_state.raster.DepthBiasClamp=clamp>0?std::exp2(std::floor(std::log2(clamp)*4.f)/4.f):0.f;
     }
     if(constants.alpha_test){
      const auto sampling=world_texture_sampler(state.stage[0][D3DTSS_MINFILTER],state.stage[0][D3DTSS_MAGFILTER],state.stage[0][D3DTSS_MIPFILTER],
       state.stage[0][D3DTSS_ADDRESSU],state.stage[0][D3DTSS_ADDRESSV],true,1u<<configured_integer(debug_overlay_.config(),"anisotropy",3));
      d3d12_->set_sampler(0,sampling.description);
     }
     // Every shelter caster runs the pixel stage, so the mask holds the top
     // surface's class (a roof over a crown is solid).
     static const ShaderProgram no_pixel{};
     hr=d3d12_->draw_indirect(*shadow_vs,constants.alpha_test||rain?*shadow_ps:no_pixel,draw_state,&viewport,*native12_.arguments,group*16u);
     if(FAILED(hr))return finish(hr);
     if(d3d12_not_done(hr)){
      // Self-report: which map and caster class lost this frame's sun shadows.
      static unsigned reports=0;
      if(!rain&&!point&&reports<64){
       ++reports;char detail[192];
       std::snprintf(detail,sizeof(detail),"frame=%llu slice=%u phase=%u %s caster=%s clamp=%.6g dirty=0x%x",
        static_cast<unsigned long long>(native_frame_index_),slice,native_shadow_phase_,hr==S_FALSE?"pipeline_compiling":"ring_dropped",
        bucket.semantic_kind==SemanticKind::terrain?"terrain":constants.alpha_test?"cutout":"opaque",
        double(draw_state.raster.DepthBiasClamp),unsigned(native_shadow_dirty_mask_));
       bridge_append_probe_event("d3d12_sun_shadow_pass_waited",detail);
      }
      return wait(hr);
     }
     ++native_shadow_draws_;native_shadow_triangles_+=group_triangles;
     ++native_frame_stats_.native_shadow_draws;native_frame_stats_.native_shadow_triangles+=group_triangles;
     perf_native_draws_+=group_draw_count;++perf_native_batches_;perf_native_vertices_+=group_triangles*3;
     profiler_.add_draw(group_triangles,group_draw_count);profiler_.add_batch();
     d3d12_->count(point?"point shadow caster batches drawn":"sun shadow caster batches drawn");
    }
   }
   // Renderer-owned per-frame casters (ground cover, critters) join the close maps'
   // per-frame layer once per collection, through the same map, viewport and
   // slice constants as the records, as the D3D11 flush draws them.
   if(!bake&&!point&&!rain&&!static_layer&&slice<3&&!(native_shadow_extras_mask_&bit)){
    native_shadow_extras_mask_|=std::uint8_t(bit);
    if(FAILED(hr=upload_native_shadow_constants(slice)))return finish(hr);
    transition_profile_pass("owned_shadow_casters");
    d3d12_->clear_slots();
    d3d12_->set_constants(2,&shadow12_.constants,sizeof(shadow12_.constants));
    d3d12_->set_depth_target(&maps,target_slice,viewport);
    draw_ground_cover_shadows_d3d12(slice,&viewport);draw_critter_shadows_d3d12(slice,&viewport);
   }
   if(static_layer){native_shadow_static_valid_[slice]=true;native_shadow_neighbour_drawn_[slice]=neighbour_caster_key();}
   else if(!point&&!rain)native_shadow_valid_[slice]=true;
  }
  return finish(S_OK);
 }catch(const std::bad_alloc&){return finish(E_OUTOFMEMORY);}
}

// ---- Temporal upscalers on D3D12 (DLSS, FSR 2): composite_world's order on
// D3D11. The plain copy and the post passes write the render-size resolved
// image; the shared begin_upscaler_frame decides the reset and draws the
// camera motion (camera_motion_d3d12); the upscaler writes the output-size
// image; finish_upscaler_frame presents it (present_upscaled_d3d12). A frame
// the upscaler cannot take shows the filtered upscale of the resolved image
// (SceneComposite::upscale), as D3D11 does.

// The upscaler's targets at this frame's sizes (render: width_ x height_,
// output: the back buffer's), remade when a size changes.
HRESULT Renderer::ensure_upscaler_d3d12(){
 auto& up=upscale12_;
 HRESULT hr=S_OK;
 // All four render-size images together: the plain render-scale path leaves
 // only resolved at this size, so a switch to DLSS/FSR at the same size must
 // still create the motion and object images.
 if(!up.resolved||!d3d12_->texture_matches(*up.resolved,width_,height_,DXGI_FORMAT_B8G8R8A8_UNORM)||
    !up.motion||!d3d12_->texture_matches(*up.motion,width_,height_,DXGI_FORMAT_R16G16_FLOAT)||
    !up.object_motion||!d3d12_->texture_matches(*up.object_motion,width_,height_,DXGI_FORMAT_R16G16_FLOAT)||
    !up.object_depth||!d3d12_->texture_matches(*up.object_depth,width_,height_,DXGI_FORMAT_R32_FLOAT)){
  up.resolved.reset();up.motion.reset();up.object_motion.reset();up.object_depth.reset();
  if(FAILED(hr=d3d12_->create_target(width_,height_,up.resolved,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_UNKNOWN))||
     FAILED(hr=d3d12_->create_target(width_,height_,up.motion,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_UNKNOWN))||
     FAILED(hr=d3d12_->create_target(width_,height_,up.object_motion,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_UNKNOWN))||
     FAILED(hr=d3d12_->create_target(width_,height_,up.object_depth,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_UNKNOWN))){
   up.resolved.reset();up.motion.reset();up.object_motion.reset();up.object_depth.reset();
   return hr;
  }
  dlss_reset_=true;
 }
 if(!up.output||!d3d12_->texture_matches(*up.output,output_width_,output_height_,DXGI_FORMAT_R8G8B8A8_UNORM)){
  up.output.reset();
  if(FAILED(hr=d3d12_->create_storage_texture(output_width_,output_height_,0,DXGI_FORMAT_R8G8B8A8_UNORM,up.output)))return hr;
  dlss_reset_=true;
 }
 return S_OK;
}

// apply_render_scale on D3D12, between frames: the render scale and the
// upscaler modes of the options, the world size, and what an upscaler turned
// off leaves (FSR's images, the targets, the host's NGX feature). The world
// targets follow the size at the next bind_world_d3d12. Never while a world
// target is bound: that frame keeps the size it was drawn at.
void Renderer::apply_upscaler_mode_d3d12(){
 if(world_d3d12_bound_)return;
 const int percent=std::clamp(configured_integer(debug_overlay_.config(),"render_scale",100),kRenderScaleMinimum,100);
 const int dlss=configured_dlss_mode(),fsr=configured_fsr_mode();
 if(percent==render_scale_percent_&&dlss==dlss_mode_&&fsr==fsr_mode_)return;
 render_scale_percent_=percent;
 const bool mode_changed=dlss!=dlss_mode_||fsr!=fsr_mode_;
 const int old_dlss=dlss_mode_;
 dlss_mode_=dlss;fsr_mode_=fsr;set_world_size();
 if(!mode_changed)return; // a render scale change alone: the targets follow the size
 dlss_reset_=true;dlss_previous_valid_=false;
 if(fsr_mode_<0)upscale12_.fsr.reset();
 if(dlss_mode_<0)upscale12_.sharpen.reset();
 if(!temporal_upscaler()){
  auto& up=upscale12_;
  up.resolved.reset();up.motion.reset();up.object_motion.reset();up.object_depth.reset();up.output.reset();up.previous_world.reset();
 }
 if(old_dlss>=0&&dlss_mode_<0){
#ifdef EID3D11_EMBEDDED
  embedded_d3d12_.dlss_release();
#else
  x64remote::dlss_release();
#endif
 }
}

// begin_dlss_object_motion on D3D12, at a world frame's start with an
// upscaler: no object motion anywhere (SceneComposite::clear_object_motion:
// zero motion, depth beyond any surface), and the world vertices as the
// previous frame left them (the used range, same slots), so a posed
// record's vertex can read where it was.
void Renderer::begin_object_motion_d3d12() noexcept{
 auto& up=upscale12_;
 up.motion_constants_frame=UINT64_MAX;
 if(!temporal_upscaler()||!up.object_motion||!up.object_depth){up.previous_world.reset();return;}
 const float zero[4]{},far_away[4]{2.f,2.f,2.f,2.f};
 d3d12_->clear_texture(*up.object_motion,zero);
 d3d12_->clear_texture(*up.object_depth,far_away);
 if(!native12_.world_vertices||!native_world_vertex_used_){up.previous_world.reset();return;}
 const UINT64 bytes=d3d12_->buffer_bytes(*native12_.world_vertices);
 if(!up.previous_world||d3d12_->buffer_bytes(*up.previous_world)!=bytes){
  up.previous_world.reset();
  if(FAILED(d3d12_->create_buffer(bytes,sizeof(NativeWorldVertexGpu),0,up.previous_world))){
   up.previous_world.reset();
   static bool reported=false;
   if(!reported){reported=true;bridge_append_probe_event("dlss_object_motion_omitted","previous world vertex copy could not be created; figures use camera motion");}
   return;
  }
 }
 if(FAILED(d3d12_->copy_buffer(*up.previous_world,*native12_.world_vertices,UINT64(native_world_vertex_used_)*sizeof(NativeWorldVertexGpu))))
  up.previous_world.reset();
}

// The object motion targets can be written now (a world pass with a temporal
// upscaler, its motion targets and last frame's world vertices), with the b11
// constants (NativeMotionConstants: last frame's unjittered world clip, the
// jitter and the render size) written once a world frame, as
// dlss_motion_constants_current on D3D11.
bool Renderer::object_motion_ready_d3d12() noexcept{
 auto& up=upscale12_;
 if(!temporal_upscaler()||scene_composited_||hit_test_active_||!world_d3d12_bound_||!world_d3d12_||!up.previous_world||
    !up.object_motion||!up.object_depth)return false;
 if(up.motion_constants_frame!=native_frame_index_){
  const auto scene=bridge_scene_world_to_clip_snapshot();
  const std::array<float,16>& previous=dlss_previous_valid_?dlss_previous_clip_:scene.valid?scene.world_to_clip:native_world_clip_;
  std::memcpy(up.motion_constants.data(),previous.data(),64);
  up.motion_constants[16]=dlss_jitter_[0];up.motion_constants[17]=dlss_jitter_[1];
  up.motion_constants[18]=float(width_);up.motion_constants[19]=float(height_);
  up.motion_constants_frame=native_frame_index_;
 }
 return true;
}

// draw_dlss_object_motion on D3D12: after a posed batch's material draw
// (flush_native_d3d12), the same indirect draw through nativeMotionVS/PS
// into the object motion targets, depth tested LESS_EQUAL against what the
// batch just wrote, opaque. b11: the previous clip, the jitter and the
// render size; t46: last frame's world vertices. The caller binds its world
// targets again. False: nothing drawn (no upscaler, history or pipeline).
bool Renderer::draw_object_motion_d3d12(const ShaderProgram& vertex,const ShaderProgram& pixel,const D3D12NativeState& batch_state,
                                        const RECT* scissor,UINT args_offset){
 auto& up=upscale12_;
 if(!object_motion_ready_d3d12())return false;
 D3D12NativeState state=batch_state;
 state.depth=D3D11_DEPTH_STENCIL_DESC{};
 state.depth.DepthEnable=TRUE;state.depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;state.depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
 state.blend=FixedBlend{};state.custom_blend=D3D12NativeState::CustomBlend{};
 D3D12Texture* const targets[2]={up.object_motion.get(),up.object_depth.get()};
 d3d12_->set_targets(targets,2,world_d3d12_.get());
 d3d12_->set_constants(11,up.motion_constants.data(),UINT(sizeof(up.motion_constants)));
 d3d12_->set_resource(46,up.previous_world.get());
 const HRESULT hr=d3d12_->draw_indirect(vertex,pixel,state,scissor,*native12_.arguments,args_offset);
 // b11 is also the ground cover and critter frame slot: zero after (unbound on D3D11).
 const std::array<float,20> zero{};
 d3d12_->set_constants(11,zero.data(),UINT(sizeof(zero)));
 d3d12_->set_resource(46,static_cast<const D3D12Buffer*>(nullptr));
 if(hr==S_FALSE)d3d12_->omit("posed-figure object motion (pipeline not ready: camera motion)",false);
 return hr==S_OK;
}

// SceneComposite::camera_motion on D3D12: cameraMotionPS over the render
// size from the world depth (t3), the object motion where a posed record
// wrote it (t1, t2), the unjittered current inverse and previous clip (b1).
void Renderer::camera_motion_d3d12(const float clip_to_world[16],const float previous[16]){
 auto& post=post12_;auto& up=upscale12_;
 if(post.programs.empty())SceneComposite::shader_programs(post.programs);
 if(post.programs.size()<10||!up.motion){d3d12_->omit("upscaler camera motion (programs or target missing)",false);return;}
 float values[48]{};
 std::memcpy(values,clip_to_world,64);std::memcpy(values+16,previous,64);
 values[32]=dlss_jitter_[0];values[33]=dlss_jitter_[1];values[34]=float(width_);values[35]=float(height_);
 const float metrics[4]={1.f/float(width_),1.f/float(height_),float(width_),float(height_)};
 d3d12_->clear_slots();
 d3d12_->set_constants(0,metrics,sizeof(metrics));
 d3d12_->set_constants(1,values,sizeof(values));
 d3d12_->set_resource(1,up.object_motion.get());
 d3d12_->set_resource(2,up.object_depth.get());
 d3d12_->set_depth_resource(3,world_d3d12_.get());
 d3d12_->set_target(up.motion.get());
 const HRESULT hr=d3d12_->draw(post.programs[0],post.programs[7],post_state(false,false),nullptr,3);
 d3d12_->clear_slots();d3d12_->set_target(nullptr);
 if(hr!=S_OK){ // no motion this frame: zero (the history is still reprojected by the camera next frame)
  const float zero[4]{};d3d12_->clear_texture(*up.motion,zero);
  d3d12_->omit("upscaler camera motion (pipeline not ready)",false);
 }
}

// SceneComposite::dlss_present on D3D12: the upscaled image to the back
// buffer (colour only: the D3D12 interface depth is never the world's).
void Renderer::present_upscaled_d3d12(){
 auto& post=post12_;
 const float metrics[4]={1.f/float(output_width_),1.f/float(output_height_),float(output_width_),float(output_height_)};
 d3d12_->clear_slots();
 d3d12_->set_constants(0,metrics,sizeof(metrics));
 d3d12_->set_resource(0,upscale12_.output.get());
 d3d12_->set_target(nullptr);
 const HRESULT hr=d3d12_->draw(post.programs[0],post.programs[8],post_state(false,false),nullptr,3);
 d3d12_->clear_slots();
 if(hr!=S_OK)d3d12_->omit("upscaled world present (pipeline not ready)",true);
 else profiler_.add_draw(1,1),profiler_.add_batch();
}

// The upscaler of the frame (DLSS or FSR 2), or the filtered upscale of the
// resolved image when it cannot run this frame (SceneComposite::upscale).
HRESULT Renderer::upscale_world_d3d12(){
 auto& post=post12_;
 if(post.programs.empty())SceneComposite::shader_programs(post.programs);
 if(post.programs.size()<10||!upscale12_.output||!upscale12_.resolved||!upscale12_.motion){d3d12_->omit("temporal upscaler (programs or targets missing)",true);return S_OK;}
 if(fsr_mode_>=0?render_fsr_d3d12():render_dlss_d3d12()){d3d12_->count(fsr_mode_>=0?"world frames upscaled (FSR 2)":"world frames upscaled (DLSS)");return S_OK;}
 const HRESULT hr=filtered_upscale_d3d12();
 if(hr==S_OK)d3d12_->count("world frames the upscaler did not take (filtered upscale)");
 return hr;
}

// SceneComposite::upscale on D3D12: the resolved image (render size) filtered
// to the back buffer with upscalePS, colour only (the D3D12 interface depth is
// never the world's). Below 100% render scale every world frame; with a
// temporal upscaler the frames it cannot take.
HRESULT Renderer::filtered_upscale_d3d12(){
 auto& post=post12_;
 if(post.programs.empty())SceneComposite::shader_programs(post.programs);
 if(post.programs.size()<6||!upscale12_.resolved){d3d12_->omit("world upscale (programs or resolved image missing)",true);return S_FALSE;}
 transition_profile_pass("scene_upscale");
 const float metrics[4]={1.f/float(width_),1.f/float(height_),float(width_),float(height_)};
 D3D11_SAMPLER_DESC linear{};linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
 linear.MaxLOD=0;linear.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
 D3D11_SAMPLER_DESC point=linear;point.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
 d3d12_->clear_slots();
 d3d12_->set_constants(0,metrics,sizeof(metrics));
 d3d12_->set_sampler(0,linear);d3d12_->set_sampler(1,point);
 d3d12_->set_resource(0,upscale12_.resolved.get());
 d3d12_->set_target(nullptr);
 const HRESULT hr=d3d12_->draw(post.programs[0],post.programs[5],post_state(false,false),nullptr,3);
 d3d12_->clear_slots();
 if(hr!=S_OK){d3d12_->omit("world upscale (pipeline not ready)",true);return S_FALSE;}
 if(!temporal_upscaler())d3d12_->count("world frames upscaled (render scale)");
 profiler_.add_draw(1,1);profiler_.add_batch();
 return S_OK;
}

// render_dlss on D3D12: NGX records its evaluation into the executor's list
// (record_external moves the inputs and the output to their states). False:
// DLSS failed this frame and is turned off with the reason on the list.
bool Renderer::render_dlss_d3d12(){
 // The first-Present deferral of render_dlss, kept for the same host.
#ifdef EID3D11_EMBEDDED
 if(!embedded_d3d12_presented_){dlss_reset_=true;dlss_previous_valid_=false;return false;}
#else
 if(!x64remote::host_presented()){dlss_reset_=true;dlss_previous_valid_=false;return false;}
#endif
 auto& up=upscale12_;
 const UpscalerFrame frame=begin_upscaler_frame();
 static const float jitter_sign=[]{char value[8]{};return GetEnvironmentVariableA("EI_RENDERER_DLSS_JITTER_SIGN",value,sizeof(value))&&value[0]=='-'?-1.f:1.f;}();
 x64remote::DlssEvaluate evaluate{};
 evaluate.jitter_x=jitter_sign*dlss_jitter_[0];evaluate.jitter_y=jitter_sign*dlss_jitter_[1];
 evaluate.mv_scale_x=evaluate.mv_scale_y=1.f;
 evaluate.render_w=width_;evaluate.render_h=height_;evaluate.out_w=output_width_;evaluate.out_h=output_height_;
 evaluate.mode=dlss_mode_;evaluate.sharpness=0.f;evaluate.reset=frame.reset;evaluate.hdr=false;evaluate.depth_inverted=false;
 transition_profile_pass("scene_dlss");
 d3d12_->clear_slots();d3d12_->set_target(nullptr);
 const D3D12Frame::ExternalUse uses[4]={{up.resolved.get(),false,false},{world_d3d12_.get(),true,false},
                                        {up.motion.get(),false,false},{up.output.get(),false,true}};
 const HRESULT hr=d3d12_->record_external("dlss",uses,4,[&](ID3D12GraphicsCommandList* list,ID3D12Resource* const* resources){
#ifdef EID3D11_EMBEDDED
  return embedded_d3d12_.dlss_evaluate(list,resources,evaluate);
#else
  return x64remote::dlss_evaluate_d3d12(list,resources,evaluate);
#endif
 });
 if(FAILED(hr)){
  char reason[96];std::snprintf(reason,sizeof(reason),"DLSS evaluation failed (0x%08lX)",static_cast<unsigned long>(hr));
  set_dlss_unavailable_reason(reason);bridge_append_probe_event("dlss_failed",reason);
  OutputDebugStringA("EID3D11 DLSS evaluation failed; DLSS turned off\n");
  dlss_previous_valid_=false;return false;
 }
 profiler_.add_draw(1,1);profiler_.add_batch();
 // The player's Sharpening after DLSS: RCAS over the output-size image in place.
 if(const float sharpness=std::clamp(configured_floating_point(debug_overlay_.config(),"upscale_sharpness",.2f),0.f,1.f);sharpness>0.f){
  transition_profile_pass("scene_dlss_rcas");
  const HRESULT sharpened=up.sharpen.apply(*d3d12_,*up.output,sharpness);
  if(FAILED(sharpened)){
   static bool reported=false;
   if(!reported){reported=true;bridge_append_probe_event("dlss_sharpen_omitted","RCAS sharpening after DLSS could not be created; the DLSS image is shown unsharpened");}
  }else if(sharpened!=S_OK)d3d12_->omit("RCAS after DLSS (pipeline not ready: unsharpened)",false);
  profiler_.add_draw(1,1);profiler_.add_batch();
 }else up.sharpen.reset();
 finish_upscaler_frame(frame);
 return true;
}

// render_fsr on D3D12 (fsr2_upscaler_d3d12.h). False: FSR failed (turned off
// for the session with the reason) or a program was not ready this frame.
bool Renderer::render_fsr_d3d12(){
 auto& up=upscale12_;
 const auto fail=[&](const char* reason){
  set_fsr_unavailable_reason(reason);bridge_append_probe_event("fsr_failed",reason);
  OutputDebugStringA("EID3D11 FSR 2 failed; FSR turned off\n");
  up.fsr.reset();dlss_previous_valid_=false;return false;
 };
 if(!up.fsr.created(width_,height_,output_width_,output_height_)){
  if(const HRESULT hr=up.fsr.create(*d3d12_,width_,height_,output_width_,output_height_);FAILED(hr)){
   char reason[96];std::snprintf(reason,sizeof(reason),"FSR could not create its resources (0x%08lX)",static_cast<unsigned long>(hr));
   return fail(reason);
  }
  dlss_reset_=true;
 }
 const UpscalerFrame frame=begin_upscaler_frame();
 Fsr2UpscalerD3D12::Frame input{};
 if(frame.camera&&!upscaler_camera_from_clip(frame.world_to_clip.data(),input.near_plane,input.far_plane,input.tan_half_fov_y))
  return fail("the camera is not a perspective projection FSR understands");
 const auto now=std::chrono::steady_clock::now();
 input.delta_seconds=fsr_last_frame_.time_since_epoch().count()?std::chrono::duration<float>(now-fsr_last_frame_).count():0.f;
 fsr_last_frame_=now;
 input.color=up.resolved.get();input.depth=world_d3d12_.get();input.motion=up.motion.get();input.output=up.output.get();
 input.jitter_x=dlss_jitter_[0];input.jitter_y=dlss_jitter_[1];
 input.jitter_phases=float(dlss_jitter_phases(output_width_,width_));
 input.sharpness=std::clamp(configured_floating_point(debug_overlay_.config(),"upscale_sharpness",.2f),0.f,1.f);
 input.reset=frame.reset;
 const HRESULT hr=up.fsr.dispatch(*d3d12_,input,[this](const char* name){transition_profile_pass(name);profiler_.add_draw(1,1);profiler_.add_batch();});
 if(FAILED(hr)){
  char reason[96];std::snprintf(reason,sizeof(reason),"FSR dispatch failed (0x%08lX)",static_cast<unsigned long>(hr));
  return fail(reason);
 }
 if(hr!=S_OK){ // a program still compiling (or a ring exhausted): this frame takes the filtered upscale
  d3d12_->count("FSR frames waiting for their pipelines");
  dlss_reset_=true;dlss_previous_valid_=false;return false;
 }
 finish_upscaler_frame(frame);
 return true;
}

// ---- Scene post on D3D12 (composite_world's passes after the resolve):
// depth of field, screen-space sun rays and bloom with their own programs,
// targets and constants, each a sequence of full-screen triangles into its
// targets and finally over the output (scene_output_d3d12). The world colour
// and depth are read from the world target. S_FALSE: nothing drawn (an
// inactive lens, or a program still compiling: counted).

HRESULT Renderer::depth_of_field_d3d12(int quality,float strength,const float clip_to_world[16],double seconds){
 auto& dof=post12_.dof;
 quality=std::clamp(quality,0,2);
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 const float work_height=float(height>1440?(height+2)/3:(height+1)/2);
 const DepthOfFieldLens lens=quality>0?depth_of_field_lens(clip_to_world,strength,work_height):DepthOfFieldLens{};
 if(!lens.active){dof.history_valid=false;return S_FALSE;}
 if(dof.programs.empty())DepthOfFieldPass::shader_programs(dof.programs); // quality 1, then 2: dofVS and five passes each
 if(dof.programs.size()<12)return E_FAIL;
 const ShaderProgram* const own=dof.programs.data()+(quality>=2?6:0);
 HRESULT hr=S_OK;
 if(width!=dof.width||height!=dof.height){
  for(auto& target:dof.work)target.reset();
  for(auto& target:dof.focus)target.reset();
  dof.history_valid=false;dof.width=width;dof.height=height; // a failed size is not retried until it changes
  dof.divisor=height>1440?3u:2u;
  dof.work_width=(std::max)(1u,(width+dof.divisor-1)/dof.divisor);dof.work_height=(std::max)(1u,(height+dof.divisor-1)/dof.divisor);
  for(UINT n=0;n<2;++n)
   if(FAILED(hr=d3d12_->create_target(dof.work_width,dof.work_height,dof.work[n],DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_UNKNOWN))||
      FAILED(hr=d3d12_->create_target(1,1,dof.focus[n],DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_UNKNOWN))){
    dof.work[0].reset();return hr;
   }
 }
 if(!dof.work[0]||!dof.work[1]||!dof.focus[0]||!dof.focus[1])return E_FAIL;
 const double elapsed=seconds-dof.last_seconds;
 const float blend=dof.history_valid&&elapsed>=0?float(1.0-std::exp(-std::min(elapsed,10.0)/DepthOfFieldPass::kFocusSeconds)):1.f;
 const float values[16]={clip_to_world[12],clip_to_world[13],clip_to_world[14],clip_to_world[15],
                         lens.aperture,lens.margin,0.f,blend,
                         float(width),float(height),1.f/width,1.f/height,
                         float(dof.work_width),float(dof.work_height),float(dof.divisor),0.f};
 const auto pass=[&](D3D12Texture* target,const ShaderProgram& pixel,const D3D12Texture* input,const D3D12Texture* focus,bool over){
  d3d12_->clear_slots();
  d3d12_->set_constants(0,values,sizeof(values));
  d3d12_->set_sampler(0,post_linear_clamp());
  d3d12_->set_depth_resource(0,world_d3d12_.get());
  d3d12_->set_resource(1,input);d3d12_->set_resource(2,focus);
  d3d12_->set_target(target);
  return d3d12_->draw(own[0],pixel,post_state(false,over),nullptr,3);
 };
 const UINT next=dof.current_focus^1u;
 if((hr=pass(dof.focus[next].get(),own[1],nullptr,dof.focus[dof.current_focus].get(),false))!=S_OK)return hr;
 dof.current_focus=next;dof.last_seconds=seconds;
 const D3D12Texture* const focus=dof.focus[dof.current_focus].get();
 if((hr=pass(dof.work[0].get(),own[2],world_d3d12_.get(),focus,false))!=S_OK||
    (hr=pass(dof.work[1].get(),own[3],dof.work[0].get(),focus,false))!=S_OK||
    (hr=pass(dof.work[0].get(),own[4],dof.work[1].get(),focus,false))!=S_OK||
    (hr=pass(scene_output_d3d12(),own[5],dof.work[0].get(),focus,true))!=S_OK)return hr;
 dof.history_valid=true;
 profiler_.add_draw(5,5);profiler_.add_batch(5);
 return S_OK;
}

HRESULT Renderer::sun_rays_d3d12(){
 auto& rays=post12_.rays;
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 if(rays.programs.empty())SunRaysPass::shader_programs(rays.programs); // sunRaysVS, mask, blur, composite
 if(rays.programs.size()<4)return E_FAIL;
 HRESULT hr=S_OK;
 if(width!=rays.width||height!=rays.height){
  rays.mask.reset();rays.blur.reset();rays.width=width;rays.height=height; // a failed size is not retried until it changes
  const UINT w=(std::max)(1u,width/4),h=(std::max)(1u,height/4);
  if(FAILED(hr=d3d12_->create_target(w,h,rays.mask,DXGI_FORMAT_R16_FLOAT,DXGI_FORMAT_UNKNOWN))||
     FAILED(hr=d3d12_->create_target(w,h,rays.blur,DXGI_FORMAT_R16_FLOAT,DXGI_FORMAT_UNKNOWN))){rays.blur.reset();return hr;}
 }
 if(!rays.mask||!rays.blur)return E_FAIL;
 // As SunRaysPass::render: streak length, samples, tint, sky share, knee, decay, haze luminance.
 const float values[12]={sun_rays_center_[0],sun_rays_center_[1],.45f,SunRaysPass::kSamples,sun_rays_tint_[0],sun_rays_tint_[1],sun_rays_tint_[2],
                         .35f,.35f,.85f,.965f,sun_rays_haze_luma_};
 const auto pass=[&](D3D12Texture* target,const ShaderProgram& pixel,const D3D12Texture* input,bool depth,bool additive){
  d3d12_->clear_slots();
  d3d12_->set_constants(0,values,sizeof(values));
  d3d12_->set_sampler(0,post_linear_clamp());
  d3d12_->set_resource(0,input);
  d3d12_->set_depth_resource(1,depth?world_d3d12_.get():nullptr);
  d3d12_->set_target(target);
  return d3d12_->draw(rays.programs[0],pixel,post_state(additive,false),nullptr,3);
 };
 if((hr=pass(rays.mask.get(),rays.programs[1],world_d3d12_.get(),true,false))!=S_OK||
    (hr=pass(rays.blur.get(),rays.programs[2],rays.mask.get(),false,false))!=S_OK||
    (hr=pass(scene_output_d3d12(),rays.programs[3],rays.blur.get(),false,true))!=S_OK)return hr;
 profiler_.add_draw(3,3);profiler_.add_batch(3);
 return S_OK;
}

HRESULT Renderer::bloom_d3d12(int quality,float strength,float ceiling,float threshold,bool contribution_only){
 auto& bloom=post12_.bloom;
 if(quality<=0||strength<=0.f)return S_OK;
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 if(bloom.programs.empty())BloomPass::shader_programs(bloom.programs); // bloomVS, prefilter13, prefilter4, down, up, composite
 if(bloom.programs.size()<6)return E_FAIL;
 HRESULT hr=S_OK;
 if(width!=bloom.width||height!=bloom.height||quality!=bloom.quality){
  for(auto& mip:bloom.mips)mip.reset();
  bloom.width=width;bloom.height=height;bloom.quality=quality;bloom.count=0; // a failed size/quality is not retried until one changes
  const UINT wanted=quality>=2?6u:4u;
  UINT count=0;
  while(count<wanted&&(std::min(width,height)>>(count+1))>=8)++count;
  count=(std::max)(count,1u);
  for(UINT n=0;n<count;++n)
   if(FAILED(hr=d3d12_->create_target((std::max)(1u,width>>(n+1)),(std::max)(1u,height>>(n+1)),bloom.mips[n],
      DXGI_FORMAT_R11G11B10_FLOAT,DXGI_FORMAT_UNKNOWN)))return hr;
  bloom.count=count;
 }
 if(!bloom.count)return E_FAIL;
 const auto curve=bloom_knee_curve(ceiling,threshold);
 float weights=0,weight=1;
 for(UINT n=0;n<bloom.count;++n,weight*=BloomPass::kWide)weights+=weight;
 const float values[8]={curve[0],curve[1],curve[2],curve[3],strength*BloomPass::kGain/weights,BloomPass::kWide,BloomPass::kGlow,0};
 const auto pass=[&](D3D12Texture* target,const ShaderProgram& pixel,const D3D12Texture* input,bool first,bool additive){
  d3d12_->clear_slots();
  d3d12_->set_constants(0,values,sizeof(values));
  d3d12_->set_sampler(0,post_linear_clamp());
  d3d12_->set_resource(0,input);
  // The prefilter reads the world depth (t1) and this frame's particle glow (t2).
  d3d12_->set_depth_resource(1,first?world_d3d12_.get():nullptr);
  d3d12_->set_resource(2,first&&bloom.glow_frame==native_frame_index_?bloom.glow.get():nullptr);
  d3d12_->set_target(target);
  return d3d12_->draw(bloom.programs[0],pixel,post_state(additive,false),nullptr,3);
 };
 UINT issued=0;
 if((hr=pass(bloom.mips[0].get(),bloom.programs[quality>=2?1:2],world_d3d12_.get(),true,false))!=S_OK)return hr;
 ++issued;
 for(UINT n=1;n<bloom.count;++n,++issued)if((hr=pass(bloom.mips[n].get(),bloom.programs[3],bloom.mips[n-1].get(),false,false))!=S_OK)return hr;
 for(UINT n=bloom.count-1;n>0;--n,++issued)if((hr=pass(bloom.mips[n-1].get(),bloom.programs[4],bloom.mips[n].get(),false,true))!=S_OK)return hr;
 if((hr=pass(scene_output_d3d12(),bloom.programs[5],bloom.mips[0].get(),false,!contribution_only))!=S_OK)return hr;
 ++issued;
 profiler_.add_draw(issued,issued);profiler_.add_batch(issued);
 return S_OK;
}

// apply_gamma_ramp on the D3D12 backend: the back buffer copied (capture),
// then gammaPS maps each channel through the ramp's LUT (256x1; float texels,
// the executor's upload formats) back into it. An identity ramp costs nothing.
HRESULT Renderer::apply_gamma_ramp_d3d12(D3D12Texture* source,D3D12Texture* target) noexcept{
 std::array<WORD,768> ramp{};bool changed=false;
 {std::lock_guard<std::mutex> lock(gamma_lock_);
  if(gamma_identity_)return S_FALSE;
  ramp=gamma_ramp_;changed=gamma_changed_;gamma_changed_=false;}
 if(gamma_failed_)return S_FALSE;
 const auto fail=[&](HRESULT result){gamma_failed_=true;return result;};
 HRESULT hr=S_OK;
 try{if(gamma_programs_d3d12_.empty())append_gamma_shader_programs(gamma_programs_d3d12_);}catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY);}
 if(gamma_programs_d3d12_.size()<2)return fail(E_FAIL);
 if(!gamma_lut_d3d12_){
  if(FAILED(hr=d3d12_->create_texture(256,1,DXGI_FORMAT_R32G32B32A32_FLOAT,gamma_lut_d3d12_)))return fail(hr);
  changed=true;
 }
 if(changed){
  std::array<float,1024> texels{};
  const auto level=[](WORD value){return float(std::min(1.0,value/65280.0));};
  for(unsigned i=0;i<256;++i){texels[i*4]=level(ramp[i]);texels[i*4+1]=level(ramp[256+i]);texels[i*4+2]=level(ramp[512+i]);texels[i*4+3]=1.f;}
  if(FAILED(hr=d3d12_->write_texture(*gamma_lut_d3d12_,RECT{0,0,256,1},texels.data(),256*16)))return fail(hr);
 }
 const UINT width=output_width_,height=output_height_;
 if(!source){
  if(!gamma_frame_d3d12_||!d3d12_->texture_matches(*gamma_frame_d3d12_,width,height,DXGI_FORMAT_B8G8R8A8_UNORM)){
   gamma_frame_d3d12_.reset();
   if(FAILED(hr=d3d12_->create_texture(width,height,DXGI_FORMAT_B8G8R8A8_UNORM,gamma_frame_d3d12_)))return fail(hr);
  }
  transition_profile_pass("gamma_ramp");
  hr=d3d12_->capture(*gamma_frame_d3d12_,0,0,RECT{0,0,LONG(width),LONG(height)});
  if(hr!=S_OK)return SUCCEEDED(hr)?S_FALSE:hr; // minimized: nothing drawn
 }else transition_profile_pass("fg_fsr_hudless_gamma");
 d3d12_->clear_slots();
 d3d12_->set_target(target);
 d3d12_->set_resource(0,source?source:gamma_frame_d3d12_.get());d3d12_->set_resource(1,gamma_lut_d3d12_.get());
 D3D11_SAMPLER_DESC linear{};linear.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;linear.MaxLOD=D3D11_FLOAT32_MAX;
 d3d12_->set_sampler(0,linear);
 hr=d3d12_->draw(gamma_programs_d3d12_[0],gamma_programs_d3d12_[1],post_state(false,false),nullptr,3);
 d3d12_->clear_slots();
 if(FAILED(hr))return hr;
 if(d3d12_not_done(hr)){if(hr==S_FALSE)d3d12_->omit("gamma_ramp (pipeline not ready)",false);return S_OK;}
 d3d12_->count("gamma ramp passes");
 profiler_.add_draw(1,1);profiler_.add_batch();
 return S_OK;
}

#endif // !EID3D11_SHADER_EMBED
} // namespace owned
