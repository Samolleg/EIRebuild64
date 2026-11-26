// Included inside namespace owned, after merged_geometry's cache helper.
namespace {
bool build_skn_groups(const RetainedVisualSample &sample,
                      const std::shared_ptr<const skn::RuntimeAsset> &asset,
                      const std::vector<unsigned>* attachmentNodes=nullptr,
                      bool weapon=false, bool arrow=false,
                      const std::map<unsigned,std::shared_ptr<const skn::Dds>>* overrides=nullptr,
                      const std::map<unsigned,std::shared_ptr<const skn::Dds>>* orm_overrides=nullptr,
                      const std::map<unsigned,std::shared_ptr<const std::uint8_t>>* material_ids=nullptr,
                      const std::vector<gltf::GarmentAnchor>* garment=nullptr,
                      const std::array<float,3>* offset=nullptr, bool headAttachment=false) {
  auto &prepared = *sample.prepared;
  const auto &data = asset->asset;
  const auto* physique=prepared.motion_physique.get();
  const bool shaped=physique && asset->gltf_asset && !attachmentNodes;
  if (data.semantic_joints.size() != data.submeshes.size())
    throw std::runtime_error(
        "SKN gameplay reference required for unit visibility");
  // SKN owns absent variants too: never expose an unmatched FIG draw.
  if(!attachmentNodes) prepared.merged_part_group.assign(sample.assembly->parts.size(), 0);
  std::vector<unsigned> nodes(data.joints.size(), UINT_MAX);
  for (unsigned j = 0; j < nodes.size(); ++j)
    for (unsigned k = 0; k < sample.assembly->skeleton->nodes.size(); ++k)
      if (data.joints[j].name == sample.assembly->skeleton->nodes[k].name) {
        nodes[j] = k;
        break;
      }
  if(asset->gltf_asset)for(unsigned j=0;j<nodes.size();++j)
    nodes[j]=static_cast<unsigned>(sample.assembly->skeleton->nodes.size())+j;
  if(asset->motion)for(unsigned j=0;j<nodes.size();++j)if(nodes[j]==UINT_MAX)
    for(unsigned i=0;i<asset->motion->nodes.size();++i)if(asset->motion->nodes[i].name==data.joints[j].name)
      nodes[j]=static_cast<unsigned>(sample.assembly->skeleton->nodes.size())+i;
  if(attachmentNodes) nodes=*attachmentNodes;
  for (unsigned node : nodes)
    if (node == UINT_MAX)
      throw std::runtime_error("SKN joint missing in legacy LNK");
  // One SKN can bind to host skeletons with different unrelated nodes. The
  // uploaded indices (including corrective records) are part of mesh identity.
  std::string binding_layout;
  if(offset) {
    binding_layout.append("|mount-offset|");
    binding_layout.append(reinterpret_cast<const char*>(offset->data()),sizeof(float)*3);
  }
  if(shaped || (physique && garment)){
    binding_layout.append("|physique|");
    if(garment)binding_layout.append(prepared.motion_asset->gltf_asset->cache_key);
    binding_layout.append(reinterpret_cast<const char*>(physique->input.data()),sizeof(physique->input));
  }
  for(unsigned node:nodes)binding_layout.append(reinterpret_cast<const char*>(&node),sizeof(node));
  // One draw per gameplay part/material/region, with a union vertex buffer
  // and independently authored index lists. LODs need not share vertices.
  const auto portraitGroup = [&](unsigned sub) {
    return attachmentNodes ? headAttachment : asset->gltf_asset &&
        asset->gltf_asset->portrait_head_submeshes.at(sub);
  };
  std::set<std::array<unsigned,5>> built;
  for (unsigned sub=0;sub<data.submeshes.size();++sub) {
    const auto &mesh = data.submeshes[sub];
    const std::array<unsigned,5> key{data.semantic_joints[sub],mesh.material,mesh.zone,mesh.layer,portraitGroup(sub)};
    if(!built.insert(key).second)continue;
    std::vector<std::vector<unsigned>> lists(data.lods.size());
    for(unsigned level=0;level<data.lods.size();++level){
      const auto& lod=data.lods[level];
      for(unsigned m=lod.first_submesh;m<lod.first_submesh+lod.submesh_count;++m){
        const auto& candidate=data.submeshes[m];
        if(std::array<unsigned,5>{data.semantic_joints[m],candidate.material,candidate.zone,candidate.layer,portraitGroup(m)}!=key)continue;
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
    // Log once per authored group when an appearance is built, never per frame
    // or per 16-bit geometry chunk. Logging must not reject a valid visual.
    const auto report_selection = [&](const char* event, const char* reason) noexcept {
      try {
        const std::string detail = "model=\"" + asset->identity.canonical_name +
            "\" part=\"" + data.joints[data.semantic_joints[sub]].name +
            "\" material=" + std::to_string(mesh.material) +
            " zone=" + std::to_string(mesh.zone) + " layer=" + std::to_string(mesh.layer) +
            " base_map=\"" + data.materials[mesh.material].maps[0] +
            "\" reason=\"" + reason + "\"";
        bridge_append_probe_event(event, detail.c_str());
      } catch (...) {
      }
    };
    unsigned semantic = nodes[data.semantic_joints[sub]], part = UINT_MAX;
    if(asset->gltf_asset)for(unsigned i=0;i<sample.assembly->parts.size();++i) {
      const auto& candidate=sample.assembly->parts[i];
      if(candidate.source_available && !candidate.explicitly_hidden && !candidate.weapon && !candidate.arrow &&
          candidate.geometry && !candidate.geometry->indices.empty() && !unit_service_node(candidate.name.c_str())) {
        part=i;break;
      }
    }
    for (unsigned i = 0; i < sample.assembly->parts.size(); ++i)
      if (!asset->gltf_asset && sample.assembly->parts[i].skeleton_node == semantic) {
        part = i;
        break;
      }
    if (part == UINT_MAX) {
      report_selection("skn_material_skipped", "semantic_part_absent");
      continue;
    }
    const auto& selected = sample.assembly->parts[part];
    if (!selected.source_available || selected.explicitly_hidden ||
        (sample.values.unit && unit_service_node(selected.name.c_str()))) {
      const char* reason = !selected.source_available ? "source_unavailable" :
          selected.explicitly_hidden ? "part_hidden" : "unit_service_node";
      report_selection("skn_material_skipped", reason);
      continue;
    }
    if (selected.assembled_character_texture && !data.materials[mesh.material].maps[0].empty())
      report_selection("skn_assembled_texture_skipped", "explicit_skn_base_map");
    unsigned chunk_index=0;
    for(const auto& chunk:chunks){
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
    group.gltf_portrait = portraitGroup(sub);
    if(overrides) {
      auto replacement=overrides->find(mesh.material);
      if(replacement!=overrides->end())group.skn_base_color_override=replacement->second;
    }
    if(orm_overrides) {
      auto replacement=orm_overrides->find(mesh.material);
      if(replacement!=orm_overrides->end())group.skn_metallic_roughness_override=replacement->second;
    }
    if(material_ids) {
      auto identity=material_ids->find(mesh.material);
      if(identity!=material_ids->end())group.skn_material_identity=identity->second;
    }
    group.gltf_weapon=weapon;group.gltf_arrow=arrow;
    group.skn_material = mesh.material;
    group.skn_casts_shadow = !asset->gltf_asset && selected.assembled_character_texture && data.materials[mesh.material].maps[0].empty()
        ? selected.casts_shadow : data.materials[mesh.material].alpha!=2;
    group.shape_count = 1;
    group.position_groups = group.normal_groups =
        static_cast<unsigned>((vertices.size() + 3) / 4);
    if(!attachmentNodes) prepared.merged_part_group[part] = group_index;
    const std::string content =
        "skn|" + asset->identity.key() + "|" + std::to_string(sub) + "|" + std::to_string(chunk_index++) + "|" + binding_layout;
    group.geometry = merged_geometry(
        content, [&](VisualPreparedResources::MergedGeometry &g) {
          g.positions.resize(group.position_groups);
          g.normals.resize(group.normal_groups);
          g.skin_vertices.resize(vertices.size() * sizeof(skn::GpuVertex));
          for (unsigned i = 0; i < vertices.size(); ++i) {
            auto v = data.vertices[vertices[i]];
            if(offset)for(unsigned c=0;c<3;++c)v.position[c]+=(*offset)[c];
            if(shaped)v=prepared.physique_surface->body[vertices[i]];
            else if(physique && garment)prepared.physique_surface->apply_garment(garment->at(vertices[i]),v);
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
            skn::GpuVertex skin{};
            if(asset->gltf_asset){
              const auto morph=asset->gltf_asset->vertex_morphs.at(vertices[i]);
              if(morph!=UINT32_MAX)skin.correction_node=0x80000000u|
                (static_cast<unsigned>(sample.assembly->skeleton->nodes.size()+data.joints.size())+morph);
            }
            if(asset->motion && asset->motion->correction_map[vertices[i]]>=0)
              skin.correction_node=static_cast<unsigned>(sample.assembly->skeleton->nodes.size()+asset->motion->nodes.size())+asset->motion->correction_map[vertices[i]];
            for(unsigned k=0;k<4;++k)skin.tangent[k]=v.tangent[k]/32767.f;
            for (unsigned k = 0; k < 4; ++k) {
              skin.joints[k] = nodes[v.joints[k]];
              skin.weights[k] = v.weights[k] / 65535.f;
              const auto& m=shaped?prepared.physique_surface->inverse_skin[v.joints[k]]:data.joints[v.joints[k]].inverse;
              for (unsigned row = 0; row < 3; ++row)
                for (unsigned col = 0; col < 4; ++col)
                  skin.inverse[k][row * 4 + col] = m[col * 4 + row];
            }
            std::memcpy(g.skin_vertices.data() + i * sizeof(skin), &skin,
                        sizeof(skin));
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
