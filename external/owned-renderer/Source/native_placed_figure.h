#pragma once
#include "engine_bridge.h"
#include <algorithm>
#include <cmath>

namespace owned {
// Borrowed source streams. Both frontends use this producer before native GPU
// submission; positions stay packed and morphs stay separate until the shader.
struct NativeFigureStreams {
    const NativePackedGroup* positions = nullptr;
    std::uint32_t position_groups = 0;
    const NativePackedGroup* normals = nullptr;
    std::uint32_t normal_groups = 0;
    const std::uint32_t* diffuse = nullptr;
    const std::uint32_t* specular = nullptr;
    const NativeTextureCoordinate* coordinates = nullptr;
    std::uint32_t coordinate_count = 0;
    const NativeVertexComponent* components = nullptr;
    std::uint32_t component_count = 0;
    const std::uint16_t* indices = nullptr;
    std::uint32_t index_count = 0;
};
inline void apply_native_figure_streams(NativeGeometryView& view,
    const NativeFigureStreams& input) noexcept {
    view.positions = input.positions; view.position_groups = input.position_groups;
    view.normals = input.normals; view.normal_groups = input.normal_groups;
    view.diffuse = input.diffuse; view.specular = input.specular;
    view.texture_coordinates = input.coordinates;
    view.texture_coordinate_count = input.coordinate_count;
    view.components = input.components; view.component_count = input.component_count;
    view.indices = input.indices; view.index_count = input.index_count;
}

// Raw material values, without the legacy CPU light/material multiplication.
// The c74 native shader owns that response for proxy and embedded models.
inline bool make_native_figure_lighting(const float* diffuse, const float* emissive,
    const float* specular, const std::array<float,16>& object_to_world,
    NativeLighting& output) noexcept {
    for (unsigned i = 0; i < 4; ++i)
        if (!std::isfinite(diffuse[i]) || !std::isfinite(emissive[i]) ||
            !std::isfinite(specular[i])) return false;
    for (float value : object_to_world) if (!std::isfinite(value)) return false;
    output = {};
    output.enabled = 1;
    output.material_alpha = (std::max)(0.f, (std::min)(1.f, diffuse[3]));
    std::copy_n(diffuse, 3, output.material_diffuse.begin());
    std::copy_n(emissive, 4, output.emissive.begin());
    std::copy_n(specular, 4, output.specular.begin());
    output.specular[3] = 1.f;
    output.object_to_world = object_to_world;
    return true;
}
// The placed-figure hook and embedded source adapter share final GPU state.
// Geometry groups and current morph deltas are borrowed synchronously; neither
// caller expands vertices or applies morphs on the CPU.
inline void apply_native_placed_state(NativeGeometryView& view,
    const NativeMorphOffset* morphs, std::uint32_t count,
    const NativeLighting& lighting, NativeOrdering ordering,
    bool default_colors, std::uint32_t diffuse, std::uint32_t specular) noexcept {
    view.morphs = morphs; view.morph_count = count;
    view.lighting = lighting; view.ordering = ordering;
    view.default_specular = std::uint32_t(view.fog) << 24;
    if (default_colors) {
        view.flags |= 4u;
        view.default_diffuse = diffuse; view.default_specular = specular;
    }
}
}
