// Compile the production storage methods against an instrumented GPU allocator.
// This checks allocation/upload and ownership, separately from shader readback.
#include <Windows.h>
#include <d3d11.h>
#include "gltf_physique_pose.h"
#include "owned_visual_evaluation.h"
#include <unordered_map>
#include <iostream>
#include <cstring>
#include <stdexcept>
namespace owned {
std::string ResourceIdentity::key() const { return canonical_name; }
static UINT align_up(UINT n,UINT a){return (n+a-1)&~(a-1);}
struct Renderer {
 struct Range {UINT base=0,count=0,owners=0,references=0;bool owned=true;};
 struct NativeSkeletonAsset {UINT offset=0,bytes=0,allocation=UINT_MAX;std::weak_ptr<const ModelCpuAsset> model;
  std::weak_ptr<const AnimationClipAsset> clip;bool animated=false;UINT gltf_allocation=UINT_MAX,physique_allocation=UINT_MAX;};
 struct NativeGltfLibrary {UINT allocation=UINT_MAX;std::weak_ptr<const gltf::GpuProgram> program;};
 enum class NativeRangeBuffer{animation};
 struct Buffer {void* Get(){return this;}} native_animation_;
 struct Profile {std::size_t bytes=0;void add_upload(std::size_t n){bytes+=n;}} profiler_;
 struct Context {Renderer* renderer;void UpdateSubresource(void*,UINT,const D3D11_BOX* box,const void* data,UINT,UINT){
  std::memcpy(renderer->memory.data()+box->left,data,box->right-box->left);}} context{this};
 Context* context_=&context;
 struct D12 {Renderer* renderer;bool fail=false;HRESULT write_buffer(int,UINT offset,const void* data,std::size_t size){
  if(fail)return E_FAIL;std::memcpy(renderer->memory.data()+offset,data,size);return S_OK;}} d12{this};
 D12* d3d12_=nullptr;int handle=0;struct {int* animation;} native12_{&handle};
 std::vector<unsigned char> memory;
 std::vector<Range> native_animation_ranges_;
 std::unordered_map<std::string,NativeSkeletonAsset> native_skeleton_assets_;
 std::unordered_map<std::string,NativeGltfLibrary> native_gltf_libraries_;
 HRESULT allocate_native_range(std::vector<Range>& ranges,UINT size,NativeRangeBuffer,UINT& allocation){
  allocation=UINT(ranges.size());ranges.push_back({UINT(memory.size()),size});memory.resize(memory.size()+size);return S_OK;}
 void native_range_freed(std::vector<Range>& ranges,UINT allocation){
  auto& r=ranges[allocation];if(!r.owned&&!r.owners&&!r.references)r.count=0;}
 HRESULT get_native_gltf_library(const std::shared_ptr<const gltf::GpuProgram>&,UINT&,UINT&);
 HRESULT get_native_skeleton_asset(const std::shared_ptr<const VisualSkeletonBinding>&,UINT&,UINT&);
 void retire_native_animation_assets();
};
#include "native_animation_storage.inl"
}
static void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
int main(){try{
 using namespace owned;
 for(bool d12:{false,true}){
  Renderer r;if(d12)r.d3d12_=&r.d12;
  auto program=std::make_shared<gltf::GpuProgram>();program->key="library";program->bytes.resize(1024*1024,0x5a);
  auto model=std::make_shared<ModelCpuAsset>();model->model_container.canonical_name="model";
  model->nodes.resize(1);model->links.resize(1);model->links[0].parent_index=UINT_MAX;
  std::vector<std::shared_ptr<VisualSkeletonBinding>> held;std::vector<UINT> allocations;
  for(unsigned i=0;i<253;++i){
   auto clip=std::make_shared<AnimationClipAsset>();clip->identity.canonical_name="clip"+std::to_string(i);
   auto binding=std::make_shared<VisualSkeletonBinding>();binding->model=model;binding->clip=clip;
   binding->tracks.resize(1);binding->level_offsets={0,1};binding->level_nodes={0};binding->gltf_animation=program;
   UINT offset,allocation;check(SUCCEEDED(r.get_native_skeleton_asset(binding,offset,allocation)),"skeleton upload");
   unsigned relative;std::memcpy(&relative,r.memory.data()+offset+24,4);check(relative!=0&&offset+relative==0,"shared relative library pointer");
   const auto before=r.profiler_.bytes;UINT again,againAllocation;
   check(SUCCEEDED(r.get_native_skeleton_asset(binding,again,againAllocation))&&again==offset&&againAllocation==allocation,"preview reuses binding");
   check(r.profiler_.bytes==before,"preview uploaded twice");held.push_back(binding);allocations.push_back(allocation);
  }
  check(r.native_gltf_libraries_.size()==1,"library duplicated per action");
  check(r.profiler_.bytes<program->bytes.size()+253*256,"library payload multiplied");
  const auto library=r.native_gltf_libraries_.at("library").allocation;
  check(r.native_animation_ranges_[library].owners==253,"library skeleton owners");
  r.native_animation_ranges_[allocations[0]].references=1;held.clear();program.reset();model.reset();
  r.retire_native_animation_assets();
  check(r.native_skeleton_assets_.size()==1&&r.native_gltf_libraries_.size()==1,"queued draw lost its library");
  r.native_animation_ranges_[allocations[0]].references=0;r.retire_native_animation_assets();
  check(r.native_skeleton_assets_.empty()&&r.native_gltf_libraries_.empty(),"expired library retained");
  check(r.native_animation_ranges_[library].count==0,"library range not freed");
 }
 for(bool dx12:{false,true}){
  Renderer r;if(dx12)r.d3d12_=&r.d12;
  auto animation=std::make_shared<gltf::GpuProgram>();animation->key="clips";animation->nodes=1;animation->bytes.resize(128);
  auto shape=std::make_shared<gltf::PhysiquePose>();shape->input={18,33,.3f,.4f,.75f};shape->locals.resize(1);
  auto palette=std::make_shared<gltf::GpuProgram>();palette->key="shape1";palette->nodes=1;palette->bytes.resize(68);
  shape->gpu=palette;
  auto model=std::make_shared<ModelCpuAsset>();model->nodes.resize(1);model->links.resize(1);
  std::vector<std::shared_ptr<VisualSkeletonBinding>> held;
  UINT pinned=UINT_MAX;
  for(unsigned i=0;i<253;++i){
   auto b=std::make_shared<VisualSkeletonBinding>();b->model=model;b->physique=shape;b->gltf_animation=animation;
   auto clip=std::make_shared<AnimationClipAsset>();clip->identity.canonical_name=std::to_string(i);b->clip=clip;
   b->tracks.resize(1);b->level_offsets={0,1};b->level_nodes={0};UINT at,slot;
   check(SUCCEEDED(r.get_native_skeleton_asset(b,at,slot)),"physique header upload");
   UINT disp=0,tag=0;std::memcpy(&disp,r.memory.data()+at+32,4);std::memcpy(&tag,r.memory.data()+at+28,4);
   const auto alloc=r.native_gltf_libraries_.at("shape1").allocation;
   check(tag==0x80000000u && at+disp==r.native_animation_ranges_[alloc].base+16,"shared physique address");
   pinned=slot;held.push_back(b);
  }
  check(r.native_gltf_libraries_.size()==2 && r.native_animation_ranges_[r.native_gltf_libraries_.at("shape1").allocation].owners==253,"physique duplicated per action");
  r.native_animation_ranges_[pinned].references=1;held.clear();model.reset();shape.reset();palette.reset();animation.reset();
  r.retire_native_animation_assets();check(r.native_gltf_libraries_.size()==2,"queued shaped draw lost data");
  r.native_animation_ranges_[pinned].references=0;r.retire_native_animation_assets();
  check(r.native_gltf_libraries_.empty(),"shape allocation leaked");
 }
 std::cout<<"PASS one physique palette for 253 actions, both backends, queued draws and retirement\n";
 Renderer r;r.d3d12_=&r.d12;r.d12.fail=true;
 auto p=std::make_shared<gltf::GpuProgram>();p->key="failure";p->bytes.resize(256);UINT offset,allocation;
 check(FAILED(r.get_native_gltf_library(p,offset,allocation)),"upload failure ignored");
 check(r.native_gltf_libraries_.empty()&&r.native_animation_ranges_[allocation].count==0,"failed upload published or leaked");
 r.d12.fail=false;check(SUCCEEDED(r.get_native_gltf_library(p,offset,allocation)),"retry after failed upload");
 std::cout<<"PASS production storage: 253 actions share one upload; previews reuse; queued draw pins library; expiry frees; failed upload retries\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
