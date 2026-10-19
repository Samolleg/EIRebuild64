#pragma once
#include "owned_visual_evaluation.h"
#include "retained_static_scene.h"
#include <array>
#include <cmath>
#include <memory>

namespace owned {

enum class ModelPreviewClock : unsigned { game, inventory, display, count };
struct ModelPreviewView {
    VisualResourceIncarnation resource{};
    VisualClipSelection selection;
    std::shared_ptr<VisualPreparedResources> prepared;
    std::uint64_t selection_revision = 0, last_frame = 0;
    double anchor_time = 0, anchor_phase = 0, last_time = 0, rate = 1;
    bool active = false;
};
struct ModelPreviewViews {
    std::array<ModelPreviewView, static_cast<unsigned>(ModelPreviewClock::count)> views;
    ModelPreviewView& at(ModelPreviewClock clock) { return views[static_cast<unsigned>(clock)]; }
};

// c74's view anchor owner, shared by native previews and typed source UI.
// Retain the selected clip only: never Step the game controller, publish a world
// pose, consume an animation queue or execute gameplay callbacks here.
inline bool prepare_model_preview_phase(ModelPreviewView& view,
    RetainedVisualSample& sample, std::uint64_t revision, std::uint64_t frame,
    double now, double phase, double rate) {
    if (!std::isfinite(now) || !std::isfinite(phase) || phase < 0 ||
        !std::isfinite(rate) || rate < 0) return false;
    const auto& resource = sample.resource;
    const bool same_clip = view.selection.database == sample.selection.database &&
        view.selection.clip == sample.selection.clip && view.selection.ordinal == sample.selection.ordinal &&
        view.selection.neutral == sample.selection.neutral;
    const bool same_resource = view.resource.signed_id == resource.signed_id &&
        view.resource.serial == resource.serial;
    const bool continuous = view.last_frame == frame || view.last_frame + 1 == frame;
    const bool reset = !view.active || !same_resource || !same_clip ||
        view.selection_revision != revision || view.rate != rate || !continuous || now < view.last_time;
    const double anchor_time = reset ? now : view.anchor_time;
    const double anchor_phase = reset ? phase : view.anchor_phase;
    double result = 0;
    if (!sample_visual_preview_phase(sample.selection, anchor_phase,
            (now - anchor_time) * rate, result)) return false;
    // Commit only a complete sample; a rejected view does not become an anchor.
    if (!same_resource || !view.prepared) view.prepared = std::make_shared<VisualPreparedResources>();
    view.resource = resource; view.selection = sample.selection; view.selection_revision = revision;
    view.anchor_time = anchor_time; view.anchor_phase = anchor_phase;
    view.last_frame = frame; view.last_time = now; view.rate = rate; view.active = true;
    sample.prepared = view.prepared;
    sample.pose.phase = result;
    return true;
}
} // namespace owned
