#pragma once
#include <array>
#include <cstdint>
namespace owned {
// Temporary F11 observations; these never participate in reuse decisions.
enum class ShadowInputDifference : unsigned {
    no_cached,world_to_clip,sun_direction,sun_enabled,class_flags,undetected,
    terrain_enabled,terrain_unavailable,terrain_identity,terrain_transform,terrain_origin,
    terrain_camera_position,terrain_lod_distances,terrain_maximum_lod,terrain_full_detail,
    terrain_visibility_fingerprint,terrain_visibility_domain,terrain_visibility_min_y,
    terrain_visibility_rows,terrain_object_to_clip,journal_full,terrain_dirty,
    active_values_unavailable,changed_visuals,changed_terrain,terrain_animation,
    source_revision,sink_unavailable,renderer_begin_failed,renderer_dirty,
    renderer_end_failed,source_changed_during_reuse,count
};
inline constexpr const char* shadow_input_difference_names[]={
    "no_cached","world_to_clip","sun_direction","sun_enabled","class_flags","undetected",
    "terrain_enabled","terrain_unavailable","terrain_identity","terrain_transform","terrain_origin",
    "terrain_camera_position","terrain_lod_distances","terrain_maximum_lod","terrain_full_detail",
    "terrain_visibility_fingerprint","terrain_visibility_domain","terrain_visibility_min_y",
    "terrain_visibility_rows","terrain_object_to_clip","journal_full","terrain_dirty",
    "active_values_unavailable","changed_visuals","changed_terrain","terrain_animation",
    "source_revision","sink_unavailable","renderer_begin_failed","renderer_dirty",
    "renderer_end_failed","source_changed_during_reuse"
};
struct ShadowInputDiagnostics {
    std::uint64_t epoch=0,attempts=0,only_world_to_clip_frames=0;
    double only_world_to_clip_max_abs_delta=0;
    std::array<std::uint64_t,static_cast<unsigned>(ShadowInputDifference::count)> counts{};
};
enum class ShadowRendererDifference : unsigned {
    publication_serial,epoch,resources_missing,enabled,invalid_map,world_to_light,
    camera_slice,bias,receiver_bounds,validity,far_scheduled,far_coverage_rejected,
    far_retained_refresh,far_static_redraw,scroll_rejected,map_scrolled,
    close_static_redraw,scroll_reject_axes,scroll_reject_texel,scroll_reject_fraction,
    scroll_reject_depth,scroll_reject_static,far_reject_axes,far_reject_texel,far_reject_fraction,
    far_reject_depth,far_reject_static,scroll_reject_summary,far_staggered,count
};
inline constexpr const char* shadow_renderer_difference_names[]={
    "publication_serial","epoch","resources_missing","enabled","invalid_map","world_to_light",
    "camera_slice","bias","receiver_bounds","validity","far_scheduled","far_coverage_rejected",
    "far_retained_refresh","far_static_redraw","scroll_rejected","map_scrolled",
    "close_static_redraw","scroll_reject_axes","scroll_reject_texel","scroll_reject_fraction",
    "scroll_reject_depth","scroll_reject_static","far_reject_axes","far_reject_texel","far_reject_fraction",
    "far_reject_depth","far_reject_static","scroll_reject_summary","far_staggered"
};
struct ShadowRendererDiagnostics {
    std::uint64_t epoch=0,begin_calls=0;
    std::array<std::uint64_t,static_cast<unsigned>(ShadowRendererDifference::count)> counts{};
};
static_assert(sizeof(shadow_input_difference_names)/sizeof(*shadow_input_difference_names)==static_cast<unsigned>(ShadowInputDifference::count));
static_assert(sizeof(shadow_renderer_difference_names)/sizeof(*shadow_renderer_difference_names)==static_cast<unsigned>(ShadowRendererDifference::count));
}