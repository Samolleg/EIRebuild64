#pragma once
#include "astral_rift.h"
#include "retained_static_submission.h"

namespace owned {
// The stream's nodes used to stop at the authored FIGs. Complete that view
// from the same immutable assembly used on the active map, before prebuild.
inline bool prepare_neighbour_astral_nodes(RetainedStaticSourceCandidate& candidate) {
    if (!candidate.assembly || !astral_model(*candidate.assembly)) return true;
    const auto& a = *candidate.assembly;
    const auto original = candidate.nodes.size();
    if (original > a.parts.size()) return false;
    for (std::size_t i = original; i < a.parts.size(); ++i) {
        const auto& part = a.parts[i];
        std::size_t source = 0;
        while (source < original && (!astral_part(a, a.parts[source]) ||
            a.parts[source].skeleton_node != part.skeleton_node)) ++source;
        if (source == original || !astral_clumps(a, part) || !part.geometry) return false;
        auto node = candidate.nodes[source];
        node.figure = part.geometry.get();
        node.positions = part.geometry->positions;
        node.normals = part.geometry->normals;
        candidate.nodes.push_back(std::move(node));
    }
    for (std::size_t i = 0; i < a.parts.size(); ++i) {
        auto& node = candidate.nodes[i];
        const auto& part = a.parts[i];
        if (!astral_dynamic(a, part)) continue;
        const float margin = astral_clumps(a, part) ? 7.f : .5f;
        for (unsigned axis = 0; axis < 3; ++axis) {
            node.bounds.minimum[axis] -= margin;
            node.bounds.maximum[axis] += margin;
        }
        node.bounds.radius += margin;
    }
    return true;
}
// Use the same phase and border-repair selector on both sides of a map edge.
inline bool neighbour_astral_values(double time, const NativePreparedPartValues& base,
    bool border, NativePreparedPartValues& values) {
    if (!std::isfinite(time) || time < 0) return false;
    values = base;
    values.astral[0] = float(std::fmod(time * .055 / 6., 1.));
    values.astral[3] = border ? 1.f : 0.f;
    return true;
}
}
