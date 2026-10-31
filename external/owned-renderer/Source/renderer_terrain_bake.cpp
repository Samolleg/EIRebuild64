// Terrain material cache (terrain_bake.h, issue #148): the visible terrain
// of the active map in resident pages. Each frame the wanted pages are known
// from the camera; the nearest ones without a usable resident page bake
// (the frame's visible terrain triangles drawn again into a small staging
// atlas through the live material), and the next frame compresses them into
// their slots and draws from them through the lean baked variant
// (terrain_bake 1/3). terrain_bake 2 keeps re-baking at the per-frame budget
// without drawing from the cache (the bake's cost).
#include "terrain_bake.h"
#include "renderer.h"
#include "engine_bridge.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace owned {
#if !defined(EID3D11_SHADER_EMBED)
namespace {

std::array<double,4> bake_multiply(const std::array<float,16>& m,double x,double y,double z,double w){
 std::array<double,4> r{};
 for(unsigned row=0;row<4;++row)r[row]=m[row*4]*x+m[row*4+1]*y+m[row*4+2]*z+m[row*4+3]*w;
 return r;
}
bool bake_invert(const std::array<float,16>& input,std::array<float,16>& output){
 double m[4][8]{};
 for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){m[r][c]=input[r*4+c];m[r][c+4]=r==c?1:0;}
 for(unsigned c=0;c<4;++c){
  unsigned pivot=c;
  for(unsigned r=c+1;r<4;++r)if(std::abs(m[r][c])>std::abs(m[pivot][c]))pivot=r;
  if(std::abs(m[pivot][c])<1e-12)return false;
  for(unsigned k=0;k<8;++k)std::swap(m[pivot][k],m[c][k]);
  const double d=m[c][c];
  for(unsigned k=0;k<8;++k)m[c][k]/=d;
  for(unsigned r=0;r<4;++r)if(r!=c){const double f=m[r][c];for(unsigned k=0;k<8;++k)m[r][k]-=f*m[c][k];}
 }
 for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)output[r*4+c]=static_cast<float>(m[r][c+4]);
 return true;
}
// Power of two nearest to v in log scale, within [lo, hi].
unsigned nearest_power_of_two(double v,unsigned lo,unsigned hi){
 unsigned p=lo;
 while(p<hi&&p*1.41421356<v)p*=2;
 return p;
}

// Buddy allocator of power-of-two rectangles over an atlas of 1024 x 1024
// roots: a free node splits in halves along the axis it is too large on;
// freeing merges free siblings back.
struct SlotAllocator{
 struct Node{unsigned x=0,y=0,w=0,h=0;int parent=-1,child[2]{-1,-1};bool used=false,dead=false;};
 std::vector<Node> nodes;std::vector<int> spare;
 void reset(unsigned width,unsigned height){
  nodes.clear();spare.clear();
  for(unsigned y=0;y+terrain_bake_max_slot<=height;y+=terrain_bake_max_slot)for(unsigned x=0;x+terrain_bake_max_slot<=width;x+=terrain_bake_max_slot){
   Node root;root.x=x;root.y=y;root.w=root.h=terrain_bake_max_slot;nodes.push_back(root);
  }
 }
 int make(const Node& node){
  if(!spare.empty()){const int index=spare.back();spare.pop_back();nodes[index]=node;return index;}
  nodes.push_back(node);return int(nodes.size()-1);
 }
 bool leaf(int i)const{return nodes[i].child[0]<0;}
 int allocate(unsigned w,unsigned h){
  int best=-1;
  for(int i=0;i<int(nodes.size());++i){
   const Node& node=nodes[i];
   if(node.dead||node.used||!leaf(i)||node.w<w||node.h<h)continue;
   if(best<0||std::uint64_t(node.w)*node.h<std::uint64_t(nodes[best].w)*nodes[best].h)best=i;
  }
  if(best<0)return -1;
  while(nodes[best].w>w||nodes[best].h>h){
   Node a=nodes[best],b=nodes[best];a.parent=b.parent=best;a.child[0]=a.child[1]=b.child[0]=b.child[1]=-1;
   const bool vertical=nodes[best].w>w&&(nodes[best].w/w>=nodes[best].h/h||nodes[best].h<=h);
   if(vertical){a.w=b.w=nodes[best].w/2;b.x+=a.w;}else{a.h=b.h=nodes[best].h/2;b.y+=a.h;}
   const int first=make(a),second=make(b);
   nodes[best].child[0]=first;nodes[best].child[1]=second;best=first;
  }
  nodes[best].used=true;return best;
 }
 void release(int i){
  if(i<0)return;
  nodes[i].used=false;
  for(int parent=nodes[i].parent;parent>=0;parent=nodes[parent].parent){
   const int a=nodes[parent].child[0],b=nodes[parent].child[1];
   if(nodes[a].used||nodes[b].used||!leaf(a)||!leaf(b))break;
   nodes[a].dead=nodes[b].dead=true;spare.push_back(a);spare.push_back(b);
   nodes[parent].child[0]=nodes[parent].child[1]=-1;
  }
 }
};

// map: 0 the active map, else the neighbour's contact map id.
std::uint64_t page_key(UINT map,int cell_x,int cell_y,int side){
 return std::uint64_t(std::uint32_t(cell_x)&0xfffffu)|(std::uint64_t(std::uint32_t(cell_y)&0xfffffu)<<20)|(std::uint64_t(side&255)<<40)|(std::uint64_t(map&0xffffu)<<48);
}

}  // namespace

// At map load (the ambient grids' rebuild): the cache's per-map cell tables
// on a worker, from a copy of the height grid.
void Renderer::start_terrain_bake_cells() noexcept try{
 terrain_bake_cells_.reset();terrain_bake_cells_generation_=ambient_map_generation_;
 auto height=std::make_shared<const std::vector<float>>(ambient_height_);
 const std::uint32_t tiles_x=ambient_tiles_x_,tiles_y=ambient_tiles_y_;
 terrain_bake_cells_future_=std::async(std::launch::async,[height,tiles_x,tiles_y](){
  const BackgroundWorkerPriority priority;
  return std::make_shared<const TerrainBakeCells>(terrain_bake_cells(*height,tiles_x,tiles_y));
 });
}catch(...){terrain_bake_cells_future_={};}

struct Renderer::TerrainBakeResidency{
 // One page-table slice per map: the active map (id 0) and each neighbour
 // with cell tables (id: its contact map), at its world origin.
 struct Slice{UINT id=0;std::string name;double origin_x=0,origin_y=0;int cells_x=0,cells_y=0;std::size_t first_cell=0;std::shared_ptr<const TerrainBakeCells> cells;};
 std::vector<Slice> slices;std::size_t table_cells=0;bool tables_resized=true;
 const Slice* slice(UINT id)const{for(const auto& s:slices)if(s.id==id)return &s;return nullptr;}
 // A wanted page: its map and node, nearest distance, slot and density.
 // whole: the node lies entirely inside the view frustum.
 struct Wanted{UINT map;int cell_x,cell_y,side;float distance;bool distant,whole;unsigned slot_w,slot_h,gutter;float density_x,density_y;};
 std::vector<Wanted> wanted;std::array<float,16> wanted_clip{};bool wanted_valid=false;std::uint64_t wanted_frame=0;
 std::vector<float> wanted_density; // per table cell: the wanted page's lower density (0: not wanted)
 std::uint64_t regrow_frame=0;
 // A resident page: its slot and whether its content is in the atlas.
 // partial: baked while not entirely inside the view frustum (part of its
 // ground may have been off-screen); view: the wanted set it baked under.
 struct Page{UINT map=0;int cell_x=0,cell_y=0,side=1;bool distant=false,partial=false;unsigned slot_w=0,slot_h=0,gutter=1;float density_x=0,density_y=0;int node=-1;std::uint64_t view=0;
  std::uint64_t last_wanted=0;bool baked=false,baking=false,stale=false;unsigned stage_x=0,stage_y=0;};
 std::unordered_map<std::uint64_t,Page> pages;
 std::vector<std::uint64_t> baking; // pages drawn into the staging atlas this frame, finalized the next
 SlotAllocator near_slots,far_slots;unsigned near_width=0,near_height=0,far_width=0,far_height=0;
 bool atlases=false,tables_dirty=true,covered=false;std::uint64_t bake_frame=UINT64_MAX,update_frame=UINT64_MAX,covered_frame=0,started_frame=0,report_frame=0;
 // Adaptive per-frame bake: GPU timestamps around each bake draw and the
 // finalize, read back a few frames later; the texel budget follows
 // terrain_bake_frame_ms from the measured cost per texel.
 struct Timing{ComPtr<ID3D11Query> disjoint;std::vector<std::array<ComPtr<ID3D11Query>,2>> spans;std::size_t used=0;double texels=0;bool active=false;};
 std::array<Timing,4> timing;unsigned timing_index=0;bool timing_open=false;
 double budget_texels=1048576,measured_ms=0,measured_texels=0;unsigned allocation_failures=0;
 double pixels_at_unit_depth=0;std::array<double,3> eye{};
};

#endif
void append_terrain_bake_shader_programs(std::vector<ShaderProgram>& out){
 const std::string& source=owned_native_shader_source();
 // NATIVE_TERRAIN_BAKE_PASS: the bake keeps the tile blend at every distance
 // (terrain_tile_blend.hlsl); the live variant fades it out with distance.
 static const D3D_SHADER_MACRO terrain[]={{"NATIVE_TERRAIN_PS","1"},{"NATIVE_TERRAIN_BAKE_PASS","1"},{nullptr,nullptr}};
 out.push_back({source,nullptr,nullptr,"nativeTerrainBakeVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,nullptr,"nativeTerrainBakeHS","hs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,nullptr,"nativeTerrainBakeDS","ds_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,terrain,"nativeTerrainBakePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,nullptr,"nativeTerrainBakeTessVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // Routing: the baked variant's vertex and hull stages, then the lean
 // terrain pixel variant reading the cache (NATIVE_TERRAIN_BAKED: no
 // material stages), and the same with the snow trail code.
 static const D3D_SHADER_MACRO baked[]={{"NATIVE_TERRAIN_BAKED","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO baked_snow[]={{"NATIVE_TERRAIN_BAKED","1"},{"NATIVE_SNOW_TRAILS","1"},{nullptr,nullptr}};
 out.push_back({source,nullptr,nullptr,"nativeTerrainBakedVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,nullptr,"nativeTerrainBakedHS","hs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,baked,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,baked_snow,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({{kTerrainBakeBc1Shader,sizeof(kTerrainBakeBc1Shader)-1},"terrain_bake_bc1",nullptr,"terrainBakeBc1CS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({{kTerrainBakeBc7Shader,sizeof(kTerrainBakeBc7Shader)-1},"terrain_bake_bc7",nullptr,"terrainBakeBc7CS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

#if !defined(EID3D11_SHADER_EMBED)
// Shaders, fixed buffers and the staging atlas with its block targets.
// The cache's shader objects, created with the startup shader preparation
// (prepare_shaders), never on the first bake frame: creating them there let
// the driver translate the bake programs mid-play (a 2.4 s stall).
HRESULT Renderer::ensure_terrain_bake_programs() noexcept{
 if(terrain_bake_vs_&&terrain_bake_tess_vs_&&terrain_bake_hs_&&terrain_bake_ds_&&terrain_bake_ps_&&terrain_baked_vs_&&terrain_baked_hs_&&terrain_baked_ps_&&
    terrain_baked_snow_ps_&&terrain_bake_bc1_cs_&&terrain_bake_bc7_cs_)return S_OK;
 if(terrain_bake_pipeline_failed_||!device_||feature_level_<D3D_FEATURE_LEVEL_11_0)return E_FAIL;
 std::vector<ShaderProgram> programs;append_terrain_bake_shader_programs(programs);
 ComPtr<ID3DBlob> code[11];HRESULT hr=S_OK;
 for(unsigned k=0;k<11&&SUCCEEDED(hr);++k){
  ComPtr<ID3DBlob> error;hr=compile_shader(programs[k],&code[k],&error);
  if(FAILED(hr)&&error)OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
 }
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),code[0]->GetBufferPointer(),code[0]->GetBufferSize(),nullptr,&terrain_bake_vs_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),code[1]->GetBufferPointer(),code[1]->GetBufferSize(),nullptr,&terrain_bake_hs_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),code[2]->GetBufferPointer(),code[2]->GetBufferSize(),nullptr,&terrain_bake_ds_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),code[3]->GetBufferPointer(),code[3]->GetBufferSize(),nullptr,&terrain_bake_ps_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),code[4]->GetBufferPointer(),code[4]->GetBufferSize(),nullptr,&terrain_bake_tess_vs_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),code[5]->GetBufferPointer(),code[5]->GetBufferSize(),nullptr,&terrain_baked_vs_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),code[6]->GetBufferPointer(),code[6]->GetBufferSize(),nullptr,&terrain_baked_hs_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),code[7]->GetBufferPointer(),code[7]->GetBufferSize(),nullptr,&terrain_baked_ps_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),code[8]->GetBufferPointer(),code[8]->GetBufferSize(),nullptr,&terrain_baked_snow_ps_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),code[9]->GetBufferPointer(),code[9]->GetBufferSize(),nullptr,&terrain_bake_bc1_cs_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),code[10]->GetBufferPointer(),code[10]->GetBufferSize(),nullptr,&terrain_bake_bc7_cs_);
 if(FAILED(hr))terrain_bake_pipeline_failed_=true;
 return hr;
}
HRESULT Renderer::ensure_terrain_bake_pipeline() noexcept{
 if(terrain_bake_srv_&&SUCCEEDED(ensure_terrain_bake_programs()))return S_OK;
 HRESULT hr=ensure_terrain_bake_programs();
 if(SUCCEEDED(hr)&&!terrain_bake_bc1_constants_){
  D3D11_BUFFER_DESC description{};description.ByteWidth=32;description.Usage=D3D11_USAGE_DYNAMIC;
  description.BindFlags=D3D11_BIND_CONSTANT_BUFFER;description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  hr=device_->CreateBuffer(&description,nullptr,&terrain_bake_bc1_constants_);
 }
 if(SUCCEEDED(hr)&&!terrain_bake_raster_){
  D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
  hr=device_->CreateRasterizerState(&raster,&terrain_bake_raster_);
 }
 if(SUCCEEDED(hr)&&!terrain_bake_draw_){
  D3D11_BUFFER_DESC description{};description.ByteWidth=32;description.Usage=D3D11_USAGE_DYNAMIC;
  description.BindFlags=D3D11_BIND_CONSTANT_BUFFER;description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  hr=device_->CreateBuffer(&description,nullptr,&terrain_bake_draw_);
 }
 if(SUCCEEDED(hr)&&!terrain_bake_values_){
  D3D11_BUFFER_DESC description{};description.ByteWidth=sizeof(TerrainBakeValues);description.Usage=D3D11_USAGE_DYNAMIC;
  description.BindFlags=D3D11_BIND_CONSTANT_BUFFER;description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  hr=device_->CreateBuffer(&description,nullptr,&terrain_bake_values_);
 }
 // Staging atlas: RGBA8 with mips, the bake's target; the block targets of both compressors.
 const unsigned side=terrain_bake_staging_side;
 D3D11_TEXTURE2D_DESC staging{};staging.Width=staging.Height=side;staging.MipLevels=terrain_bake_mips;staging.ArraySize=1;
 staging.Format=DXGI_FORMAT_R8G8B8A8_UNORM;staging.SampleDesc.Count=1;staging.Usage=D3D11_USAGE_DEFAULT;
 staging.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;staging.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
 if(SUCCEEDED(hr))hr=device_->CreateTexture2D(&staging,nullptr,&terrain_bake_texture_);
 D3D11_RENDER_TARGET_VIEW_DESC target{};target.Format=staging.Format;target.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
 if(SUCCEEDED(hr))hr=device_->CreateRenderTargetView(terrain_bake_texture_.Get(),&target,&terrain_bake_rtv_);
 if(SUCCEEDED(hr))hr=device_->CreateShaderResourceView(terrain_bake_texture_.Get(),nullptr,&terrain_bake_srv_);
 for(int format=0;format<2&&SUCCEEDED(hr);++format){
  D3D11_TEXTURE2D_DESC blocks=staging;blocks.Width=blocks.Height=side/4;blocks.Format=format?DXGI_FORMAT_R32G32B32A32_UINT:DXGI_FORMAT_R32G32_UINT;
  blocks.BindFlags=D3D11_BIND_UNORDERED_ACCESS;blocks.MiscFlags=0;
  auto& texture=format?terrain_bake_near_blocks_:terrain_bake_blocks_;auto* views=format?terrain_bake_near_block_uavs_:terrain_bake_block_uavs_;
  hr=device_->CreateTexture2D(&blocks,nullptr,&texture);
  for(unsigned mip=0;mip<terrain_bake_mips&&SUCCEEDED(hr);++mip){
   D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=blocks.Format;view.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;view.Texture2D.MipSlice=mip;
   hr=device_->CreateUnorderedAccessView(texture.Get(),&view,&views[mip]);
  }
 }
 if(FAILED(hr)){
  terrain_bake_vs_.Reset();terrain_bake_tess_vs_.Reset();terrain_bake_hs_.Reset();terrain_bake_ds_.Reset();terrain_bake_ps_.Reset();
  terrain_baked_vs_.Reset();terrain_baked_hs_.Reset();terrain_baked_ps_.Reset();terrain_baked_snow_ps_.Reset();terrain_bake_bc1_cs_.Reset();terrain_bake_bc7_cs_.Reset();
  terrain_bake_texture_.Reset();terrain_bake_rtv_.Reset();terrain_bake_srv_.Reset();terrain_bake_pipeline_failed_=true;
  bridge_append_probe_event("terrain_bake_omitted","terrain material cache shaders or staging atlas could not be created; terrain keeps the live material");
 }
 return hr;
}

// Everything per map and setting: atlases, tables, residency and the
// staging atlas. The shaders stay.
void Renderer::release_terrain_bake() noexcept{
 terrain_bake_queries_.clear();terrain_bake_measure_=terrain_bake_measured_=false;
 terrain_bake_near_.Reset();terrain_bake_near_srv_.Reset();terrain_bake_far_.Reset();terrain_bake_far_srv_.Reset();
 terrain_bake_pages_.Reset();terrain_bake_pages_srv_.Reset();terrain_bake_bake_pages_.Reset();terrain_bake_bake_pages_srv_.Reset();
 terrain_bake_sampler_.Reset();terrain_bake_state_.reset();terrain_bake_failed_=false;terrain_bake_drawn_=false;
 // The staging atlas and block targets too (ensure_terrain_bake_pipeline makes them again).
 terrain_bake_texture_.Reset();terrain_bake_rtv_.Reset();terrain_bake_srv_.Reset();terrain_bake_blocks_.Reset();terrain_bake_near_blocks_.Reset();
 for(auto& view:terrain_bake_block_uavs_)view.Reset();
 for(auto& view:terrain_bake_near_block_uavs_)view.Reset();
}

// Page-table slices: the active map once its worker-built cell tables
// arrive, then every neighbour whose cell tables its stream built. A changed
// set re-lays the table (pages of maps that left free their slots).
void Renderer::refresh_terrain_bake_slices() noexcept{
 auto& state=*terrain_bake_state_;
 if(!terrain_bake_cells_&&terrain_bake_cells_generation_==ambient_map_generation_&&terrain_bake_cells_future_.valid()&&
    terrain_bake_cells_future_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
  try{terrain_bake_cells_=terrain_bake_cells_future_.get();}catch(...){terrain_bake_cells_.reset();}
 }
 std::vector<TerrainBakeResidency::Slice> slices;
 try{
  const int cells_x=int((ambient_tiles_x_+terrain_bake_cell_tiles-1)/terrain_bake_cell_tiles);
  const int cells_y=int((ambient_tiles_y_+terrain_bake_cell_tiles-1)/terrain_bake_cell_tiles);
  if(terrain_bake_cells_&&terrain_bake_cells_generation_==ambient_map_generation_&&
     terrain_bake_cells_->cells_x==cells_x&&terrain_bake_cells_->cells_y==cells_y){
   TerrainBakeResidency::Slice active;active.id=0;active.name="active";active.cells_x=cells_x;active.cells_y=cells_y;active.cells=terrain_bake_cells_;
   slices.push_back(std::move(active));
   // Neighbours only with terrain_bake_neighbours: measured on the perf
   // frame (candidate-51) their cached plus routed passes cost 1.57 ms
   // against 1.09 live (distant ground is cheap live).
   if(configured_boolean(debug_overlay_.config(),"terrain_bake_neighbours",false))for(const auto& n:gc_neighbours_){
    if(!n.bake_cells||!n.contact_map||n.bake_cells->cells_x<=0)continue;
    TerrainBakeResidency::Slice slice;slice.id=n.contact_map;slice.name=n.name;slice.origin_x=n.origin[0];slice.origin_y=n.origin[1];
    slice.cells_x=n.bake_cells->cells_x;slice.cells_y=n.bake_cells->cells_y;slice.cells=n.bake_cells;
    slices.push_back(std::move(slice));
   }
  }
 }catch(const std::bad_alloc&){return;}
 bool same=slices.size()==state.slices.size();
 for(std::size_t k=0;same&&k<slices.size();++k)
  same=slices[k].id==state.slices[k].id&&slices[k].cells==state.slices[k].cells&&slices[k].origin_x==state.slices[k].origin_x&&slices[k].origin_y==state.slices[k].origin_y;
 if(same)return;
 std::size_t first=0;
 for(auto& slice:slices){slice.first_cell=first;first+=std::size_t(slice.cells_x)*slice.cells_y;}
 // Pages of maps no longer present give up their slots.
 for(auto it=state.pages.begin();it!=state.pages.end();){
  bool kept=false;for(const auto& slice:slices)kept|=slice.id==it->second.map;
  if(kept||it->second.baking){++it;continue;}
  (it->second.distant?state.far_slots:state.near_slots).release(it->second.node);it=state.pages.erase(it);
 }
 state.slices.swap(slices);state.table_cells=first;state.tables_resized=true;state.tables_dirty=true;state.wanted_valid=false;
}

// Page-table buffers sized to the slices (two float4 per cell).
HRESULT Renderer::ensure_terrain_bake_tables() noexcept{
 auto& state=*terrain_bake_state_;
 if(!state.tables_resized)return S_OK;
 HRESULT hr=S_OK;
 const UINT table_bytes=UINT(std::max<std::size_t>(state.table_cells,1)*2*16);
 for(int k=0;k<2&&SUCCEEDED(hr);++k){
  D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=table_bytes;buffer.Usage=D3D11_USAGE_DEFAULT;
  buffer.BindFlags=D3D11_BIND_SHADER_RESOURCE;buffer.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;buffer.StructureByteStride=16;
  auto& target=k?terrain_bake_bake_pages_:terrain_bake_pages_;auto& view=k?terrain_bake_bake_pages_srv_:terrain_bake_pages_srv_;
  target.Reset();view.Reset();
  hr=device_->CreateBuffer(&buffer,nullptr,&target);
  if(SUCCEEDED(hr))hr=device_->CreateShaderResourceView(target.Get(),nullptr,&view);
 }
 if(SUCCEEDED(hr)){state.tables_resized=false;state.tables_dirty=true;}
 return hr;
}

// The wanted pages of this camera: a quadtree over the map's 8 m cells from
// 64 m roots, keeping nodes inside the view frustum and within reach, each at
// its screen density (capped) times its stretch per axis, split while a side
// would exceed terrain_bake_page_texels; slots are the nearest powers of two.
void Renderer::update_terrain_bake_wanted() noexcept{
 auto& state=*terrain_bake_state_;
 const std::uint32_t verts_x=ambient_tiles_x_*2u+1u,verts_y=ambient_tiles_y_*2u+1u;
 if(!native_world_clip_valid_||ambient_height_.size()!=std::size_t(verts_x)*verts_y)return;
 if(state.wanted_valid&&state.wanted_clip==native_world_clip_)return;
 if(state.slices.empty())return;
 std::array<float,16> inverse{};
 if(!bake_invert(native_world_clip_,inverse))return;
 const auto unproject=[&](double x,double y,double z,std::array<double,3>& out){
  const auto h=bake_multiply(inverse,x,y,z,1);
  if(std::abs(h[3])<1e-12)return false;
  out={h[0]/h[3],h[1]/h[3],h[2]/h[3]};return true;
 };
 const auto vertex=[&](int x,int y){
  x=std::clamp(x,0,int(verts_x)-1);y=std::clamp(y,0,int(verts_y)-1);
  return ambient_height_[std::size_t(y)*verts_x+x];
 };
 const auto height=[&](double x,double y){
  x=std::clamp(x,0.0,double(verts_x-1));y=std::clamp(y,0.0,double(verts_y-1));
  const int ix=std::min(int(x),int(verts_x)-2),iy=std::min(int(y),int(verts_y)-2);
  const double fx=x-ix,fy=y-iy;
  return (vertex(ix,iy)*(1-fx)+vertex(ix+1,iy)*fx)*(1-fy)+(vertex(ix,iy+1)*(1-fx)+vertex(ix+1,iy+1)*fx)*fy;
 };
 if(state.slices.empty()||state.slices.front().id!=0)return; // the active map's cell tables first
 std::array<double,3> left{},right{};
 if(!unproject(-.5,0,.5,left)||!unproject(.5,0,.5,right))return;
 double axis[3]{right[0]-left[0],right[1]-left[1],right[2]-left[2]};
 const double axis_length=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
 if(axis_length<1e-12)return;
 const auto& clip=native_world_clip_;
 const double pixels_at_unit_depth=std::abs(clip[0]*axis[0]+clip[1]*axis[1]+clip[2]*axis[2])/axis_length*target_width()*.5;
 const auto view_depth=[&](double x,double y,double z){return clip[12]*x+clip[13]*y+clip[14]*z+clip[15];};
 const std::array<double,3> eye{inverse[2]/inverse[14],inverse[6]/inverse[14],inverse[10]/inverse[14]};
 std::array<std::array<double,4>,5> planes{};
 for(unsigned k=0;k<4;++k){
  planes[0][k]=clip[12+k]+clip[k];planes[1][k]=clip[12+k]-clip[k];
  planes[2][k]=clip[12+k]+clip[4+k];planes[3][k]=clip[12+k]-clip[4+k];planes[4][k]=clip[8+k];
 }
 const double cap=terrain_bake_density_,reach=terrain_bake_reach_,max_stretch=terrain_bake_stretch_;
 const float cell=terrain_bake_cell_metres;
 std::vector<TerrainBakeResidency::Wanted> wanted;
 const double active_w=ambient_tiles_x_*2.0,active_h=ambient_tiles_y_*2.0;
 // One slice at a time: its cells, origin and whether its heights can be
 // sampled exactly (the active map) or only bounded by its cells (neighbours).
 const TerrainBakeResidency::Slice* current=nullptr;
 const auto visit=[&](auto&& self,int cx,int cy,int side)->void{
  const auto& tables=*current->cells;const int cells_x=current->cells_x,cells_y=current->cells_y;
  if(cx>=cells_x||cy>=cells_y)return;
  const bool active=current->id==0;
  const double x0=current->origin_x+cx*cell,y0=current->origin_y+cy*cell,metres=side*cell;
  // Neighbour ground inside the active map is never drawn.
  if(!active&&x0>=0&&y0>=0&&x0+metres<=active_w&&y0+metres<=active_h)return;
  float low=1e30f,high=-1e30f;double rise_x=0,rise_y=0;
  for(int y=cy;y<std::min(cells_y,cy+side);++y)for(int x=cx;x<std::min(cells_x,cx+side);++x){
   const std::size_t i=std::size_t(y)*cells_x+x;
   low=std::min(low,tables.low[i]);high=std::max(high,tables.high[i]);rise_x=std::max(rise_x,double(tables.rise_x[i]));rise_y=std::max(rise_y,double(tables.rise_y[i]));
  }
  bool whole=true;
  for(const auto& plane:planes){
   unsigned inside=0;
   for(unsigned k=0;k<8;++k){
    const double x=(k&1)?x0+metres:x0,y=(k&2)?y0+metres:y0,z=(k&4)?high+1.0:low-1.0;
    inside+=plane[0]*x+plane[1]*y+plane[2]*z+plane[3]>=0;
   }
   if(!inside)return;
   whole=whole&&inside==8;
  }
  double depth=1e30,distance=1e30;
  if(active)for(int k=0;k<=25;++k){
   const double x=k<25?x0+metres*(k%5)/4.0:std::clamp(eye[0],x0,x0+metres),y=k<25?y0+metres*(k/5)/4.0:std::clamp(eye[1],y0,y0+metres);
   const double z=height(x,y);
   depth=std::min(depth,view_depth(x,y,z));
   distance=std::min(distance,std::sqrt((x-eye[0])*(x-eye[0])+(y-eye[1])*(y-eye[1])+(z-eye[2])*(z-eye[2])));
  }else{
   // Neighbours: the node's box (its cells' height range).
   for(unsigned k=0;k<8;++k)depth=std::min(depth,view_depth((k&1)?x0+metres:x0,(k&2)?y0+metres:y0,(k&4)?high:low));
   const double px=std::clamp(eye[0],x0,x0+metres),py=std::clamp(eye[1],y0,y0+metres),pz=std::clamp(eye[2],double(low),double(high));
   distance=std::sqrt((px-eye[0])*(px-eye[0])+(py-eye[1])*(py-eye[1])+(pz-eye[2])*(pz-eye[2]));
  }
  if(distance>reach)return;
  const double density=std::clamp(depth>.5?pixels_at_unit_depth/depth:cap,1.0,cap);
  const double sx=std::min(max_stretch,double(terrain_bake_stretch_level(float(std::sqrt(1+rise_x*rise_x))))),sy=std::min(max_stretch,double(terrain_bake_stretch_level(float(std::sqrt(1+rise_y*rise_y)))));
  const double dx=std::min(density*sx,cap),dy=std::min(density*sy,cap);
  if(side>1&&std::max(metres*dx,metres*dy)>terrain_bake_page_texels){
   const int half=side/2;
   self(self,cx,cy,half);self(self,cx+half,cy,half);self(self,cx,cy+half,half);self(self,cx+half,cy+half,half);
   return;
  }
  TerrainBakeResidency::Wanted page{};page.map=current->id;page.cell_x=cx;page.cell_y=cy;page.side=side;page.distance=float(distance);page.whole=whole;
  page.distant=terrain_bake_bc1_>0&&distance>terrain_bake_bc1_;
  page.gutter=std::clamp(unsigned(std::floor(terrain_bake_margin*std::min(dx,dy))),1u,terrain_bake_max_gutter);
  // Slot sides: the nearest power of two to the wanted size, never past the cap.
  const auto slot=[&](double wanted_density){
   unsigned s=nearest_power_of_two(metres*wanted_density+2*page.gutter,terrain_bake_min_slot,terrain_bake_max_slot);
   // Up to 1.5x the cap: halving at 1.05x dropped every page wanting the cap
   // (an 8 m page at 96 t/m needs 784 texels) to 512 texels, 62 t/m, and
   // close ground looked mipped.
   while(s>terrain_bake_min_slot&&(s-2*page.gutter)/metres>cap*1.5)s/=2;
   return s;
  };
  page.slot_w=slot(dx);page.slot_h=slot(dy);
  page.density_x=float((page.slot_w-2*page.gutter)/metres);page.density_y=float((page.slot_h-2*page.gutter)/metres);
  wanted.push_back(page);
 };
 const int top=int(terrain_bake_top_cells);
 for(const auto& slice:state.slices){
  current=&slice;
  for(int cy=0;cy<slice.cells_y;cy+=top)for(int cx=0;cx<slice.cells_x;cx+=top)visit(visit,cx,cy,top);
 }
 // The active map first, then neighbours; nearest first within each.
 std::sort(wanted.begin(),wanted.end(),[](const auto& l,const auto& r){return (l.map!=0)!=(r.map!=0)?l.map==0:l.distance<r.distance;});
 try{state.wanted_density.assign(state.table_cells,0.f);}catch(const std::bad_alloc&){return;}
 for(const auto& page:wanted){
  const auto* slice=state.slice(page.map);
  for(int y=page.cell_y;y<std::min(slice->cells_y,page.cell_y+page.side);++y)for(int x=page.cell_x;x<std::min(slice->cells_x,page.cell_x+page.side);++x)
   state.wanted_density[slice->first_cell+std::size_t(y)*slice->cells_x+x]=std::min(page.density_x,page.density_y);
 }
 state.wanted.swap(wanted);state.wanted_clip=native_world_clip_;state.wanted_valid=true;state.wanted_frame=native_frame_index_;
 state.pixels_at_unit_depth=pixels_at_unit_depth;state.eye=eye;state.tables_dirty=true;
}

// Near and distant atlases sized from the first wanted demand (with headroom),
// within the budget; tables sized to the map.
HRESULT Renderer::create_terrain_bake_atlases(double near_bytes_wanted,double far_bytes_wanted) noexcept{
 auto& state=*terrain_bake_state_;
 const double near_bpt=terrain_bake_near_bc7_?1.:4.,budget=double(terrain_bake_budget_)*1048576.0*3/4; // without the mips' third
 double near_bytes=near_bytes_wanted*1.25,far_bytes=far_bytes_wanted*1.25;
 if(near_bytes+far_bytes>budget){const double scale=budget/(near_bytes+far_bytes);near_bytes*=scale;far_bytes*=scale;}
 const unsigned width=terrain_bake_atlas_width,row=terrain_bake_max_slot;
 const auto rows=[&](double bytes,double bpt){return std::clamp(unsigned(std::ceil(bytes/bpt/width/row))*row,row,terrain_bake_atlas_max_height);};
 state.near_width=state.far_width=width;
 state.near_height=rows(near_bytes,near_bpt);
 state.far_height=terrain_bake_bc1_>0?rows(far_bytes,.5):0;
 HRESULT hr=S_OK;
 const auto atlas=[&](unsigned height,DXGI_FORMAT format,ComPtr<ID3D11Texture2D>& texture,ComPtr<ID3D11ShaderResourceView>& view){
  D3D11_TEXTURE2D_DESC description{};description.Width=width;description.Height=height;description.MipLevels=terrain_bake_mips;description.ArraySize=1;
  description.Format=format;description.SampleDesc.Count=1;description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  if(SUCCEEDED(hr))hr=device_->CreateTexture2D(&description,nullptr,&texture);
  if(SUCCEEDED(hr))hr=device_->CreateShaderResourceView(texture.Get(),nullptr,&view);
 };
 atlas(state.near_height,terrain_bake_near_bc7_?DXGI_FORMAT_BC7_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM,terrain_bake_near_,terrain_bake_near_srv_);
 if(state.far_height)atlas(state.far_height,DXGI_FORMAT_BC1_UNORM,terrain_bake_far_,terrain_bake_far_srv_);
 if(SUCCEEDED(hr)){
  D3D11_SAMPLER_DESC sampler{};
  const UINT anisotropy=1u<<std::clamp(configured_integer(debug_overlay_.config(),"anisotropy",3),0,4);
  sampler.Filter=anisotropy>1?D3D11_FILTER_ANISOTROPIC:D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampler.MaxAnisotropy=anisotropy;
  sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=float(terrain_bake_mips-1);
  hr=device_->CreateSamplerState(&sampler,&terrain_bake_sampler_);
 }
 if(FAILED(hr))return hr;
 state.near_slots.reset(width,state.near_height);state.far_slots.reset(width,state.far_height);
 auto& values=terrain_bake_values_cpu_;values=TerrainBakeValues{};
 values.atlas[0]=values.atlas[1]=1.f/terrain_bake_staging_side;
 values.sample[0]=1.f/width;values.sample[1]=1.f/state.near_height;values.sample[2]=1.f/width;values.sample[3]=state.far_height?1.f/state.far_height:0.f;
 state.atlases=true;state.started_frame=native_frame_index_;
 const double near_mb=double(width)*state.near_height*near_bpt*4/3/1048576.0,far_mb=double(width)*state.far_height*.5*4/3/1048576.0;
 // Demand of the same wanted pages for other formats and BC1 distances.
 const auto demand=[&](double near_texel_bytes,int bc1){
  double bytes=0;for(const auto& page:state.wanted)bytes+=double(page.slot_w)*page.slot_h*((bc1>0&&page.distance>bc1)?.5:near_texel_bytes);
  return bytes*4/3/1048576.0;
 };
 char table[300];
 std::snprintf(table,sizeof(table),"; demand with BC1 beyond 15/20/30/60 m: BC7 near %.0f/%.0f/%.0f/%.0f MB, RGBA8 near %.0f/%.0f/%.0f/%.0f MB; all RGBA8 %.0f MB",
  demand(1,15),demand(1,20),demand(1,30),demand(1,60),demand(4,15),demand(4,20),demand(4,30),demand(4,60),demand(4,0));
 // Per map: wanted pages and their demand at the current formats.
 std::string maps;
 for(const auto& slice:state.slices){
  unsigned count=0;double bytes=0;
  for(const auto& page:state.wanted)if(page.map==slice.id){++count;bytes+=double(page.slot_w)*page.slot_h*(page.distant?.5:near_bpt);}
  char line[160];std::snprintf(line,sizeof(line),"%s%s %u pages %.1f MB",maps.empty()?"; maps: ":", ",slice.id?slice.name.c_str():"active",count,bytes*4/3/1048576.0);
  try{maps+=line;}catch(...){}
 }
 char detail[1400];
 std::snprintf(detail,sizeof(detail),"%u wanted pages (density cap %d, %.0f px/m at depth 1): demand %.0f MB near (%s) + %.0f MB far (BC1 beyond %d m) with mips; atlases near %ux%u %.0f MB + far %ux%u %.0f MB (budget %d); staging %u^2 RGBA8 %.0f MB%s%s",
  unsigned(state.wanted.size()),terrain_bake_density_,state.pixels_at_unit_depth,near_bytes_wanted*4/3/1048576.0,terrain_bake_near_bc7_?"BC7":"RGBA8",far_bytes_wanted*4/3/1048576.0,terrain_bake_bc1_,
  width,state.near_height,near_mb,width,state.far_height,far_mb,terrain_bake_budget_,terrain_bake_staging_side,double(terrain_bake_staging_side)*terrain_bake_staging_side*4*4/3/1048576.0,table,maps.c_str());
 bridge_append_probe_event("terrain_bake_atlases",detail);
 return S_OK;
}

// This frame's bake: wanted pages without a usable resident page, nearest
// first, then usable pages baked while partly outside the view that now lie
// wholly inside it under a newer view (their unbaked ground may now be on
// screen); within the adaptive texel budget and the staging atlas. Slots
// come from the atlases, evicting least recently wanted pages when full.
void Renderer::select_terrain_bake_pages() noexcept{
 auto& state=*terrain_bake_state_;
 const std::uint64_t frame=native_frame_index_;
 std::vector<const TerrainBakeResidency::Wanted*> pending,refresh;
 for(const auto& want:state.wanted){
  auto found=state.pages.find(page_key(want.map,want.cell_x,want.cell_y,want.side));
  if(found!=state.pages.end()){
   auto& page=found->second;page.last_wanted=frame;
   // Usable: baked, at least 0.7x the wanted density on both axes (no blur
   // while catching up) and at most 1.5x (memory). Either atlas class serves.
   if(page.baked&&page.density_x>=.7f*want.density_x&&page.density_y>=.7f*want.density_y&&
      page.density_x<=1.5f*want.density_x&&page.density_y<=1.5f*want.density_y){
    if(page.partial&&want.whole&&page.view!=state.wanted_frame)refresh.push_back(&want);
    continue;
   }
   if(page.baking)continue;
  }
  pending.push_back(&want);
 }
 if(pending.empty()&&refresh.empty())return;
 pending.insert(pending.end(),refresh.begin(),refresh.end());
 const double budget=std::min(state.budget_texels,double(terrain_bake_frame_texels_)*1024.0);
 double spent=0;unsigned x=0,y=0,shelf=0;const unsigned side=terrain_bake_staging_side;
 const auto evict_one=[&](bool distant)->bool{
  std::uint64_t victim=0;const TerrainBakeResidency::Page* worst=nullptr;
  for(const auto& [key,page]:state.pages){
   if(page.distant!=distant||page.baking||page.last_wanted==frame||page.node<0)continue;
   if(!worst||page.last_wanted<worst->last_wanted){worst=&page;victim=key;}
  }
  if(!worst)return false;
  (distant?state.far_slots:state.near_slots).release(worst->node);state.pages.erase(victim);state.tables_dirty=true;
  return true;
 };
 for(const auto* want:pending){
  if(spent>0&&spent+double(want->slot_w)*want->slot_h>budget)break;
  // Staging shelf (power-of-two sides keep every page 32-texel aligned).
  unsigned sx=x,sy=y,next_shelf=shelf;
  if(sx+want->slot_w>side){sy+=shelf;sx=0;next_shelf=0;}
  if(sy+want->slot_h>side)break;
  auto& slots=want->distant?state.far_slots:state.near_slots;
  if(want->distant&&!state.far_height)continue;
  const std::uint64_t key=page_key(want->map,want->cell_x,want->cell_y,want->side);
  // The node's previous page gives up its slot first.
  if(auto old=state.pages.find(key);old!=state.pages.end()){(old->second.distant?state.far_slots:state.near_slots).release(old->second.node);state.pages.erase(old);state.tables_dirty=true;}
  int node=slots.allocate(want->slot_w,want->slot_h);
  while(node<0&&evict_one(want->distant))node=slots.allocate(want->slot_w,want->slot_h);
  if(node<0){++state.allocation_failures;continue;}
  TerrainBakeResidency::Page page;page.map=want->map;page.cell_x=want->cell_x;page.cell_y=want->cell_y;page.side=want->side;page.distant=want->distant;
  page.slot_w=want->slot_w;page.slot_h=want->slot_h;page.gutter=want->gutter;page.density_x=want->density_x;page.density_y=want->density_y;
  page.node=node;page.last_wanted=frame;page.baking=true;page.stage_x=sx;page.stage_y=sy;page.partial=!want->whole;page.view=state.wanted_frame;
  try{state.pages[key]=page;state.baking.push_back(key);}catch(const std::bad_alloc&){slots.release(node);break;}
  x=sx+want->slot_w;y=sy;shelf=std::max(next_shelf,want->slot_h);spent+=double(want->slot_w)*want->slot_h;
 }
 state.timing[state.timing_index].texels+=spent;
}

// GPU time of the cache's work: one timestamp pair per bake draw or
// finalize, summed per frame under one disjoint query.
void Renderer::begin_terrain_bake_span(std::size_t& span) noexcept{
 auto& state=*terrain_bake_state_;auto& timing=state.timing[state.timing_index];
 span=SIZE_MAX;
 if(!timing.disjoint){const D3D11_QUERY_DESC query{D3D11_QUERY_TIMESTAMP_DISJOINT,0};if(FAILED(device_->CreateQuery(&query,&timing.disjoint)))return;}
 if(!timing.active){context_->Begin(timing.disjoint.Get());timing.active=true;state.timing_open=true;}
 if(timing.used==timing.spans.size()){
  std::array<ComPtr<ID3D11Query>,2> pair;const D3D11_QUERY_DESC query{D3D11_QUERY_TIMESTAMP,0};
  if(FAILED(device_->CreateQuery(&query,&pair[0]))||FAILED(device_->CreateQuery(&query,&pair[1])))return;
  try{timing.spans.push_back(pair);}catch(const std::bad_alloc&){return;}
 }
 span=timing.used++;
 context_->End(timing.spans[span][0].Get());
}
void Renderer::end_terrain_bake_span(std::size_t span) noexcept{
 if(span==SIZE_MAX)return;
 auto& timing=terrain_bake_state_->timing[terrain_bake_state_->timing_index];
 context_->End(timing.spans[span][1].Get());
}
// Start of a frame: close the previous frame's timing, read back finished
// ones and move the texel budget toward terrain_bake_frame_ms.
void Renderer::update_terrain_bake_budget() noexcept{
 auto& state=*terrain_bake_state_;
 if(state.timing_open){
  auto& open=state.timing[state.timing_index];
  context_->End(open.disjoint.Get());state.timing_open=false;
  state.timing_index=(state.timing_index+1)%unsigned(state.timing.size());
  // An entry still unread four frames on is dropped (its queries are reissued).
  auto& next=state.timing[state.timing_index];next.active=false;next.used=0;next.texels=0;
 }
 for(auto& timing:state.timing){
  if(!timing.active||&timing==&state.timing[state.timing_index])continue;
  D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
  if(context_->GetData(timing.disjoint.Get(),&disjoint,sizeof(disjoint),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK)continue;
  double ticks=0;bool ready=true;
  for(std::size_t k=0;k<timing.used&&ready;++k){
   UINT64 begin=0,end=0;
   ready=context_->GetData(timing.spans[k][0].Get(),&begin,sizeof(begin),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK&&
         context_->GetData(timing.spans[k][1].Get(),&end,sizeof(end),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK;
   if(ready&&end>begin)ticks+=double(end-begin);
  }
  if(!ready)continue;
  if(!disjoint.Disjoint&&disjoint.Frequency&&timing.texels>0){
   const double ms=ticks*1000.0/double(disjoint.Frequency);
   state.measured_ms=ms;state.measured_texels=timing.texels;
   const double target=std::max(.05,configured_integer(debug_overlay_.config(),"terrain_bake_frame_ms",3)/10.0);
   const double wanted=state.budget_texels*target/std::max(ms,1e-3);
   state.budget_texels=std::clamp(.7*state.budget_texels+.3*wanted,262144.0,double(terrain_bake_staging_side)*terrain_bake_staging_side);
  }
  timing.active=false;timing.used=0;timing.texels=0;
 }
}

// Page tables: drawn pages (baked and usable for their cells' wanted
// density; coarse first so finer pages win) and this frame's baking pages
// at their staging origins.
void Renderer::upload_terrain_bake_tables() noexcept{
 auto& state=*terrain_bake_state_;
 if(FAILED(ensure_terrain_bake_tables()))return;
 std::vector<std::array<float,4>> table;
 try{table.assign(std::max<std::size_t>(state.table_cells,1)*2,std::array<float,4>{0,0,0,0});}catch(const std::bad_alloc&){return;}
 const float cell=terrain_bake_cell_metres;
 // Entries hold the page's world origin (its map's origin plus its cells).
 const auto place=[&](const TerrainBakeResidency::Page& page,float x,float y,bool check){
  const auto* slice=state.slice(page.map);if(!slice)return;
  for(int cy=page.cell_y;cy<std::min(slice->cells_y,page.cell_y+page.side);++cy)for(int cx=page.cell_x;cx<std::min(slice->cells_x,page.cell_x+page.side);++cx){
   const std::size_t i=slice->first_cell+std::size_t(cy)*slice->cells_x+cx;
   if(check&&i<state.wanted_density.size()&&state.wanted_density[i]>0&&std::min(page.density_x,page.density_y)<.7f*state.wanted_density[i])continue;
   table[2*i]={x+float(page.gutter),y+float(page.gutter),page.density_x,page.density_y};
   table[2*i+1]={float(slice->origin_x+page.cell_x*cell),float(slice->origin_y+page.cell_y*cell),float(page.side)*cell,float(page.gutter)};
  }
 };
 if(state.tables_dirty){
  std::vector<const TerrainBakeResidency::Page*> drawn;
  for(const auto& [key,page]:state.pages)if(page.baked)drawn.push_back(&page);
  std::sort(drawn.begin(),drawn.end(),[](const auto* l,const auto* r){return l->side>r->side;});
  for(const auto* page:drawn){
   const auto& node=(page->distant?state.far_slots:state.near_slots).nodes[page->node];
   place(*page,float(node.x)+(page->distant?terrain_bake_far_flag:0.f),float(node.y),true);
  }
  context_->UpdateSubresource(terrain_bake_pages_.Get(),0,nullptr,table.data(),0,0);
  state.tables_dirty=false;
  std::fill(table.begin(),table.end(),std::array<float,4>{0,0,0,0});
 }
 if(!state.baking.empty()){
  for(const auto key:state.baking){const auto& page=state.pages[key];place(page,float(page.stage_x),float(page.stage_y),false);}
  context_->UpdateSubresource(terrain_bake_bake_pages_.Get(),0,nullptr,table.data(),0,0);
 }
}

// The frame after a bake (all its batches drawn): the staging atlas's mips,
// compression of its pages (BC7 near, BC1 distant; RGBA8 near pages copy) and
// the copies into their slots. The pages then draw from the cache.
void Renderer::finalize_terrain_bake() noexcept{
 auto& state=*terrain_bake_state_;
 if(state.baking.empty())return;
 transition_profile_pass("terrain_bake");
 std::size_t span;begin_terrain_bake_span(span);
 context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());
 context_->GenerateMips(terrain_bake_srv_.Get());
 bool near_pages=false,far_pages=false;
 for(const auto key:state.baking){const auto& page=state.pages[key];(page.distant?far_pages:near_pages)=true;}
 ID3D11ShaderResourceView* source=terrain_bake_srv_.Get();ID3D11Buffer* constants=terrain_bake_bc1_constants_.Get();
 const unsigned side=terrain_bake_staging_side;
 const auto compress=[&](ID3D11ComputeShader* shader,ComPtr<ID3D11UnorderedAccessView>* targets){
  context_->CSSetShader(shader,nullptr,0);context_->CSSetShaderResources(0,1,&source);context_->CSSetConstantBuffers(0,1,&constants);
  for(unsigned mip=0;mip<terrain_bake_mips;++mip){
   const UINT values[8]{0,0,(side>>mip)/4,(side>>mip)/4,mip,0,0,0};
   D3D11_MAPPED_SUBRESOURCE mapped{};
   if(FAILED(context_->Map(constants,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))break;
   std::memcpy(mapped.pData,values,sizeof(values));context_->Unmap(constants,0);
   ID3D11UnorderedAccessView* target=targets[mip].Get();context_->CSSetUnorderedAccessViews(0,1,&target,nullptr);
   context_->Dispatch((values[2]+7)/8,(values[3]+7)/8,1);
  }
  ID3D11ShaderResourceView* no_view=nullptr;ID3D11UnorderedAccessView* no_target=nullptr;ID3D11Buffer* no_buffer=nullptr;
  context_->CSSetShaderResources(0,1,&no_view);context_->CSSetUnorderedAccessViews(0,1,&no_target,nullptr);context_->CSSetConstantBuffers(0,1,&no_buffer);
  context_->CSSetShader(nullptr,nullptr,0);
 };
 if(far_pages)compress(terrain_bake_bc1_cs_.Get(),terrain_bake_block_uavs_);
 if(near_pages&&terrain_bake_near_bc7_)compress(terrain_bake_bc7_cs_.Get(),terrain_bake_near_block_uavs_);
 for(const auto key:state.baking){
  auto& page=state.pages[key];
  const auto& node=(page.distant?state.far_slots:state.near_slots).nodes[page.node];
  for(unsigned mip=0;mip<terrain_bake_mips;++mip){
   if(page.distant||terrain_bake_near_bc7_){
    // Block targets hold one texel per 4x4 block.
    const D3D11_BOX blocks{(page.stage_x>>mip)/4,(page.stage_y>>mip)/4,0,((page.stage_x+page.slot_w)>>mip)/4,((page.stage_y+page.slot_h)>>mip)/4,1};
    context_->CopySubresourceRegion(page.distant?terrain_bake_far_.Get():terrain_bake_near_.Get(),mip,node.x>>mip,node.y>>mip,0,
     page.distant?terrain_bake_blocks_.Get():terrain_bake_near_blocks_.Get(),mip,&blocks);
   }else{
    const D3D11_BOX texels{page.stage_x>>mip,page.stage_y>>mip,0,(page.stage_x+page.slot_w)>>mip,(page.stage_y+page.slot_h)>>mip,1};
    context_->CopySubresourceRegion(terrain_bake_near_.Get(),mip,node.x>>mip,node.y>>mip,0,terrain_bake_texture_.Get(),mip,&texels);
   }
  }
  page.baking=false;
  // terrain_bake 2 measures the bake: its pages bake again next frame. A
  // page whose map's blend rows changed while it baked bakes again too.
  page.baked=terrain_bake_mode_!=2&&!page.stale;page.stale=false;
 }
 state.baking.clear();state.tables_dirty=true;
 end_terrain_bake_span(span);
}

void Renderer::report_terrain_bake_work() noexcept{
 if(terrain_bake_queries_.empty()||!terrain_bake_state_||terrain_bake_state_->bake_frame==native_frame_index_)return;
 std::uint64_t pixels=0,domains=0,vertices=0;
 for(const auto& query:terrain_bake_queries_){
  D3D11_QUERY_DATA_PIPELINE_STATISTICS statistics{};
  if(context_->GetData(query.Get(),&statistics,sizeof(statistics),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK)return;
  pixels+=statistics.PSInvocations;domains+=statistics.DSInvocations;vertices+=statistics.VSInvocations;
 }
 char detail[200];
 std::snprintf(detail,sizeof(detail),"%u bake draws of the first bake frame: %.2f M pixel shader invocations, %.2f M domain, %.2f M vertex",
  unsigned(terrain_bake_queries_.size()),pixels/1e6,domains/1e6,vertices/1e6);
 bridge_append_probe_event("terrain_bake_work",detail);
 terrain_bake_queries_.clear();terrain_bake_measured_=true;
}

// This flush's part in the cache. The first world flush of a frame also
// finalizes the previous frame's bake, updates the wanted pages, picks this
// frame's bake and uploads the page tables; every flush binds the cache.
Renderer::TerrainBakeFrame Renderer::update_terrain_bake() noexcept{
 TerrainBakeFrame frame;
 const auto& config=debug_overlay_.config();
 const int mode=scene_composited_?0:std::clamp(configured_integer(config,"terrain_bake",0),0,3);
 if(!scene_composited_){
  const int density=std::clamp(configured_integer(config,"terrain_bake_density",96),16,128);
  const int reach=std::clamp(configured_integer(config,"terrain_bake_reach",1000),50,1000);
  const int budget=std::clamp(configured_integer(config,"terrain_bake_budget",128),32,1024);
  const int stretch=std::clamp(configured_integer(config,"terrain_bake_stretch",16),1,16);
  const int bc1=std::clamp(configured_integer(config,"terrain_bake_bc1",60),0,1000);
  const bool near_bc7=configured_integer(config,"terrain_bake_near_format",1)!=0;
  const int frame_texels=std::clamp(configured_integer(config,"terrain_bake_frame_texels",4096),256,4096);
  if(mode==0){if(terrain_bake_state_||terrain_bake_failed_)release_terrain_bake();}
  else{
   // Switching between modes 1-3 keeps the resident pages.
   if(density!=terrain_bake_density_||reach!=terrain_bake_reach_||budget!=terrain_bake_budget_||stretch!=terrain_bake_stretch_||
      bc1!=terrain_bake_bc1_||near_bc7!=terrain_bake_near_bc7_||ambient_map_generation_!=terrain_bake_map_)release_terrain_bake();
   terrain_bake_mode_=mode;terrain_bake_density_=density;terrain_bake_reach_=reach;terrain_bake_budget_=budget;terrain_bake_stretch_=stretch;
   terrain_bake_bc1_=bc1;terrain_bake_near_bc7_=near_bc7;terrain_bake_frame_texels_=frame_texels;terrain_bake_map_=ambient_map_generation_;
   if(!terrain_bake_failed_&&SUCCEEDED(ensure_terrain_bake_pipeline())){
    if(!terrain_bake_state_){
     try{terrain_bake_state_=std::make_shared<TerrainBakeResidency>();}catch(const std::bad_alloc&){terrain_bake_failed_=true;}
    }
    auto* state=terrain_bake_state_.get();
    if(state&&state->update_frame!=native_frame_index_){
     state->update_frame=native_frame_index_;
     report_terrain_bake_work();
     update_terrain_bake_budget();
     finalize_terrain_bake();
     refresh_terrain_bake_slices();
     update_terrain_bake_wanted();
     // Atlases outgrown (neighbours arriving after the first view): made
     // again from the new demand, at most every 600 frames.
     if(state->atlases&&state->wanted_valid&&native_frame_index_-state->regrow_frame>=600){
      double near_bytes=0,far_bytes=0;
      for(const auto& page:state->wanted)(page.distant?far_bytes:near_bytes)+=double(page.slot_w)*page.slot_h*(page.distant?.5:(near_bc7?1.:4.));
      const double near_capacity=double(state->near_width)*state->near_height*(near_bc7?1.:4.),far_capacity=double(state->far_width)*state->far_height*.5;
      const double budget_bytes=double(budget)*1048576.0*3/4;
      if((near_bytes>near_capacity*1.1||far_bytes>far_capacity*1.1)&&near_capacity+far_capacity<budget_bytes*.95&&state->baking.empty()){
       state->regrow_frame=native_frame_index_;
       state->pages.clear();state->atlases=false;state->covered=false;
       terrain_bake_near_.Reset();terrain_bake_near_srv_.Reset();terrain_bake_far_.Reset();terrain_bake_far_srv_.Reset();
      }
     }
     if(state->wanted_valid&&!state->atlases&&SUCCEEDED(ensure_terrain_bake_tables())){
      double near_bytes=0,far_bytes=0;
      for(const auto& page:state->wanted)(page.distant?far_bytes:near_bytes)+=double(page.slot_w)*page.slot_h*(page.distant?.5:(near_bc7?1.:4.));
      if(FAILED(create_terrain_bake_atlases(near_bytes,far_bytes))){
       terrain_bake_failed_=true;
       bridge_append_probe_event("terrain_bake_omitted","terrain material cache atlases could not be created; terrain keeps the live material");
      }
     }
     if(state->atlases&&!terrain_bake_failed_){
      select_terrain_bake_pages();
      upload_terrain_bake_tables();
      if(!state->baking.empty()){
       const float clear[4]{0,0,0,0};context_->ClearRenderTargetView(terrain_bake_rtv_.Get(),clear);
       state->bake_frame=native_frame_index_;
       terrain_bake_measure_=!terrain_bake_measured_&&terrain_bake_queries_.empty();
      }
      // One line when every wanted page first draws from the cache.
      bool covered=true;
      for(const auto& want:state->wanted){
       const auto found=state->pages.find(page_key(want.map,want.cell_x,want.cell_y,want.side));
       if(found==state->pages.end()||!found->second.baked){covered=false;break;}
      }
      if(covered&&!state->covered&&mode!=2){
       double near_used=0,far_used=0;
       for(const auto& [key,page]:state->pages)(page.distant?far_used:near_used)+=double(page.slot_w)*page.slot_h;
       char detail[240];
       unsigned neighbour_pages=0;for(const auto& want:state->wanted)neighbour_pages+=want.map!=0;
       std::snprintf(detail,sizeof(detail),"all %u wanted pages (%u on %zu neighbour maps) drawn from the cache after %llu frames; %zu resident, slots %.1f M near + %.1f M far texels",
        unsigned(state->wanted.size()),neighbour_pages,state->slices.empty()?0:state->slices.size()-1,static_cast<unsigned long long>(native_frame_index_-state->started_frame),state->pages.size(),near_used/1e6,far_used/1e6);
       bridge_append_probe_event("terrain_bake_covered",detail);
      }
      // While not covered: one line every 600 frames on what is missing.
      if(!covered&&mode!=2&&native_frame_index_-state->report_frame>=600){
       state->report_frame=native_frame_index_;
       unsigned missing=0,partial=0,neighbour_missing=0;
       for(const auto& want:state->wanted){
        const auto found=state->pages.find(page_key(want.map,want.cell_x,want.cell_y,want.side));
        if(found==state->pages.end()||!found->second.baked){++missing;neighbour_missing+=want.map!=0;}else if(found->second.partial)++partial;
       }
       char detail[260];
       std::snprintf(detail,sizeof(detail),"%u of %u wanted pages not drawn from the cache (%u of them neighbour pages; %u resident baked while partly off-view); %u slot allocation failures so far; bake budget %.2f M texels/frame, last %.2f ms for %.2f M; %zu maps",
        missing,unsigned(state->wanted.size()),neighbour_missing,partial,state->allocation_failures,state->budget_texels/1e6,state->measured_ms,state->measured_texels/1e6,state->slices.size());
       bridge_append_probe_event("terrain_bake_incomplete",detail);
      }
      state->covered=covered;
     }
    }
    if(state&&state->atlases&&!terrain_bake_failed_){
     frame.bake=state->bake_frame==native_frame_index_&&!state->baking.empty();
     frame.sample=mode!=2;
    }
   }
  }
 }
 if(!frame.bake&&!frame.sample){
  if(terrain_bake_bound_){
   ID3D11Buffer* none=nullptr;ID3D11ShaderResourceView* view=nullptr;ID3D11SamplerState* sampler=nullptr;
   context_->VSSetConstantBuffers(9,1,&none);context_->HSSetConstantBuffers(9,1,&none);context_->DSSetConstantBuffers(9,1,&none);context_->PSSetConstantBuffers(9,1,&none);
   context_->PSSetShaderResources(PointShadowTextureLayout::terrain_bake_srv,1,&view);context_->PSSetSamplers(11,1,&sampler);
   context_->PSSetShaderResources(PointShadowTextureLayout::terrain_bake_far_srv,1,&view);
   for(const UINT slot:{PointShadowTextureLayout::terrain_bake_pages_srv,PointShadowTextureLayout::terrain_bake_bake_pages_srv}){
    context_->VSSetShaderResources(slot,1,&view);context_->HSSetShaderResources(slot,1,&view);context_->DSSetShaderResources(slot,1,&view);context_->PSSetShaderResources(slot,1,&view);
   }
   terrain_bake_bound_=false;
  }
  return frame;
 }
 // 1 draws from the cache, 2 also tints every cached pixel (terrain_bake 3).
 terrain_bake_values_cpu_.atlas[2]=!frame.sample?0.f:terrain_bake_mode_==3?2.f:1.f;
 D3D11_MAPPED_SUBRESOURCE mapped{};
 if(FAILED(context_->Map(terrain_bake_values_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return TerrainBakeFrame{};
 std::memcpy(mapped.pData,&terrain_bake_values_cpu_,sizeof(terrain_bake_values_cpu_));context_->Unmap(terrain_bake_values_.Get(),0);
 ID3D11Buffer* values=terrain_bake_values_.Get();
 context_->VSSetConstantBuffers(9,1,&values);context_->HSSetConstantBuffers(9,1,&values);context_->DSSetConstantBuffers(9,1,&values);context_->PSSetConstantBuffers(9,1,&values);
 ID3D11ShaderResourceView* near_view=frame.sample?terrain_bake_near_srv_.Get():nullptr;
 ID3D11ShaderResourceView* far_view=frame.sample?terrain_bake_far_srv_.Get():nullptr;
 ID3D11SamplerState* sampler=terrain_bake_sampler_.Get();
 context_->PSSetShaderResources(PointShadowTextureLayout::terrain_bake_srv,1,&near_view);
 context_->PSSetShaderResources(PointShadowTextureLayout::terrain_bake_far_srv,1,&far_view);context_->PSSetSamplers(11,1,&sampler);
 ID3D11ShaderResourceView* tables[2]{terrain_bake_pages_srv_.Get(),terrain_bake_bake_pages_srv_.Get()};
 const UINT slots[2]{PointShadowTextureLayout::terrain_bake_pages_srv,PointShadowTextureLayout::terrain_bake_bake_pages_srv};
 for(int k=0;k<2;++k){
  context_->VSSetShaderResources(slots[k],1,&tables[k]);context_->HSSetShaderResources(slots[k],1,&tables[k]);
  context_->DSSetShaderResources(slots[k],1,&tables[k]);context_->PSSetShaderResources(slots[k],1,&tables[k]);
 }
 terrain_bake_bound_=true;
 return frame;
}

// A map's blend rows changed (a shared edge, update_terrain_tile_blend_seams):
// its resident pages bake again from the new rows.
void Renderer::terrain_bake_rows_changed(UINT map) noexcept{
 if(!terrain_bake_state_)return;
 auto& state=*terrain_bake_state_;
 for(auto& [key,page]:state.pages)if(page.map==map){if(page.baking)page.stale=true;else page.baked=false;}
 state.tables_dirty=true;
}

// Whether a batch with this world XY range can reach a page baking this
// frame (its rectangle plus the gutter margin).
bool Renderer::terrain_bake_touches(UINT map,const float lo[2],const float hi[2]) const noexcept{
 if(!terrain_bake_state_)return false;
 const auto& state=*terrain_bake_state_;
 const auto* slice=state.slice(map);if(!slice)return false;
 const float cell=terrain_bake_cell_metres,margin=terrain_bake_margin,ox=float(slice->origin_x),oy=float(slice->origin_y);
 for(const auto key:state.baking){
  const auto found=state.pages.find(key);if(found==state.pages.end()||found->second.map!=map)continue;
  const auto& page=found->second;
  const float x0=ox+page.cell_x*cell-margin,y0=oy+page.cell_y*cell-margin,x1=ox+(page.cell_x+page.side)*cell+margin,y1=oy+(page.cell_y+page.side)*cell+margin;
  if(hi[0]>=x0&&lo[0]<=x1&&hi[1]>=y0&&lo[1]<=y1)return true;
 }
 return false;
}

// b13 for one draw: corner (bake) or routed flag (pixel stage) and the
// batch's map slice (first table entry, cells, world origin).
void Renderer::write_terrain_bake_draw(UINT flag,UINT map) noexcept{
 const auto* slice=terrain_bake_state_?terrain_bake_state_->slice(map):nullptr;
 struct{UINT flag,first,cells_x,cells_y;float origin_x,origin_y,pad0,pad1;}values{flag,0,0,0,0,0,0,0};
 if(slice){values.first=UINT(slice->first_cell*2);values.cells_x=UINT(slice->cells_x);values.cells_y=UINT(slice->cells_y);
  values.origin_x=float(slice->origin_x);values.origin_y=float(slice->origin_y);}
 D3D11_MAPPED_SUBRESOURCE mapped{};
 if(FAILED(context_->Map(terrain_bake_draw_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return;
 std::memcpy(mapped.pData,&values,sizeof(values));context_->Unmap(terrain_bake_draw_.Get(),0);
 ID3D11Buffer* buffer=terrain_bake_draw_.Get();
 context_->VSSetConstantBuffers(13,1,&buffer);context_->HSSetConstantBuffers(13,1,&buffer);context_->DSSetConstantBuffers(13,1,&buffer);context_->PSSetConstantBuffers(13,1,&buffer);
}

// One terrain batch drawn again into the staging atlas: four draws, one per
// candidate cell corner. The caller restores the batch's state afterwards.
void Renderer::draw_terrain_bake(ID3D11Buffer* arguments,UINT offset,bool tessellated,UINT map) noexcept{
 transition_profile_pass("terrain_bake");
 ID3D11RenderTargetView* target=terrain_bake_rtv_.Get();context_->OMSetRenderTargets(1,&target,nullptr);
 const D3D11_VIEWPORT viewport{0,0,float(terrain_bake_staging_side),float(terrain_bake_staging_side),0,1};context_->RSSetViewports(1,&viewport);
 context_->RSSetState(terrain_bake_raster_.Get());context_->OMSetBlendState(nullptr,nullptr,0xffffffffu);context_->OMSetDepthStencilState(nullptr,0);
 context_->PSSetShader(terrain_bake_ps_.Get(),nullptr,0);
 if(tessellated){context_->VSSetShader(terrain_bake_tess_vs_.Get(),nullptr,0);context_->HSSetShader(terrain_bake_hs_.Get(),nullptr,0);context_->DSSetShader(terrain_bake_ds_.Get(),nullptr,0);}
 else context_->VSSetShader(terrain_bake_vs_.Get(),nullptr,0);
 ComPtr<ID3D11Query> statistics;
 if(terrain_bake_measure_&&terrain_bake_state_&&terrain_bake_state_->bake_frame==native_frame_index_){
  const D3D11_QUERY_DESC query{D3D11_QUERY_PIPELINE_STATISTICS,0};
  if(SUCCEEDED(device_->CreateQuery(&query,&statistics)))context_->Begin(statistics.Get());
 }
 std::size_t span;begin_terrain_bake_span(span);
 for(unsigned corner=0;corner<4;++corner){
  write_terrain_bake_draw(corner,map);
  context_->DrawInstancedIndirect(arguments,offset);
 }
 end_terrain_bake_span(span);
 if(statistics){context_->End(statistics.Get());try{terrain_bake_queries_.push_back(statistics);}catch(...){}}
 // The live stages flush_native bound (without the trail code when no field exists).
 if(tessellated){context_->VSSetShader(terrain_tess_vs_.Get(),nullptr,0);
  context_->HSSetShader(snow_terrain_srv_?terrain_hs_.Get():terrain_hs_plain_.Get(),nullptr,0);
  context_->DSSetShader(snow_terrain_srv_?terrain_ds_.Get():terrain_ds_plain_.Get(),nullptr,0);}
 else context_->VSSetShader(native_vs_.Get(),nullptr,0);
 terrain_bake_drawn_=true;
}

// Routing: the batch's cached pixels through the lean baked variant first;
// the caller's live draw then skips them. PS b13 tells the live variant
// whether this batch was routed.
// single: single-pass routing (terrain_bake_route 2): no baked draw; the
// live draw uses the cache in place (b13.x 2). Returns whether a baked draw
// ran (the live draw then takes the strict depth test).
bool Renderer::draw_terrain_baked(ID3D11Buffer* arguments,UINT offset,bool tessellated,bool route,bool snow,UINT map,bool single) noexcept{
 route=route&&terrain_bake_state_&&terrain_bake_state_->slice(map);
 write_terrain_bake_draw(!route?0u:single?2u:1u,map);
 if(!route||single)return false;
 context_->PSSetShader(snow?terrain_baked_snow_ps_.Get():terrain_baked_ps_.Get(),nullptr,0);
 if(tessellated)context_->HSSetShader(terrain_baked_hs_.Get(),nullptr,0);else context_->VSSetShader(terrain_baked_vs_.Get(),nullptr,0);
 if(profiler_.pass_active())profiler_.sub_scope(terrain_profile_scope(map!=0,tessellated,TerrainProfilePart::cached)); // the caller adds its draws
 context_->DrawInstancedIndirect(arguments,offset);
 // The live hull stage flush_native bound (without the trail code when no field exists).
 if(tessellated)context_->HSSetShader(snow_terrain_srv_?terrain_hs_.Get():terrain_hs_plain_.Get(),nullptr,0);else context_->VSSetShader(native_vs_.Get(),nullptr,0);
 return true;
}

// After a flush: nothing to do here; the staging atlas's mips, compression
// and copies wait for the next frame, when every batch of the bake frame has
// drawn (finalize_terrain_bake).
void Renderer::finish_terrain_bake() noexcept{terrain_bake_drawn_=false;}

#endif
}  // namespace owned
