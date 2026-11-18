// Shared immutable animation storage; included inside namespace owned.
HRESULT Renderer::get_native_gltf_library(
 const std::shared_ptr<const gltf::GpuProgram>& program,UINT& offset,UINT& allocation){
 offset=0;allocation=UINT_MAX;
 if(!program||program->bytes.empty()||program->bytes.size()>UINT_MAX-15u)return E_INVALIDARG;
 const auto found=native_gltf_libraries_.find(program->key);
 if(found!=native_gltf_libraries_.end()){
  found->second.program=program;allocation=found->second.allocation;
  offset=native_animation_ranges_[allocation].base;return S_OK;
 }
 const UINT size=static_cast<UINT>(program->bytes.size());
 HRESULT hr=allocate_native_range(native_animation_ranges_,align_up(size,16),NativeRangeBuffer::animation,allocation);
 if(FAILED(hr))return hr;
 offset=native_animation_ranges_[allocation].base;
 const auto release=[&]{native_animation_ranges_[allocation].owned=false;native_range_freed(native_animation_ranges_,allocation);};
 if(d3d12_){hr=d3d12_->write_buffer(*native12_.animation,offset,program->bytes.data(),size);}
 else {D3D11_BOX box{};box.left=offset;box.right=offset+size;box.bottom=box.back=1;
  context_->UpdateSubresource(native_animation_.Get(),0,&box,program->bytes.data(),0,0);}
 if(FAILED(hr)){release();return hr;}
 try{native_gltf_libraries_.emplace(program->key,NativeGltfLibrary{allocation,program});}
 catch(...){release();throw;}
 profiler_.add_upload(size);return S_OK;
}

HRESULT Renderer::get_native_skeleton_asset(
 const std::shared_ptr<const VisualSkeletonBinding>& binding,UINT& offset,UINT& allocation){
 offset=0;allocation=UINT_MAX;
 if(!binding||!binding->model||binding->tracks.size()!=binding->model->nodes.size()||
    binding->level_offsets.size()<2||binding->level_nodes.size()>binding->tracks.size()||
    binding->level_offsets.back()!=binding->level_nodes.size())return E_INVALIDARG;
 try{
  std::string key;
  const auto add=[&](const ResourceIdentity& identity){const auto value=identity.key();
   const auto length=static_cast<UINT>(value.size());key.append(reinterpret_cast<const char*>(&length),4);key.append(value);};
  add(binding->model->model_container);add(binding->model->bone_container);
  key.push_back(binding->rigid?1:(binding->neutral?2:3));if(binding->clip)add(binding->clip->identity);
  key.append("|nodes="+std::to_string(binding->tracks.size()));
  if(binding->gltf_animation)key.append("|gltf="+binding->gltf_animation->key+"|base="+std::to_string(binding->gltf_node_base));
  if(binding->physique){
   key.append("|physique|");
   key.append(reinterpret_cast<const char*>(binding->physique->input.data()),sizeof(binding->physique->input));
  }
  const auto found=native_skeleton_assets_.find(key);
  if(found!=native_skeleton_assets_.end()){
   found->second.model=binding->model;found->second.clip=binding->clip;
   offset=found->second.offset;allocation=found->second.allocation;return S_OK;
  }
  struct Node{UINT parent,bones,bone_count,rotations,rotation_count,translations,translation_count,padding;
   UINT morphs,morph_frames,morph_points,tail;};
  const auto count=static_cast<UINT>(binding->tracks.size());
  std::vector<Node> nodes(count);
  std::vector<std::uint8_t> bytes(binding->physique?48:32);
  const auto append=[&](const void* data,std::uint64_t length,UINT& target){
   const std::uint64_t start=(bytes.size()+15ull)&~15ull;
   if(start>UINT_MAX||length>UINT_MAX-start)return false;
   target=static_cast<UINT>(start);bytes.resize(static_cast<std::size_t>(start+length));
   if(length)std::memcpy(bytes.data()+start,data,static_cast<std::size_t>(length));return true;
  };
  UINT node_offset=0,level_offsets=0,level_nodes=0;
  if(!append(nodes.data(),nodes.size()*sizeof(Node),node_offset)||
     !append(binding->level_offsets.data(),binding->level_offsets.size()*4ull,level_offsets)||
     !append(binding->level_nodes.data(),binding->level_nodes.size()*4ull,level_nodes))
   return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
  for(UINT i=0;i<count;++i){
   const auto& model_node=binding->model->nodes[i];auto& node=nodes[i];
   if(!model_node.transform_available)continue; // no level dispatch references this index
   node.parent=binding->model->links[i].parent_index;node.bone_count=static_cast<UINT>(model_node.bone_positions.size()/3);
   if(!append(model_node.bone_positions.data(),model_node.bone_positions.size()*4ull,node.bones))return E_OUTOFMEMORY;
   if(const auto* track=binding->tracks[i]){
    if(track->rotations.size()>UINT_MAX||track->translations.size()>UINT_MAX)return E_INVALIDARG;
    node.rotation_count=static_cast<UINT>(track->rotations.size());node.translation_count=static_cast<UINT>(track->translations.size());
    if(!append(track->rotations.data(),track->rotations.size()*16ull,node.rotations)||
       !append(track->translations.data(),track->translations.size()*12ull,node.translations))return E_OUTOFMEMORY;
    node.morph_frames=track->morph_frames;node.morph_points=track->morph_points;
    if(std::uint64_t(node.morph_frames)*node.morph_points!=track->morphs.size())return E_INVALIDARG;
    if(!append(track->morphs.data(),track->morphs.size()*12ull,node.morphs))return E_OUTOFMEMORY;
   }
  }
  std::memcpy(bytes.data()+node_offset,nodes.data(),nodes.size()*sizeof(Node));
  UINT gltf_offset=0,gltf_allocation=UINT_MAX;
  if(binding->gltf_animation){
   HRESULT hr=get_native_gltf_library(binding->gltf_animation,gltf_offset,gltf_allocation);if(FAILED(hr))return hr;
  }
  UINT shapeOffset=0,shapeAllocation=UINT_MAX;
  if(binding->physique){
   if(!binding->gltf_animation || !binding->physique->gpu ||
      binding->physique->gpu->nodes!=binding->gltf_animation->nodes)return E_INVALIDARG;
   HRESULT got=get_native_gltf_library(binding->physique->gpu,shapeOffset,shapeAllocation);
   if(FAILED(got))return got;
  }
  const UINT header[8]={count,static_cast<UINT>(binding->level_offsets.size()-1),node_offset,
   level_offsets,level_nodes,binding->rigid?1u:0u,gltf_offset,binding->gltf_node_base|(binding->physique?0x80000000u:0u)};std::memcpy(bytes.data(),header,sizeof(header));
  if(bytes.size()>UINT_MAX-15u)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
  const UINT aligned=static_cast<UINT>((bytes.size()+15ull)&~15ull);
  HRESULT hr=allocate_native_range(native_animation_ranges_,aligned,NativeRangeBuffer::animation,allocation);
  if(FAILED(hr))return hr;
  const UINT start=native_animation_ranges_[allocation].base;
  if(shapeAllocation!=UINT_MAX){
   const UINT relative=shapeOffset+16-start;
   std::memcpy(bytes.data()+32,&relative,4);
  }
  if(gltf_allocation!=UINT_MAX){
   // Keep the existing shader ABI: base = pose.asset + relative (uint32).
   // A shared library can precede this header. Unsigned subtraction encodes
   // that displacement modulo 2^32; shader addition restores its byte address.
   // The two live allocations are disjoint, so zero still means no library.
   const UINT relative=gltf_offset-start;
   std::memcpy(bytes.data()+24,&relative,sizeof(relative));
  }
  NativeSkeletonAsset asset{start,static_cast<UINT>(bytes.size()),allocation,binding->model,binding->clip,bool(binding->clip),gltf_allocation,shapeAllocation};
  D3D11_BOX box{};box.left=start;box.right=start+static_cast<UINT>(bytes.size());box.bottom=box.back=1;
  if(d3d12_){ // D3D12 backend: the skeleton asset into the executor's animation buffer
   const HRESULT written=d3d12_->write_buffer(*native12_.animation,start,bytes.data(),bytes.size());
   if(FAILED(written)){native_animation_ranges_[allocation].owned=false;native_range_freed(native_animation_ranges_,allocation);return written;}
  }else context_->UpdateSubresource(native_animation_.Get(),0,&box,bytes.data(),0,0);
  try{native_skeleton_assets_.emplace(std::move(key),std::move(asset));}
  catch(...){native_animation_ranges_[allocation].owned=false;native_range_freed(native_animation_ranges_,allocation);throw;}
  if(gltf_allocation!=UINT_MAX)++native_animation_ranges_[gltf_allocation].owners;
  if(shapeAllocation!=UINT_MAX)++native_animation_ranges_[shapeAllocation].owners;
  profiler_.add_upload(bytes.size());
  offset=start;
  return S_OK;
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
}

void Renderer::retire_native_animation_assets(){
 for(auto iterator=native_skeleton_assets_.begin();iterator!=native_skeleton_assets_.end();){
  const auto& asset=iterator->second;auto& range=native_animation_ranges_[asset.allocation];
  if(!range.owners&&!range.references&&(asset.model.expired()||(asset.animated&&asset.clip.expired()))){
   if(asset.gltf_allocation!=UINT_MAX)--native_animation_ranges_[asset.gltf_allocation].owners;
   if(asset.physique_allocation!=UINT_MAX)--native_animation_ranges_[asset.physique_allocation].owners;
   range.owned=false;native_range_freed(native_animation_ranges_,asset.allocation);
   iterator=native_skeleton_assets_.erase(iterator);
  }else ++iterator;
 }
 for(auto iterator=native_gltf_libraries_.begin();iterator!=native_gltf_libraries_.end();){
  const auto& library=iterator->second;auto& range=native_animation_ranges_[library.allocation];
  // Each skeleton keeps its library alive until both retained poses and queued
  // draws release that skeleton. A cached model can reuse the library later.
  if(!range.owners&&!range.references&&library.program.expired()){
   range.owned=false;native_range_freed(native_animation_ranges_,library.allocation);
   iterator=native_gltf_libraries_.erase(iterator);
  }else ++iterator;
 }
}
