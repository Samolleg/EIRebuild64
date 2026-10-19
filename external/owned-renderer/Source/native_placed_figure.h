#pragma once
#include "engine_bridge.h"

namespace owned {
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