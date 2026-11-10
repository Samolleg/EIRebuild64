// Included inside namespace owned, after merged_geometry's cache helper.
namespace {
bool build_skn_groups(const RetainedVisualSample &sample,
                      const std::shared_ptr<const skn::RuntimeAsset> &asset) {
  auto &prepared = *sample.prepared;
  const auto &data = asset->asset;
  if (data.semantic_joints.size() != data.submeshes.size())
    throw std::runtime_error(
        "SKN gameplay reference required for unit visibility");
  std::vector<unsigned> nodes(data.joints.size(), UINT_MAX);
  for (unsigned j = 0; j < nodes.size(); ++j)
    for (unsigned k = 0; k < sample.assembly->skeleton->nodes.size(); ++k)
      if (data.joints[j].name == sample.assembly->skeleton->nodes[k].name) {
        nodes[j] = k;
        break;
      }
  for (unsigned node : nodes)
    if (node == UINT_MAX)
      throw std::runtime_error("SKN joint missing in legacy LNK");
  for (unsigned sub = data.lods[0].first_submesh;
       sub < data.lods[0].first_submesh + data.lods[0].submesh_count; ++sub) {
    const auto &mesh = data.submeshes[sub];
    unsigned semantic = nodes[data.semantic_joints[sub]], part = UINT_MAX;
    for (unsigned i = 0; i < sample.assembly->parts.size(); ++i)
      if (sample.assembly->parts[i].skeleton_node == semantic) {
        part = i;
        break;
      }
    if (part == UINT_MAX || !mesh.count)
      continue;
    std::map<unsigned, unsigned> unique;
    std::vector<unsigned> vertices;
    for (unsigned i = mesh.first; i < mesh.first + mesh.count; ++i) {
      unsigned v = data.indices[i];
      if (!unique.count(v)) {
        unique[v] = static_cast<unsigned>(vertices.size());
        vertices.push_back(v);
      }
    }
    // Native records have 16-bit components; split topology offline rather than
    // truncating it, including when a future exporter supplies a large mesh.
    if (vertices.size() > 65532)
      throw std::runtime_error(
          "SKN submesh exceeds native record component limit");
    unsigned group_index = static_cast<unsigned>(prepared.merged_groups.size());
    prepared.merged_groups.emplace_back();
    auto &group = prepared.merged_groups.back();
    group.parts = {part};
    group.skn_asset = asset;
    group.skn_material = mesh.material;
    group.shape_count = 1;
    group.position_groups = group.normal_groups =
        static_cast<unsigned>((vertices.size() + 3) / 4);
    prepared.merged_part_group[part] = group_index;
    const std::string content =
        "skn|" + asset->identity.key() + "|" + std::to_string(sub);
    group.geometry = merged_geometry(
        content, [&](VisualPreparedResources::MergedGeometry &g) {
          g.positions.resize(group.position_groups);
          g.normals.resize(group.normal_groups);
          g.skin_vertices.resize(vertices.size() * sizeof(skn::GpuVertex));
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
            skn::GpuVertex skin{};
            for (unsigned k = 0; k < 4; ++k) {
              skin.joints[k] = nodes[v.joints[k]];
              skin.weights[k] = v.weights[k] / 65535.f;
              const auto &m = data.joints[v.joints[k]].inverse;
              for (unsigned row = 0; row < 3; ++row)
                for (unsigned col = 0; col < 4; ++col)
                  skin.inverse[k][row * 4 + col] = m[col * 4 + row];
            }
            std::memcpy(g.skin_vertices.data() + i * sizeof(skin), &skin,
                        sizeof(skin));
          }
          for (unsigned i = mesh.first; i < mesh.first + mesh.count; ++i)
            g.indices.push_back(
                static_cast<std::uint16_t>(unique.at(data.indices[i])));
        });
  }
  return true;
}
} // namespace
