// Prebuilt neighbour geometry pages and textures: device-only work the
// neighbour stream does off the render thread, in the renderer DLL (standard
// mode) and in the x64 render host (remote mode). Pure CPU layout plus
// ID3D11Device creation; no immediate context, no renderer state.
#include "native_prebuilt.h"

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
constexpr std::size_t kNativePrebuiltPageBytes=64u<<20;
bool finish_native_prebuilt_page(NativePrebuiltBuilder& builder) noexcept {
 if(builder.blob.size()<=16)return true;
 const UINT bytes=static_cast<UINT>(builder.blob.size());
 D3D11_BUFFER_DESC description{};description.ByteWidth=bytes;description.Usage=D3D11_USAGE_DEFAULT;
 description.BindFlags=D3D11_BIND_SHADER_RESOURCE;description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
 const D3D11_SUBRESOURCE_DATA initial{builder.blob.data(),0,0};
 NativePrebuiltPage page;page.bytes=bytes;
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

bool finish_native_prebuilt_pages(NativePrebuiltBuilder& builder) noexcept {
 const bool ok=!builder.failed&&finish_native_prebuilt_page(builder);
 std::vector<std::uint8_t>().swap(builder.blob);
 return ok;
}

// A liquid vertex's type bits (NativeMesh::liquid_types).
std::uint8_t native_liquid_vertex_types(const NativeTerrainVertex& vertex) noexcept{
 return std::uint8_t((vertex.liquid_self_illumination>0?2u:1u)|(((vertex.flags>>native_terrain_swamp_shift)&7u)!=0?4u:0u));
}

bool build_native_prebuilt_mesh(NativePrebuiltBuilder& builder,const NativeGeometryView& view,
 NativePrebuiltMesh& output) noexcept try{
 output={};
 if(builder.failed||!builder.device||!view.position_groups||!view.normal_groups||!view.component_count||
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
 // in the 64 bytes before the components (nativeImpostorSelect/-Vertex).
 const bool sphere_block=view.world.enabled&&view.world.object_space&&shape_count==1&&!has_terrain;
 const auto position_at=reserve(position_bytes),normal_at=reserve(normal_bytes),sphere_at=sphere_block?reserve(64):0,
  component_at=reserve(component_bytes),
  texture_at=reserve(texture_bytes),diffuse_at=reserve(color_bytes),specular_at=reserve(color_bytes),
  terrain_at=has_terrain?reserve(terrain_bytes):0;
 // Static LOD levels: a 64-byte table directly before the full index list
 // ({levels}, then per level {index offset, triangles, error}), the reduced
 // index lists after it.
 const NativeMeshLods* lods=view.lods&&view.lods->levels&&!has_terrain?view.lods:nullptr;
 const auto lod_table_at=lods?reserve(64):0;
 const auto index_at=reserve(index_bytes);
 std::array<std::uint64_t,kStaticLodMaxLevels> lod_at{};
 for(std::uint32_t level=0;lods&&level<lods->levels;++level){
  for(const std::uint32_t index:lods->indices[level])if(index>=view.component_count)return false;
  lod_at[level]=reserve(lods->indices[level].size()*sizeof(std::uint32_t));
 }
 const std::uint64_t total=(cursor+15u)&~std::uint64_t{15};
 if(16u+total>kNativeRawBufferLimit)return false;
 if(builder.blob.size()>16&&builder.blob.size()+total>kNativePrebuiltPageBytes&&!finish_native_prebuilt_page(builder))return false;
 if(builder.blob.empty())builder.blob.assign(16,0);
 const std::size_t base=builder.blob.size();
 if(base+total>kNativeRawBufferLimit)return false;
 builder.blob.resize(base+static_cast<std::size_t>(total));
 std::uint8_t* const out=builder.blob.data()+base;
 std::memcpy(out+position_at,positions,static_cast<std::size_t>(position_bytes));
 std::memcpy(out+normal_at,normals,static_cast<std::size_t>(normal_bytes));
 auto* components=reinterpret_cast<NativeGpuComponent*>(out+component_at);
 for(std::uint32_t index=0;index<view.component_count;++index){
  const auto& component=source_components[index];
  if(component.position>=position_count||component.normal>=normal_count||
     component.texture>=view.texture_coordinate_count){builder.blob.resize(base);return false;}
  components[index]={component.position,component.normal,component.texture,0u};
 }
 std::memcpy(out+texture_at,texture_coordinates,static_cast<std::size_t>(texture_bytes));
 if(has_terrain)std::memcpy(out+terrain_at,terrain,static_cast<std::size_t>(terrain_bytes));
 auto* indices=reinterpret_cast<std::uint32_t*>(out+index_at);
 for(std::uint32_t index=0;index<view.index_count;++index){
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
   entry[0]=at(lod_at[level]);entry[1]=static_cast<std::uint32_t>(list.size()/3);
   std::memcpy(&entry[2],&lods->error[level],sizeof(float));
  }
  output.lod_levels=lods->levels;output.lod_extra_triangles=static_cast<std::uint32_t>(lods->indices[0].size()/3);
 }
 if(sphere_block){
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

// The immutable-upload branch of get_native_owned_texture (authored mips of a
// block-compressed archive member), on a worker: the same descriptor, layout
// checks and initial data.
bool build_native_prebuilt_texture(ID3D11Device* device,const NativeOwnedTextureView& source,
 NativePrebuiltTexture& output) noexcept try{
 output={};
 if(!device||source.value_source!=1||!source.resource||!source.identity||!source.generation||source.surface||
    !source.mips||!source.mip_count||source.mip_count>32)return false;
 DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;UINT block_bytes=0;
 switch(source.format){
 case NativeOwnedTextureFormat::dxt1:format=DXGI_FORMAT_BC1_UNORM;block_bytes=8;break;
 case NativeOwnedTextureFormat::dxt3:format=DXGI_FORMAT_BC2_UNORM;block_bytes=16;break;
 case NativeOwnedTextureFormat::dxt5:format=DXGI_FORMAT_BC3_UNORM;block_bytes=16;break;
 default:return false; // other formats may take generated mips on the render thread
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
