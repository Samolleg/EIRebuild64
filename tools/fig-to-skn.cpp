// Offline FIG/BON -> SKN, with the renderer's own CPU ANM evaluator.
#include "owned_asset_loader.h"
#include "owned_visual_evaluation.h"
#include "skn_asset.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
using namespace owned;
namespace {
using Bytes = std::vector<std::uint8_t>;
void u32(Bytes &b, std::uint32_t x) {
  for (unsigned i = 0; i < 4; ++i)
    b.push_back(std::uint8_t(x >> (i * 8)));
}
void u16(Bytes &b, std::uint16_t x) {
  b.push_back(std::uint8_t(x));
  b.push_back(std::uint8_t(x >> 8));
}
void f32(Bytes &b, float f) {
  std::uint32_t x;
  std::memcpy(&x, &f, 4);
  u32(b, x);
}
void set32(Bytes &b, unsigned at, unsigned x) {
  for (unsigned i = 0; i < 4; ++i)
    b[at + i] = std::uint8_t(x >> (8 * i));
}
unsigned string(Bytes &b, const std::string &s) {
  unsigned at = unsigned(b.size());
  b.insert(b.end(), s.begin(), s.end());
  b.push_back(0);
  return at;
}
std::uint16_t half(float f) {
  std::uint32_t b;
  std::memcpy(&b, &f, 4);
  unsigned sign = (b >> 16) & 0x8000, exp = (b >> 23) & 255, m = b & 0x7fffff;
  if (exp == 255 || exp > 142)
    throw std::runtime_error("UV outside finite half range");
  int e = int(exp) - 127 + 15;
  if (e <= 0) {
    if (e < -10)
      return std::uint16_t(sign);
    m = (m | 0x800000) >> (1 - e);
    return std::uint16_t(sign + ((m + 0xfff + ((m >> 13) & 1)) >> 13));
  }
  return std::uint16_t(sign + (unsigned(e) << 10) +
                       ((m + 0xfff + ((m >> 13) & 1)) >> 13));
}
float blend(const float *p, unsigned n, const std::array<float, 3> &shape,
            unsigned stride) {
  double v = 0;
  for (unsigned i = 0; i < n; ++i) {
    double w = 1;
    if (n == 8)
      for (unsigned k = 0; k < 3; ++k)
        w *= i & (1 << k) ? shape[k] : 1 - shape[k];
    v += p[i * stride] * w;
  }
  return float(v);
}
skn::Matrix column(const std::array<float, 16> &m) {
  skn::Matrix r;
  for (unsigned i = 0; i < 4; ++i)
    for (unsigned j = 0; j < 4; ++j)
      r[i * 4 + j] = m[j * 4 + i];
  return r;
}
ResourceBlob resolve(ResourceStore &store, const std::string &name) {
  ResourceBlob b;
  ResourceFailure f;
  if (!store.resolve(name, b, f))
    throw std::runtime_error(name + ": " + f.message);
  return b;
}
struct SourceVertex {
  unsigned node, position;
  std::array<float, 3> local;
};
void convert(ResourceStore &store, const std::string &name,
             const std::filesystem::path &out, const std::filesystem::path &dds_dir) {
  ResourceFailure failure;
  ModelCpuAsset model;
  if (!decode_model_resources(resolve(store, name + ".mod"),
                              resolve(store, name + ".bon"), name, model,
                              failure))
    throw std::runtime_error(failure.message);
  const std::array<float, 3> shape = name == "unhufe"
                                         ? std::array<float, 3>{.2f, .3f, .85f}
                                         : std::array<float, 3>{.5f, .5f, .2f};
  std::array<Bytes, 13> sections;
  sections[1].push_back(0);
  std::vector<skn::Matrix> bind;
  std::vector<unsigned> remap(model.nodes.size(), UINT_MAX);
  unsigned joint_count = 0;
  std::vector<SourceVertex> source;
  for (unsigned i = 0; i < model.nodes.size(); ++i) {
    const auto &node = model.nodes[i];
    if (!node.transform_available)
      continue;
    remap[i] = joint_count++;
    const auto p = model.links[i].parent_index;
    auto g = skn::identity();
    std::array<float, 3> local{};
    for (unsigned k = 0; k < 3; ++k)
      local[k] = blend(node.bone_positions.data() + k,
                       unsigned(node.bone_positions.size() / 3), shape, 3);
    for (unsigned k = 0; k < 3; ++k)
      g[12 + k] = local[k];
    if (p != UINT_MAX) {
      if (p >= i || remap[p] == UINT_MAX)
        throw std::runtime_error("unsupported BON hierarchy");
      g = skn::multiply(bind[remap[p]], g);
    }
    bind.push_back(g);
    u32(sections[2], string(sections[1], node.name));
    u32(sections[2], p == UINT_MAX ? UINT_MAX : remap[p]);
    for (float f : local)
      f32(sections[2], f);
    for (float f : {0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f})
      f32(sections[2], f);
    auto inverse = skn::identity();
    for (unsigned k = 0; k < 3; ++k)
      inverse[12 + k] = -g[12 + k];
    for (float f : inverse)
      f32(sections[2], f);
  }
  unsigned vertex_count = 0, index_count = 0, submesh_count = 0;
  std::array<float, 3> lo{FLT_MAX, FLT_MAX, FLT_MAX},
      hi{-FLT_MAX, -FLT_MAX, -FLT_MAX};
  // Convert every available FIG part; LNK includes alternate equipment. Runtime
  // semantic visibility must select the corresponding exported submeshes.
  for (unsigned i = 0; i < model.nodes.size(); ++i) {
    if (remap[i] == UINT_MAX)
      continue;
    const auto &fig = model.nodes[i].figure;
    if (fig.indices.empty())
      continue;
    unsigned base = vertex_count;
    for (const auto &c : fig.components) {
      const unsigned group = c.position / 4, lane = c.position % 4;
      std::array<float, 3> local{};
      for (unsigned k = 0; k < 3; ++k) {
        const float *xyz = k == 0   ? fig.positions[group].x
                           : k == 1 ? fig.positions[group].y
                                    : fig.positions[group].z;
        local[k] =
            blend(xyz + lane, fig.shape_count, shape, fig.position_groups * 16);
      }
      const auto position = skn::point(bind[remap[i]], local.data());
      for (unsigned k = 0; k < 3; ++k) {
        f32(sections[3], position[k]);
        lo[k] = (std::min)(lo[k], position[k]);
        hi[k] = (std::max)(hi[k], position[k]);
      }
      const auto &ng = fig.normals[c.normal / 4];
      unsigned nl = c.normal % 4;
      std::array<float, 3> normal{ng.x[nl], ng.y[nl], ng.z[nl]};
      float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
                               normal[2] * normal[2]);
      if (!(length > 0))
        length = 1;
      for (float x : normal)
        u16(sections[3],
            std::uint16_t(std::int16_t(std::lround(x / length * 32767))));
      u16(sections[3], 0); // deterministic perpendicular tangent; converter has
                           // no normal map
      std::array<float, 3> t =
          std::abs(normal[2]) < .9f * length
              ? std::array<float, 3>{-normal[1], normal[0], 0}
              : std::array<float, 3>{0, -normal[2], normal[1]};
      float tl = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
      if (!(tl > 0))
        tl = 1;
      for (float x : t)
        u16(sections[3],
            std::uint16_t(std::int16_t(std::lround(x / tl * 32767))));
      u16(sections[3], 32767);
      const auto &uv = fig.texture_coordinates[c.texture];
      u16(sections[3], half(uv.u));
      u16(sections[3], half(uv.v));
      u16(sections[3], std::uint16_t(remap[i]));
      for (int k = 0; k < 3; ++k)
        u16(sections[3], 0);
      u16(sections[3], 65535);
      for (int k = 0; k < 3; ++k)
        u16(sections[3], 0);
      source.push_back({i, c.position, local});
      ++vertex_count;
    }
    u32(sections[5], index_count);
    u32(sections[5], unsigned(fig.indices.size()));
    u32(sections[5], 0);
    u16(sections[5], 0);
    u16(sections[5], 0);
    ++submesh_count;
    u32(sections[12], remap[i]);
    for (auto index : fig.indices) {
      u32(sections[4], base + index);
      ++index_count;
    }
  }
  for (unsigned x : {0u, vertex_count, 0u, index_count, 0u, submesh_count})
    u32(sections[6], x);
  f32(sections[6], 0);
  f32(sections[6], .1f);
  for (float x : lo)
    f32(sections[6], x);
  for (float x : hi)
    f32(sections[6], x);
  for (float x : {1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, .5f})
    f32(sections[7], x);
  u32(sections[7],0);u32(sections[7],0); // opaque, one sided
  for(const auto role:{"color","normal","orm","emission"}){
    const auto filename=name+"_"+role+".dds";
    if(dds_dir.empty()||std::string(role)=="emission")u32(sections[7],0);
    else{
      if(!std::filesystem::is_regular_file(dds_dir/filename))throw std::runtime_error("missing external DDS: "+filename);
      u32(sections[7],string(sections[1],"dds/"+filename));
    }
  }
  u32(sections[7],0);u32(sections[7],0);
  const unsigned strides[] = {0, 1, 112, 48, 4, 16, 56, 76, 0, 0, 0, 0, 4};
  Bytes bytes(32 + 8 * 24, 0);
  set32(bytes, 0, 0x004e4b53);
  set32(bytes, 4, 1);
  set32(bytes, 12, 32);
  set32(bytes, 16, 8);
  set32(bytes, 20, 32);
  for (unsigned slot = 0; slot < 8; ++slot) {
    unsigned k = slot < 7 ? slot + 1 : 12;
    while (bytes.size() % 16)
      bytes.push_back(0);
    unsigned at = 32 + slot * 24;
    set32(bytes, at, k);
    set32(bytes, at + 4, unsigned(bytes.size()));
    set32(bytes, at + 8, unsigned(sections[k].size()));
    set32(bytes, at + 12, unsigned(sections[k].size() / strides[k]));
    set32(bytes, at + 16, strides[k]);
    set32(bytes, at + 20, 1);
    bytes.insert(bytes.end(), sections[k].begin(), sections[k].end());
  }
  set32(bytes, 8, unsigned(bytes.size()));
  skn::Asset asset;
  std::string error;
  if (!skn::decode(bytes, asset, error))
    throw std::runtime_error(error);
  auto database = std::make_shared<AnimationDatabaseAsset>();
  if (!parse_animation_database(resolve(store, name + ".adb"), *database,
                                failure))
    throw std::runtime_error(failure.message);
  MemoryResourceArchive animations;
  if (!animations.open(resolve(store, name + ".anm"), failure))
    throw std::runtime_error(failure.message);
  double maximum = 0;
  std::uint64_t compared = 0;
  for (const auto &clip_name :
       std::vector<std::string>{"uattack01", "cidle01"}) {
    ResourceBlob blob;
    if (!animations.resolve(clip_name, blob, failure))
      throw std::runtime_error(failure.message);
    auto clip = std::make_shared<AnimationClipAsset>();
    if (!decode_animation_clip(blob, *clip, failure))
      throw std::runtime_error(failure.message);
    unsigned ordinal = UINT_MAX;
    for (unsigned i = 0; i < database->entries.size(); ++i)
      if (database->entries[i].name == clip_name) {
        ordinal = i;
        break;
      }
    if (ordinal == UINT_MAX)
      throw std::runtime_error("ADB entry missing");
    VisualClipSelection selection{database, clip, ordinal, false};
    const auto &entry = database->entries[ordinal];
    for (double phase : std::vector<double>{
             0., .5, double(entry.length - 1) * .5, double(entry.length - 1),
             clip_name == "uattack01" ? (name == "unhufe" ? 45. : 22.) : 1.}) {
      std::vector<VisualNodeSample> pose;
      if (!sample_visual_skeleton(model, selection, shape, {}, phase, pose,
                                  true))
        throw std::runtime_error("CPU pose evaluator rejected sample");
      std::vector<skn::Matrix> matrices(pose.size());
      VisualPoseState world;
      world.root = {1.25f, -2.5f, .75f};
      world.facing = .37f;
      for (unsigned i = 0; i < pose.size(); ++i) {
        std::array<float, 16> row;
        if (!visual_node_world_matrix(world, pose[i], {1, 1, 1}, row))
          throw std::runtime_error("CPU matrix evaluator rejected sample");
        matrices[i] = column(row);
      }
      for (unsigned v = 0; v < source.size(); ++v) {
        const auto &s = source[v];
        if (!pose[s.node].morphs.empty())
          throw std::runtime_error(
              "ANM vertex morph requires offline correspondence, outside "
              "skin-only reference");
        const auto old = skn::point(matrices[s.node], s.local.data());
        const auto skin = skn::multiply(matrices[s.node],
                                        asset.joints[remap[s.node]].inverse);
        const auto actual = skn::point(skin, asset.vertices[v].position);
        double distance = 0;
        for (unsigned k = 0; k < 3; ++k)
          distance += double(actual[k] - old[k]) * (actual[k] - old[k]);
        maximum = (std::max)(maximum, std::sqrt(distance));
        ++compared;
      }
    }
  }
  if (maximum > 1e-4)
    throw std::runtime_error("SKN reference exceeds 1e-4 m");
  std::filesystem::create_directories(out);
  std::ofstream file(out / (name + ".skn"), std::ios::binary);
  file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  if (!file)
    throw std::runtime_error("SKN output write failed");
  std::cout << name << " vertices=" << vertex_count << " joints=" << joint_count
            << " comparisons=" << compared << " max_error_m=" << maximum
            << "\n";
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  try {
    if (argc < 3) {
      std::cerr << "fig-to-skn OUTPUT_DIR [--dds-directory DDS_DIR] ARCHIVE.res [ARCHIVE.res ...]\n";
      return 2;
    }
    const bool export_mmp=std::wstring(argv[1])==L"--export-mmp";
    const bool materials=!export_mmp&&argc>4&&std::wstring(argv[2])==L"--dds-directory";
    if(export_mmp&&argc<5)throw std::runtime_error("--export-mmp TEXTURE.mmp OUTPUT.rgb ARCHIVE.res [...]");
    ResourceStoreConfig config;
    for (int i = export_mmp?4:materials?4:2; i < argc; ++i)
      config.archives.push_back(argv[i]);
    ResourceStore store;
    ResourceFailure failure;
    if (!store.open(config, failure))
      throw std::runtime_error(failure.message);
    if(export_mmp){
      const auto name=std::filesystem::path(argv[2]).u8string();
      TextureCpuAsset texture;
      if(!parse_texture_asset(resolve(store,name),texture,failure))throw std::runtime_error(failure.message);
      std::uint32_t width=0,height=0;Bytes rgb;
      if(!texture_rgb_pixels(texture,0,width,height,rgb))throw std::runtime_error("MMP RGB decode failed");
      std::ofstream output(std::filesystem::path(argv[3]),std::ios::binary);
      output.write(reinterpret_cast<const char*>(rgb.data()),rgb.size());
      if(!output)throw std::runtime_error("RGB output write failed");
      std::cout<<"width="<<width<<" height="<<height<<" bytes="<<rgb.size()<<"\n";return 0;
    }
    const std::filesystem::path dds_dir=materials?argv[3]:L"";
    convert(store, "unhufe", argv[1],dds_dir);
    convert(store, "unmogo", argv[1],dds_dir);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
