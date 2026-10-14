#pragma once
// Production snow geometry, shared with the focused mesh regression.
#include "ground_cover_wood.h"

namespace owned::ground_cover {
inline GcMesh make_mound(std::uint32_t seed) {
    GcMesh m;
    // The smooth vertex normals carry the shading (piles are many: keep the
    // vertex count of the original mesh).
    const int rings = 14, sectors = 32;
    const float c[4]{0.95f, 0.97f, 1.0f, 0.0f};
    add(m, {0, 0, 0.32f}, {0, 0, 1}, c, 0, 0, 1.05f, 0);
    const float phase = (mesh_hash(seed) & 0xffffu) * (6.2831853f / 65536.f);
    // A soft drift: (1 - r^2)^2 meets the ground with zero slope (no crease
    // at the base), fuller on its lee side and gently lumpy, never a cap.
    for (int r = 1; r <= rings; ++r) {
        const float rr = float(r) / rings;
        for (int s = 0; s < sectors; ++s) {
            const float a = s * 6.283f / sectors;
            const float wob = 1.f + .11f * std::sin(2*a + phase) + .06f * std::sin(3*a - phase);
            const float fall = std::max(0.0f, 1 - rr * rr);
            const float shape = 1 + .22f * rr * std::cos(a) + .1f * rr * rr * std::sin(2*a - phase);
            const float z = 0.32f * fall * fall * shape - (r == rings ? 0.01f : 0);
            const float slope = 0.32f * 4 * rr * fall * shape / wob;
            add(m, {std::cos(a) * rr * wob, std::sin(a) * rr * wob, z}, unit3({std::cos(a) * slope, std::sin(a) * slope, 1}), c, rr, 0,
                1.0f + 0.05f * (1 - rr), 0);
        }
    }
    for (int s = 0; s < sectors; ++s) tri(m, 0, 1 + (s + 1) % sectors, 1 + s);
    for (int r = 1; r < rings; ++r)
        for (int s = 0; s < sectors; ++s) {
            const std::uint32_t a = 1 + (r - 1) * sectors + s, b = 1 + (r - 1) * sectors + (s + 1) % sectors;
            tri(m, a, b, a + sectors); tri(m, b, b + sectors, a + sectors);
        }
    return m;
}
} // namespace owned::ground_cover
