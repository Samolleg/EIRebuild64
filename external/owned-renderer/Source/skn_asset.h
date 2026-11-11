#pragma once
// SKN v1 disk decoder. No renderer/device state and no asset repair.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#include "nala_pose_asset.h"

namespace owned::skn {
using Matrix = std::array<float, 16>;
struct Joint {
  std::string name;
  std::int32_t parent;
  std::array<float, 3> translation, scale;
  std::array<float, 4> rotation;
  Matrix inverse;
};
struct Vertex {
  float position[3];
  std::int16_t normal[4], tangent[4];
  std::uint16_t uv[2], joints[4], weights[4];
};
static_assert(sizeof(Vertex) == 48);
struct Material {
  float base[4], emission[3], roughness, metallic, ao, cutoff;
  std::uint32_t alpha, two_sided;
  std::array<std::string, 4> maps;
};
struct Submesh {
  std::uint32_t first, count, material;
  std::uint16_t zone, layer;
};
struct Lod {
  std::uint32_t first_vertex, vertex_count, first_index, index_count,
      first_submesh, submesh_count;
  float error, hysteresis;
  std::array<float, 3> minimum, maximum;
};
struct Asset {
  struct Contact {
    std::uint32_t target, source[3];
    float weight[3];
  };
  std::shared_ptr<const UnityBody::Data> nala_rig;
  std::vector<Contact> nala_contacts;
  std::vector<Joint> joints;
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<Submesh> submeshes;
  std::vector<Lod> lods;
  std::vector<Material> materials;
  std::vector<std::uint32_t> semantic_joints;
};
inline Matrix identity() {
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}
inline Matrix multiply(const Matrix &a, const Matrix &b) {
  Matrix c{};
  for (int j = 0; j < 4; ++j)
    for (int i = 0; i < 4; ++i)
      for (int k = 0; k < 4; ++k)
        c[j * 4 + i] += a[k * 4 + i] * b[j * 4 + k];
  return c;
}
inline std::array<float, 3> point(const Matrix &m, const float *p) {
  return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
          m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
          m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]};
}
inline Matrix trs(const Joint &j) {
  const auto &q = j.rotation;
  float x = q[0], y = q[1], z = q[2], w = q[3];
  Matrix m = identity();
  m[0] = (1 - 2 * (y * y + z * z)) * j.scale[0];
  m[1] = 2 * (x * y + z * w) * j.scale[0];
  m[2] = 2 * (x * z - y * w) * j.scale[0];
  m[4] = 2 * (x * y - z * w) * j.scale[1];
  m[5] = (1 - 2 * (x * x + z * z)) * j.scale[1];
  m[6] = 2 * (y * z + x * w) * j.scale[1];
  m[8] = 2 * (x * z + y * w) * j.scale[2];
  m[9] = 2 * (y * z - x * w) * j.scale[2];
  m[10] = (1 - 2 * (x * x + y * y)) * j.scale[2];
  std::copy(j.translation.begin(), j.translation.end(), m.begin() + 12);
  return m;
}
struct Reader {
  const std::vector<std::uint8_t> &b;
  void range(std::uint64_t at, std::uint64_t n) const {
    if (at > b.size() || n > b.size() - at)
      throw std::runtime_error("SKN range outside file");
  }
  std::uint32_t u32(std::uint64_t at) const {
    range(at, 4);
    return b[at] | std::uint32_t(b[at + 1]) << 8 |
           std::uint32_t(b[at + 2]) << 16 | std::uint32_t(b[at + 3]) << 24;
  }
  std::uint16_t u16(std::uint64_t at) const {
    range(at, 2);
    return std::uint16_t(b[at] | std::uint16_t(b[at + 1]) << 8);
  }
  float f32(std::uint64_t at) const {
    auto u = u32(at);
    float f;
    std::memcpy(&f, &u, 4);
    if (!std::isfinite(f))
      throw std::runtime_error("SKN non-finite number");
    return f;
  }
};
inline bool utf8(const std::string &s) {
  std::size_t i = 0;
  while (i < s.size()) {
    unsigned c = static_cast<unsigned char>(s[i++]);
    if (c < 128) {
      if (c == 0)
        return false;
      continue;
    }
    unsigned n = c >= 0xc2 && c <= 0xdf   ? 1
                 : c >= 0xe0 && c <= 0xef ? 2
                 : c >= 0xf0 && c <= 0xf4 ? 3
                                          : 99;
    if (n == 99 || n > s.size() - i)
      return false;
    unsigned cp = c & ((1u << (6 - n)) - 1);
    for (unsigned k = 0; k < n; ++k) {
      unsigned d = static_cast<unsigned char>(s[i++]);
      if ((d & 192) != 128)
        return false;
      cp = (cp << 6) | (d & 63);
    }
    if (cp < (n == 1   ? 128u
              : n == 2 ? 2048u
                       : 65536u) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
      return false;
  }
  return true;
}
inline bool relative_dds(const std::string &s) {
  if (s.empty())
    return true;
  if (s[0] == '/' || s[0] == '\\' || s.find(':') != s.npos ||
      s.find("..") != s.npos || s.find('\\') != s.npos || s.size() < 5 ||
      s.substr(s.size() - 4) != ".dds")
    return false;
  return true;
}
inline bool decode(const std::vector<std::uint8_t> &bytes, Asset &output,
                   std::string &error) noexcept {
  output = {};
  try {
    Reader r{bytes};
    if (r.u32(0) != 0x004e4b53 || r.u32(4) != 1 || r.u32(8) != bytes.size() ||
        r.u32(12) != 32 || r.u32(24) || r.u32(28))
      throw std::runtime_error("SKN header/version");
    struct Section {
      std::uint32_t at = 0, bytes = 0, count = 0, stride = 0;
    };
    std::array<Section, 14> sections{};
    auto count = r.u32(16), table = r.u32(20);
    if (count > 13 || table != 32)
      throw std::runtime_error("SKN section directory");
    r.range(table, std::uint64_t(count) * 24);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> spans{
        {0, 32 + std::uint64_t(count) * 24}};
    for (unsigned i = 0; i < count; ++i) {
      auto p = table + i * 24;
      auto type = r.u32(p);
      Section s{r.u32(p + 4), r.u32(p + 8), r.u32(p + 12), r.u32(p + 16)};
      if (!type || type >= sections.size() || sections[type].at ||
          r.u32(p + 20) != 1 || s.at % 16 || s.at < spans[0].second ||
          std::uint64_t(s.count) * s.stride != s.bytes)
        throw std::runtime_error("SKN section version/layout");
      r.range(s.at, s.bytes);
      for (auto span : spans)
        if (s.at < span.second && span.first < std::uint64_t(s.at) + s.bytes)
          throw std::runtime_error("SKN overlapping sections");
      spans.push_back({s.at, std::uint64_t(s.at) + s.bytes});
      sections[type] = s;
    }
    const unsigned strides[] = {0, 1, 112, 48, 4, 16, 56, 76};
    for (unsigned i = 1; i <= 7; ++i)
      if (!sections[i].at || sections[i].stride != strides[i])
        throw std::runtime_error("SKN required section/stride");
    for (unsigned i = 8; i < sections.size(); ++i)
      if (i != 9 && i != 12 && i != 13 && sections[i].count)
        throw std::runtime_error(
            "SKN feature not implemented by this renderer");
    auto text = [&](unsigned at) {
      const auto &s = sections[1];
      if (at >= s.bytes)
        throw std::runtime_error("SKN string offset");
      auto end = at;
      while (end < s.bytes && bytes[s.at + end])
        ++end;
      if (end == s.bytes)
        throw std::runtime_error("SKN unterminated UTF-8");
      std::string v(reinterpret_cast<const char *>(bytes.data() + s.at + at),
                    end - at);
      if (!utf8(v))
        throw std::runtime_error("SKN invalid UTF-8");
      return v;
    };
    const auto &js = sections[2];
    if (!js.count || js.count > 65535)
      throw std::runtime_error("SKN joint count");
    std::set<std::string> names;
    std::vector<Matrix> global;
    for (unsigned i = 0; i < js.count; ++i) {
      unsigned at = js.at + i * 112;
      Joint j{};
      j.name = text(r.u32(at));
      j.parent = static_cast<std::int32_t>(r.u32(at + 4));
      if (j.name.empty() || !names.insert(j.name).second || j.parent < -1 ||
          j.parent >= static_cast<int>(i))
        throw std::runtime_error("SKN joint hierarchy/name");
      for (int k = 0; k < 3; ++k)
        j.translation[k] = r.f32(at + 8 + k * 4);
      for (int k = 0; k < 4; ++k)
        j.rotation[k] = r.f32(at + 20 + k * 4);
      float norm = 0;
      for (float x : j.rotation)
        norm += x * x;
      if (std::abs(norm - 1) > 1e-4f)
        throw std::runtime_error("SKN non-unit bind quaternion");
      for (int k = 0; k < 3; ++k) {
        j.scale[k] = r.f32(at + 36 + k * 4);
        if (j.scale[k] <= 0)
          throw std::runtime_error("SKN invalid bind scale");
      }
      for (int k = 0; k < 16; ++k)
        j.inverse[k] = r.f32(at + 48 + k * 4);
      auto g = trs(j);
      if (j.parent >= 0)
        g = multiply(global[j.parent], g);
      auto check = multiply(g, j.inverse), id = identity();
      for (int k = 0; k < 16; ++k)
        if (!std::isfinite(g[k]) || !std::isfinite(check[k]) ||
            std::abs(check[k] - id[k]) > 1e-4f)
          throw std::runtime_error("SKN inconsistent inverse bind");
      global.push_back(g);
      output.joints.push_back(std::move(j));
    }
    const auto &vs = sections[3];
    output.vertices.resize(vs.count);
    for (unsigned i = 0; i < vs.count; ++i) {
      unsigned at = vs.at + i * 48;
      auto &v = output.vertices[i];
      for (int k = 0; k < 3; ++k)
        v.position[k] = r.f32(at + k * 4);
      unsigned sum = 0;
      for (int k = 0; k < 4; ++k) {
        v.normal[k] = static_cast<std::int16_t>(r.u16(at + 12 + k * 2));
        v.tangent[k] = static_cast<std::int16_t>(r.u16(at + 20 + k * 2));
        v.joints[k] = r.u16(at + 32 + k * 2);
        v.weights[k] = r.u16(at + 40 + k * 2);
        sum += v.weights[k];
        if (v.joints[k] >= js.count)
          throw std::runtime_error("SKN joint index");
      }
      for (int k = 0; k < 2; ++k) {
        v.uv[k] = r.u16(at + 28 + k * 2);
        if ((v.uv[k] & 0x7c00) == 0x7c00)
          throw std::runtime_error("SKN non-finite UV");
      }
      if (sum != 65535)
        throw std::runtime_error("SKN weight sum");
    }
    const auto &is = sections[4];
    if (is.count % 3)
      throw std::runtime_error("SKN triangle count");
    for (unsigned i = 0; i < is.count; ++i) {
      auto index = r.u32(is.at + i * 4);
      if (index >= vs.count)
        throw std::runtime_error("SKN vertex index");
      output.indices.push_back(index);
    }
    const auto &ms = sections[7];
    if (!ms.count)
      throw std::runtime_error("SKN material count");
    for (unsigned i = 0; i < ms.count; ++i) {
      unsigned at = ms.at + i * 76;
      Material m{};
      for (int k = 0; k < 4; ++k)
        m.base[k] = r.f32(at + k * 4);
      for (int k = 0; k < 3; ++k)
        m.emission[k] = r.f32(at + 16 + k * 4);
      m.roughness = r.f32(at + 28);
      m.metallic = r.f32(at + 32);
      m.ao = r.f32(at + 36);
      m.cutoff = r.f32(at + 40);
      m.alpha = r.u32(at + 44);
      m.two_sided = r.u32(at + 48);
      for (int k = 0; k < 4; ++k) {
        m.maps[k] = text(r.u32(at + 52 + k * 4));
        if (!relative_dds(m.maps[k]))
          throw std::runtime_error("SKN DDS asset ID");
      }
      if (r.u32(at + 68) || r.u32(at + 72) || m.alpha > 2 || m.two_sided > 1)
        throw std::runtime_error("SKN material flags");
      for (float x : m.base)
        if (x < 0 || x > 1)
          throw std::runtime_error("SKN base factor");
      for (float x : m.emission)
        if (x < 0)
          throw std::runtime_error("SKN emission factor");
      for (float x : {m.roughness, m.metallic, m.ao, m.cutoff})
        if (x < 0 || x > 1)
          throw std::runtime_error("SKN material factor");
      output.materials.push_back(m);
    }
    const auto &ss = sections[5];
    for (unsigned i = 0; i < ss.count; ++i) {
      unsigned at = ss.at + i * 16;
      Submesh s{r.u32(at), r.u32(at + 4), r.u32(at + 8), r.u16(at + 12),
                r.u16(at + 14)};
      if (s.first % 3 || s.count % 3 || s.first > is.count ||
          s.count > is.count - s.first || s.material >= ms.count)
        throw std::runtime_error("SKN submesh range");
      output.submeshes.push_back(s);
    }
    const auto &ls = sections[6];
    if (!ls.count || ls.count > 4)
      throw std::runtime_error("SKN LOD count");
    for (unsigned i = 0; i < ls.count; ++i) {
      unsigned at = ls.at + i * 56;
      Lod l{};
      l.first_vertex = r.u32(at);
      l.vertex_count = r.u32(at + 4);
      l.first_index = r.u32(at + 8);
      l.index_count = r.u32(at + 12);
      l.first_submesh = r.u32(at + 16);
      l.submesh_count = r.u32(at + 20);
      l.error = r.f32(at + 24);
      l.hysteresis = r.f32(at + 28);
      for (int k = 0; k < 3; ++k) {
        l.minimum[k] = r.f32(at + 32 + k * 4);
        l.maximum[k] = r.f32(at + 44 + k * 4);
        if (l.minimum[k] > l.maximum[k])
          throw std::runtime_error("SKN bounds");
      }
      auto fit = [](unsigned first, unsigned n, unsigned total) {
        return first <= total && n <= total - first;
      };
      if (!l.vertex_count || !l.index_count || !l.submesh_count ||
          !fit(l.first_vertex, l.vertex_count, vs.count) ||
          !fit(l.first_index, l.index_count, is.count) ||
          !fit(l.first_submesh, l.submesh_count, ss.count) || l.error < 0 ||
          l.hysteresis < 0 || l.hysteresis > 1 ||
          (i && l.error <= output.lods.back().error))
        throw std::runtime_error("SKN LOD ranges/thresholds");
      unsigned cursor = l.first_index;
      for (unsigned k = 0; k < l.submesh_count; ++k) {
        const auto &s = output.submeshes[l.first_submesh + k];
        if (s.first != cursor)
          throw std::runtime_error("SKN LOD submesh coverage");
        cursor += s.count;
      }
      if (cursor != l.first_index + l.index_count)
        throw std::runtime_error("SKN LOD coverage");
      for (unsigned k = l.first_index; k < cursor; ++k)
        if (output.indices[k] < l.first_vertex ||
            output.indices[k] >= l.first_vertex + l.vertex_count)
          throw std::runtime_error("SKN LOD index");
      for (unsigned k = l.first_vertex; k < l.first_vertex + l.vertex_count;
           ++k)
        for (int a = 0; a < 3; ++a)
          if (output.vertices[k].position[a] < l.minimum[a] ||
              output.vertices[k].position[a] > l.maximum[a])
            throw std::runtime_error("SKN non-conservative bind bounds");
      output.lods.push_back(l);
    }
    if (sections[12].at) {
      const auto &s = sections[12];
      if (s.stride != 4 || s.count != ss.count)
        throw std::runtime_error("SKN semantic mapping count");
      for (unsigned i = 0; i < s.count; ++i) {
        auto j = r.u32(s.at + i * 4);
        if (j >= js.count)
          throw std::runtime_error("SKN semantic mapping joint");
        output.semantic_joints.push_back(j);
      }
    }
    if (sections[9].at || sections[13].at) {
      if (!sections[9].count || !sections[13].count ||
          sections[9].stride != 1 || sections[13].stride != 1)
        throw std::runtime_error("SKN Nala requires contact and pose sections");
      const auto& pose = sections[13];
      auto rig = std::make_shared<UnityBody::Data>(nala_pose::decode(
          std::vector<std::uint8_t>(bytes.begin()+pose.at,bytes.begin()+pose.at+pose.bytes)));
      if (rig->bindInverse.size() != output.joints.size())
        throw std::runtime_error("SKN Nala skin slot count differs from section 13");
      // Section 13 owns the affine frames. Identity slot descriptors prevent
      // an ordinary section-2 reader from interpreting these as LNK joints.
      for (const auto& j : output.joints) {
        if (j.parent != -1 || j.translation != std::array<float,3>{} ||
            j.rotation != std::array<float,4>{0,0,0,1} ||
            j.scale != std::array<float,3>{1,1,1} || j.inverse != identity())
          throw std::runtime_error("SKN Nala slot descriptor must be identity");
      }
      const auto& contact = sections[9];
      std::vector<std::uint8_t> payload(bytes.begin()+contact.at,bytes.begin()+contact.at+contact.bytes);
      Reader cr{payload};
      if (cr.u32(0)!=16 || cr.u32(4)!=6 || cr.u32(8)!=16 || cr.u32(12))
        throw std::runtime_error("SKN Nala contact envelope");
      struct Table {unsigned at=0,count=0,stride=0;};
      std::array<Table,7> tables{};
      std::vector<std::pair<std::uint64_t,std::uint64_t>> ranges{{0,112}};
      const unsigned expected[]={0,32,32,48,12,8,16};
      for(unsigned i=0;i<6;++i){
        unsigned at=16+16*i,k=cr.u32(at);Table t{cr.u32(at+4),cr.u32(at+8),cr.u32(at+12)};
        if(!k || k>=tables.size() || tables[k].at || t.stride!=expected[k] || t.at%16 || t.at<112)
          throw std::runtime_error("SKN Nala contact table layout");
        std::uint64_t end=std::uint64_t(t.at)+std::uint64_t(t.count)*t.stride;
        cr.range(t.at,std::uint64_t(t.count)*t.stride);
        if(t.count){for(auto span:ranges)if(t.at<span.second && span.first<end)
          throw std::runtime_error("SKN Nala contact tables overlap");ranges.push_back({t.at,end});}
        tables[k]=t;
      }
      // Implemented subset: hidden skinned sources -> visible lid render mesh.
      // Garment stages remain unsupported and must fail instead of disappearing.
      if(tables[1].count!=2 || tables[2].count!=1 || tables[4].count || tables[5].count || tables[6].count)
        throw std::runtime_error("SKN unsupported Nala contact stage set");
      unsigned surface=tables[1].at;
      unsigned sourceFirst=cr.u32(surface+4),sourceCount=cr.u32(surface+8);
      unsigned targetFirst=cr.u32(surface+36),targetCount=cr.u32(surface+40);
      auto fits=[](unsigned first,unsigned n,std::size_t total){return first<=total && n<=total-first;};
      text(cr.u32(surface));text(cr.u32(surface+32));
      if(!sourceCount || !targetCount || !fits(sourceFirst,sourceCount,output.vertices.size()) ||
          !fits(targetFirst,targetCount,output.vertices.size()) ||
          (sourceFirst<std::uint64_t(targetFirst)+targetCount && targetFirst<std::uint64_t(sourceFirst)+sourceCount) ||
          cr.u32(surface+12) || cr.u32(surface+16) || cr.u32(surface+20) || cr.u32(surface+24)!=1 || cr.u32(surface+28) ||
          cr.u32(surface+44) || cr.u32(surface+48)!=output.indices.size() || cr.u32(surface+52) || cr.u32(surface+56) || cr.u32(surface+60) ||
          output.lods.size()!=1 || tables[3].count!=targetCount)
        throw std::runtime_error("SKN Nala contact surface ranges");
      for(unsigned index:output.indices)if(index<targetFirst || index>=targetFirst+targetCount)
        throw std::runtime_error("SKN Nala visible index outside target surface");
      unsigned stage=tables[2].at;
      const unsigned stageValues[]={0,0,1,0,targetCount,0,0,0};
      for(unsigned i=0;i<8;++i)if(cr.u32(stage+4*i)!=stageValues[i])
        throw std::runtime_error("SKN unsupported Nala contact operation");
      std::vector<bool> assigned(targetCount,false);
      for(unsigned i=0;i<targetCount;++i){
        unsigned at=tables[3].at+48*i,target=cr.u32(at);
        if(target>=targetCount || assigned[target])throw std::runtime_error("SKN Nala contact duplicate/invalid target");
        assigned[target]=true;Asset::Contact c{};c.target=targetFirst+target;double sum=0;
        for(unsigned k=0;k<3;++k){unsigned src=cr.u32(at+4+4*k);
          if(src>=sourceCount)throw std::runtime_error("SKN Nala contact source");
          c.source[k]=sourceFirst+src;c.weight[k]=cr.f32(at+16+4*k);sum+=c.weight[k];
          if(c.weight[k]<0)throw std::runtime_error("SKN Nala contact negative weight");}
        if(std::abs(sum-1)>1e-5)throw std::runtime_error("SKN Nala contact weight sum");
        for(unsigned k=0;k<5;++k)if(cr.u32(at+28+4*k))throw std::runtime_error("SKN unsupported Nala contact offset/frame");
        output.nala_contacts.push_back(c);
      }
      output.nala_rig=std::move(rig);
    }
    return true;
  } catch (const std::exception &e) {
    error = e.what();
    output = {};
    return false;
  } catch (...) {
    error = "SKN allocation failure";
    output = {};
    return false;
  }
}
} // namespace owned::skn
