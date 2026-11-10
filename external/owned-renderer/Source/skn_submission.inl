// Included inside namespace owned, after merged_geometry's cache helper.
namespace {
bool build_skn_groups(const RetainedVisualSample &sample,
                      const std::shared_ptr<const skn::RuntimeAsset> &asset) {
  auto &prepared = *sample.prepared;
  const auto &data = asset->asset;
  if (data.semantic_joints.size() != data.submeshes.size())
    throw std::runtime_error(
        "SKN gameplay reference required for unit visibility");
  // SKN owns absent variants too: never expose an unmatched FIG draw.
  prepared.merged_part_group.assign(sample.assembly->parts.size(), 0);
  std::vector<unsigned> nodes(data.joints.size(), UINT_MAX);
  for (unsigned j = 0; j < nodes.size(); ++j)
    for (unsigned k = 0; k < sample.assembly->skeleton->nodes.size(); ++k)
      if (data.joints[j].name == sample.assembly->skeleton->nodes[k].name) {
        nodes[j] = k;
        break;
      }
  if(data.nala_rig)for(unsigned j=0;j<nodes.size();++j)
    nodes[j]=static_cast<unsigned>(sample.assembly->skeleton->nodes.size())+j;
  for (unsigned node : nodes)
    if (node == UINT_MAX)
      throw std::runtime_error("SKN joint missing in legacy LNK");
  std::map<unsigned,const skn::Asset::Contact*> contacts;
  for(const auto& c:data.nala_contacts)contacts.emplace(c.target,&c);
  // One draw per gameplay part/material/region, with a union vertex buffer
  // and independently authored index lists. LODs need not share vertices.
  std::set<std::array<unsigned,4>> built;
  for (unsigned sub=0;sub<data.submeshes.size();++sub) {
    const auto &mesh = data.submeshes[sub];
    const std::array<unsigned,4> key{data.semantic_joints[sub],mesh.material,mesh.zone,mesh.layer};
    if(!built.insert(key).second)continue;
    std::vector<std::vector<unsigned>> lists(data.lods.size());
    for(unsigned level=0;level<data.lods.size();++level){
      const auto& lod=data.lods[level];
      for(unsigned m=lod.first_submesh;m<lod.first_submesh+lod.submesh_count;++m){
        const auto& candidate=data.submeshes[m];
        if(std::array<unsigned,4>{data.semantic_joints[m],candidate.material,candidate.zone,candidate.layer}!=key)continue;
        for(unsigned i=candidate.first;i<candidate.first+candidate.count;++i)lists[level].push_back(data.indices[i]);
      }
    }
    // Native components are 16-bit. Partition triangle lists across all
    // authored levels rather than rejecting a valid high resolution skin.
    std::vector<std::vector<std::vector<unsigned>>> chunks(1,
        std::vector<std::vector<unsigned>>(lists.size()));
    std::set<unsigned> used;
    for(unsigned level=0;level<lists.size();++level)for(unsigned i=0;i<lists[level].size();i+=3){
      const unsigned triangle[3]={lists[level][i],lists[level][i+1],lists[level][i+2]};
      unsigned additional=0;for(unsigned v:triangle)if(!used.count(v))++additional;
      if(used.size()+additional>65532){chunks.emplace_back(lists.size());used.clear();}
      for(unsigned v:triangle){chunks.back()[level].push_back(v);used.insert(v);}
    }
    unsigned chunk_index=0;
    for(const auto& chunk:chunks){
    unsigned semantic = nodes[data.semantic_joints[sub]], part = UINT_MAX;
    if(data.nala_rig)for(unsigned i=0;i<sample.assembly->skeleton->nodes.size();++i)
      if(sample.assembly->skeleton->nodes[i].name=="hp")semantic=i;
    for (unsigned i = 0; i < sample.assembly->parts.size(); ++i)
      if (sample.assembly->parts[i].skeleton_node == semantic) {
        part = i;
        break;
      }
    if (part == UINT_MAX)
      continue;
    const auto& selected = sample.assembly->parts[part];
    if (!selected.source_available || selected.explicitly_hidden ||
        (sample.values.unit && unit_service_node(selected.name.c_str())))
      continue;
    std::map<unsigned, unsigned> unique;
    std::vector<unsigned> vertices;
    for(const auto& list:chunk)for(unsigned v:list) {
      if (!unique.count(v)) {
        unique[v] = static_cast<unsigned>(vertices.size());
        vertices.push_back(v);
      }
    }
    if(vertices.empty())continue;
    // Defensive guard after the block partition above; never truncate indices.
    if (vertices.size() > 65532)
      throw std::runtime_error(
          "SKN submesh exceeds native record component limit");
    unsigned group_index = static_cast<unsigned>(prepared.merged_groups.size());
    prepared.merged_groups.emplace_back();
    auto &group = prepared.merged_groups.back();
    group.parts = {part};
    group.skn_asset = asset;
    group.skn_material = mesh.material;
    group.skn_casts_shadow = selected.assembled_character_texture
        ? selected.casts_shadow : data.materials[mesh.material].alpha!=2;
    group.shape_count = 1;
    group.position_groups = group.normal_groups =
        static_cast<unsigned>((vertices.size() + 3) / 4);
    prepared.merged_part_group[part] = group_index;
    if(data.nala_rig)for(unsigned i=0;i<sample.assembly->parts.size();++i){
      const auto& name=sample.assembly->parts[i].name;
      static const char* body[]={"hp","bd","hd","lh1","rh1","lh2","rh2","lh3","rh3","ll1","rl1","ll2","rl2","ll3","rl3","eyepoint1","eyepoint2"};
      for(const char* n:body)if(name==n)prepared.merged_part_group[i]=group_index;
    }
    const std::string content =
        "skn|" + asset->identity.key() + "|" + std::to_string(sub) + "|" + std::to_string(chunk_index++);
    group.geometry = merged_geometry(
        content, [&](VisualPreparedResources::MergedGeometry &g) {
          g.positions.resize(group.position_groups);
          g.normals.resize(group.normal_groups);
          const unsigned stride=data.nala_rig?sizeof(skn::SkinVertex):sizeof(skn::GpuVertex);
          g.skin_vertices.resize(vertices.size() * stride);
          for (unsigned i = 0; i < vertices.size(); ++i) {
            const auto &v = data.vertices[vertices[i]];
            unsigned block = i / 4, lane = i % 4;
            g.positions[block].x[lane] = v.position[0];
            g.positions[block].y[lane] = v.position[1];
            g.positions[block].z[lane] = v.position[2];
            g.positions[block].w[lane] = 1;
            g.normals[block].x[lane] = v.normal[0] / 32767.f;
            g.normals[block].y[lane] = v.normal[1] / 32767.f;
            g.normals[block].z[lane] = v.normal[2] / 32767.f;
            g.normals[block].w[lane] = 1;
            g.texture_coordinates.push_back(
                {skn::half_float(v.uv[0]), skn::half_float(v.uv[1])});
            g.components.push_back({static_cast<std::uint16_t>(i),
                                    static_cast<std::uint16_t>(i),
                                    static_cast<std::uint16_t>(i)});
            skn::SkinVertex record{};
            auto& skin=record.skin;
            for(unsigned k=0;k<4;++k)skin.tangent[k]=v.tangent[k]/32767.f;
            for (unsigned k = 0; k < 4; ++k) {
              skin.joints[k] = nodes[v.joints[k]];
              skin.weights[k] = v.weights[k] / 65535.f;
              const auto &m = data.joints[v.joints[k]].inverse;
              for (unsigned row = 0; row < 3; ++row)
                for (unsigned col = 0; col < 4; ++col)
                  skin.inverse[k][row * 4 + col] = m[col * 4 + row];
            }
            if(data.nala_rig){
              const auto item=contacts.find(vertices[i]);
              if(item==contacts.end())throw std::runtime_error("Nala visible contact missing");
              const auto* found=item->second;
              record.blend[3]=1;
              for(unsigned a=0;a<3;++a){
                record.blend[a]=found->weight[a];
                const auto& source=data.vertices[found->source[a]];auto& dst=record.source[a];
                for(unsigned k=0;k<4;++k){dst.joints[k]=nodes[source.joints[k]];dst.weights[k]=source.weights[k]/65535.f;
                  for(unsigned row=0;row<3;++row)dst.inverse[k][row*4+row]=1;}
                for(unsigned axis=0;axis<3;++axis){dst.position[axis]=source.position[axis];dst.normal[axis]=source.normal[axis]/32767.f;}
                dst.position[3]=1;
              }
            }
            std::memcpy(g.skin_vertices.data() + i * stride, &record,
                        stride);
          }
          g.skn_lods.levels=static_cast<unsigned>(data.lods.size()-1);
          g.skn_lods.skn_base_triangles=static_cast<unsigned>(chunk[0].size()/3);
          for(unsigned level=0;level<chunk.size();++level){
            for(unsigned v:chunk[level])g.indices.push_back(static_cast<std::uint16_t>(unique.at(v)));
            if(!level)continue;
            for(unsigned v:chunk[level])g.skn_lods.indices[level-1].push_back(unique.at(v));
            g.skn_lods.error[level-1]=data.lods[level].error;
            g.skn_lods.hysteresis[level-1]=data.lods[level].hysteresis;
          }
        });
    }
  }
  return true;
}
} // namespace
