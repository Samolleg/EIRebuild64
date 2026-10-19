#pragma once
#include "retained_static_submission.h"

namespace owned {
// The preview has its own prepared pose; it never borrows the world's mutable
// GPU pose. Both frontends pass immutable assembly/selection and caller state.
// The returned view borrows sample.prepared until synchronous submission ends.
inline bool prepare_model_preview_geometry(const RetainedVisualSample& sample,
    std::size_t ordinal, NativeWorldGeometry::Pose& pose,
    const std::array<float,16>& object_to_world,
    const std::array<float,16>& world_to_clip, double texture_time,
    std::uint8_t fog, std::uint64_t presentation_serial,
    NativeGeometryView& view) {
    NativeWorldBounds bounds{};
    NativePreparedPartSubmission* record = nullptr;
    if (!retained_visual_part_bounds(sample, ordinal, bounds) ||
        !retained_visual_part_submission(sample, ordinal, bounds, {true,true,0},
            texture_time, fog, record) || !record || !record->source) return false;
    view = *record->source;
    view.world.pose = &pose; view.world.owner = &pose.owner;
    view.world.shape = pose.shape; view.world.object_to_world = object_to_world;
    view.world.world_to_clip = world_to_clip; view.object_to_clip = world_to_clip;
    view.contribution = {true,true,0};
    view.bounds_minimum = bounds.minimum; view.bounds_maximum = bounds.maximum;
    view.prepared_identity.presentation_serial = presentation_serial;
    view.u_shift = record->values.u_shift; view.v_shift = record->values.v_shift;
    return true;
}
} // namespace owned
