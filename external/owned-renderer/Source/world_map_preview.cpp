#include "world_map_preview.h"
#include <d3d.h>

#include "owned_asset_loader.h"
#include "owned_terrain_decode.h"
#include "retained_static_submission.h"
#include "../ThirdParty/meshoptimizer/src/meshoptimizer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace owned {
namespace {
using Vec3 = std::array<double, 3>;
using Clock = std::chrono::steady_clock;
constexpr std::size_t kChunkVertices =
    65532; // complete triangles, uint16 components
constexpr std::uint64_t kIdentityDescriptionBytes = 32ull << 20;
#ifdef EID3D11_EMBEDDED
// The x64 host admits the complete mod allod; retail keeps its original budget.
constexpr std::uint64_t kAllodBytes = 512ull << 20;
#else
constexpr std::uint64_t kAllodBytes = 192ull << 20;
#endif
constexpr std::uint32_t kTextureSide = 512;
constexpr unsigned kBins = 32;

double milliseconds(Clock::time_point from) {
  return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
}
void require(bool value, const char *reason) {
  if (!value)
    throw std::runtime_error(reason);
}
Vec3 point(const NativePackedGroup *groups, std::uint32_t index) {
  const auto &g = groups[index / 4];
  const unsigned lane = index % 4;
  return {g.x[lane], g.y[lane], g.z[lane]};
}
Vec3 transform(const std::array<float, 16> &m, const Vec3 &p) {
  Vec3 result{};
  for (unsigned a = 0; a < 3; ++a)
    result[a] = m[a * 4 + 3] + m[a * 4] * p[0] + m[a * 4 + 1] * p[1] +
                m[a * 4 + 2] * p[2];
  return result;
}
void normalize(Vec3 &p) {
  const double length = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
  if (length > 0)
    for (auto &v : p)
      v /= length;
  else
    p = {0, 0, 1};
}
struct Vertex {
  Vec3 p{}, n{};
  double u = 0, v = 0, opacity = 1;
  // Local to the carrier triangle currently clipping this polygon.
  unsigned carrier_edges = 0;
};
Vertex mix(const Vertex &a, const Vertex &b, double t) {
  Vertex c;
  for (unsigned i = 0; i < 3; ++i) {
    c.p[i] = a.p[i] + (b.p[i] - a.p[i]) * t;
    c.n[i] = a.n[i] + (b.n[i] - a.n[i]) * t;
  }
  c.u = a.u + (b.u - a.u) * t;
  c.v = a.v + (b.v - a.v) * t;
  c.opacity = a.opacity + (b.opacity - a.opacity) * t;
  c.carrier_edges = a.carrier_edges & b.carrier_edges;
  return c;
}
double side(const Vec3 &a, const Vec3 &b, const Vec3 &p) {
  return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
}
struct ClipTriangle {
  std::array<Vec3, 3> p;
  double x0, y0, x1, y1;
  double slope_x = 0, slope_y = 0;
  unsigned outer_edges = 0;
  std::array<std::vector<std::array<double, 2>>, 3> outer_intervals;
  double height(const Vec3 &v) const {
    return p[0][2] + slope_x * (v[0] - p[0][0]) + slope_y * (v[1] - p[0][1]);
  }
};
std::optional<std::array<double, 2>>
outward_coverage(const Vec3 &a, const Vec3 &b, const ClipTriangle &triangle) {
  std::array<double, 2> interval{0, 1};
  for (unsigned edge = 0; edge < 3; ++edge) {
    const auto &p = triangle.p[edge], &q = triangle.p[(edge + 1) % 3];
    const double first = side(p, q, a), last = side(p, q, b);
    if (first == 0 && last == 0) {
      // An edge on the same line covers this boundary only if its interior
      // extends outward. This directional derivative needs no offset sample.
      const double outward =
          -((q[0] - p[0]) * (b[0] - a[0]) + (q[1] - p[1]) * (b[1] - a[1]));
      if (outward <= 0)
        return std::nullopt;
    } else if (first < 0 && last < 0) {
      return std::nullopt;
    } else if (first < 0) {
      interval[0] = (std::max)(interval[0], first / (first - last));
    } else if (last < 0) {
      interval[1] = (std::min)(interval[1], first / (first - last));
    }
    if (interval[0] >= interval[1])
      return std::nullopt;
  }
  return interval;
}
struct Footprint {
  ResourceIdentity resource;
  FigureCpuAsset figure;
  Vec3 minimum{}, maximum{};
  std::vector<ClipTriangle> triangles;
  std::vector<std::array<Vec3, 2>> boundary;
  std::array<std::vector<std::uint32_t>, kBins * kBins> bins;
  unsigned bin(double p, unsigned axis) const {
    const double t = (p - minimum[axis]) / (maximum[axis] - minimum[axis]);
    return unsigned(std::clamp(t * double(kBins), 0.0, double(kBins - 1)));
  }
  bool contains(const Vec3 &p) const {
    if (p[0] < minimum[0] || p[0] > maximum[0] || p[1] < minimum[1] ||
        p[1] > maximum[1])
      return false;
    for (const auto i : bins[bin(p[1], 1) * kBins + bin(p[0], 0)]) {
      const auto &t = triangles[i];
      if (side(t.p[0], t.p[1], p) >= 0 && side(t.p[1], t.p[2], p) >= 0 &&
          side(t.p[2], t.p[0], p) >= 0)
        return true;
    }
    return false;
  }
  double height(const Vec3 &p) const {
    // Match the existing projected-union owner at this point. Outside the
    // crop, extend the nearest triangle's plane for intersecting assemblies.
    for (const auto i : bins[bin(p[1], 1) * kBins + bin(p[0], 0)]) {
      const auto &t = triangles[i];
      if (side(t.p[0], t.p[1], p) >= 0 && side(t.p[1], t.p[2], p) >= 0 &&
          side(t.p[2], t.p[0], p) >= 0)
        return t.height(p);
    }
    const ClipTriangle *nearest = nullptr;
    double distance = (std::numeric_limits<double>::max)();
    for (const auto &t : triangles)
      for (unsigned edge = 0; edge < 3; ++edge) {
        const auto &a = t.p[edge];
        const auto &b = t.p[(edge + 1) % 3];
        const double dx = b[0] - a[0], dy = b[1] - a[1];
        const double along = std::clamp(
            ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy),
            0.0, 1.0);
        const double x = p[0] - a[0] - along * dx;
        const double y = p[1] - a[1] - along * dy;
        const double squared = x * x + y * y;
        if (squared < distance) {
          distance = squared;
          nearest = &t;
        }
      }
    return nearest->height(p);
  }
  // -1 outside, 0 crosses the boundary, 1 wholly inside. A rectangle with
  // no boundary crossing has one membership value throughout its interior.
  int classify(const Vec3 &lo, const Vec3 &hi) const {
    if (hi[0] < minimum[0] || lo[0] > maximum[0] || hi[1] < minimum[1] ||
        lo[1] > maximum[1])
      return -1;
    for (const auto &e : boundary) {
      double first = 0, last = 1;
      bool hit = true;
      for (unsigned a = 0; a < 2 && hit; ++a) {
        const double d = e[1][a] - e[0][a];
        if (d == 0) {
          if (e[0][a] < lo[a] || e[0][a] > hi[a])
            hit = false;
        } else {
          double p = (lo[a] - e[0][a]) / d, q = (hi[a] - e[0][a]) / d;
          if (p > q)
            std::swap(p, q);
          first = (std::max)(first, p);
          last = (std::min)(last, q);
          hit = first <= last;
        }
      }
      if (hit)
        return 0;
    }
    return contains({(lo[0] + hi[0]) * .5, (lo[1] + hi[1]) * .5, 0}) ? 1 : -1;
  }
};
Footprint footprint(ResourceStore &store,
                    const WorldMapPreviewSection &section) {
  ResourceBlob blob;
  ResourceFailure failure;
  FigureCpuAsset figure;
  if (!store.resolve(section.figure_resource, blob, failure) ||
      !parse_figure_asset(blob, figure, failure))
    throw std::runtime_error(failure.message);
  require(!figure.indices.empty() && figure.indices.size() % 3 == 0,
          "Section FIG has no triangle footprint");
  std::vector<Vec3> positions(std::size_t(figure.position_groups) * 4);
  for (unsigned axis = 0; axis < 3; ++axis)
    require(std::isfinite(section.figure_shape[axis]),
            "Non-finite section shape");
  for (std::size_t i = 0; i < positions.size(); ++i)
    for (unsigned s = 0; s < figure.shape_count; ++s) {
      double w = 1;
      if (figure.shape_count == 8)
        for (unsigned a = 0; a < 3; ++a)
          w *= s & (1u << a) ? section.figure_shape[a]
                             : 1 - section.figure_shape[a];
      const auto p = point(figure.positions.data() + s * figure.position_groups,
                           unsigned(i));
      for (unsigned a = 0; a < 3; ++a)
        positions[i][a] += w * p[a];
    }
  Footprint result;
  result.resource = figure.identity;
  result.minimum.fill((std::numeric_limits<double>::max)());
  result.maximum.fill(std::numeric_limits<double>::lowest());
  for (std::size_t t = 0; t < figure.indices.size(); t += 3) {
    ClipTriangle triangle{};
    for (unsigned i = 0; i < 3; ++i) {
      const auto ci = figure.indices[t + i];
      require(ci < figure.components.size(),
              "Section FIG component out of range");
      const auto pi = figure.components[ci].position;
      require(pi < positions.size(), "Section FIG position out of range");
      triangle.p[i] = positions[pi];
      for (unsigned a = 0; a < 3; ++a) {
        require(std::isfinite(triangle.p[i][a]),
                "Non-finite section footprint");
        result.minimum[a] = (std::min)(result.minimum[a], triangle.p[i][a]);
        result.maximum[a] = (std::max)(result.maximum[a], triangle.p[i][a]);
      }
    }
    const double area = side(triangle.p[0], triangle.p[1], triangle.p[2]);
    if (area == 0)
      continue; // vertical edges add no area to the XY union
    if (area < 0)
      std::swap(triangle.p[1], triangle.p[2]);
    const auto &a = triangle.p[0], &b = triangle.p[1], &c = triangle.p[2];
    triangle.slope_x =
        ((b[2] - a[2]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[2] - a[2])) /
        std::abs(area);
    triangle.slope_y =
        ((b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0])) /
        std::abs(area);
    triangle.x0 = triangle.x1 = triangle.p[0][0];
    triangle.y0 = triangle.y1 = triangle.p[0][1];
    for (unsigned i = 1; i < 3; ++i) {
      triangle.x0 = (std::min)(triangle.x0, triangle.p[i][0]);
      triangle.x1 = (std::max)(triangle.x1, triangle.p[i][0]);
      triangle.y0 = (std::min)(triangle.y0, triangle.p[i][1]);
      triangle.y1 = (std::max)(triangle.y1, triangle.p[i][1]);
    }
    result.triangles.push_back(triangle);
  }
  require(!result.triangles.empty() && result.maximum[0] > result.minimum[0] &&
              result.maximum[1] > result.minimum[1],
          "Empty section XY footprint");
  require(result.triangles.size() <= 8192,
          "Section footprint exceeds preview budget");
  for (std::uint32_t i = 0; i < result.triangles.size(); ++i) {
    const auto &t = result.triangles[i];
    for (unsigned y = result.bin(t.y0, 1); y <= result.bin(t.y1, 1); ++y)
      for (unsigned x = result.bin(t.x0, 0); x <= result.bin(t.x1, 0); ++x)
        result.bins[y * kBins + x].push_back(i);
  }
  using XY = std::array<double, 2>;
  std::map<std::pair<XY, XY>, unsigned> edges;
  for (const auto &t : result.triangles)
    for (unsigned k = 0; k < 3; ++k) {
      const auto &a = t.p[k];
      const auto &b = t.p[(k + 1) % 3];
      XY x{a[0], a[1]}, y{b[0], b[1]};
      if (x == y)
        continue;
      if (y < x)
        std::swap(x, y);
      ++edges[{x, y}];
    }
  for (auto &t : result.triangles)
    for (unsigned k = 0; k < 3; ++k) {
      const auto &p = t.p[k], &q = t.p[(k + 1) % 3];
      XY a{p[0], p[1]}, b{q[0], q[1]};
      if (b < a)
        std::swap(a, b);
      if (edges.at({a, b}) != 1)
        continue;
      auto &intervals = t.outer_intervals[k];
      intervals.push_back({0, 1});
      for (const auto &other : result.triangles) {
        if (&other == &t)
          continue;
        const auto covered = outward_coverage(p, q, other);
        if (!covered)
          continue;
        std::vector<std::array<double, 2>> remaining;
        for (const auto &interval : intervals) {
          if ((*covered)[1] <= interval[0] || (*covered)[0] >= interval[1]) {
            remaining.push_back(interval);
            continue;
          }
          if (interval[0] < (*covered)[0])
            remaining.push_back({interval[0], (*covered)[0]});
          if ((*covered)[1] < interval[1])
            remaining.push_back({(*covered)[1], interval[1]});
        }
        intervals = std::move(remaining);
        if (intervals.empty())
          break;
      }
      if (!intervals.empty())
        t.outer_edges |= 1u << k;
      for (const auto &interval : intervals) {
        std::array<Vec3, 2> segment{};
        for (unsigned end = 0; end < 2; ++end)
          for (unsigned axis = 0; axis < 3; ++axis)
            segment[end][axis] = p[axis] + interval[end] * (q[axis] - p[axis]);
        result.boundary.push_back(segment);
      }
    }
  require(!result.boundary.empty(),
          "Section FIG has no open footprint boundary");
  result.figure = std::move(figure);
  return result;
}

NativeOwnedTextureFormat texture_format(MmpFormat format) {
  switch (format) {
  case MmpFormat::dxt1:
    return NativeOwnedTextureFormat::dxt1;
  case MmpFormat::dxt3:
    return NativeOwnedTextureFormat::dxt3;
  case MmpFormat::dxt5:
    return NativeOwnedTextureFormat::dxt5;
  case MmpFormat::rgb565:
    return NativeOwnedTextureFormat::rgb565;
  case MmpFormat::argb1555:
    return NativeOwnedTextureFormat::argb1555;
  case MmpFormat::argb4444:
    return NativeOwnedTextureFormat::argb4444;
  case MmpFormat::argb8888:
    return NativeOwnedTextureFormat::argb8888;
  default:
    throw std::runtime_error("Unsupported preview texture format");
  }
}
struct Texture {
  ResourceIdentity resource;
  NativeOwnedTextureFormat format = NativeOwnedTextureFormat::dxt1;
  std::uint64_t identity = 0, generation = 0;
  std::uint32_t opaque_mips = 0;
  std::vector<std::uint8_t> bytes;
  std::vector<NativeOwnedTextureMip> mips;
};
constexpr std::uint64_t kDerivedTextureView = 1ull << 40;
std::uint64_t preview_texture_generation(bool derived, unsigned width,
                                         unsigned height) {
  return 0x5052000000000000ull | (derived ? kDerivedTextureView : 0) |
         (std::uint64_t(width) << 20) | height;
}

template <class Sample>
std::vector<std::uint8_t>
preview_texture_area(std::uint32_t width, std::uint32_t height,
                     std::uint32_t target_width, std::uint32_t target_height,
                     Sample sample) {
  std::vector<std::uint8_t> result(std::size_t(target_width) * target_height *
                                   4);
  for (std::uint32_t y = 0; y < target_height; ++y) {
    const auto y0 = std::uint32_t(std::uint64_t(y) * height / target_height);
    const auto y1 =
        std::uint32_t(std::uint64_t(y + 1) * height / target_height);
    for (std::uint32_t x = 0; x < target_width; ++x) {
      const auto x0 = std::uint32_t(std::uint64_t(x) * width / target_width);
      const auto x1 =
          std::uint32_t(std::uint64_t(x + 1) * width / target_width);
      std::uint64_t alpha = 0, color[3]{};
      for (auto sy = y0; sy < y1; ++sy)
        for (auto sx = x0; sx < x1; ++sx) {
          const auto p = sample(sx, sy);
          alpha += p[3];
          for (unsigned c = 0; c < 3; ++c)
            color[c] += std::uint64_t(p[c]) * p[3];
        }
      const auto count = std::uint64_t(x1 - x0) * (y1 - y0);
      auto *p = result.data() + (std::size_t(y) * target_width + x) * 4;
      for (unsigned c = 0; c < 3; ++c)
        p[c] = alpha ? std::uint8_t((color[c] + alpha / 2) / alpha) : 0;
      p[3] = std::uint8_t((alpha + count / 2) / count);
    }
  }
  return result;
}

void preview_texture_thumbnail(const TextureCpuAsset &source, std::size_t first,
                               Texture &texture) {
  TextureAlphaPixels alpha;
  std::uint32_t width = 0, height = 0;
  std::vector<std::uint8_t> rgb;
  if (!texture_alpha_pixels(source, first, alpha) ||
      alpha.format != TextureAlphaFormat::packed ||
      !texture_rgb_pixels(source, first, width, height, rgb))
    throw std::runtime_error("Cannot reduce preview texture: " +
                             source.identity.canonical_name);
  unsigned shift = 0;
  if (alpha.alpha_mask)
    while (!(alpha.alpha_mask & (1u << shift)))
      ++shift;
  const auto maximum = alpha.alpha_mask >> shift;
  auto target_width = width, target_height = height;
  while (target_width > kTextureSide || target_height > kTextureSide) {
    target_width = (std::max)(1u, target_width / 2);
    target_height = (std::max)(1u, target_height / 2);
  }
  // Keep straight BGRA with alpha-weighted colour, as for existing owned
  // mip generation. Transparent source texels must not add a black fringe.
  auto pixels = preview_texture_area(
      width, height, target_width, target_height,
      [&](std::uint32_t x, std::uint32_t y) {
        const auto *color = rgb.data() + (std::size_t(y) * width + x) * 3;
        std::uint32_t raw = 0;
        std::memcpy(&raw,
                    alpha.data + std::size_t(y) * alpha.pitch +
                        std::size_t(x) * alpha.pixel_bytes,
                    alpha.pixel_bytes);
        auto a = maximum
                     ? std::uint32_t(
                           std::uint64_t((raw & alpha.alpha_mask) >> shift) *
                           255u / maximum)
                     : 255u;
        if (alpha.has_color_key && raw >= alpha.key_low &&
            raw <= alpha.key_high)
          a = 0;
        return std::array<std::uint8_t, 4>{color[2], color[1], color[0],
                                           std::uint8_t(a)};
      });
  std::size_t bytes = 0, levels = 0;
  for (auto w = target_width, h = target_height;;) {
    bytes += std::size_t(w) * h * 4;
    ++levels;
    if (w == 1 && h == 1)
      break;
    w = (std::max)(1u, w / 2);
    h = (std::max)(1u, h / 2);
  }
  texture.bytes.reserve(bytes);
  texture.mips.reserve(levels);
  texture.format = NativeOwnedTextureFormat::argb8888;
  // A reduced view is distinct from the original and authored-mip views.
  texture.generation =
      preview_texture_generation(true, target_width, target_height);
  texture.opaque_mips = 0;
  for (unsigned level = 0;; ++level) {
    const auto offset = texture.bytes.size();
    texture.bytes.insert(texture.bytes.end(), pixels.begin(), pixels.end());
    texture.mips.push_back({texture.bytes.data() + offset, target_width,
                            target_height, std::uint32_t(pixels.size())});
    bool opaque = true;
    for (std::size_t i = 3; i < pixels.size(); i += 4)
      if (pixels[i] != 255) {
        opaque = false;
        break;
      }
    if (opaque)
      texture.opaque_mips |= 1u << level;
    if (target_width == 1 && target_height == 1)
      break;
    const auto next_width = (std::max)(1u, target_width / 2);
    const auto next_height = (std::max)(1u, target_height / 2);
    pixels = preview_texture_area(
        target_width, target_height, next_width, next_height,
        [&](std::uint32_t x, std::uint32_t y) {
          const auto *p =
              pixels.data() + (std::size_t(y) * target_width + x) * 4;
          return std::array<std::uint8_t, 4>{p[0], p[1], p[2], p[3]};
        });
    target_width = next_width;
    target_height = next_height;
  }
}
enum class Surface { opaque, cutout, layer, cover };
struct Batch {
  std::uint32_t texture = 0;
  Surface surface = Surface::opaque;
  float opacity = 1;
  bool vertex_opacity = false;
  bool instanced = false;
  std::uint32_t shape_count = 1;
  std::vector<NativePackedGroup> positions, normals;
  std::vector<NativeTextureCoordinate> uv;
  std::vector<NativeVertexComponent> components;
  std::vector<std::uint16_t> indices;
  NativePreparedMeshHandle mesh;
  NativePreparedMaterialHandle material;
  Vec3 minimum{}, maximum{};
  std::uint64_t bytes() const noexcept {
    return (positions.capacity() + normals.capacity()) *
               sizeof(NativePackedGroup) +
           uv.capacity() * sizeof(NativeTextureCoordinate) +
           components.capacity() * sizeof(NativeVertexComponent) +
           indices.capacity() * sizeof(std::uint16_t);
  }
  std::uint64_t logical_bytes() const noexcept {
    return (positions.size() + normals.size()) * sizeof(NativePackedGroup) +
           uv.size() * sizeof(NativeTextureCoordinate) +
           components.size() * sizeof(NativeVertexComponent) +
           indices.size() * sizeof(std::uint16_t);
  }
};
struct Instance {
  std::uint32_t batch = 0;
  std::uint32_t texture = 0;
  std::array<float, 16> object_to_section{};
  std::array<float, 3> shape{};
  FigureBounds bounds{};
  NativePreparedMeshHandle mesh;
  NativePreparedMaterialHandle material;
};
struct Section {
  // One immutable prepared value, not an alias of its input MPR. Its owned
  // memory incarnation isolates different MOB/FIG/shape/activation results
  // even when their backing terrain and stream counts are identical.
  ResourceBlob prepared_resource;
  std::uint64_t generation = 0;
  std::uint32_t index = 0;
  std::vector<Texture> textures;
  std::vector<Batch> batches;
  std::vector<Instance> instances;
  std::vector<std::vector<GameRibbonPoint>> outlines;
  WorldMapPreviewStatus status;
  std::weak_ptr<const void> owner;
};

std::uint64_t texture_bytes(const Texture &texture) {
  return texture.bytes.capacity() +
         texture.mips.capacity() * sizeof(NativeOwnedTextureMip);
}
bool block_compressed(NativeOwnedTextureFormat format) {
  return format == NativeOwnedTextureFormat::dxt1 ||
         format == NativeOwnedTextureFormat::dxt3 ||
         format == NativeOwnedTextureFormat::dxt5;
}
// Whether a view whose first `dropped` levels are gone can drop one more. A
// block-compressed top level needs both sides divisible by 4: a 2x2 or 1x1
// DXT mip never becomes the top of a texture.
bool reducible(const Texture &texture, std::size_t dropped) {
  if (dropped + 1 >= texture.mips.size())
    return false;
  const auto &next = texture.mips[dropped + 1];
  return !block_compressed(texture.format) ||
         (next.width % 4 == 0 && next.height % 4 == 0);
}
// Retained bytes after reduce_texture_view(texture, dropped).
std::uint64_t retained_bytes(const Texture &texture, std::size_t dropped) {
  if (!dropped)
    return texture_bytes(texture);
  const std::uint8_t *end = texture.bytes.data() + texture.bytes.size();
  return std::uint64_t(end - texture.mips[dropped].data) +
         (texture.mips.size() - dropped) * sizeof(NativeOwnedTextureMip);
}
// Quality is selected while loading, on the existing immutable mip views.
// The limit admits compacted retained data, not temporary vector capacity.
// Drops the first `levels` mips (each step checked by reducible).
std::uint64_t reduce_texture_view(Texture &texture, std::size_t levels) {
  if (!levels || levels >= texture.mips.size())
    return 0;
  const auto before = texture_bytes(texture);
  const auto &top = texture.mips[levels];
  const std::uint8_t *end = texture.bytes.data() + texture.bytes.size();
  std::vector<std::uint8_t> bytes(top.data, end);
  std::vector<NativeOwnedTextureMip> mips;
  mips.reserve(texture.mips.size() - levels);
  for (std::size_t i = levels; i < texture.mips.size(); ++i) {
    const auto &source = texture.mips[i];
    mips.push_back({bytes.data() + (source.data - top.data), source.width,
                    source.height, source.bytes});
  }
  texture.generation = preview_texture_generation(
      (texture.generation & kDerivedTextureView) != 0, top.width, top.height);
  texture.bytes = std::move(bytes);
  texture.mips = std::move(mips);
  texture.opaque_mips >>= levels;
  return before - texture_bytes(texture);
}
// Planned reductions of an admission: levels to drop per texture. Nothing is
// reduced until the whole admission fits.
using ReductionPlan = std::map<Texture *, std::size_t>;
Texture *largest_reducible_texture(Section &section,
                                   const ReductionPlan &plan) {
  Texture *largest = nullptr;
  std::uint32_t largest_bytes = 0;
  for (auto &texture : section.textures) {
    const auto found = plan.find(&texture);
    const std::size_t dropped = found == plan.end() ? 0 : found->second;
    if (reducible(texture, dropped) &&
        (!largest || texture.mips[dropped].bytes > largest_bytes)) {
      largest = &texture;
      largest_bytes = texture.mips[dropped].bytes;
    }
  }
  return largest;
}
// Texels per prepared-space unit each texture's top level spends on the
// surfaces that bind it: per triangle the larger singular value of its
// surface-to-texel map (what mip selection follows), averaged by surface area.
// Instanced meshes are measured in their instances' placement (rest shape).
std::vector<double> texture_density(const Section &section) {
  std::vector<double> weighted(section.textures.size()),
      area(section.textures.size());
  const auto add = [&](std::uint32_t t, const Vec3 (&p)[3],
                       const NativeTextureCoordinate *(&uv)[3]) {
    if (t >= section.textures.size() || section.textures[t].mips.empty())
      return;
    const auto &top = section.textures[t].mips.front();
    const Vec3 e1{p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]},
        e2{p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
    const double l1 = std::sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
    if (!(l1 > 0))
      return;
    const double x2 = (e2[0] * e1[0] + e2[1] * e1[1] + e2[2] * e1[2]) / l1;
    const double y2 = std::sqrt((std::max)(
        e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2] - x2 * x2, 0.0));
    if (!(y2 > l1 * 1e-6))
      return;
    const double du1 = (uv[1]->u - uv[0]->u) * top.width,
                 dv1 = (uv[1]->v - uv[0]->v) * top.height,
                 du2 = (uv[2]->u - uv[0]->u) * top.width,
                 dv2 = (uv[2]->v - uv[0]->v) * top.height;
    const double j00 = du1 / l1, j10 = dv1 / l1,
                 j01 = (du2 - du1 * x2 / l1) / y2,
                 j11 = (dv2 - dv1 * x2 / l1) / y2;
    const double frobenius = j00 * j00 + j01 * j01 + j10 * j10 + j11 * j11,
                 det = j00 * j11 - j01 * j10;
    const double largest = std::sqrt(
        (frobenius +
         std::sqrt((std::max)(frobenius * frobenius - 4 * det * det, 0.0))) *
        .5);
    if (!std::isfinite(largest))
      return;
    const double a = .5 * l1 * y2;
    weighted[t] += a * largest;
    area[t] += a;
  };
  const auto mesh = [&](const Batch &batch, std::uint32_t texture,
                        const std::array<float, 16> *placement) {
    for (std::size_t i = 0; i + 2 < batch.indices.size(); i += 3) {
      Vec3 p[3];
      const NativeTextureCoordinate *uv[3];
      bool valid = true;
      for (unsigned k = 0; k < 3 && valid; ++k) {
        const auto index = batch.indices[i + k];
        valid = index < batch.components.size();
        if (!valid)
          break;
        const auto &c = batch.components[index];
        valid = std::size_t(c.position) / 4 < batch.positions.size() &&
                c.texture < batch.uv.size();
        if (!valid)
          break;
        p[k] = point(batch.positions.data(), c.position);
        if (placement)
          p[k] = transform(*placement, p[k]);
        uv[k] = &batch.uv[c.texture];
      }
      if (valid)
        add(texture, p, uv);
    }
  };
  for (const auto &batch : section.batches)
    if (!batch.instanced)
      mesh(batch, batch.texture, nullptr);
  for (const auto &instance : section.instances)
    if (instance.batch < section.batches.size())
      mesh(section.batches[instance.batch], instance.texture,
           &instance.object_to_section);
  for (std::size_t t = 0; t < weighted.size(); ++t)
    weighted[t] = area[t] > 0 ? weighted[t] / area[t] : 0;
  return weighted;
}
// The largest output the preview is shown on (4K), in pixels.
constexpr double kDisplayWidth = 3840, kDisplayHeight = 2160;
// Keeps each texture's top level at the smallest level that still gives at
// least one texel per output pixel where its section is shown largest. A
// section is never drawn larger than the display: its XY extent fits the
// output, so a prepared unit covers at most
// min(width / longer side, height / shorter side) pixels.
// Returns the bytes released.
double display_pixels_per_unit(const Section &section) {
  double low[2]{(std::numeric_limits<double>::max)(),
                (std::numeric_limits<double>::max)()},
      high[2]{std::numeric_limits<double>::lowest(),
              std::numeric_limits<double>::lowest()};
  const auto add = [&](const Vec3 &p) {
    for (unsigned a = 0; a < 2; ++a) {
      low[a] = (std::min)(low[a], p[a]);
      high[a] = (std::max)(high[a], p[a]);
    }
  };
  for (const auto &batch : section.batches)
    if (!batch.instanced) {
      add(batch.minimum);
      add(batch.maximum);
    }
  for (const auto &instance : section.instances)
    for (unsigned c = 0; c < 8; ++c) {
      Vec3 p{};
      for (unsigned a = 0; a < 3; ++a)
        p[a] = (c & (1u << a)) ? instance.bounds.maximum[a]
                               : instance.bounds.minimum[a];
      add(transform(instance.object_to_section, p));
    }
  const double ex = high[0] - low[0], ey = high[1] - low[1];
  if (!(ex > 0) || !(ey > 0))
    return 0;
  return (std::min)(kDisplayWidth / (std::max)(ex, ey),
                    kDisplayHeight / (std::min)(ex, ey));
}
std::uint64_t fit_textures_to_display(Section &section) {
  const double pixels_per_unit = display_pixels_per_unit(section);
  if (!(pixels_per_unit > 0))
    return 0;
  const auto density = texture_density(section);
  std::uint64_t saved = 0;
  for (std::size_t t = 0; t < section.textures.size(); ++t) {
    auto &texture = section.textures[t];
    // No measured surface: keep it as prepared.
    if (!(density[t] > 0))
      continue;
    std::size_t levels = 0;
    while (reducible(texture, levels) &&
           density[t] / double(std::uint64_t{2} << levels) >= pixels_per_unit)
      ++levels;
    saved += reduce_texture_view(texture, levels);
  }
  section.status.cpu_bytes -= saved;
  return saved;
}
// Keeps only the referenced vertices of a batch after its indices changed
// (positions per shape, normals and UVs remapped as compact_static_mesh).
void compact_batch(Batch &batch, const std::vector<std::uint32_t> &indices) {
  const std::size_t groups = batch.positions.size() / batch.shape_count;
  std::vector<std::uint32_t> components(batch.components.size(), UINT32_MAX),
      positions(groups * 4, UINT32_MAX),
      normals(batch.normals.size() * 4, UINT32_MAX),
      coordinates(batch.uv.size(), UINT32_MAX);
  std::vector<std::uint32_t> position_sources, normal_sources, uv_sources;
  const auto remap = [](auto &map, auto &sources, std::uint32_t source) {
    auto &index = map[source];
    if (index == UINT32_MAX) {
      index = std::uint32_t(sources.size());
      sources.push_back(source);
    }
    return std::uint16_t(index);
  };
  std::vector<NativeVertexComponent> kept;
  std::vector<std::uint16_t> kept_indices;
  kept_indices.reserve(indices.size());
  for (const auto i : indices) {
    auto &index = components[i];
    if (index == UINT32_MAX) {
      const auto &source = batch.components[i];
      index = std::uint32_t(kept.size());
      kept.push_back({remap(positions, position_sources, source.position),
                      remap(normals, normal_sources, source.normal),
                      remap(coordinates, uv_sources, source.texture)});
    }
    kept_indices.push_back(std::uint16_t(index));
  }
  const auto copy_lane = [](const NativePackedGroup &source, unsigned from,
                            NativePackedGroup &target, unsigned to) {
    target.x[to] = source.x[from];
    target.y[to] = source.y[from];
    target.z[to] = source.z[from];
    target.w[to] = source.w[from];
  };
  const auto kept_groups = (position_sources.size() + 3) / 4;
  std::vector<NativePackedGroup> kept_positions(kept_groups *
                                                batch.shape_count);
  for (unsigned shape = 0; shape < batch.shape_count; ++shape)
    for (std::size_t i = 0; i < position_sources.size(); ++i)
      copy_lane(batch.positions[shape * groups + position_sources[i] / 4],
                position_sources[i] % 4,
                kept_positions[shape * kept_groups + i / 4], unsigned(i % 4));
  std::vector<NativePackedGroup> kept_normals((normal_sources.size() + 3) / 4);
  for (std::size_t i = 0; i < normal_sources.size(); ++i)
    copy_lane(batch.normals[normal_sources[i] / 4], normal_sources[i] % 4,
              kept_normals[i / 4], unsigned(i % 4));
  std::vector<NativeTextureCoordinate> kept_uv;
  kept_uv.reserve(uv_sources.size());
  for (const auto source : uv_sources)
    kept_uv.push_back(batch.uv[source]);
  batch.positions = std::move(kept_positions);
  batch.normals = std::move(kept_normals);
  batch.uv = std::move(kept_uv);
  batch.components = std::move(kept);
  batch.indices = std::move(kept_indices);
}
// Simplifies one batch to an absolute geometric error (in its own position
// units, rest shape). Open borders stay: section outlines, cut edges and card
// edges keep their silhouette. A batch whose vertex opacity carries a layer
// blend keeps every vertex: the error bound does not cover opacity.
void simplify_batch(Batch &batch, float error) {
  if (batch.vertex_opacity || batch.indices.size() < 6 || !(error > 0) ||
      batch.shape_count == 0 || batch.positions.size() % batch.shape_count)
    return;
  const std::size_t vertices = batch.components.size();
  const std::size_t groups = batch.positions.size() / batch.shape_count;
  std::vector<float> positions(vertices * 3);
  for (std::size_t v = 0; v < vertices; ++v) {
    const auto source = batch.components[v].position;
    if (std::size_t(source) / 4 >= groups)
      return;
    const auto p = point(batch.positions.data(), source);
    for (unsigned a = 0; a < 3; ++a)
      positions[v * 3 + a] = float(p[a]);
  }
  std::vector<std::uint32_t> indices(batch.indices.begin(),
                                     batch.indices.end()),
      reduced(indices.size());
  for (const auto i : indices)
    if (i >= vertices)
      return;
  float result = 0;
  const auto count = meshopt_simplify(
      reduced.data(), indices.data(), indices.size(), positions.data(),
      vertices, 3 * sizeof(float), 0, error,
      meshopt_SimplifyLockBorder | meshopt_SimplifyErrorAbsolute, &result);
  if (count < 3 || count % 3 || count >= indices.size())
    return;
  reduced.resize(count);
  compact_batch(batch, reduced);
}
// The same display bound for geometry: every batch keeps the detail whose
// removal would move its surface by at least one output pixel where its
// section is shown largest. Instanced meshes are measured in object units at
// the largest scale any of their instances places them.
void simplify_for_display(Section &section) {
  const double pixels_per_unit = display_pixels_per_unit(section);
  if (!(pixels_per_unit > 0))
    return;
  std::vector<double> instance_scale(section.batches.size(), 0);
  for (const auto &instance : section.instances) {
    if (instance.batch >= section.batches.size())
      continue;
    const auto &m = instance.object_to_section;
    double largest = 0;
    for (unsigned c = 0; c < 3; ++c)
      largest = (std::max)(largest, std::sqrt(double(m[c]) * m[c] +
                                              double(m[4 + c]) * m[4 + c] +
                                              double(m[8 + c]) * m[8 + c]));
    instance_scale[instance.batch] =
        (std::max)(instance_scale[instance.batch], largest);
  }
  std::uint64_t triangles = 0;
  for (std::size_t b = 0; b < section.batches.size(); ++b) {
    auto &batch = section.batches[b];
    if (batch.surface == Surface::cover)
      continue;
    const double scale = batch.instanced ? instance_scale[b] : 1.0;
    if (scale > 0)
      simplify_batch(batch, float(1.0 / (pixels_per_unit * scale)));
    if (!batch.instanced)
      triangles += batch.indices.size() / 3;
  }
  for (const auto &instance : section.instances)
    if (instance.batch < section.batches.size())
      triangles += section.batches[instance.batch].indices.size() / 3;
  section.status.triangles = triangles;
}
// Clips once during preparation. The projected union must not duplicate
// coverage, including where translucent terrain layers cross folded FIGs.
struct Builder {
  Section &output;
  const Footprint *footprint;
  std::map<std::string, std::uint32_t> textures;
  std::map<std::tuple<std::uint32_t, Surface, bool, std::uint32_t>, std::size_t>
      last_batch;
  std::map<std::string, unsigned> omissions;
  std::vector<std::array<Vec3, 2>> outline_edges;
  std::vector<std::uint32_t> visited;
  std::uint32_t visit = 0;
  Vec3 scale{}, offset{};
  using VertexKey = std::array<std::uint32_t, 9>;
  struct VertexHash {
    std::size_t operator()(const VertexKey &k) const noexcept {
      std::size_t h = 0;
      for (auto v : k)
        h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
      return h;
    }
  };
  std::map<std::size_t,
           std::unordered_map<VertexKey, std::uint16_t, VertexHash>>
      vertex_indices;
  static VertexKey vertex_key(const Vertex &v) {
    auto n = v.n;
    normalize(n);
    const float values[9]{float(v.p[0]), float(v.p[1]), float(v.p[2]),
                          float(n[0]),   float(n[1]),   float(n[2]),
                          float(v.u),    float(v.v),    float(v.opacity)};
    VertexKey key{};
    for (unsigned i = 0; i < 9; ++i) {
      const float value = values[i] == 0 ? 0.f : values[i];
      std::memcpy(&key[i], &value, 4);
    }
    return key;
  }

  Builder(Section &result, const Footprint *clip,
          const TerrainCpuAsset &terrain)
      : output(result), footprint(clip) {
    scale = {1, 1, 1};
    if (!clip)
      return;
    scale[0] =
        (clip->maximum[0] - clip->minimum[0]) / (terrain.sector_count_x * 32.0);
    scale[1] =
        (clip->maximum[1] - clip->minimum[1]) / (terrain.sector_count_y * 32.0);
    // Keep map metres and scenery proportions. Uniform cover crops the
    // rectangle to the authored section instead of stretching either axis.
    scale[0] = scale[1] = scale[2] = (std::max)(scale[0], scale[1]);
    double lowest = (std::numeric_limits<double>::max)();
    for (const auto &sector : terrain.sectors)
      for (std::size_t i = 0; i < 1089; ++i)
        lowest = (std::min)(lowest, double(sector.vertices[i].z));
    offset = {(clip->minimum[0] + clip->maximum[0] -
               terrain.sector_count_x * 32.0 * scale[0]) *
                  .5,
              (clip->minimum[1] + clip->maximum[1] -
               terrain.sector_count_y * 32.0 * scale[1]) *
                  .5,
              clip->minimum[2] - lowest * scale[2]};
    visited.resize(clip->triangles.size());
  }
  std::array<float, 16> fit_matrix(double rigid_height = 0) const {
    return {float(scale[0]),
            0,
            0,
            float(offset[0]),
            0,
            float(scale[1]),
            0,
            float(offset[1]),
            0,
            0,
            float(scale[2]),
            float(offset[2] + rigid_height),
            0,
            0,
            0,
            1};
  }
  std::uint32_t texture(std::shared_ptr<const TextureCpuAsset> source) {
    require(source && source->source && !source->mips.empty(),
            "Missing preview texture bytes");
    const auto key = source->identity.key();
    const auto found = textures.find(key);
    if (found != textures.end())
      return found->second;
    Texture value;
    value.resource = source->identity;
    value.format = texture_format(source->format);
    // Prefer authored small mips. Single-level packed textures need their
    // reduced view baked here, while loading, rather than retaining a full
    // HD image. Both view generations are separate from world textures.
    std::size_t first = 0;
    while (first + 1 < source->mips.size() &&
           (source->mips[first].width > kTextureSide ||
            source->mips[first].height > kTextureSide))
      ++first;
    value.generation = preview_texture_generation(
        false, source->mips[first].width, source->mips[first].height);
    value.opaque_mips = source->opaque_mips >> first;
    value.identity = 1469598103934665603ull;
    for (const auto c : key) {
      value.identity ^= std::uint8_t(c);
      value.identity *= 1099511628211ull;
    }
    if (!value.identity)
      value.identity = 1;
    const bool packed = source->format == MmpFormat::rgb565 ||
                        source->format == MmpFormat::argb1555 ||
                        source->format == MmpFormat::argb4444 ||
                        source->format == MmpFormat::argb8888;
    // A packed level at/below the ceiling can still exceed the retained
    // budget. Give its final authored level a tail so the fitter can reduce it.
    const bool missing_tail =
        packed && first + 1 == source->mips.size() &&
        (source->mips[first].width > 1 || source->mips[first].height > 1);
    if (source->mips[first].width > kTextureSide ||
        source->mips[first].height > kTextureSide || missing_tail) {
      preview_texture_thumbnail(*source, first, value);
    } else {
      std::size_t size = 0;
      for (std::size_t i = first; i < source->mips.size(); ++i)
        size += source->mips[i].bytes;
      value.bytes.reserve(size);
      for (std::size_t i = first; i < source->mips.size(); ++i) {
        const auto &mip = source->mips[i];
        require(mip.byte_offset <= source->source->size() &&
                    mip.bytes <= source->source->size() - mip.byte_offset,
                "Preview texture mip outside source");
        const auto byte_offset = value.bytes.size();
        value.bytes.insert(
            value.bytes.end(), source->source->begin() + mip.byte_offset,
            source->source->begin() + mip.byte_offset + mip.bytes);
        value.mips.push_back({value.bytes.data() + byte_offset, mip.width,
                              mip.height, mip.bytes});
      }
    }
    const auto index = std::uint32_t(output.textures.size());
    output.status.cpu_bytes +=
        value.bytes.capacity() +
        value.mips.capacity() * sizeof(NativeOwnedTextureMip);
    const auto capacity = output.textures.capacity();
    output.textures.push_back(std::move(value));
    output.status.cpu_bytes +=
        (output.textures.capacity() - capacity) * sizeof(Texture);
    textures.emplace(key, index);
    return index;
  }
  Vertex fit(Vertex v) const {
    for (unsigned a = 0; a < 3; ++a) {
      v.p[a] = v.p[a] * scale[a] + offset[a];
      v.n[a] /= scale[a];
    }
    normalize(v.n);
    return v;
  }
  void emit(const Vertex &a, const Vertex &b, const Vertex &c,
            std::uint32_t texture, Surface surface) {
    Vec3 cross{};
    const Vec3 ab{b.p[0] - a.p[0], b.p[1] - a.p[1], b.p[2] - a.p[2]},
        ac{c.p[0] - a.p[0], c.p[1] - a.p[1], c.p[2] - a.p[2]};
    cross = {ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
             ab[0] * ac[1] - ab[1] * ac[0]};
    if (cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2] < 1e-28)
      return;
    const bool vertex_opacity =
        surface == Surface::layer &&
        (a.opacity != b.opacity || a.opacity != c.opacity);
    const float opacity =
        surface == Surface::layer && !vertex_opacity ? float(a.opacity) : 1.f;
    std::uint32_t opacity_bits = 0;
    std::memcpy(&opacity_bits, &opacity, sizeof(opacity_bits));
    const auto key =
        std::make_tuple(texture, surface, vertex_opacity, opacity_bits);
    auto found = last_batch.find(key);
    if (found == last_batch.end() ||
        output.batches[found->second].components.size() + 3 > kChunkVertices) {
      Batch batch;
      batch.texture = texture;
      batch.surface = surface;
      batch.opacity = opacity;
      batch.vertex_opacity = vertex_opacity;
      batch.minimum.fill((std::numeric_limits<double>::max)());
      batch.maximum.fill(std::numeric_limits<double>::lowest());
      const auto index = output.batches.size(),
                 capacity = output.batches.capacity();
      output.batches.push_back(std::move(batch));
      output.status.cpu_bytes +=
          (output.batches.capacity() - capacity) * sizeof(Batch);
      last_batch[key] = index;
      found = last_batch.find(key);
    }
    auto &batch = output.batches[found->second];
    const auto before = batch.logical_bytes();
    auto &vertices = vertex_indices[found->second];
    const Vertex *triangle[3] = {&a, &b, &c};
    for (const auto *v : triangle) {
      const auto vertex_id = vertex_key(*v);
      const auto existing = vertices.find(vertex_id);
      if (existing != vertices.end()) {
        batch.indices.push_back(existing->second);
        continue;
      }
      const auto index = std::uint16_t(batch.components.size());
      vertices.emplace(vertex_id, index);
      const auto group = index / 4;
      const auto lane = index % 4;
      if (!lane) {
        batch.positions.emplace_back();
        batch.normals.emplace_back();
      }
      auto n = v->n;
      normalize(n);
      auto &p = batch.positions[group];
      p.x[lane] = float(v->p[0]);
      p.y[lane] = float(v->p[1]);
      p.z[lane] = float(v->p[2]);
      p.w[lane] = 1;
      auto &normal = batch.normals[group];
      normal.x[lane] = float(n[0]);
      normal.y[lane] = float(n[1]);
      normal.z[lane] = float(n[2]);
      normal.w[lane] = batch.vertex_opacity ? float(v->opacity) : 1.f;
      batch.uv.push_back({float(v->u), float(v->v)});
      batch.components.push_back({index, index, index});
      batch.indices.push_back(index);
      for (unsigned axis = 0; axis < 3; ++axis) {
        batch.minimum[axis] = (std::min)(batch.minimum[axis], v->p[axis]);
        batch.maximum[axis] = (std::max)(batch.maximum[axis], v->p[axis]);
      }
    }
    ++output.status.triangles;
    output.status.cpu_bytes += batch.logical_bytes() - before;
  }
  void terrain_sides(const std::vector<Vertex> &polygon,
                     const ClipTriangle &clip, std::uint32_t texture,
                     Surface surface) {
    for (std::size_t i = 0; i < polygon.size(); ++i) {
      const auto &start = polygon[i], &end = polygon[(i + 1) % polygon.size()];
      const unsigned edges =
          start.carrier_edges & end.carrier_edges & clip.outer_edges;
      if (!edges)
        continue;
      unsigned edge = 0;
      while (!(edges & (1u << edge)))
        ++edge;
      const auto &p = clip.p[edge], &q = clip.p[(edge + 1) % 3];
      const unsigned axis =
          std::abs(q[0] - p[0]) >= std::abs(q[1] - p[1]) ? 0 : 1;
      const double from = (start.p[axis] - p[axis]) / (q[axis] - p[axis]);
      const double to = (end.p[axis] - p[axis]) / (q[axis] - p[axis]);
      if (from == to)
        continue;
      for (const auto &interval : clip.outer_intervals[edge]) {
        double first = (interval[0] - from) / (to - from);
        double last = (interval[1] - from) / (to - from);
        if (first > last)
          std::swap(first, last);
        first = (std::max)(first, 0.0);
        last = (std::min)(last, 1.0);
        if (first >= last)
          continue;
        auto a = mix(start, end, first), b = mix(start, end, last);
        if (surface == Surface::opaque) {
          // The border follows the actual top cut, not the carrier plane at
          // the foot of the side wall. Match the rendered float coordinates
          // before stitching adjacent terrain triangles/materials.
          std::array<Vec3, 2> cut{a.p, b.p};
          for (auto &point : cut)
            for (auto &coordinate : point)
              coordinate = float(coordinate);
          if (cut[0] != cut[1])
            outline_edges.push_back(cut);
        }
        const double ah = a.p[2] - clip.height(a.p);
        const double bh = b.p[2] - clip.height(b.p);
        if (ah <= 0 && bh <= 0)
          continue;
        // Close only the raised part, without changing the original top.
        if (ah < 0) {
          a = mix(a, b, ah / (ah - bh));
          a.p[2] = clip.height(a.p);
        } else if (bh < 0) {
          b = mix(a, b, ah / (ah - bh));
          b.p[2] = clip.height(b.p);
        }
        a.n = b.n = {q[1] - p[1], p[0] - q[0], 0};
        auto base_a = a, base_b = b;
        base_a.p[2] = clip.height(a.p);
        base_b.p[2] = clip.height(b.p);
        // Oppose the top edge's winding. UVs and opacity are the actual cut's
        // edge values from this same terrain material, including upper layers.
        emit(a, base_a, base_b, texture, surface);
        emit(a, base_b, b, texture, surface);
      }
    }
  }
  void triangle(std::array<Vertex, 3> triangle, std::uint32_t texture,
                Surface surface, double rigid_height = 0) {
    if (!footprint) {
      emit(triangle[0], triangle[1], triangle[2], texture, surface);
      return;
    }
    for (auto &v : triangle) {
      v = fit(v);
      v.p[2] += rigid_height;
    }
    const bool follows_surface =
        surface == Surface::opaque || surface == Surface::layer;
    double x0 = triangle[0].p[0], x1 = x0, y0 = triangle[0].p[1], y1 = y0;
    for (unsigned i = 1; i < 3; ++i) {
      x0 = (std::min)(x0, triangle[i].p[0]);
      x1 = (std::max)(x1, triangle[i].p[0]);
      y0 = (std::min)(y0, triangle[i].p[1]);
      y1 = (std::max)(y1, triangle[i].p[1]);
    }
    const int region = footprint->classify({x0, y0, 0}, {x1, y1, 0});
    if (region < 0)
      return;
    if (region > 0 && !follows_surface) {
      emit(triangle[0], triangle[1], triangle[2], texture, surface);
      return;
    }
    if (x1 < footprint->minimum[0] || x0 > footprint->maximum[0] ||
        y1 < footprint->minimum[1] || y0 > footprint->maximum[1])
      return;
    if (++visit == 0) {
      std::fill(visited.begin(), visited.end(), 0);
      ++visit;
    }
    std::vector<std::uint32_t> candidates;
    for (unsigned y = footprint->bin(y0, 1); y <= footprint->bin(y1, 1); ++y)
      for (unsigned x = footprint->bin(x0, 0); x <= footprint->bin(x1, 0); ++x)
        for (const auto index : footprint->bins[y * kBins + x]) {
          if (visited[index] == visit)
            continue;
          visited[index] = visit;
          const auto &clip = footprint->triangles[index];
          if (x1 < clip.x0 || x0 > clip.x1 || y1 < clip.y0 || y0 > clip.y1)
            continue;
          candidates.push_back(index);
        }
    // Remove each accepted piece from the remaining polygon. The result is
    // the projected union, including folded/overlapping authored triangles;
    // no overlap is emitted twice.
    using Polygon = std::vector<Vertex>;
    std::vector<Polygon> remaining{Polygon(triangle.begin(), triangle.end())};
    for (const auto index : candidates) {
      const auto &clip = footprint->triangles[index];
      std::vector<Polygon> next_remaining;
      for (auto &polygon : remaining) {
        for (auto &v : polygon)
          v.carrier_edges = 0;
        for (unsigned edge = 0; edge < 3 && polygon.size() >= 3; ++edge) {
          Polygon inside, outside;
          inside.reserve(polygon.size() + 1);
          outside.reserve(polygon.size() + 1);
          auto previous = polygon.back();
          double pd = side(clip.p[edge], clip.p[(edge + 1) % 3], previous.p);
          for (auto current : polygon) {
            const double cd =
                side(clip.p[edge], clip.p[(edge + 1) % 3], current.p);
            if (cd == 0)
              current.carrier_edges |= 1u << edge;
            if ((pd >= 0) != (cd >= 0)) {
              auto intersection = mix(previous, current, pd / (pd - cd));
              intersection.carrier_edges |= 1u << edge;
              inside.push_back(intersection);
              outside.push_back(intersection);
            }
            (cd >= 0 ? inside : outside).push_back(current);
            previous = current;
            pd = cd;
          }
          if (outside.size() >= 3)
            next_remaining.push_back(std::move(outside));
          polygon = std::move(inside);
        }
        if (follows_surface)
          for (auto &v : polygon) {
            v.p[2] += clip.height(v.p) - footprint->minimum[2];
            // Inverse transpose of z' = z + H(x,y). Static assemblies only
            // receive their one rigid translation, never this deformation.
            v.n[0] -= clip.slope_x * v.n[2];
            v.n[1] -= clip.slope_y * v.n[2];
          }
        const auto before = output.status.triangles;
        for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
          emit(polygon[0], polygon[i], polygon[i + 1], texture, surface);
        if (follows_surface && output.status.triangles > before)
          terrain_sides(polygon, clip, texture, surface);
      }
      remaining = std::move(next_remaining);
      if (remaining.empty())
        break;
    }
  }
};

void terrain(Builder &builder, ResourceStore &store,
             const TerrainCpuAsset &source, const std::atomic<bool> &stop) {
  const auto values = frozen_owned_terrain_geometry(source);
  std::map<std::uint32_t, std::uint32_t> atlases;
  for (std::uint32_t si = 0; si < source.sectors.size() && !stop; ++si) {
    const auto &sector = source.sectors[si];
    for (std::uint32_t mi = 0; mi < sector.materials.size(); ++mi) {
      const auto &material = sector.materials[mi];
      OwnedTerrainCommand command;
      command.source_sector = si;
      command.material = std::uint16_t(mi);
      command.lod = 2;
      command.subpart_mask = 15;
      require(select_owned_terrain_descriptor_counts(material, 2, false, false,
                                                     15, command.tile_count,
                                                     command.index_count),
              "Invalid preview terrain command");
      if (!command.tile_count)
        continue;
      command.vertex_count = command.tile_count * terrain_vertices_per_tile(2);
      // The existing decoder selects the same base atlas for upper and long
      // materials.
      const std::uint32_t page = mi % (source.texture_count + 2u);
      auto atlas = atlases.find(page);
      if (atlas == atlases.end()) {
        ResourceFailure failure;
        std::shared_ptr<const TextureCpuAsset> selected;
        if (page < source.pages.size() &&
            source.pages[page].ensure_upload_bytes(failure, false))
          selected = source.pages[page].texture;
        else {
          require(page < source.texture_resources.size(),
                  "Missing preview terrain atlas name");
          ResourceBlob blob;
          auto decoded = std::make_shared<TextureCpuAsset>();
          if (!store.resolve(source.texture_resources[page] + ".mmp", blob,
                             failure) ||
              !parse_texture_asset(blob, *decoded, failure))
            throw std::runtime_error(failure.message);
          selected = std::move(decoded);
        }
        atlas =
            atlases.emplace(page, builder.texture(std::move(selected))).first;
      }
      const auto texture = atlas->second;
      std::vector<NativePackedGroup> positions((command.vertex_count + 3) / 4),
          normals(positions.size());
      std::vector<NativeTextureCoordinate> uv(command.vertex_count);
      std::vector<NativeVertexComponent> components(command.vertex_count);
      std::vector<std::uint16_t> indices(command.index_count);
      std::vector<NativeTerrainVertex> metadata(command.vertex_count);
      NativeGeometryDecodeTarget target;
      target.positions = positions.data();
      target.position_groups = std::uint32_t(positions.size());
      target.normals = normals.data();
      target.normal_groups = std::uint32_t(normals.size());
      target.texture_coordinates = uv.data();
      target.texture_coordinate_count = command.vertex_count;
      target.components = components.data();
      target.component_count = command.vertex_count;
      target.indices = indices.data();
      target.index_count = command.index_count;
      target.terrain_vertices = metadata.data();
      target.terrain_vertex_count = command.vertex_count;
      OwnedTerrainSourceDecodeContext decode;
      decode.source = &source;
      decode.sector = &sector;
      decode.material = &material;
      decode.geometry = &values;
      decode.command = command;
      require(owned_terrain_decode_valid(&decode, target) &&
                  decode_owned_terrain_tiles(decode, target),
              "Preview terrain decode failed");
      for (std::size_t i = 0; i < indices.size(); i += 3) {
        std::array<Vertex, 3> triangle;
        for (unsigned k = 0; k < 3; ++k) {
          const auto &c = components[indices[i + k]];
          triangle[k] = {point(positions.data(), c.position),
                         point(normals.data(), c.normal), uv[c.texture].u,
                         uv[c.texture].v, metadata[c.position].opacity};
        }
        builder.triangle(triangle, texture,
                         mi < source.texture_count + 2u ? Surface::opaque
                                                        : Surface::layer);
      }
    }
  }
}

template <class Index>
void compact_static_mesh(Batch &mesh, const FigureCpuAsset &figure,
                         const Index *indices, std::size_t count) {
  // Keep the exact referenced channels once per immutable FIG resource.
  std::vector<std::uint32_t> components(figure.components.size(), UINT32_MAX),
      positions(std::size_t(figure.position_groups) * 4, UINT32_MAX),
      normals(std::size_t(figure.normal_groups) * 4, UINT32_MAX),
      coordinates(figure.texture_coordinates.size(), UINT32_MAX);
  std::vector<std::uint32_t> position_sources, normal_sources, uv_sources;
  const auto remap = [](auto &map, auto &sources, std::uint32_t source) {
    auto &index = map[source];
    if (index == UINT32_MAX) {
      index = std::uint32_t(sources.size());
      sources.push_back(source);
    }
    return std::uint16_t(index);
  };
  mesh.shape_count = figure.shape_count;
  mesh.indices.reserve(count);
  mesh.components.reserve((std::min)(count, figure.components.size()));
  for (std::size_t i = 0; i < count; ++i) {
    auto &index = components[indices[i]];
    if (index == UINT32_MAX) {
      const auto &source = figure.components[indices[i]];
      index = std::uint32_t(mesh.components.size());
      mesh.components.push_back(
          {remap(positions, position_sources, source.position),
           remap(normals, normal_sources, source.normal),
           remap(coordinates, uv_sources, source.texture)});
    }
    mesh.indices.push_back(std::uint16_t(index));
  }
  const auto copy_lane = [](const NativePackedGroup &source, unsigned from,
                            NativePackedGroup &target, unsigned to) {
    target.x[to] = source.x[from];
    target.y[to] = source.y[from];
    target.z[to] = source.z[from];
    target.w[to] = source.w[from];
  };
  const auto groups = (position_sources.size() + 3) / 4;
  mesh.positions.resize(groups * figure.shape_count);
  for (unsigned shape = 0; shape < figure.shape_count; ++shape)
    for (std::size_t i = 0; i < position_sources.size(); ++i) {
      const auto source = position_sources[i];
      copy_lane(figure.positions[shape * figure.position_groups + source / 4],
                source % 4, mesh.positions[shape * groups + i / 4],
                unsigned(i % 4));
    }
  mesh.normals.resize((normal_sources.size() + 3) / 4);
  for (std::size_t i = 0; i < normal_sources.size(); ++i) {
    const auto source = normal_sources[i];
    copy_lane(figure.normals[source / 4], source % 4, mesh.normals[i / 4],
              unsigned(i % 4));
  }
  mesh.uv.reserve(uv_sources.size());
  for (const auto source : uv_sources)
    mesh.uv.push_back(figure.texture_coordinates[source]);
}

void statics(Builder &builder,
             const std::shared_ptr<const OwnedMapSnapshot> &map,
             const std::atomic<bool> &stop) {
  // A small cutout still needs its authored silhouette. Share referenced
  // geometry between instances instead of forcing the last geometric LOD.
  std::map<std::string, std::size_t> meshes;
  for (const auto &object : map->objects) {
    if (stop)
      return;
    if (!((object.family >= 53 && object.family <= 55) || object.family == 60))
      continue;
    if (const auto *why = retained_static_structure_omission(object, *map)) {
      ++builder.omissions[why];
      ++builder.output.status.omitted_objects;
      continue;
    }
    std::shared_ptr<RetainedStaticSourceCandidate> candidate;
    RetainedStaticPrepareDiagnostic diagnostic;
    if (!prepare_retained_static_candidate(map, map->terrain, object, candidate,
                                           &diagnostic)) {
      ++builder
            .omissions[retained_static_prepare_result_name(diagnostic.result)];
      ++builder.output.status.omitted_objects;
      continue;
    }
    // This is the common, terrain-grounded MOB placement used to prepare
    // every node. BON root offsets and individual part bounds are not anchors.
    double rigid_height = 0;
    if (builder.footprint) {
      Vec3 root{};
      for (unsigned a = 0; a < 3; ++a)
        root[a] =
            candidate->world_position[a] * builder.scale[a] + builder.offset[a];
      rigid_height =
          builder.footprint->height(root) - builder.footprint->minimum[2];
    }
    const auto object_fit = builder.fit_matrix(rigid_height);
    const auto before = builder.output.status.triangles;
    for (const auto &node : candidate->nodes) {
      const auto key = node.figure->identity.key();
      const auto &indices = node.figure->indices;
      const auto count = indices.size();
      Vec3 lo{}, hi{};
      lo.fill((std::numeric_limits<double>::max)());
      hi.fill(std::numeric_limits<double>::lowest());
      for (unsigned c = 0; c < 8; ++c) {
        Vec3 p{};
        for (unsigned a = 0; a < 3; ++a)
          p[a] =
              (c & (1u << a)) ? node.bounds.maximum[a] : node.bounds.minimum[a];
        p = transform(node.object_to_world, p);
        for (unsigned a = 0; a < 3; ++a) {
          p[a] = p[a] * builder.scale[a] + builder.offset[a];
          if (a == 2)
            p[a] += rigid_height;
          lo[a] = (std::min)(lo[a], p[a]);
          hi[a] = (std::max)(hi[a], p[a]);
        }
      }
      const int region =
          builder.footprint ? builder.footprint->classify(lo, hi) : 1;
      if (region < 0)
        continue;
      const auto texture = builder.texture(map->textures.at(
          *figure_texture_for_type(object, node.figure->texture_number)));
      if (region > 0) {
        auto shared = meshes.find(key);
        if (shared == meshes.end()) {
          Batch mesh;
          mesh.instanced = true;
          mesh.surface = Surface::cutout;
          compact_static_mesh(mesh, *node.figure, indices.data(), count);
          const auto index = builder.output.batches.size(),
                     capacity = builder.output.batches.capacity();
          builder.output.status.cpu_bytes += mesh.bytes();
          builder.output.batches.push_back(std::move(mesh));
          builder.output.status.cpu_bytes +=
              (builder.output.batches.capacity() - capacity) * sizeof(Batch);
          shared = meshes.emplace(key, index).first;
        }
        Instance instance;
        instance.batch = std::uint32_t(shared->second);
        instance.texture = texture;
        instance.shape = object.shape;
        instance.bounds = node.bounds;
        instance.object_to_section =
            retained_multiply_matrix(object_fit, node.object_to_world);
        const auto capacity = builder.output.instances.capacity();
        builder.output.instances.push_back(std::move(instance));
        builder.output.status.cpu_bytes +=
            (builder.output.instances.capacity() - capacity) * sizeof(Instance);
        builder.output.status.triangles += count / 3;
        continue;
      }
      for (std::size_t i = 0; i < count; i += 3) {
        std::array<Vertex, 3> triangle;
        for (unsigned k = 0; k < 3; ++k) {
          const auto ci = indices[i + k];
          const auto &c = node.figure->components[ci];
          auto n = point(node.normals.data(), c.normal);
          // MOB placement is rigid; remove translation before rotating the
          // normal.
          auto rotated = transform(node.object_to_world, n);
          for (unsigned a = 0; a < 3; ++a)
            rotated[a] -= node.object_to_world[a * 4 + 3];
          normalize(rotated);
          const auto &uv = node.figure->texture_coordinates[c.texture];
          triangle[k] = {transform(node.object_to_world,
                                   point(node.positions.data(), c.position)),
                         rotated, uv.u, uv.v};
        }
        builder.triangle(triangle, texture, Surface::cutout, rigid_height);
      }
    }
    if (builder.output.status.triangles > before)
      ++builder.output.status.static_objects;
  }
}

void finish_geometry(Section &section, std::vector<std::array<Vec3, 2>> edges) {
  section.batches.shrink_to_fit();
  section.instances.shrink_to_fit();
  section.status.cpu_bytes = section.batches.capacity() * sizeof(Batch) +
                             section.instances.capacity() * sizeof(Instance) +
                             section.textures.capacity() * sizeof(Texture);
  section.status.prop_bytes = section.instances.capacity() * sizeof(Instance);
  for (auto &batch : section.batches) {
    batch.positions.shrink_to_fit();
    batch.normals.shrink_to_fit();
    batch.uv.shrink_to_fit();
    batch.components.shrink_to_fit();
    batch.indices.shrink_to_fit();
    section.status.cpu_bytes += batch.bytes();
    if (batch.surface == Surface::cutout)
      section.status.prop_bytes += sizeof(Batch) + batch.bytes();
  }
  for (const auto &texture : section.textures)
    section.status.cpu_bytes +=
        texture.bytes.capacity() +
        texture.mips.capacity() * sizeof(NativeOwnedTextureMip);
  if (!edges.empty()) {
    while (!edges.empty()) {
      std::vector<Vec3> loop{edges.back()[0], edges.back()[1]};
      edges.pop_back();
      for (;;) {
        const auto match =
            std::find_if(edges.begin(), edges.end(), [&](const auto &e) {
              return e[0] == loop.back() || e[1] == loop.back();
            });
        if (match == edges.end())
          break;
        const bool forward = (*match)[0] == loop.back();
        loop.push_back((*match)[forward ? 1 : 0]);
        edges.erase(match);
      }
      std::vector<GameRibbonPoint> line;
      line.reserve(loop.size());
      for (const auto &p : loop)
        line.push_back({float(p[0]), float(p[1]), float(p[2])});
      section.outlines.push_back(std::move(line));
    }
    section.outlines.shrink_to_fit();
    section.status.cpu_bytes +=
        section.outlines.capacity() * sizeof(std::vector<GameRibbonPoint>);
    for (const auto &loop : section.outlines)
      section.status.cpu_bytes += loop.capacity() * sizeof(GameRibbonPoint);
  }
}

std::shared_ptr<Section> prepare(std::uint64_t generation, std::uint32_t index,
                                 WorldMapPreviewSpace space,
                                 ResourceStore &store,
                                 OwnedResourceLoader &loader,
                                 const ResourceStoreConfig &mounts,
                                 const WorldMapPreviewSection &description,
                                 const std::atomic<bool> &stop) {
  const auto started = Clock::now();
  std::optional<Footprint> clip;
  if (space == WorldMapPreviewSpace::section_fitted)
    clip.emplace(footprint(store, description));
  OwnedMapLoadRequest request;
  request.generation = generation;
  request.mounts = mounts;
  request.map_resource = description.map_resource;
  request.terrain_resource = description.terrain_resource;
  request.family_allowlist = {53, 54, 55, 60};
  request.backdrop_objects = true;
  ResourceFailure failure;
  OwnedResourceRequestToken token;
  if (!loader.request(request, failure, &token))
    throw std::runtime_error(failure.message);
  std::shared_ptr<const OwnedMapSnapshot> map;
  require(loader.wait_publish(token, map) == OwnedResourceWaitResult::ready &&
              map,
          "Preview map load did not publish");
  loader.retire_generation(generation);
  require(map->terrain && map->mob, map->failures.empty()
                                        ? "Preview MPR/MOB missing"
                                        : map->failures.front().c_str());
  require(map->terrain->immutable_descriptor_validated,
          "Preview terrain descriptor was rejected");
  auto result = std::make_shared<Section>();
  result->generation = generation;
  result->index = index;
  result->status.load_ms = milliseconds(started);
  result->status.source_peak_bytes = map->metrics.peak_owned_bytes;
  const auto assembling = Clock::now();
  Builder builder(*result, clip ? &*clip : nullptr, *map->terrain);
  terrain(builder, store, *map->terrain, stop);
  statics(builder, map, stop);
  require(!result->batches.empty(), "Preview has no drawable terrain");
  if (clip) {
    // Unknown locations keep the authored section surface and native MapG,
    // never their actual MPR or scenery. Decode and retain that owned cover
    // now, rather than falling through to the native figure at map opening.
    ResourceBlob blob;
    auto texture = std::make_shared<TextureCpuAsset>();
    if (!store.resolve(description.unknown_texture_resource, blob, failure) ||
        !parse_texture_asset(blob, *texture, failure))
      throw std::runtime_error(failure.message);
    Batch cover;
    compact_static_mesh(cover, clip->figure, clip->figure.indices.data(),
                        clip->figure.indices.size());
    cover.minimum = clip->minimum;
    cover.maximum = clip->maximum;
    cover.surface = Surface::cover;
    cover.texture = builder.texture(std::move(texture));
    result->batches.push_back(std::move(cover));
  }
  simplify_for_display(*result);
  finish_geometry(*result, std::move(builder.outline_edges));
  if (!builder.omissions.empty()) {
    result->status.reason = description.zone + ": omitted static shapes";
    for (const auto &[reason, count] : builder.omissions)
      result->status.reason += " " + reason + ":" + std::to_string(count);
  }
  result->status.prepare_ms = milliseconds(assembling);
  result->status.batches = std::uint32_t(
      result->instances.size() +
      std::count_if(result->batches.begin(), result->batches.end(),
                    [](const auto &b) { return !b.instanced; }));
  result->status.minimum.fill((std::numeric_limits<float>::max)());
  result->status.maximum.fill(std::numeric_limits<float>::lowest());
  for (const auto &batch : result->batches)
    if (!batch.instanced)
      for (unsigned a = 0; a < 3; ++a) {
        result->status.minimum[a] =
            (std::min)(result->status.minimum[a], float(batch.minimum[a]));
        result->status.maximum[a] =
            (std::max)(result->status.maximum[a], float(batch.maximum[a]));
      }
  for (const auto &instance : result->instances)
    for (unsigned c = 0; c < 8; ++c) {
      Vec3 p{};
      for (unsigned a = 0; a < 3; ++a)
        p[a] = (c & (1u << a)) ? instance.bounds.maximum[a]
                               : instance.bounds.minimum[a];
      p = transform(instance.object_to_section, p);
      for (unsigned a = 0; a < 3; ++a) {
        result->status.minimum[a] =
            (std::min)(result->status.minimum[a], float(p[a]));
        result->status.maximum[a] =
            (std::max)(result->status.maximum[a], float(p[a]));
      }
    }
  fit_textures_to_display(*result);
  // Allocate the identity through the same authority as other owned memory
  // resources. Retaining this small source description gives the prepared
  // result its own lifetime, without inventing an archive-member identity,
  // using pointer identity, or relying on a content-hash collision policy.
  std::string source = "world-map-preview:3\n" + std::to_string(generation) +
                       ":" + std::to_string(index) + ":" +
                       std::to_string(static_cast<unsigned>(space)) + "\n" +
                       description.zone + "\n" + map->terrain->identity.key() +
                       "\n" + map->mob->identity.key() + "\n";
  if (clip) {
    source += clip->resource.key() + "\n" +
              description.unknown_texture_resource + "\n";
    for (const float shape : description.figure_shape) {
      std::uint32_t bits = 0;
      std::memcpy(&bits, &shape, sizeof(bits));
      source += std::to_string(bits) + ":";
    }
    source += '\n';
  }
  for (const auto &[name, model] : map->models) {
    source += name + "\n" + model->model_container.key() + "\n" +
              model->bone_container.key() + "\n";
    for (const auto &node : model->nodes)
      source += node.figure.identity.key() + "\n";
  }
  require(source.size() <= kIdentityDescriptionBytes,
          "Preview identity description exceeds input bound");
  const auto name = "__owned\\world-map-preview\\" +
                    std::to_string(generation) + "\\" + std::to_string(index) +
                    ".geometry";
  if (!store.copy_selected_memory(name, source.data(),
                                  static_cast<std::uint32_t>(source.size()),
                                  result->prepared_resource, failure))
    throw std::runtime_error(failure.message);
  result->status.cpu_bytes += result->prepared_resource.bytes->capacity();
  result->status.state = WorldMapPreviewState::ready;
  result->owner = result;
  return result;
}

constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0,
                                         0, 0, 1, 0, 0, 0, 0, 1};
NativeOwnedFixedState material(Surface surface) {
  const bool cutout = surface == Surface::cutout;
  const bool layer = surface == Surface::layer;
  NativeOwnedFixedState s;
  s.enabled = true;
  s.cull_mode = D3DCULL_NONE;
  s.fill_mode = D3DFILL_SOLID;
  s.z_enable = D3DZB_TRUE;
  s.z_write_enable = !layer;
  s.z_func = D3DCMP_LESSEQUAL;
  s.alpha_blend_enable = layer;
  s.source_blend = layer ? D3DBLEND_SRCALPHA : D3DBLEND_ONE;
  s.destination_blend = layer ? D3DBLEND_INVSRCALPHA : D3DBLEND_ZERO;
  s.alpha_test_enable = cutout;
  s.alpha_func = D3DCMP_GREATER;
  s.alpha_ref = 200;
  s.texture_factor = 0xffffffff;
  s.specular_enable = FALSE;
  s.color_op = D3DTOP_MODULATE;
  s.color_arg1 = D3DTA_TEXTURE;
  s.color_arg2 = D3DTA_DIFFUSE;
  // Preserve layer texture coverage and authored object opacity at rest.
  // Foliage retains alpha cutout; no animated water or world VFX run here.
  s.alpha_op = cutout || layer ? D3DTOP_MODULATE : D3DTOP_SELECTARG1;
  s.alpha_arg1 = cutout || layer ? D3DTA_TEXTURE : D3DTA_DIFFUSE;
  s.alpha_arg2 = D3DTA_DIFFUSE;
  s.address_u = s.address_v = cutout ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP;
  s.min_filter = D3DTFN_LINEAR;
  s.mag_filter = D3DTFG_LINEAR;
  s.mip_filter = D3DTFP_LINEAR;
  return s;
}
} // namespace

struct WorldMapPreview::Impl {
  bool enabled = false;
  struct Cache {
    WorldMapPreviewSpace space = WorldMapPreviewSpace::section_fitted;
    ResourceStoreConfig mounts;
    std::vector<WorldMapPreviewSection> descriptions;
    std::vector<WorldMapPreviewStatus> status;
    std::vector<std::shared_ptr<Section>> sections;
  };
  std::unique_ptr<Cache> cache;
  std::vector<std::size_t> active;
  std::string cache_failure, binding_failure;
  bool cache_reused = false;
};
WorldMapPreview::WorldMapPreview() : impl_(std::make_unique<Impl>()) {}
WorldMapPreview::~WorldMapPreview() = default;
bool WorldMapPreview::enabled() const noexcept { return impl_->enabled; }
void WorldMapPreview::set_enabled(bool value) noexcept {
  if (!value)
    deactivate();
  impl_->enabled = value;
}
void WorldMapPreview::deactivate() noexcept {
  impl_->active.clear();
  impl_->binding_failure.clear();
}
void WorldMapPreview::release() noexcept {
  deactivate();
  impl_->enabled = false;
  impl_->cache.reset();
  impl_->cache_failure.clear();
  impl_->cache_reused = false;
}
namespace {
bool same_section(const WorldMapPreviewSection &a,
                  const WorldMapPreviewSection &b) {
  return a.zone == b.zone && a.terrain_resource == b.terrain_resource &&
         a.map_resource == b.map_resource &&
         a.figure_resource == b.figure_resource &&
         a.figure_shape == b.figure_shape &&
         a.unknown_texture_resource == b.unknown_texture_resource;
}
} // namespace
bool WorldMapPreview::precache_loading(
    std::uint64_t generation, WorldMapPreviewSpace space,
    ResourceStoreConfig mounts, std::vector<WorldMapPreviewSection> sections,
    std::string &failure) noexcept {
  deactivate();
  impl_->cache_reused = false;
  try {
    if (!failure.empty())
      throw std::runtime_error(failure);
    require(generation != 0, "Missing preview cache generation");
    require(!sections.empty() && sections.size() <= 65535,
            "Invalid loading-screen section count");
    if (impl_->cache && impl_->cache->space == space &&
        impl_->cache->mounts.loose_roots == mounts.loose_roots &&
        impl_->cache->mounts.archives == mounts.archives &&
        impl_->cache->mounts.archive_kinds == mounts.archive_kinds &&
        impl_->cache->descriptions.size() == sections.size() &&
        std::equal(sections.begin(), sections.end(),
                   impl_->cache->descriptions.begin(), same_section)) {
      // Same descriptions and mounts: a section that failed to prepare fails
      // again from the same data, so its cached failure is reused as well.
      impl_->cache_reused = true;
      bridge_report_event("world_map_preview_cache_reused",
                          sections.front().zone.c_str());
      for (const auto &s : impl_->cache->status)
        if (s.state != WorldMapPreviewState::ready) {
          failure = s.reason;
          return false;
        }
      return true;
    }
    // One loading cache: release the previous CPU owners before allocating the
    // next one. Expired GPU owners retire through the existing renderer path.
    impl_->cache.reset();
    impl_->cache_failure.clear();
    auto cache = std::make_unique<Impl::Cache>();
    cache->space = space;
    cache->mounts = mounts;
    cache->descriptions = std::move(sections);
    cache->status.resize(cache->descriptions.size());
    cache->sections.resize(cache->descriptions.size());
    ResourceStore store;
    OwnedResourceLoader loader;
    ResourceFailure error;
    if (!store.open(mounts, error) || !loader.start(1, error))
      throw std::runtime_error(error.message);
    const std::atomic<bool> stop{false};
    for (std::size_t i = 0; i < cache->descriptions.size(); ++i) {
      const auto &description = cache->descriptions[i];
      try {
        auto made = prepare(generation, std::uint32_t(i), space, store, loader,
                            mounts, description, stop);
        // The retained limit belongs to one native allod, not the catalog of
        // every reachable world. MapG is copied from that authoritative name.
        std::uint64_t bytes = made->status.cpu_bytes;
        for (std::size_t j = 0; j < i; ++j)
          if (cache->sections[j] &&
              cache->descriptions[j].unknown_texture_resource ==
                  description.unknown_texture_resource)
            bytes += cache->sections[j]->status.cpu_bytes;
        // Plan the reductions first and apply them only once the allod fits:
        // a section that cannot be admitted leaves the others as they were.
        ReductionPlan plan;
        std::map<Texture *, Section *> owners;
        while (bytes > kAllodBytes) {
          Section *selected = made.get();
          Texture *texture = largest_reducible_texture(*made, plan);
          const auto top = [&](Texture *t) {
            const auto found = plan.find(t);
            return t->mips[found == plan.end() ? 0 : found->second].bytes;
          };
          for (std::size_t j = 0; j < i; ++j)
            if (cache->sections[j] &&
                cache->descriptions[j].unknown_texture_resource ==
                    description.unknown_texture_resource) {
              auto *candidate =
                  largest_reducible_texture(*cache->sections[j], plan);
              if (candidate && (!texture || top(candidate) > top(texture))) {
                selected = cache->sections[j].get();
                texture = candidate;
              }
            }
          if (!texture)
            throw std::runtime_error(
                "Compacted preview data requires " + std::to_string(bytes) +
                " bytes; allod limit is " + std::to_string(kAllodBytes));
          auto &dropped = plan[texture];
          owners[texture] = selected;
          bytes -= retained_bytes(*texture, dropped) -
                   retained_bytes(*texture, dropped + 1);
          ++dropped;
        }
        for (const auto &[texture, levels] : plan)
          owners[texture]->status.cpu_bytes -=
              reduce_texture_view(*texture, levels);
        // Publish only after admission. A section that cannot fit must not
        // discard already valid sections or unrelated worlds.
        cache->status[i] = made->status;
        cache->sections[i] = std::move(made);
      } catch (const std::exception &e) {
        auto &status = cache->status[i];
        status.state = WorldMapPreviewState::failed;
        status.reason = description.zone + ": " + e.what();
        if (failure.empty())
          failure = status.reason;
        bridge_report_event("world_map_preview_cache_section_failed",
                            status.reason.c_str());
      }
    }
    for (std::size_t i = 0; i < cache->sections.size(); ++i) {
      if (cache->sections[i]) {
        cache->status[i] = cache->sections[i]->status;
        const auto &description = cache->descriptions[i];
        char detail[320];
        const auto &s = cache->status[i];
        std::snprintf(detail, sizeof(detail),
                      "zone=%s section=%u triangles=%llu batches=%u static=%u "
                      "omitted=%u cpu_bytes=%llu prop_bytes=%llu load_ms=%.2f "
                      "prepare_ms=%.2f",
                      description.zone.c_str(), unsigned(i),
                      static_cast<unsigned long long>(s.triangles), s.batches,
                      s.static_objects, s.omitted_objects,
                      static_cast<unsigned long long>(s.cpu_bytes),
                      static_cast<unsigned long long>(s.prop_bytes), s.load_ms,
                      s.prepare_ms);
        bridge_report_event("world_map_preview_ready", detail);
        if (!s.reason.empty())
          bridge_report_event("world_map_preview_static_omission",
                              s.reason.c_str());
      }
    }
    loader.shutdown();
    impl_->cache = std::move(cache);
    return failure.empty();
  } catch (const std::exception &e) {
    failure = e.what();
    impl_->cache.reset();
    impl_->cache_failure = failure;
    return false;
  }
}
bool WorldMapPreview::contains(WorldMapPreviewSpace space,const std::vector<WorldMapPreviewSection>& sections) const {
  if(!impl_->cache || impl_->cache->space!=space)return false;
  const auto& cached=impl_->cache->descriptions;
  return std::all_of(sections.begin(),sections.end(),[&](const auto& section) {
    return std::any_of(cached.begin(),cached.end(),[&](const auto& d){return same_section(section,d);});
  });
}
bool WorldMapPreview::activate(std::uint64_t generation,
                               WorldMapPreviewSpace space,
                               std::vector<WorldMapPreviewSection> sections,
                               std::string &failure) noexcept {
  deactivate();
  if (!enabled())
    return true;
  try {
    if (!failure.empty())
      throw std::runtime_error(failure);
    require(generation != 0, "Missing preview binding generation");
    require(!sections.empty() && sections.size() <= 15,
            "Invalid native section count");
    if (!impl_->cache_failure.empty())
      throw std::runtime_error(impl_->cache_failure);
    require(
        impl_->cache && impl_->cache->space == space,
        "World map previews were not baked during loading; reload the save");
    for (const auto &section : sections) {
      const auto &descriptions = impl_->cache->descriptions;
      const auto found = std::find_if(
          descriptions.begin(), descriptions.end(),
          [&](const auto &cached) { return same_section(section, cached); });
      require(
          found != descriptions.end(),
          "World map section was not baked during loading; reload the save");
      impl_->active.push_back(std::size_t(found - descriptions.begin()));
    }
    return true;
  } catch (const std::exception &e) {
    failure = e.what();
    impl_->active.clear();
    impl_->binding_failure = failure;
    return false;
  }
}
WorldMapPreviewCacheStats WorldMapPreview::cache_stats() const noexcept {
  WorldMapPreviewCacheStats result;
  if (!impl_->cache) return result;
  result.reused = impl_->cache_reused;
  result.sections = std::uint32_t(impl_->cache->status.size());
  for (const auto &status : impl_->cache->status) {
    if (status.state == WorldMapPreviewState::ready) ++result.ready;
    else ++result.failed;
    result.cpu_bytes += status.cpu_bytes;
    result.source_peak_bytes = (std::max)(result.source_peak_bytes, status.source_peak_bytes);
  }
  return result;
}
WorldMapPreviewStatus WorldMapPreview::status(std::size_t section) const {
  if (!enabled())
    return {};
  if (!impl_->binding_failure.empty())
    return {WorldMapPreviewState::failed, impl_->binding_failure};
  if (!impl_->cache)
    return {};
  if (section >= impl_->active.size())
    return {WorldMapPreviewState::failed, "Preview section index out of range"};
  return impl_->cache->status[impl_->active[section]];
}
NativeWorldLighting WorldMapPreview::preview_lighting() noexcept {
  NativeWorldLighting light;
  light.ambient = {1, 1, 1, 1};
  light.sunlight_direction = {.3f, -.4f, -.8660254f, 0};
  light.sunlight_diffuse = {.35f, .35f, .35f, 1};
  return light;
}
std::uint64_t
WorldMapPreview::section_revision(std::size_t section) const noexcept {
  if (!impl_->cache || section >= impl_->active.size())
    return 0;
  const auto &value = impl_->cache->sections[impl_->active[section]];
  return value ? value->prepared_resource.identity.container_incarnation : 0;
}
bool WorldMapPreview::submit(std::size_t section,
                             const std::array<float, 16> &clip,
                             NativeGeometrySubmitFn sink, void *context,
                             std::string &failure, WorldMapPreviewPass pass,
                             bool discovered) noexcept {
  failure.clear();
  try {
    require(enabled() && impl_->cache && sink,
            "Preview is disabled or has no sink");
    std::shared_ptr<Section> value;
    {
      require(section < impl_->active.size(),
              "Preview section index out of range");
      const auto index = impl_->active[section];
      value = impl_->cache->sections[index];
      if (!value) {
        failure = impl_->cache->status[index].reason;
        return false;
      }
    }
    const auto submit_batch =
        [&](Batch &batch, std::uint32_t part, std::uint32_t draw,
            std::uint32_t texture_index,
            const std::array<float, 16> &object_to_section,
            const std::array<float, 3> &shape, const Vec3 &minimum,
            const Vec3 &maximum, NativePreparedMeshHandle &mesh,
            NativePreparedMaterialHandle &material_handle) {
          const auto &texture = value->textures[texture_index];
          NativeGeometryView view;
          view.interface_preview = true;
          view.semantic_kind = SemanticKind::ui;
          view.ordering =
              batch.surface == Surface::layer || batch.surface == Surface::cover
                  ? NativeOrdering::preserve
                  : NativeOrdering::reorderable;
          view.positions = batch.positions.data();
          view.position_groups =
              std::uint32_t(batch.positions.size() / batch.shape_count);
          view.normals = batch.normals.data();
          view.normal_groups = std::uint32_t(batch.normals.size());
          view.texture_coordinates = batch.uv.data();
          view.texture_coordinate_count = std::uint32_t(batch.uv.size());
          view.components = batch.components.data();
          view.component_count = std::uint32_t(batch.components.size());
          view.indices = batch.indices.data();
          view.index_count = std::uint32_t(batch.indices.size());
          view.object_to_clip = clip;
          const auto &resource = value->prepared_resource.identity;
          const auto revision = resource.container_incarnation;
          view.geometry_revision = {static_cast<std::uint32_t>(revision),
                                    static_cast<std::uint32_t>(revision >> 32),
                                    1, 0, 0};
          view.geometry_revision_valid = true;
          view.prepared_mesh = &mesh;
          view.prepared_material = &material_handle;
          view.prepared_identity = {-1 - std::int32_t(value->index),
                                    value->generation,
                                    value->generation,
                                    revision,
                                    draw,
                                    0,
                                    1,
                                    true,
                                    true};
          view.world.enabled = true;
          view.world.owner = &value->owner;
          view.world.identity = &resource;
          view.world.part = part;
          view.world.record_revision = revision;
          view.world.object_space = true;
          view.world.vertex_opacity = batch.vertex_opacity;
          view.world.object_to_world = object_to_section;
          view.world.world_to_clip = clip;
          view.world.shape_count = batch.shape_count;
          view.world.shape = shape;
          view.lighting.enabled = 1;
          view.lighting.object_to_world = object_to_section;
          view.lighting.material_diffuse = {1, 1, 1};
          view.lighting.material_alpha = batch.opacity;
          // The native mask decodes this ordinal from any RGB channel. Normal
          // lit output ignores default RGB; the existing raw-ID pass uses it.
          const auto code = std::uint32_t((section + 1) * 16 + 8);
          view.default_diffuse = 0xff000000u | code * 0x00010101u;
          view.contribution = {true, true, 0};
          // The owned cover caller supplies the authoritative native frame
          // state; it must not take a terrain material preset.
          if (batch.surface != Surface::cover)
            view.owned_fixed_state = material(batch.surface);
          view.owned_texture.identity = texture.identity;
          view.owned_texture.generation = texture.generation;
          view.owned_texture.resource = &texture.resource;
          view.owned_texture.value_source = 1;
          view.owned_texture.format = texture.format;
          view.owned_texture.mips = texture.mips.data();
          view.owned_texture.mip_count = std::uint32_t(texture.mips.size());
          view.owned_texture.opaque_mips = texture.opaque_mips;
          for (unsigned a = 0; a < 3; ++a) {
            view.bounds_minimum[a] = float(minimum[a]);
            view.bounds_maximum[a] = float(maximum[a]);
          }
          view.bounds_valid = true;
          NativeGeometryResult result;
          if (!sink(context, view, result)) {
            failure = result.failure_operation ? result.failure_operation
                                               : "Preview submission failed";
            return false;
          }
          return true;
        };
    if (!discovered) {
      if (pass == WorldMapPreviewPass::opaque)
        return true;
      const auto found = std::find_if(
          value->batches.begin(), value->batches.end(),
          [](const auto &batch) { return batch.surface == Surface::cover; });
      require(found != value->batches.end(),
              "Unknown map section cover is unavailable");
      const auto part = std::uint32_t(found - value->batches.begin());
      return submit_batch(*found, part, part, found->texture, identity,
                          {0, 0, 0}, found->minimum, found->maximum,
                          found->mesh, found->material);
    }
    for (std::uint32_t part = 0; part < value->batches.size(); ++part) {
      auto &batch = value->batches[part];
      if (pass != WorldMapPreviewPass::ordered && !batch.instanced &&
          batch.surface != Surface::layer && batch.surface != Surface::cover &&
          !submit_batch(batch, part, part, batch.texture, identity, {0, 0, 0},
                        batch.minimum, batch.maximum, batch.mesh,
                        batch.material))
        return false;
    }
    for (std::uint32_t i = 0;
         pass != WorldMapPreviewPass::ordered && i < value->instances.size();
         ++i) {
      auto &instance = value->instances[i];
      auto &batch = value->batches[instance.batch];
      Vec3 lo{}, hi{};
      for (unsigned a = 0; a < 3; ++a) {
        lo[a] = instance.bounds.minimum[a];
        hi[a] = instance.bounds.maximum[a];
      }
      if (!submit_batch(
              batch, instance.batch, std::uint32_t(value->batches.size()) + i,
              instance.texture, instance.object_to_section, instance.shape, lo,
              hi, instance.mesh, instance.material))
        return false;
    }
    // Ordered layers blend against the finished terrain and scenery.
    for (std::uint32_t part = 0; part < value->batches.size(); ++part) {
      auto &batch = value->batches[part];
      if (pass != WorldMapPreviewPass::opaque &&
          batch.surface == Surface::layer &&
          !submit_batch(batch, part, part, batch.texture, identity, {0, 0, 0},
                        batch.minimum, batch.maximum, batch.mesh,
                        batch.material))
        return false;
    }
    return true;
  } catch (const std::exception &e) {
    failure = e.what();
    return false;
  }
}

bool WorldMapPreview::submit_borders(std::size_t section,
                                     const std::array<float, 16> &clip,
                                     GameRibbonSubmitFn sink, void *context,
                                     std::string &failure) noexcept {
  try {
    require(enabled() && impl_->cache && sink,
            "Preview borders have no active owner");
    std::shared_ptr<Section> value;
    {
      require(section < impl_->active.size(),
              "Preview border section index out of range");
      value = impl_->cache->sections[impl_->active[section]];
    }
    require(bool(value), "Preview border geometry is unavailable");
    for (const auto &line : value->outlines) {
      if (line.size() < 2)
        continue;
      GameRibbonView view;
      view.points = line.data();
      view.count = std::uint32_t(line.size());
      view.object_to_clip = clip;
      view.interface_pass = true;
      if (!sink(context, view)) {
        failure = "World map section border draw failed";
        return false;
      }
    }
    return true;
  } catch (const std::exception &error) {
    failure = error.what();
    return false;
  }
}
} // namespace owned
