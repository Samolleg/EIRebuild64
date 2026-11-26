// Prebuilt neighbour geometry pages and textures: device-only work the
// neighbour stream does off the render thread, in the renderer DLL (standard
// mode) and in the x64 render host (remote mode). Pure CPU layout plus
// ID3D11Device creation; no immediate context, no renderer state.
#include "native_prebuilt.h"
#include "d3d12_frame.h"
#include "texture_mips.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace owned {

using Microsoft::WRL::ComPtr;

// Prebuilt geometry pages (neighbour stream). The page layout of one mesh is
// get_native_mesh's: positions, normals, components, texture coordinates,
// diffuse and specular storage, terrain vertices, indices, each 16-aligned;
// pages keep their first 16 bytes unused like arena pages.
namespace {
// The page is reserved once (build_native_prebuilt_mesh) and must be found
// contiguous. 64 MB: each page is its own geometry buffer, so smaller pages
// cost frame time (#272: 16 MB pages drew the terrain in 32 more passes on
// D3D12 and made the D3D11 compute_build +0.2 to +0.8 ms slower, one build
// dispatch per page range). The 32-bit renderer's 64 MB reserve once failed
// on Ingos only while the startup pools set filled its address space; since
// a09af55c the largest free block stays above 300 MB through every
// neighbour prebuild there. A mesh larger than a page gets its own
// exact-size page.
constexpr std::size_t kNativePrebuiltPageBytes=64u<<20;
bool finish_native_prebuilt_page(NativePrebuiltBuilder& builder) noexcept {
 if(builder.blob.size()<=16)return true;
 const UINT bytes=static_cast<UINT>(builder.blob.size());
 D3D11_BUFFER_DESC description{};description.ByteWidth=bytes;description.Usage=D3D11_USAGE_DEFAULT;
 description.BindFlags=D3D11_BIND_SHADER_RESOURCE;description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
 const D3D11_SUBRESOURCE_DATA initial{builder.blob.data(),0,0};
 NativePrebuiltPage page;page.bytes=bytes;
 if(builder.d3d12){ // D3D12 backend: created and in upload memory here; the render thread records the copy
  if(FAILED(builder.d3d12->prepare_buffer(bytes,builder.blob.data(),page.d3d12))){builder.failed=true;return false;}
  try{builder.pages.push_back(std::move(page));}catch(...){builder.failed=true;return false;}
  builder.blob.clear();
  return true;
 }
 D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_TYPELESS;
 view.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX;view.BufferEx.NumElements=bytes/4;
 view.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW;
 if(FAILED(builder.device->CreateBuffer(&description,&initial,&page.buffer))||
    FAILED(builder.device->CreateShaderResourceView(page.buffer.Get(),&view,&page.srv))){builder.failed=true;return false;}
 try{builder.pages.push_back(std::move(page));}catch(...){builder.failed=true;return false;}
 builder.blob.clear(); // the capacity serves the next page
 return true;
}
}

std::size_t native_prebuilt_page_bytes() noexcept{return kNativePrebuiltPageBytes;}

bool finish_native_prebuilt_pages(NativePrebuiltBuilder& builder) noexcept {
 const bool ok=!builder.failed&&finish_native_prebuilt_page(builder);
 std::vector<std::uint8_t>().swap(builder.blob);
 return ok;
}

// A liquid vertex's type bits (NativeMesh::liquid_types). 8 (native_liquid_flowing):
// the fall tessellation may split and displace it, i.e. its tess weight
// (liquid_surface.hlsl liquidTessWeight = liquidFoamLayerWeight) can be
// above zero: not self-illuminated, with cascade foam or any current (the
// shader's .015 run onset is not relied on, so this never misses a fall).
std::uint8_t native_liquid_vertex_types(const NativeTerrainVertex& vertex) noexcept{
 const bool flowing=vertex.liquid_self_illumination<=0&&
  (vertex.liquid_cascade>0||vertex.liquid_current[0]!=0||vertex.liquid_current[1]!=0);
 return std::uint8_t((vertex.liquid_self_illumination>0?2u:1u)|(((vertex.flags>>native_terrain_swamp_shift)&7u)!=0?4u:0u)|
                     (flowing?native_liquid_flowing:0u));
}

bool build_native_prebuilt_mesh(NativePrebuiltBuilder& builder,const NativeGeometryView& view,
 NativePrebuiltMesh& output,const NativePrebuiltMesh* shared) noexcept try{
 output={};
 if(builder.failed||(!builder.device&&!builder.d3d12)||!view.position_groups||!view.normal_groups||!view.component_count||
    !view.texture_coordinate_count||!view.index_count||view.diffuse||view.specular||view.decode_colors||
    view.component_parts||view.morphs||view.morph_count||
    (view.world.enabled&&view.world.shape_count!=1&&view.world.shape_count!=8)||
    view.position_groups>UINT_MAX/4u||view.normal_groups>UINT_MAX/4u)return false;
 // An 8-shape mesh stores all its shapes (get_native_mesh's layout); decoded
 // sources have one.
 const UINT shape_count=view.world.enabled?view.world.shape_count:1u;
 if(shape_count!=1&&view.decode)return false;
 const std::uint32_t position_count=view.position_groups*4u,normal_count=view.normal_groups*4u;
 const bool has_terrain=(view.decoded_channels&native_decoded_terrain)!=0;
 const std::uint64_t position_bytes=(std::uint64_t)view.position_groups*sizeof(NativePackedGroup)*shape_count;
 const std::uint64_t normal_bytes=(std::uint64_t)view.normal_groups*sizeof(NativePackedGroup);
 const std::uint64_t component_bytes=(std::uint64_t)view.component_count*sizeof(NativeGpuComponent);
 const std::uint64_t texture_bytes=(std::uint64_t)view.texture_coordinate_count*sizeof(NativeTextureCoordinate);
 const std::uint64_t color_bytes=(std::uint64_t)normal_count*sizeof(std::uint32_t);
 const std::uint64_t terrain_bytes=has_terrain?(std::uint64_t)view.component_count*sizeof(NativeTerrainVertex):0;
 const std::uint64_t index_bytes=(std::uint64_t)view.index_count*sizeof(std::uint32_t);
 // Decode exactly as a get_native_mesh miss would.
 std::vector<NativePackedGroup> decoded_positions,decoded_normals;
 std::vector<NativeTextureCoordinate> decoded_texture_coordinates;
 std::vector<NativeVertexComponent> decoded_components;
 std::vector<std::uint16_t> decoded_indices;
 std::vector<NativeTerrainVertex> decoded_terrain;
 const NativePackedGroup* positions=view.positions;const NativePackedGroup* normals=view.normals;
 const NativeTextureCoordinate* texture_coordinates=view.texture_coordinates;
 const NativeVertexComponent* source_components=view.components;const std::uint16_t* source_indices=view.indices;
 const NativeTerrainVertex* terrain=view.terrain_vertices;std::uint32_t terrain_count=view.terrain_vertex_count;
 std::uint32_t steep_index_begin=UINT32_MAX;
 if(view.decode){
  decoded_positions.resize(view.position_groups);decoded_normals.resize(view.normal_groups);
  decoded_texture_coordinates.resize(view.texture_coordinate_count);
  decoded_components.resize(view.component_count);decoded_indices.resize(view.index_count);
  if(has_terrain)decoded_terrain.resize(view.component_count);
  NativeGeometryDecodeTarget target{};
  target.positions=decoded_positions.data();target.position_groups=view.position_groups;
  target.normals=decoded_normals.data();target.normal_groups=view.normal_groups;
  target.texture_coordinates=decoded_texture_coordinates.data();target.texture_coordinate_count=view.texture_coordinate_count;
  target.components=decoded_components.data();target.component_count=view.component_count;
  target.indices=decoded_indices.data();target.index_count=view.index_count;
  target.terrain_vertices=decoded_terrain.empty()?nullptr:decoded_terrain.data();
  target.terrain_vertex_count=static_cast<std::uint32_t>(decoded_terrain.size());
  if(!view.decode(view.decode_context,target))return false;
  steep_index_begin=target.steep_index_begin;
  positions=decoded_positions.data();normals=decoded_normals.data();
  texture_coordinates=decoded_texture_coordinates.data();source_components=decoded_components.data();
  source_indices=decoded_indices.data();terrain=target.terrain_vertices;terrain_count=target.terrain_vertex_count;
 }
 if(!positions||!normals||!source_components||!texture_coordinates||!source_indices)return false;
 if(has_terrain&&(!terrain||terrain_count!=view.component_count))return false;
 // Layout, relative to the mesh.
 std::uint64_t cursor=0;
 const auto reserve=[&](std::uint64_t bytes){cursor=(cursor+15u)&~std::uint64_t{15};const auto at=cursor;cursor+=bytes;return at;};
 // Object-space meshes: their bounding sphere (centre, radius; object units)
 // in the 64 bytes before the positions (nativeImpostorSelect/-Frame), so a
 // mesh sharing another's channels still carries its own sphere.
 const bool sphere_block=view.world.enabled&&view.world.object_space&&shape_count==1&&!has_terrain;
 // A shape variant of a figure already in the open page: only its sphere and
 // positions are new. Everything else (and the LOD levels) is the shared
 // mesh's, which the figure's every shape reads unchanged.
 if(shared&&(shared->page_slot!=builder.pages.size()||builder.blob.size()<=16||!sphere_block||!shared->sphere_block||
    shared->shape_count!=1||shared->position_groups!=view.position_groups||shared->normal_groups!=view.normal_groups||
    shared->component_count!=view.component_count||shared->texture_coordinate_count!=view.texture_coordinate_count||
    shared->index_count!=view.index_count||shared->decoded_channels!=view.decoded_channels||view.decode||
    builder.blob.size()+64u+position_bytes+32u>kNativePrebuiltPageBytes))shared=nullptr; // it must fit the open page
 const auto sphere_at=sphere_block?reserve(64):0;
 const auto position_at=reserve(position_bytes);
 const auto normal_at=shared?0:reserve(normal_bytes),
  component_at=shared?0:reserve(component_bytes),
  texture_at=shared?0:reserve(texture_bytes),diffuse_at=shared?0:reserve(color_bytes),specular_at=shared?0:reserve(color_bytes),
  terrain_at=has_terrain?reserve(terrain_bytes):0;
 // Static LOD levels: a 64-byte table directly before the full index list
 // ({levels}, then per level {index offset, triangles, error}), the reduced
 // index lists after it.
 const NativeMeshLods* lods=!shared&&view.lods&&view.lods->levels&&!has_terrain?view.lods:nullptr;
 const auto lod_table_at=lods?reserve(64):0;
 const auto index_at=shared?0:reserve(index_bytes);
 std::array<std::uint64_t,kStaticLodMaxLevels> lod_at{};
 for(std::uint32_t level=0;lods&&level<lods->levels;++level){
  for(const std::uint32_t index:lods->indices[level])if(index>=view.component_count)return false;
  lod_at[level]=reserve(lods->indices[level].size()*sizeof(std::uint32_t));
 }
 const std::uint64_t total=(cursor+15u)&~std::uint64_t{15};
 if(16u+total>kNativeRawBufferLimit)return false;
 if(builder.blob.size()>16&&builder.blob.size()+total>kNativePrebuiltPageBytes&&!finish_native_prebuilt_page(builder))return false;
 // A page reserves its final size once (kNativePrebuiltPageBytes, or one
 // mesh larger than that): appended meshes never grow it, and the next page reuses the
 // capacity (finish_native_prebuilt_page clears it). Growing it with the
 // appends asked the 32-bit process for one contiguous block of up to
 // ~96 MB with a copy each time (vector growth 1.5x); on Ingos with its
 // neighbours those requests failed (#272, memory_failures.log).
 if(builder.blob.empty()){
  builder.blob.reserve((std::max)(kNativePrebuiltPageBytes,static_cast<std::size_t>(16u+total)));
  builder.blob.assign(16,0);
 }
 const std::size_t base=builder.blob.size();
 if(base+total>kNativeRawBufferLimit)return false;
 builder.blob.resize(base+static_cast<std::size_t>(total));
 std::uint8_t* const out=builder.blob.data()+base;
 std::memcpy(out+position_at,positions,static_cast<std::size_t>(position_bytes));
 if(!shared)std::memcpy(out+normal_at,normals,static_cast<std::size_t>(normal_bytes));
 auto* components=reinterpret_cast<NativeGpuComponent*>(out+component_at);
 for(std::uint32_t index=0;!shared&&index<view.component_count;++index){
  const auto& component=source_components[index];
  if(component.position>=position_count||component.normal>=normal_count||
     component.texture>=view.texture_coordinate_count){builder.blob.resize(base);return false;}
  components[index]={component.position,component.normal,component.texture,0u};
 }
 if(!shared)std::memcpy(out+texture_at,texture_coordinates,static_cast<std::size_t>(texture_bytes));
 if(has_terrain)std::memcpy(out+terrain_at,terrain,static_cast<std::size_t>(terrain_bytes));
 auto* indices=reinterpret_cast<std::uint32_t*>(out+index_at);
 for(std::uint32_t index=0;!shared&&index<view.index_count;++index){
  if(source_indices[index]>=view.component_count){builder.blob.resize(base);return false;}
  indices[index]=source_indices[index];
 }
 const auto at=[&](std::uint64_t relative){return static_cast<std::uint32_t>(base+relative);};
 if(lods){
  auto* table=reinterpret_cast<std::uint32_t*>(out+lod_table_at);
  table[0]=lods->levels;
  for(std::uint32_t level=0;level<lods->levels;++level){
   const auto& list=lods->indices[level];
   std::memcpy(out+lod_at[level],list.data(),list.size()*sizeof(std::uint32_t));
   std::uint32_t* entry=table+4*(level+1);
   entry[0]=static_cast<std::uint32_t>(lod_at[level]-lod_table_at);entry[1]=static_cast<std::uint32_t>(list.size()/3);
   std::memcpy(&entry[2],&lods->error[level],sizeof(float));
  }
  output.lod_levels=lods->levels;output.lod_extra_triangles=static_cast<std::uint32_t>(lods->indices[0].size()/3);
 }
 if(sphere_block&&view.world.impostor_sphere[3]>0){
  // A part of an impostor object: the object's sphere in this part's frame.
  const auto& sphere=view.world.impostor_sphere;
  if(!std::isfinite(sphere[0]+sphere[1]+sphere[2]+sphere[3])){builder.blob.resize(base);return false;}
  std::memcpy(out+sphere_at,sphere.data(),sizeof(float)*4);output.sphere_block=true;
 }else if(sphere_block){
  constexpr float big=std::numeric_limits<float>::max();float low[3]={big,big,big},high[3]={-big,-big,-big};
  const auto point=[&](std::uint32_t index,unsigned axis){const auto& g=positions[index>>2];const auto lane=index&3u;
   return axis==0?g.x[lane]:axis==1?g.y[lane]:g.z[lane];};
  for(std::uint32_t c=0;c<view.component_count;++c)for(unsigned axis=0;axis<3;++axis){
   const float value=point(source_components[c].position,axis);
   low[axis]=(std::min)(low[axis],value);high[axis]=(std::max)(high[axis],value);
  }
  float sphere[4]={(low[0]+high[0])*.5f,(low[1]+high[1])*.5f,(low[2]+high[2])*.5f,0};
  for(std::uint32_t c=0;c<view.component_count;++c){
   float d2=0;for(unsigned axis=0;axis<3;++axis){const float d=point(source_components[c].position,axis)-sphere[axis];d2+=d*d;}
   sphere[3]=(std::max)(sphere[3],std::sqrt(d2));
  }
  if(!std::isfinite(sphere[0]+sphere[1]+sphere[2]+sphere[3])){builder.blob.resize(base);return false;}
  std::memcpy(out+sphere_at,sphere,sizeof(sphere));output.sphere_block=true;
 }
 output.page_slot=static_cast<std::uint32_t>(builder.pages.size());
 output.offset=static_cast<std::uint32_t>(base);output.bytes=static_cast<std::uint32_t>(total);
 output.position_offset=at(position_at);output.normal_offset=at(normal_at);output.component_offset=at(component_at);
 output.texture_coordinate_offset=at(texture_at);output.diffuse_storage_offset=at(diffuse_at);
 output.specular_storage_offset=at(specular_at);output.terrain_vertex_offset=has_terrain?at(terrain_at):UINT32_MAX;
 output.index_offset=at(index_at);
 if(shared){
  output.normal_offset=shared->normal_offset;output.component_offset=shared->component_offset;
  output.texture_coordinate_offset=shared->texture_coordinate_offset;output.diffuse_storage_offset=shared->diffuse_storage_offset;
  output.specular_storage_offset=shared->specular_storage_offset;output.index_offset=shared->index_offset;
  output.lod_levels=shared->lod_levels;output.lod_extra_triangles=shared->lod_extra_triangles;
 }
 output.position_groups=view.position_groups;output.normal_groups=view.normal_groups;output.shape_count=shape_count;
 output.component_count=view.component_count;output.texture_coordinate_count=view.texture_coordinate_count;
 output.index_count=view.index_count;output.decoded_channels=view.decoded_channels;
 // get_native_mesh's bounds and liquid/ground parts.
 output.steep_first_triangle=steep_index_begin<=view.index_count?steep_index_begin/3u:UINT32_MAX;
 output.has_liquid=false;output.has_ground=!has_terrain||terrain_count==0;
 output.liquid_types=0;
 for(std::uint32_t i=0;has_terrain&&i<terrain_count;++i){
  const bool liquid=(terrain[i].flags&native_terrain_water_vertex)!=0;
  output.has_liquid|=liquid;output.has_ground|=!liquid;
  if(liquid)output.liquid_types|=native_liquid_vertex_types(terrain[i]);
  if(liquid)output.liquid_wave_multiplier=(std::max)(output.liquid_wave_multiplier,std::abs(terrain[i].wind_multiplier));
 }
 for(unsigned axis=0;axis<3;++axis){output.bounds_min[axis]=std::numeric_limits<float>::max();output.bounds_max[axis]=-std::numeric_limits<float>::max();}
 for(std::uint32_t index=0;index<position_count;++index){
  const auto& group=positions[index>>2];const auto lane=index&3u;
  const float value[3]={group.x[lane],group.y[lane],group.z[lane]};
  for(unsigned axis=0;axis<3;++axis)if(std::isfinite(value[axis])){
   output.bounds_min[axis]=std::min(output.bounds_min[axis],value[axis]);
   output.bounds_max[axis]=std::max(output.bounds_max[axis],value[axis]);
  }
 }
 for(unsigned axis=0;axis<3;++axis)if(output.bounds_min[axis]>output.bounds_max[axis])output.bounds_min[axis]=output.bounds_max[axis]=0;
 return true;
}catch(...){output={};return false;}

std::string native_owned_archive_texture_key(const ResourceIdentity& resource,
 std::uint64_t generation,bool full_mips){
 // One file version's member decodes identically in every store, so the
 // active map and its neighbours (each with its own store) share one upload.
 std::string key=resource.container_file_key?
  "file:"+std::to_string(resource.container_file_key)+':'+std::to_string(resource.absolute_offset)+':'+
   std::to_string(resource.bytes)+':'+resource.canonical_name:
  "archive:"+resource.key();
 key+="|generation="+std::to_string(generation);
 key+=full_mips?"|mips=full":"|mips=authored";
 return key;
}

std::string native_owned_mip_texture_key(const NativeOwnedTextureView& source,bool generated){
 // An archive member carries identity 1 (retained_static_submission.cpp):
 // its archive entry is the identity, as on the D3D11 path, or every
 // uncompressed member shares "1:1" and draws the first one's pixels (#285).
 if(source.value_source==1&&source.resource)return native_owned_archive_texture_key(*source.resource,source.generation,generated);
 return std::to_string(source.value_source)+":"+std::to_string(source.identity)+"|generation="+std::to_string(source.generation)+
  (generated?"|mips=full":"|mips=authored");
}

DXGI_FORMAT native_owned_uncompressed_format(NativeOwnedTextureFormat format,UINT& pixel_bytes) noexcept{
 switch(format){
 case NativeOwnedTextureFormat::rgb565:pixel_bytes=2;return DXGI_FORMAT_B5G6R5_UNORM;
 case NativeOwnedTextureFormat::argb1555:pixel_bytes=2;return DXGI_FORMAT_B5G5R5A1_UNORM;
 case NativeOwnedTextureFormat::argb4444:pixel_bytes=2;return DXGI_FORMAT_B4G4R4A4_UNORM;
 case NativeOwnedTextureFormat::argb8888:pixel_bytes=4;return DXGI_FORMAT_B8G8R8A8_UNORM;
 default:pixel_bytes=0;return DXGI_FORMAT_UNKNOWN;
 }
}

namespace {
// One channel to 8 bits (renderer_internal.h expand_channel, the same math).
std::uint32_t expand_bits(std::uint32_t value,std::uint32_t mask,std::uint32_t absent){
 if(!mask)return absent;unsigned long shift=0;_BitScanForward(&shift,mask);const std::uint32_t maximum=mask>>shift;
 return static_cast<std::uint8_t>(((value&mask)>>shift)*255u/maximum);
}
} // namespace

bool native_owned_box_mips(const NativeOwnedTextureView& source,std::vector<std::vector<std::uint32_t>>& levels){
 levels.clear();
 UINT pixel_bytes=0;
 if(!source.mips||source.mip_count!=1||native_owned_uncompressed_format(source.format,pixel_bytes)==DXGI_FORMAT_UNKNOWN)return false;
 const NativeOwnedTextureMip& top=source.mips[0];
 const UINT width=top.width,height=top.height;
 if(!width||!height||width>16384||height>16384||!top.data||std::uint64_t(width)*height*pixel_bytes>top.bytes)return false;
 std::vector<std::uint32_t> level(std::size_t(width)*height);
 if(pixel_bytes==4)std::memcpy(level.data(),top.data,level.size()*4);
 else{
  const std::uint32_t rm=source.format==NativeOwnedTextureFormat::rgb565?0xf800:source.format==NativeOwnedTextureFormat::argb1555?0x7c00:0x0f00;
  const std::uint32_t gm=source.format==NativeOwnedTextureFormat::rgb565?0x07e0:source.format==NativeOwnedTextureFormat::argb1555?0x03e0:0x00f0;
  const std::uint32_t bm=source.format==NativeOwnedTextureFormat::argb4444?0x000f:0x001f;
  const std::uint32_t am=source.format==NativeOwnedTextureFormat::argb1555?0x8000:source.format==NativeOwnedTextureFormat::argb4444?0xf000:0;
  for(std::size_t i=0;i<level.size();++i){std::uint16_t raw;std::memcpy(&raw,top.data+i*2,2);
   level[i]=(expand_bits(raw,am,255)<<24)|(expand_bits(raw,rm,0)<<16)|(expand_bits(raw,gm,0)<<8)|expand_bits(raw,bm,0);}
 }
 const UINT count=full_mip_count(width,height);
 levels.reserve(count);
 UINT w=width,h=height;
 for(UINT index=0;;++index){
  levels.push_back(std::move(level));
  if(index+1>=count)break;
  const std::vector<std::uint32_t>& above=levels.back();
  const UINT nw=(std::max)(1u,w/2),nh=(std::max)(1u,h/2);
  level.assign(std::size_t(nw)*nh,0);
  box_mip_level(above.data(),w,h,level.data());
  w=nw;h=nh;
 }
 // A cut-out keeps its alpha-test coverage down the chain (texture_mips.h);
 // the 4444 spell atlases are blended art and stay as filtered.
 float coverage=0;
 if(source.format!=NativeOwnedTextureFormat::argb4444&&cut_out_coverage(levels[0].data(),levels[0].size(),coverage))
  for(std::size_t index=1;index<levels.size();++index)preserve_alpha_coverage(levels[index].data(),levels[index].size(),coverage);
 return true;
}

namespace {
// D3D12 backend: an uncompressed archive member as get_native_owned_texture_d3d12
// creates it for a world draw (world_mips): one level becomes the generated
// BGRA8 chain, authored levels stay as they are; under that branch's key.
bool prepare_uncompressed_d3d12(D3D12Preparer& d3d12,const NativeOwnedTextureView& source,NativePrebuiltTexture& output){
 UINT pixel_bytes=0;
 const DXGI_FORMAT format=native_owned_uncompressed_format(source.format,pixel_bytes);
 const UINT width=source.mips[0].width,height=source.mips[0].height;
 if(format==DXGI_FORMAT_UNKNOWN||!width||!height||width>16384||height>16384)return false;
 const bool generated=source.mip_count==1;
 std::vector<std::vector<std::uint32_t>> chain;
 std::vector<const void*> levels;std::vector<UINT> pitches;std::uint64_t bytes=0;
 if(generated){
  if(!native_owned_box_mips(source,chain))return false;
  UINT w=width;
  for(const auto& level:chain){levels.push_back(level.data());pitches.push_back(w*4u);bytes+=level.size()*4u;w=(std::max)(1u,w/2);}
  if(FAILED(d3d12.prepare_texture(width,height,DXGI_FORMAT_B8G8R8A8_UNORM,static_cast<UINT>(chain.size()),levels.data(),pitches.data(),
     output.d3d12)))return false;
 }else{
  for(UINT index=0;index<source.mip_count;++index){
   const NativeOwnedTextureMip& mip=source.mips[index];
   if(!mip.data||mip.width!=(std::max)(1u,width>>index)||mip.height!=(std::max)(1u,height>>index)||
      std::uint64_t(mip.width)*pixel_bytes*mip.height>mip.bytes)return false;
   levels.push_back(mip.data);pitches.push_back(mip.width*pixel_bytes);bytes+=std::uint64_t(mip.width)*pixel_bytes*mip.height;
  }
  if(FAILED(d3d12.prepare_texture(width,height,format,source.mip_count,levels.data(),pitches.data(),output.d3d12)))return false;
 }
 // The values the render-thread branch stores for its cache entry.
 output.key=native_owned_mip_texture_key(source,generated);
 output.generation=source.generation;output.format=format;
 output.width=width;output.height=height;output.mip_count=source.mip_count;output.bytes=bytes;
 return true;
}
} // namespace

// The immutable-upload branch of get_native_owned_texture (authored mips of a
// block-compressed archive member), on a worker: the same descriptor, layout
// checks and initial data. D3D12 backend: also the uncompressed branch.
bool build_native_prebuilt_texture(ID3D11Device* device,D3D12Preparer* d3d12,const NativeOwnedTextureView& source,
 NativePrebuiltTexture& output) noexcept try{
 output={};
 if(source.value_source!=1||!source.resource||!source.identity||!source.generation||source.surface||
    !source.mips||!source.mip_count||source.mip_count>32)return false;
 DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;UINT block_bytes=0;
 switch(source.format){
 case NativeOwnedTextureFormat::dxt1:format=DXGI_FORMAT_BC1_UNORM;block_bytes=8;break;
 case NativeOwnedTextureFormat::dxt3:format=DXGI_FORMAT_BC2_UNORM;block_bytes=16;break;
 case NativeOwnedTextureFormat::dxt5:format=DXGI_FORMAT_BC3_UNORM;block_bytes=16;break;
 case NativeOwnedTextureFormat::bc5:format=DXGI_FORMAT_BC5_UNORM;block_bytes=16;break;
 case NativeOwnedTextureFormat::bc7:format=DXGI_FORMAT_BC7_UNORM;block_bytes=16;break;
 case NativeOwnedTextureFormat::bc7_srgb:format=DXGI_FORMAT_BC7_UNORM_SRGB;block_bytes=16;break;
 default: // D3D11: other formats may take generated mips on the render thread
  if(device||!d3d12||!prepare_uncompressed_d3d12(*d3d12,source,output)){output={};return false;}
  return true;
 }
 const UINT width=source.mips[0].width,height=source.mips[0].height;
 if(!width||!height||width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)return false;
 std::vector<D3D11_SUBRESOURCE_DATA> subresources(source.mip_count);
 std::uint64_t bytes=0;
 for(UINT index=0;index<source.mip_count;++index){
  const NativeOwnedTextureMip& mip=source.mips[index];
  if(!mip.data||!mip.width||!mip.height||!mip.bytes||mip.width!=(std::max)(1u,width>>index)||
     mip.height!=(std::max)(1u,height>>index))return false;
  const UINT row_pitch=std::max(1u,(mip.width+3u)/4u)*block_bytes;
  const UINT rows=std::max(1u,(mip.height+3u)/4u);
  const std::uint64_t required=static_cast<std::uint64_t>(row_pitch)*rows;
  if(required>mip.bytes)return false;
  subresources[index]={mip.data,row_pitch,static_cast<UINT>(required)};
  bytes+=required;
 }
 if(!device){ // D3D12 backend: created with every level in upload memory here
  if(!d3d12)return false;
  std::vector<const void*> levels(source.mip_count);std::vector<UINT> pitches(source.mip_count);
  for(UINT index=0;index<source.mip_count;++index){levels[index]=subresources[index].pSysMem;pitches[index]=subresources[index].SysMemPitch;}
  if(FAILED(d3d12->prepare_texture(width,height,format,source.mip_count,levels.data(),pitches.data(),output.d3d12))){output={};return false;}
  output.key=native_owned_archive_texture_key(*source.resource,source.generation,false);
  output.generation=source.generation;output.format=format;
  output.width=width;output.height=height;output.mip_count=source.mip_count;output.bytes=bytes;
  return true;
 }
 D3D11_TEXTURE2D_DESC description{};description.Width=width;description.Height=height;
 description.MipLevels=source.mip_count;description.ArraySize=1;description.Format=format;
 description.SampleDesc.Count=1;description.Usage=D3D11_USAGE_IMMUTABLE;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 if(FAILED(device->CreateTexture2D(&description,subresources.data(),&output.texture))||
    FAILED(device->CreateShaderResourceView(output.texture.Get(),nullptr,&output.srv))){output={};return false;}
 output.key=native_owned_archive_texture_key(*source.resource,source.generation,false);
 output.generation=source.generation;output.format=format;
 output.width=width;output.height=height;output.mip_count=source.mip_count;output.bytes=bytes;
 return true;
}catch(...){output={};return false;}

} // namespace owned
