#pragma once
#include "model_preview_clock.h"
#include "model_preview_presentation.h"

namespace OwnedRender {
using owned::ModelPreviewClock;
using owned::ModelPlacementFacts;

struct ModelPreviewTime {
    ModelPreviewClock clock = ModelPreviewClock::game;
    double now = 0;
    bool supplied = false;
};
inline thread_local ModelPreviewTime model_preview_time;
// The source wrapper supplies its actual completed SetExactPosition time.
// Nested UI calls restore their caller's clock; no borrowed game data survives.
class ModelPreviewTimeScope {
    ModelPreviewTime previous_;
public:
    ModelPreviewTimeScope(ModelPreviewClock clock, double now) noexcept
        : previous_(model_preview_time) { model_preview_time = {clock, now, true}; }
    ~ModelPreviewTimeScope() { model_preview_time = previous_; }
    ModelPreviewTimeScope(const ModelPreviewTimeScope&) = delete;
    ModelPreviewTimeScope& operator=(const ModelPreviewTimeScope&) = delete;
};

// Synchronous per-view overrides. They belong to the UI invocation, never to
// a world pose, FIG flags, lighting cache or the immutable assembly. The id
// prevents an unrelated object drawn by a nested callback inheriting a view.
struct ModelPreviewPresentation {
    std::int32_t id = 0;
    ModelPlacementFacts placement{};
    float physics_yaw = 0; // inventory turntable, radians in upright model space
    bool placed = false, default_fog = false;
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

} // namespace OwnedRender
