// Included in namespace owned. Shares the existing retained-pose lifecycle.
namespace {
bool prepare_baked_motion(const RetainedVisualSample& sample){
 auto& prepared=*sample.prepared;auto& current=prepared.gpu_pose;
 try{
  if(prepared.motion_assembly!=sample.assembly){
   prepared.motion_pose_valid=false;prepared.motion_clip.reset();current.baked.reset();
   prepared.motion_source_binding.reset();prepared.motion_binding.reset();
   prepared.motion_asset.reset();prepared.motion_nodes.clear();prepared.motion_mounts.clear();
   prepared.motion_context=prepared.motion_root=UINT_MAX;
   bool present=false;auto asset=skn::select_visual(sample.assembly->skeleton->visual_reference,sample.assembly->main_hero_visual,present);
   if(asset&&asset->motion){
    const auto& motion=*asset->motion;const auto& assembly=*sample.assembly;
    for(unsigned k=0;k<3;++k)if(std::abs(retained_visual_shape(sample)[k]-motion.shape[k])>1e-5)
     throw std::runtime_error("SKM baked body shape does not match actor");
    std::set<std::string> equipment,slots;
    for(const auto& mount:motion.mounts)slots.insert(mount.name.substr(0,mount.name.find('.')));
    for(const auto& part:assembly.parts)if(part.source_available&&part.geometry&&!part.geometry->indices.empty()&&
        part.name.find('.')!=std::string::npos&&slots.count(part.name.substr(0,part.name.find('.'))))equipment.insert(part.name);
    for(unsigned i=0;i<motion.contexts.size();++i)if(motion.contexts[i].equipment==equipment)prepared.motion_context=i;
    if(prepared.motion_context==UINT_MAX){std::string names;for(const auto& n:equipment)names+=n+",";throw std::runtime_error("SKM unsupported equipment: "+names);}
    std::map<std::string,unsigned> nodes;for(unsigned i=0;i<assembly.skeleton->nodes.size();++i)nodes.emplace(assembly.skeleton->nodes[i].name,i);
    const auto index=[&](const std::string& name){auto it=nodes.find(name);if(it==nodes.end())throw std::runtime_error("SKM missing LNK node: "+name);return it->second;};
    for(unsigned i=0;i<motion.nodes.size();++i){
     if(nodes.count(motion.nodes[i].name))throw std::runtime_error("SKM authored node collides with game skeleton");
     prepared.motion_nodes.push_back(static_cast<unsigned>(assembly.skeleton->nodes.size())+i);
    }
    for(const auto& mount:motion.mounts)prepared.motion_mounts.push_back(index(mount.name));
    prepared.motion_root=index(motion.root_name);prepared.motion_asset=asset;
   }
   prepared.motion_assembly=sample.assembly;
  }
  if(!prepared.motion_asset){current.baked.reset();return true;}
  if(prepared.motion_source_binding!=current.binding){
   prepared.motion_pose_valid=false;prepared.motion_source_binding=current.binding;
   auto model=std::make_shared<ModelCpuAsset>(*current.binding->model);
   const auto& motion=*prepared.motion_asset->motion;
   for(unsigned i=0;i<motion.nodes.size();++i){
    ModelNodeCpuAsset node;node.name=motion.nodes[i].name;node.transform_available=false;
    ModelLink link;link.child=node.name;const auto parent=motion.nodes[i].parent;
    link.parent_index=parent<0?prepared.motion_root:prepared.motion_nodes[parent];
    link.parent=parent<0?motion.root_name:motion.nodes[parent].name;
    model->nodes.push_back(std::move(node));model->links.push_back(std::move(link));
   }
   auto binding=std::make_shared<VisualSkeletonBinding>(*current.binding);binding->model=model;
   binding->tracks.resize(model->nodes.size(),nullptr);
   prepared.motion_binding=std::move(binding);
  }
  current.binding=prepared.motion_binding;
  if(prepared.motion_pose_valid&&owned_visual_pose_key_matches(prepared.motion_pose_key,sample))return true;
  const auto& asset=*prepared.motion_asset->motion;const auto& context=asset.contexts[prepared.motion_context];
  std::string clip_name=context.neutral;
  if(!sample.selection.neutral){
   if(!sample.selection.database||sample.selection.ordinal>=sample.selection.database->entries.size())throw std::runtime_error("SKM no selected clip");
   clip_name=sample.selection.database->entries[sample.selection.ordinal].name;
  }
  const auto reference=context.clips.find(clip_name);if(reference==context.clips.end())throw std::runtime_error("SKM selected clip missing: "+clip_name);
  if(!prepared.motion_clip||prepared.motion_clip->name!=clip_name){
   prepared.motion_clip=skn::motion_clip(asset,clip_name,reference->second);
   const auto detail="model="+sample.assembly->skeleton->visual_reference+" context="+context.name+" clip="+clip_name+" author_nodes="+std::to_string(asset.nodes.size());
   bridge_append_probe_event("skm_clip_selected",detail.c_str());
  }
  const auto evaluated=skm::evaluate(asset,*prepared.motion_clip,sample.selection.neutral?0:sample.pose.phase);
  // Keep the snapshot model-local: portrait/native commands may replace
  // root_to_world after retained evaluation. Placement belongs to the sink.
  const auto placement=skm::identity();
  auto result=std::make_shared<std::vector<NativeWorldGeometry::Pose::Node>>(current.binding->tracks.size()+asset.correction_count);
  const auto rows=[&](unsigned index,const skm::Matrix& model){auto world=skm::mul(placement,model);for(unsigned r=0;r<3;++r)for(unsigned k=0;k<4;++k)(*result)[index].rows[r][k]=static_cast<float>(world[r*4+k]);};
  // Legacy semantic nodes retain their selected gameplay animation. The
  // authored nodes and explicit full attachment matrices replace named rows.
  std::vector<VisualNodeSample> legacy;
  if(!sample_visual_skeleton(*sample.assembly->skeleton,sample.selection,current.shape,current.center_shift,sample.pose.phase,legacy,false))throw std::runtime_error("SKM legacy semantics unavailable");
  VisualPoseState local;local.tilted_facing=local.root_rotation_valid=true;local.root_rotation={1,0,0,0};
  for(unsigned i=0;i<legacy.size();++i){std::array<float,16> m{};if(!visual_node_world_matrix(local,legacy[i],{1,1,1},m))throw std::runtime_error("SKM legacy transform invalid");skm::Matrix matrix;std::copy(m.begin(),m.end(),matrix.begin());rows(i,matrix);(*result)[i].rotation=legacy[i].rotation;for(unsigned k=0;k<3;++k)(*result)[i].translation[k]=legacy[i].translation[k];}
  rows(prepared.motion_root,evaluated.root);
  for(unsigned i=0;i<prepared.motion_nodes.size();++i)rows(prepared.motion_nodes[i],evaluated.native[i]);
  for(unsigned i=0;i<prepared.motion_mounts.size();++i)rows(prepared.motion_mounts[i],evaluated.mounts[i]);
  const auto root=skm::mul(placement,evaluated.root);
  for(unsigned i=0;i<evaluated.corrections.size();++i){
   const auto& correction=evaluated.corrections[i];auto delta=skm::vector(root,correction.data());auto normal=skm::normal(root,correction.data()+3);
   auto& target=(*result)[current.binding->tracks.size()+i];
   for(unsigned k=0;k<3;++k){target.rows[0][k]=static_cast<float>(delta[k]*prepared.motion_clip->scale);target.rows[1][k]=static_cast<float>(normal[k]);}
   target.rows[0][3]=1;
   target.morph_count=UINT_MAX; // corrective vector/normal record, not a node
  }
  for(const auto& node:*result)for(const auto& row:node.rows)for(float value:row)if(!std::isfinite(value))throw std::runtime_error("SKM nonfinite sampled pose");
  current.baked=std::move(result);prepared.motion_pose_key=owned_visual_pose_key(sample);prepared.motion_pose_valid=true;
  return true;
 }catch(const std::exception& error){
  current.baked.reset();prepared.motion_pose_valid=false;
  skn::report_rejection(sample.assembly->skeleton->visual_reference,error.what());return false;
 }
}
}
