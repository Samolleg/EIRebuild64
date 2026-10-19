#pragma once
#include "engine_bridge.h"

namespace owned {

// Completed caller material facts, shared by synchronous and queued retail
// previews and the embedded packed-figure producer. No game pointers here.
struct ModelPreviewMaterial {
    bool default_colors = false;
    bool specular = false;
    std::uint32_t diffuse = 0;
    std::uint32_t secondary = 0;
    NativeLighting lighting{};
    NativeOrdering ordering = NativeOrdering::derive_from_state;
};
inline void apply_model_preview_material(NativeGeometryView& view,
    const ModelPreviewMaterial& material) noexcept {
    view.flags = (view.flags & ~12u) | (material.default_colors ? 4u : 0u) |
        (material.specular ? 8u : 0u);
    view.default_diffuse = material.diffuse;
    view.default_specular = material.secondary;
    view.lighting = material.lighting;
    view.owned_fixed_state.specular_enable = material.specular ? TRUE : FALSE;
    view.ordering = material.ordering;
}

} // namespace owned
