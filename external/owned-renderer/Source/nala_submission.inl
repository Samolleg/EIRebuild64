// Inside namespace owned. Uses the same immutable records for all passes.
namespace {
bool build_nala_groups(const RetainedVisualSample& sample) {
 auto& prepared=*sample.prepared;
 const auto frame=nala::prepare(sample);
 if(prepared.nala_group_serial==frame->serial&&prepared.merged_assembly==sample.assembly)return true;
 prepared.merged_groups.clear();prepared.merged_part_group.assign(sample.assembly->parts.size(),-1);
 unsigned body_part=UINT_MAX;
 for(unsigned i=0;i<sample.assembly->parts.size();++i){const auto& part=sample.assembly->parts[i];
  const bool body=part.name=="hp"||part.name=="bd"||part.name=="hd"||
   part.name=="lh1"||part.name=="rh1"||part.name=="lh2"||part.name=="rh2"||part.name=="lh3"||part.name=="rh3"||
   part.name=="ll1"||part.name=="rl1"||part.name=="ll2"||part.name=="rl2"||part.name=="ll3"||part.name=="rl3"||
   part.name=="eyepoint1"||part.name=="eyepoint2";
  if(body)prepared.merged_part_group[i]=0;
  if(part.name=="hp")body_part=i;
 }
 if(body_part==UINT_MAX)throw std::runtime_error("Nala pelvis part missing");
 prepared.merged_groups.reserve(frame->meshes.size());
 for(unsigned index=0;index<frame->meshes.size();++index){const auto& mesh=frame->meshes[index];
  if(mesh.indices.empty())continue;
  prepared.merged_groups.emplace_back();auto& group=prepared.merged_groups.back();
  group.parts={body_part};group.skn_asset=frame->materials;group.skn_material=index;
  group.shape_count=1;group.position_groups=unsigned(mesh.positions.size());group.normal_groups=unsigned(mesh.normals.size());
  auto geometry=std::make_shared<VisualPreparedResources::MergedGeometry>();
  geometry->identity=frame->materials->identity;geometry->identity.canonical_name+="/"+std::to_string(index);
  geometry->positions=mesh.positions;geometry->normals=mesh.normals;geometry->texture_coordinates=mesh.uv;
  geometry->components=mesh.components;geometry->indices=mesh.indices;
  geometry->skn_lods.skn_base_triangles=unsigned(mesh.indices.size()/3);
  // Positions/normals have already been deformed and expressed once in hp
  // local space. The GPU applies only the shared hp pose and world placement.
  skn::GpuVertex skin{};skin.joints[0]=frame->root_node;skin.weights[0]=1;
  for(unsigned k=0;k<4;++k)for(unsigned a=0;a<3;++a)skin.inverse[k][a*4+a]=1;
  skin.tangent[0]=skin.tangent[3]=1;
  geometry->skin_vertices.resize(mesh.components.size()*sizeof(skin));
  for(unsigned v=0;v<mesh.components.size();++v)std::memcpy(geometry->skin_vertices.data()+v*sizeof(skin),&skin,sizeof(skin));
  group.geometry=geometry;
 }
 prepared.skn_selected=true;prepared.merged_assembly=sample.assembly;prepared.nala_group_serial=frame->serial;return true;
}
}
