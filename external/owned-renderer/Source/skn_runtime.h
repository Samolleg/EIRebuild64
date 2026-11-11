#pragma once
#include "owned_asset_loader.h"
#include "owned_resource_runtime.h"
#include "skn_asset.h"
#include <map>
#include <mutex>
namespace owned::skn {
// One rejection per model across bounds, construction and sink submission.
inline void report_rejection(const std::string &model, const std::string &reason) noexcept try {
  static std::mutex mutex;
  static std::set<std::string> reported;
  std::lock_guard<std::mutex> guard(mutex);
  if (reported.insert(model).second) {
    const std::string detail = "model=\"" + model + "\" reason=\"" + reason + "\"";
    bridge_append_probe_event("skn_asset_rejected", detail.c_str());
  }
} catch (...) {
  // Diagnostics must never interrupt rendering, including allocation failure.
}
struct Dds {
  ResourceBlob blob;
  NativeOwnedTextureFormat format{};
  std::vector<NativeOwnedTextureMip> mips;
};
struct RuntimeAsset {
  bool nala_generated=false;
  std::string shape_key;
  std::array<double,7> nala_profile{};
  ResourceIdentity identity;
  Asset asset;
  std::vector<std::array<Dds, 4>> textures;
  std::vector<float> joint_radius;
};
inline Dds dds(ResourceBlob blob, unsigned map) {
  if (!blob.bytes)
    throw std::runtime_error("DDS missing bytes");
  Reader r{*blob.bytes};
  if (r.u32(0) != 0x20534444 || r.u32(4) != 124 || r.u32(76) != 32 ||
      r.u32(84) != 0x30315844 || r.u32(132) != 3 || r.u32(136) ||
      r.u32(140) != 1)
    throw std::runtime_error("SKN requires DDS DX10 2D single texture");
  unsigned dxgi = r.u32(128), w = r.u32(16), h = r.u32(12), levels = r.u32(28);
  if (!w || !h || w > 16384 || h > 16384)
    throw std::runtime_error("SKN DDS dimensions");
  if (w % 4 || h % 4)
    throw std::runtime_error("SKN DDS BC top-level dimensions " + std::to_string(w) + "x" + std::to_string(h) + ": width and height must be multiples of four (minimum 4x4)");
  unsigned expected = map == 1 ? 83 : map == 2 ? 98 : 99;
  if (dxgi != expected)
    throw std::runtime_error("SKN DDS color space/BC format");
  Dds d;
  d.blob = std::move(blob);
  d.format = map == 1   ? NativeOwnedTextureFormat::bc5
             : map == 2 ? NativeOwnedTextureFormat::bc7
                        : NativeOwnedTextureFormat::bc7_srgb;
  unsigned full = 1;
  for (unsigned m = (std::max)(w, h); m > 1; m >>= 1)
    ++full;
  if (levels != full)
    throw std::runtime_error("SKN DDS incomplete mip chain");
  std::uint64_t at = 148;
  for (unsigned i = 0; i < levels; ++i) {
    unsigned n = ((w + 3) / 4) * ((h + 3) / 4) * 16;
    r.range(at, n);
    d.mips.push_back({d.blob.bytes->data() + at, w, h, n});
    at += n;
    w = (std::max)(1u, w / 2);
    h = (std::max)(1u, h / 2);
  }
  if (at != d.blob.bytes->size())
    throw std::runtime_error("SKN DDS trailing bytes");
  return d;
}
// Asset lookup happens only on an immutable assembly change. A selected bad
// SKN is an error, never permission to draw the FIG visual instead.
inline std::shared_ptr<const RuntimeAsset> select(const std::string &name,
                                                  bool &present) {
  static std::mutex mutex;
  static ResourceStore store;
  static std::uint64_t generation = 0;
  static std::map<std::string, std::weak_ptr<const RuntimeAsset>> cache;
  std::lock_guard<std::mutex> guard(mutex);
  present = false;
  ResourceStoreConfig config;
  std::uint64_t current = 0;
  ResourceFailure failure;
  // Memory-only/preview scenes have no optional resource namespace. Keep
  // their established FIG path; only a selected SKN may reject a visual.
  if (!owned_resource_runtime_mounts(config, &current))
    return {};
  if (current != generation) {
    store.close();
    cache.clear();
    if (!store.open(config, failure))
      throw std::runtime_error(failure.message);
    generation = current;
  }
  if (auto a = cache[name].lock()) {
    present = true;
    return a;
  }
  ResourceBlob blob;
  if (!store.resolve(name + ".skn", blob, failure)) {
    if (failure.code == ResourceFailureCode::not_found)
      return {};
    throw std::runtime_error(failure.message);
  }
  present = true;
  auto a = std::make_shared<RuntimeAsset>();
  a->identity = blob.identity;
  std::string error;
  if (!blob.bytes || !decode(*blob.bytes, a->asset, error))
    throw std::runtime_error(error);
  a->nala_generated = bool(a->asset.nala_rig);
  if(a->nala_generated)a->asset.semantic_joints.assign(a->asset.submeshes.size(),0);
  // In joint-local bind space every influenced vertex lies in this sphere.
  // Positive normalized skin weights make the posed mesh a convex combination
  // of the posed joint spheres, including rotation and non-uniform scale.
  a->joint_radius.assign(a->asset.joints.size(),-1);
  for(const auto& v:a->asset.vertices)for(unsigned k=0;k<4;++k)if(v.weights[k]){
    const auto p=point(a->asset.joints[v.joints[k]].inverse,v.position);
    const float r=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
    a->joint_radius[v.joints[k]]=(std::max)(a->joint_radius[v.joints[k]],std::nextafter(r,INFINITY));
  }
  a->textures.resize(a->asset.materials.size());
  for (unsigned i = 0; i < a->textures.size(); ++i)
    for (unsigned k = 0; k < 4; ++k) {
      const auto &map = a->asset.materials[i].maps[k];
      if (map.empty())
        continue;
      ResourceBlob texture;
      if (!store.resolve(map, texture, failure))
        throw std::runtime_error(failure.message);
      a->textures[i][k] = dds(std::move(texture), k);
    }
  cache[name] = a;
  return a;
}
inline float half_float(std::uint16_t h) {
  float v;
  unsigned sign = h >> 15, e = (h >> 10) & 31, m = h & 1023;
  v = e ? std::ldexp(float(1024 + m), int(e) - 25) : std::ldexp(float(m), -24);
  return sign ? -v : v;
}
struct GpuVertex {
  std::uint32_t joints[4];
  float weights[4];
  float inverse[4][12];
  float tangent[4];
};
static_assert(sizeof(GpuVertex) == 240);
// Section-9 sources stay independent until after GPU skinning. Never merge
// influences from different bind positions into one four-weight vertex.
struct ContactSource {
  std::uint32_t joints[4];
  float weights[4];
  float inverse[4][12];
  float position[4], normal[4];
};
struct SkinVertex {
  GpuVertex skin;
  ContactSource source[3];
  float blend[4]; // xyz = contact weights, w = contact enabled
};
static_assert(sizeof(ContactSource)==256 && sizeof(SkinVertex)==1024);
} // namespace owned::skn
