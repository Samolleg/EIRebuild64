#pragma once
#include "retained_static_submission.h"
#include <cmath>

namespace owned {

struct ModelPlacementFacts {
    std::array<float,3> position{};
    std::array<float,4> quaternion{1,0,0,0};
    std::array<float,3> scale{1,1,1};
};
// Same current-object matrix as the native c74 adapter. Only value inputs;
// constructing a UI transform never changes the source object's placement.
inline bool prepare_model_placement(const ModelPlacementFacts& facts,
    std::array<float,16>& matrix) noexcept {
    if (!retained_object_matrix(facts.position, facts.quaternion, matrix)) return false;
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 3; ++column) {
            matrix[row*4+column] *= facts.scale[column];
            if (!std::isfinite(matrix[row*4+column])) return false;
        }
    return true;
}

// Synchronous per-view overrides. They belong to the UI invocation, never to
// a world pose, FIG flags, lighting cache or the immutable assembly. The id
// prevents an unrelated object drawn by a nested callback inheriting a view.
struct ModelPreviewPresentation {
    std::int32_t id = 0;
    ModelPlacementFacts placement{};
    bool placed = false, full_ambient = false, default_fog = false;
    bool default_colors = false;
    std::uint32_t diffuse = 0xffffffffu, specular = 0xff000000u;
};
inline thread_local const ModelPreviewPresentation* model_preview_presentation = nullptr;
class ModelPreviewPresentationScope {
    ModelPreviewPresentation value_;
    const ModelPreviewPresentation* previous_;
public:
    explicit ModelPreviewPresentationScope(const ModelPreviewPresentation& value) noexcept
        : value_(value), previous_(model_preview_presentation) {
        model_preview_presentation = &value_;
    }
    ~ModelPreviewPresentationScope() { model_preview_presentation = previous_; }
    ModelPreviewPresentationScope(const ModelPreviewPresentationScope&) = delete;
    ModelPreviewPresentationScope& operator=(const ModelPreviewPresentationScope&) = delete;
};
inline const ModelPreviewPresentation* model_preview_for(std::int32_t id) noexcept {
    return model_preview_presentation && model_preview_presentation->id == id
        ? model_preview_presentation : nullptr;
}

// c74's native part9 glow: store the wrapped float phase, but quantize from
// the unrounded wrapped value. The adapter supplies its admitted palette and
// frame duration; this does not read or retain an item or consume randomness.
inline bool advance_model_preview_glow(float& phase, float elapsed,
    const std::array<float,3>& palette, std::uint32_t& rgb) noexcept {
    if (!std::isfinite(phase) || !std::isfinite(elapsed)) return false;
    for (float channel : palette)
        if (!std::isfinite(channel) || channel < 0 || channel > 1) return false;
    const double next = std::fmod(double(phase) + double(elapsed)*5.0,
        2.0*3.141592653589793);
    const double strength = (std::sin(next)+1.0)*double(.15f);
    phase = float(next);
    rgb = (std::uint32_t(strength*palette[0]*255.0)<<16) |
        (std::uint32_t(strength*palette[1]*255.0)<<8) |
        std::uint32_t(strength*palette[2]*255.0);
    return true;
}
} // namespace owned
