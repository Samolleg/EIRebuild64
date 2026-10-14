#pragma once
// Production procedural wood geometry, shared with the focused mesh checks.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace owned::ground_cover {
struct V3 { float x, y, z; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross3(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 unit3(V3 a) { const float l = std::sqrt(dot3(a, a)); return l > 1e-12f ? a * (1 / l) : V3{0, 0, 1}; }

struct GcVertex { float p[3], n[3], c[4], uv[2], ex[2]; };
static_assert(sizeof(GcVertex) == 56, "GcVertex must match the HLSL layout");
struct GcMesh { std::vector<GcVertex> v; std::vector<std::uint32_t> i; };

inline std::uint32_t mesh_hash(std::uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
struct Rng {
    std::uint32_t s;
    explicit Rng(std::uint32_t seed) : s(mesh_hash(seed) | 1u) {}
    float f() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s >> 8) * (1.0f / 16777216.0f); }
    float r(float a, float b) { return a + (b - a) * f(); }
};

inline void add(GcMesh& m, V3 p, V3 n, const float c[4], float u, float v, float ao, float wind) {
    GcVertex x{{p.x, p.y, p.z}, {n.x, n.y, n.z}, {c[0], c[1], c[2], c[3]}, {u, v}, {ao, wind}};
    m.v.push_back(x);
}
inline void tri(GcMesh& m, std::uint32_t a, std::uint32_t b, std::uint32_t c) { m.i.push_back(a); m.i.push_back(b); m.i.push_back(c); }


// Prism along a bent polyline (twigs, logs, stems). Caps carry end-grain UVs (offset 10).
inline void add_prism(GcMesh& m, const std::vector<V3>& pts, float r0, float r1, int sides, V3 col, float mix, bool caps, V3 cap_col) {
    const auto base = std::uint32_t(m.v.size());
    float along = 0;
    const float c[4]{col.x, col.y, col.z, mix};
    for (std::size_t s = 0; s < pts.size(); ++s) {
        if (s) along += std::sqrt(dot3(pts[s] - pts[s - 1], pts[s] - pts[s - 1]));
        const V3 d = unit3(pts[std::min(s + 1, pts.size() - 1)] - pts[s ? s - 1 : 0]);
        V3 cr = cross3(d, {0, 0, 1});
        if (dot3(cr, cr) < 1e-6f) cr = cross3(d, {1, 0, 0});
        const V3 side = unit3(cr), up = cross3(side, d);
        const float rr = r0 + (r1 - r0) * float(s) / float(pts.size() - 1);
        // Duplicate the seam so every triangle samples a continuous bark
        // interval, including the closing face (U=1 rather than wrapping to 0).
        for (int k = 0; k <= sides; ++k) {
            const float a = (k % sides) * 6.283f / sides;
            const V3 n = side * std::cos(a) + up * std::sin(a);
            add(m, pts[s] + n * rr, n, c, float(k) / sides, along * 1.5f, 0.65f + 0.45f * (n.z * 0.5f + 0.5f), 0);
        }
    }
    for (std::size_t s = 0; s + 1 < pts.size(); ++s)
        for (int k = 0; k < sides; ++k) {
            const std::uint32_t stride = sides + 1;
            const std::uint32_t a = base + std::uint32_t(s * stride + k), b = a + 1;
            tri(m, a, b, a + stride); tri(m, b, b + stride, a + stride);
        }
    if (!caps) return;
    const float cc[4]{cap_col.x, cap_col.y, cap_col.z, mix};
    for (int end = 0; end < 2; ++end) {
        const std::size_t s = end ? pts.size() - 1 : 0;
        V3 d = unit3(pts[end ? s : 1] - pts[end ? s - 1 : 0]);
        if (!end) d = d * -1;
        const auto center = std::uint32_t(m.v.size());
        add(m, pts[s], d, cc, 10.5f, 10.5f, 1.0f, 0);
        for (int k = 0; k < sides; ++k) {
            GcVertex v = m.v[base + s * (sides + 1) + k];
            v.n[0] = d.x; v.n[1] = d.y; v.n[2] = d.z;
            v.c[0] = cap_col.x * 0.85f; v.c[1] = cap_col.y * 0.85f; v.c[2] = cap_col.z * 0.85f;
            const float a = k * 6.283f / sides;
            v.uv[0] = 10.5f + 0.48f * std::cos(a); v.uv[1] = 10.5f + 0.48f * std::sin(a);
            m.v.push_back(v);
        }
        for (int k = 0; k < sides; ++k) tri(m, center, center + 1 + k, center + 1 + (k + 1) % sides);
    }
}

inline GcMesh make_twig(std::uint32_t seed) {
    GcMesh m; Rng r(seed);
    std::vector<V3> pts;
    for (int s = 0; s <= 4; ++s) pts.push_back({s / 4.0f - 0.5f, r.r(-0.04f, 0.04f), 0.03f});
    add_prism(m, pts, 0.018f, 0.007f, 5, {0.34f, 0.25f, 0.16f}, 0.92f, false, {});
    add_prism(m, {{0.05f, 0, 0.03f}, {0.2f, 0.12f, 0.035f}, {0.3f, 0.2f, 0.03f}}, 0.009f, 0.003f, 4, {0.34f, 0.25f, 0.16f}, 0.92f, false, {});
    return m;
}
inline GcMesh make_twig_fork(std::uint32_t seed) {
    GcMesh m; Rng r(seed);
    const V3 bark{0.3f, 0.23f, 0.15f};
    std::vector<V3> main;
    for (int s = 0; s <= 5; ++s) { const float t = s / 5.0f - 0.5f; main.push_back({t, 0.12f * std::sin(t * 3.0f) + r.r(-0.02f, 0.02f), 0.025f}); }
    add_prism(m, main, 0.017f, 0.006f, 5, bark, 0.92f, false, {});
    add_prism(m, {{0.0f, 0.1f, 0.025f}, {0.18f, 0.25f, 0.028f}, {0.28f, 0.28f, 0.025f}}, 0.009f, 0.003f, 4, bark, 0.92f, false, {});
    add_prism(m, {{-0.2f, -0.08f, 0.025f}, {-0.3f, -0.2f, 0.027f}}, 0.007f, 0.002f, 4, bark, 0.92f, false, {});
    return m;
}
// Broken stick: two pieces at an angle, pale splintered break faces.
inline GcMesh make_twig_broken() {
    GcMesh m;
    const V3 bark{0.33f, 0.25f, 0.17f}, wood{0.72f, 0.6f, 0.42f};
    add_prism(m, {{-0.5f, 0, 0.018f}, {-0.2f, 0.02f, 0.018f}, {0.02f, 0, 0.018f}}, 0.017f, 0.012f, 6, bark, 0.92f, true, wood);
    add_prism(m, {{0.08f, 0.04f, 0.014f}, {0.25f, 0.16f, 0.014f}, {0.4f, 0.24f, 0.012f}}, 0.013f, 0.006f, 6, bark, 0.92f, true, wood);
    add_prism(m, {{-0.25f, 0.02f, 0.018f}, {-0.13f, -.08f, 0.02f}, {-0.08f, -.15f, 0.015f}}, 0.008f, 0.002f, 4, bark, 0.92f, false, {});
    return m;
}
inline GcMesh make_log() {
    GcMesh m;
    std::vector<V3> pts;
    for (int s = 0; s <= 3; ++s) { const float t = s / 3.0f - 0.5f; pts.push_back({t, 0.02f * std::sin(t * 5), 0.09f}); }
    add_prism(m, pts, 0.1f, 0.09f, 8, {0.36f, 0.27f, 0.18f}, 0.92f, true, {0.66f, 0.52f, 0.34f});
    add_prism(m, {{0.1f, 0.05f, 0.12f}, {0.2f, 0.18f, 0.16f}}, 0.025f, 0.015f, 4, {0.36f, 0.27f, 0.18f}, 0.92f, false, {});
    return m;
}

inline void branch(GcMesh& m, Rng& r, V3 a, V3 dir, float len, float rad, int depth) {
    const V3 mid{a.x + dir.x * len * 0.5f + r.r(-0.04f, 0.04f), a.y + dir.y * len * 0.5f + r.r(-0.04f, 0.04f), a.z + dir.z * len * 0.5f};
    const V3 b = a + dir * len;
    add_prism(m, {a, mid, b}, rad, rad * 0.6f, 4, {0.3f, 0.25f, 0.2f}, 0.92f, false, {});
    if (depth == 0) return;
    const int kids = depth == 2 ? 4 : 2;
    for (int k = 0; k < kids; ++k) {
        const float yaw = r.r(0, 6.283f), up = r.r(0.35f, 0.9f);
        const V3 d = unit3({dir.x * 0.6f + std::cos(yaw) * (1 - up), dir.y * 0.6f + std::sin(yaw) * (1 - up), dir.z * 0.6f + up});
        const float t = r.r(0.5f, 1.0f);
        branch(m, r, a + (b - a) * t, d, len * r.r(0.5f, 0.75f), rad * 0.6f, depth - 1);
    }
}
inline GcMesh make_dead_bush(std::uint32_t seed) {
    GcMesh m; Rng r(seed);
    for (int k = 0; k < 5; ++k) {
        const float yaw = k * 1.256f + r.r(-0.3f, 0.3f), up = r.r(0.45f, 0.8f);
        branch(m, r, {0, 0, 0}, unit3({std::cos(yaw) * (1 - up), std::sin(yaw) * (1 - up), up}), r.r(0.45f, 0.7f), 0.03f, 2);
    }
    // Height-weighted bending keeps the roots planted while the branches
    // share the grass wind and recovering footprint deformation.
    for (auto& vertex : m.v) vertex.ex[1] = std::max(vertex.p[2], 0.0f);
    return m;
}
} // namespace owned::ground_cover
