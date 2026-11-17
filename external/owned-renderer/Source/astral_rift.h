#pragma once
// Astral rift effects (the Dejavu samastrift01-04 rifts and the
// samastrborder01 boundary): the named veil parts of those models get a
// flowing texture and a gentle sway, and each veil ribbon sheds crossed
// energy clumps that drift away and fade. The parts are selected by exact
// model and part name, never by texture: unrelated models share the atlas.
//
// The clump geometry is built once per assembly, here, from the veil's own
// rest geometry; its motion and fade run in the vertex and pixel stages
// (native_shader.h nativeAstralVertex/nativeAstralSample) on the game clock.
// No game objects, collision or damage are involved.
//
// Per record: the astral field of the instance flags (native_instance_flags.h
// native_astral_flags: kind 1 veil / 2 clumps, veil index); batches of such
// records draw with the astral programs only.
#include "retained_static_scene.h"
#include "native_instance_flags.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace owned {

// The veils' strip of the shared samastrtex01 atlas: U range, and the V
// origin of the clump cell below it (one strip width square).
inline constexpr float kAstralU0 = .0166015625f;
inline constexpr float kAstralU1 = .4833984375f;
inline constexpr float kAstralClumpV0 = .5166015625f;
inline constexpr float kAstralClumpV1 = .9833984375f;
inline constexpr unsigned kAstralClumpsPerRibbon = 8;
inline constexpr float kAstralVeilMargin = .5f;    // sway reach (model units)
inline constexpr float kAstralClumpMargin = 7.f;   // clump drift reach

// ASCII case-insensitive equality; `name` may carry a ".mod" suffix.
inline bool astral_name_is(const std::string& name, const char* wanted, bool model) noexcept {
    std::size_t length = name.size();
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; };
    if (model && length > 4 && name[length - 4] == '.' && lower(name[length - 3]) == 'm' &&
        lower(name[length - 2]) == 'o' && lower(name[length - 1]) == 'd') length -= 4;
    std::size_t i = 0;
    for (; i < length && wanted[i]; ++i) if (lower(name[i]) != wanted[i]) return false;
    return i == length && !wanted[i];
}

// 0: not an astral model; 1: samastrift01; 2: samastrift02-04; 3: samastrborder01.
inline unsigned astral_model_kind(const VisualAssemblyAsset& assembly) noexcept {
    if (!assembly.skeleton) return 0;
    const auto& name = assembly.skeleton->visual_reference;
    if (astral_name_is(name, "samastrift01", true)) return 1;
    if (astral_name_is(name, "samastrift02", true) || astral_name_is(name, "samastrift03", true) ||
        astral_name_is(name, "samastrift04", true)) return 2;
    if (astral_name_is(name, "samastrborder01", true)) return 3;
    return 0;
}
inline bool astral_model(const VisualAssemblyAsset& assembly) noexcept { return astral_model_kind(assembly) != 0; }

// The veil index (1-4) of an animated part of an astral model, else 0.
// Stones, shore, cores, EMPTY_AstralDepth and EMPTY_AstralFilaments keep
// their ordinary drawing although they use the same atlas.
inline unsigned astral_veil(const VisualAssemblyAsset& assembly, const VisualPartAsset& part) noexcept {
    switch (astral_model_kind(assembly)) {
    case 1: {
        static const char* const veils[] = {"empty_veillow", "empty_veilcrown", "empty_veilcross", "empty_veilwake"};
        for (unsigned i = 0; i < 4; ++i) if (astral_name_is(part.name, veils[i], false)) return i + 1;
        return 0;
    }
    case 2: return astral_name_is(part.name, "empty_veils", false) ? 1u : 0u;
    case 3: return astral_name_is(part.name, "empty_boundaryveils", false) ? 1u : 0u;
    default: return 0;
    }
}

// The generated clump part of veil n is named "astral_clumps_<n>".
inline unsigned astral_clumps(const VisualAssemblyAsset& assembly, const VisualPartAsset& part) noexcept {
    if (part.name.size() != 15 || part.name.compare(0, 14, "astral_clumps_") != 0) return 0;
    const char digit = part.name[14];
    return digit >= '1' && digit <= '4' && astral_model(assembly) ? unsigned(digit - '0') : 0u;
}

// 1 veil, 2 clumps, 0 neither: NativeGeometryView::astral[0].
inline unsigned astral_kind(const VisualAssemblyAsset& assembly, const VisualPartAsset& part) noexcept {
    if (astral_veil(assembly, part)) return 1;
    return astral_clumps(assembly, part) ? 2u : 0u;
}

// The visibility margin an animated part needs beyond its rest geometry.
inline float astral_margin(const VisualAssemblyAsset& assembly, const VisualPartAsset& part) noexcept {
    switch (astral_kind(assembly, part)) {
    case 1: return kAstralVeilMargin;
    case 2: return kAstralClumpMargin;
    default: return 0.f;
    }
}

// The instance flag bits of one part (0: not an astral part).
inline std::uint32_t astral_record_flags(const VisualAssemblyAsset& assembly, const VisualPartAsset& part) noexcept {
    const unsigned kind = astral_kind(assembly, part);
    if (!kind) return 0;
    return native_astral_flags(kind, kind == 1 ? astral_veil(assembly, part) : astral_clumps(assembly, part));
}

// The connected ribbons of one veil FIG, by shared position indices.
// samastrift02-04 and the border put several disconnected ribbons in one
// FIG, and those reuse one stretch of the atlas, so UV cannot tell them
// apart. samastrift01's four named parts are one ribbon each.
inline std::vector<std::vector<const NativeVertexComponent*>> astral_ribbons(
    const FigureCpuAsset& figure, bool split) {
    std::vector<std::vector<const NativeVertexComponent*>> ribbons;
    if (!split) {
        ribbons.emplace_back();
        for (const auto& component : figure.components) ribbons.back().push_back(&component);
        return ribbons;
    }
    const std::size_t count = figure.positions.size() * 4;
    std::vector<std::size_t> parent(count);
    for (std::size_t i = 0; i < count; ++i) parent[i] = i;
    const auto root = [&](std::size_t i) {
        while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
        return i;
    };
    for (std::size_t t = 0; t + 2 < figure.indices.size(); t += 3) {
        std::size_t corner[3];
        bool valid = true;
        for (unsigned k = 0; k < 3 && valid; ++k) {
            const auto c = figure.indices[t + k];
            valid = c < figure.components.size() && figure.components[c].position < count;
            if (valid) corner[k] = figure.components[c].position;
        }
        if (!valid) continue;
        parent[root(corner[1])] = root(corner[0]);
        parent[root(corner[2])] = root(corner[0]);
    }
    std::vector<std::size_t> ribbon_of(count, SIZE_MAX);
    for (const auto index : figure.indices) {
        if (index >= figure.components.size()) continue;
        const auto& component = figure.components[index];
        if (component.position >= count || component.texture >= figure.texture_coordinates.size()) continue;
        auto& ribbon = ribbon_of[root(component.position)];
        if (ribbon == SIZE_MAX) { ribbon = ribbons.size(); ribbons.emplace_back(); }
        ribbons[ribbon].push_back(&component);
    }
    return ribbons;
}

// Adds the clump part of every veil (once: an assembly that has them is left
// alone) and takes the veils and clumps out of shadow casting. Each clump is
// two crossed double-sided quads around a veil vertex picked along the
// strip; its number is packed into U (2 per clump: id*2 + 0..1), which the
// vertex stage turns back into the clump cell's atlas UV. The clump part
// shares the veil's node, material, texture and hidden state, and goes with
// the assembly.
inline void add_astral_clumps(VisualAssemblyAsset& assembly) {
    const unsigned model = astral_model_kind(assembly);
    if (!model) return;
    for (const auto& part : assembly.parts) if (astral_clumps(assembly, part)) return;
    const std::size_t authored = assembly.parts.size();
    for (std::size_t index = 0; index < authored; ++index) {
        const unsigned veil = astral_veil(assembly, assembly.parts[index]);
        if (!veil) continue;
        assembly.parts[index].casts_shadow = false;
        const VisualPartAsset source = assembly.parts[index];
        if (!source.geometry || source.geometry->shape_count != 1) continue;
        const auto& figure = *source.geometry;
        auto clumps = std::make_shared<FigureCpuAsset>();
        clumps->identity = figure.identity;
        clumps->identity.canonical_name += "/astral-clumps-v1";
        clumps->signature = figure.signature;
        clumps->shape_count = 1;
        clumps->material_group = figure.material_group;
        clumps->texture_number = figure.texture_number;
        clumps->bounds = figure.bounds;
        const auto ribbons = astral_ribbons(figure, model != 1);
        // 8 vertices per clump: 1024 ribbons stay far below the uint16 index limit.
        for (std::size_t ribbon = 0; ribbon < ribbons.size() && ribbon < 1024; ++ribbon)
            for (unsigned k = 0; k < kAstralClumpsPerRibbon; ++k) {
                // Evenly along the strip, alternating near its two edges.
                const float wanted_u = kAstralU0 + (kAstralU1 - kAstralU0) * float(k + 1) / float(kAstralClumpsPerRibbon + 1);
                const float wanted_v = (k & 1u) ? kAstralClumpV0 : kAstralClumpV1;
                const NativeVertexComponent* chosen = nullptr;
                float best = 1e30f;
                for (const auto* component : ribbons[ribbon]) {
                    if (component->texture >= figure.texture_coordinates.size() ||
                        component->position / 4u >= figure.positions.size()) continue;
                    const auto& uv = figure.texture_coordinates[component->texture];
                    const float score = std::abs(uv.u - wanted_u) + .2f * std::abs(uv.v - wanted_v);
                    if (score < best) { best = score; chosen = component; }
                }
                if (!chosen) continue;
                const auto& group = figure.positions[chosen->position / 4u];
                const unsigned lane = chosen->position % 4u;
                const float centre[3] = {group.x[lane], group.y[lane], group.z[lane]};
                const float id = float((ribbon * kAstralClumpsPerRibbon + k) * 2);
                for (unsigned plane = 0; plane < 2; ++plane) {
                    const auto first = static_cast<std::uint16_t>(clumps->components.size());
                    NativePackedGroup positions{}, normals{};
                    for (unsigned v = 0; v < 4; ++v) {
                        const float x = (v == 1 || v == 2) ? 1.f : 0.f, y = v >= 2 ? 1.f : 0.f;
                        positions.x[v] = centre[0] + (x - .5f) * 1.4f;
                        positions.y[v] = centre[1] + (plane == 0 ? (y - .5f) * .6f : 0.f);
                        positions.z[v] = centre[2] + (plane == 1 ? (y - .5f) * .6f : 0.f);
                        positions.w[v] = 1.f;
                        normals.z[v] = 1.f;
                        const auto vertex = static_cast<std::uint16_t>(first + v);
                        clumps->components.push_back({vertex, vertex, vertex});
                        clumps->texture_coordinates.push_back({id + x, y});
                        clumps->shape_position_radii[0] = (std::max)(clumps->shape_position_radii[0],
                            std::sqrt(positions.x[v] * positions.x[v] + positions.y[v] * positions.y[v] +
                                      positions.z[v] * positions.z[v]));
                    }
                    clumps->positions.push_back(positions);
                    clumps->normals.push_back(normals);
                    // Both windings: the quads are seen from both sides.
                    for (unsigned v : {0u, 1u, 2u, 0u, 2u, 3u, 2u, 1u, 0u, 3u, 2u, 0u})
                        clumps->indices.push_back(static_cast<std::uint16_t>(first + v));
                }
            }
        if (clumps->indices.empty()) continue;
        clumps->position_groups = static_cast<std::uint32_t>(clumps->positions.size());
        clumps->normal_groups = clumps->position_groups;
        VisualPartAsset part = source;
        part.name = "astral_clumps_" + std::to_string(veil);
        part.geometry = std::move(clumps);
        part.casts_shadow = false;
        assembly.parts.push_back(std::move(part));
    }
}

} // namespace owned
